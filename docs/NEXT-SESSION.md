# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Switch plays through Eden, from Play back to Home, with the pause menu
(#169, #170; PROJECT.md open question 32). Check `bootc status` on the A9 for
the image it runs.

## Next: milestone 2, New systems

1. **#171 PS3 (RPCS3), on the launcher Switch built.** `standalone.h` is one
   row per emulator; `vpad.h` gives any emulator virtual controllers. Research
   and a walk-through for MMagTech first, no code: RPCS3's Flathub build (on
   `flatpaks.list` already), firmware from RomM, PKG installs against
   decrypted ISOs (open question 19), where its saves and caches live, how to
   stop and freeze it, its window title, its own dialogs, and what Batocera
   does. *Lessons: emulators (the PS3 notes), image-and-ci.* One rule for
   every game; never patch the emulator (memory, and question 32).
2. Then #172 Xbox, #173 Wii, #174 Wii U.
