# Hello, car (Python)

The smallest OCTAVE frontend in Python: two live readings, the track with play / pause, the cover saved to a file, and pairing when this machine is not the head unit. Python 3.8+, standard library only. `octave_client/` is OCTAVE's Python client (`sdk/python/octave_client`) copied in, so the folder is complete on its own; `hello_car.py` is the program, about 120 lines. Copy the folder and start from it.

## Run

```bash
octave --headless --sim                    # the release download (or ./build/octave): a simulated car, no window; or --service with the UI
python templates/hello-car-python/hello_car.py
```

Enter toggles play / pause, Ctrl-C quits. From another machine, start OCTAVE with `--service-bind 0.0.0.0` and run `hello_car.py <the head unit's address>`: it asks for the 6-digit code from Settings > Service > Pair a device, asks only for the **media** permission (play / pause; readings are always allowed), and keeps the token in `.token` beside the script for next time.

Make the numbers move with the simulator: `octave_client.Client().call("sim", "setThrottle", 40)`, or a scenario (`docs/api/README.md`, "Simulator").

## What it uses

| | |
|---|---|
| `Client(host, port, token)`, `pair(code, name, scopes=["media"])`, `media_ticket()` | connect, pair, a ticket for the cover URL |
| `parameterCatalog.info(id)` | the unit and decimals of each reading |
| `obdManager.setParameterDemand("hello-car", ["RPM", "SPEED"])` | ask OCTAVE to poll these two; `[]` when done |
| `paramStore` subscription, `events()` | the values, native units |
| `nowPlaying` subscription, `togglePlay` | the track and the key |
| `GET /art?rev=N&ticket=T` | the cover, saved as `cover.<type>` |

## Check it

```bash
python templates/hello-car-python/test/check.py      # OCTAVE_BIN names the binary; default build/octave
```

Starts `octave --headless --sim` on a free port with a throwaway config, runs `hello_car.py --check 4` from a copy and requires readings and a playing track; also requires the copied `octave_client/` to still equal the SDK's files. ctest runs it as `template_python`.

## Next

`docs/api/<object>.json` lists every object's members, `docs/api/protocol.json` the protocol, `wiki/writing-a-frontend.html` walks through it. `sdk/python/examples/` has more.
