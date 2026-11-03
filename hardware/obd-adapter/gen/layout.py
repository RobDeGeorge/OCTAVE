"""Placement for the Rev 0 board: 110 x 75 mm, 4 layers.

Explicit positions for the parts whose place matters (connectors at the
edges, the ESP32 antenna at the top edge, the buck converter's hot loop,
the MCU); everything else is packed into its block's region in netlist
order, so a part sits next to the parts it was declared with.
"""

import pcbnew

W, H = 110.0, 75.0
GAP = 0.5

# block -> region (x0, y0, x1, y1), board coordinates in mm, y down
REGIONS = {
    "iso": (15, 1, 49, 14),
    "dlcsw": (15, 14, 49, 19),
    "j1850": (15, 19, 49, 50),
    "car": (0, 0, 0, 0),
    "power": (1, 51, 49, 74),
    "can": (50, 55, 81, 74),
    "mcu": (50, 20, 81, 54),
    "flash": (50, 20, 81, 54),
    "ui": (50, 1, 81, 20),
    "esp32": (82, 27, 109, 42),
    "exp": (82, 42, 109, 60),
    "usb": (82, 60, 109, 74),
}

# ref -> (x, y, rotation); refs are stable because obd_adapter.py numbers parts in declaration order
FIXED = {}


def fixed_for(design):
    by_value = {}
    for p in design.parts:
        by_value.setdefault((p.block.name, p.value), []).append(p.ref)

    def ref(block, value, i=0):
        return by_value[(block, value)][i]

    f = {
        ref("car", "OBD (Micro-Fit 2x5)"): (9.6, 26.0, 90),
        ref("mcu", "STM32G474RET6"): (65.0, 37.0, 0),
        # MCU at (65, 37): 100n at VDD pins 16 / 32 / 48 / 64, VDDA (29) caps, crystal at PF0/PF1 (5/6)
        ref("mcu", "100n", 0): (57.0, 41.0, 90),
        ref("mcu", "100n", 1): (68.5, 45.0, 0),
        ref("mcu", "100n", 2): (73.0, 33.0, 90),
        ref("mcu", "100n", 3): (61.5, 29.0, 0),
        ref("mcu", "100n", 4): (66.0, 46.8, 0),
        ref("mcu", "1u"): (66.0, 48.6, 0),
        ref("mcu", "16MHz"): (55.2, 35.5, 90),
        ref("mcu", "10p", 0): (51.4, 33.4, 0),
        ref("mcu", "10p", 1): (51.4, 37.6, 0),
        ref("mcu", "100n", 5): (54.5, 39.5, 0),      # NRST (pin 7)
        ref("mcu", "10n"): (69.5, 47.7, 90),          # VDDA
        # QSPI flash with its decoupling cap at VCC (pin 8)
        ref("flash", "W25Q128JVSIQ"): (54.7, 25.4, 0),
        ref("flash", "100n"): (60.8, 24.0, 90),
        # ESP32 supply: load switch + caps in the strip left of the module, 3V3 pin 2 at (90.75, 9.0)
        ref("esp32", "100n"): (88.5, 8.4, 90),
        ref("esp32", "22u"): (85.6, 9.4, 90),
        ref("esp32", "1u", 0): (88.5, 11.6, 90),      # EN (pin 3) RC
        ref("esp32", "10k", 0): (88.5, 14.8, 90),
        ref("esp32", "TPS22918"): (84.8, 16.0, 0),
        ref("esp32", "100k"): (88.5, 18.0, 90),
        ref("esp32", "1u", 1): (84.8, 19.6, 0),       # TPS22918 VIN
        ref("esp32", "1n"): (84.8, 21.8, 0),
        ref("esp32", "ESP_ON"): (88.5, 21.8, 90),
        ref("esp32", "ESP32-WROOM-32E-N8"): (99.5, 13.0, 0),
        ref("usb", "USB-C"): (96.0, 71.2, 0),
        ref("ui", "SWD"): (54.5, 5.0, 0),
        # buck hot loop: VIN caps -> IC -> catch diode / inductor -> output caps
        ref("power", "TPS54360BDDAR"): (30.0, 63.0, 0),
        ref("power", "2.2u", 0): (24.0, 59.5, 90),
        ref("power", "2.2u", 1): (24.0, 66.5, 90),
        ref("power", "B560C"): (36.5, 69.5, 0),
        ref("power", "8.2u"): (39.0, 60.5, 0),
        ref("power", "47u", 0): (45.5, 58.5, 90),
        ref("power", "47u", 1): (45.5, 64.5, 90),
        ref("power", "47u", 2): (45.5, 70.5, 90),
        # 3.3 V LDO (SOT-223) below the MCU: the power region has no room left for it
        ref("power", "AP2114H-3.3"): (54.4, 48.2, 0),
    }
    return f


def courtyard(fp):
    cy = fp.GetCourtyard(pcbnew.F_CrtYd)
    if cy.OutlineCount():
        bb = cy.BBox()
    else:
        bb = fp.GetBoundingBox(False)
    return (pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetY()), pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom()))


def _overlap(a, b):
    return not (a[2] + GAP <= b[0] or b[2] + GAP <= a[0] or a[3] + GAP <= b[1] or b[3] + GAP <= a[1])


def _find(region, w, h, occupied):
    y = region[1]
    while y + h <= region[3]:
        x = region[0]
        while x + w <= region[2]:
            rect = (x, y, x + w, y + h)
            if not any(_overlap(rect, o) for o in occupied):
                return rect
            x += 0.5
        y += 0.5
    return None


def place_all(board, design, fps):
    from pcbgen import place
    fixed = fixed_for(design)
    occupied = []
    for r, (x, y, rot) in fixed.items():
        place(fps[r], x, y, rot)
        occupied.append(courtyard(fps[r]))
    # ESP32 antenna keepout: the module courtyard covers x 75-124 above y 6.68
    occupied.append((75.0, 0.0, W, 6.8))
    for b in design.blocks:
        region = REGIONS[b.name]
        for p in b.parts:
            if p.ref in fixed or p.ref not in fps:
                continue
            fp = fps[p.ref]
            spot = None
            for rot in (0, 90):
                place(fp, 0, 0, rot)
                cx0, cy0, cx1, cy1 = courtyard(fp)
                w, h = cx1 - cx0, cy1 - cy0
                spot = _find(region, w, h, occupied)
                if spot:
                    break
            if spot is None:
                place(fp, 0, 0, 0)
                cx0, cy0, cx1, cy1 = courtyard(fp)
                w, h = cx1 - cx0, cy1 - cy0
                spot = _find((1, 1, W - 1, H - 1), w, h, occupied)
                print(f"layout: {p.ref} ({p.value}) overflowed region {b.name}")
            if spot is None:
                raise RuntimeError(f"{p.ref} ({p.value}) does not fit anywhere")
            place(fp, spot[0] - cx0, spot[1] - cy0, fp.GetOrientationDegrees())
            occupied.append(spot)
