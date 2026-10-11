#!/bin/sh
# The disc rig's driver (#162), on the A9. Headless, scratch root, through
# the read-only relay. usage: run.sh <name> <romId> "<script>" [frontend args...]
name=$1; rom=$2; script=$3; shift 3
mkdir -p ~/md/logs
[ "${KEEPDISC:-0}" = 1 ] || rm -f ${ROOT:-$HOME/md/root}/users/*/discs.json
rm -f /tmp/disc-*
cd ~/md/frontend || exit 1
export CABINETOS_ROMM=${ROMM:-127.0.0.1:16006} SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
./build/cabinetos-frontend --storage-root ${ROOT:-$HOME/md/root} --render-size 1920x1080 \
  --launch "$rom" --launch-after 0 --disc-script "$script" "$@" > ~/md/logs/$name.log 2>&1 &
pid=$!
limit=${LIMIT:-900}
i=0
while [ $i -lt $limit ]; do
  sleep 1; i=$((i+1))
  kill -0 $pid 2>/dev/null || break
  grep -q "disc-script\] back on Home" ~/md/logs/$name.log && break
  grep -q "\[launch\] failed" ~/md/logs/$name.log && break
done
sleep 1
kill -9 $pid 2>/dev/null
echo "ran ${i}s"
grep -aE "\[disc|\[disc-script\]|\[launch\]|\[state\]|\[core\] loaded|REFUSED|\[save\] restored|failed" ~/md/logs/$name.log | grep -v "^\[core\] \[" | head -120
