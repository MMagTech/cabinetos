# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Milestone 3. Branch `fast-cores` holds #163 (built-in games on the fast
cores) and #150 (performance profile while any game runs), both passed on
the TV 2026-10-02 and recorded in PROJECT.md after open question 31. It is
on `testing` for MMagTech to judge the image. **Never tell MMagTech something
is unrecoverable or safe to delete before every cause is checked** (memory).

## Next

1. **The `testing` image:** check the A9 updated (`bootc status`), the log
   shows `[cpus]` and `[profile]` lines at a game, and MMagTech plays one
   built-in game and one Switch game. Merge only on his explicit go; PR body
   "Closes #150, Closes #163". Then clean up the branch (memory).
2. **#63 phase 1**: the dial, the fixed tables, #209 (VRR and vsync, with its
   three TV tests), designed with #73 and #122 for the pause menu.

Also open, not in the way:
- **#200**, real Wii Remotes, when the hardware arrives; one TV session.
- **#204**, Dolphin's log level, one line, with the next testing push.
- Found 2026-10-02, filed: #212 (stale picture over a paused separate
  emulator, milestone 6), #213 (a cut-off Eden session's save, milestone 7).
