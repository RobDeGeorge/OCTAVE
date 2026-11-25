# OCTAVE LoRa pod, Rev 0

The dash-top half of the LoRa car finder (`TODO/lora-car-finder.md`, step 4): the parked car's position, beaconed over LoRa, with Bluetooth for a phone. It is the first pod on the OCTAVE pod bus (`../pod-bus/README.md`): battery voltage and RS-485 on four wires, in on one connector and out on the other. Rev 0 is a bring-up board: room to probe, unfitted headers for the things still undecided.

**Status (2026-11-23):** schematic generated, ERC clean (0 errors, 0 warnings). Board placed and **routed** (again the same day, after the cable changed from a plain UART to the pod bus): 0 unconnected pads, schematic and board agree, 2 DRC warnings, both cosmetic (the USB-C socket's outline runs off the board edge). The route is the autorouter's and has passed the rule check, not a person's eye. No fab files, no firmware. Not reviewed by a human. Nothing has been measured.

## What is on it

95 x 46 mm, 4 layers, every part on top (54 parts).

| Block | Part | Notes |
|---|---|---|
| MCU + radios | RAK4630-9-SM-I (nRF52840 + SX1262) | LoRa 902-928 MHz and Bluetooth LE. Both antennas plug into IPEX sockets on the module: no RF trace on this board. Europe: RAK4630-8-SM-I, same footprint. |
| GPS | Quectel L80-R (order code L80RE-M37) | GPS only, 15 mm patch antenna on top. Its supply is switched (TPS22918); V_BCKP is held by a 0.1 F supercap (hand-fit). |
| IMU | ASM330LHH | I2C 0x6A. "Moved while parked". |
| Supply | 0.5 A / 30 V polyfuse, SMBJ24A, B5819W, TPS7B8233 | 5 to 16 V in. See "Pod bus and supply". |
| Pod bus | two JST GH (J1, J7), THVD2450 RS-485 transceiver, 120 Ω behind the `END` switch (SW3) | |
| USB-C | the module's own USB | Firmware update, console, 5 V on the bench. |
| Bring-up | SWD, Qwiic, RESET, PAIR, two LEDs, `BENCH` and `RANGE` headers (not fitted) | |

## Pod bus and supply

J1 and J7 (JST GH) carry the same four nets, pin for pin: 1 `VIN`, 2 RS-485 `A`, 3 RS-485 `B`, 4 GND. Either is the way in; the other goes on to the next pod.

- **On the chain:** `VIN` is the car's battery, switched and fused by the OBD adapter. The chain's current passes from J1 to J7 on the board, ahead of this pod's own fuse.
- **Standalone:** `VIN` is the battery (OBD pin 16, always on) through a plain power lead; pins 2 / 3 open.
- **Last pod of the chain:** both poles of SW3 (`END`) on. That puts 120 Ω across A / B.

F1 and D1 take a reversed lead, a 24 V jump start and a load dump (D1 clamps at 38.9 V, under U1's 40 V). `VBAT_SENSE` (P0.31) reads `VIN` x 0.180: the battery's voltage, and whether the alternator is charging, so the pod knows "parked" without the adapter. The divider draws 10 µA at 12.6 V.

U7 (THVD2450) survives ±70 V on A / B, so a cable pinched against `VIN` does not kill it, and it has its own ESD protection and idle-bus failsafe: there is no TVS and no bias resistor on the bus. Its driver and receiver enables are separate pins. With both idle (the state at reset: 100k each) it is shut down, under 1 µA; listening costs 1.8 mA, so the firmware must shut it down while parked.

At 14 V the regulator burns about 0.4 W with the GPS on (an estimate: 40 mA). It is the first thing to measure on a hot dash; a small switching regulator is the fix.

## Pin plan (nRF52840)

| Signal | Pin | | Signal | Pin |
|---|---|---|---|---|
| I2C SDA / SCL | P0.13 / P0.14 | | PAIR button | P1.01 |
| GPS UART (from / to GPS) | P0.15 / P0.16 | | GNSS_EN | P1.02 |
| Pod bus UART RX / TX | P0.19 / P0.20 | | LED green / red | P1.03 / P1.04 |
| SPI SCK / MOSI / MISO / NSS (`RANGE`) | P0.17 / P0.21 / P0.24 / P0.25 | | GPS 1PPS | P0.03 |
| RANGE_IRQ | P0.29 | | IMU INT1 / INT2 | P0.02 / P0.28 |
| RS-485 driver enable / receiver enable (low) | P0.30 / P0.26 | | VBAT_SENSE | P0.31 (AIN7) |
| Spare (headers) | P0.04, P0.05 | | | |

Fast signals are on the pins Nordic allows above 10 kHz; the slow ones are on the low-frequency pins near the radio.

## Files

| File | |
|---|---|
| `gen/lora_pod.py` | **The design:** every part, its LCSC number, its pin-to-net map. Edit this, not the KiCad files. |
| `gen/pod_parts.py` | LCSC numbers, checked against JLCPCB on 2026-11-23. |
| `gen/pod_layout.py` | Placement: every part's position. |
| `gen/pod_libdefs.py` | Project symbols (RAK4630, TPS7B82, ASM330LHH, the RS-485 transceiver) and the RAK4630 footprint. |
| `gen/build.py` | `python3 build.py sch`, then `pcb`, `route`, `view` (system `python3`, KiCad 10, Freerouting 2.5 as for the adapter). Uses the OBD adapter's generator modules (`../obd-adapter/gen`). |
| `gen/routing.json` | The routing, frozen: `route` replays it and refuses if a part moved. `POD_REROUTE=1` routes afresh (about 3 minutes), then run `pcb` and `route` once more: the first pass takes the autorouter's stub vias out of the file. The autorouter does not give the same result twice; one run in three so far left a pad of the IMU open and had to be repeated. |
| `gen/review.py` | The annotated top view in `view/`. |
| `lib/3dmodels/` | LCSC models of the two modules, fetched with easyeda2kicad. |

## Before ordering

- Look at the copper in KiCad: DRC does not judge quality. GND is In1, 3V3 is In2, signals on the outer layers; supply nets are 0.4 to 0.5 mm.
- Fab outputs: `build.py` has no `fab` step yet (the adapter's is the model), and the JLCPCB rotation corrections for the new parts are not worked out.
- **RAK4630-9-SM-I had 44 in stock** on 2026-11-23. Recheck first.
- The RAK4630 footprint's land positions come from the LCSC footprint and its numbering from the datasheet's figure 1: check it against a module or the datasheet's figure 5 before ordering.
- Thermal: the modules and the GPS are rated to 85 °C; a dash top in the sun passes 80 °C. Find a supercap rated for it.
- Firmware obligations so far: drive `GNSS_RXD_M` low while `GNSS_EN` is low; `GNSS_EN` is off at reset (100k); shut the RS-485 transceiver down while parked.
- The other end of the pod bus is J11 on the OBD adapter's bottom edge (`../obd-adapter/README.md`, "Pod bus"). That board was reworked into a compact two-sided one on 2026-11-24: see its README for where it stands.
- R3, R4, D3 and D4 do not exist (they were the plain-UART cable's parts): the numbering has those gaps on purpose.
- US 915 MHz rules (hopping or a 500 kHz channel) and whether the module's FCC grant covers the antenna used: not verified.
