# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Xbox plays through xemu (#172, branch `xbox-xemu`; PROJECT.md open question
33): saves travel through RomM on and off each game's own drive, one EEPROM
for every console, never the Xbox dashboard. Judged on the TV 2026-09-29,
two players and rumble included. If it has not merged yet, that is first:
check the `testing` image on the A9, then merge on MMagTech's word. PS3 plays
through RPCS3 (#171) and Switch through Eden (#169, #170), all on
`standalone.h` and `vpad.h`. Check `bootc status` on the A9 for the image it
runs. **Never tell MMagTech something is unrecoverable or safe to delete
before every cause is checked** (memory).

## Next: milestone 2, New systems

1. **#173 Wii.** Research and a walk-through for MMagTech first, no code, as
   Xbox had: the emulator (Dolphin already plays GameCube in-process), game
   formats, saves (the Wii's NAND) and whether they can travel as zips, Wii
   Remotes and what a controller-only console does without a pointer, and
   what Batocera does. *Lessons: emulators.* One rule for every game; never
   patch the emulator.
2. Then #174 Wii U (Cemu).
