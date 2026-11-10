# OCTAVE OBD adapter, Rev 0 (bring-up board)

Our own OBD-II adapter: STM32G474 (vehicle side) + ESP32 (Bluetooth Classic SPP, BLE, Wi-Fi), every OBD-II bus, USB-C. Rev 0 is a bring-up board for the bench and the Jeep, not the final dongle. It has room to probe, test points and header access to spare pins, and it plugs straight into the car's OBD-II port. Plan and background: `TODO/diy-obd-adapter.md`.

**Status:** generated, then verified: an STM32 pin check against ST's pin data, two independent datasheet audits and ngspice simulations. Every finding is fixed or accepted, see `verify/REVIEW.md`. Not yet reviewed by a human, not ordered. Read "Before ordering" below.

**The main board is routed** (2026-11-10): 0 DRC errors, 0 unconnected pads, schematic and board agree. `gen/routing.json`, `obd-adapter.kicad_pcb`, `drc.rpt` and everything in `fab/` are that board, with the changes of 2026-11-09 in it (the buck loop, the parked-watch parts, the microSD, the mounting holes, `verify/REVIEW.md` rounds 5 to 7). The route is the autorouter's, finished by hand round the STM32; it has passed the rule check, not a person's eye.

## Files

| Path | What |
|---|---|
| `gen/obd_adapter.py` | **The design.** Every part, its LCSC number and its pin-to-net map. Edit this, not the KiCad files. |
| `gen/parts.py` | LCSC part numbers, checked against JLCPCB's parts library on 2026-11-02 and again on 2026-11-09. |
| `gen/layout.py` | Board placement: fixed positions for the parts whose position matters, block regions for the rest. |
| `gen/libdefs.py` | Project symbols KiCad doesn't ship (SOIC-8 N-MOSFET, TPS22918, CH340C, LIS2DH12), with the pinouts checked against the datasheets, and the two plug footprints (drawn by `gen/mklib.py`). |
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
- `OBD_REROUTE=1 python3 build.py route`: a full route from scratch. About 4 hours for the board as it is now, and it will not finish by itself: see "How the board is routed" before throwing the frozen route away.

Before a full route, check the placement in seconds: `python3 precheck.py` (DRC with the hand-laid power copper in place) and `python3 congestion.py` (a routing-density estimate; keep `peak` ≤ 0.55 and `p95` ≤ 0.33, the levels this board has routed clean at). `OBD_W` / `OBD_H` try another board size, `OBD_TWO_SIDED=1` puts the small parts on the bottom.

The schematic and the board are generated from the same netlist, and `kicad-cli pcb drc --schematic-parity` checks that they agree. Every pin on the schematic is joined by net label, so a net is everything that carries the same name. The sheet (A0) spaces each part by what it really takes up, labels and text included, and `build.py sch` stops if any two pieces of text touch. **Do not hand-edit the generated files**: the next build overwrites them. The exceptions are copper and labels: after routing by hand in KiCad, run `build.py freeze` so the next build replays your tracks, and copy any reference-label moves into `LABELS` in `gen/layout.py`.

## Blocks

**MCU: STM32G474RET6.** A 16 MHz crystal (for FDCAN timing), SWD on a 2x5 1.27 mm header, RESET / USER buttons, R/G/B LEDs and a 4-way `CONFIG` DIP switch (SW6): 1 ESP_ON, 2 / 3 the CAN1 / CAN2 120 Ω terminations, 4 BOOT0. All off in the car. VDDA / VREF+ are on 3V3 with their own 100 nF + 1 µF (no ferrite bead: the ADC only reads the battery divider). The USER button (PA15) and `KLINE_RX` (PC0) have no pull-up resistor; firmware uses the pins' own.

**Power:** J1962 pin 16 → 2 A 60 V PTC (2920L200/60) → 5.0SMDJ26CA → LM74700 ideal diode + AO4264E → `VBAT_P` (with 47 µF bulk).
- `VBAT_P` feeds a TPS54360B buck (60 V input; the datasheet's 600 kHz design with the output set to 5.32 V), then an SS34 diode-OR with USB VBUS (`5V` ≈ 4.9 V), then an AP2114H 3.3 V LDO (SOT-223).
- The buck's EN divider is not fitted, so the internal UVLO keeps it running through a 6 V cold crank.
- The ESP32 has its own TPS22918 load switch, placed at the module's 3V3 pin. Its ON pin is a diode OR of `ESP_PWR_EN` (PB6, off at reset) and `VBUS2` (the ESP32's USB port), so plugging into J7 always powers the ESP32. CONFIG switch 1 (`ESP_ON`) also powers it without STM32 firmware, by pulling the PB6 net to 3V3 through 1k (R70); turn it off again for normal use. Firmware that drives PB6 low with the switch left on wastes 3.3 mA and the ESP32 stays off.
- A 1k bleed (R69) holds `VBUS2` at ground while J7 is unplugged. Without it the reverse leakage of J7's SS34 (up to 0.4 mA in a hot car) raised the load switch's ON pin and the ESP32 switched itself on.
- The ideal diode (U6 LM74700 driving Q1, an AO4264E 60 V FET) blocks a reversed battery and holds `VBAT_P` up through a negative pulse, for about 20 mV where a diode costs 0.3 V. A B560C Schottky in its place was tried on 2026-11-09 (three parts and 80 µA of parked current less) and taken out again: with the battery reversed its leakage pulled `VBAT_P` to −0.7 V at 100 °C, past the buck's −0.3 V rating, and it took 0.3 V off the cranking headroom (`verify/REVIEW.md` #54, `verify/sim/sim_input_d29.py`). The battery-voltage divider is ahead of it, on `VBAT_F`.
- The TVS is bidirectional on purpose. A unidirectional one would conduct into a reversed battery and defeat the ideal diode.
- Everything on `VBAT_P` must survive the TVS's ~42 V clamp.

**DLC_SW:** a switched battery rail (SI2309CDS P-MOSFET high side, `DLC_SW_EN` = PC3). It feeds the J1850 supply, the LM2901 (through 470 Ω and a 33 V zener) and the K/L 510 Ω pull-ups (through 1N4148W, so the car's own pull-up cannot back-feed the rail while it is off). ScanTool's STN datasheets warn that pull-ups left on the battery draw sleep current and can disturb the bus, up to stalling an engine.

**J1850 VPW / PWM:** STN-style discrete circuit, adapted from the STN1110/1170/21xx reference schematics (3.3 V parts like ours).
- **Supply:** LM317L with 240 / 680 / 470 Ω gives 7.24 V for VPW. A 2N7002 on `J1850_VSEL` (PB2) shorts the 470 Ω for 4.79 V (PWM).
- **Bus+:** 2N7002 → AO3401A → Schottky, with a 10k + Schottky bus load.
- **Bus−:** MMBT3904 sink with a 10k + Schottky pull-up to `5V_BUCK`.
- **Receivers:** LM2901 on `CMP_VCC`, with 1N4148W input clamps. VPW compares Bus+ against 3.85 V (18k / 47k from `5V_BUCK`, the middle of the 3.5–4.25 V receive window). PWM compares Bus+ against Bus−. Both are active high, with 10k pull-ups to 3.3 V.
- **Timer:** everything is on TIM3 (CH1 VPW RX, CH2 PWM RX, CH3 / CH4 TX).
- **No wave shaping.** Neither ELM, STN nor OpenDiag shapes the edges. SAE J1850 asks for about 0.24–0.37 V/µs; that is a Rev A experiment (an RC on the P-FET gate).

**ISO 9141 / KWP2000:** discrete STN circuit; no L9637D. The L9637D's VS rating is under our TVS clamp, its TX input wants 5 V logic, and no reference design uses it.
- **Lines:** 510 Ω 2 W pull-ups on `DLC_SW` (through 1N4148W), MMBT3904 sinks with 470 Ω base resistors.
- **K-line receiver:** LM2901 against DLC_SW / 2.
- **Polarity:** TX high pulls the line low, so LPUART1 needs **TXINV**. RX is not inverted.

**CAN:** two TCAN1044V (CAN FD, VIO = 3.3 V, VCC from `5V_BUCK`).
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

**USB-C (J2, STM32):** device (5.1k CC), USBLC6 ESD, PA11 / PA12 (DFU bootloader + CDC). On USB power alone `5V` is about 4.6 V and `5V_BUCK` is off, so the CAN transceivers, the VPW threshold and the Bus− pull-up have no supply: **CAN and J1850 need 12 V on the plug**, also on the bench.

**USB-C (J7, ESP32):** device (5.1k CC), USBLC6 ESD, CH340C on ESP32 UART0. Its VBUS also feeds `5V` through its own SS34, so either port powers the board.

**Flashing a new board over USB only:** STM32 through J2: CONFIG switch 4 (BOOT0) on, tap RESET, flash with STM32CubeProgrammer or `dfu-util`, switch 4 off again (a blank chip starts the bootloader by itself). Keep the ESP32 off meanwhile (CONFIG switch 1 off, J7 unplugged) once it has firmware: the ROM bootloader also listens on USART2, whose RX pin (PA3) is the ESP32's TX, and takes the first interface that talks to it. ESP32 through J7: `esptool --port /dev/ttyUSB0 ...`. The two are independent; either can go first.

**Storage, expansion:** 16 MB W25Q128 on QUADSPI1 (store-and-forward); Qwiic I2C (PA8 / PA9); J5, a 2x7 header (not fitted) on the bottom edge with 3V3, 5V, USART1, I2C, PANIC_BTN and the spare GPIO (PA5, PA10, PC10, PD2).

**Parked watch and evidence** (for `TODO/traffic-stop-mode.md` and `TODO/octave-companion-app.md`):
- **U15 LIS2DH12** accelerometer on the STM32 I2C bus (0x19, SA0 high). INT1 → PC14, INT2 → PC15 (EXTI, wakes the STM32 from Stop 1). Put every event that must wake the chip on INT1: PC15 shares EXTI line 15 with the USER button (PA15), and a line takes one pin. Wake-on-motion while parked (tow / break-in, someone getting in), crash spikes while driving. A plug in the OBD port is a poor mount for measurement, fine for "something moved".
- **U16 ATECC608B** secure element on the same bus (0x60): a key that never leaves the chip, to sign the frozen OBD speed window. Needs a one-time, irreversible configuration lock in firmware.
- **J9 panic button** (JST SH 2-pin, bottom edge) for a hidden button on a cable: 1k in series at the connector, then the ESD9B3.3, a 10k pull-up and 100n → `PANIC_BTN` = PC13 (WKUP2: wakes the STM32 even from Standby). Closed = low. The diode is behind the resistor so that a cable shorted to +12 V puts about 8 mA into it, not the battery.

**Car connector:** an SAE J1962 male plug sticking straight out of the board's left end, with the board in the middle of the plug (like an ELM327 dongle). The plug sits on a small adapter board that stands across the board's edge (board 100 x 70 mm plus a 1.3 mm tab):

```
      side view                          top view
  ┌──────────┐┃                       ┌──────┐┏━━━━━━━━━━━━━━━┓
  │   plug   │┃════════ main board    │ plug │┃  main board   ┃
  └──────────┘┃                       └──────┘┗━━━━━━━━━━━━━━━┛
           adapter
```

- **Part:** Shenzhen Jiatel OBD-II-16M-12V, part no. CR-PG001 ("OBD-II 16 Pin Straight Male Connector", jiatelcn.com; drawing `OBDII-Male-16pin.pdf` rev A). Generic "OBD2 16-pin male straight" plugs elsewhere look the same, but only this one has a drawing, so buy it or check another against its numbers. Not at LCSC, so it's hand-fit.
- **Adapter board** (`obd-adapter-plug.kicad_pcb`, generated by `build.py plug`; gerbers `fab/obd-adapter-plug-rev0-gerbers.zip`, order it with the main board): 40 x 18 x 1.6 mm, 2 layers. The plug goes on its front (footprint `lib/OCTAVE.pretty/J1962_Plug_Adapter`): 2 x 8 contacts, 4.00 mm pitch, rows 8.60 mm apart; the contacts are 0.8 x 1.8 mm blades whose tails stand 4.0 mm out of the back, so 2.6 x 1.3 mm plated slots in 3.2 x 2.0 mm pads. Between the rows is a 36 x 2.0 mm slot for the main board's tab (a 1.6 mm board may be 1.76 mm thick before copper and solder), and on the back one joint pad per pin runs from the slot's edge over the pin. The silkscreen says which way round the plug goes: on the front the shroud's D outline, `WIDE SIDE 1-8` / `LATCH SIDE 9-16` and the end pins' numbers, on the back which row meets which side of the main board.
- **Main board end** (J6, footprint `J1962_Edge_Joint`): a 33.8 mm tab, 1.3 mm deep, on the left edge (0.3 mm short of the adapter's front face, so it never reaches the plug), and 16 pads 3.0 x 2.5 mm along the edge, pins 9–16 on the top side and 1–8 on the bottom, centred on the edge so the plug sits square on the board's end. Pin 16 (battery) is the bottom pad of the top row; a 1.6 mm trace runs from it up a lane beside the pads into F1. The silkscreen has the end pins' numbers (9 and 16 on top, 1 and 8 underneath) and `+12V` beside pin 16. The tab carries no copper: `prewire.TAB_KEEPOUT` keeps every router off it and off the first 0.3 mm behind the adapter, and `build.py route` prints any track that ends up there.
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

**Mounting holes:** four M3 holes (H1–H4: 3.2 mm, not plated, connected to nothing) on an 81.5 x 63.0 mm rectangle, at (3.5, 3.5), (85.0, 3.5), (3.5, 66.5) and (85.0, 66.5) mm from the board's top-left corner. The right pair is 15 mm in from the right end, because the two USB-C sockets and the ESP32's antenna take those corners. Within 3.45 mm of each centre there are no parts, tracks or vias on either side (a rule area per hole, `layout.HOLES` / `pcbgen.add_holes`), so an M3 pan head, a washer or a 6 mm standoff sits on bare solder mask; the ground pours run on underneath it. They are for a case or a tray, and for tying the board down in the car so the plug's solder joint doesn't carry it. The holes are not in the schematic, the BOM or the placement file.

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

**Dead-battery pull-downs:** PB6 carries a 5.1k UCPD dead-battery pull-down until firmware sets `PWR_CR3.UCPD_DBDIS` (PA9 enables it, and PA9 is I2C SCL, pulled up). It is safe low at reset: the ESP32 power switch is off, and CONFIG switch 1 still turns it on against the 5.1k. PB4's (CAN2 TXD) is off: PA10, which enables it, has a 10k pull-down (`verify/REVIEW.md` #34).

## Before ordering

1. **Review the schematic in KiCad.** ERC is clean, the board matches it, and `verify/REVIEW.md` lists what the audits and sims checked, but no human has reviewed it yet. Most important:
   - The J1850 and K-line front ends.
   - The ideal-diode and buck values.
   - The plug's pin numbering (`lib/OCTAVE.pretty/J1962_Plug_Adapter` and `J1962_Edge_Joint`: pin 1 top right on the plug's mating face, 9–16 on the main board's top side).
2. **Check the footprints marked in `gen/obd_adapter.py`:**
   - The Sunlord SWPA8065S inductor uses the Changjiang FNR8065S land. The audit found it matches KiCad's own Sunlord 8x8 land; its saturation current is 4.2 A (`verify/REVIEW.md` #24).
   - **The plug against a real one.** Buy the plug first and lay it on a 1:1 print of the adapter board. Check the 0.8 × 1.8 mm blade tails go into the 2.6 × 1.3 mm slots on the 4.00 × 8.60 mm grid, that pin 1 lands on the rounded-square pad and the plug's D shape matches the outline on the silkscreen, and that the plug's flange sits flat on the adapter with the main board's tab in the slot. The tab is 1.3 mm deep and should end inside the slot, 0.3 mm below the adapter's front face (0.14 mm on an adapter at the thin end of its tolerance): the recess in the back of the plug looks narrower than the tab (about 31.7 mm scaled off the drawing, against 33.8 mm), so the tab must not reach it.
3. **Check part rotations in JLCPCB's placement preview.** KiCad and JLC disagree on the zero rotation of some packages (SOT-23, SOIC, the USB-C).
4. **Recheck stock on order day.** The STM32G474RET6 had 112 in stock on 2026-11-09 (20, then 186, on 2026-11-02). Fallback: STM32G474RBT6 (128 KB flash), C529413. Low stock elsewhere: the 8.2 µH inductor (631), ATECC608B (794), AP2114H (2.9k), 5.0SMDJ26CA (4k).
5. **Confirm the J1850 PWM high-level limit** (5.25 V assumed, `verify/REVIEW.md` #10 and #32) against SAE J1850 if you have it.
6. Through-hole parts (the OBD-II plug, pin headers) need JLC's through-hole option or hand soldering; see `fab/*-hand-fit.txt`. Order the adapter board (`fab/obd-adapter-plug-rev0-gerbers.zip`, 2 layers, 1.6 mm) in the same cart; the main board must be 1.6 mm too, or the tab won't fit its slot.
7. **Look at the copper in KiCad.** The route passes DRC with schematic parity (0 errors, 0 unconnected, 0 parity issues, 0 courtyard overlaps), but DRC does not judge quality. Worth a look:
   - The buck's switch node and input loop (hand-laid in `gen/prewire.py`).
   - The USB pairs. J2's run 52 / 48 mm with 2 vias each, J7's 30 / 24 mm with 5 and 2 vias. Both are full speed (12 Mbit/s), where a few millimetres of skew does not matter.
   - The plug's edge pads and the copper behind them.
   - The crystal's tracks (`HSE_IN` 9 mm, `HSE_OUT` 18 mm).
   - The nets the router made a meal of: `CAN1_L` (95 mm, 11 vias, against 52 mm and 4 for `CAN1_H`), `J1850_VSEL` (14 vias), `ESP_RTS` (5 vias) and `EXP_PC10` (104 mm round the ESP32 to the header). All legal, and all slow enough not to care, except that the CAN1 pair is not a pair for half its length: fine on a bench at 500 kbit/s, the first thing to tidy if CAN FD at 2 Mbit/s or more misbehaves.
8. **The 8 DRC warnings that remain** are cosmetic: the outlines of the two USB-C sockets run off the board edge (4), the reference text of R54, C44 and C45 touches its own part's outline (3), and U4's footprint differs from the library copy (the tented ground pad, on purpose). One label, R22, has no clear spot and sits where it can. To move labels, do it in KiCad and copy the positions into `LABELS` in `gen/layout.py`, or the next `build.py pcb` puts them back.

**How the board is routed** (`build.py route`):

- **Frozen.** The routing is frozen in `gen/routing.json`, and `route` replays it, so every rebuild gives the same board. It refuses to run if a part has moved since the freeze. `build.py freeze` captures a routed (or hand-edited) board.
- **The frozen route is the only copy of the hand work.** Freerouting's run of 2026-11-09 stopped with 14 connections open, and the grid router could not close the last of them (`EXP_TX`, `QSPI_IO3`, later `EXP_PC10` and `ESP_RTS`): they sat in a pocket under the STM32 with no way out. Opening it took hand routing: the fan-out of the chip's west and south pins, the QSPI bus, the crossing under the chip, the header area, and pin 48's 3V3 via, moved beside C3 to open the gap its neighbours leave through. None of that is in `gen/`. `OBD_REROUTE=1` starts from nothing and puts that via back, so expect the same pocket to seal again. For a moved part use `OBD_REROUTE=patch`, which keeps the rest.
- **Placement is sticky.** `build.py pcb` puts a part back where `routing.json` has it when that spot is free, so adding or moving one part does not shuffle the others (`OBD_STICKY=0` places from scratch).
- **Routing afresh, in order:**
  1. The power stage is wired by hand (`gen/prewire.py`).
  2. Every SMD GND pad gets a via to the In1 plane, and every 3V3 pad a via to the solid In2 plane. The MCU's VSS / VSSA pins join one via under the chip. All of that is locked.
  3. Freerouting 2.5 routes the rest on F.Cu / B.Cu. Supply nets get wider classes (`WIDTHS`), and battery-side nets keep 0.30 mm from everything (`HV`).
  4. A grid router (`gen/maze.py`) finishes connections Freerouting leaves. It keeps 0.4 mm from the board edge and out of the mounting holes' keep-outs.
  5. GND stitching vias and the outer-layer GND pours go in last.
- **Battery-side clearance.** `obd-adapter.kicad_dru` (generated) makes KiCad's DRC and interactive router enforce the battery-side clearance too.
- **Freerouting is not deterministic.** A fresh route gives a different layout, and on this board it needs connections finished by hand before `build.py freeze`. Run the full `build.py route` DRC on the result: a DRC on a scratch copy with stale pours hides real errors among the false ones.

## Parked current

The board stays on with the engine off (J1962 pin 16 is unswitched battery): the STM32 sleeps and watches the accelerometer, the panic button and the buses. Estimate from datasheet typical values at 12.6 V, 25 °C, with the ESP32 and `DLC_SW` off, both CAN transceivers in standby and the STM32 in Stop 1. The buck's Eco-mode switching is assumed 75 % efficient. **Measure it on the first board.**

| Load | Rail | Current |
|---|---|---|
| LM74700 ideal-diode controller | battery | 80 µA |
| Battery-voltage divider (100k / 10k) | battery | 115 µA |
| TPS54360B, not switching | battery | 146 µA |
| AP2114H LDO ground current | 5 V | 65 µA |
| Buck feedback divider (57.6k / 10.2k) | 5 V | 78 µA |
| VPW threshold divider (18k / 47k) | 5 V | 82 µA |
| STM32G474 Stop 1 (ST's figure, RTC off) | 3.3 V | ~80 µA |
| Two TCAN1044V in standby (VIO) | 3.3 V | 17 µA |
| W25Q128 standby, LIS2DH12 at 10 Hz, ATECC608B asleep, TPS22918 off | 3.3 V | ~14 µA |
| **Total from the battery** | | **≈ 0.53 mA (0.38 Ah / 30 days)** |

- The VPW threshold divider was 2.7k / 10k (420 µA, ~0.72 mA in total), then 13k / 47k. It is 18k / 47k now: 82 µA and a 3.85 V threshold. The earlier ratios gave 4.17–4.19 V on the 5.32 V rail (`verify/REVIEW.md` #45).
- STM32 Standby instead of Stop 1, and the flash in power-down, would only save about 0.05 mA. Stop 1 keeps RAM and wakes on any EXTI pin, so the accelerometer can stay on PC14.
- With the ESP32 on: about 2 mA from the battery while it advertises over BLE (~3 mA average at 3.3 V; the LDO is linear, so 3.3 V current costs about half that from 12.6 V), about 70 mA while Wi-Fi receives the phone's recordings.
- For scale: cheap ELM327 dongles draw 10–60 mA all the time; a car's own parked drain is typically 20–50 mA.
- Further cuts if ever needed: a 1M / 100k battery divider with a 100 nF cap at the ADC pin (−0.1 mA), a lower-Iq LDO, or an STM32-controlled shutdown of the buck (EN) with a separate always-on micro-power rail.

## Known Rev 0 limits

- No J1850 wave shaping (see above).
- Parked current is about 0.53 mA from the battery (see "Parked current"). The buck, the ideal-diode controller, the battery-voltage divider and the LDO set that floor, not the MCUs.
- The J1850 Bus− pull-up (10k from `5V_BUCK` through a B5819W) stays on while parked, so pin 10 sees 5 V / 10k on every car. PWM nodes pull Bus− up anyway and VPW / CAN cars leave pin 10 open, but Rev A should switch it with `DLC_SW` like the K / L pull-ups.
- No GM single-wire CAN, no DoIP.

## Rev A notes

Part-count ideas from the review of 2026-11-09 that were not done on Rev 0 (`verify/REVIEW.md` #54 has the ones that were):

- **The ESP32's own USB port** (J7, the CH340C, its ESD chip, the auto-reset transistors, two buttons, a Schottky and the small parts around them: 17 parts) can go if the STM32 passes `esptool` through from J2 to the ESP32's UART0. That loses flashing the ESP32 on a board with blank STM32 firmware, and the separate console, so it stays on the bring-up board.
- **One dual CAN transceiver** (TI TCAN1046V) for the two TCAN1044V: not checked against JLCPCB's stock.
- **The J1850 receivers' pull-ups** (R47, R50) stay: PC6 / PC7 are the ROM bootloader's I2C4 pins and must not float during DFU. On a board that moves those two nets to other pins, the STM32's own pull-ups would do.
- Resistor arrays, an RGB LED and dual diodes would cut about a dozen more parts but save no money and make probing harder.

## Sources

- ELM327 datasheet rev J, Fig. 9 (J1850 / ISO front ends).
- ScanTool STN1110 / STN1170 / STN21xx datasheets and their reference schematics (the circuits used here).
- OpenDiag rev 1.1 KiCad sources (`maximus64/opendiag-hardware`).
- Motorola AN1731 and the Philips AU5780 / AU5783 datasheets (J1850 VPW electrical limits).
- TI datasheets: TPS54360B, LM317L, TPS22918, TCAN1044V.
