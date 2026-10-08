# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-07: Remote Play (#286) is built on branch `remote-play` (pushed, no
pull request yet) and judged piece by piece on the TV with MMagTech: the
Remote Play section in Settings (Streaming, Paired devices, Tailscale not
built), pairing on the TV, the CabinetOS tile, off means nothing listens,
the handover between TV and phone with a pause each way, Wii Remote games
greyed while streaming. Sunshine is in the image, pinned
(`build_files/install-sunshine.sh`). Why each choice: PROJECT.md question 39;
the screen: SETTINGS.md, Remote Play.

The A9 sits at the LG C1, on Wi-Fi at **192.168.1.109**: run
`tools/ui-loop.sh` with `CABINETOS_A9=cabinet@192.168.1.109`. It runs the
loop's build of `remote-play`, and **a test stand-in for Remote Play that is
not in git**: `/etc/systemd/system/cabinetos-remoteplay.service` (shadows the
image's unit), `/etc/polkit-1/rules.d/67-cabinetos-remoteplay.rules`,
`/usr/local/bin/cabinetos-remoteplay*`, and Sunshine unpacked in
`~/sunshine-test` (shown at /usr/share/sunshine by a private mount).
**Remove all of it before a testing image with Sunshine is judged.**

## Next

Milestone 7, Ready to ship (docs/ROADMAP.md). Lessons file: image-and-ci.md.

1. **Remote Play (#286): Tailscale**, with MMagTech first: what the row does
   (#286 has the intent: sign-in link as a QR code on the TV, then the
   machine's name and address; Disconnect, a separate log-out; off with
   Remote Play). Then a testing image of `remote-play`, the A9 stand-in
   removed, and these checked from the image: pairing, the handover in a
   built-in and a standalone game (PS3 not yet seen after its fix), a phone
   dropping, off means nothing listens. Then the pull request, which closes
   #286 and #289.
2. **Signed images (#135).**
3. **CabinetOS in the system's version info (#137).**
4. **Licences checked and the full texts shipped (#120).**

Also in the milestone: booting with the TV off (#268), a diagnostic report
(#195), a showcase page and README (#190), Steam black at 120 Hz on the C1
(#290).

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). After a testing image installs,
verify it from the A9 and list the possible checks; MMagTech decides which.
