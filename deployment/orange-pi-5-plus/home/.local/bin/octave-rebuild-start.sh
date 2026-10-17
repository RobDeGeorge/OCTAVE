#!/bin/bash
# OCTAVE menu launcher (wofi / Mod+D, "OCTAVE (Rebuild)": octave-rebuild.desktop).
# Always rebuilds ~/octave-cpp/build first, then starts OCTAVE through
# octave-start.sh. If OCTAVE is already running and the build produced a new
# binary, the running instance is stopped and relaunched on it. If the build
# fails, the existing binary keeps running (or is started) and a notification
# says why. Boot (hyprland.conf exec-once) still uses octave-start.sh directly
# so startup is never held up by a compile.

BUILD_DIR="$HOME/octave-cpp/build"
BIN="$BUILD_DIR/octave"
START="$HOME/.local/bin/octave-start.sh"
LOG="$HOME/.local/state/octave/rebuild.log"
LOCK="${XDG_RUNTIME_DIR:-/tmp}/octave-rebuild.lock"

mkdir -p "$(dirname "$LOG")"

# One rebuild at a time: a second Mod+D while building just says so.
exec 9>"$LOCK"
if ! flock -n 9; then
    notify-send -a OCTAVE "OCTAVE" "Already rebuilding…" 2>/dev/null
    exit 0
fi

notify() {   # notify <urgency> <timeout ms> <body>; replaces the previous bubble
    local id
    id=$(notify-send -a OCTAVE -p ${NID:+-r "$NID"} -u "$1" -t "$2" "OCTAVE" "$3" 2>/dev/null)
    [ -n "$id" ] && NID=$id
}

running() { pgrep -x octave >/dev/null; }

launch() {   # start OCTAVE detached from this script (no-op if already running)
    running || setsid -f "$START" >/dev/null 2>&1
}

before=$(stat -c %Y "$BIN" 2>/dev/null)
notify normal 0 "Rebuilding… (see $LOG)"
{
    echo "===== $(date '+%F %T') rebuild ($(git -C "$BUILD_DIR/.." log -1 --format='%h %s' 2>/dev/null))"
    nice -n 5 cmake --build "$BUILD_DIR" --parallel "$(nproc)"
} >> "$LOG" 2>&1
status=$?

if [ "$status" -ne 0 ]; then
    echo "===== build FAILED (status $status)" >> "$LOG"
    notify critical 15000 "Build failed — running the previous build. See $LOG"
    launch
    exit 1
fi
after=$(stat -c %Y "$BIN" 2>/dev/null)
echo "===== build OK" >> "$LOG"

if running && [ "$before" != "$after" ]; then
    notify normal 4000 "Built. Restarting on the new build…"
    # SIGTERM = clean quit (exit 0), which also ends octave-start.sh's
    # supervise loop, so it won't restart the old one behind our back.
    pkill -TERM -x octave
    for _ in $(seq 1 100); do running || break; sleep 0.1; done
    if running; then
        pkill -KILL -x octave
        sleep 0.5
    fi
    # Let the old supervisor finish exiting before its single-instance check races ours
    for _ in $(seq 1 30); do pgrep -f "$START" >/dev/null || break; sleep 0.1; done
elif running; then
    notify normal 3000 "Already up to date and running"
else
    notify normal 3000 "Built. Starting…"
fi
launch
