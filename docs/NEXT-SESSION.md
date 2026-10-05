# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-05: #221 (games lock to the screen), #194 (Remove and Sign out send
owed saves first) and #108 (CabinetOS boot splash) merged and promoted;
image 2026.10.05.4 is on the A9. #133 closed (not worth the space on the
Storage page; sizes go in the diagnostic report, #195). #228 (sound chip
power) still open: the udev rule did not hold across a reboot because tuned
turns controller power saving back on; next try is through tuned, tested
across a real reboot. Claire's account was removed during #194's test; her
old folder is backed up at `~/fb/claire-backup-20261005` on the A9.

## Next

1. **Offline play (#88), built from the walkthrough on the issue.** Read
   every comment on #88 first: the decisions are there (15 s, every game on
   the drive, the normal Home and Library with fewer games, a shared Recent
   shelf, switching works offline, quiet background reconnects, no Offline
   switch). **Fix #259 first** (cartridge battery saves ignore the local
   copy, a save-loss bug today; leave the three-states rule untouched).
   Two-device conflicts are deferred to #261. Still to judge on the TV: an
   "Offline" label or none, and Search working offline or greyed. Walk the
   build on the A9 with the server stopped (a firewall rule or a wrong
   address via `tools/ui-loop.sh --env`), never by stopping RomM.
