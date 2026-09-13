#!/usr/bin/env bash
#
# Disable repositories that break an image-time package resolve.
#
# Why this exists:
#
# Building an installer ISO makes dnf resolve packages, and dnf reads every
# ENABLED repository when it does. That failed with:
#
#   Failed to retrieve GPG key for repo 'terra-mesa': Couldn't open file
#   /etc/pki/rpm-gpg/RPM-GPG-KEY-terra44-mesa
#
# The key is not actually missing — it is present on a deployed system. But in a
# bootc CONTAINER image, /etc content lives at /usr/etc and /etc is only
# populated at deploy time, so an absolute `file:///etc/...` gpgkey does not
# resolve while the image is being read as a container.
#
# terra-mesa should never have been consulted anyway. Bazzite installs Valve's
# patched Mesa from it and then disables the repo — but on the built image it is
# still `enabled=1`, so Bazzite's intent did not survive. CabinetOS installs
# nothing from it either; the Mesa packages are already in the image and
# versionlocked.
#
# So: turn off the repositories we do not install from at image-build time, and
# assert that the ones we do need survive.
#
# TWO EARLIER VERSIONS OF THIS SCRIPT WERE WRONG. The first deleted repos whose
# `file://` GPG key did not exist, without expanding `$releasever`/`$basearch` —
# so every repo looked broken and it deleted all of them, including Fedora's,
# giving "There are no enabled repositories". The second thought the file had
# duplicate `enabled` keys; those lines belonged to the adjacent
# `[terra-mesa-source]` section. Hence the named list and the safety check:
# narrow, explicit, and loud when it is wrong.

source /ctx/lib.sh

group_start "Disabling repositories that break an image-time depsolve"

python3 - <<'PYTHON'
import configparser
import pathlib
import sys

# Repositories to force off. Each needs a reason.
DISABLE = {
    # Valve's patched Mesa. Bazzite installs from it at build time and means to
    # disable it afterwards, but the built image still has enabled=1. Its
    # file:// GPG key cannot be read at image-build time, which breaks any
    # depsolve — and we install nothing from it.
    "terra-mesa": "Mesa is already installed and versionlocked; breaks ISO depsolve",
    "terra-mesa-source": "source repo for the above",
}

# Repositories that must still be enabled afterwards, or the image is broken.
REQUIRED = {"fedora", "updates"}

repo_dir = pathlib.Path("/etc/yum.repos.d")
changed = []

for repo_file in sorted(repo_dir.glob("*.repo")):
    parser = configparser.ConfigParser(strict=False)
    parser.optionxform = str
    try:
        parser.read(repo_file)
    except configparser.Error as exc:
        print(f"[cabinetos]   SKIPPING unparseable {repo_file.name}: {exc}")
        continue

    touched = False
    for section in parser.sections():
        if section in DISABLE and parser[section].get("enabled", "1").strip() != "0":
            parser[section]["enabled"] = "0"
            parser[section]["enabled_metadata"] = "0"
            touched = True
            changed.append(f"{section} ({DISABLE[section]})")

    if touched:
        with repo_file.open("w") as handle:
            parser.write(handle, space_around_delimiters=False)

if changed:
    print(f"[cabinetos]   disabled {len(changed)} repo(s):")
    for entry in changed:
        print(f"[cabinetos]     {entry}")
else:
    print("[cabinetos]   nothing to disable")

# Safety check. An earlier version of this script disabled every repository in
# the image and the failure only surfaced two builds later, during an ISO
# depsolve. Assert the invariant here so that cannot happen quietly again.
enabled = []
for repo_file in sorted(repo_dir.glob("*.repo")):
    parser = configparser.ConfigParser(strict=False)
    parser.optionxform = str
    try:
        parser.read(repo_file)
    except configparser.Error:
        continue
    for section in parser.sections():
        if parser[section].get("enabled", "1").strip() == "1":
            enabled.append(section)

print(f"[cabinetos]   {len(enabled)} enabled repositories remain: {', '.join(sorted(enabled))}")

if not enabled:
    print("[cabinetos]   ERROR: no enabled repositories left")
    sys.exit(1)
if not REQUIRED & set(enabled):
    print(f"[cabinetos]   ERROR: none of {sorted(REQUIRED)} are enabled")
    sys.exit(1)
PYTHON

group_end
