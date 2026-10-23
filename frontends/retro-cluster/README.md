# Retro Cluster (experimental)

A second OCTAVE frontend, written to prove the open backend: **Rust + egui** instead of C++ / QML, painted as a mid-80s vacuum-fluorescent instrument cluster instead of the bundled UI's look. It is a separate program that talks to OCTAVE over the service API (`docs/api/`, protocol versions 1 and 2). The QML UI in `frontend/` stays the default; this one is a side experiment and is not built by CI or shipped.

Everything on screen is drawn from shapes: a 5x7 dot-matrix font, slanted seven-segment digits with ghosted unlit segments, segment ladders and a sweep tachometer. There are no font or image files.

## Run

```bash
./build/octave --service                 # the normal app, plus the service on 127.0.0.1:9876
./build/octave --headless                # or: no QML UI at all, the cluster is the only screen
./build/octave --dev --service           # or: the simulated car, for trying it at a desk

cd frontends/retro-cluster
cargo run --release                      # --host H --port P --fullscreen --theme 0|1|2 --page drive|diag
```

On the head unit itself the cluster needs nothing more. From another machine (OCTAVE started with `--service-bind 0.0.0.0`) it must pair once: on the head unit open Settings > Device > Service > Show a code, then

```bash
cargo run --release -- --host 192.168.1.20 --pair 123456      # prints the token
OCTAVE_TOKEN=<token> cargo run --release -- --host 192.168.1.20   # or --token <token>
```

Unpaired, the tape line says NOT PAIRED and the app answers nothing else. A revoked token gets the same.

Start the cluster before or after OCTAVE: until the service answers it shows NO SIGNAL and retries every second. Debian / Ubuntu / Raspberry Pi OS need `libxkbcommon-dev` and the GL / Wayland / X11 dev packages that eframe builds against.

| Key | Does |
|---|---|
| Space | play / pause |
| Left / Right | previous / next track |
| + / - (or Up / Down) | volume by 5 |
| M | mute |
| Tab | DRIVE / DIAG page |
| T | theme (amber, P1 green, VFD cyan) |
| F11 | fullscreen |
| Ctrl+Q | quit |

All the keys are on screen too, for the touch panel. Tap the tape (the progress strip) to seek.

## Pages

- **DRIVE**: sweep tachometer (shift-light flash from `shiftLightManager`), speed, coolant / oil (intake air if the car has no oil PID) / fuel / voltage ladders, a trip computer (load, throttle, MAF, MAP), the black-box trip timer, and the tape deck.
- **DIAG**: every PID the vehicle reports, live, three columns of fifteen per page.

## What it uses from the API

| Object | For |
|---|---|
| `paramStore` | every reading (one `subscribe`, 40 ms batches) |
| `parameterCatalog.displayParameters` | titles, units, ranges, and `displayScale` / `displayOffset`, so the gauges follow the user's °F / km/h choice |
| `obdManager` | `setParameterDemand("retro-cluster", ids)` for the PIDs on the current page; `connected`, `vehicleSupportedParameters`, VIN, make, year |
| `nowPlaying` | track, position, source, `togglePlay` / `next` / `previous` / `seek` / `toggleShuffle` |
| `volumeController` | `percent`, `muted`, `applyVolume`, `toggleMute` |
| `shiftLightManager`, `tripRecorder`, `clock` | the SHIFT and REC lamps, the trip timer, the clock text |

`src/link.rs` is the whole client (about 450 lines, `std::net` plus `serde_json`): a thread connects, subscribes, folds events into a `State` the UI reads, and reconnects. `src/vfd.rs` holds the drawing primitives, `src/main.rs` the layout, on a 1280x800 canvas scaled to the window.

## Known gaps

- No album art and no phone-mirror video: media streams are not on the service yet (`TODO/open-backend.md`, step 6).
- If the process is killed (not closed), its OBD demand stays registered until OCTAVE restarts.
- Not tried on the Pi or the Jeep box yet; egui needs OpenGL ES 2 or better.
