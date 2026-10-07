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
#274 (rumble kept across Wii games) merged as PR #282; image 2026.10.07.3 is
promoted and on the A9. A second-account check is #283 (a TV check, no code
expected). RomM's Mii Channel still has IGDB's "Check Mii Out Channel" cover
and summary; MMagTech's to unmatch in RomM.

The A9 sits at the LG C1, on Wi-Fi at **192.168.1.109**: run
`tools/ui-loop.sh` with `CABINETOS_A9=cabinet@192.168.1.109`.

## Next

Milestone 7, Ready to ship (docs/ROADMAP.md). Lessons file: image-and-ci.md.

1. **The installer:** Bazzite branding out (#105), the media check that fails
   good media (#106), Anaconda's disk and user screens replaced (#107), its
   own quiet splash and Wi-Fi during setup (#136). Agreed 2026-10-07 (#136's
   comment): no firmware in the installer; prove the install needs no network
   (a VM with none), then drop Anaconda's Network screen. Iterate in a VM (on
   the A9 or the Unraid server: check first that one can be made from an
   ISO), so MMagTech's install is never wiped; one real install at the end,
   onto a spare drive. The one remaining question must name the disk (size,
   model) before erasing it. Also: pin bootc-image-builder (build-disk.yml
   uses `:latest`), and build the ISO once per release, not per update.
   MMagTech, 2026-10-07: do the milestone as planned; anything that turns
   out too hard (the installer's own splash and artwork most likely) is
   skipped, not forced. MMagTech sees the work as it goes: a picture of
   every installer screen from the VM sent to him (or the VM's screen
   opened live), and the final look judged on the TV.
2. **A shipping image without the development shell (#134).**
3. **Signed images (#135).**
4. **Licences checked and the full texts shipped (#120).**

Also in the milestone: booting with the TV off (#268), whether Nintendo DS
stays (#248), a diagnostic report (#195), a showcase page and README (#190).

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). After a testing image installs,
verify it from the A9 and list the possible checks; MMagTech decides which.
