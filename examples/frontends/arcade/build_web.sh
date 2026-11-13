#!/usr/bin/env bash
# The arcade as a web frontend: the same C, compiled by Emscripten (SDL2
# from its ports; the first build downloads and compiles it), with
# src/link_web.c (the page's WebSocket) in src/link_tcp.c's place.
#
#   ./build_web.sh            ->  dist-web/{index.html, arcade.js, arcade.wasm, octave-frontend.json}
#   octave --headless --sim --service --web-root examples/frontends/arcade/dist-web
#
# Needs emcc on PATH, or an emsdk at $EMSDK or ~/emsdk.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

if ! command -v emcc >/dev/null 2>&1; then
  env_script="${EMSDK:-$HOME/emsdk}/emsdk_env.sh"
  if [ ! -f "$env_script" ]; then
    echo "build_web.sh: no emcc on PATH and no emsdk at ${EMSDK:-$HOME/emsdk} (https://emscripten.org/docs/getting_started/downloads.html)" >&2
    exit 1
  fi
  EMSDK_QUIET=1 . "$env_script" >/dev/null
fi

out=dist-web
obj=build-web
rm -rf "$out"
mkdir -p "$out" "$obj"

sdl=(-sUSE_SDL=2)
inc=(-Ithird_party/cjson -Ithird_party/stb)
# ours with every warning on, the vendored libraries with none (as in CMakeLists.txt)
objects=()
for f in main link link_web font ui art; do
  emcc -std=c11 -O2 -Wall -Wextra -Wpedantic \
       "${sdl[@]}" "${inc[@]}" -c "src/$f.c" -o "$obj/$f.o"
  objects+=("$obj/$f.o")
done
emcc -std=c11 -O2 -w "${inc[@]}" -c third_party/cjson/cJSON.c -o "$obj/cJSON.o"
emcc -std=c11 -O2 -w "${inc[@]}" -c src/stb_impl.c -o "$obj/stb_impl.o"

# One thread, no socket emulation, no file system, no eval (OCTAVE serves
# the page with script-src 'self' 'wasm-unsafe-eval'); the .wasm is found
# next to arcade.js, wherever the page's own address is.
emcc -O2 "${objects[@]}" "$obj/cJSON.o" "$obj/stb_impl.o" -o "$out/arcade.js" \
     "${sdl[@]}" \
     -sENVIRONMENT=web \
     -sALLOW_MEMORY_GROWTH=1 \
     -sFILESYSTEM=0 \
     -sDYNAMIC_EXECUTION=0 \
     -sEXPORTED_FUNCTIONS=_main,_malloc,_free \
     --pre-js web/pre.js

cp web/index.html "$out/index.html"
cp preview.png "$out/preview.png"
chmod 644 "$out"/*

# The web manifest: the native one's version, description, protocol and scopes
python3 - "$out/octave-frontend.json" <<'PY'
import json, sys
native = json.load(open("octave-frontend.json"))
web = {
    "id": native["id"] + ".web",
    "name": native["name"],
    "version": native["version"],
    "description": native["description"],
    "preview": native["preview"],
    "protocol": native["protocol"],
    "scopes": native["scopes"],
    "web": {"root": "."},
}
with open(sys.argv[1], "w") as f:
    json.dump(web, f, indent=2)
    f.write("\n")
PY

ls -l "$out"
