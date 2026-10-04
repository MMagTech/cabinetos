# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Milestone 3. **#223, Steam, built on `steam-handoff`**: "Switch to Steam" in
the Start menu hands over to Steam's Big Picture and back (PROJECT.md open
question 37). Judged on the A9 by MMagTech 2026-10-03/04; merged if this file
is on main. Games played well under it.

## Next

1. **#226, PS2 without the copy**: give PCSX2 its own window, shown by
   gamescope with the pause menu over it as for the separate emulators;
   measure in the real path with frames.py before raising Quality.
   **MMagTech's ask: this should need little of him.** Measure, build and
   compare on the A9 yourself; bring him to the TV once, at the end.
2. **#236, ext4 for extra drives, shared with Steam.** Reopened by MMagTech
   2026-10-04: exFAT/NTFS-only was decided without ext4 ever being named to
   him. Walk the proposal on the issue with him (what each format costs, in
   plain words) before building.
3. Then #63 phase 2 (ROADMAP).

Owed, small: move the option check out of `cores/build-core.sh` into its own
script, so editing the check stops rebuilding all 22 cores (it did once on
2026-10-02). Do it with the next change that touches the cores.
