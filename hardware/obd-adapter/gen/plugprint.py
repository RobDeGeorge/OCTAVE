"""A 1:1 print of the plug's adapter board, to lay a bought J1962 plug on before the boards are ordered.

Reads obd-adapter-plug.kicad_pcb (so it is the artwork that goes to the fab, not a redrawing of it) and
writes fab/obd-adapter-plug-rev0-print-1to1.pdf: US Letter, everything within the area an A4 sheet also
prints. Two views of the adapter's front (the plug's side): one to cut out and push the plug's tails
through, one with the dimensions to measure a plug against. `build.py fab` runs it.

Print at 100 % ("actual size", not "fit to page") and check the 50 mm bar with a ruler first.
"""
import math
import os
import sys

import matplotlib
matplotlib.use("pdf")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import FancyBboxPatch, Rectangle  # noqa: E402

PAGE = (215.9, 279.4)           # US Letter, mm
BLADE = (1.8, 0.8)              # the plug's contact tails (Jiatel drawing): 0.8 x 1.8 mm blades


def _stadium(ax, cx, cy, w, h, **kw):
    r = min(w, h) / 2
    ax.add_patch(FancyBboxPatch((cx - w / 2 + r, cy - h / 2 + r), w - 2 * r, h - 2 * r,
                                boxstyle=f"round,pad={r}", **kw))


def _dim(ax, x1, y1, x2, y2, text, off=0.0, **kw):
    """A dimension line between two points with its figure beside the middle."""
    ax.annotate("", (x1, y1), (x2, y2), arrowprops=dict(arrowstyle="<->", lw=0.35, color="#0050a0",
                                                         shrinkA=0, shrinkB=0))
    vertical = abs(x1 - x2) < 1e-6
    ax.text((x1 + x2) / 2 + (off if vertical else 0), (y1 + y2) / 2 + (0 if vertical else off), text,
            fontsize=5.5, color="#0050a0", ha="left" if vertical else "center", va="center",
            **kw)


def _view(ax, board, pcbnew, ox, oy, dims):
    """The adapter's front at (ox, oy) = its top-left corner on the page (mm, y down)."""
    T = pcbnew.ToMM
    bb = board.GetBoardEdgesBoundingBox()
    x0, y0 = T(bb.GetLeft()) + 0.05, T(bb.GetTop()) + 0.05      # the bounding box includes the line width
    w, h = T(bb.GetWidth()) - 0.1, T(bb.GetHeight()) - 0.1
    X = lambda x: ox + (x - x0)
    Y = lambda y: oy + (y - y0)
    ax.add_patch(Rectangle((ox, oy), w, h, fill=False, lw=0.5, ec="black"))
    fp = board.GetFootprints()[0]
    for g in fp.GraphicalItems():
        layer = board.GetLayerName(g.GetLayer())
        if g.GetClass() == "PCB_SHAPE" and layer == "F.Silkscreen":      # the shroud's D outline
            ax.plot([X(T(g.GetStart().x)), X(T(g.GetEnd().x))], [Y(T(g.GetStart().y)), Y(T(g.GetEnd().y))],
                    color="#808080", lw=0.5)
        elif g.GetClass() == "PCB_TEXT" and layer == "F.Silkscreen":
            ax.text(X(T(g.GetPosition().x)), Y(T(g.GetPosition().y)), g.GetText(), fontsize=3.6, ha="center",
                    va="center", color="#404040")
    edge = [g for g in fp.GraphicalItems() if board.GetLayerName(g.GetLayer()) == "Edge.Cuts"]
    xs = [T(p.x) for g in edge for p in (g.GetStart(), g.GetEnd())]
    ys = [T(p.y) for g in edge for p in (g.GetStart(), g.GetEnd())]
    sw, sh = max(xs) - min(xs) + (max(ys) - min(ys)), max(ys) - min(ys)     # two half-round ends
    scx, scy = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2
    _stadium(ax, X(scx), Y(scy), sw, sh, fc="#d8d8d8", ec="black", lw=0.4)
    rows = {}
    for p in fp.Pads():
        if p.GetAttribute() != pcbnew.PAD_ATTRIB_PTH:
            continue
        px, py = T(p.GetPosition().x), T(p.GetPosition().y)
        pw, ph = T(p.GetSize(pcbnew.F_Cu).x), T(p.GetSize(pcbnew.F_Cu).y)
        dw, dh = T(p.GetDrillSize().x), T(p.GetDrillSize().y)
        if p.GetNumber() == "1":                                    # pin 1: the rounded-square pad
            ax.add_patch(FancyBboxPatch((X(px) - pw / 2 + 0.3, Y(py) - ph / 2 + 0.3), pw - 0.6, ph - 0.6,
                                        boxstyle="round,pad=0.3", fc="none", ec="#b06000", lw=0.4))
        else:
            _stadium(ax, X(px), Y(py), pw, ph, fc="none", ec="#b06000", lw=0.4)
        _stadium(ax, X(px), Y(py), dw, dh, fc="black", ec="black", lw=0.2)
        ax.add_patch(Rectangle((X(px) - BLADE[0] / 2, Y(py) - BLADE[1] / 2), *BLADE, fc="white", ec="none"))
        rows.setdefault(round(py, 3), []).append((px, p.GetNumber(), dw, dh))
    (ya, a), (yb, b) = sorted(rows.items())
    a.sort()
    if dims:
        _dim(ax, X(a[0][0]), Y(ya) - 6.3, X(a[1][0]), Y(ya) - 6.3, f"{a[1][0] - a[0][0]:.2f}", off=-1.6)
        _dim(ax, X(a[0][0]), Y(ya) - 9.3, X(a[-1][0]), Y(ya) - 9.3,
             f"{a[-1][0] - a[0][0]:.2f}  (7 x {a[1][0] - a[0][0]:.2f})", off=-1.3)
        _dim(ax, ox + w + 3.0, Y(ya), ox + w + 3.0, Y(yb), f"{yb - ya:.2f}", off=0.8)
        _dim(ax, ox, oy + h + 3.0, ox + w, oy + h + 3.0, f"{w:.1f}", off=1.4)
        _dim(ax, ox - 3.0, oy, ox - 3.0, oy + h, f"{h:.1f}", off=-5.5)
        ax.text(ox + w + 9.5, Y(ya) - 2.0, f"slots {a[0][2]:.1f} x {a[0][3]:.1f}\n(white: a {BLADE[1]:.1f} x {BLADE[0]:.1f} blade)",
                fontsize=5.5, color="#0050a0", va="center")
        ax.text(ox + w + 9.5, Y(yb) + 2.0, f"tab slot {sw:.1f} x {sh:.1f}\n(main board, 1.6 mm)", fontsize=5.5,
                color="#0050a0", va="center")
    return w, h


def main(pcb, out):
    import pcbnew
    board = pcbnew.LoadBoard(pcb)
    fig = plt.figure(figsize=(PAGE[0] / 25.4, PAGE[1] / 25.4))
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, PAGE[0])
    ax.set_ylim(PAGE[1], 0)
    ax.axis("off")
    L = 25.0                                                        # left margin: inside A4's printable area too
    ax.text(L, 22, "OCTAVE OBD-II adapter Rev 0: the plug's adapter board, 1:1", fontsize=11, weight="bold")
    ax.text(L, 28, "Print at 100 % (\"actual size\", not \"fit to page\"). Measure the bar below with a ruler before "
                   "trusting anything else on this sheet.", fontsize=7)
    # the check bars
    y = 40
    ax.add_patch(Rectangle((L, y), 50, 2.5, fc="black"))
    for i in range(0, 51, 5):
        ax.plot([L + i, L + i], [y + 2.5, y + (5.0 if i % 10 == 0 else 3.8)], color="black", lw=0.4)
        if i % 10 == 0:
            ax.text(L + i, y + 7.2, str(i), fontsize=5.5, ha="center")
    ax.text(L + 54, y + 2.2, "50.0 mm", fontsize=7)
    ax.add_patch(Rectangle((L + 80, y), 50.8, 2.5, fc="black"))
    for i in range(0, 5):
        ax.plot([L + 80 + i * 12.7] * 2, [y + 2.5, y + (5.0 if i % 2 == 0 else 3.8)], color="black", lw=0.4)
    ax.text(L + 134, y + 2.2, "2 in", fontsize=7)

    ax.text(L, 62, "A. Cut out and try the plug on it", fontsize=9, weight="bold")
    ax.text(L, 67, "Front of the adapter (the plug's side). Cut along the outline, prick the 16 black slots, push the plug's "
                   "tails through.", fontsize=6.5)
    w, h = _view(ax, board, pcbnew, L + 20, 76, dims=False)
    ax.text(L, 110, "B. The same view with its dimensions (mm)", fontsize=9, weight="bold")
    _view(ax, board, pcbnew, L + 20, 128, dims=True)

    checks = [
        "All 16 tails go into the slots without bending: 2 rows of 8, 4.00 mm apart, rows 8.60 mm apart "
        "(tails 0.8 x 1.8 mm, slots 2.6 x 1.3 mm).",
        "Pin 1 lands on the square-cornered pad. Seen from the plug's mating face pin 1 is at the top right, and "
        "pins 1-8 are on the wide side of the D.",
        "The plug's shroud matches the grey D outline (40.26 mm wide side, 17.8 mm across), wide side to "
        "\"WIDE SIDE 1-8\".",
        "The plug's flange (37.6 x 17.2 mm) sits flat on the paper and stays inside the 40 x 18 mm outline.",
        "The tails stand about 4.0 mm out of the plug's back: 1.6 mm of board leaves 2.4 mm to solder.",
        "Look into the back of the plug between the rows: the grey 36 x 2 mm slot is where the main board's tab "
        "comes through (1.3 mm deep, it stops 0.3 mm below the front face). Nothing of the plug may sit in it.",
    ]
    ax.text(L, 166, "What to check", fontsize=9, weight="bold")
    for i, c in enumerate(checks):
        ax.text(L, 173 + i * 8.5, f"{i + 1}.", fontsize=7, va="top")
        ax.text(L + 5, 173 + i * 8.5, _wrap(c, 118), fontsize=7, va="top")
    ax.text(L, 232, "The footprint was drawn from the Jiatel CR-PG001 drawing (OBDII-Male-16pin.pdf rev A). If a plug from "
                    "another maker differs\nin any of 1-5, change gen/mklib.py and rebuild before ordering; if it fits, "
                    "order the adapter board with the main board.",
            fontsize=6.5, color="#404040", va="top")
    fig.savefig(out, format="pdf")
    print("wrote", os.path.relpath(out))


def _wrap(s, n):
    out, line = [], ""
    for word in s.split():
        if len(line) + len(word) + 1 > n:
            out.append(line)
            line = word
        else:
            line = (line + " " + word).strip()
    return "\n".join(out + [line])


if __name__ == "__main__":
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "obd-adapter-plug.kicad_pcb"),
         sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "fab", "obd-adapter-plug-rev0-print-1to1.pdf"))
