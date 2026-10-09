# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-08 late: Steam over Moonlight (#296) is finished on branch
`steam-moonlight`, pull request open; its testing image 2026.10.09.2 is on
the A9 and MMagTech ran the three stream tests with it. **Merge only on
MMagTech's go**, then delete the branch on his go. The A9 sits at the LG C1
on Wi-Fi at **192.168.1.109** (`CABINETOS_A9=cabinet@192.168.1.109` for
`tools/ui-loop.sh`), signed in to MMagTech's Tailscale as `cabinetos`.

## Next

Milestone 7, Ready to ship (docs/ROADMAP.md). Lessons file: testing.md, then
frontend.md.

1. **A stream started while the screen is asleep stays black (#294).** Same
   start step as #296 (`cabinetos-remoteplay started`); the open question
   (what idle does during a stream) is on the issue, to settle with him.
2. **Signed images (#135).**
3. **CabinetOS in the system's version info (#137).**
4. **Licences checked and the full texts shipped (#120).**

Also in the milestone: booting with the TV off (#268), a diagnostic report
(#195), a showcase page and README (#190), Steam black at 120 Hz on the C1
and "no signal" at every switch to Steam (#290).

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). Never press anything on the
A9 he did not ask for while he may be using it.
