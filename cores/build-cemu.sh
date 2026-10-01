#!/usr/bin/env bash
#
# Builds Cemu, the Wii U emulator, for Linux x86-64 at an exact commit: the
# program and the two folders it reads, ready to go into the image at
# /usr/lib/cabinetos/cemu.
#
# WHY FROM SOURCE AND WHY MAIN. Decided with MMagTech 2026-10-01, issue #174,
# docs/PROJECT.md open question 36: Cemu's last release, v2.6, is from
# 2025-02-06, and Cemu issue #1176 (Vulkan texture and shadow corruption on the
# Radeon 780M, 880M and 890M, the chips in most current handheld and mini PCs)
# is reported fixed only on main. Main publishes no release; its CI builds
# expire after 90 days and cannot be a pin. Batocera builds main at a pinned
# commit too. Never patched.
#
# HOW: CEMU'S OWN RECIPE, at the pin. Upstream's Linux build (build.yml) is
# vcpkg plus clang; vcpkg is a submodule and vcpkg.json names its baseline, so
# every library Cemu builds for itself is pinned by this one commit. Fedora's
# own libraries are too old for it (cores/cemu-builder/Containerfile).
#
# WHAT IT ASSERTS: the source is at the pinned commit; the program links with
# every library it still asks the system for present in Fedora 44, which is
# what the image is built from. WHAT IT CANNOT: that a game runs. That was seen
# on the A9 (2026-10-01, Hyrule Warriors, upstream's build of this commit), and
# is seen there again for every move of the pin.
#
# Usage: cores/build-cemu.sh
# Output: cores/build/cemu/{bin/Cemu, share/Cemu/{gameProfiles,resources}, VERSION}

set -euo pipefail

REPO=https://github.com/cemu-project/Cemu.git
# main, 2026-09-30: the commit MMagTech judged on the A9 on 2026-10-01. To move
# it: pick a commit, build it, play a game on the A9, and say why in the commit.
COMMIT=4e3c824faa00f6b85782db019f20f29f063f3a2a

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_ROOT="${CABINETOS_CORE_SRC:-$ROOT/.core-src}"
SRC="$SRC_ROOT/cemu"
BUILD=build-cabinetos
BUILDER="${CABINETOS_CEMU_BUILDER:-cabinetos-cemu-builder}"
OUT="${CABINETOS_CEMU_OUT:-$ROOT/cores/build/cemu}"
# vcpkg's own cache of what it built, so a second run of the same pin rebuilds
# nothing of it. Outside the checkout so a fresh clone keeps it.
VCPKG_CACHE="$SRC_ROOT/cemu-vcpkg-cache"

# --- the container ---------------------------------------------------------
if ! podman image exists "$BUILDER"; then
    echo "building $BUILDER"
    podman build -t "$BUILDER" -f "$ROOT/cores/cemu-builder/Containerfile" \
        "$ROOT/cores/cemu-builder"
fi

# --- the source ------------------------------------------------------------
mkdir -p "$SRC_ROOT" "$VCPKG_CACHE"
if [ ! -d "$SRC/.git" ]; then
    git clone --filter=blob:none "$REPO" "$SRC"
fi
git -C "$SRC" fetch --quiet origin "$COMMIT" 2>/dev/null || git -C "$SRC" fetch --quiet --all
git -C "$SRC" checkout --quiet --force "$COMMIT"
# Asserted, never trusted. Same rule as every core in build-core.sh.
HEAD=$(git -C "$SRC" rev-parse HEAD)
if [ "$HEAD" != "$COMMIT" ]; then
    echo "cemu is at $HEAD, expected the pinned $COMMIT" >&2
    exit 1
fi
# The submodules at the commits this one names: vcpkg (and with it every
# library's version), cubeb, ZArchive, imgui, the Vulkan headers.
git -C "$SRC" submodule sync --quiet --recursive
git -C "$SRC" submodule update --init --recursive --force --quiet
echo "cemu @ $COMMIT"

run_in_builder() {
    podman run --rm -v "$SRC":/src:Z -v "$VCPKG_CACHE":/vcpkg-cache:Z -w /src \
        -e GIT_CONFIG_COUNT=1 \
        -e GIT_CONFIG_KEY_0=safe.directory \
        -e GIT_CONFIG_VALUE_0='*' \
        -e VCPKG_DEFAULT_BINARY_CACHE=/vcpkg-cache \
        -e VCPKG_DISABLE_METRICS=1 \
        "$BUILDER" bash -c "$1"
}

# Upstream's options, but for three that a console has no use for: Discord's
# presence, Feral's GameMode, and portable mode (a `portable` folder beside the
# program would move every setting there, and /usr is read-only anyway).
CMAKE_ARGS=(
    -S . -B "$BUILD" -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    -DENABLE_DISCORD_RPC=OFF
    -DENABLE_FERAL_GAMEMODE=OFF
    -DALLOW_PORTABLE=OFF
)

run_in_builder "./dependencies/vcpkg/bootstrap-vcpkg.sh -disableMetrics"
run_in_builder "cmake $(printf '%q ' "${CMAKE_ARGS[@]}")"
run_in_builder "cmake --build $BUILD --parallel \$(nproc)"

# --- what goes into the image ----------------------------------------------
# Cemu writes its program to bin/Cemu_release, beside the two folders it reads
# (gameProfiles: Cemu's own per-game settings; resources: fonts, translations,
# shaders). Laid out as an install prefix: wxWidgets finds Cemu's data folder at
# <prefix>/share/Cemu from where the program is (wxStandardPaths::GetDataDir),
# as upstream's AppImage lays it out.
rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/share/Cemu"
install -m 0755 "$SRC/bin/Cemu_release" "$OUT/bin/Cemu"
cp -R "$SRC/bin/gameProfiles" "$SRC/bin/resources" "$OUT/share/Cemu/"
printf '%s\n' "$COMMIT" > "$OUT/VERSION"

# EVERY LIBRARY IT STILL ASKS THE SYSTEM FOR RESOLVES in Fedora 44, the image's
# base. The image's own install step checks the same against the image itself.
missing=$(podman run --rm -v "$OUT":/out:Z "$BUILDER" bash -c "ldd /out/bin/Cemu | grep 'not found'" || true)
if [ -n "$missing" ]; then
    echo "Cemu needs libraries Fedora 44 does not have:" >&2
    echo "$missing" >&2
    exit 1
fi
[ -f "$OUT/share/Cemu/gameProfiles/default/000500001017d800.ini" ] || {
    echo "no Cemu game profiles in $OUT/share/Cemu" >&2
    exit 1
}
echo "wrote $OUT ($(du -sh "$OUT" | cut -f1); program $(du -h "$OUT/bin/Cemu" | cut -f1))"
