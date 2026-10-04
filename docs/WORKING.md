# Working on CabinetOS

How this project is worked on: the machines, the build and test loop, and how
MMagTech wants it done. This changes rarely. What to work on next is
`docs/NEXT-SESSION.md`; what cost hours before is `docs/lessons/`.

## The loop

- **Nothing builds on the Mac.** Frontend changes: `tools/ui-loop.sh` builds on
  the A9 and shows it on the TV in about 30 s (`--restore` puts the console
  back on the image). Anything in the image (cores, units, `tmpfiles.d`, the
  session script) needs an image build: push to `testing`.
- **A merge promotes the tested image; it does not build.** Push the branch's
  final commit to `testing`, have MMagTech judge it on the A9, merge on his
  explicit go. Anything committed after the testing push makes main build
  instead, except documentation: `.md` files and `docs/` are not in the image
  and do not count (#156), apart from `docs/LICENCES.md`, which is. Write
  `NEXT-SESSION.md` inside the work's own branch, before or after the push.
- **Every session starts with only `main` and `testing`.** Delete a branch
  once it merges, on GitHub and locally. The one exception is PR #42's
  `base-update/44.20260921`, left open until after the first release
  (MMagTech).
- **Check CI yourself** (`gh run list --branch <b>`); never ask him to look at
  Actions. "No checks reported" can be a race, or an event that never fired.
- **PR bodies** lead with what the change does and why, say "Closes #N", and
  each finished issue gets a closing comment.
- **The TV**: say every step and what success looks like, then wait for "go".
- **Logs**: `sudo journalctl -o cat --since "-40 min" | grep -aE "\[launch\]|\[core\]"`;
  `-u cabinetos-session` shows almost nothing.
- **Never run `~/fb/sweep.sh` while MMagTech is testing on the TV.**

## Machines

| | | |
|---|---|---|
| **A9 Max** | `ssh -p 2222 -i ~/.ssh/cabinetos cabinet@192.168.1.212` | The reference console: Radeon 890M, gamescope/drm, 3840x2160. Judge anything visual here only. sudo's password: `cat /var/lib/cabinetos-files/password`. |
| **Test VM** | `ssh -p 2222 -i ~/.ssh/cabinetos cabinet@192.168.1.250` | Unraid, no Vulkan, llvmpipe. Headless checks only. sudo `cabinet`. |
| **RomM** | `192.168.1.10:6005` | MMagTech's server. |

Recovery if port 2222 on the A9 ever fails: at the TV with a keyboard,
Ctrl+Alt+F3, log in as `cabinet`, `sudo bootc rollback`, `sudo reboot`.

**Hand-made state on the A9, not in git:** `~/cores-dev/` (the image's cores
linked, plus a hand-built `cabinetos-ps2.so`; `--core-dir` for the loop),
`~/assets-dev/` (PCSX2's resources, `CABINETOS_ASSETS`), `~/fb/` (scratch
scripts: `speed.sh`, `xstate.sh`, `govtest.sh`, `fetch.sh`, `sweep.sh`),
`~/pcsx2-clean/` (PCSX2 checkout, a cache), `~/cabinetos-repo/` (an rsync of
this repo for builds that need podman). `ydotoold` is not started at boot.

**On the VM:** `~/frontend/` is a dev build and storage root, separate from
the session's `/var/lib/cabinetos`. Two 8 GB test disks: `vdc` (exFAT
"Games"), `vdd` (a Windows layout).

## How MMagTech wants this done

Plain answers. **Lead with what a change does and why it exists, in terms of
what breaks for the product, not the subsystem.** Keep the detail, after the
plain statement. Check the running machine before theorising, and say what is
verified and what is assumed. **Use the `MMagTech` handle, never a personal
name**: the repo is public.

