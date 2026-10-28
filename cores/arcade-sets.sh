#!/usr/bin/env bash
#
# An arcade emulator's catalogue, from its own list of games at the commit
# cores/build-core.sh pins: every set it plays, whether it works, the files
# its zip must hold and the disc it needs, so the console can pick a game's
# emulator from the files in its zip
# (docs/decisions/2026-10-28-arcade-emulator-from-the-zip.md;
# frontend/src/arcadematch.h reads it). Made here so it always matches the
# pinned emulator, never typed in.
#
# THE SOURCES, one per emulator:
#   fbneo_libretro  its "Arcade only" DAT, and its drivers' input lists for
#                   the light-gun games (the DAT names no controls)
#   mame2003_plus   its metadata/mame2003-plus.xml
#   flycast         core/hw/naomi/naomi_roms.cpp and naomi_roms_input.h, the
#                   sets its libretro build plays (NAOMI_MULTIBOARD is not
#                   defined there, core/build.h)
#
# THE FILE, <core file stem>.catalogue, one set a line:
#   <set> <w|n> <-|g|t> <crc>:<size> ... @<disc>:<sha1> ...
# w works and n does not, by the emulator's own word (FBNeo "preliminary",
# 2003-Plus "preliminary" or "protection"); g a light-gun game, t a
# touchscreen game; each file the zip must hold as its CRC32 and size (size 0
# where the list gives none, Flycast's); each disc by the CHD's name and the
# SHA1 the list gives (none for Flycast). Left out of the files: the BIOS
# options (what the set takes from the BIOS at the end of its romof chain),
# files with no dump, optional files, and Flycast's Copy and Eeprom entries.
# Flycast's also has one `!bios` line: the BIOS zips it reads from bios/dc/.
#
# THE PIN IS READ FROM cores/build-core.sh, the one place it is written. Not
# a mode of that script on purpose: every core's CI cache key covers
# build-core.sh's shared lines (ci/core-cache-key.sh), so editing a mode
# there would rebuild every core.
#
# Any other core writes nothing and exits 0, so CI can run this for every
# core.
#
# Usage: cores/arcade-sets.sh <core> <out dir>
#   writes <out dir>/<core file stem>.catalogue, e.g. fbneo_libretro.catalogue

set -euo pipefail

CORE="${1:?usage: cores/arcade-sets.sh <core> <out dir>}"
OUT="${2:?usage: cores/arcade-sets.sh <core> <out dir>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# The files to fetch from the emulator's repository, and the fewest sets its
# catalogue may come out with: a list that changed shape gives a short one.
case "${CORE}" in
fbneo_libretro)
    FILES=("dats/FinalBurn Neo (ClrMame Pro XML, Arcade only).dat")
    LEAST=8000
    ;;
mame2003_plus)
    FILES=("metadata/mame2003-plus.xml")
    LEAST=5000
    ;;
flycast)
    FILES=("core/hw/naomi/naomi_roms.cpp" "core/hw/naomi/naomi_roms_input.h")
    LEAST=300
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
tmp=$(mktemp -d)
trap 'rm -rf "${tmp}"' EXIT
src=()
for f in "${FILES[@]}"; do
    url="https://raw.githubusercontent.com/${slug}/${COMMIT}/${f// /%20}"
    url=${url//(/%28}
    url=${url//)/%29}
    dest="${tmp}/$(basename "${f}")"
    curl -fsSL --retry 3 -o "${dest}" "${url}"
    src+=("${dest}")
done
# FBNeo's drivers, for its light-gun games: only src/burn/drv, at the pin
# (about 50 MB).
if [[ "${CORE}" == fbneo_libretro ]]; then
    git init -q "${tmp}/fbneo"
    git -C "${tmp}/fbneo" remote add origin "${REPO}"
    git -C "${tmp}/fbneo" fetch -q --depth 1 --filter=blob:none origin "${COMMIT}"
    git -C "${tmp}/fbneo" sparse-checkout set --no-cone 'src/burn/drv/**' >/dev/null
    git -C "${tmp}/fbneo" checkout -q FETCH_HEAD
    src+=("${tmp}/fbneo/src/burn/drv")
fi

mkdir -p "${OUT}"
file="${OUT}/${CORE%_libretro}_libretro.catalogue"
{
    echo "# ${CORE} @ ${COMMIT}, from ${FILES[*]}"
    python3 - "${CORE}" "${src[@]}" <<'PY'
import os, re, sys
import xml.etree.ElementTree as ET

core, paths = sys.argv[1], sys.argv[2:]


printed = set()


def line(name, works, flag, files, discs):
    # A name listed twice is the first one: the emulator finds a set by name
    # and takes the first it lists (Flycast's shors2k2).
    if name.lower() in printed:
        return
    printed.add(name.lower())
    parts = [name.lower(), "w" if works else "n", flag]
    parts += ["%08x:%d" % (crc, size) for crc, size in files]
    parts += ["@%s:%s" % (d, s) for d, s in discs]
    print(" ".join(parts))


def from_xml(path, is_bios, not_working, guns):
    games = {g.get("name"): g for g in ET.parse(path).getroot().iter("game")}

    def bios_crcs(g):
        # The entry at the end of the romof chain, when it is a BIOS: its
        # files are the BIOS options, never required.
        seen = set()
        while g is not None and g.get("romof") and g.get("name") not in seen:
            seen.add(g.get("name"))
            g = games.get(g.get("romof"))
        if g is None or not is_bios(g):
            return set()
        return {r.get("crc").lower() for r in g.iter("rom") if r.get("crc")}

    for name, g in sorted(games.items()):
        if is_bios(g):
            continue
        skip = bios_crcs(g)
        files = []
        for r in g.iter("rom"):
            crc = (r.get("crc") or "").lower()
            if not crc or r.get("status") == "nodump" or r.get("optional") == "yes":
                continue
            if crc in skip:
                continue
            f = (int(crc, 16), int(r.get("size") or 0))
            if f not in files:
                files.append(f)
        discs = []
        for d in g.iter("disk"):
            if d.get("status") == "nodump" or d.get("optional") == "yes":
                continue
            discs.append((re.sub(r"\.chd$", "", d.get("name")), (d.get("sha1") or "").lower()))
        driver = g.find("driver")
        works = driver is None or driver.get("status") not in not_working
        flag = "g" if guns(g) else "-"
        line(name, works, flag, files, discs)


def fbneo_guns(drv):
    # A driver whose input list has a gun, by the lists' own names ("P1 Gun
    # X"); the driver names the list through its <Name>InputInfo.
    lists, spec, drivers = set(), {}, []
    for root, _, names in os.walk(drv):
        for n in names:
            if not n.endswith(".cpp"):
                continue
            text = open(os.path.join(root, n), encoding="latin-1").read()
            for m in re.finditer(r"struct\s+BurnInputInfo\s+(\w+?)InputList\[\]\s*=\s*\{(.*?)\};",
                                 text, re.S):
                if re.search(r'"P\d Gun X"', m.group(2)):
                    lists.add(m.group(1))
            for m in re.finditer(r"STDINPUTINFOSPEC\(\s*(\w+)\s*,\s*(\w+?)InputList\s*\)", text):
                spec[m.group(1)] = m.group(2)
            for m in re.finditer(r"struct\s+BurnDriver\w*\s+BurnDrv\w+\s*=\s*\{(.*?)\};", text, re.S):
                name = re.search(r'"([^"]+)"', m.group(1))
                info = re.search(r"\b(\w+?)InputInfo\b", m.group(1))
                if name and info:
                    drivers.append((name.group(1).lower(), info.group(1)))
    return {n for n, i in drivers if i in lists or spec.get(i) in lists}


if core == "fbneo_libretro":
    guns = fbneo_guns(paths[1])
    from_xml(paths[0], lambda g: g.get("isbios") == "yes", {"preliminary"},
             lambda g: g.get("name").lower() in guns)
elif core == "mame2003_plus":
    from_xml(paths[0], lambda g: g.get("runnable") == "no", {"preliminary", "protection"},
             lambda g: any(i.get("control") == "lightgun" for i in g.iter("input")))
elif core == "flycast":
    src = open(paths[0], encoding="latin-1").read()
    inputs = open(paths[1], encoding="latin-1").read()
    # Comments out, and the multiboard sets, which the libretro build leaves out.
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    src = re.sub(r"#ifdef NAOMI_MULTIBOARD.*?#endif", "", src, flags=re.S)
    if "#if" in src:
        sys.exit("flycast: a preprocessor block naomi_roms.cpp did not have at the last pin")
    # A gun game's inputs carry a reload or Atomiswave's trigger; a game with
    # no player buttons at all (service_btns_inputs) is played on its screen.
    guns, touch = set(), set()
    for m in re.finditer(r"static\s+InputDescriptors\s+(\w+)\s*=\s*\{(.*?)\n\};", inputs, re.S):
        if "NAOMI_RELOAD_KEY" in m.group(2) or "AWAVE_TRIGGER_KEY" in m.group(2):
            guns.add(m.group(1))
        if re.sub(r"\s", "", m.group(2)) == "{NAO_BASE_BTN_DESC}":
            touch.add(m.group(1))

    def entries(table):
        # The top-level { ... } entries of an array, as text.
        start = src.index(table)
        i = src.index("{", start) + 1
        depth, begin = 0, None
        while True:
            c = src[i]
            if c == "{":
                if depth == 0:
                    begin = i
                depth += 1
            elif c == "}":
                if depth == 0:
                    return
                depth -= 1
                if depth == 0:
                    yield src[begin + 1:i]
            i += 1

    bios = []
    for e in entries("const BIOS_t BIOS[]"):
        m = re.match(r'\s*"([^"]+)"', e)
        if m:
            bios.append(m.group(1))
    print("!bios " + " ".join(bios))
    blob = re.compile(r'\{\s*"([^"]*)"\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*(?:,\s*(\w+))?')
    for e in entries("const Game Games[]"):
        name = re.match(r'\s*"([^"]+)"', e)
        if not name:
            continue
        inner = e.index("{")
        depth, j = 0, inner
        while True:
            depth += {"{": 1, "}": -1}.get(e[j], 0)
            if depth == 0:
                break
            j += 1
        files = []
        for b in blob.finditer(e[inner:j]):
            crc = int(b.group(4), 0)
            if crc == 0 or b.group(5) in ("Copy", "Eeprom", "EepromBE16"):
                continue
            if (crc, 0) not in files:
                files.append((crc, 0))
        rest = [t.strip() for t in e[j + 1:].split(",") if t.strip()]
        discs = [(rest[0].strip('"'), "")] if rest and rest[0].startswith('"') else []
        uses = rest[1].lstrip("&") if len(rest) > 1 else ""
        flag = "g" if uses in guns else "t" if uses in touch else "-"
        line(name.group(1), True, flag, files, discs)
PY
} > "${file}"

# CHECKED, NOT ASSUMED: a list that changed shape would otherwise give an
# empty or garbled catalogue and a console that greys every arcade game.
count=$(grep -vc '^[#!]' "${file}")
(( count >= LEAST )) ||
    { echo "${CORE}: only ${count} sets read, fewer than ${LEAST}" >&2; exit 1; }
bad=$(grep -v '^[#!]' "${file}" |
    grep -vcE '^[a-z0-9_]+ [wn] [-gt]( [0-9a-f]{8}:[0-9]+)*( @[^ :]+:[0-9a-f]*)*$' || true)
(( bad == 0 )) || { echo "${CORE}: ${bad} lines are not in the catalogue's form" >&2; exit 1; }
empty=$(grep -v '^[#!]' "${file}" | grep -cE '^[a-z0-9_]+ [wn] [-gt]$' || true)
echo "${CORE}: ${count} sets (${empty} with nothing to check) at ${COMMIT} -> ${file}"
