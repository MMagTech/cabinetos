# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-05: #221 merged (games lock to the screen). #194 merged: Remove
sends a person's unsent saves with their own login, then deletes their
folder; Sign out now sends everyone's first, and both ask "Saves waiting to
upload will be lost." only for what could not be sent. Judged on the TV.
Claire's account was removed from the A9 during the test (MMagTech re-adds
her when he wants); her old folder is backed up at
`~/fb/claire-backup-20261005` on the A9. RomM holds a junk test save
`cabinet-test-194.srm` on Claire's account (Aerostar, rom 2); MMagTech was
told to delete it. Filed, after the release: #253 (4K hitch, gamescope),
#254 (N64 extra sound), #255 (high-refresh monitors); #256 (LG C1 test
before release).

## Next

1. **Offline play (#88)**, milestone 4: start with the scenario walk-through.
   Discussed 2026-10-04: the narrow version (no server: Home shows the games
   on the drive and they play). Saves already stay on the console and the
   "which copy wins" rule is built (`main.cpp`, near line 849). Missing:
   reaching Home without a server, covers saved at keep time, owed-save retry,
   and a backup of RomM's copy when another device saved in between.
