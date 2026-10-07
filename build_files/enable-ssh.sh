#!/usr/bin/env bash
#
# ===========================================================================
#  SSH IS INSTALLED, AND NOTHING LISTENS UNTIL SOMEONE AT THE TV SAYS SO.
# ===========================================================================
#
# ONE IMAGE FOR EVERYONE (MMagTech, 2026-10-07; issue #134). There is no
# separate development image any more. Two switches, both off by default, and
# off means the program that would answer is not running:
#
#   File access        Settings, Storage. Port 22: SFTP to the saves and
#                      games folders only, a password shown on the TV.
#   Developer access   Settings, System. Port 2222: the full command line,
#                      the same password (or a key). "When they turn it on,
#                      they now are a developer. Same install."
#
# Until 2026-10-07 every image was a development image: the shell on 2222 was
# always on, key only, and a DEVELOPMENT-IMAGE marker said so. That is gone.
# Why it changed: a console installed fresh from the new installer (#107) has
# no key, so it could not be reached at all; and a console handed to anyone
# else must listen to nothing until its owner turns something on.
#
# See docs/PROJECT.md, open questions 8 and 9, and docs/SETTINGS.md.

source /ctx/lib.sh

group_start "SSH: installed, off until switched on"

# openssh-server should already be present on the Fedora Atomic base. Installed
# explicitly anyway, so that this does not silently become a no-op if a future
# base image drops it or if a removal above takes it out.
if is_installed openssh-server; then
    log "openssh-server already present"
else
    log "openssh-server missing from base, installing"
    dnf5 -y install openssh-server
fi

# TWO SSHDS, ONE PER PORT, both off at boot:
#
#   sshd.service                port 22, File access: SFTP only, a password.
#                               /usr/libexec/cabinetos-files starts it when
#                               File access is turned on, stops it when off.
#   cabinetos-developer.service port 2222, Developer access: a shell, the
#                               same password or a key. The session starts
#                               and stops it by name.
#
# sshd.socket is disabled too: socket activation would open 22 on its own,
# which is the one thing File access being off promises it is not. Nothing
# is enabled here; build.sh fails the image if any of the three is.
systemctl disable sshd.service sshd.socket cabinetos-developer.service >/dev/null 2>&1 || true
log "sshd.service off until File access; cabinetos-developer.service off until Developer access"

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

group_end
