# CabinetOS

A console operating system for a Beelink SER5, built on [Bazzite](https://github.com/ublue-os/bazzite).

Power on, the frontend appears, everything from there is driven with a
controller. The library comes from a self-hosted [RomM](https://github.com/rommapp/romm)
server — games, artwork, metadata, firmware, and save state sync — so a game
started on the [Cabinet](https://github.com/MMagTech/cabinet) tvOS app can be
continued on the television it's plugged into.

No desktop. No terminal. No package manager. No browser. No app store.

> **Start here:** [`docs/PROJECT.md`](docs/PROJECT.md) is the specification and
> the phase plan. Read it before changing anything in this repository.
>
> **Current status: Phase 1 — base image.** Nothing has been built or booted
> yet. The frontend does not exist.

---

## How it is built

CabinetOS is a [bootc](https://bootc-dev.github.io/bootc/) image: the entire
operating system is one signed, versioned OCI image. There is no partial state
where the frontend and the system disagree about what version they are, and a
bad update is a rollback rather than a recovery USB stick.

Bazzite provides the kernel, the Valve-patched Mesa stack, gamescope, the
controller drivers (`xone`, the GameCube adapter module, force-feedback wheels)
and the power and thermal handling. CabinetOS removes the desktop and Steam and,
from Phase 2 onwards, adds its own session and frontend on top.

```
ghcr.io/ublue-os/bazzite:stable-44.20260908   (pinned by digest)
                │
                ├── build_files/strip-steam.sh     Steam, Lutris, Proton/umu
                ├── build_files/strip-desktop.sh   display manager, KDE apps,
                │                                  browser, terminal, app store
                └── bootc container lint
                        │
                        ▼
        ghcr.io/mmagtech/cabinetos:latest
                        │
                        ├── qcow2          → VM boot test
                        └── anaconda-iso   → USB stick → SER5
```

### Repository layout

| Path | What it is |
|---|---|
| `docs/PROJECT.md` | **The specification.** Read first. |
| `Containerfile` | Pins the Bazzite base and runs the build. |
| `build_files/build.sh` | Build entry point. Calls the strip scripts, records the package diff, runs sanity checks. |
| `build_files/lib.sh` | `remove_pkgs` and friends. Every removal goes through here. |
| `build_files/strip-steam.sh` | Removes Steam and the PC-gaming layer. |
| `build_files/strip-desktop.sh` | Removes the display manager and desktop applications. |
| `system_files/` | Files overlaid onto the image. Empty in Phase 1; Phase 2 puts the session units here. |
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

Runs on every push to `main`, on pull requests, and manually.

1. **Lint** — shellcheck over `build_files/`. Fails in seconds rather than after
   a twenty-minute image build.
2. **Build** — `podman build` against the pinned Bazzite digest.
3. **Rechunk** — re-layers the image so updates ship small deltas. This is what
   keeps the Phase 7 console update from being a multi-gigabyte download every
   time.
4. **Push** — to `ghcr.io/mmagtech/cabinetos`, tagged `latest`, the date, and
   the date plus commit SHA.
5. **Sign** — with cosign, so the installed system can verify an update came
   from this repository before rebooting into it.

Pull requests stop after step 3. They prove the image builds; they cannot
publish or sign anything.

### `build-disk.yml` — the disk images

Manual only (**Actions → Build disk images → Run workflow**), because disk
builds are slow and you only want one when you are about to install something.

Produces a `qcow2` for VM testing and an `anaconda-iso` for real hardware, and
uploads both as workflow artifacts with a 7-day retention.

---

## Getting the first build running

Four things to do on GitHub, in order.

### 1. Create the repository

It must be named **`cabinetos`** — `build-disk.yml` derives the image name from
the repository name.

```bash
gh repo create MMagTech/cabinetos --private --source=. --remote=origin
git add -A
git commit -m "Phase 1: base image"
git push -u origin main
```

### 2. Generate a signing key

Install cosign (`brew install cosign`), then, in the repository root:

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

Then push, or trigger **Actions → Build CabinetOS image → Run workflow**.

### After the first successful build

The GHCR package is created **private** by default. Make it public at
**github.com/users/MMagTech/packages/container/cabinetos/settings → Change
visibility**.

This matters because the installer ISO's kickstart runs `bootc switch` against
`ghcr.io/mmagtech/cabinetos:latest`. A private package means the installed SER5
cannot pull updates without credentials.

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

## Installing to the SER5

1. Plug in the USB stick, plus a keyboard and mouse. The installer is the one
   part of CabinetOS that needs them — after this you should never need them
   again, and if you do, that is a bug.
2. Power on and press **Delete** or **F7** during the Beelink splash for the
   boot menu. Select the USB device.
3. Anaconda starts. Set the destination to the internal NVMe, create a user, and
   install.
4. Reboot and remove the stick.

Phase 1 boots to a console login prompt. That is the expected result and the
proof that Phase 1 is done — the frontend arrives in Phase 2.

While you are in the firmware, two settings worth changing now:

- **Restore on AC power loss → Power On.** A console should come back after a
  power cut without someone finding the button.
- **Secure Boot → off.** Bazzite's signed kernel modules need Universal Blue's
  key enrolled otherwise, which is an extra step with no benefit here.

---

## Credits and licence

CabinetOS is MIT licensed. See [`LICENSE`](LICENSE).

The `Containerfile`, `Justfile`, `disk_config/` and `.github/workflows/` are
derived from the [Universal Blue image template](https://github.com/ublue-os/image-template),
which is Apache-2.0. Bazzite, Universal Blue and ChimeraOS did the work that
makes a project like this a few weeks rather than a few years.
