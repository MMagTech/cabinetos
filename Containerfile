# CabinetOS
#
# A console operating system for x86-64 PC hardware, built as a bootc image on
# top of Bazzite. The reference machine is a GEEKOM A9 Pro, but nothing here may
# assume it.
#
# Bazzite is used for its kernel, graphics stack, controller drivers and power
# handling. Its desktop and its Steam integration are removed. See
# docs/PROJECT.md for the full rationale.
#
# This file is never built on a Mac. Builds run in GitHub Actions. See README.md.

# Build scripts are mounted, not copied, so they never end up in the final image.
FROM scratch AS ctx
COPY build_files /
COPY system_files /system_files
# The terms this image ships under, carried in so build.sh can put them INSIDE
# it. Referenced from their real homes rather than duplicated into
# system_files/, so the copy in the image cannot drift from the copy in the
# repository.
COPY LICENSE /licences/LICENSE
COPY docs/LICENCES.md /licences/LICENCES.md
# The frontend binary, the twenty-one cores and PPSSPP's system files.
#
# None of them is in this repository and none of them is built here — they are
# far too slow, and .github/workflows/build-frontend.yml and build-core.yml
# already build them properly, each core pinned to an exact commit and each
# asserting that commit back out of the finished .so.
#
# ci/stage-image-payload.sh collects them into image_payload/ and refuses if
# anything is missing, so this COPY is the last step of a chain rather than the
# place the checking happens. If the build fails here saying image_payload does
# not exist, that script has not been run — see README.md.
COPY image_payload /payload

# ---------------------------------------------------------------------------
# Base image
# ---------------------------------------------------------------------------
#
# Pinned to a specific Bazzite stable tag AND its digest. The tag is for humans;
# the digest is the actual pin. Moving this is a deliberate act: bump it in its
# own commit and re-run the VM boot test.
#
# Resolved 2026-09-21:
#   ghcr.io/ublue-os/bazzite:stable == 44.20260921
#
# Variant choice: plain `bazzite` (Fedora 44 / Kinoite base), not `bazzite-deck`.
# See docs/PROJECT.md open question 3 — `bazzite-deck` carries session and
# power-button infrastructure we may want in Phase 2.
FROM ghcr.io/ublue-os/bazzite:44.20260921@sha256:dcda4d1a0437b2dd2d4f7435e86997483d616031668402ff40a385571a12d894

ARG IMAGE_NAME="${IMAGE_NAME:-cabinetos}"
ARG IMAGE_VENDOR="${IMAGE_VENDOR:-mmagtech}"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
#
# All customisation happens in build_files/build.sh, which calls the strip
# scripts in order. Keeping it in scripts rather than in RUN layers means the
# reasoning for each removal lives next to the removal.
RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=cache,dst=/var/cache \
    --mount=type=cache,dst=/var/log \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/build.sh

# ---------------------------------------------------------------------------
# Lint
# ---------------------------------------------------------------------------
#
# Verifies the image is a structurally valid bootc image. This catches a class
# of mistakes (broken /usr layout, missing kernel, bad symlinks) that would
# otherwise only show up when the disk image fails to boot.
RUN bootc container lint
