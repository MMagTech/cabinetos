#!/usr/bin/env bash
#
# ===========================================================================
#  SSH IS ENABLED BY DEFAULT IN THIS IMAGE. THAT IS DELIBERATE, AND TEMPORARY.
# ===========================================================================
#
# Phases 2 through 5 consist of booting images and finding out why they did not
# behave as expected. Doing that without a shell is a different and much worse
# project. Contribution has the same requirement. So `sshd` is on from Phase 1.
#
# This is correct for a development artifact and WRONG for a console handed to
# somebody else.
#
# PHASE 6 MUST:
#   1. Flip the default to off.
#   2. Put SSH and SFTP behind the hidden developer mode toggle.
#   3. Replace password authentication with key-based auth (open question 9).
#   4. Surface in the UI that SSH is listening whenever it is.
#
# Until then, do not install CabinetOS on a machine exposed to an untrusted
# network. See docs/PROJECT.md, open questions 8 and 9.

source /ctx/lib.sh

group_start "Enabling SSH for development"

# openssh-server should already be present on the Fedora Atomic base. Installed
# explicitly anyway, so that this does not silently become a no-op if a future
# base image drops it or if a removal above takes it out.
if is_installed openssh-server; then
    log "openssh-server already present"
else
    log "openssh-server missing from base, installing"
    dnf5 -y install openssh-server
fi

# Enabled rather than assumed-enabled. Fedora Atomic images generally ship sshd
# enabled already, but "generally" is not a thing to build a development
# workflow on.
#
# Fedora can run sshd either as a classic always-listening service or via socket
# activation, and the two conflict if both are enabled. Prefer whichever the
# base already uses rather than forcing one and breaking the other.
if systemctl is-enabled sshd.socket >/dev/null 2>&1; then
    log "sshd.socket is enabled (socket activation) — leaving it alone"
else
    log "enabling sshd.service"
    systemctl enable sshd.service
fi

# SFTP, so a frontend build can be pushed to a running console without
# reflashing it. Fedora's sshd_config enables the sftp subsystem by default;
# this asserts it rather than trusting it, and fails the build if it is absent
# so the loss is noticed here and not when a file transfer silently fails.
if grep -rqE '^[[:space:]]*Subsystem[[:space:]]+sftp' /etc/ssh/sshd_config /etc/ssh/sshd_config.d/ 2>/dev/null; then
    log "sftp subsystem is configured"
else
    log "ERROR: no sftp Subsystem line found in the sshd configuration"
    exit 1
fi

# A marker file, so a running machine can be checked for this without reading
# the image's build log. Phase 6 removes both the marker and this script.
mkdir -p /usr/share/cabinetos
cat > /usr/share/cabinetos/DEVELOPMENT-IMAGE <<'MARKER'
This CabinetOS image has SSH enabled by default.

That is deliberate for Phases 1-5, which are developed by booting images and
inspecting them. It is not the shipping configuration. Phase 6 moves SSH behind
the hidden developer mode toggle and turns it off by default.

Do not install this image on a machine exposed to an untrusted network.
MARKER

group_end
