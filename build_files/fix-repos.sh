#!/usr/bin/env bash
#
# Remove repository definitions whose local GPG key is missing.
#
# Why this exists:
#
# Building an installer ISO makes dnf resolve packages, and dnf reads every
# repository defined in the image when it does. Bazzite leaves a `terra-mesa`
# repository defined but its key file is not present:
#
#   Failed to retrieve GPG key for repo 'terra-mesa': Couldn't open file
#   /etc/pki/rpm-gpg/RPM-GPG-KEY-terra44-mesa
#
# which fails the depsolve and therefore the whole ISO build. Setting the repo
# to `enabled=0` is not enough — Bazzite already does that, and it still broke.
# The definition has to go.
#
# This is written generically rather than hard-coding `terra-mesa`, because the
# class of problem is "a repo we do not use, whose key was never shipped, breaks
# an operation that reads all repos". Upstream can add or rename these. A
# targeted fix would quietly stop working; this one keeps holding.
#
# It only touches repositories that are ALREADY broken — a `file://` GPG key
# that does not exist on disk means that repo cannot be installed from by
# anything, so removing its definition takes away nothing that worked.

source /ctx/lib.sh

group_start "Removing repository definitions with missing GPG keys"

python3 - <<'PYTHON'
import configparser
import pathlib
import sys

repo_dir = pathlib.Path("/etc/yum.repos.d")
removed = []

for repo_file in sorted(repo_dir.glob("*.repo")):
    parser = configparser.ConfigParser(strict=False)
    # Repo files use case-sensitive keys; keep them as-is.
    parser.optionxform = str
    try:
        parser.read(repo_file)
    except configparser.Error as exc:
        print(f"[cabinetos]   skipping unparseable {repo_file.name}: {exc}")
        continue

    drop = []
    for section in parser.sections():
        gpgkey = parser[section].get("gpgkey", "")
        # A repo can list several keys, whitespace-separated.
        for key in gpgkey.split():
            if key.startswith("file://"):
                path = pathlib.Path(key[len("file://"):])
                if not path.exists():
                    drop.append(section)
                    removed.append(f"{section} (missing {path})")
                    break

    if not drop:
        continue

    for section in drop:
        parser.remove_section(section)

    if parser.sections():
        with repo_file.open("w") as handle:
            parser.write(handle, space_around_delimiters=False)
        print(f"[cabinetos]   rewrote {repo_file.name}, dropped: {', '.join(drop)}")
    else:
        repo_file.unlink()
        print(f"[cabinetos]   deleted {repo_file.name} (no sections left)")

if removed:
    print(f"[cabinetos]   removed {len(removed)} repo definition(s):")
    for entry in removed:
        print(f"[cabinetos]     {entry}")
else:
    print("[cabinetos]   none found — every repo's GPG key is present")
PYTHON

group_end
