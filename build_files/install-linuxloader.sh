#!/usr/bin/env bash
#
# Put Linux Loader, which plays Sega Lindbergh games, into the image: its
# developers' own AppImage of the pinned release, unpacked, with every
# library the image has itself taken out, its own Mesa among them, so the
# image's 32-bit Mesa draws; and its catalogue beside the cores:
#
#   /usr/lib/cabinetos/linuxloader/usr/bin/linuxloader   the program
#   /usr/lib/cabinetos/linuxloader/usr/lib32/            its own library and
#                                                        what the image lacks
#   /usr/lib/cabinetos/cores/linuxloader.catalogue       which games it plays
#                                                        (frontend/src/arcadematch.h)
#
# WHY THIS BUILD: docs/decisions/2026-11-26-lindbergh-in-linux-loader.md. The
# AppImage is what the trial of 2026-11-26 ran (#620), and building the loader
# here would need a 32-bit builder and still ship the prebuilt Cg, OpenAL and
# OpenSSL its repository carries; for a player the two play the same. Never
# patched. In /usr, like RPCS3, so it moves with the image and a console that
# has never reached the internet has Lindbergh from its first boot; its
# settings and saves go where the console tells it (standalone_lindbergh.cpp),
# because /usr is read-only.
#
# ITS OWN MESA DOES NOT KNOW THE RADEON 890M (25.0.7 with LLVM 15: "LLVM
# doesn't support gfx1150", seen in the trial), and the image's 32-bit Mesa
# does, so the AppImage's is taken out with everything else the image has.
# The image's Mesa 26 links its driver into libGLX_mesa and reads no
# LIBGL_DRIVERS_PATH, so the loader's own setting of it (mainShared.c) points
# at nothing that matters.
#
# THE 32-BIT PACKAGES IT USES ARE NAMED HERE (`packages`, from build.sh), Mesa among them: today they come
# with Steam, and named they stay should Steam ever go, so Lindbergh never
# rests on Steam. And pipewire-alsa's 32-bit half, because The House of the
# Dead EX opens ALSA itself and quits without it (the trial): the 32-bit copy
# of a piece the image has for its 64-bit programs, with the same settings
# files. Nothing changes for any 64-bit program.
#
# Its licence is CC BY-NC-SA 4.0, and each library left in it has its own;
# see docs/LICENCES.md.

set -euo pipefail
source /ctx/lib.sh

readonly LINUXLOADER_VERSION=3.0.15
# The repository and commit of the release, which cores/arcade-sets.sh reads
# to make the catalogue from the same source.
# shellcheck disable=SC2034
readonly LINUXLOADER_REPO=https://github.com/lindbergh-loader/linuxloader.git
readonly LINUXLOADER_COMMIT=96334eb3c5bc028d8941ea4e180166f53d06cda7
readonly LINUXLOADER_URL="https://github.com/lindbergh-loader/linuxloader/releases/download/v${LINUXLOADER_VERSION}/linuxloader.AppImage"
readonly LINUXLOADER_SHA256=0234e1fa830a0dec9dfb3b758247ecc28b14d56a0a7c772e81a044db9fbfb200
readonly DEST=/usr/lib/cabinetos/linuxloader
readonly CORES=/usr/lib/cabinetos/cores
readonly CATALOGUE=/ctx/payload/linuxloader/linuxloader.catalogue

# THE PACKAGES WHOSE LIBRARIES IT USES FROM THE IMAGE, each the owner of one
# the AppImage carries a copy of (read on the image of 2026-11-18), and the
# image's 32-bit Mesa that draws, with the OpenGL half of the image's GL
# dispatch, which the AppImage's GLU asks for (libOpenGL.so.0) and the image
# lacked. Installed by build.sh (`packages`), in the
# OS layer, where every package installed is stamped with the base's time, so
# this layer does not carry the package database again at every build (#511).
if [[ "${1:-}" == packages ]]; then
dnf5 -y install --setopt=install_weak_deps=False \
    alsa-lib.i686 dbus-libs.i686 elfutils-libelf.i686 expat.i686 freetype.i686 \
    glibc.i686 libICE.i686 libX11.i686 libX11-xcb.i686 libXau.i686 libXext.i686 \
    libXi.i686 libXrandr.i686 libXrender.i686 libXxf86vm.i686 libasyncns.i686 \
    libatomic.i686 libbrotli.i686 libdrm.i686 libffi.i686 libgcrypt.i686 \
    libglvnd.i686 libglvnd-glx.i686 libglvnd-opengl.i686 libgpg-error.i686 libogg.i686 libpciaccess.i686 \
    libpng.i686 libsndfile.i686 libstdc++.i686 libvorbis.i686 libxcb.i686 \
    libxml2.i686 libxshmfence.i686 libzstd.i686 mesa-dri-drivers.i686 \
    mesa-libGL.i686 mesa-vulkan-drivers.i686 ncurses-libs.i686 opus.i686 \
    pulseaudio-libs.i686 SDL3.i686 systemd-libs.i686 vulkan-loader.i686 \
    xz-libs.i686 zlib-ng-compat.i686 \
    pipewire-alsa.i686
[[ -e /usr/lib/alsa-lib/libasound_module_pcm_pipewire.so ]] ||
    { log "ERROR: pipewire-alsa.i686 left no 32-bit ALSA plugin for PipeWire"; exit 1; }
exit 0
fi

log "Linux Loader ${LINUXLOADER_VERSION}"

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
fetch_pinned "${LINUXLOADER_URL}" "${LINUXLOADER_SHA256}" "${work}/linuxloader.AppImage" "Linux Loader"
got="$(sha256sum "${work}/linuxloader.AppImage" | cut -d' ' -f1)"
if [[ "${got}" != "${LINUXLOADER_SHA256}" ]]; then
    log "ERROR: Linux Loader's checksum is ${got}, expected ${LINUXLOADER_SHA256}"
    exit 1
fi

# The AppImage unpacks itself without FUSE (install-rpcs3.sh). Its own start,
# AppRun, sets only what standalone_lindbergh.cpp sets itself (APP_IMG_ROOT,
# its libraries first) and its Vulkan drivers, which go below; so it is not
# kept.
chmod +x "${work}/linuxloader.AppImage"
(cd "${work}" && ./linuxloader.AppImage --appimage-extract >/dev/null 2>&1) || true
root="${work}/squashfs-root"
for f in usr/bin/linuxloader usr/lib32/linuxloader.so; do
    [[ -s "${root}/${f}" ]] || { log "ERROR: no ${f} in the AppImage"; exit 1; }
done

rm -rf "${DEST}"
mkdir -p "${DEST}"
cp -a "${root}/usr" "${DEST}/"
printf '%s %s\n' "${LINUXLOADER_VERSION}" "${LINUXLOADER_COMMIT}" > "${DEST}/VERSION"

# WHAT THE IMAGE HAS ITSELF GOES: every library of the same name in the
# image's 32-bit folder, in both of the AppImage's library folders (lib32ID
# is what it puts first for Initial D); and its own Mesa, OpenGL dispatch,
# Vulkan drivers and kernel graphics libraries (libdrm) whatever their names,
# so only the image's draw.
taken=0
for dir in "${DEST}/usr/lib32" "${DEST}/usr/lib32ID"; do
    [[ -d "${dir}" ]] || continue
    for f in "${dir}"/*; do
        name="$(basename "${f}")"
        mesa=0
        case "${name}" in
            dri | libgallium-* | libglapi.so* | libLLVM-* | libGL.so* | libGLX* | libOpenGL.so* | \
                libGLdispatch.so* | libVkLayer_* | libvulkan_* | libdrm*) mesa=1 ;;
        esac
        if [[ ${mesa} -eq 1 || -e "/usr/lib/${name}" ]]; then
            rm -rf "${f}"
            taken=$((taken + 1))
        fi
    done
done
rm -rf "${DEST}/usr/share/vulkan"
rmdir "${DEST}/usr/lib32ID" 2>/dev/null || true
log "  took out ${taken} libraries the image has, its own Mesa among them"
for name in libSDL3_image.so.0 libSDL3_ttf.so.0 libGLU.so.1 libglut.so.3 libXmu.so.6 \
            libncurses.so.5 libtinfo.so.5 libstdc++.so.5 libpcsclite.so.1 libsndio.so.7 \
            libCg.so libCg2.so libCgGL.so libopenal.so.0 libcrypto.so.0.9.7 libssl.so.0.9.7 \
            libGLcore.so.1 libnvidia-tls.so.1 libsegaapi.so libkswapapi.so libposixtime.so; do
    [[ -e "${DEST}/usr/lib32/${name}" ]] ||
        { log "ERROR: ${name}, which the image does not have, is not in the AppImage"; exit 1; }
done

# WHAT THE CONSOLE TELLS IT, checked against the program and its library, so
# a release that renames any of it fails here rather than playing with the
# loader's defaults (docs/ARCHITECTURE.md, "What leans on other projects"):
# its command line (mainShared.c, parseArgs), the game started through the
# shell as its child (linuxExe.c), every settings key standalone_lindbergh.cpp
# writes (config.c, applyIniConfig), the controls file's names (sdlInput.c),
# the clean quit that writes the board's memory (sdlCalls.c, sdlQuit), and
# the folders it writes in the game's own (filesystemShared.c).
program="${DEST}/usr/bin/linuxloader"
library="${DEST}/usr/lib32/linuxloader.so"
need() {
    local where="$1" what="$2"
    grep -q -a -F -e "${what}" "${where}" ||
        { log "ERROR: $(basename "${where}") no longer has '${what}'"; exit 1; }
}
for s in --gamepath --config --controls --controllerdb "Starting \$ %s" "./%s" \
         "LINUX_LOADER_CONFIG_PATH" "LINUX_LOADER_CONTROLS_PATH" "LINUX_LOADER_CONTROLS_DB_PATH"; do
    need "${program}" "${s}"
done
for key in WIDTH HEIGHT BOOST_RENDER_RES FULLSCREEN DISPLAY_INDEX BORDER_ENABLED \
           WHITE_BORDER_PERCENTAGE BLACK_BORDER_PERCENTAGE KEEP_ASPECT_RATIO SCALING_FILTER \
           HIDE_CURSOR FPS_LIMITER_ENABLED FPS_TARGET FPS_OVERLAY_ENABLED FPS_OVERLAY_POSITION \
           INPUT_MODE REGION FREEPLAY EMULATE_JVS EMULATE_RIDEBOARD EMULATE_DRIVEBOARD \
           EMULATE_MOTIONBOARD EMULATE_HW210_CARDREADER EMULATE_ID_CARDREADER \
           EMULATE_TOUCHSCREEN ID_CARDFILE_AUTOLOAD CARDFILE_01 CARDFILE_02 ID_CARDFOLDER \
           JVS_PATH SERIAL_1_PATH SERIAL_2_PATH SRAM_PATH EEPROM_PATH LIBCG_PATH \
           HUMMER_FLICKER_FIX LGJ_RENDER_WITH_MESA DISABLE_BUILTIN_FONT DISABLE_BUILTIN_LOGOS \
           CUSTOM_CURSOR_ENABLED CUSTOM_CURSOR CUSTOM_CURSOR_WIDTH CUSTOM_CURSOR_HEIGHT \
           TOUCH_CURSOR TOUCH_CURSOR_WIDTH TOUCH_CURSOR_HEIGHT PRIMEVAL_HUNT_SCREEN_MODE \
           PRIMEVAL_HUNT_TEST_SCREEN_SINGLE SKIP_OUTRUN_CABINET_CHECK MJ4_ENABLED_ALL_THE_TIME \
           CPU_FREQ_GHZ RAMBO_GUNS_SWITCH HARLEY_ANALOG_REMAP ID5_CHINESE_LANGUAGE \
           ID_STEERING_REDUCTION_PERCENTAGE ENABLE_CROSSHAIRS P1_CROSSHAIR_PATH \
           P2_CROSSHAIR_PATH CUSTOM_CROSSHAIRS_WIDTH CUSTOM_CROSSHAIRS_HEIGHT \
           GSEVO_CROSSHAIR_ALWAYS_ON GSEVO_CROSSHAIR_ALWAYS_OFF DEBUG_MSGS LINDBERGH_COLOUR \
           ENABLE_NETWORK_PATCHES NIC_NAME ID_IP_SEAT_1 ID_IP_SEAT_2 OR2_IPADDRESS OR2_NETMASK \
           IP_CAB1 IP_CAB2 IP_CAB3 IP_CAB4 2SPICY_IP_CAB1 2SPICY_IP_CAB2 SRTV_IPADDRESS; do
    need "${library}" "${key}"
done
for s in ControllerGUIDs "P%d_GUID" ExitGame Coin Service Test Start "GC%d_BUTTON_%s" sdlQuit \
         compiledmesa /var/tmp/warning; do
    need "${library}" "${s}"
done

# THE CATALOGUE, made in CI from the loader's source at the same commit
# (cores/arcade-sets.sh linuxloader), which says which games it plays.
[[ -s "${CATALOGUE}" ]] || { log "ERROR: no linuxloader.catalogue in the payload at ${CATALOGUE}"; exit 1; }
grep -q "^# linuxloader @ ${LINUXLOADER_COMMIT}," "${CATALOGUE}" ||
    { log "ERROR: the catalogue was not made at ${LINUXLOADER_COMMIT}"; exit 1; }
mkdir -p "${CORES}"
install -m 0644 "${CATALOGUE}" "${CORES}/"

# READABLE BY EVERYONE: the console runs as `cabinet` (install-rpcs3.sh).
chmod -R u+rwX,go+rX,go-w "${DEST}"
shut="$(find "${DEST}" ! -perm -o+r | head -5)"
if [[ -n "${shut}" ]]; then
    log "ERROR: files in ${DEST} that the console's user cannot read:"
    echo "${shut}"
    exit 1
fi
[[ "$(stat -c %A "${program}")" == *x ]] || { log "ERROR: linuxloader is not executable by everyone"; exit 1; }

# EVERY LIBRARY ANY OF ITS PROGRAMS AND LIBRARIES NEEDS RESOLVES, against what
# is left of the AppImage first and the image after it, as it runs
# (standalone_lindbergh.cpp). It cannot be started here: there is no display.
# The A9 is where it is seen to run.
missing=""
while IFS= read -r -d '' f; do
    head -c4 "${f}" | grep -q $'\x7fELF' || continue
    out="$(LD_LIBRARY_PATH="${DEST}/usr/lib32" ldd "${f}" 2>&1 || true)"
    grep -q 'not found' <<<"${out}" && missing+="${f}:"$'\n'"$(grep 'not found' <<<"${out}")"$'\n'
done < <(find "${DEST}" -type f -print0)
if [[ -n "${missing}" ]]; then
    log "ERROR: Linux Loader is missing libraries:"
    printf '%s' "${missing}"
    exit 1
fi
log "  installed in ${DEST} ($(du -sh "${DEST}" | cut -f1)), $(grep -vc '^[#!]' "${CORES}/linuxloader.catalogue") programs and games"
