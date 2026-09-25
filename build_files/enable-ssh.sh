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
# WHAT PHASE 6 DOES ABOUT IT CHANGED ON 2026-09-19, and this list is shorter
# than the one it replaces. MMagTech's call, and the reasoning is his:
# **most people will never use this, and most of them would not want it on.**
# So the console ships closed and the people who want it turn it on.
#
# PHASE 6 MUST:
#   1. Flip the default to off. A console ships listening to nothing.
#   2. Put it behind an ORDINARY, VISIBLE row in Settings — not the hidden
#      developer-mode toggle this file used to call for. Reaching your own
#      saves is a feature, not a developer act; hiding it only stops the
#      people who need it from finding it.
#   3. Turn it on and the screen shows the address, the user name and a
#      password THIS MACHINE generated for itself. Different on every console,
#      and nothing published contains it. Turn it off and sshd stops.
#   4. The switch gives FILE ACCESS, not a shell. "Copy my saves off" and
#      "give me a root shell" are different asks with very different risk, and
#      only the first is something a console should offer in Settings.
#
# A SHELL STAYS ON THE DEVELOPMENT IMAGE, which is this script and the marker
# it writes. That distinction already exists in the build; Phase 6 uses it
# rather than inventing a second one.
#
# Until then, do not install CabinetOS on a machine exposed to an untrusted
# network. See docs/PROJECT.md, open questions 8 and 9, which are ANSWERED.

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

# TWO SSHDS, ONE PER PORT, as of 2026-09-25 (File access, docs/SETTINGS.md,
# Storage; the rules are /etc/ssh/sshd_config.d/30-cabinetos.conf).
#
#   cabinetos-dev-ssh.service   port 2222, key only, a shell. Enabled here,
#                               and it only runs on an image carrying the
#                               DEVELOPMENT-IMAGE marker written below.
#   sshd.service                port 22, File access: SFTP only, a password.
#                               NOT enabled. /usr/libexec/cabinetos-files
#                               starts it when File access is turned on in
#                               Settings and stops it when it is turned off.
#
# THE DEVELOPMENT SHELL MOVED FROM 22 TO 2222 HERE. Anything that reached a
# console with `ssh cabinet@<address>` now needs `-p 2222`, or a Host entry
# with `Port 2222`. Port 22 answers only while File access is on, and then
# with SFTP and nothing else.
#
# sshd.socket is disabled too: socket activation would open 22 on its own,
# which is the one thing File access being off promises it is not.
systemctl disable sshd.service sshd.socket >/dev/null 2>&1 || true
systemctl enable cabinetos-dev-ssh.service
log "sshd.service off until File access; cabinetos-dev-ssh.service on (port 2222)"

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
This CabinetOS image has SSH enabled by default, with a shell.

That is deliberate for Phases 1-5, which are developed by booting images and
inspecting them. It is NOT the shipping configuration.

A shipping console starts with this switched off and offers it as an ordinary
row in Settings: file access over SFTP, no shell, and a password the machine
generates for itself and shows on screen. A shell is a development thing and
stays on images carrying this file.

Do not install this image on a machine exposed to an untrusted network.
MARKER

group_end
