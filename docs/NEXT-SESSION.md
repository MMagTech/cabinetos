# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Xbox 360 plays through Xenia Edge (#192, branch `xbox360-xenia`, PROJECT.md
open question 34). Built, run on the A9 through the UI loop on 2026-09-30,
and judged there by MMagTech: rumble, exit, saves. The branch's last commit
is on `testing`. Xbox (#172), PS3 (#171) and Switch (#169, #170) play as
before. Check `bootc status` on the A9 for the image it runs. **Never tell
MMagTech something is unrecoverable or safe to delete before every cause is
checked** (memory).

## Next: milestone 2, New systems

1. **#192 Xbox 360: judge the `testing` image, then merge on MMagTech's
   explicit go.** The UI-loop run used Edge from `~/x360`; the image is the
   first to carry it at `/usr/lib/cabinetos/xenia`
   (`build_files/install-xenia.sh`). On the A9: `bootc upgrade`, reboot, play
   Forza Horizon 2 from Home, Exit to Home, check `[save] uploaded xenia` in
   the log. Nothing may be committed to the branch after the testing push.
   After the merge: close #192, delete the branch.
   Known gap: Edge's Sign In box when a second player joins (has207/xenia-edge#286,
   watched daily by a scheduled task). *Lessons: emulators.*
2. Then #173 Wii, #174 Wii U.
