# Rev 0 pre-fab verification (2026-11-02)

The design was generated, so three checks ran on it before ordering. Each worked from the flat netlist (`dump_netlist.py`) and the manufacturers' datasheets, not from the designer's notes.

1. **Pin check:** an automated check of the STM32 pins against ST's official pin database (`pins/`).
2. **Two independent schematic audits:**
   - power and vehicle buses (`power`, `dlcsw`, `j1850`, `iso`, `can`, `car`);
   - digital (`mcu`, `flash`, `ui`, `esp32`, `usb`, `exp`).

   Each checked every IC and transistor against its datasheet: symbol pin numbers, footprint pad numbering for the exact LCSC part, values and ratings.
3. **ngspice simulation** of the analog front ends at 9 / 12 / 16 / 24 V (`sim/`).

Then every finding below was either fixed in `gen/` or accepted with a reason, and the board was regenerated and rechecked: ERC, DRC with schematic parity, and the JLC BOM.

## What the checks found correct

**Pinouts and footprints**
- Every IC and transistor symbol pin and footprint pad matches the datasheet of the exact LCSC part:
  - STM32G474RET6, ESP32-WROOM-32E, TPS54360B, LM74700, AO4264E (drain on VBAT_P, so it blocks a reversed battery);
  - TCAN1044V on the TJA1051T-3 symbol (pin 5 VIO, pin 8 STB);
  - LM317L, LM339, TPS22918, W25Q128, USBLC6-2SC6, NUP2105L, HRO USB-C, Micro-Fit 43045, the tactile switch;
  - SOT-23 transistors (B/E/C, G/S/D) and every diode's cathode.
- **STM32 pin functions:** all 64 pins match ST's open pin data. All 31 alternate functions are available on the pins used: FDCAN2 AF9, FDCAN3 AF11, TIM3 AF2, LPUART1 AF8, USART1/2 AF7, QUADSPI AF10, I2C2 AF4, ADC2_IN17, USB, SWD (`pins/pincheck.md`).

**Simulation results** (`sim/plots/`)
- **J1850 VPW:**
  - transmits 7.0–7.6 V across 9–24 V, inside the 6.25–8 V spec;
  - decodes in every bus-load corner the spec allows;
  - in the listen case (another node drives the bus) the pulse-width error is at most 3 µs.
- **J1850 PWM** is within limits.
- **K-line at 10.4 kbaud:** low level is at most 1.7 % of battery voltage, and receive pulse-width error is at most 5.2 % of a bit.
- **Reverse battery at −12 / −16 / −24 V:** the board draws 0.13–0.26 mA. ISO 7637 pulse 1 puts at most 40.5 V across the 60 V MOSFET.
- **Buck:** 597 kHz, and the loop is stable.
- **Battery sense:** the ADC pin stays within its rating.
- **DLC_SW switch:** stays saturated under worst-case normal load.

## Findings and what was done

| # | Severity | Found by | Finding | Status |
|---|---|---|---|---|
| 1 | **Bug** | power audit + sim | With DLC_SW off, the car's K/L pull-up back-fed the sleeping DLC_SW loads through the 510R pull-ups. K/L idled at 2–8 V, read as a held-low line. | **Fixed:** 1N4148W in series with each pull-up. Simulated: K idles at 12.00 V with DLC_SW at 0.28 V, and K-line timing is unchanged with the adapter on. |
| 2 | **Bug** | power audit | The LM74700 input capacitor was missing (datasheet: at least 22 nF required). | **Fixed:** 100 nF 50 V on VBAT_F. |
| 3 | **Fail** | sim | The buck's EN divider (start 7.8 V, stop 6.1 V typ, 7.7 V worst) shut the buck off in a 6 V cold crank, so everything reset on each engine start. | **Fixed:** divider not fitted (DNP); the internal UVLO (~4.3 V) takes over. |
| 4 | **Fail** | sim + power audit | R27 (DLC_SW base, 0603) was over rating from 24 V upward. | **Fixed:** 4.7k 1206 (0.25 W). |
| 5 | **Fail** | sim + power audit | The 510R 1 W K/L pull-ups dissipate 1.1 W with a line held low at a 24 V jump start. | **Fixed:** Yageo 2 W 2512. |
| 6 | Risk | both audits + sim | The "5V" rail was one SS34 drop low (4.4–4.6 V). That is under the TCAN1044V's 4.5 V minimum and moved the VPW threshold below 3.5 V. | **Fixed:** FB 57.6k / 10.2k gives 5.32 V at the buck, about 4.9 V after the OR. USB-only power still gives about 4.6 V, which is marginal for CAN on the bench. |
| 7 | Risk | power audit + sim | The LM339 can exceed its 36 V absolute maximum (VBAT_P 37 V in a pulse 2a, 42 V at the TVS clamp). The LM339 is also rated 0–70 °C only. | **Fixed:** LM2901DR (same pinout, −40 to 125 °C), supplied from DLC_SW through 470R with a BZT52C33 zener (CMP_VCC). |
| 8 | Risk | power audit + sim | The SMDJ26CA (3 kW) has a 28.9 V breakdown, below the 35 V suppressed load dump. It would absorb 45–67 J. | **Fixed:** 5.0SMDJ26CA (5 kW, same SMC footprint). A higher-voltage TVS would push the MOSFET and LM2901 limits instead. |
| 9 | Risk | power audit | The B5819W Schottky input clamps leak up to 1 mA when hot, which shifts the thresholds behind 10k. | **Fixed:** 1N4148W (nA leakage). |
| 10 | Risk | sim | J1850 PWM Bus+ high was 5.5–5.6 V. | **Fixed:** LM317L divider 240 / 787 / 453 gives 7.71 V VPW (unchanged) and 5.35 V PWM, about 5.0 V on the bus. **Check:** this assumes the J1850 PWM VOH maximum is 5.25 V, which nobody has confirmed against the spec. |
| 11 | Risk | sim | Q8 (K sink) dissipates 221 mW at 24 V with a low-gain part. | **Fixed:** 470R base resistors. |
| 12 | Risk | sim | 47 µF 10 V 1206 parts are only 15–20 µF at 5 V each, so phase margin drops to about 34° in the worst case. | **Fixed:** a third 47 µF. |
| 13 | Risk | sim | ISO 7637 pulse 1 sags VBAT_P to 5.2 V (only ~14.5 µF on it), causing a reset. | **Fixed:** 47 µF 50 V electrolytic on VBAT_P. |
| 14 | Risk | digital + power audits | The AP2112K (SOT-23-5, 184 °C/W) reaches thermal shutdown with Wi-Fi in a hot car. | **Fixed:** AP2114H-3.3 in SOT-223 (1 A, ceramic-stable, pinout matches the AMS1117 symbol: 1 GND, 2 VOUT + tab, 3 VIN). |
| 15 | **Layout** | digital audit | The ESP32 load switch sat about 120 mm from the module, with 3V3_ESP on a 149 mm track and the module's caps about 20 mm from its pin. That invites brownouts on Wi-Fi TX. | **Fixed:** TPS22918, 22 µF, 100 nF, its input cap and the EN RC are placed beside the module's 3V3 / EN pins. 3V3 has a pour on In2. |
| 16 | Layout | digital audit | The flash decoupling cap was ~17 mm away, and the NRST cap ~15 mm away on a 73 mm track. | **Fixed:** both pinned next to their pins. |
| 17 | Risk | digital audit | On a blank board the ESP32 can't be powered for esptool: PB6 is held low and J3 has no 3V3 pin. | **Fixed:** the `ESP_ON` jumper (JP1, a 2-pin header with a shunt, from 3V3 to the load switch's ON pin; a solder jumper in round 1, changed so it can be switched repeatedly; CONFIG switch 1 since the layout rework, through 1k since #47). |
| 18 | Risk | digital audit | The USBLC6 was wired flow-through with two separate nets per line, so continuity depended on the clone's internal link. | **Fixed:** one net per line through pins 1/6 and 3/4. |
| 19 | Risk | digital audit | The bootloader may drive PC10 (USART3 TX), which would switch on DLC_SW. Unconfirmed: AN2606 could not be fetched. | **Fixed:** DLC_SW_EN moved to PC3; PC10 is unused. |
| 20 | Nit | digital audit | The green and blue LEDs only got 0.1–0.5 mA at 1k. | **Fixed:** 330R. |
| 21 | Nit | digital audit | The crystal's CL is 9 pF, so 12 pF caps pulled it about 20 ppm low. | **Fixed:** 10 pF C0G. |
| 22 | Nit | digital audit | VDDA has no 10 nF (DS12288 Fig. 16). | **Fixed:** 10 nF added. |
| 23 | Firmware | digital audit | The STM32 can back-power the ESP32 through its I/O while the load switch is off (QOD holds 3V3_ESP at ~0 V). | **Accepted:** firmware keeps PA1/PA2/PB5/PB7 low or as inputs until the ESP32 is on, and drives EN open-drain. Noted on the schematic. |
| 24 | Risk | sim | The inductor's saturation current is 4.2 A (power audit), but the buck's current limit is 4.5–6.25 A, so a shorted 5 V output can saturate it. | **Accepted:** normal peak is about 2 A. A Würth 7447797820 is the drop-in if needed. |
| 25 | Risk | power audit | The NPN line sinks (K, L, Bus−) have no current limit, so a line shorted to battery while transmitting kills them. | **Accepted for Rev 0:** ELM and STN share this. Rev A: a sense resistor or a protected low-side switch. |
| 26 | Nit | power audit | The comparators have no hysteresis (same as STN), so slow edges could double-trigger. | **Accepted:** firmware filters input capture. Rev A: optional 1M feedback footprints. |
| 27 | Nit | sim | J1850 edges are unshaped (under 10 ns rise). | **Accepted:** same as ELM, STN and OpenDiag. Rev A: an RC on the AO3401A gate. |
| 28 | Nit | digital audit | The SWD header is unshrouded and has pin 7 fitted, so keyed cables won't seat. | **Accepted:** use an unkeyed cable, and mind the orientation. |
| 29 | Nit | digital audit | Unconfirmed: whether the system bootloader drives I2C3 on PC8/PC9 (J1850 TX) or USART2. | **Accepted:** both have pull-downs, and DFU happens on the bench. |

## Round 2 (2026-11-03)

After round 1 was fixed, five checks ran again on the regenerated design. Each was done by a fresh reviewer with no access to the round-1 conclusions:

1. **Schematic audit** of the whole netlist against the datasheets, including ST AN2606 (what the system bootloader does to each pin).
2. **Re-simulation** of every analog front end: ISO 7637-2 pulses 1, 2a, 3a and 3b; the ISO 16750-2 5b load dump; cold crank; and tolerance corners for the LM317L, the comparator and the buck.
3. **Independent layout review** of the routed board: connectivity graph of the real copper, hot loop, return paths, decoupling distances, clearances and thermal.
4. **JLCPCB DFM and assembly review** against the official JLC/EasyEDA footprints of every LCSC part.
5. **STM32 pin check**, re-run.

### Circuit findings

| # | Severity | Found by | Finding | Status |
|---|---|---|---|---|
| 30 | **Bug** | audit | The G47x bootloader puts PC9 (`J1850_TXN`) in I2C3 open-drain with its pull-up on (AN2606 Table 101). Against R37 100k, that turns Q7 on and holds J1962 pin 10 (Bus−) low for as long as the part is in DFU. This refutes item 29. | **Fixed:** 2.2k base-emitter on Q7 (worst-case base 0.26 V in the bootloader; normal drive is still 2.3 mA). |
| 31 | **Fail** | sim | The LM2901DR is the non-B die (36 V abs max, 30 V operating). CMP_VCC reaches 37 V at a 42 V clamp and 85 °C with a high-tolerance BZT52C33, and R39 (0603) sees 0.28 W. | **Fixed:** LM2901BIDR (38 V abs max, 36 V operating, same pinout); R39 in 1206. |
| 32 | **Fail** | sim | J1850 PWM Bus+ high reaches 5.45–5.58 V in the LM317L tolerance corner (Vref 1.30 V, Iadj 100 µA), and VPW reaches 8.03 V. | **Fixed:** divider 240 / 680 / 470. Simulated across all corners: PWM 4.26–5.00 V, VPW 6.42–7.54 V. The 5.25 V PWM VOH limit is still taken on trust. |
| 33 | **Fail** | sim | If Bus+ is shorted to ground while transmitting, the LM317L current limit pulls Q2 (MMBT5401) out of saturation: 0.5–5.8 W in a SOT-23. | **Fixed:** DLC_SW high side is now a P-MOSFET (SI2309CDS, 60 V). The gate is pulled up with 47k, clamped with a 12 V zener, and driven by Q3 through 10k (1206). The FET stays fully on, and the LM317L's own limit and thermal shutdown take the fault. |
| 34 | Risk | audit | In the bootloader, PA10 (USART1_RX, pull-up) enables the dead-battery Rd on PB4 (CAN2_TXD), which requests dominant. STB is held up only 50 mV above VIH against the bootloader's PB15 pull-down. | **Fixed:** 10k pull-down on PA10; STB pull-ups 10k → 4.7k (STB ≥ 2.78 V worst case). |
| 35 | Risk | audit | The bootloader drives PA2 (USART2_TX) push-pull high while the ESP32 is unpowered and its supply is held at 0 V by the load switch. That back-powers the module through its ESD diode. Item 23 only covered the application. | **Fixed:** 1k in series with PA1, PA2, PB5 and PB7 (injection ≤ 3 mA; the RC is 10 ns at 3 Mbaud). |
| 36 | Risk | audit + sim | On USB-only power the OR'd 5V is 4.24–4.52 V, under the TCAN1044V's 4.5 V minimum and near its 4.4 V UV-rising limit. The VPW threshold also leaves its window. | **Fixed by design choice:** TCAN VCC, the VPW reference and the Bus− pull-up now run from 5V_BUCK, so they work whenever the car (or 12 V on J6) is present. **CAN and J1850 are not valid on USB-only bench power: use 12 V on J6.** |
| 37 | Risk | audit | The 2920L110 PTC derates to about 0.5 A hold at 85 °C. A hot crank with Wi-Fi can trip it. | **Fixed:** 2920L200/60 (2 A hold, 60 V). |
| 38 | Risk | audit + sim + layout | AP2114H junction temperature. At 128 °C/W with no copper, 0.25 A average gives 134–145 °C at 85 °C ambient. | **Fixed in layout:** a 3V3 F.Cu tab zone plus 5 vias into the In2 3V3 pour. Rev A: a 3V3 buck if an 85 °C rating is wanted. |
| 39 | Risk | sim | ISO 16750-2 5b (35 V suppressed load dump, 400 ms): the 5.0SMDJ26CA (VBR 28.9–31.9 V) clamps the whole pulse, 42–62 J. With a VBR-minimum part the PTC trips and latches. | **Accepted for Rev 0:** a 33CA would lift the clamp to 53 V, over the 50 V caps and closer to the 60 V FET and buck. Real alternators have central suppression, which is what 5b models. Rev A: 33CA plus 63/100 V caps. Bench: don't apply 5b. |
| 40 | Risk | audit | Temperature grade: STM32G474RE**T6**, ESP32-WROOM-32E and W25Q128JV-IQ are −40..85 °C parts. | **Accepted for bring-up.** For an 85 °C cabin rating: G474RET3, WROOM-32E-H8, W25Q128JVSJQ. |
| 41 | Marginal | sim | Pulse 1 hold-up: with a −40 °C electrolytic and a heavy load, 3V3 dips below 3.0 V for 0.5–1.6 ms (reset, self-recovering). | **Accepted:** ISO function status C. Rev A: 100–220 µF low-temperature or polymer on VBAT_P. |
| 42 | Marginal | sim | Buck gain margin is only 1.8–5.5 dB at VIN 6 V / 1.5 A (duty 0.89, subharmonic). Regulation holds. | **Accepted.** Watch SW on the scope during a crank. |
| 43 | Marginal | sim | VPW receive is invalid below VBAT ≈ 6–7.3 V (comparator common-mode range); K below 4–6.4 V. | **Accepted:** no J1850 traffic expected during cranking. |
| 44 | Nit | audit | Remaining nits are N4–N15 in the round-2 audit, for example no VBUS sense, no ESD on Qwiic/EXP, and AP2114 fold-back at start into a heavy load. | **Accepted for Rev 0.** |

### Layout findings (the round-1 board was DRC-clean but the autorouter had undone the power-stage intent)

| # | Finding | Status |
|---|---|---|
| L1 | Buck hot loop about 50 mm² across three top-side GND islands; the catch diode returned through one via. | **Fixed:** the power stage is placed explicitly and wired before routing (`gen/prewire.py`): input caps on pin 2, D5 at the SW pin, a 4-via anode ground, GND vias at every cap, and an F.Cu keepout for other nets. |
| L2 | BOOT cap loop 27.6 mm. | **Fixed:** C26 sits on pins 1/8 with stubs of about 2 mm. |
| L3 | FB, RT and COMP ran beside and under the switch node. | **Fixed:** the divider and compensation sit at pins 4–6, away from SW. FB senses 5V_BUCK at the output caps over B.Cu. |
| L4 | The In2 3V3 pour touched the net at two vias; 3V3 was distributed on 0.25 mm tracks over 90–100 mm. | **Fixed:** In2 is a solid 3V3 plane (no signals on it), and every 3V3 pad has its own via to it. |
| L5 | LDO input/output caps 44–46 mm from the regulator. | **Fixed:** they are placed at the pins. |
| L6 | Bus TVS/ESD parts sat past the devices they protect; VBAT_IN ran 30 mm, mostly on In2, before the PTC. | **Fixed:** F1 and D4 at J6.1 (VBAT_IN is a few mm of 1.2 mm F.Cu); the SMF24A and NUP2105L clamps at their J6 pins. |
| L7 | Battery and 5 V power on the 0.5 oz inner layer could fuse before the PTC trips. | **Fixed:** classes ≥ 0.4 mm and all battery-voltage nets are restricted to F.Cu/B.Cu. |
| L8 | Battery-voltage nets at 0.20 mm from MCU pins. | **Fixed:** battery-side class clearance 0.30 mm. |
| L9 | Decoupling 15–30 mm from the TCAN, LM2901 and LM317L supply pins; the LM74700 gate track was 42 mm; VBAT_SENSE filter 83 mm from PA4. | **Fixed:** placed at their pins (`near_rules()` in layout.py). |
| L10 | The flash sat at the opposite corner from the QSPI pins. | **Fixed:** moved below the MCU. |
| L11 | CAN pairs are not routed as coupled pairs. | **Accepted:** Freerouting can't. The skew doesn't matter at CAN rates; check emissions on the bench. |
| L12 | The 0.30 mm battery-side clearance existed only in the router's input, so KiCad's DRC never checked it; checking it found about 50 violations (GND fan-out and stitching vias, and a finisher route of 5V_BUCK). | **Fixed:** `obd-adapter.kicad_dru` (generated) makes KiCad's DRC and interactive router enforce it; the fan-out, stitching and finisher keep it too. Closest battery-side gap on the final board: 0.302 mm. |
| L13 | U6 (LM74700) pin 2 GND had no via (it sits between two battery pins) and hung on one thermal spoke of an F.Cu pour island with no via. A floating GND pin leaves Q1 off, and the load runs through its body diode. | **Fixed:** the fan-out tries every 0.05 mm spot when its rings fail; U6.2 has its own via 1.1 mm away. |
| L14 | The finisher placed two vias inside 0603 pads (R45.1, R12.1). | **Fixed:** moved out, and the finisher no longer puts a via in or touching any SMD pad. The only holes in SMD pads are U7's thermal vias (D7). |
| L15 | Freerouting leaves 3–8 connections, different ones each run, mostly STM32 pins boxed in by their neighbours. | **Fixed:** grid finisher (`gen/maze.py`) with rip-up for the last few; the result is frozen in `gen/routing.json`, and rebuilds replay it. 5V_BUCK branches (CAN VCC, VPW reference, FB) are 0.4 mm; its power path is 1.0–1.2 mm. |

### Assembly (DFM) findings

| # | Finding | Status |
|---|---|---|
| D1 | 26 CPL rotations disagreed with JLC's footprint orientation, and the ESP32, USB-C and Micro-Fit centroids were offset. | **Fixed:** the `JLC_CORR` table in build.py (rotation and local offset per LCSC part). Its output matched the reviewer's independently corrected CPL for all 152 parts. **Still check the JLC placement preview for U1, U4, U7, J2 and L1.** (J6 is now the hand-fit plug.) |
| D2 | Solid GND pour on 0603 pads: tombstoning risk. | **Fixed:** thermal reliefs (0.3 gap, 0.4 spoke). Large pads, the buck and the ESP32 ground pad stay solid. |
| D3 | ESP32 ground-pad vias open on the bottom mask (solder drains through). | **Fixed:** tented on B.Mask. |
| D4 | No fiducials. | **Order option:** "edge rails + fiducials added by JLCPCB". |
| D5 | L1 (C96978) has no JLC footprint, so JLC maps it by hand. | **Check:** in the preview, pads must be left and right (0/180). |
| D6 | J6 is through-hole. | **Round 4:** J6 is the OBD-II plug's edge joint (16 SMD pads, top and bottom); the plug itself is through-hole on the adapter board. Not at LCSC, hand-fit. |
| D7 | U7's paste window lies over open thermal vias. | **Accepted:** the bottom is tented; TI allows it. |

### Round 3 (2026-11-03): ESP32 programming

The plan is to program everything over USB. As built, USB-C (J2) reaches only the STM32, and the ESP32's ROM bootloader listens only on UART0, which went only to J3. So the first ESP32 flash, and any recovery, needed an external adapter on J3, and a blank STM32 left the ESP32 unpowered.

| # | Change | Why |
|---|---|---|
| E1 | JP1 `ESP_ON`: solder jumper → 2-pin 2.54 mm header with a shunt (moved to (93.5, 30), clear of the ESP32 courtyard). | It gets switched more than once; a 1.3 mm solder jumper lifts after a few reworks. |
| E2 | Second USB-C (J7, right edge) → USBLC6 → CH340C → ESP32 UART0, with the two-NPN auto-reset on DTR / RTS. | esptool / Arduino flash and a console with no adapter and no STM32 firmware. A USB hub was considered and left for a later product revision: on a bring-up board it would put one chip between the PC and both MCUs, DFU included. |
| E3 | VBUS2 → SS34 → `5V`, and the load switch's ON pin is now a diode OR (1N4148W) of PB6 and VBUS2. | J7 alone powers the board and the ESP32. Feeding VBUS2 into the PB6 net instead would lose to PB6's 5.1k UCPD dead-battery pull-down on a blank chip, and put ~4.5 V on PB6 once it is off. |
| E4 | CH340C powered from `3V3_ESP`; 1k in series with its TXD. | It is off whenever the ESP32 is, so it can't back-power the module through RXD0; an adapter on J3 can still drive RXD0. |
| E5 | `ESP RST` (SW4) and `ESP BOOT` (SW5) buttons, TS-1187A like SW1-3. | For hosts whose DTR / RTS timing defeats the auto-reset. |
| E6 | Battery-side set (`HV` in build.py) gained CAN1_TERM, CAN2_TERM, IDEAL_GATE, IDEAL_VCAP and BUCK_BOOT. | The round 2 audit counted them as battery-side, but the build didn't. The re-route put CAN2_TXD 0.165 mm from CAN1_TERM, which sits at CAN bus potential through the 120R. Fixed: re-routed, and now enforced by the rule. |

The board was fully re-routed for round 3: placement is unchanged except for the new parts and JP1, and the expansion block (J4, J5, R16-18) is now pinned where the packer had put it.

The 17 new parts are numbered after the existing ones (J7, U13, U14, Q10-11, SW4-5, D25-27, R61-65, C44-45), so no existing reference changed. The CH340C uses a project symbol (`OCTAVE:CH340C`) with V3 typed passive: the datasheet ties V3 to VCC at 3.3 V, which the stock symbol reports as two power outputs joined. Its JLC rotation (270) was checked by pad match against the TCAN1044's SOIC-8.

### Round 4 (2026-11-08): plug into the OBD-II port

The first pass should plug straight into the car instead of hanging on a Micro-Fit cable and a pigtail.

| # | Change | Why |
|---|---|---|
| P1 | Micro-Fit J6 → a J1962 male plug that sticks straight out of the board's left end with the board in its middle, like an ELM327 dongle: the plug is on a 40 × 18 mm adapter board (`obd-adapter-plug`, `build.py plug`) that stands across the edge, the main board's 33.8 mm tab through its slot, 16 edge pads (9–16 top, 1–8 bottom) soldered to its joint pads. Board 110 × 75 mm plus the tab (100 × 70 mm after P5). | Asked for (a sketch: plug at the end, board in its middle). No J1962 plug straddles a board (its rows are 8.6 mm apart), so the adapter does it. A first version had the plug flat on the board's underside in a 22 mm strip; that was replaced the same day. JLC/LCSC has no J1962 plug, so it is hand-fit. |
| P2 | New footprints in `lib/OCTAVE.pretty`, generated by `gen/mklib.py`: `J1962_Plug_Adapter` for Jiatel CR-PG001 (OBD-II-16M-12V; 2 × 8, 4.00 mm pitch, rows 8.60 mm apart, 2.2 × 1.2 mm plated slots for its 0.8 × 1.8 mm blade tails, 3.1 × 2.0 mm pads, the tab slot and the joint pads; the slots are larger since #49 and #51) and `J1962_Edge_Joint` (the main board's edge pads). 3D models `lib/3dmodels/J1962_Male_Straight_Jiatel_CR-PG001.step` and `J1962_Edge_Joint_Assembly.step` from `gen/mkplug3d.py`. | Jiatel's drawing (`OBDII-Male-16pin.pdf` rev A) is the only dimensioned straight male plug found. The first version of this footprint used the 3.00 mm row grid of Macchina's OBD2 test board (PCB-01028 R0, J1) and 1.5 mm round holes; modelling the plug from the drawing showed that grid doesn't fit it (Macchina's own M2 uses a right-angle plug, CCBDM205), so the footprint was redone and the board rerouted. **Check against a real plug before ordering** (README, Before ordering 2). |
| P3 | Pin 4 (chassis ground) → GND alongside pin 5. | The ELM327 datasheet notes that a few vehicles have no pin 5 and need pin 4. With a cable, the pin was left off to save a wire; on a plug it costs nothing. |
| P4 | Bus clamps (SMF24A ×4, SZNUP2105L ×2) re-placed by `near_rules` just inside the edge pads, level with their plug pins; the plug is centred on the left edge; F1 moved 2 mm right, and VBAT_IN is pre-wired at 1.6 mm from pin 16's edge pad up a reserved lane (`layout.VBAT_LANE`) into it. | Same rules as L6: clamps at the connector, a short battery input. |
| P5 | Layout rework, left to right along the signal: plug centred on the left end, car front ends, STM32 in the middle, ESP32 centred on the right end with its antenna hanging off it; the two USB-C ports mirrored on the long edges (STM32 top, ESP32 + CH340C bottom, where its UART pins face); the five buttons in one even row along the top with the LEDs under the STM32's; the remaining top-side parts in aligned rows and columns; the small parts (0603 / SOD-123 / SOT-23 / test points) on a 3.5 × 2.5 mm matrix, all the same way round; the buck moved to the top-left corner. 110 × 75 → 100 × 70 mm. | Asked for (orderly, smaller). Tried and rejected: two-sided assembly at 84 × 58 mm (123 small parts underneath): too dense to route on 4 layers (37 connections left; Freerouting hung with In2 as a signal layer). `gen/congestion.py` (a RUDY estimate, calibrated on these routes: keep peak ≤ 0.55, p95 ≤ 0.33 on two signal layers) picked 100 × 70; `gen/precheck.py` checks the placement against the hand copper before routing. `OBD_TWO_SIDED=1` still builds the two-sided placement. Also fixed: the HRO USB-C 3D model was 2.6 mm behind its footprint. |
| P5 | Fresh route (`OBD_REROUTE=1`), re-frozen into `gen/routing.json`. Four single-layer stub vias the finisher left (ESP_IO0, ESP_CTS, ESP_TX_MCU, EXP_PA10) were deleted before freezing. | Freerouting left 1 connection on two runs (CAN2_TXD, then QSPI_IO1, both at the LQFP, see L15); the next fresh run finished clean. |

### Round 5 (2026-11-09): audit before the reroute

Asked for: be certain the board will work and is useful as a prototype. The netlist had grown since the last full audit in round 2 (the second USB port, the CONFIG switch, the plug, the parked-watch parts, the microSD), and the plug's mechanics had never been reviewed. The audit read the whole netlist again, the new parts' datasheets, and the two plug footprints against the Jiatel drawing. The board was placed but not routed at the time, so no copper was reviewed.

| # | Severity | Finding | Status |
|---|---|---|---|
| 45 | **Fail** | The VPW receive threshold was 4.17 V nominal, and up to 4.30 V over the buck's and the resistors' tolerance. J1850 VPW wants a receiver to read low below 3.5 V and high above 4.25 V. The 13k / 47k divider kept STN's 10 / 12.7 ratio, which is meant for a 5.0 V reference; since #6 and #36 it hangs on `5V_BUCK` at 5.32 V. | **Fixed:** 18k / 47k gives 3.85 V (3.72–3.97 V over tolerance) and draws 82 µA. One new BOM line (18k, C25810, a JLCPCB basic part). 4.7k / 13k (3.91 V) was the first choice and was dropped: 300 µA on a rail that stays up while parked. Not re-simulated. |
| 46 | **Bug** | The ESP32 could switch itself on while parked. With J7 unplugged, `VBUS2` had no path to ground, so the reverse leakage of its SS34 (D25, from `5V`) went through the ON diode (D27) into the load switch's 100k pull-down. 10 µA makes 1 V, the TPS22918's VIH; an SS34 leaks more than that when hot (0.3–0.4 mA in a hot car). The ESP32 then runs whatever its firmware does, from a parked battery. | **Fixed:** R69, 1k from `VBUS2` to GND: under 0.4 V at 0.4 mA, below VIL (0.5 V) before the diode's drop. Costs 5 mA from the PC while J7 is plugged in. |
| 47 | Risk | CONFIG switch 1 tied 3V3 straight to PB6. Firmware that drives PB6 low (ESP32 off) with the switch left on shorts the rail through the pin. | **Fixed:** R70, 1k in series with the switch: 3.3 mA instead. On a blank chip PB6's 5.1k dead-battery pull-down leaves 2.76 V on PB6 and about 2.2 V on ON, above the 1.0 V VIH. |
| 48 | Risk | The panic input's ESD diode (D28, 3.3 V) sat on the connector pin, ahead of the 1k. The button is on a cable routed through the car; shorted to +12 V, the battery was straight across the diode. | **Fixed:** the diode is behind the 1k (on `PANIC_BTN`): about 8 mA into the clamp. |
| 49 | **Fit** | The plug's blade slots were 2.2 × 1.2 mm for 0.8 × 1.8 mm blades. The blade's corner was 0.03 mm inside the slot's end radius, with nothing left for the plating or for position error over 16 pins: the plug would most likely not go in. | **Fixed:** 2.6 × 1.3 mm slots in 3.2 × 2.0 mm pads (0.18 mm at the corner; a 0.35 mm ring on the long sides and 0.3 mm at the ends; 0.8 mm between pads). **Still check against a real plug.** |
| 50 | Layout | The last route ran CAN2_H, CAN2_L and LLINE over the plug's tab, up to 0.7 mm past the board's edge: inside the adapter's slot, under the joint, where the board is filed and pushed through. Nothing kept the routers off it. | **Fixed for the next route:** `prewire.TAB_KEEPOUT` (the tab and the first 0.3 mm behind the adapter, all four layers) goes to Freerouting, the grid finisher and the fan-out; `build.py route` prints any track or via that still lands there (`prewire.on_tab`). A trial route of the placed board reached every connected edge pad with nothing left of x = 1.5 mm. `gen/routing.json` still holds the old tracks until the reroute. |
| 51 | **Fit** | The adapter's slot for the tab was 1.8 mm. A 1.6 mm board is 1.6 mm ± 10 %, plus up to 0.07 mm of mask and finish: about 1.83 mm. | **Fixed:** 2.0 mm. The fillets bridge the 0.2 mm; the joint pads start 0.25 mm from the slot. |
| 52 | Risk | Nothing on either board said which way round the plug goes. The footprint's pin numbers were on the Fab layers only. The contact grid is symmetric, so the plug fits the adapter both ways round and the adapter fits the main board both ways up. Either mistake puts every pin on the wrong pad, and it shows only in the car. | **Fixed:** the adapter's front has the shroud's D outline, `WIDE SIDE 1-8` / `LATCH SIDE 9-16` and the end pins' numbers; its back says which row goes to which side of the main board. The main board has 9 / 16 (top) and 1 / 8 (bottom) at the pads and `+12V` beside pin 16. |

**Checked and found correct**

- Every LCSC number in `gen/parts.py` against JLCPCB's parts library. Thin stock on the day: STM32G474RET6 112, the 8.2 µH inductor 631, ATECC608B 794.
- The STM32 pin check, re-run on this netlist (`pins/pincheck.md`): all 31 functions available. `check.py` still asked for two nets by their old names (`ESP_CTS`, `MCU_TX_ESP`) and silently skipped them; corrected.
- The plug's pin mapping through both footprints: plug pin → adapter joint pad → main board edge pad → net, for all 16 pins, against the Jiatel drawing and J1962's numbering.
- The parts added since round 2, against their datasheets.
- ERC: 0 errors, 0 warnings. Adapter board DRC: 0 violations. Placement with the hand-laid power copper (`precheck.py`): no errors.
- The two new resistors moved nothing: every other part and reference label is where it was. R69 sits in the USB2 tile beside J7's CC resistors, R70 5.5 mm from SW6 pin 1. The routing-density estimate (`congestion.py`) is peak 0.59, p95 0.34 with and without them: slightly over the 0.55 / 0.33 guideline, as the placement already was.

**Not changed, check on the first boards**

- **Tab depth.** Changed after all, in round 6 (#53).
- **Tab-root radius.** The router bit leaves a radius of up to 1 mm where the tab meets the board's edge. With the 2.0 mm slot's round ends it holds the adapter off by at most 0.05 mm (calculated).
- **Typical figures:** #46 and #48 rest on typical figures for an SS34 and a 3.3 V ESD diode. The datasheets of the exact parts (C8678, C96512) were not checked.
- **Stale until the reroute:** the board's copper, `gen/routing.json`, the DRC figures below and the main board's files in `fab/`. The adapter board's gerbers were rebuilt.

The two new parts are numbered after the existing ones (R69, R70), so no reference changed.

### Round 6 (2026-11-09): routing audit, mounting holes, fewer parts

Three things before the reroute: an audit of what the router is told (asked for with the tab change), four mounting holes, and a pass over the netlist for parts that can go with no loss of function.

| # | Severity | Finding | Status |
|---|---|---|---|
| 53 | **Fit** | The tab was 1.6 mm deep, flush with the adapter's front face. The recess in the back of the plug looks narrower than the tab (about 31.7 mm scaled off Jiatel's drawing, against 33.8 mm), so a tab that reaches the face can hold the plug's flange off the adapter. | **Fixed:** 1.3 mm (`mklib.TAB_DEPTH`): the tab ends 0.3 mm inside the slot, 0.14 mm with an adapter at the thin end of its tolerance (1.44 mm). No part moved. |
| 54 | Change | Five parts were doing what a pin's own pull-up, or nothing, can do. A sixth change, a Schottky for the ideal diode (three parts fewer), was tried and taken out again. | **Changed:** 205 → 200 footprints, see below. |
| 55 | **Bug** | Fifteen battery-side nets (CAN1 / CAN2 H and L, J1850 P / N, the buck bootstrap and others) were listed in two classes of the router's input, and Freerouting takes the last class that names a net: they were routed at 0.15 mm clearance, not 0.30 mm. KiCad's DRC (L12) caught the result, so the board of 2026-11-08 was clean, but the router was working to the wrong rule and the finisher had to repair it. | **Fixed:** every net is in exactly one class (`build._dsn_postprocess`; checked on a generated DSN, no net listed twice). |
| 56 | Risk | The plug's pads this board does not use (J1962 pins 1, 8, 9, 12, 13) had signal clearance. In a car some of them carry battery voltage (pin 1 is often switched 12 V). | **Fixed:** their nets get the battery-side 0.30 mm in the router, the finisher, the fan-out and KiCad's DRC (`build.HV_SPARE`; tested: a track 0.2 mm from pad 1 is flagged). |
| 57 | Added | No mounting holes: the board hung on the plug's solder joint. | **Added:** four M3 holes (3.2 mm, not plated, no net) on an 81.5 × 63.0 mm rectangle, with a 3.45 mm keep-out round each centre on both sides (`layout.HOLES`). The DIP switch and the buttons moved 1.5 mm left, the test points became two rows of five, J9 and its parts moved left of J5, the microSD and Qwiic sockets 6.5 mm left. |

**#54, the parts that went**

| Was | Now | Why it is the same function |
|---|---|---|
| FB1 (600 Ω bead) + C8 (10 n) on a separate VDDA net | VDDA / VREF+ on 3V3, with their own 100 n + 1 µ at the pins | The ADC reads one thing, the battery divider (100k / 10k with 100 n at the pin, averaged). ST allows VDDA tied to VDD. |
| R2, the USER button's 10k pull-up | PA15's own pull-up | PA15 resets as JTDI with its pull-up on, so the pin is defined before firmware runs too. |
| R60, `KLINE_RX`'s 10k pull-up | PC0's own pull-up (25–55k), set by firmware | The LM2901 output is open collector. 55k into about 15 pF is under 1 µs, against a 96 µs bit at 10.4 kbaud. The pin floats until firmware sets it; PC0 is not a bootloader pin. |
| R10, the 10k pull-up on the ESP32's IO0 | IO0's own pull-up (about 45k, on at reset) | BOOT (SW5), the auto-reset transistor (Q11) and PB5 still pull it low through 1k or less. |

- **Kept:** R47 / R50, the pull-ups on `VPW_RX` / `PWM_RX`. PC6 / PC7 are the ROM bootloader's I2C4 pins, and the LM2901 has no supply while `DLC_SW` is off, so without the resistors the two pins would float during a DFU session. The first list had them going; that would have been four resistors, not two.
- **References:** the removed parts' numbers are not reused, so no other reference changed.
- **Not affected:** the battery reading (the divider is on `VBAT_F`, read against the LDO as before).

**#54, the change that was taken out again: a Schottky for the ideal diode**

For a few hours on 2026-11-09 the ideal diode (U6 LM74700, Q1 AO4264E, C19, C20) was one B560C Schottky, D29: the same part as the buck's catch diode, three parts and two extended BOM lines fewer, and 80 µA less with the car parked (0.53 → 0.45 mA). It was placed and half routed. The simulation written to check it (`sim/sim_input_d29.py`: both stages in the same deck, a generic Schottky model with the B560C's datasheet figures) showed two things the ideal diode does not do, so U6 / Q1 / C19 / C20 are back, where they were, and D29 is gone:

| | Ideal diode (as built) | B560C Schottky |
|---|---|---|
| Drop at 0.1 / 0.25 / 0.5 A | about 20 mV | 0.27 / 0.31 / 0.33 V |
| Battery reversed, −12 V, 25 °C | no current | 0.1 mA, `VBAT_P` −0.12 V |
| Battery reversed, −12 V, 100 °C | no current | 3.8 mA, `VBAT_P` −0.72 V |
| Battery reversed, −24 V, 100 °C | no current | 7.8 mA, `VBAT_P` −0.76 V |
| 6 V cold crank, `VBAT_P` | 5.92 V | 5.58 V |
| Pulse 1 (−150 V, 10 Ω, 2 ms), 1 W load, `VBAT_P` minimum | 9.0 V | 9.5 V |
| Load dump 5b / pulse 2a, `VBAT_P` peak | 32.7 / 25.6 V | 32.5 / 24.8 V |

- **Reversed battery.** A Schottky leaks in reverse (the B560C's limits: 0.5 mA at 25 °C and 20 mA at 100 °C at 60 V, taken as resistors). Nothing on `VBAT_P` takes that current, so the node goes negative until the buck's own diodes conduct (GND → D5 → SW → the high-side FET's body diode → VIN), at about −0.7 V when hot. The TPS54360B's absolute minimum on VIN is −0.3 V. The FET blocks: no current. (The deck prints −0.17 V for the FET case; that is the idle-current term of its load model, which still draws near 0 V.)
- **Cranking.** 0.3 V less on `VBAT_P`: at a 6 V crank the buck is at the end of its duty range for a 5.32 V output, and the lowest crank the board rides through rises by the same 0.3 V (the UVLO is on `VBAT_P`). `DLC_SW` is fed from `VBAT_P` too, so the K / L pull-ups and the LM317L's input (the 7.24 V VPW supply) would have sat 0.3 V lower.
- **Not different:** pulse 1 at 3.5 W (below the 4.3 V UVLO for 1.4–1.5 ms with either stage: #41), load dump, pulse 2a.
- A clamp Schottky from GND to `VBAT_P` would have dealt with the first (and leaked at 12 V all the time the car is parked); nothing short of the FET deals with the second. Three parts was not worth either.
- `sim/sim_input_d29.py` stays as the record. `sim/sim_input.py` and the `plots/input_*.png` plots (round 2) are the input path as built.

### Round 7 (2026-11-10): the route

The board of rounds 5 and 6 was routed afresh on 2026-11-09 (Freerouting 2.5, three passes, about 4 hours with the finisher). It stopped with 14 connections open. The grid finisher closed most of them; the rest took fixes to the generator and routing by hand. What that turned up:

| # | Severity | Finding | Status |
|---|---|---|---|
| 58 | **Bug** | The buck's input island (pin 2 and its two capacitors) lies inside the buck keep-out, which the grid finisher treats as closed to every net, so only Freerouting could reach it. It did not: `VBAT_P` was in two pieces, the buck cut off from the ideal diode. | **Fixed:** the hand-laid power copper now leaves the keep-out itself, a 0.6 mm track to a via 0.9 mm below it (`prewire.py`). |
| 59 | **Bug** | The accelerometer's four 3V3 pads (LGA-12, 0.5 mm pitch) were open: the plane fan-out's stub cannot leave them. | **Fixed:** pads 9 / 10 run into the 10 µF beside them, pads 2 / 3 to a via on the other side (`prewire.py`). |
| 60 | **Bug** | The grid finisher did not know the board outline (a route came back along the bottom edge), and it tested a track's centre, not its edge, against keep-outs: `EXP_PC10` ran with its edge 25 µm inside the bottom-right mounting hole's keep-out. | **Fixed:** `maze.off_edge` (0.4 mm from the outline, as Freerouting is told) and `maze.kept_out` (the copper's radius counts). The track moved 0.2 mm. |
| 61 | Layout | A pocket under the STM32 had no way out: the fan-out via of pin 48 (3V3) stood in the gap those nets leave through, and `EXP_TX`, `QSPI_IO3`, `EXP_PC10` and `ESP_RTS` could not leave. | **Fixed by hand:** the via moved beside C3, the west and south pins' fan-out, the QSPI bus, the crossing under the chip and the header area were re-laid. This lives only in `gen/routing.json`: a fresh `OBD_REROUTE=1` run would put the via back. |
| 62 | Layout | C35 (the 100 nF at PA4, the battery-voltage ADC pin) was placed by rule into the fan-out lanes of the chip's south-west corner. | **Fixed:** a fixed spot at (62.0, 41.5), in the notch at the corner of the LQFP's courtyard (`layout.NOTCHED` exempts it from the bounding-box overlap test; the real courtyards do not overlap, and DRC agrees). |
| 63 | Process | Two of the finisher's misses (#60, and a corner 4 µm inside the 0.2 mm hole clearance of J7's locating hole) passed the quick DRC on a scratch copy and were caught by the full run. The quick check filtered out every violation that named a zone, to drop the false clearances of a stale pour; a keep-out is a zone too. | **Fixed:** both tracks; the filter drops only clearance lines. The result below is from `build.py route` on the project board: placement regenerated, routing replayed, pours filled, full DRC with schematic parity. |

- **Sticky placement.** `build.py pcb` now keeps a part where the frozen route has it when that spot is free (`layout.place_all`, `OBD_STICKY=0` to turn off), so a small change no longer shuffles the board. The replay still refuses a board whose placement differs from the freeze.
- **Legal but ugly,** left as they are: `CAN1_L` 95 mm with 11 vias against 52 mm and 4 for `CAN1_H`; `J1850_VSEL` 14 vias; `ESP_RTS` 5 vias; `EXP_PC10` 104 mm (from pin 52 under the ESP32 module, down the right edge, along the bottom to J5). None is fast. The CAN1 pair is the one to redo first if CAN FD at 2 Mbit/s or more gives trouble.

### Round 8 (2026-11-10): tidying the copper, parts without a fee, the buck switching

A look for what could still go wrong on the routed board gave nine points. This round is the ones that could be dealt with at the desk: the copper that round 7 left ugly, the BOM lines that carry JLCPCB's fee for no reason, and the buck, which until now had only been analysed small-signal. The route is still the one of round 7: the changes below are hand edits to it, frozen again (`gen/routing.json`, 3243 items).

| # | Severity | Finding | Status |
|---|---|---|---|
| 64 | Layout | `HSE_OUT` ran 18 mm through two vias under the STM32 because the crystal's two terminals were the wrong way round for the chip: pins 5 / 6 (PF0 / PF1) leave side by side and the tracks had to cross. | **Fixed:** Y1's pads swapped in the netlist (pad 3 on `HSE_IN`, pad 1 on `HSE_OUT`; a crystal has no polarity) and both tracks re-laid on the top side: 8.7 and 9.0 mm, no vias. The fan-out via of `ACC_INT1` moved 0.085 mm to let them past. |
| 65 | Layout | The CAN1 pair was not a pair: `CAN1_L` hopped over `CAN1_H` and changed sides four times between the plug and the transceiver. | **Fixed, as far as the board allows:** from the plug to U9 both lines now run about 26 mm side by side, and `CAN1_L` goes to the bottom once (2 vias). It cannot stay on top: the 1.6 mm battery trace (`VBAT_IN`, on F.Cu at x = 4.6) and C19's `VBAT_F` spur lie across the way. Net totals: `CAN1_L` 93.5 mm and 9 vias (95 mm and 11), `CAN1_H` 51.7 mm and 4. |
| 66 | Layout | Round 7 read `CAN1_L`'s 95 mm against `CAN1_H`'s 52 mm as a 43 mm mismatch in the pair. It is not: the difference is in the branches, to the termination (R26 beside U9 on `CAN1_H`, the switch SW6 on `CAN1_L`) and to the ESD diode. What is wrong is where that diode is: D8 hangs on a 16 mm stub past U9, where CAN2's D9 sits in line between the plug and its transceiver. | **Accepted for Rev 0:** 16 mm of stub does nothing at 2 or 5 Mbit/s, and the diode still clamps; it clamps better in line. **Rev A:** place D8 between J6 and U9. Round 7's "the CAN1 pair is the one to redo first" is withdrawn. |
| 67 | Cost | Six BOM lines were extended parts ($3 loading fee each) where a part from JLCPCB's basic or preferred library does the same job: the buck's RT (162k), its feedback divider (57.6k / 10.2k), the 39 pF on COMP, the four SMF24A bus TVS and the 12 V gate-clamp zener. | **Fixed:** RT 160k (C22813, preferred), feedback 68k / 12k (C23231 / C22790, basic), 47 pF (C1671, basic), SMF24A C19077514 and BZT52C12 C19077410 (both preferred, same ratings class: 200 W SOD-123FL, 350 mW SOD-123 against 36 mW dissipated at the 42 V clamp). Same footprints, same positions, no copper changed. What the values do to the buck is below. |
| 68 | Cost | The 33 V zener (D17) has a preferred equivalent too. | **Kept** as the 500 mW part (C173417): it takes 0.63 W through the 470 Ω while the TVS clamps a load dump at 42 V, and the preferred part is 350 mW. |
| 69 | Record | Round 7 gave `ESP_RTS` 5 vias; it has 6. `ESP_EN`'s 92 mm is a six-point tree (the module, the 1k to the STM32, the pull-up, the RST button, the auto-reset transistor and the capacitor) close to its shortest, with its capacitor at the module's pin: nothing to tidy. | Corrected here and in the README. |
| 70 | Check | The buck had been analysed small-signal only ("Not covered"). `sim/sim_buck_sw.py` now switches it cycle by cycle: start-up, steady state from 9 to 42 V, a load step, a 6 V crank, dropout, and a sweep of the slope compensation. | **No board change.** Results below. One thing the model cannot settle goes on the first board's bench list: the SW node at 6 to 7 V input (#71). |
| 71 | Risk | Below about 7.5 V input the duty cycle passes 0.74 and the loop needs 0.22 to 0.35 A/µs of slope compensation to be free of subharmonic oscillation with 8.2 µH. TI does not publish the TPS54360B's internal ramp, so neither the model nor the datasheet can say that it has that much. | **Accepted, to measure:** the inductor is the one in TI's own 5 V / 600 kHz example, so it is expected to be fine. On the first board look at SW with 6 to 7 V on the plug and 0.5 to 1 A drawn from 5 V: pulse widths that alternate are the sign. The fix, if it shows, is a larger L1 in the same footprint (10 µH needs 0.29 A/µs at worst, 15 µH 0.19). It only matters during cranking; the output ripple it would cause is tens of millivolts. |

**The buck with the new values** (#67). The datasheet's equations, then the averaged loop model (`sim/sim_buck.py`, re-run):

| | Before | Now |
|---|---|---|
| RT, switching frequency (Eq. 7) | 162k, 597 kHz | 160k, 605 kHz |
| Feedback divider, output | 57.6k / 10.2k, 5.32 V | 68k / 12k, 5.33 V |
| Divider current | 78 µA | 67 µA |
| COMP: 13k + 6.8 nF, pole capacitor, pole | 39 pF, 314 kHz | 47 pF, 260 kHz |
| VPW threshold (18k / 47k from `5V_BUCK`) | 3.85 V (3.72 to 3.97) | 3.86 V (3.73 to 3.98) |
| Phase margin, typical (12 V, 51 µF, 0.6 A) | 71.5° | 70.9° |
| Phase margin, worst corner (42 V, 25 µF, 0.1 A, slope compensation at its high end) | 35.6° | 34.6° |

- The loop loses about 1° of phase margin from the lower pole; crossover stays between 20 and 51 kHz across the corners, the best corner has 80.8°, and the gain margin at the worst corner is 11.9 dB. The load step (0.1 to 1.5 A) undershoots 165 to 202 mV.
- The worst corner is a load dump on a board whose output capacitors have lost half their value to bias and age, at almost no load. It was 35.6° before this round as well; it is not something the part swap made.
- `5V` after the SS34 stays about 4.9 V, the TCAN1044V's supply stays inside 4.5 to 5.5 V, and the parked current is unchanged to two figures (0.53 mA).

**The buck, switching** (#70; `sim/sim_buck_sw.py`, results in `sim/out/buck_sw.json`, about 12 minutes in ngspice). A behavioural model built from the datasheet (clock, peak-current comparator, slope ramp, error amplifier, soft start, minimum on-time, pulse skipping, frequency foldback) round the board's own parts, with the ceramics derated for DC bias (51 µF of output capacitance, 36 µF at the corner). It is not TI's model, which ngspice cannot run. In continuous conduction its duty cycle, inductor ripple and output ripple agree with the hand formulas to 2 %.

| Case | Result |
|---|---|
| Start-up, 12 V applied, 0.3 and 1.0 A | Rises in 1.6 ms (datasheet soft start: 1.7 ms), 15 mV overshoot, inductor peak 1.2 / 1.9 A. Charging the input capacitors takes a 5.9 A peak from the battery for the assumed harness (50 mΩ, 2 µH) and a 100 µs edge. |
| Steady state, 9 / 12 / 14.4 / 24 / 42 V at 0.05 to 1.5 A | Output 5.333 V in all 20 cases. Ripple 1.0 to 4.3 mV p-p, and 8.5 mV at 42 V and 50 mA, where the part skips pulses. Inductor peak 2.0 A at most. |
| The same at 1.5 A with L1 30 % low and 36 µF | Ripple 3.5 to 8.8 mV, inductor peak 2.2 A (4.2 A saturation, #24). |
| Input side | `VBAT_P` ripple 75 mV p-p at most (42 V, 1.5 A). The 47 µF electrolytic carries 21 mA RMS at most; the ceramics take the rest. |
| Load step 0.1 to 1.0 A and back, 12 V | 131 mV under, 117 mV over, back within 10 mV in 0.23 ms. With 36 µF: 138 / 123 mV. About 30 % more than the averaged model gives for the same step (101 mV). |
| Cold crank 12 → 6 → 7 → 12 V, 0.25 and 0.5 A on 3V3 | `VBAT_P` 5.90 V at the bottom, duty 0.92 (0.93 with the switch at its maximum on-resistance). `5V_BUCK` stays between 5.331 and 5.343 V, `5V` at 4.98 V, and the LDO keeps 1.68 V of headroom where it needs 0.38 V. |
| Dropout, 0.6 A | Regulates down to 5.6 V at the plug. Below that the output follows the battery, about 0.18 V under it, and 3V3 stays in regulation until about 4.25 V, just above the UVLO (4.1 V at the plug). |
| Slope compensation, 0.6 A | Stable at every setting from 12 V up. At 9 V it needs 0.13 A/µs, at 7.5 V 0.22, at 6.5 V 0.28; the sweep's stable / unstable boundary falls where theory puts it. This is #71. |

- The cases above use an assumed ramp of 0.6 A/µs. The sweep is what shows how much the result depends on it: not at all above 9 V.
- Assumed, not measured: the ramp, the pulse-skipping current (0.17 A), the capacitors' bias derating and ESR, the harness. L1's resistance (31 mΩ) is from a distributor's listing.
- Outside the model: ringing on the switch node, spikes from the capacitors' inductance, EMI (all layout), losses and temperature, the boot capacitor's refresh in dropout (the datasheet's 99 % limit was applied by hand).
- The first version of the model had a latch that could stick half-way on the first soft-start pulse and showed 15 V on the output. That was the model (fixed: the minimum-on-time node clears only once the latch is off), reproduced and corrected before any of the figures above were taken.

**Firmware started** (`firmware/`, not part of the board's verification, recorded here because it is where the obligations below now live). A bring-up skeleton for the STM32: the obligations as a pin table (`firmware/src/safe_pins.c`, each row citing its item), a pin map generated from `gen/obd_adapter.py` and the pin check, a serial console, CAN in bus-monitoring mode with TXD never given to the controller. It builds without warnings for the board and for a NUCLEO-G474RE and its host tests pass. **It has not run on hardware.** Writing it turned up two things about the board:

- The only console on Rev 0 until there is USB code is USART1 on J5, and J5 is not fitted: solder it, or three wires.
- The note on the MCU sheet gave the J1850 supply as 7.7 V / 5.8 V, the figures from before #10 and #32. Corrected to 7.24 V / 4.79 V (the text on the sheet only).

**What the swap leaves with a fee:** 35 extended parts ($105 a batch, from 41 and $123). No fee-free equivalent exists for the rest: the ICs and connectors, the green and blue LEDs, USBLC6-2SC6, ESD9B3.3, the 0.1 % divider pair, the 470 Ω 1206, the 47 µF 50 V electrolytic and the 510 Ω 2 W resistors.

### Firmware obligations (all rounds)

- Keep PA1/PA2/PB5/PB7 low or as inputs until the ESP32 is powered, and drive ESP_EN open-drain (item 23).
- Set LPUART1 TXINV **before** switching PC1 to its alternate function. LPUART idles high, and through the NPN that holds K low.
- Set PWR_CR3.UCPD_DBDIS early, and put PB4/PB13 in AF (TXD high) before driving STB low. Never drive STB low while TXD is low.
- Set the W25Q128 QE bit before using quad mode.
- Abort a J1850 transmit on the first echo mismatch.
- Filter comparator input capture (item 26).
- Leave PB5 (ESP32 IO0) and PB7 (EN) as inputs while the ESP32 is being flashed through J7, or they fight the auto-reset through their 1k. With J7 plugged in, PB6 can't turn the ESP32 off (VBUS2 holds the switch on).
- Round 5: one pin per EXTI line. Line 15 is the USER button (PA15) or the accelerometer's INT2 (PC15), so route every accelerometer event that must wake the chip to INT1 (PC14). Line 3 is PA3 (ESP32 UART) or PB3 (CAN2 RXD), line 0 is PA0 or PC0 (K-line RX): pick the wake source per line.
- Round 5: enter the ROM bootloader with the ESP32 off (CONFIG switch 1 off, J7 unplugged). It listens on USART2 (PA3 is the ESP32's TX) and on I2C4 (PC6 / PC7, the J1850 receivers) as well as USB, and takes the first that talks.
- Round 5: there is no 32 kHz crystal (PC14 / PC15 are the accelerometer interrupts), so the RTC runs on LSI.
- Round 5: `ESP_SD_CD` (IO23) has no external pull-up: enable the ESP32's. Hot-inserting a card may dip `3V3_ESP` (10 µF at the socket); if the ESP32 resets on insertion, that is why.
- Round 5: the ATECC608B wakes on SDA held low for 60 µs or more, so slow traffic to the other devices on the bus can wake it; send it back to sleep, and check the parked current with the accelerometer in use. Addresses 0x19 and 0x60 are taken on the Qwiic bus.
- Round 6: two inputs have no pull-up resistor any more. Turn on PC0's pull-up before enabling LPUART1's receiver (`KLINE_RX` is an open-collector output), and keep PA15's pull-up on when the pin becomes a GPIO (it is on from reset, as JTDI; a plain input without `PUPDR` set would float). The ESP32 side needs nothing: IO0's pull-up is on at reset.

## Result after the fixes

- **ERC:** 0 errors, 0 warnings.
- **DRC (the route of 2026-11-10 with round 8's hand edits, full run with schematic parity):** 0 errors, 0 unconnected pads, 0 schematic-parity issues, 0 courtyard overlaps, no dead-end tracks or vias, no copper on the plug's tab. 8 warnings: the silkscreen outlines of the two USB-C sockets run off the board edge (4), the reference text of R54, C44 and C45 touches its own part's outline (3), and U4's footprint differs from the library copy (the tented ground pad, on purpose). The route of 2026-11-08 had 138 silkscreen warnings and 2 dead-end stubs.
- **Rules:** signals 0.15 mm track / 0.15 mm gap (Default class), battery-side nets 0.30 mm from everything, 0.2 mm from holes, 0.3 mm from the board edge (routed at 0.4), vias 0.6 / 0.3 mm. All within JLCPCB's standard 4-layer process (0.09 mm minimum).
- **STM32 pin check:** re-run on the round 5 netlist, all 31 functions OK. PC3 (`DLC_SW_EN`) is a plain GPIO.
- **Board:** 100 × 70 mm plus the plug tab, and the 40 × 18 mm plug adapter; 200 footprints and four mounting holes, 187 JLC-assembled parts, 83 BOM lines, 1 hand-fit (the J6 OBD-II plug), 2 headers not fitted (J5, J8), 10 test-point pads.
- **Price** (JLCPCB's parts library, 2026-11-10, every line in stock): $33.59 of parts per board at quantity-1 prices, of which the STM32G474RET6 is $13.00, the ESP32 module $4.56, the LM74700 $2.36 and the flash $2.26. 43 BOM lines are basic parts, 4 preferred, 36 extended; the extended ones are 35 different parts, $105 of loading fees per batch (round 8, #67). Boards, stencil, setup and shipping come on top. Round 1's figure was $30.60 for 152 parts.
- **Copper:** 2606 track segments and 637 vias (635 at 0.6 / 0.3 mm, 2 at 0.45 / 0.2 mm), 3.13 m on F.Cu and 2.70 m on B.Cu, none on the inner layers. The GND plane (In1) and the 3V3 plane (In2) are each one piece with no long slots.
- **Power nets:** every supply and battery-side net is one connected piece. `VBAT_IN` is 1.6 mm wide. The thin sections of `VBAT_F` and `VBAT_P` (12 and 6 mm under 0.6 mm) are the LM74700's anode, cathode, EN and charge-pump connections, which carry no load current, and the 0.3 mm of `5V_BUCK` is the feedback sense.
- **Track lengths:**
  - USB: J2's pair 52 / 48 mm, 2 vias each; J7's 30 / 24 mm, 5 and 2 vias. Full speed.
  - CAN: `CAN1_H` 52 mm, `CAN1_L` 94 mm; `CAN2_H` 48 mm, `CAN2_L` 75 mm. These are whole nets, with the branches to the termination switch and the ESD diodes; from the plug to the transceiver CAN1 runs about 26 mm on both lines (round 8, #65 and #66).
  - Crystal: `HSE_IN` 8.7 mm, `HSE_OUT` 9.0 mm, top side, no vias (round 8, #64). `QSPI_CLK` 23 mm. `BUCK_FB` 3.7 mm.
  - 3V3_ESP: 118 mm in all (31 mm on 2026-11-08), but the supply path from the LDO past its capacitor to the module's pin is the short run in the top right corner; the rest are light branches to the CH340C, the microSD and RN1.
  - 3V3: a solid plane on In2.
  - The ESP32 EN and STM32 NRST caps sit at their pins. `NRST` is 52 mm and `ESP_EN` 92 mm (53 to 58 mm before), because they run to the STM32, SWD, the buttons and the auto-reset transistors.

## Not covered

- **Model fidelity.** No vendor SPICE models ran in ngspice; TI's are encrypted. The LM317L, LM339, LM74700 and buck are datasheet-parameter models. The buck has an averaged loop model and, since round 8, a switching one; both are built from the datasheet, and the part's internal slope compensation is a guess in both (#71).
- **Re-simulation.** The fixes above were checked by hand calculation. Only the K/L back-feed diode was re-simulated.
- **EMC.** No emissions or immunity testing.
- **The copper, beyond DRC.** The route is rule-clean and complete, and nobody has looked at it. See the list in the README ("Before ordering", item 7): the buck loop, the USB pairs, the plug's edge pads, the crystal's tracks, the CAN1 pair and the long nets of round 7.
- **The plug on a real part.** The footprints follow one drawing. Fit, tab depth and orientation are checked only on paper. `fab/obd-adapter-plug-rev0-print-1to1.pdf` is the sheet to try a bought plug on before ordering (round 8).
- **The J1850 PWM high-level limit** (5.25 V assumed, #10 and #32) and the VPW threshold change (#45) in simulation.
- **Signal integrity.** Nothing was simulated or measured on the copper: no impedance control on the USB or CAN pairs (short, and slow enough not to need it on paper).
