# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-06, branch `c1-favourite-wii` (pushed, no PR yet, no testing image):
- #256 done at the C1: Steam's own Display > Resolution pick (incl. 4K 120)
  now reaches the TV (no forced size on Steam's gamescope); the console stays
  at 60 by decision. Findings on #256.
- #267 done and judged: the heart beside the title (Up from Play), sent to
  RomM's Favorites collection, kept and sent later when offline. Needs
  `collections.write`: the A9 was re-paired and its kept presses landed.
  Also fixed: Search's keyboard stayed open over a game's page; pairing
  showed the link without its code, then the code twice.
- VRR: confirmed on the C1 for console games and for Steam games. Steam's
  own switch works (in a game: guide + A, Performance, Enable VRR); it is
  saved in Steam's shared profile (gameid 769) and carried to the next game.
  Research on #272: steamos-manager is not involved and stays ruled out.

The A9 sits at the LG C1, on Wi-Fi at **192.168.1.109**: run
`tools/ui-loop.sh` with `CABINETOS_A9=cabinet@192.168.1.109`
(`--session-script` runs the branch's session script).

## Next

1. **Steam VRR (#272):** one check: close and reopen Steam, start a game,
   read `Set VRR enabled` in `~/.local/share/Steam/logs/systemperfmanager.txt`
   and gamescope's `GAMESCOPE_VRR_ENABLED`. Still on: close #272.
2. **Player 2 in Wild West Guns (#269)**, rom 3608 (Mario Kart Wii 208
   works). Start with Dolphin's Wii Remote log and the `BT.DINF` list in each
   game's `User/Wii/shared2/sys/SYSCONF` before and after a launch. If it
   turns out deep, say so and decide with MMagTech.
3. **One testing image** for all of it, judged together on the TV, merged on
   MMagTech's word (Closes #256, #267, and whatever #269/#272 become).

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md).
