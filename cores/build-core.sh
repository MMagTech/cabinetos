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
FOUND=$(find "$SRC" -name "$SO" -print -quit)
[ -n "$FOUND" ] || { echo "no $SO produced" >&2; exit 1; }
cp "$FOUND" "$OUT/$SO"
echo "wrote $OUT/$SO ($(du -h "$OUT/$SO" | cut -f1))"

# Asserting the CHECKOUT is at the pinned commit proves what went in. This
# reads the revision back out of the finished artifact and proves what came
# out, which is the assertion open question 13 actually asks for. It runs
# inside the builder because the binary is linked against Fedora 44's glibc and
# the host running this script need not have it.
echo "verifying $SO"
podman run --rm -v "$ROOT":/repo:Z -v "$OUT":/out:Z -w /repo "$BUILDER" \
    sh -c 'gcc -O2 -Wall -Wextra -o /tmp/core-info tools/core-info.c -ldl \
           && exec /tmp/core-info "/out/$1" "$2"' _ "$SO" "$COMMIT"

echo "sha256      $(sha256sum "$OUT/$SO" | cut -d' ' -f1)"
