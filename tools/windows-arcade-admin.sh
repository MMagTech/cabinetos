#!/usr/bin/env bash
# Windows Arcade, for a RomM admin (#762): makes what a CabinetOS console needs to play
# Windows-based arcade games in windowsarcadeloader. It runs on the admin's own computer (a Mac,
# Linux, or Windows through WSL) and only downloads, checks, packs and writes one small file:
# nothing Windows runs here.
#
#   windows-arcade-admin.sh files [--list FILE] [--work DIR] [--out FILE]
#     Once per household. Downloads every Windows file the loader's games need (the list is
#     cores/windows-arcade-files.txt, from this checkout or from GitHub), checks each one's
#     sha256 and packs them into windows-arcade-files.tar, which goes into RomM as firmware of
#     the TeknoParrot platform, where the games are. Run it again after a failure: what is
#     done is kept.
#
#   windows-arcade-admin.sh games [--yes] [--game ID] [--program PATH] [--replace]
#                                 [--dry-run] [--list FILE] [--loader DIR] PATH...
#     Once per game. PATH is a game's folder, a zip or 7z of it, or a folder of those. For each
#     game it picks the game from the loader's own list by name and the program to start, then
#     writes <gameid>.windowsloader at the game's top (inside the zip or 7z, which is not
#     unpacked). --yes accepts every first choice, for tests; --game with --program sets both
#     choices for a single PATH; --replace writes the file again for a game that has one;
#     --dry-run says what it would write and changes nothing; --loader reads the loader from a
#     folder (its tree at the pin) in place of GitHub.
#
# The loader's commit is in the list's header (`# windowsarcadeloader <commit>`), so the game
# list and the notes are read at the commit the console's image carries.

set -eu
set -o pipefail

LIST_URL=https://raw.githubusercontent.com/MMagTech/cabinetos/main/cores/windows-arcade-files.txt
LOADER_REPO=aderumier/windowsarcadeloader

script_dir=$(cd "$(dirname "$0")" && pwd)

say() { printf '%s\n' "$*"; }
die() { printf '%s\n' "$*" >&2; exit 1; }

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

size_of() { wc -c <"$1" | tr -d ' '; }

human() {
    awk -v b="$1" 'BEGIN {
        if (b >= 1073741824) printf "%.1f GB", b / 1073741824
        else if (b >= 1048576) printf "%.1f MB", b / 1048576
        else if (b >= 1024) printf "%.0f KB", b / 1024
        else printf "%d bytes", b }'
}

absolute() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s/%s\n' "$(pwd)" "$1" ;;
    esac
}

# The answer to a question; with --yes the first choice, an empty answer.
ask() {
    answer=""
    if [ "$yes" = 1 ]; then
        say "$1 (taking the first choice)"
        return 0
    fi
    printf '%s ' "$1"
    read -r answer || { say ""; die "Stopped: nothing more was written."; }
}

need() {
    command -v "$1" >/dev/null 2>&1 || die "This script needs $1, which is not on this computer: $2"
}

# ---------------------------------------------------------------------------------------------
# The list of Windows files.

# Sets list_file to a readable copy of the list.
find_list() {
    if [ -n "$list_arg" ]; then
        list_file=$list_arg
    elif [ -f "$script_dir/../cores/windows-arcade-files.txt" ]; then
        list_file=$script_dir/../cores/windows-arcade-files.txt
    else
        need curl "install curl, then run this again."
        mkdir -p "$cache_dir"
        list_file=$cache_dir/windows-arcade-files.txt
        curl -fsSL --retry 3 -o "$list_file.part" "$LIST_URL" ||
            die "Could not download the list of Windows files from $LIST_URL. Check the internet connection, then run this again."
        mv "$list_file.part" "$list_file"
    fi
    [ -r "$list_file" ] || die "Can't read the list of Windows files: $list_file"
    # The loader's commit, in the list's header: `# windowsarcadeloader <commit>`.
    loader_pin=$(awk '/^#/ { for (i = 2; i < NF; i++) if ($i == "windowsarcadeloader")
            for (j = i + 1; j <= i + 2 && j <= NF; j++) if ($j ~ /^[0-9a-f]+$/ && length($j) == 40) {
                print $j; exit } }' "$list_file")
    [ -n "$loader_pin" ] || die "The list $list_file does not name the loader's commit in its header."
    # Every other line: <verb> <cache folder>/<file name> <sha256> <url>, nothing outside its
    # cache folder, the same file never with two checksums.
    awk '
        /^#/ || /^[[:space:]]*$/ { next }
        NF != 4 || $3 !~ /^[0-9a-f]+$/ || length($3) != 64 || $4 !~ /^https:\/\// ||
        $2 !~ /^[A-Za-z0-9_.+-]+\/[A-Za-z0-9_.+-]+$/ || $2 ~ /(^|\/)\.\.?(\/|$)/ {
            print "Line " NR " of the list is not in the form the script reads: " $0; bad = 1; next }
        ($2 in sum) && sum[$2] != $3 { print "The list gives " $2 " two checksums."; bad = 1 }
        { sum[$2] = $3 }
        END { exit bad }' "$list_file" >&2 || die "The list of Windows files is damaged: $list_file"
}

# ---------------------------------------------------------------------------------------------
# Job 1: the Windows files.

# Downloads one file to $work/<path>, resumable; returns 1 when it is not there with its sha256.
fetch_one() {
    local path=$1 sum=$2 url=$3 dest part dropped tries
    dest=$work/$path
    part=$dest.part
    dropped=$work/$(basename "$path")
    if [ -f "$dest" ] && [ "$(sha256_of "$dest")" = "$sum" ]; then
        say "  Already here: $path"
        return 0
    fi
    rm -f "$dest"
    mkdir -p "$(dirname "$dest")"
    if [ -f "$dropped" ] && [ "$(sha256_of "$dropped")" = "$sum" ]; then
        mv "$dropped" "$dest"
        say "  Found it where it was dropped: $path"
        return 0
    fi
    say "  Downloading $path"
    tries=0
    while [ "$tries" -lt 2 ]; do
        tries=$((tries + 1))
        if curl -fL -C - --retry 3 --connect-timeout 30 $progress -o "$part" "$url"; then
            :
        elif [ -f "$part" ] && [ "$(sha256_of "$part")" = "$sum" ]; then
            :   # a finished part file: the server refuses to resume past its end
        else
            rm -f "$part"   # a server that cannot resume, or a broken part: start over once
            continue
        fi
        if [ "$(sha256_of "$part")" = "$sum" ]; then
            mv "$part" "$dest"
            return 0
        fi
        rm -f "$part"
        say "  The file at $url is not the one the list names (its checksum differs): if it fails again, tell CabinetOS at https://github.com/MMagTech/cabinetos/issues."
        return 1
    done
    return 1
}

job_files() {
    local started finished bytes failed path sum url n paths
    need curl "install curl, then run this again."
    need tar "install tar, then run this again."
    find_list
    work=$(absolute "${work_arg:-windows-arcade-work}")
    out=$(absolute "${out_arg:-windows-arcade-files.tar}")
    mkdir -p "$work"
    progress=-sS
    [ -t 1 ] && progress=--progress-bar
    # One line per file, in the list's order, a file several verbs share once.
    awk '!/^#/ && NF == 4 && !seen[$2]++ { print $2, $3, $4 }' "$list_file" >"$work/.files"
    n=$(wc -l <"$work/.files" | tr -d ' ')
    say "Getting the $n Windows files the loader's games need, into $work."
    started=$(date +%s)
    failed=""
    while read -r path sum url; do
        fetch_one "$path" "$sum" "$url" </dev/null || failed="$failed$path $url
"
    done <"$work/.files"
    finished=$(date +%s)
    if [ -n "$failed" ]; then
        say ""
        say "These files could not be downloaded. Download each one yourself from its address, put it in"
        say "the folder shown, then run this again:"
        printf '%s' "$failed" | while read -r path url; do
            say "  $url"
            say "    into $work/$(dirname "$path")"
        done
        exit 1
    fi
    bytes=0
    paths=""
    while read -r path sum url; do
        bytes=$((bytes + $(size_of "$work/$path")))
        paths="$paths$path
"
    done <"$work/.files"
    awk '!/^#/ && NF == 4' "$list_file" >"$work/MANIFEST"
    say "Packing them into $out."
    # COPYFILE_DISABLE: a Mac's tar adds no ._ files of its own.
    printf 'MANIFEST\n%s' "$paths" | (cd "$work" && COPYFILE_DISABLE=1 tar -cf "$out.part" -T -) ||
        { rm -f "$out.part"; die "Could not write $out: is there room for it?"; }
    mv "$out.part" "$out"
    say ""
    say "Done: $n files, $(human "$bytes"), downloaded or checked in $((finished - started)) seconds."
    say "Upload $(basename "$out") ($(human "$(size_of "$out")")) in RomM as a firmware file of the TeknoParrot platform, where your Windows arcade games are."
    say "The console builds its Windows setup from it the first time a Windows game starts."
}

# ---------------------------------------------------------------------------------------------
# The loader's own lists, read at its pin.

# Sets loader_dir to the loader's tree at the pin and writes games.tsv and notes.tsv there.
load_loader() {
    local tree
    if [ -n "$loader_arg" ]; then
        loader_dir=$loader_arg
        [ -f "$loader_dir/README.md" ] || die "No README.md in $loader_dir: --loader takes the loader's tree."
        tables_dir=$cache_dir/tables-local
    else
        find_list
        loader_dir=$cache_dir/loader-$loader_pin
        tables_dir=$loader_dir
        if [ ! -f "$loader_dir/.complete" ] || [ ! -d "$loader_dir/systemprofiles" ]; then
            need curl "install curl, then run this again."
            need tar "install tar, then run this again."
            say "Reading the loader's list of games from GitHub (about 3 MB, once)."
            rm -rf "$loader_dir"
            mkdir -p "$loader_dir"
            tree=windowsarcadeloader-$loader_pin
            curl -fsSL --retry 3 "https://codeload.github.com/$LOADER_REPO/tar.gz/$loader_pin" |
                tar -xzf - -C "$loader_dir" --strip-components=1 \
                    "$tree/README.md" "$tree/docs/IMPLEMENTATION.md" "$tree/systemprofiles" ||
                die "Could not download the loader from GitHub. Check the internet connection, then run this again."
            : >"$loader_dir/.complete"
        fi
    fi
    mkdir -p "$tables_dir"
    # README's table: | `id` | name | system | status | ...
    awk -F' [|] ' '
        /GAMELIST BEGIN/ { on = 1; next }
        /GAMELIST END/ { on = 0 }
        on && /^[|] `[a-z0-9-]+` [|]/ {
            id = $1; sub(/^[|] `/, "", id); sub(/`$/, "", id)
            print id "\t" $2 "\t" $3 "\t" $4 }' "$loader_dir/README.md" >"$tables_dir/games.tsv"
    [ -s "$tables_dir/games.tsv" ] || die "The loader's README has no game table: the script must be updated."
    # IMPLEMENTATION.md's status table: | `id` | name | system | status | notes |
    awk '
        /^[|] `[a-z0-9-]+` [|]/ {
            line = $0; id = line; sub(/^[|] `/, "", id); sub(/`.*/, "", id)
            for (i = 0; i < 4; i++) sub(/^[|][^|]*/, "", line)
            sub(/^[|] */, "", line); sub(/ *[|] *$/, "", line)
            print id "\t" line }' "$loader_dir/docs/IMPLEMENTATION.md" >"$tables_dir/notes.tsv"
}

game_field() { awk -F'\t' -v id="$1" -v f="$2" '$1 == id { print $f; exit }' "$tables_dir/games.tsv"; }
game_notes() { awk -F'\t' -v id="$1" '$1 == id { print $2; exit }' "$tables_dir/notes.tsv"; }

# The paths in the game a game's profile names (`"{dump}/<path>"`, its `files:`), from the
# game's top, one a line.
profile_paths() {
    local profile
    profile=$loader_dir/systemprofiles/$(game_field "$1" 3)/$1.yaml
    [ -f "$profile" ] || return 0
    awk '/^[[:space:]]*#/ { next }
        { s = $0
        while ((i = index(s, "{dump}/")) > 0) {
            q = i > 1 ? substr(s, i - 1, 1) : ""; s = substr(s, i + 7); p = s
            if (q == "\"" || q == "\047") { j = index(p, q); if (j) p = substr(p, 1, j - 1) }
            else sub(/[[:space:]].*$/, "", p)
            if (p != "") print p } }' "$profile"
}

# The best few games for a name: lines "<id>\t<name>\t<system>\t<status>", best first.
match_games() {
    awk -F'\t' -v q="$1" '
        function norm(s) {
            s = tolower(s); gsub(/\047/, "", s); gsub(/&/, " and ", s); gsub(/[^a-z0-9]+/, " ", s)
            return s }
        function roman(t) { return t == "ii" ? "2" : t == "iii" ? "3" : t == "iv" ? "4" : t }
        BEGIN {
            split("the of a an and tp usa jpn eur exp pc ver version", sw, " ")
            for (i in sw) stop[sw[i]] = 1
            n = split(norm(q), qt, " ")
            for (i = 1; i <= n; i++) { w = roman(qt[i]); if (!(w in stop)) want_t[w] = 1 } }
        {
            delete g; delete nm
            n = split(norm($2), t, " ")
            for (i = 1; i <= n; i++) { w = roman(t[i]); if (!(w in stop)) { g[w] = 1; nm[w] = 1 } }
            n = split($1, t, "-")
            for (i = 1; i <= n; i++) g[roman(t[i])] = 1
            s = 0
            for (w in want_t) if (w in g) s += (length(w) >= 3 ? 2 : 1)
            if (s == 0) next
            for (w in nm) if (!(w in want_t)) s -= 0.5
            printf "%08.2f\t%s\n", s + 1000, $0 }' "$tables_dir/games.tsv" |
        sort -t "$(printf '\t')" -k1,1r -k2,2 | awk -v n="${2:-5}" 'NR <= n' | cut -f2-
}

# Program names the loader's notes give a game: names ending .exe or .dll (with any suffix
# after it, as gs2.exe.ori), one a line, in the notes' order.
notes_programs() {
    game_notes "$1" | awk '{
        s = $0
        while (match(s, /`[^`]+`/)) {
            t = substr(s, RSTART + 1, RLENGTH - 2); s = substr(s, RSTART + RLENGTH)
            gsub(/\\/, "/", t); sub(/^\.\//, "", t)
            l = tolower(t)
            if (l ~ /\.(exe|dll)(\.[a-z0-9_]+)?$/ && l !~ /\.(manifest|config|ini|txt|log|xml)$/ && t !~ /^\//)
                print t } }'
}

# ---------------------------------------------------------------------------------------------
# A game's files, whatever holds them: kind (folder, zip, 7z), its top inside, its file list.

sevenzip=""
find_7z() {
    local c
    for c in 7zz 7z 7za; do
        if command -v "$c" >/dev/null 2>&1; then sevenzip=$c; return 0; fi
    done
    return 1
}

# Writes "<size>\t<path from the archive's root>" for every file of an archive or folder.
list_files() {
    case "$kind" in
        folder)
            (cd "$src" && find . -type f ! -name '._*' ! -name '.DS_Store' | sed 's|^\./||') |
                while IFS= read -r f; do printf '0\t%s\n' "$f"; done ;;
        zip)
            unzip -l "$src" | awk '
                /^ *-+/ { dash++; next }
                dash == 1 {
                    size = $1; line = $0
                    sub(/^ *[0-9]+ +[0-9\/.-]+ +[0-9:]+ +/, "", line)
                    if (line !~ /\/$/) print size "\t" line }' ;;
        7z)
            "$sevenzip" l -slt -ba "$src" | awk '
                /^Path = / { p = substr($0, 8); dir = 0; size = 0 }
                /^Size = / { size = substr($0, 8) }
                /^Attributes = / && /D/ { dir = 1 }
                /^Folder = \+/ { dir = 1 }
                /^$/ { if (p != "" && !dir) print size "\t" p; p = "" }
                END { if (p != "" && !dir) print size "\t" p }' ;;
    esac | awk -F'\t' '$2 !~ /(^|\/)__MACOSX\// && $2 !~ /(^|\/)\._/ && $2 !~ /(^|\/)\.DS_Store$/'
}

# Sets top: the folder inside an archive the game's files sit in ("" for its root, else
# "<folder>/"): the one folder everything is in, when there is only one (a zip of the game's
# folder). settle_top then corrects it from the game's notes.
find_top() {
    top=""
    [ "$kind" = folder ] && return 0
    top=$(awk -F'\t' '
        { p = $2; i = index(p, "/"); if (i == 0) { root = 1; exit } d[substr(p, 1, i)] = 1 }
        END { if (root) exit; n = 0; for (k in d) { n++; one = k } if (n == 1) print one }' "$tmp/all")
}

# Paths the loader gives from the game's top, with their folders: the notes' program names
# (Daytona New Season Edition's Sega/Daytona/Daytona.exe) and the profile's files (Transformers
# Shadows Rising's Sega/shell/Game.ini). A zip of such a game's folder contents holds only
# Sega/, and its top is its root, not Sega/.
settle_top() {
    local one=$top
    { notes_programs "$1"; profile_paths "$1"; } | awk 'index($0, "/")' >"$tmp/toppaths"
    [ -n "$one" ] || return 0
    awk -F'\t' -v one="$one" -v pathsfile="$tmp/toppaths" '
        BEGIN { while ((getline l < pathsfile) > 0) named[tolower(l)] = 1 }
        { p = tolower($2)
          if (p in named) { root = 1 }
          else if (substr(p, 1, length(one)) == tolower(one) && (substr(p, length(one) + 1) in named)) inside = 1 }
        END { exit !(root && !inside) }' "$tmp/all" && top=""
    return 0
}

# Says which files the game's profile needs that this copy of the game does not have: with one
# missing, the loader stops before the game starts.
check_profile_files() {
    profile_paths "$1" >"$tmp/needed"
    [ -s "$tmp/needed" ] || return 0
    awk -F'\t' -v top="$top" -v needfile="$tmp/needed" '
        BEGIN { while ((getline l < needfile) > 0) need[++n] = l }
        { have[tolower($2)] = 1 }
        END { for (i = 1; i <= n; i++) if (!(tolower(top need[i]) in have)) print need[i] }' \
        "$tmp/all" >"$tmp/missing"
    while IFS= read -r f; do
        say "  The loader needs $f in the game, and this copy does not have it: the game will not start without it."
    done <"$tmp/missing"
}

# ---------------------------------------------------------------------------------------------
# Job 2: the <gameid>.windowsloader file.

# Picks the program: lines "<size>\t<path from the game's top>\t<why>", the pick first, then
# the rest by size. The pick: a renamed program the loader's notes name (gs2.exe.ori) when it is
# there; else the biggest .exe, nearest the top on a tie, then the shortest name (a changed copy
# beside the program adds to its name: Tetris The Grand Master 3's game-gloader.exe beside
# game.exe); else, with no .exe at all, the first file the notes name that is there (an AVS
# game's DLL). The notes also name programs that do not start the game (a test mode, a DLL the
# payload answers), so a plain .exe or .dll they name is listed and marked, not picked.
pick_programs() {
    local id=$1
    notes_programs "$id" >"$tmp/named"
    awk -F'\t' -v top="$top" -v namedfile="$tmp/named" '
        BEGIN { while ((getline l < namedfile) > 0) named[++nn] = tolower(l) }
        {
            p = $2
            if (top != "") { if (substr(p, 1, length(top)) != top) next; p = substr(p, length(top) + 1) }
            if (p ~ /\.windowsloader$/) next
            lp = tolower(p); why = ""
            for (i = 1; i <= nn; i++)
                if (lp == named[i] || substr(lp, length(lp) - length(named[i])) == "/" named[i]) {
                    why = i; break }
            if (why == "" && lp !~ /\.exe$/) next
            print $1 "\t" p "\t" why }' "$tmp/all" >"$tmp/cands"
    if [ "$kind" = folder ]; then
        while IFS="$(printf '\t')" read -r _ p why; do
            printf '%s\t%s\t%s\n' "$(size_of "$src/$p")" "$p" "$why"
        done <"$tmp/cands" >"$tmp/cands.sized"
        mv "$tmp/cands.sized" "$tmp/cands"
    fi
    # Sort key: the pick first, then by size, nearest the top, the shortest name, by name.
    awk -F'\t' '
        { size[NR] = $1; path[NR] = $2; why[NR] = $3; lp = tolower($2)
          if ($3 != "" && lp ~ /\.exe\.[a-z0-9_]+$/ && (ren == "" || $3 < why[ren])) ren = NR
          if (lp ~ /\.exe$/) exe = 1
          if ($3 != "" && (first == "" || $3 < why[first])) first = NR }
        END {
            pick = ren
            if (pick == "" && !exe) pick = first
            for (i = 1; i <= NR; i++) {
                d = gsub(/\//, "/", path[i]); base = path[i]; sub(/^.*\//, "", base)
                printf "%d\t%012d\t%04d\t%04d\t%s\t%s\t%s\n", (i == pick ? 0 : 1), 999999999999 - size[i], d,
                    length(base), path[i], size[i], (why[i] == "" ? "" : "named in the loader\047s notes") } }' \
        "$tmp/cands" | sort | cut -f5,6,7 | awk -F'\t' '{ print $2 "\t" $1 "\t" $3 }'
}

# Writes the file into the folder, zip or 7z.
write_file() {
    local id=$1 rel=$2 file started stage
    file=$id.windowsloader
    case "$kind" in
        folder)
            printf '%s\n' "$rel" >"$src/$file" ;;
        zip|7z)
            stage=$tmp/stage
            rm -rf "$stage"
            mkdir -p "$stage/$top"
            printf '%s\n' "$rel" >"$stage/$top$file"
            started=$(date +%s)
            if [ "$kind" = zip ]; then
                # -g adds to the zip where it is, so the zip is not copied whole.
                (cd "$stage" && zip -q -g -X "$src" "$top$file") ||
                    { say "  Could not add the file to the zip."; return 1; }
            else
                (cd "$stage" && "$sevenzip" a -bso0 -bsp0 "$src" "$top$file") ||
                    { say "  Could not add the file to the 7z."; return 1; }
            fi
            say "  Added in $(($(date +%s) - started)) seconds." ;;
    esac
}

# Takes out the old file (every one at the top or one folder down), in one go: Info-ZIP and
# 7-Zip copy the whole archive to take a file out of it.
remove_old() {
    local f names=()
    while IFS= read -r f; do names+=("$f"); done <"$tmp/old"
    case "$kind" in
        folder) for f in "${names[@]}"; do rm -f "$src/$f"; done ;;
        zip)
            say "  Taking the old file out copies the whole zip once, so it takes a while and needs room for a copy."
            zip -q -nw -d "$src" "${names[@]}" ;;
        7z)
            say "  Taking the old file out copies the whole 7z once, so it takes a while and needs room for a copy."
            "$sevenzip" d -bso0 -bsp0 "$src" "${names[@]}" ;;
    esac
}

one_game() {
    local label id gname status choice n i line rel query
    src=$1
    label=$(basename "$src")
    case "$src" in
        *.zip|*.ZIP|*.Zip) kind=zip; need unzip "install unzip, then run this again."; need zip "install zip, then run this again." ;;
        *.7z|*.7Z) kind=7z; find_7z || { say "$label: a 7z needs 7-Zip (7zz or 7z), which is not on this computer, so it is left as it is."; return 0; } ;;
        *) kind=folder ;;
    esac
    say ""
    say "== $label"
    list_files >"$tmp/all"
    find_top
    # A game that has its file already, at the top or one folder down, where the console looks.
    awk -F'\t' '$2 !~ /\/.*\// && tolower($2) ~ /\.windowsloader$/ { print $2 }' "$tmp/all" >"$tmp/old"
    if [ -s "$tmp/old" ] && [ "$replace" = 0 ]; then
        say "  It has its file already ($(head -n 1 "$tmp/old")), so it is left as it is; --replace writes it again."
        return 0
    fi

    # The game.
    if [ -n "$game_arg" ]; then
        id=$game_arg
        [ -n "$(game_field "$id" 1)" ] || die "The loader has no game with the id $id."
    else
        query=${label%.*}
        [ "$kind" = folder ] && query=$label
        while :; do
            match_games "$query" 5 >"$tmp/match"
            if [ ! -s "$tmp/match" ]; then
                say "  No game in the loader's list matches \"$query\"."
                [ "$yes" = 1 ] && { say "  Left as it is."; return 0; }
                ask "  Type words from the game's name, or press Enter to leave it."
                [ -z "$answer" ] && { say "  Left as it is."; return 0; }
                query=$answer
                continue
            fi
            say "  Which game is it?"
            i=0
            while IFS="$(printf '\t')" read -r id gname _ status; do
                i=$((i + 1))
                say "   $i  $gname  ($id)"
            done <"$tmp/match"
            ask "  Press Enter for 1, type another number, or type other words to search again."
            case "$answer" in
                "") choice=1 ;;
                *[!0-9]*) query=$answer; continue ;;
                *) choice=$answer ;;
            esac
            if [ "$choice" -ge 1 ] && [ "$choice" -le "$i" ]; then break; fi
            say "  There is no game $choice."
        done
        line=$(sed -n "${choice}p" "$tmp/match")
        id=$(printf '%s\n' "$line" | cut -f1)
    fi
    gname=$(game_field "$id" 2)
    status=$(game_field "$id" 4)
    say "  Game: $gname ($id). In the loader's list: $status"
    settle_top "$id"
    check_profile_files "$id"

    # The program.
    if [ -n "$program_arg" ]; then
        rel=$program_arg
        awk -F'\t' -v p="$top$rel" '$2 == p { f = 1 } END { exit !f }' "$tmp/all" ||
            die "No file $rel in $label."
    else
        pick_programs "$id" >"$tmp/progs"
        n=$(wc -l <"$tmp/progs" | tr -d ' ')
        if [ "$n" = 0 ]; then
            say "  It holds no program (.exe), so it is left as it is."
            return 0
        fi
        say "  Which program starts the game?"
        i=0
        while IFS="$(printf '\t')" read -r size p why; do
            i=$((i + 1))
            [ "$i" -le 20 ] || break
            say "   $i  $p  $(human "$size")${why:+  ($why)}"
        done <"$tmp/progs"
        [ "$n" -gt 20 ] && say "   (and $((n - 20)) smaller ones)"
        while :; do
            ask "  Press Enter for 1, or type another number."
            choice=${answer:-1}
            case "$choice" in *[!0-9]*) choice=0 ;; esac
            if [ "$choice" -ge 1 ] && [ "$choice" -le "$n" ]; then break; fi
            say "  There is no program $answer."
        done
        rel=$(sed -n "${choice}p" "$tmp/progs" | cut -f2)
    fi

    if [ "$dry_run" = 1 ]; then
        say "  Would write $id.windowsloader: $rel"
        return 0
    fi
    [ -s "$tmp/old" ] && remove_old
    write_file "$id" "$rel"
    say "  Wrote $id.windowsloader: $rel"
}

# A folder is one game when it has its file, a program or a DLL at its top, or only one folder;
# else each zip, 7z and folder in it is a game.
is_one_game() {
    local d=$1 f dirs=0
    for f in "$d"/*; do
        [ -e "$f" ] || continue
        case "$f" in
            *.windowsloader|*.WINDOWSLOADER|*.exe|*.EXE|*.Exe|*.dll|*.DLL|*.Dll) return 0 ;;
            *.zip|*.ZIP|*.7z|*.7Z) return 1 ;;
        esac
        [ -d "$f" ] && dirs=$((dirs + 1))
    done
    [ "$dirs" -le 1 ]
}

job_games() {
    local p f
    [ "$#" -gt 0 ] || die "Name a game's folder, zip or 7z, or a folder of them."
    if [ -n "$game_arg$program_arg" ] && [ "$#" -gt 1 ]; then
        die "--game and --program are for one game at a time."
    fi
    [ "$yes" = 1 ] || [ -t 0 ] || die "Run it in a terminal to answer its questions, or add --yes to take every first choice."
    load_loader
    for p in "$@"; do
        p=$(absolute "$p")
        p=${p%/}
        if [ -d "$p" ] && ! is_one_game "$p"; then
            say ""
            say "$(basename "$p") holds several games: each is set up in turn."
            for f in "$p"/*; do
                case "$f" in *.zip|*.ZIP|*.7z|*.7Z) one_game "$f" ;; *) [ -d "$f" ] && one_game "$f" ;; esac
            done
        elif [ -e "$p" ]; then
            one_game "$p"
        else
            die "There is nothing at $p."
        fi
    done
}

# ---------------------------------------------------------------------------------------------

job=${1:-}
[ "$#" -gt 0 ] && shift
list_arg="" work_arg="" out_arg="" loader_arg="" game_arg="" program_arg=""
yes=0 replace=0 dry_run=0
paths=()
while [ "$#" -gt 0 ]; do
    case "$1" in
        --list) list_arg=$(absolute "$2"); shift 2 ;;
        --work) work_arg=$2; shift 2 ;;
        --out) out_arg=$2; shift 2 ;;
        --loader) loader_arg=$(absolute "$2"); shift 2 ;;
        --game) game_arg=$2; shift 2 ;;
        --program) program_arg=$2; shift 2 ;;
        --yes) yes=1; shift ;;
        --replace) replace=1; shift ;;
        --dry-run) dry_run=1; shift ;;
        --*) die "Unknown option $1." ;;
        *) paths+=("$1"); shift ;;
    esac
done
cache_dir=${XDG_CACHE_HOME:-$HOME/.cache}/cabinetos-windows-arcade
tmp=$(mktemp -d "${TMPDIR:-/tmp}/windows-arcade.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

case "$job" in
    files) job_files ;;
    games) job_games ${paths[@]+"${paths[@]}"} ;;
    *)
        say "Windows Arcade, for a RomM admin. Two jobs:"
        say "  $(basename "$0") files                      once: the Windows files for RomM's firmware"
        say "  $(basename "$0") games <folder, zip or 7z>  once per game: its .windowsloader file"
        exit 2 ;;
esac
