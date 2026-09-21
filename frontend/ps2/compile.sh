#!/usr/bin/env bash
#
# Compiles CabinetOS's PCSX2 host layer and links the probe, using the exact
# flags PCSX2's own build used.
#
# THE FLAGS ARE READ, NOT WRITTEN DOWN. They come out of compile_commands.json
# for a real PCSX2 translation unit, so a define or an include path that moves
# upstream moves here with it. Writing them into this file by hand is how a
# host layer ends up compiled against a different ABI than the library it links
# into — which does not fail at compile time, and does not reliably fail at
# link time either.
set -euo pipefail

BUILD="${1:?build directory}"
# The PCSX2 revision this was built from, stamped into the .so so the console
# can print what it is ACTUALLY running. Every libretro core answers the same
# question through retro_get_system_info, for the same reason: a core that
# cannot say which revision it is cannot be audited, and this project has
# already spent days recovering revisions from shipped binaries with `strings`.
VERSION="${2:-unknown}"
cd /src

command -v python3 >/dev/null || { echo "python3 is needed to read compile_commands.json" >&2; exit 1; }

cmake -B "$BUILD" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON . >/dev/null

read -r -d '' EXTRACT <<'PY' || true
import json, shlex, sys
db = json.load(open(sys.argv[1]))
ent = [e for e in db if e["file"].endswith("pcsx2/VMManager.cpp")]
if not ent:
    sys.exit("no VMManager.cpp in compile_commands.json")
args = shlex.split(ent[0]["command"])
keep, i = [], 0
while i < len(args):
    a = args[i]
    if a in ("-isystem", "-I"):
        keep += [a, args[i + 1]]; i += 2; continue
    if a.startswith(("-I", "-D", "-isystem", "-std=", "-march=", "-mtune=")) or a == "-fPIC":
        keep.append(a)
    i += 1
print(" ".join(shlex.quote(k) for k in keep))
PY
FLAGS=$(python3 -c "$EXTRACT" "$BUILD/compile_commands.json")

echo "host layer: compiling with $(echo "$FLAGS" | wc -w) flags from PCSX2's own build"

# -I/src/cabinet-ps2 so the two files find their own header; everything else is
# PCSX2's.
for f in CabinetPS2Host CabinetPS2Audio CabinetPS2Bridge CabinetPS2Probe; do
    # shellcheck disable=SC2086  # FLAGS is a deliberately word-split flag list
    clang++ $FLAGS -I/src/cabinet-ps2 -DCABINETOS_PS2_VERSION="\"$VERSION\"" \
        -c "/src/cabinet-ps2/$f.cpp" -o "$BUILD/$f.o"
done

# The link.
#
# THIS STATICALLY LINKS PCSX2 AND THE SHIPPING FRONTEND MUST NOT. PCSX2 is
# GPLv3 and this repository is MIT. docs/LICENCES.md rests its whole position on
# one sentence — "the cores are `dlopen`ed rather than statically linked" — and
# a static link puts the emulator inside the frontend's own binary, which makes
# that binary a combined work under GPLv3.
#
# It is fine HERE, because this probe is a development tool that is never
# distributed and never enters the image. The shipping path is the one open
# question 12's correction already chose for a different reason: each emulator
# becomes its own .so behind a struct of function pointers, exactly like the
# twenty-one libretro cores, which keeps the same arms-length arrangement
# LICENCES.md already relies on. Do not "simplify" this into the frontend link.
#
# Order matters for static archives, and --start-group is how you avoid having
# to work out what that order is for eighteen of them.
SYS="-lpng -ljpeg -lz -lzstd -llz4 -lwebp -lsharpyuv -lfreetype -lharfbuzz"
SYS="$SYS -lplutovg -lplutosvg -lryml -lcurl -lpcap -lfontconfig -ludev"
SYS="$SYS -lX11 -lXrandr -lXi -lXext -ldbus-1 -lSDL3 -lshaderc_shared -ldl -lpthread"

mapfile -t THIRDPARTY < <(find "$BUILD/3rdparty" -name '*.a')

# shellcheck disable=SC2086  # SYS is a deliberately word-split flag list
clang++ -o "$BUILD/cabinet-ps2-probe" \
    "$BUILD/CabinetPS2Host.o" "$BUILD/CabinetPS2Audio.o" "$BUILD/CabinetPS2Probe.o" \
    -Wl,--start-group \
    "$BUILD/pcsx2/libpcsx2.a" "$BUILD/common/libcommon.a" \
    "${THIRDPARTY[@]}" \
    -Wl,--end-group \
    $SYS

echo "host layer: linked $BUILD/cabinet-ps2-probe"

# --- the shared object the console actually loads --------------------------
#
# THIS IS WHAT SHIPS. The probe above is a development tool; this is the file
# the frontend dlopens, and it exists rather than a static link for two
# reasons, either of which would decide it on its own.
#
# The licence is the hard one: PCSX2 is GPLv3 and this repository is MIT, and
# docs/LICENCES.md rests its position on the emulators being dlopen'ed rather
# than linked in. The practical one is that the frontend's builder image does
# not carry PCSX2's thirty-odd dependencies and should not grow them.
#
# -z defs so the linker REFUSES a .so with anything unresolved. Without it a
# missing symbol becomes a dlopen failure on the console at the moment somebody
# starts a game, naming one symbol and telling you nothing about the rest.
# shellcheck disable=SC2086  # SYS is a deliberately word-split flag list
clang++ -shared -Wl,-z,defs -o "$BUILD/cabinetos-ps2.so" \
    "$BUILD/CabinetPS2Host.o" "$BUILD/CabinetPS2Audio.o" "$BUILD/CabinetPS2Bridge.o" \
    -Wl,--start-group \
    "$BUILD/pcsx2/libpcsx2.a" "$BUILD/common/libcommon.a" \
    "${THIRDPARTY[@]}" \
    -Wl,--end-group \
    $SYS

echo "host layer: linked $BUILD/cabinetos-ps2.so ($(du -h "$BUILD/cabinetos-ps2.so" | cut -f1))"

# It must LOAD, not merely link. A .so that satisfies the linker and fails at
# dlopen is the exact failure this project hit with g_host_hotkeys, and it
# reports one missing symbol at a time.
cat > /tmp/dlcheck.c <<'DL'
#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char** argv) {
    void* h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("dlopen FAILED: %s\n", dlerror()); return 1; }
    const char* needed[] = {"cps2_start","cps2_stop","cps2_running","cps2_set_pad",
                            "cps2_take_frame","cps2_drain_audio","cps2_version", 0};
    for (int i = 0; needed[i]; i++)
        if (!dlsym(h, needed[i])) { printf("missing %s\n", needed[i]); return 1; }
    printf("dlopen ok, every entry point resolves\n");
    return 0;
}
DL
clang /tmp/dlcheck.c -o /tmp/dlcheck -ldl
/tmp/dlcheck "$BUILD/cabinetos-ps2.so"
