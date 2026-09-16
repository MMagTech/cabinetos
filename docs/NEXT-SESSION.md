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
- **Twenty cores build in CI**, each asserting its pinned revision, and the
  frontend compiles there too.
- **The disk no longer fills and stays full.** Eviction works, measured.

## Pick up with these, in this order

**The next block of work is SCREENS**, decided 2026-09-16. The machine underneath
is in good shape and almost nothing of the library is reachable: 1100 playable
games and only the fifty on Home can be got at.

### 1. Library — platforms and collections

Home already points at a Library that does not exist. The design is settled and
detailed in PROJECT.md: a **tile grid, not a list** (a full-width row on a
1920pt canvas leaves a name at one end and a count at the other), a **switcher
between Platforms and Collections** as capsule pills, focus landing on the
switcher the first time and **only** the first time, and the two arcade
platforms shown as two systems rather than merged.

`catalog::coverageFor` already answers which platforms are playable and why not,
in four flavours, so the screen has its content decided for it.

### 2. The game launch screen

A full-screen cover rather than a push, with the artwork as its own backdrop.
This is where a different save state, a different core and an export are chosen
— PROJECT.md's own list — and it is the screen Home's hero artwork opens.

### 3. Download lives in ONE of those two, and the launch screen is the better home

**Marcus's question, 2026-09-16: a button on the cover, or on the launch
screen.** Both work; they should not both exist.

**Recommendation: the launch screen.** It is already the place every other
per-game decision is made, so Download joins a list rather than starting a
second mechanism, and it keeps the grid clean — a cover with an action on it
needs the hero's two-actions-one-card treatment, which is a real focus problem
to solve for every tile in a grid of hundreds.

**The bulk case already has an answer**, which is what a cover button would
otherwise be for: *Download All* at the platform level, which PROJECT.md says
CabinetOS should offer where tvOS deliberately does not.

**It must work on a game that has never been played** — that is the case worth
building it for. See *Emulation* in PROJECT.md.

### 4. What eviction still has no protection for

Do this when the Download button lands, since that is where it is enforced:

- **Keep**, so there is something eviction may not take. Today the only
  protection is "the game that is running".
- **A pending-upload check.** The policy says nothing unsynced is ever deleted,
  and nothing tracks unsynced. Harmless only because eviction takes ROMs and
  never save data.
- **The system reserve**, so kept games cannot grow until the console can no
  longer update itself.

### 5. Still owed from before, and none of it blocks the screens

- **Saves on the right triggers.** Keys do it today, which is the test
  environment and not the product. The settled triggers are in PROJECT.md.
- **The file-writing save class is not synced at all.** melonDS writes a `.sav`
  rather than exposing save RAM, so `[save] battery is 0 bytes` is correct and
  the file never reaches RomM. Neo Geo Pocket, Sega CD and FBNeo are the same
  class and all three are playable today.
- **Nothing warns that a system's BIOS is missing** until a game fails to start.
- **Hardware-rendered cores.** Flycast and Mupen64Plus are built and cannot run:
  they want a GL context the frontend does not hand over. 43 more games, plus
  PPSSPP once built.

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
- **`pgrep -f "some string"` matches your own command line.** Three times now,
  and the third was the expensive shape: a wait loop,
  `until ! pgrep -f "git clone.*flycast"; do sleep 30; done`, where the shell
  running the check has that very text in its own command line, so the pattern
  matches the searcher and the condition can never come true. It sat there for
  nine hours waiting for something that had already finished. **Match on
  something the checker cannot contain** — a pid file, `pgrep -x`, or the exit
  status of the thing you actually started.
- **Look on disk before concluding a file does not exist.** `core-manifest.json`
  is at `~/Downloads/core-manifest.json` and is not on GitHub.
- **The frontend compiles in CI now**, as of 2026-09-16, and did not before —
  it had only ever been built on the test VM, which is the single-machine
  dependency this project called out for cores and then did not apply to the
  program that loads them. Do not let that slide back.
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
