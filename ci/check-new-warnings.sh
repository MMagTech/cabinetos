#!/usr/bin/env bash
#
# No new compiler warnings in the files a pull request touches.
#
# CLAUDE.md's rule 6 asks every change for "no new warnings in the files you
# touched"; this is that rule, held by CI (#398, step 4). The tree already
# carries warnings, so -Werror would fail every change for old ones. Instead
# the warnings in the touched files are compared with the same files built
# at the pull request's base, and one that is not at the base is new.
#
# THE BASE IS COMPILED ONLY WHERE IT CAN MATTER. A warning in a .cpp comes
# from compiling that .cpp; one in a header from compiling any .cpp that
# includes it. So at the base this compiles the touched .cpp and .c files,
# and every one that includes a touched header, read from the .d files the
# head's build just wrote (-MMD). Those are the base's own includes too,
# unless an include line changed, and a file whose include line changed is
# itself touched, so the .cpp at the top of that chain is compiled anyway.
# A pull request that touches only a .cpp costs seconds; one that touches
# app.h, which nearly everything includes, costs about a whole compile.
# The Wii bridge is three files and is built whole, at the base and again
# here at the head, when any of it is touched.
#
# A warning is compared by its file and its text, not its line, so moving
# code is not a new warning. Two of the same warning in one file where the
# base had one is one new.
#
# REPORT ONLY UNTIL FAIL_FROM. A new warning fails the job from that day
# (UTC) on; before it, the same report and a warning annotation, and the
# job passes. CONTRIBUTING.md, "What CI checks", says the same.
#
# Usage: ci/check-new-warnings.sh <the head's build log>
#
# Run by build-frontend.yml after the Compile step, on pull requests only:
# HEAD is then GitHub's merge of the pull request into its base, so its first
# parent is the base and the checkout needs fetch-depth 2. It needs the
# cabinetos-builder container the job already has.

set -euo pipefail
export LC_ALL=C

FAIL_FROM=2026-10-25

HEAD_LOG="${1:?usage: ci/check-new-warnings.sh <the head build log>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SUMMARY="${GITHUB_STEP_SUMMARY:-/dev/null}"
WORK="${RUNNER_TEMP:-/tmp}/new-warnings"
rm -rf "$WORK"
mkdir -p "$WORK/base" "$WORK/head"
REPORT="$WORK/report.md"

# The end of every run: the report to the log and the run summary, then the
# exit the date allows.
finish() {
    local failed="$1" why="$2"
    printf '\n%s\n' "$why" >> "$REPORT"
    cat "$REPORT"
    cat "$REPORT" >> "$SUMMARY"
    if [[ "$failed" == yes ]]; then
        if [[ "$(date -u +%F)" < "$FAIL_FROM" ]]; then
            echo "::warning::$why (report only until $FAIL_FROM)"
            exit 0
        fi
        echo "::error::$why"
        exit 1
    fi
    exit 0
}

printf '## New warnings in the files this pull request touches\n\n' > "$REPORT"

if ! git rev-parse -q --verify 'HEAD^2' > /dev/null; then
    finish yes "HEAD is not a merge commit, so there is no base to compare with (is the checkout's fetch-depth 2?)."
fi
BASE="$(git rev-parse 'HEAD^1')"

# The files the frontend's Makefile and the Wii bridge's compile, as the pull
# request changed them (a rename is the old file gone and a new one).
git diff --name-only --no-renames "$BASE" HEAD -- \
    frontend/src frontend/third_party wiibridge > "$WORK/changed"
grep -E '\.(c|cc|cpp|h|hpp|inc)$' "$WORK/changed" > "$WORK/touched" || true

if [[ ! -s "$WORK/touched" ]]; then
    finish no "No file the frontend or the Wii bridge compiles changed."
fi

at_base() { git cat-file -e "$BASE:$1" 2> /dev/null; }

# The base's frontend objects to compile, as Makefile targets (relative to
# frontend/): each touched source still at the base, and each object whose
# .d names a touched header.
: > "$WORK/objects"
: > "$WORK/headers"
while read -r f; do
    case "$f" in
        frontend/*.cpp | frontend/*.c)
            if at_base "$f"; then
                rel="${f#frontend/}"
                echo "${rel%.*}.o" >> "$WORK/objects"
            fi
            ;;
        frontend/*) echo "${f#frontend/}" >> "$WORK/headers" ;;
    esac
done < "$WORK/touched"
if [[ -s "$WORK/headers" ]]; then
    find frontend/src frontend/third_party -name '*.d' | while read -r d; do
        if tr ' ' '\n' < "$d" | grep -xF -f "$WORK/headers" > /dev/null; then
            obj="${d#frontend/}"
            obj="${obj%.d}.o"
            src="${obj%.o}.cpp"
            if [[ "$obj" == third_party/* ]]; then src="${obj%.o}.c"; fi
            if at_base "frontend/$src"; then echo "$obj"; fi
        fi
    done >> "$WORK/objects"
fi
sort -u -o "$WORK/objects" "$WORK/objects"

git archive "$BASE" frontend/Makefile frontend/src frontend/third_party wiibridge \
    | tar -x -C "$WORK/base"
: > "$WORK/frontend-base.log"
: > "$WORK/bridge-base.log"
: > "$WORK/bridge-head.log"
SECONDS=0
if [[ -s "$WORK/objects" ]]; then
    mapfile -t objects < "$WORK/objects"
    # -O keeps each compile's lines together, so two compiles at once cannot
    # cut a warning in half.
    if ! podman run --rm -v "$WORK/base/frontend":/src:Z -w /src cabinetos-builder \
            make -j"$(nproc)" -O "${objects[@]}" > "$WORK/frontend-base.log" 2>&1; then
        cat "$WORK/frontend-base.log"
        finish yes "The base did not compile, so it cannot be compared."
    fi
fi
if grep -q '^wiibridge/' "$WORK/touched"; then
    git archive HEAD wiibridge | tar -x -C "$WORK/head"
    for side in base head; do
        if ! podman run --rm -v "$WORK/$side/wiibridge":/src:Z -w /src cabinetos-builder \
                make > "$WORK/bridge-$side.log" 2>&1; then
            cat "$WORK/bridge-$side.log"
            finish yes "The Wii bridge did not compile at the $side, so it cannot be compared."
        fi
    done
fi
seconds=$SECONDS

# One line per warning in a touched file, "<file> TAB <text> TAB <line>",
# the file from the repository's root. A header's warning repeats once per
# .cpp that includes it, so exact repeats go first.
warnings() {
    local log="$1" prefix="$2"
    { grep -E '^[^[:space:]:]+:[0-9]+:([0-9]+:)? warning: ' "$log" || true; } | sort -u \
        | awk -v prefix="$prefix" -v touched="$WORK/touched" '
            BEGIN { while ((getline t < touched) > 0) want[t] = 1 }
            {
                i = index($0, ": warning: ")
                split(substr($0, 1, i - 1), at, ":")
                file = prefix at[1]
                if (file in want) print file "\t" substr($0, i + 11) "\t" at[2]
            }'
}
# The head's frontend lines are in the job's log. The Wii bridge's lines
# there have no folder in their names, so they come from the build here.
{
    warnings "$WORK/frontend-base.log" frontend/
    warnings "$WORK/bridge-base.log" wiibridge/
} > "$WORK/base.w"
{
    warnings "$HEAD_LOG" frontend/
    warnings "$WORK/bridge-head.log" wiibridge/
} > "$WORK/head.w"

cut -f1,2 "$WORK/base.w" | sort > "$WORK/base.k"
cut -f1,2 "$WORK/head.w" | sort > "$WORK/head.k"
comm -23 "$WORK/head.k" "$WORK/base.k" > "$WORK/new.k"

{
    echo "Base \`$(git rev-parse --short "$BASE")\`: $(wc -l < "$WORK/touched") source" \
        "file(s) touched; $(wc -l < "$WORK/objects") frontend object(s) compiled again" \
        "at the base, in ${seconds}s."
    echo
    if [[ ! -s "$WORK/head.k" && ! -s "$WORK/base.k" ]]; then
        echo "No warnings in the touched files, at the base or now."
    else
        echo "| File | At the base | Now |"
        echo "|---|---|---|"
        awk -F'\t' 'FILENAME ~ /base\.k$/ { b[$1]++ } FILENAME ~ /head\.k$/ { h[$1]++ }
                    { f[$1] = 1 }
                    END { for (x in f) printf "| `%s` | %d | %d |\n", x, b[x], h[x] }' \
            "$WORK/base.k" "$WORK/head.k" | sort
    fi
} >> "$REPORT"

if [[ ! -s "$WORK/new.k" ]]; then
    finish no "No new warnings."
fi

# Each new warning with every line it is on now: one of them is the new one.
{
    echo
    echo '```'
    awk -F'\t' 'NR == FNR { n[$1 FS $2]++; next }
                ($1 FS $2) in n { k = $1 FS $2; at[k] = at[k] sep[k] $3; sep[k] = ", "; c[k]++ }
                END { for (k in n) { split(k, p, FS)
                      printf "%s: warning: %s (%d more than at the base; now on line%s %s)\n",
                             p[1], p[2], n[k], (c[k] > 1 ? "s" : ""), at[k] } }' \
        "$WORK/new.k" "$WORK/head.w" | sort
    echo '```'
} >> "$REPORT"
finish yes "$(wc -l < "$WORK/new.k" | tr -d ' ') new warning(s) in the files this pull request touches."
