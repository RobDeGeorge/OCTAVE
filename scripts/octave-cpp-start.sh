#!/bin/bash
# OCTAVE C++ start script
# Launches the native C++ build from the local clone at ~/octave-cpp
# (cloned from the Dropbox repo; does not depend on Dropbox running).
# The Python launcher is still /home/rob/Roxy/octave-start.sh — see the
# i3 config for the one-line switch back.

OCTAVE_CPP_DIR="/home/rob/octave-cpp"

cd "$OCTAVE_CPP_DIR/build" || exit 1

export DISPLAY=:0
export MESA_GL_VERSION_OVERRIDE=3.3

# Force aux/headphone output on - unmute and set port before launching
wpctl set-mute @DEFAULT_AUDIO_SINK@ 0
pactl set-sink-port alsa_output.platform-es8388-sound.stereo-fallback analog-output-headphones

exec ./octave >> /home/rob/Roxy/octave-cpp-console.log 2>&1
