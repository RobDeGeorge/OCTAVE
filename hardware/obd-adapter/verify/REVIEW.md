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
| 17 | Risk | digital audit | On a blank board the ESP32 can't be powered for esptool: PB6 is held low and J3 has no 3V3 pin. | **Fixed:** the `ESP_ON` solder jumper (3V3 to the load switch's ON pin). |
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

## Result after the fixes

- **ERC:** 0 errors, 0 warnings.
- **DRC:** 0 copper errors, 0 unconnected, 0 schematic-parity issues, 0 courtyard overlaps. 101 silkscreen warnings.
- **STM32 pin check:** re-run on the new netlist, all 31 functions OK. PC3 (`DLC_SW_EN`) is a plain GPIO.
- **Board:** 169 footprints, 152 JLC-assembled parts, 73 BOM lines. All exist at JLC; parts cost about $30.60 per board, with 35 extended parts.
- **Track lengths:**
  - 3V3_ESP: 149 mm before, 32 mm now.
  - 3V3: plus a 5051 mm² pour on In2.
  - The ESP32 EN and STM32 NRST caps now sit at their pins. Both tracks are still 55–63 mm, because they run to the STM32, SWD and buttons.

## Not covered

- **Model fidelity.** No vendor SPICE models ran in ngspice; TI's are encrypted. The LM317L, LM339, LM74700 and buck are datasheet-parameter models, and the buck was analysed small-signal rather than switching.
- **Re-simulation.** The fixes above were checked by hand calculation. Only the K/L back-feed diode was re-simulated.
- **EMC.** No emissions or immunity testing.
- **Layout quality beyond DRC.** The board is still autorouted, so check the buck hot loop and the USB pair by eye.
