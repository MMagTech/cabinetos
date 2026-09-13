#!/usr/bin/env bash
#
# Remove the desktop: the display manager, the desktop applications, and every
# obvious route to a terminal, a file browser, an app store or a web browser.
#
# READ THIS BEFORE ADDING TO THE LISTS BELOW.
#
# This script does NOT uninstall KDE Plasma itself. That is deliberate and it is
# open question 1 in docs/PROJECT.md. Short version:
#
#   - Bazzite versionlocks `plasma-*` and `qt6-*` on this base, which means it
#     treats them as load-bearing.
#   - Bazzite installs `plasma-foreground-booster-dmemcg` as part of its
#     process-priority handling for games.
#   - `xdg-desktop-portal-kde` is the portal implementation here.
#   - A cascading Plasma removal can produce an image that builds fine and does
#     not boot, and right now we have no VM test to catch that.
#
# What this script does instead is make the desktop unreachable: the default
# systemd target becomes multi-user.target, the display manager is removed and
# masked, and the desktop applications are gone. That satisfies "the user never
# sees a desktop" without betting the image on a large dependency cascade.
#
# Phase 2 owns the decision about actually deleting Plasma. A candidate list is
# at the bottom of this file, commented out.

source /ctx/lib.sh

group_start "Removing the display manager and graphical login"

# The display manager is the thing that would otherwise put a login screen in
# front of the user. CabinetOS autologins into its own session (Phase 2).
#
# `sddm` is the traditional Kinoite display manager; `plasma-login-manager` is
# its replacement in newer Plasma. Both are listed because Bazzite has been
# moving between them and remove_pkgs skips whichever is absent.
remove_pkgs "no graphical login, CabinetOS autologins into its own session" \
    sddm \
    sddm-breeze \
    sddm-wayland-plasma \
    sddm-x11 \
    plasma-login-manager \
    gdm

# Masked as well as removed, so that nothing can pull a display manager back in
# as a dependency and so graphical.target cannot start one.
disable_units "no graphical login" \
    sddm.service \
    plasma-login-manager.service \
    gdm.service

# The default target. This is what makes the Phase 1 image boot to a console
# prompt rather than to a desktop, and it is the single most effective line in
# this file. Phase 2 replaces this with a CabinetOS session unit.
log "setting default systemd target to multi-user.target"
rm -f /usr/lib/systemd/system/default.target
ln -sf /usr/lib/systemd/system/multi-user.target /usr/lib/systemd/system/default.target

group_end

group_start "Removing desktop applications"

# App store. Explicit non-goal: "No app store."
remove_pkgs "no app store" \
    plasma-discover \
    plasma-discover-flatpak \
    plasma-discover-libs \
    plasma-discover-notifier \
    plasma-discover-packagekit \
    plasma-discover-rpm-ostree \
    gnome-software

# Web browser. Explicit non-goal: "No browser."
remove_pkgs "no browser" \
    firefox \
    firefox-langpacks \
    falkon \
    epiphany

# Terminal emulators. A terminal is a route to a Linux error message, which is
# a bug by the definition in docs/PROJECT.md.
remove_pkgs "no terminal" \
    konsole \
    konsole5 \
    yakuake \
    ptyxis \
    gnome-terminal \
    xterm

# File browser. Explicit non-goal.
remove_pkgs "no file browser" \
    dolphin \
    dolphin-plugins \
    nautilus \
    krusader

# Text editors, document viewers, media players, archive tools and the rest of
# the stock desktop application set. None of these have a place on a console,
# and all of them are leaves.
remove_pkgs "desktop applications, not console software" \
    kate \
    kwrite \
    okular \
    gwenview \
    ark \
    elisa \
    kcalc \
    kcharselect \
    kmines \
    kpat \
    kmahjongg \
    kolourpaint \
    spectacle \
    haruna \
    dragon \
    kmail \
    kontact \
    korganizer \
    kaddressbook \
    akregator \
    kwalletmanager5 \
    krfb \
    krdc \
    kdeconnectd \
    kde-connect \
    kde-connect-libs \
    neochat \
    thunderbird \
    libreoffice-core

# System administration GUIs. Anything that presents partitions, snapshots or
# services to a user is a computer, not a console.
#
# NOTE: `snapper` itself is kept — Bazzite uses it for the snapshot layer. Only
# the graphical front end goes.
remove_pkgs "no system administration UI" \
    btrfs-assistant \
    plasma-systemmonitor \
    ksystemlog \
    filelight \
    partitionmanager

# Cockpit is a web-based server admin console. Useful on a server, exactly the
# wrong idea on a console. Phase 6 adds an SSH developer mode instead.
remove_pkgs "no web admin console" \
    cockpit \
    cockpit-ws \
    cockpit-bridge \
    cockpit-networkmanager \
    cockpit-podman \
    cockpit-selinux \
    cockpit-system \
    cockpit-files \
    cockpit-storaged

# Waydroid is an Android runtime. Out of scope, large, and its service is
# already disabled by Bazzite.
remove_pkgs "no Android runtime" \
    waydroid \
    waydroid-selinux

# Bazzite's own first-run and welcome UI. CabinetOS has its own first run setup
# (Phase 8) and must not show Bazzite's.
remove_pkgs "CabinetOS owns first run, not Bazzite" \
    bazzite-portal \
    ublue-os-just \
    yafti

group_end

group_start "Disabling Bazzite's automatic updater"

# The update model is one version number for the whole system (docs/PROJECT.md).
# `uupd` updates packages and Flatpaks independently of the image, which would
# create exactly the partial-version state that model exists to prevent.
# Phase 7 replaces this with a CabinetOS updater.
#
# Disabled rather than removed: `uupd` is a Bazzite integration point and
# removing it may have consequences we have not mapped. The timer is what does
# the damage.
disable_units "Phase 7 owns updates; no partial version state" \
    uupd.timer \
    rpm-ostreed-automatic.timer \
    flatpak-system-update.timer

group_end

# ---------------------------------------------------------------------------
# PHASE 2 CANDIDATES — DO NOT ENABLE WITHOUT A VM BOOT TEST
# ---------------------------------------------------------------------------
#
# The packages below are what a full Plasma removal would start from. They are
# listed here so Phase 2 has a starting point rather than a blank page. Each one
# needs to be validated, and the frontend toolkit choice (Phase 3) has to be
# made first, because it determines which Qt and Wayland libraries must stay.
#
# See docs/PROJECT.md open question 1.
#
# remove_pkgs "Phase 2: full Plasma removal" \
#     plasma-desktop \
#     plasma-workspace \
#     plasma-workspace-wayland \
#     plasma-nm \
#     plasma-pa \
#     plasma-breeze \
#     kwin \
#     kwin-wayland \
#     kde-cli-tools \
#     kdeplasma-addons \
#     xdg-desktop-portal-kde
#
# Known blockers to resolve before trying:
#   - plasma-foreground-booster-dmemcg depends on Plasma and is part of
#     Bazzite's game priority handling. Find out what it actually needs.
#   - xdg-desktop-portal-kde needs a replacement portal, or Flatpak-shaped
#     things lose file dialogs and permissions.
#   - Bazzite versionlocks plasma-* and qt6-*; a removal may fight the lock.
