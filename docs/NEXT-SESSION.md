# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Wii is merged (#173, in #205; PROJECT.md open question 35): pad games play,
Remote games greyed until #200. Xbox 360 (#192), Xbox (#172), PS3 (#171) and
Switch (#169, #170) play as programs of their own on `standalone.h` and
`vpad.h`. Check `bootc status` on the A9 for the image it runs. Before any
testing push that adds or changes an install script, run it on the A9 on the
image and check it as `cabinet` (memory). **Never tell MMagTech something is
unrecoverable or safe to delete before every cause is checked** (memory).

## Next: milestone 2, New systems

1. **#174 Wii U (Cemu).** The research is done and posted on #174 (Cemu's
   source at `4e3c824faa`, Batocera, GameTDB, the four games on RomM, now
   found again under `Nintendo Wii U/roms`). Start by walking it through
   with MMagTech and taking the four decisions listed there: which Cemu
   (v2.6 or main built from source; test both on the A9 first, Cemu #1176),
   GamePad-only games (Captain Toad), graphic packs, Wii Remotes in Wii U
   games. Then a hand test on the A9, as Xbox 360 had, then build on
   `standalone.h`. *Lessons: emulators.*
2. **#204**, Dolphin's log level: one line, with the next testing push.
3. **#200**, real Wii Remotes, when MMagTech's Remotes arrive (2026-10-03):
   the test list is in the issue.
