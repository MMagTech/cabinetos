#!/usr/bin/env bash
#
# Remove Steam and the PC-gaming storefront layer.
#
# CabinetOS is explicitly not a Steam machine (docs/PROJECT.md, Non goals). The
# library comes from RomM and games are launched by emulators we bundle. Steam,
# Lutris and the Windows-compatibility layer around them are the single largest
# chunk of Bazzite that CabinetOS has no use for.
#
# What is deliberately KEPT from Bazzite's gaming stack, and why:
#
#   gamescope (terra-gamescope*)  The micro-compositor. Phase 2 runs the
#                                 frontend inside it, and Phase 5 runs emulators
#                                 as its children. This is the single most
#                                 important thing Bazzite gives us.
#   mangohud, vkBasalt            Vulkan layers. Useful for Phase 8 performance
#                                 work on PS2/GameCube. Small.
#   mesa (Valve-patched)          The graphics stack. Non-negotiable.
#   kmod-xone, kmod-gcadapter,    Controller drivers. xone is Xbox wireless;
#   kmod-new-lg4ff, kmod-hid-*    gcadapter is the GameCube adapter, which is
#                                 directly relevant to this project.
#   tuned / power-profiles-daemon Power and thermal handling.
#   ds-inhibit                    Stops controllers being treated as keyboards
#                                 for idle purposes.

source /ctx/lib.sh

group_start "Removing Steam and the PC gaming storefront layer"

# Steam itself, plus Bazzite's wrapper scripts and desktop entries around it.
# `steam-devices`, if it exists as a separate package, provides gamepad udev
# rules and must survive — see docs/PROJECT.md open question 2. This is why
# every removal uses --no-autoremove.
remove_pkgs "not a Steam machine" \
    steam \
    steam-devices-non-free \
    bazzite-steam \
    steam-notif-daemon \
    gamescope-session-ogui-steam \
    gamescope-session-steam \
    steamdeck-backgrounds \
    steamdeck-gnome-presets \
    steamdeck-kde-presets \
    steamdeck-kde-presets-desktop

# Other PC game launchers. Same reasoning: the library is RomM's.
remove_pkgs "no third-party game launchers" \
    lutris \
    heroic-games-launcher-bin \
    bottles

# Windows compatibility. CabinetOS runs emulators, not Windows games. umu is
# Valve's Proton launcher wrapper; winetricks is a Wine configuration helper.
remove_pkgs "no Windows compatibility layer" \
    umu-launcher \
    umu-wrapper

# Bazzite installs winetricks as a loose script rather than an RPM.
if [[ -f /usr/bin/winetricks ]]; then
    log "removing /usr/bin/winetricks (loose script, not an RPM)"
    rm -f /usr/bin/winetricks
fi

# Steam's bootstrap tarball, shipped by bazzite-deck for gamescope-session.
# Large, and useless without Steam.
if [[ -f /usr/share/gamescope-session-plus/bootstrap_steam.tar.gz ]]; then
    log "removing Steam bootstrap tarball"
    rm -f /usr/share/gamescope-session-plus/bootstrap_steam.tar.gz
fi

# Desktop entries for anything we just removed, plus Bazzite's autostart entry
# that launches Steam silently at login. Leaving a .desktop file for a missing
# binary is a visible error waiting to happen.
log "removing Steam/Lutris desktop entries and autostart"
rm -f /usr/share/applications/steam.desktop \
      /usr/share/applications/bazzite-steam-bpm.desktop \
      /usr/share/applications/net.lutris.Lutris.desktop \
      /etc/skel/.config/autostart/steam.desktop \
      /etc/xdg/autostart/steam.desktop

# ---------------------------------------------------------------------------
# Record what controller udev rules survived.
# ---------------------------------------------------------------------------
#
# This is the evidence for open question 2. If gamepads do not enumerate in the
# Phase 1 VM test, this listing in the CI log is where to look first.
group_end
group_start "Controller udev rules present after Steam removal"
find /usr/lib/udev/rules.d /etc/udev/rules.d -type f 2>/dev/null \
    | grep -iE "steam|joystick|gamepad|input|uaccess|xpad|60-|70-" \
    | sort || true
group_end

group_start "Controller kernel modules present after Steam removal"
rpm -qa 'kmod-*' | sort || true
group_end
