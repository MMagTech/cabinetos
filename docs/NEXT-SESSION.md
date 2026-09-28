# Next session

What to work on next, and where things stand. **Replace this file at the end
of every session; never add to it.** Keep it to one screen.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item you pick up. New here? `docs/WORKING.md`
(machines, the loop, how MMagTech wants it done). The specification is
`docs/PROJECT.md`.

## Where things stand

- `latest` and `testing` are both `2026.09.28.2` (#155: PS2 finds its BIOS,
  the docs split, the roadmap). The A9 runs it from the image, no drop-in,
  tuned on `balanced`.
- The VM is on `2026.09.26.4`. PR #42 is parked.
- Branch `performance-profile` (pushed, no PR) holds `CABINETOS_FRAMESTATS`,
  a PS2 frame-timing hook used for #150. Reuse it for #163 or #168.
- **The A9 still has a hand-made `bios/pcsx2/bios`** from 2026-09-20. Nothing
  reads it now; delete it only with MMagTech's OK.

## Next, in order

**The goal is the first release. The order is `docs/ROADMAP.md`, one numbered
milestone per step, every open issue in one.** Finish a milestone before
starting the next; file anything else found on the way into After first
release unless it blocks the release.

Milestone 1 is done. **Milestone 2, New systems:**

1. **#169 and #170 together: the shared foundation, built with Switch
   (Eden).** Start with research and a walk-through for MMagTech, before any
   code: Eden's current state and Flathub package (not checked), the keys and
   firmware it needs and how they come from RomM, game formats, where saves
   live, controllers, what Batocera does with it, and what a player does from
   Play to back on Home. *Lessons: emulators, image-and-ci.*
2. **#171** PS3 (RPCS3), reusing #169.
3. Then #172 Xbox, #173 Wii, #174 Wii U.

Also owed: Lumines (PSP) loading an old state.
