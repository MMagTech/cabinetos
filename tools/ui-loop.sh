#!/usr/bin/env bash
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

VM=cabinet@192.168.1.250
A9=cabinet@192.168.1.212
KEY=~/.ssh/cabinetos
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
PW=cabinet

BUILD=1
VIA_VM=0
GAME=""
MENU=""
SHOT=""
RESTORE=0
EXTRA=""

while [ $# -gt 0 ]; do
    case "$1" in
        --no-build) BUILD=0 ;;
        --via-vm)   VIA_VM=1 ;;
        --game)     GAME="$2"; shift ;;
        --menu)     MENU="--overlay 400" ;;
        --shot)     SHOT="$2"; shift ;;
        --args)     EXTRA="$2"; shift ;;
        --restore)  RESTORE=1 ;;
        *) echo "unknown argument: $1" >&2; exit 1 ;;
    esac
    shift
done

say() { printf '\n== %s\n' "$*"; }

# Put the console back on the image, with no drop-ins. The state
# docs/NEXT-SESSION.md says it should be left in.
if [ "$RESTORE" -eq 1 ]; then
    say "restoring the console to the image"
    "${SSH[@]}" "$A9" "echo $PW | sudo -S rm -f $DROPIN $DROPIN_OLD >/dev/null 2>&1
                       echo $PW | sudo -S systemctl daemon-reload >/dev/null 2>&1
                       echo $PW | sudo -S systemctl restart cabinetos-session >/dev/null 2>&1
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
        rsync -az -e "ssh -i $KEY -o ConnectTimeout=20" "$ROOT/frontend/src/" "$VM:~/frontend/src/" || exit 1
        # DELETE THE ARTIFACT FIRST. `test -f` on a binary that was already
        # there passes after a failed compile, and then the deploy below
        # cheerfully ships the PREVIOUS build with a checksum that matches
        # itself — exactly the "stale binary looks like a change that did not
        # work" failure the checksum was added to catch. It happened on
        # 2026-09-21: one compile error and a full cycle showing the old screen.
        "${SSH[@]}" "$VM" 'rm -f ~/frontend/build/cabinetos-frontend'
        "${SSH[@]}" "$VM" 'cd ~/frontend && podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make 2>&1 | grep -E "error|Error|warning: unused|built " | head -20'
        "${SSH[@]}" "$VM" 'test -f ~/frontend/build/cabinetos-frontend' || {
            echo "build failed — nothing was deployed, the console still runs what it had" >&2
            exit 1
        }
    else
        say "building on the console"
        rsync -az -e "ssh -i $KEY -o ConnectTimeout=20" "$ROOT/frontend/src/" "$A9:~/frontend/src/" || exit 1
        # Same rule as above, and for the same reason.
        "${SSH[@]}" "$A9" 'rm -f ~/frontend/build/cabinetos-frontend'
        "${SSH[@]}" "$A9" 'cd ~/frontend && podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make -j24 2>&1 | grep -E "error|Error|warning: unused|built " | head -20'
        "${SSH[@]}" "$A9" 'test -f ~/frontend/build/cabinetos-frontend' || {
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
    "${SSH[@]}" "$A9" "echo $PW | sudo -S systemctl stop cabinetos-session >/dev/null 2>&1"
    scp -q -i "$KEY" "$TMP/fe" "$A9:/var/home/cabinet/cabinetos-frontend-dev" || exit 1
else
    # Built where it runs, so this is a copy within one disk rather than a
    # transfer. The checksum below still earns its place: a `cp` onto a binary
    # that is in use fails with "Text file busy", which is why the session stops
    # first, and a silent failure there looks exactly like a change that did
    # nothing.
    SUM_LOCAL=$("${SSH[@]}" "$A9" 'sha256sum ~/frontend/build/cabinetos-frontend | cut -d" " -f1')
    "${SSH[@]}" "$A9" "echo $PW | sudo -S systemctl stop cabinetos-session >/dev/null 2>&1"
    "${SSH[@]}" "$A9" 'cp ~/frontend/build/cabinetos-frontend /var/home/cabinet/cabinetos-frontend-dev' || exit 1
fi

# CHECK THE CHECKSUM. A stale binary that ignores the flag you just added looks
# exactly like a change that did not work, and cost an hour on 2026-09-21.
SUM_REMOTE=$("${SSH[@]}" "$A9" 'chmod +x /var/home/cabinet/cabinetos-frontend-dev; sha256sum /var/home/cabinet/cabinetos-frontend-dev | cut -d" " -f1')
if [ "$SUM_LOCAL" != "$SUM_REMOTE" ]; then
    echo "the binary on the console is not the one just built" >&2
    exit 1
fi
echo "deployed ${SUM_LOCAL:0:12}"

APP="/var/home/cabinet/cabinetos-frontend-dev --core-dir /var/home/cabinet/cores-dev"
[ -n "$GAME" ] && APP="$APP --launch $GAME"
[ -n "$MENU" ] && APP="$APP $MENU"
[ -n "$EXTRA" ] && APP="$APP $EXTRA"

"${SSH[@]}" "$A9" "printf '[Service]\nEnvironment=\"CABINETOS_APP=$APP\"\n' > /tmp/50-ui-loop.conf
                   echo $PW | sudo -S mkdir -p $DROPIN_DIR >/dev/null 2>&1
                   echo $PW | sudo -S cp /tmp/50-ui-loop.conf $DROPIN >/dev/null 2>&1
                   echo $PW | sudo -S systemctl daemon-reload >/dev/null 2>&1
                   echo $PW | sudo -S systemctl start cabinetos-session >/dev/null 2>&1"

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
"${SSH[@]}" "$A9" 'PID=$(ps -eo pid,args | grep "[c]abinetos-frontend-dev --core-dir" | grep -v gamescope | awk "{print \$1}" | head -1)
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
scp -q -i "$KEY" "$A9:/tmp/ui-loop.png" "$OUT" && echo "wrote $OUT"

say "it is on the television now — look at that rather than the PNG for anything
   involving colour, contrast or motion. The capture is for reading layout."

cat <<'NOTE'

   THE CONSOLE IS ON A HAND-BUILT BINARY UNTIL YOU RUN:  tools/ui-loop.sh --restore
   The drop-in is in /run, so a reboot also puts it back. It cannot outlive the
   power being cycled, which is the point.
NOTE
