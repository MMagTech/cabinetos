# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-06: #273 merged (Steam picks its own resolution and refresh, the
favourite heart, Search and pairing fixes, a Wii Remote switched on during a
game joins it). Image 2026.10.06.3 was the tested one. Milestones 2, 3 and 4
are done.

The A9 sits at the LG C1, on Wi-Fi at **192.168.1.109**: run
`tools/ui-loop.sh` with `CABINETOS_A9=cabinet@192.168.1.109`.

## Next

1. **RetroAchievements (#74), milestone 5.** Softcore only, decided: no
   hardcore built, hidden or otherwise. Read every comment on #74 first (the
   2026-10-05 research: rcheevos `rc_client`, `rc_libretro` for the libretro
   cores, the PS2 bridge needs its own memory hook; RomM already stores each
   user's `ra_username`). Start with the walkthrough of what real users will
   do with it (docs/lessons, and the memory on walking the scenarios), then
   decide with MMagTech: where sign-in lives, pop-ups during play, what the
   game's page shows. Lessons file: frontend.md (screens, words on screen).

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). After a testing image installs,
verify it from the A9 and list the possible checks; MMagTech decides which.
