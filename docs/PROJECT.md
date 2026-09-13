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
- **Hardware capability is discovered, not assumed.** The SER5 has no HDMI-CEC;
  a future machine might. Features that depend on hardware that may or may not
  be present are detected at runtime and degrade gracefully, rather than being
  designed out because the current box lacks them.
- **AMD for now.** Bazzite publishes NVIDIA variants of its images, so an NVIDIA
  machine is a base-image change rather than a rewrite — but it doubles the
  images to build and test, so it stays out of scope until there is hardware
  that needs it. See open question 11.

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

Games are downloaded on demand and cached locally. The user can see what is
cached and how much space is free, and can evict things.

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
- **Settings** — account, storage, controllers, display, system update.
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
| **Cabinet** (iOS/tvOS, Swift) | https://github.com/MMagTech/cabinet | **The reference implementation.** Source of truth for both the frontend design language and for correct RomM client behaviour. When in doubt about how a screen should look or how an API call should be made, read Cabinet. |
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

*Not yet verified:* nothing in this repo has been built or booted. The first CI
run is the first time any of it executes. See Open questions 1 and 2 — the
removal lists are deliberately conservative and partly unproven.

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

*Done when* it looks and feels like Cabinet, and every screen can be reached and
left with a controller alone.

### Phase 4 — RomM integration
**Status: not started.**

Device pairing and auth, library sync, artwork and metadata, on-demand
downloads with a queue and cache management, firmware and BIOS retrieval, save
and save state sync.

*Done when* the real library is browsable and a game downloads.

### Phase 5 — Emulators and launching
**Status: not started.**

Emulators bundled into the image. Games launch and return cleanly with no
visible desktop. Controller mapping per system, per-system configuration, save
states wired to the sync layer.

*Done when* several systems are playable end to end.

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

*Done when* a full system update happens without a keyboard.

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

Resolve in Phase 2, once there is a VM boot test to validate against and once
the frontend toolkit is chosen (Phase 3) so we know what Qt/Wayland libraries
are actually needed. `build_files/strip-desktop.sh` contains a commented-out
candidate list to start from.

### 2. Does removing `steam` also remove the controller udev rules?
**Raised: Phase 1. Mitigated, not verified.**

The `steam` package ships (or depends on a subpackage that ships) udev rules
covering a wide range of gamepads. Those rules are exactly what CabinetOS needs
and Steam itself is exactly what it does not.

Mitigation applied: all removals use `dnf5 remove --no-autoremove`, so
dependencies are not swept up with the named packages. The strip script also
logs the udev rules present in the final image.

**To verify:** check the CI build log for the `steam-devices` / udev rule
listing, and confirm gamepads still enumerate in the Phase 1 VM test.

### 3. `bazzite` or `bazzite-deck` as the base?
**Raised: Phase 1. Decided provisionally, revisit in Phase 2.**

Phase 1 uses plain `bazzite` (Kinoite/KDE based), per the brief.

But `bazzite-deck` ships infrastructure that Phase 2 will otherwise have to
build from scratch:

- `gamescope-session-plus` — the session harness that launches a fullscreen
  application under gamescope, which is precisely the Phase 2 requirement
- `inputplumber` — controller input remapping daemon
- `steamos-powerbuttond` — power button handling, which is a Phase 2 requirement
  ("shutdown and suspend reachable from a controller")

The cost is that all of it is Steam-coupled and would need untangling.

Revisit at the start of Phase 2, before writing a session from scratch. The
decision is: adapt `gamescope-session-plus` from `bazzite-deck`, or write our
own minimal session on plain `bazzite`.

### 4. Bundle libretro cores directly, or build on RetroDECK?
**Raised in the brief. Deliberately unresolved — do not resolve this now.**

To be answered no earlier than Phase 5.

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

### 10. Waking the machine with a controller
**Raised: Phase 1. Unresolved. Phase 6 owns it.**

The input model says a controller must be sufficient. That includes waking the
machine, and whether it is achievable depends on hardware:

- The reference SER5 has no HDMI-CEC, so a TV remote cannot drive it and it will
  not wake when the TV does. Other machines may have it, which is exactly the
  kind of capability that must be detected rather than assumed.
- Waking from suspend over Bluetooth depends on the controller, the adapter and
  the firmware, and is unreliable in general.

The portable fallback is to never suspend and blank the display instead, trading
idle power for a machine that is always ready. That is a defensible default for
a console and works on any hardware. If CEC is present, use it; if not, fall
back. Decide the mechanism in Phase 6 with real hardware in front of you.

### 11. NVIDIA hardware
**Raised: Phase 1. Out of scope until there is hardware that needs it.**

Bazzite publishes `bazzite-nvidia` alongside `bazzite`, so supporting an NVIDIA
machine is a base-image change rather than a rewrite. The cost is real though: a
second image to build, sign, test and boot on every change, and NVIDIA driver
breakage is the single largest maintenance tax on custom Bazzite images.

Not doing it now. If the hardware changes to something NVIDIA-based, the work is
to parameterise the base image in the `Containerfile` and matrix the build
workflow over both variants — not to restructure anything.
