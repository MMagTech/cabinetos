#!/usr/bin/env bash
#
# Put Cemu, the Wii U emulator, into the image at /usr/lib/cabinetos/cemu:
# the program and the two folders it reads, as cores/build-cemu.sh built them
# at the pinned commit (the payload's cemu/, from build-cemu.yml).
#
# WHY CEMU MAIN, BUILT HERE: decided with MMagTech 2026-10-01, issue #174,
# docs/PROJECT.md open question 36; the reasons are in cores/build-cemu.sh.
# In /usr, like RPCS3 and Xenia, so it moves with the image and a console that
# has never reached the internet has Wii U from its first boot.
#
# LAID OUT AS AN INSTALL PREFIX: bin/Cemu, and share/Cemu/{gameProfiles,
# resources}, where Cemu looks from where it is (wxStandardPaths::GetDataDir).
# Its settings, log, shader cache and saves go where the console tells it
# (standalone.cpp, prepareCemu), because /usr is read-only.
#
# Its licence is MPL-2.0; see docs/LICENCES.md.

set -euo pipefail
source /ctx/lib.sh

readonly SRC=/ctx/payload/cemu
readonly DEST=/usr/lib/cabinetos/cemu

log "Cemu $(cut -c1-10 "${SRC}/VERSION")"
[[ -x "${SRC}/bin/Cemu" ]] || { log "ERROR: no Cemu in the payload at ${SRC}"; exit 1; }

rm -rf "${DEST}"
mkdir -p "${DEST}"
cp -R "${SRC}/bin" "${SRC}/share" "${SRC}/VERSION" "${DEST}/"

# READABLE BY EVERYONE: the console runs as `cabinet`. Xenia's first testing
# image shipped a folder only root could read (install-xenia.sh); checked the
# same way here.
chmod -R u+rwX,go+rX,go-w "${DEST}"
chmod 0755 "${DEST}/bin/Cemu"
shut="$(find "${DEST}" ! -perm -o+r | head -5)"
if [[ -n "${shut}" ]]; then
    log "ERROR: files in ${DEST} that the console's user cannot read:"
    echo "${shut}"
    exit 1
fi

# EVERY LIBRARY IT NEEDS RESOLVES against the image itself. It cannot be
# started here: it opens its window first, and there is no display. The A9 is
# where it is seen to run.
missing="$(ldd "${DEST}/bin/Cemu" | grep 'not found' || true)"
if [[ -n "${missing}" ]]; then
    log "ERROR: Cemu is missing libraries:"
    echo "${missing}"
    exit 1
fi
log "  installed in ${DEST} ($(du -sh "${DEST}" | cut -f1))"
