#!/usr/bin/env bash
# Prove a packaged OCTAVE ships the simulator: run it with
# `--headless --sim --smoke` (no window, a throwaway config, exits 0 after
# 5 s) and require the log lines that only a working simulated car and
# service produce. CI runs this on the AppImage (x86_64 and aarch64, each on
# its own runner) and the macOS app; the Windows job does the same in
# PowerShell. Usage: packaging/smoke-sim.sh <binary or AppImage> [extra args]
#
# A release that ships without the simulator (built with the wrong option,
# a missing plugin, a frontend-less bundle) fails here, before it is
# attached to the release.
set -euo pipefail

BIN="$1"
shift
[ -x "$BIN" ] || { echo "::error::$BIN is not executable"; exit 1; }

# Nothing of the runner's own config is read or written
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
export HOME="$TMP" XDG_CONFIG_HOME="$TMP" XDG_DATA_HOME="$TMP" XDG_CACHE_HOME="$TMP"
# ... nor of its Qt. install-qt-action exports QT_PLUGIN_PATH and
# QML2_IMPORT_PATH; 0.9.6-rc1's AppImage found there the offscreen platform
# plugin it did not carry, so this step passed and --headless aborted on
# every other machine. The app picks the offscreen platform itself, as it
# does for a user.
unset QT_PLUGIN_PATH QML2_IMPORT_PATH QML_IMPORT_PATH QT_QPA_PLATFORM QT_QPA_PLATFORM_PLUGIN_PATH \
      LD_LIBRARY_PATH DYLD_LIBRARY_PATH DYLD_FRAMEWORK_PATH
ARGS=(--headless --sim --smoke --service 0 "$@")
case "$BIN" in
    *.AppImage) ARGS=(--appimage-extract-and-run "${ARGS[@]}") ;;   # no FUSE needed
esac

echo "==> $BIN ${ARGS[*]}"
set +e
if command -v timeout > /dev/null 2>&1; then
    timeout 90 "$BIN" "${ARGS[@]}" > "$TMP/out.log" 2>&1
    code=$?
else
    # macOS has no timeout(1) (0.9.6-rc2's macOS job stopped here with 127).
    # perl's alarm outlives the exec; SIGALRM ends the app with 128 + 14.
    perl -e 'alarm shift; exec @ARGV or exit 127' 90 "$BIN" "${ARGS[@]}" > "$TMP/out.log" 2>&1
    code=$?
    [ "$code" -eq 142 ] && code=124
fi
set -e
if [ "$code" -ne 0 ]; then
    echo "::error::$(basename "$BIN") --headless --sim --smoke exited with $code (124 = did not exit within 90 s)"
    tail -60 "$TMP/out.log"
    exit 1
fi
# The lines main.cpp, src/sim and the ELM327 stack log when the simulator is really there
for want in "Simulated car on sim://elm327" "ELM327 initialised, vehicle answered" "objects served on port" "Smoke test: headless core up"; do
    if ! grep -q -- "$want" "$TMP/out.log"; then
        echo "::error::the packaged app did not log \"$want\": the simulator or the service is missing"
        tail -60 "$TMP/out.log"
        exit 1
    fi
done
echo "Smoke test passed: the packaged app runs the simulated car and the service with no window"
