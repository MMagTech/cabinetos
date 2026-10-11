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
2026.10.10.10, the testing image of #328, signed.

## Next

1. **#328, waiting for MMagTech's merge go** (closes #318). Controller
   drivers load at boot; Bluetooth LE pads (Series X|S) pair; the base
   report lists a new kernel series' controller-driver changes. Checked on
   the A9 from the log (on the PR). He has not said whether he wants TV
   checks. After the merge: take the #318 line out of the release page's
   Known issues, and delete the branch on his go.
2. **#331 first: the console app freezes on a black screen when the TV's
   sound output is missing** (found 2026-10-10 testing #268, with a new sound
   bar on the TV over eARC). WirePlumber left the HDMI card `off` (only
   "Dummy Output"), and the frontend never got past opening sound, on the
   image's own script too. A console must reach Home with no sound output.
3. **#268, booting with no display answering: the display part works.**
   Branch `wait-for-display` (pushed, no PR). Tested once without an image
   (drop-in `ExecStart=/usr/bin/bash /var/home/cabinet/cabinetos-session-dev`;
   SELinux refuses a home file as ExecStart directly): waited with the cable
   out, then came up at 3840x2160 when it went in. The black screen after it
   was #331. Retest with the sound bar settled, then a PR and a testing image,
   the same test with a full restart. Tell MMagTech every step first and
   wait for "go".

Filed, waiting its turn: #329, release notes for every update (open for
discussion; QR code on the update prompt suggested). Eden stays on Flathub:
the project never distributes a Switch emulator (PROJECT.md, 21).

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). Never press anything on the
A9 he did not ask for while he may be using it.
