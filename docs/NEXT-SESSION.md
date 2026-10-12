# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

**2026-10-12: #364 (game manuals) is merged**, judged on the TV: the game's
RomM manual beside it on the drive, a small book on the cover (bottom right,
on frosted glass), R1 to read it. The sound fix (#367) is released; the A9
(Wi-Fi, **192.168.1.109**, `CABINETOS_A9=cabinet@192.168.1.109`) runs
`testing`, 2026.10.12.5 or the manuals image after it. Handhelds are a
non-goal (docs/PROJECT.md); Cabinet on Apple platforms is no constraint.

## Next

1. **#368, PS2 Widescreen row** (lessons: emulators.md). **Decided on #368,
   build it:** widescreen stays on for every PS2 game whose patch file has a
   `Widescreen 16:9` section; those games get a "Widescreen: On / Off" row
   in the pause menu, remembered per game on this console like the Picture
   row, from the next start of the game (test whether PCSX2 can switch it
   live). A user's Armored Core 3 has a thin hack that stretches the HUD.
2. **#268 (PR #332, on hold):** the display wait works, but the app starts
   Developer access, Remote Play and Tailscale, so with no display none came
   up. Rework: start them at boot from their saved state, independent of the
   session; then retest with the HDMI unplugged (MMagTech at the TV). Branch
   `wait-for-display`.
3. **Fewer outside downloads in every image build (decided, MMagTech
   2026-10-12).** A GitHub 500 on RPCS3's release failed a build that day.
   - RPCS3, Xenia and xemu's drive: fetched in a CI step before the image
     build, kept in the Actions cache, staged into `image_payload/`; the
     install scripts use the staged file and fall back to downloading (the A9
     dry runs have no payload). **The cache key comes from the install
     script's own pin** (URL and sha256); the sha256 is checked on every
     build, cached or not.
   - The frontend's builder container: reuse the ghcr builder image
     build-core.yml already publishes, building only if the pull fails.
   - Prove it: build 1 downloads and saves, build 2 logs "from the cache",
     and a pin change on a throwaway branch (never pushed to testing)
     fetches the new file. Left out on purpose: the Bazzite base, Terra,
     the frontend compile.

Waiting, not for this session unless MMagTech raises them: #341 (no sound
on a K1; asked for a report with the new Sound section), #363 (Deck
controls inside Steam: Spinnich has not tested in Steam yet, and suggested
treating it as Steam Controller support; a reply was drafted 2026-10-12), #362 (to discuss: sharing a
save), #360 (an update says "No connection" when only the router's DNS
fails), #353, #354, #355
(saves that never sync: Saturn cartridge, arcade high scores, cartridge
clocks; After first release), #347 (pads did not reconnect after an update
reboot), #345 (PS1 DualShock), #349 (to discuss: rolling back an update).
Upstream to watch: rommapp/romm#5175 and #5198 (save channels),
rommapp/argosy-sigil#17 and #18 (our Xbox and Xbox 360 notes),
flyinghead/flycast#2541, libretro/Genesis-Plus-GX#413.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). Never press anything on the
A9 he did not ask for while he may be using it.
