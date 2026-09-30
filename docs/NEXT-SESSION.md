# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Xbox 360 plays through Xenia Edge (#192, merged in #197; PROJECT.md open
question 34). Xbox (#172), PS3 (#171) and Switch (#169, #170) play the same
way, as programs of their own on `standalone.h` and `vpad.h`. Check `bootc
status` on the A9 for the image it runs. Before any testing push that adds or
changes an install script, run it on the A9 on the image and check it as
`cabinet` (memory). **Never tell MMagTech something is unrecoverable or safe
to delete before every cause is checked** (memory).

## Next: milestone 2, New systems

1. **#173 Wii.** Research and a walk-through first, no code, as Xbox and Xbox
   360 had: what a real user does with it, the emulator, its build and its
   options against RetroArch and Batocera, saves and how they travel, the
   controllers (the Wii Remote, the Nunchuk, the Classic Controller), formats
   on RomM sniffed by content. *Lessons: emulators.* One rule for every game;
   never patch the emulator.
2. Then #174 Wii U.
