# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

**2026-10-10: CabinetOS 2026.10.10 is released** (https://github.com/MMagTech/cabinetos/releases/latest;
installer at https://download.mmagtech.com/cabinetos.iso; site https://cabinetos.mmagtech.com).
#328 (controller drivers at boot, Bluetooth LE pairing; closed #318) is merged and promoted.
The A9 (Wi-Fi, **192.168.1.109**, `CABINETOS_A9=cabinet@192.168.1.109`) runs `testing`
2026.10.11.3, #333's image (normal startup; SSH comes up with no TV).

## Next

1. **Multi-disc, Saturn first: milestone "Multi-disc: Saturn first", plan on #162**
   (read its body and comments before anything: per-core source findings, prior art,
   catalogue counts, per-game survey). Verification issues #334 to #338; Xbox 360 is #339,
   After first release. Build it once for every libretro disc system on its own branch,
   with the automated rig on the A9 (headless only while MMagTech is not at the TV,
   through `tools/romm-readonly-relay.py` so nothing is written to RomM; put the Recent
   order back after test launches; lessons: testing.md). The pause menu and game-page
   additions are judged by MMagTech on the TV before any merge. Test games are on RomM
   (listed on #162). Metal Gear Solid's save waits on MMagTech downloading it from
   GameFAQs (Cloudflare blocks automated downloads).
2. **#333, letter column:** waiting on MMagTech's final look (Arcade through the C's, no
   stray T; one collection), then his merge go.
3. **#268 (PR #332, on hold):** the display wait works, but the app starts Developer
   access, Remote Play and Tailscale, so with no display none came up. Rework: start
   them at boot from their saved state, independent of the session; then retest with
   the HDMI unplugged (MMagTech at the TV). Branch `wait-for-display`.

Filed, waiting: #329 (release notes for every update, open for discussion), #331 (sound
manager loses the cards after a session start loop; After first release). Eden stays on
Flathub: the project never distributes a Switch emulator (PROJECT.md, 21).

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). Never press anything on the
A9 he did not ask for while he may be using it.
