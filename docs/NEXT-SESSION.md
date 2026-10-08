# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-08: Remote Play (#286) is finished on branch `remote-play`, pull
request #292, judged on the TV and verified from testing image 2026.10.08.2
on the A9. **Merge only on MMagTech's go**, then delete the branch on his go.
The A9 runs that testing image with nothing by hand: the stand-in is gone.
It sits at the LG C1 on Wi-Fi at **192.168.1.109**
(`CABINETOS_A9=cabinet@192.168.1.109` for `tools/ui-loop.sh`), signed in to
MMagTech's Tailscale as `cabinetos`, 100.67.82.30.

## Next

Milestone 7, Ready to ship (docs/ROADMAP.md). Lessons file: frontend.md.

1. **The controller battery on Home, and a one-time low-battery notice
   (#293).** MMagTech's proposal and what checking it against the console
   found are on the issue: SDL already reports each pad's battery (no sysfs
   walk), the console's one notice style, the Wii Remote bridge's battery
   byte. Walk the scenarios with him first, then build it in the loop and
   judge it on the TV.
2. **Signed images (#135).**
3. **CabinetOS in the system's version info (#137).**
4. **Licences checked and the full texts shipped (#120).**

Also in the milestone: booting with the TV off (#268), a diagnostic report
(#195), a showcase page and README (#190), Steam black at 120 Hz on the C1
(#290).

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). Never press anything on the
A9 he did not ask for while he may be using it.
