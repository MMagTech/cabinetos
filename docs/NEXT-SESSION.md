# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-09: a stream started while the screen is asleep (#294) is merged
(#299); the A9 runs image 2026.10.09.3 with nothing by hand. The A9 sits at
the LG C1 on Wi-Fi at **192.168.1.109** (`CABINETOS_A9=cabinet@192.168.1.109` for
`tools/ui-loop.sh`), signed in to MMagTech's Tailscale as `cabinetos`.

## Next

Milestone 7, Ready to ship (docs/ROADMAP.md), in this order (MMagTech,
2026-10-09). Lessons file: image-and-ci.md, then frontend.md.

1. **Signed images (#135).** First because a mistake breaks every console's
   updates, so it needs several image updates on the A9 to prove itself.
2. **A diagnostic report (#195).** Everything decided so far is on the issue
   (the QR code is a download link; what goes in, what is stripped).
3. **Licences checked and the full texts shipped (#120).**

Also in the milestone: booting with the TV off (#268), a showcase page and
README (#190), Steam black at 120 Hz on the C1 and "no signal" at every
switch to Steam (#290). CabinetOS in the version info (#137) was done in
#252.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). Never press anything on the
A9 he did not ask for while he may be using it.
