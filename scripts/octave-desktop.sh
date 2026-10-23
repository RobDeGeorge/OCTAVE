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
notify "Checking the Dropbox source and building changes…"

if [[ ! -f "$OCTAVE_BUILD/CMakeCache.txt" ]]; then
    # The Qt package registry may otherwise select an installed Android kit.
    OCTAVE_QT_CMAKE="$(/usr/bin/qmake6 -query QT_INSTALL_LIBS)/cmake/Qt6"
    cmake -S "$OCTAVE_CHECKOUT" -B "$OCTAVE_BUILD" \
        -DCMAKE_BUILD_TYPE=Release -DQt6_DIR="$OCTAVE_QT_CMAKE"
fi
# CMake automatically regenerates the existing build when its inputs change.
# Bound parallelism so launching from the desktop does not exhaust memory.
if ! cmake --build "$OCTAVE_BUILD" --target octave --parallel 4; then
    # A regenerate that raced a Dropbox sync can leave makefiles with no sources
    # ("undefined reference to main"); regenerate from scratch and retry once.
    printf 'Build failed; regenerating build files and retrying\n'
    cmake -S "$OCTAVE_CHECKOUT" -B "$OCTAVE_BUILD"
    cmake --build "$OCTAVE_BUILD" --target octave --parallel 4
fi

flock -u 9
exec 9>&-

# OCTAVE_FRESH=1 (the "OCTAVE (Fresh)" menu entry): a first launch every
# time. HOME and the XDG dirs point at a new empty folder, so there are no
# settings, no music library, no trip history and no logs from before; the
# folder is deleted when the app exits. The build above still uses the real
# home (its caches).
if [[ "${OCTAVE_FRESH:-0}" == 1 ]]; then
    fresh="$(mktemp -d -t octave-fresh.XXXXXX)"
    trap 'rm -rf -- "$fresh"' EXIT
    trap - ERR
    printf 'Fresh launch in %s\n' "$fresh"
    HOME="$fresh" XDG_CONFIG_HOME="$fresh/.config" XDG_DATA_HOME="$fresh/.local/share" \
        XDG_CACHE_HOME="$fresh/.cache" XDG_STATE_HOME="$fresh/.local/state" \
        "$OCTAVE_BUILD/octave" "$@" || true
    exit 0
fi
exec "$OCTAVE_BUILD/octave" "$@"
