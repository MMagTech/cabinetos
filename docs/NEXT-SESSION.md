# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

PS3 is built on branch `ps3-rpcs3` (#171) and pushed to `testing`: RPCS3's
official build in the image, Play downloads, installs and starts a PKG game,
ISOs start as they are. Decisions and measurements: PROJECT.md open question
19, *PS3 is built*, and 21, *RPCS3 leaves Flathub*. Played on the TV through
`tools/ui-loop.sh` with `--env CABINETOS_BINARY_rpcs3=...`: Super Stardust HD
(PKG) and God of War III (ISO, the one good ISO in the library; MMagTech is
deleting the others until he has new dumps). **Never tell him something is
unrecoverable or safe to delete before every cause is checked** (memory).

## Next: milestone 2, New systems

1. **#171, finish PS3.** *Lessons: emulators (the PS3 notes), testing.*
   - Put the A9 on the `testing` image and check RPCS3 comes from
     `/usr/lib/cabinetos/rpcs3` (no `--env`), and that Flathub's RPCS3 was
     removed by `cabinetos-flatpak-setup`.
   - At the TV, steps first and wait for "go": God of War III, pause, Exit to
     Home. It must be back on Home in seconds (the paused state now answers
     the virtual controllers; before, a minute). Dragon's Crown: Square left,
     Triangle top on its prompts.
   - Talk through **DLC and updates** with MMagTech (PROJECT.md 19, *Designed,
     not built*). Nothing to test against yet.
   - The fixed ISO build script (xorriso only, Joliet, a contents check) was
     sent to him on 2026-09-28 for new dumps; it lives on his server, not here.
   - He judges, then merge on his explicit go; close #171.
2. Then #172 Xbox, #173 Wii, #174 Wii U.
