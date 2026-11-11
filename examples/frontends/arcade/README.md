# Arcade

A fourth OCTAVE frontend, written to prove the open backend from plain C: **C11 + SDL2**, no toolkit, no font or image files. It draws an 8-bit arcade dashboard on a 320x200 canvas scaled up whole (4x on the 1280x800 head unit): a 5x5 pixel font made of rectangles, segmented ladders, chunky buttons, a flat sixteen-colour palette, and the album cover crushed to 24x24 big pixels snapped to that palette. It talks to OCTAVE over the service API (`docs/api/`, protocol versions 1 to 3): JSON lines over a TCP socket, plus one short HTTP GET for the cover. It is one of the example frontends in `examples/frontends/` (MIT licensed); the QML UI in `frontend/` is the one that ships.

## Run

```bash
./build/octave --service                 # the normal app, plus the service on 127.0.0.1:9876
./build/octave --headless                # or: no QML UI at all, the arcade is the only screen
./build/octave --headless --sim         # or: a simulated car (any release build), for trying it at a desk

cd examples/frontends/arcade
cmake -S . -B build && cmake --build build -j4
./build/octave-arcade                    # --host H --port P --fullscreen --page drive|media|dtc --scale N --token-file PATH
```

Needs `libsdl2-dev` (Debian / Ubuntu / Raspberry Pi OS) or `sdl2` / `sdl2-compat` (Arch), CMake 3.16 and a C11 compiler. It builds warning-free with `-Wall -Wextra -Wpedantic`; the two vendored libraries (`third_party/`, cJSON v1.7.18 and stb_image v2.30, with their licences) are compiled with warnings off. Start it before or after OCTAVE: until the service answers it shows NO SIGNAL and retries every second, and it re-subscribes on every reconnect.

Over the network (OCTAVE started with `--service-bind 0.0.0.0`, protocol 2) the service refuses everything until the device pairs: the PAIR page shows a keypad for the 6-digit code from the car's Settings, Service, Pair a device (keys 0-9, Backspace, Enter work too). Keep the token with `--token-file PATH` (written on a successful pair, read on the next start), or pass one with `--token T` / `OCTAVE_TOKEN`; without either the token is printed once. The cover is fetched with `Authorization: Bearer`. A token OCTAVE rejects is dropped and the PAIR page returns.

| Key | Does |
|---|---|
| 1 / 2 / 3, Tab | DRIVE / MEDIA / DTC page |
| Space | play / pause |
| Left / Right | previous / next track |
| Up / Down, + / - | volume by 5 |
| M | mute |
| R | read codes (DTC page) |
| Y / N | answer the clear-codes question |
| F11 | fullscreen |
| Ctrl+Q | quit |
| 0-9, Backspace, Enter | the pairing code (PAIR page) |

Every key is on screen too: the pages are laid out for a finger (mouse clicks and touch both land as taps).

## Install it as the screen

`octave-frontend.json` makes this folder an installable frontend: the program `build/octave-arcade --fullscreen`, asking for the **media** and **vehicle** permissions (play / pause / skip / volume / mute; connect, read and clear trouble codes).

```bash
cd examples/frontends/arcade
cmake -S . -B build && cmake --build build -j4          # the manifest expects build/octave-arcade
```

Then on the head unit: Settings > Device > Frontends > **Install from folder** (this folder), and **Try** (a timed trial that goes back by itself) or **Use** (from the next start). Over the service: `frontendManager.installFromFolder(path)`, then `tryFrontend(id, seconds)` or `setActive(id)`. Started that way it reads `OCTAVE_HOST`, `OCTAVE_PORT` and `OCTAVE_TOKEN` from its environment (the flags still win), sends the token with `hello`, and logs one `connected ...` line that OCTAVE relays into its own log as `[org.octave.example.arcade] ...`. The token holds exactly the manifest's scopes, and while it is the screen nothing else on the head unit gets in without one (pair other tools with `octave --pair-code`). Quitting with Ctrl+Q (exit code 0) quits OCTAVE too.

Pressing mute five times within four seconds always ends it and brings back OCTAVE's built-in screen: the ESP32 knob, the gesture sensor, or anything else that calls `volumeController.toggleMute()` (this frontend's own M key and mute button set `volumeController.muted` directly, a definite state, so they don't count toward the five); `octave --qml` gives the built-in screen for one start. The rules are in `wiki/writing-a-frontend.html#install`.

## Pages

- **DRIVE**: speed in big digits, a 24-segment RPM ladder with the redline, the SHIFT flash from `shiftLightManager`, coolant / fuel / voltage ladders, a CONNECT button when the car is not on the line.
- **MEDIA**: the cover (from `GET /art`, refetched when `nowPlaying.artRevision` changes), title and artist (long titles scroll), the progress tape, previous / play / next / shuffle, volume with - / + and mute.
- **DTC**: the check-engine lamp, stored codes as chunky tiles, READ, and CLEAR behind a YES / NO question; the ECU's answers (`dtcCodesChanged`, `dtcClearResult`) are shown for a few seconds.

## What it uses from the API

| Object | For |
|---|---|
| `paramStore` | SPEED, RPM, COOLANT_TEMP, FUEL_LEVEL, CONTROL_MODULE_VOLTAGE, LATERAL_G (one `subscribe`, 50 ms batches) |
| `obdManager` | `setParameterDemand("arcade", ids)`, `connected`, `connectionState`, `connect_obd`, `get_dtc_codes`, `read_dtc`, `clear_dtc`, the DTC signals |
| `nowPlaying` | track, position, `artRevision`, `togglePlay` / `next` / `previous` / `toggleShuffle` |
| `volumeController` | `percent`, `muted` (set directly for a definite state), `applyVolume` |
| `shiftLightManager`, `clock`, `berryIMU` | the SHIFT flash, the clock text, whether an IMU is there |
| `GET /art` | the current cover as PNG / JPEG bytes, decoded with stb_image |

`src/link.c` is the whole client (about 330 lines: POSIX sockets, cJSON, an SDL thread and mutex): connect, hello, subscribe, fold events into a `State` the UI copies out each frame, reconnect. `src/font.c` is the pixel font, `src/art.c` the cover crusher, `src/ui.c` the three pages and their buttons, `src/main.c` the window and the event loop.

## Known gaps

- Native units only: speed is shown as the app reports it (MPH) and coolant in °C, without the user's unit choice from `parameterCatalog.displayParameters`.
- No seek: the progress tape is display only.
- No sound of its own.
- Not tried on the Pi or the Jeep box yet; SDL2 needs a GL or framebuffer renderer there.
