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
# PINNED, AND SINCE #304 BUILT BY US: LizardByte's release for Fedora 44 at
# one exact tag, built by its own recipe with our patch on it
# (cores/build-sunshine.sh, cores/sunshine-patches/), in its own workflow
# (build-sunshine.yml); here it arrives in the payload. Until 2026-10-09 this
# installed LizardByte's own package, never patched. MMagTech reopened that:
# Sunshine stuck black on an emptied display plane, and every workaround from
# outside left another way into it. The patch is offered upstream; back to
# LizardByte's package when a release has it. To move the pin: see
# cores/build-sunshine.sh. Its licence is GPL-3.0; see docs/LICENCES.md.
#
# WHAT THE CONSOLE RELIES ON, to check on the A9 before moving the pin
# (MMagTech, 2026-10-07: an update must not break it unseen):
#   1. its virtual controller's device name contains "Sunshine" or
#      "libvirtualhid" (players.cpp, isRemote): the handover finds the
#      phone's controllers by it;
#   2. its log says "CLIENT CONNECTED" and "CLIENT DISCONNECTED"
#      (remoteplay.cpp, readLog): how a dropped phone is known;
#   3. an app's prep-cmd "do" runs before the stream's capture starts and
#      "undo" when the app is quit (cabinetos-remoteplay started/ended);
#   4. GET and POST /api/pin with pairing_id, /api/clients/list and
#      /unpair, and root.named_devices[].cert in sunshine_state.json
#      (remoteplay.cpp): pairing on the TV and a phone pairing again;
#   5. its OpenGL shaders still under /usr/share/sunshine (checked below);
#   6. our patches still apply, or are upstream now (cores/build-sunshine.sh
#      checks both and stops).
# Then pair a phone, stream, hand over in a built-in and a standalone game,
# drop the phone, and switch Remote Play off and check nothing listens.
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

readonly SRC=/ctx/payload/sunshine

shopt -s nullglob
rpms=("${SRC}"/Sunshine-*.x86_64.rpm)
shopt -u nullglob
[[ ${#rpms[@]} -eq 1 ]] || { log "ERROR: expected one Sunshine package in ${SRC}"; exit 1; }
group_start "Installing Sunshine $(head -1 "${SRC}/VERSION" | cut -d' ' -f1), patched (#286, #304)"
while read -r line; do log "  ${line}"; done < "${SRC}/VERSION"

# Its dependencies (miniupnpc is the one the base lacks) from Fedora's own
# repositories, without weak ones. Not signed: it is ours, built in this
# repository's own workflow from a pinned tag.
dnf5 -y install --setopt=install_weak_deps=False "${rpms[0]}"

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
