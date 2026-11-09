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
| 17 | Risk | digital audit | On a blank board the ESP32 can't be powered for esptool: PB6 is held low and J3 has no 3V3 pin. | **Fixed:** the `ESP_ON` jumper (JP1, a 2-pin header with a shunt, from 3V3 to the load switch's ON pin; a solder jumper in round 1, changed so it can be switched repeatedly). |
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
| P2 | New footprints in `lib/OCTAVE.pretty`, generated by `gen/mklib.py`: `J1962_Plug_Adapter` for Jiatel CR-PG001 (OBD-II-16M-12V; 2 × 8, 4.00 mm pitch, rows 8.60 mm apart, 2.2 × 1.2 mm plated slots for its 0.8 × 1.8 mm blade tails, 3.1 × 2.0 mm pads, the tab slot and the joint pads) and `J1962_Edge_Joint` (the main board's edge pads). 3D models `lib/3dmodels/J1962_Male_Straight_Jiatel_CR-PG001.step` and `J1962_Edge_Joint_Assembly.step` from `gen/mkplug3d.py`. | Jiatel's drawing (`OBDII-Male-16pin.pdf` rev A) is the only dimensioned straight male plug found. The first version of this footprint used the 3.00 mm row grid of Macchina's OBD2 test board (PCB-01028 R0, J1) and 1.5 mm round holes; modelling the plug from the drawing showed that grid doesn't fit it (Macchina's own M2 uses a right-angle plug, CCBDM205), so the footprint was redone and the board rerouted. **Check against a real plug before ordering** (README, Before ordering 2). |
| P3 | Pin 4 (chassis ground) → GND alongside pin 5. | The ELM327 datasheet notes that a few vehicles have no pin 5 and need pin 4. With a cable, the pin was left off to save a wire; on a plug it costs nothing. |
| P4 | Bus clamps (SMF24A ×4, SZNUP2105L ×2) re-placed by `near_rules` just inside the edge pads, level with their plug pins; the plug is centred on the left edge; F1 moved 2 mm right, and VBAT_IN is pre-wired at 1.6 mm from pin 16's edge pad up a reserved lane (`layout.VBAT_LANE`) into it. | Same rules as L6: clamps at the connector, a short battery input. |
| P5 | Layout rework, left to right along the signal: plug centred on the left end, car front ends, STM32 in the middle, ESP32 centred on the right end with its antenna hanging off it; the two USB-C ports mirrored on the long edges (STM32 top, ESP32 + CH340C bottom, where its UART pins face); the five buttons in one even row along the top with the LEDs under the STM32's; the remaining top-side parts in aligned rows and columns; the small parts (0603 / SOD-123 / SOT-23 / test points) on a 3.5 × 2.5 mm matrix, all the same way round; the buck moved to the top-left corner. 110 × 75 → 100 × 70 mm. | Asked for (orderly, smaller). Tried and rejected: two-sided assembly at 84 × 58 mm (123 small parts underneath): too dense to route on 4 layers (37 connections left; Freerouting hung with In2 as a signal layer). `gen/congestion.py` (a RUDY estimate, calibrated on these routes: keep peak ≤ 0.55, p95 ≤ 0.33 on two signal layers) picked 100 × 70; `gen/precheck.py` checks the placement against the hand copper before routing. `OBD_TWO_SIDED=1` still builds the two-sided placement. Also fixed: the HRO USB-C 3D model was 2.6 mm behind its footprint. |
| P5 | Fresh route (`OBD_REROUTE=1`), re-frozen into `gen/routing.json`. Four single-layer stub vias the finisher left (ESP_IO0, ESP_CTS, ESP_TX_MCU, EXP_PA10) were deleted before freezing. | Freerouting left 1 connection on two runs (CAN2_TXD, then QSPI_IO1, both at the LQFP, see L15); the next fresh run finished clean. |



- Keep PA1/PA2/PB5/PB7 low or as inputs until the ESP32 is powered, and drive ESP_EN open-drain (item 23).
- Set LPUART1 TXINV **before** switching PC1 to its alternate function. LPUART idles high, and through the NPN that holds K low.
- Set PWR_CR3.UCPD_DBDIS early, and put PB4/PB13 in AF (TXD high) before driving STB low. Never drive STB low while TXD is low.
- Set the W25Q128 QE bit before using quad mode.
- Abort a J1850 transmit on the first echo mismatch.
- Filter comparator input capture (item 26).
- Leave PB5 (ESP32 IO0) and PB7 (EN) as inputs while the ESP32 is being flashed through J7, or they fight the auto-reset through their 1k. With J7 plugged in, PB6 can't turn the ESP32 off (VBUS2 holds the switch on).

## Result after the fixes

- **ERC:** 0 errors, 0 warnings.
- **DRC (final, after P5, replayed from `gen/routing.json`):** 0 copper errors, 0 unconnected, 0 schematic-parity issues, 0 courtyard overlaps. 138 silkscreen warnings (JLCPCB clips silk on pads), 2 dead-end track stubs left by the finisher (5V_BUCK 4 mm, VBAT_SENSE 7 mm, both DC; trimming them further cut a real connection), and 1 note that U4's footprint differs from the library copy (the tented ground pad, on purpose).
- **Rules:** signals 0.15 mm track / 0.15 mm gap (Default class), battery-side nets 0.30 mm from everything (closest on the board: 0.302 mm), vias 0.6 / 0.3 mm. All within JLCPCB's standard 4-layer process (0.09 mm minimum).
- **STM32 pin check:** re-run on the new netlist, all 31 functions OK. PC3 (`DLC_SW_EN`) is a plain GPIO.
- **Board:** 100 × 70 mm plus the plug tab, and the 40 × 18 mm plug adapter; 191 footprints, 175 JLC-assembled parts, 79 BOM lines, 6 hand-fit (the J6 OBD-II plug; J3, J5, JP1-3: pin headers). Not re-priced since round 1 ($30.60 per board then, for 152 parts); round 3 adds the CH340C (extended part fee) and about $1 of parts.
- **Power nets:** each of the 15 supply and battery-side nets is one connected piece (checked by graph, zones included).
- **Track lengths:**
  - 3V3_ESP: 149 mm before, 31 mm now.
  - 3V3: a solid plane on In2.
  - The ESP32 EN and STM32 NRST caps now sit at their pins. Both tracks are still 53–58 mm, because they run to the STM32, SWD and buttons.

## Not covered

- **Model fidelity.** No vendor SPICE models ran in ngspice; TI's are encrypted. The LM317L, LM339, LM74700 and buck are datasheet-parameter models, and the buck was analysed small-signal rather than switching.
- **Re-simulation.** The fixes above were checked by hand calculation. Only the K/L back-feed diode was re-simulated.
- **EMC.** No emissions or immunity testing.
- **Layout quality beyond DRC.** The board is still autorouted, so check the buck hot loop and the USB pair by eye.
