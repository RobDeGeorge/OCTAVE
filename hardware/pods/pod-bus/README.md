# OCTAVE pod bus: what a compatible pod needs

This is the wired link of the OCTAVE pod standard (`../README.md`, which has the rules every pod keeps to). Wired pods are small boards (a LoRa beacon, a temperature probe, an accelerometer...) daisy-chained on one four-wire cable back to the OBD adapter. This sheet is the contract: a board that follows it can be plugged anywhere in the chain.

**Status (2026-11-24): draft 0.1.** The electrical rules below are built into two boards, neither ordered: the LoRa pod (`../lora-pod/`) and the OBD adapter, which has the controller's end inside its outline, J11 on its bottom edge (`../../obd-adapter/README.md`, "Pod bus"; that board was reworked on 2026-11-24 and has no fab files yet). The protocol is an outline, not a design. Nothing here has been measured; every current and length is an estimate until a chain has run.

## The chain

```
OBD adapter ── cable ── pod ── cable ── pod ── cable ── pod (END switch on)
 (controller,
  terminated)
```

- One line, no branches. The adapter is one end, the last pod the other.
- The adapter is the controller: it switches the pods' supply and decides who talks.
- A pod with only two wires connected (supply and ground) must still work alone. That is the standalone case: a plain OBD power lead, no adapter.

## Connector and cable

| | |
|---|---|
| Connector | JST GH, 4 pins, 1.25 mm, on the board: `SM04B-GHS-TB` (side entry, LCSC C189895) or `BM04B-GHS-TBT` (top entry) |
| Per pod | **Two**, wired pin for pin on the board. Either one is in, the other out |
| Cable | Four wires, pin 1 to pin 1, a `GHR-04V-S` housing at each end. Twist wires 2 and 3 together |

| Pin | Net | |
|---|---|---|
| 1 | `VIN` | The car's battery, switched and fused by the adapter |
| 2 | `A` | RS-485, non-inverting |
| 3 | `B` | RS-485, inverting |
| 4 | `GND` | |

The connector's contacts are good for about 1 A (from memory of JST's rating: check it). The chain is fused below that, see "Supply".

## Supply rules

1. **Run from 6 to 16 V** on `VIN`. That covers a cranking dip and a charging alternator.
2. **Survive, without damage:** 42 V for a load dump (the adapter's clamp voltage), 24 V continuously (a jump start), and a reversed lead at -16 V.
3. **Pass `VIN` straight through** from one connector to the other, ahead of the pod's own fuse, on copper good for 1 A (0.5 mm or wider).
4. **Fuse your own load** after that junction (a polyfuse sized for the pod, 0.5 A at most), so a fault in one pod does not take the chain down.
5. **Block reverse current** (a series Schottky or an ideal diode) so a pod never feeds the chain.
6. **At most 22 µF** on `VIN` ahead of the pod's regulator, so that switching the chain on does not trip the adapter's fuse.
7. **Regulate on the pod.** A linear regulator is fine up to about 30 mA; above that use a switching one (at 14 V a linear 3.3 V regulator burns 10.7 V times the current).
8. **No battery in a pod** that sits in the sun. A supercap if something must be held.

**Budgets (proposed, to be confirmed by measurement):**

| | Per pod | Whole chain |
|---|---|---|
| Running | declare it; 100 mA average at most without asking | 0.5 A, the adapter's fuse |
| Parked (key out) | 200 µA or less | about 1 mA |

OBD pin 16 is live with the key out, so the parked figure is what protects the car's battery. A pod that cannot meet it must switch its own loads off when the adapter says the car is parked, or when it sees the battery at rest with no alternator charging it (standalone).

## Data rules

1. **RS-485, half duplex, one pair.** A 3.3 V transceiver with:
   - bus pins that survive a short to `VIN` (**±42 V at least**). The reference part is TI THVD2450 (±70 V, LCSC C2672621). Plain MAX3485 / SP3485 types are **not** enough: pin 1 of the same cable carries the battery;
   - a receiver that reads "idle" on an open or undriven bus (failsafe), so no pod fits bias resistors;
   - 1/8 unit load or less.
2. **Driver enable (DE) and receiver enable (RE, active low) on separate MCU pins.** 100k from DE to ground and from RE to 3.3 V: a pod in reset, or with blank firmware, is off the bus and its transceiver is shut down.
3. **A 100k pull-up on the UART RX pin**, which floats while the receiver is off.
4. **A 120 Ω terminator behind a switch**, marked `END` on the board. On at the last pod only. The adapter's terminator is fixed.
5. **No clamp below 42 V on A or B.** The usual RS-485 TVS (SM712) conducts at 13 V and would burn on a pinched cable. The transceiver's own ESD protection is the protection.
6. **Never drive the bus unless addressed**, except as the protocol allows (below).

## Protocol (outline only: to be designed with the first two pods)

- UART framing on the pair, 115200 baud 8N1 to start with.
- The adapter polls. A pod answers only when asked, inside a fixed time.
- **Addresses without switches:** every pod has a unique serial number (its MCU's factory ID). After power-up the adapter runs a discovery round, gives each pod a short address and learns what kind of pod it is. Pods may be chained in any order.
- Every frame carries a length, the address, a type, and a CRC-16.
- **Parked:** the bus sleeps and every transceiver is shut down (a listening transceiver draws 1.8 mA, far over the parked budget). How a pod raises an event then (the adapter listening in short windows, or the pod's own radio) is not decided.
- What the adapter tells every pod: ignition on / off, battery voltage, vehicle speed, time.

## Board rules

- Parts rated -40 to 85 °C or better; a dash top in the sun passes 80 °C.
- A way to load firmware without the bus: SWD pads or USB.
- One LED the firmware can blink (the adapter's "identify this pod" command).
- The UART behind the transceiver on test pads or a header, for a USB-UART dongle on the bench.
- Label both bus connectors the same (`POD BUS`) and the switch `END`.

## Checklist for a new pod

- [ ] Two JST GH 4-pin connectors, pin for pin, `VIN` passed through ahead of the pod's fuse
- [ ] Runs from 6 to 16 V, survives 42 V and a reversed lead
- [ ] Own polyfuse, reverse-current blocking, 22 µF or less on `VIN`
- [ ] Fault-protected RS-485 transceiver (±42 V or more), DE and RE on separate pins with their 100k resistors
- [ ] 100k pull-up on UART RX
- [ ] 120 Ω behind an `END` switch
- [ ] No TVS on A / B
- [ ] Parked current 200 µA or less, measured
- [ ] Firmware can be loaded without the bus; an LED to identify the pod

## Reference design

`../lora-pod/gen/lora_pod.py`, the block at the top ("Pod bus: ...") and the one at the end: J1 / J7, F1, D1, D2, U1, U7 and SW3. Copy those nets into a new pod's netlist.

## Open points

- The adapter's end is a fuse and a switch, nothing more: no current limit below the 0.5 A polyfuse, and no way to measure the pod rail. On the adapter the bus shares USART1 with the J5 console.
- The 42 V figure is the adapter's clamp. A pod on a plain OBD lead has no adapter in front of it and needs its own clamp (the LoRa pod's D1).
- Maximum chain length and pod count: not worked out. RS-485 at this speed is good for far more cable than a car has; the limit will be the supply's voltage drop and the 0.5 A fuse.
- A pinched cable that shorts `VIN` to A or B with the terminator on puts the battery across 120 Ω whenever a driver pulls the other line low. The LoRa pod's resistor is a 0.25 W part; whether that is enough is untested.
