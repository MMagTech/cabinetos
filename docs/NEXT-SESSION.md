# Next session

What to work on next, and where things stand. **Replace this file at the end
of every session; never add to it.** Keep it to one screen.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item you pick up. New here? `docs/WORKING.md`
(machines, the loop, how MMagTech wants it done). The specification is
`docs/PROJECT.md`.

## Where things stand

- `ps2-bios` (#153, PS2 finds its BIOS, and the docs split into
  `NEXT-SESSION.md`, `WORKING.md` and `lessons/`) was pushed to `testing` and
  judged on the TV before merging. `latest` and `testing` are that image once
  it merges. **Check `bootc status` on the A9 for the exact version.**
- The VM is on `2026.09.26.4`. PR #42 is parked.
- **The A9 still has a hand-made `bios/pcsx2/bios`** (root-owned `bios/pcsx2`)
  from 2026-09-20. Nothing reads it now; delete it only with MMagTech's OK.

## Next, in order

**The goal is the first release. The order is `docs/ROADMAP.md`, one numbered
milestone per step, every open issue in one.** Finish a milestone before
starting the next; file anything else found on the way into After first
release unless it blocks the release.

1. **#151** Dreamcast crash after a big arcade state. Not reproduced.
   *Lessons: emulators, testing.*
2. **#150** Full CPU speed during games. Measure PS2 first.
   *Lessons: emulators, image-and-ci.*
3. Then milestone 2, **New systems** (#138): break it into one issue per
   system first.

Also owed: Lumines (PSP) loading an old state.
