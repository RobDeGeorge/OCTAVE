#!/bin/sh
# Builds Sunlight as a web frontend (GOOS=js GOARCH=wasm) into dist-web/:
# index.html, boot.js, wasm_exec.js (Go's own loader), sunlight.wasm and the
# web manifest. Serve it from OCTAVE (--web-root dist-web) or install the folder.
set -eu
cd "$(dirname "$0")"
out=dist-web

# the web manifest (web/) says what the native one says, bar the id and how it starts
for key in name version description protocol scopes; do
    a=$(grep "\"$key\":" octave-frontend.json)
    b=$(grep "\"$key\":" web/octave-frontend.json)
    if [ "$a" != "$b" ]; then
        echo "build_web.sh: \"$key\" differs between octave-frontend.json and web/octave-frontend.json" >&2
        exit 1
    fi
done

goroot=$(go env GOROOT)
loader=$goroot/lib/wasm/wasm_exec.js          # Go 1.24 and newer
[ -f "$loader" ] || loader=$goroot/misc/wasm/wasm_exec.js
[ -f "$loader" ] || { echo "build_web.sh: no wasm_exec.js under $goroot" >&2; exit 1; }

rm -rf "$out"
mkdir -p "$out"
GOOS=js GOARCH=wasm go build -trimpath -ldflags="-s -w" -o "$out/sunlight.wasm" .
cp "$loader" "$out/wasm_exec.js"   # must be the building toolchain's: the two change together
cp web/index.html web/boot.js web/octave-frontend.json preview.png "$out/"

size=$(wc -c < "$out/sunlight.wasm")
echo "$out/sunlight.wasm: $size bytes ($(gzip -9 -c "$out/sunlight.wasm" | wc -c) gzipped)"
