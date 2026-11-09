# OCTAVE OBD adapter, Rev 0 (bring-up board)

Our own OBD-II adapter: STM32G474 (vehicle side) + ESP32 (Bluetooth Classic SPP, BLE, Wi-Fi), every OBD-II bus, USB-C. Rev 0 is a bring-up board for the bench and the Jeep, not the final dongle. It has room to probe, test points and header access to spare pins, and it plugs straight into the car's OBD-II port. Plan and background: `TODO/diy-obd-adapter.md`.

**Status:** generated, then verified: an STM32 pin check against ST's pin data, two independent datasheet audits and ngspice simulations. Every finding is fixed or accepted, see `verify/REVIEW.md`. Not yet reviewed by a human, not ordered. Read "Before ordering" below.

## Files

| Path | What |
|---|---|
| `gen/obd_adapter.py` | **The design.** Every part, its LCSC number and its pin-to-net map. Edit this, not the KiCad files. |
| `gen/parts.py` | LCSC part numbers, checked against JLCPCB stock on 2026-11-02. |
| `gen/layout.py` | Board placement: fixed positions for the parts whose position matters, block regions for the rest. |
| `gen/libdefs.py` | Project symbols KiCad doesn't ship (SOIC-8 N-MOSFET, TPS22918), with the pinouts checked against the datasheets. |
| `gen/build.py` | Regenerates everything (see below). |
| `obd-adapter.kicad_sch` / `.kicad_pcb` | Generated KiCad 10 project. Open it in KiCad to review. |
| `fab/` | Generated: Gerber zip, JLCPCB BOM + CPL, hand-fit list. |
| `verify/` | Pre-fab verification: `REVIEW.md` (every finding and its fix), the STM32 pin check, the ngspice sims and plots. |

```bash
cd hardware/obd-adapter/gen
python3 build.py sch      # project symbols, schematic, ERC (kicad-cli)
python3 build.py pcb      # board with placement (pcbnew Python API)
python3 build.py route    # Freerouting autoroute, GND pours, DRC with schematic parity
python3 build.py fab      # Gerbers, drill, BOM, CPL
python3 build.py all
```

Use the system `python3` (KiCad's `pcbnew` module lives there), not the repo venv. Tools: KiCad 10, Java, and the Freerouting 2.5 jar (`~/.local/share/freerouting/freerouting-2.5.0.jar`, or set `FREEROUTING_JAR`). 2.2 loops forever on this board.

`build.py route` replays the frozen routing (`gen/routing.json`) and refuses if parts moved. To route again:

- `OBD_REROUTE=patch python3 build.py route`: keeps the frozen routing, drops only the copper over parts that moved (or in a DRC clash) and routes the gaps. Minutes, for small placement changes.
- `OBD_REROUTE=1 python3 build.py route`: a full route from scratch (20–40 min).

Before a full route, check the placement in seconds: `python3 precheck.py` (DRC with the hand-laid power copper in place) and `python3 congestion.py` (a routing-density estimate; keep `peak` ≤ 0.55 and `p95` ≤ 0.33, the levels this board has routed clean at). `OBD_W` / `OBD_H` try another board size, `OBD_TWO_SIDED=1` puts the small parts on the bottom.

The schematic and the board are generated from the same netlist, and `kicad-cli pcb drc --schematic-parity` checks that they agree. Every pin on the schematic is joined by net label, so a net is everything that carries the same name. The sheet (A0) spaces each part by what it really takes up, labels and text included, and `build.py sch` stops if any two pieces of text touch. **Do not hand-edit the generated files**: the next build overwrites them. The exceptions are copper and labels: after routing by hand in KiCad, run `build.py freeze` so the next build replays your tracks, and copy any reference-label moves into `LABELS` in `gen/layout.py`.

## Blocks

**MCU: STM32G474RET6.** A 16 MHz crystal (for FDCAN timing), SWD on a 2x5 1.27 mm header, RESET / USER buttons, R/G/B LEDs and a 4-way `CONFIG` DIP switch (SW6): 1 ESP_ON, 2 / 3 the CAN1 / CAN2 120 Ω terminations, 4 BOOT0. All off in the car.

**Power:** J1962 pin 16 → 1.1 A 60 V PTC → 5.0SMDJ26CA → LM74700 ideal diode + AO4264E → `VBAT_P` (with 47 µF bulk).
- `VBAT_P` feeds a TPS54360B buck (60 V input; the datasheet's 600 kHz design with the output set to 5.32 V), then an SS34 diode-OR with USB VBUS (`5V` ≈ 4.9 V), then an AP2114H 3.3 V LDO (SOT-223).
- The buck's EN divider is not fitted, so the internal UVLO keeps it running through a 6 V cold crank.
- The ESP32 has its own TPS22918 load switch, placed at the module's 3V3 pin. Its ON pin is a diode OR of `ESP_PWR_EN` (PB6, off at reset) and `VBUS2` (the ESP32's USB port), so plugging into J7 always powers the ESP32. CONFIG switch 1 (`ESP_ON`) also powers it without STM32 firmware; turn it off again for normal use.
- The TVS is bidirectional on purpose. A unidirectional one would conduct into a reversed battery and defeat the ideal diode.
- Everything on `VBAT_P` must survive the TVS's ~42 V clamp.

**DLC_SW:** a switched battery rail (MMBT5401 high side, `DLC_SW_EN` = PC3). It feeds the J1850 supply, the LM2901 (through 470 Ω and a 33 V zener) and the K/L 510 Ω pull-ups (through 1N4148W, so the car's own pull-up cannot back-feed the rail while it is off). ScanTool's STN datasheets warn that pull-ups left on the battery draw sleep current and can disturb the bus, up to stalling an engine.

**J1850 VPW / PWM:** STN-style discrete circuit, adapted from the STN1110/1170/21xx reference schematics (3.3 V parts like ours).
- **Supply:** LM317L with 240 / 787 / 453 Ω gives 7.71 V for VPW. A 2N7002 on `J1850_VSEL` (PB2) shorts the 453 Ω for 5.35 V (PWM).
- **Bus+:** 2N7002 → AO3401A → Schottky, with a 10k + Schottky bus load.
- **Bus−:** MMBT3904 sink with a 10k + Schottky pull-up to 5 V.
- **Receivers:** LM2901 on `CMP_VCC`, with 1N4148W input clamps. VPW compares Bus+ against 3.94 V. PWM compares Bus+ against Bus−. Both are active high, with 10k pull-ups to 3.3 V.
- **Timer:** everything is on TIM3 (CH1 VPW RX, CH2 PWM RX, CH3 / CH4 TX).
- **No wave shaping.** Neither ELM, STN nor OpenDiag shapes the edges. SAE J1850 asks for about 0.24–0.37 V/µs; that is a Rev A experiment (an RC on the P-FET gate).

**ISO 9141 / KWP2000:** discrete STN circuit; no L9637D. The L9637D's VS rating is under our TVS clamp, its TX input wants 5 V logic, and no reference design uses it.
- **Lines:** 510 Ω 2 W pull-ups on `DLC_SW` (through 1N4148W), MMBT3904 sinks with 470 Ω base resistors.
- **K-line receiver:** LM2901 against DLC_SW / 2.
- **Polarity:** TX high pulls the line low, so LPUART1 needs **TXINV**. RX is not inverted.

**CAN:** two TCAN1044V (CAN FD, VIO = 3.3 V).
- HS-CAN on J1962 6/14 uses FDCAN2 (PB12 / PB13). MS-CAN on 3/11 uses FDCAN3 (PB3 / PB4).
- STB is pulled high (standby) until firmware pulls it low.
- SZNUP2105L ESD. 120 Ω termination only through CONFIG switches 2 / 3, for bench buses.

**Radio:** ESP32-WROOM-32E-N8.
- UART2 (IO16 RX / IO17 TX, IO18 CTS / IO19 RTS) to USART2 with flow control.
- UART0 goes to its own USB-C port (J7) through a CH340C, with the usual two-transistor auto-reset on DTR / RTS, so `esptool` and the Arduino IDE flash it with no buttons and no STM32 firmware. J7 is also the ESP32's console.
- `ESP RST` (SW4, EN) and `ESP BOOT` (SW5, IO0) buttons, for when a host's DTR / RTS timing defeats the auto-reset: hold BOOT, tap RST.
- The STM32 can also reset the ESP32 (PB7) and strap IO0 (PB5). (The old `J_ESP_PROG` header, J3, is gone: J7 does its job.)
- **ESP IO header J8** (1x10, not fitted): IO14 27 26 25 33 32 35 34 + 3V3_ESP / GND, above the module. 3.3 V logic, IO34 / 35 input-only. Powered from 3V3_ESP, so anything on it turns off with the ESP32.
- **microSD J10** (Molex 104031-0811, on the bottom edge, card sticks out about 3 mm so it can be pulled) in SPI mode: MISO IO4, CLK IO5, MOSI IO21, CS IO22, card detect IO23 (switch to GND, internal pull-up). 10k pull-ups (RN1 + R68) on CS / MOSI / MISO / DAT1 / DAT2. Powered from 3V3_ESP. It is the adapter's copy of the phone's traffic stop recordings (`TODO/traffic-stop-mode.md`). IO5 is a boot strap that only needs to be high at reset (its internal pull-up; the card doesn't drive CLK).
- The module sits centred on the board's right end with its antenna section hanging off the edge (Espressif's preferred placement), so there is no copper keepout on the board.

**USB-C (J2, STM32):** device (5.1k CC), USBLC6 ESD, PA11 / PA12 (DFU bootloader + CDC). On USB power alone `5V` is about 4.6 V, marginal for the CAN transceivers (4.5 V minimum).

**USB-C (J7, ESP32):** device (5.1k CC), USBLC6 ESD, CH340C on ESP32 UART0. Its VBUS also feeds `5V` through its own SS34, so either port powers the board.

**Flashing a new board over USB only:** STM32 through J2: hold BOOT, tap RESET, flash with STM32CubeProgrammer or `dfu-util` (a blank chip starts the bootloader by itself). ESP32 through J7: `esptool --port /dev/ttyUSB0 ...`. The two are independent; either can go first.

**Storage, expansion:** 16 MB W25Q128 on QUADSPI1 (store-and-forward); Qwiic I2C (PA8 / PA9); J5, a 2x7 header (not fitted) on the bottom edge with 3V3, 5V, USART1, I2C, PANIC_BTN and the spare GPIO (PA5, PA10, PC10, PD2).

**Parked watch and evidence** (for `TODO/traffic-stop-mode.md` and `TODO/octave-companion-app.md`):
- **U15 LIS2DH12** accelerometer on the STM32 I2C bus (0x19, SA0 high). INT1 → PC14, INT2 → PC15 (EXTI, wakes the STM32 from Stop 1). Wake-on-motion while parked (tow / break-in, someone getting in), crash spikes while driving. A plug in the OBD port is a poor mount for measurement, fine for "something moved".
- **U16 ATECC608B** secure element on the same bus (0x60): a key that never leaves the chip, to sign the frozen OBD speed window. Needs a one-time, irreversible configuration lock in firmware.
- **J9 panic button** (JST SH 2-pin, bottom edge) for a hidden button on a cable: ESD9B3.3 at the connector, 1k series, 10k pull-up, 100n → `PANIC_BTN` = PC13 (WKUP2: wakes the STM32 even from Standby). Closed = low.

**Car connector:** an SAE J1962 male plug sticking straight out of the board's left end, with the board in the middle of the plug (like an ELM327 dongle). The plug sits on a small adapter board that stands across the board's edge (board 100 x 70 mm plus a 1.6 mm tab):

```
      side view                          top view
  ┌──────────┐┃                       ┌──────┐┏━━━━━━━━━━━━━━━┓
  │   plug   │┃════════ main board    │ plug │┃  main board   ┃
  └──────────┘┃                       └──────┘┗━━━━━━━━━━━━━━━┛
           adapter
```

- **Part:** Shenzhen Jiatel OBD-II-16M-12V, part no. CR-PG001 ("OBD-II 16 Pin Straight Male Connector", jiatelcn.com; drawing `OBDII-Male-16pin.pdf` rev A). Generic "OBD2 16-pin male straight" plugs elsewhere look the same, but only this one has a drawing, so buy it or check another against its numbers. Not at LCSC, so it's hand-fit.
- **Adapter board** (`obd-adapter-plug.kicad_pcb`, generated by `build.py plug`; gerbers `fab/obd-adapter-plug-rev0-gerbers.zip`, order it with the main board): 40 x 18 x 1.6 mm, 2 layers. The plug goes on its front (footprint `lib/OCTAVE.pretty/J1962_Plug_Adapter`): 2 x 8 contacts, 4.00 mm pitch, rows 8.60 mm apart; the contacts are 0.8 x 1.8 mm blades whose tails stand 4.0 mm out of the back, so 2.2 x 1.2 mm plated slots. Between the rows is a 36 x 1.8 mm slot for the main board's tab, and on the back one joint pad per pin runs from the slot's edge over the pin.
- **Main board end** (J6, footprint `J1962_Edge_Joint`): a 33.8 mm tab, 1.6 mm deep, on the left edge, and 16 pads 3.0 x 2.5 mm along the edge, pins 9–16 on the top side and 1–8 on the bottom, centred on the edge so the plug sits square on the board's end. Pin 16 (battery) is the bottom pad of the top row; a 1.6 mm trace runs from it up a lane beside the pads into F1.
- **Assembly:** solder the plug to the adapter. Push the main board's tab through the slot until the adapter's back sits on the board's edge, square it up, and solder each edge pad to its joint pad with a fillet in the corner (top side, then bottom). The 16 fillets and the tab carry the plug's insertion force; a bead of epoxy along the joint on both sides makes it stronger. Pins 1, 8, 9, 12 and 13 aren't used but are soldered too, for strength.
- **In the car:** the board sticks out about 100 mm straight from the port, in line with the plug. With the 9–16 row (and the plug's latch) up, the board's top side faces the same way. If the TJ's port faces down, the board hangs straight down from it, so check the room around your knee and tie it up if it gets bumped.
- **3D models:** `gen/mkplug3d.py` (FreeCAD; `QT_QPA_PLATFORM=offscreen freecad gen/mkplug3d.py`) makes `lib/3dmodels/J1962_Male_Straight_Jiatel_CR-PG001.step` (the plug) and `J1962_Edge_Joint_Assembly.step` (plug + adapter, used by J6) from the Jiatel drawing: 37.6 x 17.2 flange, 40.26 x 17.8 trapezoid shroud, 20 mm overall, pins 8.5 mm out of the insert (4 and 5: 10 mm, they mate first), centre key slot, latch on the 9–16 side. The narrow side of the trapezoid, the shroud wall and the latch are read off the drawing, not dimensioned.
- **Pin order:** seen from the plug's mating face the plug is the car's socket mirrored (pin 1 top right, 1–8 on the wide side of the trapezoid). On the board that face is turned over: from the left, top side up, 9..16 run left to right above the board and 1..8 below it.
- **On the bench:** an OBD-II female breakout or extension cable into the plug.

| J1962 | Signal | | J1962 | Signal |
|---|---|---|---|---|
| 2 | J1850 Bus+ | | 10 | J1850 Bus− |
| 3 | MS-CAN-H | | 11 | MS-CAN-L |
| 4 | Chassis ground → GND | | 5 | Signal ground → GND |
| 6 | CAN-H | | 14 | CAN-L |
| 7 | K-line | | 15 | L-line |
| 16 | Battery + | | 1, 8, 9, 12, 13 | not connected (soldered for strength) |

Pins 4 and 5 both go to GND. The ELM327 datasheet notes that a few vehicles have no pin 5 and need pin 4 instead.

## MCU pin plan

| Function | Pins |
|---|---|
| HSE 16 MHz | PF0 / PF1 |
| USB FS | PA11 / PA12 |
| SWD | PA13 / PA14, NRST |
| ESP32 link (USART2 + RTS/CTS) | PA2 TX, PA3 RX, PA0 CTS, PA1 RTS |
| ESP32 control | PB5 IO0, PB6 power, PB7 EN |
| QSPI flash | PB11 NCS, PB10 CLK, PB1 IO0, PB0 IO1, PA7 IO2, PA6 IO3 |
| HS-CAN (FDCAN2) | PB12 RX, PB13 TX, PB14 STB |
| MS-CAN (FDCAN3) | PB3 RX, PB4 TX, PB15 STB |
| J1850 (TIM3) | PC6 VPW RX, PC7 PWM RX, PC8 Bus+ TX, PC9 Bus− TX, PB2 V select |
| K / L line | PC0 RX, PC1 TX (LPUART1), PC2 L TX |
| DLC_SW enable | PC3 (not PC10: the bootloader may drive USART3 TX) |
| Battery voltage | PA4 (ADC2_IN17): 100k / 10k 0.1 %, 36 V full scale |
| I2C (Qwiic, U15 0x19, U16 0x60) | PA8 SDA, PA9 SCL |
| Expansion UART (USART1) | PC4 TX, PC5 RX |
| Spare to header (J5) | PA5, PA10, PC10, PD2 |
| Panic button (J9) | PC13 (WKUP2), also on J5 pin 9 |
| Accelerometer interrupts | PC14 INT1, PC15 INT2 |
| LEDs / button | PB9 R, PC11 G, PC12 B, PA15 USER |
| BOOT0 | PB8 (10k pull-down + CONFIG switch 4) |

**Why not FDCAN1:** its pins clash with USB (PA11 / PA12) and with BOOT0 (PB8). A CAN transceiver's idle-high RXD on PB8 would start the chip in the bootloader.

**Dead-battery pull-downs:** PB4 and PB6 carry UCPD dead-battery pull-downs until firmware sets `PWR_CR3.UCPD_DBDIS`. Both are safe low at reset: CAN2 TXD with its transceiver in standby, and the ESP32 power switch off.

## Before ordering

1. **Review the schematic in KiCad.** ERC is clean, the board matches it, and `verify/REVIEW.md` lists what the audits and sims checked, but no human has reviewed it yet. Most important:
   - The J1850 and K-line front ends.
   - The ideal-diode and buck values.
   - The plug's pin numbering (`lib/OCTAVE.pretty/J1962_Plug_Adapter` and `J1962_Edge_Joint`: pin 1 top right on the plug's mating face, 9–16 on the main board's top side).
2. **Check the footprints marked in `gen/obd_adapter.py`:**
   - The Sunlord SWPA8065S inductor uses the Changjiang FNR8065S land. The audit found it matches KiCad's own Sunlord 8x8 land; its saturation current is 4.2 A (`verify/REVIEW.md` #24).
   - **The plug against a real one.** Buy the plug first and lay it on a 1:1 print of the adapter board. Check the 0.8 × 1.8 mm blade tails go into the 2.2 × 1.2 mm slots on the 4.00 × 8.60 mm grid, that pin 1 lands on the rounded-square pad, and that the plug's flange covers the slot without its rear pocket fouling the main board's tab.
3. **Check part rotations in JLCPCB's placement preview.** KiCad and JLC disagree on the zero rotation of some packages (SOT-23, SOIC, the USB-C).
4. **Recheck stock on order day.** The STM32G474RET6 had 20 in stock, then 186, on 2026-11-02. Fallback: STM32G474RBT6 (128 KB flash), C529413. Low stock elsewhere: AP2114H (2.9k), 5.0SMDJ26CA (4k).
5. **Confirm the J1850 PWM high-level limit** (5.25 V assumed, `verify/REVIEW.md` #10) against SAE J1850 if you have it.
6. Through-hole parts (the OBD-II plug, pin headers) need JLC's through-hole option or hand soldering; see `fab/*-hand-fit.txt`. Order the adapter board (`fab/obd-adapter-plug-rev0-gerbers.zip`, 2 layers, 1.6 mm) in the same cart; the main board must be 1.6 mm too, or the tab won't fit its slot.
7. The board is autorouted. Before fab, look at the buck switch node and the USB pairs in KiCad.
8. DRC (2026-11-08, after the layout rework and a fresh route): 0 copper errors, 0 unconnected, 0 schematic-parity issues, 0 courtyard overlaps; 138 silkscreen warnings (labels overlapping each other or clipped by pads / the edge, which JLC clips anyway), 2 dead-end track stubs (5V_BUCK, VBAT_SENSE; DC, harmless) and the U4 library note. Tidy the labels you care about in KiCad and copy their positions into `LABELS` in `gen/layout.py`, or the next `build.py pcb` puts them back.

**How the board is routed** (`build.py route`):

- **Frozen.** The routing is frozen in `gen/routing.json`, and `route` replays it, so every rebuild gives the same board. It refuses to run if a part has moved since the freeze. `OBD_REROUTE=1` routes afresh, and `build.py freeze` captures a routed (or hand-edited) board.
- **Routing afresh, in order:**
  1. The power stage is wired by hand (`gen/prewire.py`).
  2. Every SMD GND pad gets a via to the In1 plane, and every 3V3 pad a via to the solid In2 plane. The MCU's VSS / VSSA pins join one via under the chip. All of that is locked.
  3. Freerouting 2.5 routes the rest on F.Cu / B.Cu. Supply nets get wider classes (`WIDTHS`), and battery-side nets keep 0.30 mm from everything (`HV`).
  4. A grid router (`gen/maze.py`) finishes the few connections Freerouting leaves.
  5. GND stitching vias and the outer-layer GND pours go in last.
- **Battery-side clearance.** `obd-adapter.kicad_dru` (generated) makes KiCad's DRC and interactive router enforce the battery-side clearance too.
- **Freerouting is not deterministic.** A fresh route gives a different, equally valid layout, which may need a few connections finished by hand before `build.py freeze`.

## Parked current

The board stays on with the engine off (J1962 pin 16 is unswitched battery): the STM32 sleeps and watches the accelerometer, the panic button and the buses. Estimate from datasheet typical values at 12.6 V, 25 °C, with the ESP32 and `DLC_SW` off, both CAN transceivers in standby and the STM32 in Stop 1. The buck's Eco-mode switching is assumed 75 % efficient. **Measure it on the first board.**

| Load | Rail | Current |
|---|---|---|
| LM74700 ideal-diode controller | battery | 80 µA |
| Battery-voltage divider (100k / 10k) | battery | 115 µA |
| TPS54360B, not switching | battery | 146 µA |
| AP2114H LDO ground current | 5 V | 65 µA |
| Buck feedback divider (57.6k / 10.2k) | 5 V | 78 µA |
| VPW threshold divider (13k / 47k) | 5 V | 88 µA |
| STM32G474 Stop 1 (ST's figure, RTC off) | 3.3 V | ~80 µA |
| Two TCAN1044V in standby (VIO) | 3.3 V | 17 µA |
| W25Q128 standby, LIS2DH12 at 10 Hz, ATECC608B asleep, TPS22918 off | 3.3 V | ~14 µA |
| **Total from the battery** | | **≈ 0.53 mA (0.38 Ah / 30 days)** |

- The VPW threshold divider was 2.7k / 10k (420 µA, ~0.72 mA in total); 13k / 47k keeps the 4.17 V threshold within 0.5 %.
- STM32 Standby instead of Stop 1, and the flash in power-down, would only save about 0.05 mA. Stop 1 keeps RAM and wakes on any EXTI pin, so the accelerometer can stay on PC14.
- With the ESP32 on: about 2 mA from the battery while it advertises over BLE (~3 mA average at 3.3 V; the LDO is linear, so 3.3 V current costs about half that from 12.6 V), about 70 mA while Wi-Fi receives the phone's recordings.
- For scale: cheap ELM327 dongles draw 10–60 mA all the time; a car's own parked drain is typically 20–50 mA.
- Further cuts if ever needed: a 1M / 100k battery divider with a 100 nF cap at the ADC pin (−0.1 mA), a lower-Iq LDO, or an STM32-controlled shutdown of the buck (EN) with a separate always-on micro-power rail.

## Known Rev 0 limits

- No J1850 wave shaping (see above).
- Parked current is about 0.53 mA from the battery (see "Parked current"). The buck, the ideal-diode controller, the battery-voltage divider and the LDO set that floor, not the MCUs.
- The J1850 Bus− pull-up (10k from `5V_BUCK` through a B5819W) stays on while parked, so pin 10 sees 5 V / 10k on every car. PWM nodes pull Bus− up anyway and VPW / CAN cars leave pin 10 open, but Rev A should switch it with `DLC_SW` like the K / L pull-ups.
- No GM single-wire CAN, no DoIP.

## Sources

- ELM327 datasheet rev J, Fig. 9 (J1850 / ISO front ends).
- ScanTool STN1110 / STN1170 / STN21xx datasheets and their reference schematics (the circuits used here).
- OpenDiag rev 1.1 KiCad sources (`maximus64/opendiag-hardware`).
- Motorola AN1731 and the Philips AU5780 / AU5783 datasheets (J1850 VPW electrical limits).
- TI datasheets: TPS54360B, LM317L, TPS22918, TCAN1044V.
