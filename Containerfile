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
# The controller list (SDL_GameControllerDB). Installed with the frontend, in
# the small last layer, NOT from system_files/: there it rode in the OS layer,
# and its first update shipped 60 MB to the console instead of one file.
COPY frontend/data /frontend-data
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
# Resolved 2026-10-09:
#   ghcr.io/ublue-os/bazzite:stable == 44.20261006.1
#
# Variant choice: plain `bazzite` (Fedora 44 / Kinoite base), not `bazzite-deck`.
# See docs/PROJECT.md open question 3 — `bazzite-deck` carries session and
# power-button infrastructure we may want in Phase 2.
FROM ghcr.io/ublue-os/bazzite:44.20261006.1@sha256:2cdcb7ec7ab4b2d28474dd1f25de457025ac7cf95518d8f567362d5d5ee76248

ARG IMAGE_NAME="${IMAGE_NAME:-cabinetos}"
ARG IMAGE_VENDOR="${IMAGE_VENDOR:-mmagtech}"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
#
# All customisation happens in build_files/build.sh, which calls the strip
# scripts in order. Keeping it in scripts rather than in RUN layers means the
# reasoning for each removal lives next to the removal.
#
# FIVE LAYERS, ORDERED BY HOW OFTEN EACH CHANGES, 2026-09-23 (RPCS3 added
# 2026-09-28). This was one RUN, so a 1.4 MB frontend change rebuilt and
# re-shipped everything CabinetOS adds: 546 MB to the console. See
# docs/PROJECT.md open question 27.
#
#   1. the OS        Bazzite stripped and configured. Moves on a base bump or a
#                    change to build_files/ or system_files/.
#   2. the cores     314 MB, pinned. Moves when a core is bumped.
#   3. RPCS3         338 MB, pinned. Moves when RPCS3 is bumped.
#      Xenia Edge     53 MB, pinned. Moves when Edge is bumped (2026-09-29).
#   4. core files    23 MB of PPSSPP's and PCSX2's resources. Same.
#   5. the frontend  1.4 MB. Moves with nearly every change.
#
# A console downloads only the layers whose bytes changed, so a frontend change
# ships layer 4 — PROVIDED the layers before it rebuild to identical bytes. CI
# builds from nothing every time, so that rests on `podman build --timestamp`
# in the Justfile, and on there being NO RECHUNK, which throws this order away
# and regroups the image by RPM package.
#
# One image, one digest, one update. Splitting layers changes how the image is
# stored and fetched, never what a console updates as a unit.
RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=cache,dst=/var/cache \
    --mount=type=cache,dst=/var/log \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/build.sh

RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/install-frontend.sh cores

# RPCS3, the PS3 emulator: the RPCS3 team's own build, pinned by checksum,
# 338 MB. Beside the cores because it moves as rarely as they do. Not from
# Flathub; build_files/install-rpcs3.sh says why.
RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/install-rpcs3.sh

# Xenia Edge, the Xbox 360 emulator: its developer's own Linux build, pinned by
# checksum, 53 MB. A layer of its own after RPCS3's, because Edge is released
# many times a day and its pin will move more often than RPCS3's.
# build_files/install-xenia.sh says why.
RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/install-xenia.sh

# Wii U: Cemu, built from source at a pin by build-cemu.yml, from the payload.
RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/install-cemu.sh

# xemu's blank Xbox hard drive, pinned by checksum, 1.6 MB, beside the EEPROM
# that system_files puts in the same folder. xemu itself is a Flatpak
# (flatpaks.list); build_files/install-xemu-drive.sh says why this is not.
RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/install-xemu-drive.sh

RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/install-frontend.sh system

RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/install-frontend.sh frontend

# THE VERSION, IN A LAYER OF ITS OWN, LAST. It changes with every image, so
# anywhere earlier it would drag a layer that did not change into every
# update. Here it is one small file. The console reads it after booting to
# tell whether an update applied; docs/SETTINGS.md, System. The number comes
# from ci/next-version.sh through the Justfile. os-release is named here for
# the same reason (#137): its PRETTY_NAME carries the version.
ARG CABINETOS_VERSION=dev
RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    printf '%s\n' "${CABINETOS_VERSION}" > /usr/share/cabinetos/version && \
    /ctx/brand-os-release.sh "${CABINETOS_VERSION}"

# THE BOOT SPLASH (#108): the initramfs rebuilt with the CabinetOS watermark
# build.sh put in place. After the branding on purpose, so the initramfs's
# copy of os-release names CabinetOS too.
RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/boot-splash.sh

# ---------------------------------------------------------------------------
# Lint
# ---------------------------------------------------------------------------
#
# Verifies the image is a structurally valid bootc image. This catches a class
# of mistakes (broken /usr layout, missing kernel, bad symlinks) that would
# otherwise only show up when the disk image fails to boot.
RUN bootc container lint
