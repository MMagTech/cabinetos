# What Cabinet already solves

> **Read this before designing anything.** Cabinet ships on iOS, tvOS and macOS
> and has already answered most of the questions CabinetOS runs into. This is a
> map of where those answers live, written 2026-09-14 from a full read of
> `RommApp` — about 52,000 lines of Swift across 160 files.
>
> It exists because the alternative was being discovered in pieces: a question
> comes up, CabinetOS reasons it out from the RomM API, gets it half right, and
> has to be corrected. Reading the shipped implementation first is faster and
> the answers are better, because they have been through real hardware.
>
> **The source is not checked out on the development Mac.** Clone it, and skip
> the committed `.a` archives while doing so:
>
> ```
> git clone --filter=blob:none --sparse https://github.com/MMagTech/cabinet.git
> cd cabinet && git sparse-checkout set RommApp docs
> ```
>
> Cabinet's own header comments are unusually good and usually explain *why*.
> Read the header before the code.

---

## The shape of it

| Target | Lines | What it is |
|---|---|---|
| `RommApp/RommApp` | ~39,700 | The shared core: auth, library, players, cores, settings |
| `RommApp/RommAppTV` | ~4,800 | tvOS surfaces — **the closest sibling to CabinetOS** |
| `RommApp/RommAppMac` | ~4,400 | Catalyst: Mac chrome, plus the Dolphin and PCSX2 hosts |
| `RommAppTopShelf`, `RommAppWidget`, `LayoutEditor` | ~3,200 | Extensions and a tool |

**tvOS is the reference surface for CabinetOS**, not iOS and not Mac. Same
constraints: a ten-foot display, a controller, no touch, no keyboard worth
typing on.

---

## Talking to RomM

| File | What it settles |
|---|---|
| `Auth/RommClient.swift` (1217) | Every call the app makes. An actor, because the token is mutable shared state and the pairing poll runs alongside the UI. **URLSession and Codable only — no networking library, no generated client.** |
| `Auth/RommModels.swift` (145) | Response shapes, **hand written on purpose**: generating from `openapi.json` is what makes the other third-party iOS client break across RomM versions. Decode only the fields used; ignore the rest. |
| `Auth/Session.swift` (832) | Which screen to show and the state behind it. Nothing about any particular server is baked in. |
| `Auth/Keychain.swift` (63) | The one secret: the access token, **keyed by server host** so pairing with a second instance does not clobber the first. The address is not a secret and lives in UserDefaults. |

**Lesson CabinetOS should copy exactly:** hand-written response structs that
ignore unknown fields. A generated client turns every RomM release into a
breakage.

## Saves, states and memory cards

This is the part most worth reading before touching it.

| File | What it settles |
|---|---|
| `Native/MemoryCardSync.swift` (483) | **The battery-save engine both players drive**: which copy wins at launch, when a snapshot travels, and how each region is captured — card, Game Boy clock, Sega CD cart, Dreamcast VMU, the file-writing cores' flushes. *"The views own the* when*; this type owns the* what*."* |
| `Native/MemoryCardStore.swift` (267) | Local persistence of `RETRO_MEMORY_SAVE_RAM`, mirrored to `/api/saves`. **The disk copy is written first on every snapshot, before any upload is attempted** — losing signal must never mean losing a save. The Game Boy RTC is a separate `.rtc` beside the `.srm`, the same split RetroArch uses. |

**It was extracted into one shared type for a reason worth heeding.** tvOS once
carried its own copy, it silently went stale, and tvOS spent a day syncing only
PS1 and N64 while iOS synced every platform. One engine, two callers.

**Endpoints and the tag:**

- Saves: `POST /api/saves?rom_id=&emulator=&overwrite=true`, multipart part
  `saveFile`. **`overwrite=true` is load-bearing** — it keeps a PS1 game at one
  memory card instead of one row per session.
- States: `POST /api/states?rom_id=&emulator=`, part `stateFile`, optional
  `screenshotFile`. States deliberately do **not** overwrite; a history is the
  point.
- The `emulator` tag is per **core**: `gambatte-native`, `gpgx-native`,
  `pcsx-rearmed-native`. It is what stops a launch screen offering a state the
  running core cannot restore. See PROJECT.md for the decision about whether
  CabinetOS shares these tags.

**WHICH COPY "LOAD LATEST" TAKES — read before changing CabinetOS's.**
`TVPlayerView.loadLatestState`, and its own comment says it: *"offline falls
back to the newest local state, online the server stays the source of truth."*
The guard is `kept && isOffline` — local is read ONLY when the game is kept and
the device is offline. Everything else asks RomM, filters to this core's tag,
sorts by `updatedAt` and takes the first.

**And Cabinet's Save AWAITS its upload**, reporting `"Uploading…"` then either
`"Saved to RomM."` or `"Waiting for signal to upload."` That is what makes the
server safe to treat as the source of truth: by the time a person can press
Load, the server has it.

**CabinetOS queues its upload on a worker instead**, which is right for a
machine that must not stall its frame loop — but it means save-then-load raced
the upload and silently found nothing. Closed on 2026-09-21 in Save's reporting
rather than by changing what "latest" means, plus a local fallback when the
server has nothing for this core or cannot be reached.

**Favourites and play state are RomM's, not the app's.** Favouriting in the app
syncs up; recents come back down as `order_by=last_played`. CabinetOS should
never keep a local notion of either.

## Launching a game

`Native/NativeLauncher.swift` (806).

- **Fetch every firmware file the platform lists.** A core looks BIOS up by name
  in the system directory and ignores what it does not need — Beetle Saturn
  wants one of two region BIOSes, FBNeo boards like CV1000 need none.
  *"Extra files are harmless and missing ones are the only failure that
  matters."*
- A kept game skips all of it: its directory already holds ROM and firmware, so
  it boots with zero network.
- Saturn and PS1 are **chd-only, deliberately**, matching RomM's own
  recommendation for CD platforms. cue/bin is real work and was not worth
  confusing a performance question with.

## Cores

| File | What it settles |
|---|---|
| `Native/NativeCore.swift` (544) | The cores the app ships, one case per core, and the `emulatorTag`. **Kept apart from the webview's core catalogue on purpose: that list is RomM's, this one is ours.** |
| `Native/NativeCoreOptions.swift` (1587) | A **hand-picked subset** of each core's options, not a dump of everything the core reports. |
| `Native/MAME2003PlusOptions.swift` (128) | See below. The most expensive lesson in the file. |
| `Native/NativeCoreChoice.swift` (81) | Which core runs a game where more than one can — arcade alone: a choice for this game, then the platform habit, then the default. |
| `Native/ExperimentalCores.swift` (53) | The **JIT boundary**: platforms whose cores want a recompiler the process may not carry are offered only behind a deliberate switch. Architectural rather than measured, so it does not go stale. |

> ### An unanswered core option is not the default
>
> **This cost Cabinet eight separate evenings, one option at a time.**
>
> A libretro core reads its settings by asking the frontend for each variable in
> turn. When the frontend does not answer, the core does **not** fall back to the
> default printed in its own option table — the whole case is skipped and the C
> global keeps whatever it was initialised to, which is zero. Zero means silence
> for a sample rate, black for brightness, and off for every toggle whose useful
> state is on.
>
> **So an unanswered option is not "the default", it is the worst value in the
> list, and it fails quietly.** CabinetOS's core host must answer every variable
> a core asks about.

## Storage

`Native/KeptGames.swift` (1274), `Settings/StorageView.swift` (276).

- A **kept** game is deliberate and nothing evicts it. Its manifest embeds **the
  whole `Rom` captured at keep time**, not a subset, so it can be browsed and
  launched with no network at all.
- The Storage screen deliberately shows two different things and says so: kept
  games (permanent, deliberate) and the player's cache (automatic, evictable).
  It was renamed from "Cache" the moment it held things that are not a cache.
- `Native/DownloadAll.swift` (530) sizes a platform's full list and **checks the
  disk** before queueing.
- `Native/BackgroundDownloads.swift` (258): the session identifier **never
  changes**, or an update orphans every transfer in flight.

## The player

| File | What it settles |
|---|---|
| `Native/NativePlayerView.swift` (1194) | iOS's player. The caller has already activated the core and loaded the game before this appears. |
| `RommAppTV/TVPlayerView.swift` (854) | **tvOS's, and the one to read.** Same feature set, same underlying calls; only the surface differs — a remote-navigable glass menu, no touch overlay. |
| `Native/NativePlayerAudio.swift` (240) | A ring buffer between the core's variable frame output and CoreAudio's fixed callback. **The first version allocated per callback, called `removeFirst` on a second of audio, and took a lock — all three forbidden on a realtime thread.** |
| `Native/NativePlayerRenderer.swift` (1147) | Frame upload and presentation. |

## Where the platforms deliberately diverge, and why

Worth reading before assuming a tvOS screen is just the iOS one resized.

- **`TVLibraryView`** is a tile grid, not iOS's `List`: a full-width row on a
  1920pt canvas leaves a name at the far left and a count at the far right with
  a third of the screen empty.
- **`TVRomGridView`** was written fresh rather than adapted, because the adapted
  version put `.navigationTitle` *over* the artwork, ran under the tab bar, and
  truncated nearly every caption.
- **`TVSettingsView`** is five category rows pushing flat pages, the system
  Settings shape: on a TV every row of scroll is focus travel on a remote, while
  a short top level with pushed pages is a click.
- **`TVCoverFocus`** replaces tvOS's own focus plate with a translucent rim,
  because a white plate behind a photograph reads as a hard slab.
- **`MacSidebarShell`** replaces the TabView entirely, because Catalyst puts the
  tab bar in the titlebar and every attempt to reclaim it was a trick that
  showed.
- **`OfflineLibraryView`** exists because Home and Library each had their own
  copy of the same idea — *"why would the two need to exist"* — and they drifted.

## What CabinetOS cannot inherit

- **The focus engine.** tvOS supplies one; Linux does not. See PROJECT.md,
  *Focus and selection*.
- **The webview player.** RomM's EmulatorJS path does not apply.
- **Anything Apple-shaped**: Catalyst chrome, Top Shelf, widgets, Keychain,
  UserDefaults, Metal.
- **`MotionSensor`** is iOS-only by decision, not omission: Apple removed the
  sensors from the 2021 Siri Remote.
