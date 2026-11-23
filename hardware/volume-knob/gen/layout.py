"""Board placement for the volume knob.

A 47 mm disc: the smallest that holds the cell at the top of its tolerance (13 x 42 mm) with the
module's antenna at the rim. Coordinates here are mm from the centre of the board (the encoder's shaft), +x right
and +y down as seen from the top; the board sits at (CX, CY) on the KiCad page.

Top:    the encoder in the centre, the LED ring round it, the encoder's RC parts beside it, the fuse.
Bottom: the cell across the middle (a little below centre), the ESP32 module above it with its
        antenna at the rim, USB-C at the rim below it with the charger on its left and the boost
        converter on its right; the LEDs' capacitors, each as near under its LED as the bottom
        allows, and every test pad.

FIXED parts have a position. The others (NEAR) are packed by `place_all` next to a target point,
at the spot and turn that keeps their connections shortest.
"""

import math

import pcbnew

import pcbgen

CX, CY = 100.0, 100.0
R_BOARD = 23.5
EDGE = 0.6            # parts' courtyards keep this far inside the edge
N_LEDS = 12
R_LED = 14.0          # LED ring radius: outside the encoder's feet, inside the antenna's keep-out
LED_A0 = 0.0          # the first LED's angle (degrees, clockwise from +x seen from the top)
# the encoder's pull-ups, series resistors and filter capacitors: columns beside it, level with
# each other left and right
ENC_COL_X, ENC_ROW_Y = 9.9, (3.1, 4.2, 5.3)

# the module: antenna to the rim at the top of the board (-y)
UP = -1.5             # what sits above the cell moved this far when the board grew from 44 to 47 mm
ESP_Y = -9.9 + UP
# the cell: its holes on the right (next to the boost converter), the can across the board under
# the centre, all of it on the board at 13 x 42 mm (the footprint's outline)
CELL_X, CELL_Y = 21.65, 1.2
# the can lies on the board along its axis only. Under its flanks, from 3.75 mm off the axis, there
# is 1.2 mm of air and more: room for 0402 parts (0.55 mm), not for anything taller or a test pad
CELL_STRIP = (CELL_X - 43.75, CELL_Y - 3.75, CELL_X + 1.25, CELL_Y + 3.75)
CELL_BODY = (CELL_X - 43.75, CELL_Y - 6.75, CELL_X - 1.2, CELL_Y + 6.75)
USB_Y = 18.3 - UP
HOLES = [(-16.0, -11.5 + UP), (16.0, -11.5 + UP)]     # M2
HOLE_KEEP = 2.3
# antenna: the whole strip of board beside and under it carries no parts and no copper on any
# layer (Espressif: no ground beside the antenna), from the module's keep-out line to the rim
ANTENNA_Y = ESP_Y - 5.6
ANTENNA = (-23.0, -23.0, 23.0, ANTENNA_Y + 0.15)
ANTENNA_NOTCH_X = 8.0   # half the width of the notch in the board under the antenna (the module is 13.2 wide)
# room kept free of packed parts for vias laid by prewire.py: (x0, y0, x1, y1), both sides
VIA_ROOM = [(8.8, -11.1 + UP, 9.8, -9.2 + UP),       # the 3V3 trunk's vias beside the module
            (3.2, 10.1, 5.9, 13.0),          # and beside the boost's output capacitors
            (6.65, -5.4 + UP, 7.35, -4.5 + UP), (-7.35, -5.4 + UP, -6.65, -4.5 + UP)]   # the module's cell-side corners
# fiducials (x, y, on the back): 1 mm copper dots for the pick-and-place camera
FIDUCIALS = [(-18.5, 6.5, False), (18.6, -8.6, False), (-19.0, -8.5, True), (19.3, -8.7, True)]

# ref: (x, y, rotation, on the back)
FIXED = {
    "SW1": (0, 0, 0, False),
    "U6": (0, ESP_Y, 180, True),
    "BT1": (CELL_X, CELL_Y, 180, True),
    "J1": (0, USB_Y, 180, True),
    # the boost converter and what its current loops through: hand-placed, wired by prewire.py.
    # A fifth entry (pad, dx, dy) picks the turn that puts that pad on that side of the part
    "U4": (10.2, 12.5, 0, True, ("A1", 1, -1)),       # VIN / SW / EN column on the right, VIN on top
    "L1": (12.3, 11.85, 0, True, ("1", 0, -1)),       # VBAT pad on top, SW pad level with the SW ball
    # output capacitors in a row: 3V3 pads level with the VOUT ball, ground pads level with (and
    # beside) the GND ball A2, which is the converter's only ground
    "C7": (8.45, 11.55, 0, True, ("1", 0, 1)),
    "C8": (6.45, 11.55, 0, True, ("1", 0, 1)),
    "C9": (7.45, 14.35, 0, True, ("1", -1, 0)),       # the third, under the first two
    "C5": (15.15, 11.025, 0, True, ("1", -1, 0)),     # VBAT pad next to the inductor's
    "C6": (12.9, 14.5, 0, True, ("1", 1, 0)),         # VBAT pad on the right, under the VBAT via pair
    "C12": (8.5, -12.3 + UP, 0, True, ("1", -1, 0)),       # the module's 10 uF: 3V3 pad at its supply pin
    # the charger: input pins (VBUS, ISET, /CE) towards the USB socket, output pins (OUT, FB, /PG,
    # STAT) towards the edge, each with its parts on its own side
    "U2": (-12.0, 12.6, 0, True, ("1", 1, 0)),
    # (ISET's resistor level with its pin, so that /CE, the next pin down, has a way out past it)
    "C2": (-8.0, 10.4, 0, True, ("1", -1, 0)),        # VBUS
    "R7": (-8.9, 12.0, 0, True, ("1", -1, 0)),        # ISET
    "R9": (-15.6, 10.6, 0, True, ("2", 1, 0)),        # VBAT - FB
    "R10": (-15.6, 11.7, 0, True, ("1", 1, 0)),       # FB - /PG
    "C3": (-12.6, 15.75, 0, True, ("1", 0, -1)),     # VBAT at OUT
    # the fuse is on the top, beside the cell's + hole
    "F1": (CELL_X - 2.4, CELL_Y - 0.3, 0, False, ("1", 0, -1)),
    # top: the encoder's parts. Left column, lower half: channel A (series, pull-up, filter); upper
    # half: the switch. Right column, lower half: channel B. The encoder-side pad faces the encoder
    "R19": (-ENC_COL_X, ENC_ROW_Y[0], 0, False, ("1", 1, 0)),
    "R20": (-ENC_COL_X, ENC_ROW_Y[1], 0, False, ("2", 1, 0)),
    "C16": (-ENC_COL_X, ENC_ROW_Y[2], 0, False, ("1", 1, 0)),
    "R23": (-ENC_COL_X, -ENC_ROW_Y[0], 0, False, ("1", 1, 0)),
    "R24": (-ENC_COL_X, -ENC_ROW_Y[1], 0, False, ("2", 1, 0)),
    "C18": (-ENC_COL_X, -ENC_ROW_Y[2], 0, False, ("1", 1, 0)),
    "R21": (ENC_COL_X, ENC_ROW_Y[0], 0, False, ("1", -1, 0)),
    "R22": (ENC_COL_X, ENC_ROW_Y[1], 0, False, ("2", -1, 0)),
    "C17": (ENC_COL_X, ENC_ROW_Y[2], 0, False, ("1", -1, 0)),
}

# the expansion row (TP12..TP17: GND, 3V3, IO8, BOOT, IO2, TX): plated holes at 2.54 mm on an arc beside the
# USB socket, the one stretch of rim that is clear on all four layers and not under the cell
IO_R, IO_A0, IO_PITCH = 21.8, 111.5, 2.54
for _k in range(6):
    _a = math.radians(IO_A0) + _k * 2 * math.asin(IO_PITCH / 2 / IO_R)
    FIXED[f"TP{12 + _k}"] = (round(IO_R * math.cos(_a), 3), round(IO_R * math.sin(_a), 3), 0, True)

# packed in this order: (ref, target x, target y, on the back)
def _up(rows):
    return [(ref, x, y + UP, back) for ref, x, y, back in rows]


NEAR = _up([
    # beside the module, right: the cut-off, the module's supply and reset, the LED rail switch
    ("U3", 15.0, -6.4, True), ("R11", 17.6, -8.4, True), ("C4", 12.6, -8.6, True),
    ("C13", 8.2, -13.9, True), ("U7", 9.9, -9.4, True), ("R14", 8.0, -7.4, True),
    ("C14", 8.0, -6.2, True), ("C15", 12.0, -11.4, True), ("U5", 12.4, -13.4, True), ("R13", 10.4, -14.6, True),
    ("C11", 14.6, -11.6, True),            # the LED rail's 10 uF, at the switch
    ("R8", 8.0, -5.0, True),
    # beside the module, left: USB series resistors, straps, sense
    ("R3", -8.2, -6.4, True), ("R4", -8.2, -5.2, True), ("R12", -8.2, -8.2, True), ("C10", -8.2, -9.4, True),
    ("R15", -8.2, -11.0, True), ("R25", -8.2, -12.2, True), ("R16", -8.2, -13.6, True),
    ("R17", -8.2, -15.0, True), ("R18", -8.2, -16.4, True),
]) + [
    # USB ESD array in the strip between the socket and the cell; the charger's small parts
    ("U1", -3.0, 12.6, True),
    ("C1", -3.4, 11.9, True), ("R1", -6.4, 15.2, True), ("R2", 6.4, 15.4, True),
    ("R5", -9.6, 15.4, True), ("R6", -10.2, 16.6, True),
]
# test pads, all on the bottom, packed last (after the LEDs' capacitors): 3V3, GND, VBAT, VBUS, VLED,
# EN, BOOT, TX, IO2, BOOST_EN, LED data
PADS = _up([
    ("TP1", -11.0, -5.6, True), ("TP2", -13.0, -5.6, True), ("TP4", -17.0, -5.6, True),
    ("TP6", -11.0, -7.8, True), ("TP7", -13.0, -7.8, True), ("TP8", -11.0, -13.6, True),
    ("TP9", -12.8, -14.4, True),
]) + [("TP3", 17.4, 11.9, True), ("TP10", 19.2, 9.6, True), ("TP11", 15.6, 14.3, True), ("TP5", -19.0, 9.6, True)]


def led_angle(i):
    """LED i (1..N_LEDS): its angle on the ring."""
    return LED_A0 + (i - 1) * 360.0 / N_LEDS


def polar(r, deg):
    a = math.radians(deg)
    return r * math.cos(a), r * math.sin(a)


def mm(v):
    return pcbnew.ToMM(v)


def courtyard(fp):
    """(x0, y0, x1, y1) of the footprint's courtyard on its side, board-centre coordinates."""
    poly = fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd)
    if poly.OutlineCount():
        b = poly.BBox()
    else:
        b = fp.GetBoundingBox(False)
    return (mm(b.GetLeft()) - CX, mm(b.GetTop()) - CY, mm(b.GetRight()) - CX, mm(b.GetBottom()) - CY)


def put(fp, x, y, rot=0, back=False):
    pcbgen.place(fp, CX + x, CY + y, rot, back)


def _overlap(a, b, gap=0.0):
    return a[0] < b[2] + gap and b[0] < a[2] + gap and a[1] < b[3] + gap and b[1] < a[3] + gap


def _inside(box):
    r = R_BOARD - EDGE
    return all(math.hypot(x, y) <= r for x in (box[0], box[2]) for y in (box[1], box[3]))


def _pads(fp):
    return [(p.GetNetname(), mm(p.GetPosition().x) - CX, mm(p.GetPosition().y) - CY) for p in fp.Pads()
            if p.GetNetname() and not p.GetNetname().startswith("unconnected")]


QUIET = {"GND"}       # joined by the plane: not counted as a connection to keep short


def place_all(board, design, fps):
    """Fixed parts, the LED ring, then the packed parts. Returns the refs that found no spot."""
    occupied = {False: [], True: []}    # courtyards, by side
    placed_pads = []                    # (net, x, y) of everything placed so far

    def take(ref):
        fp = fps[ref]
        occupied[fp.IsFlipped()].append(courtyard(fp))
        placed_pads.extend(_pads(fp))

    for ref, (x, y, rot, back, *want) in FIXED.items():
        put(fps[ref], x, y, rot, back)
        if want:
            pad, dx, dy = want[0]
            best = None
            for r in (0, 90, 180, 270):
                put(fps[ref], x, y, r, back)
                p = [q for q in fps[ref].Pads() if q.GetNumber() == pad][0].GetPosition()
                score = (mm(p.x) - CX - x) * dx + (mm(p.y) - CY - y) * dy
                if best is None or score > best[0] + 1e-6:
                    best = (score, r)
            put(fps[ref], x, y, best[1], back)
        take(ref)
    # the module's antenna and the screw heads are kept clear on both sides
    for side in (False, True):
        occupied[side].append(ANTENNA)
        for hx, hy in HOLES:
            occupied[side].append((hx - HOLE_KEEP, hy - HOLE_KEEP, hx + HOLE_KEEP, hy + HOLE_KEEP))
        occupied[side].extend(VIA_ROOM)
    for x, y, back in FIDUCIALS:
        occupied[back].append((x - 1.1, y - 1.1, x + 1.1, y + 1.1))
    # the module's cell-side pin row keeps a millimetre of bare board to escape into
    occupied[True].append((-6.8, ESP_Y + 5.6, 6.8, ESP_Y + 6.6))
    for sx in (-1, 1):                  # and so do its side rows
        occupied[True].append((min(sx * 6.8, sx * 7.7), ESP_Y - 5.6, max(sx * 6.8, sx * 7.7), ESP_Y + 5.6))
    tall = [CELL_BODY]                  # the cell's whole can, for everything but 0402 parts

    def blocked(fp, box, back):
        if any(_overlap(box, o, 0.05) for o in occupied[back]):
            return True
        return back and "0402" not in fp.GetFPIDAsString() and any(_overlap(box, o, 0.05) for o in tall)

    # LED ring: D1..D12 clockwise, each turned so its data runs on round the ring; its capacitor
    # is packed on the bottom under it
    by_block = {b.name: b for b in design.blocks}
    leds = [p for p in by_block["leds"].parts if p.ref.startswith("D")]
    caps = [p for p in by_block["leds"].parts if p.ref.startswith("C")]
    for i, (led, cap) in enumerate(zip(leds, caps), start=1):
        a = led_angle(i)
        x, y = polar(R_LED, a)
        # footprint at 0 degrees: DIN (pad 3) on +x, DOUT (pad 1) on -x. Clockwise on the page is
        # +angle here (y is down), the tangent there is at a + 90: turn the part's -x that way
        put(fps[led.ref], x, y, -(a + 90) + 180, False)
        take(led.ref)
    own = {cap.ref for cap in caps}
    under = [(cap.ref, *polar(R_LED, led_angle(i)), True) for i, cap in enumerate(caps, start=1)]

    missing = sorted(set(fps) - set(FIXED) - {n[0] for n in NEAR + PADS} - {p.ref for p in leds + caps})
    if missing:
        raise SystemExit(f"layout: no position for {' '.join(missing)}")
    failed = []
    for ref, tx, ty, back in NEAR + under + PADS:
        fp = fps[ref]
        best = None
        for rot in (0, 90, 180, 270):
            put(fp, tx, ty, rot, back)
            box0 = courtyard(fp)
            pads0 = [(px, py, [(qx, qy) for qn, qx, qy in placed_pads if qn == net])
                     for net, px, py in _pads(fp) if net not in QUIET]
            pads0 = [p for p in pads0 if p[2]]
            if ref in own:                          # an LED's capacitor: as near under its LED as it gets
                pads0 = []
            for ring in range(0, 49):               # 0.25 mm steps out to 12 mm
                d = ring * 0.25
                steps = max(1, int(2 * math.pi * d / 0.25))
                for k in range(steps):
                    ang = 2 * math.pi * k / steps
                    dx, dy = d * math.cos(ang), d * math.sin(ang)
                    box = (box0[0] + dx, box0[1] + dy, box0[2] + dx, box0[3] + dy)
                    if not _inside(box) or blocked(fp, box, back):
                        continue
                    cost = 0.35 * d
                    for px, py, same in pads0:
                        cost += min(math.hypot(px + dx - qx, py + dy - qy) for qx, qy in same)
                    if best is None or cost < best[0]:
                        best = (cost, tx + dx, ty + dy, rot)
                if best and d > best[0] / 0.35:     # farther rings cannot beat it
                    break
        if best is None:
            failed.append(ref)
            put(fp, 30, 0, 0, back)
            continue
        put(fp, best[1], best[2], best[3], back)
        take(ref)
    return failed
