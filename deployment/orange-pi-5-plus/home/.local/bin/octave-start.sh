#!/bin/bash
# OCTAVE launcher (Hyprland/Wayland). Adapted from ~/octave-cpp/scripts/octave-cpp-start.sh.
# No DISPLAY / MESA_GL_VERSION_OVERRIDE: Panthor does GLES 3.1 natively and Qt uses Wayland.

OCTAVE_CPP_DIR="$HOME/octave-cpp"
ANALOG_SINK="alsa_output.platform-sound.stereo-fallback"   # es8328 "Analog" card (3.5mm jack)
LOG="$HOME/.local/state/octave/console.log"

# Single instance
if pgrep -x octave >/dev/null; then
    exit 0
fi

cd "$OCTAVE_CPP_DIR/build" || exit 1

# Audio: route to the analog jack and make sure it isn't muted.
# The es8328 sink exposes a single "analog-output" port (no separate headphone port).
pactl set-default-sink "$ANALOG_SINK" || true
pactl set-sink-port "$ANALOG_SINK" analog-output || true
wpctl set-mute @DEFAULT_AUDIO_SINK@ 0 || true

mkdir -p "$HOME/.local/state/octave"

# Supervise: restart OCTAVE when it crashes or its UI watchdog aborts a hung
# GUI thread, instead of leaving a frozen screen. A clean exit (Shut Down,
# in-app restart, session logout) ends the loop. Gives up after 5 crashes
# within 3 minutes so a crash-on-start bug can't loop forever.
crashes=()
while true; do
    ./octave >> "$LOG" 2>&1
    status=$?
    [ "$status" -eq 0 ] && exit 0
    pgrep -x Hyprland >/dev/null || exit "$status"   # compositor gone: session is ending

    now=$(date +%s)
    crashes+=("$now")
    recent=()
    for t in "${crashes[@]}"; do (( now - t < 180 )) && recent+=("$t"); done
    crashes=("${recent[@]}")
    if (( ${#crashes[@]} >= 5 )); then
        echo "$(date '+%F %T') | octave-start.sh: OCTAVE exited with status $status, ${#crashes[@]} crashes in 3 min, giving up" >> "$LOG"
        notify-send -u critical "OCTAVE keeps crashing" "Gave up after ${#crashes[@]} restarts; see $LOG" 2>/dev/null
        exit "$status"
    fi
    echo "$(date '+%F %T') | octave-start.sh: OCTAVE exited with status $status, restarting (${#crashes[@]} in 3 min)" >> "$LOG"
    sleep 2
done
