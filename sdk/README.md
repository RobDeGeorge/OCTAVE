# OCTAVE SDK

Clients for OCTAVE's service API: read and drive the app (media, volume, live OBD and IMU values, settings, dashboards) from another program, with no Qt. The objects and their types are in [`docs/api/`](../docs/api/README.md); the protocol is in [`wiki/utilities.html#service-protocol`](../wiki/utilities.html#service-protocol).

Turn the service on first. It is off by default, and listens on 127.0.0.1:9876:

```bash
./build/octave --service          # with the normal UI
./build/octave --headless         # no UI: the managers and the service only
```

(Or set `"serviceEnabled": true` in `settingsConfigure.json`. To reach it from another machine add `--service-bind 0.0.0.0`; there is no authentication, so only on a network you trust.)

## Browser: `web/`

`web/index.html` is a reference client in plain JavaScript (no build, no libraries): live gauges, now playing with transport controls, volume, and an API explorer. The gauges show native units (`parameters.json`), not the user's display units. The app serves it at **http://127.0.0.1:9876/** and the page talks back over a WebSocket on the same port. One JSON request, reply or event per text frame, the same messages as the line protocol.

A browser page from anywhere else can't connect. The app refuses a WebSocket whose `Origin` isn't its own page, and refuses a host given by name rather than by address (`src/service/websocket.h`). Scripts send no `Origin` and are not affected. So open the page by address (`http://192.168.x.y:9876/`), not by a name such as `octave.local`: names are refused on purpose, since a website could point one at the car. With `--service-bind` beyond loopback the page goes to the whole network as well, as a control panel with no login.

## Python: `python/`

`python/octave_client` is a stdlib-only client (Python 3.8+):

```python
import sys; sys.path.insert(0, "sdk/python")
from octave_client import Client, launch_headless

with launch_headless() as app, Client(port=app.port) as octave:   # or Client() for a running app
    print(octave.hello())
    octave.call("volumeController", "applyVolume", 30)
    octave.subscribe("paramStore", properties=["RPM", "SPEED"], throttle_ms=100)
    for event in octave.events(timeout=5):
        print(event)
```

`Client` has `hello`, `describe`, `get`, `set`, `call`, `rows`, `subscribe` / `unsubscribe` / `events`, and `request(op, ...)` for anything else. The objects behave as they do for the touch screen: `volumeController.toggleMute()`, for one, ignores a second toggle within 150 ms (the knob's debounce), so a script that wants a definite state sets the writable property instead (`set("volumeController", "muted", False)`). Replies are matched by id, so events can interleave and requests can come from several threads. `launch_headless()` starts `octave --headless` on a free port with a throwaway config (the binary from `$OCTAVE_BIN`, else this checkout's `build/`).

Examples: `python/examples/live_gauges.py` (readings with titles and units) and `python/examples/now_playing.py` (follow the track; `next`, `play`, `volume 40`, `mute`).

Tests: `python -m unittest discover -s sdk/python/tests` runs the client against a headless app. ctest runs it as `sdk_python`.

## Another whole frontend: `frontends/retro-cluster/`

An experimental UI in Rust + egui, styled as an 80s fluorescent instrument cluster, built only on this API (see its README). Not built by CI.

## Versioning

Both clients speak protocol version 1. The Python `Client` asks `hello` on connect and raises `OctaveError` on another version (`check_version=False` skips that); the web page shows the mismatch in its header and carries on. See [`docs/api/README.md`](../docs/api/README.md) for what a version bump means.
