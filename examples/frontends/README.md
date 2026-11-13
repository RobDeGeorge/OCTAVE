# Frontends

OCTAVE's managers are served to any program over the service API (`docs/api/`, `wiki/writing-a-frontend.html`). The QML UI in `frontend/` is the one that ships; the programs here are other frontends, each in a different language, toolkit and style, written to prove that the backend is enough to build a whole UI on. They are examples, MIT licensed, and none of them is shipped; `check_all.sh` (below) checks that each one builds and passes its own tests.

| Frontend | Language / toolkit | Style | Covers |
|---|---|---|---|
| [`glass/`](glass/) | TypeScript + Svelte 5 (browser, served by the app with `--web-root`) | frosted glass tablet UI, light and dark | aims at everything the QML app does |
| [`console/`](console/) | Python + Textual (terminal) | dense, keyboard-first operator console | live data, OBD and trouble codes, trips, the library, dashboards, every setting, an API explorer |
| [`sunlight/`](sunlight/) | Go + Gio (native; also a browser build, `build_web.sh`) | brutalist black on white for direct sunlight, inverted night mode | drive readouts, every saved dashboard drawn from its JSON spec, trouble codes, trips, media with the cover |
| [`arcade/`](arcade/) | C11 + SDL2 (native; also a browser build, `build_web.sh`) | 8-bit arcade dashboard: 320x200 pixels scaled up, own pixel font, 16 colours | drive readouts, media with a palette-crushed cover, trouble codes |
| [`retro-cluster/`](retro-cluster/) | Rust + egui (native) | 1980s vacuum-fluorescent instrument cluster | driving gauges, every PID, trouble codes, trips, a spectrum analyzer, media with a dot-matrix cover |

Each has a README: how to run it, its keys, its pages, which API objects it uses, and what it can't do yet.

[`PARITY.md`](PARITY.md) checks, feature by feature, which of these reaches each control the QML app has, and lists what only the QML app can do until the backend adds it.

## Running one against a simulated car

```bash
./build/octave --headless --sim          # managers + the service + a simulated car, no QML (any release build)
./build/octave --sim --service           # or: the QML UI as well
```

Then start the frontend (see its README); they all default to `127.0.0.1:9876`. A web frontend is served by the app itself, so its page and its WebSocket share an origin:

```bash
./build/octave --headless --sim --web-root examples/frontends/glass/dist     # then open http://127.0.0.1:9876/
```

## Installing one as the screen

Each carries an `octave-frontend.json`, so after its build step it installs on the head unit (Settings > Device > Frontends > Install from folder, then Try or Use; `frontendManager.installFromFolder(path)` over the service). The exec ones then run in place of the QML UI with a token of exactly their scopes, read from `OCTAVE_HOST` / `OCTAVE_PORT` / `OCTAVE_TOKEN`; mute pressed five times within four seconds always brings the built-in screen back. Each README's "Install it as the screen" has the details.

| Frontend | Build | Install the folder | Kind | Scopes (beyond read) |
|---|---|---|---|---|
| glass | `npm install && npm run build` | `glass/dist` | web | media, vehicle, phone, settings, files, system |
| console | `pip install --target vendor textual pillow` + copy `sdk/python/octave_client` into `vendor/` | `console` (no `.venv` inside) | exec, `python3 launch.py` in a full-screen terminal | media, vehicle, phone, settings, files, system |
| sunlight | `go build -tags novulkan -o bin/sunlight .` | `sunlight` | exec | media, vehicle |
| arcade | `cmake -S . -B build && cmake --build build` | `arcade` | exec | media, vehicle |
| retro-cluster | `cargo install --path . --root dist && cp octave-frontend.json preview.png dist/` | `retro-cluster/dist` | exec | media, vehicle, files |

sunlight and the arcade also build for the browser (`./build_web.sh` in each, into `dist-web/`: Go's js/wasm port, Emscripten with SDL2): that folder installs as a web frontend (ids ending `.web`), and the browser demo offers both in Settings > Device > Frontends. Each manifest names a `preview.png`, the picture in that chooser.

glass and retro-cluster install from `dist/` because the installer copies the whole folder, refuses links (`node_modules/` has some) and stops at 256 MB (`target/` is past it). Screenshots of each against the simulated car: `wiki/images/frontends/`.

## Checking them all

```bash
examples/frontends/check_all.sh                        # OCTAVE_BIN, else ./build/octave (a dev-tools build)
examples/frontends/check_all.sh --display :83          # also tap sunlight's controls on an X display (Xvfb :83 -screen 0 1280x800x24 &)
examples/frontends/check_all.sh --only console,glass   # a subset; --keep keeps the temp dir and logs
```

It needs a developer build (`-DCMAKE_BUILD_TYPE=Debug`, or `OCTAVE_DEV_TOOLS=ON`): some tests drive the simulated car with dev-only `sim_*` actions. It starts one throwaway OCTAVE (`--headless --dev` on a free loopback port, a temp `HOME` and `XDG_CONFIG_HOME` (exports land there too), three short tagged test tracks with covers made with ffmpeg when it is installed), runs each frontend's own checks against it, prints a pass / fail table and stops the backend. The checks: the console's live pilot tests (`CONSOLE_PY` names a python with its requirements; by default `examples/frontends/console/.venv`, then `python3`), sunlight's `go vet` / `go test` and, with `--display`, `verify_controls.py`, glass's type check and build (`npm ci` first when `node_modules` is missing or older than `package-lock.json`; done before the backend starts, which then serves that build with `--web-root`) and `npm run e2e` against it (its "nothing to test" exit counts as a failure here), the TypeScript SDK's `npm test` (glass builds it as a path dependency), retro-cluster's `cargo clippy -D warnings`, and the arcade's warning-free CMake build. A missing tool shows as SKIP. If the backend dies, the live check it was in says so and the live checks after it are marked FAIL without running. On a failure the logs are kept and the table names them; the exit status is 1.

## Writing another

Start with `wiki/writing-a-frontend.html`. The smallest complete client is `retro-cluster/src/link.rs` (one file); `sdk/python` and `sdk/web` are the reference clients.
