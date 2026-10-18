# Remove the frozen Python backend from `main`

**Status:** in progress. Python was frozen on 2026-10-18; removal waits on C++ tests.
**Last updated:** 2026-10-18

## Where things stand

Rob deprecated the Python backend on 2026-10-18 and develops only in C++ from then on. The plan had been full parity until v1.0, then a freeze, but parity roughly doubled the backend work (81 of the 97 commits that touched `src/` from July to mid-October 2026 also had to touch `backend/`). Only C++ ships, and the Jeep head unit runs the C++ build too.

- **Snapshot:** branch `python-final` at `b5463c9`, the last commit with C++/Python parity. That is the fork point for anyone who wants a Python version. It hasn't been pushed yet.
- **Parity rule:** removed from `CLAUDE.md` the same day. Nobody ports changes to `backend/` any more; it will drift and break against new QML, which is accepted.
- **Dev tooling:** already on C++ (`src/dev/`, `OCTAVE_DEV_TOOLS`; b5463c9). The `dev/` clients launch `build/octave --dev --profile`.
- **Snapshot pushed:** `python-final` is on GitHub with a "frozen, fork freely" README note (7e01923).
- **C++ tests started (2026-10-18):** `tests/cpp/` + `OCTAVE_BUILD_TESTS`, run by `ctest` in CI's `cpp-compile-check`. Ported: `test_elm327_dtc` + `test_obd_extended_pids` (→ `tst_elm327protocol`), `test_adaptive_bitrate`, `test_settings_persistence`, `test_dashboard_sharing`, `test_esp32_read_buffer`. New: `tst_obdsim` (real OBDManager vs the simulated ELM327; also covers most of `test_obd_poll_demand`).

## What still blocks deleting it

**All of `tests/`** (pytest, ~21 files) imports and exercises `backend/`; it is the only behavioural test suite. CMake has no test targets, and CI's `cpp-compile-check` only builds the tree and runs a `qmllint` error pass. Deleting Python now would leave the shipped binary untested. Until the C++ tests exist, a C++ change that breaks a Python test gets that test skipped with a reason, not a Python port.

Not affected (they aren't the Python backend and stay): `scripts/fetch_platform_tools.py`, `packaging/windows/resolve_dlls.py`, `wiki/build_search_index.py`, the Android `assets/python-packages/ytmusic-bundle.zip`, and the `dev/` clients (MCP servers, screenshot / story / collage / perf scripts), which talk to the app over TCP.

## Plan

1. ~~Push the snapshot~~ (done 2026-10-18).
2. **C++ tests (in progress).** Still to port, roughly in order:
   - logic: `test_download_ytm_parse`, `test_download_partial_file` (DownloadManager; heavy deps, may want a parse helper split out), `test_obd_poll_demand` (any gap left by `tst_obdsim`), `test_rfcomm_bind_async`, `test_obd_adapter_finder` (needs Bluetooth stubbed), `test_media_playback_robustness`, `test_music_devices*`
   - phone mirror: `test_scrcpy_client_stop`, `test_scrcpy_fake_server` (the C++ `ScrcpyClient` against `tests/fake_scrcpy_server.py` or a C++ fake), `test_phone_mirror_probes`, `test_phone_mirror_media`, `test_phone_mirror_wireless`
   - QML / app level: `test_boot` (launch `build/octave --smoke`), `test_vehicle_gate_qml`, `test_phone_mirror_view_qml`, `test_wiki_viewer`; these can load QML in a QtTest with a QQmlEngine, or drive `build/octave --dev --profile` over the command server
   - `test_smoke` / `test_qt_threading` are Python-import checks with no C++ equivalent; drop them.
   Once a test is ported the Python one can go. Cross-check against `TODO/future-tests.md`. A shared static library of the managers would replace the per-test source lists in `tests/cpp/CMakeLists.txt` once there are many.
3. **Delete Python from `main`.** `backend/`, `main.py`, `setup.py`, `requirements.txt`, the Python tests the C++ suite replaced, `pyproject.toml`'s backend settings. On the developer machines (gitignored `dev/`, synced by Dropbox) also delete the Python in-app dev peers (`dev/main_dev.py`, `dev/dev_runtime.py`, `dev/mock_obd_manager.py`, `dev/mock_imu.py`, `dev/dev_tools_manager.py`, `dev/collage/bridge.py`, `backend/command_server.py`, `backend/perf_monitor.py`, `backend/perf_patches.py`) and the `python` branch of `dev/app_launch.py`.
4. **CI:** drop the Python `test` job; keep ruff only for the remaining `dev/` and `scripts/` Python, or drop it. Make the C++ build jobs depend on `ctest`.
5. **Wiki:** remove the Python file paths, commands and samples from every page (`grep -l "backend/\|Python\|pytest" wiki/*.html`), add one line on `index.html` pointing at the `python-final` branch, rebuild the index with `python wiki/build_search_index.py`.
6. **`CLAUDE.md` / README:** drop the frozen-Python notes and say plainly that the project is C++ only, linking `python-final`.

Take a perf baseline on the C++ build with the MCP tools before or alongside step 2; Python baselines don't carry over (different loop-latency probe, see the wiki's profiling section).

Delete this file when step 6 lands.
