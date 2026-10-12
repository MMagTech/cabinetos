# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

**2026-10-12: GameCube saves use Dolphin's `.gci` folder (#344, PR #356),
merged and promoted.** The A9 (Wi-Fi, **192.168.1.109**,
`CABINETOS_A9=cabinet@192.168.1.109`) runs that image, `testing` 2026.10.12;
Resident Evil 4's old card was read in on the TV and its zip reached RomM.
**#346 is decided and waiting:** no import of our own; CabinetOS moves to
RomM's save channels when they ship (rommapp/romm#5198), with the rules on
#346 (newest wins, no screen; a save the console made downloads as stored,
no conversion). Cabinet on Apple platforms is no longer a constraint; a
CabinetOS change that leaves Cabinet behind gets a record issue on
MMagTech/cabinet (first: MMagTech/cabinet#9).

## Next

1. **#351, volume keys** (reported from a Steam Deck by Spinnich, with a
   diagnostic report). **Decided, the plan is on the issue; build it:**
   - Volume keys (`KEY_VOLUMEUP`/`KEY_VOLUMEDOWN`, 115/114) from any keyboard
     change the system volume, silently, with no bar. Read them from the
     kernel's keyboards, not the window, so they work while a separate
     emulator (PS2, PS3, Switch, Xbox, Xbox 360, Wii U) has the screen.
   - At every start the volume goes back to full, unless a DRM connector of
     type eDP, DSI or LVDS exists (a built-in screen); then it keeps its level.
   - The diagnostic report lists every keyboard and the keys it can send.
   - Test on the A9 by injecting 115/114 (ydotool) on Home and in each kind of
     game, reading the sink volume back. Then reply on #351 for Spinnich to
     check a testing image on the Deck.
2. **#352, RomM Smart Collections** (Spinnich, RomM 5.4.0): the console asks
   only `/api/collections`; smart ones are `/api/collections/smart`, which the
   reference RomM (5.3.1) also serves. Show them beside the regular ones.
   MMagTech is making a few on the reference server to test with.
3. **#268 (PR #332, on hold):** the display wait works, but the app starts
   Developer access, Remote Play and Tailscale, so with no display none came
   up. Rework: start them at boot from their saved state, independent of the
   session; then retest with the HDMI unplugged (MMagTech at the TV). Branch
   `wait-for-display`.

Waiting, not for this session unless MMagTech raises them: #353, #354, #355
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
