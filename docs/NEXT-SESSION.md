# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-04, a long session: screen looks (#122, PR #249), the gamescope flag
check (#227, PR #251) and a batch of quick fixes (PR #252: #237, #188, #196,
#241, #184, #185, #212, #117, #213, #137, #118, #81), all judged on the A9
and merged. #63 closed (phase 3 is #250, after the first release, with #210
and #168); #124, #231 and #119 closed; #157 after the release; DS filed as
#248. 26 issues remain before the first release.

## Next

1. **#221** (lessons: `emulators.md`, `testing.md`), the last of milestone 3:
   built-in cores have no audio rate control, so a frame repeats or drops
   every 10 to 17 s. Measure on the A9 with frames.py first; at the end
   MMagTech watches a scrolling game on the TV.
2. **#194, removing a person deletes their folder** (milestone 7), designed
   with MMagTech 2026-10-04 and written on the issue: send what is owed
   first; if anything cannot be sent, "Some saves haven't reached RomM" /
   Remove anyway · Cancel (no count); then delete the folder and release
   their downloads. Test on a scratch account: it deletes data.
3. **Offline play (#88)**, milestone 4: start with the scenario walk-through.
   Discussed 2026-10-04: the narrow version (no server: Home shows the games
   on the drive and they play). Saves already stay on the console and the
   "which copy wins" rule is built (`main.cpp`, near line 849). Missing:
   reaching Home without a server, covers saved at keep time, owed-save retry,
   and a backup of RomM's copy when another device saved in between.
