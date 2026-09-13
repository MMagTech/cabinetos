#!/usr/bin/env bash
#
# CabinetOS image build.
#
# Called from the Containerfile with build_files/ mounted at /ctx and
# system_files/ mounted at /system_files.
#
# Phase 1 is subtractive only: nothing is installed here. The gaming stack comes
# from Bazzite and is kept as-is. Phase 2 onwards adds the session and the
# frontend.

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
    /usr/lib/bootc/install/20-cabinetos.toml
do
    if [[ -e "${expected}" ]]; then
        log "  overlaid: ${expected}"
    else
        log "  ERROR: ${expected} did not land"
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
# HDMI-CEC.
# ---------------------------------------------------------------------------
#
# Runs last, after everything that could remove a package. CEC is a hard
# requirement and every package it needs looks like cruft in a package list —
# this fails the build rather than letting one quietly disappear.
/ctx/require-cec.sh

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
