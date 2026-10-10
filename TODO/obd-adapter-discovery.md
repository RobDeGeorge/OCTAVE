# OBD adapter discovery: scan, pair, trust, connect on every platform

**Status:** active. Phase 1 (desktop Linux / Pi) is done; phases 2–4 are deferred.
**Last updated:** 2026-10-09

## Why

Connecting an OBD-II adapter used to mean knowing its MAC and pairing it in the OS first (`bluetoothctl`, Android
Settings, Windows "Add a device", then typing the MAC or COM port into Settings → OBD). The goal is for anyone to
plug in a dongle, tap "Connect Bluetooth OBD2", pick it from a list, and have OCTAVE do the rest.

## What phase 1 shipped (desktop Linux, incl. Raspberry Pi / Orange Pi)

- `OBDAdapterFinder` — `src/managers/obdadapterfinder.{h,cpp}` ↔ `backend/obd_adapter_finder.py`, QML
  `obdAdapterFinder`. API: `supported`, `bluetoothOn`, `scanning`, `devices`, `busyAddress`, `status`,
  `statusIsError`; `startScan()`, `stopScan()`, `connectAdapter(address)`, `cancel()`, `powerOn()`.
- `BluezPairer` + `BluezPinAgent` — `src/platform/bluezpairer.{h,cpp}` (compiled only when `OCTAVE_BLUEZ`, i.e.
  Linux and not Android; links Qt6::DBus) ↔ `_BluezPairer` / `_BluezPinAgent` in the Python module. Registers its
  own `org.bluez.Agent1`, answers PINs 1234 / 0000 / 6789 / 1111, sets `Trusted`. When pairing fails but the
  adapter answered, it still trusts it and tries to connect (the NEXAS accepts RFCOMM from a trusted host).
- SDP lookup for the SPP channel (fallback 1), then `OBDManager::connect_direct(mac, channel)`.
- `frontend/OBDAdapterSheet.qml` — opened from `obdConnectButton` on the empty OBD page and from Settings → OBD →
  Adapters ("Find Bluetooth Adapters"). When `supported` is false it lists `obdManager.availableAdapters`.
- Tests: `tests/test_obd_adapter_finder.py`. Wiki: `obd-manager.html#adapter-finder`,
  `signals-slots-reference.html#obdAdapterFinder`, `components.html#obd-adapter-sheet`.

**Still unverified in the car:** the scan was tested on the dev machine (it finds the Jeep's NEXAS23082749 at
88:1B:99:66:DD:5F as Classic + LE), but the pair → trust → connect path hasn't been run against a real adapter
yet. Do that on the Orange Pi before calling phase 1 done. Watch the `[OBD-BT]` lines in the log.

## Phase 2 — Android (C++ only; the Python backend never runs on Android)

Today Android lists only already-bonded devices (`OctaveOBDBridge.bondedDevices`,
`OBDManager::listAndroidPairedDevices()` at `src/managers/obdmanager.cpp:2509`) and connects **only over BLE GATT**
(`OctaveOBDBridge.connect`). Classic SPP dongles (the cheap blue "OBDII" v1.5/v2.1 clones, OBDLink LX/MX+, BAFX,
Veepeak Mini) cannot connect on Android at all.

1. **Discovery in Java**, not `QBluetoothDeviceDiscoveryAgent` (it has crashed with NullPointerException on
   Android, see the comment at `obdmanager.cpp:520`): `BluetoothAdapter.startDiscovery()` with an
   `ACTION_FOUND` receiver for Classic, plus `BluetoothLeScanner.startScan()` for BLE. Queue results like
   `pollLog()` and drain them from C++ on a timer. Needs `BLUETOOTH_SCAN` (already in the manifest,
   `neverForLocation`) requested at runtime alongside `BLUETOOTH_CONNECT`.
2. **Pairing**: `device.createBond()`; register for `ACTION_PAIRING_REQUEST`, call `device.setPin("1234")` and
   `abortBroadcast()` (it's an ordered broadcast); fall back to 0000. The system dialog may still show on some
   OEM skins — accept that.
3. **Classic SPP transport**: new path in the bridge,
   `device.createRfcommSocketToServiceRecord(SPP_UUID 00001101-0000-1000-8000-00805F9B34FB)` with the
   `createInsecureRfcommSocketToServiceRecord` / reflection `createRfcommSocket(1)` fallbacks the ELM327 apps
   use. Feed bytes into the same `pendingResponses` queue the BLE path uses so `processAndroidResponse()` is
   unchanged. `OBDManager::startAndroidConnection()` picks BLE vs Classic from what the scan saw.
4. Flip `OBDAdapterFinder::supported()` to true under `Q_OS_ANDROID` and implement the scan/pair calls through
   JNI. Keep the QML untouched — the sheet is already platform-agnostic.
5. Test on the Galaxy S22 (see the Android phone loop memory / `BUILD.md`).

## Phase 3 — Windows and macOS (C++ and Python)

Both can use Qt Bluetooth for discovery and `QBluetoothSocket` (RFCOMM) for Classic connections, which removes the
need for a COM port / `/dev/tty.*` entirely.

1. **Transport**: add a `QBluetoothSocket` path to `OBDConnectionWorker` for `rfcomm://` URLs on non-Linux
   (Linux keeps the raw-socket `RfcommSocket`). Python: python-obd needs a pyserial-compatible handler; either a
   `protocol_qtbt` URL handler bridging to `QBluetoothSocket`, or `socket.AF_BLUETOOTH` where the platform's
   Python has it (Windows CPython does, macOS does not).
2. **Pairing**:
   - macOS: `QBluetoothLocalDevice::requestPairing()` works (IOBluetooth); legacy-PIN adapters may show the OS
     PIN prompt. Needs `NSBluetoothAlwaysUsageDescription` in the bundle's Info.plist.
   - Windows: Qt can't pair. Use WinRT `DeviceInformation.Pairing.Custom.PairAsync` with a `PairingRequested`
     handler that calls `args.Accept("1234")` (C++/WinRT in C++; the `winsdk`/`winrt-Windows.Devices.Enumeration`
     package in Python, desktop-only).
3. Flip `supported` for those platforms.

## Phase 4 — BLE adapters on desktop, then iOS

- Desktop BLE: a `QLowEnergyController` transport in `OBDConnectionWorker` speaking the same three GATT profiles
  as `OctaveOBDBridge.java` (ISSC, FFF0/FFF1/FFF2, FFE0/FFE1). Python: `QtBluetooth` LE classes or `bleak`.
  Then set `connectable` true for `ble` rows and route them to the new transport.
- iOS (once the iOS build is enabled): BLE only. Classic SPP is impossible without Apple's MFi programme, so
  Classic rows must say "not supported on iPhone".

## Recommended order

Verify phase 1 in the Jeep → phase 2 (Android Classic SPP is the biggest gap for real users) → phase 3 → phase 4.
Update `obd-manager.html#adapter-finder` and `signals-slots-reference.html` with each phase.

Delete this file when all four phases are done.
