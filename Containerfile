# CabinetOS
#
# A console operating system for a Beelink SER5, built as a bootc image on top
# of Bazzite.
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

# ---------------------------------------------------------------------------
# Base image
# ---------------------------------------------------------------------------
#
# Pinned to a specific Bazzite stable tag AND its digest. The tag is for humans;
# the digest is the actual pin. Moving this is a deliberate act: bump it in its
# own commit and re-run the VM boot test.
#
# Resolved 2026-09-13:
#   ghcr.io/ublue-os/bazzite:stable == stable-44.20260908
#
# Variant choice: plain `bazzite` (Fedora 44 / Kinoite base), not `bazzite-deck`.
# See docs/PROJECT.md open question 3 — `bazzite-deck` carries session and
# power-button infrastructure we may want in Phase 2.
FROM ghcr.io/ublue-os/bazzite:stable-44.20260908@sha256:437920bae6935fd70719c1e0109f3469b1215a788330b0de924d0c7ac8aaa84c

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
