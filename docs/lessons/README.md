# Lessons

What cost hours on this project and cannot be recovered by reading the code.
**Read the ten rules below every session, then the one file for the work in
hand.** Add new lessons to the right file, in full; a summary of a warning is
not a warning.

## Ten rules for every session

1. **Judge anything visual on the A9 under gamescope, never on the VM or from
   an offscreen capture.** An offscreen capture has twice looked right while
   the TV showed nothing. ([testing](testing.md))
2. **Build it, then run it.** A green build and a passing screenshot say
   nothing about whether a game runs. ([emulators](emulators.md))
3. **Test two different games in one run.** Anything opened once and reused
   hides behind the app restart every deploy does. ([testing](testing.md))
4. **Measure the case somebody is waiting on**, and measure rather than
   reason. ([image and CI](image-and-ci.md))
5. **"No checks reported" is not a pass.** It can be a race or an event that
   never fired; `gh run list --branch <b>` tells them apart.
   ([image and CI](image-and-ci.md))
6. **A merge promotes the image tested on `testing`; anything committed after
   that push, docs included, makes main build instead.**
   ([image and CI](image-and-ci.md))
7. **Read the logs with `sudo journalctl -o cat | grep`**; `-u
   cabinetos-session` shows almost nothing. ([testing](testing.md))
8. **A fact carried across from Cabinet or a comment is a fact nobody has
   checked.** A stale comment reads exactly like a current one.
   ([emulators](emulators.md))
9. **Anything the image writes to `/var` reaches only fresh installs; put it in
   `/usr`.** ([image and CI](image-and-ci.md))
10. **Every scripted edit must assert its anchor**, or the build is green with
    the fix absent. ([frontend](frontend.md))

## Which file for which work

| File | Read before |
|---|---|
| [image-and-ci.md](image-and-ci.md) | the image, the workflows, promotion, updates, the installer |
| [testing.md](testing.md) | judging anything, the A9, the TV loop, headless tests, logs |
| [emulators.md](emulators.md) | cores, PCSX2, Dolphin, input, saves and states |
| [frontend.md](frontend.md) | screens, words on screen, network, Bluetooth, pairing, first run |
| [cases.md](cases.md) | nothing; past investigations in full, for reference |
