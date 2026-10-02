# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Milestone 2 is done but for real Wii Remotes. Wii U is merged (#208; open
question 36). Check `bootc status` on the A9 for the image it runs. **Never
tell MMagTech something is unrecoverable or safe to delete before every
cause is checked** (memory).

## Next: milestone 3, Settings: picture

The automatic quality design is recorded (PROJECT.md, "REVISED,
MMagTech 2026-10-01", after open question 31; #63 with its checklist).
Nothing of it is built. Start with what its measurements depend on:

1. **#150**, Bazzite's performance profile while a game runs, balanced on
   Home. Measure first, on the A9, with a heavy game. *Lessons: image and CI,
   testing.*
2. **#163**, emulators on the fast cores: find first why the kernel's
   preferred-core use is off on the A9.
3. Then **#63 phase 1**: the dial, the fixed tables, #209 (VRR and vsync,
   with its three TV tests), designed with #73 and #122 for the pause menu.

Also open, not in the way:
- **#200**, real Wii Remotes, when the hardware arrives (it may not on
  2026-10-03); one TV session.
- **#204**, Dolphin's log level, one line, with the next testing push.
