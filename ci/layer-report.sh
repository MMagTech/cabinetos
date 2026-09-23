#!/usr/bin/env bash
#
# What each layer CabinetOS adds on top of Bazzite actually holds.
#
# Written for the layer experiment of 2026-09-23 (docs/PROJECT.md open question
# 27), which split the image's one build step into four layers. Two things
# needed checking by looking rather than by reading the Containerfile:
#
#   1. That each layer holds what it is meant to: the OS changes in one, the
#      cores in the next, then their system files, then the frontend.
#   2. That the build context stays out of all of them. The scripts and the
#      payload reach the build through `RUN --mount=type=bind,from=ctx`, which
#      is meant to leave nothing behind. With one RUN that held; with four it
#      is asserted here: ANY FILE under ctx/ or image_payload/ fails the build.
#
# Reads the local container storage, so it runs after `just build` and before
# anything is pushed. It builds nothing and changes nothing.
#
# Usage: ci/layer-report.sh <image>        e.g. localhost/cabinetos:experiment

set -euo pipefail

IMAGE="${1:?usage: ci/layer-report.sh <image>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# The base is whatever the Containerfile's last FROM names. Read from there
# rather than repeated here, so a base bump needs no edit to this script.
BASE=$(grep -E '^FROM ' "$ROOT/Containerfile" | grep -v ' AS ' | tail -1 | awk '{print $2}')
# podman keeps a pulled image under its digest; the tag in front of it is only
# for people.
BASE_BY_DIGEST="${BASE%%:*}@${BASE##*@}"

GRAPHROOT=$(podman info --format '{{.Store.GraphRoot}}')
DRIVER=$(podman info --format '{{.Store.GraphDriverName}}')
if [[ "$DRIVER" != overlay || ! -f "$GRAPHROOT/overlay-layers/layers.json" ]]; then
    echo "storage driver is '$DRIVER', not overlay: cannot read layers one by one" >&2
    exit 1
fi

# containers/storage keeps its layer records in layers.json and, for some,
# volatile-layers.json. Read both as one list.
LAYERS_JSON=$(mktemp)
trap 'rm -f "$LAYERS_JSON"' EXIT
jq -s 'add' "$GRAPHROOT"/overlay-layers/*layers.json > "$LAYERS_JSON"

top=$(podman image inspect --format '{{.TopLayer}}' "$IMAGE")
base_top=$(podman image inspect --format '{{.TopLayer}}' "$BASE_BY_DIGEST")

# Walk down from the image's top layer to the base's, collecting ours.
ours=()
id="$top"
while [[ -n "$id" && "$id" != "$base_top" ]]; do
    ours=("$id" "${ours[@]}")
    id=$(jq -r --arg id "$id" '.[] | select(.id == $id) | .parent // ""' "$LAYERS_JSON")
done
if [[ "$id" != "$base_top" ]]; then
    echo "walked off the bottom without meeting the base's top layer" >&2
    exit 1
fi

base_count=$(podman image inspect --format '{{len .RootFS.Layers}}' "$BASE_BY_DIGEST")
echo "base:   $BASE"
echo "        $base_count layers"
echo "ours:   ${#ours[@]} layers on top, bottom first"
echo

leaked=0
n=0
for id in "${ours[@]}"; do
    n=$((n + 1))
    diff_digest=$(jq -r --arg id "$id" '.[] | select(.id == $id) | ."diff-digest"' "$LAYERS_JSON")
    diff_size=$(jq -r --arg id "$id" '.[] | select(.id == $id) | ."diff-size"' "$LAYERS_JSON")
    dir="$GRAPHROOT/overlay/$id/diff"

    # podman unshare, because a rootless store holds files owned by mapped
    # uids that the runner's own user cannot read.
    listing=$(podman unshare sh -c "cd '$dir' && find . -mindepth 1 | sed 's|^\./||' | sort")
    files=$(printf '%s\n' "$listing" | grep -c . || true)
    mib=$(awk -v b="$diff_size" 'BEGIN { printf "%.1f", b / 1048576 }')

    echo "layer $n: ${mib} MiB uncompressed, ${files} entries, ${diff_digest}"
    # The top two levels, counted, so a layer's purpose is readable at a glance.
    printf '%s\n' "$listing" | awk -F/ 'NF { k = (NF > 1 ? $1 "/" $2 : $1); c[k]++ }
        END { for (k in c) printf "    %6d  %s\n", c[k], k }' | sort -k2 | awk 'NR <= 40'

    bad=$(printf '%s\n' "$listing" | grep -E '^(ctx|payload|image_payload)/' || true)
    if [[ -n "$bad" ]]; then
        echo "    BUILD CONTEXT LEAKED INTO THIS LAYER:"
        printf '%s\n' "$bad" | awk 'NR <= 20 { print "      " $0 }'
        leaked=1
    fi
    # An empty directory where a mount went is a mount point, not a leak, but
    # it is worth seeing.
    if printf '%s\n' "$listing" | grep -qx 'ctx'; then
        echo "    note: an empty ctx/ directory (the mount point) is in this layer"
    fi
    echo
done

if [[ "$leaked" -ne 0 ]]; then
    echo "ERROR: build context files are inside the image" >&2
    exit 1
fi
echo "no build context in any layer"
