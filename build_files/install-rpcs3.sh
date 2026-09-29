#!/usr/bin/env bash
#
# Put RPCS3, the PlayStation 3 emulator, into the image: the RPCS3 team's own
# Linux build, unpacked into /usr/lib/cabinetos/rpcs3.
#
# WHY NOT FLATHUB, like Eden and xemu. Since 2026-08-13 RPCS3 shows an
# "Unofficial Build Warning" box on every start when it runs as a Flatpak
# (rpcs3qt/gui_application.cpp, `FLATPAK_ID`), with No as the default and No
# quitting. There is no setting for it. The Flathub package is community-made
# and the RPCS3 team does not support it. MMagTech, 2026-09-28: take the
# official build instead. docs/PROJECT.md, open questions 19 and 21.
#
# WHY THIS IS NOT THE /var TRAP of open question 21. That trap is a Flatpak's:
# it installs into /var, which bootc unpacks only on the first install. This
# lands in /usr, which every update replaces whole, so RPCS3 moves with the
# image exactly as the libretro cores do, and a console that has never reached
# the internet has PS3 from its first boot.
#
# PINNED THE WAY A CORE IS. The official builds are one GitHub release per
# commit (RPCS3/rpcs3-binaries-linux), kept back to 2019, so the URL names an
# exact build and the checksum makes the build fail rather than ship anything
# else. To move it: pick a release, put its URL and the sha256 of its
# AppImage here, and say why in the commit.
#
# UNPACKED, NOT RUN AS AN APPIMAGE. An AppImage mounts itself with FUSE when it
# starts; unpacked, `usr/bin/rpcs3` runs directly, finds its bundled libraries
# through its own RUNPATH ($ORIGIN/../lib), and is a plain process called
# `rpcs3` that the console can signal. Measured on the A9 2026-09-28: 94 MB
# downloaded, 338 MB unpacked, no library missing against the image, and the
# AppImage's own launcher adds nothing on this base (its libstdc++ check picks
# the system's, which is newer).
#
# Its licence is GPL-2.0; see docs/LICENCES.md.

set -euo pipefail
source /ctx/lib.sh

readonly RPCS3_VERSION="0.0.42-20076-1707d7fc"
readonly RPCS3_URL="https://github.com/RPCS3/rpcs3-binaries-linux/releases/download/build-1707d7fc883ef48ff21bdcbb0141a3211ae09cb2/rpcs3-v0.0.42-20076-1707d7fc_linux64.AppImage"
readonly RPCS3_SHA256="e3e29063410ca40fc7b5c66aaaec7c14c41144135faa09f23f5797cf15194a55"
readonly DEST=/usr/lib/cabinetos/rpcs3

log "RPCS3 ${RPCS3_VERSION}"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

# A retry, because GitHub's release CDN occasionally drops a connection
# mid-file and a build that fails for that has nothing wrong with it.
for attempt in 1 2 3; do
    if curl -fsSL --retry 3 -o "${work}/rpcs3.AppImage" "${RPCS3_URL}"; then
        break
    fi
    log "  download attempt ${attempt} failed"
    [[ ${attempt} -eq 3 ]] && { log "ERROR: could not download RPCS3"; exit 1; }
    sleep 5
done

got="$(sha256sum "${work}/rpcs3.AppImage" | cut -d' ' -f1)"
if [[ "${got}" != "${RPCS3_SHA256}" ]]; then
    log "ERROR: RPCS3's checksum is ${got}, expected ${RPCS3_SHA256}"
    exit 1
fi

# The AppImage unpacks itself without FUSE, into squashfs-root beside it. It
# still prints "failed to utilize FUSE during startup!" in a container; the
# unpack is unaffected, and the check below is what says whether it worked.
chmod +x "${work}/rpcs3.AppImage"
(cd "${work}" && ./rpcs3.AppImage --appimage-extract >/dev/null)
root="${work}/squashfs-root"
[[ -x "${root}/usr/bin/rpcs3" ]] || { log "ERROR: no usr/bin/rpcs3 in the AppImage"; exit 1; }

rm -rf "${DEST}"
mkdir -p "${DEST}"
# usr/ is the program, its libraries and its own data (fonts, icons, the
# game-compatibility patches). The AppImage's launcher and checkrt are not
# needed; see above.
cp -a "${root}/usr" "${DEST}/"
printf '%s\n' "${RPCS3_VERSION}" > "${DEST}/VERSION"

# THE CHECK THAT MATTERS: it starts, from where it now lives, against the
# image's own libraries, and it is the build this file pins. `--headless` as
# well as `--version`: without it RPCS3 builds its Qt window layer first, and
# the build container has no display for that (the first testing build died
# here, 2026-09-29, "no Qt platform plugin could be initialized"). Headless it
# makes a plain QCoreApplication (rpcs3.cpp, create_application).
if ! XDG_CONFIG_HOME="${work}/cfg" XDG_CACHE_HOME="${work}/cache" \
        "${DEST}/usr/bin/rpcs3" --headless --version >"${work}/version.out" 2>&1 ||
   ! grep -q "RPCS3 ${RPCS3_VERSION}" "${work}/version.out"; then
    cat "${work}/version.out"
    log "ERROR: RPCS3 does not start in the image, or is not ${RPCS3_VERSION}"
    exit 1
fi
missing="$(ldd "${DEST}/usr/bin/rpcs3" | grep 'not found' || true)"
if [[ -n "${missing}" ]]; then
    log "ERROR: RPCS3 is missing libraries:"
    echo "${missing}"
    exit 1
fi
log "  installed in ${DEST} ($(du -sh "${DEST}" | cut -f1))"
