# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-05: #221 merged (games lock to the screen, one frame per refresh;
PROJECT.md has the measurements). MMagTech judged it on the TV: sounds the
same, and the difference is too small to see, which matches the numbers (the
hitches left are #253's). Filed today: #253 (4K hitch, gamescope, cause
open), #254 (N64 makes 3.4% extra sound), #255 (high-refresh monitors), all
after the first release; #256 (test on his LG C1 right before release).
#194 was started on branch `remove-person` (design option A: send a removed
person's owed saves with their own login); test it with Claire's account,
which MMagTech said may be removed and re-added.

## Next

1. **#194, removing a person deletes their folder** (milestone 7), designed
   with MMagTech 2026-10-04 and written on the issue: send what is owed
   first; if anything cannot be sent, "Some saves haven't reached RomM" /
   Remove anyway · Cancel (no count); then delete the folder and release
   their downloads. Test on a scratch account: it deletes data.
2. **Offline play (#88)**, milestone 4: start with the scenario walk-through.
   Discussed 2026-10-04: the narrow version (no server: Home shows the games
   on the drive and they play). Saves already stay on the console and the
   "which copy wins" rule is built (`main.cpp`, near line 849). Missing:
   reaching Home without a server, covers saved at keep time, owed-save retry,
   and a backup of RomM's copy when another device saved in between.
