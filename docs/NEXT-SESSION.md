# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Milestone 3, item 6. **Build plumbing, on `build-plumbing`, no TV round**:
the Steam session install retries when Terra's mirror trips; a merge still
promotes the tested image when only documentation changed since the testing
push (#156), and `docs/LICENCES.md` alone builds; the core option check is
`cores/check-options.sh`, so editing it no longer rebuilds every core. This
file was committed after that branch's testing push, so its merge
promoting rather than building is the proof of #156. Merged if this file is
on main.

## Next

1. If not merged: merge on his go. After the merge, prove the other half of
   #156: a pull request changing only `docs/LICENCES.md` runs the image
   workflow, one changing only another doc runs none. Close both unmerged.
2. **#63 phase 2, the machine class, with #209 in the same image and one TV
   round** (lessons: `emulators.md`, `testing.md`). Walk the scenarios and
   name the options with MMagTech before building: what the class reads
   from the hardware (PROJECT.md, "REVISED ... the automatic quality
   design"), what unknown hardware gets, and whether PS2 and N64 keep
   Balanced equal to Quality (comment on #63, 2026-10-02). #209's part:
   the separate emulators (Eden, RPCS3, xemu, Xenia, Cemu) back to vsync on
   where #209 set them off (MMagTech, 2026-10-04, on #209).
