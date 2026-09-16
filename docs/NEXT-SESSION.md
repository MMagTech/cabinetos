# Picking this up

Written at the end of each session for whoever starts the next one, which is
usually a fresh assistant with no memory of what just happened.

**`docs/PROJECT.md` is the specification and it is authoritative. `docs/CABINET.md`
maps what Cabinet already solves.** This file is only the short version: what
state things are in, what to do next, and the handful of things that will waste
a day if nobody says them out loud.

Rewrite it at the end of a session. It is meant to be current, not a log.

---

**Read `docs/CABINET.md` before designing anything.** Cabinet ships on iOS, tvOS
and macOS and has already answered most of what comes up here. tvOS is the
surface to copy, not iOS. Cabinet is not checked out on this Mac; clone it:

```
git clone --filter=blob:none --sparse https://github.com/MMagTech/cabinet.git
cd cabinet && git sparse-checkout set RommApp docs
```

Then read `docs/PROJECT.md`, and `frontend/README.md` for the build loop.
**Nothing builds on this Mac** — the frontend and the cores are built in a
container on the test VM, and in CI.

## Where things stand

**The whole loop works.** Browse the real library, pick a game, watch it
download with progress, play it, save and load states, and leave — with the save
syncing on the way out.

- **1100 of 1644 games playable**, with twenty cores built.
- **All four backend questions are answered**, and two of them by running the
  thing rather than reading it. See open question 13.
- **Home is real**: a hero from RomM's own play history, Recent, Favorites,
  focus moving between rows, scrolling.
- **Downloads stream to disk on a worker.** Nothing that talks to a server stops
  the picture — measured at 3.21 ms on the frame thread for a save.
- **Saves, memory cards and states sync both ways** with RomM, tagged with
  Cabinet's own emulator strings so they interchange with the Apple apps.
- **BIOS comes down with the game**, every file the platform lists.
- **An in-game overlay**: Start or Escape. Resume, save state, load state, exit.
- **Twenty cores build in CI**, each asserting its pinned revision.

## Pick up with one of these

**1. Nothing evicts anything.** A ROM already on disk at the right size is
reused, and that is all. 1644 games at these sizes do not fit on a console, so
the disk fills and stays full. **This is the biggest hole in the product now.**

**The policy is decided and it is ready to build** — PROJECT.md, Phase 4: when
to evict, in what order, what is never touched, and the four numbers, which are
decisions rather than proposals. Nothing in it is waiting on a discussion. Two
things are genuinely open and neither blocks the work, and they are named as
such at the end of the section.

**And read the first subsection of it before adding a number to anything.** The
first draft of that policy was tuned to this library — "the cartridge games come
to under 2 GB" — which is true here and inverts for anyone with a complete set.
The reference library is a reference the way the SER5 is: an illustration, never
the definition. Where a rule needs a number, make it a fraction of something the
machine can measure.

The first move is structural rather than clever: **saves and states currently
live in the same directory as the ROM**, so "evict a game" would delete the one
thing that always comes back along with the only things that never do. Split
them and most of the policy's protection rules stop being needed.

**2. The Library screen.** 1100 playable games and only the ~50 on Home can be
reached. Home already points at a Library that does not exist.

**3. Saves on the right triggers.** Keys do it today, which is the test
environment and not the product. The settled triggers are in PROJECT.md: when
the game writes its memory card, from the overlay, on leaving a game, and a
controller combination.

**4. The file-writing save class is not synced at all.** melonDS writes a `.sav`
beside the ROM rather than exposing `RETRO_MEMORY_SAVE_RAM`, so `[save] battery
is 0 bytes` is correct and the file never reaches RomM. Neo Geo Pocket, Sega CD
and FBNeo's NVRAM are the same class and all three are playable today. Cabinet's
`MemoryCardSync` is the shape to copy.

**5. Nothing warns that a system's BIOS is missing.** If the server holds no
Sega CD BIOS, the person finds out from the emulator's own error message after
choosing a game and waiting for a download. Asking the server what firmware it
HAS costs nothing and can happen while the library is scanned, which is the
moment to say so instead. Downloading stays lazy — first launch of a platform,
plus whenever a game is kept. PROJECT.md, Phase 4, under the firmware section.

**6. Hardware-rendered cores.** Flycast and Mupen64Plus are built and cannot
run: they want a GL context through `RETRO_ENVIRONMENT_SET_HW_RENDER`, which
`core.cpp` refuses. That is 43 more games, plus PPSSPP once it is built, and it
is the one piece of frontend work that is genuinely new rather than more
screens. On Linux the readback Cabinet needs should not exist at all — the UI
and the core can share one context.

## Things that will bite you

- **Judge nothing visual on the VM.** Software rendering on llvmpipe. And it is
  not only motion: a television's overscan eats more vertical room than a
  framebuffer capture shows, which is how Cabinet's hero height needed four
  attempts on real hardware. **Vertical fit cannot be judged here either.**
- **Build a core, then RUN it.** Every assertion in the build pipeline passed on
  melonDS — pinned commit, asserted revision, reproducible artifact — while it
  wrote its save file to `/`, because it reads the save directory in
  `retro_init` and the frontend set it at game-load time. Silent, and it hits
  every core that writes its own saves. Fixed; the lesson is the point.
- **Run the control before believing a comparison.** `cores/backend-diff.sh`
  first said 102 of picodrive's 103 objects differed, including zlib's. The same
  setting on both sides said the same thing: the cause was LTO's random
  per-build id, not the recompiler. The tool takes the same setting twice and
  calls identical a pass — use it.
- **Ask the CORE, never the platform**, whether an archive should be opened.
  `retro_get_system_info` reports the extensions a core takes and
  `block_extract`. FBNeo reads `zip` and `7z` itself, so an arcade set must be
  handed over unextracted; `.chd` and `.rvz` are compressed and must never be
  unpacked.
- **Never dispatch on a file extension.** Thirty-two files in the reference
  library have none. Sniff the magic bytes, the way `decodeImage` already did.
- **An unanswered libretro core option is NOT the default.** The core skips the
  case and the C global keeps its zero value — silence for a sample rate, black
  for brightness, off for every toggle whose useful state is on. It fails
  quietly and it cost Cabinet eight evenings. Our core host must answer every
  variable a core asks about, and **nobody has checked that it does.**
  (melonDS's `JIT_Enable` is the exception that proves the rule: a C++
  initialiser, so its unanswered default is `true`.)
- **`catalog::coverageFor` answers FOUR different questions now.** No core
  exists, a core exists and Cabinet does not ship it, this console has not built
  it, and — new — it is built and cannot be driven. Collapsing any two hides
  work.
- **Every scripted edit must assert its anchor.** A `python - <<PY` that
  replaces text it cannot find changes nothing, the build stays green, and the
  feature silently is not there. `build-core.sh`'s melonDS patch does this.
- **Read the evidence, not just the code.** Cabinet's mGBA feature set was read
  off its shipping archive with `nm -u`, which corrected a flag choice that
  reasoning had got wrong.
- **`pgrep -f "some string"` matches your own command line.** Twice mistaken for
  a still-running process.
- **Look on disk before concluding a file does not exist.** `core-manifest.json`
  is at `~/Downloads/core-manifest.json` and is not on GitHub.
- **Stop the session before building on the VM.** The frontend runs at 300% CPU
  under llvmpipe and it is four cores. `sudo systemctl stop
  cabinetos-session.service`, build, start it again — and use `--no-block` on
  the start, or ssh hangs.

## The state that lives on the VM and not in git

- `~/frontend/` — the frontend source, built with
  `podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make`
- `~/frontend/cores/build/` — twenty built cores, where the frontend looks
- `~/cabinetos/` — a clone of this repo, where `cores/build-core.sh` runs
- `~/cabinetos/.core-src/` — per-core checkouts, ~8 GB. **Disk is tight**
  (about 3 GB free). They are a cache: delete any of them to make room and the
  next build re-clones.
- `~/run-frontend.sh` — the session launcher; points at the live RomM server.
  The original is `run-frontend.sh.bak`
- `~/.config/cabinetos/romm.json` — the RomM token, 0600. Nine scopes: read the
  library, write only the person's own play data
- `romcache/`, `system/` under `~/frontend` — downloaded ROMs and BIOS

Sudo on the VM needs the password `cabinet`, a throwaway from the public repo's
`disk_config/disk.toml`.

## Cabinet-side debts

1. **Flycast carries unscripted edits in its working tree**, so its pinned
   commit does not reproduce what ships, and **that is the only reason Flycast
   cannot share its emulator tag.** Capture the diff before anything touches
   that tree:
   `git -C spikes/cores/flycast/src diff > tools/patches/flycast-unscripted.patch`
2. **mGBA's Mac build is `-dirty` too**, and its manifest entry lists no patches
   at all. Same problem, quieter.
3. **Two "unrecoverable" tvOS revisions were recovered with `strings`** —
   picodrive's and mGBA's, both sitting in the shipping archive. Nine more are
   probably there. An hour of work turns "unknown and unknowable" into facts.
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
