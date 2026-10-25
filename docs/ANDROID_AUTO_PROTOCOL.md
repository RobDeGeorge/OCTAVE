# Android Auto Protocol (AAP): what OCTAVE's receiver speaks

OCTAVE is the head unit; the phone's Android Auto app is the other end. This file holds the byte layouts and message flow the native receiver (`src/android_auto/`) implements, the way `docs/PHONE_MIRROR_NATIVE_PLAN.md` holds the scrcpy ones. Plan and phases: `TODO/android-auto-native-receiver.md`.

Sources: aasdk (`github.com/opencardev/aasdk`, GPLv3; `src/Messenger/`, `src/Channel/`, `protobuf/aap_protobuf/`), checked against a real phone.

**Verified 2026-10-25** on a Samsung SM-S948U with Android Auto 17.9.664044 over the adb head unit server (wireless adb): handshake, discovery, every channel below opening, 1280x720 H.264 Baseline at ~20–30 fps, 48 kHz stereo media PCM, clean bye-bye. Spike scripts (Python, gitignored): `dev/aa-spike/aa_handshake.py`, `dev/aa-spike/aa_video.py`.

## Transport

**adb head unit server (what OCTAVE uses first).** Android Auto developer mode (Android Auto settings > tap *Version* ~10 times) adds *⋮ > Start head unit server*: `com.google.android.projection.gearhead/.companion.DeveloperHeadUnitNetworkService` listening on phone TCP 5277. `adb forward tcp:N tcp:5277` and connect to `127.0.0.1:N`. AAP starts at the version request; there is no AOA step.

- On Android Auto 17.9 the server **cannot be started over adb**: `am start ... START_HEAD_UNIT_SERVER` (the activity is gone) and `am startservice` (not exported, "Requires permission not exported from uid ...") both fail. The user taps *Start head unit server* once; it survives USB unplug and serves back-to-back sessions.
- If a session is dropped without a bye-bye at the wrong moment, the server can keep accepting TCP but stop answering (no version response, nothing in logcat). *Stop head unit server* / *Start* fixes it. Always end with BYEBYE.
- Check it is listening: phone `/proc/net/tcp6` has a row for local port `:149D` in state `0A`.

**USB accessory (AOA).** Same AAP after the AOA switch; not implemented yet (Phase 5 of the TODO).

## Framing

Every message travels in one or more frames:

| bytes | field |
|---|---|
| 0 | channel id |
| 1 | flags: bits 0–1 frame type (`1` FIRST, `2` LAST, `3` BULK = whole message, `0` MIDDLE), bit 2 `0x04` CONTROL message type, bit 3 `0x08` ENCRYPTED |
| 2–3 | frame payload size, big-endian u16 |
| 4–7 | **only on FIRST (not BULK) frames:** total message size, big-endian u32 |
| … | payload |

- A message is a big-endian u16 message id followed by its body (a protobuf, except where noted).
- **Encryption is per frame.** When `ENCRYPTED` is set the frame payload is TLS application data. TLS is a single stream across all channels, so decrypt every frame in arrival order *before* reassembling per channel; reassembling ciphertext per channel breaks the moment two channels interleave.
- The CONTROL bit marks control-type messages sent on a service channel: CHANNEL_OPEN_REQUEST/RESPONSE on channels ≠ 0 carry it. Everything on channel 0 and the per-service messages are sent without it (aasdk `MessageType::SPECIFIC`).
- The head unit may send every message as a single BULK frame (all OCTAVE sends is far below 64 KB). The phone fragments video.

## Handshake (channel 0, plain)

1. → `VERSION_REQUEST` (1): body is u16 major, u16 minor, big-endian, not protobuf. aasdk sends 1.6.
2. ← `VERSION_RESPONSE` (2): u16 major, u16 minor, u16 status (0 = OK). The phone answered 1.7, status 0.
3. TLS 1.2, **head unit is the TLS client**, no verification of the phone's certificate. Each TLS flight goes in `ENCAPSULATED_SSL` (3) messages, plain frames, the body being raw TLS bytes. Drive OpenSSL through a memory BIO pair. Negotiated: `ECDHE-RSA-AES128-GCM-SHA256`.
4. → `AUTH_COMPLETE` (4), plain: `AuthResponse { status = 0 }` = `08 00`.
5. Everything after is ENCRYPTED, except PING messages, which aasdk sends plain (either is accepted).

**Certificate.** The phone requires a client certificate chained to Google's "Google Automotive Link" CA. OCTAVE ships none; it loads a PEM certificate + key at runtime (`androidAutoCertPath`). The aasdk one (subject `O=JVC Kenwood, OU=01`, serial 0x1B, valid to 2045, RSA 2048) is accepted by Android Auto 17.9; logcat shows `CAR.GAL.SECURITY.LITE: Peer certificate subject name: ...` then `SSL_SUCCESS`.

## Control messages (channel 0)

| id | name | direction | notes |
|---|---|---|---|
| 5 | SERVICE_DISCOVERY_REQUEST | ← | `device_name` (e.g. "samsung SM-S948U"), `label_text`, icons |
| 6 | SERVICE_DISCOVERY_RESPONSE | → | the channels, below |
| 7 / 8 | CHANNEL_OPEN_REQUEST / RESPONSE | ←/→ | on the service's own channel, CONTROL bit set; request `{priority, service_id}`, response `{status = 0}` |
| 11 / 12 | PING_REQUEST / RESPONSE | ↔ | `{timestamp}`; the head unit should also ping (aasdk-based units do every ~1.5 s) |
| 13 / 14 | NAV_FOCUS_REQUEST / NOTIFICATION | ←/→ | answer `{focus_type = 2}` (PROJECTED) |
| 15 / 16 | BYEBYE_REQUEST / RESPONSE | ↔ | request `{reason = 1}` (USER_SELECTION); end every session with it |
| 18 / 19 | AUDIO_FOCUS_REQUEST / NOTIFICATION | ←/→ | request type 4 (RELEASE) → state 3 (LOSS); otherwise → state 1 (GAIN) |

## Service discovery response

`ServiceDiscoveryResponse` (field numbers): `channels` 1 (repeated `Service`), `driver_position` 6, `session_configuration` 13, `display_name` 14, `headunit_info` 17 (`make` 1, `model` 2, `year` 3, `vehicle_id` 4, `head_unit_make` 5, `head_unit_model` 6, `head_unit_software_build` 7, `head_unit_software_version` 8); deprecated copies of those strings at 2–5 and 7–10 are still filled by aasdk-based units.

`Service`: `id` 1 (= the channel id the phone will use), then exactly one of `sensor_source_service` 2, `media_sink_service` 3, `input_source_service` 4, `media_source_service` 5, ...

What OCTAVE announces (aasdk's channel numbering):

| ch | service | content |
|---|---|---|
| 1 | sensor source | `sensors` 1: `{sensor_type 1}` = 13 DRIVING_STATUS_DATA, 10 NIGHT_MODE |
| 3 | media sink, video | `available_type` 1 = 3 (H264_BP), `available_while_in_call` 5, `video_configs` 4: `{codec_resolution 1, frame_rate 2, width_margin 3, height_margin 4, density 5}`; resolution 1 = 800x480, 2 = 1280x720, 3 = 1920x1080; frame rate 1 = 60, 2 = 30 |
| 4 | media sink, media audio | `available_type` = 1 (PCM), `audio_type` 2 = 3 (MEDIA), `audio_configs` 3: `{sampling_rate 1 = 48000, number_of_bits 2 = 16, number_of_channels 3 = 2}` |
| 5 | media sink, guidance audio | `audio_type` = 1, 16000 Hz, 16 bit, mono |
| 6 | media sink, system audio | `audio_type` = 2, 16000 Hz, 16 bit, mono |
| 8 | input source | `touchscreen` 2: `{width 1, height 2}` = the video resolution |
| 9 | media source, microphone | `available_type` 1 = 1 (PCM), `audio_config` 2 = 16000 Hz, 16 bit, mono |

Rejections seen in logcat while getting here (`CAR.SERVICE: Critical error 2 ...`): detail 72 "No audio playback service" without an audio sink; detail 24 "No audio/mic" without the microphone source. Video, at least one audio sink and the microphone are required.

## Media sink channels (video and audio)

| id | name | direction | body |
|---|---|---|---|
| 0x8000 | SETUP | ← | `Setup { type }` (video `08 03`, audio `08 01`) |
| 0x8003 | CONFIG | → | `Config { status 1 = 2 (READY), max_unacked 2 = 1, configuration_indices 3 = [0] }` |
| 0x8001 | START | ← | `Start { session_id 1, configuration_index 2 }` |
| 0x8002 | STOP | ← | |
| 0x0001 | CODEC_CONFIG | ← | raw bytes, **not** protobuf: H.264 SPS + PPS in Annex B (29 bytes, `00 00 00 01 67 42 80 1f ...`) |
| 0x0000 | DATA | ← | 8-byte big-endian timestamp (µs), then the payload: Annex-B H.264 access unit, or s16le PCM |
| 0x8004 | ACK | → | `Ack { session_id 1, ack 2 = 1 }` for **every** CODEC_CONFIG and DATA; with `max_unacked = 1` the phone waits for it |
| 0x8007 | VIDEO_FOCUS_REQUEST | ← | |
| 0x8008 | VIDEO_FOCUS_NOTIFICATION | → | `{ focus 1 = 1 (PROJECTED), unsolicited 2 = false }`; send right after CONFIG on the video channel and in answer to every focus request |

The first video frame arrived ~0.3 s after the handshake. Media audio starts (AUDIO_FOCUS_REQUEST GAIN, then START) when the phone begins playback; with *Start music automatically* on, it does so on connect.

## Microphone (channel 9)

| id | name | direction | body |
|---|---|---|---|
| 0x8005 | MICROPHONE_REQUEST | ← | `{ open 1, anc_enabled 2, ec_enabled 3, max_unacked 4 }` |
| 0x8006 | MICROPHONE_RESPONSE | → | `{ status 1 = 0, session_id 2 }` |

Mic audio goes head unit → phone as DATA (8-byte timestamp + 16 kHz mono s16le PCM) on channel 9 while the phone has it open; the phone acks it with ACK. OCTAVE captures the default input only between an open and a close request (`AapConnection::onMicrophone`). Covered by `tst_androidauto`; not yet checked against a phone (no one was there to talk to it).

## Sensors (channel 1)

| id | name | direction | body |
|---|---|---|---|
| 0x8001 | SENSOR_REQUEST | ← | `{ type 1, min_update_period 2 }` |
| 0x8002 | SENSOR_RESPONSE | → | `{ status = 0 }` |
| 0x8003 | SENSOR_BATCH | → | `SensorBatch`: `night_mode_data` 10 `{night_mode 1}`, `driving_status_data` 13 `{status 1}` (0 = unrestricted) |

Send a batch with the current value right after the response, then whenever it changes.

## Input (channel 8)

| id | name | direction | body |
|---|---|---|---|
| 0x8002 | KEY_BINDING_REQUEST | ← | `{ keycodes 1 }` (empty for a touchscreen-only head unit) |
| 0x8003 | KEY_BINDING_RESPONSE | → | `{ status = 0 }` |
| 0x8001 | INPUT_REPORT | → | `{ timestamp 1, touch_event 3 { pointer_data 1 {x 1, y 2, pointer_id 3}, action_index 2, action 3 } }` (touch not verified yet); action 0 DOWN, 1 UP, 2 MOVED, 5 POINTER_DOWN, 6 POINTER_UP |
