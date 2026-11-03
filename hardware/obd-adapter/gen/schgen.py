"""Write a KiCad schematic from a Design.

Every connected pin gets a local net label at its end, unconnected pins get
a no-connect flag. Blocks are laid out as titled columns of parts, so the
sheet reads block by block. Power rails get one PWR_FLAG each.
"""

from design import stable_uuid
from kilib import Symbol
from sexpr import Q, dump

GRID = 2.54
CHAR_W = 1.0  # rough width of a 1.27 mm label character


def snap(v):
    return round(v / GRID) * GRID


def font(size=1.27, bold=False):
    f = ["font", ["size", size, size]]
    if bold:
        f.append(["bold", "yes"])
    return f


def prop(name, value, x, y, hide=False, justify=None):
    eff = ["effects", font()]
    if justify:
        eff.append(["justify", justify])
    p = ["property", Q(name), Q(value), ["at", x, y, 0]]
    if hide:
        p.append(["hide", "yes"])
    p.append(eff)
    return p


def label_angle(pin_rot):
    # the pin points from its end into the body; the label text goes the other way
    return {0: 180, 90: 270, 180: 0, 270: 90}[int(pin_rot) % 360]


class SchWriter:
    def __init__(self, design, paper="A0"):
        self.d = design
        self.paper = paper
        self.root = stable_uuid(design.name, "root")
        self.items = []
        self.lib = {}
        self.placed = {}  # (ref, unit) -> (x, y)

    def _sym(self, lib_id):
        if lib_id not in self.lib:
            self.lib[lib_id] = Symbol(lib_id)
        return self.lib[lib_id]

    def place_symbol(self, part, unit, x, y):
        sym = part.sym
        self.lib.setdefault(part.lib_id, sym)
        u = stable_uuid(self.d.name, part.ref, unit)
        x0, y0, x1, y1 = sym.bbox(unit)
        node = ["symbol", ["lib_id", Q(part.lib_id)], ["at", x, y, 0], ["unit", unit],
                ["exclude_from_sim", "no"], ["in_bom", "no" if part.lib_id.startswith(("power:", "Connector:TestPoint", "Jumper:SolderJumper")) else "yes"],
                ["on_board", "yes"], ["dnp", "yes" if part.dnp else "no"], ["uuid", Q(u)],
                prop("Reference", part.ref, x, snap(y - y1 - 2.0), justify=None),
                prop("Value", part.value, x, snap(y - y0 + 2.0)),
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
        nets = part.pin_net()
        for p in sym.unit_pins(unit):
            px, py = x + p.x, y - p.y
            net = nets[p.number]
            if net is None:
                self.items.append(["no_connect", ["at", px, py], ["uuid", Q(stable_uuid(u, p.number, "nc"))]])
            elif part.lib_id.startswith("power:"):
                pass
            else:
                a = label_angle(p.rot)
                just = "left bottom" if a in (0, 90) else "right bottom"
                self.items.append(["label", Q(net), ["at", px, py, a], ["fields_autoplaced", "yes"],
                                   ["effects", font(), ["justify", *just.split()]],
                                   ["uuid", Q(stable_uuid(u, p.number, "lbl"))]])

    def text(self, s, x, y, size=1.27, bold=False):
        self.items.append(["text", Q(s), ["exclude_from_sim", "no"], ["at", x, y, 0],
                           ["effects", font(size, bold), ["justify", "left", "top"]],
                           ["uuid", Q(stable_uuid(self.d.name, "text", s, x, y))]])

    def label_room(self, part, unit, side):
        nets = part.pin_net()
        n = 0
        for p in part.sym.unit_pins(unit):
            net = nets[p.number]
            if not net:
                continue
            a = label_angle(p.rot)
            if (side == "left" and a == 180) or (side == "right" and a == 0):
                n = max(n, len(net))
        return n * CHAR_W + 2

    def layout(self, col_width=None, sheet_w=1180, margin=20):
        """Blocks flow left to right in columns; parts stack inside each block."""
        x, y_top = margin, margin
        row_h = 0
        for b in self.d.blocks:
            units = [(p, u) for p in b.parts for u in sorted(p.sym.units)]
            # pack parts into rows inside the block
            bw = 0
            rows, cur, cur_w = [], [], 0
            maxw = b_width = getattr(b, "width", 260)
            for p, u in units:
                x0, y0, x1, y1 = p.sym.bbox(u)
                w = (x1 - x0) + self.label_room(p, u, "left") + self.label_room(p, u, "right") + 6
                h = (y1 - y0) + 12
                if cur and cur_w + w > maxw:
                    rows.append(cur)
                    cur, cur_w = [], 0
                cur.append((p, u, w, h))
                cur_w += w
            if cur:
                rows.append(cur)
            bh = sum(max(h for *_, h in r) for r in rows) + 14
            bw = max(sum(w for *_, w, _ in r) for r in rows) if rows else 40
            bw = max(bw, len(b.title) * 2.6, max((len(n) for n in b.notes), default=0) * 1.05)
            if x + bw > sheet_w:
                x = margin
                y_top += row_h + 10
                row_h = 0
            self.text(b.title, x, y_top, size=3, bold=True)
            ny = y_top + 6
            for i, note in enumerate(b.notes):
                self.text(note, x, ny)
                ny += 3
            yy = ny + 4
            for r in rows:
                xx = x
                rh = max(h for *_, h in r)
                for p, u, w, h in r:
                    x0, y0, x1, y1 = p.sym.bbox(u)
                    sx = snap(xx + self.label_room(p, u, "left") + 3 - x0)
                    sy = snap(yy + 6 + y1)
                    self.place_symbol(p, u, sx, sy)
                    xx += w
                yy += rh
            bh = yy - y_top
            row_h = max(row_h, bh)
            x += bw + 15

    def power_flags(self, nets):
        from design import Part
        x, y = 30.48, 10.16
        for i, net in enumerate(sorted(nets)):
            ref = f"#FLG{i + 1:02d}"
            p = Part(ref, "power:PWR_FLAG", "PWR_FLAG", "", {"1": net})
            self.place_symbol(p, 1, x + i * 25.4, y)
            # PWR_FLAG has a single pin at its origin; label it
            self.items.append(["label", Q(net), ["at", x + i * 25.4, y, 0], ["fields_autoplaced", "yes"],
                               ["effects", font(), ["justify", "left", "bottom"]],
                               ["uuid", Q(stable_uuid(ref, "lbl"))]])

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
