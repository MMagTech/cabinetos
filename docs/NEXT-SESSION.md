# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

PS3 plays through RPCS3's official build, in the image, from Play back to Home
(#171, merged in #186; PROJECT.md open questions 19 and 21). Switch plays
through Eden (#169, #170). Both run on `standalone.h`, one row per emulator,
and `vpad.h`'s virtual controllers. Check `bootc status` on the A9 for the
image it runs. **Never tell MMagTech something is unrecoverable or safe to
delete before every cause is checked** (memory).

## Next: milestone 2, New systems

1. **#172 Xbox (xemu), on the launcher Switch and PS3 built.** Research and a
   walk-through for MMagTech first, no code: xemu's Flathub build (on
   `flatpaks.list`), the BIOS, MCPX boot ROM and hard-drive image and how they
   come from RomM, the game format (XISO), where saves live (inside the
   hard-drive image) and whether they can sync, how to stop and freeze it, its
   own dialogs and window, controllers, and what Batocera does. Check whether
   its Flatpak has a warning like RPCS3's. *Lessons: emulators, image-and-ci.*
   One rule for every game; never patch the emulator.
2. Then #173 Wii, #174 Wii U.
