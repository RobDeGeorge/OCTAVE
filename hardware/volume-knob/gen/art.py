"""Silkscreen for the fun of it, on the top, in the band between the LED ring and the rim (build.py pcb calls
apply()): the wizard of the owner's terminal greeting (wizard.txt, as on the OBD adapter), a line of the
Ring's inscription round the lower right of the ring of LEDs, the project's name and the revision.

The words are outlines (art.json, written once from the font by mkart.py), bent round the board's centre.
The wizard is one filled rectangle per character of wizard.txt that is dark enough: at this size (6.5 mm)
a cell is 0.13 mm wide, so there is no shading, only the silhouette.
"""
import json
import math
import os

import pcbnew

import layout

HERE = os.path.dirname(os.path.abspath(__file__))
F = pcbnew.FromMM

# text, angle of its middle (degrees clockwise from +x, y down: 90 is the USB end), baseline radius, em size,
# and which way up: "in" = tops towards the centre (reads upright at the bottom of the board), "out" = tops
# towards the rim (reads upright at the top)
WORDS = [
    ("One Ring", 45.0, 19.85, 2.0, "in"),
    ("to rule them all", 45.0, 22.7, 2.0, "in"),
    ("OCTAVE", 235.0, 19.6, 1.55, "out"),
    ("Rev 0", 305.0, 19.6, 1.55, "out"),
]
WIZARD = (-22.5, -4.3, 0.13, 0.244)     # left x, top y (mm from the centre), cell width, cell height (50 x 26 cells)
WIZARD_INK = 0.35                       # a character at least this dark is printed
_SHADE = {" ": 0, ".": .12, ":": .18, "-": .22, "=": .3, "+": .38, "*": .48, "#": .58, "%": .64, "&": .7, "@": .8,
          "░": .3, "▒": .55, "▓": .8, "█": 1}


def _shape(board, kind):
    s = pcbnew.PCB_SHAPE(board, kind)
    s.SetLayer(pcbnew.F_SilkS)
    s.SetFilled(True)
    s.SetWidth(0)
    board.Add(s)
    return s


def apply(board):
    cx, cy = layout.CX, layout.CY
    x0, y0, cw, ch = WIZARD
    with open(os.path.join(HERE, "wizard.txt"), encoding="utf8") as f:
        rows = f.read().rstrip("\n").split("\n")
    for r, row in enumerate(rows):
        c = 0
        while c < len(row):
            k = c
            if _SHADE[row[c]] >= WIZARD_INK:        # a run of printed cells is one rectangle
                while k + 1 < len(row) and _SHADE[row[k + 1]] >= WIZARD_INK:
                    k += 1
                s = _shape(board, pcbnew.SHAPE_T_RECTANGLE)
                s.SetStart(pcbnew.VECTOR2I(F(cx + x0 + c * cw), F(cy + y0 + r * ch)))
                s.SetEnd(pcbnew.VECTOR2I(F(cx + x0 + (k + 1) * cw), F(cy + y0 + (r + 1) * ch + 0.01)))
            c = k + 1

    with open(os.path.join(HERE, "art.json")) as f:
        lines = {ln["text"]: ln for ln in json.load(f)["lines"]}
    for text, mid, rb, em, up in WORDS:
        ln = lines[text]
        half = ln["width"] * em / 2

        def bend(px, py, mid=mid, rb=rb, em=em, up=up, half=half):
            s = px * em - half                      # along the baseline, from the word's middle
            a = math.radians(mid) + (s / rb if up == "out" else -s / rb)
            r = rb + (py * em if up == "out" else -py * em)
            return F(cx + r * math.cos(a)), F(cy + r * math.sin(a))

        for outline, *holes in ln["shapes"]:
            ps = _shape(board, pcbnew.SHAPE_T_POLY).GetPolyShape()
            ps.NewOutline()
            for px, py in outline:
                ps.Append(*bend(px, py))
            for hole in holes:
                ps.NewHole()
                for px, py in hole:
                    ps.Append(*bend(px, py))
