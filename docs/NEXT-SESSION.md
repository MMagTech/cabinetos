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

1. **The installer:** Bazzite branding out (#105), the media check that fails
   good media (#106), Anaconda's disk and user screens replaced (#107), its
   own quiet splash and Wi-Fi during setup (#136).
2. **A shipping image without the development shell (#134).**
3. **Signed images (#135).**
4. **Licences checked and the full texts shipped (#120).**

Also in the milestone: booting with the TV off (#268), whether Nintendo DS
stays (#248), a diagnostic report (#195), a showcase page and README (#190).

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). After a testing image installs,
verify it from the A9 and list the possible checks; MMagTech decides which.
