# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-10: updates are signed and checked (#311 merged, #135). The A9 (Wi-Fi,
**192.168.1.109**, `CABINETOS_A9=cabinet@192.168.1.109`) is switched to
`ostree-image-signed` and took 2026.10.10.4 through the check from System
update. LizardByte/Sunshine#5885 is merged upstream (`3411311a`) but in no
release yet: keep our patch until a release has it (#304).
Arcade folders (#310) is #312: if it is still open, finish it first (its TV
checks are on the pull request).

## Next

Milestone 7, Ready to ship (docs/ROADMAP.md), in this order.

1. **A diagnostic report (#195).**
2. **Licences checked and the full texts shipped (#120).**
3. **At the first release (#308):** the installer on a GitHub Release, and the
   page, wiki and README pointing at it. Try the installer's
   `--enforce-container-sigpolicy` in `tools/installer-vm.sh` before that ISO.

Also in the milestone: booting with the TV off (#268). After first release:
#290, #283, #303, #305.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). Never press anything on the
A9 he did not ask for while he may be using it.
