# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Wii is merged (#205). **Wii U (#174) is built on branch `wiiu-cemu`, not
pushed, not yet seen through the console.** Decisions and everything built:
`docs/PROJECT.md` open question 36. Cemu main `4e3c824faa` is built on the A9
at `~/cabinetos-cemu/cores/build/cemu` (cores/build-cemu.sh; Fedora 44 +
Cemu's own vcpkg; ldd clean against the image). The console side compiles;
the GamePad greying read Captain Toad's code (`AKBE`) through RomM and
Hyrule Warriors' (`BWPE`) off the file. Check `bootc status` on the A9 for
the image it runs. **Never tell MMagTech something is unrecoverable or safe
to delete before every cause is checked** (memory).

## Next: milestone 2, New systems

1. **#174 Wii U: the TV test, with MMagTech at the TV** (he has the step
   list; it was posted 2026-10-01 and he said he would test later). Deploy
   with the loop and the built Cemu:
   `tools/ui-loop.sh --env CABINETOS_BINARY_cemu=/var/home/cabinet/cabinetos-cemu/cores/build/cemu/bin/Cemu --env CABINETOS_WIIU_CONTROLS=/var/home/cabinet/frontend/data/wiiu-controls.txt`.
   Steps: Wii U grid (three playable, Captain Toad greyed "Needs a Wii U
   GamePad"); Hyrule Warriors through the console (download, straight in,
   pad by position, sound); a save, pause menu Quit, save up to RomM (tag
   `cemu`); start again, save came back; pause menu freeze. Then
   `tools/ui-loop.sh --restore`. Then push, the first CI run of
   `build-cemu.yml` (uncached; on the A9 the libraries took 8.7 min at 24
   cores), the testing image, and his judgement before any merge.
   *Lessons: emulators.*
2. **#204**, Dolphin's log level: one line, with the next testing push.
3. **#200**, real Wii Remotes, when MMagTech's Remotes arrive (2026-10-03):
   the test list is in the issue, now with a Cemu check (item 6).
