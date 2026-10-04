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

1. **#226, PS2 without the copy**: give PCSX2 its own window, shown by
   gamescope with the pause menu over it as for the separate emulators;
   measure in the real path with frames.py before raising Quality.
   **MMagTech's ask (2026-10-03): this should need little of him.** The PS2
   decisions are made and it runs; the work is making it run better. Measure,
   build and compare on the A9 yourself; bring him to the TV once, at the
   end, to judge the picture and the feel, steps first. Never build on the A9
   while he is playing.
2. Then #63 phase 2 (ROADMAP). Steam (#223) is recorded as straight after
   #63's three phases; his decisions on it are in the issue's comments.

Owed, small: move the option check out of `cores/build-core.sh` into its own
script, so editing the check stops rebuilding all 22 cores (it did once on
2026-10-02). Do it with the next change that touches the cores.
