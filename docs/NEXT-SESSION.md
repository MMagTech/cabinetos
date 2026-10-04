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

1. **Finish #122:** MMagTech installs the `testing` image on the A9 and
   judges it as shipped (he said he will raise anything he catches later);
   merge #249 on his explicit go, close #122, delete the branch on his go.
   Not seen on the TV yet in any build: CRT aperture on N64, Dot matrix
   Pocket, the Game Boy Color dot matrix, a 3DO game.
2. Then item 8, before phase 3: the slowed A9 (#210, with the processor
   check noted there), #168, #221. Then #63 phase 3.
