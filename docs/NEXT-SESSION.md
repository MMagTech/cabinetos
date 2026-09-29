# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Xbox plays through xemu (#172, merged in #191; PROJECT.md open question 33).
PS3 plays through RPCS3 (#171) and Switch through Eden (#169, #170), all on
`standalone.h` and `vpad.h`. Check `bootc status` on the A9 for the image it
runs. **Never tell MMagTech something is unrecoverable or safe to delete
before every cause is checked** (memory).

## Next: milestone 2, New systems

1. **#192 Xbox 360 (Xenia Edge).** MMagTech moved it ahead of the Wiis on
   2026-09-29 after a hand-run test on the A9: Forza Horizon 2 and Left 4 Dead
   2 held their native 30 FPS with the GPU under 40%. Everything the test
   found is in the issue. Research and a walk-through first, no code, as Xbox
   had: how the console makes and signs in one profile, turns off Xenia's
   desktop dialogs and closes it cleanly (it ignored SIGTERM), where saves
   live and how they travel, formats including a zip named `.iso`, XBLA
   licences, title updates and DLC. The test set-up is still on the A9 in
   `~/x360` (Edge unpacked, `run.sh`, the two games). *Lessons: emulators.*
   One rule for every game; never patch the emulator.
2. Then #173 Wii, #174 Wii U.
