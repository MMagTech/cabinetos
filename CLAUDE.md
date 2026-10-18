# CabinetOS, for coding sessions

CabinetOS is a console operating system for RomM libraries: a bootc image on
Bazzite that boots into one C++ program, the frontend in `frontend/`, which
draws every screen and hosts the emulators. Everything comes from the
player's RomM server.

## Read first

1. [CONTRIBUTING.md](CONTRIBUTING.md): building, testing, what CI checks,
   how a change reaches a console, and the project's rules.
2. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): the map of the code, what
   each part owns and must never do, the main flows, where to look.
3. [docs/lessons/README.md](docs/lessons/README.md): the ten rules, then the
   lessons file for the area in hand.
4. [docs/NEXT-SESSION.md](docs/NEXT-SESSION.md): what is being worked on now.
5. [docs/WORKING.md](docs/WORKING.md): how MMagTech works: the loop, the
   testing branch, merges, judging on the television.

## Build, run, test

- Nothing builds on a Mac and the console's image has no compiler. The
  frontend builds in the `cabinetos-builder` container on Linux
  (CONTRIBUTING.md, "Building"): in CI, or in that container on the
  reference console in the maintainer's loop (`tools/ui-loop.sh`).
- Headless runs and the self-tests: CONTRIBUTING.md, "Running and testing".
- Never write to a RomM server you do not own. Test through
  `tools/romm-readonly-relay.py`.

## Rules for every change

MMagTech's six, for this code:

1. **Search before you write.** Before a new function or file, grep for one
   that already does the job (`frontend/src`, and `build_files/`, `ci/` and
   `system_files/` for scripts): whole files (`fileio.h`), running a program
   (`proc.h`), where things live (`storage.h`), RomM (`romm.h`), the design's
   numbers (`design.h`), a worker polled once a frame (`job.h`). Use or
   extend it. A new file is named in the pull request, with why.
2. **New code goes where docs/ARCHITECTURE.md puts its job.** A new
   self-contained job gets a file of its own, named for the job, never a few
   hundred lines added to a file that does something else. A new kind of
   place (a folder, a program, a part ARCHITECTURE.md does not name) is
   asked about before it is made.
3. **Remove what you replace**, in the same change: the old function, its
   declaration, its callers, its flag, its comment. If it must stay, say why
   in the pull request.
4. **No commented-out code**, and no `#if 0`. Git keeps old code.
5. **Docs change with the code.** A change that adds, removes, renames or
   moves a file, or changes who calls whom, updates docs/ARCHITECTURE.md in
   the same pull request. A comment that names a file, a line or a function
   is updated when that thing moves: a stale comment reads exactly like a
   current one (lessons rule 8).
6. **Before saying it is done:** the frontend builds in the builder with no
   more warnings than `main` has (`build-frontend.yml` lists them);
   shellcheck passes on every script you touched;
   `cabinetos-frontend --check-option-tables cores/options` passes if you
   touched a core or an option; the self-tests your change touches pass.
   List every file created, deleted or moved, and check the docs still
   match. If a check fails, say so; never work around it.

The project's own rules (RomM only, no em dashes on screen, never rely on
Steam, Vulkan first, one rule for every game, and the rest) are in
CONTRIBUTING.md, "The rules". MMagTech's working rules for sessions are in
docs/WORKING.md and docs/NEXT-SESSION.md.

## Machine details

This repository is public, so no new address, host name, password or home
path of anybody's machines goes in a tracked file. The ones still there are
being moved out (docs/ARCHITECTURE.md, "Known gaps"). The maintainer's are in
a file in his home folder, loaded here:

@~/.claude/cabinetos.local.md

[CLAUDE.local.example.md](CLAUDE.local.example.md) says what goes in it. A
session without that file loses nothing but those addresses.
