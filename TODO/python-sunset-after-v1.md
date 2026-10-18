# Freeze the Python backend at v1.0, carry only C++ forward

**Status:** deferred (scheduled): do it after v1.0 ships. Until then the dual-backend parity rule in `CLAUDE.md` stays fully in force.
**Last updated:** 2026-10-18

## Why this is parked here

OCTAVE keeps two backends with the same API: C++ (`src/`, ~28.8k lines) and Python (`backend/`, ~27.6k lines). Only C++ ships. Every release binary on every platform is built from `src/`, and the Jeep head unit (Orange Pi 5 Plus) runs the C++ build too. Parity roughly doubles the backend work: from July to mid-October 2026, 81 of the 97 commits that touched `src/` also had to touch `backend/`, and the wiki work doubles with them.

Python isn't dropped now because Rob wants a **complete, working Python version at v1.0** as a starting point for anyone who finds the project later and wants to fork a Python version. So parity holds through v1.0. Then Python is frozen as a snapshot and development carries on in C++ only.

Things to recheck before starting: v1.0 has actually shipped, and no outside contributors have turned up who depend on the Python backend. If someone has, talk to them first. A thin, unsupported Python layer might be worth keeping.

## The catch: tests still run against Python

The dev tooling moved to the C++ binary on 2026-10-18 (phase 1 steps 1-3 below). What is left:

- **All of `tests/`** (pytest, ~21 test files) imports and exercises `backend/`. CMake has **no test targets** (`enable_testing` / `add_test` / QtTest tests don't appear anywhere; Qt Test is linked into Debug builds only for the command server's synthetic input). CI's `cpp-compile-check` only checks that the tree compiles, plus a `qmllint` pass over `frontend/`.
- **Python's in-app dev peers** (`backend/command_server.py`, `backend/perf_monitor.py`, `backend/perf_patches.py`, `dev/main_dev.py`, `dev/dev_runtime.py`, `dev/mock_obd_manager.py`, `dev/mock_imu.py`, `dev/dev_tools_manager.py`, `dev/collage/bridge.py`) are now only needed to run the tooling against the Python backend (`OCTAVE_BACKEND=python`). They go into the `python-v1.0` snapshot and are deleted from `main` in phase 3 together with `backend/`. The clients in `dev/` (MCP servers, screenshot / story / collage / perf scripts) stay: they talk to either backend over TCP.

None of this is affected: Python *build tooling* used by CI (`scripts/fetch_platform_tools.py`, `packaging/windows/resolve_dlls.py`, `wiki/build_search_index.py`) and the Android `assets/python-packages/ytmusic-bundle.zip`. They aren't part of the Python backend and stay.

## Plan

### Phase 0: before v1.0 (optional but recommended)

Starting phase 1 early means v1.0 itself gets tested on the binary that ships, not only on Python.

### Phase 1: move tooling to the C++ binary

Steps 1-3 landed 2026-10-18: `src/dev/` (behind the `OCTAVE_DEV_TOOLS` CMake option) has the command server with every Python action plus the collage and `sim_*` actions, the perf monitor (`OCTAVE_PERF_SCOPE` replaces `perf_patches.py`), a simulated ELM327 under the real `OBDManager` instead of a mock manager, the IMU feed, `devToolsManager` and the overlay QML (moved to `src/dev/qml/`). `dev/app_launch.py` makes every launcher start the C++ binary; the MCP servers needed no change beyond their hints because they only talk TCP. See `wiki/development.html`.

4. **C++ tests.** Add `enable_testing()` plus a `tests/cpp/` QtTest target. Port the pure-logic tests first: `test_elm327_dtc`, `test_obd_extended_pids`, `test_download_ytm_parse`, `test_dashboard_sharing`, `test_settings_persistence`, `test_adaptive_bitrate`, `test_esp32_read_buffer`. Integration-style tests (`test_boot`, `test_vehicle_gate_qml`, `test_phone_mirror_*`) can drive the binary through the command socket from pytest. Wire `ctest` into the `cpp-compile-check` CI job. Cross-check against `TODO/future-tests.md` so the two lists don't diverge.

### Phase 2: cut the snapshot

1. Make sure Python is at full parity with C++ at the v1.0 tag (diff the manager lists and the `Slot`/`Signal`/`Property` surfaces against the C++ headers).
2. Create a long-lived branch **`python-v1.0`** from the v1.0 tag. It's the fork point: `backend/`, `main.py`, `setup.py`, `requirements.txt`, the Python tests, the Python-driven `dev/` and the wiki exactly as at v1.0 (both backends documented).
3. In that branch's README, add a note: "Frozen Python/PySide6 backend as of OCTAVE v1.0. Not maintained. Fork freely."

### Phase 3: strip Python from `main`

1. Delete `backend/`, `main.py`, `setup.py`, `requirements.txt` and the Python-only smoke tests that phase 1 replaced. On the developer machines (gitignored, synced by Dropbox) also delete the Python in-app dev peers listed above and the `python` branch of `dev/app_launch.py`.
2. CI (`.github/workflows/build.yml`): drop the Python `lint` and `test` jobs, or keep ruff limited to the remaining `dev/` and `scripts/` Python. Make the C++ build jobs depend on `ctest` instead.
3. **Wiki:** don't keep a second live wiki. The Python wiki lives on, frozen, in the `python-v1.0` branch. On `main`, remove the Python file paths, commands and code samples from every page that mentions them (as of today: architecture, building, development, getting-started, signals-slots-reference, settings-manager, settings-reference, every manager page, troubleshooting, and others; run `grep -l "backend/\|Python\|pytest" wiki/*.html`). Add one line on `index.html` linking to the `python-v1.0` branch. Rebuild the search index with `python wiki/build_search_index.py`.
4. **`CLAUDE.md`:** replace the dual-backend and parity sections with C++-only guidance and point to the `python-v1.0` branch. Update the related memory notes (`project_python_sunset_at_v1`, `feedback_python_desktop_only`).
5. README: say plainly that the project is C++ only and link the Python snapshot branch.

## Order of operations

Phase 1 step 4 (the C++ tests) → v1.0 tag with full parity → Phase 2 branch → Phase 3 strip. Don't start Phase 3 until `ctest` runs in CI. The MCP perf tools already work against `build/octave`; take a perf baseline on the C++ build before v1.0 (Python baselines don't carry over: the loop-latency probe differs, see the wiki's profiling section).

Delete this file when phase 3 lands.
