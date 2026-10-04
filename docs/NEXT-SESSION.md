# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Milestone 3. **#200, real Wii Remotes, is on `testing`** (branch
`wii-remotes`, up to date with main): the bridge, its udev rules, the search
service and the polkit rule are in the image, dry-run on the A9 2026-10-03.
Passed on the TV from the hand build: everything in the #200 test record,
plus the setup replay (pointer right after a mid-game power cycle) and the
ack fix (Geometry Wars after Wild West Guns, no "Communication interrupted").
The Wii Remotes panel was judged; Back and a finished pairing now return to
it, and the sensor bar is asked with the first Remote only.

PCSX2/pcsx2#15040 was closed by the PCSX2 team 2026-10-03 (no embedding API,
no AI contributions). Moved out: Wii U Remotes in Cemu (#231), the empty
battery in the Wii HOME menu (#230), both After first release.

## Next

1. **#200 on the testing image**, with MMagTech at the TV, steps first:
   "Pair a Wii Remote" end to end (remove both, pair one: sensor bar asked;
   pair the second: not asked; lands in the panel on the new Remote), then a
   Nunchuk game with a mid-game power cycle, proving from the log that
   `/usr/libexec/cabinetos-wii-bridge` is the bridge running. Merge on his
   "merge"; close #200; delete `wii-remotes` on his go.
2. **#226, PS2 without the copy**: give PCSX2 its own window, shown by
   gamescope with the pause menu over it as for the separate emulators;
   measure in the real path with frames.py before raising Quality. Upstream
   said no, so this is the route.
3. Then #63 phase 2 (ROADMAP).

Owed, small: move the option check out of `cores/build-core.sh` into its own
script, so editing the check stops rebuilding all 22 cores (it did once on
2026-10-02). Not done in the #200 image, which does not touch the cores.
