#!/usr/bin/env bash
#
# Builds upstream PCSX2 as a LIBRARY for Linux x86-64, at an exact revision,
# and measures what a CabinetOS host layer would still owe it.
#
# WHY THIS IS NOT cores/build-core.sh. That script builds libretro cores: one
# upstream, one `make`, one .so that already implements a frontend contract.
# PCSX2 is not a libretro core and has no retro_run. It is a whole emulator
# that expects a frontend to exist around it, and upstream ships exactly two —
# the Qt desktop app and the headless GS runner. CabinetOS has to be the third.
# Until that third one is written there is no .so to ship, so this script's
# product is a library plus an honest measurement, not a core.
#
# THE QUESTION IT WAS WRITTEN TO ANSWER, AND THE ANSWER:
#
#   Does upstream PCSX2's CMake produce a linkable library on Linux, or must
#   the frontend be carved out the way Cabinet had to on the Mac?
#
#   IT PRODUCES ONE, AND NOTHING HAS TO BE CARVED OUT. `add_library(PCSX2)` is
#   upstream's own target in pcsx2/CMakeLists.txt; the application is a
#   SEPARATE target, pcsx2-qt, behind `if(ENABLE_QT_UI)` in the top-level
#   CMakeLists. Turning that option off is supported upstream and leaves the
#   emulator as a static library. ZERO patches. Compare tools/patch-pcsx2-mac.py
#   in Cabinet, which is 546 lines of them.
#
# Usage:
#   cores/build-pcsx2.sh              build the library and run the probe
#   cores/build-pcsx2.sh --probe-only re-run the probe against an existing build
#
# Run it on a machine with the container image and the disk: the A9, the test
# VM, or a CI runner. It needs about 1.5 GB for the checkout and the build.

set -euo pipefail

# UPSTREAM PCSX2/pcsx2, AND NOT THE isztldav FORK CABINET PINS.
#
# That fork exists to add an ARM64 recompiler for Apple Silicon, where upstream
# stubs one out. This is x86-64, where upstream's recompiler is the original
# rather than a translation of it, and the fork is 291 commits behind master.
# Taking it here would be shipping a staler, Apple-specific PCSX2 on the one
# platform PCSX2 supports natively. docs/PROJECT.md, open question 12b.
#
# AND NOT libretro/pcsx2 EITHER, WHICH DOES NOT EXIST. `git ls-remote` on it
# says "Repository not found". That is what sent PlayStation 2 down this route
# rather than the libretro one; see open question 12b again.
REPO=https://github.com/PCSX2/pcsx2.git

# v2.8.2, the current stable release, 2026-09-04. The version to pin against is
# a decision rather than a default: upstream's HEAD is a v2.9.x development
# series, and a console should follow releases.
COMMIT=fd9d310ccbb6b8b62c976da8886a3c8fd3a10ff3
TAG=v2.8.2

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_ROOT="${CABINETOS_CORE_SRC:-$ROOT/.core-src}"
SRC="$SRC_ROOT/pcsx2-upstream"
BUILD=build-cabinetos
BUILDER="${CABINETOS_PCSX2_BUILDER:-cabinetos-pcsx2-builder}"
OUT="${CABINETOS_PCSX2_OUT:-$ROOT/cores/build/pcsx2}"

PROBE_ONLY=0
[ "${1:-}" = "--probe-only" ] && PROBE_ONLY=1

# --- the container ---------------------------------------------------------
if ! podman image exists "$BUILDER"; then
    echo "building $BUILDER"
    podman build -t "$BUILDER" -f "$ROOT/cores/pcsx2-builder/Containerfile" \
        "$ROOT/cores/pcsx2-builder"
fi

# --- the source ------------------------------------------------------------
if [ "$PROBE_ONLY" -eq 0 ]; then
    mkdir -p "$SRC_ROOT"
    if [ ! -d "$SRC/.git" ]; then
        # --filter=blob:none for the same reason build-core.sh gives, and there
        # are NO submodules to worry about here: PCSX2's .gitmodules is an empty
        # file and all of 3rdparty/ is vendored in the tree. 151 MB checked out,
        # 44 MB of which is 3rdparty.
        git clone --filter=blob:none "$REPO" "$SRC"
    fi
    git -C "$SRC" fetch --quiet origin "$COMMIT" 2>/dev/null || git -C "$SRC" fetch --quiet --all --tags
    git -C "$SRC" checkout --quiet --force "$COMMIT"

    # Asserted, never trusted. Same rule as every core in build-core.sh.
    HEAD=$(git -C "$SRC" rev-parse HEAD)
    if [ "$HEAD" != "$COMMIT" ]; then
        echo "pcsx2 is at $HEAD, expected the pinned $COMMIT" >&2
        exit 1
    fi
    echo "pcsx2 @ $COMMIT ($TAG)"

    # NO PATCH STEP, AND ITS ABSENCE IS THE RESULT. If a patch ever becomes
    # necessary it goes here, asserts its own anchor the way build-core.sh's do,
    # and the comment above about zero patches has to be corrected rather than
    # left to rot.
fi

# --- configure and build ---------------------------------------------------
# Every flag here is a decision, so each one says why.
#
#   ENABLE_QT_UI=OFF    the frontend is CabinetOS's. This is the flag that makes
#                       the whole route work: it drops add_subdirectory(pcsx2-qt)
#                       and leaves the PCSX2 library standing on its own.
#   ENABLE_TESTS=OFF    googletest builds a host binary, pointless here.
#   ENABLE_GSRUNNER=OFF not built by default — but it is still CONFIGURED, so
#                       the probe below can build it on demand as a link test.
#   USE_VULKAN=ON       the point. PCSX2 supports Vulkan natively and the A9 has
#                       it, so the Metal wall Cabinet had to climb is not here.
#                       docs/PROJECT.md, open question 20.
#   USE_OPENGL=ON       kept as the fallback for a machine with no Vulkan, which
#                       is the test VM, and which is exactly why the frontend's
#                       own host discovers rather than assumes.
#   X11_API=ON          the console runs on X11 under gamescope. There is no EGL
#                       display in that process at all; see NEXT-SESSION.md.
#   WAYLAND_API=OFF     would pull in extra-cmake-modules and Wayland-Egl for a
#                       windowing path this frontend will never use.
#   USE_BACKTRACE=OFF   libbacktrace-devel is the one dependency Fedora 44 has
#                       no package for. It is a crash-reporter nicety.
#   POSITION_INDEPENDENT_CODE=ON
#                       upstream's default, and load-bearing here rather than
#                       incidental: open question 12's correction says each
#                       emulator becomes its own .so, and a non-PIC static
#                       library cannot be linked into one at all.
CMAKE_ARGS=(
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    -DENABLE_QT_UI=OFF
    -DENABLE_TESTS=OFF
    -DENABLE_GSRUNNER=OFF
    -DUSE_VULKAN=ON
    -DUSE_OPENGL=ON
    -DX11_API=ON
    -DWAYLAND_API=OFF
    -DUSE_BACKTRACE=OFF
    -DPOSITION_INDEPENDENT_CODE=ON
)

run_in_builder() {
    # safe.directory through the environment rather than a gitconfig written
    # into the mounted tree — build-core.sh's reasoning, and it matters here
    # too: PCSX2's get_git_version_info() shells out to git and compiles the
    # answer in. A refused checkout gives a library that does not know which
    # revision it is.
    podman run --rm -v "$SRC":/src:Z -w /src \
        -e GIT_CONFIG_COUNT=1 \
        -e GIT_CONFIG_KEY_0=safe.directory \
        -e GIT_CONFIG_VALUE_0=/src \
        "$BUILDER" bash -c "$1"
}

if [ "$PROBE_ONLY" -eq 0 ]; then
    run_in_builder "cmake -B $BUILD $(printf '%q ' "${CMAKE_ARGS[@]}") ."
    run_in_builder "cmake --build $BUILD --target PCSX2 --parallel \$(nproc)"

    mkdir -p "$OUT"
    cp "$SRC/$BUILD/pcsx2/libpcsx2.a" "$OUT/"
    cp "$SRC/$BUILD/common/libcommon.a" "$OUT/"
    find "$SRC/$BUILD/3rdparty" -name '*.a' -exec cp {} "$OUT/" \;
    echo "wrote $OUT ($(du -sh "$OUT" | cut -f1), $(find "$OUT" -name '*.a' | wc -l) archives)"
fi

# --- the probe -------------------------------------------------------------
# THREE THINGS ARE ASSERTED, AND EACH ONE FAILED A DIFFERENT WAY WHEN IT WAS
# FIRST RUN BY HAND, WHICH IS WHY ALL THREE ARE HERE RATHER THAN JUST THE FIRST.
#
#   1. The library exists and carries the renderers and recompilers this
#      console needs. A library that built but contained a software renderer
#      and no JIT would be a green build and a useless one.
#   2. It LINKS. A static library resolves nothing — Cabinet's own comment on
#      CabinetPS2Smoke.cpp says so — and the cheapest honest link test is
#      upstream's own non-Qt frontend, pcsx2-gsrunner, which is 1332 lines in
#      one file and already implements the whole Host contract.
#   3. What a host layer still owes it, counted rather than estimated. A shared
#      object will link happily with undefined symbols and then fail at dlopen
#      with the FIRST one only, which tells you nothing about the size of the
#      job. `-z defs` makes the linker refuse instead and name them all.
cat > /tmp/cabinetos-pcsx2-probe.sh <<'PROBE'
set -u
cd "/src/$1"
L=pcsx2/libpcsx2.a
fail() { echo "PROBE FAILED: $*" >&2; exit 1; }

echo "--- 1. what is in the library ---"
[ -f "$L" ] || fail "no libpcsx2.a"
vk=$(nm -C "$L" 2>/dev/null | grep -c "GSDeviceVK::")
gl=$(nm -C "$L" 2>/dev/null | grep -c "GSDeviceOGL")
mtl=$(nm -C "$L" 2>/dev/null | grep -c "GSDeviceMTL")
rec=$(nm -C "$L" 2>/dev/null | grep -c "microVU")
printf "  %-26s %s\n" "Vulkan renderer" "$vk symbols"
printf "  %-26s %s\n" "OpenGL renderer" "$gl symbols"
printf "  %-26s %s\n" "Metal renderer" "$mtl symbols (must be 0 on Linux)"
printf "  %-26s %s\n" "microVU recompiler" "$rec symbols"
[ "$vk"  -gt 0 ] || fail "no Vulkan renderer — USE_VULKAN did not take"
[ "$rec" -gt 0 ] || fail "no recompiler — the whole point of x86-64"
[ "$mtl" -eq 0 ] || fail "Metal renderer on Linux, which cannot be right"

echo "--- 2. does it link? upstream's own non-Qt frontend ---"
cmake --build . --target pcsx2-gsrunner --parallel "$(nproc)" >/dev/null 2>&1 \
    || fail "pcsx2-gsrunner did not link against the library"
[ -x bin/pcsx2-gsrunner ] || fail "no pcsx2-gsrunner binary"
echo "  linked: bin/pcsx2-gsrunner ($(du -h bin/pcsx2-gsrunner | cut -f1))"
# And it must RUN, not merely link. Its own resources folder is not optional:
# PCSX2 reads its game database, fonts and shaders from it and refuses to start.
cp -r ../bin/resources bin/resources 2>/dev/null || true
bin/pcsx2-gsrunner -help >/tmp/run.txt 2>&1 || true
grep -q "MemoryCards Directory" /tmp/run.txt \
    || fail "the binary linked but did not initialise; see /tmp/run.txt"
echo "  ran: reached full config init"

echo "--- 3. what a CabinetOS host layer still owes it ---"
SYS="-lpng -ljpeg -lz -lzstd -llz4 -lwebp -lsharpyuv -lfreetype -lharfbuzz"
SYS="$SYS -lplutovg -lplutosvg -lryml -lcurl -lpcap -lfontconfig -ludev"
SYS="$SYS -lX11 -lXrandr -lXi -lXext -ldbus-1 -lSDL3 -lshaderc_shared -ldl -lpthread"
clang++ -shared -Wl,-z,defs -o /tmp/strict.so \
    -Wl,--whole-archive pcsx2/libpcsx2.a common/libcommon.a -Wl,--no-whole-archive \
    $(find 3rdparty -name '*.a') $SYS >/tmp/link.txt 2>&1 || true
grep -oE "undefined reference to .[^']+" /tmp/link.txt \
    | sed "s/undefined reference to .//" | c++filt \
    | sed 's/\[abi:cxx11\]//' | sed 's/(.*//' | sort -u > /tmp/owed.txt
n=$(wc -l < /tmp/owed.txt)
h=$(grep -c '^Host::' /tmp/owed.txt || true)
echo "  $n symbols, $h of them in the Host:: namespace"
[ "$n" -gt 0 ] || fail "zero owed symbols, which means the probe did not run"
cat /tmp/owed.txt
PROBE
podman run --rm -v "$SRC":/src:Z -v /tmp/cabinetos-pcsx2-probe.sh:/probe.sh:Z \
    -w /src "$BUILDER" bash /probe.sh "$BUILD"
