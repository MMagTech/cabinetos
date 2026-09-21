#!/usr/bin/env bash
#
# CabinetOS image build.
#
# Called from the Containerfile with build_files/ mounted at /ctx and
# system_files/ mounted at /system_files.
#
# The gaming stack comes from Bazzite and is kept as-is; the desktop and Steam
# are stripped out, the session is wired in, and the console itself — the
# frontend, the twenty-two cores and one core's system files — is installed
# from image_payload/ near the end. See install-frontend.sh.

set -euo pipefail

source /ctx/lib.sh

log "CabinetOS build starting"
log "base: $(grep '^PRETTY_NAME=' /usr/lib/os-release | cut -d= -f2-)"

# ---------------------------------------------------------------------------
# Overlay any files from system_files/ onto the image.
# ---------------------------------------------------------------------------
#
# Empty in Phase 1. Phase 2 uses this for the session units and autologin
# configuration.
# NOTE THE PATH. The Containerfile's ctx stage does `COPY system_files
# /system_files`, and that whole stage is bind-mounted at /ctx — so the files
# land at /ctx/system_files, not /system_files.
#
# This was originally written as /system_files guarded by `if [[ -d ... ]]`,
# which meant it silently copied nothing for several builds. Nobody noticed
# until a unit file was expected to be there. No guard now: if the directory is
# missing the build fails, loudly, here.
if [[ ! -d /ctx/system_files ]]; then
    log "ERROR: /ctx/system_files is missing — the Containerfile ctx stage is wrong"
    exit 1
fi

log "overlaying system_files/"
# /ctx is a read-only bind mount, so nothing here may modify the source. An
# earlier version tried to delete .gitkeep placeholders from it and failed.
# There are no placeholders now — every directory under system_files/ holds a
# real file — so there is nothing to exclude.
cp -avf /ctx/system_files/. / >/dev/null

# Prove it landed, rather than trusting cp's exit code. This is the check that
# would have caught the silent no-op above.
for expected in \
    /usr/bin/cabinetos-session \
    /usr/lib/systemd/system/cabinetos-session.service \
    /usr/lib/sysusers.d/cabinetos.conf \
    /usr/lib/tmpfiles.d/cabinetos.conf \
    /usr/lib/bootc/install/20-cabinetos.toml \
    /usr/libexec/cabinetos-flatpak-setup \
    /usr/lib/systemd/system/cabinetos-flatpak-setup.service \
    /usr/lib/systemd/system/cabinetos-flatpak-setup.timer \
    /usr/share/cabinetos/flatpaks.list
do
    if [[ -e "${expected}" ]]; then
        log "  overlaid: ${expected}"
    else
        log "  ERROR: ${expected} did not land"
        exit 1
    fi
done

# ---------------------------------------------------------------------------
# The licences, INSIDE the image.
# ---------------------------------------------------------------------------
#
# This image contains twenty-two emulator cores. Most are GPL and six are free
# for non-commercial use only, and the person most likely to redistribute it is
# the one who does `docker pull` and never sees this repository at all. Terms
# that live only beside the binaries are terms that do not travel with them.
#
# /usr/share/licenses/<name>/ is where Fedora puts these, so anything that
# already knows how to look for a package's licence finds ours too.
log "installing licences"
mkdir -p /usr/share/licenses/cabinetos
cp /ctx/licences/LICENSE      /usr/share/licenses/cabinetos/LICENSE
cp /ctx/licences/LICENCES.md  /usr/share/licenses/cabinetos/LICENCES.md

# Asserted rather than assumed, for the reason the system_files overlay above
# has the same check: a copy that silently does nothing leaves a green build.
for expected in \
    /usr/share/licenses/cabinetos/LICENSE \
    /usr/share/licenses/cabinetos/LICENCES.md
do
    if [[ -s "${expected}" ]]; then
        log "  installed: ${expected} ($(wc -c < "${expected}") bytes)"
    else
        log "  ERROR: ${expected} is missing or empty"
        exit 1
    fi
done

# The one line in there that constrains what anyone may do with this image, put
# where a person reads it rather than left for them to find in a table.
grep -q 'non-commercial' /usr/share/licenses/cabinetos/LICENCES.md || {
    log "  ERROR: LICENCES.md no longer mentions the non-commercial cores"
    exit 1
}

# ---------------------------------------------------------------------------
# The console's own polkit rule.
# ---------------------------------------------------------------------------
#
# Copied by the system_files overlay above; asserted here, because this one is
# invisible when it is missing. Wi-Fi configuration works on the reference
# machine today WITHOUT it — the session user is in `wheel` and Bazzite grants
# the action to that group — so a build that silently dropped this file would
# ship a console that works until the day Phase 6 tightens developer mode and
# takes the session user out of `wheel`. Then Wi-Fi stops being configurable,
# with no error anywhere near the cause. See docs/PROJECT.md open question 17.
POLKIT_RULE=/usr/share/polkit-1/rules.d/60-cabinetos-network.rules
if [[ -s "${POLKIT_RULE}" ]]; then
    log "installed: ${POLKIT_RULE}"
else
    log "  ERROR: ${POLKIT_RULE} is missing — Wi-Fi would depend on 'wheel'"
    exit 1
fi
# The action it grants, spelled out, so a rename upstream fails the build rather
# than producing a rule that matches nothing.
grep -q 'org.freedesktop.NetworkManager.settings.modify.system' "${POLKIT_RULE}" || {
    log "  ERROR: ${POLKIT_RULE} no longer names the action it exists to grant"
    exit 1
}
# The user it names has to be the one sysusers.d creates, or the grant lands on
# nobody. Both are in this repository and they must move together.
grep -q '^u cabinet ' /usr/lib/sysusers.d/cabinetos.conf || {
    log "  ERROR: the session user is no longer 'cabinet'; ${POLKIT_RULE} names it"
    exit 1
}

# The two commands the frontend RUNS rather than links against.
#
# NOTHING ELSE WOULD CATCH THESE GOING MISSING. ci/base-watch.txt watches shared
# LIBRARIES, and require-frontend-libs.sh reads `ldd` — so a strip pass that took
# NetworkManager or polkit out would leave a green build, a binary that links
# perfectly, and a console that cannot see a Wi-Fi network or say why. They are
# a real dependency of frontend/src/net.cpp and they are invisible to every
# check this repository already has.
#
# They are in the base image today: NetworkManager owns nmcli, polkit owns
# pkcheck and bluez owns bluetoothctl. None is something CabinetOS installs, and
# none should be removed — a console that cannot configure its own network
# cannot complete first run at all (open question 15b's one hard gate), and one
# that cannot pair a controller finishes setup owning a games console nobody can
# play from a sofa.
for needed in /usr/bin/nmcli /usr/bin/pkcheck /usr/bin/bluetoothctl; do
    if [[ -x "${needed}" ]]; then
        log "present: ${needed} ($(rpm -qf "${needed}" 2>/dev/null || echo 'unowned'))"
    else
        log "  ERROR: ${needed} is not in this image — the frontend runs it"
        exit 1
    fi
done

# THE SAME BLIND SPOT, ONE STEP WORSE: A LIBRARY THAT IS dlopen'd.
#
# frontend/src/gpu.cpp opens libvulkan.so.1 by hand rather than linking it, so
# that the binary still starts on a machine with no Vulkan and says so. That is
# the right behaviour and it costs the one check that would have caught its
# absence: `ldd` on the frontend does not name it, so
# require-frontend-libs.sh cannot see it and ci/base-watch.txt has nothing to
# watch.
#
# WITHOUT IT, PLAYSTATION 2 AND GAMECUBE SIMPLY DO NOT PLAY, and the console
# says only "this core wants Vulkan and no libvulkan.so.1 on this machine" —
# on a machine whose GPU is fine. Every other core keeps working, so the build
# is green, the console boots, and one tier of the library quietly goes dark.
#
# The ICD matters as much as the loader: a loader with no driver behind it
# enumerates zero devices, which is exactly what the test VM reports.
for needed in /usr/lib64/libvulkan.so.1 /usr/lib64/libvulkan_radeon.so; do
    if [[ -e "${needed}" ]]; then
        log "present: ${needed} ($(rpm -qf "${needed}" 2>/dev/null || echo 'unowned'))"
    else
        log "  ERROR: ${needed} is not in this image — PS2 and GameCube need it"
        exit 1
    fi
done

# ---------------------------------------------------------------------------
# Record the starting package set.
# ---------------------------------------------------------------------------
#
# Written into the image at /usr/share/cabinetos/. Having the before-and-after
# in the image makes it possible to answer "what did we actually remove?"
# without re-running the build, and makes Bazzite bumps diffable.
mkdir -p /usr/share/cabinetos
rpm -qa | sort > /usr/share/cabinetos/packages-before-strip.txt
log "base image has $(wc -l < /usr/share/cabinetos/packages-before-strip.txt) packages"

# ---------------------------------------------------------------------------
# Strip.
# ---------------------------------------------------------------------------
#
# Order matters slightly: Steam first, because the desktop pass removes desktop
# entries and we want Steam's gone before then.
/ctx/strip-steam.sh
/ctx/strip-desktop.sh

# ---------------------------------------------------------------------------
# Enable SSH.
# ---------------------------------------------------------------------------
#
# Runs after the strip scripts, so it can assert that nothing they removed took
# openssh-server with it. See the warning at the top of enable-ssh.sh: this is a
# development affordance that Phase 6 must take away again.
/ctx/enable-ssh.sh

# ---------------------------------------------------------------------------
# Repair repository definitions.
# ---------------------------------------------------------------------------
#
# Last, so it sees the final state. A repo whose GPG key was never shipped
# breaks any operation that reads all repos — which is what building an
# installer ISO does. See the script for the full reasoning.
/ctx/fix-repos.sh

# ---------------------------------------------------------------------------
# Phase 2: the session.
# ---------------------------------------------------------------------------
#
# Makes the machine boot into gamescope instead of a console, and closes the
# routes to a desktop that the strip pass left behind.
/ctx/configure-session.sh

# ---------------------------------------------------------------------------
# Emulator flatpaks.
# ---------------------------------------------------------------------------
#
# After the strip, because it asserts that strip-desktop.sh masked the flatpak
# update timers — which is what keeps a pinned emulator pinned. Nothing is
# installed here; see the script for why that is forced by bootc rather than
# chosen.
/ctx/configure-flatpaks.sh

# ---------------------------------------------------------------------------
# HDMI-CEC.
# ---------------------------------------------------------------------------
#
# Runs last, after everything that could remove a package. CEC is a hard
# requirement and every package it needs looks like cruft in a package list —
# this fails the build rather than letting one quietly disappear.
/ctx/require-cec.sh

# ---------------------------------------------------------------------------
# Frontend runtime libraries.
# ---------------------------------------------------------------------------
#
# Same reasoning and the same position in the order: last, after anything that
# could have removed a package. The frontend links against these, and they are
# in the base image incidentally rather than by declared dependency.
/ctx/require-frontend-libs.sh

# ---------------------------------------------------------------------------
# The console itself.
# ---------------------------------------------------------------------------
#
# The frontend, the twenty-two cores and PPSSPP's system files. Immediately
# after the library check above and deliberately so: that one names three
# libraries and says why each is needed, so when one has gone missing its error
# is the legible one. This script's own ldd sweep then catches everything that
# list does not cover, which is most of what the cores link against.
#
# Before this, the image was the OS half only and an installed machine booted
# to a black gamescope session. See the header of install-frontend.sh.
/ctx/install-frontend.sh

# ---------------------------------------------------------------------------
# Record the result.
# ---------------------------------------------------------------------------
rpm -qa | sort > /usr/share/cabinetos/packages-after-strip.txt
log "image now has $(wc -l < /usr/share/cabinetos/packages-after-strip.txt) packages"

group_start "Packages removed by this build"
comm -23 /usr/share/cabinetos/packages-before-strip.txt \
         /usr/share/cabinetos/packages-after-strip.txt
group_end

# ---------------------------------------------------------------------------
# Sanity checks.
# ---------------------------------------------------------------------------
#
# These are the things whose absence would make the image useless but would not
# fail the build. Better to fail loudly here than to find out on real hardware.
group_start "Sanity checks"

check_present() {
    local what="$1"
    local path="$2"
    if [[ -e "${path}" ]]; then
        log "  ok: ${what} (${path})"
    else
        log "  MISSING: ${what} (${path})"
        return 1
    fi
}

failed=0

# cage is the middle rung of the session's compositor ladder — the one that
# makes a GPU-less VM a usable development target, because it renders in
# software where gamescope refuses to. Looks like desktop cruft; is not.
if command -v cage >/dev/null 2>&1; then
    log "  ok: cage ($(command -v cage))"
else
    log "  MISSING: cage — the software-rendering session fallback"
    failed=1
fi

# gamescope is the compositor Phase 2 and Phase 5 are built on. If a removal
# ever takes it out, the project stops. Checked via PATH rather than a fixed
# location, because Bazzite sources it from the terra repo and may move it.
if command -v gamescope >/dev/null 2>&1; then
    log "  ok: gamescope ($(command -v gamescope))"
else
    log "  MISSING: gamescope is not on PATH"
    failed=1
fi

# The kernel. bootc container lint would also catch this, but the error here is
# more legible.
check_present "kernel modules directory" /usr/lib/modules || failed=1

# SSH must be enabled — Phases 2 to 5 are developed over it. See open question 8.
# Either the classic service or socket activation counts.
if systemctl is-enabled sshd.service >/dev/null 2>&1; then
    log "  ok: sshd.service is enabled"
elif systemctl is-enabled sshd.socket >/dev/null 2>&1; then
    log "  ok: sshd.socket is enabled (socket activation)"
else
    log "  MISSING: sshd is not enabled by either service or socket"
    failed=1
fi

# The session must be enabled, or the machine boots to a console — which is
# Phase 1 behaviour, not Phase 2.
if systemctl is-enabled cabinetos-session.service >/dev/null 2>&1; then
    log "  ok: cabinetos-session.service is enabled"
else
    log "  MISSING: cabinetos-session.service is not enabled"
    failed=1
fi

# SELinux will refuse to execute the session script from the wrong place — this
# cost a debugging round on the VM (203/EXEC from /etc). /usr/bin is correct and
# gets labelled bin_t automatically.
check_present "session script" /usr/bin/cabinetos-session || failed=1

# The emulator flatpak units. Enabled in configure-flatpaks.sh, checked again
# here for the same reason everything else in this block is: absence would not
# fail the build, it would just mean no PS3, Xbox or Switch on the machine and
# nothing saying so.
for unit in cabinetos-flatpak-setup.service cabinetos-flatpak-setup.timer; do
    if systemctl is-enabled "${unit}" >/dev/null 2>&1; then
        log "  ok: ${unit} is enabled"
    else
        log "  MISSING: ${unit} is not enabled"
        failed=1
    fi
done

check_present "emulator flatpak manifest" /usr/share/cabinetos/flatpaks.list || failed=1

# The console. install-frontend.sh already asserts each of these as it puts it
# there; they are here as well because this block is the list somebody reads to
# find out what an image is supposed to contain, and "the frontend" belongs on
# it more than anything else does.
check_present "the frontend" /usr/bin/cabinetos-frontend || failed=1
check_present "the cores" /usr/lib/cabinetos/cores || failed=1
check_present "PPSSPP's system files" /usr/share/cabinetos/system/PPSSPP/compat.ini || failed=1

# PLAYSTATION 2, WHICH IS NOT A CORE AND SO IS NOT COVERED BY THE LINE ABOVE.
#
# All four are checked rather than just the emulator, because each absence
# fails differently and all of them fail QUIETLY:
#
#   the .so        catalog.cpp reports PlayStation 2 as "not built on this
#                  console yet" and the library carries on — 71 games gone,
#                  and the console looks entirely normal
#   the libraries  it is there and cannot dlopen; the failure arrives at the
#                  moment somebody starts a game
#   the resources  PCSX2 refuses to start. Not a warning, not a degraded
#                  picture: it does not boot
check_present "the PlayStation 2 emulator" /usr/lib/cabinetos/cores/cabinetos-ps2.so || failed=1
check_present "PCSX2's rapidyaml" /usr/lib/cabinetos/cores/libryml.so.0.10.0 || failed=1
check_present "PCSX2's c4core" /usr/lib/cabinetos/cores/libc4core.so.0.2.8 || failed=1
check_present "PCSX2's resources" /usr/share/cabinetos/system/pcsx2/resources/GameIndex.yaml || failed=1

# The session must actually RUN the frontend. It ran `sleep infinity` until
# 2026-09-19, which is a session that starts, takes the display, and draws
# nothing — indistinguishable on a television from a machine that failed to
# boot.
if grep -q 'CABINETOS_APP:-/usr/bin/cabinetos-frontend' /usr/bin/cabinetos-session; then
    log "  ok: the session runs the frontend"
else
    log "  MISSING: cabinetos-session does not default to the frontend"
    failed=1
fi

# Where the console keeps games and saves. Created at every boot rather than
# built into the image, because /var in a bootc image is unpacked from the
# FIRST image only and never updated — see the file itself.
check_present "the storage root's tmpfiles rule" /usr/lib/tmpfiles.d/cabinetos.conf || failed=1

# The default target must be multi-user. The session is pulled in by it; a
# graphical.target default would try to start a desktop.
if [[ "$(readlink -f /usr/lib/systemd/system/default.target)" == *multi-user.target ]]; then
    log "  ok: default target is multi-user.target"
else
    log "  MISSING: default target is not multi-user.target"
    failed=1
fi

group_end

if [[ "${failed}" -ne 0 ]]; then
    log "sanity checks FAILED"
    exit 1
fi

log "CabinetOS build complete"
