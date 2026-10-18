# OBD adapter discovery: scan, pair, trust, connect on every platform

**Goal:** anyone can plug in a Bluetooth OBD-II dongle, tap "Connect Bluetooth OBD2", pick it from a list and have OCTAVE pair and connect, on every platform, without touching OS Bluetooth settings or typing a MAC.
**Status:** active. Desktop Linux / Pi code is done but not yet proven in the car; Android, Windows/macOS and BLE-on-desktop are not started.
**Last updated:** 2026-10-18

Done (desktop Linux, `78558d9`, `da94533`): `OBDAdapterFinder` (`src/managers/obdadapterfinder.{h,cpp}`, QML `obdAdapterFinder`), `BluezPairer` / PIN agent (`src/platform/bluezpairer.{h,cpp}`, `OCTAVE_BLUEZ` only), `frontend/OBDAdapterSheet.qml`, `tests/cpp/tst_obdadapterfinder.cpp`. On success it saves an `rfcomm://MAC/channel` adapter (`RfcommSocket`). The sheet is platform-agnostic; on unsupported platforms it lists `obdManager.availableAdapters`.

## 1. Verify desktop Linux in the Jeep (do first)

The scan has found the NEXAS (Classic + LE) on the dev machine, but scan → pair → trust → connect through the sheet has never been run against a real adapter.

1. On the Orange Pi, remove the NEXAS from BlueZ (`bluetoothctl remove <MAC>`) so the test starts from scratch.
2. Settings → OBD → Adapters → "Find Bluetooth Adapters", pick the NEXAS, connect.
3. Watch `octave-cpp.log` for the `[OBD-BT]` lines: PIN answer (or the "pair failed, trusting anyway" path), `Trusted`, SDP channel, `connect_direct`. Confirm live PIDs and that the saved `rfcomm://` adapter reconnects after a restart.
4. Fix whatever breaks.

**Effort:** S (M if it breaks). **Needs:** Orange Pi 5 Plus in the Jeep + NEXAS adapter, engine on.

## 2. Android: Classic SPP dongles and in-app scan/pair

Today Android lists only already-bonded devices (`OctaveOBDBridge.bondedDevices`, `android/src/org/octave/app/OctaveOBDBridge.java:225`; `OBDManager::listAndroidPairedDevices()`, `src/managers/obdmanager.cpp:2572`) and connects only over BLE GATT. Classic SPP dongles (cheap "OBDII" v1.5/v2.1 clones, OBDLink LX/MX+, BAFX, Veepeak Mini) can't connect at all.

1. **Discovery in Java**, not `QBluetoothDeviceDiscoveryAgent` (it has crashed with NullPointerException on Android, see `obdmanager.cpp:528`): `BluetoothAdapter.startDiscovery()` with an `ACTION_FOUND` receiver for Classic, plus `BluetoothLeScanner.startScan()` for BLE. Queue results like `pollLog()` and drain them from C++ on a timer. Needs `BLUETOOTH_SCAN` (already in the manifest, `neverForLocation`) at runtime alongside `BLUETOOTH_CONNECT`.
2. **Pairing:** `device.createBond()`; register for `ACTION_PAIRING_REQUEST`, call `device.setPin("1234")` and `abortBroadcast()` (ordered broadcast); fall back to 0000. Some OEM skins still show the system dialog; accept that.
3. **Classic SPP transport** in the bridge: `createRfcommSocketToServiceRecord(00001101-0000-1000-8000-00805F9B34FB)`, with `createInsecureRfcommSocketToServiceRecord` and reflection `createRfcommSocket(1)` fallbacks. Feed bytes into the same `pendingResponses` queue as BLE so `processAndroidResponse()` is unchanged. `OBDManager::startAndroidConnection()` picks BLE vs Classic from the scan result.
4. Make `OBDAdapterFinder::supported()` (`obdadapterfinder.cpp:136`) true under `Q_OS_ANDROID` and route scan/pair through JNI. Leave the QML alone.

**Effort:** L. **Needs:** Galaxy S22 + a Classic SPP dongle (the NEXAS works as one) and a BLE dongle for regression.

## 3. Windows and macOS

1. **Transport:** a `QBluetoothSocket` (RFCOMM) path in `OBDConnectionWorker` for `rfcomm://` URLs on non-Linux (Linux keeps `RfcommSocket`). Removes the need for a COM port / `/dev/tty.*`.
2. **Pairing:**
   - macOS: `QBluetoothLocalDevice::requestPairing()`; legacy-PIN adapters may show the OS prompt. Add `NSBluetoothAlwaysUsageDescription` to the bundle Info.plist.
   - Windows: Qt can't pair. Use WinRT `DeviceInformation.Pairing.Custom.PairAsync` with a `PairingRequested` handler calling `args.Accept("1234")` (C++/WinRT).
3. Make `supported()` true on both.

**Effort:** L. **Needs:** a Windows PC and a Mac with Bluetooth, plus a Classic dongle (NEXAS) and ideally an ECU simulator or the Jeep.

## 4. BLE adapters on desktop, then iOS

1. A `QLowEnergyController` transport in `OBDConnectionWorker` speaking the same three GATT profiles as `OctaveOBDBridge.java` (ISSC, FFF0/FFF1/FFF2, FFE0/FFE1); mark `ble` rows `connectable` and route them to it.
2. iOS (once the iOS build exists): BLE only. Classic SPP needs Apple's MFi programme, so Classic rows say "not supported on iPhone".

**Effort:** L (desktop BLE), XL with iOS. **Needs:** a BLE OBD dongle on a desktop; an iPhone and a working iOS build for the second step.

## Order

1 → 2 (Android Classic is the biggest gap for real users) → 3 → 4. Update `wiki/obd-manager.html#adapter-finder` and `wiki/signals-slots-reference.html` with each phase.

Delete this file when all four sections are done.
