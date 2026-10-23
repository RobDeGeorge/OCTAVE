#!/usr/bin/env bash
# frontends/check_all.sh: run every frontend's own checks against ONE
# throwaway OCTAVE, then print a pass / fail table.
#
#   frontends/check_all.sh                     # OCTAVE_BIN or ./build/octave (a dev-tools build: --headless --dev)
#   frontends/check_all.sh --display :83       # also tap sunlight's controls on that X display (e.g. Xvfb :83)
#   frontends/check_all.sh --only console,sunlight --keep
#
# The backend runs --headless --dev (the simulated car, no QML) on a free
# loopback port, with a temp XDG_CONFIG_HOME and a few short test tracks made
# with ffmpeg (tagged, with covers) when ffmpeg is there. Nothing touches your
# own config, the network beyond package installs, or real hardware.
#
# glass is built first and served by the backend (--web-root) for its e2e.
#
# Environment: OCTAVE_BIN (the app), CONSOLE_PY (a python with the console's
# requirements; default: frontends/console/.venv, then python3), and the
# usual go / cargo / npm / cmake on PATH. A missing tool is a SKIP, not a FAIL.

set -u
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/.." && pwd)
bin=${OCTAVE_BIN:-$repo/build/octave}
display=""
only=""
keep=0
while [ $# -gt 0 ]; do
    case $1 in
        --display) display=$2; shift 2 ;;
        --only) only=",$2,"; shift 2 ;;
        --keep) keep=1; shift ;;
        -h|--help) sed -n "2,19p" "$0"; exit 0 ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
done

if [ ! -x "$bin" ]; then
    echo "no OCTAVE at $bin: build it (cmake --build build) or set OCTAVE_BIN" >&2
    exit 2
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/octave-check.XXXXXX")
mkdir -p "$work/cfg/OCTAVE" "$work/media" "$work/logs"
backend_pid=""

cleanup() {
    if [ -n "$backend_pid" ]; then
        kill "$backend_pid" 2>/dev/null
        wait "$backend_pid" 2>/dev/null
    fi
    if [ $keep -eq 1 ]; then
        echo "kept $work"
    else
        rm -rf "$work"
    fi
}
trap cleanup EXIT INT TERM

# ── test media ─────────────────────────────────────────────────────────────
make_track() {   # make_track FILE TITLE ARTIST ALBUM FREQ SECONDS HUE
    local cover="$work/cover-$7.png"
    ffmpeg -loglevel error -y -f lavfi -i "color=c=0x$7:s=300x300" -frames:v 1 "$cover" 2>/dev/null
    ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=$5:duration=$6" -i "$cover" -map 0 -map 1 \
        -c:a libmp3lame -q:a 6 -c:v png -disposition:v attached_pic -id3v2_version 3 \
        -metadata title="$2" -metadata artist="$3" -metadata album="$4" "$work/media/$1" 2>/dev/null ||
    ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=$5:duration=$6" \
        -metadata title="$2" -metadata artist="$3" -metadata album="$4" "$work/media/$1" 2>/dev/null
}
if command -v ffmpeg >/dev/null; then
    make_track track1.mp3 "Test One" "Check Band" "First Album" 330 40 e05020
    make_track track2.mp3 "Test Two" "Other Band" "First Album" 440 35 2060e0
    make_track track3.mp3 "Test Three" "Check Band" "Second Album" 550 30 20c060
fi
tracks=$(find "$work/media" -name '*.mp3' | wc -l)
printf '{"mediaFolder": "%s"}\n' "$work/media" > "$work/cfg/OCTAVE/settingsConfigure.json"

# ── the checks (helpers) ─────────────────────────────────────────────────────────────
names=(); results=(); times=(); notes=()
record() { names+=("$1"); results+=("$2"); times+=("$3"); notes+=("$4"); }
wanted() { [ -z "$only" ] || [[ $only == *",$1,"* ]]; }

run() {   # run NAME NOTE COMMAND...: PASS / FAIL by exit status, output in logs/NAME.log
    local name=$1 note=$2; shift 2
    local log="$work/logs/$name.log" start=$SECONDS
    echo "... $name"
    if ( "$@" ) > "$log" 2>&1; then   # a subshell: the checks cd
        record "$name" PASS $((SECONDS - start)) "$note"
    else
        record "$name" FAIL $((SECONDS - start)) "$note; see $log"
        tail -15 "$log" | sed 's/^/    /'
    fi
}
skip() { record "$1" SKIP 0 "$2"; echo "... $1 skipped: $2"; }

# glass is built before the backend starts, so the backend can serve that build
# (--web-root) and the e2e drives the real page
web_root=()
glass_built=0
if wanted glass && command -v npm >/dev/null; then
    glass_build() {
        cd "$here/glass" || return 1
        [ -d node_modules ] || npm ci --no-audit --no-fund || return 1
        npm run check && npm run build
    }
    run glass-build "npm run check, npm run build" glass_build
    if [ "${results[-1]}" = PASS ]; then
        glass_built=1
        web_root=(--web-root "$here/glass/dist")
    fi
fi

# ── the backend ────────────────────────────────────────────────────────────
# The binary is run exactly once, below, and always with the temp config:
# an OCTAVE started without XDG_CONFIG_HOME opens the real user's settings,
# black box and phone pairing.
port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
# HOME too: the export slots write to the Downloads folder, which follows HOME, not XDG_CONFIG_HOME
mkdir -p "$work/home"
env HOME="$work/home" XDG_CONFIG_HOME="$work/cfg" QT_QPA_PLATFORM=offscreen "$bin" --headless --dev --service "$port" "${web_root[@]}" \
    > "$work/logs/backend.log" 2>&1 &
backend_pid=$!
up=0
for _ in $(seq 1 60); do
    if python3 - "$port" <<'EOF' 2>/dev/null; then
import json, socket, sys
s = socket.create_connection(("127.0.0.1", int(sys.argv[1])), timeout=1)
s.sendall(b'{"op":"hello","id":1}\n')
sys.exit(0 if json.loads(s.makefile().readline()).get("ok") else 1)
EOF
        up=1; break
    fi
    if ! kill -0 "$backend_pid" 2>/dev/null; then break; fi
    sleep 0.5
done
if [ $up -eq 0 ]; then
    echo "OCTAVE did not come up on port $port; log:" >&2
    tail -20 "$work/logs/backend.log" >&2
    exit 1
fi
echo "OCTAVE headless on 127.0.0.1:$port, $tracks test track(s), work dir $work"
sleep 2   # let the simulated car connect

# the backend's HOME is ours, so tests may make it write exports
export OCTAVE_HOST=127.0.0.1 OCTAVE_PORT=$port OCTAVE_EXPORT_OK=1

# console: the live pilot tests; a run where every live test skipped is a failure
if wanted console; then
    py=${CONSOLE_PY:-}
    [ -z "$py" ] && [ -x "$here/console/.venv/bin/python" ] && py=$here/console/.venv/bin/python
    [ -z "$py" ] && py=python3
    if "$py" -c 'import textual, pytest, pytest_asyncio' 2>/dev/null; then
        console_tests() {
            cd "$here/console" && "$py" -m pytest -q -rs -p no:cacheprovider && ! grep -q "no OCTAVE service" "$work/logs/console.log"
        }
        run console "pytest, live" console_tests
    else
        skip console "no textual / pytest in $py (pip install -r frontends/console/requirements.txt, or set CONSOLE_PY)"
    fi
fi

# sunlight: go vet + go test, and the control read-back on an X display
if wanted sunlight; then
    if command -v go >/dev/null; then
        sunlight_tests() {
            cd "$here/sunlight" && go vet -tags novulkan ./... && go test -tags novulkan ./...
        }
        run sunlight "go vet, go test" sunlight_tests
        if [ -n "$display" ]; then
            sunlight_controls() {
                cd "$here/sunlight" && go build -tags novulkan -o "$work/sunlight" . &&
                    python3 verify_controls.py --display "$display" --port "$port" --bin "$work/sunlight"
            }
            if [ "$tracks" -gt 0 ]; then
                run sunlight-controls "taps on $display, read back through the SDK" sunlight_controls
            else
                skip sunlight-controls "no test tracks (needs ffmpeg) to play"
            fi
        else
            skip sunlight-controls "no --display given"
        fi
    else
        skip sunlight "no go"
    fi
fi

# glass: its end-to-end tests against the build the backend serves
if wanted glass; then
    if ! command -v npm >/dev/null; then
        skip glass "no npm"
    elif [ $glass_built -eq 0 ]; then
        skip glass-e2e "the build failed (see glass-build)"
    elif ! node -e 'process.exit(require(process.argv[1]).scripts?.e2e ? 0 : 1)' "$here/glass/package.json"; then
        skip glass-e2e "no e2e script"
    else
        glass_e2e() {
            # it exits 0 with a note when it finds nothing to test: here the build is served, so that is a failure
            cd "$here/glass" && npm run e2e && ! grep -qi "nothing to test" "$work/logs/glass-e2e.log"
        }
        run glass-e2e "npm run e2e on the served build" glass_e2e
    fi
fi

# retro-cluster: what CI checks (clippy with warnings as errors)
if wanted retro-cluster; then
    if command -v cargo >/dev/null; then
        retro_tests() { cd "$here/retro-cluster" && cargo clippy --quiet --release -- -D warnings; }
        run retro-cluster "cargo clippy -D warnings" retro_tests
    else
        skip retro-cluster "no cargo"
    fi
fi

# arcade: a warning-free build
if wanted arcade; then
    if command -v cmake >/dev/null && pkg-config --exists sdl2 2>/dev/null; then
        arcade_tests() {
            cmake -S "$here/arcade" -B "$work/arcade-build" -DCMAKE_C_FLAGS=-Werror >/dev/null &&
                cmake --build "$work/arcade-build" -j4
        }
        run arcade "cmake build, -Werror" arcade_tests
    else
        skip arcade "no cmake or SDL2"
    fi
fi

# ── the table ──────────────────────────────────────────────────────────────
echo
printf '%-18s %-5s %6s  %s\n' CHECK RESULT TIME NOTES
failed=0
for i in "${!names[@]}"; do
    printf '%-18s %-5s %5ss  %s\n' "${names[$i]}" "${results[$i]}" "${times[$i]}" "${notes[$i]}"
    [ "${results[$i]}" = FAIL ] && failed=$((failed + 1))
done
echo
if ! kill -0 "$backend_pid" 2>/dev/null; then
    echo "OCTAVE exited during the checks; its log: $work/logs/backend.log (kept)"
    keep=1
    failed=$((failed + 1))
fi
if [ $failed -gt 0 ]; then
    keep=1
    echo "$failed check(s) failed; logs kept in $work/logs"
    exit 1
fi
echo "all checks passed"
