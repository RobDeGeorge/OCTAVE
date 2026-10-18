# God Object Splits (Parked)

**Status:** Deferred. This is item #4 from the original cleanup punch list — a refactor, not a bug fix. Not urgent. Should ideally come AFTER `future-tests.md` lands enough safety nets to make the refactor safe.

**Last updated:** 2026-10-18

> This plan was first written against the Python backend (`backend/settings_manager.py`, `media_manager.py`, `obd_manager.py`). The Python backend has since been removed (last version on the `python-final` branch), so the plan now covers the C++ managers that replaced them.

---

## The three targets

Three C++ manager files are well over 2000 lines and handle multiple distinct responsibilities. They violate single-responsibility hard enough that making changes to them is risky — every edit could touch unrelated subsystems. Line counts are from `wc -l` on 2026-10-18.

| File | Lines (.cpp / .h) | Current responsibilities |
|---|---|---|
| `src/managers/settingsmanager.cpp` | 2315 / 720 | Settings registry + defaults (`buildSettingsRegistry`, `buildDefaultSettings`) + OS-specific path resolution (`getAppDataDir`) + JSON load/validate/backup/atomic write + debounced save + every `Q_PROPERTY` getter and `save_*` setter + saved OBD adapters + directory history + custom themes and theme-art download + settings-section visibility and pinning + `reset_to_defaults` |
| `src/managers/mediamanager.cpp` | 3266 / 344 | `QMediaPlayer` wrapping + playback recovery and bad-track skipping + TagLib tag reading and metadata store + album art extraction (MP3 / MP4 / FLAC / OGG) and cover cache + k-means album colour extraction and theme generation + library scan and playlists (create / move / delete) + display-name stripping + library stats + playback-state save/restore + neighbour precache |
| `src/managers/obdmanager.cpp` | 3032 / 775 | `OBDManager` connection state machine + auto-reconnect + device scan + demand-driven poll list + signal dispatch table (`buildSignalDispatch`) + RFCOMM bind (`startRfcommBind` and friends) + vehicle scan + diagnostic mode + DTC / freeze frame / VIN + data watchdog, plus the whole `OBDConnectionWorker` (serial I/O, init sequence, polling) and the Android BLE path (`startAndroidConnection` through `processAndroidResponse`, ~460 lines) |

---

## Why this is parked

1. **It's a refactor, not a bug fix.** Users don't feel this directly. The app works. This is purely developer experience.
2. **It's high risk.** 2300–3300 lines of tightly-coupled code have load-bearing assumptions we don't fully understand. Breaking any of them is a regression users WILL feel, and Rob daily-drives this build.
3. **The safety net is only partial.** The C++ QtTest suite in `tests/cpp/` covers ELM327 parsing, OBD demand/poll logic, the simulated car end to end, settings backup/atomic write and a few media edge cases. It does not yet cover a settings value round-trip, the volume dispatch, metadata sanitization or recorded real-car traffic (see `future-tests.md`).
4. **You chose to skip it.** You explicitly said #4 wasn't a priority in the session where we planned Phases 1–3.

---

## Recommended order (safest first)

### 1. settingsmanager.cpp (do first)

Clearest seams, lowest risk of breaking runtime behavior. `SettingsManager` is the only class QML references, so the public API stays identical — behind the facade, code just moves to helper files.

**Proposed split:**
- `src/managers/settings/settingsdefaults.{h,cpp}` — the settings registry and `buildDefaultSettings()` (lines ~46–320). Just data.
- `src/managers/settings/settingspaths.{h,cpp}` — `getAppDataDir()` and the per-OS path logic (lines ~320–416). Free functions, no QObject.
- `src/managers/settings/settingsstore.{h,cpp}` — file locking, `validateSettings`, `readSettingsFromDisk`, `parseSettingsFile`, backup, `atomicWrite`, `writeSettingsToDisk` (lines ~623–860). Pure I/O on `QJsonObject`, no QML exposure.
- `src/managers/settings/customthemes.{h,cpp}` — `save_custom_theme`, `delete_custom_theme`, `get_custom_theme`, `downloadThemeArtAsync` (lines ~1534–1700).
- `src/managers/settingsmanager.{h,cpp}` (shrunk) — the `SettingsManager` QObject facade. Holds the members, keeps every `Q_PROPERTY` / `Q_INVOKABLE` / signal, delegates I/O to `settingsstore`.

**Target shrink:** 2315 → ~1300 lines in the facade (the getters and `save_*` setters stay; they are the API), ~1000 lines across the helpers.

### 2. mediamanager.cpp (do second)

Medium risk. More state than `SettingsManager`. The mute toggle guard, playback-state debounce, recovery/skip logic and playlist transitions have subtle interactions that need care.

**Proposed split:**
- `src/managers/media/albumcolors.{h,cpp}` — `_extract_album_colors`, `_kmeans_colors`, `_generate_theme_from_colors` (lines ~1014–1380). Pure functions over an image; the easiest and largest extraction.
- `src/managers/media/albumart.{h,cpp}` — the TagLib cover extractors for MP3 / MP4 / FLAC / OGG, `writeCoverFile`, cover-dir pruning (lines ~825–980).
- `src/managers/media/metadatastore.{h,cpp}` — `_read_tags`, `_sanitize_metadata`, the on-disk metadata store load/save and lookup (lines ~531–760).
- `src/managers/media/library.{h,cpp}` — `_scan_library_inner`, playlist create / move / delete, file-path resolution, display-name stripping, stats (lines ~1951–2960). Filesystem only.
- `src/managers/mediamanager.{h,cpp}` (shrunk) — facade that owns the `QMediaPlayer`, playback control, recovery, playback-state save/restore and precache, and composes the helpers into the `MediaManager` QObject exposed to QML.

**Prerequisite:** the `VolumeController` dispatch test and a metadata sanitization test (both in `future-tests.md`) should go in BEFORE this split. They validate the two most fragile parts of this subsystem. `tests/cpp/tst_mediamanager.cpp` already covers missing-track skipping, shuffle reselection and zero-byte covers.

### 3. obdmanager.cpp (do last)

Highest risk. Concurrent code with a worker thread, timers, queued invokes and the diagnostic-mode enter/exit handshake (`enter_diagnostic_mode`, `exit_diagnostic_mode`, `delayedReconnectAfterDiagnostic`). That is exactly the kind of subtle thing that breaks when code moves.

**Proposed split:**
- `src/managers/obd/obdconnectionworker.{h,cpp}` — `OBDConnectionWorker` (lines ~1936–2565 plus its class in `obdmanager.h`). Already a separate class; moving it is mostly mechanical. Keep `setCustomTransport()` intact, `src/dev/simelm327` and the tests depend on it.
- `src/managers/obd/obdandroidble.{h,cpp}` — the Android BLE path (lines ~2568–3032). Compiled only on Android, so it is the cleanest seam, but it can only be verified on the phone.
- `src/managers/obd/obdrfcomm.{h,cpp}` — the RFCOMM bind sequence (`startRfcommBind`, `waitForRfcommNode`, `finishRfcommBind`, `releaseRfcommProc`, lines ~1348–1500).
- `src/managers/obd/obdsignaldispatch.{h,cpp}` — `buildSignalDispatch` and `emitParameterSignal` (lines ~176–460). Just a table.
- `src/managers/obdmanager.{h,cpp}` (shrunk) — facade that owns the worker thread, routes QML slots to the right subsystem, keeps the demand/poll list, diagnostic mode and the data watchdog.

**Prerequisite:** DO NOT touch `obdmanager.cpp` until the OBD protocol replay suite from `future-tests.md` exists. `tests/cpp/tst_obdsim.cpp` (simulated car through the real `OBDManager`) and `tst_obdmanager.cpp` are a good start, but the simulator only speaks the dialect we wrote; recorded traffic from real adapters is what catches a regression in the worker.

---

## Approach: incremental, one commit per extraction

Not "one big refactor." Instead:

1. Pick one target (`settingsmanager.cpp` first).
2. Extract ONE helper at a time — start with the lowest-risk one (`settingspaths`, because it's free functions with no QObject).
3. Add the new files to `CMakeLists.txt` (the `qt_add_executable(octave ...)` source list) and to every `octave_add_test()` in `tests/cpp/CMakeLists.txt` that compiles the manager.
4. Build and run `ctest --test-dir build --output-on-failure` after each extraction.
5. Drive the app for 5 minutes with `./build/octave --dev --profile`, hitting the subsystem you just touched.
6. Commit. Move to the next helper.
7. Only after all helpers are extracted does the "shrunk facade" version get committed.

This keeps every commit small and revertable. If something breaks, `git bisect` finds the bad extraction in minutes, not hours.

---

## What would make this safer (dependencies on `future-tests.md`)

Before starting any split, these tests should exist:

| Target | Required tests before split |
|---|---|
| `settingsmanager.cpp` | Settings round-trip test (Tier 1); backup and atomic-write tests already exist in `tst_settingsmanager.cpp` |
| `mediamanager.cpp` | `VolumeController` dispatch test + metadata sanitization tests (Tier 1 + Tier 2) |
| `obdmanager.cpp` | OBD protocol replay suite (Tier 3); ELM327 parsing (`tst_elm327protocol`) and the simulated car (`tst_obdsim`) already exist |

**Rule of thumb:** if a subsystem has no test coverage, don't refactor it. Add tests first, then refactor. The tests tell you when the refactor is done.

---

## What NOT to do

- **Don't do all three at once.** That's a 3000+ line diff nobody can review and nobody can bisect.
- **Don't split just to hit a line count.** If a section of code genuinely belongs together, leave it. The goal is clearer responsibilities, not smaller files.
- **Don't change behavior during the split.** The refactor commit should be 100% code motion — no bug fixes, no feature changes, no "while I'm in here" cleanups. Those come before or after, never during.
- **Don't skip the manual test pass between extractions.** The test suite doesn't cover all of the signal/slot wiring. If you move code from `mediamanager.cpp` into a helper, the only way to know all the callers still work is to actually use the app.
- **Don't break the QML context property contract.** QML references these managers by name (`mediaManager`, `settingsManager`, `obdManager`) and expects specific `Q_PROPERTY`, `Q_INVOKABLE` and signal names on specific classes. The public API of the facade classes must stay identical during the refactor; helpers stay internal and are not registered with QML.

Delete this file when all three splits have landed.
