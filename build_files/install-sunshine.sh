#!/usr/bin/env bash
#
# Put Sunshine, Remote Play's streaming host, into the image. Issue #286;
# docs/SETTINGS.md, Remote Play.
#
# WHY IN THE IMAGE (MMagTech, 2026-10-07). Bazzite installs Sunshine on
# demand as a Flatpak, and the Flatpak cannot stream this console: it says
# so itself ("AppImage and Flatpak do not support KMS capture"), and
# gamescope offers it no other way to read the screen. Sunshine's own Fedora
# package can. A package can only be added to this read-only system when the
# image is built, and a download kept outside the image that may read the
# screen is what the image exists to avoid. An 8 MB download, 29 MB
# installed (measured in the base image); idle until Remote Play is on.
#
# PINNED, AS THE EMULATORS ARE: LizardByte's release package for Fedora 44,
# one exact version, checked against the sha256 GitHub publishes for it, so
# the build fails rather than ship another. To move it: pick a release, put
# its version and the package's sha256 here, try it on the A9, say why in the
# commit. Never patched. Its licence is GPL-3.0; see docs/LICENCES.md.
#
# WHAT RUNS IT: cabinetos-remoteplay.service, off by default, started by the
# switch. It gives Sunshine the capability to read the display itself, so the
# binary carries none. THE PACKAGE SETS ONE (cap_sys_admin,cap_sys_nice=p, in
# its file list, not a script; found by the dry run in the base image), which
# would let any program on the console run Sunshine with it; it is taken off
# here and the build checks it stayed off. The package's own
# user service and desktop launchers are removed, so nothing else can start a
# second Sunshine. Kept: its udev rules and the uhid module, which give the
# session the virtual controllers a phone plays with.

set -euo pipefail
source /ctx/lib.sh

readonly SUNSHINE_VERSION="2026.914.233613"
readonly SUNSHINE_RPM="Sunshine-${SUNSHINE_VERSION}-1.fc44.x86_64.rpm"
readonly SUNSHINE_URL="https://github.com/LizardByte/Sunshine/releases/download/v${SUNSHINE_VERSION}/${SUNSHINE_RPM}"
readonly SUNSHINE_SHA256="2abb16033ecde677f5c45322b39b6d6ae4cf297ac3e36707aa90bd3911d0e00f"

group_start "Installing Sunshine ${SUNSHINE_VERSION} (#286)"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

# A retry, because GitHub's release CDN occasionally drops a connection
# mid-file and a build that fails for that has nothing wrong with it.
for attempt in 1 2 3; do
    if curl -fsSL --retry 3 -o "${work}/${SUNSHINE_RPM}" "${SUNSHINE_URL}"; then
        break
    fi
    log "  download attempt ${attempt} failed"
    [[ ${attempt} -eq 3 ]] && { log "ERROR: could not download Sunshine"; exit 1; }
    sleep 5
done

got="$(sha256sum "${work}/${SUNSHINE_RPM}" | cut -d' ' -f1)"
if [[ "${got}" != "${SUNSHINE_SHA256}" ]]; then
    log "ERROR: Sunshine's checksum is ${got}, expected ${SUNSHINE_SHA256}"
    exit 1
fi

# Its dependencies (miniupnpc is the one the base lacks) from Fedora's own
# repositories, without weak ones.
dnf5 -y install --setopt=install_weak_deps=False "${work}/${SUNSHINE_RPM}"

# BY THE PACKAGE'S OWN FILE LIST, not by names or places, so a version that
# renames or moves its service or launchers is covered the same way
# (MMagTech, 2026-10-07: "what if they move them").
mapfile -t files < <(rpm -ql Sunshine)

# The unit gives the capability, to the Sunshine it starts and no other: off
# every file the package installed.
for f in "${files[@]}"; do
    [[ -f "${f}" && -n "$(getcap "${f}" 2>/dev/null)" ]] && setcap -r "${f}"
done

# Nothing but the switch starts it: every service, socket, timer, autostart
# entry and launcher the package installed, wherever it put them.
for f in "${files[@]}"; do
    if [[ "${f}" =~ \.(service|socket|timer|path|desktop)$ || "${f}" == */autostart/* ]]; then
        rm -f "${f}"
    fi
done

# CHECKED, NOT ASSUMED: the program, the shaders its VA-API path reads at
# start (without them it falls back to another encoder, measured on the A9),
# no capability on the file, and no library missing.
[[ -x /usr/bin/sunshine ]] || { log "ERROR: no /usr/bin/sunshine"; exit 1; }
[[ -s /usr/share/sunshine/shaders/opengl/ConvertY.frag ]] ||
    { log "ERROR: Sunshine's shaders are missing"; exit 1; }
for f in "${files[@]}"; do
    if [[ -f "${f}" && -n "$(getcap "${f}" 2>/dev/null)" ]]; then
        log "ERROR: ${f} carries a file capability; the unit gives it"
        exit 1
    fi
    if [[ -e "${f}" && ( "${f}" =~ \.(service|socket|timer|path|desktop)$ || "${f}" == */autostart/* ) ]]; then
        log "ERROR: ${f} can start Sunshine outside the switch"
        exit 1
    fi
done
if missing="$(ldd /usr/bin/sunshine | grep 'not found')" && [[ -n "${missing}" ]]; then
    log "ERROR: Sunshine is missing libraries:"
    log "${missing}"
    exit 1
fi
if left="$(grep -rlis 'bin/sunshine' /usr/lib/systemd/user /usr/lib/systemd/system \
              /usr/share/applications | grep -v cabinetos-remoteplay)" && [[ -n "${left}" ]]; then
    log "ERROR: something other than cabinetos-remoteplay can start Sunshine:"
    log "${left}"
    exit 1
fi

log "Sunshine ${SUNSHINE_VERSION} installed"
group_end
