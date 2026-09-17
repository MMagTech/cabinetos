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

**Everything is on `main`. There are no other branches and no open pull
requests.** As of 2026-09-17 the whole of this file's "what runs today" is
merged, along with a run of CI and base-image repairs; the base is pinned at
Bazzite `44.20260916`. `git log` has the detail and this file will not repeat
it.

Start from `main`, branch once, and **open the pull request against `main`**.
Four branches were once stacked on each other here, each opened before the last
had merged, and the result was three overlapping pull requests and a compile
check that did not apply to any of them. One branch at a time.

**Read `docs/CABINET.md` before designing anything.** Cabinet ships on iOS, tvOS
and macOS and has already answered most of what comes up here. tvOS is the
surface to copy, not iOS. Cabinet is not checked out on this Mac; clone it:

```
git clone --filter=blob:none --sparse https://github.com/MMagTech/cabinet.git
cd cabinet && git sparse-checkout set RommApp docs
```

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

**The whole loop works, and the whole library is now reachable.** Browse every
system and collection, open a game, play it or download it, save and load
states, and leave — with the save syncing on the way out.

- **1143 of 1644 games playable**, with twenty cores built — and every one of
  those twenty can now be RUN, not just built.
- **Home is real**: a hero from RomM's own play history, Recent, Favorites.
- **Library, a grid, and a launch screen**, built 2026-09-16. Every system
  including the ones this console cannot play, each saying why.
- **Download is the one deliberate storage act**, and it keeps the game. The
  cache stays invisible; Play fetches silently and says nothing.
- **Both floors are enforced where that button is**, measured by filling the
  disk rather than by reasoning about it.
- **Saves, memory cards and states sync both ways** with RomM, tagged with
  Cabinet's own emulator strings.
- **BIOS comes down with the game**, every file the platform lists.
- **Twenty cores build in CI**, each asserting its pinned revision, and the
  frontend compiles there too.
- **Dreamcast, Naomi and N64 play**, as of 2026-09-16. The cores that draw for
  themselves get a framebuffer inside the frontend's own GLES context, so Mario
  Kart 64 and Ikaruga run with no pixel read back anywhere.
- **The CI around all of it was repaired**, 2026-09-17, and the shape of every
  fault was the same: it produced plausible output while being wrong. Checks
  that did not run on the branch being written; twenty runners rebuilding one
  container and giving twenty chances for a mirror to fail; a weekly base check
  that had never once completed; and, once it did, a report calling a
  month-long firmware move routine while 81% of its diff was keyring noise.
  **Nothing was failing loudly.** Worth carrying as a habit rather than as
  trivia — when something here looks fine, check that it is not merely
  plausible.

## Pick up with these, in this order

### 0. Finish the core options, which is half done

The host now answers every option a core declares. Two things are left and both
are small:

- **Bring across Cabinet's per-platform choices.** `catalog::optionOverrides`
  is empty on purpose — every option gets the core's own default, which is the
  right baseline. Cabinet hand-picks a subset per platform in
  `NativeCoreOptions.swift`; port it one platform at a time with a reason
  recorded beside each choice.
- **The options MAME asks for and never declares.** Two are constant across
  every game tried and the rest vary by driver. Their values have to come from
  the core's source, not from a guess, and they are the first real customers for
  the override table.

### 1. The navigation bar, which needs a television and not a decision

The Library is reached with a temporary **L** key. That is the only thing
holding the screens apart from being a product.

**Home has about 85 points of vertical slack and the bar needs about 85** — the
arithmetic is in PROJECT.md. A bar at Title 3 plus its gap puts Recent's caption
on the bottom edge, and **overscan eats more than a capture shows**. So this is
a measurement on the SER5, not an argument here: either the bar fits, or the
hero comes down, or the bar goes elsewhere.

Everything else on this list can be done without it.

### 2. The rest of the launch screen

PROJECT.md's own list, and none of it is built: **a different save state, a
different core, and an export.** The save-state part is the one with a real
mechanism behind it already — `fetchStates` works, and the emulator tag is what
decides whether a state is offered or greyed, so the screen can be honest about
which states this build can actually load.

### 3. The Storage screen

The one place the cache is allowed to be visible, because it is somewhere a
person goes deliberately. Its data already exists: run
`./build/cabinetos-frontend --storage` and you get free space, both floors, the
upload queue, what is kept and what would be evicted, oldest first.

### 4. Download All, at the platform level

PROJECT.md says CabinetOS should offer it where tvOS deliberately does not, and
Cabinet's `DownloadAll.swift` already sizes the whole list and refuses rather
than filling the disk and letting eviction sort it out — which would evict what
it had just fetched.

### 5. PPSSPP, the twenty-first core

The only core of twenty-one not built, and the reason to build it now is that
the thing that blocked it is gone: the host serves hardware-rendered cores.
PSP is four games in the reference library, so build it for the completeness
rather than the count — and **run it**, because that is the whole lesson of the
other two. Its firmware is the special case: PPSSPP's system files ship inside
the app bundle on Apple rather than coming from RomM, which in a bootc image
becomes a path in `/usr`.

If it asks for desktop GL or Vulkan rather than GLES, the host refuses it by
name and says so in one line — and `Support::NeedsHardwareRender` is still
sitting there waiting for exactly that case.

### 6. Still owed from before, and none of it blocks the screens

- **Saves on the right triggers.** Keys do it today, which is the test
  environment and not the product. The settled triggers are in PROJECT.md.
- **The file-writing save class is not synced at all, and Dreamcast now shows
  it to your face.** melonDS writes a `.sav` rather than exposing save RAM, so
  `[save] battery is 0 bytes` is correct and the file never reaches RomM. Neo
  Geo Pocket, Sega CD and FBNeo are the same class. **Ikaruga opens on "memory
  card not connected"** — Flycast's VMU is the same problem with a title screen
  attached, and it is the first one a person would actually notice.
- **Nothing warns that a system's BIOS is missing** until a game fails to start.
  `catalog` is where it belongs — a fifth answer, and the first one that is a
  fact about the person's server rather than about this console.
- **N64 save states do not restore the machine exactly.** Reproducible to the
  digit on Mario Kart 64, and the instrument was checked — the same test on mGBA
  with a moving picture passes. It blocks nothing, because mupen64plus has no
  shared emulator tag and its states never travel. PROJECT.md, *Open against the
  frontend right now*, lists the three candidate causes and says plainly that
  none is established.
- **A game writes to disk in places eviction cannot see.** Mesa's shader cache
  in `~/.cache`, and two files the cores put in the system directory. Under
  3 MB today and it arrived with the hardware-rendered cores. PROJECT.md,
  *The cache is not the only thing a game writes to disk* — and note that one of
  those files is a Dreamcast's saved flash, so "clean the system directory" is
  not the answer.

## Things that will bite you

### About looking at what you built

- **Judge nothing visual on the VM.** Software rendering on llvmpipe. And it is
  not only motion: a television's overscan eats more vertical room than a
  framebuffer capture shows, which is how Cabinet's hero height needed four
  attempts on real hardware. **Vertical fit cannot be judged here either.**
- **Every screen photographs itself, headless.** `SDL_VIDEODRIVER=offscreen`
  needs no compositor, no session and no controller:
  ```
  SDL_VIDEODRIVER=offscreen ./build/cabinetos-frontend --romm 192.168.1.10:6005 \
    --screen library --screenshot /tmp/x.bmp --render-size 1920x1080 --frames 60
  ```
  `--screen` opens by walking the route a person walks, so a capture cannot show
  a state the product cannot reach. `--storage`, `--download` and `--unkeep` do
  the same for the things with no picture.
- **`--render-size` was quietly broken** for every screen with a pill or a panel
  on it: `presentScene` composited to the window while the capture read the
  offscreen target. Fixed 2026-09-16. The lesson is that **the instrument can be
  the thing that is wrong**, and it failed in a way that looked like a layout
  bug.
- **Stop the session before building on the VM.** The frontend runs at 300% CPU
  under llvmpipe and it is four cores. `sudo systemctl stop
  cabinetos-session.service`, build, start it again — with `--no-block` on the
  start, or ssh hangs. Or skip it entirely and use the offscreen driver.

### About the product

- **A truncated explanation is worse than none.** A tile's second line holds
  about sixteen characters beside a cover. "No core in the ..." tells a person
  strictly less than nothing — they can already see the tile is dimmed. Measure
  the column before writing the string.
- **Two tiles that read the same are one tile.** "Nintendo 64" and "Nintendo DS"
  both truncated to "Nintendo ...", and hyphens matter on their own —
  "TurboGrafx-16" and "TurboGrafx-CD" have no space before the part that
  distinguishes them.
- **`catalog::coverageFor` answers FOUR different questions.** No core exists, a
  core exists and Cabinet does not ship it, this console has not built it, and
  it is built and cannot be driven. Collapsing any two hides work.
- **An unanswered libretro core option is NOT the default.** The core skips the
  case and the C global keeps its zero value — silence for a sample rate, black
  for brightness, off for every toggle whose useful state is on. It cost Cabinet
  eight evenings. **Fixed 2026-09-16: 526 options across twenty cores, every one
  of them previously unanswered.** Run `--core-options` to see the table and
  `--core-options-off` for the control.
- **A core that declares no options is the suspicious case, not the clean one.**
  FBNeo and MAME declare theirs per driver, so the table does not exist until a
  game is loaded. MAME also asks for options it never declared, and which ones
  varies by game — those still go unanswered and must not be guessed at.
- **`av_info` is a narrow probe.** It reports geometry, frame rate and sample
  rate. Two of three cores showed no difference there between answered options
  and none, while MAME's sample rate moved 44100 to 48000. "No difference in
  av_info" does not mean no difference.
- **Ask the CORE, never the platform**, whether an archive should be opened.
  FBNeo reads `zip` and `7z` itself; `.chd` and `.rvz` must never be unpacked.
- **Never dispatch on a file extension.** Thirty-two files in the reference
  library have none. Sniff the magic bytes.

### About the machine and the work

- **Build a core, then RUN it.** Every assertion passed on melonDS while it
  wrote its save file to `/`, because it reads the save directory in
  `retro_init` and the frontend set it at game-load time. Silent, and it hits
  every core that writes its own saves.
- **Measure rather than reason, where you can.** The keep refusal was checked by
  filling the disk with a ballast file; the eviction protection by reading back
  what `--storage` says is a candidate. Both took two minutes and both would
  have been plausible-looking and wrong on paper.
- **Every scripted edit must assert its anchor.** A `python - <<PY` that
  replaces text it cannot find changes nothing, the build stays green, and the
  feature silently is not there.
- **A field added to the middle of a positional struct re-assigns the rest of
  the row.** `catalog.cpp`'s table is positional and three rows end in `true`;
  the new `system` field went last for exactly that reason.
- **Wall-clock pacing makes a headless capture emulate almost nothing.**
  `--frames 180` asks for 180 DRAWN frames, and offscreen those take under a
  tenth of a second, so the core is paced against a tenth of a second and
  emulates five frames — a black boot screen for every console ever made. The
  first Mupen64Plus capture came back black and looked exactly like a core that
  had failed. A capture now steps one emulated frame per drawn frame. **A
  hardware-rendered console needs about 1100 frames to reach a title screen**,
  which is 16 seconds on the VM, not minutes.
- **Do not judge a hardware core by its first screenshot.** Mario Kart 64 at
  frame 600 shows the Nintendo logo MIRRORED, which looks exactly like a botched
  flip. It is the logo rotating. The test that actually settles orientation is
  text that reads correctly: at frame 1100 the title screen says PUSH START
  BUTTON the right way round.
- **The weekly base bump needs two clicks, not none.** It opens a pull request
  now (the repository setting was turned on 2026-09-17), but the build on it
  lands as `action_required` and waits for approval —
  `gh api -X POST /repos/MMagTech/cabinetos/actions/runs/<id>/approve`. And
  **read the relevant list**: the first real run classified a month-long
  `linux-firmware` move as routine because `base-watch.txt` had no firmware
  entry. PROJECT.md has the whole story; the short version is that **when
  something breaks on real hardware, look at what that file does not watch.**
  (That particular move turned out to be Bazzite deliberately pinning back
  firmware that was breaking handhelds — a fix, not a regression. Chase these
  to the upstream commit before treating one as a risk.)
- **A core build failing is not always the core.** The twenty-job matrix
  fetches 267 MB of Fedora packages, and on 2026-09-16 a mirror timed out at
  under a kilobyte a second and failed `Build prosystem` — which passed on
  re-run in fifty seconds, with nothing wrong with prosystem. The container is
  now built once per run and pulled by the twenty, so that chance is twentyfold
  smaller, but it is not zero. **Re-run before reading anything into a single
  red core**, and look at the log: a `Curl error (28)` is the network, not the
  code.
- **The image build still only runs on a pull request aimed at `main`.** The
  frontend compile and the core build were widened on 2026-09-16 to run on every
  pull request, because a stack of branches had slipped past them and the
  compile check silently did not apply to the work being written. The image
  build was left narrow on purpose — twelve minutes, no path filter — so if you
  ever do target something other than `main`, that one still needs
  `gh workflow run build.yml --ref <branch>`.
- **Retargeting a pull request does not re-run CI.** The workflows fire when a
  pull request is *opened*, not when its base changes. Close and reopen it.
- **`pgrep -f "some string"` matches your own command line.** Three times now,
  the worst being a wait loop whose pattern matched the shell running the check,
  so it sat for nine hours waiting for something already finished. **Match on
  something the checker cannot contain** — a pid file, `pgrep -x`, or the exit
  status of the thing you started.
- **Look on disk before concluding a file does not exist.** `core-manifest.json`
  is at `~/Downloads/core-manifest.json` and is not on GitHub.

## The state that lives on the VM and not in git

- `~/frontend/` — the frontend source, built with
  `podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make`
- `~/frontend/cores/build/` — twenty built cores, where the frontend looks
- `~/frontend/romcache/` — downloaded ROMs, plus `kept/` and `pending/`
- `~/cabinetos/` — a clone of this repo, where `cores/build-core.sh` runs
- `~/cabinetos/.core-src/` — per-core checkouts, ~3.5 GB, of which Flycast is
  2.2 GB. They are a cache: delete any to make room and the next build re-clones
- `~/run-frontend.sh` — the session launcher. The original is `run-frontend.sh.bak`
- `~/.config/cabinetos/romm.json` — the RomM token, 0600

**Disk on the VM: about 7.6 GB free.** 4.5 GB came back on 2026-09-16 from
`podman image prune -f`, which removes untagged builder layers and leaves
`cabinetos-builder:latest` alone. If it is tight again, that is the first thing
to try, then `.core-src`.

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
2. **mGBA's Mac build is `-dirty` too**, and its manifest entry lists no patches
   at all. Same problem, quieter.
3. **Two "unrecoverable" tvOS revisions were recovered with `strings`.** Nine
   more are probably sitting in the shipping archives. An hour of work turns
   "unknown and unknowable" into facts.
4. **melonDS's archives carry no revision** while the same upstream built here
   reports one, so something in Cabinet's build is losing `GIT_VERSION`.
5. **`core-manifest.json` is still not pushed to GitHub.** It is load-bearing
   for every core and it is one unbacked file on one Mac.

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
commits or documents. `docs/PROJECT.md` still carries 26 uses of it from earlier
sessions and the repo is public; that is worth a find-and-replace.
