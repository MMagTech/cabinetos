#!/bin/sh
# Starts a second read-only relay that hides or renames files: relay.sh <port> <args...>
port=$1; shift
nohup python3 ~/md/relay.py "$port" "$@" > ~/md/relay-$port.log 2>&1 &
echo $! > ~/md/relay-$port.pid
