#!/usr/bin/env bash
#
# Put xemu-chihiro, Tovarichtch's xemu fork that plays Sega Chihiro games,
# into the image, as cores/build-xemu-chihiro.sh built it at the pinned commit
# (the payload's xemu-chihiro/, from build-xemu-chihiro.yml), with its
# catalogue beside the cores:
#
#   /usr/lib/cabinetos/xemu-chihiro/bin/xemu-chihiro   the program, everything
#                                                      it reads compiled in
#   /usr/lib/cabinetos/cores/xemu-chihiro.catalogue    which sets it plays
#                                                      (frontend/src/arcadematch.h)
#
# WHY THE FORK, BUILT HERE: docs/decisions/2026-11-26-chihiro-in-the-xemu-fork.md;
# the reasons are in cores/build-xemu-chihiro.sh. In /usr, like Cemu, so it
# moves with the image and a console that has never reached the internet has
# Chihiro from its first boot. Its settings and saves go where the console
# tells it (standalone_chihiro.cpp), because /usr is read-only.
#
# Its licence is GPL v2; see docs/LICENCES.md.

set -euo pipefail
source /ctx/lib.sh

readonly SRC=/ctx/payload/xemu-chihiro
readonly DEST=/usr/lib/cabinetos/xemu-chihiro
readonly CORES=/usr/lib/cabinetos/cores

log "xemu-chihiro $(cut -c1-10 "${SRC}/VERSION")"
# A FILE, NOT AN EXECUTABLE, IN THE PAYLOAD: GitHub's artifact upload does not
# keep permissions (install-cemu.sh), so the execute bit is set below.
for f in bin/xemu-chihiro xemu-chihiro.catalogue VERSION; do
    [[ -s "${SRC}/${f}" ]] || { log "ERROR: no ${f} in the payload at ${SRC}"; exit 1; }
done

rm -rf "${DEST}"
mkdir -p "${DEST}" "${CORES}"
cp -R "${SRC}/bin" "${SRC}/VERSION" "${DEST}/"
install -m 0644 "${SRC}/xemu-chihiro.catalogue" "${CORES}/"

# READABLE BY EVERYONE: the console runs as `cabinet` (install-cemu.sh).
chmod -R u+rwX,go+rX,go-w "${DEST}"
chmod 0755 "${DEST}/bin/xemu-chihiro"
shut="$(find "${DEST}" ! -perm -o+r | head -5)"
if [[ -n "${shut}" ]]; then
    log "ERROR: files in ${DEST} that the console's user cannot read:"
    echo "${shut}"
    exit 1
fi

# EVERY LIBRARY IT NEEDS RESOLVES against the image itself
# (build-xemu-chihiro.sh checked the same in its builder). It cannot be
# started here: there is no display. The A9 is where it is seen to run.
libs="$(ldd "${DEST}/bin/xemu-chihiro")"
if grep -q 'not found' <<<"${libs}"; then
    log "ERROR: xemu-chihiro is missing libraries:"
    grep 'not found' <<<"${libs}"
    exit 1
fi
log "  installed in ${DEST} ($(du -sh "${DEST}" | cut -f1)), $(grep -vc '^#' "${CORES}/xemu-chihiro.catalogue") sets"
