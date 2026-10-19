#!/usr/bin/env bash
# shellcheck disable=SC2016 # single-quoted commands are expanded by the remote shell
#
# Edit the UI, see it on the television, and get a picture back. One command.
#
# WHY THIS EXISTS. The loop it replaces is eight steps — rsync to the VM, build
# in the container, copy the binary to the A9, stop the session, deploy, start
# the session, signal for a capture, fetch the PNG — and it takes about two
# minutes. That is tolerable when a change is a day's work and miserable when it
# is a colour. MMagTech, 2026-09-21, on the UI pass: *"its going to be all over
# the place i have alot to tweak on the ui"*. This is for that.
#
# IT RUNS FROM THE MAC, which is the machine the editing happens on and the only
# one that cannot compile anything. **The A9 now both builds and displays** —
# 24 cores against the VM's 5, and a full clean build in 6.5 seconds — so the
# binary never crosses a machine boundary. `--via-vm` keeps the old route for
# when somebody is using the television.
#
# Usage:
#   tools/ui-loop.sh                     Home, built and shown and captured
#   tools/ui-loop.sh --shot look.png     ...and write the picture here
#   tools/ui-loop.sh --game 305          launch a game (a SNES one, cheap to boot)
#   tools/ui-loop.sh --game 305 --menu   ...and open the pause menu over it
#   tools/ui-loop.sh --no-build          deploy and capture what is already built
#   tools/ui-loop.sh --via-vm            build on the VM instead of the console
#   tools/ui-loop.sh --restore           put the console back on the image
#
#   tools/ui-loop.sh --env CABINETOS_BINARY_rpcs3=/var/home/cabinet/ps3work/AppDir/usr/bin/rpcs3
#       An environment variable for the frontend, repeatable. For trying an
#       emulator that no image carries yet (standalone.cpp, binaryOf).
#
#   tools/ui-loop.sh --session-env CABINETOS_OUTPUT=2560x1440
#       An environment variable for the SESSION script rather than the
#       frontend (--env goes on the frontend's command line, which the script
#       never reads). For trying another screen mode.
#
#   tools/ui-loop.sh --session-script
#       Run the repository's system_files/usr/bin/cabinetos-session instead of
#       the image's, for trying a change to the session (screen modes, the
#       Steam handover) without an image. Copied to the console's home and
#       started through bash; transient like the rest of the drop-in.
#
#   tools/ui-loop.sh --args "--home-backdrop 0.6,0.22,28"
#       Anything else the frontend takes, appended to its command line. This is
#       what makes a tuning pass cheap: a number behind a flag is a redeploy
#       (about 25 seconds with --no-build) instead of a rebuild. Whatever wins
#       goes back into frontend/src/design.h, which is still the record.
#
# THE CAPTURE IS THE FRONTEND'S OWN, not gamescope's. `kill -USR1` makes it
# write its framebuffer to /tmp/cabinetos-frame.bmp. That matters for a reason
# worth remembering: **gamescopectl screenshot does not capture the overlay
# planes**, so anything composited is invisible to it. The frontend photographing
# itself sees everything the frontend drew. docs/PROJECT.md, open question 24.
set -uo pipefail

# The machines come from the environment, so no address is written here
# (CLAUDE.local.example.md says what to set). The VM is needed only for
# --via-vm.
A9=${CABINETOS_A9:?set it to cabinet@<your console> (CLAUDE.local.example.md)}
KEY=${CABINETOS_KEY:-$HOME/.ssh/cabinetos}
VM=${CABINETOS_VM:-}
SSH=(ssh -i "$KEY" -o ConnectTimeout=20 -o StrictHostKeyChecking=no)
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# TRANSIENT, IN /run, AND THAT IS THE SAFETY PROPERTY. This used to live in
# /etc, where it survived a reboot — so forgetting --restore left the reference
# console running a hand-built binary indefinitely, which is exactly the trap
# that was cleaned up on 2026-09-21 when 20-heavy-systems.conf and four copies
# of an unshippable core were deleted.
#
# systemd reads drop-ins from /run/systemd/system/<unit>.d/ as well, and /run is
# a tmpfs. So a forgotten dev binary now evaporates at the next boot and the
# console returns to the image by itself. A mistake that lasts a session rather
# than for ever.
DROPIN=/run/systemd/system/cabinetos-session.service.d/50-ui-loop.conf
DROPIN_DIR=/run/systemd/system/cabinetos-session.service.d
# The old location, removed on restore too, so a console that still has one
# from before this change is cleaned up rather than left behind.
DROPIN_OLD=/etc/systemd/system/cabinetos-session.service.d/50-ui-loop.conf
# THE DEVELOPMENT SHELL IS ON PORT 2222 as of 2026-09-25 (File access took
# 22; see /etc/ssh/sshd_config.d/30-cabinetos.conf). Asked, not assumed, so
# a console still on an older image, where it is 22, keeps working.
A9_PORT=2222
"${SSH[@]}" -p 2222 -o ConnectTimeout=5 -o BatchMode=yes "$A9" true 2>/dev/null || A9_PORT=22
A9SSH=("${SSH[@]}" -p "$A9_PORT")
# AND SUDO'S PASSWORD IS FILE ACCESS'S once File access has ever been turned
# on: it is `cabinet`'s login password. The console keeps it where `cabinet`
# can read it; "cabinet" is the throwaway on a console that never had one.
SUDO='{ cat /var/lib/cabinetos-files/password 2>/dev/null || echo cabinet; } | sudo -S'

BUILD=1
VIA_VM=0
GAME=""
MENU=""
SHOT=""
RESTORE=0
EXTRA=""
ENVS=""
SESSION_ENV=""
SESSION_SCRIPT=0

while [ $# -gt 0 ]; do
    case "$1" in
        --no-build) BUILD=0 ;;
        --via-vm)   VIA_VM=1 ;;
        --game)     GAME="$2"; shift ;;
        --menu)     MENU="--overlay 400" ;;
        --shot)     SHOT="$2"; shift ;;
        --args)     EXTRA="$2"; shift ;;
        --env)      ENVS="$ENVS $2"; shift ;;
        --session-env) SESSION_ENV="$SESSION_ENV\nEnvironment=\"${2//\%/%%}\""; shift ;;
        --session-script) SESSION_SCRIPT=1 ;;
        --restore)  RESTORE=1 ;;
        *) echo "unknown argument: $1" >&2; exit 1 ;;
    esac
    shift
done
if [ "$VIA_VM" -eq 1 ] && [ -z "$VM" ]; then
    echo "CABINETOS_VM: set it to cabinet@<your build VM> for --via-vm (CLAUDE.local.example.md)" >&2
    exit 1
fi

say() { printf '\n== %s\n' "$*"; }

# Put the console back on the image, with no drop-ins. The state
# docs/NEXT-SESSION.md says it should be left in.
if [ "$RESTORE" -eq 1 ]; then
    say "restoring the console to the image"
    "${A9SSH[@]}" "$A9" "$SUDO rm -f $DROPIN $DROPIN_OLD >/dev/null 2>&1
                       $SUDO systemctl daemon-reload >/dev/null 2>&1
                       $SUDO systemctl restart cabinetos-session >/dev/null 2>&1
                       sleep 14
                       echo \"session: \$(systemctl is-active cabinetos-session)\"
                       echo \"drop-ins: \$(ls /etc/systemd/system/cabinetos-session.service.d/ 2>/dev/null | wc -l)\"
                       ps -eo args | grep '[c]abinetos-frontend' | grep -v gamescope | head -1"
    exit 0
fi

# --- build, ON THE CONSOLE ITSELF ------------------------------------------
#
# IT USED TO BUILD ON THE VM AND COPY THE BINARY ACROSS TWO MACHINES. The A9 is
# a far better build machine and always was — 24 cores against the VM's 5, 26 GB
# of RAM against 3 — and building where the binary is going to run removes the
# copy entirely. Measured 2026-09-22: a FULL clean build on the A9 takes 6.5
# seconds, against about eleven for an INCREMENTAL one on the VM.
#
# --via-vm is kept for the case this exists to serve: the A9 is the television,
# and somebody may be using it.
if [ "$BUILD" -eq 1 ]; then
    if [ "$VIA_VM" -eq 1 ]; then
        say "building on the VM"
        # --delete: a source removed or renamed in git goes here too, or the
        # Makefile's wildcard compiles it again beside its replacement.
        rsync -az --delete -e "ssh -i $KEY -o ConnectTimeout=20" "$ROOT/frontend/src/" "$VM:~/frontend/src/" || exit 1
        # The Makefile too: a new library in it (libsystemd, 2026-09-22) is
        # otherwise a link error here that CI would never have.
        rsync -az -e "ssh -i $KEY -o ConnectTimeout=20" "$ROOT/frontend/Makefile" "$VM:~/frontend/Makefile" || exit 1
        # And the vendored libraries (rcheevos, #74), which the Makefile builds.
        rsync -az --delete -e "ssh -i $KEY -o ConnectTimeout=20" "$ROOT/frontend/third_party/" "$VM:~/frontend/third_party/" || exit 1
        # DELETE THE ARTIFACT FIRST. `test -f` on a binary that was already
        # there passes after a failed compile, and then the deploy below
        # cheerfully ships the PREVIOUS build with a checksum that matches
        # itself — exactly the "stale binary looks like a change that did not
        # work" failure the checksum was added to catch. It happened on
        # 2026-09-21: one compile error and a full cycle showing the old screen.
        # The objects go too: rsync keeps the Mac's times, so a copied source
        # can look no newer than its old object and make skips it.
        "${SSH[@]}" "$VM" 'rm -f ~/frontend/build/cabinetos-frontend ~/frontend/src/*.o ~/frontend/src/*.d'
        "${SSH[@]}" "$VM" 'cd ~/frontend && podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make 2>&1 | grep -E "error|Error|warning: unused|built " | head -20'
        "${SSH[@]}" "$VM" 'test -f ~/frontend/build/cabinetos-frontend' || {
            echo "build failed — nothing was deployed, the console still runs what it had" >&2
            exit 1
        }
    else
        say "building on the console"
        # --delete, as on the VM.
        rsync -az --delete -e "ssh -i $KEY -p $A9_PORT -o ConnectTimeout=20" "$ROOT/frontend/src/" "$A9:~/frontend/src/" || exit 1
        # The Makefile too: a new library in it (libsystemd, 2026-09-22) is
        # otherwise a link error here that CI would never have.
        rsync -az -e "ssh -i $KEY -p $A9_PORT -o ConnectTimeout=20" "$ROOT/frontend/Makefile" "$A9:~/frontend/Makefile" || exit 1
        # And the vendored libraries (rcheevos, #74), which the Makefile builds.
        rsync -az --delete -e "ssh -i $KEY -p $A9_PORT -o ConnectTimeout=20" "$ROOT/frontend/third_party/" "$A9:~/frontend/third_party/" || exit 1
        # Same rules as above, objects and all, for the same reasons (6.5 s
        # for a clean build).
        "${A9SSH[@]}" "$A9" 'rm -f ~/frontend/build/cabinetos-frontend ~/frontend/src/*.o ~/frontend/src/*.d'
        "${A9SSH[@]}" "$A9" 'cd ~/frontend && podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make -j24 2>&1 | grep -E "error|Error|warning: unused|built " | head -20'
        "${A9SSH[@]}" "$A9" 'test -f ~/frontend/build/cabinetos-frontend' || {
            echo "build failed — nothing was deployed, the console still runs what it had" >&2
            exit 1
        }
    fi
fi

# --- deploy, to the A9 -----------------------------------------------------
say "deploying to the console"

if [ "$VIA_VM" -eq 1 ]; then
    TMP=$(mktemp -d)
    trap 'rm -rf "$TMP"' EXIT
    scp -q -i "$KEY" "$VM:~/frontend/build/cabinetos-frontend" "$TMP/fe" || exit 1
    SUM_LOCAL=$(shasum -a 256 "$TMP/fe" | awk '{print $1}')
    "${A9SSH[@]}" "$A9" "$SUDO systemctl stop cabinetos-session >/dev/null 2>&1"
    scp -q -i "$KEY" -P "$A9_PORT" "$TMP/fe" "$A9:/var/home/cabinet/cabinetos-frontend-dev" || exit 1
else
    # Built where it runs, so this is a copy within one disk rather than a
    # transfer. The checksum below still earns its place: a `cp` onto a binary
    # that is in use fails with "Text file busy", which is why the session stops
    # first, and a silent failure there looks exactly like a change that did
    # nothing.
    SUM_LOCAL=$("${A9SSH[@]}" "$A9" 'sha256sum ~/frontend/build/cabinetos-frontend | cut -d" " -f1')
    "${A9SSH[@]}" "$A9" "$SUDO systemctl stop cabinetos-session >/dev/null 2>&1"
    "${A9SSH[@]}" "$A9" 'cp ~/frontend/build/cabinetos-frontend /var/home/cabinet/cabinetos-frontend-dev' || exit 1
fi

# CHECK THE CHECKSUM. A stale binary that ignores the flag you just added looks
# exactly like a change that did not work, and cost an hour on 2026-09-21.
SUM_REMOTE=$("${A9SSH[@]}" "$A9" 'chmod +x /var/home/cabinet/cabinetos-frontend-dev; sha256sum /var/home/cabinet/cabinetos-frontend-dev | cut -d" " -f1')
if [ "$SUM_LOCAL" != "$SUM_REMOTE" ]; then
    echo "the binary on the console is not the one just built" >&2
    exit 1
fi
echo "deployed ${SUM_LOCAL:0:12}"

APP="/var/home/cabinet/cabinetos-frontend-dev --core-dir /var/home/cabinet/cores-dev"
# The session splits CABINETOS_APP on spaces and runs it, so `env` in front
# carries the variables, and then becomes the frontend.
[ -n "$ENVS" ] && APP="env$ENVS $APP"
[ -n "$GAME" ] && APP="$APP --launch $GAME"
[ -n "$MENU" ] && APP="$APP $MENU"
[ -n "$EXTRA" ] && APP="$APP $EXTRA"

# The session script from the repository, when asked. Started through bash, a
# system binary, so nothing depends on how a file in a home directory may be
# executed by systemd.
if [ "$SESSION_SCRIPT" -eq 1 ]; then
    "${A9SSH[@]}" -o ConnectTimeout=20 "$A9" 'cat > /var/home/cabinet/cabinetos-session-dev' \
        < "$ROOT/system_files/usr/bin/cabinetos-session" || exit 1
    SESSION_ENV="$SESSION_ENV\nExecStart=\nExecStart=/usr/bin/bash /var/home/cabinet/cabinetos-session-dev"
    echo "session script: the repository's"
fi

# Both go into printf's format, so a % in an argument is doubled to stay a %
# (SESSION_ENV's when it is read, above; its \n are printf's on purpose).
APP=${APP//\%/%%}
"${A9SSH[@]}" "$A9" "printf '[Service]\nEnvironment=\"CABINETOS_APP=$APP\"$SESSION_ENV\n' > /tmp/50-ui-loop.conf
                   $SUDO mkdir -p $DROPIN_DIR >/dev/null 2>&1
                   $SUDO cp /tmp/50-ui-loop.conf $DROPIN >/dev/null 2>&1
                   $SUDO systemctl daemon-reload >/dev/null 2>&1
                   $SUDO systemctl start cabinetos-session >/dev/null 2>&1"

# Long enough for gamescope, the frontend and — when asked for — a game and its
# 400 frames of play before the menu opens.
SETTLE=18
[ -n "$GAME" ] && SETTLE=38
say "waiting ${SETTLE}s for it to come up"
sleep "$SETTLE"

# --- capture ---------------------------------------------------------------
# The frontend, NOT gamescope and NOT the reaper: both match the same string and
# neither answers SIGUSR1.
say "capturing"
"${A9SSH[@]}" "$A9" 'PID=$(ps -eo pid,args | grep "[c]abinetos-frontend-dev --core-dir" | grep -v gamescope | awk "{print \$1}" | head -1)
                   [ -n "$PID" ] || { echo "the frontend is not running"; exit 1; }
                   rm -f /tmp/cabinetos-frame.bmp
                   kill -USR1 "$PID"
                   sleep 4
                   [ -f /tmp/cabinetos-frame.bmp ] || { echo "no capture was written"; exit 1; }
                   python3 -c "
from PIL import Image
im = Image.open(\"/tmp/cabinetos-frame.bmp\").convert(\"RGB\")
im.thumbnail((1400, 1400))
im.save(\"/tmp/ui-loop.png\")
print(\"captured\", im.size)
"' || exit 1

OUT="${SHOT:-$ROOT/.ui-loop.png}"
scp -q -i "$KEY" -P "$A9_PORT" "$A9:/tmp/ui-loop.png" "$OUT" && echo "wrote $OUT"

# --- sound -----------------------------------------------------------------
# IS THE APP'S SOUND ACTUALLY FED? 2026-10-12: the volume keys (#351) left the
# console silent on every start for a day, menus and games, while every test
# read the volume back as full. PipeWire counts each missed delivery on the
# app's stream (pw-top's ERR); a fed stream adds none, a starved one about 47
# a second. Checked after every deploy so nobody has to be listening.
say "sound"
"${A9SSH[@]}" "$A9" 'err() { timeout 3 pw-top -b -n 2 2>/dev/null | grep "cabinetos-frontend" | tail -1 | awk "{print \$9}"; }
                   a=$(err); sleep 4; b=$(err)
                   if [ -z "$a" ] || [ -z "$b" ]; then echo "SOUND: the app has no sound stream"; exit 0; fi
                   if [ $((b - a)) -gt 20 ]; then
                       echo "SOUND IS NOT BEING FED: $((b - a)) missed deliveries in 4 s. The console is silent."
                   else
                       echo "sound: fed ($((b - a)) missed in 4 s)"
                   fi'

say "it is on the television now — look at that rather than the PNG for anything
   involving colour, contrast or motion. The capture is for reading layout."

cat <<'NOTE'

   THE CONSOLE IS ON A HAND-BUILT BINARY UNTIL YOU RUN:  tools/ui-loop.sh --restore
   The drop-in is in /run, so a reboot also puts it back. It cannot outlive the
   power being cycled, which is the point.
NOTE
