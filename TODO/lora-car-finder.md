# LoRa car finder (the car as a beacon, a keychain that points at it)

**Goal:** a dash-mounted pod wired to the OBD adapter makes the parked car a LoRa beacon that knows its own position, and an electronic keychain shows a needle that always points at the car, like a compass that points home.
**Status:** active: the pod board is designed up to placement (`hardware/lora-pod/`, 2026-11-23): schematic ERC clean, board placed and routed (DRC: 0 unconnected, 2 cosmetic warnings), no fab files, no firmware. Nothing bought, no numbers measured. Every current and range figure below is an estimate.
**Last updated:** 2026-11-23

## Decisions of 2026-11-23 (they override the text below where it differs)

- **No dev boards (step 1 is skipped):** as few orders as possible. The pod is ordered with the OBD adapter and brought up with it; pod versus plug is answered on the real hardware.
- **The pod also works standalone:** it takes 5 to 16 V on its cable connector (the adapter's 5 V, or the battery through a plain OBD power lead), has automotive input protection of its own and reads the battery voltage, so it can tell "parked" without the adapter. This replaces "the pod needs no automotive protection" under "Decisions made".
- **Keychain and phone:** the pod's module is a RAK4630 (nRF52840 + SX1262), which adds Bluetooth LE for a phone and is the chip the keychain table names, so pod and keychain can share firmware. STM32WL is dropped.
- **Any car, not only the TJ:** nothing on the pod is vehicle-specific. 915 MHz parts are for the Americas; Europe takes the 868 MHz variant of the same module.
- **Ranging for the last 50 m** (garages) is not in v1, but the pod has an unfitted SPI header (`RANGE`) to try a UWB or SX1280 module on Rev 0.
- **The keychain is not in the first order.**
- **Tow / theft behaviour:** decided once the boards are in hand.

## Why it's parked

It is a new hardware project (two boards and their firmware), and its permanent home is the sensor pod of `diy-obd-adapter.md`, whose adapter has no firmware yet. The idea itself can be proven on bought dev boards with no custom PCB (step 1), so that is the place to start whenever this is picked up; the boards wait on that result.

## How it works

LoRa cannot give a direction: it says "I hear the car" and how strongly, and an antenna array that could find a bearing at 915 MHz does not fit a keychain. The needle comes from positions instead:

- The **car** knows where it is parked (GNSS in the pod).
- The **keychain** knows where it is (its own GNSS) and which way it faces (magnetometer, tilt-corrected by an accelerometer).
- **Needle** = bearing from the keychain to the car, minus the keychain's heading. Distance comes from the same two positions.

LoRa only carries the car's coordinates to the keychain, and a parked car does not move, so the link is needed rarely:

1. **Ignition off.** The adapter tells the pod; the pod keeps its last good fix and beacons it. The keychain is beside the car at that moment and stores it.
2. **Finding the car.** The keychain points from the stored position with its own GNSS and compass. No radio link is needed, so it works at any distance and when the car is deep in a concrete garage.
3. **While parked.** A short beacon every 30 to 60 s: position, battery voltage, a "moved" flag. In range (estimated 1 to 3 km in a town, less from a garage) the keychain learns of a tow, a theft or a low battery.

## Decisions made

- **A pod on the dash top, not the radio on the plug.** Under the dash both the GNSS and the 915 MHz antenna are compromised; under the windshield neither is. OBD-plug GPS trackers show the plug would work, but the pod is already planned for the same reason.
- **The adapter stays whole; the pod is a peripheral.** This changes the split sketched in `diy-obd-adapter.md` ("Sensor pod"), which puts the MCU and radio in the pod and leaves only transceivers on the plug. Rev 0 is a complete adapter on the plug, so keep that and give the pod its own small MCU. Only slow digital signals cross the cable, no raw J1850 VPW or CAN, and the adapter works without the pod.
- **Four wires, about 1 m:** 5 V, ground, UART each way, a small locking connector (JST-GH class) at both ends.
- **The pod is powered by the adapter.** Regulated 5 V from the adapter's buck (pin 16 is unswitched battery, so it is there with the key out), not raw 12 V, so the pod needs no automotive protection of its own. A low-quiescent 3.3 V regulator on the pod.
- **Car-side GNSS is kept**, although the keychain could take the park fix itself. It is what gives the towed / stolen case, and a better park position (the pod has sky, and later dead reckoning into a garage).
- **No battery in the pod.** A dash top in the sun passes 80 °C. A supercap for GNSS hot starts; a light or vented case.

## Hardware

### Pod (car side)

| Part | Pick (verify stock and price) | Notes |
|---|---|---|
| MCU + LoRa | STM32WL (STM32WLE5; modules: Wio-E5, RAK3172) | One chip. Stop 2 between beacons. |
| GNSS | Whatever the sensor pod settles on (`diy-obd-adapter.md` lists u-blox NEO-M9V / ZED-F9R); a plain low-power module is enough for this feature alone | Off while parked after the park fix; woken when the IMU sees motion. |
| IMU | Same as the sensor pod (ASM330LHH / LSM6DSV16X) | "Moving while off" = tow. The adapter's LIS2DH12 can also raise it over the UART. |
| Antenna | 915 MHz (868 in Europe), as close to a quarter wave (about 8 cm) as the case allows | The reason for the pod. Keep clear of the GNSS patch. |

No magnetometer is needed here; only the keychain needs a compass.

### Adapter side

- Rev 0 already has what a first pod needs on **J5** (`hardware/obd-adapter/README.md`): `5V`, `3V3`, USART1 and spare GPIO. Two catches: USART1 is the firmware console until the USB serial port on J2 exists, and the 5 V pin is not switched.
- **Rev A:** a dedicated pod connector fed through a **current-limited load switch**, so a pinched cable shuts that rail off instead of browning out the adapter, and so the 11.8 V low-battery cutoff can drop the pod too.
- Over the UART the adapter sends ignition on / off, battery voltage and (later) OBD speed for dead reckoning; the pod sends position, beacon status and keychain events.

### Keychain

| Part | Pick (verify) | Notes |
|---|---|---|
| MCU + LoRa | STM32WL, or nRF52840 + SX1262 if BLE setup from a phone is wanted | Decide with the pairing question below. |
| GNSS | Small low-power module, 15 to 18 mm ceramic patch | The hard part: the antenna sets the size, a cold fix takes 30 s or more. Periodic short wakes keep the ephemeris warm. |
| Compass | MMC5983 or LIS3MDL + accelerometer | Needs a figure-8 calibration routine; keep it away from the battery and any buzzer magnet. |
| Display | Ring of 12 to 16 LEDs | The compass look, readable in sunlight; colour or blink rate for distance. A small round LCD is prettier and costs power and thickness. |
| Power | 100 to 150 mAh LiPo, USB-C charging, a button to wake | Asleep it only opens short receive windows for the beacon. Estimated weeks per charge. |

Limits to design the UI around: GNSS is good to 3 to 5 m, so near the car the needle should give way to "you're here"; the keychain cannot fix indoors, so in a garage it should show the last bearing and distance and say so.

## Radio protocol (to design)

- **Beacon:** position, fix age, battery voltage, flags (moved, low battery), a counter. Around 20 bytes.
- **Encrypted and authenticated** with a key per pairing (AES-CCM class) and a counter against replay. Unencrypted, the car broadcasts its location to anyone listening.
- **Pairing** only inside a window opened by a button, as the adapter does for Bluetooth.
- **Find my keys:** the car asks, the keychain blinks and beeps. The keychain's receive windows must be timed to the beacon for this.
- **Check the regional rules before fixing the modulation.** In the US the 915 MHz band wants either frequency hopping or a wide (500 kHz) channel; Europe's 868 MHz has duty-cycle limits. Not verified.
- **Airtime sets the sleep current.** A 20-byte beacon is roughly 50 ms at SF7 and 200 ms at SF9; at one a minute that is an estimated 40 to 150 µA average on top of the adapter's parked draw (Rev 0 estimate 0.53 mA). Measure.

## OCTAVE side

- The pod reaches OCTAVE through the adapter's native mode (`diy-obd-adapter.md` step 4). For the prototype, a dev board on USB serial, read the way `esp32volumemanager` reads the knob.
- A new manager (working name `beaconmanager`: `src/managers/`, a member of `AppCore`, `expose()`, an `octave.scope` class info): paired keychains, last park position, beacon state, `pingKeychain()`.
- Settings page to pair and remove keychains; keychain status and a "find my keys" button on Home.
- The park position into the black box (`triprecorder` / `tripstore`) with the trip that ended there.
- New hardware code behind `OCTAVE_WASM` guards; wiki: `hardware-managers.html`, `hardware-setup.html`, `settings-reference.html`.

## Open questions

- Is under-dash good enough after all? Step 1 measures both mounts in the TJ; if the plug is fine, a LoRa footprint on Rev A is cheaper than a pod for people who want only this.
- BLE on the keychain (phone setup, firmware update) or LoRa + USB only?
- LED ring or small LCD?
- One keychain per car, or several keychains and several cars?
- Last 50 m in a garage: SX1280 (2.4 GHz LoRa) ranging or UWB gives a true distance, at the cost of a second radio. Leave out of v1.

## Steps

1. **Prove it on dev boards.** Two LoRa + GNSS boards (Heltec / T-Beam class), a magnetometer breakout on the handheld one. Car side on 12 V in the TJ, tried under the dash and on the dash top; measure range and fix time for both, and whether the needle is usable on foot. **Effort:** M. **Needs:** two dev boards, Jeep.
2. **Protocol and firmware on the dev boards:** encrypted beacon, pairing window, park handoff, moved flag, keychain sleep with timed receive windows. **Effort:** M to L. **Needs:** dev boards.
3. **OCTAVE prototype:** `beaconmanager` over USB serial, the settings page, the black-box park position, a QtTest with a fake serial peer. **Effort:** M. **Needs:** desktop, a dev board.
4. **Pod board:** STM32WL + GNSS + IMU + supercap, the four-wire cable, first to Rev 0's J5. Measure sleep current with the beacon running. **Effort:** L. **Needs:** adapter Rev 0 built and running firmware, bench supply, current meter.
5. **Keychain board and case:** antenna layout for GNSS and 915 MHz in a keychain is the risk; expect two spins. Compass calibration, LED ring, charging. **Effort:** L to XL. **Needs:** step 2's firmware, a way to make a case.
6. **Adapter Rev A:** the pod connector with its load switch; pod traffic through native mode instead of USB. **Effort:** M on top of `diy-obd-adapter.md` step 6. **Needs:** adapter native mode (its step 4).
7. **Hot-car and battery tests:** the pod on a summer dash, a week parked with the beacon on, the 11.8 V cutoff. **Effort:** M. **Needs:** Jeep.

## Cross-references

- `diy-obd-adapter.md`: the pod here is its sensor pod (step 7) with LoRa added and the split changed as above; step 6 (Rev A expansion port) should carry the pod connector. Its "LoRa mesh (Meshtastic)" extra would want its own SX1262 or a firmware mode of this one: decide then. Its "parked watchdog" (IMU wake) is this plan's moved flag.
- `octave-companion-app.md`: step 9 (where you parked, from the phone's location) and step 14 (tow / break-in alert, Bluetooth range only) cover the same ground with the phone. The keychain is the no-phone version, and the pod's LoRa extends the alert's range; share the park position record between them.
- `obd-adapter-discovery.md`: unrelated to the LoRa pairing, but the settings page should sit beside the adapter's.

## Recommended order

1 (go / no-go, and pod versus plug) → 2 → 3 → 4 and 5 in either order → 6 → 7. Steps 1 to 3 need no custom PCB and do not wait on the adapter.

Next: the pod board's fab outputs and a human look at its copper (`hardware/lora-pod/README.md`, "Before ordering"), then steps 2 and 3, which need no hardware.

Delete this file (and its README row) when done.
