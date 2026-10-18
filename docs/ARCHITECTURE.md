# CabinetOS architecture

How the code is laid out, what each part owns and must never do, and what
happens, step by step, in the flows that matter most. Written from the code
at the restructure of #398 ("Audit the code and restructure it for
contributors (main.cpp is 20,000 lines)"), October 2026. Start with
[CONTRIBUTING.md](../CONTRIBUTING.md) if you have not.

**When you change the layout, change this file in the same pull request**:
a new file, a moved job, a renamed part, or a change to who calls whom.

## 1. The map

### The repository

| Folder or file | What is in it | Where it ends up |
|---|---|---|
| `frontend/` | The console's one program, in C++20: `src/` (313 files, one folder; the Makefile compiles these and rcheevos), `data/` (shaders and controller lists), `third_party/rcheevos/` (RetroAchievements, vendored), `covers/` (the stand-in library's covers), `tools/fake-nmcli/` (a stand-in network tool for trying first run) | `/usr/bin/cabinetos-frontend` |
| `frontend/ps2/` | The PlayStation 2 host around PCSX2, compiled by `cores/build-pcsx2.sh` into `cabinetos-ps2.so`, not by the frontend's Makefile (so a pull request to `main` never compiles it) | `/usr/lib/cabinetos/cores/` |
| `wiibridge/` | `cabinetos-wii-bridge`, a stand-in for each real Wii Remote; GPL, its own program | `/usr/libexec/cabinetos-wii-bridge` |
| `cores/` | How every emulator the image carries is built: `build-core.sh` (the 22 libretro cores, each pinned by commit in its `case` arm), `build-pcsx2.sh`, `build-cemu.sh`, `build-sunshine.sh` and their builder containers; `options/<core>.txt`, every option each core declares at its pin | `/usr/lib/cabinetos/cores/` and the emulators' folders |
| `build_files/` | The image build's scripts: `build.sh` and the scripts it calls (remove the desktop and the storefronts, set up the session, the branding), `install-*.sh` (one per piece the image carries), `require-*.sh` (checks that fail the build) | run once, inside the image build |
| `system_files/` | Files copied over the image as they are: the session script, systemd units, the root helpers in `usr/libexec/`, polkit rules, udev rules, the diagnostic report, and a few files in `etc/` (the SSH settings, the update signing key) | `/usr` and `/etc` |
| `ci/` | What the workflows run: collecting the build outputs (`stage-image-payload.sh`), version numbers, promoting the tested image, watching the Bazzite base, and the checks every pull request runs (`checks.yml`: `check-romm-paths.sh`, `check-doc-paths.py`) | GitHub Actions |
| `.github/` | The workflows, the issue form for players, and the pull request template | GitHub |
| `tools/` | Test and maintenance tools, run by hand; they take their machines from the environment (CONTRIBUTING.md, "Running and testing") | nowhere |
| `disk_config/` | The installer and test-disk layouts | the installer ISO |
| `licences/` | The full licence texts the image ships, and `SOURCES`, where each comes from | `/usr/share/licenses/` |
| `docs/` | The specification, the decisions, the lessons, the website (`index.html` and `wiki/`, served by GitHub Pages), and `history/`: docs that describe the project as it was, each saying what replaced it | only `docs/LICENCES.md` |
| `Containerfile` | The image: the Bazzite base pinned by digest, then the build scripts in several layers | |
| `Justfile`, `cabinetos.env` | The build recipes CI calls, and their parameters | |
| `base-manifest.txt` | The base image's package list, for the weekly base-update check | |

### frontend/src

Each file opens with a comment saying what it holds; read that first.
Where a screen and the App's side of the same feature would share a name,
the screen's file says so: `libraryscreen.cpp`, `searchscreen.cpp` and
`detailscreen.cpp` are screens, and `library.cpp`, `search.cpp` and
`detail.cpp` are the App's side. A `*flow.cpp` file is the App's side of
one feature, most of them reached from Settings. Every App member function
is in the file named for its feature, and
`grep -n 'App::name(' frontend/src/*.cpp` finds it.

| Area | Files |
|---|---|
| Start and the frame | `main.cpp` (main() and App::run, the start-up in order), `cli.cpp` (every flag but `--check-option-tables`, which `main()` answers before the App exists), `startup.cpp` (start-up and shutdown steps), `frame.cpp` (the frame loop; its order is the behaviour), `input.cpp` (every event, in the order that decides who gets it), `nav.cpp`, `bar.cpp` (the top bar), `curtain.cpp` |
| The App | `app.h` (everything the program holds, as one struct), `actions.cpp` (the one place a screen's request is answered) |
| Screens | `screens.h` (what they share), `libraryscreen.cpp`, `grid.cpp`, `searchscreen.cpp`, `detailscreen.cpp` (a game's page), `settings.cpp`, `accountpanel.cpp`, `qrlink.cpp`, `choice.cpp`, `pin.cpp`, `keyboard.cpp`, `downloads.cpp`, `manualview.cpp`, `achlist.cpp`, `startscreen.cpp` |
| App code that draws its own screen | `home.cpp` (Home), `setup.cpp` (first run; its rules are `firstrun.cpp`, with `firstrun_internal.h`), `serverwait.cpp` (the wait for the server at start). These act as well as draw: first run pairs and joins networks, the wait talks to the server |
| Drawing | `ui.h`/`ui.cpp` (the primitives), `design.h` (the design system's numbers), `motion.h`, `text.cpp`, `image.cpp` (covers), `imagecodec.cpp`, `card.h`, `panels.h`, `screenparts.cpp`, `look.cpp` (the menus' colour), `looks.cpp` (a system's screen looks), `backdrop.cpp`, `notice.cpp` and `noticegallery.h` (the short messages), `qr.cpp` and `qrtexture.h`, `timewords.cpp`, `capture.h` (the SIGUSR1 picture), `shaders.inc` |
| The RomM client | `romm.h` (its face and its rules), `romm_types.h` (what it hands back), `romm_internal.h`, `romm_http.cpp` (every request goes out here), `romm_pairing.cpp`, `romm_library.cpp`, `romm_playdata.cpp`, `romm_codes.cpp`, `romm_racetest.cpp` |
| Library data | `library.cpp` (the store of cards, the shelves), `catalog.cpp` and `catalog_internal.h` (which platforms play, on which core), `covercache.cpp`, `coverfill.cpp`, `gridfill.cpp`, `search.cpp`, `codecache.cpp` and `gametdb.cpp` (game codes and controller lists read off files), `samplelibrary.h` (the stand-in library) |
| A launch and a game in play | `launch.cpp` (the frame thread's half), `launch_routes.cpp` (the worker), `play.cpp` (in play, and leaving), `pace.cpp` (the game frame), `pausemenu.cpp`, `gamesession.h`, `romfile.cpp` (what to hand a core), `disc.cpp`, `discs.cpp`, `rewind.cpp`, `states.cpp`, `rumble.cpp` |
| The libretro host | `core.h`, `core.cpp`, `core_state.h`, `core_*.cpp` (discs, hardware rendering, input, memory, options, picture, PS2), `vkhost.cpp`, `vkhost_state.h`, `vkbridge.cpp`, `libretro.h` and `libretro_vulkan.h` (the libretro API, the RetroArch team's), and the console's three option tables: `coreoptions.cpp` (set on purpose for every game), `quality.cpp` (Picture quality) and `sysopts.cpp` (a system's pause-menu options); `screenfx.cpp` (screen looks) |
| PlayStation 2 | `ps2.cpp`, `ps2window.cpp`, `overlaywin.cpp`, and `frontend/ps2/` |
| Emulators run as programs | `standalone.h`, `standalone.cpp`, `standalone_play.cpp`, `standalone_eden.cpp`, `standalone_xemu.cpp`, `standalone_rpcs3.cpp`, `standalone_xenia.cpp`, `standalone_cemu.cpp`, `vpad.cpp`, `xtitles.cpp`, `xboxhdd.cpp`, `x360profile.cpp`, `dolphinprep.cpp` |
| Saves and states | `savetable.cpp` (what each core writes, where, and its two tags on RomM: `emulatorTag` for states, `saveTag` for saves), `saves.cpp` (sending and restoring all three kinds of save: `syncSave`, `syncDirSave`, `syncFileSaves`), `dirsave.cpp` and `filesave.cpp` (their helpers), `states.cpp`, `savemirror.cpp`, `uploader.cpp` (the queue), `pending.cpp` (the markers), `offline_sync.cpp`, `playtime.cpp`, `favorites.cpp`; the requests themselves are in `romm_playdata.cpp` |
| Storage | `storage.h` (where everything lives), `storage_internal.h`, `locations.cpp`, `cache.cpp`, `drives.cpp`, `drives_fs.cpp`, `udisks.cpp`, `users.cpp`, `accounts.cpp`, `accountremove.cpp`, `fileio.cpp`, `prefs.cpp` |
| Controllers | `players.cpp`, `controllers.cpp`, `bluetooth.cpp`, `btagent.h`, `battery.cpp`, `batteryrow.cpp`, `shortcuts.cpp`, `wiiremote.cpp` (starts the Wii bridge), `wiipair.cpp`, `wiipanel.cpp`, `wiimii.cpp`, `wii.cpp`, `wiiu.cpp` |
| The machine | `net.cpp`, `wifi.cpp`, `power.cpp`, `power_menu.cpp`, `powerprofile.cpp`, `volume.cpp`, `sound.cpp`, `gpu.cpp`, `cpus.cpp`, `idle.cpp`, `proc.cpp` (running a program with no shell), `unit.cpp` (asking systemd for a root job), `update.cpp` (System update, the console's side), `files.cpp` (File access, the console's side), `server.cpp` and `serveraddress.cpp` (the RomM server this console belongs to) |
| RetroAchievements | `achievements.cpp` (the sign-in and rcheevos' client; its console table names every system), `achievements_ui.cpp`, `ra_list.cpp`, `ra_net.cpp`, `ra_state.h` |
| Settings features | `settingsflow.cpp` (the rows and their dispatch), `settingsids.h`, `about.cpp`, `credits.h`, `access.cpp` (File access and Developer access), `wikiqrview.cpp` and `wikiqr.h` (made by `tools/make-wiki-qr.py`), and one `*flow.cpp` per feature: `accountsflow.cpp`, `downloadsflow.cpp`, `drivesflow.cpp`, `pinflow.cpp`, `serveraddressflow.cpp`, `remoteplayflow.cpp`, `tailscaleflow.cpp`, `steamflow.cpp`, `updateflow.cpp`, `reportflow.cpp`; `choiceflow.cpp` asks a question over any screen |
| Remote Play, Steam, the report | `remoteplay.cpp`, `sunshinelog.cpp`, `tailscale.cpp`, `steam.cpp`, `report.cpp`, `samenames.cpp` |
| Test instruments | `probes.cpp` (runs the probes and calls most self-tests; `--same-names-test` and `--romm-race-test` run from `cli.cpp`), `selftests.cpp`, `coretests.cpp`, `testhooks.cpp`, `discrig.cpp`, `optcheck.cpp` (the option-table check CI runs). Most self-tests live in their feature's own file (`players::test` in `players.cpp`, and so on). They ship in the program and run only from a flag |

### Where it starts

1. systemd starts `cabinetos-session.service` on the first virtual console,
   as the console's own user (`system_files/usr/lib/systemd/system/`).
2. `cabinetos-session` (`system_files/usr/bin/`) reads
   `/etc/cabinetos/session.env`, then starts gamescope on the screen with the
   frontend inside it. When the machine has no Vulkan device gamescope
   accepts, it falls back to cage (software rendered), and last to a headless
   gamescope so the session stays alive.
3. The frontend's `main()` (`frontend/src/main.cpp`) builds the App and calls
   `App::run`.

## 2. What each part owns, and must never do

- **A screen draws, owns its own focus and scroll, and returns an Action.**
  It never touches the network, the disk or a core. (This is the rule for
  the screens past Home in screens.h; Home, first run and the server wait
  are App code, section 1.) `App::apply`
  (`actions.cpp`) is the one place a request is answered, so a screen cannot
  start a download by accident and can be drawn in a test (screens.h's
  header).
- **The App** (`app.h`) owns everything a screen may not: the RomM client,
  the core, the workers, the cache, the navigation. Its member order is its
  destruction order, members point at other members, and threads hold
  `this`, so it is never copied or moved, and a new member goes where its
  dependencies allow (app.h's header).
- **The frame thread must never wait on the network** (romm.h: "IT MUST
  NEVER BLOCK THE FRAME LOOP"). Downloads, uploads, cover loads and most RomM
  requests run on workers; the frame loop reads their state once a frame
  (`job.h`, `launch.h`). **Not yet true everywhere:** a launch loads the core
  on the frame thread at the press, and later restores the game's saves from
  RomM on the frame thread behind the curtain (section 6).
- **The RomM client is the only code that talks to RomM.** It is synchronous
  and owns no thread; callers run it on workers. No password ever reaches it
  (RomM's device pairing gives it a token). Plain HTTP must work. It is
  called from many threads at once: the address and the token are read and
  written only under its lock, and a request takes both together (romm.h's
  header). Known exceptions are listed under "Known gaps".
- **The libretro host** (`core.h`, `core*.cpp`) runs one core at a time
  (`Core::shared()`); libretro's callbacks are plain C, so its state is
  process-wide on purpose (`core_state.h`).
- **Emulators run as programs** are started, watched by their window titles
  and stopped by `standalone*.cpp`; a game in one of them is a game like any
  other to the rest of the console.
- **Storage** (`storage.h`) decides where everything lives. Eviction only ever
  walks `cache/`; kept games (`roms/`) and save data are never evicted.
  A save, a state or a screenshot owed to RomM is a marker on disk before
  the upload is tried (`pending.h`), so a crash or a power cut cannot lose
  it; time played and favourites are owed in their own files
  (`playtime.json`, `favorites-owed.json`).
- **The frontend never runs anything as root.** It runs as the console's
  user. For a root job it starts one named systemd unit (`unit.h`) whose
  script in `system_files/usr/libexec/` answers in a status file; otherwise
  it asks a system service (NetworkManager, udisks) directly. Either way a
  polkit rule in `system_files/usr/share/polkit-1/rules.d/` names what the
  console's user may do, and anything else is refused.
- **Running another program goes through `proc.h`, never a shell.**
  Exceptions today: the `--first-run-writes` and `--playtime-test` self-tests
  remove their scratch folders with `std::system`; the Wii bridge is started
  with `fork` and `execl` (`wiiremote.cpp`); and the frontend restarts itself
  with `execv("/proc/self/exe")` (`startup.cpp`).
- **The image** (`Containerfile`, `build_files/`) puts what it installs in
  `/usr`, which every update replaces, and a few settings files in `/etc`
  (`system_files/etc/`, and the signing policy written by
  `require-signed-updates.sh`). Nothing goes in `/var`, which an update never
  touches (lessons rule 9). The `require-*.sh` scripts fail the build rather
  than ship an image that cannot work.
- **CI** builds, stages and promotes. The weekly jobs open pull requests and
  never merge them.

## 3. The main flows, step by step

### Start-up, to Home

`App::run` (`main.cpp`) calls, in this order:

1. `parseCommandLine` (`cli.cpp`). A flag that exits ends the parse there.
2. `resolveCoresAndServer` (`cli.cpp`): where the cores are (`--core-dir`,
   else `cores/build` when it exists in the working folder, else
   `/usr/lib/cabinetos/cores`), and the server's address: `--romm`, else
   `CABINETOS_ROMM`, else `/etc/cabinetos/session.env`, else the console's
   `config/server.json` (`serveraddress.cpp`).
3. `earlyProbe` (`probes.cpp`): the tests and probes that need no storage
   and no window run and exit here.
4. `startStorage`, then `storageProbe`: the storage tree, then the probes
   that need it.
5. `startDisplay`: the window and the GL context.
6. `runFirstRun`: first run, if the console is not set up (`firstrun.h` for
   its rules, `setup.cpp` for its screens).
7. `startStartupScreen`, then `connectToServer`: the console waits for its
   server; when there is a game on the drive to play, it goes offline after
   15 seconds and tries the server in the background, otherwise it keeps
   waiting (`serverwait.cpp`).
8. `startBrowsing`; `startSync`, which starts the upload queue and sends
   first whatever an earlier run still owed (`uploader.resendOwed`);
   `recoverPowerCutSaves`, which finishes the saves a power cut left in an
   emulator run as a program (an Xbox game's drive through
   `recoverDriveSaves`, which that game's launch also runs); then the rest
   of the start-up steps.
9. `runFrameLoop` (`frame.cpp`) until the console exits or restarts; then the
   game is left, the emulators closed, the core unloaded, and `shutDown`.

### Pressing Play on a game

1. The game's page returns an Action; `App::apply` (`actions.cpp`) calls
   `launchById`, which calls `beginLaunch` (`launch.cpp`). On the frame
   thread and with no curtain, `beginLaunch` sets the core's options and
   loads the core (not for an emulator run as a program), records the keep
   for a Download, decides where the game goes, and starts the launch
   worker.
2. The worker (`launch_routes.cpp`) fetches the platform's firmware from RomM
   first, then takes one of four routes, in this order: a game on several
   discs, a game installed before it runs (PS3), a folder of files, one file.
   The bytes go straight to disk: under `cache/` for a game pulled to play,
   under `roms/` for a game kept, on the console's own storage or a drive.
3. Each frame, `pumpLaunch` (`launch.cpp`) reads the job. A failed job is
   undone (the keep record comes out; the bytes stay, to be evicted). A
   Download that was not a Play stops when the game is on disk. A game the
   person walked away from finishes quietly and does not start. A launch
   that loads no game here (these three, a core that refuses the game, an
   emulator run as a program) forgets what its press asked for: a picked
   state, Home's Resume, a start disc (`forgetLaunchAsks`).
4. The curtain comes down before the rest of the blocking work (restoring
   the saves from RomM and loading the game, both on the frame thread), so
   the person sees a curtain, not a frozen screen. A game for an emulator run as a program starts it here
   (`standalone_play.cpp`) and ends this flow; an Xbox game whose last saves
   are still on its drive has them taken off first (`recoverDriveSaves`,
   `startup.cpp`), or does not start.
5. For a core: this person's save folder for this game and core is made, a
   folder save is restored and the starting disc chosen
   (`prepareSaveDirAndDiscs`); then the file saves (most saves) are restored
   and Dolphin's settings and the Miis written, all before the load
   (`restoreBeforeLoad`).
6. The core loads the game (`loadLaunchedGame`); `setUpLoadedGame` sets up
   the session; then the battery save is restored, the options the core was
   given are logged, sound and pacing open, and play starts
   (`startPlaying`).

### Leaving a game

`App::finishExit` (`play.cpp`), in order: the time played is closed and
queued for RomM; the battery save is sent (`syncSave`); the disc in the drive
is remembered for the next Play, and the drive put back on its first disc;
RetroAchievements stops reading memory; the core unloads; then folder saves
and file saves are sent, after the unload, because that is when a core
flushes them (`syncDirSave`, `syncFileSaves`, all in `saves.cpp`). A game in
an emulator run as a program sends its saves when the program ends
(`standalone_play.cpp`, and `finishInterruptedSave` in `saves.cpp` after a
power cut).

### Saves and states reaching RomM

1. A save, a state or a screenshot is written on disk, and a marker saying
   what to send is written before the upload is tried (`pending.h`; the
   markers are in `users/<id> - <name>/pending/`).
2. The upload queue (`uploader.h`) sends it on its own thread as the person
   who made it (`romm_playdata.cpp` makes the request), and removes the
   marker when RomM has it. A marker left by a crash is sent again at the
   next start (`startSync`). States: the newest three per game and emulator
   tag are kept on RomM.
3. Each core's saves, where it writes them and the tag they carry on RomM are
   one table, `savetable.cpp`. A launch restores the newest row under its
   tag, except that this console's own copy wins while it is still owed to
   RomM, and is used when RomM has none (`saves.cpp`).
4. Offline, the console uses its copy of RomM's rows as last seen
   (`savemirror.h`), so a launch decides as it would online. What offline
   will need is fetched while online, and the server is tried in the
   background while it is away (`offline_sync.h`); what is owed waits as
   markers on disk.

### System update

1. The frontend (`updateflow.cpp`, through `update.cpp`) starts
   `cabinetos-update-check.service` with `unit::start`, by hand or weekly
   when that setting is on.
2. The unit runs `/usr/libexec/cabinetos-update check` as root: `bootc
   upgrade --check` and the size of what is missing. It writes the answer to
   `/run/cabinetos-update/status`.
3. The root script decides: it reports an update whenever bootc finds a
   different image (bootc compares digests, not versions), and "ready" when
   one is already staged. The frontend shows what the file says, and between
   checks still offers a version it found earlier if that is not the one
   running.
4. Download starts `cabinetos-update-download.service`, which stages the new
   image; the next restart or power off applies it.

How a change gets into that image is in CONTRIBUTING.md, "How a change
reaches a console".

## 4. Where to look for the most common changes

| To change | Look in |
|---|---|
| Words or layout on a screen | the screen's file (section 1), its numbers in `design.h`; judged on the television |
| A Settings row | `settingsflow.cpp` (the rows and what each does), the feature's `*flow.cpp`, docs/SETTINGS.md |
| What a button does, or where it goes | `input.cpp` (who gets the event), `nav.cpp`, `actions.cpp`; then About, How to, and `docs/wiki/navigation.md` |
| Which core plays a platform | `catalog.cpp` (the platform table), `savetable.cpp` (its saves' tag) |
| An option the console sets on a core | `coreoptions.cpp`, `quality.cpp`, `sysopts.cpp` and `wii.cpp` (Dolphin's rumble for Wii): the four sources `launch.cpp` merges, in that order, and `--check-option-tables` checks against `cores/options/` (`optcheck.cpp`); a libretro core in `catalog.cpp`'s table with no list there fails it |
| A new libretro core | `cores/build-core.sh` (a `case` arm; its name there is the core's name everywhere, and the file is that name without a trailing `_libretro`, plus `_libretro.so`), `build-core.yml`'s list, the two counts of 22 (`ci/stage-image-payload.sh`'s `EXPECTED` and `build_files/install-frontend.sh`), `cores/options/<core>.txt` (made from what `tools/core-options.c` prints for the built core; `cores/check-options.sh` runs it), `catalog.cpp` (the platform's row; the table is positional, read its struct first), `savetable.cpp` (its two tags), `coreoptions.cpp`, `achievements.cpp`'s console table for a new system, `docs/wiki/systems.md`, and the licences (CONTRIBUTING.md, "Duties") |
| Moving a core's pin | its `COMMIT` in `cores/build-core.sh`; in the same commit `cores/options/<core>.txt`, and the same commit in `licences/SOURCES` (then `tools/fetch-licences.py`) and `docs/LICENCES.md` |
| An emulator run as a program | `standalone.h`, its `standalone_<name>.cpp`, its `build_files/install-*.sh` or `system_files/usr/share/cabinetos/flatpaks.list`, its row in `catalog.cpp` |
| A RomM request or field | the client (`romm.h`, `romm_*.cpp`, `romm_types.h`); RomM's documentation and behaviour decide; the map of every request is #398's Part 5, section 48 (the audit's comments on #398 each start "Audit, Part N", and sections are numbered across the parts) |
| Saves and states | `savetable.cpp` (what and where), `saves.cpp` (sending and restoring), `states.cpp`, `uploader.cpp` (the queue), `pending.cpp` (the markers), `romm_playdata.cpp` (the requests), `standalone_play.cpp` (emulators run as programs) |
| Something in the image (a package, a unit, a rule) | `build_files/`, `system_files/`; an image build, so it is proven on `testing` |
| Something the console must do as root | a script in `system_files/usr/libexec/`, its unit, a polkit rule naming the unit, `unit::start` from the frontend, and a status file |
| A command-line flag or a self-test | `cli.cpp` (the flag), `probes.cpp` (where a probe or test is called), the feature's own file for its test (`players.cpp`'s `players::test`), `selftests.cpp`, `testhooks.cpp`. There is no test framework: a test is a flag that exits with 0 when it passes |

## 5. Decisions, and why

Each with where it is recorded.

- **Screens return Actions; the App acts.** So a screen cannot start a
  download by accident (main.cpp's and screens.h's headers).
- **One program, which owns the frame and hosts the cores itself**, instead
  of a skin on RetroArch or another frontend (docs/PROJECT.md, "Constraints
  and principles", 4; "How Cabinet hosts cores").
- **Big emulators run as programs, played as games** (docs/PROJECT.md, open
  question 32).
- **Pulled games are a cache; kept games stay** (docs/PROJECT.md, "The
  backend model"; `cache.h`).
- **gamescope, not Bazzite's gamescope-session-plus** (cabinetos-session's
  header; docs/PROJECT.md, open question 3).
- **The on-disk layout** (docs/PROJECT.md, open question 18; `storage.h`).
- **Vulkan first in the core host** (docs/PROJECT.md, open question 20).
- **The tested image is what ships**; a merge promotes it instead of building
  again (docs/SETTINGS.md, System, "THE TESTED IMAGE IS WHAT SHIPS").
- **Updates are signed, and the console checks** (`build_files/require-signed-updates.sh`).
- **No emulator settings page; three picture levels** (docs/SETTINGS.md, "The
  screen"; `quality.h`).
- **Settings, as built** (docs/PROJECT.md, open question 31; docs/SETTINGS.md
  is the final state).

## 6. Known gaps

- **The console follows any web address RomM's data hands it, with the
  sign-in attached:** `Client::fetchBytes` (`romm_http.cpp`) takes a path
  that starts with `http` as the whole address, sends the token and follows
  redirects. This breaks the rule that the console connects only to fixed
  places (CONTRIBUTING.md, "The rules"); it is #380 ("The console follows any
  web address in RomM's data, with the RomM sign-in attached").
- **The frame thread waits on the network during a launch:** `beginLaunch`
  loads the core, and `pumpLaunch` restores the game's saves from RomM and
  loads the game, all on the frame thread, behind the curtain
  (`launch.cpp`, `saves.cpp`).
- **RomM paths spelled outside the client:** the firmware and one-file game
  downloads (`launch_routes.cpp`), `probes.cpp`, the manual (`manual.cpp`),
  avatars (`startup.cpp`), small and big covers (`image.cpp`) and the cover
  cache's key (`covercache.cpp`). They should move into the client.
  `ci/check-romm-paths.sh` lists them and fails a pull request that adds
  another.
- **Threads not yet put right:** #404 ("The launch worker writes the job's
  entry path while the frame thread reads it"), #405 ("favorites.json is
  written from two threads through the same temporary file with no lock"),
  #406 ("Four background threads are never stopped at exit (pairing, remove
  and sign out, reconnect, drive backfill)").
- **CI runs one self-test** (`--check-option-tables`); the others are run by
  hand. No Python is tested, and neither are `tools/` and `disk_config/`.
  A pull request to `main` never builds the cores, `frontend/ps2`, Cemu,
  Sunshine or the image, so a fault there first shows at the maintainer's
  testing push, including a core's options list that no longer matches
  (`cores/check-options.sh` runs inside the core build).
- **No test framework and no single local check command:** a test is a flag
  in the shipped program. The compiler's warnings are listed, not counted
  against a baseline.
- **Some test tools lean on state the maintainer set up by hand** on his
  console: `tools/ui-loop.sh` (a folder of cores and the builder image),
  `tools/check-applied.sh` (the builder image) and the disc rig
  (`tools/disc-rig/README.md`). Their machines come from the environment
  (CONTRIBUTING.md, "Running and testing").
- **Some of the maintainer's own details are still in the code:** the empty server
  address field on first run shows his server's address as its example
  (`setup.cpp`), and comments carry his controllers' Bluetooth addresses
  (`bluetooth.cpp`, `bluetooth.h`, `players.cpp`, `players.h`).
- **A fork's image carries the project's key and image name until the fork
  edits them:** they are written into tracked files (CONTRIBUTING.md,
  "Generate a signing key", lists them).
- **Two test stand-ins are not in the repository:** the frame-time tool many
  comments cite as `frames.py`, and `racemock.py`, which `--romm-race-test`
  needs (`romm_racetest.cpp`).
- **Not built yet:** choosing a different core for a game, and exporting
  (`detailscreen.cpp`'s note).
- **The test instruments ship in the program**, run only by a flag.
- **A scratch `--storage-root` does not isolate a run on a console:** the
  sign-in tokens and the PIN live in `~/.config/cabinetos/` and Remote
  Play's files in `/var/lib/cabinetos`, whatever the storage root
  (`accounts.cpp`, `remoteplay.cpp`), so a headless test on a signed-in
  console acts as that console's person.
- **No documented way to run your own build on your own console**, and the
  environment variables a run off a console needs (`CABINETOS_SHADERS`,
  `CABINETOS_PAD_DB`, `CABINETOS_ASSETS`, `CABINETOS_STORAGE`,
  `CABINETOS_DRIVES` and others; `grep -n 'getenv("CABINETOS_' frontend/src/*.cpp`)
  are written down nowhere.
- **Positional data that an ordinary edit can break silently:** the App's
  member order (app.h), `Uploader::Job`'s field order (uploader.h), and the
  rows of catalog.cpp's table.
- **Building leaves files in the tree:** the Makefile writes `.o` and `.d`
  files beside the sources, and the `.d` files are not ignored by git.
- **The faults the audit recorded** are on #398, Parts 3 to 5, and become
  pull requests or issues next.

## 7. Where the docs and the code disagree

The audit (#398, Part 5 sections 44 and 45) found the docs and the code
apart in many places. The docs were brought into line in the docs tidy
(#427, "Docs tidy: every doc true to the code, or marked as history
(#398)", 2026-10-17): frontend/README.md cut down to the build; the
2026-09-20 snapshot, the three history files and the old frontend README in
`docs/history/`; the stale lines fixed where they stand, and in
docs/PROJECT.md noted under the stale line with a date, since its rule is
to record a reversal rather than edit history; an index at the top of
PROJECT.md giving where each question ended. What is left is in code
comments, which the faults step fixes. Until then, trust the code:

- **Code comments:** 59 point at the wrong place, most saying "in main.cpp"
  for code the restructure moved (app.h's own header among them). Several
  scripts say twenty-one cores (`Containerfile`, `.gitignore`,
  `cores/build-pcsx2.sh`, which also says it applies no patches and applies
  one); `build_files/build.sh` and `build_files/require-cec.sh` call HDMI-CEC
  a hard requirement, though it was shelved on 2026-09-24; several workflow
  headers say pull requests to `main` build the image, which they do not.
  Others: `frontend/Containerfile` cites an `archive.h` that `frontend/src/`
  does not have; `selftests.cpp` says `--first-run-rules` runs in CI (it does
  not); `samplelibrary.h` says main.cpp is its only user (startup.cpp is);
  `cores/check-options.sh` names catalog.cpp for what is now
  `coreoptions.cpp`; `catalog.cpp` says Cabinet's manifest is the source of
  truth for its table; `cores/build-core.sh` says core pins follow Cabinet's
  manifest and "are not to be updated here", which predates both Cabinet
  ceasing to be a constraint (2026-10-11) and the stability rule for moving
  pins (docs/PROJECT.md, "Constraints and principles", 8, 2026-10-14), which
  is the rule now.
- **More code comments the docs tidy found:** Developer access placed in
  "Settings, System" (`system_files/etc/ssh/sshd_config.d/30-cabinetos.conf`,
  `cabinetos-developer.service`, `enable-ssh.sh`, `app.h`; it is the last row
  of About); `frontend/Makefile` points to README.md for the loop, which is
  CONTRIBUTING.md, "Running and testing"; `frontend/ps2/CabinetPS2Host.cpp`
  and `.h` cite docs/PCSX2-HOST-SURFACE.md, now in `docs/history/` (a stub
  keeps the old path); `storage.h`'s layout comment, which
  frontend/README.md's tree corrects; Flycast said to have no shared tag
  (`gamesession.h`, `cores/build-core.sh`; `savetable.cpp` gives it one);
  `savetable.cpp`'s own claim that only cores built as Cabinet builds them
  are listed; "twenty-one cores" in the workflows too (`build.yml`,
  `build-core.yml`, `build-pcsx2.yml`, `build-frontend.yml`).
