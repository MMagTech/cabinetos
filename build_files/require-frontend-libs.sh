#!/usr/bin/env bash
#
# ===========================================================================
#  DO NOT REMOVE ANYTHING LISTED IN THIS FILE.
# ===========================================================================
#
# The frontend links against these at runtime. The image carries no compiler,
# so nothing here is a build dependency — these are shared objects the binary
# will dlopen or link, and if one goes missing the console does not start.
#
# They are in the Bazzite base today, but INCIDENTALLY: `rpm -q --whatrequires`
# reports nothing depending on either, which means nothing is stopping an
# upstream change from dropping them. That is precisely the situation
# require-cec.sh exists for, so this follows the same pattern — name each one,
# say why, and FAIL THE BUILD if it disappears.
#
# If you are here because the build failed, do not "fix" it by deleting the
# check. Either work out what removed the package, or install it explicitly in
# Containerfile and move it out of this list.
#
# ---------------------------------------------------------------------------
# WHY EACH ONE
# ---------------------------------------------------------------------------
#
#   libcurl.so.4        HTTP for the RomM client — the library, artwork,
#                       downloads and the device-authorisation pairing flow.
#                       Also brings the TLS stack, so an https RomM works
#                       without the frontend implementing any of it.
#
#   libarchive.so.13    Opens a ROM in whatever format its owner keeps it in —
#                       zip, 7z, rar, tar and more, through one API. Measured
#                       on a real library: 801 zip, 84 7z, a long tail of plain
#                       files and 32 with no extension at all.
#
#                       Unlike the other two this one is genuinely load-bearing
#                       in the base image rather than incidental: ostree, rpm,
#                       rpm-ostree and flatpak all link it, so it cannot go
#                       missing without the image losing the ability to update
#                       itself. Listed anyway, because that is an argument and
#                       not a guarantee.
#
#   libjson-c.so.5      Parses RomM's responses. A hand-rolled JSON parser is
#                       the wrong thing to own: it is a solved, fiddly problem
#                       and a bug in it looks like a library that is silently
#                       missing games.
#
set -euo pipefail

# shellcheck source=/dev/null
source /ctx/lib.sh 2>/dev/null || true

missing=0
for lib in libcurl.so.4 libjson-c.so.5 libarchive.so.13; do
    if [ -e "/usr/lib64/$lib" ]; then
        printf '  present  %s\n' "$lib"
    else
        printf '  MISSING  %s\n' "$lib" >&2
        missing=1
    fi
done

if [ "$missing" -ne 0 ]; then
    cat >&2 <<'MSG'

One of the libraries the frontend links against is not in the image.

Read the top of build_files/require-frontend-libs.sh before changing anything.
The fix is to install the package explicitly in Containerfile, NOT to delete
the check: a console that cannot reach its library is not a console.
MSG
    exit 1
fi

echo "frontend runtime libraries present"
