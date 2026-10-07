# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-07: #275 (Mii Channel, Miis in every Wii game, Miis on Remotes) and
#274 (rumble kept across Wii games) are built and were judged on the TV in the
loop: all passed (docs/PROJECT.md question 35, "Miis and rumble"). PR #282;
its testing image (run 37570726422, 9m28s) is built but **not yet installed
on the A9**: MMagTech installs it from Settings, System, Update.

The A9 sits at the LG C1, on Wi-Fi at **192.168.1.109**: run
`tools/ui-loop.sh` with `CABINETOS_A9=cabinet@192.168.1.109`.

## Next

Milestone 7, Ready to ship (docs/ROADMAP.md). Lessons file: image-and-ci.md.

1. **Finish PR #282:** once MMagTech has installed the testing image, verify
   it from the A9 (version, no drop-ins, the new strings in the frontend and
   the bridge), list the possible checks (all judged in the loop), merge on
   his word, then ask his go to delete `wii-speaker` and `mii-channel`,
   locally and on GitHub. RomM's Mii Channel still has IGDB's "Check Mii Out
   Channel" cover and summary; his to unmatch in RomM.
2. **The installer:** Bazzite branding out (#105), the media check that fails
   good media (#106), Anaconda's disk and user screens replaced (#107), its
   own quiet splash and Wi-Fi during setup (#136). Agreed 2026-10-07 (#136's
   comment): no firmware in the installer; prove the install needs no network
   (a VM with none), then drop Anaconda's Network screen. Iterate in a VM (on
   the A9 or the Unraid server: check first that one can be made from an
   ISO), so MMagTech's install is never wiped; one real install at the end,
   onto a spare drive. The one remaining question must name the disk (size,
   model) before erasing it.
3. **A shipping image without the development shell (#134).**
4. **Signed images (#135).**
5. **Licences checked and the full texts shipped (#120).**

Also in the milestone: booting with the TV off (#268), whether Nintendo DS
stays (#248), a diagnostic report (#195), a showcase page and README (#190).

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). After a testing image installs,
verify it from the A9 and list the possible checks; MMagTech decides which.
