# Remove the frozen Python backend from `main`

**Status:** ready. Python was frozen on 2026-10-18 and the C++ test suite replaced the pytest one the same day; what is left is the deletion itself.
**Last updated:** 2026-10-18

## Where things stand

Rob deprecated the Python backend on 2026-10-18 and develops only in C++ from then on. The plan had been full parity until v1.0, then a freeze, but parity roughly doubled the backend work (81 of the 97 commits that touched `src/` from July to mid-October 2026 also had to touch `backend/`). Only C++ ships, and the Jeep head unit runs the C++ build too.

- **Snapshot:** branch `python-final` at `b5463c9`, the last commit with C++/Python parity. That is the fork point for anyone who wants a Python version.
- **Parity rule:** removed from `CLAUDE.md` the same day. Nobody ports changes to `backend/` any more; it will drift and break against new QML, which is accepted.
- **Dev tooling:** already on C++ (`src/dev/`, `OCTAVE_DEV_TOOLS`; b5463c9). The `dev/` clients launch `build/octave --dev --profile`.
- **Snapshot pushed:** `python-final` is on GitHub with a "frozen, fork freely" README note (7e01923).
- **C++ tests done (2026-10-18):** `tests/cpp/` (17 QtTest executables, `ctest`, run in CI's `cpp-compile-check`) covers every Python test in `tests/` except `test_smoke` / `test_qt_threading`, which only check that Python modules import. See the table in `wiki/development.html#testing`.

## What blocked deleting it (resolved)

`tests/` (pytest) exercised `backend/` and was the only behavioural test suite, with no test targets in CMake, so deleting Python would have left the shipped binary untested. The C++ suite in `tests/cpp/` now covers the same ground. Until the deletion lands, a C++ change that breaks a Python test gets that test skipped with a reason, not a Python port.

Not affected (they aren't the Python backend and stay): `scripts/fetch_platform_tools.py`, `packaging/windows/resolve_dlls.py`, `wiki/build_search_index.py`, the Android `assets/python-packages/ytmusic-bundle.zip`, and the `dev/` clients (MCP servers, screenshot / story / collage / perf scripts), which talk to the app over TCP.

## Plan

1. ~~Push the snapshot~~ (done 2026-10-18).
2. ~~C++ tests~~ (done 2026-10-18).
3. **Delete Python from `main`.** `backend/`, `main.py`, `setup.py`, `requirements.txt`, the Python tests the C++ suite replaced, `pyproject.toml`'s backend settings. On the developer machines (gitignored `dev/`, synced by Dropbox) also delete the Python in-app dev peers (`dev/main_dev.py`, `dev/dev_runtime.py`, `dev/mock_obd_manager.py`, `dev/mock_imu.py`, `dev/dev_tools_manager.py`, `dev/collage/bridge.py`, `backend/command_server.py`, `backend/perf_monitor.py`, `backend/perf_patches.py`) and the `python` branch of `dev/app_launch.py`.
4. **CI:** drop the Python `test` job; keep ruff only for the remaining `dev/` and `scripts/` Python, or drop it. Make the C++ build jobs depend on `ctest`.
5. **Wiki:** remove the Python file paths, commands and samples from every page (`grep -l "backend/\|Python\|pytest" wiki/*.html`), add one line on `index.html` pointing at the `python-final` branch, rebuild the index with `python wiki/build_search_index.py`.
6. **`CLAUDE.md` / README:** drop the frozen-Python notes and say plainly that the project is C++ only, linking `python-final`.

Take a perf baseline on the C++ build with the MCP tools before or alongside step 3; Python baselines don't carry over (different loop-latency probe, see the wiki's profiling section).

Delete this file when step 6 lands.
