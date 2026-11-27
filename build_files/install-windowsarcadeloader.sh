#!/usr/bin/env bash
#
# Put windowsarcadeloader, which plays games from Windows-based arcade boards
# (#762), into the image in the loader's own layout, with the Wine it runs
# them in, and its catalogue and files list beside the cores:
#
#   /usr/lib/cabinetos/windowsarcadeloader/
#     dist/arcade-launcher                    the launcher, as
#                                             cores/build-windowsarcadeloader.sh
#                                             built it at the pin
#     dist/payloads/                          its Windows DLLs, one per board
#     systemprofiles/                         its game profiles
#     wine-runners/GE-Proton11-14-x86_64/     GE-Proton's Wine (bin/wine at
#                                             its top), with winetricks
#                                             20260125 as its bin/winetricks
#     LICENSE, VERSION                        the loader's licence and commit
#   /usr/lib/cabinetos/cores/windowsarcadeloader.catalogue
#                                             which games it plays, and each
#                                             one's Windows setup
#   /usr/lib/cabinetos/cores/windowsarcadeloader.files
#                                             every file those setups install
#                                             (frontend/src/arcadematch.h)
#
# WHY: #762, and docs/decisions/2026-11-26-windows-arcade-in-windowsarcadeloader.md.
# In /usr, like Linux Loader, so it moves with the image; the console gives it
# a root of its own in the emulator's home with links to these folders
# (standalone_windows.cpp), so every relative path of every profile works
# unchanged and nothing here is written. Never patched.
#
# GE-PROTON11-14 IS THE RUNNER THE PINNED LOADER NAMES (its defaults.yaml), the
# plain GitHub release, checked by the sha512 its release publishes and the
# sha256 GitHub gives it, as the loader's own notes install it ("sha512
# checked") with winetricks as its bin/winetricks. Only its Wine tree
# (`files/`) goes in; the Steam wrapper around it (proton, protonfixes) does
# not, since nothing here runs Steam. Its own FFmpeg (lib/x86_64-linux-gnu)
# stays: the console puts it first (lib64_dirs), the trial's fault 1.
#
# WINETRICKS IS PINNED HERE, at its tag 20260125's commit and checksum, the
# one cores/arcade-sets.sh reads the verbs' downloads from. The image has no
# other: build_files/strip-steam.sh removes Bazzite's.
#
# NO WINDOWS FILE IS IN THE IMAGE. The verbs' installers are Microsoft's and
# NVIDIA's; the admin brings them through RomM (#762), and the console builds
# each Windows setup from them with no network.
#
# Licences: the loader GPL-3.0; GE-Proton BSD-3-Clause with Wine LGPL-2.1+,
# DXVK and vkd3d-proton zlib, and the rest its LICENSE names; winetricks
# LGPL-2.1+. docs/LICENCES.md.

set -euo pipefail
source /ctx/lib.sh

readonly GEPROTON_VERSION=GE-Proton11-14
# The GitHub release; its sha256 is GitHub's digest of the asset, its sha512
# the release's own GE-Proton11-14-x86_64.sha512sum. Both checked.
readonly GEPROTON_URL="https://github.com/GloriousEggroll/proton-ge-custom/releases/download/${GEPROTON_VERSION}/${GEPROTON_VERSION}-x86_64.tar.gz"
readonly GEPROTON_SHA256=26bcea3324b68dee68bf6b9b005f4425cdb9c29872e126d790df5abfebe332a8
readonly GEPROTON_SHA512=047fe613ad000f6fac0b6795350680edc7dd78790ee1a588bb8ff2705317186acbe43736fc7a8dbf8dabc59ed64d8177a35c5f713d0104d25d40c08b318960de
# winetricks 20260125: the tag's commit, so the address never moves.
readonly WINETRICKS_VERSION=20260125
readonly WINETRICKS_URL=https://raw.githubusercontent.com/Winetricks/winetricks/b76e1ee79ac57d7aceb384f74518fc423265810c/src/winetricks
readonly WINETRICKS_SHA256=431f82fc74000e6c864409f1d8fb495d696c03928808e3e8acffc45179312a7b
readonly SRC=/ctx/payload/windowsarcadeloader
readonly DEST=/usr/lib/cabinetos/windowsarcadeloader
readonly CORES=/usr/lib/cabinetos/cores
readonly RUNNER="${DEST}/wine-runners/${GEPROTON_VERSION}-x86_64"

log "windowsarcadeloader $(cut -c1-10 "${SRC}/VERSION"), ${GEPROTON_VERSION}, winetricks ${WINETRICKS_VERSION}"
# A FILE, NOT AN EXECUTABLE, IN THE PAYLOAD: GitHub's artifact upload does not
# keep permissions (install-cemu.sh), so the execute bits are set below.
for f in dist/arcade-launcher dist/payloads/WAL.DirectInput.dll dist/payloads/wal-loader.exe \
         systemprofiles LICENSE VERSION \
         windowsarcadeloader.catalogue windowsarcadeloader.files; do
    [[ -e "${SRC}/${f}" ]] || { log "ERROR: no ${f} in the payload at ${SRC}"; exit 1; }
done
commit="$(cat "${SRC}/VERSION")"
grep -q "^# windowsarcadeloader @ ${commit}," "${SRC}/windowsarcadeloader.catalogue" ||
    { log "ERROR: the catalogue was not made at the loader's commit ${commit}"; exit 1; }
grep -q "from winetricks at ${WINETRICKS_SHA256} " "${SRC}/windowsarcadeloader.files" ||
    { log "ERROR: the files list was not read from the winetricks pinned here"; exit 1; }
# Its first line is the loader's pin alone, which the admin's script reads.
[[ "$(head -1 "${SRC}/windowsarcadeloader.files")" == "# windowsarcadeloader ${commit}" ]] ||
    { log "ERROR: the files list does not start with the loader's commit ${commit}"; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
fetch_pinned "${GEPROTON_URL}" "${GEPROTON_SHA256}" "${work}/ge.tar.gz" "${GEPROTON_VERSION}"
[[ "$(sha256sum "${work}/ge.tar.gz" | cut -d' ' -f1)" == "${GEPROTON_SHA256}" ]] ||
    { log "ERROR: ${GEPROTON_VERSION}'s sha256 is not the pin's"; exit 1; }
[[ "$(sha512sum "${work}/ge.tar.gz" | cut -d' ' -f1)" == "${GEPROTON_SHA512}" ]] ||
    { log "ERROR: ${GEPROTON_VERSION}'s sha512 is not its release's"; exit 1; }
fetch_pinned "${WINETRICKS_URL}" "${WINETRICKS_SHA256}" "${work}/winetricks" "winetricks ${WINETRICKS_VERSION}"
[[ "$(sha256sum "${work}/winetricks" | cut -d' ' -f1)" == "${WINETRICKS_SHA256}" ]] ||
    { log "ERROR: winetricks' sha256 is not the pin's"; exit 1; }

rm -rf "${DEST}"
mkdir -p "${DEST}/wine-runners" "${CORES}"
cp -R "${SRC}/dist" "${SRC}/systemprofiles" "${SRC}/LICENSE" "${SRC}/VERSION" "${DEST}/"
# THE RUNNER: the tree with bin/wine at its top is the release's files/, as
# the loader's notes and the trial's setup lay it out. Unpacked beside where
# it goes, not in /tmp, which is a tmpfs here and too small for its 1.6 GB.
unpack="${DEST}/wine-runners/.unpack"
mkdir -p "${unpack}"
tar -C "${unpack}" -xzf "${work}/ge.tar.gz"
rm -f "${work}/ge.tar.gz"
top="${unpack}/${GEPROTON_VERSION}-x86_64"
[[ -x "${top}/files/bin/wine" ]] ||
    { log "ERROR: ${GEPROTON_VERSION}'s release has no files/bin/wine"; exit 1; }
mv "${top}/files" "${RUNNER}"
# Its licence and version beside it, where its own release keeps them.
for f in LICENSE LICENSE.OFL PATENTS.AV1 version; do
    [[ -s "${top}/${f}" ]] || { log "ERROR: ${GEPROTON_VERSION}'s release has no ${f}"; exit 1; }
    cp "${top}/${f}" "${RUNNER}/"
done
rm -rf "${unpack}"
install -m 0755 "${work}/winetricks" "${RUNNER}/bin/winetricks"
install -m 0644 "${SRC}/windowsarcadeloader.catalogue" "${SRC}/windowsarcadeloader.files" "${CORES}/"

# READABLE BY EVERYONE: the console runs as `cabinet` (install-rpcs3.sh).
chmod -R u+rwX,go+rX,go-w "${DEST}"
chmod 0755 "${DEST}/dist/arcade-launcher"

# --- what the console leans on ----------------------------------------------
# Each checked in the program or file that does it, so a pin that moves any
# of it fails here (docs/ARCHITECTURE.md, "What leans on other projects").
# cores/build-windowsarcadeloader.sh checked the same in the loader's source;
# these read the build. A string here is one whole literal of the source, so
# the compiled program holds it as it is.
fail=0
need() {
    local where="$1" what="$2"
    grep -q -a -F -e "${what}" "${where}" ||
        { log "ERROR: ${where#"${DEST}"/} no longer has '${what}'"; fail=1; }
}
launcher="${DEST}/dist/arcade-launcher"
# The console finds the launcher by its process name, the program's file name,
# which the kernel keeps to 15 characters: arcade-launcher is exactly that.
name="$(basename "${launcher}")"
[[ "${name}" == arcade-launcher && ${#name} -le 15 ]] ||
    { log "ERROR: the launcher is ${name}, not the arcade-launcher the console looks for"; fail=1; }
# The command line: `run <dump>/<gameid>.windowsloader --root DIR --profile
# FILE` and `prepare <gameid> --root DIR --profile FILE` (main.rs, USAGE and
# parse_args), the game's file (config.rs, DUMP_EXT), the layers and the run
# folder (config.rs, main.rs), the pads' order and the stop (main.rs,
# players.rs). The launcher's own .NET 1.1 cache is read from XDG_CACHE_HOME;
# that name is checked in the source (cores/build-windowsarcadeloader.sh),
# not here: the compiler does not keep a name that short as one string in the
# program, so only the path beside it, winetricks/dotnet11, can be found.
for s in "arcade-launcher [run] <dump> [--root DIR] [--profile FILE]..." \
         "arcade-launcher prepare <dump> | <gameid> [--root DIR] [--profile FILE]..." \
         "--root needs a directory" "--profile needs a file" "windowsloader" \
         "launcher.yaml" "systemprofiles" "userprofiles" "drive_c/wal" "WAL_PLAYER_PADS" \
         "launcher: stopping the game" "bin/winetricks" "bin/wineserver" \
         "lib/x86_64-linux-gnu" "winetricks/dotnet11" "dotnetfx.exe"; do
    need "${launcher}" "${s}"
done
# Its built-in defaults (defaults.yaml, in the program whole): the layer order,
# the runner and folders the console's root links, and the defaults of the
# input keys the console writes in its own layer (the profile's fields
# themselves are checked in the source, cores/build-windowsarcadeloader.sh).
for s in "#   2. <root>/launcher.yaml" "#   3. <root>/systemprofiles/<system>/<game>.yaml" \
         "#   4. <root>/userprofiles/<system>/<game>.yaml" \
         "runner: ${GEPROTON_VERSION}-x86_64" "runners_dir: wine-runners" "prefix: wine-prefix/full" \
         "payloads_dir: dist/payloads" "lib64_dirs: []" "prefix_tricks: [" \
         "  exit_combo: [back, start]" "  exit_keys: [KEY_ESC]" "  keyboard_enabled: true"; do
    need "${launcher}" "${s}"
done
# The window left unmanaged (fault 5): read by every board's payload at its
# start (payload-common's start) and by the loader that starts some games.
for f in "${DEST}"/dist/payloads/wal_*.dll "${DEST}/dist/payloads/wal-loader.exe" \
         "${DEST}/dist/payloads/wal-loader64.exe"; do
    need "${f}" "WAL_DESKTOP_UNMANAGED"
done
# The runner: Wine, its server (the stop's wineserver -k), its FFmpeg, and
# DXVK's frame cap. GE-Proton11-14's DXVK (v3.1.1) reads no DXVK_FRAME_RATE:
# the cap is the option dxvk.maxFrameRate, given in DXVK_CONFIG
# (src/util/config/config.cpp, dxgi_options.cpp and d3d9_options.cpp).
for f in bin/wine bin/wineserver bin/winetricks; do
    [[ -x "${RUNNER}/${f}" ]] || { log "ERROR: the runner has no ${f}"; fail=1; }
done
for lib in libavcodec.so.62 libavformat.so.62 libavutil.so.60 libswscale.so.9 libswresample.so.6; do
    [[ -e "${RUNNER}/lib/x86_64-linux-gnu/${lib}" ]] ||
        { log "ERROR: the runner's own FFmpeg has no lib/x86_64-linux-gnu/${lib}"; fail=1; }
done
for arch in i386-windows x86_64-windows; do
    for dll in dxgi.dll d3d9.dll; do
        need "${RUNNER}/lib/wine/dxvk/${arch}/${dll}" "DXVK_CONFIG"
        need "${RUNNER}/lib/wine/dxvk/${arch}/${dll}" "dxvk.maxFrameRate"
    done
done
need "${RUNNER}/version" "${GEPROTON_VERSION}"
# winetricks: its version, its cache (W_CACHE, else XDG_CACHE_HOME's), and
# w_download_to, whose cache hit with the right sha256 downloads nothing.
wt="${RUNNER}/bin/winetricks"
# shellcheck disable=SC2016 # winetricks' own lines, as it spells them
for s in "WINETRICKS_VERSION=${WINETRICKS_VERSION}" 'W_CACHE="${W_CACHE:-${XDG_CACHE_HOME}/winetricks}"' \
         "w_download_to()" 'if test -s "${_W_cache}/${_W_file}" ; then' \
         'if [ "${_W_gotsha256sum}"x = "${_W_sum}"x ] ; then'; do
    need "${wt}" "${s}"
done
# The catalogue names this runner, and every verb it names is winetricks' or
# the launcher's own, and the files list is of those verbs alone.
grep -qx "!runner ${GEPROTON_VERSION}-x86_64" "${CORES}/windowsarcadeloader.catalogue" ||
    { log "ERROR: the catalogue does not name the runner ${GEPROTON_VERSION}-x86_64"; fail=1; }
verbs="$(grep -v '^[#!]' "${CORES}/windowsarcadeloader.catalogue" | grep -o '~[^ ]*' |
    sed 's/^~[^:]*://' | tr ',' '\n' | sort -u)"
while read -r v; do
    [[ "${v}" == dotnet11 ]] || grep -q "^load_${v}()" "${wt}" ||
        { log "ERROR: winetricks has no verb ${v}, which the catalogue names"; fail=1; }
done <<<"${verbs}"
while read -r v _; do
    grep -qxF "${v}" <<<"${verbs}" || { log "ERROR: the files list names ${v}, no game's verb"; fail=1; }
done < <(grep -v '^#' "${CORES}/windowsarcadeloader.files")
# What the launcher, winetricks and the console run from the image's own
# packages, each a program on the PATH (`type -P`: bash's built-in kill would
# answer for a missing /usr/bin/kill): winetricks' unpackers and checksum, a
# downloader it looks for before it finds its cache full, sha256sum for
# dotnet11, kill for the launcher's last stop; and the console's own
# (standalone_windows.cpp): fuse-overlayfs and fusermount3 for the game's
# folder under a person's layer, bwrap for the sandbox, tar for the admin's
# file, gamescope to build the Windows setup with no screen, prlimit.
for c in cabextract unzip 7z curl sha256sum kill fuse-overlayfs fusermount3 bwrap tar gamescope prlimit; do
    type -P "${c}" >/dev/null || { log "ERROR: the image has no ${c}, which Windows Arcade runs"; fail=1; }
done
# The options the console gives that a base update could take away, read from
# each program's own help (bubblewrap 0.12.0 and gamescope 3.17.0 on the image
# of 2026-11-18): bwrap's overlay over the shared prefix, its own pid
# namespace and /proc; gamescope's headless backend.
bwrap_help="$(bwrap --help 2>&1 || true)"
for o in --overlay-src --overlay --unshare-pid --proc; do
    grep -q -e "^ *${o} " <<<"${bwrap_help}" || { log "ERROR: the image's bwrap has no ${o}"; fail=1; }
done
gamescope_help="$(gamescope --help 2>&1 || true)"
grep -q -e '^ *--backend ' <<<"${gamescope_help}" || { log "ERROR: the image's gamescope has no --backend"; fail=1; }
grep -q -e 'headless =>' <<<"${gamescope_help}" || { log "ERROR: the image's gamescope has no headless backend"; fail=1; }
(( fail == 0 )) || exit 1
# --- end of what the console leans on --------------------------------------

# EVERY LIBRARY THE LAUNCHER, WINE AND THE PARTS OF IT EVERY GAME LOADS ASK FOR
# RESOLVES, against the runner's own folders and the image, in the order the
# console gives them (its own FFmpeg first). Other parts of the runner (the
# Steam bridges, pcap, smart cards) load only for the games that ask, and stay
# unchecked. It cannot be started here: there is no display. The A9 is where
# it is seen to run.
ld="${RUNNER}/lib/x86_64-linux-gnu:/usr/lib64:${RUNNER}/lib/wine/x86_64-unix"
missing=""
for f in "${launcher}" "${RUNNER}/bin/wine" "${RUNNER}/bin/wineserver" \
         "${RUNNER}"/lib/wine/x86_64-unix/{ntdll,win32u,winex11,winevulkan,winepulse,winedmo}.so \
         "${RUNNER}"/lib/x86_64-linux-gnu/lib{avcodec,avformat,avutil,swscale,swresample}.so.[0-9]*; do
    [[ -e "${f}" && ! -L "${f}" ]] || continue
    out="$(LD_LIBRARY_PATH="${ld}" ldd "${f}" 2>&1 || true)"
    grep -q 'not found' <<<"${out}" && missing+="${f}:"$'\n'"$(grep 'not found' <<<"${out}")"$'\n'
done
if [[ -n "${missing}" ]]; then
    log "ERROR: windowsarcadeloader is missing libraries:"
    printf '%s' "${missing}"
    exit 1
fi
shut="$(find "${DEST}" ! -type l ! -perm -o+r | head -5)"
if [[ -n "${shut}" ]]; then
    log "ERROR: files in ${DEST} that the console's user cannot read:"
    echo "${shut}"
    exit 1
fi
log "  installed in ${DEST} ($(du -sh "${DEST}" | cut -f1); runner $(du -sh "${RUNNER}" | cut -f1)), $(grep -vc '^#' "${CORES}/windowsarcadeloader.catalogue") games, $(grep -vc '^#' "${CORES}/windowsarcadeloader.files") files"
