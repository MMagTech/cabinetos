# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

**2026-10-12: #351 (volume keys) and #352 (smart collections, grouped by
owner) are merged and released**; Spinnich is asked to try the Deck's rocker.
Handhelds are now a non-goal (docs/PROJECT.md): classic controls only; the
Steam Deck inside Steam is the one exception, with no promises. The A9
(Wi-Fi, **192.168.1.109**, `CABINETOS_A9=cabinet@192.168.1.109`) runs
`testing` 2026.10.12.3, the released image. Cabinet on Apple platforms is no
longer a constraint.

## Next

1. **#341, the report's Sound section (PR from `report-sound`)**: built and
   dry-run on the A9. Push to `testing`, prove the image from the A9, get
   MMagTech's merge word, then ask the K1 user (deletableman) for a fresh
   report after updating, drafted for MMagTech's go. Their report showed
   WirePlumber failing to load `api.alsa.acp.device` on both boots, so no
   outputs at all; the A9 does not.
2. **#268 (PR #332, on hold):** the display wait works, but the app starts
   Developer access, Remote Play and Tailscale, so with no display none came
   up. Rework: start them at boot from their saved state, independent of the
   session; then retest with the HDMI unplugged (MMagTech at the TV). Branch
   `wait-for-display`. Decided against (#361, closed): restarting the picture
   when a different screen is connected while running; the LG drops HDMI
   several times a day, and a TV user rarely changes screens.

Waiting, not for this session unless MMagTech raises them: #363 (Deck
controls inside Steam, waiting on Spinnich), #362 (to discuss: sharing a
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
