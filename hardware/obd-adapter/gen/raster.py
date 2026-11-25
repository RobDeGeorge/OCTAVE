"""A fast finisher for a patch re-route (OBD_ROUTER=raster): no Freerouting.

Each net still in pieces is joined by a search on a 0.05 mm raster of the signal layers (the two
outer ones; on a 6-layer board In2 / In3 as well, for all but the supply nets). The
obstacles are KiCad's own shapes, grown by the net's half width and its clearance (the battery-side
clearance where either net is one of HV), so a path is legal by construction; vias keep off
every pad and every other net. No rip-up: what cannot be joined is reported and left to maze.py.
Where maze.py takes minutes per net on this board, this takes seconds. DRC checks the result.
"""
import heapq
import math
import random

import numpy as np
import pcbnew
from PIL import Image, ImageDraw

import layout
import maze
import pcbgen
import prewire

HV = set()     # set by finish()
MM = pcbnew.FromMM
P = 20
X0, Y0 = -2.0, -1.0
NX, NY = int((layout.W + 4) * P), int((layout.H + layout.LOBE[2] if layout.LOBE else layout.H) * P) + 2 * P
LAY = (pcbnew.F_Cu, pcbnew.B_Cu)     # the layers a path may use: finish() adds the inner signal layers of a 6-layer board
VIA_D, VIA_DRILL = 0.45, 0.2          # the signal via: fits between two rows of 0603 pads
INNER_OK = True                       # False while joining a net kept to the outer layers (the supply nets)

def poly_draw(d, sp):
    for k in range(sp.OutlineCount()):
        o = sp.Outline(k)
        pts = [((o.CPoint(i).x / 1e6 - X0) * P, (o.CPoint(i).y / 1e6 - Y0) * P) for i in range(o.PointCount())]
        if len(pts) > 2:
            d.polygon(pts, fill=1)

def _soft(o, net):
    """Copper a rip-up may take out: another signal net's track or via that is not locked (the
    autorouter's; the hand copper, the plane fan-out and what this finisher laid are locked)."""
    return (o.Type() in (pcbnew.PCB_TRACE_T, pcbnew.PCB_VIA_T) and not o.IsLocked()
            and o.GetNetname() not in (net, "GND", "3V3"))


def raster(board, net, width, hv, rip=False):
    """Per layer of LAY, where a track of `net` may run; and where a via may sit. With `rip`, the
    soft copper (_soft) is left out of those and returned as masks of its own: (free, vfree, soft, vsoft)."""
    unfused = pcbgen.UNFUSED.get(net.lstrip("/"), 0)
    clr = lambda o: max(0.3 if (hv or o.GetNetname().lstrip("/") in HV) else 0.15, unfused,     # noqa: E731
                        pcbgen.UNFUSED.get(o.GetNetname().lstrip("/"), 0)) + 0.05   # the raster and the diagonals eat up to 0.035
    vr = 0.3 if width >= 0.6 else VIA_D / 2     # the radius of the via lay() will use for this net
    img = [Image.new("1", (NX, NY), 0) for _ in LAY]
    vimg = Image.new("1", (NX, NY), 0)
    D = [ImageDraw.Draw(i) for i in img]; DV = ImageDraw.Draw(vimg)
    simg = [Image.new("1", (NX, NY), 0) for _ in LAY]
    svimg = Image.new("1", (NX, NY), 0)
    SD = [ImageDraw.Draw(i) for i in simg]; SDV = ImageDraw.Draw(svimg)
    items = list(board.GetTracks()) + [p for fp in board.GetFootprints() for p in fp.Pads()]
    for o in items:
        own = o.GetNetname() == net
        isvia = o.Type() == pcbnew.PCB_VIA_T
        ispad = o.Type() == pcbnew.PCB_PAD_T
        th = ispad and o.GetAttribute() != pcbnew.PAD_ATTRIB_SMD
        soft = rip and _soft(o, net)
        D_, DV_ = (SD, SDV) if soft else (D, DV)
        for k, L in enumerate(LAY):
            if not (o.IsOnLayer(L) or isvia or th):
                continue
            if not own:
                sp = pcbnew.SHAPE_POLY_SET()
                o.TransformShapeToPolygon(sp, L, MM(clr(o) + width / 2), MM(0.005), pcbnew.ERROR_OUTSIDE)
                poly_draw(D_[k], sp)
            # vias (radius vr): keep clear of other nets' copper, and off every SMD pad
            if not own or (ispad and not th) or th:
                sp = pcbnew.SHAPE_POLY_SET()
                g = clr(o) + vr if not own else (vr + 0.06 if not th else 0.0)
                if th and own:
                    continue
                o.TransformShapeToPolygon(sp, L, MM(g), MM(0.005), pcbnew.ERROR_OUTSIDE)
                poly_draw(DV_, sp)
    # a via goes through every layer: other nets' tracks on the inner signal layers block it too
    for o in board.GetTracks():
        if o.GetNetname() == net or o.Type() == pcbnew.PCB_VIA_T:
            continue
        for L in (pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.In3_Cu, pcbnew.In4_Cu):
            if o.IsOnLayer(L):
                sp = pcbnew.SHAPE_POLY_SET()
                o.TransformShapeToPolygon(sp, L, MM(clr(o) + vr), MM(0.005), pcbnew.ERROR_OUTSIDE)
                poly_draw(SDV if rip and _soft(o, net) else DV, sp)
    ins = Image.new("1", (NX, NY), 0); DI = ImageDraw.Draw(ins)
    e = 0.42 + width / 2
    R = lambda d, x0, y0, x1, y1, f=1: d.rectangle([(x0 - X0) * P, (y0 - Y0) * P, (x1 - X0) * P, (y1 - Y0) * P], fill=f)
    R(DI, layout.X0 + e, e, layout.W - e, layout.H - e)
    if layout.LOBE:
        lx, lx1, ld = layout.LOBE
        R(DI, lx + e, layout.H - e - 1, lx1 - e, layout.H + ld - e)
    # the buck's and the LDO's keep-outs are for F.Cu (other circuits' parts stand on the back there); the
    # plug's tab and layout.KEEPOUTS close every layer
    # (insv: where a via may stand on F.Cu. As insf, but under the ESP32 module vias are welcome: only
    # tracks on F.Cu stay out there, and the parts on the back under it need a way to the inner layers)
    insf = ins.copy(); DIF = ImageDraw.Draw(insf)
    insv = ins.copy(); DIV = ImageDraw.Draw(insv)
    for k in prewire.KEEPOUTS + list(layout.KEEPOUTS):
        every = k == prewire.TAB_KEEPOUT or k in layout.KEEPOUTS
        for d in ((DI, DIF, DIV) if every else (DIF,) if k == prewire.MODULE_KEEPOUT else (DIF, DIV)):
            R(d, k[0] - width / 2, k[1] - width / 2, k[2] + width / 2, k[3] + width / 2, 0)
    for z in board.Zones():
        if z.GetIsRuleArea() and z.GetDoNotAllowTracks():
            bb = z.GetBoundingBox()
            for d in (DI, DIF, DIV):
                R(d, bb.GetLeft() / 1e6 - width / 2, bb.GetTop() / 1e6 - width / 2, bb.GetRight() / 1e6 + width / 2, bb.GetBottom() / 1e6 + width / 2, 0)
    # a via's ring keeps the same distance from the outline as a track's edge
    vins = Image.new("1", (NX, NY), 0)
    DVI = ImageDraw.Draw(vins)
    ev = 0.42 + vr
    R(DVI, layout.X0 + ev, ev, layout.W - ev, layout.H - ev)
    for k in layout.VIA_KEEPOUTS:       # no via beside the plug's hand-soldered pads
        R(DVI, k[0] - vr, k[1] - vr, k[2] + vr, k[3] + vr, 0)
    if layout.LOBE:
        R(DVI, lx + ev, layout.H - ev, lx1 - ev, layout.H + ld - ev)
    ins, insf, insv = np.array(ins, bool), np.array(insf, bool), np.array(insv, bool)
    free = [~np.array(i, bool) & (insf if L == pcbnew.F_Cu else ins) for i, L in zip(img, LAY)]
    vfree = ~np.array(vimg, bool) & np.array(vins, bool)
    for i, L in zip(img, LAY):      # a via lands on every layer
        vfree &= ~np.array(i, bool) & (insv if L == pcbnew.F_Cu else ins)
    if rip:
        return free, vfree, [np.array(i, bool) for i in simg], np.array(svimg, bool)
    return free, vfree

def cells(items):
    out = set()
    for it in items:
        for k, L in enumerate(LAY):
            if not it.IsOnLayer(L):
                continue
            sp = pcbnew.SHAPE_POLY_SET()
            it.TransformShapeToPolygon(sp, L, MM(-0.05) if it.Type() == pcbnew.PCB_PAD_T else 0, MM(0.005), pcbnew.ERROR_INSIDE)
            im = Image.new("1", (NX, NY), 0); poly_draw(ImageDraw.Draw(im), sp)
            ys, xs = np.nonzero(np.array(im, bool))
            out |= {(k, int(y), int(x)) for y, x in zip(ys, xs)}
            if it.Type() != pcbnew.PCB_PAD_T:     # track: its centre line end points only would do, but copper is fine
                pass
    return out

def search(free, vfree, src, dst, via_cost=60, soft=None, vsoft=None, rip_cost=25, rip_via=400):
    """Cheapest path of cells from `src` to `dst`. With `soft` / `vsoft` (raster(rip=True)) the path may
    cross soft copper, at `rip_cost` a cell (`rip_via` for a via among it): the caller takes that copper out."""
    dist = {}; came = {}
    pq = [(0.0, c, None) for c in src]
    gy = sum(c[1] for c in dst) / len(dst); gx = sum(c[2] for c in dst) / len(dst)
    h = lambda c: math.hypot(c[1] - gy, c[2] - gx) * 0.7
    pq = [(h(c), 0.0, c, None) for c in src]; heapq.heapify(pq)
    while pq:
        f, g, c, prev = heapq.heappop(pq)
        if c in came:
            continue
        came[c] = prev
        if c in dst:
            path = [c]
            while came[path[-1]] is not None:
                path.append(came[path[-1]])
            return path[::-1]
        l, y, x = c
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            n = (l, y + dy, x + dx)
            if n in came or not (0 <= n[1] < NY and 0 <= n[2] < NX):
                continue
            if not (free[l][n[1], n[2]] or n in dst or n in src):
                continue
            # a turn costs a little, to keep runs straight
            turn = 0.0
            if prev is not None and prev[0] == l and (y - prev[1], x - prev[2]) != (dy, dx):
                turn = 0.6
            ng = g + math.hypot(dy, dx) + turn
            if soft is not None and soft[l][n[1], n[2]]:
                ng += rip_cost
            if ng < dist.get(n, 1e18):
                dist[n] = ng; heapq.heappush(pq, (ng + h(n), ng, n, c))
        for l2 in (range(len(free)) if vfree[y, x] else ()):
            if l2 == l or not (INNER_OK or LAY[l2] in (pcbnew.F_Cu, pcbnew.B_Cu)):
                continue
            n = (l2, y, x)
            vc = via_cost + (rip_via if vsoft is not None and vsoft[y, x] else 0)
            if n not in came and g + vc < dist.get(n, 1e18):
                dist[n] = g + vc; heapq.heappush(pq, (g + vc + h(n), g + vc, n, c))
    return None

def lay(board, net, path, width):
    nobj = board.FindNet(net)
    pos = lambda c: pcbnew.VECTOR2I(MM(c[2] / P + X0), MM(c[1] / P + Y0))
    runs, cur = [], [path[0]]
    for c in path[1:]:
        if c[0] != cur[-1][0]:
            runs.append(cur); cur = [c]
        else:
            cur.append(c)
    runs.append(cur)
    n = 0
    added = []
    for r, run in enumerate(runs):
        pts = [run[0]]
        for a, b, c in zip(run, run[1:], run[2:]):
            if (b[1] - a[1], b[2] - a[2]) != (c[1] - b[1], c[2] - b[2]):
                pts.append(b)
        if len(run) > 1:
            pts.append(run[-1])
        for a, b in zip(pts, pts[1:]):
            t = pcbnew.PCB_TRACK(board); t.SetStart(pos(a)); t.SetEnd(pos(b)); t.SetWidth(MM(width)); t.SetLayer(LAY[a[0]])
            t.SetNet(nobj); t.SetLocked(True); board.Add(t); n += 1; added.append(t)
        if r + 1 < len(runs):
            big = width >= 0.6      # supply nets keep the 0.6 / 0.3 mm via
            v = pcbnew.PCB_VIA(board); v.SetPosition(pos(run[-1])); v.SetWidth(MM(0.6 if big else VIA_D)); v.SetDrill(MM(0.3 if big else VIA_DRILL)); v.SetNet(nobj); v.SetLocked(True)
            board.Add(v); n += 1; added.append(v)
    LAID[:] = added
    return n, len(runs) - 1


LAID = []       # what the last lay() added


_EVERY = (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.In3_Cu, pcbnew.In4_Cu, pcbnew.B_Cu)


def _victims(board, added, net):
    """The soft copper `added` runs through or too close to."""
    out = []
    boxes = []
    for a in added:
        bb = a.GetBoundingBox()
        bb.Inflate(MM(0.4))
        boxes.append(bb)
    for t in board.GetTracks():
        if not _soft(t, net):
            continue
        tb = t.GetBoundingBox()
        gap = MM(0.3 if (net.lstrip("/") in HV or t.GetNetname().lstrip("/") in HV) else 0.15)
        for a, bb in zip(added, boxes, strict=True):
            if not bb.Intersects(tb):
                continue
            both = [L for L in _EVERY if (a.IsOnLayer(L) or a.Type() == pcbnew.PCB_VIA_T)
                    and (t.IsOnLayer(L) or t.Type() == pcbnew.PCB_VIA_T)]
            if any(a.GetEffectiveShape(L).Collide(t.GetEffectiveShape(L), gap) for L in both):
                out.append(t)
                break
    return out


def finish_rip(board, width_of, hv, skip=("GND", "3V3"), passes=6):
    """finish(), then for what it leaves: join each open net through the autorouter's copper where there
    is no way round it (the copper in its way is taken out; what this lays is locked), and join the nets
    that cut again with finish(). Repeats up to `passes` times. Returns the nets left in pieces."""
    global HV, LAY, INNER_OK
    left = finish(board, width_of, hv, skip, rounds=2)
    for p in range(passes):
        if not left:
            break
        print(f"raster: rip-up pass {p + 1} for {', '.join(left)}", flush=True)
        for net in left:
            width = width_of(net)
            INNER_OK = width < 0.6 and "VBUS" not in net
            for _ in range(8):
                pieces = maze._pad_pieces(board, net)
                if len(pieces) < 2:
                    break
                free, vfree, soft, vsoft = raster(board, net, width, net.lstrip("/") in HV, rip=True)
                src = cells(pieces[0])
                dst = cells([it for pc in pieces[1:] for it in pc]) - src
                path = search(free, vfree, src, dst, soft=soft, vsoft=vsoft) if src and dst else None
                if not path:
                    print(f"raster: no path for {net}, even through other nets", flush=True)
                    break
                lay(board, net, path, width)
                gone = _victims(board, list(LAID), net)
                names = sorted({t.GetNetname() for t in gone})
                for t in gone:
                    board.Remove(t)
                board.BuildConnectivity()
                print(f"raster: {net} joined" + (f" through {', '.join(names)} ({len(gone)} items out)" if gone else ""), flush=True)
        left = finish(board, width_of, hv, skip, rounds=2)
    return left

def rip(board, box, skip=("GND", "3V3")):
    """Take out every signal track and via touching `box` (mm): copper laid before the board grew
    there, in the way of what must now cross it. Returns the nets cut; finish() joins them again last."""
    bb = pcbnew.BOX2I(pcbnew.VECTOR2I(MM(box[0]), MM(box[1])), pcbnew.VECTOR2I(MM(box[2] - box[0]), MM(box[3] - box[1])))
    gone = [t for t in board.GetTracks() if t.GetNetname() not in skip and bb.Intersects(t.GetBoundingBox())]
    nets = sorted({t.GetNetname() for t in gone})
    for t in gone:
        board.Remove(t)
    board.BuildConnectivity()
    print(f"raster: lifted {len(gone)} tracks / vias of {', '.join(nets)}", flush=True)
    return nets


def finish(board, width_of, hv, skip=("GND", "3V3"), rounds=16, last=()):
    """Join every net still in pieces: the long runs first (they need the gaps the short ones would
    close), then the short gaps. No rip-up; instead, when nets are left over, everything laid is taken
    out again and the round is repeated with those nets first. Returns the nets left in pieces."""
    global HV, LAY, INNER_OK
    HV = set(hv)
    if board.GetCopperLayerCount() == 6:    # In1 / In4 are the planes
        LAY = (pcbnew.F_Cu, pcbnew.In2_Cu, pcbnew.In3_Cu, pcbnew.B_Cu)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())

    def gap(net):
        pcs = maze._pad_pieces(board, net)
        a = pcs[0][0].GetPosition()
        return max(min((it.GetPosition() - a).EuclideanNorm() for it in pc) for pc in pcs[1:])
    todo = {n: gap(n) for n in maze.broken(board, skip)}
    order = sorted(todo, key=lambda n: (n in last, todo[n] < pcbnew.FromMM(20), todo[n]))
    before = {t.m_Uuid.AsString() for t in board.GetTracks()}
    rng = random.Random(1)
    tried = []

    def attempt(order):
        global INNER_OK
        failed = []
        for net in order:
            width = width_of(net)
            for _ in range(8):
                pieces = maze._pad_pieces(board, net)
                if len(pieces) < 2:
                    break
                # supply nets (0.6 mm and wider, and VBUS) stay off the thin inner layers, as in the DSN
                INNER_OK = width < 0.6 and "VBUS" not in net
                free, vfree = raster(board, net, width, net.lstrip("/") in HV)
                src = cells(pieces[0])
                dst = cells([it for pc in pieces[1:] for it in pc]) - src
                path = search(free, vfree, src, dst) if src and dst else None
                if not path:
                    failed.append(net)
                    break
                n, v = lay(board, net, path, width)
                run = sum(math.hypot(q[1] - p[1], q[2] - p[2]) for p, q in zip(path, path[1:]) if p[0] == q[0]) / P
                print(f"raster: {net} joined ({n} items, {v} vias, {run:.1f} mm)", flush=True)
                board.BuildConnectivity()
        return failed

    def undo():
        for t in [t for t in board.GetTracks() if t.m_Uuid.AsString() not in before]:
            board.Remove(t)
        board.BuildConnectivity()
    for rnd in range(rounds):
        failed = attempt(order)
        tried.append((len(failed), rnd, list(order)))
        print(f"raster: round {rnd + 1}: no path for {', '.join(failed) or 'none'}", flush=True)
        if not failed:
            break
        undo()
        if rnd < 2:     # the nets that failed go first; after that, shuffled orders
            order = failed + [n for n in order if n not in failed]
        else:
            order = list(tried[0][2])
            rng.shuffle(order)
    else:
        best = min(tried)
        print(f"raster: no order closed everything; keeping round {best[1] + 1}'s", flush=True)
        attempt(best[2])
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    return maze.broken(board, skip)
