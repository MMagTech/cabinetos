# Lessons: the image, CI and releases

Read before touching `build_files/`, `system_files/`, `ci/`, the workflows, the testing channel or the installer.

## The image

- **`/var` IN A BOOTC IMAGE IS UNPACKED FROM THE FIRST IMAGE ONLY.** Anything a
  build writes there lands on machines installed after it and **never** on
  machines that upgraded into it. The build is green throughout. This bit open
  question 21 (flatpaks) and it nearly bit the storage root — which is why
  `/var/lib/cabinetos` is created every boot by
  `system_files/usr/lib/tmpfiles.d/cabinetos.conf` rather than by a `mkdir`.
  **Everything the image installs goes in `/usr`.**

- **A SYSFS SETTING A UDEV RULE MAKES CAN BE UNDONE BY TUNED AND BY THE
  DRIVER, AND TRIGGERING THE RULE BY HAND WILL NOT SHOW IT.** #228's rule set
  `power/control=on` on the sound controllers; triggered on a running console
  it worked, and after a real reboot it was gone: the driver turns runtime PM
  back on after its probe, and tuned's audio plugin sets
  `power_save_controller=Y` on every profile load. Before writing a rule for a
  device, read which tuned plugins touch it (`/usr/lib/python3*/site-packages/
  tuned/plugins/`), and **prove it across a real reboot**. tuned's own way in
  without copying Bazzite's profiles is the post-loaded profile
  (`/etc/tuned/post_loaded_profile`), laid over whatever profile is active.

- **Without that directory the console writes to `/`.** `storage::root()` tries
  `/var/lib/cabinetos`, and the session user cannot create it, so it falls back
  to the working directory — which for a systemd service is the root of the
  filesystem. `WorkingDirectory=/var/home/cabinet` in the unit makes the
  fallback visible rather than catastrophic, but the tmpfiles rule is the fix.

- **`ldd` prints "not found" and exits 0.** `build_files/install-frontend.sh`
  reads its output, not its status, for the frontend and all twenty-one cores.
  **Run the control** — the same script into a bare `fedora:44` names all six
  libraries the frontend needs, plus `libGL` for melonDS and **`libX11` and
  `libXext` for PPSSPP**, which nothing had written down anywhere.

- **`mesa-libGLES` is not in the base image, and `libGLESv2.so.2` is there
  anyway** — `libglvnd-gles` provides it. Do not "fix" a missing package that
  is not missing.

## Workflows and checks

- **An artifact's internal root is the least common ancestor of the files it
  actually found.** `build-core.yml` uploaded `cores/build/*.so` and
  `cores/system/`, so the twenty cores with no system files got an artifact
  rooted one directory deeper than PPSSPP's. Two layouts under one naming
  scheme, invisible until something downloaded them. Both jobs now stage into a
  fixed `artifact/` directory and upload that.

- **`just build` and `just generate-build-tags` decide whether to stamp the
  image by asking whether `git status -s` is empty.** An untracked directory in
  the working tree silently costs the image its labels and its git-sha tags.
  `image_payload/` and `.artifacts/` are gitignored for that reason, not for
  tidiness.

- **A called workflow's concurrency group can collide with its caller's**, and
  `cancel-in-progress` then has a run cancelling itself — a red build with no
  failing step. `build-core.yml` and `build-frontend.yml` therefore have **no**
  `concurrency:` at all, and `build.yml`'s group is a literal prefix rather
  than `${{ github.workflow }}`. If that ever needs to change, the fix is a
  `workflow_call` input used in the group string — but test it, because an
  invalid `concurrency` expression is a workflow that does not parse.

- **A workflow that build.yml CALLS must not also trigger itself where
  build.yml runs**, or one change queues the same twenty-one core builds
  twice. That is not merely wasteful: the runner concurrency cap means the
  duplicate starves the image build of the runners it is waiting for, and it
  doubled the wall clock of the change that introduced it. Cancelling one by
  hand leaves a red cross on a pull request whose code is fine, which is how a
  check stops meaning anything. So both called workflows now use
  **`pull_request: branches-ignore: [main]`** and have no `push:` trigger at
  all — build.yml covers main in both directions, and its path filter skips
  documentation only so it can never skip a change under `cores/`.
  **Do not narrow them to `branches: [main]`**: that is the 2026-09-16 hole
  where a stack of branches slipped past every check.

- **MEASURE THE CASE SOMEBODY IS WAITING ON, NOT THE CONVENIENT ONE.** On
  2026-09-22 this entry said a pull request builds in 6 minutes and a merge in
  16, having said 23 minutes an hour before; both were right for something and
  wrong for what a person was waiting on. The shape changed since: a pull
  request's run builds no image (under 2 minutes), a push to `testing` builds
  and publishes one (measured 2026-09-27: 11 minutes, and 24 when the cores
  rebuilt), and a merge to `main` promotes the tested image in under a minute.
  Read the current figure off `gh run list --workflow build.yml` rather than
  this line.

- **Only a push to `testing` or `main` builds an image.** A pull request's run
  checks and stops. To build another branch, push it to `testing`, or
  `gh workflow run build.yml --ref <branch>`.

- **Retargeting a pull request does not re-run CI.** Close and reopen it.

- **A `pull_request` EVENT CAN SIMPLY NOT FIRE, AND NOTHING SAYS SO.** Seen
  2026-09-20 on a pull request that changed `frontend/src/catalog.cpp` as well as
  docs, so `paths-ignore` did not apply: zero workflow runs were created, the
  branch showed *"no checks reported"*, and the pull request sat at
  **`MERGEABLE / CLEAN` with nothing having built it.** A green-looking pull
  request that ran no checks at all is the most dangerous state this repository
  can be in. **`gh pr checks` printing nothing is not the same as passing** —
  count them. The documented remedy works: close the pull request and reopen it.

- **A DOCUMENTATION-ONLY PULL REQUEST CORRECTLY RUNS NOTHING**, because
  `build.yml` ignores `**.md` and `docs/**`. That is expected and is a different
  thing from the fault above — check WHAT the pull request touches before
  deciding which one you are looking at.

- **AND THERE IS A THIRD CAUSE, WHICH IS JUST A RACE.** `gh pr checks` says
  *"no checks reported"* for the first few seconds after a push, between the
  workflow run being created and its jobs registering against the new commit.
  It is indistinguishable from the real fault by that command alone. **Tell them
  apart with `gh run list --branch <branch>`**: a run in `in_progress` means
  wait, and no run at all for the new head means the event did not fire — which
  is the one that needs the pull request closed and reopened. Seen 2026-09-21,
  where it briefly looked like the dangerous case and was not.

- **A PUSH TO `testing` CAN TAKE TWO MINUTES TO SHOW A RUN.** 2026-10-05 the
  run for a push appeared about 100 s after it, by which time a manual
  `gh workflow run` had been started; the push's run then cancelled the
  manual one (one image build per branch at a time). Wait three minutes
  before deciding a push fired nothing, and do not dispatch on top of it.

- **`ci/base-watch.txt` now watches the CORES' libraries too**, added
  2026-09-19 off a real `ldd` sweep rather than guessed at — including
  `libX11` and `libXext`, which are PPSSPP's and which nothing had written
  down. Three of the frontend's own were missing from that list as well. The
  image build would now fail rather than ship broken, but it would fail with
  no obvious cause; this is what makes the base-bump pull request say "read
  this" first.

- **The weekly base bump needs two clicks, not none.** It opens a pull request,
  but the build on it lands as `action_required` and waits for approval —
  `gh api -X POST /repos/MMagTech/cabinetos/actions/runs/<id>/approve`. And
  **read the relevant list**: when something breaks on real hardware, look at
  what `ci/base-watch.txt` does not watch.

- **A core build failing is not always the core.** `Curl error (28)` is the
  network. Re-run before reading anything into a single red core. This matters
  more now: twenty-one core builds gate every image build.

- **GitHub serves some repositories at 55 KB/s over git and 9.8 MB/s over
  HTTPS.** `build-core.sh` clones `--filter=blob:none` — but **NOT for
  submodules**, where the lazy blob fetch is thirty times slower than cloning
  them whole. The comment in the script says so; do not "tidy" it.

- **`core-manifest.json` IS on GitHub**, at `docs/core-manifest.json` in
  Cabinet, and has been since `37ca75d`.

## Promotion, the testing channel and updates

- **PROMOTION COMPARES THE FILES THE IMAGE IS BUILT FROM** (`ci/promote-tested.sh`).
  Until #156 it compared whole trees, docs included (learned 2026-09-27): a
  handover committed after the testing push made the merge BUILD a new image
  instead of promoting the judged one. Now `.md` files and `docs/` are
  ignored, the same files `build.yml`'s trigger ignores, except
  `docs/LICENCES.md`, which the image carries. Any other file committed
  after the testing push still makes main build.

- **CREATING `testing` AT A COMMIT GITHUB ALREADY HAS BUILDS NOTHING.** The
  first push to it carried no new commits (the same commit had gone up on
  `system-update`), so the trigger's path filter saw no changed files and
  no run started, silently. Start it by hand: `gh workflow run build.yml
  --ref testing`. Any later push with a new commit builds on its own.

- **A test on the TV that fakes an update state writes the REAL
  `settings.json`.** `--update-dir <dir>` makes the frontend read a status
  file you write, but what it concludes (`update_pending`, `_boot`,
  `_checked`, `_found`) goes into `/var/lib/cabinetos/config/settings.json`.
  A fake "ready" left there means the next real boot says "Update didn't
  apply". Copy the file aside first and put it back after. Headless
  captures should set `CABINETOS_STORAGE` to a scratch root.

- **Show a state, then wait for "next".** Running eight states on a timer
  went past faster than MMagTech could judge them, and the last one stayed
  up looking like a real failure. One state per message worked.

- **A CHANGE TO THE UPDATE SCREENS NEEDS TWO `testing` BUILDS TO TEST.**
  The check is done by the frontend the console is RUNNING, so the first
  build only delivers the new screens; they can only be seen answering a
  check once there is a second, newer build to find. Found 2026-09-25
  sending MMagTech to test the Update available panel against the very
  build that carried it: the old frontend answered, with no panel.

- **Anything in `/tmp` on the A9 is gone after a reboot**, including a log
  you started to watch an update. Read the previous boot with
  `journalctl -b -1`.

## The installer

- **Try every installer change in the VM first: `tools/installer-vm.sh`.**
  QEMU in podman on the A9 (`/dev/kvm` is open to all), an empty NVMe disk,
  no network unless `--net`, and a picture of the screen after each step.
  `repack` puts this checkout's `disk_config/iso.toml` into an existing ISO
  (kickstart as `/osbuild.ks`, the module list as `images/updates.img`) for a
  try in minutes; the ISO that ships is always CI's. Open question 5, *Built
  2026-10-07*, has what the installer does now and why.

- **After an install the VM boots the ISO again**, because QEMU's
  `bootindex` overrides the boot order the installer wrote. Real firmware
  boots the new drive. Switch the VM off and `boot` the disk alone.

- **The installer runtime has no `clear`, `stty`, `tput` or fb0.** Clear with
  escape codes; take the screen's width from
  `/sys/class/drm/card*-*/modes` of the connected output. Its only console
  font is `Lat2-Terminus16`; `setfont -d` doubles it.

- **Switching an Anaconda module off does not always hide its screen.** The
  Network module was off and the installer said so, and Network & Host Name
  was still drawn. Users really did go. Check the screen, not the config.

- **Keys typed into the VM go to whatever has focus.** An `n` in a shell
  command typed before the console switch landed answered the erase question
  and switched the VM off (correctly, with nothing written). Take a picture
  before typing; type slowly (very fast keys jam a key down); and read a disk
  with guestfish rather than by typing commands.

- **The media check passes on a perfect copy.** Run in the VM on the ISO
  itself, it counted to 100% and went on. The 2026-09-19 failure (`Supported
  ISO: no`, aborting at 4.8%) was reading the USB stick, not the ISO.

- **The installer runtime carries NO firmware**, which is why the GPU and
  Wi-Fi die in it and why neither matters. The tell: it also failed to load
  `gc_11_5_0_pfp.bin`, a file that *is* in Fedora's package and *is* in our
  image. **One thing that should not have failed was worth more than all the
  things that did.** Its kernel is stock Fedora's, not ours.

- **AN APPIMAGE UNPACKS WITH ITS OWN PERMISSIONS.** Xenia Edge's
  `squashfs-root/usr` is 0700, `cp -a` kept it, and the first testing image
  had the emulator readable by root alone: the console runs as `cabinet` and
  called it "not installed" (2026-09-30). The UI loop never saw it, because
  there the test copy belonged to `cabinet`. **An emulator is judged from the
  image, not only from a copy.** `install-xenia.sh` now fails the build on
  any file others cannot read.

- **A GITHUB ARTIFACT LOSES ITS FILES' EXECUTE BIT.** `actions/upload-artifact`
  does not keep permissions, so Cemu's program reached the image build as a
  plain file, and `install-cemu.sh`, which checked `-x` before copying, said
  there was no Cemu at all (2026-10-02). PCSX2 never showed it, because a
  shared library is never executed directly. **Check that a payload file
  exists, then set its mode in the image**, as `install-cemu.sh` now does.


- **A package can set a file capability with no script at all.** Sunshine's
  RPM marks `/usr/bin/sunshine` `cap_sys_admin,cap_sys_nice=p` in its file
  list; `rpm -qp --scripts` showed nothing of it. The dry run in the base
  image caught it only because the install script checked `getcap`. Check
  the installed result, not the package's description, and act on the
  package's own file list (`rpm -ql`), so a version that moves a file is
  still covered.
