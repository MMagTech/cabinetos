#!/usr/bin/env bash
#
# Builds windowsarcadeloader, the launcher that plays games from Windows-based
# arcade boards under Wine (MMagTech/cabinetos#762), for Linux x86-64 at an
# exact commit, laid out as the loader lays itself out, ready to go into the
# image at /usr/lib/cabinetos/windowsarcadeloader:
#
#   dist/arcade-launcher         the Linux launcher
#   dist/payloads/               the Windows DLLs and programs it puts beside a
#                                game (one per board), and TMNT's input bridge
#   systemprofiles/              its game profiles, one per game, and the files
#                                they name
#   LICENSE, VERSION             its licence and the commit
#
# WHY THIS LOADER, AND WHY FROM SOURCE: #762, and the trial of 2026-11-26 that
# ran 12 of 13 games on the A9. Its own GitHub releases are tags (v0.1, v0.2),
# not the commit the trial ran, and are built on Ubuntu 24.04; this builds the
# same source on Fedora 44, the image's base, so the launcher links the glibc
# and SDL 3 the console has. Never patched.
#
# HOW: the loader's own build.sh in Fedora 44 (cores/windowsarcadeloader-builder/
# Containerfile), with the crates its Cargo.lock pins. Its last step, TMNT's
# bridge (mcs -sdk:2), needs Ubuntu's Mono (Containerfile.mono beside it), so
# that one line of build.sh, read out of it, runs there first, and build.sh's
# own `mcs` is answered by a stand-in that checks the DLL is already built.
#
# WHAT IT ASSERTS, in the source at the pin: every thing the console leans on
# (standalone_windows.cpp and the image's install script): the subcommands and
# flags, the `.windowsloader` file, the layers and their order, the profile
# keys the console writes and that unknown keys are refused, the variables it
# sets (WAL_DESKTOP_UNMANAGED, WAL_PLAYER_PADS), the run folder, the stop on
# SIGTERM, the shared prefix's verbs, the winetricks it runs, dotnet11's cache,
# and build.sh's line for TMNT's bridge. Then that the lock did not move, every
# file build.sh installs is there, and the launcher's libraries resolve in
# Fedora 44. WHAT IT CANNOT: that a game runs. That is seen on the A9.
#
# Usage: cores/build-windowsarcadeloader.sh [--check]
#   --check   only fetch the source at the pin and run the assertions above
# Output: cores/build/windowsarcadeloader/

set -euo pipefail

REPO=https://github.com/aderumier/windowsarcadeloader.git
# 2026-11-23, "Runner: GE-Proton11-14 by default": the commit the trial of
# 2026-11-26 built and ran on the A9. To move it: a run of the games on the A9,
# the runner it names (install-windowsarcadeloader.sh's GE-Proton pin moves with
# it), and cores/windows-arcade-files.txt made again (cores/arcade-sets.sh).
COMMIT=2bfe0694bce3c603163cad390624a8ad416852f9

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_ROOT="${CABINETOS_CORE_SRC:-$ROOT/.core-src}"
SRC="$SRC_ROOT/windowsarcadeloader"
BUILDER="${CABINETOS_WAL_BUILDER:-cabinetos-windowsarcadeloader-builder}"
MONO="${CABINETOS_WAL_MONO:-cabinetos-windowsarcadeloader-mono}"
OUT="${CABINETOS_WAL_OUT:-$ROOT/cores/build/windowsarcadeloader}"

# --- the source ------------------------------------------------------------
mkdir -p "$SRC_ROOT"
if [ ! -d "$SRC/.git" ]; then
    git clone --filter=blob:none "$REPO" "$SRC"
fi
git -C "$SRC" fetch --quiet origin "$COMMIT" 2>/dev/null || git -C "$SRC" fetch --quiet --all
git -C "$SRC" checkout --quiet --force "$COMMIT"
git -C "$SRC" clean -fdxq
# Asserted, never trusted. Same rule as every core in build-core.sh.
HEAD=$(git -C "$SRC" rev-parse HEAD)
if [ "$HEAD" != "$COMMIT" ]; then
    echo "windowsarcadeloader is at $HEAD, expected the pinned $COMMIT" >&2
    exit 1
fi
echo "windowsarcadeloader @ $COMMIT"

# --- what the console leans on, in this source -----------------------------
# Each line is a file of the loader and a string it must hold, spelt as the
# source spells it, so a renamed flag, key or variable stops the build here
# rather than reaching a console that would be ignored or refused.
fail=0
need() {
    grep -qF -- "$2" "$SRC/$1" || { echo "windowsarcadeloader: $1 no longer has: $2" >&2; fail=1; }
}
L=crates/launcher
# The command line: `run <dump>/<gameid>.windowsloader` and `prepare <gameid>`,
# with --root and --profile (main.rs, parse_args and real_main).
need $L/src/main.rs '"--root" => args.root'
# Its program is arcade-launcher, the process name the console looks for
# (Cargo.toml's [[bin]], build.sh's dist/arcade-launcher).
need $L/Cargo.toml 'name = "arcade-launcher"'
need build.sh 'dist/arcade-launcher'
need $L/src/main.rs '"--profile" => args.layers.push'
grep -qE '\["run", .*"prepare".*\]\.contains\(&c\.as_str\(\)\)' "$SRC/$L/src/main.rs" ||
    { echo "windowsarcadeloader: main.rs no longer takes run and prepare" >&2; fail=1; }
# prepare: the prefix, then prefix_tricks, then the game's tricks (main.rs).
awk '/"prepare" => \{/ {on=1} on && /apply_tricks\(&p\.prefix_tricks\)/ {a=NR}
     on && /apply_tricks\(&p\.tricks\)/ {b=NR} on && /^        \}/ {exit}
     END {exit !(a && b && a < b)}' "$SRC/$L/src/main.rs" ||
    { echo "windowsarcadeloader: prepare no longer installs prefix_tricks, then tricks" >&2; fail=1; }
# The game's file: <gameid>.windowsloader, its first line the program (config.rs).
need $L/src/config.rs 'pub const DUMP_EXT: &str = "windowsloader";'
# The layers and their order: defaults, <root>/launcher.yaml, the system
# profile, the user profile, then each --profile, the last winning
# (config.rs, Layers::load and Profile::load_id).
need $L/src/config.rs 'for layer in [root.join("launcher.yaml"), template] {'
need $L/src/config.rs 'pub const SYSTEM_PROFILES: &str = "systemprofiles";'
need $L/src/config.rs 'pub const USER_PROFILES: &str = "userprofiles";'
awk '/fn load_id\(/ {on=1} on && /merge\(&mut value, layers\.user\)/ {u=NR}
     on && /for layer in extra/ {e=NR} on && /^    \}/ {exit}
     END {exit !(u && e && u < e)}' "$SRC/$L/src/config.rs" ||
    { echo "windowsarcadeloader: the --profile layers no longer come last" >&2; fail=1; }
# The keys the console writes in its --profile layer, as the profile's fields
# (config.rs, Profile and InputConfig); both refuse a key they do not know.
for field in 'pub prefix: PathBuf,' 'pub lib64_dirs: Vec<PathBuf>,' 'pub env: BTreeMap<String, String>,' \
             'pub input: InputConfig,' 'pub exit_combo: Vec<String>,' 'pub exit_keys: Vec<String>,' \
             'pub keyboard_enabled: bool,' 'pub prefix_tricks: Vec<String>,' 'pub tricks: Vec<String>,' \
             'pub guns_enabled: bool,' 'pub touch: bool,' 'pub mouse_wrap: bool,'; do
    need $L/src/config.rs "$field"
done
[ "$(grep -c '#\[serde(deny_unknown_fields)\]' "$SRC/$L/src/config.rs")" -ge 2 ] ||
    { echo "windowsarcadeloader: its profiles no longer refuse unknown keys" >&2; fail=1; }
# An empty exit_combo is never pressed (mapping.rs, all_pressed).
need $L/src/mapping.rs '!sources.is_empty() && sources.iter().all('
# The window left unmanaged (fault 5), read by the payloads and the loader.
need crates/payload-common/src/desktop.rs 'std::env::var("WAL_DESKTOP_UNMANAGED").is_ok_and(|v| v.trim() == "1")'
need crates/loader/src/main.rs 'std::env::var("WAL_DESKTOP_UNMANAGED").is_ok_and(|v| v.trim() == "1")'
# The console's order of the players' pads (players.rs).
need $L/src/players.rs 'pub const ORDER_ENV: &str = "WAL_PLAYER_PADS";'
need $L/src/players.rs 'std::env::var(ORDER_ENV)'
# The run folder, where some games write (main.rs).
need $L/src/main.rs 'wine.prefix.join("drive_c/wal").join(system.name()).join(profile.slug())'
# The stop: SIGTERM or SIGINT (ctrlc with its "termination" feature) sets the
# flag the game loop reads, and that runs wineserver -k (main.rs).
need $L/Cargo.toml 'ctrlc = { version = "3", features = ["termination"] }'
need $L/src/main.rs 'ctrlc::set_handler(move || s.store(true, Ordering::SeqCst))?;'
awk '/if \(hub\.exit_requested \|\| stop\.load\(Ordering::SeqCst\)\) && !killing \{/ {on=NR}
     on && NR <= on + 5 && /let _ = wine\.wineserver\("-k"\);/ {ok=1} END {exit !ok}' "$SRC/$L/src/main.rs" ||
    { echo "windowsarcadeloader: its stop no longer runs wineserver -k" >&2; fail=1; }
# The runner and the shared prefix, where the console's root links them
# (defaults.yaml, embedded in the launcher), and the winetricks it runs: the
# runner's own bin/winetricks first (wine.rs, apply_tricks).
need $L/src/defaults.yaml 'runner: GE-Proton11-14-x86_64'
need $L/src/defaults.yaml 'runners_dir: wine-runners'
need $L/src/defaults.yaml 'prefix: wine-prefix/full'
need $L/src/defaults.yaml 'payloads_dir: dist/payloads'
need $L/src/defaults.yaml 'prefix_tricks: ['
need $L/src/wine.rs 'let winetricks = [self.runner.join("bin/winetricks"), self.runner.with_file_name("winetricks")]'
# GE-Proton's own FFmpeg, last unless lib64_dirs names it first (wine.rs).
need $L/src/wine.rs 'for extra in ["lib/x86_64-linux-gnu", "lib/i386-linux-gnu"] {'
need $L/src/wine.rs 'let lib64 = if config.lib64_dirs.is_empty() {'
# dotnet11 is the launcher's own verb, its installer cached by XDG_CACHE_HOME,
# not W_CACHE (wine.rs, dotnet11.rs); the only verb winetricks does not run.
need $L/src/wine.rs 'if verb == "dotnet11" {'
[ "$(grep -o 'verb == "' "$SRC/$L/src/wine.rs" | wc -l)" -eq 1 ] ||
    { echo "windowsarcadeloader: apply_tricks installs a verb other than dotnet11 itself" >&2; fail=1; }
need $L/src/wine/dotnet11.rs 'std::env::var_os("XDG_CACHE_HOME")'
need $L/src/wine/dotnet11.rs '.join("winetricks/dotnet11");'
# TMNT's bridge, the one line of build.sh run in the Mono container.
MCS_LINE='mcs -target:library -sdk:2 -out:dist/payloads/WAL.DirectInput.dll.tmp tools/tmnt-direct-input/InputBridge.cs'
need build.sh "$MCS_LINE"
need build.sh 'mv -f dist/payloads/WAL.DirectInput.dll.tmp dist/payloads/WAL.DirectInput.dll'
(( fail == 0 )) || exit 1
[[ "${1:-}" == --check ]] && { echo "the source at the pin has everything the console leans on"; exit 0; }

# --- the containers --------------------------------------------------------
for pair in "$BUILDER:Containerfile" "$MONO:Containerfile.mono"; do
    image=${pair%%:*}
    if ! podman image exists "$image"; then
        echo "building $image"
        podman build -t "$image" -f "$ROOT/cores/windowsarcadeloader-builder/${pair#*:}" \
            "$ROOT/cores/windowsarcadeloader-builder"
    fi
done


# run_in <image> <command> [podman options]
run_in() {
    local image="$1" cmd="$2"; shift 2
    podman run --rm -v "$SRC":/src:Z -w /src \
        -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0='*' \
        "$@" "$image" bash -c "$cmd"
}

# --- TMNT's bridge, with Ubuntu's Mono ---------------------------------------
mkdir -p "$SRC/dist/payloads"
run_in "$MONO" "$MCS_LINE"
head -c2 "$SRC/dist/payloads/WAL.DirectInput.dll.tmp" | grep -q MZ ||
    { echo "Mono built no WAL.DirectInput.dll" >&2; exit 1; }

# --- the launcher and the payloads, its own build.sh -------------------------
# build.sh's `mcs` line is answered by this stand-in: the DLL it names is the
# one built above, and build.sh's next line moves it into place.
stand_in="$(mktemp -d)"
trap 'rm -rf "$stand_in"' EXIT
cat > "$stand_in/mcs" <<'MCS'
#!/bin/sh
# cores/build-windowsarcadeloader.sh: built in the Mono container already.
for a in "$@"; do case "$a" in -out:*) out="${a#-out:}" ;; esac; done
[ -s "${out:-}" ] || { echo "mcs stand-in: ${out:-?} was not built in the Mono container" >&2; exit 1; }
MCS
chmod 0755 "$stand_in/mcs"
# shellcheck disable=SC2016 # expanded in the container
run_in "$BUILDER" 'PATH=/stand-in:$PATH ./build.sh' -v "$stand_in":/stand-in:Z

# The crates are the ones its Cargo.lock pins: the build did not move them.
git -C "$SRC" diff --quiet -- Cargo.lock ||
    { echo "the build changed windowsarcadeloader's Cargo.lock" >&2; exit 1; }
# Every file build.sh installs into dist/ is there.
mapfile -t built < <(
    sed -n 's|.*install_file "[^"]*" \(dist/[^" ]*\).*|\1|p' "$SRC/build.sh" | grep -v '\$'
    sed -n 's|^for f in \(.*\); do$|\1|p' "$SRC/build.sh" | tr ' ' '\n' | sed 's|^|dist/payloads/|'
    echo dist/payloads/WAL.DirectInput.dll
)
[ "${#built[@]}" -ge 30 ] || { echo "read only ${#built[@]} files from build.sh" >&2; exit 1; }
for f in "${built[@]}"; do
    [ -s "$SRC/$f" ] || { echo "build.sh did not make $f" >&2; exit 1; }
done

# --- what goes into the image ----------------------------------------------
rm -rf "$OUT"
mkdir -p "$OUT/dist"
install -m 0755 "$SRC/dist/arcade-launcher" "$OUT/dist/arcade-launcher"
cp -R "$SRC/dist/payloads" "$OUT/dist/"
cp -R "$SRC/systemprofiles" "$OUT/"
cp "$SRC/LICENSE" "$OUT/LICENSE"
printf '%s\n' "$COMMIT" > "$OUT/VERSION"

# EVERY LIBRARY THE LAUNCHER ASKS THE SYSTEM FOR RESOLVES in Fedora 44, the
# image's base. The image's own install step checks the same against the image.
libs=$(podman run --rm -v "$OUT":/out:Z "$BUILDER" ldd /out/dist/arcade-launcher)
if grep -q 'not found' <<<"$libs"; then
    echo "the launcher needs libraries Fedora 44 does not have:" >&2
    grep 'not found' <<<"$libs" >&2
    exit 1
fi
echo "wrote $OUT ($(du -sh "$OUT" | cut -f1); launcher $(du -h "$OUT/dist/arcade-launcher" | cut -f1), $(find "$OUT/dist/payloads" -type f | wc -l | tr -d ' ') payload files, $(find "$OUT/systemprofiles" -name '*.yaml' | wc -l | tr -d ' ') profiles)"
