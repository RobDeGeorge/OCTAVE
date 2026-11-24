"""Project symbols and footprints of the LoRa pod (lib/OCTAVE.kicad_sym, lib/OCTAVE.pretty).

Pinouts from the datasheets, 2026-11-23: RAK4630 (RAKwireless datasheet, figure 1 and the pin
table), TPS7B82-Q1 (TI, DGN package), ASM330LHH (ST, table 2). TPS22918 is the adapter's definition.
"""

import os

import mklib

P, I, O, B, PI, PO, NC = "passive", "input", "output", "bidirectional", "power_in", "power_out", "no_connect"

_GPIO = {4: "P0.13", 5: "P0.14", 6: "P0.15", 7: "P0.16", 8: "P0.17", 9: "P0.19", 10: "P0.20", 11: "P0.21",
         12: "P0.10/NFC2", 13: "P0.09/NFC1", 23: "P0.24", 24: "P0.25", 25: "P1.01", 26: "P1.02", 27: "P1.03",
         28: "P1.04", 29: "P0.03", 30: "P0.02", 31: "P0.28", 32: "P0.29", 33: "P0.30", 34: "P0.26",
         39: "P0.31/AIN7", 40: "P0.05/AIN3", 41: "P0.04/AIN2"}


def symbols():
    import libdefs
    g = lambda n: (n, _GPIO[n], B)      # noqa: E731
    rak = mklib.ic("RAK4630", [
        (44, "VBAT_NRF", PI), (43, "VDD_NRF", PI), (20, "VBAT_SX", PI), (21, "VBAT_IO_SX", PI), (1, "VBUS", PI), None,
        (2, "USB-", B), (3, "USB+", B), None, (17, "~{RESET}", I), (18, "SWDCLK", I), (19, "SWDIO", B), None,
        (15, "RF_BT", P), (37, "RF_LoRa", P), None,
        (14, "GND", PI), (16, "GND", PI), (22, "GND", PI), (35, "GND", PI), (36, "GND", PI), (38, "GND", PI),
        (42, "GND", PI)],
        [g(4), g(5), g(6), g(7), g(8), g(9), g(10), g(11), g(12), g(13), None, g(23), g(24), g(25), g(26), g(27),
         g(28), g(29), g(30), g(31), g(32), g(33), g(34), None, g(39), g(40), g(41)],
        footprint="OCTAVE:RAK4630", desc="RAKwireless nRF52840 (BLE 5) + SX1262 (LoRa) module", width=25.4)
    # TI TPS7B82-Q1, DGN (HVSSOP-8 PowerPAD): 1 IN, 2 EN, 3 NC, 4-6 GND, 7 NC, 8 OUT, pad GND
    ldo = mklib.ic("TPS7B82_DGN", [(1, "IN", PI), (2, "EN", I), None, (3, "NC", NC), (7, "NC", NC)],
                   [(8, "OUT", PO), None, (4, "GND", PI), (5, "GND", PI), (6, "GND", PI), (9, "GND", PI)],
                   footprint="Package_SO:HVSSOP-8-1EP_3x3mm_P0.65mm_EP1.57x1.89mm",
                   desc="40 V 300 mA LDO, 2.7 uA quiescent, automotive")
    # ST ASM330LHH, LGA-14. I2C: CS high, SDO/SA0 is the address bit. Mode 1: SDx / SCx to GND or VDDIO.
    # Pins 10 / 11: connect to VDDIO, GND or leave open (left open here)
    asm = mklib.ic("ASM330LHH", [(8, "VDD", PI), (5, "VDDIO", PI), (12, "CS", I), (1, "SDO/SA0", I), (2, "SDx", P),
                                 (3, "SCx", P), (6, "GND", PI), (7, "GND", PI)],
                   [(13, "SCL", I), (14, "SDA", B), None, (4, "INT1", O), (9, "INT2", O), None,
                    (10, "NC", NC), (11, "NC", NC)],
                   footprint="Package_LGA:LGA-14_3x2.5mm_P0.5mm_LayoutBorder3x4y",
                   desc="Automotive 6-axis IMU, I2C / SPI")
    return [s for s in libdefs.SYMBOLS if str(s[1]).strip('"') == "TPS22918"] + [rak, ldo, asm]


def rak4630_footprint(path):
    """lib/OCTAVE.pretty/RAK4630.kicad_mod: 23 x 15 mm, 44 lands at 1.2 mm, centred on the module's edge.

    Land positions are those of the LCSC / EasyEDA footprint of C33033533 (they agree with the datasheet's
    figure 5); the numbering is the datasheet's figure 1, seen from above: 1-13 along the top from the
    right, 14-22 down the left, 23-35 along the bottom from the left, 36-44 up the right."""
    pads = [(1 + i, 8.2 - 1.2 * i, -7.5, 0.9, 1.3) for i in range(13)]
    pads += [(14 + i, -11.5, y, 1.3, 0.9) for i, y in enumerate((-5.41, -4.21, -1.81, -0.61, 0.59, 1.79, 2.99, 4.19, 5.39))]
    pads += [(23 + i, -8.7 + 1.2 * i, 7.5, 0.9, 1.3) for i in range(13)]
    pads += [(36 + i, 11.5, y, 1.3, 0.9) for i, y in enumerate((5.45, 4.25, 1.85, 0.65, -0.55, -1.75, -2.95, -4.15, -5.35))]
    font = "(effects (font (size 1 1) (thickness 0.15)))"

    def line(a, b, layer, w):
        return (f'\t(fp_line (start {a[0]} {a[1]}) (end {b[0]} {b[1]}) (stroke (width {w}) (type solid)) '
                f'(layer "{layer}"))\n')

    def rect(x, y, layer, w):
        c = [(-x, -y), (x, -y), (x, y), (-x, y)]
        return "".join(line(c[i], c[(i + 1) % 4], layer, w) for i in range(4))
    out = ('(footprint "RAK4630"\n\t(version 20241229)\n\t(generator "octave_gen")\n\t(layer "F.Cu")\n'
           '\t(descr "RAKwireless RAK4630 (nRF52840 + SX1262), 23 x 15 x 3 mm, 44 lands at 1.2 mm")\n'
           '\t(attr smd)\n'
           f'\t(fp_text reference "REF**" (at 0 -9.6) (layer "F.SilkS") {font})\n'
           f'\t(fp_text value "RAK4630" (at 0 9.6) (layer "F.Fab") {font})\n')
    out += rect(11.5, 7.5, "F.Fab", 0.1)
    out += rect(12.4, 8.4, "F.CrtYd", 0.05)
    for sx in (-1, 1):                  # corner marks outside the lands; pin 1 is top right
        for sy in (-1, 1):
            cx, cy = sx * 11.65, sy * 7.65
            out += line((cx, cy), (cx - sx * 1.6, cy), "F.SilkS", 0.15) + line((cx, cy), (cx, cy - sy * 1.2), "F.SilkS", 0.15)
    out += ('\t(fp_circle (center 9.4 -8.7) (end 9.6 -8.7) (stroke (width 0.2) (type solid)) (fill solid) '
            '(layer "F.SilkS"))\n')
    for n, x, y, w, h in pads:
        out += (f'\t(pad "{n}" smd rect (at {round(x, 3)} {round(y, 3)}) (size {w} {h}) '
                '(layers "F.Cu" "F.Paste" "F.Mask"))\n')
    out += ('\t(model "${KIPRJMOD}/lib/3dmodels/RAK4630_LCSC_C33033533.step"\n\t\t(offset (xyz 0 0 0))\n'
            '\t\t(scale (xyz 1 1 1))\n\t\t(rotate (xyz 0 0 0))\n\t)\n)\n')
    os.makedirs(path, exist_ok=True)
    with open(os.path.join(path, "RAK4630.kicad_mod"), "w") as f:
        f.write(out)
