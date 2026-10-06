# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-06: offline play (#88), the cartridge-save fix (#259) and the build's
Steam-session fallback (#270) merged in #264; image 2026.10.06 is `latest`.
The A9 now sits at the LG C1, on Wi-Fi at **192.168.1.109** (not the cable
address): run `tools/ui-loop.sh` with `CABINETOS_A9=cabinet@192.168.1.109`.

## Next

One session, in this order, one testing image judged together, then merged.

1. **120 Hz and VRR on the C1 (#256).** The console picks the TV's
   resolution but always 60 Hz; ask for 120 when offered. The C1 reports VRR
   40-120 Hz to the GPU (Game Optimizer on). Then check games still pace
   cleanly (`[pace]` lines) with VRR.
2. **The favourite button (#267).** A heart or a star by the title on the
   game's page, reached with Up from Play; build both, MMagTech picks on the
   TV. Works offline, sent when the server is back.
3. **Player 2 in Wild West Guns (#269).** Two Remotes work in Mario Kart Wii
   and Bit.Trip Beat; this game says "register a second Remote". Start with
   Dolphin's Wii Remote log and the `BT.DINF` list before and after a launch.
   If it turns out deep, say so and decide with MMagTech.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md).
