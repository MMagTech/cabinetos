# Picking this up

Written at the end of each session for whoever starts the next one, which is
usually a fresh assistant with no memory of what just happened.

**`docs/PROJECT.md` is the specification and it is authoritative. `docs/CABINET.md`
maps what Cabinet already solves.** This file is only the short version: what
state things are in, what to do next, and the handful of things that will waste
a day if nobody says them out loud.

Rewrite it at the end of a session. It is meant to be current, not a log.

---

## Before anything else

**Everything is on `main`.** No other branches and no open pull requests.
[#22](https://github.com/MMagTech/cabinetos/pull/22) — the on-disk folder
layout, keeping per person, the second drive and "Remove download" actually
removing the download — merged as `8f23fc7`, so everything described below as
"runs today" is on `main`.

**THE FILES ON DISK MOVED, 2026-09-18.** There is no `romcache/` any more and no
`system/`. Games are in `roms/` and `cache/` under a platform folder, firmware
is in `bios/`, and every save, state and keep is under `users/<id> - <name>/`.
The test VM's own files were moved across and checked file by file; **there is
no migration tool in the tree and there should not be**, because nobody has run
CabinetOS outside of building it, so the one machine that needed moving has been
moved. Anything built from here starts on this layout.

Start from `main`, branch once, and **open the pull request against `main`**.
Four branches were once stacked on each other here, each opened before the last
had merged, and the result was three overlapping pull requests and a compile
check that did not apply to any of them. One branch at a time.

**Read `docs/CABINET.md` before designing anything.** Cabinet ships on iOS, tvOS
and macOS and has already answered most of what comes up here. tvOS is the
surface to copy, not iOS. Cabinet is not checked out on this Mac; clone it:

```
git clone --filter=blob:none --sparse https://github.com/MMagTech/cabinet.git
cd cabinet && git sparse-checkout set RommApp docs tools
```

`tools` is in that list now because the per-core build scripts are the only
honest record of how a core is built — see *Cabinet-side debts*.

Then read `docs/PROJECT.md`, and `frontend/README.md` for the build loop.

**The test VM is `cabinet@192.168.1.250`**, key at `~/.ssh/cabinetos`, sudo
password `cabinet` — a throwaway from the public repo's `disk_config/disk.toml`.
The RomM server it talks to is `192.168.1.10:6005`.

**Nothing builds on this Mac.** The frontend and the cores build in a container
on the VM and in CI. The VM loop is eleven seconds:

```
rsync -az -e "ssh -i ~/.ssh/cabinetos" frontend/src/ cabinet@192.168.1.250:~/frontend/src/
ssh -i ~/.ssh/cabinetos cabinet@192.168.1.250 \
  'cd ~/frontend && podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make'
```

## Where things stand

**The whole loop works, the whole library is reachable, and every emulator this
console ships can be run.** Browse every system and collection, open a game,
play it or download it, save and load states, and leave — with the save syncing
on the way out.

- **1147 of 1644 games playable, with all twenty-one cores built** — and every
  one of the twenty-one can be RUN, not just built. PPSSPP was the last, landed
  2026-09-17.
- **Home is real**: a hero from RomM's own play history, Recent, Favorites.
- **Library, a grid, and a launch screen**, built 2026-09-16. Every system
  including the ones this console cannot play, each saying why.
- **Download is the one deliberate storage act**, and it keeps the game. The
  cache stays invisible; Play fetches silently and says nothing. **"Remove
  download" removes it and gives the space back**, as of 2026-09-19.
- **The files are somewhere a person can find them**, as of 2026-09-18:
  `roms/`, `cache/`, `bios/`, and a folder per person holding their saves and
  states. Keeping a game is a decision per person rather than a flag on it, so
  two people share one copy and one releasing does not take it from the other.
- **Plug a second drive in and it is used** — internal or USB, no setup screen,
  one folder claimed on it and nothing else touched. Unplug it and the console
  copes; plug it back in and the duplicate copy goes.
- **Both floors are enforced where that button is**, measured by filling the
  disk rather than by reasoning about it.
- **Saves, memory cards and states sync both ways** with RomM, tagged with
  Cabinet's own emulator strings. Six cores share a tag; PPSSPP is the newest
  and has the strongest case of the six, because no configuration difference is
  left to justify.
- **BIOS comes down with the game**, every file the platform lists — except
  PSP's, which is not a console's firmware and ships with the emulator instead.
- **Dreamcast, Naomi, N64 and PSP play.** The cores that draw for themselves get
  a framebuffer inside the frontend's own GLES context, so Mario Kart 64,
  Ikaruga and Lumines run with no pixel read back anywhere.
- **PSP saves reach RomM and come back**, proven end to end: quit, a 40 KB zip
  lands on the server, delete it locally, relaunch, byte-identical. The first
  DIRECTORY save, and the pattern the other seven file-writing platforms follow.
- **Twenty-one cores build in CI**, each asserting its pinned revision, and the
  frontend compiles there too. Three of them are now known to be
  **byte-identical across machines**, the newest being PPSSPP — a 38 MB CMake
  build, which is the shape that could plausibly have picked up a timestamp.

## Pick up with these, in this order

> **Decided 2026-09-17: no more UI is designed or tuned until CabinetOS is
> installed on the reference machine.** The user's call. **It was still in
> shipping on 2026-09-19** — delayed, expected within a day or two — so this
> line still holds. When it lands, read item 1b first: installing the OS on it
> does NOT bring the frontend with it. This list is ordered by it, and
> PROJECT.md records why — the short version is that overscan, motion and
> vertical fit cannot be judged on a software-rendered VM, so building more
> screens here is building against a lie.
>
> **The line is the acceptance test, not the subsystem.** If the test is "does
> this look right", it waits. If the test is a measurement or a behaviour, it
> goes ahead — and a screen that already exists is not frozen, because fixing
> something *wrong* is not the same as tuning something.

### 0. The PS3 PKG experiment — DONE 2026-09-18, and the answer was good news

**Installing a PS3 game does not cost a second copy of it.** The worry was that a
19.8 GB download would become 40 GB on disk and that PS3 would need its own
storage model because of it. It does not: **the installed game is the same size
as the PKG**, so once the PKG is deleted the game costs what any other game
costs. The doubling is real but it only lasts while both exist.

Measured on the VM, which has no GPU — installing is decrypt-and-unpack, so it
needed none. RPCS3 came from Flathub; nothing was built.

| | Super Stardust HD | Sly Cooper |
|---|---|---|
| PKG | 287,265,040 B | 19,843,204,240 B |
| Installed | 287,260,549 B | 19,843,198,103 B |
| Install time | 18 s | 123 s — 161 MB/s |
| Peak, both present | 574 MB | 39.7 GB |

All four questions are answered, in PROJECT.md under open question 19, *The PKG
install, MEASURED 2026-09-18*:

- **What it produces and where**: `dev_hdd0/game/<TITLEID>/`, and nothing
  anywhere else.
- **The ratio**: 1.00x, both times.
- **The PKG can be deleted** — proven by booting both games afterwards, not by
  looking at the directory. Neither reached gameplay and neither could: no GPU.
- **The `.rap` goes in `dev_hdd0/home/<user id>/exdata/<CONTENT ID>.rap`**, with
  a lowercase extension, and RPCS3 names the exact file when it is missing. It
  is **16 bytes**. Per-user, exactly where open question 18 put it.

**Firmware was measured too**: the 206 MB PUP decrypts in 17 s into a 195 MB
`dev_flash` tree. One install per machine.

### 0b. A decrypted ISO is better where it exists, which is six titles of thirty

**A decrypted ISO is the better shape, and it needs nothing from this console.**
MMagTech has a script that converts his disc dumps to ISOs; RPCS3 opens such an
image directly — mounts it as the disc itself, no install, no `.rap`, no second
copy — **provided it carries a 20-byte PS3 disc header** that `xorriso`,
`mkisofs` and `hdiutil` do not write. Without it RPCS3 rejects the file as
`non-PS3ISO`.

Proved both ways on a real 12.5 GB image: rejected unstamped, booted stamped.
PROJECT.md has the byte layout under open question 19, *The better answer: a
decrypted ISO*.

**The fix lives on the server, deliberately.** Stamp the file once and it is
correct for everything that reads it. Two tools were handed to MMagTech on
2026-09-18 and are NOT in this repository: `stamp-ps3-iso.command` for images
already built, and his own `Build PS3 ISO.command` with the header step added —
its verification was also wrong, checking only for an ISO9660 signature that
every ISO has.

**Two games are converted and verified** — Bioshock and Bioshock 2, both stamped
with the last-sector field matching the real file size. The rest are still disc
folders or PKGs.

**But it only reaches six of the thirty titles.** 24 are PSN PKGs with no disc
behind them, so no conversion is possible and **the install route below is the
majority case, not a fallback.** Where an ISO does exist there is nothing to
build — one file whose size RomM knows, so the reuse test works, no install
phase, no transient 2x.

**DECIDED 2026-09-18: this console reads a PKG or a stamped ISO, and NOT a disc
folder.** MMagTech's call, and it removes work: a disc folder is hundreds or
thousands of files — Mass Effect 2 is **8,337** — and downloading a tree that
size from RomM is a transfer path that does not exist here and would need its
own progress, resume and partial-tree handling. Converting first makes it one
download that everything already handles.

**And nothing is built for the folders at all**, not even a way to say they are
not ready — the four that remain are mid-conversion, so the state is temporary.
PS3 support here is a PKG or a stamped ISO; the folder never reaches this
console.

**What is left for PS3, and it is not storage.** Four things the PKG route
turned up that the design still has to answer — RPCS3 refusing to install without `--headless`,
an exit status that reports failure after logging success, an interrupted
install leaving a partial tree nothing cleans up, and a recompiler cache written
outside the virtual drive onto the OS volume. All four are in PROJECT.md. **PS3
still cannot be PLAYED here**; that needs Vulkan and waits on the A9 Pro, which
is open question 20.

### 1. Saves that actually reach the server — **DO THIS NEXT**

The biggest real hole in the product, and **the audit of 2026-09-17 measured
it: 47 of the 81 saves on the server — 58% — are for platforms this console can
neither upload nor restore.** It has been recorded here as "the file-writing
save class", which reads like an edge case. It is the majority.

**Do Dreamcast first.** Thirteen saves, the largest count of any platform, and
it explains the symptom below rather than sitting beside it. Flycast never
exposes the VMU through `RETRO_MEMORY_SAVE_RAM`; it reads and writes
`vmu_save_A1.bin` in the **system** directory under `dc/` — which is now
`bios/dc/`, beside the BIOS. Cabinet restores it there before boot and captures
it after unload. Write the bytes before boot, read them after, upload if
changed, and there are thirteen real cards on the server to test the restore
against.

**And it closes the one gap the new layout left open.** `bios/` is supposed to
hold replaceable firmware, and `vmu_save_A1.bin` is the one file in there that
cannot be fetched again. Once Dreamcast saves travel, the card belongs in
`users/<id> - <name>/saves/Sega Dreamcast/<romId>/flycast/` like every other
save, and what stays in `bios/dc/` is `dc_nvmem.bin` — the console's own clock
and language settings, which are a machine fact rather than a person's. See
open question 18, *Four things building it turned up*.

PROJECT.md, *The save audit*, has the per-platform table of where every core
writes its file and the two guards to copy (a uniform fill means the game never
saved; Sega CD's cart is a separate region from its internal RAM).

- **The file-writing class, in full**, with where each core actually puts the
  file. All but the first are relative to the SAVE directory, which is now
  `users/<id> - <name>/saves/<platform>/<romId>/<core>/` and holds one game's
  files rather than every game's: Dreamcast `bios/dc/vmu_save_A1.bin` — the
  system directory, not the save one; MAME `nvram/<stem>.nv`; FBNeo
  `fbneo/<stem>.fs`; 3DO `opera/shared/nvram.0.srm`; Sega CD `*.brm` plus
  `*cart.brm` as its own region; Neo Geo Pocket `*.flash`; DS `*.sav`; PSP the
  `PSP/SAVEDATA/**` tree. Two of those were **already sitting on this console's
  disk** from real runs — `scd_U.brm` and `mame2003-plus/nvram/*.nv`. **Nothing
  on this machine says which game wrote either**, because the old layout gave
  every core one shared save directory and recorded no more than the file name;
  they were kept rather than guessed at, in
  `users/1 - MMagTech/saves/unattributed/`. The capture half can still be
  written and checked against them without playing anything new.
  Cabinet solved every one of them in `MemoryCardSync.swift`; read it before
  designing anything.
- **PSP IS DONE, and it is the worked example for the other seven.**
  `frontend/src/dirsave.h` and `syncDirSave` in main.cpp: restore before the
  core loads, capture after the unload, compare against a baseline taken at
  launch, upload only when something moved. The other seven are simpler than
  PSP was, because each is one file rather than a tree.
- **`[save] battery is 0 bytes` is correct, not a fault**, for every core in
  that class. It is the host saying the core exposes no save RAM.
- **PSP is a third shape, and Cabinet ALREADY SYNCS IT — do not repeat my
  mistake here.** PPSSPP saves into memory-stick DIRECTORIES —
  `PSP/SAVEDATA/<id>/` holding `PARAM.SFO`, `DATA.BIN` and icons. I wrote that
  this does not sync, on the strength of a comment in
  `NativeCore.savesOverSaveRAM` that says *"Save sync for PSP is its own future
  feature"*. **That comment is stale in Cabinet's own source.** MMagTech
  corrected it in one sentence, and there is a real save on the server:
  `Lumines - Puzzle Fusion (USA) (Cabinet).srm`, 51,426 bytes,
  `emulator=ppsspp-native`, updated 2026-08-28.

  Cabinet archives the subtree with `FileWrapper` and pushes it through the
  **same store, endpoint and saveRAM region** as a cartridge battery, on the
  same after-shutdown trigger. So the design is done and the tag already
  matches ours.

  **The container was the obstacle and it is now decided: ZIP.** PPSSPP's save
  format is the FOLDER — there is no single-file PSP save, PPSSPP defines no
  export format, and RomM stores one opaque file per rom and emulator. Cabinet's
  August blob is Apple's `rtfd` archive labelled `.srm`, readable nowhere
  without Foundation. **MMagTech has fixed the Cabinet side to zip (2026-09-17,
  reported, not yet pushed to GitHub and not seen from here)**, so this console
  needs to read and write zip and does NOT need an `rtfd` writer. The frontend
  already links libarchive, which does both.

  **Verify it from the first save the fixed build uploads** — four bytes settle
  it, `PK\x03\x04` is zip — and read three things off that same file: what the
  zip is ROOTED at (`ULUS10002LUMINES/…` vs `SAVEDATA/…` vs `PSP/SAVEDATA/…`,
  which decides where we unzip and is invisible until you look), whether the tag
  is still `ppsspp-native`, and whether Cabinet still READS `rtfd` — because the
  only PSP save MMagTech owns is still in the old format.

  The zip round trip is measured, not assumed: zipped the real save folder on
  this console, deleted the original, unzipped it back, all four files
  byte-identical, and Lumines ran against the restored folder and quit cleanly.
  PROJECT.md, *What PPSSPP is supposed to use*, has the detail.
- **Saves on the right triggers.** Keys do it today, which is the test
  environment and not the product. The settled triggers are in PROJECT.md.

All of it is measured by whether a file lands on the server, so the VM answers
these completely.

### 1b. The image does not carry the frontend or the cores — **and that surprises people**

**Noticed 2026-09-19, when MMagTech asked a reasonable question: if we install
CabinetOS on the mini PC now, does the work we do afterwards just arrive as
updates?** Half of it does. The OS half is genuinely self-updating — merge to
main, the image rebuilds, `bootc` pulls it, and packages, system files and the
session service all travel.

**The frontend and the twenty-one cores do not travel, because they are not in
the image.** `system_files/usr/bin/cabinetos-session` still runs a placeholder:

```
APP="${CABINETOS_APP:-/usr/bin/sleep infinity}"
```

So a freshly installed machine boots to a black gamescope session, and none of
the frontend work of the last fortnight reaches it. Everything that has been
built here lives at `~/frontend` on the test VM and is compiled by hand.

**And there is a tripwire waiting for whoever does it.** `build.yml` now ignores
`frontend/**`, because the image does not contain the frontend and a session of
frontend work was starting a thirteen-minute image build per push for nothing.
**Take that line back out the day the frontend goes in**, or the check will
quietly stop covering the thing it exists for. The comment in the workflow says
so too.

**What it needs:** the frontend binary and `cores/build/*.so` installed into the
image — which is also where PPSSPP's 13 MB of system files go, at
`/usr/share/cabinetos/system/`, the one part of open question 18 that is decided
and not built. The frontend is compiled in CI already (`build-frontend.yml`) and
the cores are built and cached (`build-core.yml`), so the pieces exist; nothing
collects them into the image.

**Until it is done**, a mini PC is another machine to push source at and build
on, exactly like the VM. That is still worth having the day it arrives — it is
the only way to judge the UI on a television — but it is not "install it once
and it keeps up".

### 2. Finish the core options, which is half done

The host answers every option a core declares, and **the override table is now
wired into the launch path as well as the audit** — it was not, until PPSSPP
needed the first real entry. Two things are left:

- **Bring across Cabinet's per-platform choices.** `catalog::optionOverrides`
  has exactly one entry, PPSSPP's CPU engine. Cabinet hand-picks a subset per
  platform in `NativeCoreOptions.swift`; port it one platform at a time with a
  reason recorded beside each choice.
- **The options MAME asks for and never declares.** Two are constant across
  every game tried and the rest vary by driver. Their values have to come from
  the core's source, not from a guess.

### 3. The N64 save states that do not restore exactly

Reproducible to the digit, the instrument was checked, and three candidate
causes are written down with none established. It blocks nothing today, and it
matters because portable save states are the premise the whole product rests on.
The cheapest discriminating experiment is in PROJECT.md.

### 4. PSP's save state, and a crash that is understood but not closed

**The save DATA is done** — see item 1. Two things are left, and they are the
same shape: PPSSPP is the only core that emulates on a thread of its own.

**A threaded core only advances when the frontend COMPLETES a frame**, not when
`retro_run` is called. That one fact explains both of the following, and it was
found by a wait loop that made no progress at all.

**The crash.** Quitting a PSP game while it is still booting used to kill the
console, inside the core's own boot thread. Quitting now defers until the
machine is up — measured on the case that crashed twice, which waits 4.1s and
exits cleanly. **It is not closed:** in the headless capture configuration the
core sometimes never boots at all (one run: 2,384 frames, zero audio), and
tearing it down then aborts at process exit in a static `std::thread`
destructor inside the core. Not seen in the ordinary configuration. Same root
cause — the core never finished starting, so its own shutdown cannot clean up.

**The state.** PPSSPP produces a 41,943,040-byte state at a demo screen, so it
CAN serialize. Whether the restore is exact is unknown, because `--state-test`
warms up in a tight `retro_run` loop with no frame in it and this core makes no
progress there. Fixing the instrument is the work, and the diagnosis above is
the fix: give the warm-up a real frame loop.

The core produces a 41,943,040-byte state at a demo screen — verified. Whether
the restore is exact is **not** answered, and it cannot be by the existing tool:
`--state-test` warms up in a tight loop of `retro_run` with no wall clock in it,
and PPSSPP is the only core in the set that emulates on a thread of its own, so
three thousand calls produce no sound, a static picture and a zero-byte state
while the same core reaches its attract demo on the ordinary launch path.
**A core with its own emulation thread is not frame-deterministic under that
test.** Fixing the instrument is the work; a capture reporting
`retro_serialize_size` is the stopgap that exists today.

### 5. The on-disk folder layout — DONE 2026-09-18

**Built, and the test VM was moved onto it.** Open question 18 in PROJECT.md has
the detail and the numbers; the short version is that `romcache/` and `system/`
are gone and this is what a console holds now:

```
<root>/
├── roms/<platform>/<romId> - <name>     kept games
├── cache/<platform>/<romId> - <name>    pulled games — the ONLY thing eviction touches
├── bios/                                firmware from RomM, and the core system directory
├── users/<id> - <name>/{saves,states,keeps,pending,screenshots,config}/
├── config/
└── logs/
```

`frontend/src/storage.{h,cpp}` owns it and `cache.{h,cpp}` was rewritten around
it. The root is `/var/lib/cabinetos` when that can be created and written and
the working directory otherwise, which on the VM is `~/frontend`;
`--storage-root` overrides it and the answer is printed at startup.

**One spelling of a platform, everywhere: RomM's `fs_slug`.** The first build
had `roms/psx/` for games, as the agreed shape wrote it, and
`saves/Sony Playstation/` for saves, because mirroring the server was the reason
for the per-user tree — one console filed two ways, which MMagTech rejected on
sight. `fs_slug` is also the only one that is unique: two Arcade platforms share
the slug `arcade`, 223 games between them, needing different cores.

**The one thing the shape does not answer:** `bios/` still mixes replaceable and
irreplaceable, because libretro gives a core exactly ONE system directory and
Flycast writes the Dreamcast's flash into it. The PSP fonts moved out — they
ship in the image — but there is no name in the shape for "what a core wrote
into its system directory". Item 1 below takes the VMU out of there;
`dc_nvmem.bin`, the console's own clock and language, stays and rebuilds itself
if lost.

**The second drive was built the next day, 2026-09-19** — plug one in and it is
used, no setup screen, and the console never holds two copies of a game. Open
question 14 has the rules and the measurements; the scenario it was tested
against is MMagTech's own: keep a game with the drive in, unplug it, play it,
plug it back in.

**The one thing still missing is the screen** that says a drive is not
connected. The console says it on stderr, once. Everything else about a missing
drive already behaves correctly without it.

**"Remove download" now removes the download, as of 2026-09-19.** It used to
demote the game to the cache and free nothing, which MMagTech called out — the
row says Remove and it removed nothing, and reclaiming space is why anybody
presses it. Two callers still demote and neither is somebody asking for space:
the game being played right now, whose files the core has open, and a keep whose
download failed. The delete calls `syncfs`, because btrfs otherwise reports the
old free-space figure until a transaction commits and the screen would show no
change at all.

### 6. Nothing warns that a system's BIOS is missing

Until a game fails to start. `catalog` is where it belongs — a fifth answer, and
the first one that is a fact about the person's server rather than about this
console. The answer is a lookup, not a layout, so the tile that shows it can
reuse the wording already measured for the other four.

### 7. The disk that eviction cannot see

Mesa's shader cache in `~/.cache`, plus files the cores write into `bios/`.
Under 3 MB today. **One of them is a Dreamcast's saved flash**, so "clean the
system directory" is not the answer — and it also holds 13 MB of PSP system
files that are part of a build's output rather than anything reclaimable.
PROJECT.md, *The cache is not the only thing a game writes to disk*.

**The folder layout narrowed this rather than solving it.** Saves used to land
in there too and now go under a person, and the PSP files belong in
`/usr/share/cabinetos/system/` inside the image once something installs them
there. What is left is genuinely the machine's own emulator state.

### 8. Power button to a clean shutdown

Phase 2's last mechanical item, and it is a behaviour rather than a picture.

---

## Waiting on the reference machine, and deliberately not started

Ordered for whenever it is installed. **Do not begin these in the VM.** The
machine is now the **GEEKOM A9 Pro**, not the SER5 — older text below and in
PROJECT.md says "the SER5" and means this one.

- **The navigation bar.** The Library is reached with a temporary **L** key.
  Home has about 85 points of vertical slack and the bar needs about 85 — the
  arithmetic is in PROJECT.md. Either the bar fits, or the hero comes down, or
  the bar goes elsewhere, and only a television can say which.
- **The Storage screen.** Its data already exists and can be finished without
  it — run `./build/cabinetos-frontend --storage` — but the screen itself is a
  layout.
- **The rest of the launch screen**: a different save state, a different core,
  an export. **The save-state half is a mechanism and can be built now.**
- **Download All, at the platform level.** Cabinet's `DownloadAll.swift` sizes
  the whole list and refuses rather than filling the disk. The sizing and the
  refusal are measurable; the screen is not.
- **PSP's internal resolution.** The core is answered with its own declared
  default, 480x272, which is the PSP's own screen and what Cabinet ships on a
  television. Cabinet's Mac uses 1920x1088. Raising it is a look-and-performance
  decision on Vega integrated graphics and it needs the panel.
- **The audio governor's 20 ms cushion.** Inherited from Cabinet rather than
  measured here; the lead it permits *is* input lag. Tune it with a pad in hand.
- **The boot splash**, and the rest of the branding.
- **Everything about motion, the letterbox glow and the safe area.**

## Things that will bite you

### About looking at what you built

- **Judge nothing visual on the VM.** Software rendering on llvmpipe. And it is
  not only motion: a television's overscan eats more vertical room than a
  framebuffer capture shows. **Vertical fit cannot be judged here either.**
- **Read the pixels before believing the picture.** PPSSPP's first capture was
  not blank — it was a plausible, nearly-black rendering with faintly legible
  text, and it read as a core that renders black. The maximum pixel in the whole
  1920x1080 frame was RGB **(4,4,4)**. Pulling that number out of the BMP took a
  minute and turned a guess into a fact; the cause was the frontend obeying the
  frame's alpha channel.
- **Every screen photographs itself, headless.** `SDL_VIDEODRIVER=offscreen`
  needs no compositor, no session and no controller:
  ```
  SDL_VIDEODRIVER=offscreen ./build/cabinetos-frontend --romm 192.168.1.10:6005 \
    --screen library --screenshot /tmp/x.bmp --render-size 1920x1080 --frames 60
  ```
  `--screen` opens by walking the route a person walks, so a capture cannot show
  a state the product cannot reach. `--storage`, `--download` and `--unkeep` do
  the same for the things with no picture.
- **`--frames N` only ends the run when there is a `--screenshot` to take.**
  Without one the loop never exits and the command sits there at full CPU. This
  cost about five minutes; `--download 200 --screenshot /tmp/x.bmp --frames 300`
  is the shape that terminates.
- **`--storage-root <path>` puts a whole console somewhere else**, which is how
  the two-disk and the out-of-space tests above were run without disturbing the
  real tree. Everything — games, saves, keeps, config — goes under it.
- **To watch a real game, launch it**: `--launch <romId> --launch-after 1
  --frames N`. A PSP game needs about 2500 drawn frames to reach its attract
  demo on this VM, which is roughly two minutes; Dreamcast about 1400. Add
  `--overlay-exit` to make it quit back to Home by itself, which is the only way
  to exercise the unload path without a controller.
- **Stop the session before building on the VM.** The frontend runs at 300% CPU
  under llvmpipe and it is four cores. Or skip it and use the offscreen driver.

### About the product

- **A truncated explanation is worse than none.** A tile's second line holds
  about sixteen characters beside a cover. Measure the column before writing the
  string.
- **Two tiles that read the same are one tile.**
- **`catalog::coverageFor` answers FOUR different questions.** No core exists, a
  core exists and Cabinet does not ship it, this console has not built it, and
  it is built and cannot be driven. Collapsing any two hides work. Only the
  first three are reachable today; the fourth is empty because every core runs.
- **An unanswered libretro core option is NOT the default.** The core skips the
  case and the C global keeps its zero value. **This is no longer an argument,
  it is a demonstration**: run `--core-options-off` and launch a PSP game, and
  it ends at *"the core needs a render target this context cannot build"*,
  because the resolution option falls back to "Auto", which sizes the render to
  a display a libretro frontend never reports. Without the option work, PSP
  would not start at all. `--core-options` prints the table; 601 options across
  twenty-one cores, one of them ours.
- **A core that declares no options is the suspicious case, not the clean one.**
  FBNeo and MAME declare theirs per driver, so the table does not exist until a
  game is loaded.
- **`av_info` is a narrow probe.** Geometry, frame rate, sample rate. "No
  difference in av_info" does not mean no difference.
- **Ask the CORE, never the platform**, whether an archive should be opened.
- **Never dispatch on a file extension.** Thirty-two files in the reference
  library have none. Sniff the magic bytes.

### About the machine and the work

- **Build a core, then RUN it.** This is now four for four. Every assertion in
  the build pipeline passed on melonDS while it wrote its save to `/`; on
  PPSSPP it passed while the memory card went somewhere unwritable, while the
  picture drew at 1.5% brightness, and while the override table was being
  printed rather than applied. **The build pipeline cannot see any of it.**
- **Measure rather than reason, where you can.** Two minutes of measurement has
  beaten a plausible argument every time it has been tried here.
- **Run the control.** `--core-options-off` and `cores/backend-diff.sh` exist for
  it, and the control has twice been more informative than the result.
- **Every scripted edit must assert its anchor.** A patch that matches nothing
  leaves a green build with the fix absent.
- **A field added to the middle of a positional struct re-assigns the rest of
  the row.** `catalog.cpp`'s table is positional.
- **Wall-clock pacing makes a headless capture emulate almost nothing**, which is
  why a capture steps one emulated frame per drawn frame.
- **Do not judge a hardware core by its first screenshot.** The test that settles
  orientation is text that reads correctly.
- **Two podman containers with `:Z` over overlapping paths will break each
  other.** `:Z` relabels the whole mounted tree for one container's SELinux
  category, so running `podman run -v ~/cabinetos:/repo:Z` while a core is
  building under `~/cabinetos/.core-src` relabels the build out from under it —
  the running compiler then fails with **"Permission denied"** writing its own
  dependency files, in one directory, for no visible reason. Cost one PPSSPP
  build. **Do not start a second container over a parent of a running one.**
- **`pgrep -f "some string"` matches your own command line**, and so does
  `pkill -f`. This bit twice more in one session: `pkill -f "hrydgard/ppsspp"`
  and `pkill -f "git-remote-https"` each killed the ssh session issuing them,
  because the remote command contained the pattern. **Match on something the
  checker cannot contain** — `pgrep -x`, a pid file, or the exit status of the
  thing you started. `pgrep -x` also refuses names over 15 characters, so
  `cabinetos-frontend` needs `ps -eo args | grep "[c]abinetos-frontend"`.
- **The weekly base bump needs two clicks, not none.** It opens a pull request,
  but the build on it lands as `action_required` and waits for approval —
  `gh api -X POST /repos/MMagTech/cabinetos/actions/runs/<id>/approve`. And
  **read the relevant list**: when something breaks on real hardware, look at
  what `ci/base-watch.txt` does not watch.
- **A core build failing is not always the core.** `Curl error (28)` is the
  network. Re-run before reading anything into a single red core.
- **GitHub serves some repositories at 55 KB/s over git and 9.8 MB/s over
  HTTPS.** A full clone of PPSSPP is 324,844 objects and took three hours at
  that rate on a machine that pulls a tarball in seconds. `build-core.sh` now
  clones `--filter=blob:none`, which finished the same clone in 45 seconds —
  but **NOT for submodules**, where the lazy blob fetch is thirty times slower
  than cloning them whole. The comment in the script says so; do not "tidy" it.
- **RPCS3 will not install a PKG or firmware unless you say `--headless`.**
  `--no-gui --installpkg` prints *"Cannot perform installation in no-gui mode!"*,
  relaunches itself with a window, and opens a file chooser — on a machine with
  no pointer, that is a hang. Plain `--installpkg` with no mode flag does the
  same. Only `rpcs3 --headless --installpkg <path>` builds no window and
  installs straight through.
- **RPCS3 exits 134 after a successful install.** SIGABRT in a static destructor
  at teardown, on every one of the three installs run here, *after* logging
  `Successfully installed`. **Read the log line, not the exit code.** Same shape
  as PPSSPP's teardown abort.
- **An interrupted PS3 install leaves the partial tree behind**, and nothing
  cleans it up. One was killed three seconds in and left 1.5 GB that looks
  installed. Delete the title directory before retrying.
- **`timeout` does not kill RPCS3 under flatpak.** It signals the `flatpak run`
  wrapper; `rpcs3` inside the bwrap sandbox keeps going. One "150 second" boot
  ran for four minutes. Follow it with `pkill -x rpcs3` — and mind that a
  `pkill` aimed at a stuck process will also kill an install you started in the
  same breath, which happened here and cost a 19.8 GB run.
- **Flathub stalls from this network, silently.** Two `flatpak install` runs sat
  with established connections and zero bytes read for ten minutes each. A retry
  loop fixes it, because ostree resumes from what it already fetched:
  `for i in $(seq 1 30); do timeout 240 flatpak install -y --user ... && break; done`.
  A large download running at the same time makes it much worse.
- **The image build still only runs on a pull request aimed at `main`.** If you
  target something else, it needs `gh workflow run build.yml --ref <branch>`.
- **Retargeting a pull request does not re-run CI.** Close and reopen it.
- **`core-manifest.json` IS on GitHub**, at `docs/core-manifest.json` in
  Cabinet, and has been since `37ca75d`. This file and PROJECT.md both said
  otherwise for four days. The `~/Downloads/core-manifest.json` copy is
  byte-identical to it.

## The state that lives on the VM and not in git

- `~/frontend/` — the frontend source, built with
  `podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make`
- `~/frontend/cores/build/` — **twenty-one** built cores, where the frontend looks
- `~/frontend/` is also the **storage root**, because `/var/lib/cabinetos` does
  not exist on this machine and cannot be created by the `cabinet` user, so the
  root falls back to the working directory. It holds:
  - `bios/` — BIOS fetched from RomM, files the cores write into their system
    directory, and **`PPSSPP/`**, 13 MB of PSP system files that came out of the
    core build rather than off the server. Copy it from
    `~/cabinetos/cores/system/` after building that core. On a real console that
    one lands in `/usr/share/cabinetos/` and is symlinked in at startup.
  - `cache/<platform>/` — downloaded games, and the only thing eviction touches
  - `roms/<platform>/` — kept games. Also still holds one loose
    `Dr. Mario (World) (Rev 1).gb` somebody put there by hand for `--core`,
    deliberately left where a command that expects it can find it.
  - `users/1 - MMagTech/` — every save, state, keep and unsent upload, plus
    `saves/unattributed/`, which is the two old shared save piles kept whole
    because nothing in them says which game wrote them. They are the material to
    test the file-writing capture against; see item 1.
  - `config/user.json` — the RomM user id and name, cached so a console with no
    network still knows whose saves it is holding.
  - `config/drives.json` — which games drives were here last time, and the ONLY
    thing about storage that is remembered rather than read off the disk. It
    exists so the console can say "your games drive is not connected" once and
    then stop; nothing decides where a file is from it.
- **There is no `romcache/` and no `system/` any more**, on this machine or in
  the code.
- `/var/mnt/games/CabinetOS/` — **the VM's games drive**, claimed automatically
  the first time the frontend ran after 2026-09-19, holding its own `roms/` and
  `cache/`. Kept games go there now. The console claims that one folder and
  nothing else on the disk: `flatpak/`, `ps3lab/` and `layout-backup/` sit
  beside it untouched, which is the whole of rule 1.
- `~/cabinetos/` — a clone of this repo, where `cores/build-core.sh` runs
- `~/cabinetos/.core-src/` — per-core checkouts, **4.8 GB, of which PPSSPP is
  3.4 GB**. They are a cache: delete any to make room and the next build
  re-clones. Flycast's was deleted on 2026-09-17 to make room for PPSSPP.
- `~/run-frontend.sh` — the session launcher. The original is `run-frontend.sh.bak`
- `~/.config/cabinetos/romm.json` — the RomM token, 0600
- `/var/mnt/games/flatpak/` — **a flatpak user installation holding RPCS3**, 2.7 GB,
  reached with `FLATPAK_USER_DIR=/var/mnt/games/flatpak`. It is on the games disk
  deliberately: the KDE runtime it needs is 1.1 GB and `/var` has 5 GB.
- `/var/mnt/games/layout-backup/` — a tar and a `sha256sum` list of every save
  the VM held before the folder layout moved them, 2026-09-18. The move was
  verified file by file and the saves are live in the tree, so this is belt and
  braces rather than the only copy. 11 MB; delete it whenever.
- `/var/mnt/games/ps3lab/` — the PS3 experiment. `rpcs3/dev_flash` (195 MB
  firmware), `rpcs3/dev_hdd0/game/` with Super Stardust HD and Sly Cooper
  installed, both `.rap`s in `rpcs3/dev_hdd0/home/00000001/exdata/`, and the PUP
  in `dl/`. Both PKGs were deleted after installing, on purpose — that was the
  experiment. RPCS3's paths are set by a hand-written
  `~/.var/app/net.rpcs3.RPCS3/config/rpcs3/vfs.yml`, and its renderer is set to
  **Null** in `config.yml` because the VM has no Vulkan.

### The VM has TWO disks now, and the second one is the point

| | |
|---|---|
| `/dev/vda4` → `/var` | 21.6 GB btrfs, **about 5.3 GB free**. The OS and everything above. |
| `/dev/vdb` → `/var/mnt/games` | **100 GB btrfs**, empty, label `cabinetos-games`. Added 2026-09-17. |

In `/etc/fstab` by UUID with `nofail`, and **proved across a reboot** rather
than assumed. `nofail` matters: a machine that will not boot because a games
drive is missing is exactly what open question 14 forbids.

**It exists to test the two-drive design, not just to hold a big PKG.** Four
things become measurable that were decisions on paper. **Two are now done**:

1. ~~**That demoting a kept game is a RENAME, not a copy.**~~ **Done
   2026-09-18.** Kept Mario Kart 64 onto the games disk, released it, and the
   entry came out of `cache/` with the **same device and inode** it went into
   `roms/` with — `58:82064` both times. So it was renamed, and the cost is the
   same whatever the game weighs. For comparison, copying that same 12.6 MB
   across the two filesystems took **0.124 s**, which is 101 MB/s — a 19.8 GB
   PS3 title would be about three minutes of copying because somebody changed
   their mind about keeping it. That is the whole reason `roms/` and `cache/`
   repeat per drive rather than once at the root.
2. **That a missing drive degrades rather than errors.** `umount /dev/vdb` with
   the console running. Written down as a requirement; **still never exercised,
   and it cannot be yet**: `storage::locations()` returns one location, so there
   is no second one to remove. It becomes testable the day open question 14's
   second location is wired in, and everything below it already takes a location.
3. **Both disk floors against realistic numbers** — 5.3 GB on one volume and
   98 GB on the other, rather than ballast on a single disk.
4. ~~**A PS3 PKG install at full size.**~~ **Done.** Sly Cooper peaked at
   39.7 GB with the PKG and the install both present — which could never have
   fitted on the old disk — and settled at 19.8 GB once the PKG was deleted.

**Adding it in Unraid is not obvious** and cost some time: the VM editor will
not resize an existing vDisk at all, and the option to add a second one is
hidden behind the **BASIC / ADVANCED toggle** at the top right of the Edit VM
page. The terminal alternative is `qemu-img resize` on the file under
`/mnt/user/domains/<vm>/`.

**`disk_config/disk.toml` still says `minsize = "20 GiB"`**, so a VM rebuilt
from a fresh qcow2 comes out small again with no second disk. If this work
continues, that number should change — it is a one-line edit nobody has made.

`podman image prune -f` is still the first thing to try when `/var` gets tight,
then `~/cabinetos/.core-src`.

## The heavy-systems discussion: what it settled, 2026-09-17

The user's ask, 2026-09-17:

> "We should work on the remaining emulator and discuss the addition of others,
> because none of my Cabinet builds currently have Wii U, PS3, Xbox, Xbox 360 or
> Switch and I'd like to consider those."

The emulator is done, and **the discussion happened.** What it settled:

| | |
|---|---|
| **Hardware** | Settled. The reference machine is now a **GEEKOM A9 Pro** — Ryzen AI 9 HX 370, Radeon 890M — replacing the SER5. MMagTech has seen it running God of War 3. **Do not reopen this.** |
| **Systems Cabinet does not have** | **Allowed.** PS3 may be OS-only. Open question 19. |
| **Save data** | A folder tree, and the PSP mechanism built the same day already covers it. |
| **Save states** | RPCS3 has none, **and they are not needed** — snapshots earn their keep on cartridge machines, not on a console with real in-game saves. |
| **Renderer** | RPCS3 wants Vulkan. So do parallel-RDP and Flycast. **One piece of host work serves three systems** — open question 20. |
| **Installation** | ~~The one real problem.~~ **Measured 2026-09-18 and it is not a problem.** A PKG installs to its own size, the PKG can then be deleted, and the game still boots. Item 0 above. |
| **Storage** | Unchanged by the faster box: 307 GB, one 37 GB title. |

**The PKG install experiment is done — item 0 above. What is left is a Vulkan
path in the host, which cannot be built until the A9 Pro exists**, plus four
mechanical findings from the install that PROJECT.md records.

The original material follows, because the numbers are still the numbers.

Counted off the live server 2026-09-17, because "should we support X" is a
different question when X is 109 games and when it is none — and the last column
is the one that was missing before:

| System | Games | Library size | Largest single title |
|---|---|---|---|
| **Switch** | **109** | 310 GB | **28.3 GB** |
| **PS3** | **30** | 307 GB | **37.0 GB** |
| PS Vita | 27 | 22 GB | 3.2 GB |
| Wii | 2 | 7 GB | 4.7 GB |
| **Wii U** | **0** | — | not in the library at all |
| **Xbox** | **0** | — | not in the library at all |
| **Xbox 360** | **0** | — | not in the library at all |
| PS2 | 71 | 111 GB | 6.6 GB |
| GameCube | 14 | 11 GB | 1.3 GB |
| PSP | 4 | 3 GB | 1.8 GB — **plays, as of today** |

**Three of the five the user named serve zero games today.** Not an argument
against them, but it should be said before any effort is estimated.

**Two things that were put to the user rather than decided:**

1. **These are not libretro cores.** Every one of the twenty-one is a `.so` this
   frontend loads and drives in its own process and its own frame loop. Wii U,
   PS3, Xbox 360 and Switch emulation lives in standalone applications with
   their own windows, input and renderers — the same shape as PS2 and GameCube,
   and therefore **open question 12**, not a new question. Cabinet answered it
   for those two by embedding real PCSX2 and Dolphin as libraries rather than
   launching them. Answer 12 first, or answer them together.
2. **Storage stops being theoretical.** Switch and PS3 alone are 617 GB. More to
   the point, **a single 37 GB title is larger than the free space the console
   keeps in reserve** — the cache, both floors and Download All were all designed
   against cartridge and disc-sized games, and none has ever seen one game that
   big. That is cheap to check against the model and has not been checked.

**The recommendation given, for whoever picks this up if the user has not
replied:** PS3 and Switch are the only two of the five that would serve a game
today, they are the two heaviest systems in emulation, and they land on the same
unanswered question as PS2 and GameCube — which are already in the plan and
already have a proven answer in Cabinet. So: PS2 and GameCube first, then judge
PS3 and Switch with that experience in hand.

## Something the user wants discussed, in its own session

**Account switching.** RomM has users; tvOS already switches between them.
Raised 2026-09-16 with the words "we would implement it slightly different", and
explicitly deferred to a session of its own — so do not start building it as a
side effect of something else. Read Cabinet's tvOS account handling and
`Auth/Keychain.swift` first (the token is already keyed by server host), then
ask what the difference is before writing anything. It touches things already
built: Home is assembled from RomM's play history, and favourites and recents
are RomM's rather than local, so whose account they come from stops being
implicit the moment there is more than one.

## Licensing, which is now written down

**`docs/LICENCES.md`** lists every core, its licence, its upstream and the
commit this project pins. Modelled on Cabinet's own, which had already solved
it.

**The one line that shapes decisions:** six of the twenty-one cores — FBNeo,
MAME 2003-Plus, Snes9x, Genesis Plus GX, PicoDrive and Opera — are free for
**non-commercial use only**. CabinetOS is free, is not sold, and takes no
donations, and that is what keeps them legitimate. **Selling a machine with this
image on it would break it**, which matters because the hardware has already
changed once and may change again.

**The terms ship INSIDE the image**, at `/usr/share/licenses/cabinetos/`,
installed and asserted by `build_files/build.sh` — because whoever pulls the
image is exactly the person who never sees this repository.

Still owed: the licence text readable on the console (Settings → About), and a
verification pass over each line against the source it came from — they were
carried across from Cabinet's list rather than checked here, and this project's
own rule is that a fact carried across is a fact nobody has checked.

## Cabinet-side debts

1. **Flycast carries unscripted edits in its working tree**, so its pinned
   commit does not reproduce what ships, and **that is the only reason Flycast
   cannot share its emulator tag.** Capture the diff before anything touches
   that tree:
   `git -C spikes/cores/flycast/src diff > tools/patches/flycast-unscripted.patch`
2. **The manifest does not describe how a core is built.** PPSSPP's entry says
   `patches: null` and `build_args: null`; `tools/build-ppsspp.sh` applies two
   source patches and passes CMake flags, two of which change what the binary
   is. The same shape as Flycast's thin patch inventory. **The builder scripts
   are the real record**, and the manifest is load-bearing for parity — so this
   is worth a pass across every core, not just this one.
3. **A comment in `NativeCore.savesOverSaveRAM` says PSP save sync is "its own
   future feature".** It was built afterwards — `MemoryCardSync.swift:324` plus
   the archive/unpack/restore trio in `NativeLauncher` — and the comment never
   moved. It cost a wrong claim in a pull request here on 2026-09-17. One line
   to fix, and worth a look for others like it: **a stale comment reads exactly
   like a current one.**
4. **mGBA's Mac build is `-dirty` too**, and its manifest entry lists no patches
   at all. Same problem, quieter.
5. **Two "unrecoverable" tvOS revisions were recovered with `strings`.** Nine
   more are probably sitting in the shipping archives. An hour of work turns
   "unknown and unknowable" into facts.
6. **melonDS's archives carry no revision** while the same upstream built here
   reports one, so something in Cabinet's build is losing `GIT_VERSION`.

## How the user wants this done

Plain answers. **Lead with what a change does and why it exists, in terms of
what breaks for the product — not in terms of the subsystem.** This was said
twice in one session and drifted back both times; a PR came back as "completely
foreign to me what it did and what it exists for", and a later explanation "went
way over my head". Keep the dense detail, but put it after the plain statement.

Check the running machine before theorising — most wrong turns come from
reasoning off an error message instead of looking. Say plainly what is verified
and what is assumed; they notice and ask. And they push back usefully: "are we
sure we can't do X?" and "you need to read Cabinet" both produced better answers
than the first one.

**Use the `MMagTech` handle, never the user's personal name** — not in files,
commits or documents, and the repo is public. This file previously said
`docs/PROJECT.md` still carried 26 uses of the personal name; as of 2026-09-17
every one of its 29 occurrences is the handle, and a pattern search for a name
in the places one would sit — *"X's prompt"*, *"X said"*, *"X chose"* — finds
none. **Treat that as checked rather than as done**, since a search cannot
prove the absence of a word nobody wrote down, and keep the rule regardless.
