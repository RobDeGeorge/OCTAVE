"""Finish the last few connections Freerouting leaves: a grid A* router on F.Cu / B.Cu.

Freerouting stalls with a handful of connections left on this board, and which ones changes from
run to run. For each net still in more than one copper piece, this searches a 0.1 mm grid on the
outer layers (a via costs extra) for a path whose track, at the net's width plus its clearance,
touches no other net on that layer and whose vias clear every layer, using KiCad's own item
shapes. The path is laid as locked copper; DRC checks the result like everything else.
"""

import heapq
import math

import pcbnew

MM = pcbnew.FromMM
STEP = MM(0.1)
LAYERS = (pcbnew.F_Cu, pcbnew.B_Cu)
ALL = (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu)
VIA_D, VIA_DRILL = 0.6, 0.3


def _pieces(board, net):
    """Copper pieces of `net` (lists of items), joined through tracks, vias, pads and zone fills."""
    board.BuildConnectivity()
    con = board.GetConnectivity()
    items = [t for t in board.GetTracks() if t.GetNetname() == net]
    items += [p for fp in board.GetFootprints() for p in fp.Pads() if p.GetNetname() == net]
    ids = {i.m_Uuid.AsString(): i for i in items}
    parent = {u: u for u in ids}

    def find(u):
        while parent[u] != u:
            parent[u] = parent[parent[u]]
            u = parent[u]
        return u

    def join(a, b):
        if a in parent and b in parent:
            parent[find(a)] = find(b)
    for it in items:
        for n in con.GetConnectedItems(it):
            join(it.m_Uuid.AsString(), n.m_Uuid.AsString())
    for z in board.Zones():
        if z.GetNetname() != net or z.GetIsRuleArea():
            continue
        for layer in z.GetLayerSet().Seq():
            fill = z.GetFilledPolysList(layer)
            for k in range(fill.OutlineCount()):
                anchor = f"z{z.m_Uuid.AsString()}{layer}{k}"
                parent[anchor] = anchor
                for it in items:
                    if it.IsOnLayer(layer) and fill.Contains(it.GetPosition(), k):
                        join(it.m_Uuid.AsString(), anchor)
    groups = {}
    for u, it in ids.items():
        groups.setdefault(find(u), []).append(it)
    return list(groups.values())


class _Grid:
    def __init__(self, board, net, box, width, clearance, keepouts, rip=False, hv=(), hv_clearance=0):
        self.rip = rip
        self.x0, self.y0, self.x1, self.y1 = box
        self.nx = (self.x1 - self.x0) // STEP + 1
        self.ny = (self.y1 - self.y0) // STEP + 1
        self.half = width // 2
        self.clr = clearance + MM(0.04)   # margin for diagonal steps between grid points (0.02 left 0.138 of 0.15)
        self.keepouts = [tuple(MM(v) for v in k) for k in keepouts]
        self.obs = []
        for o in list(board.GetTracks()) + [p for fp in board.GetFootprints() for p in fp.Pads()]:
            if o.GetNetname() == net and o.Type() != pcbnew.PCB_PAD_T or \
                    o.GetNetname() == net and o.GetAttribute() not in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                continue    # own copper is free to cross (own-net holes still block vias)
            bb = o.GetBoundingBox()
            if bb.GetRight() < self.x0 - MM(2) or bb.GetLeft() > self.x1 + MM(2) or \
                    bb.GetBottom() < self.y0 - MM(2) or bb.GetTop() > self.y1 + MM(2):
                continue
            soft = o.Type() in (pcbnew.PCB_TRACE_T, pcbnew.PCB_VIA_T) and not o.IsLocked() and o.GetNetname() != net
            # the larger clearance wins: battery-side obstacles keep hv_clearance from any net
            gap = max(self.clr, hv_clearance + MM(0.02)) if o.GetNetname().lstrip("/") in hv else self.clr
            self.obs.append((bb.GetLeft(), bb.GetTop(), bb.GetRight(), bb.GetBottom(), o, o.GetNetname() == net, soft, gap))
        # SMD pads of every net, own included: a via may not sit in or touch one (solder wicks
        # down an open hole; DFM flags it)
        self.smd = [p for fp in board.GetFootprints() for p in fp.Pads()
                    if p.GetAttribute() == pcbnew.PAD_ATTRIB_SMD and fp.GetBoundingBox().Intersects(
                        pcbnew.BOX2I(pcbnew.VECTOR2I(self.x0 - MM(2), self.y0 - MM(2)),
                                     pcbnew.VECTOR2I(self.x1 - self.x0 + MM(4), self.y1 - self.y0 + MM(4))))]
        for z in board.Zones():   # rule areas (antenna keepout) block everything
            if z.GetIsRuleArea():
                bb = z.GetBoundingBox()
                self.keepouts.append((bb.GetLeft(), bb.GetTop(), bb.GetRight(), bb.GetBottom()))
        self.cache = {}

    def pos(self, i, j):
        return pcbnew.VECTOR2I(self.x0 + i * STEP, self.y0 + j * STEP)

    def _hit(self, shape, pt, reach, layers, own_ok):
        """None if a fixed item is in the way, else the soft (rippable) items hit."""
        soft = []
        for l, t, r, b, o, own, rip, gap in self.obs:
            if own and own_ok:
                continue
            if r < pt.x - reach - gap or l > pt.x + reach + gap or b < pt.y - reach - gap or t > pt.y + reach + gap:
                continue
            for L in layers:
                if o.IsOnLayer(L) and o.GetEffectiveShape(L).Collide(shape, gap):
                    if not (self.rip and rip):
                        return None
                    soft.append(o)
                    break
        return soft

    def probe(self, i, j, layer):
        k = (i, j, layer)
        if k not in self.cache:
            pt = self.pos(i, j)
            if any(a <= pt.x <= c and b <= pt.y <= d for a, b, c, d in self.keepouts):
                self.cache[k] = None
            else:
                self.cache[k] = self._hit(pcbnew.SHAPE_CIRCLE(pt, self.half), pt, self.half + self.clr + MM(1),
                                          (layer,), True)
        return self.cache[k]

    def via_probe(self, i, j):
        k = (i, j, "via")
        if k not in self.cache:
            pt = self.pos(i, j)
            ring = pcbnew.SHAPE_CIRCLE(pt, MM(VIA_D / 2))
            if any(p.GetEffectiveShape(L).Collide(ring, MM(0.05)) for p in self.smd for L in LAYERS
                   if p.IsOnLayer(L) and (p.GetPosition() - pt).EuclideanNorm() < MM(4)):
                self.cache[k] = None
                return None
            a, b = self.probe(i, j, LAYERS[0]), self.probe(i, j, LAYERS[1])
            c = None if a is None or b is None else self._hit(
                pcbnew.SHAPE_CIRCLE(pt, MM(VIA_D / 2)), pt, MM(VIA_D) + self.clr + MM(1), ALL, False)
            self.cache[k] = None if c is None else a + b + c
        return self.cache[k]


def _cells(grid, items):
    """Grid cells inside the copper of `items`, per outer layer."""
    out = set()
    for it in items:
        bb = it.GetBoundingBox()
        for layer in LAYERS:
            if not it.IsOnLayer(layer):
                continue
            sh = it.GetEffectiveShape(layer)
            i0 = max(0, (bb.GetLeft() - grid.x0) // STEP)
            i1 = min(grid.nx - 1, (bb.GetRight() - grid.x0) // STEP + 1)
            j0 = max(0, (bb.GetTop() - grid.y0) // STEP)
            j1 = min(grid.ny - 1, (bb.GetBottom() - grid.y0) // STEP + 1)
            for i in range(i0, i1 + 1):
                for j in range(j0, j1 + 1):
                    if sh.Collide(grid.pos(i, j), 0):
                        out.add((i, j, layer))
    return out


def _astar(grid, src, dst, via_cost=40, rip_cost=60):
    goal = {(i, j, L) for i, j, L in dst}
    gx = sum(c[0] for c in dst) / len(dst)
    gy = sum(c[1] for c in dst) / len(dst)
    h = lambda c: math.hypot(c[0] - gx, c[1] - gy) * 0.5  # noqa: E731  (admissible enough; dst is a set)
    openq = [(h(c), 0.0, c, None) for c in src]
    heapq.heapify(openq)
    came, best = {}, {c: 0.0 for c in src}
    steps = 0
    while openq:
        f, g, c, prev = heapq.heappop(openq)
        if c in came:
            continue
        came[c] = prev
        if c in goal:
            path = [c]
            while came[path[-1]] is not None:
                path.append(came[path[-1]])
            path = path[::-1]
            soft = {}
            for a, b in zip(path, path[1:]):
                hit = grid.via_probe(a[0], a[1]) if a[2] != b[2] else grid.probe(*b)
                for o in hit or []:
                    soft[o.m_Uuid.AsString()] = o
            return path, list(soft.values())
        steps += 1
        if steps > 3000000:
            return None
        i, j, L = c
        nbrs = []
        for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            ni, nj = i + di, j + dj
            if 0 <= ni < grid.nx and 0 <= nj < grid.ny:
                nbrs.append(((ni, nj, L), math.hypot(di, dj)))
        other = LAYERS[1] if L == LAYERS[0] else LAYERS[0]
        nbrs.append(((i, j, other), via_cost))
        for n, cost in nbrs:
            ni, nj, nL = n
            if n in came:
                continue
            extra = 0
            if nL != L:
                hit = grid.via_probe(i, j)
                if hit is None:
                    continue
                extra = rip_cost * len(hit)
            elif not (n in goal or n in src):
                hit = grid.probe(ni, nj, nL)
                if hit is None:
                    continue
                extra = rip_cost * len(hit)
            ng = g + cost + extra
            if ng < best.get(n, 1e18):
                best[n] = ng
                heapq.heappush(openq, (ng + h(n), ng, n, c))
    return None, []


def _lay(board, grid, net, path, width):
    """Turn a cell path into locked tracks (collinear runs merged) and vias."""
    nobj = board.FindNet(net)
    runs, cur = [], [path[0]]
    for c in path[1:]:
        if c[2] != cur[-1][2]:
            runs.append(cur)
            cur = [c]
        else:
            cur.append(c)
    runs.append(cur)
    added = []
    for r, run in enumerate(runs):
        pts = [run[0]]
        for a, b, c in zip(run, run[1:], run[2:]):
            if (b[0] - a[0], b[1] - a[1]) != (c[0] - b[0], c[1] - b[1]):
                pts.append(b)
        if len(run) > 1:
            pts.append(run[-1])
        for a, b in zip(pts, pts[1:]):
            t = pcbnew.PCB_TRACK(board)
            t.SetStart(grid.pos(a[0], a[1]))
            t.SetEnd(grid.pos(b[0], b[1]))
            t.SetWidth(width)
            t.SetLayer(a[2])
            t.SetNet(nobj)
            t.SetLocked(True)
            board.Add(t)
            added.append(t)
        if r + 1 < len(runs):
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(grid.pos(run[-1][0], run[-1][1]))
            v.SetWidth(MM(VIA_D))
            v.SetDrill(MM(VIA_DRILL))
            v.SetNet(nobj)
            v.SetLocked(True)
            board.Add(v)
            added.append(v)
    return added


def _save(o):
    if o.Type() == pcbnew.PCB_VIA_T:
        return ("via", o.GetPosition(), o.GetWidth(pcbnew.F_Cu), o.GetDrillValue(), o.GetNetname(), o.IsLocked())
    return ("trk", o.GetStart(), o.GetEnd(), o.GetWidth(), o.GetLayer(), o.GetNetname(), o.IsLocked())


def _load(board, s):
    if s[0] == "via":
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(s[1]); v.SetWidth(s[2]); v.SetDrill(s[3]); v.SetNet(board.FindNet(s[4])); v.SetLocked(s[5])
        board.Add(v)
        return v
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(s[1]); t.SetEnd(s[2]); t.SetWidth(s[3]); t.SetLayer(s[4]); t.SetNet(board.FindNet(s[5])); t.SetLocked(s[6])
    board.Add(t)
    return t


HV, HV_CLR = (), 0     # set by finish()


def _join(board, net, a, rest, width, clearance, keepouts, rip, whole=False, max_rip=2):
    ba = _bbox(a)
    near = min(rest, key=lambda it: (it.GetPosition() - a[0].GetPosition()).EuclideanNorm())
    nb = near.GetBoundingBox()
    m = MM(8)
    box = (min(ba[0], nb.GetLeft()) - m, min(ba[1], nb.GetTop()) - m,
           max(ba[2], nb.GetRight()) + m, max(ba[3], nb.GetBottom()) + m)
    box = (max(box[0], MM(0.5)), max(box[1], MM(0.5)), min(box[2], MM(109.5)), min(box[3], MM(74.5)))
    if whole:
        box = (MM(0.5), MM(0.5), MM(109.5), MM(74.5))
    # put a grid point on the centre of the finest-pitch pad at either end: an LQFP pin at x.25 /
    # x.75 sits half a step off a 0.1 mm grid, and a track leaving it off-axis can't clear the
    # neighbouring pins, so the pin looks boxed in
    pads = [it for it in list(a) + [near] if it.Type() == pcbnew.PCB_PAD_T]
    if pads:
        ref = min(pads, key=lambda p: min(p.GetBoundingBox().GetWidth(), p.GetBoundingBox().GetHeight())).GetPosition()
        box = (box[0] + (ref.x - box[0]) % STEP, box[1] + (ref.y - box[1]) % STEP, box[2], box[3])
    grid = _Grid(board, net, box, width, clearance, keepouts, rip, HV, HV_CLR)
    src = _cells(grid, a)
    dst = _cells(grid, rest) - src
    if not src or not dst:
        return None
    path, soft = _astar(grid, src, dst)
    if not path or len(soft) > max_rip:
        return None
    victims = sorted({o.GetNetname() for o in soft})
    saved = [_save(o) for o in soft]
    for o in soft:      # Remove() frees the item: nothing may touch it afterwards
        board.Remove(o)
    added = _lay(board, grid, net, path, width)
    vias = sum(1 for x, y in zip(path, path[1:]) if x[2] != y[2])
    return added, vias, victims, saved


def _pad_pieces(board, net):
    return sorted([pc for pc in _pieces(board, net) if any(it.Type() == pcbnew.PCB_PAD_T for it in pc)], key=len)


def broken(board, skip):
    nets = sorted({p.GetNetname() for fp in board.GetFootprints() for p in fp.Pads() if p.GetNetname()})
    return [n for n in nets if n not in skip and not n.startswith("unconnected-") and len(_pad_pieces(board, n)) > 1]


def finish(board, width_of, clearance_of, keepouts, skip=("/GND",), rounds=3, rip=False, hv=(), hv_clearance=0.3,
           only=None):
    """Join every net that is still in pieces. First around obstacles (locally, then board-wide);
    failing that, through at most two unlocked tracks of other nets, kept only if every net it cut
    can then be re-joined without further rip-up (otherwise rolled back exactly).
    `only` limits it to those nets (seeding hard nets before Freerouting).
    Returns the nets left in pieces."""
    def plain(net, laid):
        for _ in range(8):
            pieces = _pad_pieces(board, net)
            if len(pieces) < 2:
                return True
            w, c = MM(width_of(net)), MM(clearance_of(net))
            a, rest = pieces[0], [it for pc in pieces[1:] for it in pc]
            r = _join(board, net, a, rest, w, c, keepouts, False) or \
                _join(board, net, a, rest, w, c, keepouts, False, whole=True)
            board.BuildConnectivity()
            if not r:
                return False
            laid += r[0]
            print(f"maze: {net} joined ({len(r[0])} segments, {r[1]} vias)", flush=True)
        return len(_pad_pieces(board, net)) < 2

    global HV, HV_CLR
    HV, HV_CLR = set(hv), MM(hv_clearance)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    for rnd in range(rounds):
        todo = [n for n in broken(board, skip) if only is None or n in only]
        if not todo:
            break
        for net in todo:
            if plain(net, []):
                continue
            if not rip:
                print(f"maze: no path for {net}", flush=True)
                continue
            pieces = _pad_pieces(board, net)
            if len(pieces) < 2:
                continue
            w, c = MM(width_of(net)), MM(clearance_of(net))
            r = _join(board, net, pieces[0], [it for pc in pieces[1:] for it in pc], w, c, keepouts, True)
            board.BuildConnectivity()
            if not r:
                print(f"maze: no path for {net}", flush=True)
                continue
            added, vias, victims, saved = r
            laid = list(added)
            ok = all(plain(v, laid) for v in victims) and plain(net, laid)
            if ok:
                print(f"maze: {net} joined through {', '.join(victims)} (re-routed)", flush=True)
                continue
            for it in laid:          # roll back exactly
                board.Remove(it)
            for sv in saved:
                _load(board, sv)
            board.BuildConnectivity()
            print(f"maze: {net}: rip-up of {', '.join(victims)} could not be re-routed; rolled back", flush=True)
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    return sorted(n for n in broken(board, skip) if only is None or n in only)


def prune(board, skip=("/GND", "/3V3")):
    """Remove unlocked tracks and vias left dangling by rip-up (repeat until none)."""
    total = 0
    while True:
        board.BuildConnectivity()
        con = board.GetConnectivity()
        dead = []
        for t in board.GetTracks():
            if t.IsLocked() or t.GetNetname() in skip:
                continue
            if t.Type() == pcbnew.PCB_VIA_T:
                if len(con.GetConnectedItems(t)) < 2:
                    dead.append(t)
            elif con.TestTrackEndpointDangling(t, False):
                dead.append(t)
        if not dead:
            return total
        for t in dead:
            board.Remove(t)
        total += len(dead)


def _bbox(items):
    bbs = [it.GetBoundingBox() for it in items]
    return (min(b.GetLeft() for b in bbs), min(b.GetTop() for b in bbs),
            max(b.GetRight() for b in bbs), max(b.GetBottom() for b in bbs))
