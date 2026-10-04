# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Milestone 3. **#236, ext4 drives, built on `ext4-drives`**: ext4 accepted
beside exFAT and NTFS; Format on any drive with no file under `CabinetOS/`,
the confirm saying what is on it; Format makes ext4 "Games" with
`CabinetOS/` and `SteamLibrary/`; `SteamLibrary/` added to Steam's list at
each handover; Bazzite's three automounters removed. Checked on the A9 before
the testing push: the udisks ext4 call (label, Linux type, `-m 0`), the claim
helper on a root-owned ext4, the automounter removal in podman, the Steam
list writer against the A9's own file. Judged on the TV and merged if this
file is on main.

## Next

1. If not merged: the TV round on the testing image (the SanDisk stick),
   then merge on his go.
2. Then #63 phase 2 (ROADMAP).

Owed, small: move the option check out of `cores/build-core.sh` into its own
script, so editing the check stops rebuilding all 22 cores (it did once on
2026-10-02). Do it with the next change that touches the cores.
