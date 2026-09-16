# CabinetOS

> **Read this file first.**
> This is the living specification for CabinetOS. Every session working on this
> repository should read it before touching anything else, and should update it
> when decisions are made, phases change status, or new uncertainty appears.
>
> **Starting a session?** `docs/NEXT-SESSION.md` is the short version — what
> state things are in, what to pick up, and the things that waste a day if
> nobody says them. This file is the specification and is authoritative; that
> one is the handover. Rewrite it at the end of a session.
>
> **`docs/CABINET.md` is the companion to this file.** Cabinet already ships on
> iOS, tvOS and macOS and has answered most of what CabinetOS runs into. Read it
> before designing anything, and read Cabinet's own source before inventing an
> answer — it is not checked out here, so clone it. Doing this piecemeal, a
> question at a time, produced several half-right answers that had to be
> corrected.
>
> Rules for maintaining this document:
> - When a phase is finished, change its status and say what actually shipped,
>   not what was planned.
> - When you are unsure about something, add it to **Open questions**. Do not
>   guess and do not quietly resolve an open question without saying so.
> - When a decision is reversed, record the reversal rather than editing history.

---

## Where the project is — 2026-09-16

**Phase 0 complete. Phase 1 complete. Phase 2 mostly done. Phase 3 well under
way and running. Phase 5 started early and the hardest question in it is
answered.**

### The thing that matters most

**Save states are portable between Cabinet and CabinetOS.** Proved, not
reasoned about: Gambatte built for Linux x86-64 at the same commit Cabinet's
macOS build is pinned to, then each platform loading the other's state and
producing an identical digest — with a same-platform control run to make it a
result rather than a coincidence. The emulation is bit-identical across
architectures for twenty-five seconds of video and audio.

That is the premise the whole product rests on, and it was the largest unknown.
See Phase 3's notes and open question 13.

### What runs today

The frontend is a real program on the test VM, booted into by the session
rather than launched by hand:

- C++20, SDL3, one EGL/GLES 3 context. No toolkit. `frontend/`.
- The design system's focus treatment, motion, canvas and type ramp, verified
  identical at 4K, 1080p and 720p.
- Text (Noto Sans, with CJK fallback), cover art (async, budgeted, evicting),
  and **frosted glass**.
- **An on-screen keyboard**, which was the gate on everything downstream.
- **A libretro core host** that loads a `.so`, paces it against the wall clock,
  plays its audio, draws its picture, and saves and restores its state.
  **Dr. Mario runs.**

### Open against the frontend right now

- **A white line reported under the keyboard's title, not reproduced.** Every
  row between the title and the field was scanned in the captured framebuffer
  for a bright horizontal run and there is none; the strongest edge there is the
  field's own top boundary, which can only darken. So it is either a VNC scaling
  artefact or something the capture does not see. **Ask before chasing it.**
- **Horizontal wrapping is in, vertical is not.** Judge it with a pad.

### What is still unknown, honestly

- **Nothing has been judged on a television.** Motion, the letterbox glow and
  the safe area are all recorded as needing the SER5, which is not yet
  installed. A software-rendered VM cannot answer any of them.
- **Twenty cores of twenty-one are built**, and all four backend-sensitive ones
  are settled: pcsx_rearmed and melonDS take the recompiler and share Cabinet's
  tag, picodrive matches Cabinet's flags exactly, and Flycast is blocked on
  Cabinet's own unscripted edits rather than on anything here. **1100 of 1644
  games are playable.** PPSSPP is the one not built.
- **Two of the twenty cannot be RUN here**, whatever the build says: Flycast and
  Mupen64Plus render through a GL context the frontend does not yet hand over.
  `catalog::coverageFor` says so rather than offering a game that would fail.
- **Every core builds in CI**, on a GitHub runner from a bare checkout, with the
  finished `.so` asserted to report the pinned revision as its own version
  string — see open question 13.
- **No controller has ever been attached.** The permissions chain is verified
  by reading; a real pad is not.

### The Cabinet-side debts this project has found

1. **Flycast carries unscripted edits** in its working tree, so its pin does not
   reproduce what ships — for Dreamcast and Naomi. Capture that diff before
   anything touches the tree.
2. **Eleven of twenty-three cores ship different revisions to iOS and macOS**,
   and eleven of the twenty-one tvOS revisions were recorded as unrecoverable.
   `core-manifest.json` pins each forward, which is right and far cheaper now,
   in alpha, than once anyone has a save history. **Two of the eleven have since
   been recovered straight out of the shipping archives** — see open question 13
   — and the same trick probably works on several more.
3. **mGBA's Mac archive reports `e31759b24-dirty`**, so that build carries a
   working-tree modification no script applies, in a core whose manifest entry
   lists no patches at all. Same shape as Flycast's, found the same way.

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

#### Cores and platforms are not the same list

Read from `NativeCore.swift` 2026-09-13, after this document got it wrong once.
**26 platforms, 22 cores**, and the mapping runs both ways:

| Core | Platforms it serves | |
|---|---|---|
| **Genesis Plus GX** | Genesis, Sega CD, Master System, Game Gear | **4 for one build** |
| **Gambatte** | Game Boy, Game Boy Color | 2 |
| **Beetle PCE Fast** | TurboGrafx-16, TurboGrafx-CD | 2 |
| **FBNeo** *and* **MAME 2003-Plus** | Arcade | **2 cores, 1 platform** |
| every other core | one platform each | |

Note what PicoDrive does *not* cover: it is Sega 32X alone. Genesis, Master
System and Game Gear are Genesis Plus GX. Reading the core list as a platform
list gets that backwards.

Three consequences, and the third shapes the code:

1. **One build can light up four platforms.** Genesis Plus GX is the best value
   per build in the set, which matters when ordering Phase 5.
2. **Wiring a core once does not mean every platform under it works.** The
   reference implementation's own rule, learned by losing saves: *wire it per
   core, confirm it per platform.* Genesis Plus GX exposes cartridge save RAM
   through the standard call for Genesis, Master System and Game Gear — but Sega
   CD's internal backup RAM is a separate file the core writes itself. Beetle
   PCE Fast does something for CD games and nothing at all for HuCards, which
   have no save hardware.
3. **Configuration is keyed by PLATFORM, not by core.** Verified:
   `NativeCoreOptionsStore.dictionary(for: platform)`,
   `padDevice(for: platform)`, `platform.supportsSecondPlayer`. The same core
   binary gets a different option table and a different controller device type
   depending on which system it is being asked to be — Sega CD forces
   `cart_size`, Saturn forces its save method, 32X gets its own pad type.

   **CabinetOS must key its own configuration the same way.** A
   `core -> settings` map would be wrong by construction, and expensive to
   unpick once a settings UI exists on top of it.

So the Phase 5 target is **21 libretro cores plus Dolphin and PCSX2 — 23 builds
— covering 27 platforms.**

#### And only three of them need a GPU

Also verified from the frontend's own source: exactly **three cores ever ask for
a graphics context** via `RETRO_ENVIRONMENT_SET_HW_RENDER` — **Flycast**,
**Mupen64Plus** and **PPSSPP**. The frontend's own comment calls the rest "the
twelve software-rendered cores", counting the set it had at the time.

Everything else hands over a finished pixel buffer, including several that look
like they should not: melonDS does its 3D on the CPU with a threaded rasteriser,
Opera is fully software, and vecx is deliberately built with its GLES path
compiled out (`HAS_GPU=0`).

That is why the first core to build is a software one: it needs no hardware
render callback, no shared context, no FBO, and no readback. Those exist only
for three cores and can wait until one of them is the target.

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

**And keeping is an action on the GAME, not only a row in Settings.** Added
2026-09-16 at Marcus's prompt, and it matches what Cabinet already ships — a
per-game toggle, with the size shown, removable from the same place it was
added. Settings is where you go to see the whole picture; the game's own screen
is where the decision is actually made.

**It has to work on a game that has never been played**, which is the case that
matters most and the one a promote-from-cache model misses entirely: browsing
the library, picking something for later, and having it there when you come
back. On a game already in the cache it pins what is there; on one that is not,
it is a download that stays. Cabinet calls this *Keep on device*; the word on
the button here should be whichever of **Download** or **Keep** reads better on
a television, and that is a Phase 4 wording decision rather than a design one.

**Kept games are what shrinks the cache**, since the cache is simply whatever
space is left over — see the cache policy in Phase 4.

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

### Shaders, and the glow around the picture

Missed on the first Phase 0 read and added 2026-09-13 at Marcus's prompt. Not
needed to get a core running, and very much part of what the product looks like.

**Eleven shaders**, one Metal fragment function each, with one pipeline built
per shader at attach — *"picking a shader in the pause menu is a dictionary
lookup, not a recompile."*

| | |
|---|---|
| `sharp` | "None" — unfiltered, the default |
| `sabr` | a scaler, offered everywhere |
| `crtAperture`, `crtEasymode`, `crtMattias`, `crtBeam`, `crtCaligari`, `crtGeom` | six CRT looks |
| `lcd` | a generic LCD grid |
| `gameBoy` | the dot-matrix look, built for that specific screen |
| `vmuLCD` | offered in **no** menu; set directly by the VMU player |

**They are gated per platform, and that gating is the interesting part.** The CRT
shaders simulate a television, and a Game Boy was never displayed on one — so
handhelds (GB, GBC, GBA, Game Gear, NGPC, DS) drop all six CRTs and get one
real-screen shader instead. It cuts both ways: consoles drop the handheld
shaders, *"since a PS1 game offering a Game Boy dot-matrix was the same mismatch
in the other direction."*

**And the choice is stored per PLATFORM, not per core** — with the source calling
out exactly the bug this document already records: Genesis Plus GX serves four
platforms, and a shader picked for Genesis was silently carrying into Sega CD,
Master System and Game Gear. That is the third independent confirmation of the
platform-keying rule, and it should settle it.

A stored value for a shader a platform no longer offers falls back to None
rather than being trusted.

**The history is worth keeping, because it is a warning.** The original six came
from RomM/EmulatorJS's own bundled set. Two ScaleHQ scalers and a `crt-geom`
slang port *"looked bad enough in this Metal port that Marcus dropped them on
sight"*. A shader that is well regarded elsewhere is not automatically good once
reimplemented — judge each on the panel.

#### The letterbox glow

Separate from shaders, and **a television feature specifically**: tvOS and Mac
compile it, iOS does not, because *"the phone's screen has no dead space worth
lighting."*

It lights the dead area around the picture, ramping out from the game's edge to
the physical edge of the screen. Three settings, and the numbers are not round:

| | Peak white opacity at the picture's edge |
|---|---|
| Off | 0 |
| Subtle | **0.025** |
| Strong | **0.04** |

Reach is fixed at 100% — the ramp always travels the whole dead space. A
separate reach control was built and then dropped once 100% proved to be the
only value worth having.

**CabinetOS needs this more than Cabinet does, not less.** An integer-scaled
Game Boy picture on a 4K television is a small bright rectangle in a very large
black field — which is precisely the case the glow exists for, and it is the
normal case here rather than an edge one.

**And the tuning lesson is the one this project keeps relearning.** Two sets of
preset values guessed from a mockup were both wrong on real hardware — bleeding
into the picture, banding, not reaching the edge. A continuous slider on a real
panel found the numbers, and only then were presets chosen. Do not guess these;
build the slider.

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

**And CabinetOS made it anyway, 2026-09-14.** The first game launched from the
library was played with the arrow keys, and those arrows moved the Tetris piece
*and* shifted focus on the Home screen behind it — so leaving the game would
have landed on something nobody chose. Same bug, different platform, found the
same way: by someone actually playing it rather than by reading the code.

The fix is one `InputOwner` asked once per event — Keyboard, Game, or UI — and
not a `!playing` check added at each call site, which is the shape this section
warns against. Worth noting *why* the bug survived a careful read: the core's
input is polled per frame from `SDL_GetKeyboardState`, while the UI's comes from
the event queue. Two different mechanisms, so nothing in either one looks wrong
on its own, and only the rule catches it.

---

## Controls: what CabinetOS inherits, and what it cannot

Raised 2026-09-13. The input *model* in *What CabinetOS is* does not change —
the controller is required, keyboard and mouse are supported and never needed.
What changes is the machinery underneath, and it is not a port of Cabinet's.

Checked on the running image rather than assumed.

### The rules that carry over unchanged

- **The controller belongs to the core, or to the UI, never both.** On tvOS this
  was forced: the focus engine kept consuming presses, so B read as "go back"
  and dismissed the player instead of reaching the core. On Linux nothing fights
  us — but the rule stands, because the same button still has to mean two things
  at two times, and any design where it means both at once is the same bug.
- **Mapping is ours, not the user's**, per *Emulation*.
- **The libretro device type is per PLATFORM, not per core** — a 3-button versus
  6-button Genesis pad is `retro_set_controller_port_device`, not a core option.
  See *Cores and platforms are not the same list*.

### What Linux gives us that tvOS could not

**Verified present in the image**, all of it from Bazzite and none of it ours to
maintain:

| | |
|---|---|
| `gcadapter_oc` | The official **GameCube adapter** — four real GC pads. Dolphin's native input, on the machine that runs Dolphin. |
| `hid-playstation`, `hid-nintendo`, `hid-sony`, `hid-steam` | DualSense, DualShock, Switch Pro, Steam Controller, in-kernel |
| `xone_*`, `xpad` | Xbox wired and wireless, including the dongle |
| `hid-fanatec`, `hid-t150`, `hid-tmff-new`, `hid-logitech-new` | Four force-feedback **wheel** drivers |
| `psxpad-spi` | Real PlayStation pads over SPI |

And the one that matters most for a cabinet: **real spinners, trackballs, dials
and light guns are just input devices here.** MAME 2003-Plus is in the set
specifically for the early-80s boards whose controls were exactly those, and
Cabinet can only offer them as a touchscreen approximation. CabinetOS can take
the real thing. That is a capability the reference implementation does not have
and cannot get.

Also: **Steam is gone, so Steam Input is not in the way.** Pads arrive raw and
the mapping is entirely ours.

### What is harder here, and none of it is optional

1. **One controller can appear as several devices.** A DualSense over Bluetooth
   presents its gamepad, its motion sensors and its touchpad as separate evdev
   nodes. Enumerate naively and a console with one pad says three are connected.
   SDL's gamepad layer collapses most of this; it does not collapse all of it,
   and the settings screen must show what a person recognises as *their
   controller*, not a device list.
2. **Unknown controllers exist.** tvOS took a curated list. Linux takes anything
   that speaks HID. SDL3 carries a large built-in mapping database, but a pad it
   has never seen produces a working device with meaningless buttons. **Cabinet
   never needed a remapping screen; CabinetOS does** — and it has to be usable
   with the very controller whose buttons are wrong, which means driving it by
   position ("press the button below the others") rather than by name.
3. **Player assignment is ours.** Apple hands out `playerIndex`. Here, which pad
   is player one is a decision, it has to be visible, and it has to survive a
   controller sleeping and reconnecting mid-session. Four-player arcade and
   GameCube make this real rather than theoretical.
4. **Bluetooth pairing is ours to build.** tvOS had Apple's own Settings; bluez
   over D-Bus is the equivalent and there is no UI for it in the image. It has a
   chicken-and-egg at the centre: **the first controller cannot be paired using
   a controller.** The answer is that a pad connected over USB works
   immediately, and pairing is reachable from there — which must be *said* in
   first-run setup rather than left to be discovered.
5. **Rumble quality varies by driver**, and there is no Taptic Engine to fall
   back to the way Cabinet has on a phone.

### The permission detail, and a Phase 2 decision that paid for itself

Gamepads are the one input the frontend reads **directly from `/dev/input`**;
keyboard and mouse arrive through Wayland from the compositor. Different path,
different failure mode, and it is worth knowing which is which when something
does not respond.

`/dev/input/event*` is `root:input` mode `0660`, and the session user is **not**
in the `input` group. Access comes from an ACL instead:

```
SUBSYSTEM=="input", ENV{ID_INPUT_JOYSTICK}=="?*", TAG+="uaccess"
```

systemd-logind applies that ACL **to the active session on the seat**. The
CabinetOS session is `Seat=seat0`, `Active=yes` — verified — because Phase 2
gave it `PAMName=login` and a real logind session rather than running it as a
bare service.

**So a Phase 2 decision is what makes controllers work at all in Phase 5.** Had
the session been a plain unit with no PAM session, every gamepad would be
unreadable and it would present as a controller bug rather than a session bug.
Recorded here so that nobody "simplifies" the unit later and spends a week on it.

*Not yet verified:* no gamepad has been attached to the VM. The rule and the
seat are confirmed; the ACL actually appearing on a real pad is not.

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

##### Resume on a game that is not downloaded — RAISED AND CLOSED, 2026-09-16

**Recorded because it looks like a problem and is not, and somebody will raise
it again.**

Recent and the hero come from RomM's `last_played`, which is the household's
history across every device. So the game Home offers to resume may have been
last played on a phone and never downloaded on this console, and pressing Resume
then means fetching several gigabytes and a save state before anything starts.

That was argued here as a broken promise — a wait behind a button whose purpose
is that there is no wait — with three proposed fixes: a progress bar inside the
pill, pre-fetching the hero while idle, and a badge saying which kind of Resume
was coming.

**Closed by Marcus, and he is right.** You press Resume, it downloads, it plays.
The wait is the wait whichever way it is presented, and the download already
shows progress and already takes Escape to cancel. Warning someone in advance
does not shorten it and does not change what they would do — they want to play
that game.

**The rule this section states is about the number of ACTIONS, not the number of
seconds.** Resume is still one action. It is slower some of the time.

*Pre-fetching the hero while the console is idle remains available* as an
optimisation, the way real consoles have it, and it is a performance idea rather
than a correction to this design. Nothing about Home changes.

#### The hero card, read from Cabinet's tvOS source

**Read 2026-09-14 from `RommApp/RommApp/Home/HomeView.swift`.** tvOS has shipped
this and CabinetOS should inherit it rather than re-derive it. The numbers below
are that file's, and the reasoning next to them is its own.

**The tvOS composition** (`tvContent`) — this is the one to follow, not the Mac
variant:

```
VStack(spacing: 16), padding: horizontal 60, top 0, bottom 16
    hero          height = min(screenHeight * 0.40, 420), wide, padding-bottom 20
    Recent        a shelf, only when there are recents besides the hero
                  else, when loaded and there are none: the empty state
    Favorites     a second shelf, only when there are any
```

**The hero card itself:**

| Part | Treatment |
|---|---|
| Artwork | **Fitted, not filled** — box art is tall and the hero is wide, so filling slices the art to a strip of its middle |
| Backdrop | The *same* artwork, filled, **blurred 20**, with black at 15% over it — so the leftovers are the art's own colours rather than letterbox bars |
| Art inset | `padding-top 14`, keeping the fit image off the card's rounded top corners, which otherwise clip a sliver |
| Band | A **frosted material**, not a black gradient — the gradient painted over the very backdrop that makes the card worth looking at |
| Band content | Title (headline) over platform label (caption), spacing 2, padding h12 v10 |
| Band height | Computed from the two line heights + 2 + vertical padding, not hardcoded |
| Corner radius | 18 |
| Resume pill | Overlaid **top-trailing**, inset 12; capsule, ultra-thin material, play glyph + "Resume", min-width 92, padding h14 v8 |

**The two actions are load-bearing and must not collapse into one.** The pill
goes *straight into the game*, with the previous choices made and the newest
state loaded. The artwork opens the detail screen, which is where a different
state, a different core or an export is chosen. Cabinet's own comment: stopping
at a screen with a Play button on it is two actions, not one.

##### The hero height is a hard-won number, and it carries a warning

The comment above `min(height * 0.40, 420)` records the iteration, and it is
worth reading before anyone "tidies" it:

- **0.42 / 460** pushed Recent's caption past the bottom edge on a 1080pt screen.
- **0.34 / 380** *still* cut it off on real hardware.
- **0.28 / 300** fit with margin to spare.
- **0.34 / 360** left a visible gap below Recent's caption.
- **0.40 / 420** is where it landed.

The reason the second attempt failed is the part CabinetOS must take seriously:

> *"a physical TV's overscan safe area eats more vertical room than the
> simulator's raw framebuffer capture shows."*

**That is exactly the trap this project's VM is set up to fall into.** The
standing rule is "judge no motion on the VM"; this widens it. A `--screenshot`
from a software-rendered VM will overstate the vertical room available on a real
television in the same way the tvOS simulator did. **Vertical fit is not
answerable on the VM either** — only on the SER5, on a real panel.

##### The empty state is the first thing to build, because it is today's truth

Home is resume-first and nothing has ever been played, so there is nothing to
resume. Cabinet's tvOS copy, written for a television rather than reused from
the phone — its own comment notes that "on the go" means nothing on a TV:

> **Nothing to resume yet**
> Pick something from Library and it'll be here next time.

Centred, title2 bold over title3 secondary, minimum height 300. This is the
honest Home until a play history exists, and it is buildable now.

**The Mac variant differs and is not the model**: hero `min(h * 0.34, 320)`,
spacing 14, top padding 24. Cabinet's own note asks that the two be kept
structurally in step while the scale differs. CabinetOS is a ten-foot interface,
so it follows tvOS.

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

**Text, added the same day.** FreeType rasterises glyphs on first sight into one
greyscale atlas; strings are drawn as textured quads on the same path cover art
and a running core's frame will use.

- **The font is Noto Sans**, Regular / Medium / SemiBold / Bold, **already in
  the Bazzite base**, so the type ramp costs the image nothing. `ci/base-watch.txt`
  should gain `google-noto-sans-fonts`, since the frontend now depends on it.
- **Noto Sans CJK is the fallback**, also already present. A ROM library is full
  of Japanese titles and Noto Sans has no CJK coverage; a missing glyph walks
  the fallback list rather than drawing a box. Verified with a Japanese title in
  the shelf.
- **Glyphs are rasterised at device pixels and laid out in design points.** A
  31pt caption is 31 pixels tall at 1080p and 62 at 4K, and the atlas is keyed
  by device size, so the same label at two scales is two entries. Rasterising at
  the design size and letting the GPU scale would make 4K text a blurry upscale
  of 1080p text, which is exactly what a console must not look like. **Verified:
  1.0 design point of edge softness at both 4K and 1080p** — an upscale would
  show two.
- **Baselines snap to a device pixel.** A baseline landing on a half pixel makes
  a whole line slightly soft, which on a television reads as cheap rather than
  as antialiasing.
- Long titles truncate with a real ellipsis (U+2026, one glyph).

**Cover art, added the same day.** libjpeg and libpng — both already in the
base, so still nothing bundled and nothing vendored.

- **Decoding never touches the frame thread.** Four worker threads decode; the
  GL upload happens on the frame thread because GL is not thread-safe; a cover
  that is not ready simply is not drawn yet. A shelf is dozens of JPEGs and
  decoding one on the thread that drives a core is how a console stutters.
- **There is a memory budget, and it is tested.** 192 MB by default, evicted
  least-recently-used. **Verified**: with a 1 MB budget the covers scrolled past
  are evicted and the ones on screen are kept, so resident settles at the
  working set rather than at the budget. That is deliberate — **the budget is a
  target, not a hard cap**, because flashing a blank card to honour a number is
  the wrong trade.
- **Where the bytes come from is one `std::function`.** Files today,
  authenticated RomM requests in Phase 4, and the cache never learns what a
  server is. Keys are not paths — everything after `#` is stripped — so a real
  URL with a query string is already the shape it expects.
- **Odd-shaped covers are fitted over a blurred echo of themselves**, never
  cropped, with a black 18% scrim between. That is the reference
  implementation's own rule and its own threshold: more than 0.06 away from 3:4.
  The echo is a high mip level sampled back up and scaled 1.3, which is a box
  blur for the price of a texture fetch rather than a blur pass.
- **Everything is clipped to the card's rounded corners** — the echo included,
  which is why the clip rectangle is a separate thing from the drawn rectangle.
- Format is detected from magic bytes, never a file extension: a server hands
  you a content type and a body, not a filename. WebP is next when something
  needs it; libwebp is already there.
- A malformed image cannot take the console down. libjpeg's default error
  handler calls `exit()`; this one does not.

**The first core runs, 2026-09-13.** Gambatte, and **Dr. Mario boots**.

This is the first empirical evidence for anything in open question 13, which
until now was entirely read rather than run:

- **Built at the manifest's pinned commit, `platform=unix`, first attempt, zero
  patches.** No source edits, no flags beyond the default, no workarounds. The
  Linux path is as easy as the Makefiles said it was.
- **The prefix-and-merge apparatus really is unnecessary.** `dlopen` with
  `RTLD_LOCAL`, resolve the `retro_*` entry points, done. No wrapper, no
  `ld -r`, no exported-symbol list, no `-fno-common`.
- **`cores/build-core.sh` is reproducible and was proved so** — the checkout was
  deleted and the whole thing re-run from nothing. git runs on the host, the
  compile runs in the builder container, and the pinned commit is checked out
  and then **asserted**, which is the discipline Cabinet's own scripts lack.

Reported by the core itself: `Gambatte v0.5.0-netlink`, 160x144, **59.7275 fps**,
32768 Hz, aspect 1.1111.

- **Frame pacing is wall-clock, not per-draw**, with at most two catch-up frames
  and the accumulator capped at four intervals. **Verified**: 96 emulated frames
  against 59.7275 fps is 1.607s, and 53,087 audio frames against 32,768 Hz is
  1.620s — the two agree to within 1%, which is the check that says emulated
  time is advancing at the rate the core asked for.
- **Audio goes to PipeWire through SDL3**, pushed from the frame loop. There is
  no audio callback at all, so there is nothing that can block — which is the
  rule the reference implementation had to work to keep.
- **The picture is integer-scaled and nearest-neighbour.** A Game Boy is 160x144
  and every pixel was somebody's deliberate choice in 1989; scaling by 6.4 makes
  some of them twice the size of their neighbours, which is visible from a sofa.
  Phase 8 can offer the smooth option; the default should be honest.
- **The overlay is drawn straight over the game** — a scrim, a panel, text. No
  compositing trick, no second surface. That is the payoff of hosting cores in
  process, exactly as *How Cabinet hosts cores* predicted.
- **`SET_HW_RENDER` is refused, deliberately and out loud.** Three cores in the
  set want it and none of them is this one; pretending would hand a core a
  context that does not exist.

**A bug worth remembering**, because it cost the first run: `dlerror()` clears
itself on read, so `dlerror() ? dlerror() : "..."` returns null the second time,
and assigning null to a `std::string` segfaults. A perfectly clear "file not
found" became a crash with no message. Read it once.

**One VM artefact, not a fault:** on llvmpipe the frontend cannot draw fast
enough, so the accumulator discards the time it cannot use and the game runs in
slow motion rather than sprinting to catch up. That is the designed behaviour
and the right one; it will not happen on a GPU.

**Seeing it on the actual screen.** The frontend runs as the session's app via
`CABINETOS_APP`, which is the hook Phase 2 left for exactly this, so the VM now
boots to the frontend rather than to a placeholder.

It can also **photograph itself on demand, without stopping**:

```
kill -USR1 $(pgrep -f cabinetos-frontend)   # writes /tmp/cabinetos-frame.bmp
```

That is not a debugging convenience. The test VM has no way to show a person a
picture, and the machines that matter later are in other people's living rooms,
where "send me a photo of the telly" is the whole bug-report channel — see
*HDMI-CEC will not be tested by the author*, which has this problem already.

A latent bug found on the way: the session expands `CABINETOS_APP` **unquoted**,
so a path containing a space is word-split. RomM filenames contain spaces
constantly. Worked around with a wrapper script for now; the session should stop
word-splitting.

**Save states work, 2026-09-13.** `retro_serialize`/`retro_unserialize` wired
up, plus save RAM and arbitrary memory regions (the Game Boy clock lives in one
of its own, and saving only the save RAM loses the clock Pokemon Gold and Silver
depend on).

Dr. Mario's state is **26,882 bytes**. Verified by round trip: save, run 300
frames, restore, run 300 frames, compare.

- **The machine restores exactly.** Video bit-identical across 300 frames.
- **Restoring is deterministic.** Restore twice and both runs agree on video
  *and* audio, which is the control that separates "the restore is wrong" from
  "the path taken to get there was different".
- **Loading a state produces an audible click** — a transient at the seam rather
  than lost state. RetroArch mutes briefly after a load for exactly this reason
  and so should we.
- **Save RAM: Dr. Mario has none**, and that is unremarkable — its cartridge
  has no battery. **Save states are a separate mechanism and do not depend on
  it**: Cabinet offers them from the pause menu for these games exactly as it
  does for any other, and so must CabinetOS. This document briefly framed the
  absence as a finding; it is not one.

#### The test was wrong three times before it was right

Worth recording, because the mistake is easy and the failure mode is a test that
passes while proving nothing.

1. **It compared video on a static title screen.** Dr. Mario's title is one
   unchanging picture; it matches itself no matter what the machine is doing. The
   first version reported PASS on that. **A determinism test must first prove
   that the thing it measures actually varies** — this one now asserts the
   picture moves before trusting a video comparison, and says so in its output.
2. **Then it blamed audio residue, then the resampler's filter history, then
   `retro_serialize` having a side effect.** All three were wrong, and each was
   killed by a control run rather than by reasoning.
3. **The actual answer was that the game is silent.** Traced second by second:
   Dr. Mario untouched plays a ding at one second, a blip at nine, and nothing
   for the next twenty-five. Every "audio differs" reading had been a comparison
   of silence against a click.

The test now reports **INCONCLUSIVE** when there is no audio to compare, rather
than reporting a difference. A test that cannot tell "no signal" from "signal
differs" is worse than no test, because it is believed.

#### And then it was proved against Cabinet's own build

Run the same day, at Marcus's insistence that reasoning about this was not
enough. **The headline result of the project so far.**

Cabinet's macOS Gambatte is pinned to `d9d6cd06` — **the same commit CabinetOS
built for Linux**. So the comparison is clean: one revision, two architectures,
two operating systems.

`tools/state-probe.c` is the instrument. One C file, no dependencies beyond
`dlfcn` and `libretro.h`, **compiled from identical source on both machines** so
that the harness cannot be the variable. It runs a core from boot with no input,
serializes, and can load a state from the other side and run on.

| | |
|---|---|
| macOS **arm64**, 1500 frames | video `dbaef6ffc1e259e7`  audio `a279cfa152a8a553` |
| Linux **x86-64**, 1500 frames | video `dbaef6ffc1e259e7`  audio `a279cfa152a8a553` |

**The emulation is bit-identical across architectures** — every pixel and every
sample, for twenty-five seconds. That was not a foregone conclusion and it is
the foundation everything else rests on.

Then the cross-load, which is the actual question:

| | 300 frames after loading |
|---|---|
| Linux x86-64 loading the **Mac** state | `e2cf3f73bd7379b1` |
| macOS arm64 loading the **Linux** state | `e2cf3f73bd7379b1` |
| macOS loading its **own** state (control) | `e2cf3f73bd7379b1` |

> **Save states are portable between Cabinet and CabinetOS.** Loading the other
> platform's state is indistinguishable from loading your own. The control run
> is what makes that a result rather than a coincidence.

One curiosity, harmless: the two state files are the same size but **23 of
26,882 bytes differ** — raw pointer values Gambatte writes into the state
(`0x000188d0cc30` on Mac against `0x04ab1c48` on Linux, right after the `dmgpal`
label). They are written and never meaningfully read, so they change nothing.
Worth knowing because **a byte-comparison of two states is NOT a valid parity
check** — these would fail it while being perfectly compatible.

#### The cores know their own revision, if you let them

Found while comparing the two builds. The Mac core reported itself as
`Gambatte v0.5.0-netlink d9d6cd0`; the Linux one, built in the container, said
only `Gambatte v0.5.0-netlink`.

The Makefile does:

```
GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
```

and compiles it into the string the core returns from `retro_get_system_info`.
**Build without git on PATH and the core forgets which revision it is.** The
builder container had no git; it does now, and the Linux core reports `d9d6cd0`
like the Mac one.

**This is the answer to "the `emulator` tag carries no version."** A core that
self-identifies can be checked at load time against the manifest, and a mismatch
becomes a refusal rather than a save state that silently will not load. Two
things follow, both cheap:

1. **The frontend should assert** the loaded core's reported version against the
   manifest, and say so loudly when it does not match.
2. **Cabinet should put the revision in the RomM `emulator` tag**, so a state
   carries the identity of the thing that wrote it. That is a Cabinet-side
   change, since Cabinet writes the tag.

Not every core does this — it needs checking per core, the same way everything
else in this question did.

#### What this still does not prove

- **One core of twenty-one**, and the easiest one: Gambatte has no recompiler at
  all, so there is no CPU-backend variable to get wrong. **The backend-sensitive
  cores are untested** — pcsx_rearmed, melonDS, Flycast and picodrive, where the
  Linux default differs from Cabinet's build. That is still the real risk.
- **macOS arm64 against Linux x86-64.** Cabinet's *tvOS* build is a third thing,
  and its Gambatte revision is one of the eleven that are unrecoverable.
- Same flags on both sides, every core option left at its default.

**The on-screen keyboard, and frosted glass with it, 2026-09-13.**

The keyboard is the gate on everything downstream: first-run setup cannot be
reached without it, and it is also the answer to the Wi-Fi question (open
question 17). **There was nothing to copy** — tvOS supplied Cabinet's, so this
is designed from the input model rather than ported.

- Digits on their own row rather than behind a shift layer, because a Wi-Fi
  passphrase is usually being read off the underside of a router.
- **Vertical movement keeps the horizontal POSITION, not the index.** Rows have
  different key counts and widths, so moving down from `p` lands near `l`
  rather than on whatever happens to be ninth.
- **Horizontal movement wraps; vertical clamps.** This reverses an earlier
  decision recorded here, and the earlier one was wrong. "No wrapping" is right
  for a shelf, where the next item is a different game and landing somewhere
  unexpected loses your place. It is wrong for a keyboard, where the grid is
  twelve wide and every key is equally somewhere you might have meant: without
  it, `1` to `del` is eleven presses instead of one. **PlayStation and Xbox both
  wrap their keyboards**, and that is the reason. Vertical still clamps — five
  rows is short enough to cross directly, and wrapping from the space bar up to
  the digits would skip the letters, which is where somebody pressing up is
  almost always going.
- Shift is one-shot, the way a phone keyboard behaves.
- Backspace steps over a whole UTF-8 code point, not a byte.
- The field shows the **tail** when it overflows: what you are typing is at the
  end, and a field that scrolls off the right hides the character just pressed.
- **A physical keyboard types into the same field**, through the same string and
  the same commit. Not a second path.
- The button legend is on screen. A controller-only UI has to say what the
  buttons do, because there is no convention to fall back on and no pointer to
  explore with.
- Per-field shortcut keys, because reducing typing beats speeding it up.

**The layout is a fixed 12-column grid**, every row totalling exactly twelve
units. The first version sized the panel to its widest row — a long function row
— and left the letter rows short, so a third of the panel sat empty beside the
letters. It looked unfinished and it wasted the one thing a ten-foot keyboard is
short of, which is reach.

**Consulted rather than invented.** PlayStation, Xbox and Steam all converge on
the same thing, and it is not a clever layout: it is a **real keyboard's own
geography**.

| | |
|---|---|
| Backspace | right end of the number row, where a real keyboard's backspace is |
| Shift | bottom-left of the letter block, where a real keyboard's shift is |
| Space | spanning the bottom, with the commit key at its right |

None of that is decoration. Somebody hunting for backspace looks top-right
*before* they read anything, and a layout that rewards the guess is faster than
one that has to be read first. The familiar arrangement is the optimisation.

The bottom row absorbs whatever is left over, so the grid stays square whatever
a field asked for — a password field passes no shortcut keys and simply gets a
longer space bar.

**Verified** inside the 60pt safe area at 1920x1080: margins 62 left, 360 right,
168 top, 140 bottom.

**And backdrop blur, finally.** It was on the list of six things tvOS gave
Cabinet for free and it is the largest of them. The keyboard is what forced it:
a translucent panel over cover art without blur is not "less pretty", it is
**unreadable** — the art shows through and competes with the text. First version
proved it.

The mechanism: the scene is drawn into an offscreen texture, mipmapped, and a
panel samples that texture at a coarse level under itself. **A mip chain is a
box blur the GPU already built**, so this costs a texture fetch rather than a
blur pass — which matters on integrated graphics that also has a PS2 to run.

Two rules learned immediately, both the hard way:

1. **Glass does not nest.** Every glass surface samples the same captured scene,
   so a key drawn as its own glass re-samples the bright cover art and ignores
   the darkening of the panel it sits on. It looked like stained glass. **One
   glass layer per modal**; everything on it is an ordinary surface.
2. **The tint must be dark.** A white tint over a blur lightens, and this is a
   dark interface. What makes a material read as a material here is that it
   *dims* what is behind it as well as softening it.

#### The safe area, and whether any of this is really ten-foot

Asked directly, and checked rather than asserted.

**What holds up.** The 1920x1080 canvas, verified identical at 4K, 1080p and
720p. Type from the ten-foot ramp throughout — keys at Title 3, labels at
Callout, the screen title at Title 2. Focus unmistakable at a glance. Every
control reachable by direction plus confirm.

**What did not, and was fixed on the spot.** The keyboard's button legend was
**Caption 1, 25pt**. That size is inside Apple's ten-foot ramp, but it is the
size for something *glanceable* — and a legend telling you what the buttons do
is a line you have to **read**. Now Callout. The rule worth keeping: **anything
a person must read sits at Callout or above; Caption is for things they merely
glance at.**

**What still is not enforced.** The safe area was the last of the six things
tvOS gave Cabinet for free, and it was being met **by inheritance rather than by
design** — the design system's `contentInset` of 60 happens to equal tvOS's own
safe area, because it was copied from there. It is now a named constant
(`kSafeInset`) with a `--safe-area` overlay that draws it, plus a 5% overscan
allowance, so it can be checked on a television rather than reasoned about.

Measured on the running frame:

| | |
|---|---|
| Content bounding box | left **63pt**, right 297pt, top 171pt, bottom 141pt |
| Inside the 60pt safe area | **yes** |
| Inside a 5% overscan allowance (96pt) | **no** — the shelf's own content inset is 60 |

That failure is expected rather than alarming: **60pt is Apple's judgement of
what survives on the televisions people actually own**, and 5% is the analog-era
worst case. Modern sets mostly present 1:1 over HDMI. But it is exactly the
question a monitor cannot answer, and this document already records the hero
being resized three times over it — including once where the simulator showed it
fitting and real hardware did not.

**So: run with `--safe-area` on the SER5, on a real television, before trusting
any of it.**

#### The letterbox glow, built 2026-09-13

Every number taken from `BiasGlow.swift` rather than invented, including one
that would have been missed.

- **The ramp is `peak * (1 - t^1.7)`, not linear.** The shallow exponent gives a
  flat start so the brightness carries toward the physical edge instead of
  collapsing early. **Verified on the rendered frame**: flat for the first ~60
  points, then rolling off to nothing at the screen edge.
- **White, deliberately.** It adds luminance without hue, so it can never clash
  with whatever the game is rendering.
- **The side bars span the full height; top and bottom only the picture's
  width**, so the corners belong to the sides.
- **A static noise dither is composited in**, masked by the same ramp. This is
  the one that would have been missed: **a long near-black ramp bands visibly on
  an 8-bit panel**, and the dither is what stops it. Static, never animated — a
  moving dither in a dark room is a shimmer, which is worse than the banding.
- **It never covers a game pixel.** The shader discards inside the picture rect.
  **Verified**: the darkest pixel inside the picture measures 0.0 with the glow
  at full strength.

**And the background behind a running game is now BLACK**, not the menu's
backdrop gradient. The reference implementation's player clears to black and it
is right for two reasons: a gradient around a game picture is decoration
competing with the thing being looked at, and bias lighting means light against
black — on a purple backdrop it is neither. Caught only by rendering the glow
and seeing it sit on the wrong thing.

Measured on black, outward from the picture edge, at Strong: 12, 12, 12, 11, 8,
4, 0. Subtle by design — this is bias lighting, not an effect.

**Still to judge on a television.** The two peak values, 0.025 and 0.04, were
found with a slider on a real panel after two sets guessed from a mockup were
both wrong. Nothing about a software-rendered VM can confirm them.

**Not there yet:** shaders, and the remaining screens.

**Known gap worth recording now:** there is no text *shaping*, only advance and
kerning from FreeType. That is correct for Latin and adequate for CJK, and wrong
for Arabic, Hebrew and the Indic scripts, which need HarfBuzz. No game library
seen so far needs it; if one does, HarfBuzz is already in the image as a
FreeType dependency.

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

**First run copies Cabinet's flow rather than inventing one.** Read from the
source 2026-09-13: tvOS already solves this problem and has shipped the answer.

1. **`ServerSetupView`** — one field, the RomM address, and nothing else. Its
   own note explains the copy: *"the one thing someone genuinely wonders here:
   no password is coming."*
2. **`PairingView`** — the app asks the server to start a device authorisation,
   shows a short code, and **displays the approval URL as a QR code**, because
   *"tvOS has no comfortable way to type into a browser with a remote."* The
   person scans it with the phone already signed in to RomM and approves there.
   The app never sees the password, and the token can be revoked server-side.

**So the whole typing burden on a television today is one hostname.** No
password, ever. That number matters, because it is the size of the problem any
first-run convenience is competing against.

#### Getting a ROM out of RomM, measured

**Tested against the live server 2026-09-14** by downloading one, because the
path from a library entry to a running game had never been walked.

**ROMs arrive zipped.** `fs_name` is `Tetris.zip`; the archive holds
`Tetris (World) (Rev 1).gb`. Nothing hands a libretro core a zip, so the
frontend unzips. Endpoints:

| | |
|---|---|
| `GET /api/roms/{id}/files` | the files a ROM is made of — multi-disc, or a `cue` beside its `bin` |
| `GET /api/roms/{id}/content/{file_name}` | the bytes |

**And the cores want opposite things, which decides the design:**

| Core | `need_fullpath` | Wants |
|---|---|---|
| gambatte | no | the ROM as a **buffer in memory** |
| genesis_plus_gx | **yes** | a **real path on disk** |

`retro_get_system_info` reports this per core, so **ask the core, never the
platform**. The launch path therefore needs both halves — unzip to memory for a
buffer core, unzip to a file for a fullpath core — and a fullpath core with a
multi-file game needs every file beside it, not just the one that was asked for.

**What is NOT yet proven**, and was wrongly implied before this was checked:
Dr. Mario ran from a **hand-placed local file** with `--core` and `--rom` as
command-line paths. The core host loading a `.so` and a ROM *from disk* and
running it with save states is real. Everything between RomM and that point —
downloading, unzipping, where a downloaded ROM lives, what evicts it, choosing
the core from the platform, and reaching any of it from the UI — does not exist.

#### How saves, states and firmware actually work, read from Cabinet

**Read 2026-09-14 from `RommApp/RommApp/Auth/RommClient.swift`,
`Native/NativeLauncher.swift`, `Native/NativeCore.swift` and
`Native/KeptGames.swift`.** This was being reasoned about from the API surface,
which was the wrong way round: tvOS and macOS already ship the answer.

**The console is not the home for any of this. RomM is.** Saves, save states and
memory cards all live on the server and all come back down. The reference server
holds 80 saves and 54 states already, several of them written by Cabinet.

##### Saves and states are different endpoints, on purpose

| | |
|---|---|
| Battery saves, memory cards | `POST /api/saves?rom_id=&emulator=&overwrite=true`, multipart, part `saveFile` |
| Save states | `POST /api/states?rom_id=&emulator=`, multipart, part `stateFile`, optional `screenshotFile` |
| Reading either | `GET /api/saves?rom_id=` / `/api/states?rom_id=`, then `/{id}/content` |

**`overwrite=true` on saves is load-bearing**: it replaces the server's copy of
the same file name instead of stacking a row per upload, which is *"what keeps a
PS1 game at one memory card rather than one per session."* States deliberately
do not overwrite — a state history is the point of states.

##### The `emulator` tag is the compatibility mechanism, and CabinetOS must get it right

Every upload carries an `emulator` string. Cabinet's is per **core**, not per
platform — `gambatte-native`, `gpgx-native`, `pcsx-rearmed-native` — and its own
comment says why:

> *libretro state formats are core-build-specific, so each player's states are
> tagged distinctly on purpose: a separate tag keeps each player's launch UI
> from offering states it cannot actually restore.*

So the tag is what stops a launch screen offering a state that will fail. Which
raises the decision CabinetOS cannot avoid:

> **Does CabinetOS write `gambatte-native`, or a tag of its own?**

**It should write the same tag — and only because of work already done.** The
whole point of the Phase 3 result is that a Gambatte state is bit-identical
between Cabinet's macOS arm64 build and a Linux x86-64 build *at the same
commit*. `core-manifest.json` pins that commit, `build-core.sh` asserts it, and
CI proves the artifact is reproducible. Those three together are what make
sharing a tag safe rather than optimistic.

**The rule, therefore: share Cabinet's tag only where the build is provably the
same thing** — same pinned commit *and* the same build arguments. Where
CabinetOS pulls a different lever, it must use a different tag, or Cabinet's UI
will offer the person a state that cannot load. Today gambatte and
genesis_plus_gx both match Cabinet's configuration, so both share. The
recompiler-sensitive cores are exactly the ones where this has not been settled,
which is one more reason open question 13's remaining half matters.

A tag that is wrong in the safe direction costs a greyed-out state. Wrong in the
other direction, it costs someone their progress.

##### Firmware: fetch everything the platform lists

From `NativeLauncher`:

> *Fetches every firmware file the platform lists rather than assuming which one
> the board wants: a core looks BIOS files up by name in the system directory,
> ignores what it does not need (Beetle Saturn wants one of two region BIOSes;
> FBNeo boards like CV1000 need none at all), so extra files are harmless and
> missing ones are the only failure that matters.*

So: `GET /api/firmware?platform_id=`, download all of it into the system
directory, and let the core pick. Do not try to be clever about which BIOS a
given game needs.

###### Firmware is per PLATFORM, so fetch the whole lot once and stop thinking about it

**Marcus, 2026-09-16: a BIOS "just needs downloading for that platform one time
and then the platform uses it for all games on it".** That is already how
CabinetOS stores it and it is better than the reference implementation here —
one shared `system/` directory, with a file already present at the right size
skipped. Cabinet's tvOS stages firmware into each game's own cache directory
instead, so twenty-seven Dreamcast games mean twenty-seven copies of
`dc_boot.bin`.

**What is still per-launch is the asking**, and it need not be. Every launch
calls `fetchFirmware(platformId)` before the ROM, even when every file is
already on disk: a round trip each time, and offline it fails and logs a
complaint on a launch that was going to work anyway.

**Measured against the live server, 2026-09-16, which settles it:**

| | |
|---|---|
| All firmware, every platform | **212 MB** |
| PlayStation 3 alone | **197 MB** |
| **Every platform this console has a core for** | **~15 MB** |

Ninety-three percent of that total is firmware for a system with no core in the
manifest and no prospect of one. For everything actually playable it is fifteen
megabytes — the entire BIOS collection, for every system, permanently.

**Fetching the whole 15 MB at setup was proposed and Marcus chose otherwise:
fetch a platform's firmware the first time a game on that platform is launched.**
He is right, on two counts. It is less machinery — the launch path already does
exactly this, and the only change is not asking again afterwards — and it is a
simpler thing to hold in your head: the console fetches what a game needs when
that game needs it, with no separate preparation step. Fifteen megabytes is not
enough saving to justify inventing a setup phase for.

> **Fetch a platform's firmware on the first launch of a game on it, and never
> ask again. Keeping a game fetches its platform's firmware too.**

**The second sentence is the one that is easy to forget**, and it is Cabinet's
behaviour already: *"Keeping a game pulls its ROM and its platform's
firmware."* Without it there is a real hole — download a PlayStation game for
later, go offline, and it will not start, because the machine has the game and
not the system file it needs. Keeping is a promise that a game will work later,
and later may have no network in it.

**Checking is not downloading, and the two should not be confused.** The console
can ask what firmware a platform *has* without fetching any of it, which is a
cheap list request and is all the missing-BIOS warning below needs. So: ask
early, download when first needed.

Two details worth keeping: `missing_from_fs` files are skipped, since the server
lists them and does not have them; and a failure is still not fatal, because
which BIOS a core needs is the core's business and most platforms need none.

**Fetch again when a platform appears that was not there at setup.** A library
grows; someone adds Saturn games next month. "Once" means once per platform, not
once per console.

###### Tell the person their server has no BIOS for a system, BEFORE they pick a game

**Marcus, 2026-09-16, and it is a real gap.** Today a platform whose BIOS the
server does not hold fails at launch with the CORE's error message — measured
earlier the same day, on Sega CD: `Unable to open CD BIOS:
"system/bios_CD_U.bin"`. Clear, actionable, and delivered at the worst possible
moment, after the person chose a game and waited for a download.

**Asking what the server holds is what makes the better version possible**, and
it costs nothing: a firmware list per platform, no downloads. Do that while the
library is being scanned and the console knows, up front, which systems it
cannot play — so it can say so once, about a whole system, rather than per game
and after the fact.

It needs one small thing that does not exist: **a list of which platforms cannot
start without firmware at all.** PlayStation, Saturn, Sega CD, 3DO, Dreamcast,
PS2 and TurboGrafx-CD; most systems need nothing. That list is stable, short,
and a property of the hardware rather than of anyone's library.

**The honest limit, and it is why this is a coarse check rather than a precise
one:** *which* BIOS a given game wants is genuinely not knowable up front — this
section already records Beetle Saturn taking either of two region BIOSes and
FBNeo boards needing none — so the console must not try to verify that a
platform's firmware is *sufficient*. What it can say with certainty is that the
server offered **nothing** for a system that cannot boot without something, and
that is the case worth warning about.

Where it belongs is `catalog`, beside the four answers it already gives for why
a game cannot be played. This is a fifth, and unlike the others it is a fact
about the person's server rather than about this console or the manifest.

##### What a kept game is

`KeptGame` embeds **the whole `Rom` captured at keep time**, not a subset, so a
kept game can be browsed and launched with no network at all — cover paths and
platform identifiers included. Its directory holds the ROM *and* its firmware,
so it boots with zero requests. States stay internal to it and are deliberately
never exposed in the Files mirror, because they are core-build-specific and
belong to RomM's database rather than its filesystem layout.

##### What this means for eviction

Almost nothing on the console is irreplaceable, which makes reclaiming space
safe: ROMs, firmware, saves, states and memory cards all come back from RomM.
**The one thing that cannot be re-fetched is something written locally that has
not been uploaded yet** — hence "local first, always", and a `pending-states`
directory that eviction must never touch.

It also adds a requirement this project had not accounted for: **the console
must upload, not merely download.** That needs write scope on assets, which the
first pairing did not request.

#### When a save happens — decided 2026-09-15

Keys currently trigger it: F5 writes a state, F8 restores the newest loadable
one, F6 pushes the game's own save. **That is the test environment and not the
product.** The intended triggers, settled rather than deferred:

- **When the game writes its memory card**, so a save that the game itself
  considers made is a save the console has.
- **From the in-game menu**, as a deliberate act.
- **On leaving a game**, always — nobody should lose progress because they
  quit.
- **A controller combination**, so a state can be taken without opening
  anything.

Cabinet's shape for the same thing: *"the views own the* when*, the sync engine
owns the* what*"* — the triggers belong to the screens, the capture and upload
belong to one shared type. CabinetOS should keep that split from the start,
because tvOS once carried its own copy of the *what* and it silently went stale.

**Nothing that talks to a server may stop the picture. Fixed 2026-09-15.**
Uploads were on the frame thread, which on a LAN with a 60 KB state was
imperceptible and on a slow link is a visible hang — and against a server that
does not answer, curl's timeout would leave the console looking dead for thirty
seconds.

The split that matters is not "put it on a thread", it is **which** part moves:

- **The frame thread reads the core and writes the local copy.** Reading has to
  happen there because a core is not thread-safe, and the local write has to
  happen before the upload is queued, or *local first* stops being true the
  moment the process dies between the two.
- **Only the network moves to the worker.** That is the part that can take
  thirty seconds.

**Measured, saving a state and a memory card together: 3.21 ms on the frame
thread**, which is a core read and two local writes, inside a single 16.7 ms
frame. Every upload completed afterwards on the worker.

Loading a state is network work too and got the same treatment: the search and
fetch happen on a worker, and only applying the state touches the core, which
waits for the frame thread.

**The queue is drained on the way out, not abandoned.** Anything still pending
is a save someone has already made. Quitting is the one place waiting for the
network is correct, because there is no picture left to stop.

#### Downloads stream, and the console keeps drawing

**Built and measured 2026-09-14.** Two things were wrong with the first working
launch, and only one of them was the obvious one.

**It held the whole ROM in memory.** Fine for a 19 KB Game Boy file; the same
library holds a 1.78 GB arcade set, and a 4 GB console does not get to keep one
of those in a `std::vector`. `Client::fetchToFile` streams to disk through a
buffer and never holds the body.

**And it downloaded on the frame thread**, which is the worse of the two because
it is the half a person experiences: press a button on a large game and the
console freezes solid — no animation, no progress, no way to change your mind —
for as long as a few gigabytes takes. A worker does it now; the frame loop reads
an atomic snapshot and keeps drawing. `romm::Client` was deliberately built
synchronous so callers could do this, the same way `ImageCache` already did.

Details that are not incidental:

- **Written to `<name>.part` and renamed only on success**, so an interrupted
  download can never be mistaken later for a complete ROM.
- **No whole-transfer timeout.** A deadline is wrong for a file that can
  legitimately take twenty minutes on a slow link; a *stall* is caught by a
  low-speed limit instead.
- **Escape cancels the download rather than quitting.** Three gigabytes is a
  long time to be unable to change your mind. The progress callback returning
  false is what reaches a transfer already in flight.
- **A progress bar only when the server declared a size.** It often does not,
  and a bar that invents its own total is a lie — the honest fallback is to show
  what has arrived and draw no bar.
- **An archive is untrusted input.** A member named `../../etc/thing` is reduced
  to its last path component, so it lands inside the cache directory or nowhere.

**Measured, streaming a 112 MB Sega CD image:** the file arrived complete, the
`.chd` was correctly passed through rather than unpacked, Genesis Plus GX loaded
it, and the refusal was `Unable to open CD BIOS: "system/bios_CD_U.bin"` — a
clear, actionable message rather than a silent failure. **Sega CD needs firmware
from RomM**, which is a scope this console's token does not currently hold.

**What this still does not do.** A ROM already on disk at the size RomM reported
is reused rather than re-fetched, which is the crude half of *cached versus
kept*. **Nothing evicts anything.** A library of 1644 games at these sizes will
not fit on a console, so the disk fills and stays full. That is the next thing
this needs.

#### The cache policy — decided 2026-09-16, not yet built

**The rule is that the person never thinks about storage, and never loses
anything they would miss.** Everything below serves those two sentences. From
Marcus's proposal, with four changes argued for rather than accepted.

##### What Cabinet already does, on both its platforms

**Read from `NativeLauncher.swift` 2026-09-16 at Marcus's prompt, and it should
have been read before any of this was designed.** Neither Apple platform has an
eviction policy, for two different reasons, and the difference is the whole
reason CabinetOS needs one.

**The Mac has no cache at all.** A game is either *kept* — chosen by the person,
in a permanent directory, never touched — or it is downloaded into a temporary
directory that is deleted when the player closes. `cleanUpTempDirectories()`
runs on the way into a launch as well as out of one, "so temp space holds at
most the one game about to load". Two states, no middle, nothing to decide.

**The Apple TV has a cache and delegates the deleting.** It writes into the
system caches directory keyed by rom id and lets tvOS reclaim it whenever it
likes, system-wide across every app — and when the file has gone, the next
launch simply downloads again with, in its own words, "no special handling
needed".

> **CabinetOS is the only one of the three that has to decide for itself, and
> that is not an oversight in the design — it is what being the operating system
> costs.** There is no prior art to copy here because neither sibling has the
> problem.

**Two things do carry over.** The shape is the same one already specified in
*Emulation*: kept versus transient, with keeping being the deliberate act. And
the Mac is proof that **"delete it when they stop playing" is shippable** — it
is what that app does today — so discarding is the safe fallback wherever any of
the machinery below is uncertain, rather than something to be nervous about.

**And one warning, from tvOS's own history.** It used to behave exactly like the
Mac, and that is recorded as a mistake: every launch "used to redownload into a
fresh temp directory deleted unconditionally on exit, so replaying a game
already on Recent or Favorites cost a full download every single time even
though nothing about the file had changed." Replaying the same handful of games
is the living-room pattern, and it is CabinetOS's pattern too. **So the middle
tier has to exist here, even though the Mac gets away without one.**

##### None of this may be tuned to one library, one disk or one connection

**Raised by Marcus against the first draft of this section, and he was right.**
That draft justified its eviction order with "every cartridge game in the
library together is under 2 GB", which is a fact about *this* reference library
— about three hundred cartridge games — and it inverts for anyone with a
complete set, where the cartridge half is tens of gigabytes. It then closed by
saying the threshold should be settled against a real library, which bakes in
whichever library happened to get measured.

*Hardware* already has this rule in this document: the SER5 "sets the
performance floor, not the ceiling", and CabinetOS "must not have quietly grown
dependencies on this particular box". **The library is the same kind of
reference and deserves the same sentence.** The first draft did not give it one.

So the standard for every rule below is that it holds for all four corners, and
the reference library is an illustration in the margin rather than the basis:

| | |
|---|---|
| **A library of one shape** | all cartridges, or all discs, or a mix |
| **A disk of any size** | a 32 GB eMMC stick and a 4 TB NVMe |
| **A library far larger than the disk, or far smaller** | permanent pressure, or none ever |
| **A connection of any speed** | see below — this is the assumption that matters most |

**Where a number is unavoidable, express it as a fraction of something the
machine can measure**, not as a constant somebody chose while looking at their
own collection.

##### The assumption underneath all of it: that a re-download is cheap

**Stated because the first draft relied on it silently.** The argument that
eviction is harmless — "it is still on the server and comes back in minutes" —
is true against a RomM on the same fast LAN, which is this project's own setup.
It is false for a server in another building, over WiFi, or across the internet,
where a 4 GB game is twenty minutes rather than forty seconds.

**That does not change what is safe to delete. It changes whether deleting
quietly is the right manners.** Where re-fetching is cheap, handling it silently
is the console-like behaviour this whole section argues for. Where it is
expensive, the same silence spends twenty minutes of somebody's evening.

**DECIDED: the behaviour does not change, because the answer already exists and
it is `keep`.** Someone on a slow link marks the games they care about, and the
console never touches those — that is precisely what keeping is for, and it is
better than the alternatives on every count:

- **Measuring throughput and switching behaviour** means the console acts
  differently on Tuesday than it did on Monday, for reasons invisible to the
  person using it. A console that is unpredictable is worse than one that is
  occasionally slow.
- **Asking before each eviction** is a dialog box about storage, which is the
  exact thing *What CabinetOS is* rules out, and it would fire most often for
  the person least able to act on it.

So the policy below is the only behaviour, not a fast-link default. What a slow
link changes is the *advice*: first-run and the Storage screen should say that
keeping a game means never waiting for it again, which is a sentence worth
writing regardless.

##### The policy in one paragraph

**Marcus's, 2026-09-16, and it is better than the version it replaced because it
is sayable.** Everything else in this section is detail underneath it:

> **The games you have played on this console are on the disk. They stay until
> the disk needs the room, and then the ones you have not played for longest go
> first. Nothing that is running, nothing you marked as keep, and nothing still
> waiting to reach RomM is ever touched.**

**The cache is invisible, and that is the decision.** Marcus, 2026-09-16,
ending a long detour: *"No one knows or cares if the game exists in cache on the
OS. You go to the game and hit play. If it isn't cached it downloads. If it is
cached it doesn't."*

That is right, and the reason it is right is that **the feedback already exists
at the only moment it is useful**. Pressing play on a game that is not on disk
already shows a progress bar and already takes Escape to back out. So the person
finds out immediately, at the point of asking, and can change their mind for the
cost of one button press.

**A badge warning them beforehand does not shorten the download.** They want to
play that game; the information changes nothing they would do, and it adds a
thing to think about to a screen whose whole job is that there is nothing to
think about.

So: no promise about what is cached, no marker on the shelf, nothing in the UI
at all. **Everything below this line is internal.**

The one exception is the Storage screen, which stays — because it is somewhere a
person goes *deliberately*, looking for exactly this. Nobody cares until they go
looking, and then they should find it.

**Two things this deletes**, both recorded so nobody re-adds them:

- **The downloaded badge on shelf cards**, and with it the whole question of what
  the shelf promises about the disk.
- **"Cleared 40 games" as a worry**, which was the main argument for the deferred
  size rule below. It was a concern about how a list would read, on a screen
  nobody is watching.

**And "longest since played" means since PLAYED, not since downloaded.** A game
fetched months ago and played last night stays; a game fetched last night and
never started goes first. Easy to implement backwards.

##### Never on a timer. Only under pressure, only at a safe moment

**Nothing is evicted because time has passed.** A cached game on a half-empty
disk costs nothing and deleting it only buys a re-download. Expiry is the
intuitive answer and it is the wrong one.

**The safe moment is the start of a download that will not fit.** Free exactly
enough for the incoming game, least-recently-played first, and stop the moment
there is room. Nothing is deleted speculatively, in the background, or while a
game is running.

**Pressure is simply the disk being full**, and there is no cache size to
configure. Marcus, 2026-09-16: a person picks a game and chooses Download, those
downloads stay, "and by nature shrink disk space available for cache".

That is the whole sizing rule, and it deletes a setting:

> **The cache is whatever is left.** Kept games take what they take, the save
> floor is never crossed, and the cache has the remainder.

An earlier draft had a configured budget *and* a free-space limit, whichever
bound first — two numbers doing one number's job, and the configured one is
unanswerable anyway. Nobody knows what to set a cache size to, and on a console
the drive is for games regardless.

**It degrades exactly the right way.** Keep enough games and the cache shrinks
to nothing, at which point every un-kept game downloads, plays, and is dropped
on the way out — which is precisely what Cabinet's Mac does today, and it ships.
Keep so many that nothing fits at all and the download refuses and says so,
which is the one failure this policy ever shows anybody.

##### Everything on the disk is a copy of RomM. That is the whole rule

**Marcus, 2026-09-16, after this section had drifted into categories for the
third time: saves, BIOS and memory cards are all stored on RomM.** They are, and
this document has said so twice and then built tiers of protected things on top
of it anyway.

> **Everything here is a copy of something on the server. The only exception is
> what has not been uploaded yet.**

So **everything is evictable** — ROMs, save states, battery saves, memory cards,
firmware. There is no protected tier, because there is nothing to protect. The
earlier draft's "counted but never evicted" list was inventing a distinction the
server had already removed.

**Saves and firmware simply never come up**, which is an observation rather than
a rule. Measured on the test machine: 124 MB of ROMs against 40 KB of battery
saves and 384 KB of firmware. Every save for all 1644 games in the reference
library is around 33 MB. They will never be the largest thing in a list sorted
by size, so nothing needs to say they are special.

**And the previous draft's reason for exempting firmware was simply wrong** —
"deleting it breaks the next launch of that system". It does not. It re-fetches,
like everything else does.

##### Save states can be the biggest thing on the machine, not a rounding error

**Corrected 2026-09-16, and the first estimate here was badly wrong.** It assumed
states are taken at checkpoints and put fifty of them at 325 MB.

**People save-scum.** Grinding through a hard section means a state every twenty
seconds or so, which is around 360 in a two-hour evening. At the **6.5 MB** a DS
state measures — a real number from this project's own melonDS build — that is
**2.3 GB from one session**, more than most ROMs on the disk. PS2 will be larger
still. States do not overwrite, deliberately, because the history is the point.

So the conclusion inverts. States are not a small thing to be exempted, they are
one of the largest things to be managed, and they are on RomM with their
screenshots like everything else.

> **Keep the newest state for a game as long as its ROM is there** — it is the
> one that gets loaded — **and let older states be ordinary candidates**, fetched
> back from RomM when somebody actually picks one off the launch screen.

That is the same principle as the ROM cache, applied one level down, and it
needs no new machinery: they join the same oldest-first list.

##### The case this does not solve: save-scumming while offline

**The upload queue is the one thing that cannot be evicted, and save-scumming is
exactly what makes it enormous.** An evening of it with no server reachable is
gigabytes of pending states by morning, and no floor protects against data that
is itself the thing filling the disk.

This was already recorded as an open problem in a milder form — "a long spell
offline defeats the floor" — and the realistic magnitude makes it worth solving
rather than noting. It is a conversation with the person ("this console has not
reached your server in three days") rather than a storage rule, and **it belongs
with whatever handles being offline, not here.**

A cheaper half-answer exists and is not chosen: while offline and short of
space, the oldest *pending* states for a game could be dropped rather than the
newest, since a save-scummer wants the last one and not the three hundredth from
the bottom. That trades a promise this document makes — local first, nothing
written is ever lost — against a disk that stops working, and **that trade needs
Marcus rather than an assistant.**

##### The eviction unit is a FILE, not a game

**This is the first change to the proposal, and it is structural.** Today a
game's cache directory holds the ROM *and* its save states *and* its battery
save together — `romcache/2813/` has three `.state` files and an `.srm` beside a
1 MB Game Boy ROM. So "evict a game" would delete the one thing that can always
be fetched again along with the only things that cannot.

The proposal patches this with a rule — never evict a game with unsynced saves.
That is correct and it should not be necessary. **Separate what the console
wrote from what it downloaded**, and the ROM becomes unconditionally safe to
delete rather than conditionally, because there is no longer anything precious
in the same unit to take with it. The case where a download fails over a few
kilobytes of old save goes away with it, which is a poor trade to have been
making.

What the rule protects is then the **upload queue**, not save data in general —
a distinction that matters, because a save already on RomM is a cache like the
ROM beside it.

What remains protected, and it is now short:

| | |
|---|---|
| The running game's ROM | it is in use |
| The running game's save data | it is being written |
| **Anything not yet uploaded** | the only irreplaceable data on the machine |
| Anything **kept** | the person asked for it; *Emulation* already says this is never automatic |

**And what is evictable is wider than ROMs, for the same reason.** A synced save
state is a cache of RomM like everything else, and a well-played DS game can
hold hundreds of megabytes of state history that would cost a few megabytes to
fetch back on demand. It belongs in the same candidate list, ordered the same
way — no separate mechanism, and the size rule below already keeps it out of the
way when it is not worth taking.

The unit is therefore **any local file that RomM can return**, which is a longer
sentence than "the ROM" and the same idea.

##### The floor has to be enforced DURING the download, not before it

**Second change.** The failure the reserve exists to prevent is a save that
cannot write because a download filled the disk — and that happens *while* the
download runs. Checking once at the start does not prevent it, and the size is
not always known: this document already records that the server frequently
declares no length, which is why there is a progress bar only sometimes.

So the streaming writer checks free space as it goes and aborts when the next
write would cross the floor, deleting its `.part` — which returns the space it
had taken. A `statvfs` every few megabytes is not a cost worth optimising.

**The size of the DOWNLOAD is in the library record, not the HTTP response.**
`fs_size_bytes` is present on every ROM and is what makes room for the transfer
possible to reserve at all. Do not reach for `Content-Length`.

It is the size of the *archive*, though, so it is what the download needs and
**not** what the game will cost once unpacked. See below.

##### Unpacking: ask the archive, do not guess a ratio

**Third change. The first draft said "budget twice the archive" and that is
wrong in the direction that fills the disk**, as Marcus pointed out: an archive
is *compressed*, so what comes out of it is not the size that went in.

Measured on this library rather than argued: Space Harrier is an **868 KB** zip
holding a **2 MB** ROM. Extraction holds both, so the peak is 2.9 MB — **3.4×
the archive**, not 2×. And that is a mild case. DS and N64 ROMs are padded out
to power-of-two sizes with empty space, which compresses far harder, so no
multiplier is safe for the set.

**There is no need to estimate at all.** A zip declares each entry's
uncompressed size in its own index, 7z likewise, and `archive_entry_size()`
hands it over when the header is read — before a byte is extracted.
`romfile.cpp` already walks those headers to pick the member to use; it simply
does not add up the sizes while it is there.

> **peak = the archive + what its index says will come out of it**, known before
> committing to the extraction, and refused cleanly if it will not fit.

Same idiom as everywhere else in this document: ask the core what it takes, ask
the magic bytes what the file is, ask the archive what it holds. The multiplier
was a guess standing in for a fact that was already on disk.

**A format that declares nothing** — some streamed archives report an unknown
entry size — is the only case needing a fallback, and the honest fallback is to
extract into the space available and fail if it runs out, not to invent a ratio.

Two exceptions already established elsewhere, worth restating because they make
the peak vanish entirely where they apply: an arcade set is handed to FBNeo
unextracted, and `.chd` and `.rvz` are never unpacked at all.

##### What a game costs is what is ON DISK, not what RomM said it was

**Falls out of the above and is its own accounting error.** The policy's budget
would naturally use `fs_size_bytes`, since that is what the library record
carries and what "free exactly enough for this game" was written against. For
anything archived that is the *compressed* size, and the cache ends up holding
the larger thing. Budgeting against it under-counts every archived game.

**And today it under-counts twice over, because the archive is never deleted.**
`romcache/39/` holds `Tetris.zip` and the extracted `Tetris.gb` side by side,
and that is not an oversight: the "do we already have this?" check `stat`s the
downloaded archive against the size RomM reported, so deleting it would mean
re-downloading the game on every launch.

So the unpacking cost is not transient at all as things stand — it is permanent,
and it is the compressed size of every archived game in the cache, forever.

> **Decided: the archive goes once it has been unpacked, and the reuse check
> moves to the extracted file.** It costs a small per-game record of what was
> unpacked and how big it should be — which the cache wants regardless, since
> the budget has to be computed from real sizes on disk rather than from the
> server's idea of them.

##### Order: least-recently-played, and that is the whole rule

**Fourth change, and it answers "should size matter" and "should small systems
be exempt" with one mechanism.**

The intent is easy to state: **do not delete a hundred things that were cheap to
keep in order to house one thing that is not.** Freeing 4 GB by removing four
thousand Game Boy games is a bad trade whatever the library looks like — each
one is a separate thing somebody may come back to, and together they were
costing almost nothing.

**The threshold that expresses this has to scale with the need, not with a
number somebody picked.**

> **Shipped rule: least-recently-played, full stop. The size refinement below
> is DEFERRED until there is evidence it is needed.**

The refinement it defers: ignore candidates smaller than one percent of the
space being freed, so that a single 4 GB download does not take forty Game Boy
games with it.

**Its reason has since evaporated.** The argument was that "cleared 40 games"
reads as destructive — a worry about how a list would look, on a screen the
decision above says nobody is watching. What remains is the actual cost, and the
actual cost is that forty small games come back in a second or two each.

**So it stays deferred and it may never be needed.** Kept here because the
arithmetic is done and someone will think of it again.

One percent is recorded rather than left to be re-derived, because it has no
units and belongs to no library. Needing 4 GB it ignores anything under 40 MB,
leaving cartridge games alone. Needing 50 MB for a Game Boy Advance title it
ignores anything under 500 KB, so cartridge games ARE the candidates. **The same
rule gives the opposite answer when the library is the opposite shape**, which a
fixed megabyte threshold could never do.

**Exempting small systems outright was considered and rejected**, though the
instinct behind it is right. A permanently exempt class can grow past the budget,
and then the disk is full of things nothing is allowed to delete — with
*Download All* (which this document says CabinetOS should offer, reversing
tvOS's call) that is not a hypothetical. This version has the same practical
effect and cannot reach that state.

When it is added, it is keyed on **size relative to the need**, never on
system: that needs no table of platforms to go stale, and a system's name was
never the thing that mattered.

##### Free a margin, not exactly enough

**Also changed, and it is the difference between eviction being an event and
eviction being constant.** "Free exactly enough for the incoming game" is the
obvious rule and on a disk that sits near its budget it means evicting on every
single launch, forever, until the cache holds nothing but the game currently
running. The person never sees it, they just never benefit from the cache again.

So free enough for the incoming game **and a margin beyond it**, so that the
next few launches cost nothing. The margin is a fraction of the budget rather
than a size — a tenth — which keeps it sensible on a 32 GB stick and on a 4 TB
drive without being told which one it is on.

##### "Least recently played" means on THIS console

Play history belongs to RomM and this document says the console should keep no
local notion of it. **Eviction order is a different question**: not "when did
this household last play this game" but "when did this machine last use this
copy". A game played on the Apple TV yesterday is not evidence that the copy on
this disk is worth keeping.

So the timestamp is a property of the cache, written when a ROM is launched
from it. Recorded here because it looks like the rule it is not, and because
depending on RomM would make eviction fail when the server is unreachable —
which is exactly when the console is least able to re-fetch anything.

##### The reserve, and what it is actually protecting

**Saves are kilobytes. States are the cost, and they are larger than they
look.** Measured on this project's own cores:

| | |
|---|---|
| Game Boy (Gambatte) | 26,882 bytes |
| Sega 32X (picodrive) | 679,178 bytes |
| Nintendo DS (melonDS) | **6,526,677 bytes** |

States deliberately do not overwrite — a history is the point — so one
well-played DS game can accumulate hundreds of megabytes on its own, and PS2
will be worse.

**A fixed reserve rather than a percentage is right**, because saves do not
scale with disk size — a 4 TB drive does not generate more save states than a
32 GB one, the same person plays the same games. That is the one place in this
section where a constant is the correct shape.

**But it should be sized for what is actually irreplaceable, which is far less
than it looks.** Saves, memory cards and save states all live on RomM once they
have been uploaded, and the local copies are caches of the server exactly as the
ROMs are. Cabinet already treats them that way — its own note says state caching
is "opportunistic, not queued", refreshed on ordinary online visits.

> **Nothing on this machine is irreplaceable except the upload queue.** Not the
> ROM, not the memory card, not the state history. Only what has been written
> and not yet sent.

That is a pending queue and room to write one more state — which is small on an
ordinary evening and **is not small on a bad one**. See *save-scumming while
offline* above: 360 states in two hours at 6.5 MB each is 2.3 GB of queue, and
PS2 is worse.

**So the floor is 2 GB, or 5% of the disk, whichever is smaller** — and it is
chosen knowing it does not cover that case, because **no floor can.** A reserve
protects one kind of data from another; it cannot protect data from itself. The
floor is sized to keep an ordinary session safe and to stop a download filling
the disk under a save, which are the failures it can actually prevent.

The other one is a conversation rather than a number, and it is recorded above
as unsolved.

**And keeping a game must respect it too.** Kept games are never evicted, so
without this check a person can keep enough games to starve the reserve and
leave the console with nothing it is permitted to delete. Keeping is the one
place the console may refuse.

##### Why this differs from a real console, deliberately

A PS5 never evicts. It tells you the disk is full and makes you choose, because
an installed game is a thing you put there and removing it silently would be
hostile.

**Ours is a copy of something still sitting on the RomM server.** Evicting is
not destruction, it is spending bandwidth later. That is the streaming-device
model rather than the console one, and it is the right one here.

> The console-like property being preserved is **that nobody has to think about
> storage** — not that deletion must be manual.

The Storage screen shows what is cached, what is kept, what is used and what is
free, and **lists what was cleared to make room** rather than letting things
vanish. Anything the person cared about was already protected by keeping it.

##### The numbers, decided

**These are decisions, not proposals.** They cannot be improved by more
thinking: settling them properly needs a full disk on real hardware, which does
not exist yet, and a starting value that gets corrected by a measurement is
strictly better than an argument that blocks the work. Build with these, change
them when a machine says otherwise, and record the reversal here when it
happens.

| | | |
|---|---|---|
| Ignore candidates below | **nothing — deferred**, then 1% of the space being freed if it is ever needed | oldest-first is what ships; the refinement waits for evidence |
| Free beyond what is needed | **10%** of the budget | so eviction is an occasional event rather than every launch |
| Save floor | **2 GB, or 5% of the disk, whichever is smaller** | it protects the upload queue and room for one more write, not the state history — those are on RomM |
| Unpacking headroom | **the archive + what its index declares**, transient | read from the archive, never estimated; not needed at all for `.chd`, `.rvz` or an arcade set |

**The floor is smaller than the five gigabytes first proposed** because of what
it turned out to be protecting. Five was sized for a full local state history,
and a state history is a cache of RomM like everything else.

##### Genuinely still open, and neither blocks building it

- **A long spell offline defeats the floor**, because the upload queue is itself
  the thing filling the disk and no reserve can protect data from its own
  growth. That is a "this needs to reach the server" conversation rather than a
  storage rule, and it belongs with whatever handles being offline for a week.
- **Per location, not global.** Open question 14 already says the cached/kept
  distinction applies per storage location. The budget, the floor and the
  eviction pass are all properties of the *active* location; this section is
  written as though there is one, and it should be read that way until there
  are two.

##### Two things to inherit rather than rediscover

- **Download All sizes up front and refuses**, rather than filling the disk and
  letting eviction sort it out — which would evict what it had just fetched.
  Cabinet's `DownloadAll.swift` already does exactly this.
- **The one failure the person ever sees** is "the disk is full of things you
  asked me to keep". Its wording belongs with the Storage screen, and the screen
  it points at already exists in the design.

#### A platform is not its slug, and "Arcade" is two platforms

**Measured against the live server 2026-09-14**, on RomM 5.1.0 with read-only
device-token access — 35 platforms, about 1,600 ROMs. The library contains two
platforms that are identical in every field a client would naively key on:

| `id` | `name` | `slug` | `fs_slug` | ROMs |
|---|---|---|---|---|
| 22 | Arcade | `arcade` | **FBNEO** | 141 |
| 45 | Arcade | `arcade` | **MAME2003** | 82 |

Same `slug`, same `name`, different `id` and `fs_slug`. **A client keying
platforms by `slug` silently loses 82 games**, and one keyed by `name` shows the
user two entries called "Arcade" with no way to tell them apart.

- **Key by `id`.** It is the only field that is actually unique.
- **Route to a core by `fs_slug`**, which is what carries the intent.
- **Take the display name from the manifest's `systems` field**, which already
  disambiguates them: *"Arcade (FinalBurn Neo)"* and *"Arcade (MAME 2003-Plus)"*.

##### "A core exists" and "this console has it" are different questions

**Found 2026-09-15 by using it.** After exiting a game, Home's hero was
*Mushihime-sama Futari* — an arcade game that cannot start, because FBNeo has
not been built. `catalog::coverageFor` was answering from Cabinet's manifest,
which says a core exists for arcade, and the console had no such `.so`.

So `Support` now carries **`NotInstalled`** beside `NoCore` and `Excluded`, and
a Playable answer is downgraded when the core file is not on disk. Three
distinct reasons a game is absent, and the difference is the whole point:

| | |
|---|---|
| `NoCore` | nothing in the manifest serves it — Jaguar, ColecoVision. Permanent. |
| `Excluded` | a core exists and Cabinet does not ship it — Game & Watch. Deliberate. |
| `NotInstalled` | **this console has not built it yet.** Temporary, and today it is most of them. |

**With two of twenty-one cores built: 217 playable games of 1644.** That number
is the honest one and it is the argument for the remaining cores. A console must
not offer what it cannot run — but collapsing "we haven't built it" into "you
can't have it" would have hidden how much of the library is waiting on work
rather than on a decision.

**This is deliberate on the server, not a scan artefact.** The split exists
because mame2003_plus was what ran on iOS, and FBNeo serves the companion
controller and light-gun cases. So the two arcade platforms are a real
distinction the library already makes, and **CabinetOS should surface them as
two arcade systems rather than merging them.** Both cores are pinned in the
manifest — `fbneo_libretro` at `2444fbe3`, `mame2003_plus` at `21256d24` — and
neither takes build arguments on any Apple platform.

It also sharpens the rule recorded under *Controls*: configuration is keyed by
**platform**, not by core. Here is the converse — two platforms that share a
name and a slug and must not share a core. Neither the core nor the slug
identifies anything on its own.

#### Cover paths are not URLs until they are encoded

**Found 2026-09-14, against the live server.** RomM returns cover paths with a
cache-busting query appended, and the timestamp in it contains a space:

```
/assets/romm/resources/roms/8/230/cover/small.png?ts=2026-02-08 21:13:30
```

curl rejects that outright — *"Malformed input to a URL function"* — so handing
the path straight to a fetch fails for **every cover in the library**. The
failure mode is what makes it worth writing down: an empty result reads as "this
cover failed", `ImageCache` draws nothing for a cover that failed, and the whole
library renders with no art and **not one error message anywhere**. A silent
total failure costs more to diagnose than a loud partial one.

`romm.cpp` encodes conservatively before fetching: anything already legal in a
URL is left alone — including a `%` that begins a valid escape, so an
already-encoded path is not encoded twice — and everything else becomes `%XX`.

**The general lesson, which is the reason this is in the specification rather
than only in a commit message: counting is not fetching.** The probe reported
`covers: 0 of 171 have art` and that number was *correct* — Game & Watch has no
art — while every cover on every other platform was broken. Two different
questions, and only one of them was being asked. The probe now fetches a real
cover and checks the bytes are actually a PNG or JPEG, because a 200 carrying an
HTML error page is equally useless to a texture upload.

**Not every platform has cover art**, and that is normal rather than a fault.
Game & Watch has none of 171. Any "no art" state in the UI has to look
deliberate, not broken.

#### Plain HTTP must work. This is a bug CabinetOS can simply not have

**Apple's App Transport Security refuses plain HTTP**, so a tvOS app talking to
`http://romm.local:8080` needs an explicit exception — and a self-hosted RomM on
a home LAN is *very often* exactly that. **CabinetOS has no ATS**, nothing on
Linux forbids plain HTTP, and the whole problem is therefore avoidable.

Avoidable, but only if it is not designed back in. Two ways it creeps in:

1. **Prefilling `https://` in the address field.** It reads as helpful and it
   pushes people toward a scheme their server does not speak. Removed: the
   field starts empty with `romm.local:8080` as the placeholder.
2. **Requiring a scheme at all.** The field should accept a bare host and port.

**What the client must do:**

- **Accept a bare host.** `romm.local:8080`, `192.168.1.50:8080`, `romm.lan`.
- **Probe rather than assume.** With no scheme given, try both and keep
  whichever answers — preferring `http` for an address that is obviously local
  (RFC1918, `.local`, `.lan`, a bare hostname) and `https` otherwise.
- **Remember which worked**, so the probe happens once.
- **Never refuse plain HTTP.** Not for a LAN address, not with a warning that
  cannot be dismissed.
- **Self-signed certificates: ask once, then remember.** A home server with
  HTTPS usually has a self-signed or internal-CA certificate, which is the
  second wall of the same kind. Silently accepting is wrong and silently
  refusing is worse; asking once about a server the person typed in themselves
  is the honest middle.

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

1. ~~**One core, at a pinned SHA.**~~ **DONE 2026-09-13, on the VM rather than
   in CI.** Gambatte: pinned, built, loaded, run, with Dr. Mario on screen and
   audio out. It covers two platforms, Game Boy and Game Boy Color. **Still
   owed: the same thing in CI**, so it is not a thing that works on one
   machine — which is the exact failure this whole open question is about.
2. **One hardware-rendered core.** Flycast, because it is also Dreamcast and
   Naomi, and because it is the one that proves the GL context and the
   no-readback path.
3. **One backend-sensitive core.** pcsx_rearmed, built twice — `DYNAREC=0` and
   the Linux default — with a state written by each loaded by the other. That
   answers the parity question locally even if the Mac↔Apple TV test never
   happens.
4. **Genesis Plus GX next**, before the rest: software, no recompiler, and the
   best coverage in the set — one build is Genesis, Sega CD, Master System and
   Game Gear. Confirm each of those four separately, per the rule above; Sega CD
   in particular writes its saves by a different mechanism than the other three.
5. **The rest**, which by then are a loop.
6. **Dolphin and PCSX2 last**, as their own `.so` files, against upstream PCSX2
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

**Controls are a bigger piece of work here than they were in Cabinet** — see
*Controls*. Three things in this phase have no equivalent in the reference
implementation and cannot be ported from it:

1. **A remapping screen**, because Linux accepts controllers SDL has never seen.
   It has to be drivable with the very pad whose buttons are wrong, so it must
   ask by position — "press the button below the others" — not by name.
2. **Bluetooth pairing**, over bluez and D-Bus. The first controller cannot be
   paired using a controller, so USB-first has to work and first-run setup has
   to say so.
3. **Player assignment** that is visible and survives a pad sleeping and
   reconnecting. Four-player arcade and the GameCube adapter make this real.

**Wired is the bootstrap, not the fallback.** A first-run screen should offer
USB as the thing that simply works and Bluetooth as the convenience, not the
other way round. USB is deterministic; Bluetooth on a cheap mini PC is the part
that might not come up at all, since some combo cards need firmware blobs.
Three first-run states, not two: searching, found, and **no adapter — plug
something in**.

**Do NOT auto-pair the first gamepad discovered.** It is the obvious design and
it is wrong: the first advertisement in range may be a neighbour's pad, the
user's own second controller, or one belonging to the console beside it, and
pairing it silently gives no clue what happened.

> **Pair on a button press, not on discovery.** Show what was found, and let the
> pad that sends the first input become player one. The confirmation is the very
> input being established, which is why it costs the user nothing and cannot
> pick the wrong device.

And the discovery filter is less clean than it sounds. Class-of-Device
peripheral/gamepad catches DualSense, DualShock and most 8BitDo pads.
**Xbox controllers are the awkward case** — a proprietary Bluetooth profile
rather than plain HID, historically finicky under BlueZ. Several pads also only
enter pairing mode on a held button combination the user has to know, so the
screen has to name it per brand or at least say "hold the pairing button".

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
| **genesis_plus_gx** | `HAVE_CDROM` left at its default of 0 | `HAVE_CDROM=1`, set by a `uname -s` test inside the `unix` branch | `HAVE_CDROM=0` |

**genesis_plus_gx was added 2026-09-13, and it is not a recompiler.** It was
found while preparing the second core, and it matters because it shows the
divergence is not only about CPU backends — the shape of the problem is wider
than the table's first five rows suggested.

`Makefile.libretro` defaults `HAVE_CDROM = 0` at line 7. The `unix` branch then
does this, and no Apple branch does anything equivalent:

```make
ifneq ($(findstring Linux,$(shell uname -s)),)
  HAVE_CDROM = 1
endif
```

which reaches the compiler as `-DHAVE_CDROM`. It is the libretro **physical
CD-ROM drive** interface, and it lands on Sega CD — the one platform of the four
this core serves that the handover already flagged as saving by a different
mechanism than the other three.

##### MEASURED 2026-09-14: it does not touch the save state format

Both variants were compiled on the test VM and their object files compared.
**Of 115 object files, exactly three differ**, and all three are libretro's
VFS/CD-ROM plumbing:

```
libretro/libretro-common/cdrom/cdrom.o
libretro/libretro-common/vfs/vfs_implementation.o
libretro/libretro-common/vfs/vfs_implementation_cdrom.o
```

**Nothing under `core/` differs.** `core/state.c` is byte-identical, and so is
every `core/cd_hw/*` object — `cdc`, `cdd`, `scd`, `pcm`, `gfx`, `cd_cart`. The
save state is produced entirely by `core/`, so the flag cannot affect it. The
source agrees: `HAVE_CDROM` appears in exactly two files in the whole tree, both
under `libretro/libretro-common/`, and in none under `core/`.

**This is stronger than the cross-load test that was planned**, and it cost no
ROM transfer. A state written by one build and loaded by the other would have
proved that one game's state survives. Comparing the objects proves the entire
emulation core is the same machine code — for all 22 Sega CD titles in the
library, and for the three cartridge systems as well.

**The test is not vacuous.** The standing rule is that a test must first show
the thing it measures actually varies, and it does: the `HAVE_CDROM=1` build is
`b217941923…` against the pinned build's `78a2522871…`. The flag changes the
binary. It just changes none of the binary that matters here.

**The decision stands and the reason narrows.** Keep `HAVE_CDROM=0`, but no
longer out of save-state caution — that is answered. It stays because the
console has no optical drive, so `=1` compiles in three objects of physical-CD
access that can never run, and because matching Cabinet is free. **Stop carrying
the caveat.**

##### FOURTEEN CORES, 2026-09-15 — and what building them taught

Twelve added in one pass, every pin and build argument read from the manifest.
**217 playable games became 934 of 1644.** (Fifteen by the end of that session,
986 games; twenty and 1100 the next day, when the five below landed.)

Three things the pipeline had to learn, each found by building rather than by
reading:

**Upstream output names do not match manifest names, and there is no rule.**
`beetle_ngp` produces `mednafen_ngp_libretro.so`. A hand-maintained table of
twenty-one such names goes stale, and the frontend would need a second copy of
it. So `build-core.sh` discovers whatever `*_libretro.so` the build produced and
files it under the core as the **manifest** knows it — the identity the pins,
the emulator tags and `catalog.cpp` already use. The rename is printed, never
silent. A name already ending in `_libretro` does not get a second one, and
`catalog.cpp` carries the same rule with a comment on both sides saying so.

**Some cores cannot be asked what revision they are, and that is upstream's
bug.** `beetle_pce_fast` and `beetle_saturn` both report a bare version: their
`libretro.c` is a **C** file using `GIT_VERSION`, while their Makefile adds
`-DGIT_VERSION` to **`CXXFLAGS`** only, so the define never arrives and the
empty-string fallback wins. Verified on both rather than assumed from the
matching symptom.

> **Not patched into working.** Adding the missing flag would change our binary
> against Cabinet's, which builds the same upstream with the same blind spot,
> and diverging from Cabinet to satisfy our own test is backwards.
> `VERIFY_REVISION=0` marks such a core with its reason. The *checkout* is still
> asserted at the pinned commit; only reading it back is lost.

**And FBNeo reads archives itself** — its `valid_extensions` are
`zip|7z|cue|ccd`. An arcade set must be handed over **unextracted**, which is
exactly what *ask the core, never the platform* already does. The rule was
written before anything needed it and turned out to be load-bearing on the
first core that did.

##### And the build turns out to be reproducible across machines

The same commit built on a GitHub `ubuntu-24.04` runner and on the Fedora test
VM produced **byte-identical** artifacts:

| | |
|---|---|
| gambatte | `b2ee839c226af409765b083e17b211265a44ca184f5ef85763fe003ad63ca582` |
| genesis_plus_gx | `78a252287120173a8acc65c6d345dbdecb2fd7b0805a28f7df48c05baed3699b` |

Found incidentally while setting up the comparison above, and worth more than it
looks. *"Both machines can build it"* is a weaker claim than *"both machines
produce the same bytes"*. The second means a sha256 in a CI log is a fact about
the revision and the flags rather than about the machine, so a core can be
checked against it anywhere — and it means a future mismatch is a real signal
rather than noise to be explained away.

**Decision: build with `HAVE_CDROM=0`.** The conservative choice is free here.
The console has no optical drive and never will — ROMs arrive from RomM as
files — so the lever disables a feature the hardware cannot use, and matching
Apple costs nothing to get it.

**The manifest corroborates it.** `build_args` is `null` for `ios`, `tvos` and
`mac`, so nothing overrides the line-7 default on any Apple platform. Verified
at the pinned commit `a7985a9c`, not merely at upstream `master`: the default,
the `uname` test and the absence of any `HAVE_CDROM` in the Apple branches are
all identical there.

**And the manifest records a second divergence of the same shape, in vecx:**
`HAS_GPU=0`, because *"Makefile defaults HAS_GPU=1 off macOS, which builds a
GLES2 path this frontend cannot drive."* Cabinet already passes that on both
Apple platforms. It is a third instance of the pattern — the `unix` branch
asking `uname` what machine it is on and changing the build — and it is the
reason the `build.<platform>` field exists. **Read `build_args` from the
manifest before building any core**, rather than assuming an empty
`MAKEARGS` because the core is not in the recompiler table.

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

##### CORRECTION, 2026-09-16: two of the eleven were recovered from the archives

"The archives embed nothing" was checked with `strings` across several cores and
it is not true of all of them. It is not true of the two that were looked at
today, and both were on the unrecoverable list:

| | |
|---|---|
| `libpicodrive_tvos.a` | `2.05-733c711` — **the pinned revision**, and the same one the Mac ships |
| `libmgba_tvos.a` | `e31759b24e7a4e3899285ff720d7b573ac328ae7`, in full |

So tvOS picodrive was never behind; only iOS was, at `6248b51`. The manifest
records picodrive as diverging across platforms with tvOS unknown, and the
answer was in the binary the whole time.

**The mechanism is the one this document found later and did not go back and
apply**: a core that compiles `git rev-parse --short HEAD` into its version
string carries that string into the archive. The recovery ran before that was
understood, which is why it concluded unrecoverable.

> **Worth an hour, Cabinet-side: run `strings` over the other nine tvOS
> archives.** Every Makefile-based core in the set has the same `GIT_VERSION`
> line, so several more revisions are probably sitting in the artifacts. Each
> one recovered turns "unknown and unknowable" into a fact, and shrinks the
> regression surface the realignment release has to carry.

##### And two cores lost their revision on Cabinet's side, in opposite ways

Both found by comparing our builds against the shipping archives:

- **mGBA's Mac archive reports `e31759b24-dirty`.** Cabinet's Mac build carries
  a working-tree modification that no script applies — the same class of problem
  as Flycast's unscripted edits, in a core whose manifest entry lists no patches
  at all. The iOS and tvOS archives are clean at that commit.
- **melonDS's archives report `melonDS 0.9.3` with no revision**, while the same
  upstream built here reports `0.9.3 66b5d26`. Its Makefile has the
  `GIT_VERSION` line and it reached our binary, so something about Cabinet's
  build is losing it — the same failure `build-core.sh` was taught to prevent by
  passing `safe.directory` through the environment.

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

##### BUILT IN CI, 2026-09-13 — and the build-time assertion now exists

`.github/workflows/build-core.yml` builds the core matrix on a GitHub runner,
in the same Fedora 44 builder container the frontend and the test VM use. The
first green run took about 45 seconds from a bare checkout.

More importantly, the assertion this question asks for — *"asserts at build
time that what it produced matches"* — is implemented, and it checks the
artifact rather than the checkout:

> `build-core.sh` asserted the **checkout** was at the pinned commit. That
> proves what went in, not what came out.

Every Makefile-based core in the set compiles its own revision into the string
it reports through `retro_get_system_info`:

```
GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
```

`tools/core-info.c` reads that back out of the finished `.so`, and fails the
build if it is not the pinned revision. It `dlopen`s with `RTLD_LOCAL` the way
the frontend does, and checks the libretro API version against the header the
frontend was compiled against. **No ROM is needed** — `retro_get_system_info`
is documented as callable before `retro_init`, which is the only reason any of
this is runnable in CI, where there is no game to give it.

What gambatte reports, from the first CI run:

```
name        Gambatte
version     v0.5.0-netlink d9d6cd0
extensions  gb|gbc|dmg
api         1
revision    d9d6cd0, as pinned
sha256      b2ee839c226af409765b083e17b211265a44ca184f5ef85763fe003ad63ca582
```

**The `-netlink` suffix was checked, not assumed.** A Linux build quietly
turning on what the Apple build has off is the entire subject of this question,
so `HAVE_NETWORK` was read from the Makefile: it is set in the `unix` branch and
in both the `ios-arm64` and `tvos-arm64` branches. All three agree. This is
parity holding, not drift.

**One live failure mode was found and closed while writing the assertion.** Git
refuses a bind-mounted tree it considers dubiously owned; the core's own
`|| echo unknown` swallows that silently; and the result is a core that does not
know what revision it is — which is exactly the fact this question is about.
`build-core.sh` now passes `safe.directory` in through `GIT_CONFIG_*` rather
than writing a gitconfig into the mounted tree.

**What CI still cannot answer.** Save-state parity needs a ROM, and no ROM
belongs in this repository. `tools/state-probe.c` remains the instrument and the
test machine remains where it runs.

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

#### ANSWERED 2026-09-15, for pcsx_rearmed: the CPU backend does not break states

**The question this section was written to pose.** Read from the source at the
pinned commit `ba61a4fd`, after an object-file comparison of `DYNAREC=0` against
`DYNAREC=lightrec` showed `libpcsxcore/misc.o` differing — which is where
`SaveState` and `LoadState` live, and looked at first like the bad answer.

It is not. `SaveState` and `LoadState` both call `ndrc_freeze`, and that
function is built to be read by a core with a different backend from the one
that wrote it:

- **Saving with no recompiler blocks writes nothing.** `new_dynarec_save_blocks`
  returns 0 and `ndrc_freeze` returns before writing a byte.
- **Loading tolerates the section being absent**: the 8-byte `"ariblks"` header
  fails to match, the reader seeks back, and the state continues to parse.
- **Loading tolerates it being present and useless**: the size is read, the data
  is consumed, and then `if (psxCpu != &psxInt) new_dynarec_load_blocks(...)`
  declines to apply it on an interpreter.

And what the section holds is **block addresses** — a recompiler cache hint, not
emulated machine state. That is why it is optional at all.

**A lightrec build writes no section either.** The real implementation is gated
`#if !defined(DRC_DISABLE) && !defined(LIGHTREC)`, so LIGHTREC takes the same
stubs the interpreter does.

##### What this settles

> **Cabinet's single `pcsx-rearmed-native` tag across `DYNAREC=0` on iOS/tvOS
> and `DYNAREC=ari64` on the Mac is correct, not a latent bug.** It had looked
> like one: the same tag on two different CPU backends is exactly the
> configuration this document warned could silently produce unloadable states.

**So CabinetOS can take the faster Linux backend and still share the tag.** On
Vega integrated graphics that is the difference between comfortable and
marginal for PS1, and it was the thing this question was holding back.

**Scope, stated precisely.** This is pcsx_rearmed, read from its source rather
than measured by cross-loading a state. melonDS, picodrive, Flycast and
mupen64plus each need the same check before their tags are shared — a different
core may put real machine state behind its dynarec, and nothing here says
otherwise. The object-file diff is the way in: it took two minutes and pointed
straight at the one file worth reading.

#### ANSWERED 2026-09-16, for melonDS and picodrive — and this time it was measured

The scope note above asked for the same check on four more cores. Two of them
are now done, and done better than pcsx_rearmed was: not read from the source
and believed, but **run**, with each build loading the other's state and each
build loading its own as the control.

`cores/backend-diff.sh` builds a core twice, changing one variable, and reports
which objects differ. `cores/hash-objects.py` is what makes that answer true.
Both are new, and between them they are the instrument this question needed.

##### melonDS: take the recompiler, share the tag

**The lever.** The unix branch sets `JIT_ARCH=x64` on x86-64; Cabinet's iOS and
tvOS builds set nothing and run the interpreter, its Mac sets `aarch64`. Same
shape as pcsx_rearmed, and Cabinet again ships both under one tag.

**The object diff** put the difference in the machine, not beside it: ten
objects changed, including `NDS.o`, `DSi.o`, `ARM.o` and `CP15.o`, plus twelve
that exist only in the recompiler build. That is a much larger footprint than
pcsx_rearmed's single `misc.o`, and it is why reading was not enough here.

**The source says it is deliberate.** In the whole core there are exactly two
`#ifdef JIT_ENABLED` blocks inside any `DoSavestate`, both guarded
`if (!file->Saving)`, and neither writes a byte. Nothing about the recompiler is
ever stored. On LOAD a JIT build repairs what an interpreter state does not
carry and throws its block cache away:

```c
// hack, the JIT doesn't really pipeline
// but we still want JIT save states to be
// loaded while running the interpreter
FillPipeline();
```

and, at the end of `NDS::DoSavestate`, `ARMJIT::ResetBlockCache()` and
`ARMJIT_Memory::Reset()`. The interpreter build even keeps the `JIT_Enable`
variable, with upstream's comment "Needed for savestate".

**And then it was run**, against Tetris DS through `tools/state-probe.c`:

| | |
|---|---|
| 600 frames from boot, interpreter | video `f359e84a8fed0383`  audio `675a983465de49b3` |
| 600 frames from boot, recompiler | video `f359e84a8fed0383`  audio `675a983465de49b3` |
| state size, both | 6,526,677 bytes |

Every load combination — each build's own state and the other's — ran on to the
same digest, `41b5c97d81c50383`, with the two own-state runs as the control.

**The trap that would have made this prove nothing**, and it is this document's
own: an unanswered core option. `state-probe` answers `GET_VARIABLE` with NULL,
so `melonds_jit_enable` never reaches the core, and if `Config::JIT_Enable` had
then been zero the "recompiler" build would have run the interpreter and the two
sides would have been identical for the most boring possible reason. It is
`int JIT_Enable = true` under `#ifdef JIT_ENABLED` — a C++ initialiser rather
than a zeroed C global — and the run confirms it, printing "Resetting JIT block
cache" on that side only.

> **CabinetOS builds melonDS with `JIT_ARCH=x64` and writes `melonds-native`.**

##### picodrive: the states are identical, and we take the interpreter anyway

**The lever** is `use_sh2drc`, the SH2 recompiler the 32X needs two of. It
defaults to 1 on x86-64; Cabinet gets 0 from the Makefile's own Apple block,
turned off there for code-signing reasons that do not apply to this console.

**The states are byte-identical.** Both builds run to frame 600 and write states
that `cmp` reports as not differing at all. The source agrees: `sh2_pack` copies
`SH2_REG_SIZE` bytes, which is `offsetof(SH2, macl) + sizeof(macl)`, and every
drc field in the struct sits after `macl`. `SH2_STATE_SIZE` is a compile-time
constant either way.

**But the two backends do not produce the same picture.** From an identical
boot they diverge in video and audio digest within 60 frames while converging on
that identical machine state — something timing-visible lands differently. Each
build is deterministic on its own (same digests twice), so it is the backend.

So the choice is not about states at all, and it comes down to this: nothing
here needs the recompiler. The 32X is two 23 MHz SH2s and this is an x86-64
console. Taking it would buy performance nobody is short of and pay for it with
a picture that differs from the Apple TV's.

> **CabinetOS builds picodrive with `use_sh2drc=0` — exact flag parity with
> Cabinet — and writes `picodrive-native`.** Revisit only with a measurement
> from real hardware, knowing the states will survive the change.

##### The instrument itself needed fixing first, and that is the lesson

The picodrive comparison first reported **102 of 103 objects differing,
including zlib's** — which no CPU backend can reach. Read as a result, that
number costs this core its shared tag.

It was not a result. A control run with **the same setting on both sides**
reported the same thing, and the cause is that picodrive builds with `-flto`:
GCC writes a random per-invocation id into every LTO section name.

```
.gnu.lto_.profile.3bda9114828bb356        first build
.gnu.lto_.profile.ca2804fcf73ae283        second build
```

`cores/hash-objects.py` replaces that id with sixteen zeroes — length
preserving, so nothing in the file moves — before hashing. That took the noise
from 102 objects to about 12, and the remaining 12 are a different set each
time.

**The deeper point is that on an LTO core the objects are not the emulator.**
The machine is generated at link time, so the intermediate objects hold compiler
bytecode and only the artifact is meaningfully reproducible. And it is:
**four builds of picodrive produced four byte-identical `.so` files** while
differing in a random handful of objects each time. `backend-diff.sh` now says
so itself and points at `state-probe` instead when it sees that pattern.

Two things follow for the rest of this document:

1. **"The build is reproducible" is a per-core claim.** It was established for
   gambatte and genesis_plus_gx, neither of which uses LTO. picodrive is
   reproducible where it counts and not at the object level, and a future core
   may be neither.
2. **Run the control first.** `backend-diff.sh` takes the same setting twice and
   reports identical-is-a-pass, which costs one pair of builds and is the
   difference between a measurement and a number.

##### mupen64plus: it builds, and the tag stays unshared

This core has **four** levers rather than one — `WITH_DYNAREC`, `FORCE_GLES3`,
and the `LLE` / `HAVE_PARALLEL_RSP` / `HAVE_PARALLEL_RDP` / `HAVE_THR_AL` group
that selects low-level RSP and RDP emulation. Cabinet turns the last four on and
the unix branch leaves them off, so "match the CPU backend" was never the whole
job here.

**And matching Cabinet's backend does not link.** With `WITH_DYNAREC=` empty,
`cp0.c`, `interrupt.c` and `r4300_core.c` still reference `dyna_jump`,
`dyna_stop` and `dynarec_jump_to`, because those calls are guarded by
`#ifndef NO_ASM` rather than by `WITH_DYNAREC`. Cabinet's ios-arm64 case adds
`-DNO_ASM`; the unix case has no equivalent, so the .so fails to link with five
undefined references. `DYNAFLAGS` is the only variable that can carry the define
in from the command line without replacing a flags variable wholesale.

**One difference is recorded rather than matched.** Cabinet also adds
`-Ofast -funsafe-math-optimizations` to three flags variables that cannot be
extended from the command line, so this build gets the unix branch's
`-O3 -ffast-math`. That is a floating-point difference in an emulator whose
output is floating point.

> **No emulator tag for mupen64plus.** It cannot be settled while the core
> cannot run here, and an unshared tag costs nothing today because nothing can
> write an N64 state yet.

##### And running one of them found a save bug that building them could not

melonDS is the first core CabinetOS ships that writes its own save file rather
than exposing `RETRO_MEMORY_SAVE_RAM`. Launching Tetris DS from the real library
printed:

```
Save file: /Tetris DS.sav
```

At the **root of the filesystem**, where it cannot be written. The frontend was
passing its save directory to `loadGame`, and melonDS reads the directory in
`retro_init` — inside `Core::load`, which happens first — copies it into a
static buffer and never asks again. It had been handed an empty string.

**The failure is silent.** The game runs, the save never lands, and nothing says
so. And it is not a melonDS bug: it hits every core that writes its own save
file, which is the class Cabinet already lost saves to once by a different
route — Neo Geo Pocket, Sega CD, FBNeo's NVRAM, Dreamcast's VMU. Two of those
have been "playable" on this console for days.

`Core::setDirectories` now exists and is called **before** `load`, at both call
sites. Verified by running it again: `romcache/saves/Tetris DS.sav`.

> **This is the argument for launching a core rather than building it.** Every
> assertion in the build pipeline passed on that core — pinned commit, asserted
> revision, reproducible artifact — and none of them could see this.

**Still open, and now concrete:** the sync layer only knows about
`RETRO_MEMORY_SAVE_RAM`, so `[save] battery is 0 bytes` is correct for melonDS
and the `.sav` beside the ROM is not uploaded to RomM at all. Phase 4 owes the
file-writing class its own path, the way `MemoryCardSync` does in Cabinet.

##### A core that is built and still cannot be run is a third thing

Building Flycast and Mupen64Plus would have made `catalog::coverageFor` call
Dreamcast and N64 **Playable**, because its installed-check is "is the .so on
disk". They are not: both render through `RETRO_ENVIRONMENT_SET_HW_RENDER`,
which `core.cpp` refuses. That is the hero-offering-an-arcade-game bug again,
one layer further in.

So `Support` now carries **`NeedsHardwareRender`** beside `NoCore`, `Excluded`
and `NotInstalled`. Four answers, and they lead to four different pieces of
work — which is the whole reason this document warned against collapsing them.

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
  cannot tell. **There is now a cheap fix**: several cores compile their own git
  revision into the version string they report, so the frontend can assert the
  loaded core against the manifest and Cabinet can put the revision in the tag.
  Verified on Gambatte — see Phase 3. Needs checking per core, and the Cabinet
  half is a Cabinet-side change.
- ~~**Nothing has been compiled.**~~ **DONE, 2026-09-13.** Gambatte built for
  Linux x86-64 at its pinned commit with `make platform=unix`, **first attempt,
  zero patches**, and Dr. Mario runs on it. See Phase 3. The remaining twenty
  are now a loop rather than a question — but they are still twenty, and the
  backend-sensitive ones still need their flags set explicitly.
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

### 15. A phone companion page for first run
**Raised 2026-09-13 as a design proposal. NOT ADOPTED YET — deferred, with
reasons. Revisit after the on-screen keyboard exists.**

The proposal: CabinetOS serves a small web page during first run and shows a QR
code pointing at itself, so the RomM address, credentials and Wi-Fi details can
be typed on a phone instead of with a D-pad. Off afterwards, with a toggle.

**The parts that are right, and are recorded elsewhere rather than here:**
directional navigation as an architectural rule, the on-screen keyboard as a
properly designed screen rather than an afterthought, showing every platform the
server exposes and marking the unsupported ones, and USB as the input bootstrap.
Those are in Phases 3, 4 and 6 already.

**Why the companion page is deferred.** Three reasons, in order of weight.

1. **It is a fix for a pain nobody has felt yet.** The on-screen keyboard does
   not exist. Until it does, and until somebody has typed on it from a sofa,
   there is no measurement of how bad the problem is — only an assumption that
   it is bad. Build the thing that might make the companion unnecessary first.

2. **The problem is one hostname, not "a URL and credentials."** Cabinet's tvOS
   flow, read from the source: `ServerSetupView` takes the address and nothing
   else, then `PairingView` shows the approval URL **as a QR code** for the
   phone that is already signed in to RomM. The password is never typed on the
   television at all. CabinetOS should copy that flow, and when it does, the
   entire D-pad typing burden is a hostname, once. A whole second interface is a
   lot of machinery to save twenty characters.

3. **It cannot solve the case that would justify it.** Wi-Fi credentials are the
   genuinely painful typing, and a page served by the console is unreachable
   from a phone when the console is not on the network yet. Solving that needs
   the console to bring up its own access point — a large piece of work, and
   often impossible while the same radio is also meant to be a client. So the
   companion addresses the small problem and not the big one.

**If it is built anyway, the security surface is the part to get right**, and
"it is only a home LAN" is not a mitigation: a home LAN contains guests, IoT
devices and a television with an advertising SDK in it.

| | |
|---|---|
| **A one-time code shown on the screen**, carried in the QR and required by the page | The one that matters. Turns "anyone on the LAN" into "anyone who can see the television". |
| **Time-limited, not merely "off after first run"** | A setup abandoned half way otherwise leaves it listening forever. |
| **Write-only** | Never echo a stored credential back to the page. |
| **Guard the address field itself** | The real attack is not reading the page, it is pointing the console at an attacker's "RomM" and harvesting the token it then goes and fetches. |
| **Not HTTPS** | A self-signed certificate on a LAN teaches the user to click through certificate warnings, which is worse than the thing it fixes. |

**And the question the proposal already identifies is the right one to settle
first: rescue tool, or companion product?** The answer should be *rescue tool*,
and it should be enforced structurally rather than by intention — one page, no
navigation, time-limited, off by default. **If it ever grows a menu, it has
become a product**, with a second design language and a second set of bugs, and
nobody decided to build that.

*Do not resolve before the on-screen keyboard has been used on a television.*

### 16. Is a mouse supported, or not?
**Raised 2026-09-13. Two documents currently disagree. Needs a decision, not a
default.**

*The input model* in this document says: **Supported — keyboard and mouse. A
convenience, never a dependency.**

The first-run design proposal says the opposite, and gives a good reason:
*"pointer input pulls the design toward hover states and click targets, which is
a different interaction model."* That is true, and it is the reason tvOS has no
pointer.

Both cannot stand. The honest options:

1. **Mouse is not supported.** Amend the input-model table. Cleanest, and it
   matches how the design is actually being built.
2. **A mouse moves focus and clicks the focused thing, and nothing else** — no
   hover states, no pointer, no cursor. Cheap to keep true, and it means a
   plugged-in mouse does something sensible rather than nothing.

What must not happen is leaving the table saying "supported" while no screen is
built for it, because that is a promise the product does not keep.

**Keyboard is not in question** and is settled: every screen must be fully
operable by directional input plus confirm and back, from whatever device
supplies them. That is already an architectural rule rather than a feature.

### 17. Wi-Fi credentials on a controller-only console
**Raised 2026-09-13. The mechanism is DECIDED; the UI is Phase 6.**

The hard case in first-run setup, and the one the companion page (open question
15) cannot solve: a page served by the console is unreachable from a phone when
the console is not on the network yet.

#### The permissions question, answered first, because it is the blocker

Checked on the running image. **The frontend can do the entire Wi-Fi flow
without a password prompt**, which a console has no way to answer anyway:

| polkit action | `allow_active` | needed for |
|---|---|---|
| `wifi.scan` | **yes** | listing networks |
| `network-control` | **yes** | activating a connection |
| `enable-disable-wifi` | **yes** | turning the radio on |
| `settings.modify.own` | **yes** | saving a user-scoped connection |
| `settings.modify.system` | `auth_admin_keep` — **but see below** | saving a connection that comes up at boot |

The system-wide case is granted by a rule Bazzite already ships:

```js
if (action.id == "org.freedesktop.NetworkManager.settings.modify.system" &&
    subject.isInGroup("wheel") && subject.local) return polkit.Result.YES;
```

**The session user is in `wheel`, so this works today — by inheritance, not by
design.** `wheel` also means sudo, and Phase 6 tightening security around
developer mode is exactly the change that would remove the session user from it
— **silently breaking Wi-Fi configuration**, presenting as a network bug rather
than a permissions one. Same class of trap as the controller ACL in *Controls*.

**So CabinetOS should ship its own polkit rule** granting that one action to the
session user directly, rather than depending on `wheel`. Cheap, and it decouples
"can configure the network" from "can become root", which are not the same
privilege and should not be the same grant.

#### The ladder, in the order the UI should offer it

1. **Ethernet — and skip the screen entirely when it is already up.** Do not ask
   someone to confirm a network they are already on. A console under a
   television is very often within reach of a cable, and this path involves no
   typing at all.
2. **The on-screen keyboard.** The baseline, and **this is what every console
   does** — PlayStation, Xbox, Apple TV and Switch all make you type the
   passphrase with a controller. It is not a product failure, it is the normal
   experience, and the keyboard has to exist for the RomM address regardless.
3. **A USB keyboard.** Thirty seconds, and people have one in a drawer. The
   input model already requires that one works wherever text is entered, so this
   costs nothing beyond saying it on screen.
4. **A phone on a USB cable.** Enable tethering and the console is online
   immediately — NetworkManager picks it up as an ordinary ethernet device with
   no configuration at all. **This already works and needs no code.** It is
   worth naming on the screen because nobody thinks of it, and it turns an
   unreachable console into a connected one in seconds.

#### Against the console running its own access point

The "proper" IoT answer — broadcast a setup network, have the phone join it,
serve the page there. **Decided against for now**, on four counts:

- **Hardware-dependent.** Plenty of cheap combo cards do AP mode badly or not at
  all, and this project's hardware rule is that capability is discovered rather
  than assumed.
- **It is a mode switch, not an addition.** While acting as an access point the
  radio generally cannot also scan for the network it is meant to join.
- **The phone loses internet** while attached to it, which reliably confuses
  people and makes them abandon the flow.
- `wifi.share.open` is **denied** even to an active session in this image, so it
  needs a polkit change too.

Revisit only if the on-screen keyboard turns out to be genuinely unusable on a
television, which is a thing to find out rather than assume.

#### Three decisions that matter more than the mechanism

1. **Show the password by default.** Every console hides it and every console is
   wrong: there is nobody shoulder-surfing a living room, and not being able to
   see what you typed *is* the entire difficulty. This one choice removes most
   of the pain for free.
2. **Skip the screen when already online.**
3. **Remember networks.** NetworkManager does this for nothing.

And three details that will otherwise be discovered late: **hidden SSIDs** need
a "join another network" path where the name is typed too; **the passphrase is
usually being read off the underside of a router**, so digits and symbols must
not be buried behind a shift layer; and **802.1X enterprise is a different form
entirely** — out of scope, and better refused plainly than half-supported.
