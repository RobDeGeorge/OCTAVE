"""Silkscreen that says what things are, on top of the reference labels (build.py pcb calls apply()).

Front (the parts side):
- NO_REF: the small parts carry no reference there; the chips keep theirs.
- NAMES: the parts a person touches (buttons, test points, connectors) carry their function where
  their reference was. The reference stays on the footprint, hidden.
- FRONT: each circuit's name beside its own chip; LEGEND: the CONFIG switch's positions.
- BORDERS: the lines between the circuits, drawn where nothing is in their way.
- PIN1: a "1" beside pin 1 of each header; BOARD: the board's name and revision.

Back (no parts on it): a map of the front, seen through the board. Each circuit has its outline and
its name behind it, the plug's 16 pins and the two headers' pins have their signals beside them.

Also on the back, in the J1850 box: the wizard (wizard.txt) and a quote (art.json, from mkart.py).

All positions are board coordinates in mm (y down), the same on both sides: text on the back is
mirrored by KiCad. check() lists anything that touches a pad, a part, the edge or other silkscreen.
"""

import json
import os
import re

import pcbnew

import layout

SMALL, NORMAL, TITLE, BIG = (1.0, 0.15), (1.0, 0.15), (1.2, 0.18), (1.5, 0.22)   # (height, stroke) mm
LINE = 0.15         # outline width

# ref -> the text that takes its reference's place on the front
NAMES = {
    "SW6": "CONFIG", "SW1": "RESET", "SW3": "USER", "SW4": "ESP RST", "SW5": "BOOT",     # read together: ESP RST  BOOT
    "TP1": "VBAT", "TP2": "5V", "TP3": "3V3", "TP4": "GND", "TP5": "DLC",
    "TP6": "J+", "TP7": "J-", "TP8": "VJ", "TP9": "K", "TP10": "L",
    "U5": "ESP PWR",        # the ESP32's load switch, under the STM32's USB port
    "J1": "SWD", "J2": "STM32", "J7": "ESP32", "J9": "PANIC", "J5": "EXP", "J4": "QWIIC", "J8": "ESP IO",
}
# ... and where it goes when it is wider than the room the reference had (x, y, angle)
NAME_AT = {
    "J2": (85.0, 8.4, 0),       # in the pocket left of the port, over "USB" (FRONT): no room at the port itself
    "J7": (96.4, 60.9, 0),      # above the port, right of U13's reference
    "U5": (95.95, 13.42, 0),    # under U5, between U2 and the board edge
    "J8": (72.0, 19.3, 0),      # right of R14's label
}
TP_NAME_DY = -1.65  # a test point's name: this far above its pad (the lower row's names stand between the rows)

# The small parts (R, C, D, Q ...) carry no reference on the front: their room goes to the circuits'
# names and outlines. Chips keep theirs. (The placement file and the schematic say which part is which.)
NO_REF = re.compile(r"^(C|D|F|L|Q|R|RN|Y)\d+$")

# front: (text, x, y, angle, style), each beside its own chip
FRONT = [
    ("5V BUCK", 14.1, 3.4, 0, BIG),
    ("DLC SW", 36.2, 13.0, 90, BIG),
    ("BUS ESD", 18.2, 27.7, 0, SMALL),          # D19 D20 (J1850), D9 D8 (CAN)
    ("12V IN", 19.3, 29.95, 0, TITLE),
    ("MS CAN", 28.95, 21.2, 0, NORMAL),          # above U10
    ("HS CAN", 27.5, 35.35, 0, NORMAL),          # above U9 (its reference moved right: layout.LABELS)
    ("J1850", 42.4, 38.1, 90, BIG),
    ("K/L LINE", 5.2, 55.4, 90, NORMAL),
    ("STM32", 57.4, 29.3, 0, BIG),
    ("3V3", 64.65, 43.8, 0, NORMAL), ("LDO", 64.65, 45.3, 0, NORMAL),     # right of U8
    ("FLASH", 64.65, 48.4, 0, NORMAL),           # above U3
    ("SD CARD", 76.5, 57.6, 0, NORMAL),
    ("R", 60.5, 15.2, 0, SMALL), ("G", 64.0, 15.2, 0, SMALL), ("B", 67.5, 15.2, 0, SMALL),   # under D1 D2 D3
    ("CRYPTO", 61.6, 17.6, 0, SMALL),            # right of U16
    ("ACCEL", 61.5, 24.9, 0, SMALL),             # right of U15 and its capacitor
    ("ESP32", 86.2, 24.4, 0, NORMAL),            # above the module
    ("USB", 97.6, 50.3, 0, SMALL), ("UART", 97.6, 51.6, 0, SMALL),       # right of U14 and its reset pair
    ("USB", 85.0, 9.9, 0, SMALL),                # under J2's name (NAME_AT): the STM32's USB port
]
# front: a "1" beside pin 1 of each header (x, y). Not J5: at 1.0 mm nothing fits between it and J9, and its
# own corner mark and square pad say which pin is 1 (the table on the back numbers them all)
PIN1 = {"J8": (66.7, 22.0), "J1": (43.8, 19.7)}
# front: the board's name and revision, one line each from (x, y) down, in the clear corner left of the buck
BOARD = (7.2, 13.6, 1.5, ["OCTAVE", "OBD", "REV {rev}"])
# back: where the board house prints its order number (JLCPCB: "Specify a location" when ordering)
ORDER_NO = ("JLCJLCJLCJLC", 70.6, 9.6, 0, NORMAL)

# front: the lines between the circuits (polylines; a shared border is written twice and drawn once).
# Each is drawn only where no pad, part or label is in its way.
_Y = 62.5       # above the test points and the bottom connectors
BORDERS = [
    [(7.3, 1.2), (34.45, 1.2), (34.45, 19.6), (20.9, 19.6), (20.9, 22.7), (3.6, 22.7), (3.6, 7.0), (7.3, 7.0), (7.3, 1.2)],      # buck
    [(34.45, 1.2), (45.0, 1.2), (45.0, 8.9), (43.4, 8.9), (43.4, 18.7), (34.45, 18.7)],                                          # DLC switch
    [(3.6, 28.85), (24.3, 28.85), (24.3, 49.4), (13.6, 49.4), (13.6, 45.15), (3.6, 45.15)],                                      # 12 V input
    [(13.6, 49.4), (24.3, 49.4), (24.3, _Y), (3.6, _Y)],                                                                         # K / L line
    [(20.9, 19.6), (33.6, 19.6), (33.6, 34.3), (24.3, 34.3), (24.3, 22.7)],                                                      # MS CAN
    [(33.6, 34.3), (33.6, 47.6), (24.3, 47.6)],                                                                                  # HS CAN
    [(33.6, 22.4), (52.35, 22.4), (52.35, _Y), (24.3, _Y)],                                                                      # J1850
    [(52.35, 26.05), (79.75, 26.05), (79.75, 50.15), (67.2, 50.15), (67.2, 42.45), (52.35, 42.45)],                              # STM32
    [(62.15, 46.6), (62.15, 50.15), (57.6, 50.15), (57.6, _Y), (52.35, _Y)],                                                    # LDO
    [(62.15, 46.6), (67.2, 46.6)],                                                                                               # LDO's name | FLASH
    [(67.2, 50.15), (75.1, 50.15), (75.1, 52.6), (67.6, 52.6), (67.6, 56.6), (57.6, 56.6)],                                      # flash
    [(79.75, 50.15), (79.75, 59.6), (73.05, 59.6), (73.05, 63.0)],                                                               # card socket
]
# the CONFIG switch (SW6), in the pocket right of the LEDs: lines from (x, y) down, starting at x
LEGEND = (69.9, 11.2, 1.3, ["CONFIG", "1 ESP ON", "2 HS TERM", "3 MS TERM", "4 BOOT0", "OFF IN CAR"])

# The plug's edge pads: 1-8 are on the back, 9-16 behind them on the front (pin n + 8 behind pin n)
PLUG_PINS = ["NC", "J1850+", "MSCAN H", "GND", "GND", "HSCAN H", "K-LINE", "NC",
             "NC", "J1850-", "MSCAN L", "NC", "NC", "HSCAN L", "L-LINE", "+12V"]
PIN_X = 4.5         # the pin names start here (the pads end at 3.0)
PIN_COL = (3.4, 19.3, 13.9, 50.7)   # their column: no outline crosses it

# back: circuit -> (outline, [(text, x, y, angle, style)])
_H = layout.H
MAP = {
    "buck": ([(7.3, 1.2), (34.2, 1.2), (34.2, 22.6), (14.2, 22.6), (14.2, 19.0), (4.2, 19.0), (4.2, 7.4), (7.3, 7.4)],
             [("5V BUCK", 22.0, 5.2, 0, BIG), ("12V IN  5.3V OUT", 22.0, 7.6, 0, SMALL)]),
    "dlcsw": ([(34.6, 1.2), (45.0, 1.2), (45.0, 8.9), (43.4, 8.9), (43.4, 18.7), (34.6, 18.7)],
              [("DLC SW", 39.2, 9.4, 0, TITLE), ("12V FEED", 39.0, 11.5, 0, SMALL), ("FOR K / L", 39.0, 13.0, 0, SMALL), ("AND J1850", 39.0, 14.5, 0, SMALL)]),
    "input": ([(14.2, 28.3), (24.3, 28.3), (24.3, 49.7), (14.2, 49.7)],
              [("12V IN", 19.25, 35.6, 0, TITLE), ("FUSE  TVS", 19.25, 38.0, 0, SMALL), ("IDEAL DIODE", 19.25, 39.5, 0, SMALL)]),
    "can": ([(14.2, 22.9), (33.6, 22.9), (33.6, 47.6), (24.6, 47.6), (24.6, 28.0), (14.2, 28.0)],
            [("CAN FD", 29.1, 33.8, 0, TITLE), ("MS CAN", 29.1, 25.6, 0, NORMAL), ("PINS 3/11", 29.1, 27.2, 0, SMALL),
             ("HS CAN", 29.1, 40.6, 0, NORMAL), ("PINS 6/14", 29.1, 42.2, 0, SMALL)]),
    "j1850": ([(33.9, 22.9), (52.4, 22.9), (52.4, 61.9), (24.6, 61.9), (24.6, 47.9), (33.9, 47.9)],
              [("J1850", 43.15, 25.1, 0, BIG), ("VPW / PWM", 43.15, 27.5, 0, NORMAL), ("PINS 2/10", 43.15, 29.3, 0, SMALL)]),
    "iso": ([(4.2, 51.0), (24.3, 51.0), (24.3, 62.4), (4.2, 62.4)],
            [("ISO 9141 / KWP", 14.25, 53.4, 0, NORMAL), ("K-LINE", 10.3, 56.6, 0, NORMAL), ("PIN 7", 10.3, 58.2, 0, SMALL),
             ("L-LINE", 19.6, 56.6, 0, NORMAL), ("PIN 15", 19.6, 58.2, 0, SMALL)]),
    "tp": ([(6.5, 62.7), (24.3, 62.7), (24.3, _H - 0.5), (6.5, _H - 0.5)], []),      # names: _test_points
    "panic": ([(24.6, 62.7), (39.0, 62.7), (39.0, _H - 0.5), (24.6, _H - 0.5)],
              [("PANIC", 31.8, 65.3, 0, NORMAL), ("BUTTON", 31.8, 67.0, 0, NORMAL)]),
    "swd": ([(43.8, 12.2), (52.2, 12.2), (52.2, 21.6), (43.8, 21.6)], [("SWD", 48.0, 16.9, 0, TITLE)]),
    "mcu": ([(52.7, 26.3), (79.5, 26.3), (79.5, 49.9), (62.1, 49.9), (62.1, 42.3), (52.7, 42.3)],
            [("STM32G474", 67.0, 33.6, 0, BIG), ("MCU", 67.0, 36.0, 0, NORMAL)]),
    "ldo": ([(52.7, 42.6), (61.8, 42.6), (61.8, 50.2), (57.6, 50.2), (57.6, 62.3), (52.7, 62.3)],
            [("3V3 LDO", 57.25, 46.4, 0, NORMAL)]),
    "flash": ([(57.9, 50.5), (67.6, 50.5), (67.6, 56.5), (57.9, 56.5)], [("FLASH", 62.75, 53.5, 0, NORMAL)]),
    "esp32": ([(80.2, 26.3), (99.6, 26.3), (99.6, 44.6), (80.2, 44.6)],
              [("ESP32", 89.9, 29.0, 0, BIG), ("BT  BLE  WIFI", 89.9, 39.0, 0, NORMAL)]),
    "espusb": ([(80.6, 44.9), (99.6, 44.9), (99.6, 61.0), (80.6, 61.0)],
               [("USB UART", 90.1, 51.6, 0, TITLE), ("CH340C", 90.1, 53.6, 0, SMALL)]),
}
# back: names with no outline (behind the part itself)
LOOSE = [
    ("OCTAVE OBD ADAPTER", 70.6, 13.0, 0, BIG),
    ("CONFIG", 52.2, 5.6, 0, SMALL), ("RESET", 59.35, 5.6, 0, SMALL), ("USER", 65.65, 5.6, 0, SMALL),
    ("ESP", 71.95, 4.9, 0, SMALL), ("RST", 71.95, 6.2, 0, SMALL), ("ESP", 78.25, 4.9, 0, SMALL), ("BOOT", 78.25, 6.2, 0, SMALL),
    ("USB", 94.0, 3.2, 0, NORMAL), ("STM32", 94.0, 4.8, 0, NORMAL),
    ("USB", 94.0, _H - 4.4, 0, NORMAL), ("ESP32", 94.0, _H - 2.8, 0, NORMAL),
    ("CRYPTO", 55.8, 16.2, 0, SMALL), ("ACCEL", 55.0, 24.6, 0, SMALL),
    ("QWIIC", 77.0, 66.0, 0, NORMAL), ("I2C 3V3", 77.0, 67.6, 0, SMALL),
]
# back, for the fun of it, both in the J1850 circuit's box (it is the roomiest): the wizard of the owner's
# terminal greeting (wizard.txt, one filled rectangle per character, sized by how dark the character is)
# under the box's name, and a line of Gandalf's under that (art.json: outlines made by mkart.py).
WIZARD = (49.4, 31.0, 0.25, 0.47)       # right edge x, top y, cell width, cell height (50 x 26 cells)
QUOTE = (38.5, 50.9, 2.0, 2.75)         # centre x, first baseline y, em size, line pitch
_SHADE = {" ": 0, ".": .12, ":": .18, "-": .22, "=": .3, "+": .38, "*": .48, "#": .58, "%": .64, "&": .7, "@": .8,
          "\u2591": .3, "\u2592": .55, "\u2593": .8, "\u2588": 1}

# The headers' pins come through the board. J8 (one row): each name under its pin, turned.
# J5 (2 x 7): a table behind the card socket, as nothing fits round the pins.
EXP_TABLE = (58.3, 57.4, 72.6, _H - 0.5)
EXP_NAMES = {"3V3": "3V3", "5V": "5V", "GND": "GND", "EXP_TX": "TX", "EXP_RX": "RX", "EXP_PA5": "PA5",
             "EXP_PA10": "PA10", "PANIC_BTN": "PANIC", "EXP_PD2": "PD2", "I2C_SDA": "SDA", "I2C_SCL": "SCL",
             "EXP_PC10": "PC10"}


def _text(board, s, x, y, angle=0, style=SMALL, back=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetLayer(pcbnew.B_SilkS if back else pcbnew.F_SilkS)
    t.SetMirrored(back)
    t.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(style[0]), pcbnew.FromMM(style[0])))
    t.SetTextThickness(pcbnew.FromMM(style[1]))
    t.SetTextAngleDegrees(angle)
    t.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
    board.Add(t)
    return t


def _segment(board, a, b, back=True):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
    s.SetLayer(pcbnew.B_SilkS if back else pcbnew.F_SilkS)
    s.SetWidth(pcbnew.FromMM(LINE))
    s.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(a[0]), pcbnew.FromMM(a[1])))
    s.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(b[0]), pcbnew.FromMM(b[1])))
    board.Add(s)
    return s


def _outline(board, pts):
    return [_segment(board, a, b) for a, b in zip(pts, pts[1:] + pts[:1], strict=True)]


def _art(board):
    """The wizard and the quote on the back (mirrored here: KiCad mirrors text only); returns their boxes."""
    here = os.path.dirname(os.path.abspath(__file__))
    F = pcbnew.FromMM

    def shape(kind):
        s = pcbnew.PCB_SHAPE(board, kind)
        s.SetLayer(pcbnew.B_SilkS)
        s.SetFilled(True)
        s.SetWidth(0)
        board.Add(s)
        return s

    x1, y0, cw, ch = WIZARD
    with open(os.path.join(here, "wizard.txt"), encoding="utf8") as f:
        rows = f.read().rstrip("\n").split("\n")
    for r, row in enumerate(rows):
        c = 0
        while c < len(row):
            d, k = _SHADE[row[c]], c
            if d == 1:      # a run of full cells is one rectangle
                while k + 1 < len(row) and _SHADE[row[k + 1]] == 1:
                    k += 1
            if d:
                w, h = ((k - c + 1) * cw, ch) if d == 1 else (max(0.15, cw * d ** .5), max(0.15, ch * d ** .5))
                cx, cy = x1 - (c + k + 1) / 2 * cw, y0 + (r + .5) * ch
                s = shape(pcbnew.SHAPE_T_RECTANGLE)
                s.SetStart(pcbnew.VECTOR2I(F(cx - w / 2), F(cy - h / 2)))
                s.SetEnd(pcbnew.VECTOR2I(F(cx + w / 2), F(cy + h / 2)))
            c = k + 1
    boxes = [(x1 - max(map(len, rows)) * cw, y0, x1, y0 + len(rows) * ch)]

    cx, base, em, pitch = QUOTE
    with open(os.path.join(here, "art.json")) as f:
        lines = json.load(f)["lines"]
    for i, ln in enumerate(lines):
        y, half = base + i * pitch, ln["width"] * em / 2
        for outline, *holes in ln["shapes"]:
            ps = shape(pcbnew.SHAPE_T_POLY).GetPolyShape()
            ps.NewOutline()
            for px, py in outline:
                ps.Append(F(cx + half - px * em), F(y - py * em))
            for hole in holes:
                ps.NewHole()
                for px, py in hole:
                    ps.Append(F(cx + half - px * em), F(y - py * em))
        boxes.append((cx - half, y - 1.0 * em, cx + half, y + 0.35 * em))
    return boxes


def _box(o, grow=0.0):
    b = o.GetBoundingBox()
    T = pcbnew.ToMM
    return (T(b.GetLeft()) - grow, T(b.GetTop()) - grow, T(b.GetRight()) + grow, T(b.GetBottom()) + grow)


def _glyphs(t):     # KiCad's text box has ~0.25 mm of air round the glyphs
    return _box(t, -0.25)


def _obstacles(fps):
    """What silkscreen must keep off, per silk layer: [(name, box)]."""
    T = pcbnew.ToMM
    obst = {pcbnew.F_SilkS: [], pcbnew.B_SilkS: []}
    for ref, fp in fps.items():
        for pad in fp.Pads():
            for side, cu in ((pcbnew.F_SilkS, pcbnew.F_Cu), (pcbnew.B_SilkS, pcbnew.B_Cu)):
                if pad.IsOnLayer(cu) or pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                    obst[side].append((f"{ref} pad {pad.GetNumber()}", _box(pad)))
        c = fp.GetCourtyard(pcbnew.F_CrtYd)
        if c.OutlineCount() and not fp.GetValue().startswith("ESP32") and not ref.startswith(("J6", "TP")):
            b = c.BBox()        # a label under a part cannot be read (the module and the plug hang over the edge;
            #                     a test point is a bare pad: its pad and its ring are what a label keeps off)
            obst[pcbnew.F_SilkS].append((f"{ref} body", (T(b.GetLeft()), T(b.GetTop()), T(b.GetRight()), T(b.GetBottom()))))
        for g in fp.GraphicalItems():
            if g.GetLayer() in obst and not (isinstance(g, pcbnew.PCB_TEXT) and not g.IsVisible()):
                obst[g.GetLayer()].append((f"{ref} silk", _glyphs(g) if isinstance(g, pcbnew.PCB_TEXT) else _box(g)))
        r = fp.Reference()
        if r.IsVisible() and r.GetLayer() in obst:
            obst[r.GetLayer()].append((f"{ref} label", _glyphs(r)))
    return obst


def _borders(board, fps, front):
    """Draw BORDERS on the front, each line once and only where it is clear; returns (mm drawn, mm asked)."""
    lanes = {}      # (is horizontal, the fixed coordinate) -> [(from, to)]
    for pts in BORDERS:
        for a, b in zip(pts, pts[1:], strict=False):
            h = a[1] == b[1]
            assert h or a[0] == b[0], f"border {a} - {b} is not along an axis"
            lanes.setdefault((h, a[1] if h else a[0]), []).append(tuple(sorted((a[0], b[0]) if h else (a[1], b[1]))))
    boxes = [b for _, b in _obstacles(fps)[pcbnew.F_SilkS]] + [_glyphs(t) for t in front]
    m, step, drawn, asked = 0.25, 0.05, 0.0, 0.0
    for (h, c), spans in sorted(lanes.items()):
        merged = []
        for lo, hi in sorted(spans):
            if merged and lo <= merged[-1][1]:
                merged[-1][1] = max(merged[-1][1], hi)
            else:
                merged.append([lo, hi])
        for lo, hi in merged:
            asked += hi - lo
            run = None
            for i in range(int(round((hi - lo) / step)) + 2):
                v = lo + i * step
                x, y = (v, c) if h else (c, v)
                free = v <= hi + 1e-6 and not any(b[0] - m < x < b[2] + m and b[1] - m < y < b[3] + m for b in boxes)
                if free:
                    run = [v, v] if run is None else [run[0], v]
                elif run:
                    if run[1] - run[0] >= 1.0:      # shorter than that is a speck
                        _segment(board, (run[0], c) if h else (c, run[0]), (run[1], c) if h else (c, run[1]), back=False)
                        drawn += run[1] - run[0]
                    run = None
    return drawn, asked


def _pads(fps, ref):
    return {p.GetNumber(): (pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)) for p in fps[ref].Pads()}


def apply(board, fps, design, rev):
    """Add every label; returns (front texts, back texts, back outline segments) for check()."""
    front, back, lines = [], [], []
    for ref, name in NAMES.items():
        r = fps[ref].Reference()
        p = r.GetPosition()
        x, y, angle = NAME_AT.get(ref, (pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), r.GetTextAngleDegrees()))
        if ref.startswith("TP"):
            x, y = _pads(fps, ref)["1"]
            x, y, angle = x + (0.15 if ref == "TP1" else 0), y + TP_NAME_DY, 0     # VBAT: off the hole's keep-out
        t = _text(board, name, x, y, angle)
        t.own = ref
        front.append(t)
        r.SetVisible(False)
    for ref, fp in fps.items():
        if NO_REF.match(ref):
            fp.Reference().SetVisible(False)
    for s, x, y, angle, style in FRONT:
        front.append(_text(board, s, x, y, angle, style))
    x, y, pitch, rows = LEGEND
    for i, s in enumerate(rows):
        t = _text(board, s, x, y + i * pitch)
        t.Move(pcbnew.VECTOR2I(pcbnew.FromMM(x) - t.GetBoundingBox().GetLeft(), 0))
        front.append(t)
    for ref, (x, y) in PIN1.items():
        t = _text(board, "1", x, y)
        t.own = ref
        front.append(t)
    x, y, pitch, rows = BOARD
    for i, s in enumerate(rows):
        front.append(_text(board, s.format(rev=rev), x, y + i * pitch, 0, NORMAL))
    drawn, asked = _borders(board, fps, front)
    print(f"silk: {drawn:.0f} of {asked:.0f} mm of the front's borders drawn (the rest is under parts)")

    for pts, texts in MAP.values():
        lines += _outline(board, pts)
        for s, x, y, angle, style in texts:
            back.append(_text(board, s, x, y, angle, style, back=True))
    for s, x, y, angle, style in LOOSE + [ORDER_NO]:
        back.append(_text(board, s, x, y, angle, style, back=True))
    back.append(_text(board, f"REV {rev}", 70.6, 15.6, 0, NORMAL, back=True))

    nets = {p.ref: p.pin_net() for p in design.parts}
    for g in list(fps["J6"].GraphicalItems()):      # the footprint's own "1" and "8" on the back: said below
        if isinstance(g, pcbnew.PCB_TEXT) and g.GetLayer() == pcbnew.B_SilkS:
            fps["J6"].Remove(g)
    # the plug: "n NAME" beside pad n, "n+8 NAME" (the pad behind it, on the front) under it
    for n, (_, y) in _pads(fps, "J6").items():
        n = int(n)
        if n > 8:
            continue
        for k, dy in ((n, -0.75), (n + 8, 0.75)):
            t = _text(board, f"{k} {PLUG_PINS[k - 1]}", PIN_X, y + dy, 0, SMALL, back=True)
            b = t.GetBoundingBox()     # mirrored: it reads from its right end
            t.Move(pcbnew.VECTOR2I(pcbnew.FromMM(PIN_X) - b.GetLeft(), 0))
            back.append(t)
    # the test points: each name behind its pad
    for ref, name in NAMES.items():
        if ref.startswith("TP"):
            x, y = _pads(fps, ref)["1"]
            back.append(_text(board, name, x, y, 0, SMALL, back=True))
    # J8: the pin's signal under it, reading up
    for n, (x, y) in _pads(fps, "J8").items():
        name = nets["J8"][n].replace("ESP_", "").replace("3V3_ESP", "3V3")
        t = _text(board, name, x, y + 1.3, 90, SMALL, back=True)
        b = t.GetBoundingBox()
        t.Move(pcbnew.VECTOR2I(0, pcbnew.FromMM(y + 1.0) - b.GetTop()))
        back.append(t)
    # J5: the table, two pins a row as on the header (odd row nearest the edge)
    x0, y0, x1, y1 = EXP_TABLE
    lines += _outline(board, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)])
    back.append(_text(board, "EXP (J5)", (x0 + x1) / 2, y0 + 1.2, 0, NORMAL, back=True))
    for i in range(7):
        y = y0 + 2.8 + i * 1.4
        for k, x in ((2 * i + 1, x1 - 0.5), (2 * i + 2, x1 - 7.6)):   # as read from the back: odd on the left
            t = _text(board, f"{k} {EXP_NAMES[nets['J5'][str(k)]]}", x, y, 0, SMALL, back=True)
            b = t.GetBoundingBox()
            t.Move(pcbnew.VECTOR2I(pcbnew.FromMM(x) - b.GetRight(), 0))
            back.append(t)
    global _art_boxes
    _art_boxes = _art(board)
    return front, back, lines


_art_boxes = []


def check(board, fps, front, back, lines):
    """Everything a label must keep off, as boxes; prints each label that touches one."""
    def hit(a, b):
        return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]

    glyphs, obst = _glyphs, _obstacles(fps)
    obst[pcbnew.B_SilkS] += [("outline", _box(s)) for s in lines]
    for name, a in zip(["the wizard"] + ["the quote"] * 9, _art_boxes, strict=False):
        what = sorted({n for n, b in obst[pcbnew.B_SilkS] if hit(a, b)} | {f"'{t.GetText()}'" for t in back if hit(a, glyphs(t))})
        if what:
            print(f"silk: {name} at {a[0]:.1f},{a[1]:.1f}..{a[2]:.1f},{a[3]:.1f} touches {', '.join(what[:6])}")
    bad = 0
    for side, texts in ((pcbnew.F_SilkS, front), (pcbnew.B_SilkS, back)):
        for i, t in enumerate(texts):
            g = glyphs(t)
            own = getattr(t, "own", None)     # a name sits where its part's reference did: at its own outline
            what = [n for n, b in obst[side] if hit(g, b) and n not in (f"{own} body", f"{own} silk")]
            what += [f"'{o.GetText()}'" for o in texts[:i] if hit(g, glyphs(o))]
            if g[0] < 0.4 or g[1] < 0.4 or g[2] > layout.W - 0.4 or g[3] > layout.H - 0.4:
                what.append("the board edge")
            if what:
                bad += 1
                print(f"silk: '{t.GetText()}' ({'front' if side == pcbnew.F_SilkS else 'back'}) "
                      f"at {g[0]:.1f},{g[1]:.1f}..{g[2]:.1f},{g[3]:.1f} touches {', '.join(sorted(set(what))[:6])}")
    print(f"silk: {len(front)} labels on the front, {len(back)} on the back, {bad} to move")
    return bad
