# Trip sync: the black box's drives from one OCTAVE to another

**Goal:** every drive the Pi records ends up on Rob's phone and on his server, in the Trip History of the OCTAVE running there, with no separate companion app.
**Status:** active: built end to end on the desktop (2026-11-27), tested against a real `octave --headless` and a fake adb. Nothing has run on the phone or the Pi yet.
**Last updated:** 2026-11-27

## What Rob asked for (2026-11-27)

- The logs must come off the Pi onto "my server or phone or both".
- No companion app: the OCTAVE app on the phone and the OCTAVE on the Pi talk to each other, "all my octaves working with one another".
- Build both ways so it is robust: pushed over the adb link the mirror already has, and OCTAVE to OCTAVE over the service.
- The simulated car is never saved, and never synced.

## Built

- **The unit that travels** is a drive's CSV + JSON pair under `history/trips/`. No new format.
- **Receiving:** `TripStore::importTrip` / `importInbox` (`src/managers/tripstore.cpp`), `history/inbox/`, `tripRecorder.importInbox()`. Same id = already here; `simulated` refused; unreadable = `*.rejected`.
- **`tripSync`** (`src/managers/tripsync.{h,cpp}`, experimental): peers, `addPeer(host, port, code)` (the `pair` op with scope `files`), `syncNow()`, a pull by itself (15 s after start, every 5 minutes, app to the front), `<config>/trip-sync.json`.
- **Serving:** `GET /trips`, `/trips?id=N&format=json|csv[&z=1]` (`TripSync::serve`, route in `src/main.cpp`, scope `files` in `servicebridge.cpp`).
- **adb push:** `TripSync::pushOverAdb`, fed by `PhoneMirrorManager::isRunningChanged` (`src/appcore.cpp`): 20 s after a phone connects and 5 s after a drive ends, 100 drives a run, into `/sdcard/Android/data/org.octave.app/files/history/inbox/`, each file as `*.part` then `mv`. Leaves `car.json` (this OCTAVE's network addresses) beside the inbox when the service listens beyond loopback.
- **The shutdown button** (Rob, 2026-11-27: he always shuts the Pi down with the button and wants the drive on his phone when he gets home): `NetworkManager::shutdown_device` first runs `TripSync::finishBeforePowerOff` (`setBeforePowerOff`, wired in `src/appcore.cpp`): the drive is ended (`TripRecorder::stopForPowerOff`, no new one starts), pushed over adb, and only then `systemctl poweroff` runs, after 30 s at the latest. `networkManager.poweringOff` is true meanwhile and the Power card says so.
- **The drive being recorded, in parts**, for a computer cut off with no shutdown: `TripSync::snapshotNow`, `TripStore::writeSnapshot` (JSON marked `partial`), run every `snapshotMinutes` of `trip-sync.json`. **Off by default** (0): Rob did not want a push every 2 minutes. The receiver's `importTrip` adds each part and replaces them with the whole drive. A partial drive is not handed on and not counted as "had".
- **On the phone:** `TripSync::start()` adds the app's external `history/inbox` as a second inbox (Android only) and reads `car.json` into `carHint` (the "Use" button).
- **UI:** Settings > OBD > Black Box > Other OCTAVEs (`frontend/settings/OBDSettingsPage.qml`).
- **Tests:** `tst_tripsync` (served files; pair + pull from a real headless OCTAVE; push through a shell-script adb), `tst_triprecorder::aDriveIsTakenInFromTheInbox`.

## Left: all of it needs the devices

1. **The Pi's service on the car's network.** Pull needs the Pi started with `--service --service-bind 0.0.0.0` (or the hotspot's address); there is no setting for the bind address, only the flag. Decide with Rob whether a setting should exist, then pair the phone once. **Effort:** S. **Device:** Pi, S22.
2. **adb may write where the app reads: unproven.** Check on the S22 and S26: (a) `adb shell mkdir -p` and `adb push` under `/sdcard/Android/data/org.octave.app/files/` succeed on their Android versions; (b) the app can then read, rename and delete those files (they are created by the shell user, and Android 11+ may not hand them to the app); (c) `QStandardPaths::standardLocations(AppDataLocation)` on Android has the `/Android/data/` entry `TripSync::start()` looks for (the log line "Drives pushed over adb arrive in ..." says which folder it chose). If (b) fails, the fallback is `/sdcard/Download/OCTAVE-trips/` plus a folder pick in the app, or `adb shell run-as` / `content write` into the app. **Effort:** S to check, M if the fallback is needed. **Device:** S22, S26, Pi.
3. **A drive on the phone, seen in the app.** Mirror on the Pi with a drive in its history, open OCTAVE on the phone, look at Trip History; then the same over Wi-Fi with adb push switched off. **Effort:** S. **Device:** S22, Pi.
4. **The server.** `octave --headless` at home, paired with the phone's OCTAVE (the phone's service must be on and bound to the network for that, which on Android nobody has tried) or with the Pi when it is on the home Wi-Fi. If the phone cannot serve, the server pulls from the Pi only. **Effort:** M. **Device:** S22, a server.
5. **Finding each other with no address at all** (mDNS) for the case with no mirroring. The `car.json` note covers a mirrored phone. **Effort:** M. **Device:** S22, Pi.
6. **Shut Down in the Jeep.** After a real drive with the phone mirrored wirelessly, press Shut Down: how long until the power goes, and is the drive on the phone whole. If any other way of shutting the Pi down is in use (a hardware button, a script calling `systemctl poweroff` itself), it does not pass through `shutdown_device` and sends nothing: route it through `networkManager.shutdownDevice()`. **Effort:** S. **Device:** S26, Pi, Jeep.
7. **Size.** A drive's CSV is held in memory once on each side (compressed on the wire). Fine for hours of driving on the Pi; look again if a day-long drive ever fails on the phone. **Effort:** S. **Device:** S22.

## Decisions to keep

- Deletes do not travel. The Pi prunes at its cap (`blackBoxMaxMB`); the phone and the server are the archive. A receiver never fetches an id twice (`had` in `trip-sync.json`, every id that was ever in its history), so a drive deleted or pruned there stays gone.
- Trips of several cars will share one history on the phone and the server. Every drive carries its VIN; a filter by car in `frontend/TripHistory.qml` comes when there is a second car.
- Values travel through the CSV at 7 significant digits, which is what a reading is worth.
- Units of custom PIDs are looked up in the receiver's catalog, so they are blank in the receiver's CSV unless it has the same `custom-pids.json`.

## Cross-references

- `octave-companion-app.md`: its item 10 (car health on the phone) is this plan for the trip part; its "separate app" decision is now in question for the rest.
- `android-cpp-port.md`: the phone side needs the APK to be good enough to keep installed.
- `open-backend.md` / `frontend-platform.md`: pairing and scopes the peer client must follow.
- `maps.md`: position in the black box travels with the drive like any other reading.

## Order

2 first (it decides whether the adb way works as built), then 3 and 6, then 1 and 4; 5 and 7 only if they turn out to matter.

**Delete this file when** drives recorded in the Jeep show up on the phone and the server without anyone copying a file.
