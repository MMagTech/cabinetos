# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Milestone 3. **Screen looks (#122) built on `shaders`, PR #249, judged on the
A9 TV 2026-10-04 with the test build, pushed to `testing`.** RetroArch's own
GLSL shaders, unchanged, run by `frontend/src/screenfx.cpp`; one Look row in
the pause menu per system; TV systems start on CRT easymode, handhelds on
LCD 3x; N64, Dreamcast and 3DO get a shorter list. The glow is always on at
Strong, no setting. Decisions and reasons: PROJECT.md, *Shaders, and the glow
around the picture*. DS filed as #248 (stays or goes, milestone 7).

## Next

1. **#227 on `gamescope-flags`** (the base-update check that gamescope
   still takes every flag the session passes): built and dry-run on the A9,
   PR open; merge on MMagTech's go, close #227, delete the branch on his go.
   Only `ci/` changed, so the merge builds main itself (no testing image to
   judge; the image's contents do not change).
2. **#221** (lessons: `emulators.md`, `testing.md`), the last of milestone 3:
   built-in cores have no audio rate control, so a frame repeats or drops
   every 10 to 17 s. Measure on the A9; at the end MMagTech watches a
   scrolling game on the TV.
3. Then milestone 4, offline play (#88): start with the scenario walk-through.
   Discussed 2026-10-04: the narrow version (no server: Home shows the games
   on the drive and they play; saves already stay on the console and the
   "which copy wins" rule is built, `main.cpp:849`); missing is reaching Home
   without a server, covers saved at keep time, owed-save retry, and a backup
   of RomM's copy when another device saved in between.
