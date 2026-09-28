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

1. **#150** Performance profile during games. Measure PS2 first.
   *Lessons: emulators, image-and-ci.*
2. **#151** Dreamcast crash after a big arcade state. Not reproduced.
   *Lessons: emulators, testing.*
3. **#63 / #73** Picture quality. *Lessons: emulators, testing.*
4. **#88** Offline play. *Lessons: frontend.*
5. The installer, boot splash, and power button. *Lessons: image-and-ci.*
6. The first-hour walk-through with the real installer. *Lessons: testing.*

Also owed: Lumines (PSP) loading an old state; four pads at once (#115).
Parked: #154, PCSX2's `patches.zip` (its per-game fixes; the image has none).
MMagTech wants it supported later, not now.
