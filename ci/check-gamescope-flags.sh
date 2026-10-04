#!/usr/bin/env bash
#
# Does a gamescope still take everything cabinetos-session passes it? (#227)
#
#   ci/check-gamescope-flags.sh <gamescope --help output> [session script]
#
# Prints each flag or backend the session uses that the help text no longer
# lists, one per line, and exits 1 if there is any; 0 when all are there; 2
# when it could not tell (no help text, or no flags found in the session).
#
# WHY. gamescope comes from the Bazzite base, not from us. A renamed or dropped
# flag makes gamescope refuse to start, and the session then falls to a lower
# rung (no 4K, no VRR, software drawing) with nothing said until somebody looks
# at a television. ci/check-base-update.sh runs this on every new base, so the
# base-update pull request says so first. Same idea as the emulator option check
# built for #63.
#
# THE FLAGS ARE READ FROM THE SESSION, not listed here, so a flag added to the
# session is checked without anyone remembering this file: every `--flag` from a
# line that starts gamescope (`exec gamescope \` or `gamescope --...`) down to
# the `-- ${APP}` that ends it, plus each backend named after `--backend` or
# passed to run_gamescope. Steam's session is gamescope-session-plus's own, from
# the same base, and not ours to check.

set -euo pipefail

HELP="${1:?usage: check-gamescope-flags.sh <help text> [session script]}"
SESSION="${2:-system_files/usr/bin/cabinetos-session}"

if [[ ! -s "${HELP}" ]] || ! grep -q -- '--backend' "${HELP}"; then
    echo "could not read gamescope's --help" >&2
    exit 2
fi

mapfile -t flags < <(awk '
    /^[[:space:]]*#/ { next }
    /(^|[[:space:]])gamescope[[:space:]]+(\\|--)/ { inside = 1 }
    inside {
        n = split($0, w, /[[:space:]]+/)
        for (i = 1; i <= n; i++)
            if (w[i] ~ /^--[a-z][a-z0-9-]+$/) print w[i]
        if ($0 ~ /--[[:space:]]+\$\{APP\}/) inside = 0
    }' "${SESSION}" | sort -u)

mapfile -t backends < <(grep -v '^[[:space:]]*#' "${SESSION}" \
    | grep -oE -- '(--backend|run_gamescope)[[:space:]]+"?[a-z]+"?' \
    | awk '{ gsub(/"/, "", $2); print $2 }' | sort -u)

# The anchor, asserted: the session has always passed at least these four, and
# finding fewer means the reading above broke, not that gamescope is fine.
if (( ${#flags[@]} < 4 )) || (( ${#backends[@]} < 1 )); then
    echo "found only ${#flags[@]} flags and ${#backends[@]} backends in ${SESSION}" >&2
    exit 2
fi

missing=0
for f in "${flags[@]}"; do
    # Listed as "  --flag" or "  -X, --flag", followed by spaces or the end.
    if ! grep -qE -- "(^|[[:space:],])${f}([[:space:]=]|$)" "${HELP}"; then
        echo "${f}"
        missing=1
    fi
done
for b in "${backends[@]}"; do
    # Listed under --backend as "    drm => ...".
    if ! grep -qE "^[[:space:]]+${b}[[:space:]]+=>" "${HELP}"; then
        echo "--backend ${b}"
        missing=1
    fi
done

echo "checked ${#flags[@]} flags (${flags[*]}) and ${#backends[@]} backends (${backends[*]})" >&2
exit "${missing}"
