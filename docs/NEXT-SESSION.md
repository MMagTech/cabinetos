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
file at **https://download.mmagtech.com/cabinetos.iso** (Cloudflare R2). The
site and wiki are at **https://cabinetos.mmagtech.com**. The A9 (Wi-Fi,
**192.168.1.109**, `CABINETOS_A9=cabinet@192.168.1.109`) runs `testing`
2026.10.10.10 (#328's image, promoted to everyone), signed.

## Next

1. **#268, booting with no display answering: the display part works;
   retest, then a PR.** Branch `wait-for-display` (pushed, no PR): the
   session waits for a display's answer instead of guessing 1920x1080.
   Tested once without an image: with the cable out it waited; with it in,
   it came up at 3840x2160. The black screen after that was the test's own
   fault (#331): the first drop-in pointed `ExecStart` at a home-folder file,
   SELinux refused it, and two minutes of failed starts left WirePlumber
   without the sound cards. **Use `ExecStart=/usr/bin/bash
   /var/home/cabinet/cabinetos-session-dev` from the start**, and check
   `wpctl status` shows the HDMI sink before and after. Then a PR and a
   testing image, the same test with a full restart. Tell MMagTech every
   step first and wait for "go". He now has a sound bar on the TV over eARC.

#328 (#318, controllers) merged and promoted 2026-10-10. Filed, waiting its
turn: #329 (release notes, open for discussion), #331 (after a session start
loop the sound manager loses the cards; After first release).

Eden stays on Flathub: the project never distributes a Switch emulator
(PROJECT.md, 21).

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). Never press anything on the
A9 he did not ask for while he may be using it.
