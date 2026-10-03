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

1. **#200, real Wii Remotes: the build.** Tested at the TV 2026-10-02: the
   TechKens pair (Wii PIN refused, an agent's `0000` taken), reconnect on a
   button, power off for good, and Dolphin takes one as player 1 (Mario Kart
   Wii, headless). Everything decided is in `docs/PROJECT.md` question 35,
   "DECIDED, MMagTech 2026-10-02"; the test record is on #200. Build it on
   branch `wii-remotes`, then judge on the TV: the pointer on the sensor
   bar, test 4 (pads do not stutter with two TechKens connected) and test 5
   (two Remotes and a pad in Mario Kart).
2. **#226, PS2 without the copy**: give PCSX2 its own window, shown by
   gamescope with the pause menu over it as for the separate emulators;
   measure in the real path with frames.py before raising Quality.
3. Then #63 phase 2 (ROADMAP).

Owed, small: move the option check out of `cores/build-core.sh` into its own
script, so editing the check stops rebuilding all 22 cores (it did once on
2026-10-02). Do it with the next change that touches the image.
