# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Wii is merged (#205). **Wii U (#174) passed on the TV through the console
(the loop build) on 2026-10-01** and is on branch `wiiu-cemu`, pushed to
`testing` for its image. Decisions, what was built and what he judged:
`docs/PROJECT.md` open question 36. Check `bootc status` on the A9 for the
image it runs. **Never tell MMagTech something is unrecoverable or safe to
delete before every cause is checked** (memory).

## Next: milestone 2, New systems

1. **#174 Wii U: the testing image.** Check the first CI build of Cemu
   (`build-cemu.yml`, uncached; on the A9 its libraries took 8.7 min at 24
   cores), then `bootc upgrade` the A9 to `testing` and have MMagTech run
   the same steps from the image itself: Wii U grid (Captain Toad greyed),
   Hyrule Warriors (pad, sound), save, Quit, start again (save back). Merge
   only on his explicit go, then close #174. *Lessons: emulators.*
2. **#204**, Dolphin's log level: one line, with the next testing push.
3. **#200**, real Wii Remotes, when MMagTech's Remotes arrive (2026-10-03):
   the test list is in the issue, now with a Cemu check (item 6).
