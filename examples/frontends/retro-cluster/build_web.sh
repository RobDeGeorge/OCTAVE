#!/bin/sh
# Builds the cluster as a web frontend (wasm32-unknown-unknown) into
# dist-web/: index.html, boot.js, retro_cluster.js (wasm-bindgen's loader),
# retro_cluster_bg.wasm and the web manifest. Serve it from OCTAVE
# (--web-root dist-web) or install the folder.
#
# Needs the wasm32 target (rustup target add wasm32-unknown-unknown; Arch:
# the rust-wasm package) and the wasm-bindgen program of the version in
# Cargo.lock (cargo install wasm-bindgen-cli --version <that>, or
# WASM_BINDGEN=/path/to/it). wasm-opt (binaryen) makes it smaller when there.
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

# the loader and the program's glue are one version, or the page fails at start
bindgen=${WASM_BINDGEN:-wasm-bindgen}
want=$(awk '/^name = "wasm-bindgen"$/ { getline; gsub(/[^0-9.]/, ""); print; exit }' Cargo.lock)
have=$("$bindgen" --version 2>/dev/null | awk '{ print $2 }') || have=
if [ "$have" != "$want" ]; then
    echo "build_web.sh: needs wasm-bindgen $want (found: ${have:-none}): cargo install wasm-bindgen-cli --version $want --locked" >&2
    exit 1
fi

# for size (every visitor downloads it): no symbol names, optimised for size
CARGO_PROFILE_RELEASE_STRIP=true CARGO_PROFILE_RELEASE_OPT_LEVEL=s CARGO_PROFILE_RELEASE_LTO=fat \
    cargo build --release --locked --target wasm32-unknown-unknown
rm -rf "$out"
mkdir -p "$out"
"$bindgen" --target web --no-typescript --out-dir "$out" --out-name retro_cluster \
    target/wasm32-unknown-unknown/release/retro-cluster.wasm
if command -v wasm-opt >/dev/null 2>&1; then
    wasm-opt -Oz -o "$out/retro_cluster_bg.wasm" "$out/retro_cluster_bg.wasm"
fi
cp web/index.html web/boot.js web/octave-frontend.json preview.png "$out/"

size=$(wc -c < "$out/retro_cluster_bg.wasm")
echo "$out/retro_cluster_bg.wasm: $size bytes ($(gzip -9 -c "$out/retro_cluster_bg.wasm" | wc -c) gzipped)"
