# Future Tests (Parked)

**Goal:** add the tests that make the god-object splits safe and catch the silent failures the current suite misses.

**Status:** deferred. A menu, not a commitment; each item is also a prerequisite for a step in `god-object-splits.md`, so it gets picked up when that work starts.

**Last updated:** 2026-10-18

**Why parked:** the C++ QtTest suite (`tests/cpp/`, 18 tests via `octave_add_test()` in `tests/cpp/CMakeLists.txt`, run by `ctest --test-dir build --output-on-failure` in CI) already covers the subsystems that broke most often: ELM327 parsing, OBD demand polling, the simulated car, phone mirror, downloads, dashboards, settings backup/atomic write. What's left is useful but not urgent.

Items are ordered easiest first.

---

## 1. VolumeController dispatch test

**Effort:** S (~30 min)
**Unblocks:** the `mediamanager.cpp` split

`VolumeController` (`src/managers/volumecontroller.{h,cpp}`) persists the percent to `SettingsManager` and emits `volumeApplied(percent, linear)`; `main.cpp` connects the outputs to that signal.

Steps:
1. New `tests/cpp/tst_volumecontroller.cpp`; register with `octave_add_test(tst_volumecontroller SOURCES managers/volumecontroller.h managers/volumecontroller.cpp managers/settingsmanager.h managers/settingsmanager.cpp LIBS Qt6::Gui)`.
2. Give it a temp `XDG_CONFIG_HOME` / `HOME` / `APPDATA` like `tst_settingsmanager::init()`.
3. With a `QSignalSpy`: `applyVolume(75)` emits once with `(75, 0.5625)` and `settingsManager.currentVolume()` becomes 75.
4. Clamping: `applyVolume(-5)` → 0, `applyVolume(150)` → 100.
5. `toLinear()` and `toLinearStatic()` agree and follow `(percent/100)^2`.

## 2. Metadata sanitization tests

**Effort:** S (~1 h)
**Unblocks:** the `mediamanager.cpp` split (with item 1)

`MediaManager::_sanitize_metadata(text, maxLength)` (`src/managers/mediamanager.cpp:301`) is static and `TstMediaManager` is already a friend (`mediamanager.h:178`).

Steps:
1. Add a data-driven case to `tests/cpp/tst_mediamanager.cpp` covering: control characters (`\x00`, `\x1f`), emoji, UTF-8 bytes mis-decoded as Latin-1, empty strings, combining characters / NFC vs NFD, and over-long strings (the `maxLength` cap).
2. Assert the cleaned output.
3. Optional: feed a few small tagged files through `_read_tags()` (`mediamanager.cpp:737`).

## 3. Settings round-trip and upgrade test

**Effort:** S–M (1–2 h)
**Unblocks:** the `settingsmanager.cpp` split

Today `tst_settingsmanager.cpp` only proves one value survives a truncated file and that a failed write leaves the file intact.

Steps:
1. Set a broad spread of settings through the real setters (`setCurrentVolume`, `save_theme_setting`, `save_media_folder`, `save_obd_saved_adapters`, `save_home_obd_parameters`, a custom theme, a pinned setting, ...).
2. `flushPendingSave()`, destroy the manager, construct a new one in the same temp config dir, check every property.
3. Upgrade path: write an "old format" `settingsConfigure.json` (missing newer keys, one key with the wrong type), construct `SettingsManager`, assert it loads cleanly, fills defaults and keeps the valid old values (exercises `validateSettings()`).

## 4. Signal wiring tests

**Effort:** M (~3 h, including the `main.cpp` refactor)

The ~27 cross-manager `connect()` calls live inline in `src/main.cpp`; only `tst_boot` exercises them, and it asserts nothing about them.

Steps:
1. Move the cross-manager `connect()` calls into a function (e.g. `wireManagers(...)`) that both `main.cpp` and a test can call.
2. New `tests/cpp/tst_signalwiring.cpp`; with `QSignalSpy` / `QTRY_COMPARE`, check:
   - `volumeController.applyVolume(75)` reaches `MediaManager::setVolume` with the curved value
   - a track change in `MediaManager` updates the playback state saved through `SettingsManager`
   - changing `obdBluetoothPort` in settings triggers `OBDManager`'s debounced reconnect

## 5. Headless QML integration tests

**Effort:** L (~1 day to get the first one reliably green, then ongoing)

The setup exists: `tests/cpp/qmlfixturehost.cpp`, `tst_qmlfixtures` (runs `tests/fixtures/*Smoke.qml`) and the fake-manager-behind-a-real-view pattern in `tst_phonemirrorview.cpp`.

Steps:
1. Pick ONE cross-cutting path, e.g. "the volume slider follows a volume change from the gesture sensor / ESP32 knob".
2. Load that view with real or fake managers as context properties, drive it with `QTest::mouseClick()` or property writes, assert the state.
3. Get it reliably green in CI before adding a second (headless timing and platform-plugin flakiness is the main risk).

## 6. OBD protocol replay suite

**Effort:** L–XL (~2 days initial, plus recording time in the car; ongoing as cars are added)
**Unblocks:** the `obdmanager.cpp` split. Without it that split is "refactor and pray."

`tst_obdsim` drives the real `OBDManager` through `OBDConnectionWorker::setCustomTransport()` with `src/dev/simelm327`, but the simulator only speaks the dialect we wrote.

Steps:
1. Add a capture mode (or a logging transport) that records adapter request/response pairs with timing.
2. Record traffic from the Jeep first, then other cars and ELM327 clones; check in as JSON under `tests/fixtures/obd/{make}/{model}/`.
3. New `tests/cpp/tst_obdreplay.cpp`: a `QIODevice` that answers from a fixture, plugged in via `setCustomTransport()`.
4. Cover echo/prompt handling, `SEARCHING...`, `NO DATA`, `CAN ERROR`, slow clones, multi-frame replies, protocol negotiation, diagnostic mode and reconnects.

---

**Skip:** QML snapshot tests, mock-every-manager frameworks and coverage-percentage targets; instead write a regression test for each bug that ships.

Delete this file when every item above is either done or explicitly dropped.
