# Frontend parity

Rob's question: is every control OCTAVE has natively reachable from other frontends? This matrix has one row per user-facing feature of the bundled QML UI (`frontend/`). Each row gives the API members the feature uses (`object.member`, all in `docs/api/`) and whether each frontend in `frontends/` does it.

- **Yes**: the frontend has its own control or readout for it.
- **Partial**: some of it, or only through a generic path (the console's settings table or API pane, a readout without the control). The note says which.
- **No**: not in that frontend, although the API allows it.
- **API missing** in the notes: no frontend can do it until the backend adds what is named, with the lane A item that closes it (`TODO/open-backend.md` and the overnight run's list).

How it was made: every `object.member` that each QML page touches was extracted from the source (the same set `tst_conformance` checks) and grouped into features page by page. Each other frontend's source was then searched for the members it calls. Glass is lane B's and still growing: its column is read from lane B's branch at `bfbed55` (plus its work in progress) and will move. The console column reflects its control-parity panes (Library, Dashboards, Car, Phone, System).

Video (the phone mirror picture, Android Auto frames) is out of scope for every frontend but QML until media streams exist. Purely visual options (album-art transitions, 3D tilt, blur) count as settings: a frontend reaches them when it can change the setting, whether or not it draws the effect itself.

## Media

| Feature | API | QML | glass | console | retro-cluster | sunlight | Notes |
|---|---|---|---|---|---|---|---|
| Now playing: title, artist, album, source, position | `nowPlaying.*` | Yes | Yes | Yes | Yes | Yes | |
| Play / pause, next, previous | `nowPlaying.togglePlay` / `next` / `previous` | Yes | Yes | Yes | Yes | Yes | |
| Seek | `nowPlaying.seek`, `canSeek` | Yes | Yes | Yes | Yes | Yes | |
| Shuffle | `nowPlaying.toggleShuffle`, `shuffled` | Yes | Yes | Yes | Yes | Yes | |
| Volume and mute | `volumeController.applyVolume`, `percent`, `muted`, `effectivelyMuted` | Yes | Yes | Yes | Yes | Yes | |
| Current cover | `GET /art`, `nowPlaying.artRevision` | Yes | Yes | No | Yes | Yes | console: a terminal; retro-cluster draws it as a dot matrix |
| Library: songs with artist, album, length, sort | `mediaManager.get_media_files`, `get_band`, `get_album`, `get_formatted_duration`, `sort_media_files`, `get_total_duration`, `get_album_count`, `get_artist_count` | Yes | Yes | Yes | No | No | |
| Play a library song | `mediaManager.play_file` | Yes | Yes | Yes | No | No | |
| Playlists: select, create, delete | `mediaManager.get_playlist_names`, `select_playlist`, `create_playlist`, `delete_playlist` | Yes | Yes | Yes | No | No | |
| Move a song to a playlist, delete a song | `mediaManager.move_song_to_playlist`, `delete_song`, `get_movable_playlist_names` | Yes | Yes | Yes | No | No | |
| Library song covers | `mediaManager.get_album_art`, `get_neighbor_album_arts`, `GET /art?song=`, `?queue=` | Yes | No | Yes | No | No | GET /art?song= and ?queue= landed (lane A 9b); console: 32 x 16 half-block covers |
| Rescan library, strip filenames | `mediaManager.scan_library`, `strip_filenames` | Yes | No | Yes | No | No | console: API pane only |
| Media folder | `settingsManager.save_media_folder`, `mediaFolder`, `mediaManager.browse_music_folders` | Yes | Partial | Yes | No | No | browsing: `mediaManager.browse_music_folders` (lane A 9, landed); glass: a typed path so far |
| Waveform / spectrum | `audioAnalyzer.levels`, `setDemand` | Yes | No | Yes | Yes | No | retro-cluster: AUDIO page; console: a one-line spectrum on the Library pane |
| Phone media while mirroring | `nowPlaying.*` (follows the phone), `phoneMirrorManager.mediaTitle` / `mediaPlayPause` / ... | Yes | Yes | Partial | Partial | Partial | console, retro-cluster, sunlight: through `nowPlaying` only |
| Phone queue | `phoneMirrorManager.mediaQueue`, `mediaQueueTitle`, `mediaQueueActiveId`, `mediaSkipToQueueItem` | Yes | No | Yes | No | No | |

## Downloads (not in app-store builds)

| Feature | API | QML | glass | console | retro-cluster | sunlight | Notes |
|---|---|---|---|---|---|---|---|
| Search, results, more results | `downloadManager.search`, `search_more`, `searchResults`, `isSearching` | Yes | Yes | Yes | No | No | |
| Download a result | `downloadManager.download` | Yes | Yes | Yes | No | No | |
| Queue: progress, pause, resume, cancel, clear | `downloadManager.downloadQueue`, `pause_download`, `resume_download`, `cancel_download`, `clear_finished_downloads`, `clear_download_queue` | Partial | Yes | Yes | No | No | QML shows the queue and clears finished ones only |
| Target playlist | `downloadManager.set_download_playlist`, `get_download_playlist` | Yes | Yes | Yes | No | No | |
| Play a finished download | `mediaManager.play_downloaded_song`, `play_file_at_path` | Yes | No | Yes | No | No | |

## OBD

| Feature | API | QML | glass | console | retro-cluster | sunlight | Notes |
|---|---|---|---|---|---|---|---|
| Connection state, detail, progress, log | `obdManager.connectionState`, `connectionStatus`, `connectionDetail`, `connectionProgress`, `connectionLog` | Yes | Yes | Yes | Partial | Partial | retro-cluster, sunlight: state only |
| Connect, reconnect, disconnect | `obdManager.connect_obd`, `reconnect`, `close` | Yes | Yes | Yes | No | No | |
| Force connect, reset connection | `obdManager.force_connect`, `reset_connection` | Yes | Yes | Yes | No | No | |
| Ports and adapters, connect to one | `obdManager.availableAdapters`, `refresh_ports`, `scan`, `connect_to_adapter` | Yes | Yes | Yes | No | No | |
| Bluetooth adapter scan, power on, pair | `obdAdapterFinder.*` | Yes | Yes | Yes | No | No | console: no power-on or cancel |
| Saved adapters | `settingsManager.obdSavedAdapters`, `remove_obd_saved_adapter` | Yes | Yes | Yes | No | No | console: settings table shows the list |
| Direct MAC connect, target address | `obdManager.connect_direct`, `set_target_address` | Yes | No | Yes | No | No | |
| Vehicle: VIN, make, year, supported PIDs, scan | `obdManager.vin`, `vehicleMake`, `vehicleModelYear`, `vehicleSupportedParameters`, `scan_vehicle` | Yes | Yes | Yes | Partial | Partial | retro-cluster, sunlight: readouts only |
| Live values of every PID, demand-driven | `paramStore`, `parameterCatalog.displayParameters`, `obdManager.setParameterDemand` | Yes | Yes | Yes | Yes | Partial | sunlight: DRIVE's five and the open dashboard's PIDs |
| Home OBD cards: choose, order | `settingsManager.homeOBDParameters`, `add_home_obd_parameter`, `remove_home_obd_parameter`, `move_home_obd_parameter` | Yes | Yes | Yes | No | No | console: settings table (list as JSON) |
| Trouble codes: read, clear, MIL | `obdManager.read_dtc`, `clear_dtc`, `get_dtc_codes`, `dtcCodesChanged`, `milStatusChanged` | Yes | Yes | Yes | Yes | Yes | |
| Freeze frame | `obdManager.read_freeze_frame`, `freezeFrameChanged` | Yes | Yes | Yes | Yes | No | the frame's PID values since svc/9 |
| Diagnostic mode, status, current codes | `obdManager.enter_diagnostic_mode`, `exit_diagnostic_mode`, `read_status`, `read_current_dtc` | Yes | Yes | Yes | No | No | |
| Units (°F / °C, mph / km/h) | `settingsManager.gaugeTempUnit`, `gaugeSpeedUnit`, `displayParameters` | Yes | Yes | Yes | Yes | Yes | all follow the setting; QML, glass and the console can change it |
| OBD settings (fast mode, reconnect attempts, card style, fuel tank, timeout) | `settingsManager.save_obd_*`, `save_fuel_tank_capacity` | Yes | Yes | Yes | No | No | console: settings table |

## Shift light

| Feature | API | QML | glass | console | retro-cluster | sunlight | Notes |
|---|---|---|---|---|---|---|---|
| Live shift light | `shiftLightManager.active`, `activeFlag`, `lit` | Yes | Yes | Yes | Yes | Yes | |
| Flags editor (RPM bands, colours) | `shiftLightManager.flags`, `setFlags` | Yes | Yes | Yes | No | No | console: a table, one flag or all as JSON, checked before `setFlags` |
| Look and behaviour (lamp size, glow, pulse, flash opacity, home card, max RPM) | `shiftLightManager.*` properties, `settingsManager.save_setting` | Yes | Yes | Partial | No | No | console: settings table |

## Dashboards

| Feature | API | QML | glass | console | retro-cluster | sunlight | Notes |
|---|---|---|---|---|---|---|---|
| Show a dashboard from its spec | `dashboardManager.dashboards`, `loadDashboard`, `supportedSchema` | Yes | Yes | Partial | No | Yes | sunlight draws every widget type in Go; console: the grid and cells as text |
| Duplicate, delete, restore built-ins | `dashboardManager.duplicateDashboard`, `deleteDashboard`, `restoreBuiltInDashboards`, `hiddenBuiltInCount` | Yes | Yes | Yes | No | No | |
| Share and import | `dashboardManager.exportDashboard`, `importDashboardFromText`, `lastShareError` | Yes | Yes | Yes | No | No | QML also uses the head unit's clipboard and file picker; exported files are not downloadable (GET /export/..., lane A 9c, landed) |
| Edit: place, move, resize, PID, props, save | `dashboardManager.saveDashboard`, `normalizedSpec`, `canPlace`, `fitAt`, `uniqueIdForLabel` | Yes | Yes | Partial | No | No | console: the spec as JSON, through `normalizedSpec`, no visual grid editor |

## Black box (trips)

| Feature | API | QML | glass | console | retro-cluster | sunlight | Notes |
|---|---|---|---|---|---|---|---|
| Recording state, current trip | `tripRecorder.recording`, `currentTripId`, `currentTripElapsedS`, `currentSampleCount` | Yes | Yes | Yes | Yes | Yes | sunlight: the REC lamp and timer, and the open trip on TRIPS |
| Trip list and details | `tripRecorder.trips`, `requestDetail`, `detailReady` | Yes | Yes | Yes | Yes | Yes | sunlight: the list and a summary in the user's units |
| Charts of a trip's values | `tripRecorder.requestSeries`, `seriesReady` | Yes | Yes | Yes | No | No | console: one value at a time as a sparkline |
| End trip now | `tripRecorder.endTripNow` | Yes | Yes | Yes | Yes | No | |
| Export, delete, clear history | `tripRecorder.exportTrip`, `deleteTrip`, `clearHistory`, `lastExportPath`, `GET /export/trip` | Partial | Yes | Yes | Partial | No | QML: clear history only; console: downloads the export over GET /export/trip |
| Black box on / off, size cap, sweep all PIDs, storage | `tripRecorder.setEnabled`, `storageBytes`, `settingsManager.save_black_box_*` | Yes | Yes | Partial | No | No | console: settings table |

## Sensors and accessories

| Feature | API | QML | glass | console | retro-cluster | sunlight | Notes |
|---|---|---|---|---|---|---|---|
| IMU readings (G-force, heading, pitch / roll, altitude, temperature) | `paramStore` IMU ids, `berryIMU.setDemand`, `berryIMU.connected` | Yes | Yes | Yes | No | Partial | sunlight: its G-force and compass widgets, no demand |
| Tare, reset tare | `berryIMU.calibrateTare`, `resetTare` | Yes | Yes | Yes | No | No | |
| IMU settings (enable, rate, decimals, units, cards) | `settingsManager.save_imu_enabled`, `save_sensor_*` | Yes | Yes | Partial | No | No | console: settings table |
| Gesture sensor: enable, status, mapping, reset | `gestureSensor.gestures`, `actions`, `setGestureAction`, `resetMappingToDefaults`, `connectionStatus`, `settingsManager.save_gesture_*` | Yes | Yes | Yes | No | No | console: enable in the settings table; the mapping takes two arguments (API pane) |
| ESP32 knob: ports, connect, status, LED and step settings | `esp32VolumeManager.availablePorts`, `refresh_ports`, `connect_device`, `connected`, `get_connection_detail`, `settingsManager.save_esp32_*` | Yes | Yes | Yes | No | No | console: settings only |

## Phone mirroring and Android Auto

| Feature | API | QML | glass | console | retro-cluster | sunlight | Notes |
|---|---|---|---|---|---|---|---|
| Session: start, stop, state, notices, devices, wake | `phoneMirrorManager.startScrcpy`, `stopScrcpy`, `sessionState`, `isRunning`, `devices`, `selectDevice`, `mirrorNotice`, `dismissNotice`, `wakePhone`, `phoneAsleep` | Yes | Yes | Yes | No | No | |
| Phone keys: home, back, app switch | `phoneMirrorManager.pressHome`, `pressBack`, `pressAppSwitch` | Yes | Yes | Yes | No | No | |
| Wireless and QR pairing | `phoneMirrorManager.wirelessAddress`, `setWirelessAddress`, `forgetWireless`, `startQrPairing`, `qrPairingRows`, `cancelQrPairing` | Yes | Yes | Yes | No | No | |
| Mirror settings (size, DPI, bit rates, audio gain and ducking, screen off) | `settingsManager.save_scrcpy_*`, `phoneMirrorManager.setDisplaySize` | Yes | Yes | Partial | No | No | console: settings table |
| The mirrored picture and touch | `stream` phoneMirrorManager video (H.264 records), `phoneMirrorManager.injectTouch` | Yes | Yes | Partial | No | No | streams landed (lane A 12); glass: WebCodecs player with touch; console: a stats line, no decoder |
| Android Auto: launch, close, install state | `androidAutoManager.launchDhuSeamless`, `closeDhu`, `isDhuEmbedded`, `isDhuInstalled`, `getDhuInstallInstructions` | Yes | Yes | Yes | No | No | |
| Android Auto picture | `stream` androidAutoManager frames (JPEG records) | Yes | Yes | Partial | No | No | frames landed (lane A 12); glass: JPEG view; console: a 48 x 28 half-block preview |

## Settings, device, network, diagnostics

| Feature | API | QML | glass | console | retro-cluster | sunlight | Notes |
|---|---|---|---|---|---|---|---|
| Every setting by its save slot | `settingsManager.*` properties, `save_*`, `get_settings_registry` | Yes | Yes | Yes | No | No | console: the generic table covers all of them from `describe` |
| Display: theme, font, UI scale, clock, bottom bar, settings layout | `settingsManager.save_theme_setting`, `save_font_setting`, `save_ui_scale`, `save_clock_*`, `fontCatalog.fonts` | Yes | Yes | Yes | No | Partial | console: theme picker from `themeCatalog` with swatches; sunlight: its own day / night only |
| Custom themes, Now Playing Studio | `themeCatalog.themes`, `settingsManager.customThemes`, `customThemeData`, `save_custom_theme`, `delete_custom_theme`, the art / background settings | Yes | Partial | Partial | No | No | console: Now Playing presets (`nowPlayingPresets`, `applyNowPlayingPreset`) and the theme picker; custom themes only through the settings table |
| Export and import all settings | `settingsManager.apply_settings` and every property | No | No | Yes | No | No | console: one JSON object out, `apply_settings` back in, refusals listed |
| Settings sections shown, pinned settings | `settingsManager.save_settings_section_visibility`, `is_settings_section_visible`, `toggle_pin_setting` | Yes | No | Partial | No | No | console: API pane |
| Network status | `networkManager.isConnected`, `networkName`, `refreshNetwork` | Yes | Yes | Yes | No | No | |
| Updates: check, apply, restart | `networkManager.checkForUpdates`, `applySelfUpdate`, `restartApp`, `updateStatus`, `selfUpdateStatus` | Yes | Yes | Yes | No | No | |
| Shut down the device | `networkManager.shutdown_device` | Yes | Yes | Yes | No | No | |
| Diagnostics: version, device info, recent log, export logs | `diagnosticsManager.appVersion`, `deviceInfo`, `recentLogLines`, `exportLogs`, `logDir` | Yes | Yes | Yes | No | No | the exported bundle stays on the head unit (GET /export/..., lane A 9c, landed); clipboard copy is head-unit-local by design |
| About: build, commit, licences | `settingsManager.get_git_commit_*`, `diagnosticsManager.licenseText` | Yes | Partial | Yes | No | No | |
| Device name | `settingsManager.save_device_name` | Yes | Yes | Yes | No | No | |

## What no frontend but QML can do

Nothing any more: every feature above has an API, and at least one frontend besides QML uses it. The last gaps closed tonight: library and queue covers (`GET /art?song=` / `?queue=`), folder browsing, theme palettes, Now Playing presets, export downloads (`GET /export/...`, which the console offers), and video (the `stream` op, which glass plays). What remains are No cells: work for a frontend, not for the backend.

Head-unit-local by design (no gap): the clipboard actions, opening a folder in the head unit's file manager, window size and position.
