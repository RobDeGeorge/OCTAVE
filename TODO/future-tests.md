# Future Tests (Parked)

**Status:** Not committed to. This is a menu of things we could add to the test suite over time, organized by effort and value. Pick from it when you have the headspace for testing work.

**Last updated:** 2026-04-11

---

## Current state (baseline)

The smoke test suite at `tests/test_smoke.py` runs 21 tests in CI on every push / PR:

- **16 import tests** (one per backend module) — catches syntax errors, missing dependencies, top-level crashes
- **QApplication init test** — proves headless Qt startup works
- **Volume curve math test** — pins the quadratic curve in `backend/volume_utils.py`
- **Safe manager instantiation test** — constructs `SettingsManager` / `AudioAnalyzer` / `NetworkManager` with a tmp config dir

That's the floor. Everything below is optional additions, ordered by value-to-effort ratio.

---

## Tier 1: Cheap wins (start here)

### 1. ELM327 protocol unit tests

**File:** `tests/test_elm327_protocol.py`
**Effort:** ~2 hours
**Value:** Very high

`backend/elm327_protocol.py` is pure Python with no I/O. It parses ELM327 adapter responses into PID values. Feed it recorded byte strings, assert the parsed output.

**Why this is the single highest-value test suite we could add:** OBD bugs are silent and hardware-dependent. Right now there's no way to catch a parser regression without plugging into a real car. This suite would give us confidence to refactor the OBD subsystem without physical hardware in the loop — making `god-object-splits.md` viable for `obd_manager.py`.

**How to build it:** record real adapter responses using the MCP server in dev mode, check them in as fixtures under `tests/fixtures/elm327/`, parametrize a test that feeds each fixture through the parser and asserts the expected output. Cover the major PID types (coolant temp, RPM, speed, fuel level, throttle position) plus edge cases (NO DATA responses, adapter timeouts, protocol-level errors).

### 2. Settings round-trip test

**File:** `tests/test_settings_manager.py`
**Effort:** ~1 hour
**Value:** High — catches JSON schema drift

```python
def test_settings_persist_across_restart(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    from backend.settings_manager import SettingsManager

    s1 = SettingsManager()
    s1.setCurrentVolume(42)
    s1.setThemeSetting("CosmicVoyager")
    # ... set a bunch of other settings ...
    del s1

    s2 = SettingsManager()
    assert s2.currentVolume == 42
    assert s2.themeSetting == "CosmicVoyager"
```

Catches: someone renames a JSON key, someone removes a default, someone breaks migration logic when adding a new field. Users would otherwise only find this when they update OCTAVE and discover their saved settings got cleared — which is terrible.

**Bonus:** also test the migration path. Write an "old format" settings JSON file to the tmp dir first, construct `SettingsManager`, assert it reads cleanly and upgrades to the new format. This catches schema-evolution bugs.

### 3. VolumeController dispatch test

**File:** `tests/test_volume_controller.py`
**Effort:** ~20 minutes
**Value:** High — guards the Phase 3 refactor

```python
from unittest.mock import MagicMock
from backend.volume_utils import VolumeController

def test_apply_volume_dispatches_to_all_outputs():
    media = MagicMock()
    spotify = MagicMock()
    spotify.is_connected.return_value = True
    phone_mirror = MagicMock()
    esp32 = MagicMock()
    settings = MagicMock()

    vc = VolumeController(settings, media, spotify, phone_mirror, esp32)
    vc.applyVolume(75)

    settings.setCurrentVolume.assert_called_once_with(75)
    media.setVolume.assert_called_once_with(0.5625)  # (75/100)^2
    spotify.set_volume.assert_called_once_with(75)
    phone_mirror.setVolume.assert_called_once_with(0.5625)
    esp32.send_volume_update.assert_called_once_with(75)
```

Would have caught every volume-sync bug we fixed in Phase 3. Tiny test, huge coverage on a fragile subsystem. Also cover edge cases:

- Clamping: `applyVolume(-5)` → 0, `applyVolume(150)` → 100
- Spotify disconnected: `spotify.is_connected.return_value = False` — should NOT call `spotify.set_volume`
- Android mode: `phone_mirror=None` — should not crash, should still dispatch to the others
- Per-output None: any single output being None should not crash the dispatch

### 4. qmllint in CI

**Where:** `.github/workflows/build.yml` lint job
**Effort:** ~30 minutes
**Value:** Medium-high

Add a step to the existing `lint` job:

```yaml
- name: Install PySide6 for qmllint
  run: pip install PySide6==6.8.0
- name: Run qmllint on all QML files
  run: pyside6-qmllint frontend/*.qml frontend/settings/*.qml
```

Catches: QML syntax errors, missing imports, unresolved property references, type mismatches, deprecated API usage. The `scrcpy_capture.py` missing-`Qt`-import bug that ruff caught in Phase 2 had a direct QML equivalent waiting to happen — qmllint would catch that class of bug on the QML side.

**Caveat:** qmllint has some false positives with Qt Quick Controls and custom properties. Start with it as warn-only (`|| true`) to see the baseline noise, then tighten once the noise is characterized.

---

## Tier 2: Medium effort, good payoff

### 5. Spotify error rate-limiter test

**File:** `tests/test_spotify_rate_limiter.py`
**Effort:** ~2 hours
**Value:** Medium — guards load-bearing Phase 2 logic

The rate limiter we built in Phase 2 (`POLL_ERROR_THRESHOLD=5`, `POLL_ERROR_EMIT_INTERVAL=30s`) is the kind of code that silently rots — nobody notices if the threshold gets bumped accidentally or if the timer logic breaks. Test it:

```python
def test_poll_error_rate_limiting(qapp, monkeypatch):
    from backend.spotify_manager import SpotifyManager
    from unittest.mock import MagicMock

    sm = SpotifyManager()
    fake_time = [0.0]
    monkeypatch.setattr("time.time", lambda: fake_time[0])

    # Mock _sp to always raise
    sm._sp = MagicMock()
    sm._sp.current_playback.side_effect = Exception("network down")

    errors_emitted = []
    sm.errorOccurred.connect(lambda msg: errors_emitted.append(msg))

    # Force synchronous execution of the thread pool for testing
    # ... (monkeypatch _executor.submit to run synchronously) ...

    # Fire the fetch 10 times — should emit once at the threshold (5)
    for _ in range(10):
        sm._poll_playback_state()

    assert len(errors_emitted) == 1  # not 10

    # Advance time past the interval, should emit again
    fake_time[0] += 31.0
    sm._poll_playback_state()
    assert len(errors_emitted) == 2
```

Fiddly because of the thread pool — may need to monkeypatch `_executor.submit` to run synchronously. Worth it, because this logic is exactly the kind of thing that breaks silently and costs a day of debugging.

### 6. Music metadata sanitization tests

**File:** `tests/test_media_metadata.py`
**Effort:** ~1 hour
**Value:** Medium

`media_manager.py` has ID3 tag sanitization logic (control character removal, encoding normalization). Pure functions, easy to test. Pass in strings with:

- Control characters (`\x00`, `\x1f`)
- Emoji (`🎵`, `🎸`)
- Mixed encodings (UTF-8 bytes interpreted as Latin-1)
- Empty strings
- Unicode normalization quirks (combining characters, NFC vs NFD)
- Extremely long strings (DoS protection for malformed tags)

Assert the cleaned output. Prevents regressions in how OCTAVE displays song titles from badly-tagged MP3s — which is a surprisingly large fraction of real-world music libraries.

### 7. pytest-qt signal assertion tests

**File:** `tests/test_signals.py`
**Effort:** ~3 hours
**Value:** Medium-high

Add `pytest-qt` as a dev dependency. It provides `qtbot.waitSignal(signal, timeout=100)` — a clean way to assert signals fire within a time window.

Write one test per critical signal path:

- "Calling `media_manager.next_track()` emits `currentTrackChanged` within 100ms"
- "Calling `spotify_manager.set_volume(50)` emits `volumeChanged(50)`"
- "Calling `volume_controller.applyVolume(75)` emits `currentVolumeChanged(75)` on `settings_manager`"

Catches signal/slot wiring regressions — the kind of thing that silently breaks when someone renames a signal and forgets to update all the `.connect()` callers.

### 8. Android stub interface test

**File:** extend `tests/test_smoke.py`
**Effort:** ~1 hour
**Value:** Medium

The Android build uses `backend/stubs.py` to replace hardware managers. If the real `OBDManager` grows a method that QML calls, and the stub doesn't have it, the Android build breaks silently at runtime.

```python
def test_stubs_cover_real_interfaces():
    from backend import stubs
    from backend import obd_manager, berryimu_manager
    # ... etc ...

    pairs = [
        (stubs.StubOBDManager, obd_manager.OBDManager),
        (stubs.StubBerryIMUManager, berryimu_manager.BerryIMUManager),
        # ... etc ...
    ]

    for stub_cls, real_cls in pairs:
        real_public = {m for m in dir(real_cls) if not m.startswith("_")}
        stub_public = {m for m in dir(stub_cls) if not m.startswith("_")}
        missing = real_public - stub_public
        assert not missing, f"{stub_cls.__name__} missing: {missing}"
```

Would catch: "QML calls `obd_manager.new_feature()`, Android build crashes at runtime because the stub doesn't have `new_feature()`."

---

## Tier 3: High effort, worth it eventually

### 9. Headless QML integration test

**File:** `tests/test_qml_integration.py`
**Effort:** ~1 day initial, ongoing maintenance
**Value:** High — but only once the suite exists and is reliably green

Build a minimal `tests/fixtures/TestMain.qml` that loads a single view, inject mock managers via `engine.rootContext().setContextProperty()`, use `QTest.mouseClick()` to trigger user interactions, assert resulting state.

Catches bugs only a full integration test can see:
- "The volume slider doesn't update when the gesture sensor changes volume"
- "Clicking 'Connect Spotify' doesn't open the OAuth browser"
- "The settings page doesn't reflect changes made via the ESP32 knob"

**Warning:** these tests are notoriously flaky on headless runners if you're not careful — timing issues, missing platform plugins, subpixel rendering differences. Start with ONE test that you can get reliably green in CI, then expand slowly. Don't try to build a whole integration suite on day one.

### 10. OBD protocol replay suite

**File:** `tests/test_obd_replay.py`
**Effort:** ~2 days initial, ongoing as new cars are added
**Value:** Very high — tests a subsystem that's currently impossible to test without hardware

Record real adapter responses from several cars (different makes, models, ELM327 firmware versions) via the MCP server. Save them as JSON fixtures in `tests/fixtures/obd/{make}/{model}/`. Replay them through `OBDManager` with a mocked serial port that returns the recorded bytes.

Tests the entire OBD subsystem — connection state machine, watchdog timer, diagnostic-mode cancellation, protocol negotiation, watcher refresh — all without a car.

**This is the prerequisite for safely splitting `obd_manager.py`** (see `god-object-splits.md`). Without this, the `obd_manager.py` split is "refactor and pray."

---

## Things to skip

### Snapshot testing QML visuals

Flaky on headless runners (subpixel AA differences, driver-specific color variations, timing). Rewards churn (every theme tweak breaks 50 snapshots) over substance. There are exceptions if you only snapshot a few critical pages, but in general: not worth it for this project.

### Mocking every manager in every test

Brittle — every time you add a method or rename a signal, dozens of mocks need updating. Mock ONLY at the seam you're testing. Testing `VolumeController.applyVolume()`? Mock the five managers it calls. Testing `SpotifyManager.refresh_devices()`? Mock `self._sp` (the spotipy client). Don't reach for a universal mock framework.

### 100% coverage as a goal

Coverage percentage is a vanity metric. It rewards writing trivial tests that pass through getters and setters, and it doesn't distinguish a test that catches real bugs from a test that exists just to hit lines.

**Better target:** every silent failure mode has a regression test. Every time a bug ships that "should have been caught," write the test that would have caught it before fixing the bug. Over time, the suite organically covers the failure modes that actually matter.

---

## Recommended next batch (if we ever do this)

If you want the maximum bang for buck in one day of testing work, do these three together:

1. **ELM327 protocol tests** (~2 hours) — unlocks OBD refactoring later
2. **VolumeController dispatch test** (~20 minutes) — protects the Phase 3 refactor
3. **qmllint in CI** (~30 minutes) — catches a whole class of QML-side bugs for free

Together they cover the three most fragile subsystems in the app (OBD parsing, volume sync, QML) and take ~3 hours combined — leaving most of the day for the Settings round-trip test and music metadata sanitization tests if you're still feeling it.
