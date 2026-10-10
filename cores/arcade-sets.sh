#!/usr/bin/env bash
#
# The list of arcade sets an arcade core runs, from the core's own DAT at the
# commit cores/build-core.sh pins (#310). One set name per line.
#
# WHY: a RomM folder named after the platform (`arcade`, `mame`, `cps`) holds
# games for both arcade cores, and the console picks the core per game by the
# zip's set name: FinalBurn Neo if its list has the set, else MAME 2003-Plus if
# its list does, else the game is greyed as not supported here
# (frontend/src/catalog.cpp, arcadeCoreFor). The lists are generated here so
# they always match the pinned core, never typed in.
#
# THE PIN IS READ FROM cores/build-core.sh, the one place it is written. Not
# a mode of that script on purpose: build-core.sh keys every core's CI cache,
# so editing it rebuilds all twenty-two cores.
#
# A core with no set list (every core but the two arcade ones) writes nothing
# and exits 0, so CI can run this for every core.
#
# Usage: cores/arcade-sets.sh <core> <out dir>
#   writes <out dir>/<core file stem>.sets, e.g. fbneo_libretro.sets

set -euo pipefail

CORE="${1:?usage: cores/arcade-sets.sh <core> <out dir>}"
OUT="${2:?usage: cores/arcade-sets.sh <core> <out dir>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# The DAT inside the core's repository, and the attribute that marks an entry
# that is a BIOS rather than a game (a BIOS zip in a folder is not a game).
case "${CORE}" in
fbneo_libretro)
    # "Arcade only" carries the Neo Geo sets too (mslug is in it); the other
    # DATs in that folder are FinalBurn Neo's console systems.
    DAT="dats/FinalBurn Neo (ClrMame Pro XML, Arcade only).dat"
    NOT_A_GAME='isbios="yes"'
    ;;
mame2003_plus)
    DAT="metadata/mame2003-plus.xml"
    NOT_A_GAME='runnable="no"'
    ;;
*)
    exit 0
    ;;
esac

# The case arm in build-core.sh, and its REPO= and COMMIT= lines. Asserted:
# a pin that cannot be read fails the build rather than shipping no list.
arm=$(awk -v c="${CORE})" '$1 == c {on=1} on {print} on && /;;/ {exit}' "${ROOT}/cores/build-core.sh")
REPO=$(sed -n 's/^ *REPO=//p' <<<"${arm}")
COMMIT=$(sed -n 's/^ *COMMIT=//p' <<<"${arm}")
[[ "${REPO}" =~ ^https://github.com/[^/]+/[^/]+\.git$ ]] ||
    { echo "${CORE}: no REPO= in its build-core.sh arm" >&2; exit 1; }
[[ "${COMMIT}" =~ ^[0-9a-f]{40}$ ]] ||
    { echo "${CORE}: no 40-character COMMIT= in its build-core.sh arm" >&2; exit 1; }

slug=${REPO#https://github.com/}
slug=${slug%.git}
url="https://raw.githubusercontent.com/${slug}/${COMMIT}/${DAT// /%20}"
url=${url//(/%28}
url=${url//)/%29}
tmp=$(mktemp)
trap 'rm -f "${tmp}"' EXIT
curl -fsSL --retry 3 -o "${tmp}" "${url}"

mkdir -p "${OUT}"
file="${OUT}/${CORE%_libretro}_libretro.sets"
{
    echo "# ${CORE} @ ${COMMIT}, from ${DAT}"
    grep -o '<game [^>]*>' "${tmp}" | grep -v "${NOT_A_GAME}" |
        sed -n 's/.* name="\([^"]*\)".*/\1/p' | tr '[:upper:]' '[:lower:]' | LC_ALL=C sort -u
} > "${file}"

# CHECKED, NOT ASSUMED: a DAT that changed shape would otherwise give an empty
# or garbled list and a console that greys every arcade game.
count=$(grep -vc '^#' "${file}")
(( count > 1000 )) || { echo "${CORE}: only ${count} sets read from ${DAT}" >&2; exit 1; }
bad=$(grep -v '^#' "${file}" | grep -vcE '^[a-z0-9_]+$' || true)
(( bad == 0 )) || { echo "${CORE}: ${bad} set names are not plain [a-z0-9_]" >&2; exit 1; }
echo "${CORE}: ${count} sets from ${DAT} at ${COMMIT} -> ${file}"
