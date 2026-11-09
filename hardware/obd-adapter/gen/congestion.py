"""Routing congestion estimate for a placed board, before routing it (RUDY).

RUDY (Rectangular Uniform wire DensitY, Spindler & Johannes 2007): each net needs about its
bounding box's half-perimeter of wire; spread that wire evenly over the box and add up every
net's share per grid cell. Dividing by what a cell can carry (its width over the track pitch,
times the signal layers) gives a utilisation map: above ~0.7 the router starts to struggle, above
1 it will not finish. Seconds instead of a 20-minute route, so placement can be tuned against it.

Calibrated on this board: on two signal layers a placement routed clean at peak 0.56 / p95 0.32
and left 37 connections at peak 0.67 / p95 0.41, so keep peak <= 0.55 and p95 <= 0.33.

Nets on planes (GND on In1, 3V3 on In2) are left out: each pad gets a via down instead, which
`via_load` counts as blocked area round the pad. Pads block their own layer.

    python3 congestion.py [board.kicad_pcb] [heatmap.png]
"""

import os
import sys

import pcbnew

PLANES = ("GND", "3V3")
PITCH = 0.30            # track width 0.15 + clearance 0.15 (the Default net class)
# signal layers: F.Cu and B.Cu (In1 / In2 are planes); OBD_IN2=signal (build.py) adds In2
LAYERS = 3 if os.environ.get("OBD_IN2") == "signal" else 2
CELL = 2.0              # mm


def rudy(board, cell=CELL):
    """Returns (grid of utilisation, x0, y0, cell, nx, ny)."""
    T = pcbnew.ToMM
    bb = board.GetBoardEdgesBoundingBox()
    x0, y0, x1, y1 = T(bb.GetLeft()), T(bb.GetTop()), T(bb.GetRight()), T(bb.GetBottom())
    nx, ny = int((x1 - x0) / cell) + 1, int((y1 - y0) / cell) + 1
    demand = [[0.0] * nx for _ in range(ny)]
    blocked = [[0.0] * nx for _ in range(ny)]       # area blocked, summed over both signal layers
    nets = {}
    for fp in board.GetFootprints():
        for p in fp.Pads():
            q = p.GetPosition()
            x, y = T(q.x), T(q.y)
            n = p.GetNetname()
            if n and not n.startswith("unconnected") and n not in PLANES:
                nets.setdefault(n, []).append((x, y))
            # pad area blocks routing on its layer(s); a plane-net pad's fan-out via blocks both
            s = p.GetBoundingBox()
            a = T(s.GetWidth()) * T(s.GetHeight())
            layers = (1 if p.IsOnLayer(pcbnew.F_Cu) else 0) + (1 if p.IsOnLayer(pcbnew.B_Cu) else 0)
            if n in PLANES:
                a, layers = a + 0.9 * 0.9, max(layers, 2)
            i, j = int((y - y0) / cell), int((x - x0) / cell)
            if 0 <= i < ny and 0 <= j < nx:
                blocked[i][j] += a * layers
    for pts in nets.values():
        if len(pts) < 2:
            continue
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        bx0, bx1, by0, by1 = min(xs), max(xs), min(ys), max(ys)
        w, h = max(bx1 - bx0, cell), max(by1 - by0, cell)
        # wire length: half-perimeter, scaled up for nets of many pins (a Steiner tree is longer)
        k = len(pts)
        q = 1.0 if k <= 3 else 1.0 + 0.1 * (k - 3) ** 0.5 * 3
        wire = (bx1 - bx0 + by1 - by0) * q
        dens = wire / (w * h)                         # mm of wire per mm^2 of its box
        for i in range(max(0, int((by0 - y0) / cell)), min(ny, int((by0 + h - y0) / cell) + 1)):
            for j in range(max(0, int((bx0 - x0) / cell)), min(nx, int((bx0 + w - x0) / cell) + 1)):
                cx0, cy0 = x0 + j * cell, y0 + i * cell
                ox = max(0.0, min(cx0 + cell, bx0 + w) - max(cx0, bx0))
                oy = max(0.0, min(cy0 + cell, by0 + h) - max(cy0, by0))
                demand[i][j] += dens * ox * oy
    # a router goes round a pad, so judge each cell with its neighbours (a 3 x 3 window, 6 mm) and
    # let pads block at most 3/4 of the routing area
    util = [[0.0] * nx for _ in range(ny)]
    for i in range(ny):
        for j in range(nx):
            dem = blk = area = 0.0
            for a in range(max(0, i - 1), min(ny, i + 2)):
                for b in range(max(0, j - 1), min(nx, j + 2)):
                    dem += demand[a][b]
                    blk += blocked[a][b]
                    area += cell * cell * LAYERS
            util[i][j] = dem * PITCH / max(area - blk, area / 4)   # wire mm x pitch = area it needs
    return util, x0, y0, cell, nx, ny


def summary(util):
    flat = sorted(v for row in util for v in row)
    n = len(flat)
    return {"peak": flat[-1], "p95": flat[int(n * 0.95)], "mean": sum(flat) / n,
            "over_0.7": sum(v > 0.7 for v in flat), "over_1": sum(v > 1.0 for v in flat)}


def heatmap(util, x0, y0, cell, path, scale=12):
    from PIL import Image
    ny, nx = len(util), len(util[0])
    im = Image.new("RGB", (nx * scale, ny * scale))
    px = im.load()
    for i in range(ny):
        for j in range(nx):
            v = min(util[i][j] / 1.2, 1.0)
            col = (int(255 * min(1, 2 * v)), int(255 * min(1, 2 * (1 - v))), 40)
            for a in range(scale):
                for b in range(scale):
                    px[j * scale + b, i * scale + a] = col
    im.save(path)


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    pcb = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "obd-adapter.kicad_pcb")
    u, x0, y0, c, nx, ny = rudy(pcbnew.LoadBoard(pcb))
    print({k: round(v, 2) if isinstance(v, float) else v for k, v in summary(u).items()})
    if len(sys.argv) > 2:
        heatmap(u, x0, y0, c, sys.argv[2])
        hot = sorted(((u[i][j], x0 + (j + 0.5) * c, y0 + (i + 0.5) * c) for i in range(ny) for j in range(nx)), reverse=True)[:8]
        print("hottest cells (util, x, y):", [(round(v, 2), round(x, 1), round(y, 1)) for v, x, y in hot])
