#!/bin/bash
# Makes dev/test.h264: an Annex-B H.264 stream for the video player's dev
# page, two resolutions back to back (a resolution change mid-stream, like a
# phone rotating). Baseline, no B-frames, a keyframe every 30 frames.
set -e
cd "$(dirname "$0")"
enc() { ffmpeg -loglevel error -y -f lavfi -i "testsrc2=size=$1:rate=30:duration=4" -c:v libx264 -profile:v baseline -bf 0 -g 30 -keyint_min 30 -sc_threshold 0 -pix_fmt yuv420p -f h264 "$2"; }
enc 640x360 part1.h264
enc 480x640 part2.h264
cat part1.h264 part2.h264 part1.h264 > test.h264
rm -f part1.h264 part2.h264
ls -la test.h264
