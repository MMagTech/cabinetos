# CabinetOS

A console operating system for x86-64 PC hardware, built on [Bazzite](https://github.com/ublue-os/bazzite).

Power on, the frontend appears, everything from there is driven with a
controller. The library comes from a self-hosted [RomM](https://github.com/rommapp/romm)
server — games, artwork, metadata, firmware, and save state sync — so a game
started on the [Cabinet](https://github.com/MMagTech/cabinet) tvOS app can be
continued on the television it's plugged into.

No desktop. No terminal. No package manager. No browser. No app store.

The controller is the primary input device and the only one the design assumes
exists — every screen must be navigable with one alone. A keyboard and mouse work
if attached, for things like typing a RomM server address, but nothing may
*require* them. A console ships with nothing listening on the network; a row in
Settings turns on file access over SFTP, so you can copy your saves off, and
shows the address and a password the machine generated for itself. It lands in
Phase 6 — see `docs/PROJECT.md`, open question 9.

> **Start here:** [`docs/PROJECT.md`](docs/PROJECT.md) is the specification and
> the phase plan. Read it before changing anything in this repository.
>
> **Current status: Phase 3/5, and the console runs.** 1147 of 1644 games in
> the reference library are playable, all twenty-one cores are built, and every
> platform this console claims to play has been launched and measured. Saves,
> memory cards and save states sync both ways with RomM. What is missing is the
> television: the image does not yet carry the frontend or the cores, and no UI
> is tuned until it is running on the reference machine.

> [!WARNING]
> **These images have SSH enabled by default, with a shell and password
> authentication.** That is deliberate for Phases 1–5, which are developed by
> booting images and inspecting them, but it is not the shipping configuration:
> a shipping console starts with nothing listening and offers file access as a
> switch in Settings. Until Phase 6, do not install CabinetOS on a machine
> exposed to an untrusted network.

### Hardware

The reference machine is a GEEKOM A9 Pro (Ryzen AI 9 HX 370, Radeon 890M),
because that is the box available. Older text in `docs/` says "the SER5" and
means this one. **It is not the product's definition.** The target is
generic x86-64 PC hardware: the reference machine sets the performance floor,
nothing machine-specific goes in the image, and hardware capabilities that may or
may not be present — HDMI-CEC, for instance — are detected at runtime rather than
designed around. NVIDIA is out of scope until there is hardware that needs it;
Bazzite publishes NVIDIA variants, so it is a base-image change, not a rewrite.

### RomM

- **Works with RomM 5.1 or newer**, tested on 5.1.0 and 5.3.1.
- **Saves and save states made by Cabinet and CabinetOS carry over** between
  them. Progress saved in RomM's web player or in other apps does not: the
  console does not read it, and leaves it untouched on the server.

---

## How it is built

CabinetOS is a [bootc](https://bootc-dev.github.io/bootc/) image: the entire
operating system is one signed, versioned OCI image. There is no partial state
where the frontend and the system disagree about what version they are, and a
bad update is a rollback rather than a recovery USB stick.

Bazzite provides the kernel, the Valve-patched Mesa stack, gamescope, the
controller drivers (`xone`, the GameCube adapter module, force-feedback wheels)
and the power and thermal handling. CabinetOS removes the desktop and Steam and
adds its own session, its frontend and twenty-one emulator cores on top.

```
ghcr.io/ublue-os/bazzite:stable-44.20260908   (pinned by digest)
                │
                ├── build_files/strip-steam.sh     Steam, Lutris, Proton/umu
                ├── build_files/strip-desktop.sh   display manager, KDE apps,
                │                                  browser, terminal, app store
                ├── build_files/enable-ssh.sh      sshd + sftp (development only)
                ├── build_files/configure-session.sh   boot into gamescope
                ├── build_files/install-frontend.sh    THE CONSOLE ITSELF:
                │        /usr/bin/cabinetos-frontend
                │        /usr/lib/cabinetos/cores/          21 cores, 259 MB
                │        /usr/share/cabinetos/system/       PPSSPP's, 13 MB
                └── bootc container lint
                        │
                        ▼
        ghcr.io/mmagtech/cabinetos:latest
                        │
                        ├── qcow2          → VM boot test
                        └── anaconda-iso   → USB stick → real hardware
```

**The frontend and the cores are not in this repository and are not built by
the image build.** They come from `build-frontend.yml` and `build-core.yml`,
which the image build calls, and `ci/stage-image-payload.sh` collects what they
produce into `image_payload/` before `podman build` runs. Building the image on
a Linux box by hand means doing the same thing first:

```bash
ci/stage-image-payload.sh              # from frontend/build and cores/build
ci/stage-image-payload.sh <artifacts>  # from a merged CI artifact download
just build
```

It is not optional and it fails loudly. Until 2026-09-19 the image contained
none of it, and an installed machine booted into a gamescope session running
`sleep infinity` — a black screen.

### Repository layout

| Path | What it is |
|---|---|
| `docs/PROJECT.md` | **The specification.** Read first. |
| `Containerfile` | Pins the Bazzite base and runs the build. |
| `build_files/build.sh` | Build entry point. Calls the strip scripts, records the package diff, runs sanity checks. |
| `build_files/lib.sh` | `remove_pkgs` and friends. Every removal goes through here. |
| `build_files/strip-steam.sh` | Removes Steam and the PC-gaming layer. |
| `build_files/strip-desktop.sh` | Removes the display manager and desktop applications. |
| `build_files/enable-ssh.sh` | Enables SSH and SFTP for development. **Phase 6 must undo this.** |
| `build_files/install-frontend.sh` | Puts the frontend, the 21 cores and PPSSPP's system files in the image, and proves every library they link against resolves inside it. |
| `frontend/` | The console's own source. C++20, SDL3, one GLES context. See `frontend/README.md`. |
| `cores/build-core.sh` | Builds one libretro core at an exact pinned commit, and asserts that commit back out of the finished `.so`. |
| `ci/stage-image-payload.sh` | Collects the frontend and the cores into `image_payload/` for the image build, and refuses if anything is missing. |
| `ci/check-base-update.sh` | Detects and classifies Bazzite base updates. |
| `ci/base-watch.txt` | Packages CabinetOS depends on. Grows with the project. |
| `base-manifest.txt` | The base's package list as of the current pin. Generated. |
| `system_files/` | Files overlaid onto the image: the session script and its unit, the `cabinet` user, and the `tmpfiles.d` rule that creates `/var/lib/cabinetos` at every boot. |
| `disk_config/` | `bootc-image-builder` configuration for the qcow2 and the ISO. |
| `cabinetos.env` | Image name, owner, tags. |
| `Justfile` | Build recipes. Linux only — see below. |

### Why nothing builds on a Mac

Podman, bootc and `bootc-image-builder` need a Linux kernel and, for the disk
images, KVM. **This project is never built locally.** All builds run in GitHub
Actions. The `Justfile` exists so the CI steps are readable and so the build can
be run on a Linux machine later, not because it is expected to run here.

---

## CI

### `build.yml` — the image

Runs on every push to `main`, on pull requests, and manually. Documentation-only
changes are skipped; `frontend/**` is **not** skipped, because the image carries
the frontend.

1. **Lint** — shellcheck over `build_files/`, `ci/` and `cores/`. Fails in
   seconds rather than after a twenty-minute image build.
2. **Frontend and cores** — calls `build-frontend.yml` and `build-core.yml`,
   rather than repeating their steps. About ten minutes, and it means every
   image build proves all twenty-one cores are at their pinned revisions.
   `ci/stage-image-payload.sh` then collects the results and refuses if any is
   missing.
3. **Build** — `podman build` against the pinned Bazzite digest.
4. **Rechunk** — re-layers the image so updates ship small deltas. This is what
   keeps the Phase 7 console update from being a multi-gigabyte download every
   time.
5. **Push** — to `ghcr.io/mmagtech/cabinetos`, tagged `latest`, the date, and
   the date plus commit SHA.
6. **Sign** — with cosign, so the installed system can verify an update came
   from this repository before rebooting into it.

Pull requests stop after step 4. They prove the image builds; they cannot
publish or sign anything.

### `base-update.yml` — keeping up with Bazzite

Weekly, and manually. Bazzite rebuilds daily, so this does not just watch the
digest — it pulls the new base, diffs its package manifest against
`base-manifest.txt`, and classifies the result against `ci/base-watch.txt`:

- **ROUTINE** — nothing CabinetOS depends on moved. Most weeks.
- **RELEVANT** — the kernel, Mesa, gamescope, a controller driver, bluez,
  PipeWire or similar changed. Read it.

It also flags *strip-list drift*: a package the strip scripts remove that
upstream has dropped or renamed, which would silently turn a removal into a
no-op and let a desktop application back into the image.

It opens a pull request and never merges one. Build a qcow2 from the branch and
boot it before merging.

> **Note:** pull requests opened with the default `GITHUB_TOKEN` do not trigger
> other workflows, so the image build will not run on them automatically. Add a
> fine-grained PAT as a `BASE_UPDATE_TOKEN` secret (contents + pull-requests
> write) and the workflow will use it, or just close and reopen the PR.

### `build-disk.yml` — the disk images

Manual only (**Actions → Build disk images → Run workflow**). Build the
installer ISO **once per release**: an installed console updates itself from
GHCR, so a stick only has to be new enough to install, and a change to
`disk_config/iso.toml` is the other reason to make one. It builds the
`anaconda-iso` alone by default; `qcow2` (a VM disk) or both are an option.
Artifacts are kept for 90 days. The ISO builder is pinned by digest; try any
installer change in a throwaway VM first with `tools/installer-vm.sh`.

---

## Getting the first build running

Three things to do on GitHub, plus one you can skip for now.

### 1. Create the repository

It must be named **`cabinetos`** — `build-disk.yml` derives the image name from
the repository name.

```bash
gh repo create MMagTech/cabinetos --private --source=. --remote=origin
git add -A
git commit -m "Phase 1: base image"
git push -u origin main
```

### 2. Generate a signing key — optional, and skippable for now

**The build works without this.** If no `SIGNING_SECRET` is set, the image is
built and published unsigned and the run summary says so. Signing matters for
Phase 7, when an installed machine needs to verify that an update really came
from this repository. Skip it until then if you like.

Cosign, without Homebrew:

```bash
curl -sSLo /tmp/cosign https://github.com/sigstore/cosign/releases/latest/download/cosign-darwin-arm64 && chmod +x /tmp/cosign && sudo mv /tmp/cosign /usr/local/bin/cosign
```

Then, in the repository root:

```bash
cosign generate-key-pair
```

This writes `cosign.key` (private) and `cosign.pub` (public). It will ask for a
password — **leave it empty**, or CI cannot use the key non-interactively.

- Commit `cosign.pub`. It is public by design.
- Never commit `cosign.key`. `.gitignore` covers it. Anyone holding it can sign
  an image your console will accept as an update.

### 3. Add the key as a repository secret

**Settings → Secrets and variables → Actions → New repository secret**

- Name: `SIGNING_SECRET`
- Value: the entire contents of `cosign.key`, including the `-----BEGIN` and
  `-----END` lines.

```bash
gh secret set SIGNING_SECRET < cosign.key
```

### 4. Check Actions permissions

**Settings → Actions → General → Workflow permissions** must be set to
**Read and write permissions**, or the push to GHCR fails with a 403.

This one is not optional — it is the most common reason a first build fails.

Then push, or trigger **Actions → Build CabinetOS image → Run workflow**.

### After the first successful build

The GHCR package is created **private** by default. Make it public at
**github.com/users/MMagTech/packages/container/cabinetos/settings → Change
visibility**.

This matters because the installer ISO's kickstart runs `bootc switch` against
`ghcr.io/mmagtech/cabinetos:latest`. A private package means the installed
machine cannot pull updates without credentials.

---

## Writing the disk image to a USB stick

Build the ISO first: **Actions → Build disk images → Run workflow**, tag
`latest`, type `anaconda-iso`. Download the artifact when it finishes and unzip
it — the ISO is at `bootiso/install.iso`.

On macOS:

```bash
diskutil list
```

Identify the USB stick carefully — the next command destroys everything on the
target device. Look for the right size and the right name, not the right number;
disk numbers move.

```bash
diskutil unmountDisk /dev/diskN
```

```bash
sudo dd if=bootiso/install.iso of=/dev/rdiskN bs=4m status=progress
```

Note `rdiskN`, not `diskN` — the raw device is many times faster.

```bash
diskutil eject /dev/diskN
```

---

## Installing to real hardware

1. Plug in the USB stick and a keyboard. The installer is the one part of
   CabinetOS that *requires* one (and the mouse makes its one screen easier).
   Afterwards it is never necessary; any CabinetOS screen that cannot be
   completed with a controller alone is a bug.
2. Power on and press **F7** during the firmware splash for the boot menu
   (**Delete** for setup) on a GEEKOM A9 Pro; other machines differ. Select
   the USB device, then **Install CabinetOS**.
3. One question: *This will erase this machine's 2.0 TB drive (model).
   Continue? (y/n)*. With more than one internal drive it lists them first
   and asks which. USB drives are never offered. **n** switches the machine
   off with nothing written.
4. **Time & Date**: pick the time zone (it drives Appearance's scheduled dark
   hours), **Done**, then **Begin Installation**. The disk, the `cabinet`
   account and everything else are already answered, and no network is
   needed.
5. It restarts by itself into CabinetOS. Take the stick out once it has.

**It boots into first run**: network (Wi-Fi works here, where the console's
firmware is), then your RomM server, then pairing a controller.

While you are in the firmware, two settings worth changing now:

- **Restore on AC power loss → Power On.** A console should come back after a
  power cut without someone finding the button.
- **Secure Boot → off.** Bazzite's signed kernel modules need Universal Blue's
  key enrolled otherwise, which is an extra step with no benefit here.

---

## Credits and licence

CabinetOS's own source is MIT licensed. See [`LICENSE`](LICENSE).

**The emulator cores it ships are not, and six of them are free for
non-commercial use only.** [`docs/LICENCES.md`](docs/LICENCES.md) lists every
core, its licence, its upstream and the exact commit it was built from.
**CabinetOS is free, is not sold, and takes no donations** — that is what keeps
those cores legitimate in this build, and it is a deliberate constraint rather
than an oversight.

CabinetOS ships no games, no BIOS and no firmware. All of those come from your
own RomM server.

The `Containerfile`, `Justfile`, `disk_config/` and `.github/workflows/` are
derived from the [Universal Blue image template](https://github.com/ublue-os/image-template),
which is Apache-2.0. Bazzite, Universal Blue and ChimeraOS did the work that
makes a project like this a few weeks rather than a few years.
