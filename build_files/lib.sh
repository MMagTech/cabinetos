#!/usr/bin/env bash
#
# Shared helpers for the CabinetOS build scripts.
#
# The important one is `remove_pkgs`. Every removal in this repo goes through
# it, for two reasons:
#
#   1. It skips packages that are not installed instead of failing the build.
#      Bazzite's package set moves. A removal list that hard-fails when upstream
#      renames or drops a package turns every Bazzite bump into a broken build
#      for a reason that has nothing to do with us.
#
#   2. It uses `--no-autoremove`, so dnf removes exactly what it is told and
#      does not sweep up shared dependencies. This is the conservative-removals
#      principle from docs/PROJECT.md made mechanical. We want to remove
#      applications, not cascade into libraries that the gaming stack needs.
#
# It also logs what it removed and what it skipped, so the CI log is the record
# of what was actually present in a given Bazzite build.

set -euo pipefail

log() {
    echo "[cabinetos] $*"
}

group_start() {
    echo "::group::$*"
}

group_end() {
    echo "::endgroup::"
}

# is_installed <package>
is_installed() {
    rpm -q "$1" >/dev/null 2>&1
}

# remove_pkgs <reason> <package>...
#
# Removes the packages that are actually installed, leaving dependencies alone.
remove_pkgs() {
    local reason="$1"
    shift

    local present=()
    local absent=()

    for pkg in "$@"; do
        if is_installed "${pkg}"; then
            present+=("${pkg}")
        else
            absent+=("${pkg}")
        fi
    done

    log "remove (${reason}):"

    if [[ ${#absent[@]} -gt 0 ]]; then
        log "  not installed, skipping: ${absent[*]}"
    fi

    if [[ ${#present[@]} -eq 0 ]]; then
        log "  nothing to do"
        return 0
    fi

    log "  removing: ${present[*]}"
    dnf5 -y remove --no-autoremove "${present[@]}"
}

# disable_units <reason> <unit>...
#
# Disables and masks system units. Masking rather than only disabling, because
# a masked unit cannot be pulled in as a dependency of something else — which
# is the failure mode we care about for the display manager.
disable_units() {
    local reason="$1"
    shift

    log "mask (${reason}): $*"
    for unit in "$@"; do
        systemctl disable "${unit}" 2>/dev/null || true
        systemctl mask "${unit}" 2>/dev/null || true
    done
}

# fetch_pinned <url> <sha256> <dest> <what>
#
# A file an install script pins by URL and sha256 (RPCS3, Xenia Edge, xemu's
# drive). CI fetches each one before the image build and keeps it in its
# Actions cache (ci/fetch-pinned.sh), staged at /ctx/payload/downloads/<sha256>,
# so a GitHub release that answers 500 (RPCS3's, 2026-10-12) no longer fails a
# build. Without that copy (an A9 dry run, a build by hand) it downloads, with
# a retry, as before. Either way the caller checks the sha256 afterwards.
fetch_pinned() {
    local url="$1" sha="$2" dest="$3" what="$4"
    local staged="/ctx/payload/downloads/${sha}"
    if [[ -f "${staged}" ]]; then
        cp "${staged}" "${dest}"
        log "  ${what}: from the build's download cache"
        return 0
    fi
    local attempt
    for attempt in 1 2 3; do
        if curl -fsSL --retry 3 -o "${dest}" "${url}"; then
            log "  ${what}: downloaded"
            return 0
        fi
        log "  download attempt ${attempt} failed"
        sleep 5
    done
    log "ERROR: could not download ${what}"
    exit 1
}
