#!/usr/bin/env bash
#
# Check whether the pinned Bazzite base has moved, and work out whether the move
# matters to CabinetOS.
#
# Bazzite rebuilds daily. A dependency bot pointed at it would open a pull
# request every day, and a pull request that arrives every day stops being read.
# So this does three things a bot cannot:
#
#   1. Compares the package manifest of the new base against the one committed
#      at the last bump, so the PR says what actually changed rather than just
#      "digest moved".
#   2. Classifies that change against ci/base-watch.txt — the packages CabinetOS
#      relies on — and labels the result RELEVANT or ROUTINE.
#   3. Flags strip-list drift: removal targets in build_files/ that no longer
#      exist in the base, which means a strip script has silently become a
#      no-op and a desktop application may be creeping back in.
#
# Writes to the working directory:
#   base-manifest.txt   the new base's package list (committed with the bump)
#   /tmp/pr-body.md     the pull request body
#
# Sets these outputs when running under GitHub Actions:
#   changed, relevant, old_version, new_version, new_digest, summary

set -euo pipefail

# Byte ordering, so `sort`, `comm` and `join` agree with each other.
export LC_ALL=C

BASE_REPO="${BASE_REPO:-ghcr.io/ublue-os/bazzite}"
BASE_CHANNEL="${BASE_CHANNEL:-stable}"
MANIFEST="base-manifest.txt"
WATCHLIST="ci/base-watch.txt"
PR_BODY="/tmp/pr-body.md"

say() { echo "[base-update] $*" >&2; }

# wc pads its output on some platforms, and these numbers go into the PR body.
count() { wc -l < "$1" | tr -d '[:space:]'; }

emit() {
    # Set a GitHub Actions output, when there is one to set.
    [[ -n "${GITHUB_OUTPUT:-}" ]] && echo "$1=$2" >> "${GITHUB_OUTPUT}"
    say "$1=$2"
}

# ---------------------------------------------------------------------------
# 1. What are we pinned to now?
# ---------------------------------------------------------------------------

pin_line="$(grep -oE '^FROM[[:space:]]+[^[:space:]]+@sha256:[0-9a-f]{64}' Containerfile | head -1)"
if [[ -z "${pin_line}" ]]; then
    say "ERROR: could not find a digest-pinned FROM line in the Containerfile."
    say "The pin must look like: FROM repo:tag@sha256:<64 hex chars>"
    exit 1
fi

old_ref="${pin_line#FROM}"
old_ref="${old_ref// /}"
old_digest="sha256:${old_ref##*@sha256:}"
old_tag="${old_ref%@*}"
old_tag="${old_tag##*:}"

say "pinned: ${old_tag} (${old_digest})"

# ---------------------------------------------------------------------------
# 2. What is upstream now?
# ---------------------------------------------------------------------------
#
# skopeo reads the manifest and config blob only — a few hundred kilobytes
# rather than the ten gigabytes a pull would cost. Nothing below this point runs
# unless the digest has actually moved.

inspect="$(skopeo inspect "docker://${BASE_REPO}:${BASE_CHANNEL}")"
new_digest="$(jq -r '.Digest' <<<"${inspect}")"
new_version="$(jq -r '.Labels["org.opencontainers.image.version"] // "unknown"' <<<"${inspect}")"

say "upstream ${BASE_CHANNEL}: ${new_version} (${new_digest})"

if [[ "${new_digest}" == "${old_digest}" ]]; then
    say "no change"
    emit changed false
    exit 0
fi

emit changed true
emit old_version "${old_tag}"
emit new_version "${new_version}"
emit new_digest "${new_digest}"

# ---------------------------------------------------------------------------
# 3. What changed inside it?
# ---------------------------------------------------------------------------

say "pulling the new base to read its package list (this is the slow part)"
podman pull "${BASE_REPO}@${new_digest}" >/dev/null

podman run --rm "${BASE_REPO}@${new_digest}" \
    rpm -qa --queryformat '%{NAME} %{VERSION}-%{RELEASE}\n' \
    | sort -u > /tmp/new-manifest.txt

say "new base has $(count /tmp/new-manifest.txt) packages"

first_run=false
if [[ ! -s "${MANIFEST}" ]]; then
    say "no committed manifest — establishing a baseline, no comparison possible"
    first_run=true
    : > /tmp/old-manifest.txt
else
    cp "${MANIFEST}" /tmp/old-manifest.txt
fi

# Package names on each side, for add/remove detection.
awk '{print $1}' /tmp/old-manifest.txt | sort -u > /tmp/old-names.txt
awk '{print $1}' /tmp/new-manifest.txt | sort -u > /tmp/new-names.txt

comm -13 /tmp/old-names.txt /tmp/new-names.txt > /tmp/added.txt
comm -23 /tmp/old-names.txt /tmp/new-names.txt > /tmp/removed.txt

# Version changes: packages present on both sides whose version differs.
: > /tmp/changed.txt
join /tmp/old-manifest.txt /tmp/new-manifest.txt \
    | awk 'NF==3 && $2 != $3 { print $1, $2, "->", $3 }' \
    >> /tmp/changed.txt

say "added=$(count /tmp/added.txt) removed=$(count /tmp/removed.txt) updated=$(count /tmp/changed.txt)"

# ---------------------------------------------------------------------------
# 4. Does any of it matter to us?
# ---------------------------------------------------------------------------

# Load the watchlist once. It is consulted a few thousand times below, and
# re-reading the file for each package is the difference between seconds and
# minutes.
WATCH_PATTERNS=()
while read -r pattern; do
    [[ -z "${pattern}" || "${pattern}" == \#* ]] && continue
    WATCH_PATTERNS+=("${pattern}")
done < "${WATCHLIST}"
say "watching ${#WATCH_PATTERNS[@]} package patterns"

# matches_watchlist <package-name>
matches_watchlist() {
    local name="$1" pattern
    for pattern in "${WATCH_PATTERNS[@]}"; do
        # shellcheck disable=SC2053  # glob match is the point
        [[ "${name}" == ${pattern} ]] && return 0
    done
    return 1
}

: > /tmp/relevant-added.txt
: > /tmp/relevant-removed.txt
: > /tmp/relevant-changed.txt

# The `|| true` on each loop body matters: without it, `set -e` exits the script
# the first time a package does not match the watchlist, which is most of them.
while read -r name; do
    [[ -n "${name}" ]] && matches_watchlist "${name}" \
        && echo "${name}" >> /tmp/relevant-added.txt || true
done < /tmp/added.txt

while read -r name; do
    [[ -n "${name}" ]] && matches_watchlist "${name}" \
        && echo "${name}" >> /tmp/relevant-removed.txt || true
done < /tmp/removed.txt

while read -r name rest; do
    [[ -n "${name}" ]] && matches_watchlist "${name}" \
        && echo "${name} ${rest}" >> /tmp/relevant-changed.txt || true
done < /tmp/changed.txt

relevant_count=$(( $(count /tmp/relevant-added.txt) \
                 + $(count /tmp/relevant-removed.txt) \
                 + $(count /tmp/relevant-changed.txt) ))

# ---------------------------------------------------------------------------
# 5. Strip-list drift.
# ---------------------------------------------------------------------------
#
# A removal target that existed in the OLD base and does not exist in the new one
# means upstream dropped or renamed it during this update. If it was renamed, the
# real package is still in the image and our removal is now doing nothing —
# a desktop application quietly coming back.
#
# Deliberately scoped to packages that were present before. Removal lists also
# contain speculative entries for packages this base has never shipped;
# remove_pkgs skips those by design and logs them at build time, and reporting
# them here every single update would bury the signal that matters.
# Continuation lines in the remove_pkgs calls are one package name each. The
# grep can also catch a bare shell keyword on its own indented line, so those
# are filtered out — a false positive here is only noise in the report, but
# noise is what this whole script exists to avoid.
grep -hoE '^[[:space:]]+[a-zA-Z0-9][a-zA-Z0-9._+-]*[[:space:]]*\\?$' build_files/strip-*.sh \
    | tr -d ' \\' \
    | grep -vxE 'fi|done|else|esac|then|do|exit|true|false|return|group_end|group_start' \
    | sort -u > /tmp/strip-targets.txt || true

: > /tmp/drift.txt
while read -r target; do
    [[ -z "${target}" ]] && continue
    # Systemd unit names appear in the disable_units calls; they are not packages.
    case "${target}" in *.service|*.timer|*.socket|*.target) continue ;; esac
    if grep -qxF "${target}" /tmp/old-names.txt && ! grep -qxF "${target}" /tmp/new-names.txt; then
        echo "${target}" >> /tmp/drift.txt
    fi
done < /tmp/strip-targets.txt

drift_count=$(count /tmp/drift.txt)

# ---------------------------------------------------------------------------
# 6. Verdict.
# ---------------------------------------------------------------------------

if [[ "${first_run}" == true ]]; then
    verdict="BASELINE"
    summary="Baseline manifest recorded — no comparison was possible."
elif (( relevant_count > 0 )); then
    verdict="RELEVANT"
    summary="${relevant_count} change(s) to packages CabinetOS depends on."
else
    verdict="ROUTINE"
    summary="No changes to packages CabinetOS depends on."
fi

emit relevant "$([[ "${verdict}" == RELEVANT ]] && echo true || echo false)"
emit summary "${summary}"

# ---------------------------------------------------------------------------
# 7. Write the pull request body.
# ---------------------------------------------------------------------------

{
    echo "## Bazzite base update: \`${old_tag}\` → \`${new_version}\`"
    echo
    echo "**${verdict}** — ${summary}"
    echo
    echo "| | |"
    echo "|---|---|"
    echo "| Channel | \`${BASE_CHANNEL}\` |"
    echo "| Old digest | \`${old_digest}\` |"
    echo "| New digest | \`${new_digest}\` |"
    echo "| Packages | $(count /tmp/new-manifest.txt) |"
    echo

    if [[ "${verdict}" == "BASELINE" ]]; then
        echo "There was no committed \`base-manifest.txt\` to compare against, so"
        echo "this run only records one. The next base update will produce a real"
        echo "diff."
        echo
    fi

    if (( relevant_count > 0 )); then
        echo "### Changes CabinetOS depends on"
        echo
        echo "Matched against \`ci/base-watch.txt\`. **Read these before merging.**"
        echo
        if [[ -s /tmp/relevant-changed.txt ]]; then
            echo "<details open><summary>Updated ($(count /tmp/relevant-changed.txt))</summary>"
            echo; echo '```'; cat /tmp/relevant-changed.txt; echo '```'; echo "</details>"; echo
        fi
        if [[ -s /tmp/relevant-added.txt ]]; then
            echo "<details open><summary>Added</summary>"
            echo; echo '```'; cat /tmp/relevant-added.txt; echo '```'; echo "</details>"; echo
        fi
        if [[ -s /tmp/relevant-removed.txt ]]; then
            echo "<details open><summary>Removed — check nothing we rely on has gone</summary>"
            echo; echo '```'; cat /tmp/relevant-removed.txt; echo '```'; echo "</details>"; echo
        fi
    fi

    if (( drift_count > 0 )); then
        echo "### ⚠️ Strip-list drift"
        echo
        echo "These packages are named in \`build_files/strip-*.sh\`, existed in the"
        echo "previous base, and are gone from this one. \`remove_pkgs\` skips missing"
        echo "packages silently, so nothing breaks — but if one was **renamed** rather"
        echo "than dropped, the real package is still in the image and the removal is"
        echo "now doing nothing."
        echo
        echo '```'; cat /tmp/drift.txt; echo '```'
        echo
    fi

    echo "### Full package diff"
    echo
    for f in changed added removed; do
        n=$(count "/tmp/${f}.txt")
        (( n == 0 )) && continue
        # Capitalised without ${f^}, which needs bash 4 and is not portable.
        label="$(tr '[:lower:]' '[:upper:]' <<<"${f:0:1}")${f:1}"
        echo "<details><summary>${label} (${n})</summary>"
        echo; echo '```'
        head -500 "/tmp/${f}.txt"
        (( n > 500 )) && echo "... $(( n - 500 )) more"
        echo '```'; echo "</details>"; echo
    done

    echo "---"
    echo
    echo "**Do not merge on a green build alone.** A CabinetOS image that builds"
    echo "is not the same as one that boots. Build a qcow2 from this branch"
    echo "(Actions → Build disk images) and boot it before merging, per"
    echo "\`docs/PROJECT.md\` constraint 2."
} > "${PR_BODY}"

# ---------------------------------------------------------------------------
# 8. Apply the bump.
# ---------------------------------------------------------------------------

cp /tmp/new-manifest.txt "${MANIFEST}"

python3 - "${BASE_REPO}" "${new_version}" "${new_digest}" <<'PY'
import re, sys, pathlib
repo, version, digest = sys.argv[1:4]
p = pathlib.Path("Containerfile")
s = p.read_text()
new_from = f"FROM {repo}:{version}@{digest}"
s2, n = re.subn(r'^FROM\s+\S+@sha256:[0-9a-f]{64}.*$', new_from, s, count=1, flags=re.M)
assert n == 1, "failed to rewrite the FROM line"

# Keep the "resolved on" comment honest.
import datetime
today = datetime.date.today().isoformat()
s2 = re.sub(r'^# Resolved \d{4}-\d{2}-\d{2}:$', f"# Resolved {today}:", s2, count=1, flags=re.M)
s2 = re.sub(r'^#   ghcr\.io/ublue-os/bazzite:stable == .*$',
            f"#   {repo}:stable == {version}", s2, count=1, flags=re.M)
p.write_text(s2)
print(f"Containerfile pinned to {new_from}")
PY

say "done: ${verdict}"
