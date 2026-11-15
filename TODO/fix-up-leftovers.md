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
| Fold Android's own ELM327 path into the shared worker (it declares "Connected" 2 s after `0100` whatever answers; no DTC reads, batching or custom PIDs) | `src/managers/obdmanager.cpp` (the `Q_OS_ANDROID` block near the end) | L |
| Update the prebuilt Android OpenSSL off the end-of-life 3.1 line and record its hash | `android/openssl/` | S–M |
| The legacy DHU launch blocks the GUI thread for up to a minute. Native Android Auto ([android-auto-native-receiver.md](android-auto-native-receiver.md)) replaces the DHU, so fix this only if the DHU path stays | `src/managers/androidautomanager.cpp:463-520` | M |

## 2. Needs the car or the Pi

| Item | Where | Effort |
|---|---|---|
| Confirm the OBD fixes on a real adapter: a refused clear (engine running), key off / key on recovery on a USB adapter, the cancelled retry timer | `src/managers/obdmanager.cpp` | S |
| Measure gauge text re-fit (`fontSizeMode: Text.HorizontalFit` at up to 30 Hz) and arc re-tessellation before changing them | `frontend/gauges/` | M |
| Trip ids and prune order use the wall clock; a head unit with no RTC and a stale boot clock can prune the wrong trips | `src/managers/triprecorder.cpp:292`, `tripstore.cpp` (prune) | M |

## 3. Needs a browser build (Qt wasm kit)

| Item | Where | Effort |
|---|---|---|
| Every page connection is loopback and so `full`; a hosted web frontend's manifest scopes are written and never enforced; the playground loads `?frontend=<any URL>` without asking | `src/platform/wasm/webservice.cpp:109`, `packaging/wasm/frontend-host.js:103`, `playground.html` | M |
| Tag bytes stay in wasm memory for every picked song; the 512 MB cap is reached at about 4,000 MP3s and later songs silently lose their tags | `src/platform/wasm/weblibrary.cpp` | M |
| More than 400 queued events in one pump are dropped with no `dropped` notice | `webservice.cpp:88-95` | S |
| A frontend that reloads itself restarts socket numbering and is never answered | `frontend-host.js:102` | S |
| The media manager's new background tag pass was not compiled for wasm | `src/managers/mediamanager.cpp` | S |

## 4. Needs a CI release run

| Item | Where | Effort |
|---|---|---|
| Pin linuxdeploy and its Qt plugin (today: the moving `continuous` tag, no hash) and wasm-bindgen (piped from curl) by hash | `packaging/linux/build-appimage.sh:138-139`, `.github/workflows/wiki-pages.yml` | S |
| Make a jar that differs from its source an error once the toolchain difference the warning mentions is understood | `.github/workflows/build.yml` (phone-server job) | M |
| A sanitizer job and `-DOCTAVE_WERROR=ON` in `cpp-compile-check` (the options exist; CI's GCC is older than the one they were tried with) | `.github/workflows/build.yml`, `CMakeLists.txt` | S |
| Run the C++ tests on more than Linux x86_64 Debug | `.github/workflows/build.yml` | M |

## 5. Desktop work that was not reached

| Item | Where | Effort |
|---|---|---|
| Unpaired peers at eight addresses can hold all 64 connection slots: keep slots for loopback | `src/service/commandserver.cpp:111` | M |
| A test for the limited-browser trust tier at the bridge (needs a WebSocket client in `tst_servicebridge`) | `tests/cpp/tst_servicebridge.cpp` | M |
| Shutdown is serial on the GUI thread (30 s seen in the log); managers die while QML is alive (TypeErrors at exit) | `src/appcore.cpp:437-457`, `src/main.cpp:796, 836` | M |
| The watchdog's abort path logs through the mutex a stuck GUI thread may hold; crash handlers use `signal()` with no alternate stack | `src/util/uiwatchdog.cpp`, `src/util/logger.cpp` | S–M |
| Diagnostics export misses `hang-*.txt` and still picks up the Python-era `octave.log`; the copy runs on the GUI thread | `src/managers/diagnosticsmanager.cpp:54, 100-125` | S |
| The simulator: gear and throttle controls are overwritten by the model and the autopilot; multi-frame replies ignore `ATH1` | `src/sim/carsim.cpp:72-77`, `demodrive.cpp`, `simelm327.cpp` | S |
| Interleaved multi-ECU frames with headers off can be spliced into one message | `src/managers/elm327protocol.cpp:592-631` | M |
| `networkmanager.cpp` (1,254 lines) and `androidautomanager.cpp` have no test | `tests/cpp/` | M |

## 6. Refactors, one at a time, after the above

1. `SettingsManager` table-driven: one row per setting instead of up to nine places; ends the double storage (`save_setting("uiScale", …)` writes the file and not the live value). **Effort:** L.
2. `OBDManager` split (connection lifecycle, scheduling, diagnostics) and the removal of the dead state and the cargo-cult locks. **Effort:** L.
3. `MediaPlayer.qml` on a real C++ list model (today a JS array, five invokable calls per row, a 300 ms refresh timer). **Effort:** L.
4. `OBDMenu.qml`: the 1,870-line shift-light popup into its own file, on `SettingsSlider` and the gauge library. **Effort:** M.
5. The dashboard editor's document model and undo out of QML JavaScript into C++ where QtTest reaches it. **Effort:** L.
6. Mechanical QML sweep: the always-true `typeof` / `settingsManager ?` guards, `EnvironmentTheme`'s dead flags, the unused `Spacing` tokens. The pasted `dp()` helpers wait on [dp-helper-consolidation.md](dp-helper-consolidation.md). **Effort:** M.

## 7. Protocol 4 (one breaking release)

Decided 2026-11-15: add camelCase names beside the 80 snake_case invokables and mark the old ones `octave.deprecated`, deprecate the per-parameter OBD signals nothing listens to, then remove them all in a single protocol 4 together with `settingsLayoutStyle` (its layouts are gone) and the change that a `pair` request with no scope list gets `read`, not `full`. Before the removal: ask whether any frontend outside this repository depends on the old names. **Effort:** L, desktop.

## Owner decisions still open

- Pairing lockouts last until restart (an existing test asserts a new code does not lift them): keep, or expire after a time?
- A paired frontend can switch the car to the simulator (`connect_to_adapter("sim://elm327")`, `vehicle` scope) and the drive is recorded: mark such trips as simulated, or leave?
- A failed Spotify re-sign-in clears a working refresh token (`spotifymanager.cpp:459`): what should the state read afterwards?
- One-tap destructive actions (forget device, End trip, Revoke, chip ✕): add a confirm step?

## Order

Sections 1 and 2 as devices are at hand; section 4 with the next release; section 5 any desktop day; section 6 only after 5; section 7 last, since its rename sweep touches nearly every QML file.

Delete this file when the tables are empty.
