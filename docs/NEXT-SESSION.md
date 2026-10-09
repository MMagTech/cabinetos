# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-09: the Bazzite base 44.20261006.1 is merged (#302), with base-update
pull requests listing their own checks (#301) and the controller list
(#266). The A9 runs image 2026.10.09.4 with nothing by hand: all 19 log
checks (`tools/base-checks.sh`) and all seven TV checks passed. The A9 sits
at the LG C1 on Wi-Fi at **192.168.1.109** (`CABINETOS_A9=cabinet@192.168.1.109`
for `tools/ui-loop.sh` and `tools/base-checks.sh`), signed in to MMagTech's
Tailscale as `cabinetos`. It takes updates only from its own System update
screen (the PIN is his to enter); never sudo over SSH.

## Next

Milestone 7, Ready to ship (docs/ROADMAP.md), in this order (MMagTech,
2026-10-09). Lessons file: image-and-ci.md, then frontend.md.

1. **A stream into Steam goes black on the way back to Home (#304).** Found
   in the base-update test, there since #298. The test it must pass is on
   the issue: restart, no controller, stream straight in, Home, Steam, back
   to Home, picture on the phone throughout. Upstream first (Sunshine), our
   fallback recorded there. Also the pads helper's false failure, same issue.
2. **Signed images (#135).** First of the rest because a mistake breaks every
   console's updates, so it needs several image updates on the A9 to prove
   itself.
3. **A diagnostic report (#195).** Everything decided so far is on the issue
   (the QR code is a download link; what goes in, what is stripped).
4. **Licences checked and the full texts shipped (#120).**

Also in the milestone: booting with the TV off (#268); the showcase page,
wiki and README (#190), hosted on GitHub (MMagTech, 2026-10-09), its place in
the order still to settle with him (proposed: the writing right after #135,
the pictures last). Moved to After first release on 2026-10-09: Steam at
120 Hz on the C1 (#290), Miis with a second account (#283). Wii Remote
pairing assumes `hci0` (#303) is After first release too.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). Never press anything on the
A9 he did not ask for while he may be using it.
