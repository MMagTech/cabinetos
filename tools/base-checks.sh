#!/usr/bin/env bash
#
# The A9 half of a base update's checks. Run from the Mac once the A9 runs
# the testing image built from the base-update branch.
#
# THE BASE-UPDATE PULL REQUEST LISTS ITS OWN CHECKS (MMagTech, 2026-10-09):
# only for the areas of ci/base-watch.txt the update changed, split into what
# the A9's log answers and what needs him at the TV. This does the first
# half: it works out the changed areas the same way the pull request did
# (ci/base-report.py, main's manifest against the branch's), confirms the A9
# really runs the new base, runs each area's log: check there as `cabinet`,
# and prints the TV checks to hand him. It changes nothing on the console and
# presses nothing, so it is safe while he is at the TV.
#
#   tools/base-checks.sh base-update/44.20261006.1
#
# Exit 0 when every log check passed.
set -uo pipefail
export LC_ALL=C

BRANCH="${1:?usage: tools/base-checks.sh <base-update branch>}"
A9="${CABINETOS_A9:-cabinet@192.168.1.212}"
KEY="${CABINETOS_KEY:-$HOME/.ssh/cabinetos}"
SSH=(ssh -p 2222 -i "$KEY" -o ConnectTimeout=20 -o BatchMode=yes "$A9")
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cd "$ROOT" || exit 2
git fetch -q origin main "$BRANCH" || { echo "could not fetch main and $BRANCH"; exit 2; }
git show origin/main:base-manifest.txt > "$TMP/old.txt" &&
    git show "origin/$BRANCH:base-manifest.txt" > "$TMP/new.txt" || exit 2
python3 ci/base-report.py checks "$TMP/old.txt" "$TMP/new.txt" > "$TMP/checks.tsv" || exit 2

fail=0
ok() { printf 'ok    %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; [[ -n "${2:-}" ]] && sed 's/^/      /' <<<"$2"; fail=1; }

echo "== areas this update changed"
awk -F'\t' '$1 == "area" { print "      " $2 }' "$TMP/checks.tsv"

echo "== from the A9's log ($A9)"
# Proof first: the A9 runs THIS base, not the one before. Every package the
# update brings to a watched area must be installed at its new version (ones
# CabinetOS removes are left out).
want=$(python3 ci/base-report.py expect "$TMP/old.txt" "$TMP/new.txt" | sort)
if [[ -n "$want" ]]; then
    have=$("${SSH[@]}" "rpm -q --qf '%{NAME} %{VERSION}-%{RELEASE}\n' $(awk '{print $1}' <<<"$want" | tr '\n' ' ') 2>/dev/null")
    missing=$(comm -23 <(echo "$want") <(grep -v 'not installed' <<<"$have" | sort -u))
    if [[ -z "$missing" ]]; then
        ok "The A9 runs this base: the changed packages have their new versions"
    else
        bad "The A9 runs this base: $(wc -l <<<"$missing" | tr -d ' ') packages are not at the branch's versions" \
            "$(head -8 <<<"$missing")"
    fi
fi

while IFS=$'\t' read -r kind check command; do
    [[ "$kind" == log ]] || continue
    out=$("${SSH[@]}" bash -s <<<"$command" 2>&1)
    if [[ $? -eq 0 ]]; then ok "$check"; else bad "$check" "$out"; fi
done < "$TMP/checks.tsv"

echo "== at the TV (MMagTech)"
n=0
while IFS=$'\t' read -r kind check _; do
    [[ "$kind" == tv ]] || continue
    n=$((n + 1)); printf '%3d.  %s\n' "$n" "$check"
done < "$TMP/checks.tsv"
(( n )) || echo "      nothing: the log answers all of it"

exit $fail
