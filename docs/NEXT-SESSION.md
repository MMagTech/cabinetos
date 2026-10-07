# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-07: the installer (#105 to #107, #136) and Developer access (#134)
merged as PR #287; image 2026.10.07.5 is promoted and on the A9. The
installer asks one question and needs no network (proved in a VM); the real
install is the last step before release (#111, on the release stick). There
is no development image any more: the command line on port 2222 is
**Developer access**, off by default, in Settings, About, hidden until
Version is pressed seven times. It is on on the A9 and on the Unraid VM, so
the key works as before.

The A9 sits at the LG C1, on Wi-Fi at **192.168.1.109**: run
`tools/ui-loop.sh` with `CABINETOS_A9=cabinet@192.168.1.109`.

## Next

Milestone 7, Ready to ship (docs/ROADMAP.md). Lessons file: image-and-ci.md.

1. **Remote Play (#286).** Sunshine on the console, Moonlight to play from,
   off by default. MMagTech wants it before the first release. **First a
   feasibility test on the A9**, as #202 was: can Sunshine capture the
   running console under gamescope on the 890M, at what cost, and what happens
   to the TV. Bazzite installs Sunshine on demand (`ujust setup-sunshine`,
   Flatpak or Homebrew) and has a `virtual-monitor` option: try that first.
   If the display question turns out very large, decide again with MMagTech
   before building. Whether Sunshine is in the image or downloaded on first
   use is decided when it is built.
2. **Signed images (#135).**
3. **CabinetOS in the system's version info (#137).**
4. **Licences checked and the full texts shipped (#120).**

Also in the milestone: booting with the TV off (#268), a diagnostic report
(#195), a showcase page and README (#190).

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). After a testing image installs,
verify it from the A9 and list the possible checks; MMagTech decides which.
