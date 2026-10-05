#!/usr/bin/env bash
#
# THE BOOT SPLASH SAYS CABINETOS (#108). The splash is Fedora's "bgrt" theme
# as Bazzite ships it: the machine maker's logo from the firmware, a spinner,
# and a small watermark at the bottom, which Bazzite made its own logo.
# MMagTech, 2026-10-05: swap only that little logo. build.sh has already put
# ours (system_files/usr/share/plymouth/themes/spinner/watermark.png, drawn by
# build_files/assets/make-boot-watermark.py) over Bazzite's.
#
# THE SPLASH RUNS FROM THE INITRAMFS, so the file on disk is not what shows
# until the initramfs is rebuilt with it. This is the same dracut call the
# Universal Blue images make when they build theirs.
set -euo pipefail

WM=/usr/share/plymouth/themes/spinner/watermark.png
if ! cmp -s "${WM}" "/ctx/system_files${WM}"; then
    echo "boot-splash: the CabinetOS watermark is not at ${WM}" >&2
    exit 1
fi

kvers=(/usr/lib/modules/*/)
if [[ ${#kvers[@]} -ne 1 ]]; then
    echo "boot-splash: expected one kernel in /usr/lib/modules, found ${#kvers[@]}" >&2
    exit 1
fi
KVER="$(basename "${kvers[0]}")"
IMG="/usr/lib/modules/${KVER}/initramfs.img"

# /root is a link to var/roothome, which exists only on a running console
# (tmpfiles makes it). With nothing at the end of the link, dracut leaves
# out both the link and the directory, which Bazzite's initramfs has. So it
# exists for the length of the rebuild and is removed in this same step: the
# image's own /var stays empty. Found by the dry run on the A9 2026-10-05,
# comparing the file lists; this was the only difference.
made_roothome=0
if [[ ! -e /var/roothome ]]; then
    mkdir -p /var/roothome
    made_roothome=1
fi
dracut --no-hostonly --kver "${KVER}" --reproducible --zstd --add ostree -f "${IMG}"
[[ ${made_roothome} -eq 1 ]] && rmdir /var/roothome
chmod 0600 "${IMG}"

# ASSERTED, not assumed: the rebuilt initramfs carries our watermark, byte
# for byte. Without this a theme change that dracut did not pick up would
# build green and show Bazzite's logo.
if ! lsinitrd -f "${WM#/}" "${IMG}" | cmp -s - "${WM}"; then
    echo "boot-splash: the rebuilt initramfs does not carry the CabinetOS watermark" >&2
    exit 1
fi
echo "boot-splash: initramfs for ${KVER} rebuilt with the CabinetOS watermark ($(du -h "${IMG}" | cut -f1))"
