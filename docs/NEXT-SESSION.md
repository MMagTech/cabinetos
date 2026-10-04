# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Milestone 3. **#200, real Wii Remotes, merged in #232** (2026-10-03); main
promoted the image judged on the A9 (2026.10.04.2). PS2's Quality is 3x
because the console copies every PS2 frame off the GPU (#226).
PCSX2/pcsx2#15040 was closed by the PCSX2 team (no embedding API, no AI
contributions), so our own route is the only one.

## Next

1. **#223, Steam: one entry that hands over to Big Picture.** Moved ahead
   of PS2 by MMagTech, 2026-10-03; he has a full day for it. The audit
   (read-only, 2026-10-02) is the issue's fourth comment, and **his
   decisions are in its other comments**: read all of them before calling
   anything open. In short: nothing of Steam runs or shows for someone who
   never picks it; the 32-bit libraries stay; the handoff screen and the
   size setting appear only after Steam is picked; picking it by accident
   has a clear way back on the first-run screen; a setting removes Steam
   completely and gives the disk back; and kept games still overflow to
   another drive exactly as before (`storage::spaceOf` must leave Steam's
   slice out). Order: go through the audit's findings with him (the
   cardwire blocker, the slice as a file, the default size), walk what
   real users will do with it, then build on his go, then the A9 TV.
2. **#226, PS2 without the copy**: give PCSX2 its own window, shown by
   gamescope with the pause menu over it as for the separate emulators;
   measure in the real path with frames.py before raising Quality.
   **MMagTech's ask: this should need little of him.** Measure, build and
   compare on the A9 yourself; bring him to the TV once, at the end.
3. Then #63 phase 2 (ROADMAP).

Owed, small: move the option check out of `cores/build-core.sh` into its own
script, so editing the check stops rebuilding all 22 cores (it did once on
2026-10-02). Do it with the next change that touches the cores.
