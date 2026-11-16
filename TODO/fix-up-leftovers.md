# Fix-up leftovers: what the 2026-11-15 review found and the fixing session did not close

**Goal:** finish the review's fix list: the items that need a phone, the car, a browser build or a release run to verify, and the refactors that were too large for one day.

**Status:** deferred. The session fixed what could be verified on a desktop (branch `fixing-party`); these are the rest.

**Last updated:** 2026-11-15

**Why parked:** each item below either cannot be verified without a device or a CI release run, or is a refactor that should follow the bug fixes rather than be mixed with them. None was skipped for being unimportant. The licensing points are in [licensing-review.md](licensing-review.md).

## 1. Needs a phone (S22)

| Item | Where | Effort |
|---|---|---|
| Guard the media-session callbacks and the delayed lambda in the phone server (an exception there ends the mirror), then rebuild and commit the jar | `phone_server/.../control/PhoneMedia.java:118-153, 574` | S |
| Only open a queue item's `content://` icon when its authority belongs to the session's package; bound the decode size | `PhoneMedia.java:521`, `FakeContext.java:51` | S–M |
| Reconnect watchdog: check `timedOut` after every accept, make it volatile | `device/DesktopConnection.java:75-125` | S |
| udev: match the adb interface (`ENV{ID_USB_INTERFACES}=="*:ff4201:*"`) instead of whole vendors, then confirm a fresh install still mirrors | `packaging/linux/51-android.rules` | S |
| Stopping the mirror joins the worker on the GUI thread (up to seconds with a hung adb): make the stop asynchronous | `src/managers/phonemirrormanager.cpp:1257`, `src/phone_mirror/scrcpyclient.cpp:319-336, 772-777` | M |
| Tell the manager when the mirror / AirPlay view is hidden so frames stop (Android Auto has `setViewVisible`) | `frontend/PhoneMirrorView.qml`, `AirPlayView.qml`, managers | M |
| Fold Android's own ELM327 path into the shared worker (it declares "Connected" 2 s after `0100` whatever answers; no DTC reads, batching or custom PIDs) | `src/managers/obdmanager_android.cpp` | L |
| Update the prebuilt Android OpenSSL off the end-of-life 3.1 line and record its hash | `android/openssl/` | S–M |
| The legacy DHU launch blocks the GUI thread for up to a minute. Native Android Auto ([android-auto-native-receiver.md](android-auto-native-receiver.md)) replaces the DHU, so fix this only if the DHU path stays | `src/managers/androidautomanager.cpp:463-520` | M |

## 2. Needs the car or the Pi

| Item | Where | Effort |
|---|---|---|
| Confirm the OBD fixes on a real adapter: a refused clear (engine running), key off / key on recovery on a USB adapter, the cancelled retry timer | `src/managers/obdmanager.cpp` | S |
| Measure gauge text re-fit (`fontSizeMode: Text.HorizontalFit` at up to 30 Hz) and arc re-tessellation before changing them | `frontend/gauges/` | M |
| Trip ids and prune order use the wall clock; a head unit with no RTC and a stale boot clock can prune the wrong trips | `src/managers/triprecorder.cpp:292`, `tripstore.cpp` (prune) | M |

## 3. Needs a browser build run in a browser

The branch compiles for the browser (2026-11-15, `packaging/wasm/build.sh` with `QT_WASM=~/Qt/6.12.0/wasm_multithread QT_HOST=/usr` on zion). These need the page opened and used, not only built.

| Item | Where | Effort |
|---|---|---|
| Every page connection is loopback and so `full`; a hosted web frontend's manifest scopes are written and never enforced; the playground loads `?frontend=<any URL>` without asking | `src/platform/wasm/webservice.cpp:109`, `packaging/wasm/frontend-host.js:103`, `playground.html` | M |
| Tag bytes stay in wasm memory for every picked song; the 512 MB cap is reached at about 4,000 MP3s and later songs silently lose their tags | `src/platform/wasm/weblibrary.cpp` | M |
| More than 400 queued events in one pump are dropped with no `dropped` notice | `webservice.cpp:88-95` | S |
| A frontend that reloads itself restarts socket numbering and is never answered | `frontend-host.js:102` | S |

## 4. Needs a CI release run

| Item | Where | Effort |
|---|---|---|
| Pin linuxdeploy and its Qt plugin (today: the moving `continuous` tag, no hash) and wasm-bindgen (piped from curl) by hash | `packaging/linux/build-appimage.sh:138-139`, `.github/workflows/wiki-pages.yml` | S |
| Make a jar that differs from its source an error once the toolchain difference the warning mentions is understood | `.github/workflows/build.yml` (phone-server job) | M |
| A sanitizer job and `-DOCTAVE_WERROR=ON` in `cpp-compile-check` (the options exist; CI's GCC is older than the one they were tried with) | `.github/workflows/build.yml`, `CMakeLists.txt` | S |
| Run the C++ tests on Windows first, then macOS. Do this before the other rows: on 2026-11-15 the VM pass found that no library file had played on Windows since April (a path check against the native separator, `mediamanager.cpp` `_is_safe_path`), which a Windows test run would have caught. Start as a step that may fail without failing the job, and tighten once it is green | `.github/workflows/build.yml` | M |

## 5. Desktop work that was not reached

| Item | Where | Effort |
|---|---|---|
| Shutdown is serial on the GUI thread (30 s seen in the log) | `src/appcore.cpp:437-457`, `src/main.cpp:796, 836` | M |
| The diagnostics export copies its files on the GUI thread | `src/managers/diagnosticsmanager.cpp:54, 100-125` | S |
| The simulator: the throttle control is overwritten by the model and the autopilot (a gear chosen by hand stays since 2026-11-15) | `src/sim/carsim.cpp:72-77`, `demodrive.cpp` | S |
| With headers off, two ECUs waiting for the same sequence digit cannot be told apart (the earlier-opened message takes the first frame). Reading trouble codes with headers on (`ATH1`, reassembly per ECU id) would end the guess; the simulator prints headed multi-frame replies to test it against | `src/managers/elm327protocol.cpp` (`splitMessages`), `obdconnectionworker.cpp` | M |
| Crash handlers have their own stack on the GUI thread only: a stack overflow on a worker thread still leaves nothing in the log | `src/util/logger.cpp` (`installCrashHandlers`) | S |
| `networkmanager.cpp` (1,254 lines) and `androidautomanager.cpp` have no test | `tests/cpp/` | M |
| A loopback HTTP request counts as a browser only when it carries `Origin` or `Sec-Fetch-Site`. A scope-limited web frontend in an engine that sends neither (older WebKit) gets full loopback trust for a same-origin `fetch('/export/...')` and needs no ticket. Found by the 2026-11-15 review; older than the branch | `src/service/commandserver.cpp:419`, `src/service/servicebridge.cpp:279` | M |
| The UI watchdog can block before it aborts: its earlier warnings go through the message handler, which writes to stderr (stdio lock, blocking `write`) before the timed log lock; a GUI thread stuck in a stderr write to a stalled launcher pipe takes the watchdog with it. Check how the car's launcher wires stderr first | `src/util/uiwatchdog.cpp:127-154`, `src/util/logger.cpp:130` | S–M |
| The trip database is called damaged only when the schema statements fail: a corrupt page inside `samples` or `trips` opens as healthy and every later insert fails. Run `PRAGMA quick_check` at open (measure it on a large database on the Pi first), and prune old `.damaged-*` copies | `src/managers/tripstore.cpp:283-320` | M |
| The first scan of a library reads each new file's tags and cover on the GUI thread (`_auto_strip_display_names` → `_strip_single_filename` → `_cache_metadata`; 300 files, 300 reads) | `src/managers/mediamanager.cpp` | M |

## 6. Refactors, one at a time, after the above

1. Done 2026-11-15: `SettingsManager` is table-driven (one row per setting, one store of truth, writable properties). Left from it: derive the pinned-settings registry's slot names from the table; compile it on Windows (the dead file locking and its includes were removed; the browser build compiles).
2. Done 2026-11-15: `OBDManager` is split by responsibility (`obdmanager.cpp`, `obdmanager_connection.cpp`, `_polling.cpp`, `_diagnostics.cpp`, `_android.cpp`, and `obdconnectionworker.{h,cpp}`); the write-only members and the locks on GUI-thread-only state are gone. Left from it: `m_lock` on the Android path (goes when that path folds into the worker, section 1), `m_hasActiveWatchers` (always true), the published no-op `set_auto_reconnect()`; Windows, macOS and Android were not compiled.
3. `MediaPlayer.qml` on a real C++ list model (today a JS array, five invokable calls per row, a 300 ms refresh timer). **Effort:** L.
4. Done 2026-11-15: the shift-light popup is `frontend/obd/ShiftLightEditor.qml` and four small components (1,006 lines, was 1,870 inside `OBDMenu.qml`), with a fixture test. Its toggles and sliders stayed its own: `SettingsToggle` / `SettingsSlider` differ in size and handle shape. Left: the owner's look at it on screen.
5. The dashboard editor's document model and undo out of QML JavaScript into C++ where QtTest reaches it. **Effort:** L.
6. Mechanical QML sweep: the always-true `typeof` / `settingsManager ?` guards, `EnvironmentTheme`'s dead flags, the unused `Spacing` tokens. The pasted `dp()` helpers wait on [dp-helper-consolidation.md](dp-helper-consolidation.md). **Effort:** M.

## 7. Finishing the API rename (protocols 4 and 5)

Done 2026-11-15: mediaManager, obdManager, audioAnalyzer, esp32VolumeManager, clock and networkManager have a camelCase method beside each snake_case one, and the 113 old ones are marked deprecated ("goes in protocol 5"). `docs/api/README.md` promises a deprecated member one full protocol version, so they stay through protocol 4.

Left:
- Done 2026-11-15: the QML, the SDK examples, the templates and the wiki pages use the new names (the wiki keeps the old ones only in the "names" notes, in C++ snippets, and where the same word is a gesture action or a dev-tool action).
- `settingsManager`: done with the table-driven refactor. 85 properties are writable and 121 `save_*` / `get_*` methods are deprecated in their favour; the QML still calls the methods and moves with the sweep above.
- Deprecate, with the same notice: the per-parameter OBD signals nothing listens to, `settingsLayoutStyle` (its layouts are gone), and announce that a `pair` request with no scope list will get `read`, not `full`.
- Protocol 5: remove everything deprecated above. **Effort:** M, desktop.

## Decided 2026-11-15

- Pairing lockouts last until restart: kept as they are.
- Simulated drives are marked (`trips.simulated`, schema 2) and shown as "simulated car" in Trip History. A paired frontend can still switch the car to the simulator.
- Destructive one-tap actions ask first (`settings/ConfirmButton.qml`).
- Frontends outside this repository must stay possible: protocol 4 removes nothing without a deprecation period and a release of notice.

## Owner decision still open

- A failed Spotify re-sign-in clears a working refresh token (`spotifymanager.cpp:459`): what should the state read afterwards?

## Order

Sections 1 and 2 as devices are at hand; section 4 with the next release; section 5 any desktop day; section 6 only after 5; section 7's QML sweep last, since it touches nearly every QML file.

Delete this file when the tables are empty.
