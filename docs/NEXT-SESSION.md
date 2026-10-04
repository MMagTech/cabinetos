# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Milestone 3, item 6 done; shaders moved ahead of phase 3. **#63 phase 2 with #209, on `machine-class`,
judged on the A9 TV 2026-10-04**: the console picks its starting picture
level from its graphics chip until someone moves the dial (the A9 picks
Quality); N64 Balanced 2x; vsync on in Eden, RPCS3, Cemu and Xenia. The TV
round found two Quality values that were never measured and too heavy for
the A9, now fixed: PS3 1080p, Xbox 360 720p at every level. PROJECT.md,
"DECIDED, MMagTech 2026-10-04: the machine class". Merged in #246.

## Next

1. **Shaders and CRT looks (#122), a discussion with MMagTech before
   anything is built** (lessons: `frontend.md`, `testing.md`). MMagTech,
   2026-10-04: it "needs a discussion because it can't be a direct copy" of
   Cabinet. Read #122 and PROJECT.md *Shaders, and the glow around the
   picture* (Cabinet's eleven looks, the three it dropped), then walk the
   choices with him: which looks, where they are chosen (the old "pause
   menu, per system" is reopened), how few settings, and something fun for
   the kids. Name every real option before he decides; each look is judged
   on the TV.
2. Then item 8, before phase 3: the slowed A9 (#210, with the processor
   check noted there), #168, #221. Then #63 phase 3.
