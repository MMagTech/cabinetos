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

## Where the project is — 2026-09-13

**Phase 1 complete. Phase 2 mostly done. Phase 0 done — see *The design
system*, *How Cabinet hosts cores* and *The frontend toolkit*.**

The Phase 0 session read Cabinet's source rather than its documentation.
Its two findings that change the plan:

1. **A Linux x86-64 core build is small, not large.** Every one of the 23
   cores has a working Linux path, the host layer is 95% portable C++ with
   the GL path already written, and the Apple-only apparatus (symbol
   prefixing, static merges, JIT walls) **disappears** rather than being
   ported. Open question 13 is largely resolved.
2. **The risk is not "can it be built", it is "will it be the same".** The
   same core built for Linux defaults to a *different CPU backend* than the
   Apple build, and Cabinet's own build system could not be run by anyone but
   its author on the machine that last ran it.

**The recovery ran the same day, and found a live bug.** `core-manifest.json`
now exists in Cabinet. **Eleven of twenty-three cores are shipping different
revisions to iOS and macOS today**, and eleven of the twenty-one tvOS revisions
are gone for good — so for those cores, whether a save state crosses between
Cabinet's *own* apps is unknown and now unknowable. The manifest pins each core
forward onto one revision, which is the right fix and is far cheaper now, in
alpha, than once anyone has a save history. One reproducibility hole remains and
it is in Flycast. See open question 13.

Running infrastructure:

| | |
|---|---|
| Repository | https://github.com/MMagTech/cabinetos |
| Image | `ghcr.io/mmagtech/cabinetos:latest` — public, unsigned |
| Test machine | Unraid VM at `192.168.1.250`, 4 GB, VirtIO-GPU, SSH key installed |
| Reference hardware | Beelink SER5 — **not yet installed**, awaiting a spare NVMe |

Everything in the pipeline has run green at least once: image build → GHCR →
signing (skipped, no key) → qcow2 → VM boot → `bootc upgrade` in place →
installer ISO. The one untested link is **booting that ISO** and installing to
real hardware.

Known gaps, none blocking:

- **Unsigned images.** No `SIGNING_SECRET` set. Optional until Phase 7, when an
  installed machine needs to verify an update before rebooting into it.
- **SSH is on by default with password auth.** Deliberate for Phases 1–5, and a
  debt Phase 6 must pay — see open question 8.
- **The ISO has never been booted.**
- **HDMI-CEC is specified and protected but unverified.** Nobody on the project
  has an adapter.

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
| Cabinet — `RommAppTV/TVCoverFocus.swift` | same repo | **The three focus treatments, in 200 lines.** The single most useful file in the repository for Phase 3; its comments record why each system default was rejected. |
| Cabinet — `RommApp/RommApp/UI/TenFootMetrics.swift` | same repo | Every ten-foot size in one place. |
| Cabinet — `RommApp/RommApp/Native/NativeLauncher.swift` | same repo | The ROM's path from RomM into a running core, and the three directories. |
| Cabinet — `RommApp/RommApp/Native/NativePlayerRenderer.swift` | same repo | The frame loop, the accumulator and the audio governor. |
| Cabinet — `CLAUDE.md`, `ROADMAP.md`, `docs/settled.md` | same repo | Conventions and decisions already made. Read before proposing anything. Its **tvOS conventions** section is short and every line of it is a mistake already paid for. |
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

Read in Phase 0, from the source rather than the tree.

Cabinet is its own **libretro frontend** — `Native/Libretro/LibretroFrontend.mm`,
`LibretroCoreAPI.h`, `libretro.h`, plus Metal shaders. Cores are compiled to
static archives per platform and linked in: `libflycast_ios.a`,
`libflycast_tvos.a`, `libflycast_mac.a` and so on, built by `tools/build-core.sh`
and per-core scripts.

**23 cores, not 25** — this document previously counted the `Libretro` and
`Archive` directories, which are the frontend and a vendored 7-Zip/zlib, not
cores. The 23:

> BeetleNGP, BeetlePCEFast, BeetleVB, FBNeo, FCEUmm, Flycast, GW, Gambatte,
> GenesisPlusGX, MAME2003Plus, MGBA, MelonDS, Mupen64Plus, Opera, PCSXReARMed,
> PPSSPP, PicoDrive, ProSystem, Saturn (Beetle), Snes9x, Stella2014, VeMUlator,
> Vecx

Two of them — **GW and VeMUlator — are iOS-only by decision**, not by
limitation: tiny canvases that belong in a hand rather than on a television.
Both have `_ios.a` and `_mac.a` and no `_tvos.a`. CabinetOS should inherit that
decision and carry **21 cores**, not 23.

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

## How Cabinet hosts cores

Read from the source 2026-09-13. This is the Phase 0 deliverable that matters
as much as the colour palette, because it is what decides the toolkit.

### The shape: one C++ frontend, cores as data

`LibretroFrontend` is a **process-global singleton**, deliberately. Libretro's
callbacks are plain C function pointers with no context argument, so there is
exactly one frontend's worth of state no matter how the wrapper is shaped, and
exactly one core active at a time. Cores are not re-entrant and the app never
runs two games at once.

Each core is described to the frontend by a **`LibretroCoreAPI` struct** — 21
function pointers covering the libretro entry points the frontend actually uses
(`init`, `load_game`, `run`, `serialize`, `get_memory_data`, and so on). The
frontend never names a core's symbols. That indirection exists purely because
only one core can carry the standard `retro_*` names when they are all statically
linked into one binary, so every other core's archive gets its symbols renamed
with a per-core prefix first.

**On Linux that entire mechanism is unnecessary.** One `.so` per core, `dlopen`ed
with `RTLD_LOCAL`, gives namespace isolation for free. The struct stays — it is a
good shape — but it gets filled by `dlsym` instead of by a generated wiring file.
See open question 13.

### A ROM's journey from RomM to a running core

`NativeLauncher.prepare(rom:session:)`, in order:

1. **Resolve the platform and core.** RomM's platform slug is canonicalised,
   mapped to a `NativePlatform`, which names one core (or several, where the
   user gets a picker — arcade only).
2. **Reject formats the core cannot take.** Saturn, PS1 and Dreamcast are
   `.chd`-only and single-file, deliberately, matching RomM's own recommended
   format for CD platforms. A multi-file cue/bin is refused with an explanation
   rather than attempted.
3. **Find the bytes.** Three tiers, checked in this order:
   - a **kept** game's directory (iOS/Mac), which already holds ROM and
     firmware — zero network;
   - a **cache** directory keyed by rom id (tvOS), left in place between
     launches so a replay costs no download;
   - otherwise **download** the ROM and *every* firmware file the platform
     lists. Not the one the board needs — all of them. A core looks BIOS files
     up by name in the system directory and ignores what it does not want, so
     extra files are harmless and a missing one is the only failure that
     matters.
4. **Extract if archived** (the vendored 7-Zip/zlib in `Native/Archive`).
5. **Restore saves before boot**, not after: any core-written save file is
   placed at the exact name the core will look for, which is the loaded
   content's basename.
6. **Activate the core**, apply core options, set the controller port device
   type, then `loadGame(romPath, systemDirectory:, saveDirectory:)`.

Three directories, and they are three different things — a lesson Cabinet
learned by losing saves:

| | What it is | Lifetime |
|---|---|---|
| **work directory** | where the ROM and firmware sit | per launch, or the kept/cache directory |
| **system directory** | where the core looks up BIOS by name | usually the work directory; the app bundle for PSP, whose "firmware" ships with the app |
| **save directory** | where a core writes its own save files | **must outlive the session** — `CoreSaves/<rom id>/`, namespaced by core on multi-core platforms |

**Pointing the save directory at the per-launch temp directory is how those
saves used to vanish.** CabinetOS must not repeat it: the save directory is
persistent storage from the first line of code, and on a multi-core platform it
is namespaced by core so two arcade emulators cannot overwrite each other's
NVRAM.

### Saves and save states are two different mechanisms

**In-game saves** (the cartridge battery, the memory card) arrive two ways, and
a core uses one or the other:

- `RETRO_MEMORY_SAVE_RAM` — the frontend reads and writes the core's buffer
  directly. Most cores.
- **A real file the core writes itself** into the save directory. Neo Geo
  Pocket, Sega CD, Dreamcast's VMU, FBNeo's NVRAM, melonDS's `.sav`.

The file-writing class has a sharp edge: **`retro_unload_game` is the one moment
those cores flush.** Cabinet's `unloadGame` exists precisely to force it at quit,
in RetroArch's own order — SRAM save, then unload, then deinit. A session killed
by the OS loses everything since launch on those platforms. CabinetOS owns its
own shutdown, so it can do better here than Cabinet can.

Three sizing traps, all confirmed on hardware, all of which CabinetOS inherits:

- **mGBA** reports a placeholder 128 KB until it has autodetected the save type,
  then re-initialises its buffer. Poll the size; a restore seated before that
  moment has to be applied again.
- **Genesis Plus GX** trims its reported size once the game is running.
- **Restore copies the smaller of blob and region** rather than demanding an
  exact match, because of the two above.
- **Game Boy's real-time clock is a separate region** (`RETRO_MEMORY_RTC`).
  Saving only the save RAM silently loses the clock Pokémon Gold and Silver
  depend on. It travels as its own `.rtc` file.

**Save states** are `retro_serialize`/`retro_unserialize`, taken on the thread
that drives `runFrame` — a snapshot taken mid-`retro_run` is corrupt by
definition. They are written only when the user picks Save state; nothing
autosaves them. Two cores (GW, VeMUlator) cannot serialize at all and their slot
UI hides.

### Where a save state lives, and how it reaches RomM

Local first, always. A kept game's state is written into a **`pending-states`
queue** inside its own directory before any upload is attempted, so losing
signal mid-save never loses the save.

The filename is the conflict resolution. Each state is
`<rom basename> [<ISO timestamp>]` with colons, dots and `T` flattened — RomM's
own naming — so an upload lands exactly as if it had happened online and can
never overwrite anything. **Syncing is only "finish the uploads."**

Upload is `POST /api/states?rom_id=&emulator=`, multipart, with the state and an
optional screenshot captured on the paused frame.

> **The `emulator` tag is the only thing standing between a good state and a
> corrupt one, and it does not carry a version.** It is a bare slug —
> `flycast-native`, `mgba-native`, `pcsx-rearmed-native` — and the launch screen
> uses it to grey out states the running core cannot restore. A CabinetOS build
> of a *different revision* of the same core would upload under the same tag, and
> Cabinet would offer the state as loadable. This is the failure mode *Core
> parity is a hard constraint* describes, and the tag cannot detect it. Either
> the builds are genuinely identical, or the tag has to grow a build identity.
> See open question 13.

### The frame loop, and why it is not trivial

This is the part that decides the toolkit, so it is worth stating exactly.

The renderer is driven by a **display link at the panel's refresh rate**, which
is *not* the rate the core wants. An NTSC core asks for 59.94; an Apple TV's
display link can run far above that. So each draw:

1. Accumulates wall-clock time against `1 / core.targetFPS`.
2. Runs `retro_run` while the accumulator has a whole interval in it, **at most
   twice per draw**, so a stall cannot bank a debt and repay it as a stutter.
   The accumulator itself is capped at four intervals — time beyond that is
   simply gone.
3. Presents the latest frame whether or not a core frame was produced, so the
   picture holds steady while the core is not yet due.

Running the core once per display tick instead was measured wrong on hardware:
Dreamcast produced 65,000–85,000 audio frames a second against 44,100 of
realtime, ~1.5× too fast, with the surplus discarded — which is what made music
play back sped up.

**One core needs a second brake.** Flycast's threaded renderer free-runs its
emulation thread as far ahead as its render queue allows — measured at up to
five times realtime. RetroArch's backpressure is a blocking audio callback;
Cabinet's callback must never block (it feeds a realtime ring), so the brake is
an **audio governor**: when the core's own audio output is ahead of the wall
clock by more than a 20 ms cushion, it is not due, whatever the accumulator says.
Skipping the run leaves the render queue unconsumed, which is what actually
stalls the emulation thread.

The cushion is felt latency: the lead the governor permits *is* input lag, at
10 ms per hundredth of a second. It was 50 ms, was reported as bad input lag on
Dreamcast, and is now 20 ms. **This is a number CabinetOS will have to tune
again**, because the display path is different.

Applied to every core the governor made things worse (it slowed N64 down). It is
Flycast-only, by measurement.

### Video: two paths, and one of them is free on Linux

**Software-rendered cores** (most of them) hand the frontend a pixel buffer in
one of three libretro formats — RGB1555, XRGB8888, RGB565 — which is copied out
per frame because cores reuse their buffer.

**Hardware-rendered cores** (Flycast, Mupen64Plus, PPSSPP) ask for a GL context
via `RETRO_ENVIRONMENT_SET_HW_RENDER` and render into an FBO the frontend owns.
The frontend then has to get those pixels *back* to draw them, and on Apple that
means `glReadPixels` into a pixel-pack double buffer, publishing the previous
frame while the current one copies. It costs a frame of latency and was worth it:
the synchronous version measured 12.7 ms inside `glReadPixels` on a heavy
Dreamcast scene — two thirds of the whole frame.

> **On Linux that readback should not exist.** It is there because the core
> renders in GL and the display path is Metal, so the pixels must cross an API
> boundary on the CPU. A Linux frontend that draws with GL or Vulkan can sample
> the core's FBO texture directly. This is a performance *gain* from porting,
> not a cost — and it removes the single largest per-frame cost the Apple build
> has on its three heaviest cores.

### The in-game overlay, and the input-mode rule

The overlay is not composited by anything clever. **The frontend owns the frame
loop, so the pause menu is simply a view drawn over the game surface** — a
scrim, a panel, and buttons. That is the whole mechanism, and it is the direct
payoff of hosting cores in-process rather than launching them.

The one hard rule is about input, and it is architectural:

> **While a game runs, the controller belongs to the core exclusively. While the
> overlay is open, it belongs to the UI.** Never both.

On tvOS this is `controllerUserInteractionEnabled`, flipped by whether the menu
is visible. Without it the focus engine kept consuming presses, so B read as "go
back" and dismissed the player instead of reaching the core as a face button —
reported from real hardware as *"controllers work on the homescreen but in game
b exits the game"*.

CabinetOS has no focus engine handed to it, so it must implement both halves —
but it must implement the *rule*, not just the routing. Any design where a
button can mean two things at once is the same bug.

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

Taken from Cabinet's own icon generator, `tools/make_icon.swift`.

**Verified against the source 2026-09-13: every value below is correct.** One
detail was missing and is added — the backdrop's middle stop sits at 0.55, not
at the midpoint.

| Role | Value |
|---|---|
| Backdrop | `#3A2268` (0.0) → `#120C26` (**0.55**) → `#090614` (1.0), vertical, top to bottom |
| Cabinet body | `#EEEAE2` |
| Marquee | `#FF7AC7` → `#FFC457`, horizontal |
| Screen | `#58E8F6` → `#2484D6`, vertical, with a white sheen at 26% fading out |
| Control panel / base | `#CEC7BC` |
| Joystick | `#3A3444` |
| Buttons | `#EC405C`, `#FFC457` |

**But this is the icon's palette, and the app does not use it.** That is worth
saying plainly, because this document previously implied otherwise.

Cabinet has **no colour assets and no design tokens at all** — checked: the
asset catalogues hold app icons and nothing else, and there is not one
`.colorset` in the repository. What the app actually looks like comes from three
places, none of which is the icon:

1. **Black, white and system semantic colours.** `Color.black` (57 uses),
   `Color.white` (41), `.primary`, `.secondary`, `.tertiary`. Everything is
   dark; `colorScheme` is forced to `.dark` where the platform would otherwise
   have a say.
2. **System materials** — `.regularMaterial`, `.ultraThinMaterial`, and on
   tvOS 26 real Liquid Glass. This is the single largest contributor to the
   look, and it is the thing CabinetOS gets none of for free.
3. **The artwork itself.** Cover art is the brightest thing on every screen, by
   explicit design, and backgrounds are almost always a blurred, desaturated,
   darkened copy of the art in front of them.

There are exactly **two** deliberate colours in the whole app:

| | Value | Where |
|---|---|---|
| Platform tile panel | `#241A3D` | `TVLibraryView.panel`, the library's tile background |
| Accent | **unset** — Apple's default | the prominent pause-menu button, progress tints |

The panel colour carries a note worth keeping: it is *"deliberately darker and
less saturated than it looks in a browser mockup: the same sRGB values render
far more vividly on a wide-gamut TV, and the first build of this tile came out a
loud electric purple on real hardware."* CabinetOS ships to televisions. Tune on
one.

**The accent colour is a decision CabinetOS has to make and Cabinet never did.**
Nothing sets one, so `Color.accentColor` is whatever Apple's default tint is on
the platform. A Linux frontend has no such default. The icon offers the obvious
candidates: the screen cyan `#58E8F6` reads as "powered on" and has the contrast
for a focus tint against a dark ground; the marquee pair `#FF7AC7`/`#FFC457` is
warmer and more arcade. Pick one in Phase 3 and put it in the token table below.

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

## The design system

The Phase 0 deliverable. Extracted from Cabinet's source 2026-09-13, written so
that someone who has never read Swift can reproduce it.

**Read the caveat first.** Cabinet is a SwiftUI app on a platform that hands it a
focus engine, a type ramp, a materials system and a navigation container. A large
part of "what Cabinet looks like" is tvOS behaviour that Cabinet never wrote
down, because it never had to. This section writes it down. Where a value comes
from Cabinet's own source it is stated as such; where it comes from the platform
it says so, and those are the places CabinetOS has to *build* something rather
than *match* something.

### The canvas

tvOS lays out in a **1920×1080 point space regardless of panel resolution** — a
4K television renders the same layout at 2×. Every number in this section is in
those points.

CabinetOS should adopt the same convention: **design at 1920×1080 and scale**.
It makes every number here directly usable, it matches what the reference
implementation was tuned against, and it means a 4K panel is a rendering
decision rather than a layout one.

**VERIFIED on the VM, 2026-09-13.** The frontend renders the same frame at
3840×2160, 1920×1080 and 1280×720 and the layout is identical to within a pixel
in design points — a focused cover measures 277.0, 278.0 and 277.5 design points
of visible fill against a predicted 277.2. It is rendered *natively* at each
size rather than upscaled: the UI's shapes are signed-distance fields, so a 4pt
rim is exactly 4pt and an edge is exact at any resolution.

**Most televisions this lands on will be 4K, and plenty will not be, and neither
may be assumed.** So the frontend can render offscreen at any size and read the
frame back (`--render-size`), which is how a 1280×800 development VM proves its
layout on a 4K set nobody here owns. Do that for any layout change; it costs
seconds.

Two consequences worth holding on to:

- **Non-16:9 panels letterbox rather than stretch.** Verified: the VM's own
  1280×800 output produces correct bars. A console puts the slack in bars; it
  does not distort the picture.
- **4K costs real fill rate.** Drawing rectangles at 3840×2160 is free, but
  Phase 8 should decide deliberately whether the *game* is upscaled by gamescope
  from its native resolution or rendered larger. That is a performance decision
  on Vega integrated graphics, not a layout one, and this canvas keeps the two
  separable.

**Overscan is real and the simulator lies about it.** Home's hero was sized
three times before it fit: 0.42/460 cut the shelf caption off, 0.34/380 still cut
it off *on real hardware although the simulator showed it fitting*, 0.28/300 fit
with room to spare, and it finally settled at 0.40/420. Budget a safe area and
verify it on a television, not on a screenshot.

### Colour tokens

Cabinet has none, so these are named here for CabinetOS to implement.

Two kinds of value below, and the difference matters. **Literal** values are read
straight out of Cabinet's source and are exact. **Semantic** values are what
Cabinet asks the platform for — `.secondary`, `.tertiary`, `Color.red` — so the
number given is Apple's documented dark-mode value, not something measured here.
Treat the semantic ones as the intended relationship and settle the exact numbers
when there is something on a television.

| Token | Value | Kind | What it is |
|---|---|---|---|
| `bg` | `#000000` | literal | The ground. Genuinely black, not near-black. |
| `bg-modal` | `#212121` → `#141414` vertical | literal | Full-screen covers with no artwork to blur (account, PIN, setup) |
| `surface` | `#241A3D` | literal | The one solid panel colour — library tiles. Source is `rgb(0.14, 0.10, 0.24)`; use those floats rather than the hex if there is any doubt. |
| `text-primary` | `#FFFFFF` | literal | |
| `text-secondary` | white @ ~60% | semantic | Platform labels, counts, captions under a title |
| `text-tertiary` | white @ ~30% | semantic | Chevrons, disclosure marks |
| `scrim-overlay` | black @ 55% | literal | Behind the pause menu, and over blurred backdrops |
| `focus-rim` | white @ 85% | literal | The 4pt ring on focused artwork |
| `accent` | **to be chosen** | — | Prominent action fills |
| `destructive` | system red | semantic | Quit, sign out, delete |

**Materials are the hard part.** Cabinet leans on four surface treatments that
Linux gives you nothing for:

| Cabinet's name | What it does | Linux equivalent |
|---|---|---|
| `.ultraThinMaterial` | heavy blur, very light tint | backdrop blur, ~30px, white @ 10% |
| `.regularMaterial` | heavy blur, mid tint | backdrop blur, ~40px, white @ 18% |
| Liquid Glass `.regular` | the above plus edge refraction and specular highlight | **no equivalent** |
| Liquid Glass tinted | as above, tinted white @ 22–35% | **no equivalent** |

CabinetOS should implement the first two as a real backdrop blur and **not
attempt the last two.** Liquid Glass is a system effect with per-frame cost that
Apple absorbs; an imitation of it is both expensive and recognisably not it. The
honest translation is a tinted blur, which is what Cabinet itself falls back to
on tvOS 18 and which the source describes as adequate.

### Type

The ramp is Apple's tvOS text styles. Cabinet names styles, never sizes, with
two exceptions. **These sizes are Apple's published tvOS ramp, not measured on
the device** — treat them as the intended proportions and verify once there is
something on a screen.

| Cabinet's name | Size / weight | Used for |
|---|---|---|
| Large Title | 76 bold | Game detail title; settings page titles |
| Title 1 | 57 | — |
| Title 2 | 48 bold | Shelf headers ("Recent", "Favorites"); pause-menu game name (semibold) |
| Title 3 | 38 semibold | Settings rows, tile titles, switcher pills, pause-menu buttons |
| Headline | 38 semibold | Hero card game title |
| Callout | 31 | Cover captions; secondary detail under a settings row |
| Body | 29 | |
| Footnote | 29 | Game counts on tiles |
| Caption 1 | 25 | Hero card platform label |

**One hardcoded size exists** in the whole tvOS UI — **40 bold**, the rom grid's
own screen title inside its glass chip. It sits between Title 2 and Large Title
because a grid title should not shout as loudly as a game's name does. Everything
else names a style.

One rule is recorded as a mistake already made: **shelf captions and grid
captions must be the same style.** Home's shelves ran at Title 3 while the
library grid ran at Callout, "not a deliberate size difference." Both are
Callout now.

### Spacing and sizing

Everything below is from `TenFoot` and the tvOS views, in points.

| | Value |
|---|---|
| **Content inset**, horizontal | 60 (Home) / 80 (Library, grid, detail, settings) |
| **Shelf cover** | 260 × 347 (3:4) |
| **Shelf spacing** | 40 |
| **Shelf vertical padding** | 24 — headroom for the focus scale, not decoration |
| **Caption gap** below a shelf cover | 6 |
| **Grid cover** | adaptive, minimum 260 |
| **Grid column spacing** | 48 |
| **Grid row spacing** | 44 |
| **Caption gap** below a grid cover | 10 |
| **Platform tile** | adaptive minimum 380 wide, **200 tall**, spacing 36 both axes |
| **Settings column** | max width **1100**, rows 16 apart |
| **Settings row padding** | 32 horizontal, 22 vertical |
| **Hero card** | full content width, height `min(screenHeight × 0.40, 420)` |
| **Detail cover** | 340 × 460 |
| **Pause panel** | max width 560, padding 40 |

`TenFoot` declares `gridCoverMinimum = 240` but the grid that uses it hardcodes
260. **Take 260** — the hardcoded value is the one that shipped and was looked at.

### Corner radii

There is a real system here and it is worth keeping: **radius tracks the size and
the seriousness of the thing.**

| Radius | Applied to |
|---|---|
| 8 | A cover thumbnail inside another element |
| 10 | Shelf cover art |
| 12 | Grid cover art |
| 16 | Detail-screen cover; settings rows |
| 18 | Hero card; platform tiles; pause-menu buttons |
| 32 | The pause-menu panel |
| capsule | Pills, chips, the Resume button, the library switcher |

### Focus and selection

**This is the most important part of the document.** On tvOS the focus engine is
free; on Linux it is the single biggest thing CabinetOS has to build. Cabinet's
own rule sits in its conventions file, put there after the same mistake was made
at least twice. In summary — the original is longer and names specific SwiftUI
styles:

> Never use the system's default focus treatment. It paints a solid plate over
> whatever the element already has, and it reserves no headroom for its own
> scale growth, so a focused row grows into its neighbour.

Cabinet therefore defines **three** focus treatments, and everything focusable
uses one of them.

**Reserved headroom is the other half of that rule**, and it is a layout
obligation rather than a style one: a shelf carries 24pt of vertical padding for
no reason except that its cards grow by 10% when focused, and without it the
grown card is clipped against the rail's bounds. Every container holding
focusable elements has to budget for their focused size.

#### 1. Artwork — lift, shadow, rim

For anything whose content is a picture.

| | Rest | Focused | Pressed |
|---|---|---|---|
| Scale | 1.00 | **1.10** | 1.02 |
| Shadow | none | black @ 55%, blur 26, offset y +14 | |
| Rim | none | white @ 85%, **4pt, inset** | |
| Duration | | 180 ms ease-out | 120 ms ease-out |

Two details that are not obvious and both came from real bugs:

- **Pressed scales *down* from focused**, to 1.02. The card is already raised, so
  a click has to read as a push *into* the screen or it does not read at all.
- **The caption slides down** by `coverHeight × 0.05 + 2` when its card is
  focused. A 1.10 scale about the centre advances the bottom edge by 5% of the
  height, which buries the caption underneath it. The 0.05 is half of
  `1.10 − 1`; if the scale changes, this changes with it.

The rim is **suppressed on composite elements** — anything whose label mixes art
with its own text, like a platform tile. A rectangle drawn around the whole
button always crosses the text somewhere. The scale and shadow carry focus
perfectly well alone.

#### 2. Text controls — tint, lift, a shape behind

For a short label or a pill: a shelf's "Recent ›" header, the Platforms /
Collections switcher, a save-state entry.

| | Rest | Focused |
|---|---|---|
| Text | secondary | white |
| Background | none | tinted blur, white @ 25% |
| Scale | 1.00 | **1.06** |
| Padding | 14 horizontal, 8 vertical | |
| Duration | | 180 ms ease-out |

#### 3. Rows — a surface that is always there

For a full-width settings-style row.

| | Rest | Focused |
|---|---|---|
| Background | blur, untinted | blur, white @ 22% |
| Scale | 1.00 | **1.03** |
| Radius | 16 | |
| Duration | | 180 ms ease-out |

The scale shrinks as the element grows: **1.10 for a cover, 1.06 for a pill,
1.03 for a full-width row.** That is not arbitrary — a full-width row growing
10% would collide with its neighbours, and a small pill growing 3% would not
read at all.

#### Where focus lands

- **Home puts focus on the hero** on arrival, explicitly, arbitrated against the
  account chip that sits above it in reading order.
- **Library puts focus on the switcher** on first arrival — but *only* the first
  time. Re-entering from a pushed screen must leave focus where back-navigation
  put it. Cabinet got this wrong first: forcing focus on every appearance yanked
  it away whenever the user came back from another tab.
- The primary action on a screen should be reachable without travelling
  through secondary ones.

#### Selection is not focus

A selected library switcher pill is tinted **white @ 35%**; a focused one is
tinted **white @ 25%** and scaled. The two states are independent and both are
visible at once. This matters: a controller-driven UI where the user can move
focus away from the current selection has to show both, or they lose their place.

### Motion

Cabinet's motion vocabulary is small and almost entirely ease-out. That is the
system, and it should be kept.

| Duration | Curve | What |
|---|---|---|
| **60–80 ms** | ease-out | Button press feedback; an LED changing |
| **120 ms** | ease-out | Press state on a focused card |
| **150 ms** | ease-out | Pause-menu button focus; menu appear/dismiss |
| **180 ms** | ease-out | **The focus transition. The most-used value in the app.** |
| **220 ms** | snappy | A segmented choice changing |
| **250 ms** | ease-in-out | A pairing code appearing; a progress bar |
| **280 ms** | ease-out | Launch transition, secondary elements |
| **350 ms** | ease-out | Artwork arriving asynchronously — a cover mosaic filling in |
| **350 ms** | ease-in-out | The in-game overlay appearing and dismissing |
| **600 ms** | ease-in-out | A deliberate state change the user should watch |
| **1400 ms** | ease-in-out, repeating | Boot-curtain shimmer |

Springs appear **four** times in the whole app, always for something with
physical character. Two are tuned; two use the platform default to snap a
dragged element back:

| Response | Damping | What |
|---|---|---|
| 0.34 | 0.55 | A control pad element settling |
| 0.40 | 0.42 | The launch transition's lead element — deliberately loose, so it overshoots |
| default | default | A dragged sheet returning to rest (twice) |

**Rules that fall out of this:**

1. **Ease-out is the default.** Things arrive quickly and settle. Ease-in-out is
   for a change of state the user asked for; ease-in is used nowhere.
2. **180 ms is the focus tempo**, and nothing about focus should be slower. A
   controller user crosses a shelf faster than that, and the animations must not
   queue up behind them.
3. **Asynchronous content fades in at 350 ms**, never snaps. Cover art arriving
   over a network is the common case.
4. **Springs are rare and mean something.** Everything else is a curve.

> **Phase 3 warning, already recorded:** software rendering in the VM makes
> motion choppy. **Build the motion from these numbers; do not judge it there.**
> An animation tuned against software rendering is tuned against the wrong
> feedback.

### Navigation model

**Four destinations, always reachable, in a bar across the top:**

> Home · Library · Search · Settings

Search is drawn apart from the other three — it is not just a fourth tab. Library
is **hidden entirely** when the machine is offline, rather than shown empty:
its only honest content offline is exactly what Home already shows, and *"if both
tabs are the same why two."*

Settings is a real destination on a television, not a corner button. On a phone
that placement is about thumb reach; a controller has no thumb reach and every
destination costs the same number of clicks.

Within that:

- **Each tab owns a navigation stack.** Library pushes to a platform's grid.
- **Game detail is a full-screen cover, not a push.** It replaces the screen
  entirely, with the artwork as its own backdrop.
- **The player is a full-screen cover over the detail screen.** So quitting a
  game returns to the detail screen, and backing out again returns to where the
  user was browsing.
- **No screen titles that repeat the tab.** Library has no "Library" heading;
  Settings has no "Settings" heading. The bar already says it.
- **A pushed page carries its title as ordinary content at the top of its own
  scroll view**, never as system chrome. On tvOS the system version painted over
  the artwork.

#### Home is resume-first

Home is not a menu. Its structure is fixed:

1. **The hero** — what you were playing. Focused on arrival.
2. **Recent** — everything else recently played, as a horizontal shelf.
3. **Favorites** — a second shelf, only if there are any.

The hero carries **two** actions and the distinction is load-bearing:

- **Resume** (a pill in its top-right corner) goes *straight into the game*,
  with the previous choices already made and the newest state loaded, wherever
  that state was written. Resume means resume; stopping at a screen with a Play
  button on it is two actions, not one.
- **The artwork itself** opens the detail screen, which is where you go to pick a
  different state, change the core, or export.

When there is nothing to resume, Home says so in its own words and points at the
Library — it does not show an empty shelf.

### Component inventory

Everything CabinetOS's frontend needs to draw, with the treatment it uses.

| Component | Shape | Focus treatment |
|---|---|---|
| **Cover card** | 3:4 art, radius 10–12, caption below | Artwork |
| **Hero card** | wide, radius 18, art fitted over a blurred copy of itself, frosted title band at the bottom | Artwork (no rim) |
| **Resume pill** | capsule, blurred fill, icon + label | Text control |
| **Shelf** | header row (title + chevron) then a horizontal rail | header is a Text control; cards are Artwork |
| **Rail edge fade** | the rail is masked to transparent over its outer 4% at each end | — |
| **Platform tile** | 380×200, radius 18, label left, cover right, blurred art behind | Artwork (no rim) |
| **Cover grid** | adaptive columns, two-line captions with reserved space | Artwork |
| **Switcher** | a row of capsule pills, one selected | Text control |
| **Screen-title chip** | a static glass capsule, not a button | — |
| **Settings row** | full width, title + detail + optional value + chevron | Row |
| **Settings page** | plain large title, then rows, max 1100 wide | — |
| **Pause menu** | scrim, centred panel radius 32, full-width buttons | its own — scale 1.04, 150 ms |
| **Primary action button** | the one place a solid opaque fill is right | platform default |
| **Badges** | small overlays on a cover: incompatible, favourite, downloaded | — |
| **On-screen keyboard** | *does not exist in Cabinet* — tvOS provides one | **CabinetOS must build this** |
| **Progress** | a determinate bar for downloads; the label carries the percentage | — |
| **Toast / banner** | capsule, blurred, slides in from the top edge, self-dismissing | — |

Two rules about lists worth carrying:

- **A list of covers gets two caption lines with reserved space**, so rows stay
  aligned whether a title wraps or not. One line truncated almost every real
  title at these widths.
- **A wide screen does not get a full-width row.** A row stretched to 1920
  points leaves a name at the far left and a count at the far right with a third
  of the screen empty between them. Use a tile grid, which also gives the focus
  engine a real two-dimensional field to move in.

### What CabinetOS has to build that Cabinet got for free

Stated plainly, because it is the honest cost of "owned, not skinned":

1. **A focus engine.** Spatial navigation between arbitrary rectangles, with
   remembered focus per container, and the three treatments above.
2. **An on-screen keyboard.** The baseline for all text entry, per the input
   model. Cabinet never wrote one.
3. **Backdrop blur** as a real, cheap effect, since it is load-bearing in almost
   every component.
4. **Asynchronous image loading** with the 350 ms fade, placeholder handling and
   a memory budget.
5. **Safe-area handling** for overscan.
6. **The text ramp**, as actual numbers, with a font chosen and shipped in the
   image.

None of these is research. All of them are work, and they are the reason Phase 3
is a phase.

---

## The frontend toolkit

**Recommendation: C++20, SDL3 for platform and input, one EGL / OpenGL ES 3.x
context, and a hand-written retained UI layer. Evaluate RmlUi for the UI layer
before writing one.**

Decided in Phase 0 with Part 1's findings in hand. Record a reversal here rather
than editing this, if it is reversed.

### What the program actually is

The constraint that decides this is in *Emulation*, and Part 1 sharpened it:

> The frontend is not a launcher. It owns the frame loop, loads cores as
> libraries, feeds them ROM data and controller input, presents their output,
> and draws its own UI over the top — **in the same graphics context**, at a
> stable 60 Hz, while a PS2 is being emulated underneath.

Six hard requirements fall out, and each one eliminates candidates:

1. **Cheap C FFI, called per frame.** `retro_run` is called up to twice per
   draw; `retro_serialize` moves megabytes. Any toolkit whose foreign-function
   boundary has per-call overhead or a marshalling step is disqualified.
2. **A real GL context the frontend owns and hands to cores.** Flycast,
   Mupen64Plus and PPSSPP render through
   `RETRO_ENVIRONMENT_SET_HW_RENDER` into an FBO. The toolkit must let the
   frontend create that context, not create one for it and hide it.
3. **The UI must draw into the same context.** This is what deletes the
   `glReadPixels` readback — the largest per-frame cost on Apple's three
   heaviest cores. A toolkit that composites the game as a separate surface or
   texture handoff gives that cost straight back.
4. **Frame pacing under the frontend's control**, to the precision the
   accumulator and the audio governor need. A toolkit that owns the render
   thread and decides when frames happen is fighting the one thing this program
   must get right.
5. **A Wayland client**, with no X11 assumption, since the session is gamescope
   or cage.
6. **Embeds two large C++ emulators.** Dolphin and PCSX2 are not libretro cores;
   they are whole emulators with host layers Cabinet has already written —
   `CabinetDolphinHost.cpp` (574 lines) and `CabinetPS2Host.cpp` (811 lines),
   plus their bridges. **Both are plain, portable C++ today.**

### Why C++

Because it is the language the program is already written in.

Cabinet's frontend is Objective-C++ whose Objective-C surface is a thin veneer:
of `LibretroFrontend.mm`'s 2,281 lines, **125 touch an Apple type, and 60 of
those are in the wrapper at the bottom.** The C++ underneath — environment
callback, video refresh, readback ladder, input state, option handling, the
core-tolerates-deinit table — ports substantially unchanged. The GL path is
**already EGL and GLES3**, behind the `CABINET_ANGLE` flag, because the Mac
build reaches GLES through ANGLE. On Linux that is the native path.

The two heavy emulators' host layers compile as they are. Every core is C or
C++. Every other emulator project that hosts cores in-process — RetroArch,
Dolphin, PCSX2, Flycast — is C++ with a hand-written renderer. Choosing anything
else means writing and maintaining a binding layer across the hottest boundary
in the program, forever, for a UI convenience.

The counter-argument is real and should be stated: **the UI is the majority of
Phase 3's work, and C++ gives you none of it.** That is true. It is also true in
every other candidate, because the thing that would have saved the most work —
tvOS's focus engine — has no equivalent anywhere. See the alternatives below.

### The shape to build

**One process. One EGL context. Cores as `.so` files.**

```
cabinetos-frontend  (C++20, SDL3, EGL/GLES3)
  ├── libretro host      — the ported LibretroFrontend, dlopen + RTLD_LOCAL
  ├── UI layer           — focus engine, layout, text, blur, OSK
  ├── RomM client        — REST + WebSocket
  └── session            — storage, saves, sync queue

/usr/lib/cabinetos/cores/
  ├── flycast_libretro.so
  ├── mgba_libretro.so
  ├── … 21 cores
  ├── dolphin.so         — Dolphin + CabinetDolphinHost, behind the same struct
  └── pcsx2.so           — PCSX2 + CabinetPS2Host, likewise
```

Three consequences, all good:

- **The symbol-prefixing apparatus disappears.** `RTLD_LOCAL` gives namespace
  isolation for free, so `bsat_wrapper.c`, the `ld -r` merges, the exported
  symbol lists and `-fno-common` all go. See open question 13.
- **Dolphin and PCSX2 need not be linked into the frontend.** Put each behind the
  same struct-of-function-pointers the libretro cores use, compile its host layer
  into its own `.so`, and the frontend binary stays small and fast to rebuild —
  which is what makes the SFTP development loop bearable.
- **Separate `.so` files rechunk into smaller image layers**, which the update
  model cares about: a core bump moves one layer, not the whole image.

`SDL3` covers Wayland, gamepads (including hotplug and the Steam-style mappings
Bazzite already ships udev rules for), and audio. It is already in the base
image. Use it for platform, input and audio; **do not** use its renderer — the
frontend needs the raw GL context.

Audio goes to PipeWire through SDL3, with the same rule as Cabinet: **the
callback never blocks.** It drains a ring the draw loop fills. That rule is what
made the audio governor necessary, and it is the right rule.

### The UI layer

The design system section is the specification for this. Before writing it from
nothing, **evaluate RmlUi**: it gives layout, text shaping, and a CSS-like
styling system, and — decisively — it renders through a backend *you* supply, so
it shares the frontend's GL context rather than owning one. That is the property
that matters, and most UI libraries do not have it.

Dear ImGui is the other option and has direct precedent: **PCSX2's own
`FullscreenUI` is a controller-driven, cover-art, ten-foot console UI built on
ImGui, running over a live emulator.** Cabinet's PCSX2 patch #13 disables it
precisely because Cabinet has its own — which is a demonstration that the
approach works, from inside this project's own dependencies. The reservation is
that immediate mode makes remembered focus, caption slides and interruptible
transitions awkward enough that a retained layer tends to get built on top
anyway.

Either way, **the focus engine is ours.** Nothing provides it.

### The alternatives, and why not

| | Why it was considered | Why not |
|---|---|---|
| **Qt 6 / QML** | The strongest alternative. Gives layout, text, a focus and key-navigation model, shader effects for blur, and animation declared exactly as the design system states it (`easing.type: Easing.OutQuad; duration: 180` maps one to one). Qt 6 is **already in the base image** — see open question 1. | Qt Quick's scene graph owns the render thread and its vsync cadence, which is the one thing requirement 4 says must be ours. It is injectable (`QSGRenderNode`, `beforeRendering`) but you are then fighting the framework at the hottest point in the program. Qt Virtual Keyboard is GPL-or-commercial. **Worth a spike before committing against it** — see below. |
| **Rust + wgpu** | Good FFI to C, strong tooling, memory safety where it is genuinely useful. | `wgpu` abstracts away the GL context the cores require, so you would run raw EGL beside it and share textures across two graphics abstractions. C++ interop (Dolphin, PCSX2) needs a C shim — smaller than it sounds, since Cabinet already wrote those bridges, but real. And this project has one developer, who is not a Rust developer. |
| **Godot** | Owns a frame loop, has a UI system with focus neighbours, exports to Linux/Wayland. | Owns the frame loop *its* way. Embedding a core's GL FBO into its renderer, and embedding Dolphin and PCSX2 at all, is fighting the engine. Ships a large runtime to draw ten screens. |
| **GTK4** | In the image already. | A desktop application toolkit. No ten-foot story, no focus model of the kind needed, and the same render-loop ownership problem without Qt's compensating strengths. |
| **Flutter** | Real Linux embedder, decent FFI. | The game reaches the screen through the texture registry — a handoff, which is requirement 3 given straight back. Desktop-oriented. |
| **Electron / web** | — | A browser is a non-goal, and this is the frame loop of an emulator. |
| **RetroArch, EmulationStation, ES-DE** | Solve much of this already. | Constraint 4: the frontend is owned, not skinned. Also none of them hosts real PCSX2 and Dolphin in-process, which is the thing that makes one overlay and one save-state path possible. |

### The one spike worth running before Phase 3 starts

Qt is the only alternative strong enough to be worth an hour of doubt, and the
question between it and C++ is narrow and testable:

> **Can a Qt Quick scene draw over a libretro core's FBO, in the same GL
> context, with the frame loop paced by us rather than by the scene graph?**

Build one screen — a shelf of covers with the three focus treatments — over
Flycast, in the VM, under cage. If the pacing is ours and the readback is gone,
Qt buys a great deal of Phase 3 for free and the base image already carries it.
If it is not, the answer is C++ and the hour was worth it.

Do not run this spike for Godot, Flutter or Rust. Their objections are
structural, not empirical.

### What this decides about the image

- **Runtime dependencies stay small**: SDL3, Mesa (EGL/GLES), PipeWire, and
  whatever the UI layer needs for text. All already present.
- **Open question 1 reopens slightly.** If the spike chooses Qt, Plasma's Qt 6
  is load-bearing rather than dead weight and the question answers itself. If it
  chooses C++, Qt has no user in the image and the removal argument gets its
  first real reason beyond tidiness.
- **`ci/base-watch.txt` gains** SDL3, Mesa and PipeWire, per *Staying current
  with Bazzite*'s own instruction that the list must grow with the project.

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
**Status: COMPLETE, 2026-09-13.**

*Done when* a developer who has never read a line of Swift could reproduce the
look and feel of Cabinet from the document alone.

**Shipped, all from reading Cabinet's source rather than its documentation:**

- ***The design system*** — the canvas, colour tokens, the type ramp, spacing,
  corner radii, the three focus treatments, the motion vocabulary, the
  navigation model, and a component inventory. It ends with an explicit list of
  the six things tvOS provided free that CabinetOS has to build.
- ***How Cabinet hosts cores*** — the ROM's path from RomM into a running core,
  the three directories and why confusing them lost saves, how in-game saves
  differ from save states, where a state lives before and after upload, the
  frame loop and the audio governor, the two video paths, and the overlay's
  input-mode rule.
- ***The frontend toolkit*** — the recommendation, the requirements that produce
  it, the alternatives, and the one spike worth running before Phase 3 starts.
- **Open question 13, scoped** — every core checked rather than assumed.

**What it changed:**

- **The palette was verified and is correct**, with one missing gradient stop
  added — but this document's claim that it is what the app looks like was
  wrong. Cabinet has no colour assets at all. Corrected in *Branding*.
- **The core count was wrong.** 23, not 25, and two of those are iOS-only by
  decision, so CabinetOS carries 21.
- **A Linux core build is much cheaper than feared**, and the Apple-only
  apparatus disappears rather than being ported.
- **The parity risk moved.** It is not "can the cores be built" — it is that a
  plain Linux build silently selects a *different CPU backend*, and that
  Cabinet's build system cannot currently be run by anyone. Both have concrete
  fixes, recorded in open question 13.
- **There is a one-evening test that answers the save-state question** on
  hardware the project already owns, with no Linux toolchain. It is the highest
  value work available right now. See open question 13.

### Phase 1 — Base image
**Status: COMPLETE, 2026-09-13.**

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

**Status changed to COMPLETE 2026-09-13.** A qcow2 was built, booted in an
Unraid VM, and reached a console prompt. SSH works. The machine has since been
upgraded in place with `bootc upgrade`, which pulled 1.0 GB of a 5.0 GB image
because only changed layers moved — the Phase 7 update mechanism working, months
early.

An `anaconda-iso` also builds. It has **not** been booted; installing to real
hardware is the one step in the chain never exercised.

### Phase 2 — Boot to frontend
**Status: in progress. The session works; splash and power button remain.**

Autologin, no display manager, a custom session launching a fullscreen
placeholder application. Every route to a desktop, file manager or terminal
closed. Shutdown and suspend reachable from a controller.

A keyboard and mouse attached to the session must work — they simply must not be
needed. Closing "every route to a terminal" means the UI offers none, not that
input devices are blocked.

*Done when* power on leads to the placeholder with no keyboard involved.

**Shipped:** `cabinetos-session.service` takes tty1 via
`Conflicts=getty@tty1.service`, so there is no login prompt to fall back to.
`PAMName=login` gives it a real logind session on seat0. `StartLimitIntervalSec=0`,
because a console that has given up retrying is a support call.
`/usr/bin/cabinetos-session` works down the compositor ladder (gamescope/drm →
cage → headless). The session user is created by the image via `sysusers.d`.
`plasma-setup` and the `plasma.desktop` session are gone. Seven dead-weight
services disabled — 91 MB and 5 seconds of boot.

Verified on the VM: session active, zero restarts, correct fallback chosen.

**Remaining:**

1. **Boot splash.** Design settled — see *Branding*. Needs `quiet loglevel=0` on
   the kernel command line, a Plymouth theme, and a handoff to the frontend with
   no black flash. Testable in the VM; Plymouth's renderers do not need Vulkan.
2. **Power button → clean shutdown.**
3. **Re-verify on a freshly installed image**, rather than one upgraded in place.

### Phase 3 — Frontend shell
**Status: in progress. The foundation runs on the VM, 2026-09-13.**

**Shipped:** `frontend/` — C++20, SDL3, one EGL/GLES 3 context, and a UI layer
of our own, built in a pinned Fedora 44 container (the image carries no
compiler, deliberately) and run as an ordinary Wayland client of the session's
existing `cage`. No toolkit, no Qt, no spike needed — see *The frontend
toolkit* for why that argument resolved without one.

Running and verified on the VM:

- A real GLES 3.2 context under cage, on llvmpipe, 1280×800.
- The design canvas, letterboxing correctly on a 16:10 panel, and **identical
  layout at 4K, 1080p and 720p** — measured, see *The canvas*.
- The **artwork focus treatment, to the point**: 1.10 scale, a 4pt inset white
  rim at 85%, a black 55% shadow blurred 26 and offset 14 down, the press state
  pushing back to 1.02, and the caption sliding clear of the grown card.
- 180 ms ease-out, retargeting from the current value on interruption rather
  than jumping — the reference implementation's own animation behaviour.
- Controller and keyboard both driving focus, neither required.

**Not there yet, and each is its own piece of work:** text (there is no font
layer, so captions are placeholder bars sized to the real type's space), images,
and cores.

The real frontend, built against the Phase 0 spec, running on fake data. Home,
browse, game detail, settings, in-game overlay. Full controller navigation, plus
the on-screen keyboard, which everything else that needs text entry depends on.

**Start with the Qt spike** in *The frontend toolkit*, not with a screen. It is
the only toolkit question left worth an hour, and it is cheap. Then:

1. **The focus engine before any screen**, because every screen depends on it
   and it is the thing tvOS gave Cabinet free. Three treatments, remembered
   focus per container, spatial navigation.
2. **One shelf of covers over a black background**, to get the 180 ms focus
   tempo, the 1.10 lift and the caption slide right. Everything else is that
   component in different arrangements.
3. **The on-screen keyboard early**, not last. First-run setup cannot be reached
   without it, and it is the gate on Phase 4 being testable at all.

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

**Order, from Phase 0's scoping.** The instinct is to build all twenty-one cores
and then find out. Do the opposite:

1. **One core, in CI, at a pinned SHA.** Gambatte — the smallest, pure C, no
   recompiler, no firmware. `make platform=unix`. This proves the whole
   pipeline: pin, build, package, load, run.
2. **One hardware-rendered core.** Flycast, because it is also Dreamcast and
   Naomi, and because it is the one that proves the GL context and the
   no-readback path.
3. **One backend-sensitive core.** pcsx_rearmed, built twice — `DYNAREC=0` and
   the Linux default — with a state written by each loaded by the other. That
   answers the parity question locally even if the Mac↔Apple TV test never
   happens.
4. **The other eighteen**, which by then are a loop.
5. **Dolphin and PCSX2 last**, as their own `.so` files, against upstream PCSX2
   rather than the ARM64 fork.

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
**Raised in the brief. RESOLVED in Phase 0: bundle directly, as `.so` files.**

Cabinet runs native cores **in-process**, which is the libretro shape.
RetroDECK is a curated set of *standalone* emulators behind ES-DE — someone
else's frontend, which constraint 4 rules out on its own, and a process-launching
model, which throws away the single overlay and single save-state path that
in-process buys (open question 12).

Phase 0 closed the remaining doubt. Every core has a working `platform=unix`
path producing `<core>_libretro.so` directly, so bundling is not merely
preferable, it is **less work than any alternative** — the Apple-only merge and
symbol-renaming apparatus disappears and nothing replaces it. See open question
13 and *The frontend toolkit* for the layout.

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

**Phase 0 confirmed that, with a number.** Reading both patch scripts: roughly
five of Dolphin's eight edit groups and nine of PCSX2's seventeen are Apple or
Metal walls that simply do not exist on Linux. The remainder are not port work at
all — they are the frontend claiming the audio, the input, the on-screen messages
and the present path, which it has to do on any platform. And the host layers
Cabinet wrote are already plain portable C++: 574 lines for Dolphin, 811 for
PCSX2, with only 155 and 681 lines of Objective-C++ beside them.

**One correction to this entry.** It says "in-process, all of them", and that
remains right about *who owns the frame loop* — but on Linux "in-process" should
not mean "statically linked". Each emulator becomes its own `.so`, loaded behind
the same struct of function pointers the libretro cores use, so the frontend
binary stays small and a core bump moves one image layer instead of all of them.
Same process, same frame loop, same overlay; different linkage. See *The frontend
toolkit*.

The remaining work is open question 13 — producing Linux builds of the same
cores — not an architectural choice.

### 13. Building the same cores for Linux x86-64
**Raised: Phase 1. Repo layout DECIDED. SCOPED IN PHASE 0, 2026-09-13 — most of
this is now answered. What remains is listed at the end and is small.**

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

#### Scoped in Phase 0 — what the code actually says

Checked 2026-09-13 by reading Cabinet's build scripts and fetching all twenty
upstream libretro Makefiles. **Verified** means read from the source;
**assumed** means reasoned and not run. Nothing here has been built — no Linux
toolchain has touched any of it, because nothing builds on this Mac.

##### Every core has a Linux path, and it is the best-tested path they have

**VERIFIED, all twenty Makefile-based cores.** Each one has a `unix` branch, and
each one **defaults to `platform = unix` when `uname` says Linux**. This is the
standard libretro Makefile preamble, and it is the configuration RetroArch's own
Linux builds ship — far more exercised than the `ios-arm64` and `tvos-arm64`
cases Cabinet uses.

Every `unix` branch produces `<core>_libretro.so`. **So the Linux build is not
just possible, it is the simplest target Cabinet has**: `make platform=unix`
yields the artifact directly, with no merge step, no symbol renaming and no
wrapper.

The three CMake cores — mGBA, Flycast, PPSSPP — all carry a `LIBRETRO` option
and Linux handling, and all three ship official Linux libretro cores upstream.
**Assumed** to build; not compiled here.

##### The ARM-assembly worry is inverted

**VERIFIED.** Five cores touch assembly at all: FBNeo, GW, pcsx_rearmed,
mupen64plus and picodrive. In every case the assembly is gated behind ARM
architecture detection and **is simply not compiled on x86-64**. FBNeo's is Vita
only. GW's `linux_x86_64` is an explicitly supported upstream platform.
picodrive's ARM cores (Cyclone, DrZ80) are selected by `ARCH` and give way to
their C equivalents (FAME, CZ80).

So no core is blocked by ARM assembly. **The real risk runs the other way**, and
it is the important finding of Part 1:

> **On Linux x86-64, several cores turn ON a recompiler that the Apple build has
> OFF.** A build that just says `platform=unix` inherits a different CPU
> emulation backend than the one Cabinet ships — silently, with no warning, and
> producing a core that is *better* but not *the same*.

**VERIFIED**, per core, from the Makefiles:

| Core | Cabinet on tvOS | Plain Linux x86-64 build | Lever |
|---|---|---|---|
| **pcsx_rearmed** | `DYNAREC=0`, pure interpreter | `DYNAREC=lightrec` — a real recompiler, plus `LIGHTREC_CUSTOM_MAP=1` | `DYNAREC=0` |
| **melonDS** | no `JIT_ARCH`, interpreter | `JIT_ARCH=x64` — the x86-64 recompiler | `JIT_ARCH=` |
| **mupen64plus** | `WITH_DYNAREC=` (empty), `-DNO_ASM`, GLES3 | `WITH_DYNAREC=x86_64` (needs **nasm**) and desktop `-lGL`, not GLES | `WITH_DYNAREC= FORCE_GLES3=1` |
| **picodrive** | `APPLE=1` forces `use_sh2drc=0` | SH2 recompiler **on** (32X, Sega CD) | `use_sh2drc=0` |
| **Flycast** | `-DTARGET_NO_REC`, interpreter | full x64 SH4 dynarec | omit / keep |

Cabinet's Mac build already pulls several of these levers the other way
(`DYNAREC=ari64`, `JIT_ARCH=aarch64`), so the mechanism is proven; only the
values differ.

**This is why the manifest as previously specified is not enough.** Repository
plus commit SHA does not describe a build. It must also record **the make
arguments and CMake flags**, or two builds of the same revision will differ in
the one dimension that matters.

##### And the source-level patches are part of the build too

**VERIFIED.** `build-core.sh` and `build-flycast.sh` patch upstream source
in-flight, and not all of those patches are Apple workarounds. Three change
behaviour and **must travel to Linux**:

- **Flycast `CPU_RATIO = 2`.** Upstream charges every interpreted SH4
  instruction 8 cycles, an effective 25 MHz, which is the direct cause of heavy
  scenes slowing down *inside the emulated machine*. Cabinet changes it to 2, an
  effective 100 MHz. Both neighbouring values were measured on device and
  rejected. **On a Linux build with the dynarec on, this constant is not used at
  all** — which is a behaviour difference between platforms that nobody has
  reasoned about yet.
- **melonDS's missing unload flush.** Upstream's `retro_unload_game` never
  flushes, so a save made less than two seconds before quitting is dropped —
  and save-then-quit is exactly how people leave a game. Cabinet inserts the
  flush. Linux needs it identically.
- **VeMUlator's `strchr` → `strrchr`.** Finds the extension at the last dot
  rather than the first, so a dot anywhere in a parent directory name does not
  silently load the card as nothing. (VeMUlator is iOS-only, so this one does not
  travel — but it is the same class of patch.)

The Apple-only patches — melonDS's `MAP_JIT` W^X and fastmem bracketing,
pcsx_rearmed's 16 KB page-size fix, Flycast's `JITWriteProtect` dlsym bypass,
the Beetle PCE `zutil.h` `fdopen` fix — **all disappear.**

##### The prefix-and-merge apparatus disappears entirely

**VERIFIED, and confirming what this question guessed.** Cabinet merges each
core into one relocatable object exporting only `<prefix>_retro_*` forwarders,
because Apple's toolchain ships no object-file symbol renamer and only one core
can carry the standard names. On Linux, one `.so` per core `dlopen`ed with
`RTLD_LOCAL` gives that isolation for free.

Gone with it: `bsat_wrapper.c` and its eighteen `sed`-derived copies, the
`ld -r` merges, `-exported_symbols_list`, `ar rcs`, and the `-fno-common`
compiler shim that exists only because Apple's linker cannot localise common
symbols. That is most of `build-core.sh`'s length and nearly all of its
subtlety.

##### The two heavy emulators are the easy half, not the hard half

This document assumed Dolphin and PCSX2 would be the long pole. **They are the
only two things already solved.**

**VERIFIED:** both are **pinned to an exact commit and asserted at build time** —
Dolphin at `a1e636d`, PCSX2 at `c89cb8ae`. `build-dolphin-mac.sh` re-reads
`git rev-parse` and fails if the tree has moved. That is precisely what the core
manifest is supposed to do, already implemented, for the two cases it was assumed
would be hardest.

Their patch scripts classify cleanly:

| | Apple walls that vanish | Seats for the frontend that must be re-cut |
|---|---|---|
| **Dolphin** (8 groups, 19 edits) | Catalyst JIT W^X; libusb `IOServiceAuthorize`; AGL/NSOpenGL; the Quartz input backend; `NSScreen` HDR headroom | where `Sys` lives; audio out; `Pad::GetStatus` input |
| **PCSX2** (17 groups) | Catalyst JIT; CocoaTools AppKit; the Metal renderer's AppKit corners; the Metal present path (3 groups); EyeToy/AVFoundation; the Homebrew/FFmpeg leak; libwebp archive split | the host layer itself; audio; input; on-screen messages; the present guard, in whatever form Vulkan needs |

Roughly **five of Dolphin's eight** and **nine of PCSX2's seventeen** are Apple
or Metal walls that do not exist on Linux. The rest are the frontend claiming
the loop, the audio, the input and the OSD — which it must do on any platform,
and which is *the same work* rather than a port.

And the host layers themselves are already portable: `CabinetDolphinHost.cpp`
(574 lines) and `CabinetPS2Host.cpp` (811), plus their C bridges, are plain C++.
Only the `.mm` files — 155 lines for Dolphin, 681 for PCSX2 — are Apple-specific,
and they are audio, Cocoa shims and the Metal drawable probe.

> **One deliberate divergence from "the same source".** PCSX2 is pinned to the
> **isztldav fork**, which exists solely to add ARM64 JIT recompilers that
> upstream stubs out on Apple Silicon — about 23,000 lines of new arm64 emitter,
> which the fork's own README says was translated with LLM help. **On x86-64
> that fork buys nothing**, and upstream PCSX2's x86-64 recompiler is the
> original, first-class one. CabinetOS should track **upstream PCSX2**, not the
> fork. Record it as an exception in the manifest with this reasoning next to
> it, rather than letting it look like drift.

##### The urgent finding: Cabinet's core build is not reproducible by anyone

**VERIFIED, and this is more serious than the missing SHAs.**

1. `spikes/` is **gitignored**. The per-core source checkouts live only on the
   machine that built the shipping archives.
2. `build-core.sh` derives every core's wrapper by `sed`-ing
   `spikes/BeetleSaturnStatic/bsat_wrapper.c` — a file its own comment describes
   as *"hand-written, not part of the cloned repo."* It is not in the
   repository. **`tools/build-core.sh` cannot run on a fresh clone of Cabinet.**
3. The committed `.a` archives embed no revision. Checked with `strings` across
   several: nothing. The revisions are **not recoverable from the artifacts.**

So the current core revisions exist in exactly one place: the working trees on
one Mac. If that machine is lost, parity cannot be established against what
Cabinet ships today — only re-established from a fresh pin, which invalidates
every save state made so far.

##### RECOVERED, 2026-09-13 — and it found a live bug

The recovery was run on the build Mac the same day. `core-manifest.json` now
exists in Cabinet (not yet pushed at the time of writing). CabinetOS consumes it
once it lands; do not keep a copy here, it would drift.

It did not merely record what was there. **It found that Cabinet is shipping
different revisions of the same core to different apps, right now.**

**Eleven of twenty-three cores diverge between iOS and macOS.** In every case
macOS is newer, by between one day and eleven weeks:

| Core | iOS | macOS | Drift |
|---|---|---|---|
| prosystem | 2026-06-04 | 2026-08-22 | **11 weeks** |
| picodrive | 2026-07-29 | 2026-08-20 | 3 weeks |
| beetle_vb | 2026-07-29 | 2026-08-23 | 3.5 weeks |
| fceumm | 2026-07-28 | 2026-08-22 | 3.5 weeks |
| gambatte | 2026-07-31 | 2026-08-21 | 3 weeks |
| beetle_pce_fast | 2026-07-31 | 2026-08-28 | 4 weeks |
| pcsx_rearmed | 2026-08-02 | 2026-08-27 | 3.5 weeks |
| genesis_plus_gx | 2026-08-07 | 2026-08-28 | 3 weeks |
| snes9x | 2026-08-08 | 2026-08-16 | 1 week |
| mame2003_plus | 2026-08-19 | 2026-08-28 | 1 week |
| beetle_saturn | 2026-08-10 | 2026-08-11 | 1 day |

The other twelve are aligned across every platform they ship to: beetle_ngp,
Dolphin, FBNeo, Flycast, GW, melonDS, mGBA, Mupen64Plus, Opera, PCSX2,
PPSSPP, Stella2014, vecx, VeMUlator.

The cause is visible in `build-core.sh` and is exactly the mechanism this
document predicted: a separate checkout per platform, each cloned `--depth 1`
whenever that platform was *first* built. macOS support landed most recently, so
its trees are the freshest.

**And eleven of the twenty-one tvOS revisions are gone.** Those per-platform
checkouts no longer exist on the machine, and the archives embed nothing:
beetle_ngp, beetle_pce_fast, beetle_saturn, fceumm, gambatte, genesis_plus_gx,
mGBA, pcsx_rearmed, picodrive, prosystem, snes9x.

Put those two facts together and the live consequence is this:

> **For eleven cores, the compatibility of a save state between Cabinet's Mac
> and its Apple TV is unknown today and cannot be made known**, because the
> revision one side was built from no longer exists anywhere.

##### What the manifest decided, and the cost it commits to

`core-manifest.json` chooses a `pinned_commit` per core — the macOS revision
wherever there was a choice, on the grounds that it is the newer of the two in
use and has been play-tested there. **That is the right call**, and it makes the
lost tvOS revisions stop mattering, because they are being replaced rather than
matched.

It is not free, and the cost should be taken deliberately rather than
discovered:

- **Eleven cores get rebuilt for iOS and tvOS at revisions never run on those
  platforms.** That is a real regression surface arriving all at once. It wants
  its own release, not a change mixed in with others, and it wants
  `docs/core-quality-pass` re-run afterwards.
- **Existing Apple TV and iPhone save states for those eleven cores may not
  survive it**, and nobody can check in advance. That is acceptable *now* —
  alpha, one user — and will not be acceptable once there are people with save
  histories. **This is the last cheap moment to do it.**

##### The hole that is still open, and it is in the worst possible core

The manifest records, in Flycast's own patch entry:

> `"where": "tools/build-flycast.sh, and UNSCRIPTED edits in the working tree"`

**So the pin does not reproduce the shipping Flycast.** Commit `a172e000` plus
`build-flycast.sh` gives something, but not what is in the app, because there
are hand edits in that tree that no script applies.

Flycast is the worst core for this to be true of. It is Dreamcast *and* Naomi,
it is the heaviest system that is not PS2 or GameCube, and it is one of only two
cores that can answer the save-state parity question cleanly.

The Flycast patch inventory is also the thinnest in the manifest — one vague
entry covering `CPU_RATIO=2` *"plus recompiler/W^X and SH4 changes"*, with
`build-flycast.sh`'s own `first_run` and VMU-screen-callback patches not listed
separately at all.

**Before anything else touches that tree:**

```
git -C spikes/cores/flycast/src diff > tools/patches/flycast-unscripted.patch
git -C spikes/cores/flycast/src status --porcelain
```

Commit the patch file, then either fold it into `build-flycast.sh` or apply it
from the file. Until that exists, Flycast cannot be rebuilt — on any platform,
including the ones that ship today.

##### And `bsat_wrapper.c` is still not in the repository

`build-core.sh` derives every core's wrapper by `sed`-ing
`spikes/BeetleSaturnStatic/bsat_wrapper.c`, which is gitignored and
hand-written. `build-flycast.sh` carries a complete equivalent inline as a
heredoc, so this is a copy rather than a rewrite — but until it is done,
`build-core.sh` cannot run on a fresh clone.

#### The core manifest, revised

Supersedes the sketch above. Per core:

| Field | Why |
|---|---|
| `repo` | upstream URL |
| `commit` | exact SHA, **checked out and asserted**, never `--depth 1` of `HEAD` |
| `systems` | which RomM platforms it serves |
| `platforms` | which targets it is built for |
| `build.<platform>` | **the make arguments or CMake flags**, per platform — this is the new field, and the table above is why |
| `patches` | which in-flight source patches apply, and to which platforms |
| `notes` | deliberate divergences, like PCSX2's fork |

It belongs in **Cabinet**, because Cabinet is the app that ships and the source
of the constraint. CabinetOS reads it, builds the same revisions with the
recorded flags, and asserts at build time. A mismatch becomes a failed build
rather than a save state that silently will not load.

#### The test that answers the whole question, and can be run this week

The parity risk is not theoretical and it does not need CabinetOS to exist to
measure. **Cabinet already ships two different configurations of the same core.**

For the experiment to mean anything the two builds must differ *only* in the CPU
backend — same commit, same patches. The recovered manifest says which cores
qualify, and it narrows the field:

| Core | Same commit everywhere? | tvOS | macOS | Usable? |
|---|---|---|---|---|
| **melonDS** | yes — `66b5d263` | interpreter | `JIT_ARCH=aarch64` | **clean** |
| **Flycast** | yes — `a172e000` | `-DTARGET_NO_REC` | recompilers on | **caveat** |
| pcsx_rearmed | **no** — and tvOS revision lost | interpreter | `DYNAREC=ari64` | confounded, drop it |

> **Write a save state on the Mac and load it on the Apple TV, for melonDS and
> Flycast.** If it loads, CPU backend does not affect state format, and
> CabinetOS can take the faster Linux defaults — proper recompilers for
> Dreamcast, DS and PS1 rather than interpreters, which on Vega integrated
> graphics is the difference between comfortable and marginal. If it does not,
> every core must be built with Cabinet's exact backend, and that becomes a hard
> line in the manifest.

**melonDS is the clean experiment.** One tree, one revision, all four patches
applied to every platform, and the only difference is the recompiler.

**Flycast carries a caveat** and it is the unscripted-edits problem above: both
platforms build from one shared checkout, so if a hand edit was made *between*
the tvOS build and the Mac build, the two came from different tree states and
the result means nothing. Capture that diff first. If the answer from melonDS
and Flycast disagree, believe melonDS.

That is a one-evening test on hardware the project already owns, it needs no
Linux toolchain, and it is the highest-value thing anyone can do for Phase 5
right now. **Until it is run, assume states are backend-sensitive and match
Cabinet's flags exactly.**

#### Still open after Phase 0

- **Flycast's unscripted working-tree edits.** The most urgent item on the
  project, and it is Cabinet-side. Until that diff is captured, Flycast cannot
  be reproduced on any platform, CabinetOS included.
- **The manifest has no `linux` row, and several `build_args` are null.** The
  nulls are honest — `build-core.sh` passes no extra make arguments for those
  cores, so the core's own Makefile platform case decides. **But the Linux case
  decides differently**, and picodrive and Mupen64Plus are exactly the two where
  a null reads as "nothing to match" while the Linux default quietly turns on a
  recompiler (`use_sh2drc`, `WITH_DYNAREC=x86_64`). When CabinetOS adds its
  rows, every one of them must be explicit — never null — even where the value
  is "the default".
- **The save-state backend question above.** Untested. Blocks nothing until
  Phase 5, but shapes the manifest.
- **Realigning the eleven diverged cores.** Cabinet-side, its own release, with
  the core quality pass re-run. Best done before there are users with save
  histories.
- **The `emulator` tag carries no version.** See *How Cabinet hosts cores*. A
  state written by a mismatched build is offered as loadable, because the tag
  cannot tell. Either the builds are genuinely identical, or the tag grows a
  build identity — and that is a **Cabinet-side change**, since Cabinet writes
  the tag today.
- **Nothing has been compiled.** Everything above is read from source. The first
  real signal is a CI job that builds one core — Gambatte, the smallest — with
  `make platform=unix` at a pinned SHA. Do that in Phase 5 before the other
  twenty.
- **Firmware.** Cabinet fetches every firmware file a platform lists from RomM.
  CabinetOS inherits that, but PSP is a special case: PPSSPP's system files ship
  *inside the app bundle*, not from RomM. In a bootc image they become a path in
  `/usr`, which is fine, but it is a thing to remember rather than discover.

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
