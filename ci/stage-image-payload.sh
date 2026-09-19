#!/usr/bin/env bash
#
# Assemble everything the image has to carry that is not in this repository:
# the frontend binary, the twenty-one libretro cores, and the one core's worth
# of system files that ships with an emulator rather than coming off RomM.
#
# WHY THIS EXISTS AT ALL. Until 2026-09-19 the image contained none of it. The
# Containerfile copied build_files, system_files and two licence files, so a
# freshly installed machine booted into a gamescope session running
# `sleep infinity` — a black screen — and every game this console can play
# lived on one development VM and was compiled by hand. docs/PROJECT.md,
# Phase 5.
#
# None of these are built here. They are big, they are slow, and they come from
# somewhere that already builds them properly:
#
#   the frontend   .github/workflows/build-frontend.yml
#   the cores      .github/workflows/build-core.yml, each pinned to an exact
#                  commit and each asserting that commit back out of the
#                  finished .so
#
# So this only ever COLLECTS and CHECKS. It builds nothing, and it fails before
# the thirteen-minute image build starts rather than after it.
#
# Usage:
#   ci/stage-image-payload.sh              from this tree's own builds
#                                          (frontend/build, cores/build,
#                                          cores/system) — the test VM, or a
#                                          Linux box doing the whole thing by
#                                          hand
#   ci/stage-image-payload.sh <dir>        from a merged CI artifact download,
#                                          which already has this exact shape
#
# Output, always, at image_payload/ in the repository root:
#
#   bin/cabinetos-frontend
#   cores/<core>_libretro.so      x21
#   system/PPSSPP/...             PPSSPP's fonts and lookup tables
#
# image_payload/ is gitignored, which also keeps `git status -s` clean — the
# Justfile reads that to decide whether to stamp the image with a revision, so
# an untracked staging directory would silently cost the build its labels and
# its git-sha tags.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/image_payload"

if [ $# -ge 1 ]; then
    SRC="$1"
    FRONTEND="$SRC/bin/cabinetos-frontend"
    CORES="$SRC/cores"
    SYSTEM="$SRC/system"
else
    FRONTEND="$ROOT/frontend/build/cabinetos-frontend"
    CORES="$ROOT/cores/build"
    SYSTEM="$ROOT/cores/system"
fi

# The cores this console is supposed to have, read off the script that builds
# them rather than written out again here. cores/build-core.sh's case arms are
# the list, and a name that is not one of them is the error it already prints.
mapfile -t CORE_NAMES < <(grep -oE '^[a-z0-9_]+\)' "$ROOT/cores/build-core.sh" | tr -d ')')

# Cross-check on the line above, not a second copy of the list. If somebody
# reshapes that case statement, this says so instead of quietly shipping an
# image with nineteen emulators in it.
EXPECTED=21
if [ "${#CORE_NAMES[@]}" -ne "$EXPECTED" ]; then
    echo "expected $EXPECTED cores in cores/build-core.sh, found ${#CORE_NAMES[@]}" >&2
    printf '  %s\n' "${CORE_NAMES[@]}" >&2
    echo "If a core was genuinely added or removed, change EXPECTED here and say why." >&2
    exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/cores" "$OUT/system"

# --- The frontend ----------------------------------------------------------

[ -f "$FRONTEND" ] || { echo "no frontend binary at $FRONTEND" >&2; exit 1; }
install -m 0755 "$FRONTEND" "$OUT/bin/cabinetos-frontend"

# It has to be an executable this machine's kernel will run, not a script, an
# empty file, or — the one that has actually happened in this project — an
# artifact that downloaded as a directory. `file` is in every runner image.
if command -v file >/dev/null 2>&1; then
    case "$(file -b "$OUT/bin/cabinetos-frontend")" in
        ELF\ 64-bit\ LSB\ *executable*|ELF\ 64-bit\ LSB\ *shared\ object*) ;;
        *) echo "the frontend is not an x86-64 ELF: $(file -b "$OUT/bin/cabinetos-frontend")" >&2
           exit 1 ;;
    esac
fi

# --- The cores -------------------------------------------------------------
#
# Named after the core as the MANIFEST knows it, not as upstream's makefile
# names its output — beetle_ngp builds mednafen_ngp_libretro.so. The three
# lines below are the same rule as cores/build-core.sh and as
# catalog::coreFileName, and all three must agree: the frontend looks the file
# up by this name, and when it does not find one it reports the platform as
# "the core for this system is not built on this console yet". A wrong name
# here is twenty-one working emulators the console says it does not have.
missing=()
for core in "${CORE_NAMES[@]}"; do
    so="${core%_libretro}_libretro.so"
    if [ -f "$CORES/$so" ]; then
        install -m 0644 "$CORES/$so" "$OUT/cores/$so"
    else
        missing+=("$core ($so)")
    fi
done

if [ "${#missing[@]}" -ne 0 ]; then
    echo "these cores are not in $CORES:" >&2
    printf '  %s\n' "${missing[@]}" >&2
    exit 1
fi

# Anything in the source directory that is NOT one of the twenty-one. Not an
# error — a stale .so from a rename would be — but it does not go in the image,
# and saying so beats it vanishing silently.
for f in "$CORES"/*.so; do
    [ -e "$f" ] || continue
    [ -f "$OUT/cores/$(basename "$f")" ] || echo "not shipped, not in the core list: $(basename "$f")"
done

# --- The one core that brings its own system files -------------------------
#
# PPSSPP's are fonts, VFPU lookup tables and a per-game compatibility list.
# They are not a console's firmware and they do not come from RomM; they ship
# with the emulator, and retro_init warns "Core system files missing, expect
# bugs" without them — in a log line nobody reads, while the game runs and
# renders no text. So their absence is a build failure here.
if [ -d "$SYSTEM" ] && [ -n "$(ls -A "$SYSTEM" 2>/dev/null)" ]; then
    cp -R "$SYSTEM/." "$OUT/system/"
fi
[ -f "$OUT/system/PPSSPP/compat.ini" ] || {
    echo "PPSSPP's system files are not in $SYSTEM — PSP would run with no fonts" >&2
    exit 1
}

# --- What went in ----------------------------------------------------------

echo "staged $OUT"
printf '  frontend  %s\n' "$(du -h "$OUT/bin/cabinetos-frontend" | cut -f1)"
printf '  cores     %s in %s\n' "$(find "$OUT/cores" -name '*.so' | wc -l | tr -d ' ')" \
       "$(du -sh "$OUT/cores" | cut -f1)"
printf '  system    %s, %s entries\n' "$(du -sh "$OUT/system" | cut -f1)" \
       "$(find "$OUT/system" -mindepth 2 -maxdepth 2 | wc -l | tr -d ' ')"
printf '  total     %s\n' "$(du -sh "$OUT" | cut -f1)"
