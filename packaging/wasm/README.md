# packaging/wasm

The browser host for the Qt for WebAssembly build of OCTAVE, published at
`robdegeorge.com/OCTAVE/` (the wiki sits under it at `/OCTAVE/wiki/`).

The CMake wasm build (`build-wasm/`) produces `octave.js`, `octave.wasm`,
`octave.data` and Qt's generic `qtloader.js` + `octave.html`. The files here
replace that generated page when the build is staged for the website:

- `index.html`: the OCTAVE-branded page. It fetches `octave.wasm` and
  `octave.data` itself to show download progress, compiles the module, then
  calls Qt's `qtLoad()` (the Qt 6.12 `qtloader.js` API) with the module and
  the data package pre-supplied, shows the app full-viewport once Qt reports
  `onLoaded`, and shows an error panel if a download, compile or the app itself
  fails. The page expects `octave.js`, `octave.wasm`, `octave.data` and
  `qtloader.js` next to it, copied verbatim from the build. Nothing else from
  the build is needed (not `octave.html`, not `qtlogo.svg`).
- `coi-serviceworker.js`: [gzuidhof/coi-serviceworker](https://github.com/gzuidhof/coi-serviceworker)
  0.1.7, MIT (`LICENSE.coi-serviceworker`), vendored unchanged from the npm
  package. The threaded build needs cross-origin isolation (COOP/COEP headers),
  which GitHub Pages cannot send; the service worker adds them to every
  response and reloads the page once on first visit. It must be the first
  script on the page. A server that already sends the headers (see below) makes
  it a no-op.

## Building

`build.sh` configures and builds the wasm target and, with `OUT_DIR` set,
stages everything the site root needs:

```bash
QT_WASM=~/Qt/6.12.0/wasm_multithread QT_HOST=~/Qt/6.12.0/gcc_64 \
OUT_DIR=/tmp/octave-site packaging/wasm/build.sh
```

Variables: `QT_WASM` (the wasm kit), `QT_HOST` (a matching desktop Qt with the
same modules, for the host tools; falls back to `QT_HOST_PATH`), `BUILD_DIR`
(default `build-wasm`), `OUT_DIR`, `EMSDK` (sourced when `emcc` is not on
`PATH`, default `~/emsdk`; Qt 6.12 wants emscripten 5.0.5) and `JOBS`.
Kits: `aqt install-qt all_os wasm 6.12.0 wasm_multithread` and
`aqt install-qt linux desktop 6.12.0 linux_gcc_64`, both with modules
`qt5compat qtmultimedia qtquick3d qtquicktimeline qtwebsockets qtshadertools`.

`.github/workflows/wiki-pages.yml` runs the same script on every push to
`main` that touches `src/`, `frontend/`, `CMakeLists.txt`, `packaging/wasm/`
or `wiki/`, with the Qt kits and emsdk cached between runs, and deploys the
result to GitHub Pages with the wiki under `wiki/`.

## Testing locally

Threads need the headers, so a plain `python -m http.server` is not enough
on first load. Copy the build output next to the page and serve it with any
static server that sends `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`, for example:

```bash
mkdir -p /tmp/octave-site
cp packaging/wasm/index.html packaging/wasm/coi-serviceworker.js /tmp/octave-site/
cp build-wasm/octave.js build-wasm/octave.wasm build-wasm/octave.data build-wasm/qtloader.js /tmp/octave-site/
python3 -c 'import http.server as h,functools as f
class H(h.SimpleHTTPRequestHandler):
    def end_headers(s):
        s.send_header("Cross-Origin-Opener-Policy","same-origin"); s.send_header("Cross-Origin-Embedder-Policy","require-corp"); super().end_headers()
h.ThreadingHTTPServer(("127.0.0.1",8080),f.partial(H,directory="/tmp/octave-site")).serve_forever()'
```

Then open `http://127.0.0.1:8080/`. The demo runs the simulated car
(`--dev` style carsim) and talks to no hardware.
