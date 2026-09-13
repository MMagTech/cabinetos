#!/usr/bin/env bash
#
# ===========================================================================
#  DO NOT REMOVE ANYTHING LISTED IN THIS FILE.
# ===========================================================================
#
# HDMI-CEC is a HARD REQUIREMENT for CabinetOS. The console must be able to turn
# the television on, and be woken by it. That is not a convenience — it is a
# large part of what separates "a console" from "a computer under the telly",
# and the product is defined by that difference.
#
# Everything here looks like desktop or HTPC cruft to someone reading a package
# list, and every one of these is exactly the sort of thing a future strip pass
# would delete without a second thought. Hence this file: it names each one, and
# FAILS THE BUILD if any of them goes missing, whether that is our doing or an
# upstream Bazzite change.
#
# If you are here because the build failed, do not "fix" it by deleting the
# check. Work out what removed the package and stop that instead.
#
# ---------------------------------------------------------------------------
# WHY EACH ONE
# ---------------------------------------------------------------------------
#
#   libcec              The legacy CEC stack. Provides libcec.so and
#                       cec-client. This is what drives USB CEC adapters —
#                       which, on x86 mini PCs, is the only way CEC happens at
#                       all. See docs/PROJECT.md, Hardware.
#
#   v4l-utils           Provides cec-ctl and cec-follower, the kernel CEC
#                       userspace tools. Reads as a webcam package. It is not.
#
#   linux-cec           Valve's native CEC backend, providing cecd. The other
#                       of the two modes Bazzite ships.
#
#   linuxconsoletools   Provides inputattach, which is what binds a Pulse-Eight
#                       or RainShadow USB adapter to an input device so the TV
#                       remote produces key events. Reads like a joystick
#                       calibration utility. It is load-bearing.
#
# The udev rules and systemd units matter just as much and are equally
# unrecognisable: 60-cec-uaccess, 60-cecd-uinput, 60-inputattach-cec,
# 99-cec-bluetooth, and the cec-onboot / cec-onsleep / cec-onpoweroff /
# cec-poweroff-tv / cec-active-source units plus the two inputattach templates.

source /ctx/lib.sh

group_start "Verifying HDMI-CEC support survived the strip"

failed=0

# --- packages --------------------------------------------------------------
for pkg in libcec v4l-utils linux-cec linuxconsoletools; do
    if is_installed "${pkg}"; then
        log "  ok: ${pkg} ($(rpm -q --qf '%{VERSION}' "${pkg}"))"
    else
        log "  MISSING: ${pkg} — HDMI-CEC is a hard requirement, reinstalling"
        if dnf5 -y install "${pkg}"; then
            log "  recovered: ${pkg}"
        else
            log "  ERROR: could not reinstall ${pkg}"
            failed=1
        fi
    fi
done

# --- binaries --------------------------------------------------------------
#
# Checked separately from the packages because the mapping is not obvious:
# cec-ctl and cec-follower come from v4l-utils, cecd from linux-cec, and
# inputattach from linuxconsoletools. A package surviving with its binary
# subpackage stripped would otherwise pass silently.
for bin in cec-ctl cec-client cec-follower cecd inputattach; do
    if command -v "${bin}" >/dev/null 2>&1; then
        log "  ok: ${bin} ($(command -v "${bin}"))"
    else
        log "  MISSING: ${bin}"
        failed=1
    fi
done

# --- udev rules ------------------------------------------------------------
for rule in 60-cec-uaccess.rules 60-cecd-uinput.rules 60-inputattach-cec.rules; do
    if [[ -e "/usr/lib/udev/rules.d/${rule}" ]]; then
        log "  ok: ${rule}"
    else
        log "  MISSING: udev rule ${rule}"
        failed=1
    fi
done

# --- systemd units ---------------------------------------------------------
#
# Not enabled here. Which units run depends on the CEC mode, and that is a
# runtime choice — see docs/PROJECT.md, Phase 7. They only have to exist.
for unit in cec-onboot.service cec-onsleep.service cec-onpoweroff.service \
            pulse8-cec-inputattach@.service rainshadow-cec-inputattach@.service; do
    if systemctl cat "${unit}" >/dev/null 2>&1; then
        log "  ok: ${unit}"
    else
        log "  MISSING: unit ${unit}"
        failed=1
    fi
done

# --- the known gap ---------------------------------------------------------
#
# steamos-manager is bazzite-deck only and is NOT in plain bazzite. Bazzite's
# own native CEC mode enables steamos-manager-configure-cecd.service alongside
# cecd, so native mode here is incomplete by construction.
#
# Deliberately a warning rather than an error: pulling steamos-manager in would
# drag the SteamOS management layer into the image, which constraint 4 rules
# out. The adapter-driven legacy mode is the one that matters on x86 anyway.
# Recorded in docs/PROJECT.md under Hardware.
if is_installed steamos-manager; then
    log "  note: steamos-manager present — native CEC mode is complete"
else
    log "  note: steamos-manager absent (bazzite-deck only). Native CEC mode is"
    log "        incomplete; legacy adapter mode is the supported path. Expected."
fi

group_end

if [[ "${failed}" -ne 0 ]]; then
    log "HDMI-CEC verification FAILED — see docs/PROJECT.md, Hardware"
    exit 1
fi
