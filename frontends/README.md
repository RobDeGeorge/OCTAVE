# Frontends

OCTAVE's managers are served to any program over the service API (`docs/api/`, `wiki/writing-a-frontend.html`). The QML UI in `frontend/` is the one that ships; the programs here are other frontends, each in a different language, toolkit and style, written to prove that the backend is enough to build a whole UI on. None of them is shipped. CI's `frontends` job checks that each one builds and passes its own tests.

| Frontend | Language / toolkit | Style | Covers |
|---|---|---|---|
| [`glass/`](glass/) | TypeScript + Svelte 5 (browser, served by the app with `--web-root`) | frosted glass tablet UI, light and dark | aims at everything the QML app does |
| [`console/`](console/) | Python + Textual (terminal) | dense, keyboard-first operator console | live data, OBD and trouble codes, trips, every setting, downloads, an API explorer |
| [`sunlight/`](sunlight/) | Go + Gio (native) | brutalist black on white for direct sunlight, inverted night mode | drive readouts, every saved dashboard drawn from its JSON spec, media with the cover |
| [`arcade/`](arcade/) | C11 + SDL2 (native) | 8-bit arcade dashboard: 320x200 pixels scaled up, own pixel font, 16 colours | drive readouts, media with a palette-crushed cover, trouble codes |
| [`retro-cluster/`](retro-cluster/) | Rust + egui (native) | 1980s vacuum-fluorescent instrument cluster | driving gauges, every PID, trouble codes, trips, a spectrum analyzer, media with a dot-matrix cover |

Each has a README: how to run it, its keys, its pages, which API objects it uses, and what it can't do yet.

[`PARITY.md`](PARITY.md) checks, feature by feature, which of these reaches each control the QML app has, and lists what only the QML app can do until the backend adds it.

## Running one against a simulated car

```bash
./build/octave --headless --dev          # managers + the service + the simulated car, no QML (dev-tools build)
./build/octave --dev --service           # or: the QML UI as well
```

Then start the frontend (see its README); they all default to `127.0.0.1:9876`. A web frontend is served by the app itself, so its page and its WebSocket share an origin:

```bash
./build/octave --headless --dev --web-root frontends/glass/dist     # then open http://127.0.0.1:9876/
```

## Writing another

Start with `wiki/writing-a-frontend.html`. The smallest complete client is `retro-cluster/src/link.rs` (one file); `sdk/python` and `sdk/web` are the reference clients.
