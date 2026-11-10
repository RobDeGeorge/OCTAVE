"""Pages drawn like a reference schematic: parts placed by hand, joined by wires.

A page is a function that places parts (`D.part`), runs wires between pins and points
(`D.wire`), and ends nets in power symbols (`D.power`) or global labels (`D.label`). Any pin it
leaves alone gets a global label at its end, so a page is never missing a connection, and
`D.finish()` checks the drawing against the design:

- every group of touching wires / pins / symbols carries exactly one net, and its pins are the
  design's pins of that net (a wire on the wrong pin is an error, not a short);
- every group has a name (a power symbol or global label), so the net is named as on the board;
- a wire may not run over a pin end it isn't meant to join;
- text may not sit on wires or symbols (SchWriter.check).

Coordinates are millimetres on the sheet, y down; keep pin ends on the 1.27 mm grid. A part's
pins are read back with `p["ANODE"]` / `p["4"]` (name or number), so the wiring follows the parts.
"""

from design import stable_uuid
from schgen import TEXT_H, field_box, font, prop, text_w
from schsheets import RAILS, SheetWriter
from sexpr import Q

G = 2.54
AWAY_ANGLE = {(1, 0): 0, (-1, 0): 180, (0, -1): 90, (0, 1): 270}
DIRS = {"right": (1, 0), "left": (-1, 0), "up": (0, -1), "down": (0, 1)}


def _r(v):
    return round(v, 3)


class Placed:
    """A part's unit on the sheet: its pin ends and their outward directions."""

    def __init__(self, part, unit, x, y, rot, mirror):
        self.part, self.unit, self.x, self.y, self.rot, self.mirror = part, unit, x, y, rot, mirror
        self.pins = {}
        for p in part.sym.unit_pins(unit):
            px, py = self.tf(p.x, -p.y)
            # the pin points from its end into the body; outward is the other way
            import math
            a = math.radians(p.rot)
            ix, iy = self.tf(round(math.cos(a)), -round(math.sin(a)), offset=False)
            self.pins[p.number] = (p, (_r(x + px), _r(y + py)), (-ix, -iy))
        x0, y0, x1, y1 = part.sym.bbox(unit)
        c = [self.tf(a, -b) for a, b in ((x0, y0), (x1, y1))]
        self.body = (_r(x + min(c[0][0], c[1][0])), _r(y + min(c[0][1], c[1][1])),
                     _r(x + max(c[0][0], c[1][0])), _r(y + max(c[0][1], c[1][1])))

    def tf(self, sx, sy, offset=True):
        """Symbol offset (sheet axes, y down) -> sheet offset with mirror then rotation."""
        if self.mirror == "x":
            sy = -sy
        elif self.mirror == "y":
            sx = -sx
        r = self.rot % 360
        if r == 90:
            sx, sy = sy, -sx
        elif r == 180:
            sx, sy = -sx, -sy
        elif r == 270:
            sx, sy = -sy, sx
        return sx, sy

    def __getitem__(self, key):
        """Pin end by number or name."""
        if key in self.pins:
            return self.pins[key][1]
        hits = [v for v in self.pins.values() if v[0].name == key]
        if len({h[1] for h in hits}) == 1:      # one pin, or several stacked on one spot (LM317L VO)
            return hits[0][1]
        if len(hits) != 1:
            raise KeyError(f"{self.part.ref}: pin {key!r} ({len(hits)} matches)")
        return hits[0][1]

    def out(self, key):
        """The direction a wire leaves pin `key` in ("left" / "right" / "up" / "down")."""
        num = self.number(key)
        return {v: k for k, v in DIRS.items()}[self.pins[num][2]]

    def number(self, key):
        if key in self.pins:
            return key
        hits = [n for n, v in self.pins.items() if v[0].name == key]
        if not hits:
            raise KeyError(f"{self.part.ref}: no pin {key!r}")
        return hits[0]


class Drawing:
    def __init__(self, writer: SheetWriter, design):
        self.w, self.d = writer, design
        self.parts = {p.ref: p for p in design.parts}
        self.placed = {}            # (ref, unit) -> Placed
        self.wires = []             # ((x0, y0), (x1, y1))
        self.names = []             # (point, net, kind)
        self.ncs = set()
        self.n = 0

    # -- placing ----------------------------------------------------------------------------
    def part(self, ref, x, y, rot=0, mirror=None, unit=1, fields="auto"):
        part = self.parts[ref]
        pl = Placed(part, unit, x, y, rot, mirror)
        pl.fields = fields
        self.placed[(ref, unit)] = pl
        return pl

    def wire(self, *pts):
        """A wire through the points (pin ends or (x, y)); each leg must be level or upright."""
        pts = [(_r(a), _r(b)) for a, b in pts]
        for a, b in zip(pts, pts[1:]):
            if a == b:
                continue
            if a[0] != b[0] and a[1] != b[1]:
                raise ValueError(f"wire leg {a} -> {b} is neither level nor upright")
            self.wires.append((a, b))

    def power(self, pt, net, direction=None):
        """A power symbol at pt: GND points down, a rail up, unless told otherwise."""
        direction = direction or ("down" if net == "GND" else "up")
        self.names.append(((_r(pt[0]), _r(pt[1])), net, ("power", direction)))

    def label(self, pt, net, direction="right"):
        self.names.append(((_r(pt[0]), _r(pt[1])), net, ("label", direction)))

    def name(self, pt, net, direction="right", length=2 * G):
        """Name a wired net: a short stub off pt, then a global label."""
        dx, dy = DIRS[direction]
        pt = (round(pt[0] / 1.27) * 1.27, round(pt[1] / 1.27) * 1.27)     # on the connection grid
        end = (pt[0] + dx * length, pt[1] + dy * length)
        self.wire(pt, end)
        self.label(end, net, direction)

    def find(self, value, pins):
        """The one part of `value` whose pins carry `pins` ({pin number: net})."""
        hits = [p.ref for p in self.d.parts if p.value == value and
                all(p.pin_net().get(k) == v for k, v in pins.items())]
        if len(hits) != 1:
            raise KeyError(f"{value} with {pins}: {hits}")
        return hits[0]

    # -- helpers for the usual shapes ------------------------------------------------------------
    def inline(self, ref, start, direction="right", first="1", fields="auto"):
        """A two-pin part in a wire: pin `first` at `start`, its other pin 7.62 mm on in `direction`.
        Returns the part (its other pin is p[other])."""
        dx, dy = DIRS[direction]
        for rot in (0, 90, 180, 270):
            pl = Placed(self.parts[ref], 1, 0, 0, rot, None)
            nums = list(pl.pins)
            other = [n for n in nums if n != pl.number(first)][0]
            a, b = pl.pins[pl.number(first)][1], pl.pins[other][1]
            vx, vy = b[0] - a[0], b[1] - a[1]
            if (vx == 0 or (vx > 0) == (dx > 0)) and (vy == 0 or (vy > 0) == (dy > 0)) and \
                    (abs(vx) > 0) == (dx != 0) and (abs(vy) > 0) == (dy != 0):
                return self.part(ref, start[0] - a[0], start[1] - a[1], rot, fields=fields)
        raise ValueError(f"{ref}: no rotation puts pin {first} first going {direction}")

    def hang(self, ref, at, to="GND", direction="down", first="1", fields="right"):
        """A two-pin part off a wire point: pin `first` at `at`, the other end to a power symbol
        (to=None: left open for the caller)."""
        p = self.inline(ref, at, direction, first, fields=fields)
        other = [n for n in p.pins if n != p.number(first)][0]
        if to:
            self.power(p.pins[other][1], to, direction if to == "GND" or direction == "up" else None)
        return p

    def lab(self, pt, net, direction, length=2.54):
        """A short stub off pt and a global label at its end."""
        dx, dy = DIRS[direction]
        end = (pt[0] + dx * length, pt[1] + dy * length)
        self.wire(pt, end)
        self.label(end, net, direction)
        return end

    def pin_lab(self, p, key, net=None, length=2.54):
        """A label straight out of pin `key` of part p (the pin's own net by default)."""
        num = p.number(key)
        net = net or p.part.pin_net()[num]
        return self.lab(p[key], net, p.out(key), length)

    def nc(self, pt):
        self.ncs.add((_r(pt[0]), _r(pt[1])))

    def text(self, s, x, y, size=1.27, bold=False):
        self.w.text(s, x, y, size=size, bold=bold)

    # -- connectivity -----------------------------------------------------------------------
    def _points(self):
        pts = set()
        for a, b in self.wires:
            pts |= {a, b}
        for pl in self.placed.values():
            pts |= {v[1] for v in pl.pins.values()}
        pts |= {p for p, _, _ in self.names}
        return pts

    @staticmethod
    def _on(p, a, b):
        """p on segment a-b (ends included)."""
        if a[0] == b[0] == p[0]:
            return min(a[1], b[1]) <= p[1] <= max(a[1], b[1])
        if a[1] == b[1] == p[1]:
            return min(a[0], b[0]) <= p[0] <= max(a[0], b[0])
        return False

    def groups(self):
        pts = list(self._points())
        parent = {p: p for p in pts}

        def find(u):
            while parent[u] != u:
                parent[u] = parent[parent[u]]
                u = parent[u]
            return u
        for a, b in self.wires:
            on = [p for p in pts if self._on(p, a, b)]
            for p in on:
                parent[find(p)] = find(a)
        out = {}
        for p in pts:
            out.setdefault(find(p), set()).add(p)
        return list(out.values())

    # -- finishing --------------------------------------------------------------------------
    def finish(self):
        """Labels on loose pins, junctions, then check the drawing against the design and write
        it into the page. Returns the problems found."""
        problems = []
        wired = {p for a, b in self.wires for p in (a, b)}
        named = {p for p, _, _ in self.names}
        # loose pins: a global label (or a power symbol on a two-pin part) straight out
        for (ref, unit), pl in self.placed.items():
            nets = pl.part.pin_net()
            for num, (pin, pt, out) in pl.pins.items():
                net = nets[num]
                touched = pt in wired or pt in named or any(self._on(pt, a, b) for a, b in self.wires)
                if net is None:
                    if touched:
                        problems.append(f"{ref}.{num} is not connected but a wire / name reaches it")
                    else:
                        self.nc(pt)
                    continue
                if touched:
                    continue
                d = {v: k for k, v in DIRS.items()}[out]
                two_pin = len(pl.part.sym.unit_pins(unit)) <= 2
                if net in RAILS and two_pin:
                    self.power(pt, net, d)
                else:
                    self.label(pt, net, d)
        # check: each touching group is one net, named, with only that net's pins
        pin_at = {}
        for (ref, unit), pl in self.placed.items():
            nets = pl.part.pin_net()
            for num, (pin, pt, out) in pl.pins.items():
                pin_at.setdefault(pt, []).append((ref, num, nets[num]))
        name_at = {}
        for pt, net, _ in self.names:
            name_at.setdefault(pt, []).append(net)
        for grp in self.groups():
            pins = [x for p in grp for x in pin_at.get(p, [])]
            nets = {x[2] for x in pins if x[2]} | {n for p in grp for n in name_at.get(p, [])}
            if len(nets) > 1:
                problems.append(f"wires join nets {sorted(nets)}: {[f'{r}.{n}' for r, n, _ in pins]}")
            elif pins and not any(name_at.get(p) for p in grp) and any(x[2] for x in pins):
                problems.append(f"net {sorted(nets)} has no name: {[f'{r}.{n}' for r, n, _ in pins]}")
        # a wire running over a pin end in its middle joins it: allowed only on purpose (same net,
        # checked above), but a pin of a part hit mid-wire is worth a look if it is another net
        for (ref, unit), pl in self.placed.items():
            for num, (pin, pt, out) in pl.pins.items():
                for a, b in self.wires:
                    if pt not in (a, b) and self._on(pt, a, b):
                        nets = pl.part.pin_net()
                        if nets[num] is None:
                            problems.append(f"wire over unconnected pin {ref}.{num}")
        self._emit()
        return problems

    def _emit(self):
        w = self.w
        # symbols
        for (ref, unit), pl in self.placed.items():
            self._symbol(pl)
        # wires (and their boxes, so text keeps off them)
        for i, (a, b) in enumerate(self.wires):
            w.items.append(["wire", ["pts", ["xy", *a], ["xy", *b]], ["stroke", ["width", 0], ["type", "default"]],
                            ["uuid", Q(stable_uuid(self.d.name, w.name, "wire", a, b, i))]])
            # (ends left out: a wire meets its label / symbol / pin there by design)
            if a[1] == b[1]:
                x0, x1 = sorted((a[0], b[0]))
                box = (x0 + 0.5, a[1] - 0.15, x1 - 0.5, a[1] + 0.15)
            else:
                y0, y1 = sorted((a[1], b[1]))
                box = (a[0] - 0.15, y0 + 0.5, a[0] + 0.15, y1 - 0.5)
            if box[0] < box[2] and box[1] < box[3]:
                w.note_box(box, f"wire {a}-{b}", None, True)
        # junctions: three or more things meet, or a wire ends on another's middle
        ends = {}
        for a, b in self.wires:
            for p in (a, b):
                ends[p] = ends.get(p, 0) + 1
        # (a power symbol's pin on a wire's middle needs the dot too: KiCad joins it only then)
        pinpts = {v[1] for pl in self.placed.values() for v in pl.pins.values()}
        pinpts |= {p for p, _, (kind, _) in self.names if kind == "power"}
        for p in self._points():
            n = ends.get(p, 0) + (1 if p in pinpts else 0)
            mid = sum(1 for a, b in self.wires if p not in (a, b) and self._on(p, a, b))
            if n + 2 * mid >= 3:
                w.items.append(["junction", ["at", *p], ["diameter", 0], ["color", 0, 0, 0, 0],
                                ["uuid", Q(stable_uuid(self.d.name, w.name, "junction", p))]])
        # names
        for i, (pt, net, (kind, direction)) in enumerate(self.names):
            angle = AWAY_ANGLE[DIRS[direction]]
            key = f"drawn{i}:{pt}"
            if kind == "power":
                w.power_symbol(net, pt[0], pt[1], angle, key)
            else:
                w.global_label(net, pt[0], pt[1], angle, key)
        for p in sorted(self.ncs):
            w.items.append(["no_connect", ["at", *p], ["uuid", Q(stable_uuid(self.d.name, w.name, "nc", p))]])

    def _symbol(self, pl):
        w, part = self.w, pl.part
        sym = part.sym
        w.lib.setdefault(part.lib_id, sym)
        u = stable_uuid(self.d.name, part.ref, pl.unit)
        x0, y0, x1, y1 = pl.body
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        vis = [part.ref, part.value]
        f = pl.fields
        if f == "auto":
            f = "above" if (x1 - x0) > (y1 - y0) and len(sym.unit_pins(pl.unit)) <= 2 else "right"
        if f == "above":            # ref over the body, value under it
            spots = [(cx, y0 - 1.6), (cx, y1 + 1.6)]
        elif f == "below":
            spots = [(cx, y1 + 1.6), (cx, y1 + 1.6 + 2.54)]
        elif f == "left":
            wl = max(text_w(s) for s in vis)
            spots = [(x0 - 1.0 - wl / 2, cy - 1.27), (x0 - 1.0 - wl / 2, cy + 1.27)]
        elif f == "top":            # stacked over the body (ICs)
            spots = [(cx, y0 - 1.6 - 2.54), (cx, y0 - 1.6)]
        elif f in ("topleft", "topright"):      # over a corner, clear of a pin in the middle
            wl = max(text_w(s) for s in vis)
            fx = x0 + wl / 2 if f == "topleft" else x1 - wl / 2
            spots = [(fx, y0 - 1.6 - 2.54), (fx, y0 - 1.6)]
        elif f == "under":          # below a ground symbol at the bottom pin
            spots = [(cx, y1 + 8.89), (cx, y1 + 8.89 + 2.54)]
        elif f in ("bottomleft", "bottomright"):
            wl = max(text_w(s) for s in vis)
            fx = x0 + wl / 2 if f == "bottomleft" else x1 - wl / 2
            spots = [(fx, y1 + 1.6), (fx, y1 + 1.6 + 2.54)]
        else:                       # "right": stacked beside the body
            wr = max(text_w(s) for s in vis)
            spots = [(x1 + 1.0 + wr / 2, cy - 1.27), (x1 + 1.0 + wr / 2, cy + 1.27)]
        node = ["symbol", ["lib_id", Q(part.lib_id)], ["at", pl.x, pl.y, pl.rot]]
        if pl.mirror:
            node.append(["mirror", pl.mirror])
        node += [["unit", pl.unit], ["exclude_from_sim", "no"],
                 ["in_bom", "no" if part.lib_id.startswith("Connector:TestPoint") else "yes"],
                 ["on_board", "yes"], ["dnp", "yes" if part.dnp else "no"], ["uuid", Q(u)],
                 # (a field's angle turns with its part: 90 / 270 read level as 90 / 270, a part
                 # turned 180 needs 0 or its text is drawn upside down)
                 prop("Reference", part.ref, *spots[0], angle=pl.rot if pl.rot % 180 else 0),
                 prop("Value", part.value, *spots[1], angle=pl.rot if pl.rot % 180 else 0),
                 prop("Footprint", part.footprint, pl.x, pl.y, hide=True),
                 prop("Datasheet", "", pl.x, pl.y, hide=True),
                 prop("Description", "", pl.x, pl.y, hide=True)]
        for k, v in (("Note", part.note), ("LCSC", part.lcsc), ("MPN", part.mpn)):
            if v:
                node.append(prop(k, v, pl.x, pl.y, hide=True))
        for p in sym.unit_pins(pl.unit):
            node.append(["pin", Q(p.number), ["uuid", Q(stable_uuid(u, p.number))]])
        node.append(["instances", ["project", Q(self.d.name),
                                   ["path", Q(w.path), ["reference", Q(part.ref)], ["unit", pl.unit]]]])
        w.items.append(node)
        w.note_box(pl.body, f"{part.ref} body", part.ref, True)
        for s, (sx, sy) in zip(vis, spots):
            w.note_box(field_box(s, sx, sy, None), f"{part.ref} field {s}", part.ref)


# --------------------------------------------------------------------------------------------
# The pages
# --------------------------------------------------------------------------------------------

def _refs(design, block):
    by = {}
    for p in [b for b in design.blocks if b.name == block][0].parts:
        by.setdefault(p.value, []).append(p.ref)
    return lambda value, i=0: by[value][i]


def power_page(D, design):
    """J1962 pin 16 -> PTC -> TVS -> ideal diode -> buck -> 5 V OR -> LDO, left to right on one
    rail at y = RAIL, each stage's parts hanging off it to ground. Laid out with a running x."""
    ref = _refs(design, "power")
    RAIL = 60.96
    STEP = 12.7                 # between two parts hanging off the rail (room for "47u 50V" beside each)
    ROW2 = 170.18               # the second row: 5V_BUCK -> OR -> LDO

    def hang(r, x, y=RAIL, fields="right"):
        c = D.part(r, x, y + 8.89, fields=fields)
        D.wire((x, y), c["1"])
        D.power(c["2"], "GND")
        return c

    def room(r):
        """Distance to the next hanging part: this one's body, its labels beside it, a gap."""
        p = D.parts[r]
        need = 2.3 + 1.0 + max(text_w(p.ref), text_w(p.value)) + 1.5 + 2.3
        return max(STEP, -(-need // G) * G)

    def hang_row(refs, x, y=RAIL):
        """Parts hanging off the rail from x on; returns the x after the last one."""
        for r in refs:
            hang(r, x, y)
            x += room(r)
        return x

    D.text("Power: J1962 pin 16 -> PTC -> TVS -> ideal diode -> 5.3 V buck -> 5 V OR (USB) -> 3.3 V LDO",
           20.32, 15.24, size=3, bold=True)
    for i, note in enumerate([b for b in design.blocks if b.name == "power"][0].notes):
        D.text(note, 20.32, 22.86 + i * 3.0)

    # -- input: VBAT_IN -> PTC -> VBAT_F -> TVS, input cap --------------------------------
    x = 30.48
    D.label((x, RAIL), "VBAT_IN", "left")
    f1 = D.part(ref("2A 60V"), x + 10.16, RAIL, rot=90, fields="above")
    D.wire((x, RAIL), f1["1"])
    x = f1["2"][0] + 3.81
    D.power((x, RAIL), "VBAT_F")
    x += 7.62
    tvs = D.part(ref("5.0SMDJ26CA"), x, RAIL + 8.89, rot=270)
    D.wire((x, RAIL), tvs["1"])
    D.power(tvs["2"], "GND")
    x += 22.86
    hang(ref("100n", 0), x)                              # LM74700 input cap
    # -- the ideal diode: FET in the rail (sources left, drains right), its controller under it
    x_en = x + 15.24
    x_vcap = x_en + 15.24
    x_anode = x_vcap + 5.08
    xq = x_anode + 12.7
    q1 = D.part(ref("AO4264E"), xq, RAIL, mirror="x", fields="top")
    D.wire(f1["2"], q1["1"])
    xs, xd = q1["1"][0] - 2.54, q1["7"][0] + 2.54
    D.wire((xs, q1["3"][1]), (xs, RAIL))
    D.wire((xs, q1["3"][1]), q1["3"])
    D.wire((xs, q1["2"][1]), q1["2"])
    D.wire((xd, q1["8"][1]), (xd, q1["5"][1]))
    for d in ("5", "6", "7", "8"):
        D.wire(q1[d], (xd, q1[d][1]))
    u6 = D.part(ref("LM74700QDBVRQ1"), xq, RAIL + 43.18, fields="right")
    gy = q1["4"][1] + 5.08
    D.wire(u6["GATE"], (xq, gy), (xs + 1.27, gy), (xs + 1.27, q1["4"][1]), q1["4"])
    D.name(((xs + 1.27 + xq) / 2, gy), "IDEAL_GATE", "down", length=2.54)
    D.wire(u6["ANODE"], (x_anode, u6["ANODE"][1]), (x_anode, RAIL))
    D.wire(u6["CATHODE"], (xd, u6["CATHODE"][1]), (xd, q1["5"][1]))
    D.power(u6["GND"], "GND")
    c20 = D.part(ref("100n", 1), x_vcap, u6["VCAP"][1] - 5.08 - 3.81, rot=180, fields="left")
    D.wire(u6["VCAP"], (x_vcap, u6["VCAP"][1]), c20["1"])
    D.wire(c20["2"], (x_vcap, c20["2"][1] - 2.54), (x_anode, c20["2"][1] - 2.54))
    D.name((x_vcap + 2.54, u6["VCAP"][1]), "IDEAL_VCAP", "down", length=7.62)
    D.wire(u6["EN"], (x_en, u6["EN"][1]), (x_en, RAIL))

    # -- VBAT_P: input caps, test point, the buck's VIN ---------------------------------
    x = xd + 2.54
    D.power((x, RAIL), "VBAT_P")
    x = hang_row([ref("47u 50V"), ref("10u", 0), ref("100n", 2), ref("2.2u", 0), ref("2.2u", 1)], x + 5.08)
    D.part(ref("VBAT_P"), x, RAIL, fields="top")
    x_rt = x + 7.62
    xu7 = x_rt + 15.24 + 10.16
    u7 = D.part(ref("TPS54360BDDAR"), xu7, RAIL + 5.08, fields="top")
    D.wire((xd, RAIL), u7["VIN"])
    # RT to ground; EN open (internal pull-up: UVLO only)
    r19 = D.part(ref("162k"), x_rt, RAIL + 25.4, fields="left")
    D.wire(u7["RT/CLK"], (x_rt, u7["RT/CLK"][1]), r19["1"])
    D.power(r19["2"], "GND")
    D.name((x_rt + 5.08, u7["RT/CLK"][1]), "BUCK_RT", "down", length=5.08)
    gnd_y = u7["GND"][1] + 2.54
    D.wire(u7["GNDPAD"], (u7["GNDPAD"][0], gnd_y), (u7["GND"][0], gnd_y))
    D.wire(u7["GND"], (u7["GND"][0], gnd_y))
    D.power((u7["GND"][0], gnd_y), "GND")

    # -- switch node: BOOT cap, catch diode, inductor --------------------------------------
    SW = u7["SW"][1]
    xb = u7["BOOT"][0] + 2.54
    c26 = D.part(ref("100n", 3), xb + 7.62, SW - 3.81, fields="right")
    D.wire(u7["BOOT"], (xb, u7["BOOT"][1]), (xb, c26["1"][1]), c26["1"])
    D.name((xb, c26["1"][1]), "BUCK_BOOT", "up", length=2.54)
    xd5 = c26["1"][0] + 15.24
    d5 = D.part(ref("B560C"), xd5, SW + 3.81, rot=270, fields="right")
    D.power(d5["A"], "GND")
    xl = xd5 + 20.32
    l1 = D.part(ref("8.2u"), xl, SW, rot=90, fields="above")
    D.wire(u7["SW"], l1["1"])
    D.name((xd5 + 7.62, SW), "BUCK_SW", "up", length=2.54)
    # compensation under the chip: COMP -> 39p to ground, and 13k + 6.8n to ground
    comp_y = u7["COMP"][1]
    xc = u7["COMP"][0] + 5.08
    c28 = D.part(ref("39p"), xc, RAIL + 30.48, fields="right")
    r22 = D.part(ref("13k"), xc + 10.16, RAIL + 30.48, fields="right")
    c27 = D.part(ref("6.8n"), xc + 10.16, RAIL + 43.18, fields="right")
    D.wire(u7["COMP"], (xc, comp_y), c28["1"])
    split = c28["1"][1] - 3.81
    D.wire((xc, split), (xc + 10.16, split), r22["1"])
    D.power(c28["2"], "GND")
    D.wire(r22["2"], c27["1"])
    D.power(c27["2"], "GND")
    D.name((xc + 10.16, split), "BUCK_COMP", "right", length=2.54)
    D.name((xc + 10.16, (r22["2"][1] + c27["1"][1]) / 2), "BUCK_COMP_RC", "right", length=2.54)

    # -- 5V_BUCK: output caps and the feedback divider (row 1 ends here) -----------------
    OUT = SW
    x = l1["2"][0] + 3.81
    D.power((x, OUT), "5V_BUCK")
    xr = hang_row([ref("47u", 0), ref("47u", 1), ref("47u", 2)], x + 5.08, OUT)
    D.wire(l1["2"], (xr, OUT))
    r20 = D.part(ref("57.6k"), xr, OUT + 45.72, fields="right")
    r21 = D.part(ref("10.2k"), xr, OUT + 63.5, fields="right")
    D.wire((xr, OUT), r20["1"])
    D.wire(r20["2"], r21["1"])
    D.power(r21["2"], "GND")
    fb_y = (r20["2"][1] + r21["1"][1]) / 2
    xfb = u7["FB"][0] + 1.27
    D.wire(u7["FB"], (xfb, u7["FB"][1]), (xfb, fb_y), (xr, fb_y))
    D.name((xr - 12.7, fb_y), "BUCK_FB", "down", length=2.54)

    # -- row 2: 5V_BUCK -> OR diode -> 5V (OR'd with the STM32 USB's VBUS) -> LDO -> 3V3 ---
    D.text("5 V: the buck OR'd with USB power; 3.3 V LDO", 132.08, ROW2 - 22.86, size=1.8, bold=True)
    x = 137.16
    D.power((x, ROW2), "5V_BUCK")
    d6 = D.part(ref("SS34", 0), x + 10.16, ROW2, rot=180, fields="above")
    D.wire((x, ROW2), d6["A"])
    x = d6["K"][0] + 3.81
    D.power((x, ROW2), "5V")
    xd7 = x + 7.62
    d7 = D.part(ref("SS34", 1), xd7, ROW2 + 3.81, rot=270, fields="right")
    D.wire(d7["A"], (xd7, ROW2 + 12.7))
    D.label((xd7, ROW2 + 12.7), "VBUS", "down")
    D.text("(from the STM32's USB-C)", xd7 - 8.89, ROW2 + 22.86)
    xtp2 = hang_row([ref("10u", 1), ref("10u", 2)], xd7 + 17.78, ROW2)
    D.part(ref("5V"), xtp2, ROW2, fields="top")
    u8 = D.part(ref("AP2114H-3.3"), xtp2 + 17.78, ROW2, fields="top")
    D.wire(d6["K"], u8["VI"])
    D.power(u8["GND"], "GND")
    x = u8["VO"][0] + 7.62
    hang(ref("10u", 3), x, ROW2)
    D.part(ref("3V3"), x + STEP, ROW2, fields="top")
    D.wire(u8["VO"], (x + STEP + 5.08, ROW2))
    D.power((x + STEP + 5.08, ROW2), "3V3")

    # -- battery sense: VBAT_F / 11 to PA4 ----------------------------------------------------
    sx, sy = 40.64, ROW2 - 5.08
    D.text("Battery sense (to PA4)", sx - 15.24, sy - 17.78, size=1.8, bold=True)
    r23 = D.part(ref("100k 0.1%"), sx, sy, fields="left")
    r24 = D.part(ref("10k 0.1%"), sx, sy + 12.7, fields="left")
    D.power(r23["1"], "VBAT_F")
    D.wire(r23["2"], r24["1"])
    D.power(r24["2"], "GND")
    tap = (sx, (r23["2"][1] + r24["1"][1]) / 2)
    c35 = D.part(ref("100n", 4), sx + 10.16, sy + 12.7, fields="right")
    D.wire(tap, (sx + 10.16, tap[1]), c35["1"])
    D.power(c35["2"], "GND")
    D.wire((sx + 10.16, tap[1]), (sx + 20.32, tap[1]))
    D.label((sx + 20.32, tap[1]), "VBAT_SENSE", "right")

    # -- ground test point ------------------------------------------------------------------
    D.text("GND test point", 86.36, ROW2 - 22.86, size=1.8, bold=True)
    tp4 = D.part(ref("GND"), 93.98, ROW2 - 5.08, fields="right")
    D.power(tp4["1"], "GND")
    return []


def notes(D, design, block, x, y):
    for i, note in enumerate([b for b in design.blocks if b.name == block][0].notes):
        D.text(note, x, y + i * 3.0)


def car_can_page(D, design):
    """The J1962 plug (every pin labelled: it fans out to five pages), then each CAN channel left
    to right: the bus pair from the plug, its clamp under the pair, its 120R termination over it,
    the transceiver turned so the bus comes in on its left and the logic leaves on its right."""
    car, can = _refs(design, "car"), _refs(design, "can")
    D.text("Car plug and CAN", 20.32, 15.24, size=3, bold=True)
    notes(D, design, "car", 20.32, 22.86)

    j6 = D.part(car("OBD-II plug (J1962 male)"), 50.8, 81.28, fields="top")
    D.text("J1962 pin out (male plug, the car's view of it mirrored)", 25.4, 60.96, size=1.5, bold=True)
    for num in [str(n) for n in range(1, 17)]:
        net = j6.part.pin_net()[num]
        if net is not None:
            D.pin_lab(j6, num, length=5.08)
    D.text("4 = chassis GND, 5 = signal GND, 16 = battery +", 25.4, 99.06)
    D.text("1, 8, 9, 12, 13: not used", 25.4, 102.1)

    def channel(i, x0, y0):
        u = D.part(can("TCAN1044VDRQ1", i), x0 + 55.88, y0 + 2.54, mirror="y", fields="under")
        h, l = u["CANH"], u["CANL"]
        D.label((x0, h[1]), f"CAN{i + 1}_H", "left")
        D.label((x0, l[1]), f"CAN{i + 1}_L", "left")
        D.wire((x0, h[1]), h)
        D.wire((x0, l[1]), l)
        # clamp under the pair: K1 to CAN_H (crosses CAN_L), K2 to CAN_L, A to ground
        d = D.part(can("SZNUP2105L", i), x0 + 15.24, l[1] + 10.16, fields="right")
        D.wire(d["1"], (d["1"][0], h[1]))
        D.wire(d["2"], (d["2"][0], l[1]))
        D.power(d["A"], "GND")
        # 120R from CAN_H up to the CONFIG switch (which closes it onto CAN_L on a bench bus)
        r = D.hang(can("120", i), (x0 + 30.48, h[1]), to=None, direction="up", fields="right")
        D.label(r["2"], f"CAN{i + 1}_TERM", "up")
        D.text(f"(CONFIG sw. {i + 2} -> CAN{i + 1}_L)", x0 - 7.62, h[1] - 20.32)
        # logic side, to the STM32
        for k in ("TXD", "RXD", "S"):
            D.pin_lab(u, k, length=5.08)
        D.pin_lab(u, "VIO", length=5.08)
        D.power(u["VCC"], "5V_BUCK")
        D.power(u["GND"], "GND")
        # its decoupling (at the VCC / VIO pins on the board) and the STB pull-up
        xc = x0 + 101.6
        D.text("decoupling, STB pull-up", xc - 2.54, y0 - 12.7)
        for k, (val, rail) in enumerate((("100n", "5V_BUCK"), ("100n", "3V3"))):
            ref = [p for p in u.part.block.parts if p.value == val and rail in p.pin_net().values()][i].ref
            c = D.part(ref, xc + k * 12.7, y0, fields="right")
            D.power(c["1"], rail)
            D.power(c["2"], "GND")
        rp = D.part(can("4.7k", i), xc + 25.4, y0, fields="right")
        D.power(rp["1"], "3V3")
        D.lab(rp["2"], f"CAN{i + 1}_STB", "down", length=2.54)

    D.text("HS-CAN (J1962 6 / 14): CAN1", 93.98, 40.64, size=1.8, bold=True)
    channel(0, 101.6, 71.12)
    D.text("MS-CAN (J1962 3 / 11): CAN2", 93.98, 106.68, size=1.8, bold=True)
    channel(1, 101.6, 137.16)
    notes(D, design, "can", 101.6, 175.26)


def j1850_page(D, design):
    """Row 1: the LM317L supply (7.2 V VPW / 4.8 V PWM via Q4). Row 2: the Bus+ driver
    (2N7002 -> P-FET -> Schottky) and the Bus- driver (NPN sink, pull-up). Row 3: the two receive
    comparators (VPW: Bus+ vs a reference; PWM: Bus+ vs Bus-), the comparator supply, the spare unit."""
    j, F = _refs(design, "j1850"), D.find
    D.text("SAE J1850 VPW / PWM (J1962 pins 2 / 10)", 20.32, 15.24, size=3, bold=True)

    # -- row 1: supply --------------------------------------------------------------------
    D.text("Supply: VJ1850 = 7.2 V (VPW); J1850_VSEL high shorts the 470R: 4.8 V (PWM)", 25.4, 33.02, size=1.8, bold=True)
    Y = 50.8
    D.power((30.48, Y), "DLC_SW")
    u11 = D.part(j("LM317LIDR"), 58.42, Y, fields="top")
    D.wire((30.48, Y), u11["VI"])
    D.hang(F("100n", {"1": "DLC_SW"}), (40.64, Y))
    D.wire(u11["VO"], (127.0, Y))
    D.power((127.0, Y), "VJ1850")
    D.hang(j("1u"), (76.2, Y))
    D.part(j("VJ1850"), 111.76, Y, fields="top")
    r32 = D.hang(j("240"), (88.9, Y), to=None)
    adj_y = r32["2"][1] + 7.62
    D.wire(u11["ADJ"], (u11["ADJ"][0], adj_y), (88.9, adj_y), r32["2"])
    D.name((66.04, adj_y), "VJ_ADJ", "down")
    r33 = D.inline(j("680"), (88.9, adj_y), "down", fields="left")
    D.hang(F("470", {"1": "VJ_N1"}), r33["2"], fields="left")
    q4 = D.part(j("2N7002", 0), 104.14, r33["2"][1] + 15.24, fields="right")
    D.wire(r33["2"], (q4["D"][0], r33["2"][1]), q4["D"])
    D.name((96.52, r33["2"][1]), "VJ_N1", "up")
    D.power(q4["S"], "GND")
    gx = q4["G"][0] - 2.54
    D.wire(q4["G"], (gx, q4["G"][1]), (gx, q4["G"][1] + 7.62))
    D.hang(F("10k", {"1": "J1850_VSEL"}), (gx, q4["G"][1] + 7.62), fields="left")
    D.lab((gx, q4["G"][1] + 5.08), "J1850_VSEL", "left", length=5.08)

    # -- row 2: Bus+ driver -----------------------------------------------------------------
    D.text("Bus+ driver: TXP high -> Q5 on -> P-FET Q6 on", 25.4, 119.38, size=1.8, bold=True)
    Y2 = 160.02
    q5 = D.part(j("2N7002", 1), 53.34, Y2, fields="right")
    D.label((33.02, Y2), "J1850_TXP", "left")
    D.wire((33.02, Y2), q5["G"])
    D.hang(F("10k", {"1": "J1850_TXP"}), (40.64, Y2), fields="left")
    D.power(q5["S"], "GND")
    q6 = D.part(j("AO3401A"), 78.74, Y2 - 15.24, mirror="x", fields="right")
    pg_y = q6["G"][1]
    D.wire(q5["D"], (q5["D"][0], pg_y), q6["G"])
    D.hang(j("1.5k"), (66.04, pg_y), to="VJ1850", direction="up", first="2", fields="right")
    D.wire(q6["S"], (q6["S"][0], q6["S"][1] - 5.08))
    D.power((q6["S"][0], q6["S"][1] - 5.08), "VJ1850")
    D.name((60.96, pg_y), "J1850_PG", "up", length=2.54)
    d11 = D.inline(j("B5819W", 0), (q6["D"][0], q6["D"][1] + 5.08), "right", first="A", fields="above")
    D.wire(q6["D"], d11["A"])
    D.name((q6["D"][0], q6["D"][1] + 2.54), "J1850_PD", "left", length=2.54)
    bus = d11["K"][1]
    D.wire(d11["K"], (139.7, bus))
    D.label((139.7, bus), "J1850_P", "right")
    D.text("to J1962 pin 2", 132.08, bus - 5.08)
    D.hang(F("SMF24A", {"1": "J1850_P"}), (96.52, bus), first="1")
    D.part(j("J1850_P"), 106.68, bus, fields="top")
    r38 = D.hang(F("10k", {"1": "J1850_P", "2": "J1850_PLOAD"}), (116.84, bus), to=None, fields="left")
    d12 = D.inline(j("B5819W", 1), r38["2"], "down", first="A", fields="right")
    D.power(d12["K"], "GND")
    D.name(r38["2"], "J1850_PLOAD", "right", length=2.54)

    # -- row 2: Bus- driver -----------------------------------------------------------------
    D.text("Bus- driver (PWM): TXN high -> Q7 pulls Bus- low", 165.1, 119.38, size=1.8, bold=True)
    q7 = D.part(j("MMBT3904"), 210.82, Y2, fields="right")
    D.label((172.72, Y2), "J1850_TXN", "left")
    r39 = D.inline(j("1k"), (185.42, Y2), "right", fields="above")
    D.wire((172.72, Y2), r39["1"])
    D.hang(j("100k"), (177.8, Y2), fields="left")
    D.wire(r39["2"], q7["B"])
    D.hang(j("2.2k"), (200.66, Y2), fields="left")
    D.name((195.58, Y2), "J1850_NB", "up", length=2.54)
    D.power(q7["E"], "GND")
    nbus = q7["C"][1] - 7.62
    D.wire(q7["C"], (q7["C"][0], nbus), (256.54, nbus))
    D.label((256.54, nbus), "J1850_N", "right")
    D.text("to J1962 pin 10", 248.92, nbus - 5.08)
    r42 = D.inline(F("10k", {"2": "J1850_N"}), (223.52, nbus), "up", first="2", fields="right")
    d13 = D.inline(j("B5819W", 2), r42["1"], "up", first="K", fields="right")
    D.power(d13["A"], "5V_BUCK")
    D.name(r42["1"], "J1850_NPU", "left", length=2.54)
    D.hang(F("SMF24A", {"1": "J1850_N"}), (233.68, nbus), first="1")
    D.part(j("J1850_N"), 243.84, nbus, fields="top")

    # -- row 3: receivers ---------------------------------------------------------------------
    D.text("Receivers: LM2901 on CMP_VCC; open-collector outputs pulled up to 3.3 V", 25.4, 182.88, size=1.8, bold=True)
    Y3 = 205.74
    ua = D.part(j("LM2901BIDR"), 88.9, Y3, unit=1, fields="topright")
    D.label((33.02, ua["+"][1]), "J1850_P", "left")
    r44 = D.inline(F("10k", {"2": "VPW_IN"}), (40.64, ua["+"][1]), "right", fields="above")
    D.wire((33.02, ua["+"][1]), r44["1"])
    D.wire(r44["2"], ua["+"])
    D.hang(F("1N4148W", {"1": "VPW_IN"}), (55.88, ua["+"][1]), first="K", fields="left")
    D.name((66.04, ua["+"][1]), "VPW_IN", "up", length=2.54)
    ref_y = ua["-"][1] + 7.62
    D.wire(ua["-"], (73.66, ua["-"][1]), (73.66, ref_y))
    D.hang(F("47k", {"1": "VPW_REF"}), (73.66, ref_y), fields="right")
    r45 = D.inline(j("18k"), (73.66, ref_y), "left", first="2", fields="below")
    D.power(r45["1"], "5V_BUCK")
    D.name((73.66, ref_y - 2.54), "VPW_REF", "right", length=2.54)
    D.wire(ua["2"], (111.76, Y3))
    D.label((111.76, Y3), "VPW_RX", "right")
    D.hang(F("10k", {"2": "VPW_RX"}), (104.14, Y3), to="3V3", direction="up", first="2", fields="left")
    ub = D.part(j("LM2901BIDR"), 205.74, Y3, unit=2, fields="topright")
    D.label((149.86, ub["+"][1]), "J1850_P", "left")
    r48 = D.inline(F("10k", {"2": "PWM_INP"}), (157.48, ub["+"][1]), "right", fields="above")
    D.wire((149.86, ub["+"][1]), r48["1"])
    D.wire(r48["2"], ub["+"])
    D.hang(F("1N4148W", {"1": "PWM_INP"}), (172.72, ub["+"][1]), first="K", fields="left")
    D.name((185.42, ub["+"][1]), "PWM_INP", "up", length=2.54)
    inn_y = ub["-"][1] + 12.7
    D.label((149.86, inn_y), "J1850_N", "left")
    r49 = D.inline(F("10k", {"2": "PWM_INN"}), (157.48, inn_y), "right", fields="below")
    D.wire((149.86, inn_y), r49["1"])
    D.wire(r49["2"], (190.5, inn_y), (190.5, ub["-"][1]), ub["-"])
    D.hang(F("1N4148W", {"1": "PWM_INN"}), (177.8, inn_y), first="K", fields="left")
    D.name((185.42, inn_y), "PWM_INN", "down", length=2.54)
    D.wire(ub["1"], (228.6, Y3))
    D.label((228.6, Y3), "PWM_RX", "right")
    D.hang(F("10k", {"2": "PWM_RX"}), (220.98, Y3), to="3V3", direction="up", first="2", fields="left")
    ud = D.part(j("LM2901BIDR"), 287.02, Y3, unit=4, fields="topright")
    D.pin_lab(ud, "+", length=5.08)
    D.pin_lab(ud, "-", length=5.08)
    D.text("spare unit, inputs tied off", 271.78, Y3 + 10.16)
    ue = D.part(j("LM2901BIDR"), 317.5, Y3, unit=5, fields="right")
    D.power(ue["V+"], "CMP_VCC")
    D.power(ue["V-"], "GND")
    D.text("CMP_VCC: DLC_SW via 470R, 33 V zener", 330.2, 182.88, size=1.8, bold=True)
    D.power((332.74, 198.12), "DLC_SW")
    r43 = D.inline(F("470", {"2": "CMP_VCC"}), (332.74, 198.12), "right", fields="above")
    D.wire(r43["2"], (375.92, 198.12))
    D.power((375.92, 198.12), "CMP_VCC")
    D.hang(j("BZT52C33"), (347.98, 198.12), first="K")
    D.hang(F("100n", {"1": "CMP_VCC"}), (363.22, 198.12))
    D.text("Notes", 157.48, 40.64, size=1.8, bold=True)
    notes(D, design, "j1850", 157.48, 48.26)


def iso_page(D, design):
    """DLC_SW (the switched battery) on top, then the K-line (driver, pull-up, clamp, receiver)
    and the L-line (driver, pull-up, clamp)."""
    iso, sw, F = _refs(design, "iso"), _refs(design, "dlcsw"), D.find
    D.text("ISO 9141 / 14230 K- and L-line; DLC_SW", 20.32, 15.24, size=3, bold=True)

    # -- DLC_SW: P-FET high side, zener gate clamp, 2N7002 level shift ---------------------
    D.text("DLC_SW: battery to the legacy-bus front ends, off in sleep (DLC_SW_EN)", 25.4, 33.02, size=1.8, bold=True)
    q2 = D.part(sw("SI2309CDS"), 101.6, 63.5, mirror="x", fields="right")
    top = q2["S"][1] - 7.62
    D.power((45.72, top), "VBAT_P")
    D.wire((45.72, top), (q2["S"][0], top), q2["S"])
    gate = q2["G"]
    d10 = D.hang(sw("BZT52C12"), (60.96, top), to=None, first="K", fields="left")
    r29 = D.hang(sw("47k"), (71.12, top), to=None, fields="right")
    D.wire(d10["A"], (60.96, gate[1]), gate)
    D.wire(r29["2"], (71.12, gate[1]))
    D.name((60.96, gate[1]), "DLC_SW_G", "left", length=2.54)
    r30 = D.inline(sw("10k"), (83.82, gate[1]), "down", fields="right")
    q3 = D.part(sw("2N7002"), 81.28, r30["2"][1] + 10.16, fields="right")
    D.wire(r30["2"], q3["D"])
    D.name((83.82, r30["2"][1] + 2.54), "DLC_SW_D", "left", length=2.54)
    D.power(q3["S"], "GND")
    D.label((48.26, q3["G"][1]), "DLC_SW_EN", "left")
    D.wire((48.26, q3["G"][1]), q3["G"])
    D.hang(sw("100k"), (58.42, q3["G"][1]), fields="left")
    out = q2["D"][1] + 5.08
    D.wire(q2["D"], (q2["D"][0], out), (147.32, out))
    D.power((147.32, out), "DLC_SW")
    D.hang(sw("1u"), (121.92, out))
    D.part(sw("DLC_SW"), 134.62, out, fields="top")

    def line(name, y, d_pu, r_pu, q, r_b, r_dn, tvs, tp, pin):
        """DLC_SW -> diode -> 510R -> the line; an NPN sinks it; clamp, test point."""
        D.power((30.48, y), "DLC_SW")
        dd = D.inline(d_pu, (35.56, y), "right", first="A", fields="above")
        D.wire((30.48, y), dd["A"])
        rr = D.inline(r_pu, (dd["K"][0] + 7.62, y), "right", fields="above")
        D.wire(dd["K"], rr["1"])
        D.name((dd["K"][0] + 2.54, y), f"{name}_PU", "up", length=2.54)
        D.wire(rr["2"], (139.7, y))
        D.label((139.7, y), name, "right")
        D.text(f"to J1962 pin {pin}", 132.08, y - 5.08)
        qq = D.part(q, 76.2, y + 15.24, fields="right")
        D.wire(qq["C"], (qq["C"][0], y))
        D.power(qq["E"], "GND")
        D.label((33.02, qq["B"][1]), f"{name}_TX", "left")
        rb = D.inline(r_b, (50.8, qq["B"][1]), "right", fields="above")
        D.wire((33.02, qq["B"][1]), rb["1"])
        D.wire(rb["2"], qq["B"])
        D.name(rb["2"], f"{name}_B", "down", length=2.54)
        D.hang(r_dn, (40.64, qq["B"][1]), fields="left")
        D.hang(tvs, (99.06, y), first="1")
        D.part(tp, 109.22, y, fields="top")

    # -- K-line + its receiver ------------------------------------------------------------
    D.text("K-line (J1962 pin 7): driver, 510R pull-up, receiver (LM2901 unit C)", 25.4, 99.06, size=1.8, bold=True)
    YK = 119.38
    line("KLINE", YK, iso("1N4148W", 0), iso("510 2W", 0), iso("MMBT3904", 0), iso("470", 0), iso("100k", 0),
         iso("SMF24A", 0), iso("KLINE"), 7)
    r57 = D.inline(F("10k", {"2": "K_IN"}), (119.38, YK), "down", fields="left")
    uc = D.part(F("LM2901BIDR", {}), 162.56, YK + 22.86, unit=3, fields="topright")
    kin_y = uc["+"][1]
    D.wire(r57["2"], (119.38, kin_y), uc["+"])
    D.hang("D18", (132.08, kin_y), first="K", fields="left")
    D.name((142.24, kin_y), "K_IN", "up", length=2.54)
    kref = (149.86, uc["-"][1] + 7.62)
    D.wire(uc["-"], (149.86, uc["-"][1]), kref)
    D.hang(F("10k", {"1": "K_REF"}), kref, fields="right")
    r58 = D.inline(F("10k", {"2": "K_REF"}), kref, "left", first="2", fields="below")
    D.power(r58["1"], "DLC_SW")
    D.name((149.86, kref[1] - 2.54), "K_REF", "right", length=2.54)
    D.wire(uc["13"], (185.42, uc["13"][1]))
    D.label((185.42, uc["13"][1]), "KLINE_RX", "right")
    D.text("K vs DLC_SW / 2: K high -> KLINE_RX high", 175.26, YK + 33.02)
    D.text("(open collector: PC0's internal pull-up, set by firmware)", 175.26, YK + 36.02)

    # -- L-line -------------------------------------------------------------------------------
    D.text("L-line (J1962 pin 15): driver, 510R pull-up (transmit only)", 25.4, 175.26, size=1.8, bold=True)
    line("LLINE", 195.58, iso("1N4148W", 1), iso("510 2W", 1), iso("MMBT3904", 1), iso("470", 1), iso("100k", 1),
         iso("SMF24A", 1), iso("LLINE"), 15)
    notes(D, design, "iso", 210.82, 175.26)
    notes(D, design, "dlcsw", 210.82, 33.02)


# the K-line receiver's input clamp sits in the J1850 block of the netlist, but reads with the K-line
PAGE_OF = {"D18": "K-line and DLC switch"}     # ref -> page, for a part drawn away from its block's page
SPLIT = {"U12"}         # multi-unit parts whose units are drawn on more than one page (LM2901: K-line = unit C)


PAGES = {"Power": power_page, "Car plug and CAN": car_can_page, "J1850": j1850_page,
         "K-line and DLC switch": iso_page}
