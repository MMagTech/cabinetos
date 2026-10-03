# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Milestone 3. **#63 phase 1 merged in #225** (2026-10-02) with #73, #204, #209
and #217; main promoted the image judged on the A9. PS2's Quality is 3x
because the console copies every PS2 frame off the GPU (#226). Check
**PCSX2/pcsx2#15040** for a reply at the start of the session and tell
MMagTech; answer there only on his go.

## Next

1. **#200, real Wii Remotes.** Pairing a TechKen Remote through BlueZ failed
   on 2026-10-02: found by the limited search (`btmgmt find -l -b`),
   connects, but every PIN request ends "No agent available": BlueZ's Wii
   PIN callback (`plugins/autopair.c`, `wii_pincb`) is not answering. Start
   with one attempt under `bluetoothd -d` to see why (restarting bluetooth
   drops the pads for a few seconds; say so first). The rest of #200's test
   list follows. Hardware: two Remotes with MotionPlus and Nunchuks, a USB
   sensor bar (power only).
2. **#226, PS2 without the copy**: give PCSX2 its own window, shown by
   gamescope with the pause menu over it as for the separate emulators;
   measure in the real path with frames.py before raising Quality.
3. Then #63 phase 2 (ROADMAP).

Owed, small: move the option check out of `cores/build-core.sh` into its own
script, so editing the check stops rebuilding all 22 cores (it did once on
2026-10-02). Do it with the next change that touches the image.
