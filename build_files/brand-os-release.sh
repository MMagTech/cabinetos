#!/usr/bin/env bash
#
# Name CabinetOS in /usr/lib/os-release (#137; docs/PROJECT.md open question 7).
#
#   brand-os-release.sh <version>
#
# THE COSMETIC FIELDS ONLY. NAME, PRETTY_NAME, VERSION, the boot menu's name,
# the default hostname and the links change. Every identifier stays Bazzite's:
# ID, ID_LIKE, VERSION_ID, VARIANT_ID, CPE_NAME, IMAGE_ID, OSTREE_VERSION and
# BUILD_ID are read by dnf ($releasever, repository paths) and by Universal
# Blue's own tools, and changing them breaks those, not the look. LOGO stays
# until there is a CabinetOS logo to point at.
#
# Run in the image's last layer with the version, so the one file that changes
# every build is small and changes there.

set -euo pipefail

VERSION="${1:?usage: brand-os-release.sh <version>}"
FILE=/usr/lib/os-release
REPO="https://github.com/MMagTech/cabinetos"

# set_field KEY VALUE: replace the line, or add it when the base has none.
# Asserted afterwards, so a base that changed its format fails the build
# rather than shipping half a rename.
set_field() {
    local key="$1" value="$2"
    if grep -q "^${key}=" "${FILE}"; then
        sed -i "s|^${key}=.*|${key}=\"${value}\"|" "${FILE}"
    else
        printf '%s="%s"\n' "${key}" "${value}" >> "${FILE}"
    fi
    grep -qx "${key}=\"${value}\"" "${FILE}" || {
        echo "brand-os-release: ${key} did not take" >&2
        exit 1
    }
}

set_field NAME "CabinetOS"
set_field PRETTY_NAME "CabinetOS ${VERSION}"
set_field VERSION "${VERSION}"
set_field BOOTLOADER_NAME "CabinetOS ${VERSION}"
set_field DEFAULT_HOSTNAME "cabinetos"
set_field HOME_URL "${REPO}"
set_field DOCUMENTATION_URL "${REPO}"
set_field SUPPORT_URL "${REPO}/issues"
set_field BUG_REPORT_URL "${REPO}/issues"

# The identifiers must still be Bazzite's.
for key in ID VERSION_ID IMAGE_ID; do
    grep -q "^${key}=" "${FILE}" || { echo "brand-os-release: ${key} is missing" >&2; exit 1; }
done
grep -q '^ID=bazzite$' "${FILE}" || { echo "brand-os-release: ID changed" >&2; exit 1; }

echo "[cabinetos] os-release: $(grep '^PRETTY_NAME=' "${FILE}" | cut -d= -f2-)"
