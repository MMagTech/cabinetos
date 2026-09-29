#!/usr/bin/env bash
#
# Put xemu's blank Xbox hard drive into the image, at
# /usr/share/cabinetos/xemu/xbox_hdd.qcow2. Every Xbox game gets a copy of it
# in its own folder the first time it is played (standalone.cpp, prepareXemu).
#
# WHICH DRIVE. xemu's own: an 8 GB drive, formatted, holding only the
# open-source xemu-dashboard (xemu-project/xemu-dashboard) and nothing of
# Microsoft's. MMagTech chose it over the older 2020 image, whose dashboard
# only says "insert a disc", 2026-09-29 (issue #172). The dashboard is never
# meant to be seen: a game that quits to it goes back to Home.
#
# IN THE IMAGE, not fetched on first boot: it is 1.6 MB, it is data rather
# than a program, and a console that never reaches the internet can still
# play Xbox games that are on its drive.
#
# PINNED THE WAY RPCS3 IS: a release URL and the file's sha256. The EEPROM
# beside it is not fetched; it is ours, in system_files, and it never changes
# (tools/xbox-eeprom.py says why).
#
# xemu-dashboard is MIT licensed; see docs/LICENCES.md.

set -euo pipefail
source /ctx/lib.sh

readonly DRIVE_RELEASE="v20260516-0955"
readonly DRIVE_URL="https://github.com/xemu-project/xemu-dashboard/releases/download/${DRIVE_RELEASE}/xbox_hdd.qcow2"
readonly DRIVE_SHA256="00d7df7a2bc235f8801764f00b7f40e194d1e392f7a9619d6b2396c89770f6dd"
readonly DEST=/usr/share/cabinetos/xemu

log "xemu's blank drive, xemu-dashboard ${DRIVE_RELEASE}"
mkdir -p "${DEST}"
for attempt in 1 2 3; do
    if curl -fsSL --retry 3 -o "${DEST}/xbox_hdd.qcow2" "${DRIVE_URL}"; then
        break
    fi
    log "  download attempt ${attempt} failed"
    [[ ${attempt} -eq 3 ]] && { log "ERROR: could not download xemu's drive"; exit 1; }
    sleep 5
done
got="$(sha256sum "${DEST}/xbox_hdd.qcow2" | cut -d' ' -f1)"
if [[ "${got}" != "${DRIVE_SHA256}" ]]; then
    log "ERROR: xemu's drive has checksum ${got}, expected ${DRIVE_SHA256}"
    exit 1
fi
[[ -f "${DEST}/eeprom.bin" && $(stat -c %s "${DEST}/eeprom.bin") -eq 256 ]] || {
    log "ERROR: no 256-byte eeprom.bin in ${DEST}; it comes from system_files"
    exit 1
}
chmod 0644 "${DEST}/xbox_hdd.qcow2" "${DEST}/eeprom.bin"
log "  installed in ${DEST}"
