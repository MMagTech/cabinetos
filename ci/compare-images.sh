#!/usr/bin/env bash
#
# How much a console holding one image downloads to update to another.
#
# A console fetches every layer of the new image whose digest it does not
# already hold, and nothing else. So the download is exactly the sum of the
# compressed sizes of the new image's layers that the old one lacks. This reads
# both manifests off the registry and does that sum. Nothing is pulled.
#
# Written for the layer experiment of 2026-09-23, docs/PROJECT.md open question
# 27. The number to beat was 546 MB for a 1.4 MB frontend change.
#
# Usage: ci/compare-images.sh <old> <new>
#   each is a registry reference (docker://...) or a saved manifest file

set -euo pipefail

manifest() {
    local ref="$1" raw
    if [[ -f "$ref" ]]; then
        raw=$(cat "$ref")
    else
        raw=$(skopeo inspect --raw "$ref")
    fi
    # A multi-arch index points at the real manifest; follow it to amd64.
    if jq -e '.manifests' >/dev/null <<<"$raw"; then
        local digest
        digest=$(jq -r 'first(.manifests[] | select(.platform.architecture == "amd64") | .digest)' <<<"$raw")
        raw=$(skopeo inspect --raw "${ref%:*}@${digest}")
    fi
    printf '%s' "$raw"
}

OLD=$(manifest "${1:?usage: ci/compare-images.sh <old> <new>}")
NEW=$(manifest "${2:?usage: ci/compare-images.sh <old> <new>}")

mb() { awk -v b="$1" 'BEGIN { printf "%.1f MB", b / 1000000 }'; }

old_total=$(jq '[.layers[].size] | add' <<<"$OLD")
new_total=$(jq '[.layers[].size] | add' <<<"$NEW")
echo "old: $(jq '.layers | length' <<<"$OLD") layers, $(mb "$old_total")"
echo "new: $(jq '.layers | length' <<<"$NEW") layers, $(mb "$new_total")"
echo

fetched=$(jq -n --argjson old "$OLD" --argjson new "$NEW" '
    ($old.layers | map(.digest)) as $have
    | [$new.layers | to_entries[] | select(.value.digest as $d | $have | index($d) | not)]')

count=$(jq 'length' <<<"$fetched")
bytes=$(jq '[.[].value.size] | add // 0' <<<"$fetched")

echo "layers the new image has and the old one does not: $count"
jq -r '.[] | "  layer \(.key + 1): \(.value.size) bytes  \(.value.digest)"' <<<"$fetched"
echo
echo "A CONSOLE ON THE OLD IMAGE DOWNLOADS: $(mb "$bytes")"
