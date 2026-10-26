# CarPlay without a dongle (spike)

**Status:** deferred, research only (2026-10-25). Needs an iPhone on USB and a decision already taken: OCTAVE never ships an Apple accessory identity, the user supplies one, exactly the rule for the Android Auto head-unit certificate (`TODO/android-auto-native-receiver.md`, Risks).
**Last updated:** 2026-10-25

**Goal:** real CarPlay (Apple's car UI, touch, Siri, audio) from an iPhone on USB into OCTAVE, with no Carlinkit dongle.

## What the research found (2026-10-25, not verified against a phone)

- CarPlay checks an Apple accessory identity (MFi authentication), normally held in Apple's authentication coprocessor chip. Software can't create one.
- **LIVI** (`github.com/f-io/LIVI`, GPL-3.0-or-later, Rust) implements the CarPlay accessory side natively on Linux and macOS, wired ("any USB port, no OTG") and wireless (Bluetooth + its own Wi-Fi AP). It needs the MFi coprocessor on I²C (`carPlayMfiI2cBus`, `carPlayMfiPowerGpio`) or over the network ("LIVI Link"); it neither ships nor bypasses one. Note: the Carlinkit plan in `TODO/carlinkit-dongle-port.md` describes an older, TypeScript LIVI that drove the dongle; the project has moved on.
- **DiPlay** (`github.com/shihabal3amri/DiPlay`, GPL-3.0, UI parts AGPL-3.0) is a CarPlay receiver APK for BYD head units built on **xcertplay** (GPL-3.0). It authenticates with "an experimental accessory identity recovered from public Carlinkit firmware" and says the bundled private key is extractable and that acceptance after future iOS updates is unresolved.
- So a software-only CarPlay works only with an identity someone extracted; Apple can stop accepting it in an iOS update, for every user at once. Legally greyer than the Android Auto certificate.

## Why it's parked

The AirPlay path (`TODO/iphone-airplay.md`) gives an iPhone mirroring with touch and its audio with no identity at all; finish that first. CarPlay is a project the size of the native Android Auto receiver, and the identity question is fragile.

## Steps

1. **Read before writing.** **Effort:** M. **Device:** none. Study xcertplay and LIVI's wired path: how the head unit (USB host) talks iAP2 to the iPhone, the authentication exchange, how the iPhone brings up its network interface over USB, and the CarPlay session on top (AirPlay-like screen and audio streams). Write the byte layouts into `docs/CARPLAY_PROTOCOL.md` the way `docs/ANDROID_AUTO_PROTOCOL.md` was written. Check which of UxPlay's pieces (`third_party/uxplay/`: mirror stream, FairPlay, NTP) the CarPlay session reuses. Licences: LIVI and xcertplay are GPL-3.0 and can be used; DiPlay's AGPL parts are read, not copied.
2. **Spike: one wired session with a picture on the desktop.** **Effort:** L. **Device:** desktop Linux + iPhone on USB + a user-supplied identity in a gitignored `dev/carplay-identity/`. USB access (libusb or the kernel's USB networking), the identity loaded from `<config>/carplay/` like `androidAutoCertPath`, the H.264 into the shared decoder.
3. **Decide** from the spike whether to build it out (touch, audio, Siri, the page, settings, Pi) or keep AirPlay as the iPhone answer. Write the build-out as its own plan then.

Related: `TODO/iphone-airplay.md`, `TODO/carlinkit-dongle-port.md` (the hardware route: the dongle holds the chip), `TODO/android-auto-native-receiver.md` (same certificate policy).

Delete this file when step 3 is decided.
