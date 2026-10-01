# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Wii plays on a pad (#173), built on branch `wii-dolphin`, not merged, no
testing image yet; PROJECT.md open question 35 has the research, the
decisions and what was checked headless. Real Wii Remotes are #200, after.
Xbox 360 (#192), Xbox (#172), PS3 (#171) and Switch (#169, #170) play as
programs of their own on `standalone.h` and `vpad.h`. Check `bootc status`
on the A9 for the image it runs. Before any testing push that adds or
changes an install script, run it on the A9 on the image and check it as
`cabinet` (memory). **Never tell MMagTech something is unrecoverable or safe
to delete before every cause is checked** (memory).

## Next: milestone 2, New systems

1. **#173 Wii, the testing image.** Branch `wii-dolphin`, pushed to
   `testing` on 2026-09-30. Judged on the TV from the loop build (PROJECT.md
   35): the grid, Geometry Wars and Mario Kart Wii on a pad, the save up and
   back, the HOME menu, GameCube unchanged. On the image, before the reboot:
   **set aside the A9's hand copy at `/var/lib/cabinetos/bios/dolphin-emu`**
   (`mv` to `dolphin-emu.hand`), or it shadows the image's `Sys`; then check
   the log names the image's `Sys` and that `/usr/share/cabinetos/wii-controls.txt`
   loads (`[wii] 6222 games`). Try the HOME menu's "Wii Menu" choice. Then
   MMagTech judges, says "merge", close #173 with a comment, delete the
   branch. Then the Xenia Edge reply (#286 there): test OK in Left 4 Dead 2's
   Sign In box first, show him the reply before posting. *Lessons: emulators.*
2. Then #174 Wii U.
