"""Silkscreen that says what things are, on top of the reference labels (build.py pcb calls apply()).

Both sides carry parts, so both follow the same rules:
- NO_REF: the small parts carry no reference; the chips keep theirs.
- NAMES: the parts a person touches (buttons, test points, connectors) carry their function where
  their reference was. The reference stays on the footprint, hidden.
- Each circuit is named and fenced off: a tile (layout.TILES) gets its name over its first row and its
  outline; FRONT / BACK name what is not a tile (beside its own chip) and BORDERS fence it.
  Every line is drawn only where no pad, part or label is in its way.
- PIN1: a "1" beside pin 1 of a header; BOARD: the board's name and revision.

Where layout.RESERVED / RESERVED_TOP keep parts out: on the back the signal of each of the plug's 16 pins
beside its pad, the board's name and the board house's order number; on the front the signal of each J8
pin under it, J5's pinout as a table over J5, the CONFIG switch's positions under the switch.

All positions are board coordinates in mm (y down), the same on both sides: text on the back is
mirrored by KiCad. check() lists anything that touches a pad, a part, the edge or other silkscreen.
"""

import json
import os
import re

import pcbnew

import layout

SMALL = NORMAL = (1.0, 0.15)        # (height, stroke) mm: the smallest JLCPCB prints well
TITLE, BIG = (1.2, 0.18), (1.5, 0.22)
LINE = 0.15         # outline width
_W, _H, _YC = layout.W, layout.H, layout.YC

# ref -> the text that takes its reference's place
NAMES = {
    "SW6": "CONFIG", "SW1": "RESET", "SW3": "USER", "SW4": "ESP EN", "SW5": "BOOT",     # read together: ESP EN  BOOT
    "TP1": "VBAT", "TP2": "5V", "TP3": "3V3", "TP4": "GND", "TP5": "DLC",
    "TP6": "J+", "TP7": "J-", "TP8": "VJ", "TP9": "K", "TP10": "L",
    "J2": "USB", "J9": "PANIC", "J4": "QWIIC", "J8": "ESP IO", "J11": "POD BUS",
}
# ... and where it goes when it is wider than the room the reference had (x, y, angle)
NAME_AT = {
    "J2": (_W - 8.6, 9.4, 0),           # under the port
    "J8": (layout.ESPIO_AT[0] - 2.4, layout.ESPIO_AT[1] + 3.6, 90),   # before its pin names, reading up
    "J9": (3.3, _H - 4.3, 90),          # left of the socket, reading up
    "SW6": (layout.RESERVED_TOP["config"][0] + 3.0, layout.RESERVED_TOP["config"][1] + 0.9, 0),    # heads its legend
}
TP_NAME_DY = -1.65  # a test point's name: this far above its pad (in its tile's title strip)
# ... but beside or under the pad for the two that stand in a circuit's own tile (dx, dy in board mm)
TP_NAME_AT = {"TP9": (-1.9, 0.0), "TP10": (0.0, 2.3)}
# tiles whose name is not printed though its strip is kept (the test points: each pad has its own name
# there); layout.NO_TITLE lists those with no strip at all
NO_TITLE = {"TP SUPPLY", "TP J1850"}

# The small parts (R, C, D, Q ...) carry no reference: their room goes to the circuits' names and
# outlines. Chips keep theirs. (The placement file and the schematic say which part is which.)
NO_REF = re.compile(r"^(C|D|F|L|Q|R|RN|Y)\d+$")

# front: (text, x, y, angle, style), each beside its own chip
FRONT = [
    ("5V BUCK", layout.BX - 1.0, 2.0, 0, BIG),
    ("12V IN", 4.7, _YC + 5.0, 90, TITLE),       # up the lane between the plug's pads and the TVS
    ("STM32", layout.MCU_AT[0] + 2.0, layout.MCU_AT[1] - 7.8, 0, BIG),            # over the chip, before U1
    ("3V3 LDO", layout.LDO_AT[0] - 1.3, layout.LDO_AT[1] + 4.5, 0, NORMAL),         # under U8, before its reference
    ("FLASH", _W - 21.3, _YC + 17.0, 0, NORMAL),         # under U3
    ("SD CARD", _W - 6.0, _YC + 11.3, 0, NORMAL),       # above the card socket
]
# back: the same for what is not a tile there
BACK = []
# a "1" beside pin 1 of each header (ref -> x, y; on the header's own side)
PIN1 = {"J8": (layout.ESPIO_AT[0], layout.ESPIO_AT[1] - 1.55)}
# front: the board's name and revision, one line each from (x, y) on, reading up, between J4 and J10
BOARD = (60.2, _H - 3.5, 1.4, ["OCTAVE", "OBD", "REV {rev}"])
# back: the board's name, its revision, and where the board house prints its order number (JLCPCB:
# "Specify a location" when ordering), in layout.RESERVED["title"]
BACK_TITLE = ("OCTAVE", "OBD", "REV {rev}", "JLCJLCJLCJLC")

# front: lines between the circuits that are not tiles (polylines along the axes; a shared border is
# written twice and drawn once). Each is drawn only where no pad, part or label is in its way.
_BK = layout.BUCK_BOX
_CX = next(t[2][0] for t in layout.TILES if t[0] == "MS CAN") - layout.GAP / 2     # the CAN tiles' fence: the input chain's right border
_EX, _EY = layout.ESD_AT[0] + 5.9 + layout.GAP / 2, layout.ESD_AT[1] - layout.GAP / 2     # the ESD tile's right and top fence
BORDERS = [
    [(_BK[0] - 0.3, 0.6), (_BK[2] + 0.3, 0.6), (_BK[2] + 0.3, _BK[3] + 0.2), (_EX, _BK[3] + 0.2), (_EX, _EY),
     (_BK[0] - 0.3, _EY), (_BK[0] - 0.3, 0.6)],                                         # buck, stepping round the ESD tile
    [(3.2, _BK[3] + 0.2), (_CX, _BK[3] + 0.2), (_CX, _YC + 19.3), (3.2, _YC + 19.3)],                                            # 12 V input
]
BACK_BORDERS = []
# the CONFIG switch (SW6), under it (layout.RESERVED_TOP["config"]): lines from the top down
LEGEND = ["1 ESP ON", "2 HS TERM", "3 MS TERM", "4 BOOT0", "OFF IN CAR"]

# The plug's edge pads: 1-8 are on the back, 9-16 behind them on the front (pin n + 8 behind pin n)
PLUG_PINS = ["NC", "J+", "MSH", "GND", "GND", "HSH", "K", "NC",
             "NC", "J-", "MSL", "NC", "NC", "HSL", "L", "12V"]
PIN_X = 4.25        # the pin names start here (the pads end at 3.0)

# back, for the fun of it, if a clear patch is left: the wizard of the owner's terminal greeting
# (wizard.txt, one filled rectangle per character, sized by how dark the character is)
WIZARD_CELL = (0.21, 0.35)          # cell width, cell height (50 x 26 cells)
_SHADE = {" ": 0, ".": .12, ":": .18, "-": .22, "=": .3, "+": .38, "*": .48, "#": .58, "%": .64, "&": .7, "@": .8,
          "\u2591": .3, "\u2592": .55, "\u2593": .8, "\u2588": 1}

# J5 (2 x 7): a table in layout.RESERVED_TOP["exp"], over the header, as nothing fits round the pins.
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


def _wizard(board, x1, y0):
    """The wizard on the back, its right edge at x1 (mirrored here: KiCad mirrors text only); returns its box."""
    here = os.path.dirname(os.path.abspath(__file__))
    F = pcbnew.FromMM
    cw, ch = WIZARD_CELL
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
                s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_RECTANGLE)
                s.SetLayer(pcbnew.B_SilkS)
                s.SetFilled(True)
                s.SetWidth(0)
                s.SetStart(pcbnew.VECTOR2I(F(cx - w / 2), F(cy - h / 2)))
                s.SetEnd(pcbnew.VECTOR2I(F(cx + w / 2), F(cy + h / 2)))
                board.Add(s)
            c = k + 1
    return (x1 - max(map(len, rows)) * cw, y0, x1, y0 + len(rows) * ch)


def wizard_size():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "wizard.txt"), encoding="utf8") as f:
        rows = f.read().rstrip("\n").split("\n")
    return max(map(len, rows)) * WIZARD_CELL[0], len(rows) * WIZARD_CELL[1]


WIZARD_NEAR = (62.9, 24.2)       # the wizard's centre goes as near this as the back allows: under the module, where the DLC switch was
QUOTE_EM = (2.0, 1.8, 1.6, 1.45, 1.3, 1.2)     # letter sizes tried, biggest first (mm per em; a capital is 0.75 em tall)


class _Free:
    """The back's clear silkscreen room, as a 0.1 mm raster with an integral image over it."""
    STEP = 0.1

    def __init__(self, boxes, margin=0.25):
        import numpy as np
        self.np = np
        self.nx, self.ny = int(round(_W / self.STEP)), int(round(_H / self.STEP))
        self.occ = np.zeros((self.ny, self.nx), dtype=np.int32)
        e = int(round(0.6 / self.STEP))                 # the board's edge
        self.occ[:e, :] = self.occ[-e:, :] = 1
        self.occ[:, :e] = self.occ[:, -e:] = 1
        for b in boxes:
            self.block(b, margin, False)
        self._sum()

    def block(self, b, margin=0.25, resum=True):
        i = lambda v: int(round(v / self.STEP))     # noqa: E731
        x0, y0 = max(0, i(b[0] - margin)), max(0, i(b[1] - margin))
        x1, y1 = min(self.nx, i(b[2] + margin) + 1), min(self.ny, i(b[3] + margin) + 1)
        if x1 > x0 and y1 > y0:
            self.occ[y0:y1, x0:x1] = 1
        if resum:
            self._sum()

    def _sum(self):
        np = self.np
        self.ii = np.zeros((self.ny + 1, self.nx + 1), dtype=np.int64)
        self.ii[1:, 1:] = self.occ.cumsum(0).cumsum(1)

    def clear(self, w, h, step=0.5):
        """Every clear w x h box on a `step` lattice: arrays (x0, y0) of their top-left corners, board mm."""
        np = self.np
        nw, nh, k = int(round(w / self.STEP)), int(round(h / self.STEP)), int(round(step / self.STEP))
        ii = self.ii
        s = ii[nh:, nw:] - ii[:-nh, nw:] - ii[nh:, :-nw] + ii[:-nh, :-nw]
        ys, xs = np.nonzero(s[::k, ::k] == 0)
        return xs * k * self.STEP, ys * k * self.STEP

    def nearest(self, w, h, centre):
        xs, ys = self.clear(w, h, 0.25)
        if not len(xs):
            return None
        i = ((xs + w / 2 - centre[0]) ** 2 + (ys + h / 2 - centre[1]) ** 2).argmin()
        return float(xs[i]), float(ys[i])


def _quote_words():
    """The quote (art.json) as words: [[(text, width em, low em, high em, shapes shifted to x = 0)] per line]."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "art.json")) as f:
        lines = json.load(f)["lines"]
    out = []
    for ln in lines:
        shapes = sorted(ln["shapes"], key=lambda sh: min(p[0] for p in sh[0]))
        groups, end = [], None
        for sh in shapes:
            x0, x1 = min(p[0] for p in sh[0]), max(p[0] for p in sh[0])
            if end is None or x0 - end > 0.15:      # letters are under 0.1 em apart, words over 0.2
                groups.append([])
                end = x1
            groups[-1].append(sh)
            end = max(end, x1)
        texts = ln["text"].split()
        assert len(groups) == len(texts), (ln["text"], len(groups))
        row = []
        for text, g in zip(texts, groups, strict=True):
            x0 = min(p[0] for sh in g for p in sh[0])
            x1 = max(p[0] for sh in g for p in sh[0])
            lo = min(p[1] for sh in g for p in sh[0])
            hi = max(p[1] for sh in g for p in sh[0])
            row.append((text, x1 - x0, lo, hi, [[[(px - x0, py) for px, py in c] for c in sh] for sh in g]))
        out.append(row)
    return out


def _quote(board, free, quiet=False):
    """The quote on the back, a word at a time, among the parts: each line of it runs left to right (as
    read, so right to left in board coordinates) and steps down as it goes, the lines one under the
    other; every word takes the clear spot nearest its place on that path that still reads in order.
    Returns [(word, box)]."""
    import numpy as np
    F = pcbnew.FromMM
    lines = _quote_words()
    flat = [(k, w) for k, row in enumerate(lines) for w in row]
    for em in QUOTE_EM:
        pitch, gap = 1.25 * em, 0.45 * em
        cands = []
        for k, j, (_text_, wd, lo, hi, _shapes) in ((k, j, w) for k, row in enumerate(lines) for j, w in enumerate(row)):
            row = lines[k]
            total = sum(w[1] for w in row) + 0.5 * (len(row) - 1)
            f = (sum(w[1] + 0.5 for w in row[:j]) + wd / 2) / total
            span = total * em * 1.25                                    # a little looser than typeset
            tx = _W * (0.22 + 0.18 * k) + (f - 0.5) * span              # as read: 0 is the left end
            ty = _H * (0.12 + 0.24 * k) + (f - 0.5) * 2.5 * em           # each line steps down as it goes
            w_mm, h_mm = wd * em, (hi - lo) * em
            xs, ys = free.clear(w_mm, h_mm, 0.25)
            vx = _W - xs - w_mm                                         # as read: the word's left end
            cost = (vx + w_mm / 2 - tx) ** 2 + (1.5 * (ys + h_mm / 2 - ty)) ** 2
            base = ys + hi * em                                         # the baseline
            cands.append((vx, base, w_mm, cost, xs, ys))
        if any(len(c[0]) == 0 for c in cands):
            continue
        best, back_ptr = [cands[0][3]], [None]
        ok = True
        for i in range(1, len(flat)):
            pvx, pb, pw, _, _, _ = cands[i - 1]
            vx, b, _, cost, _, _ = cands[i]
            newline = flat[i][0] != flat[i - 1][0]
            prev = best[-1]
            tot = np.full(len(vx), np.inf)
            ptr = np.zeros(len(vx), dtype=np.int64)
            for c0 in range(0, len(vx), 512):
                sl = slice(c0, c0 + 512)
                if newline:     # a new line: clearly under the last word of the line before
                    okm = b[sl, None] >= pb[None, :] + pitch
                    pair = 0.0
                else:           # the same line: close after the word before, level with it or a step down, never up
                    okm = (vx[sl, None] >= (pvx + pw)[None, :] + gap) & (b[sl, None] >= pb[None, :] - 0.05) & \
                          (b[sl, None] <= pb[None, :] + 1.2 * pitch) & (vx[sl, None] <= (pvx + pw)[None, :] + 6.0)
                    pair = 6.0 * (vx[sl, None] - (pvx + pw)[None, :] - 1.5 * gap) ** 2     # words of a line keep together
                m = np.where(okm, prev[None, :] + pair, np.inf)
                ptr[sl] = m.argmin(1)
                tot[sl] = m.min(1) + cost[sl]
            if not np.isfinite(tot).any():
                ok = False
                break
            best.append(tot)
            back_ptr.append(ptr)
        if not ok:
            continue
        idx = [int(best[-1].argmin())]
        for i in range(len(flat) - 1, 0, -1):
            idx.append(int(back_ptr[i][idx[-1]]))
        idx.reverse()
        placed = []
        for (_k, (text, wd, lo, hi, shapes)), c, i in zip(flat, cands, idx, strict=True):
            x0, y0 = float(c[4][i]), float(c[5][i])
            xr, base = x0 + wd * em, y0 + hi * em      # mirrored here: KiCad mirrors text only
            for outline, *holes in shapes:
                sh = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_POLY)
                sh.SetLayer(pcbnew.B_SilkS)
                sh.SetFilled(True)
                sh.SetWidth(0)
                board.Add(sh)
                ps = sh.GetPolyShape()
                ps.NewOutline()
                for px, py in outline:
                    ps.Append(F(xr - px * em), F(base - py * em))
                for hole in holes:
                    ps.NewHole()
                    for px, py in hole:
                        ps.Append(F(xr - px * em), F(base - py * em))
            placed.append((text, (x0, y0, xr, y0 + (hi - lo) * em)))
        print(f"silk: the quote's {len(placed)} words are on the back at {em} mm per em")
        return placed
    if not quiet:
        print("silk: no room on the back for the quote in reading order: left out")
    return []


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
        flipped = fp.IsFlipped()
        c = fp.GetCourtyard(pcbnew.B_CrtYd if flipped else pcbnew.F_CrtYd)
        if c.OutlineCount() and not fp.GetValue().startswith("ESP32") and not ref.startswith(("J6", "TP")):
            b = c.BBox()        # a label under a part cannot be read (the module and the plug hang over the edge;
            #                     a test point is a bare pad: its pad and its ring are what a label keeps off)
            obst[pcbnew.B_SilkS if flipped else pcbnew.F_SilkS].append(
                (f"{ref} body", (T(b.GetLeft()), T(b.GetTop()), T(b.GetRight()), T(b.GetBottom()))))
        for g in fp.GraphicalItems():
            if g.GetLayer() in obst and not (isinstance(g, pcbnew.PCB_TEXT) and not g.IsVisible()):
                obst[g.GetLayer()].append((f"{ref} silk", _glyphs(g) if isinstance(g, pcbnew.PCB_TEXT) else _box(g)))
        r = fp.Reference()
        if r.IsVisible() and r.GetLayer() in obst:
            obst[r.GetLayer()].append((f"{ref} label", _glyphs(r)))
    return obst


def _borders(board, fps, texts, polylines, back):
    """Draw the polylines on one side, each line once and only where it is clear; returns (mm drawn, mm asked)."""
    lanes = {}      # (is horizontal, the fixed coordinate) -> [(from, to)]
    for pts in polylines:
        for a, b in zip(pts, pts[1:], strict=False):
            h = a[1] == b[1]
            assert h or a[0] == b[0], f"border {a} - {b} is not along an axis"
            lanes.setdefault((h, round(a[1] if h else a[0], 3)), []).append(tuple(sorted((a[0], b[0]) if h else (a[1], b[1]))))
    boxes = [b for _, b in _obstacles(fps)[pcbnew.B_SilkS if back else pcbnew.F_SilkS]] + [_glyphs(t) for t in texts]
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
                free = v <= hi + 1e-6 and 0.4 < x < _W - 0.4 and 0.4 < y < _H - 0.4 and \
                    not any(b[0] - m < x < b[2] + m and b[1] - m < y < b[3] + m for b in boxes)
                if free:
                    run = [v, v] if run is None else [run[0], v]
                elif run:
                    if run[1] - run[0] >= 1.0:      # shorter than that is a speck
                        _segment(board, (run[0], c) if h else (c, run[0]), (run[1], c) if h else (c, run[1]), back=back)
                        drawn += run[1] - run[0]
                    run = None
    return drawn, asked


def _tile_labels(fps, on_back, rect, title, chips):
    """One tile's name and its chips' references. The name goes to the middle, the left or the right of its
    strip, whichever lets most references stand clear inside the tile; each reference takes the clear spot
    nearest its chip. A reference with no clear spot goes on its chip's body, between the pads, where it
    reads until the chip is fitted; a chip too small for that loses it (printed)."""
    import numpy as np
    F, T = pcbnew.FromMM, pcbnew.ToMM
    x0, y0, x1, y1 = rect
    lay = pcbnew.B_SilkS if on_back else pcbnew.F_SilkS
    boxes = [b for n, b in _obstacles(fps)[lay]
             if not (n.endswith(" label") and n[:-6] in chips) and b[0] < x1 and b[2] > x0 and b[1] < y1 and b[3] > y0]
    st = 0.05
    nx, ny = int(round((x1 - x0) / st)), int(round((y1 - y0) / st))

    def grid(extra):
        occ = np.zeros((ny, nx), dtype=np.int32)
        e = int(round(0.1 / st))
        occ[:e, :] = occ[-e:, :] = 1
        occ[:, :e] = occ[:, -e:] = 1
        for b in boxes + extra:
            i0, j0 = max(0, int((b[0] - 0.05 - x0) / st)), max(0, int((b[1] - 0.05 - y0) / st))
            i1, j1 = min(nx, int((b[2] + 0.05 - x0) / st) + 1), min(ny, int((b[3] + 0.05 - y0) / st) + 1)
            if i1 > i0 and j1 > j0:
                occ[j0:j1, i0:i1] = 1
        return occ

    def spot(occ, w, h, centre):
        nw, nh = int(round(w / st)) + 1, int(round(h / st)) + 1
        if nw >= nx or nh >= ny:
            return None
        ii = np.zeros((ny + 1, nx + 1), dtype=np.int64)
        ii[1:, 1:] = occ.cumsum(0).cumsum(1)
        sm = ii[nh:, nw:] - ii[:-nh, nw:] - ii[nh:, :-nw] + ii[:-nh, :-nw]
        ys, xs = np.nonzero(sm == 0)
        if not len(xs):
            return None
        cx, cy = x0 + xs * st + w / 2, y0 + ys * st + h / 2
        # (a spot beside or under the chip rather than one as near but off its corner)
        i = (np.minimum(abs(cx - centre[0]), abs(cy - centre[1])) * 0.5 + np.hypot(cx - centre[0], cy - centre[1])).argmin()
        return float(cx[i]), float(cy[i])

    sizes = {}
    for c in chips:
        r = fps[c].Reference()
        r.SetTextAngleDegrees(0)
        g = _glyphs(r)
        sizes[c] = (g[2] - g[0], g[3] - g[1])
    options = [None]
    if title:
        ty = title.GetPosition().y
        tw = T(title.GetBoundingBox().GetWidth())
        options = [(x0 + x1) / 2, x0 + 0.15 + tw / 2, x1 - 0.15 - tw / 2]
    best = None
    for k, tx in enumerate(options):
        extra = []
        if title:
            title.SetPosition(pcbnew.VECTOR2I(F(tx), ty))
            tb = _glyphs(title)
            if any(tb[0] < b[2] and b[0] < tb[2] and tb[1] < b[3] and b[1] < tb[3] for b in boxes):
                continue
            extra.append((tb[0] - 0.5, tb[1], tb[2] + 0.5, tb[3]))      # (a letter's width between name and reference)
        occ = grid(extra)
        placed, dist = {}, 0.0
        for c in chips:
            bb = fps[c].GetBoundingBox(False)
            centre = (T(bb.GetCenter().x), T(bb.GetCenter().y))
            w, h = sizes[c]
            p = spot(occ, w, h, centre)
            reach = max(T(bb.GetWidth()), T(bb.GetHeight())) / 2 + 1.6      # no further: it would read as a neighbour's
            if p and max(abs(p[0] - centre[0]), abs(p[1] - centre[1])) <= reach:
                placed[c] = p
                dist += np.hypot(p[0] - centre[0], p[1] - centre[1])
                i0, j0 = int((p[0] - w / 2 - 0.1 - x0) / st), int((p[1] - h / 2 - 0.1 - y0) / st)
                occ[max(0, j0):j0 + int((h + 0.2) / st) + 1, max(0, i0):i0 + int((w + 0.2) / st) + 1] = 1
        key = (len(chips) - len(placed), round(dist, 1), k)
        if best is None or key < best[0]:
            best = (key, tx, placed)
    if best is None:        # the name fits nowhere clear in its strip: leave it in the middle (check() says so)
        best = ((0,), options[0], {})
    _, tx, placed = best
    if title:
        title.SetPosition(pcbnew.VECTOR2I(F(tx), ty))
    for c in chips:
        r = fps[c].Reference()
        if c in placed:
            r.SetPosition(pcbnew.VECTOR2I(F(placed[c][0]), F(placed[c][1])))
            continue
        pads = [p.GetBoundingBox() for p in fps[c].Pads()]
        r.SetPosition(fps[c].GetBoundingBox(False).GetCenter())
        g = r.GetBoundingBox()
        g.Inflate(-F(0.2))
        if any(g.Intersects(pb) for pb in pads):
            r.SetVisible(False)
            print(f"silk: {c} has no room for its reference: hidden")
        else:
            print(f"silk: {c}'s reference is on its body (no room beside it in its block)")


def _pads(fps, ref):
    return {p.GetNumber(): (pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)) for p in fps[ref].Pads()}


def apply(board, fps, design, rev):
    """Add every label; returns (front texts, back texts, []) for check()."""
    front, back = [], []

    def put(s, x, y, angle=0, style=SMALL, on_back=False, own=None):
        t = _text(board, s, x, y, angle, style, back=on_back)
        if own:
            t.own = own
        (back if on_back else front).append(t)
        return t

    for ref, name in NAMES.items():
        r = fps[ref].Reference()
        p = r.GetPosition()
        flipped = fps[ref].IsFlipped()
        x, y, angle = NAME_AT.get(ref, (pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), r.GetTextAngleDegrees()))
        if ref.startswith("TP"):
            x, y = _pads(fps, ref)["1"]
            dx, dy = TP_NAME_AT.get(ref, (0.0, TP_NAME_DY))
            x, y, angle = x + dx, y + dy, 0
        put(name, x, y, angle, on_back=flipped, own=ref)
        r.SetVisible(False)
    for ref, fp in fps.items():
        if NO_REF.match(ref) or ref == "J5":        # (J5: its table's heading names it)
            fp.Reference().SetVisible(False)
    for s, x, y, angle, style, *own in FRONT:      # (a sixth item: the part the text stands on)
        put(s, x, y, angle, style, own=own[0] if own else None)
    for s, x, y, angle, style in BACK:
        put(s, x, y, angle, style, on_back=True)
    for ref, (x, y) in PIN1.items():
        put("1", x, y, on_back=fps[ref].IsFlipped(), own=ref)
    x, y, pitch, rows = BOARD
    for i, s in enumerate(rows):
        put(s.format(rev=rev), x + i * pitch, y, 90, NORMAL)
    # the tiles: each one's name over its first row, its outline round it (half the gap between two
    # tiles out, so neighbours share the line), its chips' references wherever the tile has room
    fences = {False: list(BORDERS), True: list(BACK_BORDERS)}
    tiles = []
    for name, (side, (x0, y0, x1, y1)) in layout.TILE_RECTS.items():
        on_back = side == "B"
        t = None
        if name not in NO_TITLE and name not in layout.NO_TITLE:
            t = put(name, (x0 + x1) / 2, y0 + layout.TILE_MARGIN + 0.7, 0, SMALL, on_back=on_back)
        rows = next(tl for tl in layout.TILES if tl[0] == name)[3]
        chips = [c for c in (s.lstrip("<").partition("@")[0] for row in rows for s in row) if re.match(r"^(U|J)\d+$", c)]
        tiles.append((on_back, (x0, y0, x1, y1), t, [c for c in chips if fps[c].Reference().IsVisible()]))
        g = layout.GAP / 2
        fences[on_back].append([(x0 - g, y0 - g), (x1 + g, y0 - g), (x1 + g, y1 + g), (x0 - g, y1 + g), (x0 - g, y0 - g)])
    for on_back, rect, t, chips in tiles:
        _tile_labels(fps, on_back, rect, t, chips)

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
            t = put(f"{k} {PLUG_PINS[k - 1]}", PIN_X, y + dy, 0, SMALL, on_back=True)
            b = t.GetBoundingBox()     # mirrored: it reads from its right end
            t.Move(pcbnew.VECTOR2I(pcbnew.FromMM(PIN_X) - b.GetLeft(), 0))
    # J8: the pin's signal under it, reading up
    for n, (x, y) in _pads(fps, "J8").items():
        name = nets["J8"][n].replace("3V3_ESP", "3V3").replace("ESP_", "")
        t = put(name, x, y + 2.3, 90, SMALL)
        b = t.GetBoundingBox()
        t.Move(pcbnew.VECTOR2I(0, pcbnew.FromMM(y + 1.75) - b.GetTop()))
    # J5: the table, in three columns of condensed letters
    x0, y0, x1, y1 = layout.RESERVED_TOP["exp"]
    fences[False].append([(x0, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0)])
    put("EXP (J5)", (x0 + x1) / 2, y0 + 0.75, 0, NORMAL)
    x = x0 + 0.4
    for col in range(3):
        cells = []
        for k in range(1 + 5 * col, min(15, 6 + 5 * col)):
            t = put(f"{k} {EXP_NAMES[nets['J5'][str(k)]]}", x, y0 + 2.4 + ((k - 1) % 5) * 1.3, 0, SMALL)
            t.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(0.8), pcbnew.FromMM(1.0)))
            t.Move(pcbnew.VECTOR2I(pcbnew.FromMM(x) - t.GetBoundingBox().GetLeft(), 0))
            cells.append(t)
        x = max(pcbnew.ToMM(t.GetBoundingBox().GetRight()) for t in cells) + 0.35
    # the CONFIG switch's positions, under it
    x0, y0, x1, y1 = layout.RESERVED_TOP["config"]
    for i, s in enumerate(LEGEND):
        t = put(s, x0 + 0.3, y0 + 2.2 + i * 1.3, 0, SMALL)
        t.Move(pcbnew.VECTOR2I(pcbnew.FromMM(x0 + 0.3) - t.GetBoundingBox().GetLeft(), 0))
    # the board's name, its revision and the order number's place (the last reading up the edge)
    x0, y0, x1, y1 = layout.RESERVED["title"]
    for i, s in enumerate(BACK_TITLE[:-1]):
        put(s.format(rev=rev), x1 - 2.75, y0 + 1.0 + i * 1.4, 0, NORMAL, on_back=True)
    put(BACK_TITLE[-1], x0 + 1.0, y0 + 5.9, 90, NORMAL, on_back=True)
    # the wizard and the quote, on the back wherever the parts and the labels leave room
    global _art_boxes, _art_names
    _art_boxes, _art_names = [], []
    edges = [(min(a, c) - 0.1, min(b, e) - 0.1, max(a, c) + 0.1, max(b, e) + 0.1)
             for pts in fences[True] for (a, b), (c, e) in zip(pts, pts[1:], strict=False)]
    free = _Free([b for _, b in _obstacles(fps)[pcbnew.B_SilkS]] + [_glyphs(t) for t in back] + edges)
    w, h = wizard_size()
    spot = free.nearest(w + 0.2, h + 0.2, WIZARD_NEAR)
    if spot:
        _art_boxes.append(_wizard(board, spot[0] + 0.1 + w, spot[1] + 0.1))
        _art_names.append("the wizard")
        free.block(_art_boxes[-1])
    else:
        print(f"silk: no clear {w:.1f} x {h:.1f} mm patch on the back for the wizard: left out")
    # the quote: first with every tile closed to it (the words between the blocks, not in them); if the
    # lines do not fit that way at any size, with the tiles' free corners open too
    import copy
    strict = copy.copy(free)
    strict.occ = free.occ.copy()
    for _name, (side, rect) in layout.TILE_RECTS.items():
        if side == "B":
            strict.block(rect, 0.0, False)
    strict._sum()
    words = _quote(board, strict, quiet=True) or _quote(board, free)
    for word, box in words:
        _art_boxes.append(box)
        _art_names.append(f"the quote's '{word}'")
    for on_back, texts in ((False, front), (True, back)):
        drawn, asked = _borders(board, fps, texts, fences[on_back], on_back)
        print(f"silk: {drawn:.0f} of {asked:.0f} mm of the {'back' if on_back else 'front'}'s borders drawn "
              f"(the rest is under parts)")
    return front, back, []


_art_boxes, _art_names = [], []


def check(board, fps, front, back, lines):
    """Everything a label must keep off, as boxes; prints each label that touches one."""
    def hit(a, b):
        return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]

    glyphs, obst = _glyphs, _obstacles(fps)
    obst[pcbnew.B_SilkS] += [("outline", _box(s)) for s in lines]
    for name, a in zip(_art_names, _art_boxes, strict=True):
        what = sorted({n for n, b in obst[pcbnew.B_SilkS] if hit(a, b)} | {f"'{t.GetText()}'" for t in back if hit(a, glyphs(t))})
        if what:
            print(f"silk: {name} at {a[0]:.1f},{a[1]:.1f}..{a[2]:.1f},{a[3]:.1f} touches {', '.join(what[:6])}")
    bad = 0
    for side, texts in ((pcbnew.F_SilkS, front), (pcbnew.B_SilkS, back)):
        for i, t in enumerate(texts):
            g = glyphs(t)
            own = getattr(t, "own", None)     # a name sits where its part's reference did: at its own outline
            what = [n for n, b in obst[side] if hit(g, b) and n not in (f"{own} body", f"{own} silk", f"{own} label")]
            what += [f"'{o.GetText()}'" for o in texts[:i] if hit(g, glyphs(o))]
            if g[0] < 0.4 or g[1] < 0.4 or g[2] > layout.W - 0.4 or g[3] > layout.H - 0.4:
                what.append("the board edge")
            if what:
                bad += 1
                print(f"silk: '{t.GetText()}' ({'front' if side == pcbnew.F_SilkS else 'back'}) "
                      f"at {g[0]:.1f},{g[1]:.1f}..{g[2]:.1f},{g[3]:.1f} touches {', '.join(sorted(set(what))[:6])}")
    print(f"silk: {len(front)} labels on the front, {len(back)} on the back, {bad} to move")
    return bad
