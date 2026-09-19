# CabinetOS build recipes.
#
# Derived from the Universal Blue image-template (Apache-2.0):
#   https://github.com/ublue-os/image-template
#
# None of this runs on macOS. `build` needs podman on Linux; the VM recipes need
# KVM. In practice these are called by .github/workflows/build.yml. They are
# kept in the repo so the CI steps are readable and so the build can be run on a
# Linux box later without reverse-engineering the workflow.

set dotenv-filename := "cabinetos.env"
set dotenv-load

export image_name := env_var("IMAGE_NAME")
export repo_organization := env_var("REPO_ORGANIZATION")
export image_desc := env_var("IMAGE_DESC")
export image_keywords := env_var("IMAGE_KEYWORDS")
export image_logo_url := env_var("IMAGE_LOGO_URL")
export default_tag := env_var("DEFAULT_TAG")
export bib_image := env_var("BIB_IMAGE")

[private]
default:
    @just --list

# Check that the Justfile parses. Run by CI before anything else.
#
# This is a syntax check, not a format check. Upstream's template runs
# `just --fmt --check`, which fails on formatting differences — a needless way
# to break a build. Formatting is available as `just fmt` if you want it.
[group('Just')]
check:
    #!/usr/bin/env bash
    set -eou pipefail
    just --summary > /dev/null
    echo "Justfile parses"

# Reformat the Justfile in place.
[group('Just')]
fmt:
    #!/usr/bin/env bash
    just --unstable --fmt -f Justfile

# Remove build output.
[group('Utility')]
clean:
    #!/usr/bin/env bash
    set -eou pipefail
    rm -rf output/
    rm -f previous.manifest.json changelog.md output.env

[group('Utility')]
[private]
image_name:
    @echo "${image_name}"

[group('Utility')]
generate-default-tag $tag=default_tag:
    @echo "${tag}"

# Build the container image.
[group('Build')]
build $target_image=image_name $tag=default_tag:
    #!/usr/bin/env bash
    set -euox pipefail

    # The frontend, the cores and PPSSPP's system files. Not in this
    # repository and not built here — collected by ci/stage-image-payload.sh,
    # which is what the workflow runs before this. Checked here so the failure
    # is one sentence rather than a podman COPY error thirty lines into a
    # build log.
    if [[ ! -d image_payload ]]; then
        set +x
        echo "image_payload/ is missing." >&2
        echo >&2
        echo "The image carries the frontend and the twenty-one cores, and" >&2
        echo "neither is in this repository. Build them and collect them:" >&2
        echo >&2
        echo "    ci/stage-image-payload.sh              from this tree" >&2
        echo "    ci/stage-image-payload.sh <artifacts>  from a CI download" >&2
        exit 1
    fi

    LABELS=()
    if [[ -z "$(git status -s)" ]]; then
        GIT_SHA=$(git rev-parse --short HEAD)
        LABELS+=("--label" "org.opencontainers.image.documentation=https://raw.githubusercontent.com/{{ repo_organization }}/{{ image_name }}/${GIT_SHA}/README.md")
        LABELS+=("--label" "org.opencontainers.image.source=https://github.com/{{ repo_organization }}/{{ image_name }}/blob/${GIT_SHA}/Containerfile")
        LABELS+=("--label" "org.opencontainers.image.url=https://github.com/{{ repo_organization }}/{{ image_name }}/tree/${GIT_SHA}")
        LABELS+=("--label" "org.opencontainers.image.revision=${GIT_SHA}")
        LABELS+=("--label" "org.opencontainers.image.version={{ default_tag }}.$(date +%Y%m%d)-${GIT_SHA}")
    fi

    LABELS+=("--label" "org.opencontainers.image.created=$(date -u +%Y\-%m\-%d\T%H\:%M\:%S\Z)")
    LABELS+=("--label" "org.opencontainers.image.description={{ image_desc }}")
    LABELS+=("--label" "org.opencontainers.image.title={{ image_name }}")
    LABELS+=("--label" "org.opencontainers.image.vendor={{ repo_organization }}")
    LABELS+=("--label" "org.opencontainers.image.licenses=MIT")

    podman build \
      "${LABELS[@]}" \
      --pull=newer \
      --tag "${target_image}:${tag}" \
      --file Containerfile \
      .

# Re-layer the image so updates ship smaller deltas.
[group('Build')]
ostree-rechunk $target_image=image_name $tag=default_tag:
    #!/usr/bin/env bash
    set -xeuo pipefail

    GRAPHROOT="$(podman info --format '{{ '{{.Store.GraphRoot}}' }}')"

    podman run --rm --pull=never --privileged \
      --mount=type=image,src="${target_image}:${tag}",target=/rpm-ostree \
      --mount=type=bind,src=${GRAPHROOT},target=/run/host-container-storage,rw \
      --mount=type=tmpfs,target=/run/rpm-ostree-storage \
      --entrypoint /usr/bin/rpm-ostree \
      "localhost/${target_image}:${tag}" \
      compose build-chunked-oci \
      --max-layers 127 \
      --format-version=2 \
      --bootc \
      --rootfs /rpm-ostree \
      --output "containers-storage:[overlay@/run/host-container-storage+/run/rpm-ostree-storage]localhost/${target_image}:${tag}"

# Generate the set of tags to publish.
[group('Utility')]
generate-build-tags $target_image=image_name $tag=default_tag:
    #!/usr/bin/env bash
    set -eou pipefail

    DATE=$(date +%Y%m%d)
    BUILD_TAGS=()
    if [[ -z "$(git status -s)" ]]; then
        GIT_SHA=$(git rev-parse --short HEAD)
        BUILD_TAGS+=("${tag}-${GIT_SHA}")
        BUILD_TAGS+=("${DATE}-${GIT_SHA}")
    fi
    BUILD_TAGS+=("${DATE}")
    BUILD_TAGS+=("${tag}")

    echo "${BUILD_TAGS[@]}"

[group('Utility')]
tag-images $target_image=image_name $tag=default_tag tags="":
    #!/usr/bin/env bash
    set -eoux pipefail

    IMAGE=$(podman inspect ${target_image}:${tag} | jq -r .[].Id)
    podman untag ${IMAGE}

    for tag in {{ tags }}; do
        podman tag $IMAGE "${target_image}:${tag}"
    done

    podman images

# Build a bootable disk image locally. Linux only, needs root and KVM.
[group('Disk')]
[private]
_build-bib $target_image $tag $type $config:
    #!/usr/bin/env bash
    set -euo pipefail

    BUILDTMP=$(mktemp -p "${PWD}" -d -t _build-bib.XXXXXXXXXX)

    sudo podman run \
      --rm -it --privileged --pull=newer --net=host \
      --security-opt label=type:unconfined_t \
      -v $(pwd)/${config}:/config.toml:ro \
      -v $BUILDTMP:/output \
      -v /var/lib/containers/storage:/var/lib/containers/storage \
      "${bib_image}" \
      --type ${type} --use-librepo=True --rootfs=btrfs \
      "${target_image}:${tag}"

    mkdir -p output
    sudo mv -f $BUILDTMP/* output/
    sudo rmdir $BUILDTMP
    sudo chown -R $USER:$USER output/

# Build a qcow2 for VM testing.
[group('Disk')]
build-qcow2 $target_image=("localhost/" + image_name) $tag=default_tag: && (_build-bib target_image tag "qcow2" "disk_config/disk.toml")

# Build the installer ISO.
[group('Disk')]
build-iso $target_image=("localhost/" + image_name) $tag=default_tag: && (_build-bib target_image tag "anaconda-iso" "disk_config/iso.toml")

# Shellcheck the build scripts.
[group('Lint')]
lint:
    #!/usr/bin/env bash
    set -eou pipefail
    # SC1091: shellcheck cannot follow `source /ctx/lib.sh`, because /ctx only
    # exists inside the container build.
    find build_files ci cores -iname "*.sh" -type f -exec shellcheck -e SC1091 "{}" ';'
