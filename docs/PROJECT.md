# CabinetOS

> **Read this file first.**
> This is the living specification for CabinetOS. Every session working on this
> repository should read it before touching anything else, and should update it
> when decisions are made, phases change status, or new uncertainty appears.
>
> Rules for maintaining this document:
> - When a phase is finished, change its status and say what actually shipped,
>   not what was planned.
> - When you are unsure about something, add it to **Open questions**. Do not
>   guess and do not quietly resolve an open question without saying so.
> - When a decision is reversed, record the reversal rather than editing history.

---

## What CabinetOS is

CabinetOS is a console operating system for generic x86-64 hardware.

The user experience target is a PlayStation or an Xbox, not a Linux PC. You
press the power button, the frontend appears, and everything from that point on
is driven with a controller.

The user never sees:

- a desktop
- a terminal or console
- a package manager
- a file browser
- a Linux error message

### The input model

**The controller is the primary input device, and the only one the design may
assume exists.**

Every screen, every flow, and every recoverable error state must be fully
navigable with a controller alone. Any state the machine can reach where a
controller is not sufficient to continue is a bug, not a limitation. This is the
single constraint that most shapes the frontend: it rules out designs that are
only tolerable with a pointer, and it is why text entry gets an on-screen
keyboard rather than a text field and a shrug.

**A keyboard and mouse are supported, but never required.** If one is plugged
in, it should work — typing a RomM server address, a Wi-Fi password or a search
query is genuinely faster on a keyboard, and there is no reason to refuse it.

The distinction that matters:

| | |
|---|---|
| **Required** | Controller. Everything must work with one. |
| **Supported** | Keyboard and mouse. A convenience, never a dependency. |
| **Never** | A flow that *only* works with a keyboard or mouse. |

Concretely: the on-screen keyboard is the baseline for all text entry and is
always reachable. A physical keyboard types into the same field when present.
Neither implementation may be the only one.

---

## Hardware

**The target is x86-64 PC hardware, not one specific machine.**

The development and reference machine is a Beelink SER5 mini PC (AMD Ryzen 5,
Radeon Vega integrated graphics) because that is the spare box available. It is
not the product's definition. If the project works out, the hardware may change,
and CabinetOS must not have quietly grown dependencies on this particular box in
the meantime.

What follows from that:

- **The reference machine sets the performance floor, not the ceiling.** Phase 8
  tunes PS2, GameCube, Dreamcast and Naomi to run acceptably on Vega integrated
  graphics. Anything faster is a bonus, and nothing may *require* a specific GPU.
- **No machine-specific quirks in the image.** No SER5 firmware workarounds, no
  hard-coded device paths, no assumptions about a particular audio or network
  chip. If the SER5 needs something unusual, that is a strong signal the fix
  belongs upstream in Bazzite, not here.
- **Hardware capability is discovered, not assumed** — with one exception, HDMI-CEC,
  which is a requirement rather than a capability. See below.
- **AMD for now.** Bazzite publishes NVIDIA variants of its images, so an NVIDIA
  machine is a base-image change rather than a rewrite — but it doubles the
  images to build and test, so it stays out of scope until there is hardware
  that needs it. See open question 11.

### HDMI-CEC is a hard requirement

**The console must be able to turn the television on, and be woken by it.** That
is not a nicety. It is a large part of what separates a console from a computer
sitting under the telly, and this product is defined by that difference.

Treat it like the controller: a thing the design may assume exists.

#### A USB adapter is required, on essentially any mini PC

**The Beelink SER5 has no wired CEC pin. Neither does the GMKtec K11.** Nor do
almost any x86 mini PCs — the HDMI connector carries the CEC line, but the board
does not wire it to anything. This is close to universal and should be planned
for rather than checked hopefully.

So the hardware requirement is a **USB CEC adapter**, and it is part of the
bill of materials for any CabinetOS machine.

| Adapter | Verdict |
|---|---|
| **Pulse-Eight** | The known good choice. Has a dedicated `inputattach` unit in the image. |
| RainShadow | Also has a unit present; untested here. |
| Ugreen | **Behaves inconsistently in native mode.** Avoid, or use legacy mode. |

#### Two modes, and they behave differently

Bazzite ships both CEC stacks, switched with `ujust cec-mode` or the Bazzite
Portal:

- **Legacy** — `libcec` and `cec-ctl`, driving the `cec-onboot`, `cec-onsleep`
  and `cec-onpoweroff` services. This is the mode for external USB adapters,
  and therefore **the mode that matters on x86**.
- **Native** — Valve's `linux-cec`/`cecd`, for devices with CEC wired into the
  kernel. **`cecd` is known to interfere with wakeup on HTPC setups using
  dongles**, which is exactly our configuration, so native is not the default
  to reach for here.

Native mode is also **incomplete in this image**: Bazzite's native mode enables
`steamos-manager-configure-cecd.service` alongside `cecd`, and `steamos-manager`
is `bazzite-deck`-only. Pulling it in would drag the SteamOS management layer
into the image, which constraint 4 rules out. Legacy adapter mode is the
supported path; this is recorded rather than fixed.

#### The packages are protected, by an assertion rather than a comment

Every package CEC needs looks like cruft in a package list — `v4l-utils` reads
as a webcam package, `linuxconsoletools` as a joystick utility — and both are
load-bearing. `build_files/require-cec.sh` names each one with its reason and
**fails the build** if any goes missing, whether by our hand or an upstream
change. Audited and confirmed present on the running image: `libcec`,
`v4l-utils`, `linux-cec`, `linuxconsoletools`, the four udev rules, and all
seven CEC systemd units.

#### CEC will not be tested by the author

**Marcus is not buying a CEC adapter. Testing will come from other people.**
That is a fine arrangement and it has consequences worth stating plainly,
because they shape how the feature must be built:

- **The no-adapter path is the one that gets exercised daily**, on every machine
  the author owns. That is lucky — it is also the common case for anyone who
  has not bought an adapter yet — but it means the with-adapter path only ever
  runs on someone else's television.
- **CEC must never be able to break boot or the session.** A missing, unplugged
  or misbehaving adapter has to be a quiet no-op. If CEC code can hang startup
  when no adapter is present, the author will not see it; if it can hang startup
  when one *is* present, the author will not see that either.
- **The settings UI has to be self-explanatory**, because the person switching
  modes will be debugging their own television without the author watching. A
  mode switch that needs explaining is a mode switch that will be reported as
  broken.
- **There must be a way to get a useful report back.** Which adapter, which
  mode, which television, what happened. Developer mode's SSH is the obvious
  channel for anyone technical; for everyone else the UI should show enough
  state — adapter detected, mode, last CEC event — that a photograph of the
  screen is a useful bug report.
- **Be honest in the status.** A hard requirement nobody on the project can test
  is a hard requirement in name only until someone confirms it. Until then it is
  specified and implemented, not verified, and the phase notes should say so
  rather than implying otherwise.

#### CabinetOS must surface the mode switch itself

**CabinetOS removes the terminal, and with it `ujust` and the Bazzite Portal** —
both mechanisms Bazzite provides for choosing a CEC mode. Verified: `ujust` is
gone from the built image.

So mode selection has to exist in CabinetOS's own settings, or the choice is
unreachable on a finished machine. **Phase 7 task**, recorded there.

The reference implementation is `/usr/share/ublue-os/just/81-bazzite-fixes.just`,
which survives in the image even though `ujust` does not. It writes `CEC_MODE`
to `/etc/default/cec-control`, toggles the three legacy units against
`cecd.service`, handles the two `inputattach` templates, and writes a `cecd`
config with `logical_address = "playback"`, `suspend_tv = true` and
`allow_standby = false`.

---

## The backend model

**RomM is the single source of truth for the library.** CabinetOS is a client.

Everything comes from the RomM server over its REST API:

- games and ROM files
- artwork and metadata
- platform information
- firmware and BIOS files
- save files and save states

Authentication uses a **client API token** obtained through RomM's device
pairing flow, so that a machine with no keyboard can be authorised without
typing a password.

CabinetOS stores nothing the server does not already know, with two exceptions:

1. the local cache of downloaded games
2. emulator configuration

Games are downloaded on demand and cached locally, and can additionally be
**kept** — pinned to the internal drive so they are never re-fetched or evicted.
The user can see what is cached, what is kept, and how much space is free, and
can move games between the two. See *Emulation* for why the distinction matters.

Saves and save states sync back to the server, so a game started on Apple TV
can be continued on CabinetOS and vice versa. This bidirectional continuity is
the single most valuable feature of the product and should be treated as such
when trading off scope.

---

## The frontend

The frontend matches the existing Cabinet apps in visual language, navigation
feel and motion, so that the Apple TV app and CabinetOS read as one product
rather than two things that share a name.

Screens:

- **Home** — resume-first. What you were playing, continue it.
- **Browse** — by platform and by collection.
- **Game detail** — artwork and metadata.
- **In-game overlay** — reachable from a controller button without leaving the
  game. Offers save states, resume, and exit.
- **Settings** — account, storage, controllers, display, system update, and
  **About**, which carries the version and the credits (see *Branding*).
- **First run setup** — pairs with a RomM server. On-screen keyboard is the
  baseline; a physical keyboard types into the same field if one is attached.

The frontend is **owned, not skinned**. CabinetOS does not ship someone else's
frontend with a theme applied, because that makes the product's identity
hostage to an upstream project's design decisions.

---

## Reference implementations and upstreams

Future sessions should look here before inventing anything.

| What | Where | Why it matters |
|---|---|---|
| **Cabinet** (iOS/tvOS, Swift) | https://github.com/MMagTech/cabinet | **The reference implementation.** Source of truth for the frontend design language, for correct RomM client behaviour, and for how cores are hosted in-process. When in doubt about how a screen should look or how an API call should be made, read Cabinet. |
| Cabinet — `RommApp/RommApp/Native/` | same repo | The core inventory and the libretro frontend. Read `Libretro/LibretroFrontend.mm` before writing ours. |
| Cabinet — `RommApp/RommAppMac/` | same repo | The macOS app: the persistent-local-library half of the model CabinetOS is a hybrid of, and the only platform with Dolphin and PCSX2. |
| Cabinet — `tools/build-core.sh` et al | same repo | Per-platform core builds. CabinetOS adds a Linux target — open question 13. |
| Cabinet — `docs/native-in-game-saves.md` | same repo | How saves and save states work today. Phase 4 must match it. |
| Cabinet — `docs/core-quality-pass-2026-08-17.md` | same repo | Which cores are good and why they were chosen. |
| Cabinet — `CLAUDE.md`, `ROADMAP.md`, `docs/settled.md` | same repo | Conventions and decisions already made. Read before proposing anything. |
| **RomM** | https://github.com/rommapp/romm | The server. |
| RomM docs — Client API Tokens | https://docs.romm.app/latest/developers/client-api-tokens/ | Device pairing flow for keyboard-less auth. Phase 4. |
| RomM docs — Device Sync Protocol | https://docs.romm.app/latest/developers/device-sync-protocol/ | Wire format for syncing saves, states and play sessions. Phase 4. |
| RomM docs — API Reference | https://docs.romm.app/latest/developers/api-reference/ | REST endpoints. |
| RomM docs — WebSockets | https://docs.romm.app/latest/developers/websockets/ | Live update channels. |
| RomM docs — OpenAPI | https://docs.romm.app/latest/developers/openapi/ | Client codegen. |
| **Grout** (RomM first-party) | RomM ecosystem docs | RomM's own Linux handheld companion for muOS/NextUI. Does bidirectional ROM/save/state sync over Wi-Fi. Read it before writing our own sync layer — it may already define the behaviour we want. |
| **Bazzite** | https://github.com/ublue-os/bazzite | Our base image. |
| **ublue image-template** | https://github.com/ublue-os/image-template | Structure and conventions this repo follows. Our `Justfile`, `.github/workflows/` and `disk_config/` are derived from it (Apache-2.0). |

---

## The update model

**One version number for the whole system.**

The UI checks for a new release, shows a console-style update screen, pulls the
new image, and reboots into it. The user never updates a core, a package, a
dependency or a frontend separately.

There is no state in which the frontend and the system are on different
versions. This is the reason for building on a bootc/OSTree base: the entire OS
is a single signed, versioned artifact, and a bad update is a rollback rather
than a recovery USB stick.

Corollary: Bazzite's own automatic updater (`uupd`) is disabled, because it
would create exactly the partial-version state this model exists to prevent.

---

## Emulation

Native emulators running locally. Not streaming, not browser-based.

- Controller mapping is configured per system, by us, not by the user.
- Games launch from the frontend and return to it cleanly, with no visible
  transition to a desktop, a console, or another application's UI.
- Target systems are everything currently emulated on the existing setup,
  including the heavy ones: **PS2, GameCube, Dreamcast and Naomi.**

The reference machine's Vega integrated graphics is the performance floor that
matters here. Phase 8 exists because those four systems will need tuning rather
than defaults on hardware of that class.

### How Cabinet does it today

**Cabinet on tvOS runs native cores in-process.** You select a game, Cabinet
pulls the ROM from RomM, and injects it into an emulator core running inside the
app. There is no handoff to a second application and no separate launcher step.

**This is the single most important fact about the frontend**, and it has a
consequence that the Phase 0 and Phase 3 toolkit decision must be made in full
knowledge of:

> The CabinetOS frontend is not a launcher. It is an emulator host with a UI on
> top — a program that owns the frame loop, loads cores as libraries, feeds them
> ROM data and controller input, and presents their output.

That is a substantially larger and more constrained program than a menu that
shells out to other binaries. It rules out toolkits that cannot cheaply embed a
C library and put its frames on screen at a stable 60fps. It is also what makes
the in-game overlay tractable: when you own the frame loop, drawing over it is
natural. See open question 12, which is about what happens for the systems where
in-process is not viable.

### Core parity is a hard constraint

**CabinetOS must use the same cores as Cabinet, at the same versions.**

Save states are core-specific. A state written by one core is not readable by a
different core for the same system, and frequently not by a different *version*
of the same core — libretro cores break their own state format between releases
routinely. Since the entire point of the RomM sync layer is that a game started
on Apple TV continues on CabinetOS, a core mismatch silently destroys the
product's best feature. The game will boot; the save state will not load.

So core selection is not a CabinetOS decision. It is inherited.

What follows:

1. **Cores are bundled in the image and pinned**, never pulled from a package
   manager that can update them independently. This is consistent with the
   update model: one version number for the whole system, and cores are part of
   that version.
2. **Core versions move in lockstep across all Cabinet platforms.** Bumping a
   core is a coordinated release, not a CabinetOS-local change.
3. **There is no shared source of truth for core revisions yet, and there needs
   to be.** `tools/build-core.sh` clones each core's upstream repository with
   `git clone --depth 1` and builds whatever `HEAD` happened to be that day. No
   commit is recorded. The committed `.a` archives are the only artifact, and
   nothing says what source produced them.

   That makes core parity currently unachievable by construction: CabinetOS
   cannot build "the same revision" because the revision was never written down.
   It is also a latent problem inside Cabinet itself — rebuilding one core
   months after another means the two platforms carry different revisions of it.

   The fix is small and it belongs in Cabinet: a **core manifest** recording,
   per core, the upstream repository and an exact commit SHA. See open
   question 13.

   (`tools/generate_cores_map.py` is a different thing despite the name — it
   maps RomM platforms to *EmulatorJS* cores, generated from RomM's frontend
   source. Not a native core version manifest.)

### What Cabinet actually ships

From the repository tree (read the code in Phase 0 — this is the shape, not the
detail):

Cabinet is its own **libretro frontend** — `Native/Libretro/LibretroFrontend.mm`,
`LibretroCoreAPI.h`, `libretro.h`, plus Metal shaders. Cores are compiled to
static archives per platform and linked in: `libflycast_ios.a`,
`libflycast_tvos.a`, `libflycast_mac.a` and so on, built by `tools/build-core.sh`
and per-core scripts.

25 core directories, covering the light and mid-weight systems:

> BeetleNGP, BeetlePCEFast, BeetleVB, FBNeo, FCEUmm, Flycast, GW, Gambatte,
> GenesisPlusGX, MAME2003Plus, MGBA, MelonDS, Mupen64Plus, Opera, PCSXReARMed,
> PPSSPP, PicoDrive, ProSystem, Saturn (Beetle), Snes9x, Stella2014, VeMUlator,
> Vecx

And, **macOS only**, the two heavy ones — also as static archives, also
in-process:

> `RommAppMac/Dolphin/libdolphin_mac.a`, `RommAppMac/PCSX2/libpcsx2_mac.a`,
> built by `tools/build-dolphin-mac.sh` and `tools/build-pcsx2-mac.sh` with
> `tools/patch-dolphin-mac.py` and `tools/patch-pcsx2-mac.py`.

Dreamcast and Naomi are Flycast, which is present on every platform already.

### CabinetOS is a hybrid of the two Cabinet apps

| | Cabinet tvOS | Cabinet macOS | CabinetOS |
|---|---|---|---|
| Library | RomM, pulled on demand | local | RomM, pulled on demand |
| ROM storage | transient | persistent local | **both** |

CabinetOS takes the tvOS model — RomM as the source of truth, games pulled when
you want them — and adds the macOS model's persistence: a game can be **kept**
on the internal drive instead of re-fetched every time.

**Where** games live is the user's choice, not a fixed path. A console with a
small system drive and a big second drive is the normal shape, and a USB drive
should work too. Settings offers a storage location; the rest of the system
follows it.

That has a consequence for anyone building Phase 4: **do not hard-code the game
storage path.** It is configuration from the first line of code. Retrofitting
multiple locations into something that assumed one is expensive; designing for
it now costs nothing. See open question 14 for what still has to be decided.

Those are two different things and the UI must treat them as such:

- **Cached** — a side effect of playing something. Evictable without asking.
  The system may reclaim it when space runs low.
- **Kept** — a deliberate choice by the user. Never evicted automatically.
  Survives regardless of free space, and if space runs out the system says so
  rather than quietly deleting a game someone asked it to hold.

The Settings storage screen shows both, and lets a cached game be promoted to
kept and a kept game released back to cached.

---

## Developer mode

CabinetOS has no terminal, no file browser and no package manager. That makes it
a console, and it also makes it very hard to work on — and hard for anyone else
to contribute to.

**Developer mode is the single sanctioned exception.** It is hidden, off by
default, and explicitly opted into from Settings. When enabled it provides:

- **SSH** — shell access to the machine.
- **SFTP** — file transfer, so a new frontend build can be pushed to a running
  console without reflashing it. This is the thing that makes the Phase 3 to
  Phase 5 development loop bearable.

Rules it must follow:

1. **Hidden by default.** A normal user browsing Settings must not stumble into
   it. It is not a visible toggle with a scary label; it is somewhere you have
   to know to look.
2. **Off until deliberately enabled**, and the state survives reboots and system
   updates.
3. **Visibly on when it is on.** If SSH is listening, the UI says so somewhere
   the user will see it. A console that is quietly accepting remote logins is
   not acceptable, even on a home LAN.
4. **Enabling and disabling it is controller-driven**, like everything else. The
   person turning it on does not yet have a shell.
5. **It does not change the rest of the product.** No terminal appears in the
   UI, no desktop becomes reachable, nothing about the console experience
   changes. It opens a door from outside; it does not put one inside.
6. **Turning it off actually stops the service**, rather than only hiding the
   toggle.

The shipping, user-facing version of this lands in **Phase 6**. A cruder
build-time escape hatch will be wanted earlier — see open question 8.

---

## Staying current with Bazzite

The base is pinned by digest, so CabinetOS never changes underneath itself. The
cost of that is somebody has to move the pin, and a pin nobody moves is how a
project ends up two years behind its base with an unreviewable upgrade ahead of
it.

`.github/workflows/base-update.yml` runs weekly and opens a pull request when the
pinned digest has moved.

**Why not Renovate or Dependabot.** Bazzite rebuilds daily. A dependency bot
would open a pull request every day that said "digest changed" and nothing more.
A pull request that arrives every day and carries no information is worse than
no automation at all, because it trains you to merge without reading.

So `ci/check-base-update.sh` does the part a bot cannot:

1. **Diffs the package manifests.** `base-manifest.txt` is the base's package
   list as of the last bump, committed alongside the pin. The PR shows what
   actually changed — added, removed, and version bumps — instead of a digest.
2. **Classifies it.** Changes are matched against `ci/base-watch.txt`, the list
   of packages CabinetOS actually depends on: kernel, Mesa, gamescope,
   controller kmods, bluez, PipeWire, tuned, NetworkManager, sshd. The PR is
   labelled **RELEVANT** (read it) or **ROUTINE** (a green build is probably
   enough). Most weeks are routine.
3. **Flags strip-list drift.** A package the strip scripts remove which existed
   in the old base and is gone from the new one. `remove_pkgs` skips missing
   packages by design, so nothing breaks — but if upstream *renamed* it rather
   than dropping it, the real package is still in the image and the removal has
   silently become a no-op. That is how a desktop application comes back.

**`ci/base-watch.txt` must grow with the project.** Phase 2 adds whatever the
session depends on; Phase 5 adds the emulators. A package CabinetOS relies on
that is not on that list can break in an update that looked routine.

**Nothing is merged automatically, ever.** An image that builds is not an image
that boots. Build a qcow2 from the branch and boot it first — constraint 2 above
is the rule this automation serves, not one it replaces.

**Known limitation:** pull requests created with the default `GITHUB_TOKEN` do
not trigger other workflows, so the image build will not run on them by itself.
Either supply a `BASE_UPDATE_TOKEN` secret (a fine-grained PAT with contents and
pull-request write), which the workflow prefers when present, or close and
reopen the PR to kick CI off by hand.

---

## Measured behaviour

Numbers from the first booted image, a 1GB Unraid VM, 2026-09-13. Replace these
when they are measured again on real hardware — a VM has no GPU, so nothing here
says anything about graphics or emulation.

### Resource use is about services, not packages

**Installed packages that never run cost nothing.** Plasma is still installed and
contributes zero: the default target is `multi-user.target`, both display
managers are masked, and there are no Plasma processes. Removing another
thousand packages would shrink the image and free no memory at all.

What costs memory is **services that run**. At idle: **629 MB used, 32 running
services, 18.7s boot** (14s of it userspace).

Stopping seven services that a console has no use for recovered **91 MB, 14% of
idle memory**, and took the service count to 24:

| Service | Why it has no place here |
|---|---|
| `input-remapper` | 84 MB across two processes, the single largest consumer. CabinetOS owns controller mapping itself (Phase 5). |
| `cardwired` | 38 MB, and **6.8s of the 14s boot** — the slowest unit on the system. |
| `ModemManager` | Cellular modems. |
| `displaylink` | USB display adapters. |
| `gssproxy` | Kerberos/NFS credentials. |
| `systemd-homed` | Portable home directories. |
| `upower` | Battery monitoring on a mains-powered console. |

Deliberately left alone, and why: `tuned` (41 MB) is the power and thermal
management the project depends on; `firewalld` (50 MB) is a security posture
decision, not a performance one; `uresourced` and `dmemcg-booster` are Bazzite's
game process-priority layer; `ds-inhibit` stops controllers being treated as
keyboards for idle purposes.

**So the Phase 8 performance lever is the service list, not the package list.**
Worth ~90 MB and ~7s of boot before touching anything contentious. Emulation
performance itself will be bound by GPU throughput and single-thread CPU speed,
neither of which any of this affects.

### Session infrastructure present

`gamescope` is in the image and runs. `gamescope-session-plus` is not — see open
question 3. The only Wayland session defined is `plasma.desktop`, which Phase 2
removes.

### A GPU-less VM *can* show the frontend, via cage

Established 2026-09-13, correcting an earlier claim in this document that
nothing visual could be developed without real hardware.

**gamescope requires a Vulkan device it accepts**, and a VM's virtual GPU does
not provide one. The image ships the Venus driver (`virtio_icd`), but the host
must expose Vulkan over VirtIO-GPU for it to do anything, and Unraid's QEMU does
not — the guest reports `+virgl` but Vulkan enumeration finds no devices at all.
So gamescope falls to Mesa's software renderer and rejects it outright:
`vulkan: selecting physical device 'llvmpipe' ... not a valid physical device`.

**But `cage` is already in the image and renders in software.** Confirmed
running under the real session service, with a test application visible on the
VM's console. cage is a minimal kiosk compositor — one fullscreen app, the same
basic job as gamescope, without the display features.

The session script therefore works down a ladder: gamescope on drm, then cage,
then gamescope headless. **The frontend does not care which is hosting it** — it
is a Wayland client either way.

What this changes: **Phase 3 can be built and looked at in a VM.** Only
performance, display features and final integration need the SER5.

What it does not change: cage has no VRR, no HDR and no scaling, so it is a
development convenience, never the production path. Falling back to it on real
hardware means something is wrong with the GPU, and the About screen should name
the running compositor so that state is visible rather than mysterious.

### First boot shows Linux

`bazzite-hardware-setup.service` runs visibly on first boot and takes long
enough to notice. `plasma-setup.service` — Plasma's out-of-box wizard — is
present and inactive only because nothing starts a graphical session.

Both are "the user sees Linux" moments, which the product rules out. Phase 2
should remove `plasma-setup` outright and either own the hardware-setup step or
hide it behind a splash.

`plasma-setup.service` runs a `bootutil` binary that decides whether to show the
wizard based on a `plasma-setup-done` flag file. Dropping the flag file in place
would suppress it, but removing the unit is cleaner — CabinetOS owns first run.

### Other facts worth keeping

- The installed system is **7.9 GB**. `/` is a 43 MB read-only composefs; all
  real storage is `/var`.
- `sshd.service` is enabled, `sshd.socket` disabled — the classic always-listening
  form, not socket activation.
- The hostname is `bazzite`. Branding, Phase 8.
- `systemd-udev-settle.service` costs 4.3s at boot and is deprecated upstream.
  Worth investigating what still pulls it in.

---

## Branding and the boot experience

### The palette

Taken from Cabinet's own icon generator, `tools/make_icon.swift`. These are the
product's colours, not an approximation, and the frontend (Phase 3) should be
built from the same set.

| Role | Value |
|---|---|
| Backdrop | `#3A2268` → `#120C26` → `#090614`, vertical |
| Cabinet body | `#EEEAE2` |
| Marquee | `#FF7AC7` → `#FFC457`, horizontal |
| Screen | `#58E8F6` → `#2484D6`, vertical, with a white sheen at 26% fading out |
| Control panel / base | `#CEC7BC` |
| Joystick | `#3A3444` |
| Buttons | `#EC405C`, `#FFC457` |

### The boot splash

**The icon is an arcade cabinet, so the splash is an arcade cabinet powering
on.** It appears the moment the firmware hands over, dark; the marquee lights,
then the screen glows; it holds until the frontend has drawn its first frame.

It covers the three things that otherwise show Linux to the user: the scrolling
kernel text, Bazzite's first-boot hardware setup job, and the gap before the
frontend is ready.

The handoff from splash to frontend must have no black flash in it. That is the
fiddly part, and it is what separates a console from a Linux box with a nice
wallpaper.

**Text on the splash: the wordmark "CabinetOS" and nothing else.** A boot screen
names the machine; it does not explain it.

**Development builds may show a version string**, small, in a corner — genuinely
useful when a VM and a mini PC are both running different builds. Off for
release builds.

### Attribution belongs in Settings → About, not on the boot screen

Considered and rejected: putting "a fork of Bazzite" on the splash. It breaks
the product's own rule — that line tells the user they are looking at a Linux
distribution, which is exactly what the rest of the design works to avoid. No
console does it: a PS5 does not say "built on FreeBSD", an Apple TV does not say
"based on Darwin". And the audience is wrong, because the people who care are
reading this repository, not squinting at a television.

Credit instead goes where Sony and Apple put it: **Settings → About**, with full
acknowledgement of Bazzite, Universal Blue, ChimeraOS, and the emulator projects
whose work this is built on. Also in the README, where it already is.

This is not a licensing question — Bazzite's licence is satisfied by crediting in
the documentation. It is a question of what the product should feel like.

### What the boot chain actually looks like

| Stage | Ours? |
|---|---|
| Firmware logo | No. Vendor's, in the motherboard's own chip. Usually *disableable* in firmware settings, which is worth doing — black is cleaner than someone else's logo. |
| Boot menu | Ours. Hidden. |
| Kernel text | Ours. Hidden — needs `quiet` and `loglevel=0`, neither of which is set today. |
| Splash | **Ours.** |
| Frontend | Ours. |

Any PC-based console has the firmware seam; SteamOS and Batocera included. Real
consoles avoid it only by making the firmware too.

---

## Non goals

- Not a Steam machine.
- Not a general purpose desktop.
- No app store.
- No browser.
- Nothing that makes it a computer instead of a console.

The one sanctioned exception is **developer mode**, above: hidden, opt-in, and
invisible to anyone who has not deliberately turned it on.

---

## Constraints and principles

1. **Stay at the application layer.** No custom kernel modules, no hardware
   hacks. Those are what make upstream changes dangerous, and the whole point of
   basing on Bazzite is to let someone else own the kernel and driver problem.
2. **The Bazzite base is pinned to a specific tag and digest**, and only moved
   deliberately, as its own commit, with a VM boot test. Automation proposes
   base bumps and explains what changed; it never merges one. See *Staying
   current with Bazzite*.
3. **Anything that could leave the user stuck at a terminal is a bug.**
4. **The frontend is owned, not a skin** on someone else's frontend.
5. **Removals are conservative.** When it is not clear that a Bazzite package is
   safe to remove, it stays in the image and goes in Open questions. A slightly
   larger image costs nothing. An image that does not boot, or that boots
   without controller support, costs a reflash — and eventually costs the
   ability to trust the base at all.
6. **Builds happen in CI, on Linux.** The project is developed on a Mac, which
   cannot build or run bootc images. Nothing in this repo may depend on being
   able to build locally.

---

## Phase plan

### Phase 0 — Design system spec
**Status: not started.**

Extract from Cabinet: colours, typography, spacing, corner radii, focus and
selection behaviour, motion curves and durations, navigation model, and a
component inventory. Write it as a toolkit-agnostic document.

While reading Cabinet for this, also record **how it hosts cores** — how a ROM
gets from RomM into a running core, where save states live, and how the overlay
is drawn over a running game. Phase 3 needs that as much as it needs the colour
palette, because the frontend is an emulator host rather than a launcher (see
*Emulation*), and that is the constraint that decides the toolkit.

Scope open question 13 at the same time: read `tools/build-core.sh`, the
per-core build scripts, `tools/generate_cores_map.py`, and the two Mac patch
scripts, and work out what a Linux target costs. It is the long pole in Phase 5
and it is knowable now.

*Done when* a developer who has never read a line of Swift could reproduce the
look and feel of Cabinet from the document alone.

### Phase 1 — Base image
**Status: in progress.**

A `Containerfile` from a pinned Bazzite tag, with the desktop and Steam removed
and the gaming stack retained. GitHub Actions builds, signs and publishes to
GHCR, and produces a bootable disk image.

*Done when* CI is green and the image boots in a VM to a console prompt.

*Shipped so far:* repository scaffold, `Containerfile` pinned to
`ghcr.io/ublue-os/bazzite:stable-44.20260908`
(`sha256:437920bae6935fd70719c1e0109f3469b1215a788330b0de924d0c7ac8aaa84c`),
strip scripts, SSH enabled for development, build/sign/push workflow, disk image
workflow, and the Bazzite base-update watcher.

*First green build: 2026-09-13.* `ghcr.io/mmagtech/cabinetos:latest`, public and
pullable, unsigned (no `SIGNING_SECRET` set yet).

What the build proved:

- Controller support survives the Steam removal — open question 2, now resolved.
- `gamescope` is present, at `/usr/sbin/gamescope` rather than `/usr/bin`.
- The default target is `multi-user.target`, so the image boots to a console.
- `openssh-server` was already in the base; `sshd.service` is enabled.

What it also showed, which is worth knowing before anyone reads too much into
the strip scripts: **the base went from 2745 packages to 2705.** Forty packages.
Most of the desktop application list was never installed on this base at all —
no Firefox, no Discover, no Okular, no GNOME anything — and Plasma itself is
untouched by design. The desktop is *unreachable*, not absent. See open
question 1.

The display manager here is `plasma-login-manager`, not `sddm`. Listing both was
the right call.

*Not yet verified:* the image has never been booted. Everything above is from
the build log.

*Remaining before Phase 1 is done:* build a disk image and boot it.

### Phase 2 — Boot to frontend
**Status: not started.**

Autologin, no display manager, a custom session launching a fullscreen
placeholder application. Every route to a desktop, file manager or terminal
closed. Shutdown and suspend reachable from a controller.

A keyboard and mouse attached to the session must work — they simply must not be
needed. Closing "every route to a terminal" means the UI offers none, not that
input devices are blocked.

*Done when* power on leads to the placeholder with no keyboard involved.

### Phase 3 — Frontend shell
**Status: not started.**

The real frontend, built against the Phase 0 spec, running on fake data. Home,
browse, game detail, settings, in-game overlay. Full controller navigation, plus
the on-screen keyboard, which everything else that needs text entry depends on.

**Nearly all of this can be built in a VM**, via cage — see *Measured
behaviour*. Layout, colour, typography, artwork grids, navigation, focus, every
settings screen, first-run setup. Controllers too, passed through over USB.

**Except motion.** Software rendering is choppy, so an animation that feels
wrong in the VM may be fine on hardware — and worse, an animation *tuned* in the
VM is tuned against the wrong feedback. Cabinet's design language is
substantially about how things move. Build the motion, do not judge it there.

*Done when* it looks and feels like Cabinet, and every screen can be reached and
left with a controller alone. The "feels" half is answerable only on the SER5.

### Phase 4 — RomM integration
**Status: not started.**

Device pairing and auth, library sync, artwork and metadata, on-demand
downloads with a queue, firmware and BIOS retrieval, save and save state sync.

Storage management distinguishes **cached** from **kept** (see *Emulation*):
cached games are evictable, kept games are not, and the user moves games between
the two.

*Done when* the real library is browsable, a game downloads and plays, and a
kept game survives a cache eviction.

### Phase 5 — Emulators and launching
**Status: not started.**

Cores and emulators bundled into the image. Games launch and return cleanly with
no visible desktop. Controller mapping per system, per-system configuration, save
states wired to the sync layer.

Every system runs in-process, matching Cabinet — including PS2 and GameCube,
which Cabinet embeds as real PCSX2 and Dolphin rather than as libretro cores
(open question 12). One overlay implementation, one save state path.

The work here is open question 13 — building the same cores at the same
revisions for Linux x86-64 — not choosing an architecture.

*Done when* several systems are playable end to end, and a save state written on
Apple TV loads on CabinetOS.

### Phase 6 — Real hardware
**Status: not started.**

Install on real hardware — the SER5 is the reference machine. Performance
tuning, Bluetooth controller pairing, audio output, display and resolution
handling.

Ship **developer mode** as specified above: hidden, opt-in from Settings,
enabling SSH and SFTP, visibly indicated while active, and genuinely stopped
when switched off. This is what lets builds be pushed to a running console
without reflashing it, and what makes the project contributable by anyone other
than its author.

Also verify the input model on real hardware: a Bluetooth controller must pair
and wake the machine, and a USB keyboard must work if plugged in without being
required for anything.

*Done when* the machine is usable from the sofa with a controller alone, and
reachable over SSH when developer mode is on.

### Phase 7 — Unified updates
**Status: not started.**

A release manifest carrying one version number for the whole system. The UI
checks, shows a console-style update screen, pulls, and reboots.

**Also in this phase: HDMI-CEC mode selection in Settings.** CabinetOS removes
the terminal, and with it `ujust` and the Bazzite Portal — both of Bazzite's
mechanisms for choosing between legacy and native CEC. Without a CabinetOS
setting, the choice is unreachable on a finished machine, and CEC is a hard
requirement (see *Hardware*). The reference implementation is
`/usr/share/ublue-os/just/81-bazzite-fixes.just`, which survives in the image.

*Done when* a full system update happens without a keyboard, and the CEC mode
can be changed with a controller.

### Phase 8 — Heavy systems and polish
**Status: not started.**

PS2, GameCube, Dreamcast and Naomi tuned. Boot splash, branding, settings
depth, first run setup.

*Done when* it is something you would hand to someone else without explaining
anything.

---

## Open questions

Nothing here should be resolved by guessing. Each of these needs either a test
or a decision made deliberately.

### 1. How much of KDE Plasma can actually be removed?
**Raised: Phase 1. Unresolved.**

Phase 1 removes the display manager and the desktop *applications*, and
switches the default systemd target to `multi-user.target`, but does **not**
uninstall Plasma itself. The desktop becomes unreachable rather than absent.

Reasons for the caution:

- Bazzite's own `Containerfile` applies `dnf versionlock` to `plasma-*` and
  `qt6-*` on the Kinoite base, which implies those packages are load-bearing for
  the image build.
- Bazzite installs `plasma-foreground-booster-dmemcg`, which is part of its
  process-priority handling for games and has a Plasma dependency.
- `xdg-desktop-portal-kde` is the portal implementation on this base. Removing
  it without a replacement may break file dialogs and permissions for anything
  Flatpak-shaped later.
- A cascading `dnf5 remove` of Plasma on an OSTree-derived image risks an image
  that builds but does not boot — and we currently have no way to notice that
  before the first VM test.

**What the first build showed:** stripping removed 40 packages out of 2745. The
desktop applications this base was assumed to carry were mostly not there in the
first place. So the size argument for removing Plasma is weaker than it looked —
the remaining weight is Plasma and Qt themselves, and taking those out is the
risky part, not the part with obvious payoff.

Which reframes the question. It is not "how do we make the image smaller"; it is
"does leaving Plasma installed cost us anything real?" Candidate answers: attack
surface, confusion for contributors, and the chance that something in it starts
on boot and fights the CabinetOS session. The third is the only one that would
actually break the product, and Phase 2 will find out.

Resolve in Phase 2, once there is a booted image to validate against and once
the frontend toolkit is chosen (Phase 3) so we know what Qt and Wayland
libraries are actually needed. `build_files/strip-desktop.sh` contains a
commented-out candidate list to start from.

### 2. Does removing `steam` also remove the controller udev rules?
**Raised: Phase 1. RESOLVED by the first build — no, controller support is intact.**

`dnf5 remove --no-autoremove` did what it was meant to. The first green build
(2026-09-13) removed `steam` itself and left everything around it:

- `kmod-xone` — Xbox wireless — present.
- `kmod-gcadapter_oc` — GameCube adapter — present.
- `kmod-new-lg4ff`, `kmod-hid-tmff2`, `kmod-hid-fanatecff`, `kmod-t150-driver` —
  force-feedback wheels — present.
- `50-steam-horipad-controller.rules` and the rest of the udev input rules —
  present.

Still to confirm on real hardware: that a controller actually enumerates and is
usable. The packages being there is necessary, not sufficient.

### 3. `bazzite` or `bazzite-deck` as the base?
**Raised: Phase 1. RESOLVED: stay on plain `bazzite` and write our own session.**

Checked on the booted image. Plain `bazzite` has **gamescope** —
`/usr/bin/gamescope`, plus `gamescopectl`, `gamescopereaper` and
`gamescopestream` — which is the part that matters and the part that would have
taken months to build.

What it does **not** have is `gamescope-session-plus`: no
`/usr/share/gamescope-session-plus/`, no `gamescope-session*` binary, and the
only Wayland session offered is `plasma.desktop`. That harness is
`bazzite-deck`-only.

So the choice is real, and the answer is to write our own. `gamescope-session-plus`
is a shell harness whose substance is Steam bootstrapping, `steamos-manager`
integration and Steam Deck hardware handling — all of which CabinetOS would be
unpicking rather than using. Switching to `bazzite-deck` to get it would drag in
Steam, `inputplumber` and the SteamOS management layer, and constraint 4 says
the frontend is owned rather than skinned.

What CabinetOS actually needs is small: autologin, then gamescope, then the
frontend inside it. The pieces of `bazzite-deck` worth borrowing are its power
button handling and possibly `inputplumber`, and those can be taken
individually if Phase 6 wants them.

**gamescope runs in the Unraid VM.** `gamescope --backend headless` starts,
runs its child and exits cleanly, so the session plumbing is developable without
the SER5. Note the VM's virtual GPU is QXL — fine for the headless backend;
switching the VM to VirtIO-GPU would be worth doing before testing the DRM
backend.

### 4. Bundle libretro cores directly, or build on RetroDECK?
**Raised in the brief. Still open, but narrowed.**

What we now know (see *Emulation*): Cabinet already runs native cores
**in-process**. That is the libretro shape, and it points strongly at CabinetOS
bundling cores directly rather than building on RetroDECK, which is a curated
set of *standalone* emulators behind ES-DE — someone else's frontend, which
constraint 4 rules out anyway.

What is not settled is whether in-process works for everything. See open
question 12, which is now the question that actually matters here.

Do not resolve before Phase 5.

### 5. Anaconda ISO vs. a plain disk image for installing to real hardware
**Raised: Phase 1. Both are built; neither is tested.**

CI produces both a `qcow2` (for the Phase 1 VM boot test) and an `anaconda-iso`
(for installing to real hardware). The ISO runs a graphical installer, which is
a keyboard-and-mouse experience and therefore contradicts the product's
principles — but it only happens once, at install time, on a machine that has
not been set up yet.

If a keyboard-free install becomes a requirement, the alternative is a `raw`
image written directly to the target machine's drive from another computer.
Decide in Phase 6.

### 6. `/opt` mutability
**Raised: Phase 1. Left at Bazzite's default.**

The ublue template offers making `/opt` immutable so packages can write there.
Some emulators may want `/opt`. Left alone for now; revisit in Phase 5 when we
know how emulators are packaged.

### 7. OS branding in `/usr/lib/os-release`
**Raised: Phase 1. Deferred to Phase 8.**

Changing `NAME` and `PRETTY_NAME` is cosmetic and safe. Changing `ID` is not —
`dnf` and the repo definitions resolve `$releasever` and repo paths from it.
Phase 8 owns branding; when it happens, change the cosmetic fields only.

### 8. How do we get a shell before Phase 6 ships developer mode?
**Raised: Phase 1. RESOLVED: SSH is on from the start.**

**Decision:** `openssh-server` is installed and `sshd` is enabled from Phase 1
onward. Developer mode (Phase 6) is about *closing* SSH by default and gating it
behind the toggle — not about opening it.

Reasoning: Phases 2 through 5 consist of booting images and finding out why they
did not behave as expected. Doing that without a shell is not a hardship, it is
a different and much worse project. Contribution has the same requirement.

**This creates a debt that Phase 6 must pay.** An image that boots with SSH
listening is correct for a development tool and wrong for a console handed to
someone else. The tracked obligations are:

1. Phase 6 must flip the default to off and put SSH behind developer mode.
2. Until then, CabinetOS is a development artifact. It should not be installed
   on a machine exposed to an untrusted network, and the README says so.
3. Password authentication is the interim mechanism because it is the only one
   that works before there is a UI to enrol a key. It is not the shipping
   answer — see open question 9.

`build_files/enable-ssh.sh` carries the same warning next to the code.

### 9. How is developer mode revealed, and how does it authenticate?
**Raised: Phase 1. Unresolved. Phase 6 owns it.**

Two separate questions:

**Discovery.** How does a developer turn it on without a normal user finding it?
Prior art ranges from a version-number press count (Android), to a hidden entry
in an About screen, to a controller input sequence. It needs to be discoverable
from documentation and not by accident.

**Authentication.** Password auth on a home LAN console is weak, and there is no
good way to type a strong password with a controller. Likely answer is public
key only, with the key supplied through the UI or fetched from a GitHub username
— but that needs deciding rather than assuming. Also unresolved: whether SSH
binds to all interfaces or only the LAN, and whether the machine advertises
itself over mDNS so a developer can find it without knowing its IP.

### 10. Waking the machine, and turning the TV on
**Raised: Phase 1. Largely DECIDED — HDMI-CEC is a requirement. Phase 6 tunes it.**

Superseded by the hardware requirement above. CEC is no longer "use it if the
machine happens to have it"; the console turns the television on and is woken by
it, and a USB adapter is part of the bill of materials because essentially no
x86 mini PC wires the CEC pin.

What remains for Phase 6, with real hardware and a Pulse-Eight adapter present:

- **Legacy or native mode in practice.** Legacy is the expected answer — it is
  the adapter path, and `cecd` is known to interfere with wakeup on exactly this
  kind of dongle setup. Confirm rather than assume.
- **Suspend versus display-blank.** `cecd`'s config carries `suspend_tv` and
  `allow_standby`; the legacy path uses the `cec-onsleep` and `cec-onpoweroff`
  services. Which combination gives a clean "press the button, everything wakes"
  is an empirical question.
- **The controller path is still separate.** CEC handles the television.
  Bluetooth wake-from-suspend for a controller remains unreliable in general, so
  a machine that stays awake and blanks its display is still the likely default.

### 11. NVIDIA hardware
**Raised: Phase 1. Out of scope until there is hardware that needs it.**

Bazzite publishes `bazzite-nvidia` alongside `bazzite`, so supporting an NVIDIA
machine is a base-image change rather than a rewrite. The cost is real though: a
second image to build, sign, test and boot on every change, and NVIDIA driver
breakage is the single largest maintenance tax on custom Bazzite images.

Not doing it now. If the hardware changes to something NVIDIA-based, the work is
to parameterise the base image in the `Containerfile` and matrix the build
workflow over both variants — not to restructure anything.

### 12. In-process cores for the heavy systems, or separate processes?
**Raised: Phase 1. RESOLVED by reading the Cabinet repository: in-process, all of them.**

I had assumed the heavy systems would need standalone PCSX2 and Dolphin in their
own processes, because their libretro cores lag the standalone emulators badly.

That was wrong, and the answer Cabinet already uses is better than either option
I had considered. **Cabinet embeds the real PCSX2 and Dolphin as static
archives** — `libpcsx2_mac.a`, `libdolphin_mac.a`, built from source with
`tools/build-pcsx2-mac.sh` and `tools/build-dolphin-mac.sh` plus patch scripts —
rather than using their libretro cores. Full emulator quality, still in-process.

**Consequences, all good:**

- **One overlay implementation, not two.** The frontend owns the frame loop for
  every system. No gamescope compositing tricks, no per-emulator overlay.
- **One save state path.** The frontend owns state for every system, and hands
  it to the RomM sync layer the same way each time.
- **No process launching at all**, which removes an entire category of "returned
  to a desktop for half a second" bugs from Phase 5.

**And the work is easier here than it was on macOS.** Dolphin and PCSX2 both
target Linux x86-64 as a first-class platform. The Mac build needed patch
scripts to get there; the Linux build should need fewer, or none.

The remaining work is open question 13 — producing Linux builds of the same
cores — not an architectural choice.

### 13. Building the same cores for Linux x86-64
**Raised: Phase 1. Repo layout DECIDED. The rest is Phase 5, scoped in Phase 0.**

**Decision: CabinetOS stays a separate repository from Cabinet.**

Reasons:

- Nothing is shared at the CI level. Cabinet builds with Xcode on macOS;
  CabinetOS builds a container image on Linux. A monorepo would mean path
  filters on every workflow to stop each toolchain running on the other's
  commits.
- Cabinet is roughly 270 MB because it commits built `.a` archives. Anyone
  cloning CabinetOS to work on the OS would pull a quarter of a gigabyte of
  Apple binaries they cannot use.
- Different release cadences — App Store submissions against OS images — and
  different contributors. Someone who wants to help with the console is not
  necessarily an iOS developer.
- Blast radius. A mistake in OS work should not be able to break the CI of the
  app that ships today.

A third repository holding just the cores was considered and rejected as
premature: it is real work, it disrupts a shipping app, and it leaves three
things to keep in step instead of two. Revisit only if the coupling actually
starts to hurt.

**The repo layout is not what guarantees core parity — a manifest is.** With one,
parity is enforced by data and the directory structure stops mattering. Without
one, a monorepo would not save it either.

#### The core manifest

Needed in **Cabinet**, because Cabinet is the app that ships and the source of
the constraint. Per core: upstream repository, exact commit SHA, which systems it
serves, and which platforms it is built for. `build-core.sh` should check out
that SHA instead of cloning `HEAD`, and record it on any bump.

CabinetOS then reads the manifest, builds the same revisions for Linux x86-64,
and asserts at build time that what it produced matches. A mismatch becomes a
failed build rather than a save state that silently will not load.

#### Still open

- **Shared objects, not static archives — probably.** Cabinet merges each core
  into a single relocatable object exporting only `<prefix>_retro_*` forwarders,
  so many cores can link into one binary without their `retro_*` symbols
  colliding. That is a neat solution to an Apple-platform constraint. On Linux
  the constraint does not exist: one `.so` per core, `dlopen`ed with
  `RTLD_LOCAL`, gets namespace isolation for free — so the symbol-prefixing work
  disappears rather than being ported. Separate `.so` files also rechunk into
  smaller image layers, which the update model cares about. Confirm in Phase 0.
- **How much of the Mac patch scripts carry over.** `patch-pcsx2-mac.py` and
  `patch-dolphin-mac.py` are presumably working around Apple-specific problems;
  both emulators target Linux x86-64 first-class, so the answer may be "none".
- **Which cores have a Linux build path at all.** `build-core.sh` selects a
  `MAKE_PLATFORM` per core and the script's own comments note that tvOS support
  was only verified for one core rather than assumed. Do the same checking for
  Linux rather than assuming every core's Makefile has a usable case.
- **x86-64 versus the ARM assumption.** Every existing Cabinet target is arm64.
  Some libretro cores carry hand-written ARM assembly paths with C fallbacks —
  `pcsx_rearmed` most obviously, given its name. Expect at least one core to
  need attention here.

### Prior art: how Cabinet and Grout already do this

Read 2026-09-13. **Cabinet has already solved most of the storage problem, and
CabinetOS should inherit its model rather than invent one.**

#### Cabinet — `docs/scope-native-offline.md`, `scope-download-all.md`

- **"Keep on device" already exists**, and the cached/kept distinction in this
  document matches Cabinet's exactly: a per-game toggle, permanent storage shown
  with its size, removable where it was added, and explicitly *not* a cache —
  "caches serve speed, kept games serve a promise". Keeping a game pulls its ROM
  **and its platform's firmware**. Keyed by rom id.
- **The kept-game manifest embeds the whole `Rom` object**, not a hand-picked
  subset, which is what makes offline navigation work: cover art, platform
  label and metadata are all present with no server. This matters enormously for
  portable drives — see below.
- **Saves are written locally first**, into a per-game `pending-states`
  directory, before any attempt to reach RomM. Conflicts are *designed out*
  rather than resolved: each queued file carries RomM's own timestamped name, so
  an upload lands exactly as if it had happened online and nothing overwrites
  anything. Sync is only "finish the uploads", and is safe to run often.
- **Save state caching is opportunistic, not queued.** A state is cached when a
  game is kept and refreshed on ordinary online visits. The reasoning, reached
  before building: Cabinet already fetches live whenever online, so the only gap
  is between the last check and losing signal — closed by refreshing on ordinary
  use rather than by a background job.
- **Offline is one signal, not two.** `NetworkMonitor` combines real
  disconnection with a deliberate Offline Mode toggle into a single `isOffline`
  that every screen asks, so both drive identical code paths.
- **States are never exposed to other apps**; ROMs are. A state blob is
  core-format-specific and useless elsewhere, a ROM is not.
- **Download All exists on Mac and iOS but deliberately not tvOS**, on the
  grounds that "a television keeps a handful of games and has the disk for
  that". **CabinetOS should reverse that call.** It is television-shaped but has
  a large dedicated drive and a user who explicitly chose where games live —
  which is the case Download All is for.

#### Grout — RomM's own Linux handheld client

- Pulls ROMs to the device in **the host frontend's expected folder layout**
  (muOS/NextUI), not a layout of its own. Pushes on session end, on idle, or on
  a schedule. Fully playable offline between syncs.
- Matches saves to games by **platform plus filename**, case-insensitively, with
  PSP's directory saves matched by Game ID instead. Not by hash, and not by
  RomM id.
- Conflicts are surfaced, not resolved automatically: a per-game screen
  defaulting to Skip, where the user picks Keep Local or Keep Remote.

#### The finding that matters most

**Grout syncs save files only. It explicitly refuses to sync save states**,
because states "require both sides to use the same emulator and sometimes even
the same version".

That is RomM's own first-party client independently confirming the core-parity
constraint in *Emulation* above — and it draws the opposite conclusion, because
it cannot control what emulator the handheld runs.

Cabinet **can** sync states, and does, precisely because it controls both ends
and ships identical cores. That is not a minor feature difference; it is the
thing Cabinet does that the rest of the ecosystem cannot.

**So core parity is not a nice-to-have that makes saves more convenient. It is
the entire reason CabinetOS can offer continuity at all.** Get it wrong and the
product degrades to what Grout already does for free.

### 14. User-selectable game storage
**Raised: Phase 1. Mostly DECIDED. Design in Phase 4, UI in Phase 6/8.**

The user picks where games are stored — internal drive, second SSD, or USB.

**Binds Phase 4 immediately:** the storage path is configuration from the first
line of code, never a constant, and the cached/kept distinction applies per
location rather than globally.

#### Decided

**One active location, with migration between them.** Upgrading to a larger
drive is a normal thing to want, so moving the library is a first-class
operation rather than something the user does by hand. It must survive being
interrupted — power cut, unplugged cable — and resume, without losing a kept
game or leaving two half-copies.

**A missing drive degrades; it never errors.** If the game drive is absent,
games simply come from RomM again. That falls straight out of RomM being the
source of truth: the local copy is a cache, and a cache that has gone away is
re-fetched, not mourned. The console starts normally and stays fully usable.

The UI must still *say* so — plainly, once, somewhere visible — because
silently re-downloading a library over Wi-Fi is its own kind of rude. Kept games
whose drive is missing should be listed as such, since the kept flag lives in
configuration rather than on the drive itself.

**Unresolved within this:** save states written since the last sync live only on
that drive. Losing the drive is harmless for ROMs and not harmless for those.
Phase 4 should sync saves aggressively enough that the window is small, and the
warning should be honest about it.

**Formatting: not decided, not ruled out.** There is console precedent — the
PS5 formats an internal M.2, the Xbox formats external drives — so it is not
inherently un-console-like. If it happens: never the default action, a
confirmation that cannot be fumbled through on a controller, and prefer adopting
a drive as-is wherever possible.

#### Portable drives, and the problem with them

**Decided: a drive should move between CabinetOS machines.** The obvious case is
two boxes in one house sharing a RomM server, and there the drive should simply
work.

**The tension, raised by Marcus:** CabinetOS is tied to a RomM login. Move the
drive to a machine paired with a *different* server and the ROM files are
present but the library describing them is not — names, artwork, metadata,
collections and save history all live server-side, and game identifiers are
specific to a server instance.

**Better answer, from Cabinet rather than invented:** I proposed matching by
file hash. Cabinet already does something more useful — **its kept-game manifest
embeds the whole `Rom` object**, which is what lets it navigate a library
offline with real cover art and platform labels and no server at all.

Carry that onto the drive and the drive becomes **self-describing**. Plugged
into a machine paired with a different server, the games are still browsable and
playable, because everything needed to present them travelled with them. No hash
lookup, no dependency on the new server having the same content.

Neither Cabinet nor Grout uses hashes, incidentally: Cabinet keys on RomM's rom
id, Grout matches on platform plus filename. A hash index may still be worth
adding for *adoption* — recognising that a file on the drive is the same game
the new server already knows about, so it links up rather than sitting as a
duplicate — but it is an optimisation on top of a self-describing drive, not the
mechanism itself.

That gives three sensible tiers instead of a binary:

| Situation | Result |
|---|---|
| Same RomM server | Everything works. The common case. |
| Different server, same games | Browsable and playable from the drive's own manifest; adoption links them to the new server's library. |
| Different server, unknown games | Still browsable and playable from the manifest. Saves have nowhere to go. |

It also argues for a **documented on-disk layout** rather than an
implementation-defined one, since the drive is now a thing other software has to
understand.

What it does **not** solve is saves: those are server-side, so a game continued
on a different server starts from that server's save history. That is correct
behaviour rather than a bug, but the UI should not pretend otherwise.

**Follow Cabinet's save model rather than designing one.** Saves write locally
first, into a pending queue, and upload afterwards — losing signal mid-save
never loses the save. Conflicts are designed out by giving each queued file
RomM's own timestamped name, so uploads never overwrite and syncing is only
"finish the uploads". Grout instead surfaces conflicts for the user to resolve,
which is the right call for a client that cannot write the filename — and the
wrong one here, since we can.
