# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue, and it
waits its turn.** Replace this file at the end of every session; list only
what the next session works on, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`.

## Where things stand

**2026-10-13:** `latest` is 2026.10.13. Merged on 2026-10-12: the PS2
Widescreen row (#368), a folder game downloading once (#372), the build's
download cache (#373), and **What's new**, the player changelog
(`docs/wiki/changelog.md`, live on the site). **Every pull request that
changes what consoles get now adds its changelog entry, in player's words,
under its testing image's version, or says `Changelog: none`** (WORKING.md;
`changelog.yml` checks it). The A9 (Wi-Fi, **192.168.1.109**,
`CABINETOS_A9=cabinet@192.168.1.109`) runs `testing`, 2026.10.13. The
issue review ran on 2026-10-12: everything open is in **Later** (no date,
worked only when MMagTech raises it), except #341.

## Next

1. **#341, no sound on deletableman's HC Technology HCAR5000-MI** (Ryzen 7
   5800U; lessons: testing.md, "The silent day"). His report of 2026-10-13
   (on the issue) after he found a fix: Steam's first start asked for a sound
   output, he picked **Renoir/Cezanne HDMI/DP Audio Controller** over **Audio
   Adapter (Unitek Y-247A)**, and from then on CabinetOS had sound in menus
   and games, across reboots. The report now shows the HDMI output (DP 1, his
   LG TV) as the default and the app's stream on it; a USB sound adapter
   (C-Media, the Unitek) is also plugged in. **Guess, not verified:** the
   sound server's default was the USB adapter, with nothing listening on it,
   and the console never chooses an output, so it played into the adapter;
   Steam set the system default, which is why the fix outlived Steam.
   **First, research whether that adapter is plugged in or built in**
   (MMagTech: built-in sound on machines like this is to be supported; a USB
   sound stick somebody adds is not). The log shows C-Media `0d8c:0014` on
   USB port `usb 3-3`, which Linux's USB list names "Audio Adapter (Unitek
   Y-247A)", a plug-in stick; but some mini PCs wire a C-Media USB chip
   inside for the headphone jack. Check the HCAR5000-MI's specs and
   teardowns, and draft a question for deletableman. Either way the fault is
   the console playing through whatever output the system picked; a fix that
   plays through the TV the picture goes to would cover a built-in jack too,
   without supporting USB sticks as such. Check
   it against his first report (which showed WirePlumber failing to load its
   ALSA plugin: a second fault, or the same?) and WirePlumber's priorities
   (USB against HDMI). Then name the options to MMagTech before building
   (prefer the output on the screen in use; a Sound output row in Settings;
   or say so in the wiki), with what each does off the A9. Draft a reply to
   deletableman; MMagTech posts it.
2. **#268, starting with no TV answering.** Built on `saved-services-at-boot`
   (the display wait put back, and `cabinetos-saved-services.service` starting
   File access, Developer access, Remote Play and Tailscale at boot from
   settings.json; dry-run on the A9). Merge `main` into it, add its
   changelog entry, push to `testing`. **Tested remotely, no one at the TV
   (agreed with MMagTech, 2026-10-13):** install the testing image the way
   Settings does (`sudo systemctl start cabinetos-update-download.service`,
   wait for `state=ready` in `/run/cabinetos-update/status`, `sudo systemctl
   reboot`; check first that no game is running). Then (a) the boot: the log
   shows `cabinetos-saved-services` starting each way in that is on, before
   the app; (b) no display: as root, `echo off >
   /sys/class/drm/card*-HDMI-A-1/status` and restart the session; the log
   says it is waiting, SSH keeps answering, and Remote Play's behaviour is
   noted; `echo detect >` the same file, and the console comes up at
   `output 3840x2160 (from the display)`. The forced state resets at a
   reboot, so (a) and (b) are proven separately; a real power-on with the
   cable out waits until MMagTech is at the TV, if he wants it. **GitHub closed #268 on 2026-10-11
   when the first try merged, and that try was undone**: ask MMagTech to
   reopen it. Its changelog entry: a console started with the TV off or
   unplugged now comes up at the TV's own size.
3. **#329, release notes on the console:** MMagTech is asking his Discord
   group whether a small QR code on the "Update available" screen, opening
   What's new on a phone, is worth having. Nothing to build until he brings
   the answer.

Waiting, not for this session unless MMagTech raises them: Spinnich's answer
on the folder fix (a reply was drafted for MMagTech to post); the issue-review
session's list of doc changes, if it left one; #181 (updates and DLC,
Later, no date). Upstream to watch: rommapp/romm#5175 and #5198 (save
channels), rommapp/argosy-sigil#17 and #18, flyinghead/flycast#2541,
libretro/Genesis-Plus-GX#413.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). Never press anything on the
A9 he did not ask for while he may be using it.
