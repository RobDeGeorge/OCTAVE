# Future Tests (Parked)

**Goal:** catch the silent failures the current suite misses, and make bigger changes to the managers safe.

**Status:** deferred. A menu, not a commitment; pick an item up when work in its area starts.

**Last updated:** 2026-11-01

**Why parked:** the C++ QtTest suite (`tests/cpp/`, 45 tests via `octave_add_test()` in `tests/cpp/CMakeLists.txt`, run by `ctest --test-dir build --output-on-failure` in CI) already covers the subsystems that broke most often: ELM327 parsing, OBD demand polling, the simulated car, phone mirror, dashboards, settings backup/atomic write. What's left is useful but not urgent.

Items are ordered easiest first. Done 2026-10-18: the VolumeController test (`tst_volumecontroller`), metadata sanitizing cases in `tst_mediamanager` (which found and fixed emoji being stripped from tags), and the settings round-trip and older-file tests in `tst_settingsmanager`.

---

## 1. Signal wiring tests: the hardware inputs

**Effort:** S once the core library exists (`TODO/open-backend.md`), else M

Done 2026-11-01, on the real binary instead of a QtTest: `tests/wiring/check_wiring.py` (ctest `wiring`) boots `octave --headless --sim` and checks over the service API that volume and mute reach the outputs on the curve and are saved, RPM reaches the shift light, readings reach `paramStore` and the black box, the gauge unit settings reach the catalog, and a track change is saved and lights the visualizer. A QtTest that builds `AppCore` itself was not possible: the tests compile their own sources and there is no manager library yet.

Left: the wires whose source is hardware the service cannot drive: `GestureManager::actionTriggered` and the ESP32 knob's `volumeChangeRequested` / `muteToggleRequested` into `VolumeController` / `NowPlaying` (`AppCore::wireInputs`), and the knob LED colour (`wireKnobLed`). Either a QtTest on `AppCore` once the core library lands (emit the signals, spy on `VolumeController`), or a dev-only `sim` call that emits them. Also not wiring but nearby: `obdBluetoothPort` changing triggers `OBDManager`'s debounced reconnect, which belongs in `tst_obdsim`.

## 2. Headless QML integration tests

**Effort:** L (~1 day to get the first one reliably green, then ongoing)

The setup exists: `tests/cpp/qmlfixturehost.cpp`, `tst_qmlfixtures` (runs `tests/fixtures/*Smoke.qml`) and the fake-manager-behind-a-real-view pattern in `tst_phonemirrorview.cpp`.

Steps:
1. Pick ONE cross-cutting path, e.g. "the volume slider follows a volume change from the gesture sensor / ESP32 knob".
2. Load that view with real or fake managers as context properties, drive it with `QTest::mouseClick()` or property writes, assert the state.
3. Get it reliably green in CI before adding a second (headless timing and platform-plugin flakiness is the main risk).

## 3. OBD protocol replay suite

**Effort:** L–XL (~2 days initial, plus recording time in the car; ongoing as cars are added)
**Unblocks:** any large change to `obdmanager.cpp`. Without it, refactoring the OBD worker is "refactor and pray."

`tst_obdsim` drives the real `OBDManager` through `OBDConnectionWorker::setCustomTransport()` with `src/sim/simelm327`, but the simulator only speaks the dialect we wrote.

Steps:
1. Add a capture mode (or a logging transport) that records adapter request/response pairs with timing.
2. Record traffic from the Jeep first, then other cars and ELM327 clones; check in as JSON under `tests/fixtures/obd/{make}/{model}/`.
3. New `tests/cpp/tst_obdreplay.cpp`: a `QIODevice` that answers from a fixture, plugged in via `setCustomTransport()`.
4. Cover echo/prompt handling, `SEARCHING...`, `NO DATA`, `CAN ERROR`, slow clones, multi-frame replies, protocol negotiation, diagnostic mode and reconnects.

---

**Skip:** QML snapshot tests, mock-every-manager frameworks and coverage-percentage targets; instead write a regression test for each bug that ships.

Delete this file when every item above is either done or explicitly dropped.
