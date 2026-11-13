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
on first load. Stage the site with `OUT_DIR` (above) and serve it with
`serve.py`, which sends `Cross-Origin-Opener-Policy: same-origin`,
`Cross-Origin-Embedder-Policy: require-corp` and, as GitHub Pages does,
`Access-Control-Allow-Origin: *`:

```bash
python3 packaging/wasm/serve.py /tmp/octave-site 8080
```

Then open `http://127.0.0.1:8080/`. The demo runs the simulated car
(`--dev` style carsim) and talks to no hardware.

## Frontends on the demo page

`index.html` lists the site's web frontends (`frontends/catalog.json`) in
Settings > Device > Frontends, as installed frontends are listed on a head
unit. The page reads the catalog into `globalThis.octaveFrontendCatalog`
before the core starts, and `src/platform/wasm/webfrontends.{h,cpp}` puts a
folder with each one's manifest into the frontends root in the page's
memory, so `frontendManager.frontends` and `setActive` work unchanged. Use
applies at once (no restart in a page): the page follows
`frontendManager.activeId` over its own `Module.octaveService` connection
and shows the chosen frontend over the Qt screen in the same sandboxed
iframe as the playground, starting the simulated car when no car is
connected. Its **OCTAVE** button (`setActive("")`), or mute pressed five
times, brings the built-in screen back. `index.html?frontend=<slug>` opens
on one, and the browser remembers the last one shown (`localStorage`).
Install from folder / zip and Remove are hidden there: the files are the
site's.

## The frontend playground

`playground.html` runs a web frontend (one written for the service API,
`wiki/writing-a-frontend.html`) against this same core, in the browser, with
nothing to install: `/OCTAVE/playground.html?frontend=glass`. The core runs
as on `index.html`, its own screen shown small on the right; it starts the
simulated car itself (`obdManager.connect_to_adapter("sim://elm327")`).

- **The service without a socket.** A page can't listen on a port, so the
  wasm build offers the service protocol to the page as
  `Module.octaveService` (`src/platform/wasm/webservice.{h,cpp}`): `open`,
  `send`, `close`, each message one request handled by the same
  `CommandServer` ops and `ServiceBridge` a socket's line goes to
  (`CommandServer::handleLineOn`). The service is always on in the browser
  build, and only this page can reach it.
- **The frontend, unmodified.** It runs in an iframe whose page gets a
  `WebSocket` stand-in before its own scripts, carrying each text frame to
  the playground by `postMessage`; the playground passes it to
  `Module.octaveService`. So `new WebSocket("ws://" + location.host + "/")`
  just works. Binary stream records (video) are not carried.
- **Sandboxed.** The iframe is `sandbox="allow-scripts allow-forms"` without
  `allow-same-origin`: the frontend has an opaque origin, can't reach this
  site, its storage or the playground's page, and has only the bridge. That
  is what makes `?frontend=<url>` (any folder or `index.html` whose host
  allows CORS, as GitHub Pages does) safe to offer. `localStorage` throws in
  there, so a frontend must not depend on it.
- **Bundled frontends**: `frontends/hello-car-js/` (the template),
  `frontends/glass/`, and three native examples built for the browser,
  `frontends/sunlight/`, `frontends/arcade/` and `frontends/retro-cluster/`
  (each when built: glass with npm, the others with their `build_web.sh`),
  staged by
  `stage_frontends.py` (from `build.sh`) with `frontends/catalog.json`: each
  one's own `octave-frontend.json` plus its `slug`, `path` and `preview`
  (its picture for the chooser in Settings > Device > Frontends). To offer
  another, add it to `FRONTENDS` there. `frontends/native.json` lists the
  example with no browser build (the console): the chooser shows it greyed
  out.
- **Shared code**: the stand-in, the bridge and the loader are
  `frontend-host.js`, used by this page and `index.html`.
- **Not available there**: `GET /art` and `/export` (no HTTP server: covers
  don't show), pairing (the page counts as a program on the device), video.
- **The check**: `node packaging/wasm/check_playground.mjs
  http://127.0.0.1:8080/` loads each bundled frontend in headless Chromium
  and passes when it shows two different RPM readings from the core
  (`SHOT_DIR=` keeps screenshots); then on `index.html` it checks that
  `frontendManager.frontends` lists the catalog, that `setActive` shows one
  on top with live values, and that the OCTAVE button brings the screen
  back. The Pages workflow runs it after the
  build (advisory for now).
