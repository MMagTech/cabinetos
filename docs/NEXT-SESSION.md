# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-05: #221 merged and promoted (image 2026.10.05.2). #194 is built on
branch `remove-person`, tested headless on the A9 (a copy, then Claire's
real account: her owed test save was sent with her login, then her folder
and login went). Claire is therefore removed from the A9; her old folder is
backed up at `~/fb/claire-backup-20261005` on the A9. RomM holds a test save
`cabinet-test-194.srm` on Claire's account for Aerostar (GB, rom 2); delete
it on MMagTech's word. Filed 2026-10-05, after the release: #253 (4K hitch,
gamescope, cause open), #254 (N64 extra sound), #255 (high-refresh
monitors); #256 (LG C1 test before release).

## Next

1. **#194 on the TV, then testing and merge.** The screens are untested
   (the A9 has a PIN, so the test used `--remove-account`). MMagTech
   re-adds Claire (pairing, approved on RomM as Claire). Deploy the branch
   with `tools/ui-loop.sh`. Plant a pre-2026-09-26 style marker (byte count
   only, e.g. `users/13 - claire/pending/1-old.srm.pending` holding `5`), so
   the warning shows without touching the network. He removes Claire from
   Settings > Accounts: "Some saves haven't reached RomM", Cancel first
   (she stays), then Remove anyway (she goes). He judges the words. Then
   push to `testing` (plain push, no force), check the A9, merge on "merge".
2. **Offline play (#88)**, milestone 4: start with the scenario walk-through.
   Discussed 2026-10-04: the narrow version (no server: Home shows the games
   on the drive and they play). Saves already stay on the console and the
   "which copy wins" rule is built (`main.cpp`, near line 849). Missing:
   reaching Home without a server, covers saved at keep time, owed-save retry,
   and a backup of RomM's copy when another device saved in between.
