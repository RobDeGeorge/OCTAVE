# Retro Cluster (experimental)

A second OCTAVE frontend, written to prove the open backend: **Rust + egui** instead of C++ / QML, painted as a mid-80s vacuum-fluorescent instrument cluster instead of the bundled UI's look. It is a separate program that talks to OCTAVE over the service API (`docs/api/`, protocol version 1). The QML UI in `frontend/` stays the default; this one is a side experiment and is not shipped; CI only checks that it builds (`cargo clippy -D warnings`).

Everything on screen is drawn from shapes: a 5x7 dot-matrix font, slanted seven-segment digits with ghosted unlit segments, segment ladders and a sweep tachometer. There are no font or image files.

## Run

```bash
./build/octave --service                 # the normal app, plus the service on 127.0.0.1:9876
./build/octave --headless                # or: no QML UI at all, the cluster is the only screen
./build/octave --dev --service           # or: the simulated car, for trying it at a desk

cd frontends/retro-cluster
cargo run --release                      # --host H --port P --fullscreen --theme 0|1|2 --page drive|diag
```

Start the cluster before or after OCTAVE: until the service answers it shows NO SIGNAL and retries every second. Debian / Ubuntu / Raspberry Pi OS need `libxkbcommon-dev` and the GL / Wayland / X11 dev packages that eframe builds against.

| Key | Does |
|---|---|
| Space | play / pause |
| Left / Right | previous / next track |
| + / - (or Up / Down) | volume by 5 |
| M | mute |
| Tab | next page (DRIVE, DIAG, DTC, TRIPS) |
| T | theme (amber, P1 green, VFD cyan) |
| F11 | fullscreen |
| Ctrl+Q | quit |

All the keys are on screen too, for the touch panel. Tap the tape (the progress strip) to seek.

## Pages

- **DRIVE**: sweep tachometer (shift-light flash from `shiftLightManager`), speed, coolant / oil (intake air if the car has no oil PID) / fuel / voltage ladders, a trip computer (load, throttle, MAF, MAP), the black-box trip timer, and the tape deck, with the album cover redrawn as a 40x40 dithered dot matrix.
- **DIAG**: every PID the vehicle reports, live, three columns of fifteen per page.
- **DTC**: the check-engine lamp, the stored trouble codes, the freeze frame, read buttons, and a clear that needs a second press within 4 s.
- **TRIPS**: the black box's recorded drives (newest first) and the selected drive's summary in the user's units; export a drive, or end the one being recorded.

## What it uses from the API

| Object | For |
|---|---|
| `paramStore` | every reading (one `subscribe`, 40 ms batches) |
| `parameterCatalog.displayParameters` | titles, units, ranges, and `displayScale` / `displayOffset`, so the gauges follow the user's °F / km/h choice |
| `obdManager` | `setParameterDemand("retro-cluster", ids)` for the PIDs on the current page; `connected`, `vehicleSupportedParameters`, VIN, make, year; `read_dtc`, `clear_dtc`, `read_freeze_frame`, `get_dtc_codes` / `get_mil_status` / `get_freeze_frame` and their change signals, `dtcClearResult` |
| `nowPlaying` | track, position, source, `togglePlay` / `next` / `previous` / `seek` / `toggleShuffle`; `artRevision`, and the cover from `GET /art` when it changes |
| `volumeController` | `percent`, `muted`, `applyVolume`, `toggleMute` |
| `tripRecorder` | `trips`, `requestDetail` / `detailReady`, `exportTrip` / `exportFinished`, `endTripNow`, the REC lamp and the trip timer |
| `shiftLightManager`, `clock` | the SHIFT lamp, the clock text |

`src/link.rs` is the whole client (about 450 lines, `std::net` plus `serde_json`): a thread connects, subscribes, folds events into a `State` the UI reads, and reconnects. `src/vfd.rs` holds the drawing primitives, `src/main.rs` the layout, on a 1280x800 canvas scaled to the window.

## Known gaps

- No phone-mirror video: media streams are not on the service yet (`TODO/open-backend.md`, step 6).
- If the process is killed (not closed), its OBD demand stays registered until OCTAVE restarts.
- Not tried on the Pi or the Jeep box yet; egui needs OpenGL ES 2 or better.
