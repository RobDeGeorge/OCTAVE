#!/usr/bin/env bash
# Build and launch the native app from this checkout (no installed app copy).
set -Eeuo pipefail

OCTAVE_CHECKOUT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# Dropbox also syncs CMake caches; keep each computer's generated files separate.
OCTAVE_BUILD="$OCTAVE_CHECKOUT/build-desktop-$(uname -n)"
OCTAVE_STATE="${XDG_STATE_HOME:-$HOME/.local/state}/octave"
mkdir -p -- "$OCTAVE_STATE"
OCTAVE_LOG="$OCTAVE_STATE/desktop-launch.log"

notify() {
    if command -v notify-send >/dev/null 2>&1; then
        notify-send --app-name=OCTAVE "OCTAVE" "$1" || true
    fi
}

# Multiple menu clicks must not start simultaneous builds in the same directory.
exec 9>"$OCTAVE_STATE/desktop-build.lock"
if ! flock -n 9; then
    notify "A build is already running. OCTAVE will open when it finishes."
    exit 0
fi
exec >"$OCTAVE_LOG" 2>&1
trap 'notify "Launch failed. Details: $OCTAVE_LOG"' ERR
cd -- "$OCTAVE_CHECKOUT"
printf 'Launching native OCTAVE from %s\n' "$OCTAVE_CHECKOUT"

if [[ ! -f "$OCTAVE_BUILD/CMakeCache.txt" ]]; then
    cmake -S "$OCTAVE_CHECKOUT" -B "$OCTAVE_BUILD" -DCMAKE_BUILD_TYPE=Release
fi
# CMake automatically regenerates the existing build when its inputs change.
# Bound parallelism so launching from the desktop does not exhaust memory.
cmake --build "$OCTAVE_BUILD" --target octave --parallel 4

flock -u 9
exec 9>&-
exec "$OCTAVE_BUILD/octave" "$@"
