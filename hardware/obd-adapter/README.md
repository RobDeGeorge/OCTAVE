# OCTAVE OBD adapter, Rev 0 (bring-up board)

Our own OBD-II adapter: STM32G474 (vehicle side) + ESP32 (Bluetooth Classic SPP, BLE, Wi-Fi), every OBD-II bus, USB-C. Rev 0 is a bring-up board for the bench and the Jeep, not the final dongle. It has room to probe, test points, header access to spare pins, and a cable to the car instead of a plug on the board. Plan and background: `TODO/diy-obd-adapter.md`.

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

Use the system `python3` (KiCad's `pcbnew` module lives there), not the repo venv. Tools: KiCad 10, Freerouting 2.x (`/opt/freerouting`, AUR `freerouting-bin`).

The schematic and the board are generated from the same netlist, and `kicad-cli pcb drc --schematic-parity` checks that they agree. Every pin on the schematic is joined by net label, so a net is everything that carries the same name. **Do not hand-edit the generated files**: the next build overwrites them. If you route by hand in KiCad, stop running `pcb` / `route` and keep the board under hand control from then on.

## Blocks

**MCU: STM32G474RET6.** A 16 MHz crystal (for FDCAN timing), SWD on a 2x5 1.27 mm header, RESET / BOOT / USER buttons and R/G/B LEDs.

**Power:** J1962 pin 16 → 1.1 A 60 V PTC → 5.0SMDJ26CA → LM74700 ideal diode + AO4264E → `VBAT_P` (with 47 µF bulk).
- `VBAT_P` feeds a TPS54360B buck (60 V input; the datasheet's 600 kHz design with the output set to 5.32 V), then an SS34 diode-OR with USB VBUS (`5V` ≈ 4.9 V), then an AP2114H 3.3 V LDO (SOT-223).
- The buck's EN divider is not fitted, so the internal UVLO keeps it running through a 6 V cold crank.
- The ESP32 has its own TPS22918 load switch (`ESP_PWR_EN`, PB6, off at reset), placed at the module's 3V3 pin. Close the `ESP_ON` solder jumper to power it without STM32 firmware.
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
- SZNUP2105L ESD. 120 Ω termination only through the `TERM` jumpers, for bench buses.

**Radio:** ESP32-WROOM-32E-N8.
- UART2 (IO16 RX / IO17 TX, IO18 CTS / IO19 RTS) to USART2 with flow control.
- UART0 + EN + IO0 on `J_ESP_PROG` for esptool. The STM32 can also reset the ESP32 (PB7) and strap IO0 (PB5).
- The antenna sits at the top edge, with the module's keepout cleared on every layer.

**USB-C:** device (5.1k CC), USBLC6 ESD, PA11 / PA12 (DFU bootloader + CDC). On USB power alone `5V` is about 4.6 V, marginal for the CAN transceivers (4.5 V minimum).

**Storage, expansion:** 16 MB W25Q128 on QUADSPI1 (store-and-forward); Qwiic I2C (PA8 / PA9); a 2x6 header with 3V3, 5V, USART1 and spare GPIO.

**Car cable:** Molex Micro-Fit 3.0 2x5, right angle, to an OBD-II pigtail:

| Micro-Fit | J1962 | Signal |
|---|---|---|
| 1 | 16 | Battery + |
| 2 | 5 | Signal ground |
| 3 | 2 | J1850 Bus+ |
| 4 | 10 | J1850 Bus− |
| 5 | 7 | K-line |
| 6 | 15 | L-line |
| 7 | 6 | CAN-H |
| 8 | 14 | CAN-L |
| 9 | 3 | MS-CAN-H |
| 10 | 11 | MS-CAN-L |

J1962 pin 4 (chassis ground) is not used; signal ground carries the return.

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
| I2C (Qwiic) | PA8 SDA, PA9 SCL |
| Expansion UART (USART1) | PC4 TX, PC5 RX |
| Spare to header | PA5, PA10, PC13, PD2 |
| LEDs / button | PB9 R, PC11 G, PC12 B, PA15 USER |
| BOOT0 | PB8 (10k pull-down + BOOT button) |

**Why not FDCAN1:** its pins clash with USB (PA11 / PA12) and with BOOT0 (PB8). A CAN transceiver's idle-high RXD on PB8 would start the chip in the bootloader.

**Dead-battery pull-downs:** PB4 and PB6 carry UCPD dead-battery pull-downs until firmware sets `PWR_CR3.UCPD_DBDIS`. Both are safe low at reset: CAN2 TXD with its transceiver in standby, and the ESP32 power switch off.

## Before ordering

1. **Review the schematic in KiCad.** ERC is clean, the board matches it, and `verify/REVIEW.md` lists what the audits and sims checked, but no human has reviewed it yet. Most important:
   - The J1850 and K-line front ends.
   - The ideal-diode and buck values.
   - The car-connector pinout against your pigtail.
2. **Check the footprints marked in `gen/obd_adapter.py`:**
   - The Sunlord SWPA8065S inductor uses the Changjiang FNR8065S land. The audit found it matches KiCad's own Sunlord 8x8 land; its saturation current is 4.2 A (`verify/REVIEW.md` #24).
   - The Micro-Fit 43045-1000 orientation.
3. **Check part rotations in JLCPCB's placement preview.** KiCad and JLC disagree on the zero rotation of some packages (SOT-23, SOIC, the USB-C).
4. **Recheck stock on order day.** The STM32G474RET6 had 20 in stock, then 186, on 2026-11-02. Fallback: STM32G474RBT6 (128 KB flash), C529413. Low stock elsewhere: AP2114H (2.9k), 5.0SMDJ26CA (4k).
5. **Confirm the J1850 PWM high-level limit** (5.25 V assumed, `verify/REVIEW.md` #10) against SAE J1850 if you have it.
6. Through-hole parts (Micro-Fit, pin headers) need JLC's through-hole option or hand soldering; see `fab/*-hand-fit.txt`.
7. The board is autorouted. Before fab, look at the buck switch node, the ESP32 antenna area and the USB pair in KiCad.
8. DRC (2026-11-02, after the verification fixes): 0 copper errors, 0 unconnected, 0 schematic-parity issues, 0 courtyard overlaps; 101 silkscreen warnings (labels overlapping each other or clipped by pads / the edge, which JLC clips anyway). Tidy the labels you care about in KiCad.

**How the board is routed** (`build.py route`): GND first. Every SMD ground pad gets a short track and a via to the In1 plane, and the MCU's VSS / VSSA pins join one via under the chip. All of that is locked. Freerouting then routes everything else (supply nets in wider classes, see `WIDTHS`). Afterwards: GND stitching vias wherever one fits, GND pours on both outer layers, and a 3V3 pour on In2 around the routed signals. Freerouting is not deterministic, so each `route` run gives a different, equally valid layout. The checked-in board is one such run.

## Known Rev 0 limits

- No J1850 wave shaping (see above).
- Not optimised for sleep current. The parts to power down are there (`DLC_SW`, ESP32 load switch, CAN standby), but the 3.3 V regulator and the buck stay on.
- No GM single-wire CAN, no DoIP.

## Sources

- ELM327 datasheet rev J, Fig. 9 (J1850 / ISO front ends).
- ScanTool STN1110 / STN1170 / STN21xx datasheets and their reference schematics (the circuits used here).
- OpenDiag rev 1.1 KiCad sources (`maximus64/opendiag-hardware`).
- Motorola AN1731 and the Philips AU5780 / AU5783 datasheets (J1850 VPW electrical limits).
- TI datasheets: TPS54360B, LM317L, TPS22918, TCAN1044V.
