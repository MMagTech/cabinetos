#!/usr/bin/env bash
#
# Put the console into the image: the frontend, the twenty-one libretro cores,
# and the system files that ship with one of them.
#
# ===========================================================================
#  THIS IS WHAT MAKES AN INSTALLED MACHINE A CONSOLE RATHER THAN A BLACK
#  SCREEN.
# ===========================================================================
#
# Before this existed the image carried the OS half and nothing else. Merge to
# main, the image rebuilt, bootc pulled it, and packages, system files and the
# session service all travelled — but the frontend and the cores did not,
# because they were never in it. `cabinetos-session` ran `sleep infinity`
# inside gamescope, so a freshly installed machine came up, took tty1, started
# a compositor, and drew nothing at all. Every emulator this console can run
# lived on one development VM and was compiled there by hand.
#
# WHERE EACH PART GOES, AND WHY THERE
#
#   /usr/bin/cabinetos-frontend        A program. Where programs go, and on the
#                                      PATH the session script uses.
#
#   /usr/lib/cabinetos/cores/          Architecture-specific shared objects, so
#                                      /usr/lib and not /usr/share. The
#                                      frontend falls back to this path when
#                                      there is no cores/build beside it — see
#                                      the resolution block in main.cpp — and
#                                      --core-dir still overrides both.
#
#   /usr/share/cabinetos/system/       PPSSPP's fonts, VFPU tables and per-game
#                                      compatibility list. DECIDED in
#                                      docs/PROJECT.md open question 18 and
#                                      unbuilt until now: storage::ensureTree
#                                      already symlinks whatever it finds here
#                                      into the console's system directory at
#                                      startup, leaving alone any name that is
#                                      already a real file — which is what
#                                      keeps a development machine working,
#                                      since there the same files sit in bios/
#                                      where the core build left them.
#
#                                      `/system` AND NOT /usr/share/cabinetos
#                                      ITSELF. That directory also holds a
#                                      DEVELOPMENT-IMAGE marker and two package
#                                      inventories, and an earlier version of
#                                      the link step put all three where a core
#                                      goes looking for its fonts.
#
# All three are under /usr, which on a bootc machine is replaced wholesale by
# every update. That is the entire point of doing this: it is what makes the
# frontend and the cores travel with the image the way the session service
# already does. Nothing here may go in /var — see
# system_files/usr/lib/tmpfiles.d/cabinetos.conf for what that trap looks like.

set -euo pipefail

source /ctx/lib.sh

PAYLOAD=/ctx/payload

group_start "Installing the frontend and the cores"

if [[ ! -d "${PAYLOAD}" ]]; then
    log "ERROR: ${PAYLOAD} is missing — ci/stage-image-payload.sh was not run"
    exit 1
fi

# --- The frontend ----------------------------------------------------------

install -D -m 0755 "${PAYLOAD}/bin/cabinetos-frontend" /usr/bin/cabinetos-frontend
log "installed /usr/bin/cabinetos-frontend ($(du -h /usr/bin/cabinetos-frontend | cut -f1))"

# --- The cores -------------------------------------------------------------

mkdir -p /usr/lib/cabinetos/cores
install -m 0644 "${PAYLOAD}"/cores/*.so /usr/lib/cabinetos/cores/

cores=$(find /usr/lib/cabinetos/cores -name '*.so' | wc -l)
log "installed ${cores} cores into /usr/lib/cabinetos/cores ($(du -sh /usr/lib/cabinetos/cores | cut -f1))"

# The count is checked again here, having already been checked against
# cores/build-core.sh before the image build started. Not redundant: what that
# script proved is that the staging directory was complete, and what this
# proves is that the contents of it reached the image. A copy that silently
# does nothing leaves a green build, which is the exact failure the
# system_files overlay in build.sh was written to catch after it happened.
if [[ "${cores}" -ne 21 ]]; then
    log "ERROR: expected 21 cores in the image, found ${cores}"
    exit 1
fi

# --- PPSSPP's system files -------------------------------------------------

mkdir -p /usr/share/cabinetos/system
cp -R "${PAYLOAD}/system/." /usr/share/cabinetos/system/
log "installed the core system files ($(du -sh /usr/share/cabinetos/system | cut -f1))"

# Named, not counted. Without compat.ini the core logs "Core system files
# missing, expect bugs" at a level nothing reads and then runs a game that
# renders no text — which is a working build, a green check, and a broken
# console.
if [[ ! -f /usr/share/cabinetos/system/PPSSPP/compat.ini ]]; then
    log "ERROR: PPSSPP's system files did not land — PSP would run with no fonts"
    exit 1
fi

group_end

# ---------------------------------------------------------------------------
# Every library, resolved against THIS image.
# ---------------------------------------------------------------------------
#
# The one check that is worth more than all the others here, because it is the
# only one that asks the question the console actually depends on: can the
# binary, and each of the twenty-one cores, find everything it links against in
# the image it is about to ship in?
#
# The frontend is compiled in a Fedora 44 container to match Bazzite 44, so the
# two SHOULD agree. They agree by construction and not by declaration, though:
# build_files/require-frontend-libs.sh names three libraries that are in the
# base incidentally, with nothing depending on them, and a Bazzite bump is free
# to drop any of them. This catches that, and it catches the same thing for the
# cores — whose dependencies nobody has ever written down. Flycast alone wants
# zlib, libzip, alsa and udev off the system.
#
# `ldd` on a missing dependency prints "=> not found" and still exits 0, which
# is why this reads the output rather than the status.
group_start "Resolving every library against the image"

unresolved=0
check_links() {
    local what="$1"
    local out
    out=$(ldd "${what}" 2>/dev/null | grep 'not found' || true)
    if [[ -n "${out}" ]]; then
        log "  UNRESOLVED  $(basename "${what}")"
        printf '%s\n' "${out}" | sed 's/^/      /'
        unresolved=1
    fi
}

check_links /usr/bin/cabinetos-frontend
for so in /usr/lib/cabinetos/cores/*.so; do
    check_links "${so}"
done

if [[ "${unresolved}" -ne 0 ]]; then
    log "ERROR: something in the image cannot find a library it links against."
    log "Install the package explicitly in Containerfile and add it to"
    log "build_files/require-frontend-libs.sh with the reason. Do not delete"
    log "this check: a console that cannot start its own frontend is not a"
    log "console."
    exit 1
fi

log "every library resolves: the frontend and all ${cores} cores"
group_end
