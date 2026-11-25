# OCTAVE OBD adapter, Rev 0 (bring-up board)

Our own OBD-II adapter: STM32G474 (vehicle side) + ESP32 (Bluetooth Classic SPP, BLE, Wi-Fi), every OBD-II bus, USB-C. Rev 0 is a bring-up board for the bench and the Jeep, not the final dongle. It has room to probe, test points and header access to spare pins, and it plugs straight into the car's OBD-II port. Plan and background: `TODO/diy-obd-adapter.md`.

**Status:** generated, then verified: an STM32 pin check against ST's pin data, two independent datasheet audits and ngspice simulations. Every finding is fixed or accepted, see `verify/REVIEW.md`. Not yet reviewed by a human, not ordered. Read "Before ordering" below.

**Compact two-sided board, after four design reviews (2026-11-24 and 25): routed, order files in `fab/`, not reviewed by a person, not ordered.** The board is **80 x 55 mm, six layers, parts on both sides** (74 on top, 144 underneath, 218 in all; 205 fitted by JLCPCB): 86 % of the area of the first compact pass (89 x 57.5 mm) and 60 % of the one-sided 100 x 70 mm board with its pod-bus lobe.

- **Routed and rule-checked:** 0 unconnected pads, 0 errors, schematic and board agree (ERC 0 / 0). Eight DRC warnings are left: silkscreen outlines over the board edge (the USB-C socket's twice, the module's once), U4's footprint differing from the library copy on purpose (1), and four vias KiCad calls dangling (each joins tracks of one layer; the clean-up step leaves any whose removal would open a net). `gen/routing.json` is this route and `build.py route` replays it.
- **Six layers** (F.Cu signal, In1 GND, In2 signal, In3 signal, In4 3V3, B.Cu signal). 292 signal vias of 0.45 / 0.2 mm, 305 of 0.6 / 0.3 mm. **Six layers and two-sided assembly cost more than Rev 0 would have; neither is priced.**
- **0402 for the logic-level resistors and small capacitors** (`HV_NETS` in `gen/obd_adapter.py` says which nets keep 0603: everything that can see the battery or a bus line, the buck's own parts, the 0.1 % divider, 4.7 µF and 10 µF). 0402 was estimated to get the board to 80 x 50 mm; it gets it to 80 x 55, because the chips, diodes, transistors and connectors are what fill the back, and 0402 shrinks none of them.
- **One USB-C port for both chips:** a hub (U18 CH334P, Y2), the CH340C (U14) with its auto-reset pair on the hub's second port; one RGB LED (D1) in place of three; the four ESP32 programming pads are gone.
- **Changed by the design review of 2026-11-24** (three reviews: circuit, copper, manufacturing; what each found is in the next section).
- **`fab/` is this board** (`build.py fab`, 2026-11-25): gerbers, BOM, placement file for both sides, and the plug adapter's gerbers with its anchoring holes. The placement file's angles are checked against JLCPCB's own footprints for all but the inductor (see "What an order still needs"); the route of 2026-11-25 (after the fourth review's changes) is itself not reviewed.
- **Not re-verified:** `verify/REVIEW.md`, the pin check and the simulations describe the Rev 0 layout and netlist. Nothing added on 2026-11-24 is simulated.
- **Sections below "Blocks"** partly still describe the Rev 0 layout: "Before ordering" and "Known Rev 0 limits" have not been rewritten for the compact board.

## Design review of 2026-11-24

Three independent reviews of the routed board (circuit against datasheets; the copper, measured; manufacturing and the plug joint). What they found and what was done:

**Fixed in the netlist**
- **USB ESD array's clamp pin (U2 pin 5) is on 3V3, not VBUS.** On VBUS, the hub's D+ pull-up fed the cable-less VBUS through the array: about 1 mA while parked, and enough on the ESP32 load switch's ON pin to turn the ESP32 on in a warm car.
- **Pod supply switch: U19 TPS27S100A** (HTSSOP-14) replaces the discrete P-FET switch (Q12, Q13, R71, D29 are gone; C52 and R72 have new jobs). It limits at 0.82 A (R72 3k; 0.59-1.05 A over tolerance and temperature), shuts down hot and retries. The P-FET would have burned in a shorted pod cable before the polyfuse tripped. **A shorted cable is survived, not free:** U19 then cycles at its thermal limit and draws about 0.2 A from the battery for as long as it is on, under the polyfuse's hold current, and its fault flag is not wired (no pin left). Firmware must switch `POD_EN` off when the pods stop answering and when the car is parked. U19 is rated 40 V, 48 V for 0.4 s.
- **Input TVS: 5.0SMDJ33CA** (it was 26CA). The 26 V part breaks down at 28.9-31.9 V, so it conducted through the whole of a clamped load dump (35 V, up to 0.4 s). The 33 V part starts at 36.7 V and clamps higher: about 42 V at 10 A and 46 V at 43 A at 25 °C, an estimated 49 to 50 V hot. That is at the rating of the 50 V capacitors and 1 to 2 V over U19's 48 V transient rating in the worst pulse in a hot car; accepted. **The LM317L (U11) is behind its own 30 V clamp** (Q15 BCX56-16 in SOT-89 as an emitter follower, D31 30 V zener, R80 1k): a firmware rule could not have protected it from a pulse that rises in a microsecond. The follower costs 0.7 to 1.5 V: Bus+ falls under VPW's 6.25 V when the plug is under about 9.0 V (10.8 V at worst, cold); without it 8.2 / 9.3 V. With Bus+ shorted while the adapter transmits, the LM317L's current limit flows through Q15, about 1 W: the SOT-89 takes that for a while, and **firmware must stop transmitting when `VPW_RX` does not echo the bus going high.** The comparator supply's resistor R43 is 1k (it was 470 Ω), so its 33 V zener stays inside its rating at the 41 V the TVS can let by for 0.4 s.
- **Q14: CONFIG switch 4 (BOOT0) also holds the ESP32 in reset,** and R78 holds the ESP32's TX line idle meanwhile. A USB cable powers the ESP32 by itself now, and one byte from it while the STM32's ROM bootloader picks its interface kept DFU from enumerating.
- **D30, a 5.1 V zener on the panic input:** the ESD diode alone lets PC13 reach 5 to 7 V with the cable shorted to the battery. R66 is 2k in 1206: 4.5 mA into the zener at 14.4 V (5.36 V at most), about 5.6 V at a 28 V jump start.
- **10 µF and 100 nF at the hub** (C55, C58): its low-voltage reset is 3.15 V at worst (datasheet V2.91: 2.6 / 3.0 / 3.15 V), the LDO 3.25 V before a load step, so the static margin is 100 mV; 3.15 V is also the hub's lowest operating voltage. **Check on the first board with Wi-Fi transmitting.** If the hub resets, it needs a small regulator of its own from 5V (its V5 pin cannot take 5V directly: on USB power that rail is 4.0 to 4.6 V, under the pin's 4.5 V minimum).

**Fixed in the copper** (`gen/prewire.py`, `gen/pcbgen.py`, `gen/build.py`)
- **`VBAT_P` leaves the ideal-diode FET on a 1.0 mm trunk** to the buck, the 10 µF and the 47 µF, with a via for the branches. Everything behind the FET had hung on the LM74700's 0.4 mm sense track and one via.
- **No fan-out via in a pad** (there were 20, under the STM32's pins and the accelerometer among them), none in a signal pin's way out of a fine-pitch chip, none in the USB-C shell's slot. The hub's centre pad has four vias in it on purpose, and about a dozen vias of a pad's own net stand 0.05 to 0.09 mm beside it (the buck's hand-laid ground vias, the module's ground pad, a few others). **Order the board with filled and capped vias.**
- **Beside the plug's hand-soldered pads:** no via and no pour within 0.45 mm of the pads on either side, no pour under the PTC, the PTC's battery pad 1.2 mm from the pads (0.65 before), 0.6 mm clearance round the unfused `VBAT_IN` copper (0.3 before).
- **Each CAN transceiver's 5 V capacitor is wired straight to its VCC pin;** the pod supply runs 0.6 mm on the outer layers (0.3 mm on an inner layer before); four ground vias under the hub; three to four more ground vias at the plug's ground pins, the TVS and the 47 µF.
- **No battery-side net on the top layer under the ESP32 module** (measured on this route: none).
- **The high-speed USB pair is laid by hand** (`gen/prewire.py`): socket, ESD array, hub, on B.Cu but for 2 mm at the socket, 21.5 and 22.0 mm long, three vias in all, the two lines 0.2 to 0.6 mm apart. Its impedance is not designed (0.15 mm tracks over the 3V3 plane, about 100 Ω differential by estimate). The STM32 is on the hub's port 3 now, so the upstream pins have their side of the chip alone.
- **Both CAN transceivers have their own 5 V feed** from the OR diode's anode, down In2 beside the feedback-sense trace, to a via at each one's 5 V capacitor. (The J1850 pull-up and reference divider, under 1 mA together, may still join the sense trace.)
- **The plug's two ground pins have four vias** on a short bus beside them, laid by hand; the LM317L's input capacitor is wired straight to its input pin; the pod switch's exposed pad has six vias into the ground plane; VBUS may use the inner layers (kept to the outer ones it ran 145 mm round the board); the pod output is 0.6 mm on the outer layers and the pod input 0.6 mm throughout (36 mm of it on an inner layer, where the finisher put it); the ESP32's supply pin has a via straight down to its 100 nF.
- **Track ends are welded** onto the track they join: the finisher had left 16 joints held only by overlapping round ends, narrower than a track.
- **The plug adapter board has two plated 0.45 mm holes in each joint pad,** at the end where pulling the plug out starts to peel it. (The main board's pads cannot have them: pin n and pin n + 8 are back to back.)

**Left as it is, on purpose or for now**
- **The joint still needs epoxy or a case that clamps the plug and the board.** The fillets were estimated to start lifting near 42 N; a J1962 plug takes 20 to 90 N to pull. Never pull the adapter out by its board. Treat the bead of epoxy along the joint as required.
- **The four buttons stand 5.7 mm apart:** a fingertip presses two. That is the price of the 80 mm length.
- **The battery-side clearance of 0.3 mm does not apply to `VJ1850` (7.2 V, regulated) and `K_IN` (behind 10k):** with it, the corner where the J1850 regulator stands over the comparator could not be wired.
- **The RGB LED's green and blue may be swapped:** its datasheet drawing has pin 3 green and pin 4 blue, as wired; JLCPCB's symbol for the same part number has them the other way round. A firmware swap if so.
- **The hub's D+ pull-up is always on** (the CH334P has no cable-detect pin): a cable to a powered-off host back-drives its D+ with up to 1.8 mA while parked.
- **The 3.3 V LDO** dissipates about 0.6 W with Wi-Fi sustained; in a hot car it may reach its thermal shutdown, which resets the board. Measure its tab on the first board.
- **The STM32 cannot tell that a USB cable is in** (no pin left): firmware polls, or stays attached at 0.1 mA from the battery.
- **Silkscreen outlines stand closer than 0.15 mm to their own part's pads** in about 300 places (the library's drawings); JLCPCB trims them.

## Design review of 2026-11-25 (fourth round)

Three more independent reviews (circuit, the copper of the route of that morning, the order package). No blocker in the circuit or the copper; one in the placement file. What was done:

- **Q15's placement angle was 180 degrees out** and its reel (C549696) had 4 pieces: it is the same part on the other reel now (C100026, BCX56-16,115) with its own correction. Every other part number's angle was confirmed against JLCPCB's footprint, J11 moved 0.25 mm.
- **The DLC switch is in the corner behind the buck's input,** under the copper it switches from: one via up into `VBAT_P`. It stood under the ESP32 module, and `VBAT_P` and `DLC_SW` crossed the board to it and back: 95 and 112 mm of battery-side track under the reset button, the STM32 and its crystal. The wizard has its old place.
- **Q15 has copper:** four vias in its collector tab to a patch on the top side (about 0.25 cm2, all there is room for). On the bare pad it was good for 0.5 W; the fault it has to stand (Bus+ shorted while transmitting) is 0.4 W typical, 1.4 W at worst. **Still a firmware obligation:** stop transmitting when `VPW_RX` does not echo.
- **The K-line runs from the plug's pin to its TVS first** (it was last on the line, 34 mm on, behind the driver).
- **The plug's two ground pins reach their vias on 2 mm stubs** (0.5 mm before: the narrowest point of the supply's return and the TVS's).
- **The ESP32's 22 µF is wired to the module's supply pin** with a second via (it was joined through a header pin, 17 mm away by copper); four more vias in the LDO's tab.
- **R67 100k** (was 10k): with the panic cable shorted to the battery the zener's 5.4 V fed 0.2 mA into the parked 3.3 V rail, about what the rail's loads take. **C58 100 nF on `VPW_REF`.** R30 is 0603.
- **Firmware:** the four ESP32 lines with no resistor to a rail are in analog mode until the module is on (`firmware/src/safe_pins.c`).

**Looked at and left**
- **CAN termination switch:** each CAN_L has a 36 to 40 mm branch to the CONFIG switch, on In2 under the buck. At 500 kbit/s that is a few pF on a 60 Ω bus, under a ground plane; moving the termination would cost the switch.
- **Unsuppressed load dump** (87 V, 0.5 Ω, 0.4 s) is not survivable: the TVS fails short and the PTC opens. The board is sized for the suppressed pulse (35 V), which is what cars of the last decades give.
- **Q1 (60 V) has 0 to 5 V of margin on ISO pulse 1** since the TVS went to 33 V (it is avalanche-rated, the source is 10 Ω). **Y2 is a 20 pF crystal on about 11 pF** (the hub's pins have 16 pF each inside): about +70 ppm, USB allows 500. `5V_BUCK` is 5.48 V nominal against the CAN transceivers' 5.5 V recommended maximum.
- **K, L and Bus- sinks have no current limit** (as in the ELM and STN reference circuits): firmware releases K when `KLINE_RX` does not read low within 100 µs. K-line reception is undefined under about 8 V at the plug (comparator headroom).
- **The catch diode is 6 mm from the buck's switch pin;** the 5 V rail passes 0.3 mm from the crystal's pad under the STM32; the pour has six islands held by one via each. First-board measurements, not changes.
- **Parked current** is 0.59 mA typical at 25 °C; expect 1.3 to 1.5 mA in a hot car.

## What an order still needs

- **A panel.** Two-sided assembly is JLCPCB's Standard PCBA, whose smallest board or panel is 70 x 70 mm; the board is 81.3 x 55. Rails can only go on the top and bottom edges (the tab is on the left, the module overhangs the right by 6.2 mm), and 5 mm rails give 65 mm: **6 mm rails on a 2 mm routed gap with mouse-bite tabs** (71 x 81.3 mm), tabs kept away from J2, J4, J9, J10, J11 and the buck's output capacitors (x 23 to 31 on the top edge), three 1 mm fiducials and tooling holes on the rails. No V-cut: the pours come to 0.3 mm from the edge and the connectors sit flush with it. JLCPCB can add the rails itself; say so in the remarks.
- **On the order:** 6 layers, 1.6 mm, **filled and capped vias** (the hub's paste openings sit on its four vias; the LDO's tab, U19's pad and Q15's tab have vias in them too), In1 = GND plane and In4 = 3V3 plane in the remarks, bottom side reflowed first, "confirm parts placement" ticked. The 0.2 mm vias are JLCPCB's small-via option.
- **The placement file is checked against JLCPCB's own footprints** for 94 of the 95 part numbers (fourth review, 2026-11-25); the inductor L1 (C96978) could not be fetched and is not polarised. `fab/obd-adapter-rev0-rotation-check.txt` lists what is left. **Look in the preview at Q15 first** (its origin correction, -0.65 mm, is an estimate), then L1, C22 and one SOT-23 on the back.
- **Stock on order day:** STM32G474RET6 (C521608) was at 35 pieces on 2026-11-25; TPS27S100A 743. JLCPCB marks the DIP switch (C40736) and the CH340C (C84681) as no longer made, with stock.
- **The USB-C socket's four shell stakes get no paste** in KiCad's footprint: check they are soldered, or do it by hand.
- **CH340C is listed for -20 to +70 °C.** In a parked car in winter or summer the ESP32's USB serial port may not work; nothing else depends on it.

**History: the one-sided 100 x 70 mm board was routed** (2026-11-10): 0 DRC errors, 0 unconnected pads, schematic and board agree. `gen/routing.json`, `obd-adapter.kicad_pcb`, `drc.rpt` and everything in `fab/` are that board, with the changes of 2026-11-09 in it (the buck loop, the parked-watch parts, the microSD, the mounting holes, `verify/REVIEW.md` rounds 5 to 7; round 9 moved C23 to the buck's VIN pin). The route is the autorouter's, finished by hand round the STM32; it has passed the rule check, not a person's eye. Later the same day (`verify/REVIEW.md` round 8): the crystal's tracks and the CAN1 pair were re-laid by hand, and six BOM lines moved to parts from JLCPCB's no-fee libraries, which changed four values round the buck (RT 160k, feedback 68k / 12k, 47 pF on COMP).

## Files

| Path | What |
|---|---|
| `gen/obd_adapter.py` | **The design.** Every part, its LCSC number and its pin-to-net map. Edit this, not the KiCad files. |
| `gen/parts.py` | LCSC part numbers, checked against JLCPCB's parts library on 2026-11-02, 2026-11-09 and 2026-11-10. |
| `gen/layout.py` | Board placement, both sides: fixed positions for the parts whose position is the design (`fixed_for`), each circuit as a block of rows on the side it names (`TILES`), the MCU's decoupling at its pins (`near_rules`), and the areas kept clear for labels (`RESERVED`). |
| `gen/silk.py` | The silkscreen that says what things are, on both sides: each block's name and outline, function names, pinouts. See "Silkscreen". |
| `gen/wizard.txt`, `gen/art.json`, `gen/mkart.py` | The picture and the quote on the back: see "Silkscreen". |
| `gen/libdefs.py` | Project symbols KiCad doesn't ship (SOIC-8 N-MOSFET, TPS22918, CH340C, LIS2DH12), with the pinouts checked against the datasheets, and the two plug footprints (drawn by `gen/mklib.py`). |
| `gen/build.py` | Regenerates everything (see below). |
| `gen/raster.py` | The finisher after the autorouter: joins what is left open on a raster of the signal layers, with rip-up; also the router for small patches. See "Before ordering", the routing notes. |
| `gen/congestion.py` | Routing-density estimate for a placed board, before routing it (`OBD_SIGNAL_LAYERS`, default 4). |
| `obd-adapter.kicad_sch` / `.kicad_pcb` | Generated KiCad 10 project. Open it in KiCad to review. |
| `gen/plugprint.py` | Draws the 1:1 print of the plug's adapter board from its `.kicad_pcb` (run by `build.py fab`). |
| `fab/` | Generated: Gerber zips, JLCPCB BOM + CPL, hand-fit list, and `obd-adapter-plug-rev0-print-1to1.pdf`, the sheet to try a bought plug on. |
| `firmware/` | Bring-up firmware for the STM32 (bare-metal C, CMake + `arm-none-eabi-gcc`): reset-safe pins, clock, a serial console, battery voltage, I2C scan, CAN listen-only, K-line. Builds for the board and for a NUCLEO-G474RE; **not yet run on hardware**. See `firmware/README.md`. |
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

The schematic and the board are generated from the same netlist, and `kicad-cli pcb drc --schematic-parity` checks that they agree. The schematic is eight pages, all drawn like a reference schematic (`gen/drawn.py`: parts placed and wired by hand in Python, one function per page), with `obd-adapter-schematic.pdf` as the copy to hand to a reader (`kicad-cli sch export pdf`, after `build.py sch`). Supply rails carry power symbols (on every pin, connectors included) and every other net a global label with the board's net name, so a net is everything that carries the same name. After a label, `[3,6]` lists the other pages its net is on; a name with no list is a node of that page only. The big chips have a label on each signal pin, and their small circuits (crystal, decoupling, reset, pull-ups, the USB pair, the auto-reset transistors) are wired. The STM32 page carries a "Firmware must" list, a short form of the obligations in `verify/REVIEW.md`. The two small pages (car plug and CAN, K-line) are A4, the rest A3 (`PAPER_OF` in `gen/drawn.py`). `build.py sch` checks each drawing against the netlist (a wire on the wrong pin or an unnamed net stops it) and stops if any two pieces of text touch. **Do not hand-edit the generated files**: the next build overwrites them. The exceptions are copper and labels: after routing by hand in KiCad, run `build.py freeze` so the next build replays your tracks, and copy any reference-label moves into `LABELS` in `gen/layout.py`.

## Blocks

**MCU: STM32G474RET6.** A 16 MHz crystal (for FDCAN timing), SWD on a 2x5 1.27 mm header, RESET / USER buttons, one RGB LED (D1, common anode on 3V3: the three pins sink it through 1k / 330 / 330 Ω, so they are **active low**) and a 4-way `CONFIG` DIP switch (SW6): 1 ESP_ON, 2 / 3 the CAN1 / CAN2 120 Ω terminations, 4 BOOT0. All off in the car. VDDA / VREF+ are on 3V3 with their own 100 nF + 1 µF (no ferrite bead: the ADC only reads the battery divider). The USER button (PA15) and `KLINE_RX` (PC0) have no pull-up resistor; firmware uses the pins' own.

**Power:** J1962 pin 16 → 2 A 60 V PTC (2920L200/60) → 5.0SMDJ33CA → LM74700 ideal diode + AO4264E → `VBAT_P` (with 47 µF bulk).
- `VBAT_P` feeds a TPS54360B buck (60 V input; the datasheet's 600 kHz design with the output set to 5.33 V), then an SS34 diode-OR with USB VBUS (`5V` ≈ 4.9 V), then an AP2114H 3.3 V LDO (SOT-223).
- The buck's EN divider is not fitted, so the internal UVLO keeps it running through a 6 V cold crank. A cycle-by-cycle simulation (`verify/sim/sim_buck_sw.py`, `verify/REVIEW.md` round 8) has `5V_BUCK` unmoved through that crank, regulating down to 5.6 V at the plug, with under 9 mV of ripple from 9 to 42 V and 131 mV of dip on a 0.1 to 1.0 A load step.
- **To check on the first board:** the buck's SW node with 6 to 7 V on the plug and 0.5 to 1 A drawn from 5 V. Alternating pulse widths would mean the part's internal slope compensation (not published) is short for 8.2 µH at that duty cycle; a 10 or 15 µH inductor in the same footprint is the fix (`verify/REVIEW.md` #71).
- The ESP32 has its own TPS22918 load switch, placed under the module at its 3V3 pin. Its ON pin follows `ESP_PWR_EN` (PB6, off at reset) through a diode (D26), against the switch's own 100k pull-down. CONFIG switch 1 (`ESP_ON`) also powers it without STM32 firmware, by pulling the PB6 net to 3V3 through 1k (R70); turn it off again for normal use. Firmware that drives PB6 low with the switch left on wastes 3.3 mA and the ESP32 stays off.
- R69 (1k from `VBUS` to GND) is back with the USB hub: plugging in a cable turns the ESP32's load switch on through a diode (D27), and the 1k keeps the OR diode's leakage from doing the same while parked (`verify/REVIEW.md` #46). The USB ESD array's clamp pin is on 3V3 for the same reason (see the design review above).
- The ideal diode (U6 LM74700 driving Q1, an AO4264E 60 V FET) blocks a reversed battery and holds `VBAT_P` up through a negative pulse, for about 20 mV where a diode costs 0.3 V. A B560C Schottky in its place was tried on 2026-11-09 (three parts and 80 µA of parked current less) and taken out again: with the battery reversed its leakage pulled `VBAT_P` to −0.7 V at 100 °C, past the buck's −0.3 V rating, and it took 0.3 V off the cranking headroom (`verify/REVIEW.md` #54, `verify/sim/sim_input_d29.py`). The battery-voltage divider is ahead of it, on `VBAT_F`.
- The TVS is bidirectional on purpose. A unidirectional one would conduct into a reversed battery and defeat the ideal diode.
- Everything on `VBAT_P` must survive the TVS's clamp, 47 V at worst (49 to 50 V hot). The LM317L does not, and stands behind its own 30 V follower clamp (Q15, D31, R80).

**DLC_SW:** a switched battery rail (SI2309CDS P-MOSFET high side, `DLC_SW_EN` = PC3). It feeds the J1850 supply, the LM2901 (through 1k and a 33 V zener) and the K/L 510 Ω pull-ups (through 1N4148W, so the car's own pull-up cannot back-feed the rail while it is off). ScanTool's STN datasheets warn that pull-ups left on the battery draw sleep current and can disturb the bus, up to stalling an engine.

**J1850 VPW / PWM:** STN-style discrete circuit, adapted from the STN1110/1170/21xx reference schematics (3.3 V parts like ours).
- **Supply:** LM317L with 240 / 680 / 470 Ω gives 7.24 V for VPW. A 2N7002 on `J1850_VSEL` (PB2) shorts the 470 Ω for 4.79 V (PWM).
- **Bus+:** 2N7002 → AO3401A → Schottky, with a 10k + Schottky bus load.
- **Bus−:** MMBT3904 sink with a 10k + Schottky pull-up to `5V_BUCK`.
- **Receivers:** LM2901 on `CMP_VCC`, with 1N4148W input clamps. VPW compares Bus+ against 3.86 V (18k / 47k from `5V_BUCK`, the middle of the 3.5–4.25 V receive window). PWM compares Bus+ against Bus−. Both are active high, with 10k pull-ups to 3.3 V.
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
- **UART0 (IO1 / IO3) is the ESP32's own USB serial port**: a CH340C (U14, on the hub's port 2, powered from `3V3_ESP`) with the usual two-transistor DTR / RTS auto-reset, so `esptool` flashes the module through the one USB-C port with no firmware on the STM32 and no button pressed. Plugging the cable in powers the ESP32 by itself (see R69 above). 1k in series with the CH340C's TXD.
- `ESP EN` (SW4, EN) and `BOOT` (SW5, IO0) buttons, side by side: hold BOOT, tap EN for the ROM bootloader by hand. The STM32 can do the same (PB7 resets, PB5 straps IO0).
- **ESP IO header J8** (1x10, not fitted): IO14 27 26 25 33 32 35 34 + 3V3_ESP / GND, above the module. 3.3 V logic, IO34 / 35 input-only. Powered from 3V3_ESP, so anything on it turns off with the ESP32.
- **microSD J10** (Molex 104031-0811, on the bottom edge, card sticks out about 3 mm so it can be pulled) in SPI mode: MISO IO4, CLK IO5, MOSI IO21, CS IO22, card detect IO23 (switch to GND, internal pull-up). 10k pull-ups (RN1 + R68) on CS / MOSI / MISO / DAT1 / DAT2. Powered from 3V3_ESP. It is the adapter's copy of the phone's traffic stop recordings (`TODO/traffic-stop-mode.md`). IO5 is a boot strap that only needs to be high at reset (its internal pull-up; the card doesn't drive CLK).
- The module sits centred on the board's right end with its antenna section hanging off the edge (Espressif's preferred placement), so there is no copper keepout on the board.

**USB-C (J2), one port for both chips:** device (5.1k on CC1 / CC2), USBLC6 ESD, then a USB 2.0 hub (U18, WCH CH334P, QFN-16, with its own 12 MHz crystal Y2). Port 3 is the STM32 (PA11 / PA12: the ROM's DFU bootloader, and a CDC serial port once firmware has one), port 2 the CH340C on the ESP32's UART0. A computer sees both at once, and neither needs firmware on the other. The hub runs from 3V3 (both its supply pins, as its datasheet allows for a hub on the board): it is powered whenever the two chips behind it are, and sleeps at 0.12 mA typical (0.4 mA at most) with no cable in. That is added to the parked current. The pair from J2 to the hub is high-speed (480 Mbit/s); keep it short and together. On USB power alone `5V` is about 4.6 V and `5V_BUCK` is off, so the CAN transceivers, the VPW threshold and the Bus− pull-up have no supply: **CAN and J1850 need 12 V on the plug**, also on the bench.

**Flashing a new board over USB only:** one cable. STM32: CONFIG switch 4 (BOOT0) on, tap RESET, flash with STM32CubeProgrammer or `dfu-util`, switch 4 off again (a blank chip starts the bootloader by itself). Switch 4 also holds the ESP32 in reset (Q14), so it cannot talk on USART2 while the STM32's bootloader is choosing its interface. ESP32: switch 4 off, then `esptool --port /dev/ttyUSB0 ...` (or `ttyACM`, as the CH340C enumerates).

**Pod bus (J11, bottom edge, between J5 and the Qwiic socket):** the controller end of the four-wire pod chain (`../pod-bus/README.md`): JST GH, pin 1 `POD_VIN`, 2 RS-485 A, 3 B, 4 GND. Its circuit is on the back beside it: the transceiver (`RS-485`) and the supply switch (`POD PWR`).
- Supply: `VBAT_P` -> F2 (0.5 A polyfuse, 60 V, under the 47 µF at the input chain, so the run across the board is fused) -> U19 (TPS27S100A: current limit 0.82 A, thermal shutdown with retry) -> pin 1. PD2 high turns it on (through 4.7k); 100k holds it off through reset. A pod cable shorted to ground is survived for any length of time. The adapter cannot measure the rail; U19's fault flag is not connected. `POD_F` is a 0.6 mm track on the outer layers.
- Data: U17 THVD2450 (bus pins survive ±70 V) on **USART1, shared with J5's console pins**: PC4 drives DI, the receiver reaches PC5 through 1k (R76), so a USB serial adapter on J5 pin 6 wins over it. PA5 is DE (100k down), PC10 is /RE (100k up): the transceiver is shut down through reset and with blank firmware. PC10 is the pin the bootloader may drive as USART3 TX; on /RE that only switches the receiver on or off.
- R77, 120 Ω, is fitted across A / B: the adapter is always one end of the chain.
- A listening transceiver draws 1.8 mA: firmware must shut it down (/RE high, DE low) and decide about `POD_VIN` when the car is parked.

**Storage, expansion:** 16 MB W25Q128 on QUADSPI1 (store-and-forward); Qwiic I2C (PA8 / PA9); J5, a 2x7 header (not fitted) on the bottom edge with 3V3, 5V, USART1, I2C, PANIC_BTN and the spare GPIO (PA5, PA10, PC10, PD2).

**Parked watch and evidence** (for `TODO/traffic-stop-mode.md` and `TODO/octave-companion-app.md`):
- **U15 LIS2DH12** accelerometer on the STM32 I2C bus (0x19, SA0 high). INT1 → PC14, INT2 → PC15 (EXTI, wakes the STM32 from Stop 1). Put every event that must wake the chip on INT1: PC15 shares EXTI line 15 with the USER button (PA15), and a line takes one pin. Wake-on-motion while parked (tow / break-in, someone getting in), crash spikes while driving. A plug in the OBD port is a poor mount for measurement, fine for "something moved".
- **U16 ATECC608B** secure element on the same bus (0x60): a key that never leaves the chip, to sign the frozen OBD speed window. Needs a one-time, irreversible configuration lock in firmware.
- **J9 panic button** (JST SH 2-pin, bottom edge) for a hidden button on a cable: 2k (1206) in series at the connector, then the ESD9B3.3 and a 5.1 V zener (D30), a 100k pull-up and 100n → `PANIC_BTN` = PC13 (WKUP2: wakes the STM32 even from Standby). Closed = low. The diode is behind the resistor so that a cable shorted to +12 V puts about 4.5 mA into it, not the battery. (The pull-up is 100k so that the zener's 5.4 V cannot push more into a parked 3.3 V rail than the rail's loads take.)

**Car connector:** an SAE J1962 male plug sticking straight out of the board's left end, with the board in the middle of the plug (like an ELM327 dongle). The plug sits on a small adapter board that stands across the board's edge (board 80 x 55 mm plus a 1.3 mm tab):

```
      side view                          top view
  ┌──────────┐┃                       ┌──────┐┏━━━━━━━━━━━━━━━┓
  │   plug   │┃════════ main board    │ plug │┃  main board   ┃
  └──────────┘┃                       └──────┘┗━━━━━━━━━━━━━━━┛
           adapter
```

- **Part:** Shenzhen Jiatel OBD-II-16M-12V, part no. CR-PG001 ("OBD-II 16 Pin Straight Male Connector", jiatelcn.com; drawing `OBDII-Male-16pin.pdf` rev A). Generic "OBD2 16-pin male straight" plugs elsewhere look the same, but only this one has a drawing, so buy it or check another against its numbers. Not at LCSC, so it's hand-fit.
- **Adapter board** (`obd-adapter-plug.kicad_pcb`, generated by `build.py plug`; gerbers `fab/obd-adapter-plug-rev0-gerbers.zip`, order it with the main board): 40 x 18 x 1.6 mm, 2 layers. The plug goes on its front (footprint `lib/OCTAVE.pretty/J1962_Plug_Adapter`): 2 x 8 contacts, 4.00 mm pitch, rows 8.60 mm apart; the contacts are 0.8 x 1.8 mm blades whose tails stand 4.0 mm out of the back, so 2.6 x 1.3 mm plated slots in 3.2 x 2.0 mm pads. Between the rows is a 36 x 2.0 mm slot for the main board's tab (a 1.6 mm board may be 1.76 mm thick before copper and solder), and on the back one joint pad per pin runs from the slot's edge over the pin. The silkscreen says which way round the plug goes: on the front the shroud's D outline, `WIDE SIDE 1-8` / `LATCH SIDE 9-16` and the end pins' numbers, on the back which row meets which side of the main board.
- **Main board end** (J6, footprint `J1962_Edge_Joint`): a 33.8 mm tab, 1.3 mm deep, on the left edge (0.3 mm short of the adapter's front face, so it never reaches the plug), and 16 pads 3.0 x 2.5 mm along the edge, pins 9–16 on the top side and 1–8 on the bottom, centred on the edge so the plug sits square on the board's end. Pin 16 (battery) is the bottom pad of the top row; a 1.6 mm trace runs from it up a lane beside the pads into F1. The silkscreen has the end pins' numbers on top (9 and 16) and `+12V` beside pin 16; underneath, every pad has its signal and that of the pad behind it (`gen/silk.py`). The tab carries no copper: `prewire.TAB_KEEPOUT` keeps every router off it and off the first 0.3 mm behind the adapter, and `build.py route` prints any track that ends up there.
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

**No mounting holes.** The compact board hangs from the plug; a case has to hold it by its edges. (Rev 0's one-sided layout had four M3 holes.)

## MCU pin plan

| Function | Pins |
|---|---|
| HSE 16 MHz | PF0 / PF1 |
| USB FS | PA11 / PA12 |
| SWD | PA13 / PA14, NRST |
| ESP32 link (USART2 + RTS/CTS, to the module's UART2 on IO16 / IO17 / IO18 / IO19) | PA2 TX, PA3 RX, PA0 CTS, PA1 RTS |
| ESP32 control | PB5 IO0, PB6 power, PB7 EN |
| QSPI flash | PB11 NCS, PB10 CLK, PB1 IO0, PB0 IO1, PA7 IO2, PA6 IO3 |
| HS-CAN (FDCAN2) | PB12 RX, PB13 TX, PB14 STB |
| MS-CAN (FDCAN3) | PB3 RX, PB4 TX, PB15 STB |
| J1850 (TIM3) | PC6 VPW RX, PC7 PWM RX, PC8 Bus+ TX, PC9 Bus− TX, PB2 V select |
| K / L line | PC0 RX, PC1 TX (LPUART1), PC2 L TX |
| DLC_SW enable | PC3 (not PC10: the bootloader may drive USART3 TX) |
| Battery voltage | PA4 (ADC2_IN17): 100k / 10k 0.1 %, 36 V full scale |
| I2C (Qwiic, U15 0x19, U16 0x60) | PA8 SDA, PA9 SCL |
| Expansion UART (USART1), also the pod bus | PC4 TX, PC5 RX (through 1k from U17) |
| Pod bus control (also on J5) | PA5 DE, PC10 /RE, PD2 pod supply on |
| Spare to header (J5) | PA10 |
| Panic button (J9) | PC13 (WKUP2), also on J5 pin 9 |
| Accelerometer interrupts | PC14 INT1, PC15 INT2 |
| RGB LED (active low) / button | PB9 R, PC11 G, PC12 B, PA15 USER |
| BOOT0 | PB8 (10k pull-down + CONFIG switch 4) |

**Why not FDCAN1:** its pins clash with USB (PA11 / PA12) and with BOOT0 (PB8). A CAN transceiver's idle-high RXD on PB8 would start the chip in the bootloader.

**Dead-battery pull-downs:** PB6 carries a 5.1k UCPD dead-battery pull-down until firmware sets `PWR_CR3.UCPD_DBDIS` (PA9 enables it, and PA9 is I2C SCL, pulled up). It is safe low at reset: the ESP32 power switch is off, and CONFIG switch 1 still turns it on against the 5.1k. PB4's (CAN2 TXD) is off: PA10, which enables it, has a 10k pull-down (`verify/REVIEW.md` #34).

## Before ordering

1. **Review the schematic in KiCad.** ERC is clean, the board matches it, and `verify/REVIEW.md` lists what the audits and sims checked, but no human has reviewed it yet. Most important:
   - The J1850 and K-line front ends.
   - The ideal-diode and buck values.
   - The plug's pin numbering (`lib/OCTAVE.pretty/J1962_Plug_Adapter` and `J1962_Edge_Joint`: pin 1 top right on the plug's mating face, 9–16 on the main board's top side).
2. **Check the footprints marked in `gen/obd_adapter.py`:**
   - The Sunlord SWPA8065S inductor uses the Changjiang FNR8065S land. The audit found it matches KiCad's own Sunlord 8x8 land; its saturation current is 4.2 A (`verify/REVIEW.md` #24), against a simulated peak of 2.2 A.
   - **The plug against a real one.** Buy the plug first and try it on `fab/obd-adapter-plug-rev0-print-1to1.pdf`, a 1:1 print of the adapter board (US Letter or A4, printed at 100 %; measure its 50 mm bar before trusting it). The sheet lists six checks. In short: check the 0.8 × 1.8 mm blade tails go into the 2.6 × 1.3 mm slots on the 4.00 × 8.60 mm grid, that pin 1 lands on the rounded-square pad and the plug's D shape matches the outline on the silkscreen, and that the plug's flange sits flat on the adapter with the main board's tab in the slot. The tab is 1.3 mm deep and should end inside the slot, 0.3 mm below the adapter's front face (0.14 mm on an adapter at the thin end of its tolerance): the recess in the back of the plug looks narrower than the tab (about 31.7 mm scaled off the drawing, against 33.8 mm), so the tab must not reach it.
3. **Check part rotations in JLCPCB's placement preview.** KiCad and JLC disagree on the zero rotation of some packages (SOT-23, SOIC, the USB-C).
4. **Recheck stock on order day.** Every BOM line was in stock on 2026-11-10. The STM32G474RET6 had 289 (112 on 2026-11-09; 20, then 186, on 2026-11-02). Fallback: STM32G474RBT6 (128 KB flash), C529413. Low stock elsewhere: the 8.2 µH inductor (631), AP2114H (676), ATECC608B (783), LM2901 (1.7k), TCAN1044V (1.9k), the 33 V zener (2.9k), 5.0SMDJ26CA (3.9k).
5. **Confirm the J1850 PWM high-level limit** (5.25 V assumed, `verify/REVIEW.md` #10 and #32) against SAE J1850 if you have it.
6. Through-hole parts (the OBD-II plug, pin headers) need JLC's through-hole option or hand soldering; see `fab/*-hand-fit.txt`. Order the adapter board (`fab/obd-adapter-plug-rev0-gerbers.zip`, 2 layers, 1.6 mm) in the same cart; the main board must be 1.6 mm too, or the tab won't fit its slot.
7. **Look at the copper in KiCad.** The route passes DRC with schematic parity (0 errors, 0 unconnected, 0 parity issues, 0 courtyard overlaps), but DRC does not judge quality. Worth a look:
   - The buck's switch node and input loop (hand-laid in `gen/prewire.py`). C23 (100 nF) sits at U7's VIN pin since `verify/REVIEW.md` #72; the 2.2 µF pair is 6 mm away.
   - The USB pairs. J2's run 52 / 48 mm with 2 vias each, J7's 30 / 24 mm with 5 and 2 vias. Both are full speed (12 Mbit/s), where a few millimetres of skew does not matter.
   - The plug's edge pads and the copper behind them.
   - The crystal's tracks (`HSE_IN` 8.7 mm, `HSE_OUT` 9.0 mm, both on the top side with no vias since round 8).
   - The nets the router made a meal of: `J1850_VSEL` (115 mm, 14 vias), `ESP_RTS` (6 vias) and `EXP_PC10` (105 mm round the ESP32 to the header). All legal, and all slow enough not to care.
   - The CAN1 pair. From the plug to the transceiver (U9) both lines run about 26 mm; `CAN1_L` crosses to the bottom once (2 vias), because the battery trace and C19 close the top side to it. The totals (`CAN1_L` 94 mm with 9 vias, `CAN1_H` 52 mm with 4) are mostly the branches to the termination switch (SW6) and to the ESD diode D8, which hangs on a 16 mm stub past U9 where CAN2's D9 sits in line. A stub that short is harmless at CAN FD rates; Rev A should put D8 between the plug and U9 anyway.
8. **The 5 DRC warnings that remain** are cosmetic: the outlines of the two USB-C sockets run off the board edge (4), and U4's footprint differs from the library copy (the tented ground pad, on purpose). To move labels, do it in KiCad and copy the positions into `LABELS` in `gen/layout.py`, or the next `build.py pcb` puts them back.

**How the board is routed** (`build.py route`):

- **Frozen.** The routing is frozen in `gen/routing.json`, and `route` replays it, so every rebuild gives the same board. It refuses to run if a part has moved since the freeze. `build.py freeze` captures a routed (or hand-edited) board.
- **How the compact board was routed (2026-11-24).** `OBD_REROUTE=1 python3 build.py route`, about 40 minutes, then `build.py finish` once more:
  1. The power stage is wired by hand (`gen/prewire.py`), with the hub's centre-pad via and the accelerometer's supply and ground stubs.
  2. Every SMD GND pad gets a via to the In1 plane, and every 3V3 pad a via to the In4 plane. The MCU's VSS / VSSA pins join one via under the chip. All of that is locked.
  3. Freerouting 2.5 routes the rest on F.Cu, In2, In3 and B.Cu, in up to four runs of 12 passes each (`OBD_PASSES`). It left 10 connections open. Supply nets get wider classes (`WIDTHS`) and stay on the outer layers; battery-side nets keep 0.30 mm from everything (`HV`).
  4. `gen/raster.py` finishes (`build.py finish` runs this step alone on a routed board): it joins each open net on a 0.05 mm raster of the four signal layers; where there is no way round the autorouter's copper it goes through it, takes that copper out and joins the nets it cut again; then it takes out whatever the autorouter left closer than the rules allow and joins that again; then stubs and copper islands go. The first run reported every net whole while DRC still found four open; a second `finish` closed them.
  5. GND stitching vias and the outer-layer GND pours.
- **What did not work, so nobody tries it again.** Rows of small parts 0.2 mm apart (0.8 mm between pad rows): no via fits between them, and the route ended with pads walled in that nothing could reach; they are 0.5 mm apart now. A decoupling capacitor placed straight under the STM32 pins next to its own: its two plane vias walled in pins 25 / 26, which no router could then reach (the 1 µF sits further in now, and the LDO 1.8 mm below the chip). The grid router `gen/maze.py` on six layers: over four minutes a net, and it closed nothing the raster finisher had left; it is no longer in the flow. With 0603 parts, an 80 x 52 mm outline; with 0402, 80 x 50 and 80 x 54: the parts fit by area, the blocks with their names and the label areas on the back do not.
- **Placement is not sticky any more.** Rev 0's `build.py pcb` put a part back where `routing.json` had it; the compact layout places everything from `gen/layout.py` alone (`fixed_for`, `TILES`, `near_rules`). Moving a part means a new route: `OBD_REROUTE=patch OBD_ROUTER=raster` keeps the frozen routing and re-routes round the parts that moved (`OBD_RIP=x0,y0,x1,y1` lifts old copper in a box first).
- **Freerouting is not deterministic,** and neither is the order the finisher works in when it has to try several: a fresh route gives a different board. The frozen one is the board.
- **Battery-side clearance.** `obd-adapter.kicad_dru` (generated) makes KiCad's DRC and interactive router enforce the battery-side clearance too.

## Silkscreen

`gen/silk.py` (run by `build.py pcb`) says what things are. Both sides carry parts on the compact board, so both follow the same rules.

- **Circuits are named and fenced off.** Each circuit is a block (`TILES` in `gen/layout.py`): its chip first, then its parts in rows. A block has its name over its first row and its outline round it, on whichever side it sits; two blocks side by side share the line between them, and outlines are squared up (`_square` in `gen/silk.py`): edges of neighbouring blocks within 2.4 mm of each other move out onto one line, so a row of blocks ends on one edge. Only the lines move; no box grows over another block, the wizard or a reserved label area, or to where parts would hide the line. On the back the rows are laid so that they read left to right with the board turned over. What is not a block is named beside its own chip (`FRONT` in `gen/silk.py`: `5V BUCK`, `12V IN`, `STM32`, `3V3 LDO`, `FLASH`, `SD CARD`) and fenced with `BORDERS`. Every line is drawn only where no pad, part or label is in the way, so the fences are broken where parts stand close.
- **Small parts carry no reference** (R, C, D, Q, L, F, Y, RN); every chip shows its own. A block's chips have their references wherever the block has clear room nearest the chip: in the title strip beside the name, or beside the chip in its row. Which resistor is which is in the schematic, the board file and the placement file.
- **The parts a person touches carry their function where their reference was:** the buttons (`RESET`, `USER`, `ESP EN`, `BOOT`: the last two read together), the `CONFIG` switch with its four positions and `OFF IN CAR` spelled out under it, the connectors (`USB`, `PANIC`, `POD BUS`, `QWIIC`, `ESP IO`), and on the back the test points, each name at its own pad: `VBAT 5V 3V3 GND DLC` in a row under J8, `J+ J- VJ` beside the J1850 driver, `K` and `L` in the K-line and L-line blocks.
- **Pinouts.** On the front: J5's as a table over J5 (`EXP (J5)`, three columns), and the signal of each J8 pin under it, with a `1` at pin 1. On the back: the signal of each of the plug's 16 pins beside its pad (pin n and, under it, pin n + 8 on the other side; `MSH` / `MSL` / `HSH` / `HSL` are the two CAN pairs).
- **The board's name and revision** are on the front (reading up, between the Qwiic socket and the card socket) and in the back's top corner, where `JLCJLCJLCJLC` reads up the edge: JLCPCB prints its order number there when the order says "Specify a location".
- **The wizard and the quote are on the back.** The wizard (`gen/wizard.txt`) takes the clear patch under the module (84 % of its earlier size: the DLC switch took its old place behind the buck). The quote (`gen/art.json`) is placed a word at a time among the blocks: each line reads left to right and steps down as it goes, the lines one under the other, every word in the clear spot nearest its place that keeps the reading order. The build says at what size it fitted, or that it left either out.

Text is 1.0 mm high with a 0.15 mm stroke at the smallest, on both boards: JLCPCB's stated minimums (it was 0.8 / 0.12 until 2026-11-17). The reference labels and the footprints' own texts follow the same rule, and the project's DRC enforces it. The parts' outlines are KiCad's library ones, 0.12 mm wide. To change a label edit the tables in `gen/silk.py`; `build.py pcb` lists every label that touches a pad, a part, the board edge or other silkscreen. Run `build.py pcb` and `build.py route` as two commands: in one process the board is reloaded without the project's rules and the pours are filled 0.5 mm from the edge, not 0.3.

## Firmware and the bench

`firmware/` is where the STM32's firmware starts: a bring-up skeleton, not an ELM327 replacement. It applies the "Firmware obligations" of `verify/REVIEW.md` as a pin table (`firmware/src/safe_pins.c`), takes its pin map from `gen/obd_adapter.py` (`firmware/tools/gen_pins.py`, checked by its tests), and never transmits on CAN. It compiles without warnings for two targets and its host tests pass; nothing has run on a chip yet.

- **Before the boards arrive:** a NUCLEO-G474RE (same chip, ST-LINK on board) runs the `nucleo` target, so the clock, console, ADC, I2C and CAN-listen code can be proven first. `can listen` on it needs a 3.3 V CAN transceiver breakout.
- **On Rev 0 the console is USART1 on J5** (pin 5 TX, pin 6 RX, 3.3 V), and J5 is not fitted. Until the firmware has a USB serial port on J2 (its next step), solder J5 or three wires and use a 3.3 V USB serial adapter.
- **The car side on the bench:** an OBD-II female breakout or extension cable into the plug, and 12 V on pin 16: CAN and J1850 have no supply on USB power alone.

## Parked current

The board stays on with the engine off (J1962 pin 16 is unswitched battery): the STM32 sleeps and watches the accelerometer, the panic button and the buses. Estimate from datasheet typical values at 12.6 V, 25 °C, with the ESP32 and `DLC_SW` off, both CAN transceivers in standby and the STM32 in Stop 1. The buck's Eco-mode switching is assumed 75 % efficient. **Measure it on the first board.**

| Load | Rail | Current |
|---|---|---|
| LM74700 ideal-diode controller | battery | 80 µA |
| Battery-voltage divider (100k / 10k) | battery | 115 µA |
| TPS54360B, not switching | battery | 146 µA |
| AP2114H LDO ground current | 5 V | 65 µA |
| Buck feedback divider (68k / 12k) | 5 V | 67 µA |
| VPW threshold divider (18k / 47k) | 5 V | 82 µA |
| STM32G474 Stop 1 (ST's figure, RTC off) | 3.3 V | ~80 µA |
| Two TCAN1044V in standby (VIO) | 3.3 V | 17 µA |
| W25Q128 standby, LIS2DH12 at 10 Hz, ATECC608B asleep, TPS22918 off | 3.3 V | ~14 µA |
| CH334P hub asleep, no cable (typical; 0.4 mA at most) | 3.3 V | 120 µA |
| **Total from the battery** | | **≈ 0.60 mA (0.43 Ah / 30 days)** |

- The VPW threshold divider was 2.7k / 10k (420 µA, ~0.72 mA in total), then 13k / 47k. It is 18k / 47k now: 82 µA and a 3.86 V threshold on the 5.33 V rail. The earlier ratios gave 4.17–4.19 V (`verify/REVIEW.md` #45).
- STM32 Standby instead of Stop 1, and the flash in power-down, would only save about 0.05 mA. Stop 1 keeps RAM and wakes on any EXTI pin, so the accelerometer can stay on PC14.
- With the ESP32 on: about 2 mA from the battery while it advertises over BLE (~3 mA average at 3.3 V; the LDO is linear, so 3.3 V current costs about half that from 12.6 V), about 70 mA while Wi-Fi receives the phone's recordings.
- For scale: cheap ELM327 dongles draw 10–60 mA all the time; a car's own parked drain is typically 20–50 mA.
- Further cuts if ever needed: a 1M / 100k battery divider with a 100 nF cap at the ADC pin (−0.1 mA), a lower-Iq LDO, or an STM32-controlled shutdown of the buck (EN) with a separate always-on micro-power rail.

## Known Rev 0 limits

- No J1850 wave shaping (see above).
- Parked current is about 0.60 mA from the battery (see "Parked current"). The buck, the ideal-diode controller, the battery-voltage divider and the LDO set that floor, not the MCUs.
- The J1850 Bus− pull-up (10k from `5V_BUCK` through a B5819W) stays on while parked, so pin 10 sees 5 V / 10k on every car. PWM nodes pull Bus− up anyway and VPW / CAN cars leave pin 10 open, but Rev A should switch it with `DLC_SW` like the K / L pull-ups.
- No GM single-wire CAN, no DoIP.

## Rev A notes

Part-count ideas from the review of 2026-11-09 that were not done on Rev 0 (`verify/REVIEW.md` #54 has the ones that were):

- (Done on the compact board: the ESP32's own USB-C socket is gone; a hub puts both chips on J2, see "USB-C" under "Blocks".)
- **One dual CAN transceiver** (TI TCAN1046V) for the two TCAN1044V: not checked against JLCPCB's stock.
- **The J1850 receivers' pull-ups** (R47, R50) stay: PC6 / PC7 are the ROM bootloader's I2C4 pins and must not float during DFU. On a board that moves those two nets to other pins, the STM32's own pull-ups would do.
- Resistor arrays and dual diodes would cut a few more parts but save no money and make probing harder. (The three LEDs became one RGB LED on 2026-11-24.)

## Sources

- ELM327 datasheet rev J, Fig. 9 (J1850 / ISO front ends).
- ScanTool STN1110 / STN1170 / STN21xx datasheets and their reference schematics (the circuits used here).
- OpenDiag rev 1.1 KiCad sources (`maximus64/opendiag-hardware`).
- Motorola AN1731 and the Philips AU5780 / AU5783 datasheets (J1850 VPW electrical limits).
- TI datasheets: TPS54360B, LM317L, TPS22918, TCAN1044V.
