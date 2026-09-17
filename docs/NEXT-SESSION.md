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

**Everything is on `main` except one open pull request.** As of 2026-09-17,
[#19](https://github.com/MMagTech/cabinetos/pull/19) — *PSP plays: build the
last emulator, and fix what running it found* — is open against `main` with all
25 checks green. Everything described below as "runs today" includes it. If it
has merged by the time you read this, then everything is on `main` again.

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
  cache stays invisible; Play fetches silently and says nothing.
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
- **Twenty-one cores build in CI**, each asserting its pinned revision, and the
  frontend compiles there too. Three of them are now known to be
  **byte-identical across machines**, the newest being PPSSPP — a 38 MB CMake
  build, which is the shape that could plausibly have picked up a timestamp.

## Pick up with these, in this order

> **Decided 2026-09-17: no more UI is designed or tuned until CabinetOS is
> installed on the SER5.** The user's call. This list is ordered by it, and
> PROJECT.md records why — the short version is that overscan, motion and
> vertical fit cannot be judged on a software-rendered VM, so building more
> screens here is building against a lie.
>
> **The line is the acceptance test, not the subsystem.** If the test is "does
> this look right", it waits. If the test is a measurement or a behaviour, it
> goes ahead — and a screen that already exists is not frozen, because fixing
> something *wrong* is not the same as tuning something.

### 0. The conversation that was started and not finished

**Which of Wii U, PS3, Xbox, Xbox 360 and Switch to add.** The material was put
in front of the user on 2026-09-17 and they have not answered yet. Do not
re-derive it; it is written up at the bottom of this file, with the numbers
counted off the live server the same day. **Do not go researching emulator
projects before that answer comes back** — the user asked to *consider* these,
and what they are worth considering against matters more than a list of names.

### 1. Saves that actually reach the server

The biggest real hole in the product, and **the audit of 2026-09-17 measured
it: 47 of the 81 saves on the server — 58% — are for platforms this console can
neither upload nor restore.** It has been recorded here as "the file-writing
save class", which reads like an edge case. It is the majority.

**Do Dreamcast first.** Thirteen saves, the largest count of any platform, and
it explains the symptom below rather than sitting beside it. Flycast never
exposes the VMU through `RETRO_MEMORY_SAVE_RAM`; it reads and writes
`vmu_save_A1.bin` in the **system** directory under `dc/` — the same `dc/` the
BIOS lives in. Cabinet restores it there before boot and captures it after
unload. Write the bytes before boot, read them after, upload if changed, and
there are thirteen real cards on the server to test the restore against.

PROJECT.md, *The save audit*, has the per-platform table of where every core
writes its file and the two guards to copy (a uniform fill means the game never
saved; Sega CD's cart is a separate region from its internal RAM).

- **The file-writing class, in full**, with where each core actually puts the
  file: Dreamcast `system/dc/vmu_save_A1.bin`; MAME `nvram/<stem>.nv`; FBNeo
  `fbneo/<stem>.fs`; 3DO `opera/shared/nvram.0.srm`; Sega CD `*.brm` plus
  `*cart.brm` as its own region; Neo Geo Pocket `*.flash`; DS `*.sav`; PSP the
  `PSP/SAVEDATA/**` tree. Three of those are **already sitting on this
  console's disk** from real runs — `scd_U.brm`, `mame2003-plus/nvram/*.nv` and
  the PSP tree — so the capture half can be written and checked without playing
  anything new. Cabinet solved every one of them in `MemoryCardSync.swift`;
  read it before designing anything.
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

### 4. PSP's save state, which is half answered

The core produces a 41,943,040-byte state at a demo screen — verified. Whether
the restore is exact is **not** answered, and it cannot be by the existing tool:
`--state-test` warms up in a tight loop of `retro_run` with no wall clock in it,
and PPSSPP is the only core in the set that emulates on a thread of its own, so
three thousand calls produce no sound, a static picture and a zero-byte state
while the same core reaches its attract demo on the ordinary launch path.
**A core with its own emulation thread is not frame-deterministic under that
test.** Fixing the instrument is the work; a capture reporting
`retro_serialize_size` is the stopgap that exists today.

### 5. The on-disk folder layout — decided, not built

**Open question 18, agreed with MMagTech 2026-09-17.** The current layout was
never designed, it accumulated: there are TWO save directories, `system/` mixes
replaceable BIOS with an irreplaceable Dreamcast flash and 13 MB of PPSSPP
fonts, every core shares one flat save pile, and `romcache/` is named "cache"
while holding kept games and everyone's saves.

The agreed shape takes RetroArch's and RetroBat's vocabulary (`roms`, `saves`,
`states`, `bios`, `config`), adds the one thing neither needs — a `cache/` that
is the only directory eviction may touch — and namespaces per user the way RomM
itself does, mirroring its `users/<user>/saves/<platform>/<romId>/<core>/`.
One convention throughout: **the number identifies, the words are for you** —
`users/1 - MMagTech/`, `roms/psx/321 - Crash Bandicoot.chd`.

**One kept game is one file, however many people play it**, which changes the
one piece of existing code: `cache::keep/unkeep/isKept` is a boolean per rom and
has to become *kept by whom*, so that one person releasing does not take the
game away from another. Releasing the LAST keep demotes to cache rather than
deleting — un-keep is never destructive — and that is why `roms/` and `cache/`
repeat on every drive rather than once at the root: otherwise a demotion means
copying gigabytes between disks because somebody changed their mind.

**Do it before there are machines with play histories.** Today it is one user
and one directory; later it is moving every save on every console, and saves are
the only data here that cannot be re-downloaded. It is a behaviour, not a
picture, so the SER5 decision does not hold it up.

### 6. Nothing warns that a system's BIOS is missing

Until a game fails to start. `catalog` is where it belongs — a fifth answer, and
the first one that is a fact about the person's server rather than about this
console. The answer is a lookup, not a layout, so the tile that shows it can
reuse the wording already measured for the other four.

### 7. The disk that eviction cannot see

Mesa's shader cache in `~/.cache`, plus files the cores write into the system
directory. Under 3 MB today. **One of them is a Dreamcast's saved flash**, so
"clean the system directory" is not the answer — and as of PPSSPP the system
directory also holds 13 MB of PSP system files that are part of the build's
output rather than anything reclaimable. PROJECT.md, *The cache is not the only
thing a game writes to disk*.

### 8. Power button to a clean shutdown

Phase 2's last mechanical item, and it is a behaviour rather than a picture.

---

## Waiting on the SER5, and deliberately not started

Ordered for whenever it is installed. **Do not begin these in the VM.**

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
- `~/frontend/system/` — BIOS files fetched from RomM, files the cores write,
  and **`PPSSPP/`**, 13 MB of PSP system files that came out of the core build
  rather than off the server. Copy it from `~/cabinetos/cores/system/` after
  building that core.
- `~/frontend/romcache/` — downloaded ROMs, plus `saves/`, `kept/` and `pending/`
- `~/cabinetos/` — a clone of this repo, where `cores/build-core.sh` runs
- `~/cabinetos/.core-src/` — per-core checkouts, **4.8 GB, of which PPSSPP is
  3.4 GB**. They are a cache: delete any to make room and the next build
  re-clones. Flycast's was deleted on 2026-09-17 to make room for PPSSPP.
- `~/run-frontend.sh` — the session launcher. The original is `run-frontend.sh.bak`
- `~/.config/cabinetos/romm.json` — the RomM token, 0600

**Disk on the VM: about 5.3 GB free**, down from 7.6 GB because PPSSPP's source
tree is 3.4 GB. `podman image prune -f` is the first thing to try if it gets
tight, then `.core-src`.

## The discussion that is open: which heavy systems to add

The user's ask, 2026-09-17:

> "We should work on the remaining emulator and discuss the addition of others,
> because none of my Cabinet builds currently have Wii U, PS3, Xbox, Xbox 360 or
> Switch and I'd like to consider those."

The emulator is done. **The discussion was opened with the material below and
the user has not answered yet.**

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
