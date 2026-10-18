#!/usr/bin/env bash
#
# Is every numbered question in docs/PROJECT.md in its index? (#398, step 4)
#
#   ci/check-project-index.sh [docs/PROJECT.md]
#
# REPORT ONLY: prints each question with no line in the index, each line in
# the index with no question, and a question count the index gives that no
# longer matches, and always exits 0. .github/workflows/checks.yml runs it on
# every pull request; on GitHub each one is also a warning on the pull request.
#
# WHY. The index at the top of PROJECT.md ("## Index") was written by hand in
# #427, because GitHub does not show a file that size and the file is searched
# instead: a question by its number (`### 32.`), its state from the index. A
# question added without its index line is found only by someone reading all
# 17,000 lines. A question is a `### N.` heading (10b and the like too); its
# index line is `- **N.` under "### The open questions, and where each ended".

set -euo pipefail

DOC="${1:-docs/PROJECT.md}"

# number <tab> line, for the questions, then for the index's lines.
questions=$(awk '/^### [0-9]+[a-z]?\. / { n = $2; sub(/\.$/, "", n); print n "\t" NR }' "${DOC}")
indexed=$(awk '
    /^### The open questions, and where each ended/ { inside = 1; next }
    inside && (/^## / || /^---/) { exit }
    inside && /^- \*\*[0-9]+[a-z]?\./ { n = $2; sub(/^\*\*/, "", n); sub(/\.$/, "", n); print n "\t" NR }
' "${DOC}")

problems=()
while IFS=$'\t' read -r n line; do
    [[ -n "${n}" ]] || continue
    if ! cut -f1 <<< "${indexed}" | grep -qx -- "${n}"; then
        problems+=("${line}"$'\t'"Question ${n} is not in the index (\"The open questions, and where each ended\").")
    fi
done <<< "${questions}"
while IFS=$'\t' read -r n line; do
    [[ -n "${n}" ]] || continue
    if ! cut -f1 <<< "${questions}" | grep -qx -- "${n}"; then
        problems+=("${line}"$'\t'"The index lists question ${n}, which has no \"### ${n}.\" heading.")
    fi
done <<< "${indexed}"

# "**Open questions**: the 51 below", in "The sections, in order".
count=$(grep -c . <<< "${questions}" || true)
said=$(grep -n -o -E '\*\*Open questions\*\*: the [0-9]+ below' "${DOC}" | head -n 1 || true)
if [[ -n "${said}" ]]; then
    said_line="${said%%:*}"
    said_count=$(grep -o -E '[0-9]+ below' <<< "${said}" | grep -o -E '[0-9]+')
    if [[ "${said_count}" != "${count}" ]]; then
        problems+=("${said_line}"$'\t'"The index says ${said_count} open questions; there are ${count}.")
    fi
fi

if [[ ${#problems[@]} -eq 0 ]]; then
    echo "All ${count} questions are in the index."
    exit 0
fi
for p in "${problems[@]}"; do
    echo "${DOC}:${p%%$'\t'*}: ${p#*$'\t'}"
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::warning file=${DOC},line=${p%%$'\t'*}::${p#*$'\t'}"
    fi
done
echo "Report only: this check never fails."
exit 0
