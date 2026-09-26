#!/usr/bin/env bash
#
# Make the image that was TESTED the one that ships, instead of building it
# again. MMagTech, 2026-09-25: the point of `testing` is that a feature is
# built once, tried on his console, and when it is good, that goes to main.
#
# On a push to main: if main's files are exactly the files of the commit the
# current `testing` image was built from, that image is tagged `latest`
# (and latest-<sha>, and the date tags) with no build. Same digest, so the
# same bytes, the same version number and the same signature. If they are
# not the same files (merged without testing, or main moved on since), this
# says so and the workflow builds, as it always did.
#
# "The same files" is the git TREE, not the commit: a merge commit has a new
# hash but, from a branch that was up to date with main, the same tree.
#
# Writes promoted=true|false to $GITHUB_OUTPUT.
#
# Usage: ci/promote-tested.sh <registry/image>     needs REGISTRY_USER and
#                                                   REGISTRY_TOKEN to push tags

set -euo pipefail

IMAGE="${1:?usage: ci/promote-tested.sh <registry/image>}"
out() { echo "promoted=$1" >> "${GITHUB_OUTPUT:-/dev/stdout}"; }

if ! info=$(skopeo inspect "docker://${IMAGE}:testing" 2>/dev/null); then
    echo "no testing image to promote; building"
    out false; exit 0
fi
rev=$(jq -r '.Labels["org.opencontainers.image.revision"] // empty' <<<"${info}")
version=$(jq -r '.Labels["org.opencontainers.image.version"] // empty' <<<"${info}")
digest=$(jq -r '.Digest' <<<"${info}")

if [[ -z "${rev}" ]] || ! git cat-file -e "${rev}^{commit}" 2>/dev/null; then
    echo "testing was built from '${rev:-nothing recorded}', which this checkout does not have; building"
    out false; exit 0
fi

tested=$(git rev-parse "${rev}^{tree}")
here=$(git rev-parse "HEAD^{tree}")
if [[ "${tested}" != "${here}" ]]; then
    echo "main (${here:0:12}) is not what testing was built from (${rev}, ${tested:0:12}); building"
    echo "the difference:"
    git diff --stat "${rev}" HEAD | tail -20
    out false; exit 0
fi

sha=$(git rev-parse --short HEAD)
date=$(date +%Y%m%d)
echo "main is exactly ${rev}, tested as ${version} (${digest}): promoting it"
for tag in latest "latest-${sha}" "${date}-${sha}" "${date}"; do
    skopeo copy --preserve-digests \
        --dest-creds "${REGISTRY_USER}:${REGISTRY_TOKEN}" \
        "docker://${IMAGE}@${digest}" "docker://${IMAGE}:${tag}"
    echo "  tagged ${tag}"
done

# Read back, not assumed.
now=$(skopeo inspect "docker://${IMAGE}:latest" | jq -r '.Digest')
if [[ "${now}" != "${digest}" ]]; then
    echo "latest is ${now}, not the tested ${digest}" >&2
    exit 1
fi
{
    echo "## Promoted, not built"
    echo
    echo "- \`latest\` is the tested image: version \`${version}\`, built from \`${rev}\`"
    echo "- Digest: \`${digest}\`"
} >> "${GITHUB_STEP_SUMMARY:-/dev/null}"
out true
