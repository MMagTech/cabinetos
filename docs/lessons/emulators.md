# Lessons: emulators and cores

Read before working on a core, an emulator, input, saves or states.

## Before believing a core works

- **Build a core, then RUN it.** Five for five — and the fifth was the worst,
  because nothing was newly built at all: every one of the 223 arcade games
  turned out to be unable to start, and had been for a day. **A green build and
  a passing screenshot say nothing about whether a game runs.**

- **A dark first capture is usually a slow boot.** PlayStation needs about
  6000 frames to clear the Sony logo on this VM, Saturn 2600, Sega 32X 2000,
  Neo Geo Pocket 1200. **And a Saturn capture is not repeatable** — the same
  2000-frame run gave max=209 and then max=8.

- **A core will happily "load" something that is not a game.** Genesis Plus GX
  accepted an HTML error page, reported correct Master System geometry, ran,
  and drew black for three thousand frames.

- **A silent fallback is worse than a failure.** Flycast substitutes its own
  boot ROM when it cannot find the real one and says nothing at any log level.

- **A fact carried across is a fact nobody has checked.** Three of the eight
  rows in a save-path table taken from a working implementation were wrong on
  this console. All three failed silently. Two minutes of `find` after a launch
  caught all three.

- **`av_info` is a narrow probe.** Geometry, frame rate, sample rate.

- **An unanswered libretro core option is NOT the default.** The core skips the
  case and the C global keeps its zero value. **This is a demonstration, not an
  argument**: run `--core-options-off` and launch a PSP game and it ends at
  *"the core needs a render target this context cannot build"*.

- **A core that declares no options is the suspicious case, not the clean one.**
  FBNeo and MAME declare theirs per driver, so the table does not exist until a
  game is loaded.

- **Ask the CORE, never the platform**, whether an archive should be opened.

- **Never dispatch on a file extension.** Thirty-two files in the reference
  library have none. Sniff the magic bytes.

## Input

- **`RETRO_DEVICE_INDEX_ANALOG_BUTTON` IS NOT A STICK.** It is libretro's third
  analogue index and its `id` is a joypad button id — L2 is 12, R2 is 13. Any
  code that tests for the LEFT index and treats "everything else" as the right
  stick answers "how far is the trigger pressed" with the right stick's Y axis.
  That is what this console did until 2026-09-19.

- **AND ANSWERING IT WRONGLY IS WORSE THAN NOT ANSWERING IT.** Flycast reads
  the analogue trigger first and falls back to the digital L2/R2 bit **only
  when that value is exactly zero**. Cabinet returns a plain 0 here and
  therefore works by taking the fallback; a real pad's right stick rests a few
  hundred counts off centre, which is not zero, so the fallback never ran. On
  Dreamcast those triggers are the accelerator and the brake.

- **A BUTTON THAT DOES NOTHING IS USUALLY CORRECT.** A RetroPad has sixteen
  inputs and a real machine has fewer. The Dreamcast pad has no shoulder
  BUTTONS at all — its L and R are analogue triggers, so on a Switch Pro
  Controller the top shoulders are meant to be silent and ZL/ZR are the
  triggers. The console prints this per game now:
  `[input] port 0 does nothing in this game: ...`, which is the half that
  answers the question somebody actually asks.

- **Telling a core about SOME of its ports is the same as telling it about
  none.** Flycast returns early from `retro_set_controller_port_device` while
  any of its four ports is unset.

## Picture and rotation

- **THE CONSOLE RUNS ON X11, NOT WAYLAND.** gamescope embeds an Xwayland
  server and SDL picks the `x11` driver, so the GL context is GLX and **there
  is no EGL display in the process at all**. A picture handed over as an
  EGLImage fails with `EGL_NOT_INITIALIZED` on the television while working
  perfectly under `SDL_VIDEODRIVER=offscreen`, where SDL does use EGL. It cost
  a PlayStation 2 game that played with sound and a black screen.

- **THE COPY BETWEEN VULKAN AND GL COSTS TWELVE MICROSECONDS.** Everything else
  in that path is the frontend waiting for the EMULATOR to finish drawing,
  because the core's work is queued ahead of ours. Both obvious optimisations
  — an optimally-tiled destination through `VK_EXT_image_drm_format_modifier`,
  and an exported semaphore instead of the fence — were reasoned about, one was
  BUILT and measured, and neither is worth anything. The tables are in
  `vkhost.cpp`'s `createShared`. **Do not rebuild either without a number that
  contradicts them.**

- **A core reports TWO geometries and neither is wrong.** MAME 2003-Plus
  declares 224x256 in `av_info` for Arkanoid — the picture as SHOWN, already
  turned — and hands back 256x224 from `video_refresh` every frame. A layout
  must use the second. The `[core] WxH` line at load prints the first.

- **A TURNED BOARD'S DECLARED ASPECT IS ALREADY TURNED.** FBNeo says 0.75 for
  DoDonPachi while handing back 448x224. Inverting it turns the picture twice
  and stretches it, which Cabinet shipped once and wrote down.

- **A core calling `SET_ROTATION(0)` is ordinary, not a no-op to ignore.**
  Flycast does it explicitly, which silently overwrote a rotation forced in for
  a test. Anything per-game must be cleared in `loadGame`, not in `load`.

- **Only ARCADE cores ever rotate.** MMagTech, 2026-09-19: a console was built
  to put its picture on a television the right way up. It is what bounds the
  rule above to boards, where it is safe.

- **Rotation does not come from a MAME DAT and never did.** Checked in
  Cabinet's own tree because it was raised as a likely memory: the three
  MAME-derived JSON files it ships hold `rotary`, `dial`, `trackball`,
  `pedals`, `lightgun` and `paddle` — control panels, not screens.

## Cores, naming and coverage

- **`catalog::coverageFor` answers FOUR different questions.** No core exists, a
  core exists and Cabinet does not ship it, this console has not built it, and
  it is built and cannot be driven. Collapsing any two hides work.

- **The core-file naming rule now lives in THREE places** —
  `catalog::coreFileName`, `cores/build-core.sh` and
  `ci/stage-image-payload.sh` — and the comment is on all three. A manifest
  name already ending in `_libretro` does not get a second one. Getting it
  wrong cost every arcade game on this console for a day.

## PS2 and GameCube

- **`dolphin_renderer` IS NOT AN API SELECTOR.** It takes "Hardware" and
  nothing else in a release build; setting it to "Vulkan" silently turns
  hardware rendering OFF. The API comes from what the frontend advertises in
  `GET_PREFERRED_HW_RENDER`.

- **DOLPHIN DECLARES ZERO CORE OPTIONS UNTIL A GAME IS LOADED**, so
  `--core-options` reports none for it. That is the suspicious case, not the
  clean one — the same shape as FBNeo and MAME.

- **DOLPHIN'S USER DIRECTORY IS UNDER THE SAVE DIRECTORY, NOT THE SYSTEM ONE.**
  `Boot.cpp` prefers `<saveDir>/User` when the frontend gives it a save
  directory, and only falls back to `<system>/dolphin-emu/User`. An hour went
  on a `Dolphin.ini` written in the second place and read from the first.

- **`pcsx2_shared_memory_cards` DEFAULTS TO ON** and puts every game's save in
  one `Mcd001.ps2` in the system directory — a card that belongs to no rom and
  therefore cannot be synced at all. **`pcsx2_analog_mode1` DEFAULTS TO OFF**,
  which is the DualShock's analogue mode disabled and reads as dead sticks.
  Both are in `catalog::optionOverrides` now.

- **ROMM MATCHES A SAVE ROW BY FILENAME ALONE, AND THE MAC'S SPELLING IS
  DIFFERENT FROM THIS CONSOLE'S.** `cabinet-604.ps2` against
  `Burnout 3 Takedown (Cabinet).srm`. Four separate things had to agree before
  one card could travel — the format, the name, the region extension and the
  emulator tag — and three of them were wrong. See open question 12b.

- **DOLPHIN PUTS THE REGION *AND THE CARD SIZE* IN THE FILENAME.** Ask for
  `cabinet-937.raw` and get `cabinet-937.USA.raw`, or `cabinet-937.USA.251.raw`
  for a 2 MB card. With the name goes the row's identity on the server, so
  `MemoryCardSize` is pinned here. **Cabinet for Mac leaves it at -1** and has
  the same latent fault — see the Cabinet-side debts.

- **A FRESHNESS RULE IS NOT OPTIONAL FOR A PLATFORM WHOSE CORE CREATES ITS OWN
  CARD.** Six of the seven PS2 and GameCube rows on the reference server held
  nothing: three PS2 cards were 8,650,752 bytes of `0xFF` with no format header
  at all, and three GameCube cards had nothing in either copy of their
  directory. Deleted 2026-09-20 with MMagTech's say-so, each re-verified empty
  immediately beforehand.

## Building a whole emulator (PCSX2)

- **A LIBRARY THAT BUILDS TELLS YOU ALMOST NOTHING. A LIBRARY THAT LINKS TELLS
  YOU EVERYTHING.** `libpcsx2.a` built on the first real attempt and that result
  was nearly worthless on its own: a static archive resolves no symbols, so it
  cannot report a single missing host function. Cabinet's own comment on
  `CabinetPS2Smoke.cpp` says exactly this and it is why that file exists.

- **THE CHEAPEST LINK TEST WAS ALREADY IN THE TREE.** `pcsx2-gsrunner` is
  upstream's own Qt-free frontend, one file, 1332 lines, implementing the whole
  `Host` contract. Building it proved linkability in 2.2 seconds and needed no
  code from us. **Look for upstream's second frontend before writing a smoke
  test** — PPSSPP, Dolphin and RPCS3 all have one too.

- **A SHARED OBJECT LINKS HAPPILY WITH UNDEFINED SYMBOLS**, then fails at
  `dlopen` naming only the FIRST one. That is the worst possible instrument for
  sizing a job: it says "you are missing `g_host_hotkeys`" whether you are
  missing one symbol or two hundred. **`-Wl,-z,defs` makes the linker refuse and
  name them all**, which turned "some unknown amount of host layer" into 57.

- **AND THE FIRST ONE IT NAMES IS A VARIABLE, NOT A FUNCTION.**
  `g_host_hotkeys` is a global the frontend must define. Anybody grepping the
  `Host::` namespace for it will not find it.

- **`find_package(X11)` SUCCEEDS WITHOUT `libXi-devel`** and then the build
  fails at CMake GENERATE time, after "Configuring done", on a missing
  `X11::Xi` target. It reads like a CMake bug. It is a missing package.

- **FEDORA SUPPLIES WHAT CATALYST COULD NOT.** Cabinet hand-cross-compiled ten
  dependencies with pinned tarballs and SHA sums; Fedora 44 met every version
  constraint PCSX2 states, with one exception (`libbacktrace`, which is an
  option). **Check the distribution before believing a port is hard** — the
  difficulty recorded in a reference implementation is usually the reference
  platform's, not the problem's.

- **PCSX2 REFUSES TO START WITHOUT ITS `bin/resources` FOLDER** — game database,
  fonts, GS shaders. It does not degrade, it says "Resources directory is
  missing" and stops. The same shape as PPSSPP's 13 MB of system files.

- **UPSTREAM'S SECOND FRONTEND IS A LINK TEST, NOT A SHORTCUT TO A RUNNING
  GAME.** `pcsx2-gsrunner` looks like a headless PCSX2 and is not one: it
  replays GS dumps and refuses anything else at
  `VMManager::IsGSDumpFileName`. It proved the library links and it is the best
  `Host` reference there is; it will not boot a disc. **Check what upstream's
  harness is FOR before planning a measurement around it.**

- **A TOOL THAT PRINTS FORTY LINES AND THEN EXITS 1 HAS NOT NECESSARILY GOT
  FAR.** gsrunner's `LoadStartupSettings()` resets the console log level from
  empty settings at the end of config init, so every `Console.Error` after that
  point reaches nobody — including the one naming the actual problem. The
  directory listing that precedes it is the last thing you see and it looks
  like progress. **When a program goes quiet at exactly the same place every
  time, suspect the logger before the logic.**

- **A SEPARATE BUILDER CONTAINER WAS THE RIGHT CALL.** PCSX2 needs about thirty
  packages the frontend does not. Putting them in `frontend/Containerfile` would
  have slowed every one of the twenty-one core builds to serve one thing.

## PS3 (parked)

- **RPCS3 will not install a PKG or firmware unless you say `--headless`**, and
  it **exits 134 after a successful install** — SIGABRT in a static destructor,
  *after* logging `Successfully installed`. **Read the log line, not the exit
  code.**

- **An interrupted PS3 install leaves the partial tree behind**, and nothing
  cleans it up. Delete the title directory before retrying.

- **`timeout` does not kill RPCS3 under flatpak.** It signals the `flatpak run`
  wrapper. Follow it with `pkill -x rpcs3` — and mind that a `pkill` aimed at a
  stuck process will also kill an install you started in the same breath.

- **Flathub stalls from this network, silently.** A retry loop fixes it, because
  ostree resumes:
  `for i in $(seq 1 30); do timeout 240 flatpak install -y --user ... && break; done`.
