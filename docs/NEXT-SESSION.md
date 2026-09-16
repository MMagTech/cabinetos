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

- **986 of 1644 games playable**, with fifteen cores built.
- **Home is real**: a hero from RomM's own play history, Recent, Favorites,
  focus moving between rows, scrolling.
- **Downloads stream to disk on a worker.** Nothing that talks to a server stops
  the picture — measured at 3.21 ms on the frame thread for a save.
- **Saves, memory cards and states sync both ways** with RomM, tagged with
  Cabinet's own emulator strings so they interchange with the Apple apps.
- **BIOS comes down with the game**, every file the platform lists.
- **An in-game overlay**: Start or Escape. Resume, save state, load state, exit.
- **Fifteen cores build in CI**, each asserting its pinned revision.

## Pick up with one of these

**1. The four remaining backend questions, and mGBA.** melonDS, picodrive,
Flycast and mupen64plus each turn on a recompiler Cabinet has off, and each
needs the check pcsx_rearmed just had before its emulator tag can be shared —
see open question 13 for how that went and what the object diff showed. mGBA is
CMake rather than a Makefile and needs a different path in `build-core.sh`.
Together they are worth about 260 more games.

**2. Nothing evicts anything.** A ROM already on disk at the right size is
reused, and that is all. 1644 games at these sizes do not fit on a console, so
the disk fills and stays full. The design is settled in PROJECT.md — cached is
evictable, kept is not, the person only ever opts *in* to keeping — and none of
it is built.

**3. The Library screen.** 986 playable games and only the ~50 on Home can be
reached. Home already points at a Library that does not exist.

**4. Saves on the right triggers.** Keys do it today, which is the test
environment and not the product. The settled triggers are in PROJECT.md: when
the game writes its memory card, from the overlay, on leaving a game, and a
controller combination.

## Things that will bite you

- **Judge nothing visual on the VM.** Software rendering on llvmpipe. And it is
  not only motion: a television's overscan eats more vertical room than a
  framebuffer capture shows, which is how Cabinet's hero height needed four
  attempts on real hardware. **Vertical fit cannot be judged here either.**
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
- **`catalog::coverageFor` answers three different questions.** No core exists,
  a core exists and Cabinet does not ship it, and *this console has not built it
  yet*. Collapsing them hides how much of the library is waiting on work.
- **Every scripted edit must assert its anchor.** A `python - <<PY` that
  replaces text it cannot find changes nothing, the build stays green, and the
  feature silently is not there. That happened twice in one session.
- **Read the evidence, not just the code.** The filename sanitiser turning
  "Pokémon" into "Pok__mon" was visible in the server's own listing.
- **`pgrep -f "some string"` matches your own command line.** Twice mistaken for
  a still-running process.
- **Look on disk before concluding a file does not exist.** `core-manifest.json`
  is at `~/Downloads/core-manifest.json` and is not on GitHub.

## The state that lives on the VM and not in git

- `~/frontend/` — the frontend source, built with
  `podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make`
- `~/frontend/cores/build/` — fifteen built cores, where the frontend looks
- `~/cabinetos/` — a clone of this repo, where `cores/build-core.sh` runs
- `~/run-frontend.sh` — the session launcher; points at the live RomM server.
  The original is `run-frontend.sh.bak`
- `~/.config/cabinetos/romm.json` — the RomM token, 0600. Nine scopes: read the
  library, write only the person's own play data
- `romcache/`, `system/` under `~/frontend` — downloaded ROMs and BIOS

Sudo on the VM needs the password `cabinet`, a throwaway from the public repo's
`disk_config/disk.toml`.

## Two Cabinet-side debts

1. **Flycast carries unscripted edits in its working tree**, so its pinned
   commit does not reproduce what ships. Capture that diff before anything
   touches the tree — and Flycast is one of the four cores whose backend
   question is still open.
2. **`core-manifest.json` is still not pushed to GitHub.** It is load-bearing
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
