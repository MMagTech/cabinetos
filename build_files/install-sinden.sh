#!/usr/bin/env bash
#
# What Sinden's light-gun software needs from the image (#575), and a check
# that all of it is here. Sinden's files come with system_files/, in
# /usr/lib/cabinetos/sinden, with the launcher (/usr/libexec/cabinetos-sinden)
# that starts one copy of them for each gun plugged in, its unit
# (cabinetos-sinden@.service) and its udev rule (73-cabinetos-sinden.rules).
#
# WHY IT SHIPS: docs/decisions/2026-11-02-sinden-software-in-the-image.md.
# Sinden's licence lets only Sinden distribute its software, and Sinden gave
# this project permission to ship it in the image, unchanged, with its
# licence file (License.txt, beside it).
#
# WHAT IS THERE: Sinden's Linux software 2.05, its "Ubuntu_Version22_04_Beta"
# build, as it comes in Sinden's 2.07b package
# (SindenLightgunSoftwareReleaseV2.07b.zip, PCversion/Ubuntu_Version22_04_Beta/
# LightgunUbuntu_22_04): LightgunMono.exe, its settings file, its licence and
# its readme, each checked below against the package's own bytes; and its
# camera library, libCameraInterface.so, rebuilt for Fedora. Sinden's own
# needs Ubuntu's libjpeg8, which Fedora does not carry, so it was rebuilt
# from the source Sinden sent, against Fedora 44's libjpeg-turbo, in
# cabinetos-builder, and stripped (its debug information names the private
# source's insides). That source is never shared: it is in no repository and
# no log, and only the built library is here. Its checksum below is that
# build's. Left out: Sinden's libSdlInterface.so, the library of its own
# calibration window, which needs SDL 1.2, SDL_image 1.2 and libjpeg8 (none
# in the image) and loads only when the program is asked for that window,
# which the console never does.
#
# WHAT IT NEEDS: Mono, the runtime the program is built for, and libgdiplus,
# Mono's System.Drawing: Fedora 44's own packages. Measured on the reference
# console on 2026-11-27: both guns, both cameras at 66 frames a second, on
# Fedora's Mono 6.14.1 and libgdiplus 6.2 with this camera library. mono-core
# brings seven more of Mono's own packages it requires (mono-data,
# mono-data-sqlite, mono-extras, mono-mvc, mono-wcf, mono-web, mono-winforms),
# and everything else is already in the base: 94 MB on disk, 28 MB to
# download, in the image build of 2026-11-28. And the commands the program
# runs itself, all in the base:
# lsusb, udevadm and v4l2-ctl (without either of the first two it quits
# straight after its banner), grep and bash; and bwrap, which the launcher
# builds each gun's world with. In the OS layer, from build.sh, after the
# strip, as LinkUp's package is.

set -euo pipefail

# shellcheck source=build_files/lib.sh
source /ctx/lib.sh

readonly SINDEN=/usr/lib/cabinetos/sinden

group_start "Installing what Sinden's light-gun software needs"

dnf5 -y install --setopt=install_weak_deps=False mono-core libgdiplus
log "Mono: $(rpm -q mono-core), $(rpm -q libgdiplus)"

failed=0

# Sinden's files, byte for byte as its package has them, and the camera
# library as built. A file that differs is not what Sinden allowed us to
# ship, or not the library that was tested.
while read -r sum name; do
    if [[ ! -s "${SINDEN}/${name}" ]]; then
        log "  ERROR: ${SINDEN}/${name} is missing"
        failed=1
    elif [[ "$(sha256sum "${SINDEN}/${name}" | cut -d' ' -f1)" != "${sum}" ]]; then
        log "  ERROR: ${SINDEN}/${name} is not the file this script pins"
        failed=1
    else
        log "  ok: ${SINDEN}/${name}"
    fi
done <<'EOF'
55b7306123bec9ccbbf73fc38cd0a0b4aa3e36faa97669b05a9553fd67328ccb LightgunMono.exe
20e5f1ebfcfb05651cb2539a56d81438f424f53f48c1c4dc4644ffe4df5dce60 LightgunMono.exe.config
83d2792717559d55fd95071f95baee357469772e6f64e2558052186bee84285d License.txt
78998b7d30122cb2c44b6b177503b1638dafa2da3cc5f49683287d58d8595785 readme.txt
97ea921d4e2a05866b556df672e245e30c0fca10351ee6a2702391b0c6fd27be libCameraInterface.so
EOF

# `ldd` prints "not found" and exits 0 (docs/lessons/image-and-ci.md), so the
# output is read.
missing=$(ldd "${SINDEN}/libCameraInterface.so" 2>&1 | grep 'not found' || true)
if [[ -n "${missing}" ]]; then
    log "  ERROR: the camera library needs what the image lacks: ${missing}"
    failed=1
else
    log "  ok: the camera library finds every library it needs"
fi

# Mono reaches libgdiplus by the name its own settings map it to.
if ! grep -q 'libgdiplus' /etc/mono/config; then
    log "  ERROR: /etc/mono/config no longer maps gdiplus to libgdiplus"
    failed=1
fi
if ! ls /usr/lib64/libgdiplus.so* >/dev/null 2>&1; then
    log "  ERROR: libgdiplus is not in /usr/lib64"
    failed=1
fi
if ! mono --version >/dev/null; then
    log "  ERROR: mono does not run"
    failed=1
fi

for needed in mono bwrap lsusb udevadm v4l2-ctl grep bash realpath; do
    if command -v "${needed}" >/dev/null 2>&1; then
        log "  ok: ${needed} ($(command -v "${needed}"))"
    else
        log "  ERROR: ${needed} is not in the image; Sinden's software or its launcher runs it"
        failed=1
    fi
done

# The rule has to start the unit that exists.
if ! grep -q 'cabinetos-sinden@%k.service' /usr/lib/udev/rules.d/73-cabinetos-sinden.rules ||
   [[ ! -s /usr/lib/systemd/system/cabinetos-sinden@.service ]] ||
   [[ ! -x /usr/libexec/cabinetos-sinden ]]; then
    log "  ERROR: the Sinden rule, unit and launcher do not match up"
    failed=1
fi

[[ "${failed}" -eq 0 ]] || exit 1
log "Sinden's light-gun software has everything it needs"

group_end
