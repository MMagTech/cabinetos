#!/bin/sh
# Starts a second read-only relay that hides or renames files: relay.sh <port> <args...>
# Its server is CABINETOS_RELAY_UP, as for the first (README.md).
: "${CABINETOS_RELAY_UP:?set it to http://<your RomM server>:<port>}"
rig=$(cd "$(dirname "$0")" && pwd) || exit 1
port=$1; shift
nohup python3 "$rig/relay.py" "$port" "$@" > "$rig/relay-$port.log" 2>&1 &
echo $! > "$rig/relay-$port.pid"
