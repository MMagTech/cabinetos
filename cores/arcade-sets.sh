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
#   fbneo_libretro  its "Arcade only" DAT, and its drivers (the DAT names no
#                   controls): their input lists for the light-gun games
#                   ("P1 Gun X") and the mahjong panels ("P1 Kan", "P1 Pon",
#                   "P1 Chi"), and their casino genre (GBF_CASINO)
#   mame2003_plus   its metadata/mame2003-plus.xml, and its catver.ini for the
#                   games made for anything but a controller
#   flycast         core/hw/naomi/naomi_roms.cpp and naomi_roms_input.h, the
#                   sets its libretro build plays (NAOMI_MULTIBOARD is not
#                   defined there, core/build.h)
#   mame_libretro   MAME 0.289's -listxml, mamedev's release asset, pinned in
#                   cores/fetch-mame.sh: its machines, and for the flags its
#                   own words, ismechanical, a mahjong, hanafuda or gambling
#                   control, a light gun, and a light gun on a machine with a
#                   touchscreen device (Megatouch, Touchmaster, Funcube and
#                   the rest, which MAME reports as light guns)
#
# THE FILE, <core file stem>.catalogue, one set a line:
#   <set> <w|n> <-|g|x> [b<buttons>] <crc>:<size> ... @<disc>:<sha1> ...
# w works and n does not, by the emulator's own word (FBNeo "preliminary",
# 2003-Plus "preliminary" or "protection", MAME 0.289 "preliminary"); g a
# light-gun game; x a game made
# for anything but a controller or a light gun (docs/decisions/
# 2026-10-28-arcade-emulator-from-the-zip.md): a touchscreen, a mahjong or
# hanafuda panel, a gambling, medal or electromechanical machine, a photo
# booth, a test tape. Only what each list marks plainly: an awkward set it
# does not mark plays. b<buttons>, MAME 0.289's alone, is how many buttons
# player 1 has, since that core names none of a game's buttons
# (frontend/src/fightstick.h). Each file the zip must hold as its CRC32 and
# size (size 0 where the list gives none, Flycast's); each disc by the CHD's
# name and the SHA1 the list gives (none for Flycast), a space in its name
# written %20. Left out of the files:
# the BIOS options (what the set takes from the BIOS at the end of its romof
# chain; for MAME 0.289, the files with a `bios=`), files with no dump,
# optional files, and Flycast's Copy and Eeprom entries. Flycast's also has
# one `!bios` line: the BIOS zips it reads from bios/dc/.
#
# THE PIN IS READ FROM cores/build-core.sh, the one place it is written, and
# for MAME 0.289, which is not built there, from cores/fetch-mame.sh (its
# LISTXML_URL and LISTXML_SHA256; $CABINETOS_LISTXML may name a copy already
# fetched, which is checked the same). Not a mode of build-core.sh on
# purpose: every core's CI cache key covers its shared lines
# (ci/core-cache-key.sh), so editing a mode there would rebuild every core.
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
    FILES=("metadata/mame2003-plus.xml" "metadata/catver.ini")
    LEAST=5000
    ;;
flycast)
    FILES=("core/hw/naomi/naomi_roms.cpp" "core/hw/naomi/naomi_roms_input.h")
    LEAST=300
    ;;
mame_libretro)
    FILES=("mame0289lx.zip")
    LEAST=40000
    ;;
*)
    exit 0
    ;;
esac

tmp=$(mktemp -d)
trap 'rm -rf "${tmp}"' EXIT
src=()
if [[ "${CORE}" == mame_libretro ]]; then
    # MAME 0.289's -listxml, by the pin in cores/fetch-mame.sh. Asserted.
    url=$(sed -n 's/^LISTXML_URL=//p' "${ROOT}/cores/fetch-mame.sh")
    sha=$(sed -n 's/^LISTXML_SHA256=//p' "${ROOT}/cores/fetch-mame.sh")
    [[ "${url}" =~ ^https://github.com/ && "${sha}" =~ ^[0-9a-f]{64}$ ]] ||
        { echo "${CORE}: no LISTXML_URL= and LISTXML_SHA256= in cores/fetch-mame.sh" >&2; exit 1; }
    dest="${tmp}/${FILES[0]}"
    if [[ -n "${CABINETOS_LISTXML:-}" ]]; then
        cp "${CABINETOS_LISTXML}" "${dest}"
    else
        curl -fsSL --retry 3 -o "${dest}" "${url}"
    fi
    got=$(sha256sum "${dest}" | cut -d' ' -f1)
    [[ "${got}" == "${sha}" ]] ||
        { echo "${CORE}: ${url} has sha256 ${got}, the pin says ${sha}" >&2; exit 1; }
    src+=("${dest}")
    COMMIT="${sha}"
    FILES=("${url}")
else
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
for f in "${FILES[@]}"; do
    url="https://raw.githubusercontent.com/${slug}/${COMMIT}/${f// /%20}"
    url=${url//(/%28}
    url=${url//)/%29}
    dest="${tmp}/$(basename "${f}")"
    curl -fsSL --retry 3 -o "${dest}" "${url}"
    src+=("${dest}")
done
fi
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


def line(name, works, flag, files, discs, buttons=0):
    # A name listed twice is the first one: the emulator finds a set by name
    # and takes the first it lists (Flycast's shors2k2).
    if name.lower() in printed:
        return
    printed.add(name.lower())
    parts = [name.lower(), "w" if works else "n", flag]
    if buttons:
        parts.append("b%d" % buttons)
    parts += ["%08x:%d" % (crc, size) for crc, size in files]
    # A disc's name may hold spaces (MAME 0.289's "road burners v1.04"),
    # which the line separates by: written %20, and % as %25.
    parts += ["@%s:%s" % (d.replace("%", "%25").replace(" ", "%20"), s) for d, s in discs]
    print(" ".join(parts))


def from_xml(path, is_bios, not_working, flag_of):
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
        line(name, works, flag_of(g), files, discs)


def fbneo_flags(drv):
    # A driver whose input list has a gun, by the lists' own names ("P1 Gun
    # X"), is a light-gun game; one with a mahjong panel's keys, or in the
    # casino genre, is not a controller's. The driver names its list through
    # its <Name>InputInfo.
    lists, panels, spec, drivers = set(), set(), {}, []
    for root, _, names in os.walk(drv):
        for n in names:
            if not n.endswith(".cpp"):
                continue
            text = open(os.path.join(root, n), encoding="latin-1").read()
            for m in re.finditer(r"struct\s+BurnInputInfo\s+(\w+?)InputList\[\]\s*=\s*\{(.*?)\};",
                                 text, re.S):
                if re.search(r'"P\d Gun X"', m.group(2)):
                    lists.add(m.group(1))
                if re.search(r'"P1 (Kan|Pon|Chi)"', m.group(2)):
                    panels.add(m.group(1))
            for m in re.finditer(r"STDINPUTINFOSPEC\(\s*(\w+)\s*,\s*(\w+?)InputList\s*\)", text):
                spec[m.group(1)] = m.group(2)
            for m in re.finditer(r"struct\s+BurnDriver\w*\s+BurnDrv\w+\s*=\s*\{(.*?)\};", text, re.S):
                name = re.search(r'"([^"]+)"', m.group(1))
                info = re.search(r"\b(\w+?)InputInfo\b", m.group(1))
                if name:
                    drivers.append((name.group(1).lower(), info.group(1) if info else "",
                                    "GBF_CASINO" in m.group(1)))
    flags = {}
    for n, i, casino in drivers:
        if casino or i in panels or spec.get(i) in panels:
            flags[n] = "x"
        elif i in lists or spec.get(i) in lists:
            flags[n] = "g"
    return flags


def catver_flags(path):
    # MAME 2003-Plus's own catver.ini: the categories of games made for
    # anything but a controller.
    flags, section = {}, None
    for l in open(path, encoding="latin-1"):
        l = l.strip()
        if l.startswith("["):
            section = l
        elif section == "[Category]" and "=" in l:
            name, cat = l.split("=", 1)
            if re.match(r"(Tabletop / (Mahjong|Hanafuda)|Casino /|Slot Machine /|"
                        r"Electromechanical /|Medal Game /|Misc\. / Print Club|Utilities /)", cat):
                flags[name.lower()] = "x"
    if len(flags) < 200:
        sys.exit("mame2003_plus: catver.ini gave only %d games not made for a controller" % len(flags))
    return flags


if core == "fbneo_libretro":
    flags = fbneo_flags(paths[1])
    if sum(f == "x" for f in flags.values()) < 100:
        sys.exit("fbneo_libretro: its drivers gave too few games not made for a controller")
    from_xml(paths[0], lambda g: g.get("isbios") == "yes", {"preliminary"},
             lambda g: flags.get(g.get("name").lower(), "-"))
elif core == "mame2003_plus":
    flags = catver_flags(paths[1])
    from_xml(paths[0], lambda g: g.get("runnable") == "no", {"preliminary", "protection"},
             lambda g: flags.get(g.get("name").lower()) or
             ("g" if any(i.get("control") == "lightgun" for i in g.iter("input")) else "-"))
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
    # no player buttons at all (service_btns_inputs) is played on its screen,
    # not a controller.
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
        flag = "g" if uses in guns else "x" if uses in touch else "-"
        line(name.group(1), True, flag, files, discs)
elif core == "mame_libretro":
    import zipfile
    # A machine is a set unless it is a BIOS, a device or not runnable. Read
    # as it streams: the list is 327 MB.
    panels = {"mahjong", "hanafuda", "gambling"}
    marks = {"x": 0, "g": 0}
    with zipfile.ZipFile(paths[0]) as z:
        xml = [n for n in z.namelist() if n.endswith(".xml")]
        if len(xml) != 1:
            sys.exit("mame_libretro: the -listxml zip holds %d xml files, not one" % len(xml))
        with z.open(xml[0]) as f:
            for _, m in ET.iterparse(f):
                if m.tag != "machine":
                    continue
                if "yes" in (m.get("isbios"), m.get("isdevice")) or m.get("runnable") == "no":
                    m.clear()
                    continue
                files = []
                for r in m.iter("rom"):
                    crc = (r.get("crc") or "").lower()
                    if (not crc or r.get("status") == "nodump" or r.get("optional") == "yes"
                            or r.get("bios")):
                        continue
                    f2 = (int(crc, 16), int(r.get("size") or 0))
                    if f2 not in files:
                        files.append(f2)
                discs = [(d.get("name"), (d.get("sha1") or "").lower()) for d in m.iter("disk")
                         if d.get("status") != "nodump" and d.get("optional") != "yes"]
                controls = list(m.iter("control"))
                kinds = {c.get("type") for c in controls}
                touch = any("touch" in (d.get("name") or "") for d in m.iter("device_ref"))
                if m.get("ismechanical") == "yes" or kinds & panels or ("lightgun" in kinds and touch):
                    flag = "x"
                elif "lightgun" in kinds:
                    flag = "g"
                else:
                    flag = "-"
                if flag != "-":
                    marks[flag] += 1
                buttons = max([int(c.get("buttons") or 0) for c in controls
                               if c.get("player") in (None, "1")] or [0])
                driver = m.find("driver")
                works = driver is None or driver.get("status") != "preliminary"
                line(m.get("name"), works, flag, files, discs, buttons)
                m.clear()
    if marks["x"] < 5000 or marks["g"] < 400:
        sys.exit("mame_libretro: only %d games marked not for a controller and %d light-gun "
                 "games; the list changed shape" % (marks["x"], marks["g"]))
PY
} > "${file}"

# CHECKED, NOT ASSUMED: a list that changed shape would otherwise give an
# empty or garbled catalogue and a console that greys every arcade game.
count=$(grep -vc '^[#!]' "${file}")
(( count >= LEAST )) ||
    { echo "${CORE}: only ${count} sets read, fewer than ${LEAST}" >&2; exit 1; }
bad=$(grep -v '^[#!]' "${file}" |
    grep -vcE '^[a-z0-9_]+ [wn] [-gx]( b[0-9]+)?( [0-9a-f]{8}:[0-9]+)*( @[^ :]+:[0-9a-f]*)*$' || true)
(( bad == 0 )) || { echo "${CORE}: ${bad} lines are not in the catalogue's form" >&2; exit 1; }
empty=$(grep -v '^[#!]' "${file}" | grep -cE '^[a-z0-9_]+ [wn] [-gx]( b[0-9]+)?$' || true)
echo "${CORE}: ${count} sets (${empty} with nothing to check) at ${COMMIT} -> ${file}"
