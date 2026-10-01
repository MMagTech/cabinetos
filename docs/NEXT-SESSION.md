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

1. **#173 Wii, judged on the TV, three things left.** The build was put on
   the TV with
   `tools/ui-loop.sh --env CABINETOS_WII_CONTROLS=/var/home/cabinet/fb/frontend/data/wii-controls.txt`;
   a reboot removes it, so run that again first. Judged so far (PROJECT.md
   35): the grid (playable first, then greyed: "looks good now"), Geometry
   Wars and Mario Kart Wii on a pad, both saves up to RomM. Left: start Mario
   Kart Wii again (success: the licence is there; the log says "unpacked ...
   Mario Kart Wii.zip"); R3, the Wii's HOME menu and its "Wii Menu"; one
   GameCube game as before. Then the testing image: **set aside the A9's hand
   copy at `/var/lib/cabinetos/bios/dolphin-emu`** or it shadows the image's
   `Sys`. Then merge on his go, close #173, and the Xenia Edge reply (#286
   there): test OK in Left 4 Dead 2's Sign In box first, show him the reply
   before posting. *Lessons: emulators.*
2. Then #174 Wii U.
