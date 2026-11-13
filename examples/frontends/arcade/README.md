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

Over the network (OCTAVE started with `--service-bind 0.0.0.0`, protocol 2) the service refuses everything until the device pairs: the PAIR page shows a keypad for the 6-digit code from the car's Settings, Service, Pair a device (keys 0-9, Backspace, Enter work too). It asks for the manifest's two permissions, media and vehicle, and no more. Keep the token with `--token-file PATH` (written on a successful pair, read on the next start), or pass one with `--token T` / `OCTAVE_TOKEN`; without either the token is printed once. The cover is fetched with `Authorization: Bearer`. A token OCTAVE rejects is dropped and the PAIR page returns.

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

Every key is on screen too: the pages are laid out for a finger (mouse clicks and touch both land as taps, a finger once).

## In a browser

The same C also builds as a **web frontend** with Emscripten (SDL2 from its ports; one thread, no socket emulation): `src/link_web.c` takes `src/link_tcp.c`'s place and talks to OCTAVE over the page's own WebSocket, one JSON message per text frame.

```bash
cd examples/frontends/arcade
./build_web.sh                           # emcc on PATH, or an emsdk at $EMSDK or ~/emsdk
./build/octave --headless --sim --service --web-root examples/frontends/arcade/dist-web    # from the repo root
# then http://127.0.0.1:9876/ ; from a phone add --service-bind 0.0.0.0 and open http://<the car's address>:9876/
```

`dist-web/` holds `index.html`, `arcade.js`, `arcade.wasm` (about 0.8 MB together, 0.25 MB gzipped) and its own `octave-frontend.json` (`org.octave.example.arcade.web`, `"web": {"root": "."}`), so the folder installs like any web frontend. The page scales the 320x200 picture up whole, centred on black, never smoothed: by a whole number of screen pixels where that fits, to the full width on a phone held upright (landscape is the bigger picture). Every action is a tap target; the keys in the table work too, apart from F11 and Ctrl+Q.

- **Pairing**: from another device the PAIR keypad comes up as it does natively. The token is kept in the page's `localStorage` when the browser allows it; otherwise it lasts until the page closes.
- **Cover**: fetched from `/art` with a media ticket (`mediaTicket`), never the token, and decoded by the same stb_image code.
- **Inside the OCTAVE browser demo** (a sandboxed frame with no origin of its own) it runs on the page's stand-in `WebSocket`. There is no host to fetch `/art` from there, so the cover stays the placeholder, and nothing is stored.
- It draws with SDL's software renderer on a 2D canvas: no WebGL context for a phone to drop in the background. There is no inline script, as OCTAVE serves web frontends with `script-src 'self' 'wasm-unsafe-eval'`.

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

`src/link.c` is the protocol (cJSON): hello, pairing, subscribe, fold events into a `State` the UI copies out each frame. How the messages travel is one more file: `src/link_tcp.c` natively (POSIX sockets, an SDL thread and mutex, reconnect, the HTTP GET for the cover), `src/link_web.c` in a browser (the page's WebSocket and `fetch`, no thread); `src/link_impl.h` is what the two halves share. `src/font.c` is the pixel font, `src/art.c` the cover crusher, `src/ui.c` the three pages and their buttons, `src/main.c` the window and the event loop (the browser's frame clock on the web), `web/` the page around the canvas.

## Known gaps

- Native units only: speed is shown as the app reports it (MPH) and coolant in °C, without the user's unit choice from `parameterCatalog.displayParameters`.
- No seek: the progress tape is display only.
- No sound of its own.
- Not tried on the Pi or the Jeep box yet; SDL2 needs a GL or framebuffer renderer there.
