# OCTAVE pods

A pod is a small board that adds one thing to OCTAVE (a control, a sensor, a radio) and reaches it over one of the standard links below. Pods may differ in everything else: processor, shape, supply, what they do. This sheet is the minimum every pod keeps to.

**Status (2026-11-25): draft 0.1, a proposal.** Two pods exist as designs, neither ordered, neither with firmware. The rules marked *(proposed)* are built into no board or firmware yet. See "Open points".

Not pods: the OBD adapter (`../obd-adapter/`), which is the hub the wired pods hang from, and the head unit itself.

## The pods

| Pod | Folder | Link | What it adds | State |
|---|---|---|---|---|
| LoRa pod | `lora-pod/` | Wired | The parked car's position over LoRa, GPS, "moved while parked" | Routed, no fab files, no firmware |
| Volume knob | `volume-knob/` | Radio | A battery volume knob with an LED ring | Routed, fab files, no firmware |

## The links

A pod has at least one. It may have other radios or sockets of its own (the LoRa pod's LoRa and Bluetooth, for example); those are the pod's business, not a link to OCTAVE.

| Link | Path to OCTAVE | Spec | State |
|---|---|---|---|
| **Wired** | Pod bus (four wires: battery and RS-485) to the OBD adapter, which passes it on | `pod-bus/README.md` | Electrical rules written and built into the LoRa pod and the adapter. Protocol is an outline |
| **Radio** | ESP-NOW to the ESP32-S3 USB dongle on the head unit | None yet | The app's side exists for the knob only (`src/managers/esp32volumemanager.cpp`: `+N` / `-N` / `M` lines in; volume, mute and colour out). No dongle or pod firmware in this repository |
| **USB** *(proposed)* | A USB serial port on the head unit | None yet | Nothing uses it. Every pod so far has USB for firmware, so it costs no parts |

## Minimum spec

Every pod, whatever its link:

1. **One standard link** from the table above, following that link's own sheet.
2. **Says what it is** *(proposed)*. On request, over its link, a pod gives four things: its kind, its hardware revision, its firmware version and a unique serial number (the processor's factory ID). OCTAVE lists and tells pods apart by these, so no pod needs an address switch.
3. **Quiet until spoken for.** A pod in reset, or with blank firmware, does not drive or jam its link.
4. **Firmware loads without OCTAVE:** USB or debug pads on the board.
5. **One light the firmware can blink,** for "which pod is this one".
6. **Says what it draws with the key out.** A wired pod: 200 µA or less (the pod bus's figure). A battery pod: how long its cell lasts, and it must not need the car to stay awake.
7. **Takes a parked car's heat.** Parts rated -40 to 85 °C or better. No lithium-ion or lithium-polymer cell. A pod that needs stored energy uses a cell made for the heat (the knob's lithium titanate) or a supercap.
8. **Compact and plain.** A simple outline, nothing sticking out past it, and its size given in its README.
9. **Marked:** `OCTAVE`, the pod's name and `REV n` in the silkscreen; every connector and switch named.
10. **Kept the same way in this repository:** `hardware/pods/<name>/`, the design in `gen/` as code (edit that, not the KiCad files), and a `README.md` that opens with what the pod is, its link and an honest status line.

A wired pod also meets the whole of `pod-bus/README.md`, which has its own checklist.

## How the two pods stand

Read from their READMEs, not checked on the boards.

| Rule | LoRa pod | Volume knob |
|---|---|---|
| 1 Link | Wired, built in | Radio |
| 2 Says what it is | No firmware | No firmware |
| 3 Quiet until spoken for | Yes: the transceiver is shut down at reset | Not looked at |
| 4 Firmware without OCTAVE | USB-C and debug pads | USB-C |
| 5 A light | Two LEDs | The LED ring |
| 6 Key-out draw | Not measured | Sleep current is designed for, not measured |
| 7 Heat | Not checked here | "80 °C or more": below the 85 °C in this sheet |
| 8 Compact and plain | 95 x 46 mm rectangle; the USB socket overhangs the edge | 47 mm round |
| 9 Marked | Not checked here | Not checked here |
| 10 Repository | Yes | Yes |

## Checklist for a new pod

- [ ] Which link, and that link's sheet followed
- [ ] Answers "what are you": kind, hardware revision, firmware version, serial number
- [ ] Off its link while in reset or unprogrammed
- [ ] Firmware can be loaded with no OCTAVE present
- [ ] A light the firmware can blink
- [ ] Key-out current (or cell life) stated, then measured
- [ ] Every part -40 to 85 °C or better; no lithium-ion cell
- [ ] Plain outline, size in the README
- [ ] `OCTAVE`, name and revision in the silkscreen
- [ ] `hardware/pods/<name>/` with `gen/` and a README

## Open points

- **The "what are you" message** is the one rule that makes pods a family to the app, and it does not exist: its format has to be the same on every link, and the app has nothing that lists pods yet.
- **The radio link has no sheet.** The knob's lines are volume only. A second radio pod needs pairing, the identity message and a way for the dongle to tell pods apart written down first.
- **USB as a link** is proposed here and used by nothing.
- **Cells.** The pod bus sheet says "no battery in a pod that sits in the sun"; this sheet allows a heat-rated cell, for the knob. Both stand until one is chosen.
- **85 °C.** The knob's README says its parts are rated 80 °C or more. Either the knob is an exception (it lives in the cabin's shade, in the hand) or the rule is 80 °C.
- **One generator.** The LoRa pod builds with the OBD adapter's generator modules; the knob has its own copy. A third pod should not make a third.
