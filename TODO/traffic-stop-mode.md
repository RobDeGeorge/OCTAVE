# Traffic stop mode (one trigger: the phone records and alerts, the car keeps copies)

**Goal:** one tap on OCTAVE, or a hidden wired button, makes the phone record audio + video from its own cameras, text a contact list with the location and upload encrypted, hash-chained pieces to the user's cloud within seconds, while the Pi and the custom OBD adapter keep backup copies in the car. No extra cameras needed. Live streaming to contacts is the stretch goal.
**Status:** deferred: scoped and researched 2026-11-07 (three research passes: existing products, PCB hardware, surviving phone seizure). Nothing built. Spikes on the S22 come first (step 1).
**Last updated:** 2026-11-09

## Why it's parked

It's a safety feature: failing silently at the one moment it's needed is worse than not having it. It needs the companion app (`octave-companion-app.md`), which isn't built, and the adapter's backup role needs its ESP32 bridge (`diy-obd-adapter.md` step 3). The phone-side unknowns need spikes before any design is locked.

## What the research found

### Existing products and why they fall short

| Product | What it did | Why it isn't enough |
|---|---|---|
| ACLU Mobile Justice | Recorded, auto-uploaded to the ACLU | **Shut down 2025-02-28** when the vendor contract ended |
| NYCLU Stop and Frisk Watch | Hardware-button start, locked recording, shake to send | Bystander tool; NYC only |
| "I'm getting pulled over" Siri Shortcut | Dims screen, DND, texts one contact, front camera | Upload is manual after stopping; iOS only; no lock-screen run |
| Google Personal Safety (Pixel) | Emergency video, backs up while recording, shares link | Pixel only, needs signal, one camera |
| Legal Equalizer | Record, SOS text to 3–5 contacts, state rights | Maintenance unclear; you must handle the phone |
| Noonlight / Citizen Protect / bSafe | SOS to dispatch or agents, live to guardians | Dispatches police (wrong direction); subscriptions; Citizen's history |
| Life360 | Crash/SOS to family | **Sold precise location to data brokers** (~20 % of 2020 revenue) |
| Owlcam (LTE dashcam) | "OK Owl, I'm getting pulled over" → cloud | **Bankrupt Jan 2020**; new owner doubled the fee |
| Nexar | Cheap cloud dashcam | Paid for by selling data to insurers and cities; **~130 TB exposed in a 2025 hack** |
| BlackVue Cloud | LTE upload, pre-event buffer | No stop mode; uploads only if signal right then; 2–30 day retention |
| Tesla Sentry / dashcam | Local USB recording | Police **tow Teslas and get warrants** for the footage |
| OpenWatch | Live low-res stream + later high-res sync | Right pattern; project dead |

Deleted-footage cases (Philadelphia 2021, Baltimore / Christopher Sharp): **the evidence survived only where it was already in the cloud.** Apple removed ICEBlock-style apps in Oct 2025 under DOJ pressure, so app-store distribution is a risk.

### Gaps nobody fills (our targets)

1. Upload **while** recording to storage the user owns, with no vendor backend that can die or sell the data.
2. More than one camera, plus vehicle data (speed before the stop).
3. A trigger that needs no reach for the phone. Attorneys warn that reaching for a phone can escalate a stop.
4. Proof the footage wasn't altered: hash chain, signatures, third-party timestamps.
5. Survives a seized, switched-off or no-signal phone, and works for iPhone owners.
6. Open source and self-hostable.

### Phone facts that shape the design

- **The shell server's recording dies with adb today.** The process survives the drop (`nohup ... & wait`, `src/phone_mirror/scrcpyclient.cpp:606-611`), but it only writes to the socket. A camera session ends as soon as the client is gone (`phone_server/.../Server.java:136-145`, persistence is display-only), and `CleanUp.java:195,214` deletes the jar when stdin closes. It can't restart after a reboot.
- **Shell camera with the screen locked is unproven.** scrcpy issue #5977: "Camera disconnected" seconds after start on a Galaxy S22+ (Android 14).
- **Companion app rules (Android 14+):** a background start may not open the camera or mic, even with a Companion Device Manager association. Routes that work:
  - (a) pre-arm a foreground service of type `connectedDevice|camera|microphone` while the app is visible (OCTAVE's shell can `am start` it at drive start) and open the camera on trigger;
  - (b) on the adapter's BLE event (`CompanionDeviceService.onDevicePresenceEvent`), launch a lock-screen activity (`setShowWhenLocked` / `setTurnScreenOn`) under a background-start exemption, then start the service;
  - (c) a "Record" action on the persistent notification.
- **Power-off:** Samsung "Lock network and security" asks for an unlock before power-off, but a held volume-down + side forces a restart anyway. iPhone powers off from the lock screen. `ACTION_SHUTDOWN` gives about 10 s, and a forced restart gives nothing. **Continuous upload is the only real protection.**
- **Segments, not one file:** a single MP4 is unplayable if killed (its index, `moov`, is written only at stop). Use `MediaRecorder.setNextOutputFile()` or fragmented MP4 in 2 s pieces, with Opus audio every 1–2 s on its own priority queue (audio is the cheapest proof).
- **Law (US, brief):**
  - *Riley v. California* requires a warrant to search phone contents.
  - Every circuit that has ruled protects filming police in public (*Fields*, *Glik*).
  - Forced biometric unlock: the circuits are split (9th: allowed; D.C.: a 5th Amendment violation). A PIN is protected.
  - Audio in all-party-consent states (e.g. Massachusetts) is the user's responsibility.

## Architecture: the phone does the job, the car keeps copies

Decided 2026-11-07: **the phone is the main recorder, beacon and cloud uploader.** The Pi and the custom OBD adapter are backup storage that receive a copy of every piece while the phone is in range. No extra cameras are needed; they are an optional add-on (end of this section).

```
 trigger: OCTAVE button | knob long-press | hidden panic button (adapter) | phone notification action
                                   │
                                   ▼
 ┌──────────────────────── Phone: OCTAVE companion app ─────────────────────────┐
 │ pre-armed foreground service (camera | microphone | connectedDevice)         │
 │ front + rear camera + mic → 2 s fMP4 + Opus pieces, hash-chained, signed     │
 │ SMS beacon + location to contacts · cloud upload · live stream (stretch)     │
 └──────┬───────────────────────────────┬──────────────────────────────┬────────┘
        │ mobile data                   │ Wi-Fi (Pi hotspot)           │ Wi-Fi / BLE
        ▼                               ▼                              ▼
 user-owned cloud storage        Pi: <config>/incidents/        OBD adapter: microSD,
 (encrypted, off the car)        viewer, black-box freeze      hidden, always-hot, survives
                                                                engine-off and the Pi
```

### 1. Phone (main)

- **Companion app** (`companion/`, Kotlin). The phone-server-only route can't do it: its camera recording ends when adb drops (see the research above).
- **Recording:**
  - A foreground service of type `camera|microphone|connectedDevice`, started while visible at drive start (OCTAVE's shell can `am start` it) and held all drive. The camera opens on trigger.
  - Front + rear camera where the phone allows that pair (`getConcurrentCameraIds()`), otherwise front only, plus the mic.
  - 2 s fragmented-MP4 pieces plus Opus audio every 1–2 s. Audio goes first on its own queue: it's the cheapest proof.
- **Triggers:**
  - OCTAVE's button and the adapter's panic button arrive as a BLE event (Companion Device Manager association with the adapter), or over the mirror connection while it's up.
  - The notification's "Record" action works with no car at all.
  - If the service isn't armed, a lock-screen activity is the fallback start.
- **Beacon:**
  - The app sends the SMS itself (`SmsManager`, `SEND_SMS`): contacts, a message template, location and a link to the uploads / live view.
  - Re-sends the location every N minutes while active.
  - `SEND_SMS` is fine for a sideloaded app. On Google Play it is restricted to default SMS apps, so plan on sideload / F-Droid. That also avoids the app-store removal risk.
- **Cloud:** each piece sealed with libsodium `crypto_box_seal` to the user's public key, then a resumable upload (tus / S3 presigned / WebDAV) to storage the user owns. The server only ever holds ciphertext.
- **Evidence:** SHA-256 per piece, chained to the previous piece, signed with an Android Keystore / StrongBox key, plus time, GPS and cell metadata. Hashes go out before the media.
- **Hardening:**
  - Battery-optimisation whitelist (via the shell).
  - Best-effort flush on `ACTION_SHUTDOWN`.
  - On trigger, a prompt to put the phone in Lockdown (turns off biometric unlock; recording keeps going).
  - Samsung "Lock network and security" on (power-off and the network quick toggles need an unlock). It doesn't stop a forced restart, which is why every piece leaves the phone at once.
- **Live stream (stretch):** WHIP or RTMP from the app to a user-run relay or a service account; the link goes in the SMS.
- **Phone server fallback** (phone without the app): a detached incident mode that writes pieces on the phone, keeps recording after the socket drops (`cleanup=false`), and pushes to the Pi when it can. The shell also installs, whitelists and starts the app.

### 2. Pi (backup copy + control)

- `IncidentManager` (`src/managers/incidentmanager.{h,cpp}`, exposed as `incident`):
  - the trigger UI and knob long-press;
  - a receiver for the phone's pieces over the Pi hotspot;
  - per-copy status (phone recording / cloud up to mm:ss / Pi up to mm:ss / adapter up to mm:ss);
  - the incident viewer and the decrypt + verify tool.
- Freezes the black box (`triprecorder` / `tripstore`): the last N minutes of OBD next to the video.
- Not trustworthy alone: it dies with the engine and gets towed with the car.

### 3. Custom OBD adapter (backup copy that outlives the Pi)

Always-hot, hidden under the dash, keeps running after engine-off. It now **does need on-board storage** to hold media:

- **Storage:** microSD (high-endurance, 32–64 GB) on the ESP32 (the STM32G474 has no SD host). **On Rev 0 since 2026-11-09:** J10 on the bottom edge, SPI mode (MISO IO4, CLK IO5, MOSI IO21, CS IO22, detect IO23), about 2 MB/s, plenty for the pieces. At 2 cameras × 500 kbps + audio, an hour is about 500 MB. Pieces arrive already encrypted; the adapter only stores and forwards them, so a stolen card shows nothing.
- **Receiving:**
  - Wi-Fi from the phone: the ESP32 joins the Pi hotspot or offers its own access point when the Pi is off. A realistic 2–5 Mbps is enough for 480p pieces.
  - BLE is too slow for video and carries the trigger and status only.
- **Trigger:** the panic button (Rev 0: J9, a JST SH 2-pin socket with ESD protection, on PC13 / WKUP2), sent to the phone over BLE and to the Pi over the native protocol.
- **Evidence of its own:** frozen + signed OBD speed window (Rev 0: U16 ATECC608B on the STM32 I2C bus), with the piece hashes it received logged in the same chain.
- **Forwarding later:** when the incident is over and the phone or the Pi has internet again, the adapter re-offers anything the cloud is missing.
- **Optional, not needed for v1:** head-unit power-hold output, supercap + unplug alarm, its own LTE-M modem (nRF9151) as a phone-free beacon.

### Coverage

| What happens | Survives on |
|---|---|
| Phone taken after recording started | cloud (if signal), Pi, adapter |
| Phone forced off / restarted | everything uploaded or copied up to that second |
| No cell signal | Pi + adapter (car copies only, so the car being towed is the risk) |
| Engine off / Pi shut down | phone, cloud, adapter |
| Adapter unplugged | phone, cloud, Pi |
| Phone taken **before** the trigger | nothing from the phone; the adapter's OBD record only. Covered only by the optional cameras below |

### Optional extras (later, not required)

- USB cameras on the Pi (cheap, dies with the engine).
- The "survivor pod" from the research pass: hidden board with its own always-hot feed, camera (RV1106), microSD, GNSS and Cat-1bis modem + eSIM, so recording and the beacon work with no phone at all. Merges with the sensor pod in `diy-obd-adapter.md`. Roughly $80–120.

### Rules

- **Manual trigger only;** nothing starts on a guess.
- **No OCTAVE-run server** in the evidence path; storage and relay belong to the user; no telemetry.
- **Never claim the evidence is safe until it is.** The status line shows each copy separately.
- **Visible recording indicator** (legal), with an optional "quiet" mode that keeps a small dot.
- **Android only for the main path.** For an iPhone, OCTAVE can at most remind the user to run an Apple Shortcut.

## Spikes (answer before designing further)

On the S22 (ask before running anything against it):
1. Can a pre-armed foreground service (`camera|microphone|connectedDevice`) open front + rear cameras after the screen locks, and keep going for 15+ minutes (heat, battery)?
2. Can a BLE event from an ESP32 dev board (standing in for the adapter), through a Companion Device Manager association, start recording, or launch the lock-screen fallback?
3. Can the app push 2 s pieces to the Pi over its hotspot and to a cloud target over mobile data at the same time, and send SMS with location?
4. Can an ESP32 + microSD receive the pieces over Wi-Fi fast enough?
5. (Fallback only) Does a shell camera session hold with the screen locked (scrcpy #5977)?

## Steps

1. **Spikes 1–4.** **Effort:** M. **Device:** S22, ESP32 dev board + microSD breakout.
2. **Evidence format:** piece + hash-chain + signature spec, a verifier (`tools/incident/verify`) and a QtTest. Used by the phone, the Pi and the adapter. **Effort:** M. **Device:** desktop.
3. **Companion app v1:** pre-armed service, cameras, pieces, encryption, upload to one cloud target, SMS beacon, notification trigger. Shared with the notifications work in `octave-companion-app.md`. **Effort:** L–XL. **Device:** S22, S26.
4. **Pi side:** `IncidentManager`, trigger UI, piece receiver, black-box freeze, status, viewer. **Effort:** L. **Device:** desktop, Pi.
5. **Adapter backup:** ESP32 Wi-Fi receiver + microSD, BLE trigger / status, panic button, signed OBD window. Rev 0 has the microSD slot, panic input, ATECC608B and a wake-on-motion accelerometer (LIS2DH12). Firmware after `diy-obd-adapter.md` step 3 (ESP32 bridge). **Effort:** L. **Device:** adapter, S22, Jeep.
6. **Storage setup:** user-configured cloud target, key pair (private key never on the phone or a server), decrypt + verify for the user. **Effort:** M. **Device:** desktop.
7. **Phone-server fallback** (detached incident mode). **Effort:** M. **Device:** S22.
8. **Stretch: live stream** from the app. **Effort:** L. **Device:** S22 on mobile data.
9. **Optional extras:** Pi USB cameras, then the survivor pod. **Effort:** M, then XL.

Each step updates the wiki (new `traffic-stop.html`, `phone-mirror-android-auto.html`, `settings-reference.html`, `hardware-managers.html`) and regenerates `docs/api/` for new manager surface, per CLAUDE.md.

## Cross-references

- `diy-obd-adapter.md`: the adapter becomes the always-hot backup store: microSD on the ESP32's SDMMC host, Wi-Fi receiver for the phone's pieces, BLE trigger / status, panic button, signed OBD window. The survivor pod is an optional extra that would merge with its sensor pod. Rev 0 carries the hardware for all of it (microSD J10, panic J9, ATECC608B, LIS2DH12); parked current is about 0.5 mA with the ESP32 off (`hardware/obd-adapter/README.md`, "Parked current").
- `octave-companion-app.md`: this plan decides its "extend phone_server or build an app" question in favour of the app.
- `phone-mirror-robustness.md`: the shell fallback is only as reliable as the phone connection.

## Recommended order

Spikes (1) → evidence format (2) → companion app (3) → Pi side (4) → storage setup (6) before first real use → adapter backup (5) as the adapter plan reaches its ESP32 bridge → fallback (7) → live stream (8). Extras (9) only if wanted.

Delete this file (and its README row) when done.
