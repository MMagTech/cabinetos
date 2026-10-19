#!/usr/bin/env bash
#
# Does a gamescope still take everything we pass it? (#227)
#
#   ci/check-gamescope-flags.sh <gamescope --help output> [script...]
#
# Prints each flag or backend our scripts use that the help text no longer
# lists, after the script's name, one per line, and exits 1 if there is any; 0
# when all are there. It cannot tell, and says why on stderr, with 2 when the
# help text is missing or is not gamescope's, and 3 when it read fewer flags
# or backends from a script than that script passes today (the reading broke).
# The scripts default to the two that start gamescope: cabinetos-session, and
# cabinetos-steam-download (Steam's first download).
#
# WHY. gamescope comes from the Bazzite base, not from us. A renamed or dropped
# flag makes gamescope refuse to start: the session then falls to a lower rung
# (no 4K, no VRR, software drawing) with nothing said until somebody looks at a
# television, and Install Steam fails at once. ci/check-base-update.sh runs this
# on every new base, so the base-update pull request says so first. Same idea
# as the emulator option check built for #63.
#
# THE FLAGS ARE READ FROM THE SCRIPTS, not listed here, so a flag added to one
# is checked without anyone remembering this file: every `--flag` (or
# `--flag=value`) after a `gamescope` that starts it (`exec gamescope \` or
# `gamescope --...`) up to the bare `--` that ends gamescope's own options,
# or a line that does not go on with `\`; and every `--flag` assigned to a
# variable or bash array expanded there (`${COMPOSED[@]...}`, `"${backend}"`,
# `$EXTRA`), followed through the variables those assignments expand in turn,
# on one line or several. Other programs' flags inside a `$(...)` in an
# assignment are not gamescope's and are left out. Plus each backend named
# after `--backend` or passed to run_gamescope. Short flags (`-W`) are not
# read, so the scripts use the long ones. A variable expanded on the gamescope
# line that its script never assigns (one from the environment) is named on
# stderr: what it holds cannot be read. Steam's session is
# gamescope-session-plus's own, from the same base, and not ours to check.

set -euo pipefail

HELP="${1:?usage: check-gamescope-flags.sh <help text> [script...]}"
shift
SCRIPTS=("$@")
if (( ${#SCRIPTS[@]} == 0 )); then
    SCRIPTS=(system_files/usr/bin/cabinetos-session
             system_files/usr/libexec/cabinetos-steam-download)
fi

if [[ ! -s "${HELP}" ]] || ! grep -q -- '--backend' "${HELP}"; then
    echo "could not read gamescope's --help" >&2
    exit 2
fi

# How many flags and backends each script passes gamescope today: the floor
# the reading below is held to. Reading fewer means the reading broke (a way
# of passing them it cannot follow), not that gamescope is fine (#487). A
# change that takes a flag or backend out of one of these scripts lowers its
# number here in the same commit; one that adds a flag may raise it. A script
# not named here has always passed at least --backend, --output-width and
# --output-height, and one backend.
floor() {
    case "$1" in
        # --backend --output-width --output-height --adaptive-sync
        # --force-composition --ready-fd; drm and headless.
        cabinetos-session) echo 6 2 ;;
        # --backend --output-width --output-height; headless.
        cabinetos-steam-download) echo 3 1 ;;
        *) echo 3 1 ;;
    esac
}

# What one script passes gamescope, one item a line: "flag --x" for each long
# flag, "via NAME" for each variable a flag came through, and "unset NAME" for
# a variable expanded on a gamescope line that the script never assigns.
read_script() {
    awk '
        # Long flags written out in one word of a gamescope line, quotes
        # aside, and the variables the word expands.
        function word(s,   t) {
            t = s
            gsub(/["\047]/, "", t)
            sub(/=.*/, "", t)
            if (t ~ /^--[a-z][a-z0-9-]+$/) found(t, "")
            refs(s, 1)
        }
        function found(f, from) {
            if (!(f in flag)) { flag[f] = 1; print "flag " f }
            if (from != "" && !(from in via)) { via[from] = 1; print "via " from }
        }
        # Every $NAME and ${NAME...} in s, to be followed to its assignments.
        function refs(s, direct,   t, name) {
            t = s
            while (match(t, /\$\{?[A-Za-z_][A-Za-z0-9_]*/)) {
                name = substr(t, RSTART, RLENGTH)
                sub(/^\$\{?/, "", name)
                if (!(name in want)) { want[name] = 1; queue[++queued] = name }
                if (direct) onLine[name] = 1
                t = substr(t, RSTART + RLENGTH)
            }
        }
        # The value assigned at column p of line ln: a quoted string, an
        # array `(...)` (which can run over several lines), or a bare word
        # (with any `$(...)` in it whole).
        function value(ln, p,   s, c, q, depth, out, inq) {
            s = line[ln]
            c = substr(s, p, 1)
            if (c == "\"" || c == "\047") {
                q = index(substr(s, p + 1), c)
                return q ? substr(s, p + 1, q - 1) : substr(s, p + 1)
            }
            depth = 0; out = ""; inq = ""
            while (1) {
                if (p > length(s)) {
                    if (depth == 0 || ln >= lines) return out
                    ln++; s = line[ln]; p = 1; out = out " "
                    continue
                }
                c = substr(s, p, 1)
                if (inq != "") {
                    if (c == inq) inq = ""
                } else if (c == "#" && (p == 1 || substr(s, p - 1, 1) ~ /[[:space:]]/)) {
                    # A comment, inside an array over several lines.
                    p = length(s) + 1
                    continue
                } else if (c == "\"" || c == "\047") {
                    inq = c
                } else if (c == "(") {
                    depth++
                } else if (c == ")") {
                    if (depth == 0) return out
                    depth--
                } else if (depth == 0 && c ~ /[[:space:];&|]/) {
                    return out
                }
                out = out c; p++
            }
        }
        # Every assignment to name (`name=`, `name+=`, `local name=`), its
        # long flags taken and the variables it expands followed.
        function assigned(name,   ln, s, off, at, len, v, any, t) {
            any = 0
            for (ln = 1; ln <= lines; ln++) {
                s = line[ln]; off = 0
                while (match(s, "(^|[^A-Za-z0-9_])" name "\\+?=")) {
                    # Kept: the matching below moves RSTART and RLENGTH.
                    at = RSTART; len = RLENGTH
                    any = 1
                    v = value(ln, off + at + len)
                    while (gsub(/\$\([^()]*\)/, "", v)) {}
                    t = " " v
                    while (match(t, /[^A-Za-z0-9_${}-]--[a-z][a-z0-9-]+/)) {
                        found(substr(t, RSTART + 1, RLENGTH - 1), name)
                        t = substr(t, RSTART + RLENGTH)
                    }
                    refs(v, 0)
                    off += at + len - 1
                    s = substr(s, at + len)
                }
            }
            return any
        }

        # Whole-line comments count as empty lines, for both readings.
        /^[[:space:]]*#/ { line[++lines] = ""; next }
        { line[++lines] = $0 }

        {
            n = split($0, w, /[[:space:]]+/)
            i = 1
            if (!inside) {
                for (; i < n; i++) {
                    if (w[i] == "gamescope" && (w[i + 1] == "\\" || w[i + 1] ~ /^--/)) {
                        inside = 1
                        i++
                        break
                    }
                }
                if (!inside) next
            }
            for (; i <= n; i++) {
                if (w[i] == "--") { inside = 0; break }
                if (w[i] ~ /^#/) break
                word(w[i])
            }
            if ($0 !~ /\\[[:space:]]*$/) inside = 0
        }

        END {
            for (k = 1; k <= queued; k++)
                if (!assigned(queue[k]) && (queue[k] in onLine)) print "unset " queue[k]
        }' "$1"
}

missing=0
for script in "${SCRIPTS[@]}"; do
    name="${script##*/}"

    mapfile -t items < <(read_script "${script}")
    flags=(); via=(); unset_vars=()
    for item in ${items[@]+"${items[@]}"}; do
        case "${item}" in
            flag\ *) flags+=("${item#flag }") ;;
            via\ *) via+=("${item#via }") ;;
            unset\ *) unset_vars+=("${item#unset }") ;;
        esac
    done
    mapfile -t flags < <(printf '%s\n' ${flags[@]+"${flags[@]}"} | grep . | sort -u)

    mapfile -t backends < <(grep -v '^[[:space:]]*#' "${script}" \
        | grep -oE -- '(--backend|run_gamescope)[[:space:]]+"?[a-z]+"?' \
        | awk '{ gsub(/"/, "", $2); print $2 }' | sort -u)

    # The floor, asserted (above): fewer than today means the reading broke.
    read -r min_flags min_backends < <(floor "${name}")
    if (( ${#flags[@]} < min_flags )) || (( ${#backends[@]} < min_backends )); then
        echo "read only ${#flags[@]} flags (${flags[*]-}) and ${#backends[@]} backends (${backends[*]-}) from ${script}, which passes at least ${min_flags} and ${min_backends}" >&2
        exit 3
    fi
    for v in ${unset_vars[@]+"${unset_vars[@]}"}; do
        echo "${name}: \$${v} reaches gamescope and is never assigned here: what it holds is not checked" >&2
    done

    for f in "${flags[@]}"; do
        # Listed as "  --flag" or "  -X, --flag", followed by spaces or the end.
        if ! grep -qE -- "(^|[[:space:],])${f}([[:space:]=]|$)" "${HELP}"; then
            echo "${name}: ${f}"
            missing=1
        fi
    done
    for b in "${backends[@]}"; do
        # Listed under --backend as "    drm => ...".
        if ! grep -qE "^[[:space:]]+${b}[[:space:]]+=>" "${HELP}"; then
            echo "${name}: --backend ${b}"
            missing=1
        fi
    done

    echo "${name}: checked ${#flags[@]} flags (${flags[*]}${via[*]:+; through ${via[*]}}) and ${#backends[@]} backends (${backends[*]})" >&2
done

exit "${missing}"
