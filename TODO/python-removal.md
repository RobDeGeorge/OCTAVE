# Remove the frozen Python backend from `main`

**Status:** in progress. Python was frozen on 2026-10-18; removal waits on C++ tests.
**Last updated:** 2026-10-18

## Where things stand

Rob deprecated the Python backend on 2026-10-18 and develops only in C++ from then on. The plan had been full parity until v1.0, then a freeze, but parity roughly doubled the backend work (81 of the 97 commits that touched `src/` from July to mid-October 2026 also had to touch `backend/`). Only C++ ships, and the Jeep head unit runs the C++ build too.

- **Snapshot:** branch `python-final` at `b5463c9`, the last commit with C++/Python parity. That is the fork point for anyone who wants a Python version. It hasn't been pushed yet.
- **Parity rule:** removed from `CLAUDE.md` the same day. Nobody ports changes to `backend/` any more; it will drift and break against new QML, which is accepted.
- **Dev tooling:** already on C++ (`src/dev/`, `OCTAVE_DEV_TOOLS`; b5463c9). The `dev/` clients launch `build/octave --dev --profile`.

## What still blocks deleting it

**All of `tests/`** (pytest, ~21 files) imports and exercises `backend/`; it is the only behavioural test suite. CMake has no test targets, and CI's `cpp-compile-check` only builds the tree and runs a `qmllint` error pass. Deleting Python now would leave the shipped binary untested. Until the C++ tests exist, a C++ change that breaks a Python test gets that test skipped with a reason, not a Python port.

Not affected (they aren't the Python backend and stay): `scripts/fetch_platform_tools.py`, `packaging/windows/resolve_dlls.py`, `wiki/build_search_index.py`, the Android `assets/python-packages/ytmusic-bundle.zip`, and the `dev/` clients (MCP servers, screenshot / story / collage / perf scripts), which talk to the app over TCP.

## Plan

1. **Push the snapshot.** `git push origin python-final`. On that branch, add a README note: "Frozen Python/PySide6 backend as of 2026-10-18 (OCTAVE 0.9.5+). Not maintained. Fork freely."
2. **C++ tests.** Add `enable_testing()` and a `tests/cpp/` QtTest target. Port the pure-logic tests first: `test_elm327_dtc`, `test_obd_extended_pids`, `test_download_ytm_parse`, `test_dashboard_sharing`, `test_settings_persistence`, `test_adaptive_bitrate`, `test_esp32_read_buffer`. Integration tests (`test_boot`, `test_vehicle_gate_qml`, `test_phone_mirror_*`) can drive `build/octave --dev --profile` through the command server from pytest or a small QtTest harness; the simulated ELM327 (`src/dev/simelm327.cpp`) makes the OBD ones possible without hardware. Wire `ctest` into `cpp-compile-check`. Cross-check against `TODO/future-tests.md`.
3. **Delete Python from `main`.** `backend/`, `main.py`, `setup.py`, `requirements.txt`, the Python tests the C++ suite replaced, `pyproject.toml`'s backend settings. On the developer machines (gitignored `dev/`, synced by Dropbox) also delete the Python in-app dev peers (`dev/main_dev.py`, `dev/dev_runtime.py`, `dev/mock_obd_manager.py`, `dev/mock_imu.py`, `dev/dev_tools_manager.py`, `dev/collage/bridge.py`, `backend/command_server.py`, `backend/perf_monitor.py`, `backend/perf_patches.py`) and the `python` branch of `dev/app_launch.py`.
4. **CI:** drop the Python `test` job; keep ruff only for the remaining `dev/` and `scripts/` Python, or drop it. Make the C++ build jobs depend on `ctest`.
5. **Wiki:** remove the Python file paths, commands and samples from every page (`grep -l "backend/\|Python\|pytest" wiki/*.html`), add one line on `index.html` pointing at the `python-final` branch, rebuild the index with `python wiki/build_search_index.py`.
6. **`CLAUDE.md` / README:** drop the frozen-Python notes and say plainly that the project is C++ only, linking `python-final`.

Take a perf baseline on the C++ build with the MCP tools before or alongside step 2; Python baselines don't carry over (different loop-latency probe, see the wiki's profiling section).

Delete this file when step 6 lands.
