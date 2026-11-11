# Contributing to OCTAVE

Thanks for looking. There are two ways in, and they need different things from you.

## Build a frontend (no C++ needed)

OCTAVE's screen is replaceable. Every manager the built-in QML UI uses is served over a documented JSON protocol, so you can write a whole new UI, a phone remote, a second screen or a script in any language, under any license.

1. **Try one first.** The [frontend playground](https://robdegeorge.com/OCTAVE/playground.html) runs a web frontend against the real OCTAVE core and a simulated car in your browser. The [gallery](https://robdegeorge.com/OCTAVE/wiki/frontend-gallery.html) shows the examples.
2. **Start from a template.** `templates/` has the smallest working frontends in JavaScript, Python and Go. `examples/frontends/` has five complete ones (Svelte, Textual, Gio, SDL2, egui). Both are MIT, as are the SDKs in `sdk/`, so copy freely ([Your Frontend, Your License](wiki/writing-a-frontend.html#license)).
3. **Develop against a simulated car.** Any release build runs `octave --headless --sim`: the managers, the service on `127.0.0.1:9876`, and a car that drives, with no window. The walkthrough is [Writing a Frontend](wiki/writing-a-frontend.html). The API is in [docs/api/](docs/api/README.md) and the [Service API Reference](wiki/api-reference.html).
4. **Share it.** Publish it with the GitHub topic `octave-frontend`, then open a [Show your frontend](https://github.com/RobDeGeorge/OCTAVE/issues/new?template=show-your-frontend.yml) issue. If the API is missing something, open an [API request](https://github.com/RobDeGeorge/OCTAVE/issues/new?template=api-request.yml).

The protocol is versioned (`protocolVersion` in `hello`); additions never break you, and a breaking change bumps the version and is listed in [docs/api/CHANGELOG.md](docs/api/CHANGELOG.md). Members marked experimental may still change.

## Change OCTAVE itself

The app is C++ / Qt 6 (`src/`) with a QML UI (`frontend/`), GPL-3.0-or-later. [BUILD.md](BUILD.md) has every platform.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j8
ctest --test-dir build --output-on-failure
./build/octave --sim            # the app with a simulated car
```

- One logical change per pull request, with a test in `tests/cpp/` for logic changes.
- Update the wiki page your change affects in the same pull request (`wiki/`, then `python wiki/build_search_index.py`).
- A change to a manager's public surface (a `Q_PROPERTY`, `Q_INVOKABLE`, slot, signal or PID) regenerates the published API in the same commit: `./build/octave --dump-api docs/api && python wiki/build_api_reference.py && npm --prefix sdk/typescript run generate && (cd sdk/go && go generate ./...)`. New members start experimental; `python tools/api/check_compat.py` (run in CI) refuses an unannounced breaking change.
- CI (`.github/workflows/build.yml`) runs ruff, the Linux build, `ctest` and a `qmllint` error pass on every pull request.

Bugs and ideas: [issues](https://github.com/RobDeGeorge/OCTAVE/issues). Deferred plans that anyone can pick up are in [TODO/](TODO/README.md).
