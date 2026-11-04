"""Placement for the Rev 0 board: 110 x 75 mm, 4 layers.

Three kinds of placement, in this order:

1. FIXED (`fixed_for`): explicit positions for the parts whose position is the design: the
   connectors at the edges, the ESP32 antenna at the top edge, the MCU and its crystal, the
   input-protection chain right below the car connector, and the 5 V buck laid out as a unit
   (input caps at VIN, boot cap across BOOT / SW, inductor and catch diode at SW, output caps
   on the inductor, compensation and feedback at their pins).
2. NEAR (`near_rules`): every decoupling cap, clamp, ESD part and series resistor goes to the
   free spot nearest the pin(s) it serves, as soon as those pins are placed.
3. Everything else is packed into its block's region in netlist order.
"""

import math

import pcbnew

W, H = 110.0, 75.0
GAP = 0.25

# block -> region (x0, y0, x1, y1), board coordinates in mm, y down
REGIONS = {
    "iso": (22, 1, 49, 13),
    "dlcsw": (22, 13, 49, 19),
    "j1850": (22, 19, 49, 51),
    "car": (0, 0, 0, 0),
    "power": (1, 52, 49, 74),
    "can": (50, 55, 81, 74),
    "mcu": (50, 20, 81, 54),
    "flash": (50, 20, 81, 54),
    "ui": (50, 1, 81, 20),
    "esp32": (82, 27, 109, 42),
    "exp": (82, 42, 109, 60),
    "usb": (82, 60, 109, 74),
    "espusb": (89.2, 27, 100.8, 58),     # J3 / JP1 to J7 under the ESP32 module, down beside J5
}

# ESP32 antenna: the module courtyard covers x 75-124 above y 6.68
KEEPOUTS = [(75.0, 0.0, W, 6.8)]
# copper beside the LDO's tab (U8 at (54.4, 48.2)): heat spreading + vias into the In2 3V3 plane
LDO_TAB = (56.4, 45.0, 60.2, 51.4)

# reference labels placed by hand in KiCad (absolute x, y in mm, text angle): applied after placement
LABELS = {
    "C24": (16.0, 66.0, 180), "C25": (16.0, 63.5, 180), "C27": (39.0, 67.5, 0), "C28": (35.5, 69.0, 0),
    "R20": (33.0, 71.2, 90), "R21": (28.5, 71.2, 270),
}

# the buck converter, laid out around U7 at (BX, BY)
BX, BY = 27.0, 66.5


def _refs(design):
    by_value = {}
    for p in design.parts:
        by_value.setdefault((p.block.name, p.value), []).append(p.ref)

    def ref(block, value, i=0):
        return by_value[(block, value)][i]
    return ref


def fixed_for(design):
    ref = _refs(design)
    X, Y = BX, BY
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
        # QSPI flash under the MCU, next to its QSPI pins (PA6 / PA7 / PB0 / PB1 / PB10 on the bottom edge)
        ref("flash", "W25Q128JVSIQ"): (65.1, 55.3, 0),
        # ESP32 supply: load switch + caps in the strip left of the module, 3V3 pin 2 at (90.75, 9.0)
        ref("esp32", "ESP32-WROOM-32E-N8"): (99.5, 13.0, 0),
        ref("esp32", "100n"): (88.5, 8.4, 90),
        ref("esp32", "22u"): (85.6, 9.4, 90),
        ref("esp32", "1u", 0): (88.5, 11.6, 90),      # EN (pin 3) RC
        ref("esp32", "10k", 0): (88.5, 14.8, 90),
        ref("esp32", "TPS22918"): (84.8, 16.0, 0),
        ref("esp32", "100k"): (88.5, 18.0, 90),
        ref("esp32", "1u", 1): (84.8, 19.6, 0),       # TPS22918 VIN
        ref("esp32", "1n"): (84.8, 21.8, 0),
        ref("esp32", "ESP_ON"): (93.5, 30.0, 0),   # 2.54 mm header (origin pin 1), clear of the ESP32 courtyard
        # ESP32 USB port on the right edge (J2's edge offset, turned to face out) and its buttons and
        # power-OR diodes in the strip left of the module (its courtyard is x 89.75-109.25 below y 6.7)
        ref("espusb", "USB-C ESP32"): (106.2, 34.0, 90),
        ref("espusb", "CH340C"): (95.0, 50.0, 0),       # between the Qwiic connector and J5
        # the expansion block where the region packer put it before the CH340C took part of its region
        ref("exp", "EXP"): (102.815, 43.815, 0),
        ref("exp", "Qwiic"): (85.945, 45.325, 0),
        ref("exp", "4.7k", 0): (92.025, 42.775, 0),
        ref("exp", "4.7k", 1): (95.525, 42.775, 0),
        ref("exp", "10k"): (99.025, 42.775, 0),
        ref("espusb", "ESP_RST"): (78.5, 12.0, 0),
        ref("espusb", "ESP_BOOT"): (78.5, 19.0, 0),
        ref("espusb", "1N4148W", 0): (78.5, 23.0, 0),    # PB6 -> ON
        ref("espusb", "1N4148W", 1): (78.5, 25.6, 0),    # VBUS2 -> ON
        ref("usb", "USB-C"): (96.0, 71.2, 0),
        ref("ui", "SWD"): (54.5, 5.0, 0),
        # input chain under the car connector: J6.1 -> PTC -> TVS -> ideal diode (LM74700 + FET)
        ref("power", "2A 60V"): (6.0, 34.6, 0),        # pad 1 VBAT_IN left, pad 2 VBAT_F right
        ref("power", "5.0SMDJ26CA"): (9.4, 42.6, 270),    # pad 1 (VBAT_F) up at F1
        ref("power", "AO4264E"): (16.6, 36.0, 0),      # source pins 1-3 left, drain 5-8 right
        ref("power", "LM74700QDBVRQ1"): (16.5, 41.3, 180),   # GATE / CATHODE left (to the FET), VCAP right
        ref("power", "100n", 1): (19.5, 41.3, 90),             # VCAP cap: pad 2 (VBAT_F) up at ANODE
        ref("power", "100n", 0): (13.6, 44.6, 270),            # LM74700 input cap at EN / VBAT_F
        ref("power", "47u 50V"): (19.2, 47.8, 0),
        # buck: U7 (TPS54360B) at (X, Y); VIN / BOOT left, SW / GND / COMP / FB right
        ref("power", "TPS54360BDDAR"): (X, Y, 0),
        ref("power", "2.2u", 0): (X - 6.24, Y - 0.63, 180),   # pad 1 (VIN) next to pin 2
        ref("power", "2.2u", 1): (X - 6.24, Y - 3.08, 180),
        ref("power", "100n", 3): (X, Y - 3.65, 0),            # boot cap across pins 1 / 8
        ref("power", "8.2u"): (X + 8.2, Y - 4.6, 90),         # SW pad down at pin 8
        ref("power", "B560C"): (X + 16.0, Y - 5.0, 90),       # cathode down at the SW strip
        ref("power", "47u", 0): (X + 5.5, Y - 11.45, 90),
        ref("power", "47u", 1): (X + 8.0, Y - 11.45, 90),
        ref("power", "47u", 2): (X + 10.5, Y - 11.45, 90),
        ref("power", "SS34", 0): (X + 15.35, Y - 11.9, 180),  # 5V_BUCK -> 5V, anode at the caps
        ref("power", "13k"): (X + 5.6, Y + 0.63, 0),          # COMP: R19 + C27 along pin 6's row,
        ref("power", "6.8n"): (X + 8.8, Y + 0.63, 0),         # 39p just below
        ref("power", "39p"): (X + 5.6, Y + 2.3, 0),
        ref("power", "10.2k"): (X + 2.9, Y + 4.7, 270),       # FB divider under pin 5
        ref("power", "57.6k"): (X + 4.6, Y + 4.7, 90),
        ref("power", "162k"): (X - 2.9, Y + 4.7, 270),        # RT under pin 4
        # 3.3 V LDO (SOT-223) below the MCU
        ref("power", "AP2114H-3.3"): (54.4, 48.2, 0),
    }
    return f


def near_rules(design):
    """[(ref, [(target ref, pad number), ...], max distance mm)]: put ref as close as possible
    to the mean of the target pads."""
    ref = _refs(design)
    parts = {p.ref: p for p in design.parts}
    mcu = ref("mcu", "STM32G474RET6")
    car = ref("car", "OBD (Micro-Fit 2x5)")
    rules = []

    def on(r, *targets, dist=10.0):
        rules.append((r, list(targets), dist))

    def by_net(block, value, net):
        return [p.ref for p in design.parts if p.block.name == block and p.value == value
                and net in p.pin_net().values()]

    def ic(block, value, i=0):
        return ref(block, value, i)

    # MCU periphery
    on(ref("mcu", "4.7u"), (mcu, "32"))
    on(ref("mcu", "600R@100MHz"), (mcu, "29"))
    on(ref("mcu", "10k"), (mcu, "61"))                      # BOOT0 pull-down
    for net, pin in (("ESP_CTS_M", "13"), ("MCU_TX_ESP_M", "14"), ("ESP_IO0_M", "58"), ("ESP_EN_M", "60")):
        on(by_net("esp32", "1k", net)[0], (mcu, pin))
    on(ref("flash", "10k"), (ref("flash", "W25Q128JVSIQ"), "1"))
    on(ref("flash", "100n"), (ref("flash", "W25Q128JVSIQ"), "8"))
    # input chain
    lm = ic("power", "LM74700QDBVRQ1")
    fet = ic("power", "AO4264E")
    tvs = ic("power", "5.0SMDJ26CA")
    on(ref("power", "10u", 0), (fet, "5"))                  # VBAT_P 10u
    on(ref("power", "100n", 2), (fet, "7"))                 # VBAT_P 100n
    on(ref("power", "100k 0.1%"), (tvs, "1"))               # VBAT sense: 100k at the source,
    on(ref("power", "10k 0.1%"), (mcu, "18"))               # 10k + 100n at PA4
    on(ref("power", "100n", 4), (mcu, "18"), dist=16)
    # 5 V OR + LDO
    ldo = ic("power", "AP2114H-3.3")
    on(ref("power", "10u", 2), (ldo, "3"))                  # LDO input
    on(ref("power", "10u", 3), (ldo, "2"))                  # LDO output
    on(ref("power", "SS34", 1), (ldo, "3"), dist=14)        # VBUS -> 5V
    on(ref("power", "10u", 1), (ref("power", "SS34", 0), "1"))
    # car-connector protection: clamp right at the connector pin
    on(ic("j1850", "SMF24A", 0), (car, "3"), dist=14)
    on(ic("j1850", "SMF24A", 1), (car, "4"), dist=14)
    on(ic("iso", "SMF24A", 0), (car, "5"), dist=14)
    on(ic("iso", "SMF24A", 1), (car, "6"), dist=14)
    on(ic("can", "SZNUP2105L", 0), (car, "7"), (car, "8"), dist=14)
    on(ic("can", "SZNUP2105L", 1), (car, "9"), (car, "10"), dist=14)
    # CAN transceivers: VCC / VIO caps and the STB pull-up at their pins
    for i in (0, 1):
        u = ic("can", "TCAN1044VDRQ1", i)
        on(by_net("can", "100n", "5V_BUCK")[i], (u, "3"))
        on(by_net("can", "100n", "3V3")[i], (u, "5"))
        on(ref("can", "4.7k", i), (u, "8"))
    # DLC_SW, J1850, K-line front ends
    on(ref("dlcsw", "1u"), (ic("dlcsw", "SI2309CDS"), "3"))
    on(ref("dlcsw", "BZT52C12"), (ic("dlcsw", "SI2309CDS"), "1"))
    reg = ic("j1850", "LM317LIDR")
    cmp_ = ic("j1850", "LM2901BIDR")
    on(by_net("j1850", "100n", "DLC_SW")[0], (reg, "1"))
    on(ref("j1850", "1u"), (reg, "2"))
    on(by_net("j1850", "100n", "CMP_VCC")[0], (cmp_, "3"))
    on(ref("j1850", "BZT52C33"), (cmp_, "3"))
    on(by_net("j1850", "470", "CMP_VCC")[0], (cmp_, "3"))
    # ESP32 USB port: ESD and CC at the connector, CH340C periphery at its pins
    j7 = ref("espusb", "USB-C ESP32")
    ch = ic("espusb", "CH340C")
    on(ic("espusb", "USBLC6-2SC6"), (j7, "A6"), (j7, "A7"), dist=8)
    on(ref("espusb", "5.1k", 0), (j7, "A5"), dist=8)
    on(ref("espusb", "5.1k", 1), (j7, "B5"), dist=8)
    on(ref("espusb", "10u"), (j7, "A4"), dist=8)
    on(ref("espusb", "SS34"), (j7, "A4"), dist=10)
    on(ref("espusb", "100n"), (ch, "16"), dist=6)
    on(ref("espusb", "1k"), (ch, "2"), dist=6)
    for i, pin in ((0, "13"), (1, "14")):
        on(ref("espusb", "10k", i), (ch, pin), dist=8)
        on(ref("espusb", "MMBT3904", i), (ch, pin), dist=10)
    assert all(r in parts for r, _, _ in rules)
    return rules


def courtyard(fp):
    cy = fp.GetCourtyard(pcbnew.F_CrtYd)
    if cy.OutlineCount():
        bb = cy.BBox()
    else:
        bb = fp.GetBoundingBox(False)
    return (pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetY()), pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom()))


def _overlap(a, b):
    return not (a[2] + GAP <= b[0] or b[2] + GAP <= a[0] or a[3] + GAP <= b[1] or b[3] + GAP <= a[1])


def _inside(r):
    return r[0] >= 0.5 and r[1] >= 0.5 and r[2] <= W - 0.5 and r[3] <= H - 0.5


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


def _pad_pos(fp, num):
    pts = [p.GetPosition() for p in fp.Pads() if p.GetNumber() == num]
    if not pts:
        raise KeyError(f"{fp.GetReference()} has no pad {num}")
    return pcbnew.ToMM(pts[0].x), pcbnew.ToMM(pts[0].y)


_RINGS = None


def _rings(dist, step=0.25):
    global _RINGS
    if _RINGS is None or _RINGS[0] != (dist, step):
        n = int(dist / step)
        pts = [(i * step, j * step) for i in range(-n, n + 1) for j in range(-n, n + 1)
               if math.hypot(i, j) * step <= dist]
        pts.sort(key=lambda p: math.hypot(*p))
        _RINGS = ((dist, step), pts)
    return _RINGS[1]


def _place_near(fp, target, dist, occupied):
    """The free spot nearest `target` for fp (rotations 0 / 90 / 180 / 270), by fp's centre."""
    from pcbgen import place
    tx, ty = target
    shapes = []
    for rot in (0, 90, 180, 270):
        place(fp, 0, 0, rot)
        c = courtyard(fp)
        shapes.append((rot, c))
    for dx, dy in _rings(dist):
        cx, cy = tx + dx, ty + dy
        for rot, (x0, y0, x1, y1) in shapes:
            ox, oy = (x0 + x1) / 2, (y0 + y1) / 2  # courtyard centre relative to the origin
            rect = (cx - ox + x0, cy - oy + y0, cx - ox + x1, cy - oy + y1)
            if _inside(rect) and not any(_overlap(rect, o) for o in occupied):
                place(fp, cx - ox, cy - oy, rot)
                return rect
    return None


def place_all(board, design, fps):
    from pcbgen import place
    fixed = fixed_for(design)
    rules = near_rules(design)
    occupied = list(KEEPOUTS) + [LDO_TAB]
    placed = set()
    for r, (x, y, rot) in fixed.items():
        place(fps[r], x, y, rot)
        occupied.append(courtyard(fps[r]))
        placed.add(r)
    near_refs = {r for r, _, _ in rules}

    def run_rules():
        progress = True
        while progress:
            progress = False
            for r, targets, dist in rules:
                if r in placed or not all(t in placed for t, _ in targets):
                    continue
                pts = [_pad_pos(fps[t], n) for t, n in targets]
                tgt = (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))
                spot = _place_near(fps[r], tgt, dist, occupied)
                if spot is None:
                    print(f"layout: {r} found no spot within {dist} mm of {targets}")
                    continue
                occupied.append(spot)
                placed.add(r)
                progress = True

    run_rules()
    for b in design.blocks:
        region = REGIONS[b.name]
        for p in b.parts:
            if p.ref in placed or p.ref in near_refs or p.ref not in fps:
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
            placed.add(p.ref)
            run_rules()
    run_rules()
    missing = [r for r in near_refs if r not in placed]
    for r in missing:  # a NEAR part whose spot was taken: pack it anywhere
        fp = fps[r]
        place(fp, 0, 0, 0)
        cx0, cy0, cx1, cy1 = courtyard(fp)
        spot = _find((1, 1, W - 1, H - 1), cx1 - cx0, cy1 - cy0, occupied)
        place(fp, spot[0] - cx0, spot[1] - cy0, 0)
        occupied.append(spot)
        print(f"layout: {r} packed anywhere (no NEAR spot)")
