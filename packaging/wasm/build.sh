#!/usr/bin/env bash
# Build OCTAVE for the browser (Qt for WebAssembly) and, optionally, stage the
# site files. Used by .github/workflows/wiki-pages.yml and runnable locally.
#
#   QT_WASM    Qt wasm kit, e.g. ~/Qt/6.12.0/wasm_multithread   (required)
#   QT_HOST    matching desktop Qt for the host tools (qsb, balsam, moc ...),
#              e.g. ~/Qt/6.12.0/gcc_64; defaults to $QT_HOST_PATH   (required)
#   BUILD_DIR  CMake build dir                          (default: build-wasm)
#   OUT_DIR    if set, copy the site files there: octave.js, octave.wasm,
#              octave.data, qtloader.js and this folder's index.html,
#              coi-serviceworker.js, LICENSE.coi-serviceworker
#   EMSDK      emsdk checkout to source if emcc is not already on PATH
#              (default: ~/emsdk). Qt 6.12 wants emscripten 5.0.5.
#   JOBS       parallel build jobs (default: nproc)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
src="$(cd "$here/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$src/build-wasm}"
QT_HOST="${QT_HOST:-${QT_HOST_PATH:-}}"
EMSDK="${EMSDK:-$HOME/emsdk}"
JOBS="${JOBS:-$(nproc)}"

[ -n "${QT_WASM:-}" ] || { echo "build.sh: set QT_WASM to the Qt wasm kit (e.g. ~/Qt/6.12.0/wasm_multithread)" >&2; exit 2; }
[ -n "$QT_HOST" ] || { echo "build.sh: set QT_HOST (or QT_HOST_PATH) to the matching desktop Qt" >&2; exit 2; }
[ -x "$QT_WASM/bin/qt-cmake" ] || { echo "build.sh: $QT_WASM/bin/qt-cmake not found" >&2; exit 2; }

if ! command -v emcc >/dev/null 2>&1; then
    [ -f "$EMSDK/emsdk_env.sh" ] || { echo "build.sh: emcc not on PATH and no $EMSDK/emsdk_env.sh" >&2; exit 2; }
    # shellcheck disable=SC1091
    source "$EMSDK/emsdk_env.sh" >/dev/null
fi
echo "emcc: $(emcc --version | head -1)"

"$QT_WASM/bin/qt-cmake" -S "$src" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH="$QT_HOST" \
    -DOCTAVE_ENABLE_DOWNLOADS=OFF \
    -DOCTAVE_BUILD_TESTS=OFF \
    -DOCTAVE_DEV_TOOLS=OFF
cmake --build "$BUILD_DIR" -j "$JOBS"

for f in octave.js octave.wasm octave.data qtloader.js; do
    [ -f "$BUILD_DIR/$f" ] || { echo "build.sh: $BUILD_DIR/$f missing after build" >&2; exit 1; }
done
ls -l "$BUILD_DIR"/octave.js "$BUILD_DIR"/octave.wasm "$BUILD_DIR"/octave.data

if [ -n "${OUT_DIR:-}" ]; then
    mkdir -p "$OUT_DIR"
    cp "$BUILD_DIR"/octave.js "$BUILD_DIR"/octave.wasm "$BUILD_DIR"/octave.data "$BUILD_DIR"/qtloader.js "$OUT_DIR/"
    cp "$here"/index.html "$here"/coi-serviceworker.js "$here"/LICENSE.coi-serviceworker "$OUT_DIR/"
    echo "staged site in $OUT_DIR"
fi
