# Starter templates

Copy one of these folders and you have an OCTAVE frontend that connects, pairs when it is not on the head unit (asking only for the media permission), shows RPM and speed live, plays and pauses, and shows the cover. Each is complete on its own, under 200 lines, with its own README, and each has a check that runs it against the simulated car, so they cannot rot (ctest `template_js`, `template_python`, `template_go`, each registered when its language is found).

| Folder | Language | Runs where | Client |
|---|---|---|---|
| [`hello-car-js/`](hello-car-js/) | plain JavaScript, no bundler | a browser, served by OCTAVE itself with `--web-root` | `octave.js`, 60 lines, in the folder |
| [`hello-car-python/`](hello-car-python/) | Python 3.8+, standard library | a terminal | `sdk/python/octave_client`, copied into the folder |
| [`hello-car-go/`](hello-car-go/) | Go 1.22+, standard library | a terminal | `sdk/go`, a module dependency |

Try one against a car you do not have:

```bash
./build/octave --headless --sim --web-root templates/hello-car-js     # any build: the simulated car and the service, no window
```

then open <http://127.0.0.1:9876/>, or run the Python or Go one against the same app. The car idles until something drives it: `{"op":"call","object":"sim","method":"setThrottle","args":[40]}` over the service, or a scenario (`docs/api/README.md`, "Simulator").

## Install one as the screen

`hello-car-js` and `hello-car-python` each carry an `octave-frontend.json`, so the folder is also an installable frontend: Settings > Device > Frontends > Install from folder, then Use (or `frontendManager.installFromFolder(path)` and `setActive(id)` over the service). From the next start the JavaScript one is served at `/` and the Python one runs as the screen in place of the built-in UI, each with only the **media** permission. Each README's "Install it as the screen" has the details; ctest `templates_install` runs both that way against the simulated car. `hello-car-go` has no manifest yet: to install it, build it, read `OCTAVE_HOST`, `OCTAVE_PORT` and `OCTAVE_TOKEN` from the environment as `hello_car.py` does, and add an `octave-frontend.json` whose `exec` names the binary inside the folder.

Where to go from a template: `wiki/writing-a-frontend.html` (the walkthrough), `docs/api/<object>.json` (every object's members), `docs/api/protocol.json` (the protocol), `sdk/` (typed clients with reconnects).
