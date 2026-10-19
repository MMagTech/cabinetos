#!/usr/bin/env bash
#
# Does a gamescope still take everything we pass it? (#227)
#
#   ci/check-gamescope-flags.sh <gamescope --help output> [script...]
#
# Prints each flag or backend our scripts use that the help text no longer
# lists, after the script's name, one per line, and exits 1 if there is any; 0
# when all are there; 2 when it could not tell (no help text, or too few flags
# found in a script). The scripts default to the two that start gamescope:
# cabinetos-session, and cabinetos-steam-download (Steam's first download).
#
# WHY. gamescope comes from the Bazzite base, not from us. A renamed or dropped
# flag makes gamescope refuse to start: the session then falls to a lower rung
# (no 4K, no VRR, software drawing) with nothing said until somebody looks at a
# television, and Install Steam fails at once. ci/check-base-update.sh runs this
# on every new base, so the base-update pull request says so first. Same idea
# as the emulator option check built for #63.
#
# THE FLAGS ARE READ FROM THE SCRIPTS, not listed here, so a flag added to one
# is checked without anyone remembering this file: every `--flag` after a
# `gamescope` that starts it (`exec gamescope \` or `gamescope --...`) up to
# the bare `--` that ends gamescope's own options, every `--flag` assigned to
# a bash array expanded there (`${COMPOSED[@]...}`), plus each backend named
# after `--backend` or passed to run_gamescope. Short flags (`-W`) are not read, so
# the scripts use the long ones. Steam's session is gamescope-session-plus's
# own, from the same base, and not ours to check.

set -euo pipefail

HELP="${1:?usage: check-gamescope-flags.sh <help text> [script...]}"
shift
SCRIPTS=("$@")
if (( ${#SCRIPTS[@]} == 0 )); then
    SCRIPTS=(system_files/usr/bin/cabinetos-session
             system_files/usr/libexec/cabinetos-steam-download)
fi

if [[ ! -s "${HELP}" ]] || ! grep -q -- '--backend' "${HELP}"; then
    echo "could not read gamescope's --help" >&2
    exit 2
fi

missing=0
for script in "${SCRIPTS[@]}"; do
    name="${script##*/}"

    # Words after `gamescope` up to the bare `--`: each long flag, and each
    # bash array expanded there ("array NAME"), whose flags are read from its
    # assignments below (COMPOSED=(--force-composition), vrr=(--adaptive-sync)).
    mapfile -t words < <(awk '
        /^[[:space:]]*#/ { next }
        {
            n = split($0, w, /[[:space:]]+/)
            i = 1
            if (!inside) {
                for (; i < n; i++) {
                    if (w[i] == "gamescope" && (w[i + 1] == "\\" || w[i + 1] ~ /^--/)) {
                        inside = 1
                        i++
                        break
                    }
                }
                if (!inside) next
            }
            for (; i <= n; i++) {
                if (w[i] == "--") { inside = 0; break }
                if (w[i] ~ /^--[a-z][a-z0-9-]+$/) print w[i]
                else if (match(w[i], /\$\{[A-Za-z_][A-Za-z0-9_]*\[@\]/))
                    print "array " substr(w[i], RSTART + 2, RLENGTH - 5)
            }
        }' "${script}" | sort -u)
    flags=()
    for word in "${words[@]}"; do
        if [[ "${word}" == array\ * ]]; then
            array="${word#array }"
            mapfile -t -O "${#flags[@]}" flags < <(grep -v '^[[:space:]]*#' "${script}" \
                | grep -oE "(^|[^A-Za-z0-9_])${array}\+?=\([^)]*\)" \
                | grep -oE -- '--[a-z][a-z0-9-]+' || true)
        else
            flags+=("${word}")
        fi
    done
    mapfile -t flags < <(printf '%s\n' "${flags[@]}" | sort -u)

    mapfile -t backends < <(grep -v '^[[:space:]]*#' "${script}" \
        | grep -oE -- '(--backend|run_gamescope)[[:space:]]+"?[a-z]+"?' \
        | awk '{ gsub(/"/, "", $2); print $2 }' | sort -u)

    # The anchor, asserted: each script has always passed at least --backend,
    # --output-width and --output-height, and finding fewer means the reading
    # above broke, not that gamescope is fine.
    if (( ${#flags[@]} < 3 )) || (( ${#backends[@]} < 1 )); then
        echo "found only ${#flags[@]} flags and ${#backends[@]} backends in ${script}" >&2
        exit 2
    fi

    for f in "${flags[@]}"; do
        # Listed as "  --flag" or "  -X, --flag", followed by spaces or the end.
        if ! grep -qE -- "(^|[[:space:],])${f}([[:space:]=]|$)" "${HELP}"; then
            echo "${name}: ${f}"
            missing=1
        fi
    done
    for b in "${backends[@]}"; do
        # Listed under --backend as "    drm => ...".
        if ! grep -qE "^[[:space:]]+${b}[[:space:]]+=>" "${HELP}"; then
            echo "${name}: --backend ${b}"
            missing=1
        fi
    done

    echo "${name}: checked ${#flags[@]} flags (${flags[*]}) and ${#backends[@]} backends (${backends[*]})" >&2
done

exit "${missing}"
