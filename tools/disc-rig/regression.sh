#!/bin/sh
# The disc rig's regression pass (#162): one short script per system.
rig=$(cd "$(dirname "$0")" && pwd) || exit 1
r() { name=$1; rom=$2; shift 2; echo "== $name"; LIMIT=500 "$rig/run.sh" "reg-$name" "$rom" "$@" | grep -E "fault|refused|frame.*check|BOOT|put back"; }
r ps1 3634 "wait 300; startover 2; wait 300; state; startover 1; wait 300; load; resume; wait 60; check; quit"
r dc 3637 "wait 600; insert 2; wait 600; check; startover 1; wait 600; check; startover 2; wait 600; check; quit"
r 3do 3636 "wait 300; startover 2; wait 300; check; quit"
r saturn 3630 "wait 600; insert 2; wait 300; check; state; startover 1; wait 300; load; resume; wait 60; check; quit"
r segacd 3639 "wait 300; insert 2; wait 300; check; startover 1; wait 300; check; quit"
r midinsert 3637 "wait 600; insert 2; wait 30; play 3638; wait 600; check; quit"
