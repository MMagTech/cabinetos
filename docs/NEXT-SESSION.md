# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Milestone 3, item 6 done. **#63 phase 2 with #209, on `machine-class`,
judged on the A9 TV 2026-10-04**: the console picks its starting picture
level from its graphics chip until someone moves the dial (the A9 picks
Quality); N64 Balanced 2x; vsync on in Eden, RPCS3, Cemu and Xenia. The TV
round found two Quality values that were never measured and too heavy for
the A9, now fixed: PS3 1080p, Xbox 360 720p at every level. PROJECT.md,
"DECIDED, MMagTech 2026-10-04: the machine class". Merged if this file is on
main.

## Next

1. If not merged: merge on his go.
2. **Item 7, before phase 3** (lessons: `testing.md`, `emulators.md`):
   the slowed A9 (#210), including the processor check noted there (God of
   War III at Performance and Quality with the processor capped); the drop
   rule ignoring rewind snapshot frames (#168); whether built-in cores
   repeat or drop frames (#221).
3. Then **#63 phase 3**, the drop rule, record-only first.
