# Future Tests (Parked)

**Status:** Not committed to. This is a menu of things we could add to the test suite over time, organized by effort and value. Pick from it when you have the headspace for testing work.

**Last updated:** 2026-10-18

**Why parked:** the C++ QtTest suite now covers the subsystems that broke most often (ELM327 parsing, OBD demand polling, the simulated car, phone mirror, downloads, dashboards). What is left below is useful but not urgent, and each item is also a prerequisite for a step in `god-object-splits.md`, so it gets picked up when that work starts.

---

## Current state (baseline)

The suite lives in `tests/cpp/` (QtTest, one executable per `tst_*.cpp`, registered with `octave_add_test()` in `tests/cpp/CMakeLists.txt`). `ctest --test-dir build --output-on-failure` runs 17 tests, headless on the offscreen platform, and CI runs them on every push / PR:

- **Protocol and parsing:** `tst_elm327protocol` (DTC / freeze frame / VIN parsing, extended PIDs, multi-frame replies, every extended parameter wired), `tst_esp32readbuffer`, `tst_downloadutil`, `tst_adaptivebitrate`, `tst_scrcpyclient` (fake scrcpy server)
- **Managers:** `tst_settingsmanager` (backup restore after a truncated file, failed write leaves the file intact), `tst_dashboardmanager` (import sanitizer, export/import round trip), `tst_mediamanager` (missing tracks, shuffle, zero-byte covers), `tst_obdmanager` (demand-driven poll list, RFCOMM bind failure), `tst_obdadapterfinder`, `tst_phonemirror`, `tst_vehiclegate`
- **Integration:** `tst_obdsim` (the real `OBDManager` against `src/dev/simelm327`), `tst_boot` (the app boots, and `--dev --profile` answers on the command server)
- **QML:** `tst_qmlfixtures` (runs `tests/fixtures/*Smoke.qml` through `tests/cpp/qmlfixturehost`), `tst_phonemirrorview`, `tst_musicdeviceassets`

CI also runs a `qmllint` error pass over `frontend/` in the `cpp-compile-check` job, so the old "qmllint in CI" item is done and dropped from this list. So is the old ELM327 parser suite (`tst_elm327protocol`) and the Android stub-interface check (the C++ build uses `#ifdef Q_OS_ANDROID` inside the real managers, not separate stub classes, so there is nothing to drift).

That's the floor. Everything below is optional additions, ordered by value-to-effort ratio.

---

## Tier 1: Cheap wins (start here)

### 1. Settings round-trip test

**File:** extend `tests/cpp/tst_settingsmanager.cpp`
**Effort:** ~1 hour
**Value:** High — catches JSON schema drift

The existing tests prove one value survives a truncated file. Add a test that sets a broad spread of settings through the real setters (`setCurrentVolume`, `save_theme_setting`, `save_media_folder`, `save_obd_saved_adapters`, `save_home_obd_parameters`, a custom theme, a pinned setting, ...), calls `flushPendingSave()`, destroys the manager, constructs a new one in the same temp config dir and checks every property. The existing `init()` already gives each test its own `XDG_CONFIG_HOME` / `HOME` / `APPDATA`.

Catches: someone renames a JSON key, someone removes a default, someone breaks validation when adding a new field. Users would otherwise only find this when they update OCTAVE and discover their saved settings got cleared — which is terrible.

**Bonus:** also test the upgrade path. Write an "old format" `settingsConfigure.json` (missing newer keys, a key with the wrong type) into the temp dir first, construct `SettingsManager`, and assert it reads cleanly, fills in defaults and keeps the valid old values. This catches schema-evolution bugs in `validateSettings()`.

### 2. VolumeController dispatch test

**File:** new `tests/cpp/tst_volumecontroller.cpp`, `octave_add_test(tst_volumecontroller SOURCES managers/volumecontroller.h managers/volumecontroller.cpp managers/settingsmanager.h managers/settingsmanager.cpp LIBS Qt6::Gui)`
**Effort:** ~30 minutes
**Value:** High — guards the single volume entry point

`VolumeController` (`src/managers/volumecontroller.{h,cpp}`) persists the percent to `SettingsManager` and emits `volumeApplied(percent, linear)`; `main.cpp` connects the outputs to that signal. Test it with a `QSignalSpy`:

- `applyVolume(75)` emits once with `(75, 0.5625)` and `settingsManager.currentVolume()` becomes 75
- Clamping: `applyVolume(-5)` → 0, `applyVolume(150)` → 100
- `toLinear()` and `toLinearStatic()` agree and follow `(percent/100)^2`

Give it a temp `XDG_CONFIG_HOME` like `tst_settingsmanager`. Tiny test, good coverage on a subsystem that has drifted before.

---

## Tier 2: Medium effort, good payoff

### 3. Music metadata sanitization tests

**File:** extend `tests/cpp/tst_mediamanager.cpp`
**Effort:** ~1 hour
**Value:** Medium

`MediaManager::_sanitize_metadata(text, maxLength)` (`src/managers/mediamanager.cpp`, around line 302) is static and the test class is already a friend (`friend class TstMediaManager` in `mediamanager.h`). Pass in strings with:

- Control characters (`\x00`, `\x1f`)
- Emoji (`🎵`, `🎸`)
- Mis-decoded text (UTF-8 bytes read as Latin-1)
- Empty strings
- Unicode normalization quirks (combining characters, NFC vs NFD)
- Extremely long strings (the `maxLength` cap, for malformed tags)

Assert the cleaned output. Prevents regressions in how OCTAVE displays song titles from badly-tagged files — which is a surprisingly large fraction of real-world music libraries. If it is worth the effort, also feed a few small tagged files through `_read_tags()`.

### 4. Signal wiring tests

**File:** new `tests/cpp/tst_signalwiring.cpp`, or cases added to the manager tests that already exist
**Effort:** ~3 hours
**Value:** Medium-high

The per-manager tests check each manager on its own. The connections between managers are made in `src/main.cpp` and only `tst_boot` exercises them, without asserting anything about them. Pull the cross-manager `connect()` calls into a function the test can call (or build the same graph in the test), then use `QSignalSpy` / `QTRY_COMPARE` for each critical path:

- `volumeController.applyVolume(75)` reaches `MediaManager::setVolume` with the curved value
- A track change in `MediaManager` updates the playback state saved through `SettingsManager`
- Changing `obdBluetoothPort` in settings triggers `OBDManager`'s debounced reconnect

Catches signal/slot wiring regressions — the kind of thing that silently breaks when someone renames a signal and forgets to update a `connect()` in `main.cpp`.

---

## Tier 3: High effort, worth it eventually

### 5. Headless QML integration tests

**File:** new fixtures in `tests/fixtures/`, run by `tests/cpp/tst_qmlfixtures.cpp`
**Effort:** ~1 day initial, ongoing maintenance
**Value:** High — but only once the tests are reliably green

The fixture host exists (`tests/cpp/qmlfixturehost.cpp`, used for the music-device and wiki smokes), and `tst_phonemirrorview.cpp` shows the pattern of a fake manager behind a real view. Extend that to the cross-cutting UI paths: load one view with real or fake managers as context properties, drive it with `QTest::mouseClick()` or property writes, assert the resulting state.

Catches bugs only a full integration test can see:
- "The volume slider doesn't update when the gesture sensor changes volume"
- "The settings page doesn't reflect changes made via the ESP32 knob"

**Warning:** these tests are notoriously flaky on headless runners if you're not careful — timing issues, missing platform plugins, subpixel rendering differences. Start with ONE test that you can get reliably green in CI, then expand slowly.

### 6. OBD protocol replay suite

**File:** new `tests/cpp/tst_obdreplay.cpp`, fixtures under `tests/fixtures/obd/{make}/{model}/`
**Effort:** ~2 days initial, ongoing as new cars are added
**Value:** Very high — the simulated car only speaks the dialect we wrote

`tst_obdsim` already drives the real `OBDManager` through `OBDConnectionWorker::setCustomTransport()` with `src/dev/simelm327`. The replay suite uses the same hook with a `QIODevice` that answers from recorded traffic: capture real adapter request/response pairs from several cars and ELM327 clones (different makes, firmware versions, the Jeep first), check them in as JSON fixtures, and replay them.

Tests the OBD subsystem against real-world quirks — echo and prompt handling, `SEARCHING...`, `NO DATA`, `CAN ERROR`, slow clones, multi-frame replies, protocol negotiation, diagnostic mode and reconnects — all without a car.

**This is the prerequisite for safely splitting `obdmanager.cpp`** (see `god-object-splits.md`). Without this, the split is "refactor and pray."

---

## Things to skip

### Snapshot testing QML visuals

Flaky on headless runners (subpixel AA differences, driver-specific color variations, timing). Rewards churn (every theme tweak breaks 50 snapshots) over substance. There are exceptions if you only snapshot a few critical pages, but in general: not worth it for this project. The dev screenshot tooling is for humans, not assertions.

### Mocking every manager in every test

Brittle — every time you add a method or rename a signal, dozens of fakes need updating. Fake ONLY at the seam you're testing. Testing `VolumeController::applyVolume()`? Use a real `SettingsManager` in a temp dir and a `QSignalSpy`. Don't reach for a universal mock framework.

### 100% coverage as a goal

Coverage percentage is a vanity metric. It rewards writing trivial tests that pass through getters and setters, and it doesn't distinguish a test that catches real bugs from a test that exists just to hit lines.

**Better target:** every silent failure mode has a regression test. Every time a bug ships that "should have been caught," write the test that would have caught it before fixing the bug. Over time, the suite organically covers the failure modes that actually matter.

---

## Recommended next batch (if we ever do this)

If you want the maximum bang for buck in one day of testing work, do these together:

1. **VolumeController dispatch test** (~30 minutes) — protects the single volume entry point
2. **Settings round-trip test** (~1 hour) — unblocks the `settingsmanager.cpp` split
3. **Metadata sanitization tests** (~1 hour) — with item 2, unblocks the `mediamanager.cpp` split

That leaves most of the day for the signal wiring tests. The OBD replay suite is its own project; start it when there is time to record traffic in the Jeep.

Delete this file when every item above is either done or explicitly dropped.
