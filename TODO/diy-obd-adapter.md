# DIY OBD adapter (OCTAVE's own Bluetooth OBD-II scanner + sensor pod)

**Goal:** build our own OBD-II adapter that speaks every OBD protocol (including the TJ's J1850 VPW), works with every app over the ELM327 command set, and adds an OCTAVE native mode with an on-adapter PID scheduler, hardware timestamps and store-and-forward into the black box; optionally a tethered sensor pod (GNSS dead reckoning, IMU, baro, extras).
**Status:** parked — brainstormed 2026-10-22, no hardware bought, no code written. A hardware project, not a code task; nothing in OCTAVE depends on it.
**Last updated:** 2026-10-22

## Why this is parked here

Off-the-shelf dongles work today (the NEXAS over `rfcomm://`), so this is a "best possible" upgrade, not a blocker. The gains are real but come from firmware, not silicon:

- **Speed:** with a clone ELM327 every PID waits a full Bluetooth round trip (20–50 ms). An adapter-side scheduler keeps the bus busy and pushes samples. Expected several-fold gain even on 10.4 kbps VPW; unmeasured.
- **No gaps in the black box:** the adapter keeps polling and buffers to flash while Bluetooth is down, then backfills `TripStore`.
- **True sample times:** µs timestamps at bus arrival instead of host arrival.
- **Battery-safe:** clones draw 20–50 mA forever; target is under 1 mA asleep.

The riskiest part is the J1850 VPW front end and driver: every open project found stalls there (see "Prior art"). Do the cheap CAN bench step first and only commit to the board once VPW works in the Jeep.

## Key facts established

- **The TJ (1997–2006) is J1850 VPW, not CAN.** CAN arrived with the JK (2007). An ESP32 + CAN-transceiver dongle shows nothing on the Jeep.
- **"ELM327" is the command language, not the chip.** Genuine ELM327 (a PIC with Elm firmware) is weak: small buffers, one protocol, no CAN FD, fixed firmware. Clones are worse. Ranking: own MCU > STN2120 (ScanTool, OBDLink's chip family; ELM superset, all legacy protocols proven) > genuine ELM327 > clone.
- **What OCTAVE sends today** (`src/managers/elm327protocol.cpp`, `obdmanager.cpp`): `ATZ ATE0 ATL0 ATS0 ATH0 ATSP0 ATAT2 ATRV`. Compatibility mode must cover at least those plus Mode 01–09, `ATSH/ATCRA/ATMA`.
- **BLE UUIDs OCTAVE's Android bridge expects** (`android/src/org/octave/app/OctaveOBDBridge.java`): service `FFE0`, characteristic `FFE1`; also the Microchip transparent-UART service `49535343-FE7D-4AE5-8FA9-9FAFD205E455`. Classic SPP: UUID `1101`.
- **No existing OBD scanner uses the STM32G474** (searched 2026-10-22). The G4 family launched 2019, is active, mature, and on ST's longevity program (check the end date before layout).
- **Nothing newer beats the G474 for this job.** The MCU is never the bottleneck. Alternatives considered: NXP S32K3 (automotive-grade, overkill), STM32H5 (secure boot, but 2 FDCAN and no HRTIM), Renesas RA8 (speed we can't use), ESP32-P4/C5/C6 (classic CAN only, no BT Classic).
- **The real constraint is the radio:** Bluetooth Classic SPP (needed for Linux `rfcomm://`, Android Classic, Windows) is vanishing from new chips. The original ESP32 (WROOM-32E) is the cheap Classic + BLE + Wi-Fi part. Going BLE-only (nRF54-class) only becomes possible once desktop OCTAVE does BLE GATT (see `obd-adapter-discovery.md`).

## Architecture

```
 J1962 plug ──► protection ──► transceivers ──► STM32G474 ◄─UART 3 Mbaud─► ESP32 ──► BT Classic SPP
 (pin 16 12V)   TVS, reverse    VPW/PWM/K/CAN   vehicle-side                     ├─► BLE GATT (FFE0/FFE1)
                polarity, buck                  real-time engine                 └─► Wi-Fi (OTA, TCP :35000)
                                                     │
                                                     ├─► USB-C (CDC serial, wired to the Orange Pi)
                                                     └─► 16 MB QSPI flash (store-and-forward)
```

- **STM32G474:** 3× FDCAN, HRTIM for VPW/PWM bit timing (input capture + PWM out), UARTs for K-line, native USB.
- **ESP32 (original):** Classic + BLE + Wi-Fi bridge only; power-gated in sleep.

### Vehicle protocols

| Bus | J1962 pins | Front end |
|---|---|---|
| **J1850 VPW** (TJ, GM, Chrysler) | 2 | Discrete ~7 V switched driver with slew shaping + comparator receiver; start from the ELM327 datasheet reference circuit and OpenDiag (MC33390 is obsolete) |
| J1850 PWM (old Ford) | 2 / 10 | Discrete differential 5 V, same reference circuit |
| ISO 9141-2 / KWP2000 | 7 (K), 15 (L) | L9637D-class K-line transceiver (openobd2 uses TLIN1027) |
| ISO 15765 CAN / CAN FD | 6 / 14 | TCAN1462-Q1 / TJA1462 |
| GM single-wire CAN (stretch) | 1 | NCV7356 |
| Ford MS-CAN (stretch) | 3 / 11 | Second CAN FD transceiver behind an analog switch |

DoIP (automotive Ethernet) is out of scope.

### Power

- TVS on pin 16 (e.g. SMDJ26CA, load dump / ISO 7637), ideal-diode reverse protection (LM74700-Q1), 4–60 V automotive buck (LMR36015 class), ESD on every bus pin.
- Sleep under 1 mA: ESP32 load-switched off, STM32 in Stop2, CAN transceiver in standby with bus wake. Wake on battery > ~13.2 V, bus activity or button. Hard cutoff at 11.8 V.
- Precision divider for `ATRV` / streamed battery voltage.

### Host side

- **Compatibility mode (default):** ELM327 dialect; works with OCTAVE, Torque, Car Scanner, OBD Fusion unchanged.
- **Native mode (`AT@OCT` or similar):** COBS-framed binary + CRC; host sends PID list with priority/rate, adapter schedules and pushes µs-timestamped samples; batches up to 6 PIDs per Mode 01 request on CAN; raw Mode 22 / UDS (e.g. TJ transmission temp); listen-only CAN sniff with hardware filters; store-and-forward backfill. OCTAVE side: a new protocol class next to `elm327protocol`, selected after the handshake.

### Safety and security

- Never transmits on its own; true listen-only for sniffing; rate limits on raw requests.
- Mode 04 / 08 and UDS write/routine services locked until explicitly unlocked.
- Pairing only in a 2-minute window after a button press; bonded-only after that.
- Watchdogs on both MCUs; STM32 holds bus lines recessive if the radio crashes. Signed OTA (Wi-Fi) or USB DFU. Note: the G474 system bootloader reportedly can't update over FDCAN (ST forum, disputed via AN5405); irrelevant since we update over USB/Wi-Fi.

### Physical

Low-profile right-angle plug (the TJ port is near the driver's knee), one RGB LED, one button. Rough BOM $30–50 at hobby quantities; verify part numbers and stock before buying.

## Sensor pod (optional, later board)

An under-dash dongle is a bad sensor mount (wobbles, steel + wiring wreck the magnetometer, no sky for GNSS). So split it: **OBD plug** (protection, power, bus transceivers) ── ~1 m cable ── **pod** rigidly mounted on the dash top / under the windshield (MCU, radio, sensors). VPW and CAN are fine over a metre. Auto-align the pod to the car frame: gravity at standstill = down, acceleration while OBD speed rises = forward. Add a Qwiic / STEMMA QT I2C + UART expansion port to rev A of the main board either way.

| Sensor | Pick (verify availability) | Notes |
|---|---|---|
| **GNSS** | u-blox dead-reckoning module (NEO-M9V class, or ZED-F9R dual-band) | Headline feature: feed it OBD vehicle speed, keeps position through tunnels / canyons / trees. Its PPS disciplines the adapter clock. |
| IMU | ST ASM330LHH (automotive) or LSM6DSV16X (on-chip fusion) | G-forces, pitch/roll, DR input |
| Barometer | BMP390 / LPS22HH | Altitude, climb rate |
| Magnetometer | LIS3MDL / MMC5983 | Weak even in the pod; prefer GNSS course + gyro for heading |
| Ambient light | VEML7700 | Auto-dim OCTAVE |
| Temp / humidity | SHT45 | Cabin |
| CO | Electrochemical module | Soft-top TJ exhaust-leak safety; needs calibration care |
| TPMS receiver | CC1101 + aftermarket 315/433 MHz valve-cap sensors | TJ has no TPMS; useful when airing down |

GNSS draws ~25 mA: switch it off in sleep, supercap or coin cell for hot starts. Sensor channels ride the native mode and store-and-forward like OBD samples. On the Pi the BerryIMU (`berryimumanager`) already covers IMU; the pod mainly helps phone/tablet builds and other cars, and OCTAVE should prefer the better-mounted source.

## Extras brainstorm ("wacky things")

**Firmware only, no extra hardware (top pick):**
- **Death-wobble detector:** high-rate IMU, characteristic lateral oscillation at speed, logged with GPS; onset trend before it gets violent.
- Driveline vibration FFT vs OBD speed / RPM: tire vs driveshaft vs engine.
- **Battery / starter health:** fast-sample pin 16 during cranking, trend dip depth and shape over months.
- **Alternator diode check:** AC ripple on the 12 V line.
- Rollover inclinometer with alarm angle.
- Pothole / trail roughness map: IMU spikes + GPS in the black box.
- Parked watchdog: IMU motion interrupt wakes the adapter on a bump or tow.

**Cheap hardware (top pick: temp probes + digital inputs):**
- Temperature probe inputs (1-Wire / thermocouple) for transmission, diffs, transfer case (TJ OBD doesn't report these).
- Digital inputs: 4WD engaged, locker on, aux lights.
- Winch current (Hall clamp) and aux / dual-battery voltage.
- CO₂ (SCD41, drowsiness nudge), PM2.5 trail dust.
- NFC: tap to pair / pick driver profile.

**Off-grid (top pick: LoRa mesh):**
- LoRa mesh (SX1262 running Meshtastic, separate from our ESP32): off-grid convoy messaging and position sharing.
- LTE-M tracker (nRF9151 class): "where's my Jeep" + theft alerts; needs a data plan.

**Pure wacky:** AS3935 lightning detector, ADS-B plane spotting (1090 MHz SDR, bulky), Geiger counter, e-ink status display on the pod, seismograph mode while parked.

None of the extras touch the core adapter; they hang off the expansion port and native-mode channels.

## Prior art (searched 2026-10-22)

- [OpenDiag](https://www.tindie.com/products/opendiag/opendiag-open-source-obd2-interface/): open hardware, K-line / PWM / **VPW**, ELM327 command set, files on GitHub. Best VPW reference. MCU unconfirmed.
- [openobd2](https://github.com/FFY00/openobd2): SAME51 + TLIN1027, CAN done, VPW/PWM/K-line planned.
- [Alex Sidorenko's adapter](https://hackaday.com/tag/odb-2): LPC1517, ELM327 command set, all OBD-II protocols. Model for the command layer.
- [OM127 reboot](https://hackaday.io/project/158560-om127-reboot/): STM32F103 + TJA1050 + transistor/op-amp K-line; VPW/PWM unfinished.
- [5ghub.us OBD kit](https://5ghub.us/wp-content/uploads/2026/08/OBD_Scanner_UserGuide.pdf): commercial STM32 kit, VPW/K-line/CAN wiring docs.
- From memory: comma.ai panda (STM32F4 / H7), CANable 2.0 (STM32G431, its firmware forks are a CAN FD starting point), Macchina M2 (SAM3X), OBDLink (STN chips).

## Steps

1. **Bench CAN, compatibility mode over USB.** Nucleo-G474RE + CAN transceiver breakout; implement the ELM327 subset above; point OCTAVE at it as a plain serial port. **Effort:** M. **Needs:** desktop, ECU simulator (e.g. ECUsim 2000) or any CAN car.
2. **J1850 VPW front end on perfboard**, tested in the Jeep from a laptop over USB. A junkyard JTEC PCM powered on the bench saves trips to the car. **Effort:** L (riskiest step). **Needs:** Jeep (or bench PCM), scope ideally. **Fallback:** if this stalls, put an STN2120 on the board as the legacy-bus engine and keep the STM32 for the scheduler, buffering and native mode.
3. **ESP32 bridge:** SPP + BLE `FFE0`/`FFE1`, then pair through `OBDAdapterFinder`. **Effort:** M. **Needs:** Pi, S22.
4. **Native mode** in firmware + a new OCTAVE protocol class beside `elm327protocol`, with a QtTest in `tests/cpp/` (and a `carsim` / `simelm327`-style simulated native adapter for `--dev`). Measure PIDs/s against the NEXAS. **Effort:** L. **Needs:** desktop, Jeep.
5. **Store-and-forward backfill** into `TripRecorder` / `TripStore`. **Effort:** M. **Needs:** desktop, Jeep.
6. **Rev A board:** KiCad, enclosure, expansion port, sleep-current measurement, transient testing. **Effort:** L. **Needs:** bench supply, multimeter / current meter.
7. **Sensor pod board:** GNSS (DR with OBD speed) + IMU + baro first, then extras from the brainstorm. OCTAVE side: sensor channels into the managers (likely alongside `berryimumanager`) and the black box. **Effort:** L–XL. **Needs:** Jeep.

## Cross-references

- `obd-adapter-discovery.md`: steps 3 and the BLE-only future depend on its scan/pair work on each platform (BLE-on-desktop not started).
- `trip-history.md`: step 5 and the sensor channels feed the black box and its health trends (battery/alternator health fits there).
- `dashboards-roadmap.md` / `docs/GAUGE_AUTHORING.md`: new parameters (pod sensors, temp probes) need PID metadata in `OBDParameterModel.qml` and the gauge doc.

## Recommended order

1 → 2 (go/no-go on own VPW vs STN2120) → 3 → 4 → 5 → 6 → 7. Steps 1–5 need no custom PCB.

Delete this file (and its README row) when done.
