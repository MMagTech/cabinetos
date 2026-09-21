# Embedding upstream PCSX2: the build, and what the host layer owes it

Measured on the A9 Max, 2026-09-21, against `PCSX2/pcsx2` at **v2.8.2**
(`fd9d310ccbb6b8b62c976da8886a3c8fd3a10ff3`). Everything here is reproducible
with one command:

```bash
cores/build-pcsx2.sh
```

43 seconds from a clean clone on the A9, including the build and the probe.

---

## The question this was written to answer

> Does upstream PCSX2's CMake produce a linkable library on Linux, or must the
> frontend be carved out the way Cabinet had to on the Mac?

**It produces one, and nothing has to be carved out. There are no patches at
all.**

This corrects a belief that has been carried in two handover documents. The
sentence *"PCSX2's CMake builds an APPLICATION, not a library — Cabinet had to
carve the frontend out"* is not true of upstream and was not what Cabinet did.

- `pcsx2/CMakeLists.txt` line 8 is **`add_library(PCSX2)`**. The emulator is a
  library target upstream, and always has been.
- The application is a **separate target**, `pcsx2-qt`, reached through
  `if(ENABLE_QT_UI)` in the top-level `CMakeLists.txt`. Turning that option off
  is supported upstream and simply does not add the subdirectory.
- What Cabinet's `tools/patch-pcsx2-mac.py` actually does — 546 lines of it — is
  replace things **Mac Catalyst cannot compile**: SDL3, cubeb, `CocoaTools.mm`,
  an `NSView`/`CAMetalLayer` seam, `pthread_jit_write_protect_np` behind
  `dlsym`, and FFmpeg found through Homebrew. Not one of those is a Linux
  problem. It never changed a target type.

## What was built, and what is in it

| | |
|---|---|
| `libpcsx2.a` | **35 MB**, plus `libcommon.a` and 16 vendored archives — 18 in all, 49 MB |
| Patches applied | **none** |
| Wall clock | **25 s** to build the library on 20 of the A9's 24 cores |

Read out of the finished archive rather than assumed:

| | |
|---|---|
| Vulkan renderer | **220 symbols** — `USE_VULKAN=ON` took |
| OpenGL renderer | 127 symbols — the fallback for a machine with no Vulkan, which is the test VM |
| Metal renderer | **0** — correct on Linux, and the assertion exists because a non-zero here would mean the build had gone somewhere strange |
| microVU recompiler | 318 symbols, with `recRecompile`, `iopRec` and the vtlb dynarec beside it |

**The recompilers matter more than the count suggests.** This is x86-64, so
these are PCSX2's original emitters rather than the machine-translated ARM64
ones Cabinet had to pin a fork for. Open question 12b's argument for taking
upstream over `isztldav/pcsx2` is not merely tidiness — it is that the fork's
whole reason for existing is absent here.

## It links, and it runs

A static library resolves nothing, which is Cabinet's own comment on
`CabinetPS2Smoke.cpp` and the reason that file exists. The cheapest honest link
test on Linux needs no new code at all: **upstream ships a second frontend.**

`pcsx2-gsrunner` is **1332 lines in one file**, has no Qt, implements the whole
`Host` contract, and is maintained in-tree by PCSX2. It links against the
library in **2.2 seconds**, produces a 28 MB binary, and that binary runs:

```
[    0.0006] Processor count: 24 cores, 24 processors, 2 clusters
[    0.0006]   Enabling MTVU.
[    0.0007] MemoryCards Directory: /root/.config/PCSX2/memcards
```

**That is a second reference implementation, and it is better than Cabinet's in
one specific way**: it is Linux-native, it is upstream's, and it therefore
cannot go stale against the version we pin. Cabinet's `CabinetPS2Host.cpp` is
the better guide to *what a console frontend wants*; gsrunner is the better
guide to *what this version of PCSX2 requires*. Read both.

**PCSX2 will not start without its resources folder** — `bin/resources`, holding
the game database, fonts and GS shaders. It does not degrade, it refuses. That
folder has to ship, the same way PPSSPP's 13 MB already do at
`/usr/share/cabinetos/system/`.

## What a CabinetOS host layer still owes it: 57 symbols

A shared object links happily with undefined symbols and then fails at `dlopen`
naming only the **first** one — which says nothing about the size of the job.
`-Wl,-z,defs` makes the linker refuse instead and name them all.

```
57 symbols, 53 of them in the Host:: namespace
```

**Three independent counts agree**, which is the reason to trust the number:
Cabinet answers 54 on the Mac, upstream's gsrunner implements 52 on Linux, and
the linker demands 53 here. The differences are Apple-only entry points and one
or two the linker never reaches in this configuration.

### The 53 in `Host::`

| Group | Functions |
|---|---|
| **The display path — the only hard ones** | `AcquireRenderWindow` `ReleaseRenderWindow` `BeginPresentFrame` `RequestResizeHostDisplay` `IsFullscreen` `SetFullscreen` |
| VM lifecycle | `OnVMStarting` `OnVMStarted` `OnVMPaused` `OnVMResumed` `OnVMDestroyed` `RequestVMShutdown` `OnGameChanged` |
| Save states | `OnSaveStateLoading` `OnSaveStateLoaded` `OnSaveStateSaved` |
| Settings | `LoadSettings` `CheckForSettingsChanges` `CommitBaseSettingChanges` `RequestResetSettings` `SetDefaultUISettings` |
| Threading | `RunOnCPUThread` `PumpMessagesOnCPUThread` |
| Input | `OnInputDeviceConnected` `OnInputDeviceDisconnected` `SetMouseMode` `SetMouseLock` |
| Messages and errors | `ReportInfoAsync` `ReportErrorAsync` `CreateHostProgressCallback` |
| Text and clipboard | `BeginTextInput` `EndTextInput` `CopyTextToClipboard` `GetTextFromClipboard` |
| Localisation | `Internal::GetTranslatedStringImpl` `TranslatePluralToString` `LocaleSensitiveCompare` `LocaleCircleConfirm` |
| Achievements | `OnAchievementsLoginRequested` `OnAchievementsLoginSuccess` `OnAchievementsRefreshed` `OnAchievementsHardcoreModeChanged` |
| Game list | `RefreshGameListAsync` `CancelGameListRefresh` |
| Capture | `OnCaptureStarted` `OnCaptureStopped` |
| Application shell | `RequestExitApplication` `RequestExitBigPicture` `InNoGUIMode` `OpenURL` `ShouldPreferHostFileSelector` `OpenHostFileSelectorAsync` `OnPerformanceMetricsUpdated` |

### The four outside it

```
InputManager::ConvertHostKeyboardCodeToIcon
InputManager::ConvertHostKeyboardCodeToString
InputManager::ConvertHostKeyboardStringToCode
g_host_hotkeys
```

`g_host_hotkeys` is the one that bites first: it is a **variable**, not a
function, and it is what a `dlopen` of the unfinished library fails on before
mentioning any of the other 56.

### Most of these are not work

**The honest shape of the job is small.** The large majority of that table is a
one-line stub — a console has no clipboard, no file selector, no achievements
login, no Big Picture mode to exit to, and no game list of PCSX2's own. Cabinet
answers most of them in one or two lines each, and gsrunner does the same.

**The six in the first row are the job**, and they are the ones the Vulkan host
built last session already has the pieces for. `frontend/src/vkhost.cpp` owns a
device, a queue and a picture that crosses into the GLES texture the UI draws.
Cabinet's `CabinetPS2Host` runs the VM on its own thread and presents into a
`CAMetalLayer`; this is the same shape with Vulkan instead, and PCSX2 supports
Vulkan natively so there is no Metal wall to climb.

## Dependencies: ten hand-built tarballs become one `dnf` line

Cabinet cross-compiled **ten** external dependencies for Catalyst by hand, with
pinned tarballs and SHA sums, in `tools/build-pcsx2-deps-mac.sh`. On Fedora 44
every one of them is a package, and every version constraint PCSX2 states is
already met:

| | needs | Fedora 44 has |
|---|---|---|
| libpng | ≥ 1.6.40 | 1.6.55 |
| zstd | ≥ 1.5.5 | 1.5.7 |
| SDL3 | ≥ 3.2.6 | 3.4.0 |
| freetype | ≥ 2.10 | 2.14.1 |
| plutovg | ≥ 1.1.0 | 1.3.2 |
| plutosvg | ≥ 0.0.7 | 0.0.7 |
| ryml | unversioned | 0.10.0 |
| shaderc | for `USE_VULKAN` | 2026.1 |

**`libbacktrace-devel` is the only gap**, and `USE_BACKTRACE=OFF` disposes of
it — it is a crash-reporter nicety that PCSX2 makes an option for that reason.

**`libXi-devel` is the one that will waste an hour if nobody says it.**
`find_package(X11)` succeeds without it, configure gets all the way to the last
step, and then `common/CMakeLists.txt` fails at GENERATE time on a missing
`X11::Xi` target. It reads like a CMake bug rather than a missing package.

## What the image would have to gain

Not measured against the running image yet, and it is the next cheap thing to
check. `ldd` on the shared object names these beyond what the frontend and the
twenty-one cores already pull in:

```
libshaderc_shared.so.1   libSPIRV-Tools.so   libSPIRV-Tools-opt.so
libplutovg.so.1          libplutosvg.so.0    libryml.so.0.10.0
libpcap.so.1             libharfbuzz.so.0
```

**`ci/base-watch.txt` is where these belong once PS2 ships**, and the reason is
recorded there already: a base bump that drops one of these gives a green build
and a console that cannot start a PlayStation 2 game.

## What this does NOT answer

- **Nothing has been emulated.** A library that links and a binary that
  initialises are not a PS2 game. The gsrunner binary could be pointed at a
  disc, and that is the cheapest next measurement.
- **No host layer exists.** 57 symbols are named; none is written.
- **Nothing is in CI**, deliberately. Adding a PCSX2 build to the image workflow
  before there is anything to ship would add minutes to every build and prove
  nothing that this script does not prove on demand.
- **Save states are still new work**, unchanged by any of this. Cabinet's Mac
  PS2 state is PCSX2's own slot 1 through `VMManager::SaveStateToSlot`, keyed by
  disc serial and CRC — not a buffer, not uploaded, not tagged. Open question
  12b. **Memory cards are the part that travels, and Burnout 3 is the only real
  one that exists.**
