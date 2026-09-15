#!/usr/bin/env bash
#
# Builds one libretro core for Linux x86-64, at an exact revision.
#
# This is the Linux column of Cabinet's core manifest. The discipline it
# enforces is the whole point, and it is what Cabinet's own build scripts did
# not do (see docs/PROJECT.md, open question 13):
#
#   * the commit is checked out and then ASSERTED, never cloned as HEAD;
#   * the make arguments are recorded here, not left to the platform default,
#     because a plain Linux build silently turns ON recompilers the Apple build
#     has OFF, in five cores;
#   * the output is a plain .so. No symbol prefixing, no relocatable merge, no
#     exported-symbol lists. RTLD_LOCAL gives namespace isolation for free, so
#     the entire apparatus Cabinet needs for Apple simply does not exist here.
#
# Split by necessity: git lives on the host, the toolchain lives in the
# frontend's builder container, and neither has the other. So this does the
# version control itself and hands the compile to podman. Run it on the machine
# with the container image, which today is the test VM and in CI is the runner.
#
# Usage: cores/build-core.sh <core>

set -euo pipefail

CORE="${1:-}"

# Whether the finished .so can be asked which revision it is. Almost every core
# compiles `git rev-parse --short HEAD` into the string it reports, and where it
# does, that is asserted. A few cannot, through upstream bugs rather than
# anything we do, and those are marked in their case arm with the reason.
#
# NOT patched into working. Adding the missing flag ourselves would change the
# binary against Cabinet's, which builds the same upstream and has the same
# blind spot, and diverging from Cabinet to satisfy our own test is exactly
# backwards. The checkout is still asserted at the pinned commit either way;
# what is lost is only the ability to read it back out.
VERIFY_REVISION=1
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_ROOT="${CABINETOS_CORE_SRC:-$ROOT/.core-src}"
OUT="${CABINETOS_CORE_OUT:-$ROOT/cores/build}"

# Per core: upstream, pinned commit, make directory, makefile, extra arguments.
#
# COMMIT values come from Cabinet's core-manifest.json `pinned_commit`, which is
# the revision every platform must build from going forward. They are not to be
# "updated" here; they move in Cabinet, and this follows.
case "$CORE" in
gambatte)
    REPO=https://github.com/libretro/gambatte-libretro.git
    COMMIT=d9d6cd06382d1ced30de34d56d3609452323dab1
    MAKEDIR=.
    MAKEFILE=Makefile.libretro
    # No recompiler exists in this core at all, on any platform, so there is
    # nothing here to keep in step with Cabinet. That is exactly why it is the
    # first core: it proves the pipeline without also testing the parity
    # question.
    MAKEARGS=()
    SO=gambatte_libretro.so
    ;;
genesis_plus_gx)
    REPO=https://github.com/libretro/Genesis-Plus-GX.git
    COMMIT=a7985a9c4278ac352f8ca7bb4d3cc6b36e9e3e7d
    MAKEDIR=.
    MAKEFILE=Makefile.libretro
    # HAVE_CDROM is the lever here, and it is not a recompiler — see
    # docs/PROJECT.md, open question 13. Makefile.libretro line 7 defaults it to
    # 0; the unix branch then turns it on from a `uname -s` test, and no Apple
    # branch does anything equivalent. The manifest agrees: build_args is null
    # for ios, tvos and mac, so nothing overrides the default there.
    #
    # It is the libretro PHYSICAL CD-ROM DRIVE interface, so it lands on Sega
    # CD, the one system of this core's four that saves by a different
    # mechanism. Whether it perturbs the state format is not established, and
    # the standing rule is to match Cabinet until it is. Free to obey: the
    # console has no optical drive and never will, so this turns off a feature
    # the hardware cannot use.
    MAKEARGS=(HAVE_CDROM=0)
    SO=genesis_plus_gx_libretro.so
    ;;
fceumm)
    REPO=https://github.com/libretro/libretro-fceumm.git
    COMMIT=236ccdfc911e84c60fea6b9d0699c2d440a8de14
    MAKEDIR=.
    MAKEFILE=Makefile
    # NES.
    MAKEARGS=()
    ;;
snes9x)
    REPO=https://github.com/libretro/snes9x.git
    COMMIT=890b5d445538fe790aa3add3d5702c80f551e0ae
    MAKEDIR=libretro
    MAKEFILE=Makefile
    # SNES. Its makefile lives in libretro/, not at the root.
    MAKEARGS=()
    ;;
beetle_pce_fast)
    REPO=https://github.com/libretro/beetle-pce-fast-libretro.git
    COMMIT=2f623abd033257b969370b73d9da982dcb0c3fdd
    # Cannot report its revision, and it is upstream's bug rather than ours:
    # libretro.c is a C file that uses GIT_VERSION, while the Makefile adds
    # -DGIT_VERSION to CXXFLAGS only, so the define never reaches it and the
    # `#ifndef GIT_VERSION / #define GIT_VERSION ""` fallback wins. Cabinet
    # builds the same upstream and has the same blind spot.
    VERIFY_REVISION=0
    MAKEDIR=.
    MAKEFILE=Makefile
    # TurboGrafx-16 and TurboGrafx-CD, both.
    MAKEARGS=()
    ;;
beetle_ngp)
    REPO=https://github.com/libretro/beetle-ngp-libretro.git
    COMMIT=a50d5ac288a81f2104ddf43195a4efdd15c72227
    MAKEDIR=.
    MAKEFILE=Makefile
    # Neo Geo Pocket Color.
    MAKEARGS=()
    ;;
beetle_vb)
    REPO=https://github.com/libretro/beetle-vb-libretro.git
    COMMIT=83ed42608601fb7b01d41e4f8fb2007a37b8c84e
    MAKEDIR=.
    MAKEFILE=Makefile
    # Virtual Boy.
    MAKEARGS=()
    ;;
beetle_saturn)
    REPO=https://github.com/libretro/beetle-saturn-libretro.git
    COMMIT=ed549bdac0e1a830bb794fa720e45c225a45355c
    # Same upstream bug as beetle_pce_fast, and the same family: libretro.c is
    # a C file using GIT_VERSION while the Makefile puts -DGIT_VERSION in
    # CXXFLAGS. Verified, not assumed from the symptom.
    VERIFY_REVISION=0
    MAKEDIR=.
    MAKEFILE=Makefile
    # Saturn. Needs a region BIOS, which the launcher fetches.
    MAKEARGS=()
    ;;
stella2014)
    REPO=https://github.com/libretro/stella2014-libretro.git
    COMMIT=4a7da82595d27b8df7af1ecb467a64b642a41bc9
    MAKEDIR=.
    MAKEFILE=Makefile
    # Atari 2600.
    MAKEARGS=()
    ;;
prosystem)
    REPO=https://github.com/libretro/prosystem-libretro.git
    COMMIT=8a88014287c7a01cd568067e5a557d0a2b2a051f
    MAKEDIR=.
    MAKEFILE=Makefile
    # Atari 7800.
    MAKEARGS=()
    ;;
opera)
    REPO=https://github.com/libretro/opera-libretro.git
    COMMIT=a501a278d057b952d1ad6165549c59ab178ca497
    MAKEDIR=.
    MAKEFILE=Makefile
    # 3DO. Needs a BIOS.
    MAKEARGS=()
    ;;
vecx)
    REPO=https://github.com/libretro/libretro-vecx.git
    COMMIT=8f671cc9d737f2890c3ce19e177e2984dcae121f
    MAKEDIR=.
    MAKEFILE=Makefile
    # Vectrex. HAS_GPU=0 is the lever, and it is NOT a recompiler: the
    # Makefile defaults HAS_GPU=1 off macOS, which builds a GLES2 path this
    # frontend cannot drive. Cabinet passes it on both Apple platforms, so this
    # matches rather than diverges.
    MAKEARGS=("HAS_GPU=0")
    ;;
mame2003_plus)
    REPO=https://github.com/libretro/mame2003-plus-libretro.git
    COMMIT=21256d24120b04916c5197d95b757635ca880fd9
    MAKEDIR=.
    MAKEFILE=Makefile
    # Arcade, the MAME 2003-Plus half of it.
    MAKEARGS=()
    ;;
fbneo_libretro)
    REPO=https://github.com/libretro/FBNeo.git
    COMMIT=2444fbe3ddab193b6c0e6f2d39b6dde041fbee4c
    MAKEDIR=src/burner/libretro
    MAKEFILE=Makefile
    # Arcade, the FinalBurn Neo half. Its makefile is four directories down.
    MAKEARGS=()
    ;;
*)
    echo "unknown core: $CORE" >&2
    exit 1
    ;;
esac

SRC="$SRC_ROOT/$CORE"

if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$SRC_ROOT"
    # Not --depth 1: a shallow clone of a branch cannot check out an arbitrary
    # commit, and an arbitrary commit is the entire requirement.
    git clone "$REPO" "$SRC"
fi

git -C "$SRC" fetch --quiet origin "$COMMIT" 2>/dev/null || git -C "$SRC" fetch --quiet --all
git -C "$SRC" checkout --quiet --force "$COMMIT"
git -C "$SRC" submodule update --init --recursive --quiet

HEAD=$(git -C "$SRC" rev-parse HEAD)
if [ "$HEAD" != "$COMMIT" ]; then
    echo "$CORE is at $HEAD, expected the pinned $COMMIT" >&2
    exit 1
fi
echo "$CORE @ $COMMIT"

# platform=unix is the core's own Linux case, and on every Makefile-based core
# in the set it is also the default when uname says Linux. It is the
# best-tested path these cores have; the ios-arm64 case Cabinet uses is the
# unusual one.
BUILDER="${CABINETOS_BUILDER:-cabinetos-builder}"

# safe.directory is not paranoia about this checkout, it is about the core's
# own Makefile. Every Makefile-based core in the set does
#   GIT_VERSION := $(shell git rev-parse --short HEAD || echo unknown)
# and compiles the answer into the version string it reports. If git refuses
# the bind-mounted tree as dubiously owned it fails QUIETLY into "unknown", the
# `||` swallows it, and the core ships not knowing what revision it is. Passing
# it through the environment avoids writing a gitconfig into the mounted tree.
podman run --rm -v "$SRC":/src:Z -w /src \
    -e GIT_CONFIG_COUNT=1 \
    -e GIT_CONFIG_KEY_0=safe.directory \
    -e GIT_CONFIG_VALUE_0=/src \
    "$BUILDER" \
    make -C "$MAKEDIR" -f "$MAKEFILE" platform=unix "${MAKEARGS[@]}" -j"$(nproc)"

mkdir -p "$OUT"

# DISCOVER the .so rather than being told its name, and file it under the
# MANIFEST core name.
#
# Upstream output names do not match manifest names and there is no rule to it:
# beetle_ngp builds mednafen_ngp_libretro.so, beetle_pce_fast builds
# mednafen_pce_fast_libretro.so. Hand-maintaining that list for twenty-one cores
# is a table that goes stale, and the frontend would need a second copy of it to
# find anything.
#
# So the artifact is named after the core as the manifest knows it — the same
# identity the pins, the emulator tags and catalog.cpp already use — and this
# script finds whatever was actually produced. One rule, no mapping.
mapfile -t BUILT < <(find "$SRC" -name '*_libretro.so' -newer "$SRC/.git" 2>/dev/null)
if [ "${#BUILT[@]}" -eq 0 ]; then
    mapfile -t BUILT < <(find "$SRC" -name '*_libretro.so')
fi
[ "${#BUILT[@]}" -ne 0 ] || { echo "no *_libretro.so was produced" >&2; exit 1; }
if [ "${#BUILT[@]}" -gt 1 ]; then
    echo "ambiguous: the build produced ${#BUILT[@]} cores" >&2
    printf '  %s\n' "${BUILT[@]}" >&2
    exit 1
fi
# A manifest name that already ends in _libretro does not get a second one:
# fbneo_libretro would otherwise be filed as fbneo_libretro_libretro.so.
# catalog.cpp applies the same rule when it looks for the file — the two must
# agree, and this comment is on both.
SO="${CORE%_libretro}_libretro.so"
UPSTREAM=$(basename "${BUILT[0]}")
[ "$UPSTREAM" = "$SO" ] || echo "built $UPSTREAM, filing it as $SO"
cp "${BUILT[0]}" "$OUT/$SO"
echo "wrote $OUT/$SO ($(du -h "$OUT/$SO" | cut -f1))"

# Asserting the CHECKOUT is at the pinned commit proves what went in. This
# reads the revision back out of the finished artifact and proves what came
# out, which is the assertion open question 13 actually asks for. It runs
# inside the builder because the binary is linked against Fedora 44's glibc and
# the host running this script need not have it.
echo "verifying $SO"
if [ "$VERIFY_REVISION" -eq 1 ]; then
    EXPECT="$COMMIT"
else
    EXPECT=""
    echo "note: this core cannot report its revision — see its case arm"
fi
podman run --rm -v "$ROOT":/repo:Z -v "$OUT":/out:Z -w /repo "$BUILDER" \
    sh -c 'gcc -O2 -Wall -Wextra -o /tmp/core-info tools/core-info.c -ldl \
           && if [ -n "$2" ]; then exec /tmp/core-info "/out/$1" "$2"; \
              else exec /tmp/core-info "/out/$1"; fi' _ "$SO" "$EXPECT"

echo "sha256      $(sha256sum "$OUT/$SO" | cut -d' ' -f1)"
