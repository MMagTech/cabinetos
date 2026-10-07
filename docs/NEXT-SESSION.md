# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-07: milestone 6 is done. The wording pass and notice length (#110)
and sound kept on through tuned (#228) are merged (PR #280); the tested image
2026.10.07 is promoted. #132 (sleep) moved to After first release.

The A9 sits at the LG C1, on Wi-Fi at **192.168.1.109**: run
`tools/ui-loop.sh` with `CABINETOS_A9=cabinet@192.168.1.109`.

## Next

Milestone 7, Ready to ship (docs/ROADMAP.md), in this order. Lessons file:
image-and-ci.md (the installer).

1. **No sound from the Wii Remote's speaker (#276).** MMagTech, 2026-10-07:
   a release blocker unless it proves impossible; at least a real attempt.
   Lessons file: emulators.md. Read every comment on the issue first.
2. **Mii Channel support (#275): a feasibility investigation and a plan, no
   production code.** MMagTech's proposal (2026-10-07) is the latest comment
   on #275: answer its four open questions on the A9 (a WAD booted through
   the Dolphin core, the IOS install, the bridge's 0x16/0x17 handling, the
   `RFL_DB.dat` path), then a short plan with the order of work. He reviews
   the plan before anything is built. He has the WAD files on RomM.
3. **The Wii Remote's speaker volume and rumble kept across games (#274)**,
   in the same plan: Dolphin's `WiimoteNew.ini` written at launch, no change
   to the core.
4. **The installer:** Bazzite branding out (#105), the media check that fails
   good media (#106), Anaconda's disk and user screens replaced (#107), its
   own quiet splash and Wi-Fi during setup (#136).
5. **A shipping image without the development shell (#134).**
6. **Signed images (#135).**
7. **Licences checked and the full texts shipped (#120).**

Also in the milestone: booting with the TV off (#268), whether Nintendo DS
stays (#248), a diagnostic report (#195), a showcase page and README (#190).

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). After a testing image installs,
verify it from the A9 and list the possible checks; MMagTech decides which.
