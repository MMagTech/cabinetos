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

## The image, the build and CI

Moved here from the README on 2026-10-09 (#190), checked against the
workflows and `Containerfile` that day. The README is now for people the
console is shown to.

**The image.** CabinetOS is a [bootc](https://bootc-dev.github.io/bootc/)
image. `Containerfile` pins the Bazzite base by digest (`44.20261006.1` at the
time of writing) and runs `build_files/build.sh`, which calls the other scripts
in `build_files/`: removing the PC-gaming storefronts (Steam itself stays, for
its one entry) and the desktop, the session,
the frontend and its libraries, Sunshine, Steam's session, RPCS3, Xenia, Cemu,
xemu's drive, the Flathub emulators' pins and the branding. `system_files/` is
overlaid onto the image.

**What the image carries that this repository does not hold.** The frontend
(`build-frontend.yml`), the twenty-two libretro cores at pinned commits
(`build-core.yml`, cached per revision), PCSX2 (`build-pcsx2.yml`) and Cemu
(`build-cemu.yml`). `ci/stage-image-payload.sh` collects them into
`image_payload/` before `podman build`, and refuses if anything is missing.
Eden and xemu come from Flathub on a console's first boot with a network, at
the versions `system_files/usr/share/cabinetos/flatpaks.list` names.

**`build.yml`.** Lint (shellcheck over `build_files/`, `ci/` and `cores/`), the
frontend and cores, the image build, then a push to
`ghcr.io/mmagtech/cabinetos`. A push to `testing` publishes `cabinetos:testing`;
a merge to `main` promotes the tested image to `latest` rather than building
(the "Promote the tested image" job). The image is signed with cosign only when
the `SIGNING_SECRET` secret is set; consoles do not verify signatures yet
(#135). Pull requests build and never publish. The path filter skips `**.md`
and `docs/**`, except `docs/LICENCES.md`, which is in the image.

**Weekly, on Mondays.** `base-update.yml` checks for a new Bazzite base and
opens a pull request listing what moved; `pad-db-update.yml` refreshes the
controller list (`frontend/data/gamecontrollerdb.txt`);
`terra-fallback-update.yml` keeps the stored spare of Steam's session
(`build_files/terra-fallback/`) current. None of them
merges anything.

**`build-disk.yml`**, by hand only: the installer ISO (`anaconda-iso`, an
artifact kept 90 days) or a qcow2. Build it once per release; an installed
console updates itself. Try installer changes in a throwaway VM first with
`tools/installer-vm.sh`.

**Repository settings the build depends on.** Settings, Actions, General,
Workflow permissions: **Read and write**, or the push to GHCR fails with a 403.
The GHCR package must be **public**, or installed consoles cannot pull updates.

**The site.** `docs/index.html` (the showcase page), `docs/wiki/` and
`docs/media/` are served by GitHub Pages from `main`'s `docs/` folder;
`docs/_config.yml` keeps these working notes off the site. Media is captured on
the A9: stills with the frontend's own `SIGUSR1` capture, video from
gamescope's PipeWire node with `gst-launch-1.0` (both on the image), encoded to
720p60 H.264 with the image's `ffmpeg`.
