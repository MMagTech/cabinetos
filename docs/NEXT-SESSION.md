# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

**2026-10-10: CabinetOS 2026.10.10 is released**, the first release
(https://github.com/MMagTech/cabinetos/releases/latest). The installer is one
file at **https://download.mmagtech.com/cabinetos.iso** (Cloudflare R2,
MMagTech's account; GitHub takes at most 2 GB per release file and the ISO is
6.66 GB). **Build disk images** with *publish* ticked builds the ISO and
replaces that file, with its SHA-256 beside it; the release page links it.
The site and wiki are at **https://cabinetos.mmagtech.com** (GitHub Pages,
custom domain; the old github.io addresses redirect). Shipped today: the
diagnostic report (#195), How to (#314), Controllers as one row, licences
checked and the full texts in the image (#120). The A9 (Wi-Fi,
**192.168.1.109**, `CABINETOS_A9=cabinet@192.168.1.109`) runs `testing`
2026.10.10.9, the released image, signed.

## Next

1. **#318, one controller seated twice: the two-minute test first**, with
   MMagTech at the TV (lessons: testing.md). Only the Xbox pad on. Start a
   SNES game, read `[players]` in the journal for a second pad with the same
   Bluetooth address, quit; then the same with a PS2 game. Only PS2: the fix
   is in the PS2 start path. Both: SDL's Xbox HIDAPI driver at game start;
   talk through what a setting change touches before choosing. Neither: keep
   the workaround (on the troubleshooting page). **No guard that touches every
   controller** without his go: he was wary of it.
2. **#268, booting with the TV off.** Ready to ship, left over.

Then MMagTech picks from After first release. Ideas he raised, not filed:
Switch gyro (only if enough games use motion: count them first) and 3DS (only
if a stable libretro core exists; check Azahar). **Wii stays Wii Remotes
only** (MMagTech, 2026-10-10).

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). Never press anything on the
A9 he did not ask for while he may be using it.
