#!/bin/bash
# Makes tests/data/stream-test.h264: the dev-only video source for the
# service's media streams (sim_video in src/dev/devruntime.cpp, and
# tst_streamhub). Raw Annex-B H.264, constrained baseline, 30 fps, a
# keyframe every second: 2 s of 640x360 then 2 s of 1280x720, so a client
# sees a config change mid-stream exactly as when the phone rotates.
# Needs ffmpeg with libx264. Run from the repo root.
set -euo pipefail
out=${1:-tests/data/stream-test.h264}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$(dirname "$out")"
enc=(-c:v libx264 -profile:v baseline -level 3.1 -pix_fmt yuv420p -preset veryfast -tune zerolatency
     -g 30 -keyint_min 30 -sc_threshold 0 -b:v 300k -bsf:v h264_mp4toannexb -an -f h264)
ffmpeg -loglevel error -y -f lavfi -i "testsrc2=size=640x360:rate=30:duration=2" "${enc[@]}" "$tmp/a.h264"
ffmpeg -loglevel error -y -f lavfi -i "testsrc=size=1280x720:rate=30:duration=2" "${enc[@]}" "$tmp/b.h264"
cat "$tmp/a.h264" "$tmp/b.h264" > "$out"
echo "$out: $(stat -c %s "$out") bytes"
