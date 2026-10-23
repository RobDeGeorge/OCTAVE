# Hello, car (plain JavaScript)

The smallest OCTAVE frontend that does something: two live readings, the track with play / pause, the cover, and pairing when the browser is not on the head unit. Plain JavaScript, no bundler, no dependencies: `octave.js` is the whole protocol in 60 lines, `app.js` the page in 80. Copy the folder and start from it.

## Run

```bash
octave --headless --sim --web-root templates/hello-car-js     # the release download (or ./build/octave): a simulated car, no window; or --service --web-root with the UI
```

Open <http://127.0.0.1:9876/>. On the head unit itself nothing else is needed. From a phone or another machine, start OCTAVE with `--service-bind 0.0.0.0`, open `http://<the head unit's address>:9876/` (by address, not by name: the browser rule in `docs/api/README.md`), and the page asks for the 6-digit code from Settings > Service > Pair a device. It asks only for the **media** permission (play / pause); readings are always allowed. The token stays in the browser.

A release build has the simulated car too: `octave --headless --sim` works everywhere, and `sim.setThrottle(40)` or `sim.scenario("highway")` over the service makes the numbers move (`docs/api/README.md`, "Simulator").

## What it uses

| | |
|---|---|
| `hello`, `pair`, `mediaTicket` | connect, pair with `scopes: ["media"]`, a ticket for the cover URL |
| `parameterCatalog.info(id)` | the unit and decimals of each reading |
| `obdManager.setParameterDemand("hello-car", ["RPM", "SPEED"])` | ask OCTAVE to poll these two (it polls only what someone shows) |
| `paramStore` subscription | the values, native units, 10 a second at most |
| `nowPlaying` subscription, `togglePlay` | the track and the button |
| `GET /art?rev=N&ticket=T` | the cover |

## Check it

```bash
node templates/hello-car-js/test/check.mjs      # OCTAVE_BIN names the binary; default build/octave
```

Starts `octave --headless --sim` on a free port with a throwaway config and drives `octave.js` through what the page does: pairing with the media scope, the readings, play / pause, the cover with and without a ticket. ctest runs it as `template_js` when CMake finds `node`.

## Next

Everything the bundled UI can do is reachable the same way: `docs/api/<object>.json` lists each object's members, `docs/api/protocol.json` the protocol, and `wiki/writing-a-frontend.html` walks through it. For a typed client with reconnects, use `sdk/typescript`.
