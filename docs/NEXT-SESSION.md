# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-07: #276 (the Remote's speaker) is closed as not possible with the
TechKen Remotes; findings on the issue. Its side fix (a reconnected Remote
gets its motion setup back) merged as PR #281; image 2026.10.07.2 is promoted
and on the A9. The plan for #275 and #274 is reviewed (comments on both).

The A9 sits at the LG C1, on Wi-Fi at **192.168.1.109**: run
`tools/ui-loop.sh` with `CABINETOS_A9=cabinet@192.168.1.109`.

## Next

Milestone 7, Ready to ship (docs/ROADMAP.md). Lessons file: emulators.md.

1. **Build #275 and #274, in the order on #275's plan comment** (read it and
   #274's first):
   1. The Mii Channel entry: MMagTech put the Mii Channel WAD on RomM on its
      own (2026-10-07; check the entry after his rescan). Its save also
      carries `User/Wii/shared2/menu/FaceLib/RFL_DB.dat`. Keep the existing
      Wii save zip layout (`catalog::directorySaveRoot`, relative to
      `User/Wii/title`); do not change it for other games.
   2. The user's `RFL_DB.dat` copied into every Wii game's NAND at launch.
   3. Rumble kept across Wii games through `dolphin_enable_rumble` (#274).
   4. Miis on Remotes through the bridge, from one file per player light.
      MMagTech: in scope, before the first release.
   No core change anywhere. Judge each on the TV; one testing image.
2. **The installer:** Bazzite branding out (#105), the media check that fails
   good media (#106), Anaconda's disk and user screens replaced (#107), its
   own quiet splash and Wi-Fi during setup (#136).
3. **A shipping image without the development shell (#134).**
4. **Signed images (#135).**
5. **Licences checked and the full texts shipped (#120).**

Also in the milestone: booting with the TV off (#268), whether Nintendo DS
stays (#248), a diagnostic report (#195), a showcase page and README (#190).

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). After a testing image installs,
verify it from the A9 and list the possible checks; MMagTech decides which.
