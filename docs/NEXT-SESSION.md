# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

**2026-10-11: multi-disc (#342) and the letter column (#333) are merged and
promoted.** The A9 (Wi-Fi, **192.168.1.109**, `CABINETOS_A9=cabinet@192.168.1.109`)
runs `testing` 2026.10.11.4, the promoted image, and is back on it (no loop
drop-in). An all-systems sweep on that image passed (#162). The wiki has
*Games on several discs* (cabinetos.mmagtech.com/wiki/discs.html).

## Next

1. **#344, GameCube saves restored from RomM never reach Dolphin** (`card.raw`
   written, `card.USA.raw` read; a fresh install loses its GameCube saves).
   The fix plan is on the issue; its own PR (lessons: emulators.md). Test on
   the rig through the read-only relay with a card that exists only on RomM.
2. **#268 (PR #332, on hold):** the display wait works, but the app starts
   Developer access, Remote Play and Tailscale, so with no display none came
   up. Rework: start them at boot from their saved state, independent of the
   session; then retest with the HDMI unplugged (MMagTech at the TV). Branch
   `wait-for-display`.

Waiting, not for this session unless MMagTech raises them: #347 (pads did not
reconnect after an update reboot; capture steps on the issue if it recurs),
#345 (PS1 DualShock, After first release), #346 and #349 (to discuss: existing
saves from RomM; rolling back an update). Upstream reports to track:
flyinghead/flycast#2541, libretro/Genesis-Plus-GX#413 (#162 has our fallbacks).

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). Never press anything on the
A9 he did not ask for while he may be using it.
