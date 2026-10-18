#!/usr/bin/env bash
#
# The code nothing calls, as the linker sees it: a report in the run's
# summary, never a failure (#398, step 4).
#
# The frontend is compiled again into a scratch copy with every function and
# every variable in a section of its own (-ffunction-sections
# -fdata-sections) and linked with --gc-sections --print-gc-sections, which
# names each section nothing reachable from main refers to. AT -O0, because
# at -O2 a function used only where it was inlined has an unused copy of
# its own, and would be listed though it is used. The flags are the
# Makefile's own with -O2 made -O0, so they cannot drift from the real build.
# The binary that ships is not this one and is not touched: this builds under
# $RUNNER_TEMP, after the upload.
#
# What the list means: a function or variable listed is not reached from
# main by any call, pointer or table, so it can go, or something that should
# reach it does not. A function used only by another listed one is listed
# too. Not listed: the C++ library's own templates (std::, __gnu_cxx::),
# vendored rcheevos (counted only), and static functions nobody calls, which
# the compiler drops before the linker sees them (-Wunused-function names
# those in the Warnings step).
#
# Usage: ci/report-unused-code.sh
#
# Run by build-frontend.yml at the end of the Compile job, on pull requests.
# It needs the cabinetos-builder container the job already has.

set -uo pipefail
export LC_ALL=C

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 0

SUMMARY="${GITHUB_STEP_SUMMARY:-/dev/null}"
WORK="${RUNNER_TEMP:-/tmp}/unused-code"
rm -rf "$WORK"
mkdir -p "$WORK"
REPORT="$WORK/report.md"
printf '## Unused code (a report, never a failure)\n\n' > "$REPORT"

# Every way out says why in the summary and passes.
done_with() {
    [[ -n "${1:-}" ]] && printf '%s\n' "$1" >> "$REPORT"
    cat "$REPORT"
    cat "$REPORT" >> "$SUMMARY"
    exit 0
}

cxxflags=$(sed -n 's/^CXXFLAGS[[:space:]]*?=[[:space:]]*//p' frontend/Makefile)
cflags=$(sed -n 's/^CFLAGS[[:space:]]*?=[[:space:]]*//p' frontend/Makefile)
if [[ "$cxxflags" != *-O2* || "$cflags" != *-O2* ]]; then
    done_with "No report: frontend/Makefile's CXXFLAGS or CFLAGS default is not where this reads it, or has no -O2."
fi
cxxflags="${cxxflags/-O2/-O0} -ffunction-sections -fdata-sections"
cflags="${cflags/-O2/-O0} -ffunction-sections -fdata-sections"

git archive HEAD frontend/Makefile frontend/src frontend/third_party | tar -x -C "$WORK"
SECONDS=0
if ! podman run --rm -v "$WORK/frontend":/src:Z -w /src \
        -e CXXFLAGS="$cxxflags" -e CFLAGS="$cflags" cabinetos-builder \
        make -j"$(nproc)" LDFLAGS="-Wl,--gc-sections -Wl,--print-gc-sections" \
        > "$WORK/build.log" 2>&1; then
    tail -n 40 "$WORK/build.log"
    done_with "No report: the scratch build did not link (its log is in this step's output)."
fi
seconds=$SECONDS

# "ld: removing unused section '.text._ZN4cache4findEi' in file 'src/cache.o'"
# becomes "<function or variable> TAB <object> TAB <symbol>". A variable's
# section is named for where it lives (.data.rel.ro.local. for a vtable,
# say); the merged constants and strings have no symbol and are left out.
awk -v q="'" '
    /removing unused section/ {
        split($0, part, q)
        sec = part[2]; kind = ""
        if (sub(/^[.]text[.]/, "", sec)) kind = "function"
        else if (sub(/^[.](data[.]rel[.]ro[.]local|data[.]rel[.]ro|data[.]rel[.]local|data[.]rel|data|rodata|bss|tdata|tbss)[.]/, "", sec)) kind = "variable"
        if (kind == "" || sec ~ /^(str|cst)[0-9]/ || sec ~ /^(startup|unlikely|hot|exit)$/) next
        print kind "\t" part[4] "\t" sec
    }' "$WORK/build.log" > "$WORK/raw"
cut -f3 "$WORK/raw" | c++filt > "$WORK/names"
cut -f1,2 "$WORK/raw" | paste - "$WORK/names" > "$WORK/all"

vendored=$(awk -F'\t' '$1 == "function" && $2 ~ /^third_party\//' "$WORK/all" | wc -l)
# Ours: "<file> TAB <kind> TAB <name>", by file. The C++ library's templates
# and the type information that goes with every vtable are left out.
awk -F'\t' '$2 !~ /^third_party\// && $3 !~ /^((vtable|VTT|construction vtable) for )?(std|__gnu_cxx)::/ && $3 !~ /^typeinfo /' "$WORK/all" \
    | awk -F'\t' '{ file = $2; sub(/[.]o$/, ".cpp", file); print "frontend/" file "\t" $1 "\t" $3 }' \
    | sort -u > "$WORK/ours"
functions=$(awk -F'\t' '$2 == "function"' "$WORK/ours" | wc -l)
variables=$(awk -F'\t' '$2 == "variable"' "$WORK/ours" | wc -l)
files=$(cut -f1 "$WORK/ours" | sort -u | wc -l)

{
    echo "The linker found $functions function(s) and $variables variable(s) in $files" \
        "file(s) that nothing reachable from \`main\` uses (and $vendored function(s)" \
        "in vendored rcheevos, not listed). A scratch build at -O0, ${seconds}s;" \
        "the binary that ships is not this one."
    echo
    if [[ -s "$WORK/ours" ]]; then
        echo "<details><summary>The list, by file</summary>"
        echo
        echo '```'
        awk -F'\t' '$1 != last { print $1; last = $1 } { print "    " $3 ($2 == "variable" ? "  (variable)" : "") }' \
            "$WORK/ours"
        echo '```'
        echo "</details>"
    fi
} >> "$REPORT"
done_with
