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
# one that cannot compile anything. Nothing builds on the Mac; the VM builds and
# the A9 displays, which is the arrangement docs/NEXT-SESSION.md describes.
#
# Usage:
#   tools/ui-loop.sh                     Home, built and shown and captured
#   tools/ui-loop.sh --shot look.png     ...and write the picture here
#   tools/ui-loop.sh --game 305          launch a game (a SNES one, cheap to boot)
#   tools/ui-loop.sh --game 305 --menu   ...and open the pause menu over it
#   tools/ui-loop.sh --no-build          deploy and capture what is already built
#   tools/ui-loop.sh --restore           put the console back on the image
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
DROPIN=/etc/systemd/system/cabinetos-session.service.d/50-ui-loop.conf
PW=cabinet

BUILD=1
GAME=""
MENU=""
SHOT=""
RESTORE=0

while [ $# -gt 0 ]; do
    case "$1" in
        --no-build) BUILD=0 ;;
        --game)     GAME="$2"; shift ;;
        --menu)     MENU="--overlay 400" ;;
        --shot)     SHOT="$2"; shift ;;
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
    "${SSH[@]}" "$A9" "echo $PW | sudo -S rm -f $DROPIN >/dev/null 2>&1
                       echo $PW | sudo -S systemctl daemon-reload >/dev/null 2>&1
                       echo $PW | sudo -S systemctl restart cabinetos-session >/dev/null 2>&1
                       sleep 14
                       echo \"session: \$(systemctl is-active cabinetos-session)\"
                       echo \"drop-ins: \$(ls /etc/systemd/system/cabinetos-session.service.d/ 2>/dev/null | wc -l)\"
                       ps -eo args | grep '[c]abinetos-frontend' | grep -v gamescope | head -1"
    exit 0
fi

# --- build, on the VM ------------------------------------------------------
if [ "$BUILD" -eq 1 ]; then
    say "building on the VM"
    rsync -az -e "ssh -i $KEY -o ConnectTimeout=20" "$ROOT/frontend/src/" "$VM:~/frontend/src/" || exit 1
    "${SSH[@]}" "$VM" 'cd ~/frontend && podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make 2>&1 | grep -E "error|Error|warning: unused|built " | head -20'
    # `make` above prints "built ..." on success. A compile error prints an
    # error line and no "built", so check for the artifact rather than trusting
    # a grep of the log.
    "${SSH[@]}" "$VM" 'test -f ~/frontend/build/cabinetos-frontend' || { echo "build failed"; exit 1; }
fi

# --- deploy, to the A9 -----------------------------------------------------
say "deploying to the console"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
scp -q -i "$KEY" "$VM:~/frontend/build/cabinetos-frontend" "$TMP/fe" || exit 1
SUM_LOCAL=$(shasum -a 256 "$TMP/fe" | awk '{print $1}')

# The binary is in use, so the session has to stop before it can be replaced.
"${SSH[@]}" "$A9" "echo $PW | sudo -S systemctl stop cabinetos-session >/dev/null 2>&1"
scp -q -i "$KEY" "$TMP/fe" "$A9:/var/home/cabinet/cabinetos-frontend-dev" || exit 1

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

"${SSH[@]}" "$A9" "printf '[Service]\nEnvironment=\"CABINETOS_APP=$APP\"\n' > /tmp/50-ui-loop.conf
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
