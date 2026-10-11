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

1. **Multi-disc, Saturn first: PR #342 (branch `multi-disc`), plan and results on #162**
   (lessons: emulators.md "Games on several discs", testing.md "the disc rig").
   Built and measured headless on the A9 the night of 2026-10-11; not merged, no
   testing image (`testing` holds #333 and #332, so a plain push is refused).
   Next: MMagTech judges the menu on the TV with `tools/ui-loop.sh` (Panzer Dragoon
   Saga: game page, Change disc, Insert disc 2 through the Saturn BIOS, Start over);
   then the open questions on #162: achievements on a disc change (PROJECT.md 43,
   options a/b/c), an upstream report to Flycast (disk_index kept across games) on
   his go, a replacement for Night Trap (bad dump; Redump-checked list on #162).
   Insert at a game's own prompt still needs saves: Metal Gear Solid (GameFAQs, his
   download) and a Dreamcast VMU save at the end of disc 1.
2. **#333, letter column:** waiting on MMagTech's final look (Arcade through the C's, no
   stray T; one collection), then his merge go. The A9 still runs its image.
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
