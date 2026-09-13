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
if [[ -d /system_files ]]; then
    log "overlaying system_files/"
    # .gitkeep files exist only to keep empty directories in git.
    find /system_files -name .gitkeep -delete
    cp -avf /system_files/. / >/dev/null
fi

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

# The default target must be multi-user, or Phase 1's done-criterion (boots to a
# console prompt) is not met.
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
