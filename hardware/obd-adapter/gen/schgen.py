"""Write a KiCad schematic from a Design.

Every connected pin gets a local net label at its end, unconnected pins get
a no-connect flag. Blocks are laid out as titled columns of parts, so the
sheet reads block by block. Power rails get one PWR_FLAG each.

Parts are spaced by what they really take up, labels and text included, and
check() fails the build if any two pieces of text touch.
"""

from design import stable_uuid
from kilib import Symbol
from sexpr import Q, dump, find

GRID = 2.54
FIELD_GRID = 1.27
CHAR_RATIO = 1.05  # stroke font advance / size: 1.22 mm measured at 1.27 mm, rounded up
TEXT_H = 1.7       # drawn height of a 1.27 mm line
CLEAR = 0.3        # least gap between two pieces of text
PART_GAP = 5.08    # between two parts' extents
STUB = GRID        # wire from a pin to its label, so the label clears the pin number
BLOCK_GAP = 20
TITLE_H = 7        # a block's title line
NOTE_H = 3
# (width, height) of the paper, and what the title block takes out of its bottom edge
PAPER = {"A1": (841, 594), "A0": (1189, 841)}
TITLE_BLOCK_H = 50


def snap(v, grid=GRID):
    return round(v / grid) * grid


def font(size=1.27, bold=False):
    f = ["font", ["size", size, size]]
    if bold:
        f.append(["bold", "yes"])
    return f


def prop(name, value, x, y, hide=False, justify=None, angle=0):
    eff = ["effects", font()]
    if justify:
        eff.append(["justify", justify])
    p = ["property", Q(name), Q(value), ["at", x, y, angle]]
    if hide:
        p.append(["hide", "yes"])
    p.append(eff)
    return p


def label_angle(pin_rot):
    # the pin points from its end into the body; the label text goes the other way
    return {0: 180, 90: 270, 180: 0, 270: 90}[int(pin_rot) % 360]


def text_w(s, size=1.27):
    return len(s) * size * CHAR_RATIO


def label_box(net, x, y, angle):
    """Sheet box of a net label anchored at (x, y); the text sits on the pin's side of the line."""
    w, h = text_w(net), TEXT_H
    if angle == 0:
        return (x, y - h, x + w, y + 0.2)
    if angle == 180:
        return (x - w, y - h, x, y + 0.2)
    if angle == 90:
        return (x - h, y - w, x + 0.2, y)
    return (x - h, y, x + 0.2, y + w)


def stub_end(x, y, angle):
    dx, dy = {0: (STUB, 0), 180: (-STUB, 0), 90: (0, -STUB), 270: (0, STUB)}[angle]
    return x + dx, y + dy


def field_box(s, x, y, justify):
    w = text_w(s)
    x0 = {"left": x, "right": x - w, None: x - w / 2}[justify]
    return (x0, y - TEXT_H / 2, x0 + w, y + TEXT_H / 2)


def hit(a, b, gap=CLEAR):
    return a[0] < b[2] + gap and b[0] < a[2] + gap and a[1] < b[3] + gap and b[1] < a[3] + gap


def shift(box, x, y):
    return (box[0] + x, box[1] + y, box[2] + x, box[3] + y)


def union(boxes):
    boxes = list(boxes)
    return (min(b[0] for b in boxes), min(b[1] for b in boxes),
            max(b[2] for b in boxes), max(b[3] for b in boxes))


class Unit:
    """One symbol unit worked out relative to its origin, in sheet coordinates (y down)."""

    def __init__(self, part, unit):
        self.part, self.unit = part, unit
        sym = part.sym
        pins = sym.unit_pins(unit)
        # a two-pin part drawn upright is turned on its side, pin 1 on the left: its labels then
        # read level and keep to their own line, clear of the reference and value
        self.rot = 90 if len(pins) == 2 and all(p.x == 0 for p in pins) and pins[0].y != pins[1].y else 0
        x0, y0, x1, y1 = sym.bbox(unit)
        c = [self.turn(x0, y0), self.turn(x1, y1)]
        self.body = (min(p[0] for p in c), min(p[1] for p in c), max(p[0] for p in c), max(p[1] for p in c))
        nets = part.pin_net()
        numbers = find(sym.node, "pin_numbers")
        self.stubbed = numbers is None and not part.lib_id.startswith("power:")
        self.labels, self.no_connects = [], []
        seen = {}
        for p in pins:
            px, py = self.turn(p.x, p.y)
            net = nets[p.number]
            if net is None:
                self.no_connects.append((p, px, py))
            elif (px, py) in seen:  # stacked pins (several GND on one spot) share one label
                if seen[px, py] != net:
                    raise ValueError(f"{part.ref}: stacked pins on two nets at pin {p.number}")
            else:
                seen[px, py] = net
                a = 0 if part.lib_id.startswith("power:") else label_angle(p.rot + self.rot)
                self.labels.append((net, p, px, py, a))
        self.label_boxes = [label_box(net, *self.anchor(x, y, a), a) for net, _, x, y, a in self.labels]
        self.stub_boxes = []
        if self.stubbed:
            for _, _, x, y, a in self.labels:
                ex, ey = stub_end(x, y, a)
                self.stub_boxes.append((min(x, ex), min(y, ey), max(x, ex), max(y, ey)))
        self.fields = self.place_fields()
        self.extent = union([self.body, *self.label_boxes, *(field_box(s, x, y, j) for s, x, y, j in self.fields)])

    def turn(self, x, y):
        """Symbol point (y up) to sheet offset (y down) with the symbol's rotation."""
        y = -y
        return (y, -x) if self.rot == 90 else (x, y)

    def anchor(self, x, y, angle):
        return stub_end(x, y, angle) if self.stubbed else (x, y)

    def visible(self):
        ref = self.part.ref
        return [ref, self.part.value] if not ref.startswith("#") else [self.part.value]

    def place_fields(self):
        """First spot where the reference and value clear the body and every label."""
        x0, y0, x1, y1 = self.body
        cx = snap((x0 + x1) / 2, FIELD_GRID)
        above, below = snap(y0 - 1.4, FIELD_GRID), snap(y1 + 1.4, FIELD_GRID)
        ext = union([self.body, *self.label_boxes])
        step = FIELD_GRID * 2
        vis = self.visible()
        stack = lambda x, y, j: [(s, x, y + i * step, j) for i, s in enumerate(vis)]
        apart = [(vis[0], cx, above, None)] + ([(vis[1], cx, below, None)] if len(vis) > 1 else [])
        tries = [
            apart,                                                 # over and under the body
            stack(x1 + 1.27, (y0 + y1) / 2 - step / 2, "left"),    # to its right
            stack(x0 - 1.27, (y0 + y1) / 2 - step / 2, "right"),   # to its left
            stack(x1 + 1.27, y0 - step * len(vis), "left"),        # top right corner
            stack(x0 - 1.27, y0 - step * len(vis), "right"),       # top left corner
            stack(cx, ext[1] - step * len(vis), None),             # clear of everything, above
        ]
        keep = [self.body, *self.label_boxes, *self.stub_boxes]
        for fields in tries:
            boxes = [field_box(s, x, y, j) for s, x, y, j in fields]
            if not any(hit(b, k) for b in boxes for k in keep) and not any(
                    hit(a, b) for i, a in enumerate(boxes) for b in boxes[i + 1:]):
                return fields
        raise ValueError(f"{self.part.ref}: nowhere to put the reference and value")


class SchWriter:
    def __init__(self, design, paper="A0"):
        self.d = design
        self.paper = paper
        self.root = stable_uuid(design.name, "root")
        self.items = []
        self.lib = {}
        self.boxes = []  # (sheet box, what is there, its part, is it drawing not text), for check()

    def note_box(self, box, what, owner=None, drawing=False):
        self.boxes.append((box, what, owner, drawing))

    def place_unit(self, g, x, y):
        part, unit = g.part, g.unit
        sym = part.sym
        self.lib.setdefault(part.lib_id, sym)
        u = stable_uuid(self.d.name, part.ref, unit)
        fields = {s: f for s, *f in g.fields}
        hidden = part.ref not in fields

        def shown(name, value):
            fx, fy, just = fields[value] if value in fields else (0, 0, None)
            # a field's angle turns with its symbol: 90 on a turned part reads level
            return prop(name, value, x + fx, y + fy, justify=just, angle=g.rot, hide=value not in fields)

        node = ["symbol", ["lib_id", Q(part.lib_id)], ["at", x, y, g.rot], ["unit", unit],
                ["exclude_from_sim", "no"], ["in_bom", "no" if part.lib_id.startswith(("power:", "Connector:TestPoint", "Jumper:SolderJumper")) else "yes"],
                ["on_board", "yes"], ["dnp", "yes" if part.dnp else "no"], ["uuid", Q(u)],
                prop("Reference", part.ref, x, y, hide=True) if hidden else shown("Reference", part.ref),
                shown("Value", part.value),
                prop("Footprint", part.footprint, x, y, hide=True),
                prop("Datasheet", "", x, y, hide=True),
                prop("Description", "", x, y, hide=True)]
        if part.note:
            node.append(prop("Note", part.note, x, y, hide=True))
        if part.lcsc:
            node.append(prop("LCSC", part.lcsc, x, y, hide=True))
        if part.mpn:
            node.append(prop("MPN", part.mpn, x, y, hide=True))
        for p in sym.unit_pins(unit):
            node.append(["pin", Q(p.number), ["uuid", Q(stable_uuid(u, p.number))]])
        node.append(["instances", ["project", Q(self.d.name),
                                   ["path", Q("/" + self.root), ["reference", Q(part.ref)], ["unit", unit]]]])
        self.items.append(node)
        for p, px, py in g.no_connects:
            self.items.append(["no_connect", ["at", x + px, y + py], ["uuid", Q(stable_uuid(u, p.number, "nc"))]])
        for net, p, px, py, a in g.labels:
            just = "left bottom" if a in (0, 90) else "right bottom"
            lx, ly = g.anchor(px, py, a)
            if g.stubbed:
                self.items.append(["wire", ["pts", ["xy", x + px, y + py], ["xy", x + lx, y + ly]],
                                   ["stroke", ["width", 0], ["type", "default"]],
                                   ["uuid", Q(stable_uuid(u, p.number, "stub"))]])
            self.items.append(["label", Q(net), ["at", x + lx, y + ly, a], ["fields_autoplaced", "yes"],
                               ["effects", font(), ["justify", *just.split()]],
                               ["uuid", Q(stable_uuid(u, p.number, "lbl"))]])
            self.note_box(label_box(net, x + lx, y + ly, a), f"{part.ref} label {net}", part.ref)
        self.note_box(shift(g.body, x, y), f"{part.ref} body", part.ref, True)
        for box in g.stub_boxes:
            self.note_box(shift(box, x, y), f"{part.ref} stub", part.ref, True)
        for s, fx, fy, just in g.fields:
            self.note_box(field_box(s, x + fx, y + fy, just), f"{part.ref} field {s}", part.ref)

    def text(self, s, x, y, size=1.27, bold=False):
        self.items.append(["text", Q(s), ["exclude_from_sim", "no"], ["at", x, y, 0],
                           ["effects", font(size, bold), ["justify", "left", "top"]],
                           ["uuid", Q(stable_uuid(self.d.name, "text", s, x, y))]])
        self.note_box((x, y, x + text_w(s, size), y + size * 1.4), f"text {s[:30]}")

    def rows(self, block, max_w):
        """The block's parts as rows: multi-pin parts first, then two-pin parts on a column grid."""
        units = [Unit(p, u) for p in block.parts for u in sorted(p.sym.units)]
        pair = [g for g in units if len(g.part.sym.unit_pins(g.unit)) <= 2 and len(g.part.sym.units) == 1]
        pair.sort(key=lambda g: len(g.part.sym.unit_pins(g.unit)) == 1)  # test points last
        big = [g for g in units if g not in pair]
        rows, cur, cur_w = [], [], 0
        for g in big:
            w = g.extent[2] - g.extent[0] + PART_GAP
            if cur and cur_w + w > max_w:
                rows.append(cur)
                cur, cur_w = [], 0
            cur.append((g, w))
            cur_w += w
        if cur:
            rows.append(cur)
        if pair:
            cell = max(g.extent[2] - g.extent[0] for g in pair) + PART_GAP
            per = max(1, int(max_w // cell))
            for i in range(0, len(pair), per):
                rows.append([(g, cell) for g in pair[i:i + per]])
        return rows

    def layout(self, sheet_w=1180, margin=20, y_top=40):
        """Blocks flow left to right, then wrap; each stacks its rows of parts inside its own box."""
        x, row_h = margin, 0
        for b in self.d.blocks:
            rows = self.rows(b, getattr(b, "width", 260))
            bw = max(sum(w for _, w in r) for r in rows)
            bw = max(bw, text_w(b.title, 3), max((text_w(n) for n in b.notes), default=0))
            if x + bw > sheet_w:
                x = margin
                y_top += row_h + BLOCK_GAP
                row_h = 0
            self.text(b.title, x, y_top, size=3, bold=True)
            ny = y_top + TITLE_H
            for note in b.notes:
                self.text(note, x, ny)
                ny += NOTE_H
            yy = ny + 2
            for r in rows:
                xx = x
                for g, w in r:
                    e = g.extent
                    self.place_unit(g, snap(xx - e[0] + GRID), snap(yy - e[1] + GRID))
                    xx += w
                yy += max(g.extent[3] - g.extent[1] for g, _ in r) + 2 * GRID
            row_h = max(row_h, yy - y_top)
            x += bw + BLOCK_GAP

    def power_flags(self, nets, x=20.32, y=30.48):
        """The PWR_FLAGs, one row above the blocks."""
        from design import Part
        for i, net in enumerate(sorted(nets)):
            p = Part(f"#FLG{i + 1:02d}", "power:PWR_FLAG", "PWR_FLAG", "", {"1": net})
            self.place_unit(Unit(p, 1), x + i * 30.48, y)

    def check(self):
        """What is wrong with the sheet: text that collides, or anything off the paper."""
        problems = []
        boxes = sorted(self.boxes, key=lambda b: b[0][0])
        for i, (a, what, owner, drawn) in enumerate(boxes):
            for b, other, owner2, drawn2 in boxes[i + 1:]:
                if b[0] > a[2] + CLEAR:
                    break
                # a part's own body and stubs meet its labels by design; two drawings may touch
                if hit(a, b, 0) and not (drawn and drawn2) and not (owner == owner2 and (drawn or drawn2)):
                    problems.append(f"{what} overlaps {other}")
        w, h = PAPER[self.paper]
        e = union(b[0] for b in self.boxes)
        if e[2] > w - 15 or e[3] > h - TITLE_BLOCK_H:
            problems.append(f"sheet content reaches {e[2]:.0f} x {e[3]:.0f} mm on {self.paper}")
        return problems

    def write(self, path, title, rev, date, comments=()):
        tb = ["title_block", ["title", Q(title)], ["date", Q(date)], ["rev", Q(rev)],
              ["company", Q("OCTAVE")]]
        for i, c in enumerate(comments):
            tb.append(["comment", i + 1, Q(c)])
        doc = ["kicad_sch", ["version", "20250114"], ["generator", Q("octave_gen")],
               ["generator_version", Q("9.0")], ["uuid", Q(self.root)], ["paper", Q(self.paper)], tb,
               ["lib_symbols", *[s.node for s in self.lib.values()]],
               *self.items,
               ["sheet_instances", ["path", Q("/"), ["page", Q("1")]]],
               ["embedded_fonts", "no"]]
        with open(path, "w") as f:
            f.write(dump(doc) + "\n")
