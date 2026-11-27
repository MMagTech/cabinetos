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
#                   controls and marks no file optional): their BIOS files'
#                   BRF_OPT, their input lists for the light-gun games
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
#   sm2emu          sm2-emu's data/games.xml, at the commit cores/build-sm2emu.sh
#                   pins: its games, a clone taking its parent's regions
#                   where it names none of its own, and its preliminary flag
#                   and light gun from its parent as sm2-emu's own loader
#                   does (src/rom/game_db.cpp); a region it marks
#                   required="false" is left out, as an optional file is
#   supermodel      Supermodel's Config/Games.xml, at the commit
#                   cores/build-supermodel.sh pins: its Model 3 games, a
#                   clone's files laid over its parent's region by region as
#                   its own loader does (src/GameLoader.cpp), a region marked
#                   required="false" left out. Its list marks nothing not
#                   working and gives the mounted guns and the light gun the
#                   same control, so the g and x marks are MAME 0.289's for the
#                   same set (its -listxml, as for mame_libretro): The Lost
#                   World is a light gun there, L.A. Machineguns and The Ocean
#                   Hunter are not
#   xemu-chihiro    MAME 0.289's -listxml for the Chihiro sets and their
#                   parents (sega/chihiro.cpp; it marks every one preliminary
#                   and gives them all the same controls, so it marks nothing
#                   here), and the fork's own hw/xbox/chihiro/chihiro.c and
#                   config_spec.yml, at the commit cores/build-xemu-chihiro.sh
#                   pins: a set's ID, the four letters at boot.id 0x30 its
#                   converted image carries, and the name the fork gives its
#                   saves, both its parent's, from chihiro_game_names; it works
#                   when the fork has controls for it, one of its JVS profiles
#                   (chihiro.jvs.profile), and is a light-gun game when that
#                   profile has a trigger. Which parent set each profile is
#                   for is the table below (PROFILE_SETS), checked against the
#                   fork's profiles and names, since the fork names its
#                   profiles after its cabinets, not after MAME's sets
#   linuxloader     Linux Loader's own source, at the commit
#                   build_files/install-linuxloader.sh pins (the AppImage it
#                   puts in the image is that release's): its list of the
#                   programs it starts, in its order (mainShared.c, games[]),
#                   its table of game versions by fingerprint (gameData.c, the
#                   fingerprints in config.h) with each one's kind (DRIVING,
#                   FLYING, DIGITAL are played on a controller, SHOOTING with a
#                   light gun, MAHJONG on a panel) and whether it works. Which
#                   program each game is is the table below (PROGRAM_GAMES),
#                   checked against the loader's programs, its games and its
#                   own list of original programs (cleanElfCRC32's "| name"),
#                   since the loader names a game's version, not its program.
#                   A test menu, a server box and Segaboot are no game and
#                   have no line
#   windowsarcadeloader  windowsarcadeloader's own tree, at the commit
#                   cores/build-windowsarcadeloader.sh pins: one line per
#                   game profile (systemprofiles/<system>/<gameid>.yaml, over
#                   its built-in defaults.yaml); whether it works by the
#                   status column of its own status table (docs/
#                   IMPLEMENTATION.md, which README's list is made from); its
#                   marks by what its profile marks (the rule is in the code
#                   below); and the Windows setup the loader's `prepare`
#                   builds for it. With it a second file, the files list:
#                   every file those setups' verbs download, read from the
#                   winetricks build_files/install-windowsarcadeloader.sh pins
#                   (and dotnet11's from the launcher, its own verb)
#
# THE FILE, <core file stem>.catalogue, one set a line (Linux Loader's: one
# program a line, by its name in lower case, then one game version a line,
# by its fingerprint as eight hex digits, each with only its marks, and a
# `!programs` line of the program names in the loader's order, as spelt):
#   <set> <w|n> <-|g|x> [b<buttons>] <crc>:<size> ... @<disc>:<sha1> ... [=<id>:<name>]
# w works and n does not, by the emulator's own word (FBNeo "preliminary",
# 2003-Plus "preliminary" or "protection", MAME 0.289 "preliminary"); g a
# light-gun game; x a game made
# for anything but a controller or a light gun (docs/decisions/
# 2026-10-28-arcade-emulator-from-the-zip.md): a touchscreen, a mahjong or
# hanafuda panel, a gambling, medal or electromechanical machine, a photo
# booth, a test tape. Only what each list marks plainly: an awkward set it
# does not mark plays. b<buttons>, MAME 0.289's, sm2-emu's and Supermodel's
# alone, is how many buttons player 1 has, since none names a game's buttons
# to the console (frontend/src/fightstick.h): MAME's -listxml says, sm2-emu's
# list says 3 for a game it gives three buttons ("buttons3"), Supermodel's 4
# for a fighting game or Spikeout and 3 for Virtua Striker, and neither says
# anything for a driving or gun game. Each file the zip must hold as its CRC32
# and size (size 0 where the list gives none, Flycast's, sm2-emu's and
# Supermodel's); each disc by the CHD's
# name and the SHA1 the list gives (none for Flycast), a space in its name
# written %20. A Chihiro game's =<id>:<name> is the ID its converted image's
# boot.id carries and the name its saves take (xemu-chihiro's alone; it has
# no files to check, the image being one file the library's owner made). Left
# out of the files:
# the BIOS files the emulator does not ask for (FBNeo's that its drivers mark
# BRF_OPT, Neo Geo's choices of BIOS among them; for 2003-Plus and MAME
# 0.289, the BIOS choices other than the default, the files with a `bios=`
# naming another), files with no dump,
# optional files, and Flycast's Copy and Eeprom entries. MAME 0.289's also
# holds the files of the devices in the machine (its device_refs), which its
# -listxml lists under each device. Flycast's also has
# one `!bios` line: the BIOS zips it reads from bios/dc/.
#
# windowsarcadeloader's line is a game, by its id (the name of the game's
# <gameid>.windowsloader file), with only its marks and its Windows setup:
#   <gameid> <w|n> <-|g|x> ~<prefix>:<verb>,<verb>,... [%<KEY>=<default>:<choices>] ...
# <prefix> the last part of the Wine prefix the game runs in (full, the shared
# one, or its own), the verbs every winetricks verb `prepare` installs there,
# in its order: the prefix's prefix_tricks, then the game's tricks not among
# them. A % word for each picture option the profile declares (a key ending
# RESOLUTION or RENDER_SCALE): the value its merged env gives, then a choice's
# values in the profile's order, `a|b|c`, or a number's range, `min-max`. One
# `!runner` line names the runner's folder (defaults.yaml's runner). Its files
# list, windowsarcadeloader.files (tracked as cores/windows-arcade-files.txt,
# which CI checks against it), one file a verb downloads a line, in
# winetricks' cache layout:
#   <verb> <cache folder>/<file name> <sha256> <url>
#
# THE PIN IS READ FROM cores/build-core.sh, the one place it is written, and
# for MAME 0.289, which is not built there, from cores/fetch-mame.sh (its
# LISTXML_URL and LISTXML_SHA256; $CABINETOS_LISTXML may name a copy already
# fetched, which is checked the same), and for sm2-emu and Supermodel,
# programs of their own, from cores/build-sm2emu.sh,
# cores/build-supermodel.sh and cores/build-xemu-chihiro.sh (their REPO= and
# COMMIT=), and for Linux Loader, which is not built here, from
# build_files/install-linuxloader.sh (its LINUXLOADER_REPO= and
# LINUXLOADER_COMMIT=), and for windowsarcadeloader from
# cores/build-windowsarcadeloader.sh (its REPO= and COMMIT=), with winetricks
# from build_files/install-windowsarcadeloader.sh (its WINETRICKS_URL and
# WINETRICKS_SHA256). Not a mode of build-core.sh on
# purpose: every core's CI cache key covers its shared lines
# (ci/core-cache-key.sh), so editing a mode there would rebuild every core.
#
# Any other core writes nothing and exits 0, so CI can run this for every
# core.
#
# Usage: cores/arcade-sets.sh <core> <out dir>
#   writes <out dir>/<core file stem>.catalogue, e.g. fbneo_libretro.catalogue,
#   or for a program of its own its name's, sm2emu.catalogue,
#   supermodel.catalogue, xemu-chihiro.catalogue, linuxloader.catalogue or
#   windowsarcadeloader.catalogue (and windowsarcadeloader.files)

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
sm2emu)
    FILES=("data/games.xml")
    LEAST=80
    ;;
supermodel)
    FILES=("Config/Games.xml")
    LEAST=55
    ;;
xemu-chihiro)
    FILES=("hw/xbox/chihiro/chihiro.c" "config_spec.yml")
    LEAST=40
    ;;
linuxloader)
    FILES=("src/loader/mainShared.c" "src/loader/config/gameData.c" "src/loader/config/config.h"
           "src/loader/mainL.c" "src/loader/linuxExe.c" "src/loader/graphics/gpuVendor.c")
    LEAST=80
    ;;
windowsarcadeloader)
    # Fetched as one sparse checkout (below), not file by file: the profiles
    # are 418 files.
    FILES=("systemprofiles" "docs/IMPLEMENTATION.md" "crates/launcher/src/defaults.yaml"
           "crates/launcher/src/wine.rs" "crates/launcher/src/wine/dotnet11.rs")
    LEAST=400
    ;;
*)
    exit 0
    ;;
esac

tmp=$(mktemp -d)
trap 'rm -rf "${tmp}"' EXIT
src=()
# MAME 0.289's -listxml into $1, by the pin in cores/fetch-mame.sh, with its
# URL and SHA-256 in LISTXML_URL and LISTXML_SHA. Asserted.
listxml() {
    LISTXML_URL=$(sed -n 's/^LISTXML_URL=//p' "${ROOT}/cores/fetch-mame.sh")
    LISTXML_SHA=$(sed -n 's/^LISTXML_SHA256=//p' "${ROOT}/cores/fetch-mame.sh")
    [[ "${LISTXML_URL}" =~ ^https://github.com/ && "${LISTXML_SHA}" =~ ^[0-9a-f]{64}$ ]] ||
        { echo "${CORE}: no LISTXML_URL= and LISTXML_SHA256= in cores/fetch-mame.sh" >&2; exit 1; }
    if [[ -n "${CABINETOS_LISTXML:-}" ]]; then
        cp "${CABINETOS_LISTXML}" "$1"
    else
        curl -fsSL --retry 3 -o "$1" "${LISTXML_URL}"
    fi
    local got
    got=$(sha256sum "$1" | cut -d' ' -f1)
    [[ "${got}" == "${LISTXML_SHA}" ]] ||
        { echo "${CORE}: ${LISTXML_URL} has sha256 ${got}, the pin says ${LISTXML_SHA}" >&2; exit 1; }
}
if [[ "${CORE}" == mame_libretro ]]; then
    listxml "${tmp}/${FILES[0]}"
    src+=("${tmp}/${FILES[0]}")
    COMMIT="${LISTXML_SHA}"
    FILES=("${LISTXML_URL}")
else
# The case arm in build-core.sh, and its REPO= and COMMIT= lines; for sm2-emu
# and Supermodel their own build scripts'. Asserted: a pin that cannot be read
# fails the build rather than shipping no list.
case "${CORE}" in
sm2emu) arm=$(cat "${ROOT}/cores/build-sm2emu.sh") ;;
supermodel) arm=$(cat "${ROOT}/cores/build-supermodel.sh") ;;
xemu-chihiro) arm=$(cat "${ROOT}/cores/build-xemu-chihiro.sh") ;;
linuxloader) arm=$(sed -n 's/^readonly LINUXLOADER_//p' "${ROOT}/build_files/install-linuxloader.sh") ;;
windowsarcadeloader) arm=$(cat "${ROOT}/cores/build-windowsarcadeloader.sh") ;;
*) arm=$(awk -v c="${CORE})" '$1 == c {on=1} on {print} on && /;;/ {exit}' "${ROOT}/cores/build-core.sh") ;;
esac
REPO=$(sed -n 's/^ *REPO=//p' <<<"${arm}")
COMMIT=$(sed -n 's/^ *COMMIT=//p' <<<"${arm}")
[[ "${REPO}" =~ ^https://github.com/[^/]+/[^/]+\.git$ ]] ||
    { echo "${CORE}: no REPO= where its pin is written" >&2; exit 1; }
[[ "${COMMIT}" =~ ^[0-9a-f]{40}$ ]] ||
    { echo "${CORE}: no 40-character COMMIT= where its pin is written" >&2; exit 1; }

slug=${REPO#https://github.com/}
slug=${slug%.git}
[[ "${CORE}" == windowsarcadeloader ]] || for f in "${FILES[@]}"; do
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
# windowsarcadeloader's profiles, notes and launcher source, at the pin, and
# winetricks at the version and checksum install-windowsarcadeloader.sh pins
# (its WINETRICKS_URL and WINETRICKS_SHA256), the one the image carries as the
# runner's bin/winetricks: its verbs' downloads are the files list.
if [[ "${CORE}" == windowsarcadeloader ]]; then
    git init -q "${tmp}/wal"
    git -C "${tmp}/wal" remote add origin "${REPO}"
    git -C "${tmp}/wal" fetch -q --depth 1 --filter=blob:none origin "${COMMIT}"
    git -C "${tmp}/wal" sparse-checkout set --no-cone "${FILES[@]/#//}" >/dev/null
    git -C "${tmp}/wal" checkout -q FETCH_HEAD
    [[ "$(git -C "${tmp}/wal" rev-parse HEAD)" == "${COMMIT}" ]] ||
        { echo "${CORE}: the checkout is not at ${COMMIT}" >&2; exit 1; }
    src+=("${tmp}/wal")
    WT_URL=$(sed -n 's/^readonly WINETRICKS_URL=//p' "${ROOT}/build_files/install-windowsarcadeloader.sh")
    WT_SHA=$(sed -n 's/^readonly WINETRICKS_SHA256=//p' "${ROOT}/build_files/install-windowsarcadeloader.sh")
    [[ "${WT_URL}" =~ ^https://raw.githubusercontent.com/Winetricks/winetricks/[0-9a-f]{40}/src/winetricks$ &&
       "${WT_SHA}" =~ ^[0-9a-f]{64}$ ]] ||
        { echo "${CORE}: no WINETRICKS_URL= and WINETRICKS_SHA256= in build_files/install-windowsarcadeloader.sh" >&2; exit 1; }
    curl -fsSL --retry 3 -o "${tmp}/winetricks" "${WT_URL}"
    got=$(sha256sum "${tmp}/winetricks" | cut -d' ' -f1)
    [[ "${got}" == "${WT_SHA}" ]] ||
        { echo "${CORE}: ${WT_URL} has sha256 ${got}, the pin says ${WT_SHA}" >&2; exit 1; }
    src+=("${tmp}/winetricks")
    FILES+=("${WT_URL}")
    # The second file, the files list, written by the Python below.
    export WAL_FILES_OUT="${OUT}/${CORE}.files" WAL_COMMIT="${COMMIT}"
fi
# Supermodel's marks are MAME 0.289's for the same set (above), and
# xemu-chihiro's sets are MAME 0.289's.
if [[ "${CORE}" == supermodel || "${CORE}" == xemu-chihiro ]]; then
    listxml "${tmp}/mame0289lx.zip"
    src+=("${tmp}/mame0289lx.zip")
    FILES+=("${LISTXML_URL}")
fi

mkdir -p "${OUT}"
file="${OUT}/${CORE%_libretro}_libretro.catalogue"
# A program of its own is named for itself (arcadematch.cpp, loadFrom).
[[ "${CORE}" == sm2emu || "${CORE}" == supermodel || "${CORE}" == xemu-chihiro ||
   "${CORE}" == linuxloader || "${CORE}" == windowsarcadeloader ]] &&
    file="${OUT}/${CORE}.catalogue"
{
    echo "# ${CORE} @ ${COMMIT}, from ${FILES[*]}"
    python3 - "${CORE}" "${src[@]}" <<'PY'
import hashlib, os, re, sys, zipfile
import xml.etree.ElementTree as ET

core, paths = sys.argv[1], sys.argv[2:]


def mame_machines(path, devices=False):
    # MAME 0.289's machines from its -listxml zip, one at a time as the list
    # streams (it is 327 MB), each cleared once used. A BIOS, a device or a
    # machine that cannot run is not a set; devices=True gives the devices
    # instead.
    with zipfile.ZipFile(path) as z:
        xml = [n for n in z.namelist() if n.endswith(".xml")]
        if len(xml) != 1:
            sys.exit("%s: the -listxml zip holds %d xml files, not one" % (core, len(xml)))
        with z.open(xml[0]) as f:
            for _, m in ET.iterparse(f):
                if m.tag != "machine":
                    continue
                if devices:
                    if m.get("isdevice") == "yes":
                        yield m
                elif "yes" not in (m.get("isbios"), m.get("isdevice")) and m.get("runnable") != "no":
                    yield m
                m.clear()


def mame_flag(m):
    # MAME 0.289's own words: x for ismechanical, a mahjong, hanafuda or
    # gambling control, or a light gun on a machine with a touchscreen device
    # (which MAME reports as a light gun); g for a light gun.
    kinds = {c.get("type") for c in m.iter("control")}
    touch = any("touch" in (d.get("name") or "") for d in m.iter("device_ref"))
    if (m.get("ismechanical") == "yes" or kinds & {"mahjong", "hanafuda", "gambling"}
            or ("lightgun" in kinds and touch)):
        return "x"
    return "g" if "lightgun" in kinds else "-"


printed = set()


def line(name, works, flag, files, discs, buttons=0, image=None):
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
    if image:
        parts.append("=%s:%s" % image)
    print(" ".join(parts))


def other_bios_choices(g):
    # The CRCs of g's BIOS choices other than its default, the one the
    # emulator loads (the list marks it default="yes", or it is the first):
    # each a file with a bios= naming another choice. MAME 0.289 and
    # 2003-Plus ask for every other file, the default's and a BIOS file with
    # no bios= (PGM's) among them.
    sets = list(g.iter("biosset"))
    default = next((b.get("name") for b in sets if b.get("default") == "yes"),
                   sets[0].get("name") if sets else None)
    return {r.get("crc").lower() for r in g.iter("rom")
            if r.get("crc") and r.get("bios") and r.get("bios") != default}


def from_xml(path, is_bios, unasked, not_working, flag_of):
    games = {g.get("name"): g for g in ET.parse(path).getroot().iter("game")}

    def bios_crcs(g):
        # The entry at the end of the romof chain, when it is a BIOS: those
        # of its files the emulator does not ask for, unasked(bios).
        seen = set()
        while g is not None and g.get("romof") and g.get("name") not in seen:
            seen.add(g.get("name"))
            g = games.get(g.get("romof"))
        if g is None or not is_bios(g):
            return set()
        return unasked(g)

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


def fbneo_bios_options(drv, bios):
    # The CRCs of the BIOS bios's files that FBNeo's drivers mark optional
    # (BRF_OPT): Neo Geo's choices of BIOS and a few other boards'. Its
    # loader asks for every other file, a BIOS's included
    # (src/burner/libretro/libretro.cpp, its "is required" check): all six
    # of PGM's. A file no driver lists as a BIOS file stops the list.
    global bios_marks
    if bios_marks is None:
        bios_marks = {}
        entry = re.compile(r'\{\s*"([^"]+)"\s*,\s*(?:0x[0-9a-fA-F]+|\d+)\s*,\s*(0x[0-9a-fA-F]+)\s*,'
                           r'\s*([^}]*)\}')
        for root, _, names in os.walk(drv):
            for n in names:
                if not n.endswith((".cpp", ".h")):
                    continue
                for m in entry.finditer(open(os.path.join(root, n), encoding="latin-1").read()):
                    if "BRF_BIOS" in m.group(3):
                        key = (m.group(1).lower(), int(m.group(2), 16))
                        bios_marks.setdefault(key, set()).add("BRF_OPT" in m.group(3))
    out = set()
    for r in bios.iter("rom"):
        if not r.get("crc"):
            continue
        key = (r.get("name").lower(), int(r.get("crc"), 16))
        if key not in bios_marks:
            sys.exit("fbneo_libretro: %s's %s is in no driver's BIOS files; the drivers changed shape"
                     % (bios.get("name"), r.get("name")))
        # Optional only where every driver listing it says so.
        if bios_marks[key] == {True}:
            out.add(r.get("crc").lower())
    return out


bios_marks = None


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
    from_xml(paths[0], lambda g: g.get("isbios") == "yes",
             lambda b: fbneo_bios_options(paths[1], b), {"preliminary"},
             lambda g: flags.get(g.get("name").lower(), "-"))
elif core == "mame2003_plus":
    flags = catver_flags(paths[1])
    from_xml(paths[0], lambda g: g.get("runnable") == "no", other_bios_choices,
             {"preliminary", "protection"},
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
    def asked(m):
        # The files MAME asks for of machine or device m, as (CRC, size).
        files, others = [], other_bios_choices(m)
        for r in m.iter("rom"):
            crc = (r.get("crc") or "").lower()
            if (not crc or r.get("status") == "nodump" or r.get("optional") == "yes"
                    or crc in others):
                continue
            f2 = (int(crc, 16), int(r.get("size") or 0))
            if f2 not in files:
                files.append(f2)
        return files

    # A device's files (a sound chip's, an I/O board's) are listed under the
    # device, not under the machines holding it, and MAME asks for them too:
    # a first pass over the list for them. A machine's device_refs name every
    # device in it, a device's own devices included.
    device_files = {d.get("name"): asked(d) for d in mame_machines(paths[0], devices=True)}
    if sum(1 for f in device_files.values() if f) < 500:
        sys.exit("mame_libretro: only %d devices with files; the list changed shape"
                 % sum(1 for f in device_files.values() if f))
    marks = {"x": 0, "g": 0}
    for m in mame_machines(paths[0]):
        files = asked(m)
        for d in m.iter("device_ref"):
            for f2 in device_files.get(d.get("name"), []):
                if f2 not in files:
                    files.append(f2)
        discs = [(d.get("name"), (d.get("sha1") or "").lower()) for d in m.iter("disk")
                 if d.get("status") != "nodump" and d.get("optional") != "yes"]
        flag = mame_flag(m)
        if flag != "-":
            marks[flag] += 1
        buttons = max([int(c.get("buttons") or 0) for c in m.iter("control")
                       if c.get("player") in (None, "1")] or [0])
        driver = m.find("driver")
        works = driver is None or driver.get("status") != "preliminary"
        line(m.get("name"), works, flag, files, discs, buttons)
    if marks["x"] < 5000 or marks["g"] < 400:
        sys.exit("mame_libretro: only %d games marked not for a controller and %d light-gun "
                 "games; the list changed shape" % (marks["x"], marks["g"]))
elif core == "sm2emu":
    # Its comments hold "--", which an XML parser refuses; sm2-emu's own
    # (pugixml) does not mind. Nothing it reads is in one.
    text = re.sub(r"<!--.*?-->", "", open(paths[0], encoding="utf-8").read(), flags=re.S)
    games = {g.get("name"): g for g in ET.fromstring(text).iter("game")}

    def chain(g):
        # The game, its parent, its parent's parent.
        out = []
        while g is not None and all(x is not g for x in out):
            out.append(g)
            g = games.get(g.get("parent")) if g.get("parent") else None
        return out

    guns = 0
    for name, g in sorted(games.items()):
        c = chain(g)
        regions = {}
        for x in reversed(c):
            # A region the clone names replaces its parent's whole.
            for r in x.iter("region"):
                regions[r.get("name")] = r
        files = []
        for r in regions.values():
            if r.get("required") == "false":
                continue
            for f in r.iter("file"):
                crc = (f.get("crc") or "").lower().removeprefix("0x")
                if crc and (int(crc, 16), 0) not in files:
                    files.append((int(crc, 16), 0))
        gun = any(x.find("lightgun") is not None for x in c)
        guns += gun
        inputs = next(([i.get("type") for i in x.iter("input")] for x in c
                       if x.find("inputs") is not None), [])
        line(name, not any(x.get("preliminary") == "true" for x in c), "g" if gun else "-",
             files, [], 3 if "buttons3" in inputs else 0)
    if guns < 3:
        sys.exit("sm2emu: only %d light-gun games; the list changed shape" % guns)
elif core == "supermodel":
    text = re.sub(r"<!--.*?-->", "", open(paths[0], encoding="utf-8").read(), flags=re.S)
    # Model 3 only: its list also defines the Real3D Pro-1000, a graphics
    # board, which is no game.
    games = {g.get("name"): g for g in ET.fromstring(text).iter("game")
             if g.findtext("hardware/platform") == "Sega Model 3"}
    marks = {}
    for m in mame_machines(paths[1]):
        if m.get("name") in games:
            marks[m.get("name")] = mame_flag(m)

    def regions(g):
        # Its regions by name, each a list of (offset, crc), as its loader
        # builds them: a clone's region replaces the parent's file at each
        # offset it names and adds the rest; a region only the clone has is
        # added whole. One level of parent, which is all it allows.
        out = {}
        parent = games.get(g.get("parent")) if g.get("parent") else None
        for x in ([parent] if parent is not None else []) + [g]:
            for r in x.iter("region"):
                if r.get("required") == "false":
                    out.pop(r.get("name"), None)
                    continue
                files = out.setdefault(r.get("name"), {})
                for f in r.iter("file"):
                    files[int(f.get("offset"), 0)] = int(f.get("crc32"), 16)
        return out

    for name, g in sorted(games.items()):
        files = []
        for r in regions(g).values():
            for crc in r.values():
                if (crc, 0) not in files:
                    files.append((crc, 0))
        inputs = {i.get("type") for i in g.iter("input")}
        if not inputs and g.get("parent") in games:
            inputs = {i.get("type") for i in games[g.get("parent")].iter("input")}
        buttons = 4 if inputs & {"fighting", "spikeout"} else 3 if "soccer" in inputs else 0
        line(name, True, marks.get(name, "-"), files, [], buttons)
    if "g" not in marks.values():
        sys.exit("supermodel: MAME 0.289 marks none of its sets a light-gun game; "
                 "a list changed shape")
elif core == "xemu-chihiro":
    # WHICH MAME SET EACH OF THE FORK'S JVS PROFILES IS FOR: it names its
    # profiles after its cabinets (hw/xbox/chihiro/chihiro.c, chihiro_cabinets)
    # and its game names after MAME's parent sets (chihiro_game_names). Checked
    # below against both, so a profile or a game added at a new pin stops the
    # build until it is placed here.
    PROFILE_SETS = {
        "hotd3": ["hotd3"], "vc3": ["vcop3"], "gs": ["ghostsqu"], "ctx": ["crtaxihr"],
        "or2": ["outr2", "outr2st"], "ok": ["ollie"], "wmmt2": ["wangmid", "wangmid2"],
        "gundam": ["gundamos"],
    }
    src = open(paths[0], encoding="utf-8").read()
    table = re.search(r"\} chihiro_game_names\[\] = \{(.*?)\n\};", src, re.S)
    if not table:
        sys.exit("xemu-chihiro: no chihiro_game_names table in chihiro.c")
    ids = {n: i for i, n in re.findall(r'\{\s*"([A-Z0-9]{4})",\s*"([a-z0-9_]+)"\s*\}',
                                       table.group(1))}
    if len(ids) < 14:
        sys.exit("xemu-chihiro: only %d games in chihiro_game_names" % len(ids))

    # config_spec.yml's keys by their indentation, enough to read the
    # profiles and which have a trigger, with no YAML reader on the runner.
    def keys_under(path):
        stack, out = [], []
        for l in open(paths[1], encoding="utf-8"):
            m = re.match(r"^( *)([A-Za-z0-9_]+):", l)
            if not m:
                continue
            stack = stack[:len(m.group(1)) // 2] + [m.group(2)]
            if stack[:-1] == path:
                out.append(m.group(2))
        return out

    spec = open(paths[1], encoding="utf-8").read()
    profiles = re.search(r"\n    profile:\n      type: enum\n      values: \[([a-z0-9_, ]+)\]", spec)
    if not profiles:
        sys.exit("xemu-chihiro: no chihiro.jvs.profile enum in config_spec.yml")
    profiles = [p.strip() for p in profiles.group(1).split(",")]
    if sorted(profiles) != sorted(PROFILE_SETS):
        sys.exit("xemu-chihiro: its profiles are %s, the table here %s"
                 % (sorted(profiles), sorted(PROFILE_SETS)))
    guns = set()
    for p, sets in PROFILE_SETS.items():
        for s in sets:
            if s not in ids:
                sys.exit("xemu-chihiro: %s, %s's set here, is not in chihiro_game_names" % (s, p))
        if "trigger" in keys_under(["chihiro", "jvs", p]):
            guns.update(sets)
    if guns != {"hotd3", "vcop3", "ghostsqu"}:
        sys.exit("xemu-chihiro: its light-gun profiles give %s" % sorted(guns))
    played = {s for sets in PROFILE_SETS.values() for s in sets}
    count = 0
    for m in mame_machines(paths[2]):
        if m.get("sourcefile") != "sega/chihiro.cpp":
            continue
        parent = m.get("cloneof") or m.get("name")
        image = (ids[parent], parent) if parent in ids else None
        line(m.get("name"), parent in played, "g" if parent in guns else "-", [], [],
             image=image)
        count += 1
    if count < 40:
        sys.exit("xemu-chihiro: MAME 0.289 gave only %d Chihiro sets" % count)
elif core == "linuxloader":
    # WHICH GAMES EACH PROGRAM IS, by the start of their names in the loader's
    # table (config.h's defines, gameData.c's rows): the loader names a game's
    # version, not the program it runs. Checked below against its programs,
    # its games and its list of original programs, so a program or a game
    # added at a new pin stops the build until it is placed here.
    PROGRAM_GAMES = {
        "a.elf": ["HUMMER_SBQN"], "abc": ["AFTER_BURNER_CLIMAX_"],
        "apacheM.elf": ["TOO_SPICY_"], "chopperM.elf": ["HARLEY_DAVIDSON_"],
        "drive.elf": ["SEGA_RACE_TV_"], "dsr": ["R_TUNED_"],
        "gsevo": ["GHOST_SQUAD_EVOLUTION_"], "hod4M.elf": ["THE_HOUSE_OF_THE_DEAD_4_"],
        "hodexRI.elf": ["THE_HOUSE_OF_THE_DEAD_EX_"], "hummer_Master.elf": ["HUMMER_EXTREME_"],
        "id4.elf": ["INITIALD_4_"], "id5.elf": ["INITIALD_5_"], "Jennifer": ["OUTRUN_2_SP_SDX_"],
        "lgj_final": ["LETS_GO_JUNGLE_SBLU"], "lgjsp_app": ["LETS_GO_JUNGLE_SBNR_SPECIAL"],
        "main.exe": ["PRIMEVAL_HUNT_"], "mj4": ["MJ4_"], "q2satl_lind": ["QUIZ_AXA_"],
        "ramboM.elf": ["RAMBO_"], "vf5": ["VIRTUA_FIGHTER_5_"],
        "vsg": ["GHOST_SQUAD_EVOLUTION_"], "vt3": ["VIRTUA_TENNIS_3_"],
        "vt3_Lindbergh": ["VIRTUA_TENNIS_3_"],
    }
    # NO GAME TO PLAY: Segaboot, the board's own menu, and the groups of the
    # test menus and of Initial D's server boxes.
    NOT_A_GAME = ("SEGABOOT_",)
    NOT_A_GAME_GROUPS = {"GROUP_ID_SERVERBOX"}
    KINDS = {"DRIVING": "-", "FLYING": "-", "DIGITAL": "-", "SHOOTING": "g", "MAHJONG": "x"}
    main, data, header, hook, exe, gpu = (open(p, encoding="utf-8").read() for p in paths)
    # WHAT THE CONSOLE LEANS ON, besides the tables (docs/ARCHITECTURE.md,
    # "What leans on other projects"): the fingerprint as the console takes
    # it (arcadematch.cpp, lindberghFingerprint), the game started through
    # the shell as the loader's child (standalone_lindbergh.cpp), and the
    # graphics chip's maker read from OpenGL, which the image's Mesa answers
    # "AMD" on the reference console.
    for what, text, needle in (
            ("mainL.c", hook, "info->dlpi_phdr[2].p_type == PT_LOAD"),
            ("mainL.c", hook, "info->dlpi_phdr[2].p_flags == 5"),
            ("mainL.c", hook, "info->dlpi_phdr[2].p_vaddr + 10), 0x4000)"),
            ("linuxExe.c", exe, "int sysCmd = system(command);"),
            ("gpuVendor.c", gpu, "glad_glGetString(GL_VENDOR)"),
            ("gpuVendor.c", gpu, 'strstr(config->GPUVendorString, "AMD")')):
        if needle not in text:
            sys.exit("linuxloader: %s no longer has '%s'" % (what, needle))
    games = re.search(r"char \*games\[\] = \{(.*?)\};", main, re.S)
    if not games:
        sys.exit("linuxloader: no games[] in mainShared.c")
    programs = re.findall(r'"([^"]+)"', games.group(1))
    if programs[-1] != "END":
        sys.exit("linuxloader: games[] does not end with END")
    programs = programs[:-1]
    if sorted(programs) != sorted(PROGRAM_GAMES):
        sys.exit("linuxloader: its programs are %s, the table here %s"
                 % (sorted(programs), sorted(PROGRAM_GAMES)))
    crcs = {n: int(v, 16) for n, v in re.findall(r"#define (\w+) (0x[0-9a-fA-F]+)", header)}
    rows = re.findall(r"\{(\w+), \"([^\"]*)\", \"([^\"]*)\", \"([^\"]*)\", [^{}]*?\b(WORKING|NOT_WORKING), "
                      r"\w+, (\w+), \d+, \d+, (-1|\w+),", data)
    if len(rows) < 80:
        sys.exit("linuxloader: only %d rows in gameData.c" % len(rows))
    print("!programs " + " ".join(programs))
    owner, kind_of = {}, {}
    for name, _title, _short, dvp, status, kind, group in rows:
        if name not in crcs:
            sys.exit("linuxloader: %s is in gameData.c and not in config.h" % name)
        if kind not in KINDS:
            sys.exit("linuxloader: %s is a %s game, a kind the table here does not know"
                     % (name, kind))
        if name.startswith(NOT_A_GAME) or group in NOT_A_GAME_GROUPS or group.endswith("_TEST"):
            continue
        mine = [p for p, starts in PROGRAM_GAMES.items()
                if any(name.startswith(s) for s in starts)]
        if not mine:
            sys.exit("linuxloader: %s is no program's game in the table here" % name)
        for p in mine:
            owner.setdefault(p, set()).add(dvp)
            kind_of.setdefault(p, set()).add(KINDS[kind])
        line("%08x" % crcs[name], status == "WORKING", KINDS[kind], [], [])
    # The loader's own list of original programs names each one's version:
    # every one of those its table of games also names must be that
    # program's here.
    named = {dvp for ds in owner.values() for dvp in ds}
    for dvp, prog in re.findall(r"// (DVP-\w+)\s*\|\s*([\w.]+)", main):
        if prog in PROGRAM_GAMES and dvp in named and dvp not in owner.get(prog, set()):
            sys.exit("linuxloader: its originals list %s as %s, which the table here does "
                     "not give that program" % (dvp, prog))
    for p in programs:
        kinds = kind_of.get(p, set())
        if len(kinds) != 1:
            sys.exit("linuxloader: %s's games are of %s kinds" % (p, sorted(kinds) or "no"))
        line(p, True, kinds.pop(), [], [])
elif core == "windowsarcadeloader":
    wal, winetricks = paths
    # THE PROFILES' KEYS, read by the few forms the profiles use at the pin: a
    # top-level `key: value` or `key: [a, b]` (a flow list, on one line or
    # more), and input's own keys one level in. No YAML library: neither
    # this Mac's Python nor CI's has one. A form not read here stops the
    # build, so a profile written another way is seen, never misread.
    TOP = ("name", "system", "prefix", "prefix_archive", "prefix_tricks", "tricks", "runner")
    INPUT = ("guns_enabled", "touch", "mouse_wrap")

    def uncomment(text):
        out, quote = [], None
        for i, c in enumerate(text):
            if quote:
                quote = None if c == quote else quote
            elif c in "\"'":
                quote = c
            elif c == "#" and (i == 0 or text[i - 1] in " \t"):
                break
            out.append(c)
        return "".join(out).rstrip()

    def unquote(v):
        v = v.strip()
        return v[1:-1] if len(v) >= 2 and v[0] == v[-1] and v[0] in "\"'" else v

    # A GAME'S PICTURE OPTIONS: the options its profile declares for the
    # GUI (options: groups, each with options: items) whose key ends
    # RESOLUTION or RENDER_SCALE. Read in the one form the profiles use: an
    # item `- key: <KEY>` with its `kind:`, a one-line `choices: [...]` for a
    # choice, `min:` and `max:` for a number, at the item's indent.
    PICTURE_KEY = r"\bkey: *[\"']?[A-Z0-9_]*(RESOLUTION|RENDER_SCALE)\b"
    VALUE = r"[A-Za-z0-9.]+"

    def picture_options(path, lines):
        found, i = [], 0
        while i < len(lines):
            m = re.match(r"^(\s*)- key: *[\"']?([A-Z0-9_]+)[\"']?$", lines[i])
            i += 1
            if not m or not re.search(r"(RESOLUTION|RENDER_SCALE)$", m.group(2)):
                continue
            item, indent = {}, len(m.group(1)) + 2
            while i < len(lines):
                n = re.match(r"^(\s*)(- )?([a-z_]+):(.*)$", lines[i])
                if not n or n.group(2) or len(n.group(1)) < indent:
                    break
                if len(n.group(1)) == indent:
                    item[n.group(3)] = n.group(4).strip()
                i += 1
            kind = item.get("kind")
            if kind == "choice":
                v = item.get("choices", "")
                if not re.fullmatch(r"\[[^\[\]]+\]", v):
                    sys.exit("windowsarcadeloader: %s's %s has no one-line choices" % (path, m.group(2)))
                choices = [unquote(x) for x in v[1:-1].split(",")]
                if not all(re.fullmatch(VALUE, c) for c in choices):
                    sys.exit("windowsarcadeloader: %s's %s choices %r" % (path, m.group(2), choices))
                found.append((m.group(2), choices, None))
            elif kind == "number":
                lo, hi = item.get("min", ""), item.get("max", "")
                if not (re.fullmatch(r"-?\d+", lo) and re.fullmatch(r"-?\d+", hi)):
                    sys.exit("windowsarcadeloader: %s's %s has no whole min and max" % (path, m.group(2)))
                found.append((m.group(2), None, (int(lo), int(hi))))
            else:
                sys.exit("windowsarcadeloader: %s's %s is of kind %r, not choice or number"
                         % (path, m.group(2), kind))
        return found

    def profile(path):
        lines = open(path, encoding="utf-8").read().split("\n")
        keys, inp, notes, top = {}, {}, [], None
        env, options = {}, []
        comments = " ".join(l.strip()[1:] for l in lines if l.strip().startswith("#"))
        # Its opening comment, before the first key: what the game is.
        head = []
        for l in lines:
            if l.startswith("#"):
                head.append(l[1:].strip())
            elif l.strip():
                break
        i = 0
        while i < len(lines):
            raw = lines[i]
            l = uncomment(raw)
            if "<<:" in l:
                sys.exit("windowsarcadeloader: %s merges a YAML anchor, a form not read here" % path)
            m = re.match(r"^([A-Za-z_]\w*):(.*)$", l)
            if m:
                top, v = m.group(1), m.group(2).strip()
                if top == "input" and v:
                    sys.exit("windowsarcadeloader: %s's input is a flow map, a form not read here" % path)
                if top in TOP:
                    if v.startswith("["):
                        while "]" not in v:
                            i += 1
                            v += " " + uncomment(lines[i]).strip()
                        if not re.fullmatch(r"\[[^\[\]{}]*\]", v):
                            sys.exit("windowsarcadeloader: %s's %s is not a flat list" % (path, top))
                        keys[top] = [unquote(x) for x in v[1:-1].split(",") if x.strip()]
                    elif not v or v[0] in "&*|>{-":
                        sys.exit("windowsarcadeloader: %s's %s is in a form not read here" % (path, top))
                    else:
                        keys[top] = unquote(v)
            else:
                m = re.match(r"^(\s+)([A-Za-z_]\w*):(.*)$", l)
                if m and m.group(2) in INPUT:
                    if top != "input" or m.group(1) != "  ":
                        sys.exit("windowsarcadeloader: %s has %s outside input's first level"
                                 % (path, m.group(2)))
                    v = m.group(3).strip()
                    if v not in ("true", "false"):
                        sys.exit("windowsarcadeloader: %s's %s is %r, not true or false"
                                 % (path, m.group(2), v))
                    inp[m.group(2)] = v == "true"
                elif m and m.group(2) in TOP[2:]:
                    sys.exit("windowsarcadeloader: %s has %s indented" % (path, m.group(2)))
                elif m and top == "env" and m.group(1) == "  ":
                    env[m.group(2)] = unquote(m.group(3))
                if top == "options" and l.strip():
                    options.append(l)
                elif re.search(PICTURE_KEY, l):
                    sys.exit("windowsarcadeloader: %s names a picture option outside options:" % path)
                elif top == "input" and raw.strip().startswith("#"):
                    notes.append(raw.strip()[1:])
            i += 1
        keys["env"], keys["options"] = env, picture_options(path, options)
        return keys, inp, " ".join(head), " ".join(notes), comments

    launcher = os.path.join(wal, "crates/launcher/src")
    defaults, default_input, _, _, _ = profile(os.path.join(launcher, "defaults.yaml"))
    if defaults.get("prefix") != "wine-prefix/full" or len(defaults.get("prefix_tricks", [])) < 30:
        sys.exit("windowsarcadeloader: defaults.yaml no longer gives the shared prefix and its verbs")
    if "WAL_VOLUME" not in defaults["env"]:
        sys.exit("windowsarcadeloader: defaults.yaml's env was not read")
    if any(default_input.get(k) is not False for k in INPUT):
        sys.exit("windowsarcadeloader: defaults.yaml no longer sets %s false" % ", ".join(INPUT))

    # WORKS: the loader's own status table (docs/IMPLEMENTATION.md, "Games
    # status", one row per game id, the one tools/gamelist.py makes README's
    # list from). Its status column is the loader's own words. A game works
    # when that column starts "works" (in every form: "works (user: 100%)",
    # "works fullscreen", "works headless") or "user" (a player's own report
    # of play: "user 100%", "user ok", "user-tested", "user: perfect"). Not
    # working: reached play in tests or by hand but not marked working ("in
    # game", "in fight", "headless tests"), "in progress", "boots", "title",
    # "not running", and a profile with no row. Every status must start in
    # one of those forms; another stops the build, to be read.
    doc = open(os.path.join(wal, "docs/IMPLEMENTATION.md"), encoding="utf-8").read().split("\n")
    status, on = {}, False
    for l in doc:
        if l.startswith("| Game id | Game | System | Result |"):
            on = True
            continue
        if on and l.startswith("|---"):
            continue
        if on and not l.startswith("| `"):
            if status:
                break
            continue
        if on:
            cells = [c.strip() for c in l.strip().strip("|").split(" | ")]
            status[cells[0].strip("`")] = cells[3]
    if len(status) < 400:
        sys.exit("windowsarcadeloader: only %d rows in its status table" % len(status))
    for gid, st in status.items():
        if not re.match(r"(works|user|in (game|fight|progress)|headless|boots|title|not running)\b", st):
            sys.exit("windowsarcadeloader: %s's status %r starts in a form not read here" % (gid, st[:60]))

    # THE MARKS, from what the profiles mark plainly, in this order: x when
    # its input reads a touch panel or slider (input.touch) or a trackball or
    # spinner through the mouse (input.mouse_wrap); x when its name or its
    # opening comment says what the cabinet is played with or is: a mahjong
    # panel, a medal or slot machine, a coin pusher, a drum, dance or guitar
    # game, a trackball or a spinner. A profile with guns_enabled (the gun or
    # mouse as a pointer) is g when its status row counts its guns ("2
    # guns"), x when its opening comment or its input's comments say the
    # pointer is a touch screen, g when any of its comments or its status
    # row say gun ("Direct3D 9 lightgun game"), and x when nothing names
    # the pointer. Every other game is played on a controller.
    WORDS = (r"\b(mahjong|medal|slot machine|coin pusher|drum|dance game|guitar|trackball"
             r"|spinner)")

    def mark(keys, inp, head, notes, comments, st):
        what = (keys.get("name", "") + " " + head).lower()
        if inp.get("touch") or inp.get("mouse_wrap") or re.search(WORDS, what):
            return "x"
        if not inp.get("guns_enabled"):
            return "-"
        if re.search(r"\b\d+ (light)?guns?\b", st.lower()):
            return "g"
        if re.search(r"\btouch", what + " " + notes.lower()):
            return "x"
        said = (comments + " " + st).lower()
        return "g" if re.search(r"\bguns?\b|\blight ?guns?\b|\bgunners?\b", said) else "x"

    games = {}
    root = os.path.join(wal, "systemprofiles")
    for system in sorted(os.listdir(root)):
        if not os.path.isdir(os.path.join(root, system)):
            continue
        for f in sorted(os.listdir(os.path.join(root, system))):
            if not f.endswith(".yaml"):
                continue
            gid = f[:-5]
            if gid in games:
                sys.exit("windowsarcadeloader: %s is in two systems, which the loader refuses" % gid)
            if not re.fullmatch(r"[a-z0-9][a-z0-9-]*", gid):
                sys.exit("windowsarcadeloader: the game id %r is not in the form read here" % gid)
            keys, inp, head, notes, comments = profile(os.path.join(root, system, f))
            # Its board's payload; not always its folder's (Battle Fantasia's
            # NESiCA release is a Type X game).
            if not re.fullmatch(r"[a-z0-9]+", keys.get("system", "")):
                sys.exit("windowsarcadeloader: %s/%s names no system" % (system, f))
            merged = dict(default_input, **inp)
            # THE VERBS prepare installs (main.rs): its prefix_tricks, then
            # its tricks not among them, in the prefix it names.
            prefix_tricks = keys.get("prefix_tricks", defaults["prefix_tricks"])
            verbs = prefix_tricks + [t for t in keys.get("tricks", []) if t not in prefix_tricks]
            prefix = keys.get("prefix", defaults["prefix"])
            if not re.fullmatch(r"wine-prefix/[a-z0-9][a-z0-9_-]*", prefix) or not verbs:
                sys.exit("windowsarcadeloader: %s's prefix %r or its verbs are not read here" % (gid, prefix))
            for v in verbs:
                if not re.fullmatch(r"[a-z0-9_]+", v):
                    sys.exit("windowsarcadeloader: %s's verb %r" % (gid, v))
            st = status.get(gid, "")
            # Each picture option, with the value the merged profile's env
            # gives it (defaults.yaml's env, then the profile's).
            menv = dict(defaults["env"], **keys["env"])
            words = []
            for key, choices, span in keys["options"]:
                d = menv.get(key)
                if d is None or not re.fullmatch(VALUE, d):
                    sys.exit("windowsarcadeloader: %s's env gives %s no default" % (gid, key))
                if choices is not None and d not in choices:
                    sys.exit("windowsarcadeloader: %s's %s default %s is not a choice" % (gid, key, d))
                if span is not None and not (re.fullmatch(r"-?\d+", d) and span[0] <= int(d) <= span[1]):
                    sys.exit("windowsarcadeloader: %s's %s default %s is out of range" % (gid, key, d))
                words.append("%%%s=%s:%s" % (key, d, "|".join(choices) if choices is not None
                                             else "%d-%d" % span))
            games[gid] = (bool(re.match(r"(works|user)\b", st)), mark(keys, merged, head, notes, comments, st),
                          prefix.split("/")[-1], verbs, words)
    # The runner's folder, the same for every game (defaults.yaml's runner).
    runner = defaults.get("runner", "")
    if not isinstance(runner, str) or not re.fullmatch(r"[A-Za-z0-9._-]+", runner):
        sys.exit("windowsarcadeloader: defaults.yaml names no runner")
    print("!runner " + runner)
    for gid, (works, flag, prefix, verbs, words) in sorted(games.items()):
        print(" ".join(["%s %s %s ~%s:%s" % (gid, "w" if works else "n", flag, prefix, ",".join(verbs))]
                       + words))

    # THE FILES EACH VERB DOWNLOADS, from winetricks' own source: its
    # load_<verb>, every helper that body calls and every verb it w_calls,
    # each w_download (into the verb's own cache folder, W_PACKAGE, which a
    # w_call sets to the verb called) and w_download_to (into the folder it
    # names), the file named by its URL unless the call names it. The whole
    # body is read, both architectures' branches, since the prefix is 64-bit,
    # and the fallbacks: 7-Zip for Windows, which w_try_7z and w_try_unzip
    # fetch only when the image's 7z or unzip is missing or fails.
    # dotnet11 is the launcher's own (wine.rs, dotnet11.rs), its file read
    # from there: winetricks' cache folder dotnet11/, its URL and SHA-256.
    wt = open(winetricks, encoding="utf-8").read().split("\n")
    funcs, name = {}, None
    for l in wt:
        m = re.match(r"^([A-Za-z_]\w*)\(\)\s*\{?\s*$", l)
        if m:
            name = m.group(1)
            funcs[name] = []
        elif name and l.startswith("}"):
            name = None
        elif name and not l.strip().startswith("#"):
            funcs[name].append(l)
    for need in ("w_download", "w_download_to", "w_call"):
        if need not in funcs:
            sys.exit("windowsarcadeloader: winetricks has no %s()" % need)
    # w_download_to <folder> <url> <sha256> [file]; w_download <url> <sha256>
    # [file], into "${W_PACKAGE}" (winetricks' own definitions, asserted).
    if not any(re.search(r'_W_packagename="\$1"', l) for l in funcs["w_download_to"]) or \
            not any('w_download_to "${W_PACKAGE}" "$@"' in l for l in funcs["w_download"]) or \
            not any('_W_file=$(basename "${_W_url}")' in l for l in funcs["w_download_to"]):
        sys.exit("windowsarcadeloader: winetricks' w_download_to or w_download changed form")
    PLUMBING = {"w_download", "w_download_to", "w_call", "w_do_call"}
    calls_cache = {}

    def downloads(fname):
        # Whether a function downloads, or calls one that does.
        if fname not in calls_cache:
            calls_cache[fname] = False
            body = " ".join(funcs[fname])
            calls_cache[fname] = bool(re.search(r"\bw_(download|download_to|call)\b", body)) or any(
                downloads(w) for w in set(re.findall(r"\b([A-Za-z_]\w*)\b", body))
                if w in funcs and w not in PLUMBING and w != fname)
        return calls_cache[fname]

    def words(rest):
        return [unquote(w) for w in re.findall(r'"[^"]*"|\S+', rest)]

    def walk(fname, package, seen, out):
        if (fname, package) in seen:
            return
        seen.add((fname, package))
        for l in funcs[fname]:
            for m in re.finditer(r"\bw_download_to\s+(.*)$", l):
                a = words(m.group(1))
                out.append((a[0], a[1], a[2], a[3] if len(a) > 3 and not a[3].startswith(("$", "#", ";", "|", "&")) else None))
            for m in re.finditer(r"\bw_download\s+(?!_to)(.*)$", l):
                a = words(m.group(1))
                out.append((package, a[0], a[1], a[2] if len(a) > 2 and not a[2].startswith(("$", "#", ";", "|", "&")) else None))
            for m in re.finditer(r"\bw_call\s+([a-z0-9_]+)", l):
                if "load_" + m.group(1) not in funcs:
                    sys.exit("windowsarcadeloader: winetricks has no load_%s" % m.group(1))
                walk("load_" + m.group(1), m.group(1), seen, out)
            for w in set(re.findall(r"\b([A-Za-z_]\w*)\b", l)):
                if w in funcs and w not in PLUMBING and w != fname and not w.startswith("load_") and downloads(w):
                    walk(w, package, seen, out)

    dotnet11 = open(os.path.join(launcher, "wine/dotnet11.rs"), encoding="utf-8").read()
    wine_rs = open(os.path.join(launcher, "wine.rs"), encoding="utf-8").read()
    own = re.findall(r'verb == "([a-z0-9_]+)"', wine_rs)
    if own != ["dotnet11"]:
        sys.exit("windowsarcadeloader: the launcher installs %s itself, the code here reads dotnet11" % own)
    url = re.search(r'^const URL: &str = "([^"]+)";$', dotnet11, re.M)
    sha = re.search(r'^const SHA256: &str = "([0-9a-f]{64})";$', dotnet11, re.M)
    if not (url and sha and '.join("winetricks/dotnet11");' in dotnet11
            and 'let file = cache.join("dotnetfx.exe");' in dotnet11):
        sys.exit("windowsarcadeloader: dotnet11.rs no longer names its file as read here")
    rows = set()
    for verb in sorted({v for g in games.values() for v in g[3]}):
        if verb == "dotnet11":
            found = [("dotnet11", url.group(1), sha.group(1), "dotnetfx.exe")]
        else:
            if "load_" + verb not in funcs:
                sys.exit("windowsarcadeloader: winetricks has no verb %s" % verb)
            found = []
            walk("load_" + verb, verb, set(), found)
        for folder, u, s, f in found:
            f = f or u.rsplit("/", 1)[-1]
            for what, value in (("folder", folder), ("URL", u), ("checksum", s), ("file", f)):
                if "$" in value or " " in value:
                    sys.exit("windowsarcadeloader: %s's %s %r is not plain text" % (verb, what, value))
            if not re.fullmatch(r"[0-9a-f]{64}", s) or not u.startswith("https://") or "/" in f:
                sys.exit("windowsarcadeloader: %s downloads %s %s, a form not read here" % (verb, u, s))
            rows.add((verb, folder + "/" + f, s, u))
    seen_path = {}
    for verb, path, s, u in rows:
        if seen_path.setdefault(path, (s, u)) != (s, u):
            sys.exit("windowsarcadeloader: two files at %s in winetricks' cache" % path)
    with open(os.environ["WAL_FILES_OUT"], "w", encoding="utf-8") as out:
        # Its first line is the loader's pin alone: the admin's script reads it
        # (tools/windows-arcade-admin.sh) to fetch the loader at the same commit.
        out.write("# windowsarcadeloader %s\n"
                  "# Each verb's downloads, from winetricks at %s and the loader at\n"
                  "# that commit (cores/arcade-sets.sh). Made, never typed:\n"
                  "# <verb> <cache folder>/<file> <sha256> <url>\n"
                  % (os.environ["WAL_COMMIT"], hashlib.sha256(open(winetricks, "rb").read()).hexdigest()))
        for r in sorted(rows):
            out.write(" ".join(r) + "\n")
    print("! %d profiles, %d verbs, %d files (%d lines)" % (
        len(games), len({v for g in games.values() for v in g[3]}), len(seen_path), len(rows)),
        file=sys.stderr)
PY
} > "${file}"

# CHECKED, NOT ASSUMED: a list that changed shape would otherwise give an
# empty or garbled catalogue and a console that greys every arcade game.
count=$(grep -vc '^[#!]' "${file}")
(( count >= LEAST )) ||
    { echo "${CORE}: only ${count} sets read, fewer than ${LEAST}" >&2; exit 1; }
FORM='^[a-z0-9_.]+ [wn] [-gx]( b[0-9]+)?( [0-9a-f]{8}:[0-9]+)*( @[^ :]+:[0-9a-f]*)*( =[A-Z0-9]{4}:[a-z0-9_]+)?$'
# windowsarcadeloader's: a game id (dashes in it), then its Windows setup.
[[ "${CORE}" == windowsarcadeloader ]] &&
    FORM='^[a-z0-9][a-z0-9-]* [wn] [-gx] ~[a-z0-9][a-z0-9_-]*:[a-z0-9_]+(,[a-z0-9_]+)*( %[A-Z0-9_]+=[A-Za-z0-9.]+:([A-Za-z0-9.]+(\|[A-Za-z0-9.]+)*|-?[0-9]+--?[0-9]+))*$'
bad=$(grep -v '^[#!]' "${file}" | grep -vcE "${FORM}" || true)
(( bad == 0 )) || { echo "${CORE}: ${bad} lines are not in the catalogue's form" >&2; exit 1; }
if [[ "${CORE}" == windowsarcadeloader ]]; then
    files=$(grep -vc '^#' "${WAL_FILES_OUT}")
    bad=$(grep -v '^#' "${WAL_FILES_OUT}" |
        grep -vcE '^[a-z0-9_]+ [a-z0-9_]+/[^ /]+ [0-9a-f]{64} https://[^ ]+$' || true)
    (( files >= 25 && bad == 0 )) ||
        { echo "${CORE}: ${WAL_FILES_OUT} has ${files} lines, ${bad} not in its form" >&2; exit 1; }
    echo "${CORE}: ${files} files the verbs download -> ${WAL_FILES_OUT}"
fi
empty=$(grep -v '^[#!]' "${file}" | grep -cE '^[a-z0-9_]+ [wn] [-gx]( b[0-9]+)?$' || true)
echo "${CORE}: ${count} sets (${empty} with nothing to check) at ${COMMIT} -> ${file}"
