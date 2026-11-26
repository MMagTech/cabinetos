#!/usr/bin/env bash
#
# Builds xemu-chihiro, Tovarichtch's fork of xemu that plays Sega Chihiro
# arcade games, for Linux x86-64 at an exact commit: one program, ready to go
# into the image at /usr/lib/cabinetos/xemu-chihiro/bin/xemu-chihiro.
#
# WHY THE FORK, AND WHY FROM SOURCE:
# docs/decisions/2026-11-26-chihiro-in-the-xemu-fork.md. xemu itself has no
# Chihiro board (xemu-project/xemu#2761) and the fork's only Linux build is an
# AppImage; Batocera 44 builds it from this same commit. A pinned build keeps
# the same emulator for everyone. NEVER PATCHED: Batocera's six patches are
# for its own paths, its cross build and light guns, and the two that hide
# things on screen are settings here (standalone_chihiro.cpp). A patch that
# looks needed is named to MMagTech first (docs/WORKING.md).
#
# HOW: the fork's own configure and make, with Batocera 44's configure flags
# (package/batocera/emulators/xemu-chihiro/xemu-chihiro.mk at this commit)
# less its cross-compile prefix, in a Fedora 44 container
# (cores/xemu-chihiro-builder/Containerfile). Its subprojects come from the
# revisions its .wrap files name, which meson fetches. The program is
# stripped and renamed; it carries everything else it reads (its fonts, the
# board's logo and jingle, its shader seeds) compiled in.
#
# WHAT IT ASSERTS: the source is at the pin; the Vulkan renderer is built;
# every settings key the console writes is one config_spec.yml defines, read
# from frontend/src/standalone_chihiro.cpp itself so the two cannot drift
# (which is why build-xemu-chihiro.yml's cache key covers that file), and no
# keyboard shortcut is left out; and each thing the console leans on that the
# fork does not promise (docs/ARCHITECTURE.md, "What leans on other
# projects"): its input numbers, the board files it finds beside the BIOS,
# the boot.id it reads a game's name by, the saves and cards it writes and
# where, the network firmware's version mark, and its save on quit. Every
# library it still asks the system for is in Fedora 44. WHAT IT CANNOT: that a
# game runs. That is seen on the A9 for every move of the pin.
#
# Usage: cores/build-xemu-chihiro.sh
# Output: cores/build/xemu-chihiro/{bin/xemu-chihiro, VERSION}

set -euo pipefail

REPO=https://github.com/Tovarichtch/xemu.git
# perf-experimental, 2026-10-05, `git describe` v0.8.134-410-g98fbd75dd0: what
# Batocera 44 builds, and what the research of 2026-11-25 ran on the A9 (#619:
# six games at 60 frames a second at 4x). To move it: the update rule
# (docs/decisions/2026-10-23-when-an-emulator-pin-moves.md), a run of the
# Chihiro games on the A9, and ci/list-arcade-changes.sh's list on the PR.
COMMIT=98fbd75dd0f3088a4988abc1698feb37b844a41a
VERSION=0.8.134-410-g98fbd75dd0

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_ROOT="${CABINETOS_CORE_SRC:-$ROOT/.core-src}"
SRC="$SRC_ROOT/xemu-chihiro"
BUILDER="${CABINETOS_XEMU_CHIHIRO_BUILDER:-cabinetos-xemu-chihiro-builder}"
OUT="${CABINETOS_XEMU_CHIHIRO_OUT:-$ROOT/cores/build/xemu-chihiro}"
CONSOLE="$ROOT/frontend/src/standalone_chihiro.cpp"

# Batocera 44's flags, as its .mk gives them at this commit, without
# --cross-prefix. Its --x86-version=3 is for its x86-64-v3 builds only.
CONFIGURE=(
    --target-list=i386-softmmu
    "--extra-cflags=-DXBOX=1 -O3 -Wno-error=redundant-decls -Wno-error=unused-but-set-variable"
    --extra-ldflags=
    --enable-sdl --enable-opengl --enable-trace-backends=nop
    --disable-kvm --disable-xen --disable-werror --disable-curl --disable-vnc
    --disable-vnc-sasl --disable-docs --disable-tools --disable-guest-agent
    --disable-tpm --disable-rdma --disable-replication --disable-capstone
    --disable-libiscsi --disable-spice --disable-user --disable-stack-protector
    --disable-glusterfs --disable-curses --disable-gnutls --disable-nettle
    --disable-gcrypt --disable-crypto-afalg --disable-virglrenderer
    --disable-vhost-net --disable-vhost-crypto --disable-vhost-user
    --disable-virtfs --disable-snappy --disable-bzip2 --disable-vde
    --disable-seccomp --disable-numa --disable-lzo --disable-smartcard
    --disable-usb-redir --disable-bochs --disable-cloop --disable-dmg
    --disable-vdi --disable-vvfat --disable-qcow1 --disable-qed
    --disable-parallels --disable-hvf --disable-whpx --with-default-devices
    --disable-renderdoc --enable-pixman
)

# --- the container ---------------------------------------------------------
if ! podman image exists "$BUILDER"; then
    echo "building $BUILDER"
    podman build -t "$BUILDER" -f "$ROOT/cores/xemu-chihiro-builder/Containerfile" \
        "$ROOT/cores/xemu-chihiro-builder"
fi

# --- the source ------------------------------------------------------------
mkdir -p "$SRC_ROOT"
if [ ! -d "$SRC/.git" ]; then
    git clone --filter=blob:none "$REPO" "$SRC"
fi
git -C "$SRC" fetch --quiet origin "$COMMIT" 2>/dev/null || git -C "$SRC" fetch --quiet --all
git -C "$SRC" checkout --quiet --force "$COMMIT"
# A clean tree each time, subprojects and build folder included, so nothing a
# previous build left stands in for this one.
git -C "$SRC" clean -fdxq
# Asserted, never trusted. Same rule as every core in build-core.sh.
HEAD=$(git -C "$SRC" rev-parse HEAD)
if [ "$HEAD" != "$COMMIT" ]; then
    echo "xemu-chihiro is at $HEAD, expected the pinned $COMMIT" >&2
    exit 1
fi
echo "xemu-chihiro @ $COMMIT"

run_in_builder() {
    podman run --rm -v "$SRC":/src:Z -w /src \
        -e GIT_CONFIG_COUNT=1 \
        -e GIT_CONFIG_KEY_0=safe.directory \
        -e GIT_CONFIG_VALUE_0='*' \
        "$BUILDER" bash -c "$1"
}

# --- the console's settings, in this source ---------------------------------
# Every row of standalone_chihiro.cpp's settings, `{"<table>", "<key>",`, is a
# key under that table in config_spec.yml, the file the fork generates its
# settings from: a key it does not define it ignores, so a renamed one would
# be silently dropped. And every keyboard shortcut config_spec.yml defines is
# one the console sets (to 0, none): a new one would otherwise keep its key.
fail=0
podman run --rm -i -v "$SRC":/src:ro,Z -v "$CONSOLE":/console.cpp:ro,Z "$BUILDER" \
    python3 - /src/config_spec.yml /console.cpp <<'PY' || fail=1
import re, sys, yaml
spec = yaml.safe_load(open(sys.argv[1]))
rows = re.findall(r'^\s*\{"([a-z0-9_.]+)", "([a-z0-9_]+)",', open(sys.argv[2]).read(), re.M)
if len(rows) < 80:
    sys.exit("standalone_chihiro.cpp gave only %d settings rows; its table changed shape" % len(rows))
bad = 0
for table, key in rows:
    node = spec
    for part in table.split(".") + [key]:
        node = node.get(part) if isinstance(node, dict) else None
    if node is None:
        print("xemu-chihiro defines no %s.%s at the pin" % (table, key), file=sys.stderr)
        bad += 1
hotkeys = set(spec["input"]["hotkeys"])
set_here = {k for t, k in rows if t == "input.hotkeys"}
for k in sorted(hotkeys - set_here):
    print("xemu-chihiro has a keyboard shortcut %s the console does not turn off" % k, file=sys.stderr)
    bad += 1
print("%d settings rows checked against config_spec.yml, %d shortcuts off" % (len(rows), len(set_here)))
sys.exit(1 if bad else 0)
PY

# --- what the console leans on, in this source --------------------------------
need() {   # file, fixed string, what it means
    grep -qF -- "$2" "$SRC/$1" || { echo "xemu-chihiro: $3 ($1 no longer has: $2)" >&2; fail=1; }
}
# Its input numbers: a pad's button is 2001 plus SDL's, an axis pushed one way
# 3001 plus the axis times two plus 1 for positive, 0 nothing
# (standalone_chihiro.cpp, padButton and padAxis).
need ui/xemu-input.h '#define CHIHIRO_GAMEPAD_BUTTON_BASE 2001' "a pad button's number moved"
need ui/xemu-input.h '#define CHIHIRO_GAMEPAD_AXIS_BASE 3001' "a pad axis's number moved"
need ui/xemu-input.h '(CHIHIRO_GAMEPAD_AXIS_BASE + (axis) * 2 + ((positive) ? 1 : 0))' \
    "a pad axis's direction is numbered another way"
# The board's files it finds beside chihiro_xbox_bios.bin, under the names
# MAME's chihiro.zip gives them, which the console takes out of that zip.
need hw/xbox/chihiro/chihiro.c '"fpr21042_m29w160et.bin",' "the media board's flash is found by another name"
need hw/xbox/chihiro/chihiro.c 'next_to, "ic10_g24lc64.bin",' "the ic10 EEPROM is found by another name"
need hw/xbox/chihiro/chihiro.c 'next_to, "ic11_24lc024.bin",' "the ic11 EEPROM is found by another name"
need hw/xbox/chihiro/chihiro.c 'next_to, "pc20_g24lc64.bin",' "the pc20 EEPROM is found by another name"
# The network firmware's version, read from the file net_firmware_path names.
need hw/xbox/chihiro/chihiro.c '#define MB_FIRM_VERSION_MARK    "FIRM_VERSION is here!"' \
    "the network firmware is read another way"
# A game's name, which names its saves and cards, from the four letters at
# boot.id 0x30, found through the image's FATX root folder (arcadematch.cpp,
# chihiroId, reads it the same way; cores/arcade-sets.sh reads the table).
need hw/xbox/chihiro/chihiro.c 'memcmp(hdr, "FATX", 4) != 0' "a game image is no longer FATX"
need hw/xbox/chihiro/chihiro.c 'uint32_t spc = ldl_le_p(hdr + 8);' "the image's cluster size moved"
need hw/xbox/chihiro/chihiro.c 'uint32_t root = ldl_le_p(hdr + 12);' "the image's root folder moved"
need hw/xbox/chihiro/chihiro.c 'uint64_t data = 4096 + ROUND_UP(entries * (entries < 0xFFF0 ? 2 : 4), 4096);' \
    "the image's FAT is laid out another way"
need hw/xbox/chihiro/chihiro.c 'g_ascii_strncasecmp((const char *)e + 2, "boot.id", 7) != 0' \
    "boot.id is found another way"
need hw/xbox/chihiro/chihiro.c 'uint32_t first = ldl_le_p(e + 0x2C);' "a folder entry's first cluster moved"
need hw/xbox/chihiro/chihiro.c 'chihiro_game_name_of(bid + 0x30, out, out_len);' \
    "a game's name is read from elsewhere in boot.id"
need hw/xbox/chihiro/chihiro.c '} chihiro_game_names[] = {' "the table of games' names moved"
# The saves it writes and where (standalone_chihiro.cpp, the save folder;
# savetable.cpp): `<data>/saves/<name>.sav` and `<name>.dimm`, `<data>/cards/`,
# under SDL's preferred folder for "xemu", "xemu", which XDG_DATA_HOME places.
need ui/xemu-settings.cc 'char *base = SDL_GetPrefPath("xemu", "xemu");' "its data folder moved"
need hw/xbox/chihiro/chihiro.c 'chihiro_data_dir("saves", saves_dir, sizeof(saves_dir))' "its saves moved"
need hw/xbox/chihiro/chihiro.c 'snprintf(chihiro_save_path, sizeof(chihiro_save_path), "%s/%s.sav",' \
    "a save is named another way"
need hw/xbox/chihiro/chihiro.c 'snprintf(out, out_len, "%.*s.dimm", (int)(len - 4), chihiro_save_path);' \
    "the media board's save is named another way"
need hw/xbox/chihiro/chihiro.c 'chihiro_data_dir("cards", dir, sizeof(dir))' "its cards moved"
# A card it makes is named `<game>[_p1|_p2]_<date>[_<n>].bin`, the game as its
# cabinet table names it, and the slot that holds it is the setting below for
# that game (standalone_chihiro.cpp, cardSlots).
need hw/xbox/chihiro/chihiro.c 'snprintf(name, sizeof(name), "%s%s_%s%s.bin", c->cards, who, day, more);' \
    "a new card is named another way"
need hw/xbox/chihiro/chihiro.c 'snprintf(who, sizeof(who), "_p1");' "player 1's card is named another way"
need hw/xbox/chihiro/chihiro.c 'snprintf(who, sizeof(who), "_p2");' "player 2's card is named another way"
need hw/xbox/chihiro/chihiro.c '&chihiro_card_stock_vsg, "ghostsqu",' "Ghost Squad's cards are named another way"
need hw/xbox/chihiro/chihiro.c 'CHIHIRO_DRIVE_V257,    false, CHIHIRO_MONITOR_15KHZ, NULL, "wangmid",' \
    "Maximum Tune's cards are named another way"
need hw/xbox/chihiro/chihiro.c 'CHIHIRO_DRIVE_V257,    false, CHIHIRO_MONITOR_15KHZ, NULL, "wangmid2",' \
    "Maximum Tune 2's cards are named another way"
need hw/xbox/chihiro/chihiro.c '&chihiro_card_stock_gs, "gundamos",' "Gundam's cards are named another way"
need hw/xbox/chihiro/chihiro.c '? &g_config.chihiro.card_reader.crp1231.mt1' "Maximum Tune's card slot moved"
need hw/xbox/chihiro/chihiro.c ': &g_config.chihiro.card_reader.crp1231.mt2;' "Maximum Tune 2's card slot moved"
need hw/xbox/chihiro/chihiro.c 'return &g_config.chihiro.card_reader.hw210.gundam;' "Gundam's card slot moved"
need hw/xbox/chihiro/chihiro.c 'return &g_config.chihiro.card_reader.hw210.slot1;' "Ghost Squad's first slot moved"
# SIGTERM IS ITS QUIT, and the save is written on the way out: qemu turns the
# signal into a shutdown, and the board's exit notifier flushes the save and
# the media board's (measured on the A9, 2026-11-25: gone in 0.26 s, written).
need system/runstate.c 'void qemu_system_killed(int signal, pid_t pid)' "SIGTERM is no longer a shutdown"
need hw/xbox/chihiro/chihiro.c 'if (chihiro_usb_save_flush(chihiro_save_path))' "the save is no longer written at quit"
need hw/xbox/chihiro/chihiro.c 'qemu_add_exit_notifier(&chihiro_exit_notifier);' "nothing writes the save at quit"
described=$(git -C "$SRC" describe --tags --match 'v*' 2>/dev/null || true)
[ "$described" = "v$VERSION" ] ||
    { echo "xemu-chihiro describes itself as ${described:-nothing}, not v$VERSION" >&2; fail=1; }
(( fail == 0 )) || exit 1

# --- the build -------------------------------------------------------------
run_in_builder "./configure $(printf '%q ' "${CONFIGURE[@]}") 2>&1 | tee configure.log &&
                make -j\$(nproc) -C build qemu-system-i386 2>&1 | tee make.log"
grep -q '^#define CONFIG_VULKAN' "$SRC/build/config-host.h" ||
    { echo "xemu-chihiro configured without its Vulkan renderer" >&2; exit 1; }
grep -q '^#define CONFIG_SDL' "$SRC/build/config-host.h" ||
    { echo "xemu-chihiro configured without SDL" >&2; exit 1; }

# --- what goes into the image ----------------------------------------------
rm -rf "$OUT"
mkdir -p "$OUT/bin"
run_in_builder "strip -o /src/build/xemu-chihiro /src/build/qemu-system-i386"
install -m 0755 "$SRC/build/xemu-chihiro" "$OUT/bin/xemu-chihiro"
printf '%s\n' "$COMMIT" > "$OUT/VERSION"

# EVERY LIBRARY IT STILL ASKS THE SYSTEM FOR RESOLVES in Fedora 44, the image's
# base; the image's own install step checks the same against the image
# itself. libcurl is among them (its builder's header says why).
libs=$(podman run --rm -v "$OUT":/out:Z "$BUILDER" ldd /out/bin/xemu-chihiro)
if grep -q 'not found' <<<"$libs"; then
    echo "xemu-chihiro needs libraries Fedora 44 does not have:" >&2
    grep 'not found' <<<"$libs" >&2
    exit 1
fi
echo "$(grep -c '=>' <<<"$libs") libraries, every one in Fedora 44"
grep -cE ': warning:' "$SRC/make.log" | sed 's/^/compiler warnings: /' || true
echo "wrote $OUT ($(du -sh "$OUT" | cut -f1); program $(du -h "$OUT/bin/xemu-chihiro" | cut -f1))"
