# Freeze the Python backend at v1.0, carry only C++ forward

**Status:** deferred (scheduled): do it after v1.0 ships. Until then the dual-backend parity rule in `CLAUDE.md` stays fully in force.
**Last updated:** 2026-10-17

## Why this is parked here

OCTAVE keeps two backends with the same API: C++ (`src/`, ~28.8k lines) and Python (`backend/`, ~27.6k lines). Only C++ ships. Every release binary on every platform is built from `src/`, and the Jeep head unit (Orange Pi 5 Plus) runs the C++ build too. Parity roughly doubles the backend work: from July to mid-October 2026, 81 of the 97 commits that touched `src/` also had to touch `backend/`, and the wiki work doubles with them.

Python isn't dropped now because Rob wants a **complete, working Python version at v1.0** as a starting point for anyone who finds the project later and wants to fork a Python version. So parity holds through v1.0. Then Python is frozen as a snapshot and development carries on in C++ only.

Things to recheck before starting: v1.0 has actually shipped, and no outside contributors have turned up who depend on the Python backend. If someone has, talk to them first. A thin, unsupported Python layer might be worth keeping.

## The catch: dev tooling and tests run against Python

Freezing Python without doing this first would leave the shipped binary with no behavioural tests and no perf tooling:

- **All of `tests/`** (pytest, ~21 test files) imports and exercises `backend/`. CMake has **no test targets** (`enable_testing` / `add_test` / QtTest don't appear anywhere). CI's `cpp-compile-check` only checks that the tree compiles, plus a `qmllint` pass over `frontend/`.
- **`dev/main_dev.py`** does `import main` and layers on `dev/dev_runtime.py`, `dev/dev_tools_manager.py`, `dev/mock_obd_manager.py` (872 lines) and `dev/mock_imu.py`. The simulated OBD/IMU and keyboard controls only exist on the Python side.
- **The command socket** `backend/command_server.py` (1016 lines, JSON-lines over TCP on `localhost:9876`, active only with `--profile`) has **no C++ peer**. Everything that drives the app depends on it: `dev/mcp_server.py` (octave-perf MCP), `dev/mcp_benchmark.py` (octave-benchmark MCP), `dev/perf/run_perf_loop.py`, `dev/perf/brutalize.py`, `dev/screenshots/{stories,auto}.py` and `dev/shots_gap/*.py`.

None of this is affected: Python *build tooling* used by CI (`scripts/fetch_platform_tools.py`, `packaging/windows/resolve_dlls.py`, `wiki/build_search_index.py`) and the Android `assets/python-packages/ytmusic-bundle.zip`. They aren't part of the Python backend and stay.

## Plan

### Phase 0: before v1.0 (optional but recommended)

Starting phase 1 early means v1.0 itself gets tested on the binary that ships, not only on Python.

### Phase 1: move tooling to the C++ binary

1. **C++ command server.** Add `src/managers/commandserver.{h,cpp}` (QTcpServer on 127.0.0.1:9876, enabled by `--profile`) that speaks the same JSON-lines protocol and actions as `backend/command_server.py`. Then the MCP servers, perf scripts and screenshot tools work unchanged against `build/octave`. Port the actions in use first (grep the `dev/` clients for `"action":`), not all ~150 at once. Design it as the first draft of the long-term service API in `TODO/backend-service-split.md`, not as a throwaway dev hook.
2. **Mock/sim mode in C++.** Port `dev/mock_obd_manager.py` and `dev/mock_imu.py` behind a `--dev` / `--sim` flag (or a `OCTAVE_DEV` CMake option so it's compiled out of release builds), plus the `install_sim_commands` hooks from `dev/dev_runtime.py`. Decide whether the dev overlay (`dev/DevOverlay.qml`, `dev/DevToolsPanel.qml`) carries over as is. It's QML, so probably yes, once a C++ `devToolsManager` exists.
3. **Point the launchers at C++.** Make `dev/screenshots`, `dev/perf/*` and the MCP servers start `build/octave --profile` instead of `python main.py`. Update the `.mcp.json` entries for both machines (see the per-machine entries).
4. **C++ tests.** Add `enable_testing()` plus a `tests/cpp/` QtTest target. Port the pure-logic tests first: `test_elm327_dtc`, `test_obd_extended_pids`, `test_download_ytm_parse`, `test_dashboard_sharing`, `test_settings_persistence`, `test_adaptive_bitrate`, `test_esp32_read_buffer`. Integration-style tests (`test_boot`, `test_vehicle_gate_qml`, `test_phone_mirror_*`) can drive the binary through the command socket from pytest. Wire `ctest` into the `cpp-compile-check` CI job. Cross-check against `TODO/future-tests.md` so the two lists don't diverge.

### Phase 2: cut the snapshot

1. Make sure Python is at full parity with C++ at the v1.0 tag (diff the manager lists and the `Slot`/`Signal`/`Property` surfaces against the C++ headers).
2. Create a long-lived branch **`python-v1.0`** from the v1.0 tag. It's the fork point: `backend/`, `main.py`, `setup.py`, `requirements.txt`, the Python tests, the Python-driven `dev/` and the wiki exactly as at v1.0 (both backends documented).
3. In that branch's README, add a note: "Frozen Python/PySide6 backend as of OCTAVE v1.0. Not maintained. Fork freely."

### Phase 3: strip Python from `main`

1. Delete `backend/`, `main.py`, `setup.py`, `requirements.txt` and the Python-only smoke tests that phase 1 replaced.
2. CI (`.github/workflows/build.yml`): drop the Python `lint` and `test` jobs, or keep ruff limited to the remaining `dev/` and `scripts/` Python. Make the C++ build jobs depend on `ctest` instead.
3. **Wiki:** don't keep a second live wiki. The Python wiki lives on, frozen, in the `python-v1.0` branch. On `main`, remove the Python file paths, commands and code samples from every page that mentions them (as of today: architecture, building, development, getting-started, signals-slots-reference, settings-manager, settings-reference, every manager page, troubleshooting, and others; run `grep -l "backend/\|Python\|pytest" wiki/*.html`). Add one line on `index.html` linking to the `python-v1.0` branch. Rebuild the search index with `python wiki/build_search_index.py`.
4. **`CLAUDE.md`:** replace the dual-backend and parity sections with C++-only guidance and point to the `python-v1.0` branch. Update the related memory notes (`project_python_sunset_at_v1`, `feedback_python_desktop_only`).
5. README: say plainly that the project is C++ only and link the Python snapshot branch.

## Order of operations

Phase 1 (command server first, since it unblocks all the dev tooling at once; then the mocks, then the tests) → v1.0 tag with full parity → Phase 2 branch → Phase 3 strip. Don't start Phase 3 until `ctest` runs in CI and the MCP perf tools work against `build/octave`.

Delete this file when phase 3 lands.
