#!/bin/sh
# tile.sh <out.png> <cols> <img>... : the images in order, scaled to 352x240, labelled
out=$1; cols=$2; shift 2
n=$#; rows=$(( (n + cols - 1) / cols ))
args=""; i=0; filt=""
for f in "$@"; do
  args="$args -i $f"
  lab=$(basename "$f" .png | sed 's/disc-//')
  filt="${filt}[$i:v]scale=352:240,drawtext=text='$lab':x=4:y=4:fontcolor=yellow:fontsize=16[v$i];"
  i=$((i+1))
done
ins=""; j=0; while [ "$j" -lt "$n" ]; do ins="${ins}[v$j]"; j=$((j+1)); done
# pad to a full grid with black
while [ "$j" -lt $((rows*cols)) ]; do filt="${filt}color=black:s=352x240:d=1[v$j];"; ins="${ins}[v$j]"; j=$((j+1)); done
# $args is split on purpose: it holds one -i and one file per image.
# shellcheck disable=SC2086
ffmpeg -loglevel error -y $args -filter_complex "${filt}${ins}xstack=inputs=$((rows*cols)):layout=$(python3 -c "
c=$cols;r=$rows
print('|'.join(f'{(k%c)*352}_{(k//c)*240}' for k in range(c*r)))")" -frames:v 1 "$out"
