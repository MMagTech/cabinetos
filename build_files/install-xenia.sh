#!/usr/bin/env bash
#
# Put Xenia Edge, the Xbox 360 emulator, into the image: its developer's own
# Linux build, unpacked into /usr/lib/cabinetos/xenia.
#
# WHY EDGE. It is a fork of Xenia Canary whose developers build and recommend
# a native Linux version rather than Canary under Wine, and Batocera's default
# for Xbox 360. Measured on the A9 2026-09-29: Forza Horizon 2 and Left 4 Dead
# 2 held their native 30 FPS, the GPU under 40%. docs/PROJECT.md, open
# question 34; issue #192.
#
# WHY NOT FLATHUB: it is not there. In /usr, like RPCS3, so it moves with the
# image and a console that has never reached the internet has Xbox 360 from
# its first boot.
#
# PINNED THE WAY RPCS3 IS. Edge publishes a release for nearly every commit
# (has207/xenia-edge, tagged with the short SHA) and has no stable channel, so
# the URL names one exact build and the checksum makes the build fail rather
# than ship anything else. GitHub's own digest for the asset is the same
# sha256. To move it: pick a release, put its tag and the sha256 of its
# AppImage here, try it on the A9, and say why in the commit. Never patched.
#
# UNPACKED, NOT RUN AS AN APPIMAGE, as RPCS3 is: `usr/bin/xenia_edge` runs
# directly, finds its bundled libraries through its own RUNPATH
# ($ORIGIN/../lib), and is a plain process called `xenia_edge` the console can
# find and close. Measured on the A9 2026-09-29: 19 MB downloaded, 53 MB
# unpacked, no library missing against the image, run exactly this way.
#
# Its licence is BSD-3-Clause; see docs/LICENCES.md.

set -euo pipefail
source /ctx/lib.sh

readonly XENIA_VERSION="a7c39fa"
readonly XENIA_URL="https://github.com/has207/xenia-edge/releases/download/${XENIA_VERSION}/xenia_edge_linux.AppImage"
readonly XENIA_SHA256="5966be6ee2e1e438508d9d6a752e8d6e2a248019531e1db64701e0cc81ce668c"
readonly DEST=/usr/lib/cabinetos/xenia

log "Xenia Edge ${XENIA_VERSION}"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

# A retry, because GitHub's release CDN occasionally drops a connection
# mid-file and a build that fails for that has nothing wrong with it.
for attempt in 1 2 3; do
    if curl -fsSL --retry 3 -o "${work}/xenia.AppImage" "${XENIA_URL}"; then
        break
    fi
    log "  download attempt ${attempt} failed"
    [[ ${attempt} -eq 3 ]] && { log "ERROR: could not download Xenia Edge"; exit 1; }
    sleep 5
done

got="$(sha256sum "${work}/xenia.AppImage" | cut -d' ' -f1)"
if [[ "${got}" != "${XENIA_SHA256}" ]]; then
    log "ERROR: Xenia Edge's checksum is ${got}, expected ${XENIA_SHA256}"
    exit 1
fi

# The AppImage unpacks itself without FUSE, into squashfs-root beside it.
chmod +x "${work}/xenia.AppImage"
(cd "${work}" && ./xenia.AppImage --appimage-extract >/dev/null)
root="${work}/squashfs-root"
[[ -x "${root}/usr/bin/xenia_edge" ]] || { log "ERROR: no usr/bin/xenia_edge in the AppImage"; exit 1; }

rm -rf "${DEST}"
mkdir -p "${DEST}"
# usr/ is the program and its libraries. The AppImage's launcher is not
# needed. Nothing else goes beside the program: Xenia writes its log there
# unless told otherwise, and the console always tells it otherwise
# (standalone.cpp, `--log_file`), because /usr is read-only.
cp -a "${root}/usr" "${DEST}/"
rm -f "${DEST}/usr/bin/"*.log
printf '%s\n' "${XENIA_VERSION}" > "${DEST}/VERSION"

# THE CHECK THAT CAN BE MADE HERE: every library it needs resolves, against
# the image's own. It cannot be started in the build container: it opens its
# window before anything else, and there is no display. The A9 is where it is
# seen to run.
missing="$(ldd "${DEST}/usr/bin/xenia_edge" | grep 'not found' || true)"
if [[ -n "${missing}" ]]; then
    log "ERROR: Xenia Edge is missing libraries:"
    echo "${missing}"
    exit 1
fi
log "  installed in ${DEST} ($(du -sh "${DEST}" | cut -f1))"
