# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-10: images are signed. `testing` is 2026.10.10.3, the first signed
build, from `signed-images` (#311, open; measurements on #135). The A9 (Wi-Fi,
**192.168.1.109**, `CABINETOS_A9=cabinet@192.168.1.109`) still runs
2026.10.09.5, unsigned and unchecked. #310 is built on `arcade-folders` (#312,
open, NOT on `testing`). LizardByte/Sunshine#5885 is approved by a maintainer
with no comments; its one red check (Homebrew on Ubuntu) fails on Sunshine's
master too.

## Next

Milestone 7, Ready to ship (docs/ROADMAP.md), in this order. Lessons file:
image-and-ci.md.

1. **Signed images (#311).** MMagTech: System update to 2026.10.10.3 (PIN),
   restart, then once `sudo bootc switch --enforce-container-sigpolicy
   ghcr.io/mmagtech/cabinetos:testing` and reboot. `rpm-ostree status` must
   show `ostree-image-signed:docker://...`. Then a couple of updates from
   System update, then he merges #311.
2. **Arcade folders (#312).** After #311 is merged: rebase on main, push to
   `testing` (plain fast-forward). `cores/arcade-sets.sh` runs in CI for the
   first time there (a pull request run builds no cores); check the core job
   logs "8310 sets" and "5275 sets" and the image build logs both lists.
   Then his TV checks, listed on #312.
3. **A diagnostic report (#195).**
4. **Licences checked and the full texts shipped (#120).**
5. **At the first release (#308):** the installer on a GitHub Release, and the
   page, wiki and README pointing at it. Try the installer's new
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
