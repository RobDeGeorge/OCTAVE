"""The schematic as a hierarchy: a top page with a block diagram and one page per group of blocks.

Nets are named the same on every page and on the board (no "/" prefix): supply rails by power
symbols, every other net by global labels (a net's name is its global label's). The board's
footprints point at their symbol through the page (`sheet_of`), so KiCad's schematic parity check
pairs them.

Pages lay their blocks out with the column packer from schgen.py (`SheetWriter`); a page can
instead be drawn by hand (`drawn.py`), with wires and parts placed like a reference schematic.
"""

import math

from design import stable_uuid
from kilib import Symbol
from schgen import (CLEAR, GRID, PAPER, TEXT_H, SchWriter, Unit, field_box, font, hit, label_box, prop, shift,
                    snap, text_w, union)
from sexpr import Q, dump

PAPER.update({"A4": (297, 210), "A3": (420, 297), "A2": (594, 420)})

# (page name, file suffix, blocks on it, what it shows)
SHEETS = [
    ("Power", "power", ["power"],
     "Battery input protection, 5 V buck, 5 V OR, 3.3 V LDO"),
    ("Car plug and CAN", "car", ["car", "can"],
     "J1962 plug; HS-CAN and MS-CAN transceivers"),
    ("J1850", "j1850", ["j1850"],
     "SAE J1850 VPW / PWM front end and its supply"),
    ("K-line and DLC switch", "iso", ["iso", "dlcsw"],
     "ISO 9141 / 14230 K- and L-line; DLC_SW, the switched battery for them"),
    ("STM32", "mcu", ["mcu", "flash", "ui"],
     "STM32G474: vehicle-side MCU, QSPI flash, SWD, buttons, LEDs, CONFIG switch"),
    ("ESP32", "esp32", ["esp32", "espusb", "sd"],
     "ESP32 radio bridge, its own USB-C / CH340C, microSD"),
    ("USB and expansion", "usb", ["usb", "exp", "sense"],
     "STM32 USB-C, Qwiic, expansion header; accelerometer, secure element, panic button"),
]

# nets drawn with a power symbol rather than a label
RAILS = {"GND", "3V3", "5V", "5V_BUCK", "3V3_ESP", "VDDA", "VBAT_IN", "VBAT_F", "VBAT_P", "VBUS", "VBUS2",
         "DLC_SW", "CMP_VCC", "VJ1850"}
POWER_FLAGS = {"GND", "VBAT_IN", "VBUS", "VBUS2", "VBAT_F", "VBAT_P", "5V", "VDDA", "DLC_SW", "CMP_VCC", "5V_BUCK"}

# away direction of a label / power symbol from its pin, per label angle (y down)
AWAY = {0: (1, 0), 180: (-1, 0), 90: (0, -1), 270: (0, 1)}


def sheet_uuid(design, name):
    return stable_uuid(design.name, "sheet", name)


def sheet_file(design, suffix):
    return f"{design.name}-{suffix}.kicad_sch"


def sheet_of(design):
    """{part ref: (sheet uuid, sheet name, sheet file)} for the board's footprints."""
    import drawn
    out = {}
    blocks = {b.name: b for b in design.blocks}
    files = {name: sheet_file(design, suffix) for name, suffix, _, _ in SHEETS}
    for name, suffix, names, _ in SHEETS:
        for bn in names:
            for p in blocks[bn].parts:
                out[p.ref] = (sheet_uuid(design, name), name, sheet_file(design, suffix))
    for ref, name in drawn.PAGE_OF.items():
        out[ref] = (sheet_uuid(design, name), name, files[name])
    return out


def _rot_vec(x, y, rot):
    """Turn a sheet vector (y down) by rot degrees counter-clockwise as seen on screen."""
    r = math.radians(rot)
    return (round(x * math.cos(r) + y * math.sin(r), 4), round(-x * math.sin(r) + y * math.cos(r), 4))


class PowerSymbols:
    """Power symbols shared by every page: GND points down, every other rail up (a bar)."""

    def __init__(self):
        self.n = self.flags = 0
        self.syms = {"GND": Symbol("power:GND"), "rail": Symbol("power:+3V3")}

    def sym(self, net):
        return self.syms["GND" if net == "GND" else "rail"]

    def next_ref(self):
        self.n += 1
        return f"#PWR{self.n:03d}"


class SheetWriter(SchWriter):
    """One page: the blocks' parts with power symbols on rails and global labels on other nets."""

    def __init__(self, design, name, blocks, root, power, paper="A3"):
        super().__init__(design, paper)
        self.name, self.blocks = name, blocks
        self.root_uuid = root
        self.uuid = sheet_uuid(design, name)
        self.power = power

    @property
    def path(self):
        return f"/{self.root_uuid}/{self.uuid}"

    # -- one power symbol or global label at the end of a pin -------------------------------
    def power_symbol(self, net, x, y, angle, key):
        dx, dy = AWAY[angle]
        sym = self.power.sym(net)
        self.lib.setdefault(sym.lib_id, sym)
        if net == "GND":
            rot = {(0, 1): 0, (1, 0): 90, (0, -1): 180, (-1, 0): 270}[(dx, dy)]
        else:
            rot = {(0, -1): 0, (-1, 0): 90, (0, 1): 180, (1, 0): 270}[(dx, dy)]
        # its name just beyond the symbol, out along the pin, level
        w = text_w(net)
        ox, oy = (dx * (4.2 + w / 2), 0) if dx else (0, dy * 5.0)
        ref = self.power.next_ref()
        u = stable_uuid(self.d.name, self.name, "pwr", key)
        self.items.append(["symbol", ["lib_id", Q(sym.lib_id)], ["at", x, y, rot], ["unit", 1],
                           ["exclude_from_sim", "no"], ["in_bom", "no"], ["on_board", "yes"], ["dnp", "no"],
                           ["uuid", Q(u)],
                           prop("Reference", ref, x, y, hide=True),
                           prop("Value", net, x + ox, y + oy, angle=0),
                           prop("Footprint", "", x, y, hide=True),
                           prop("Datasheet", "", x, y, hide=True),
                           prop("Description", "", x, y, hide=True),
                           ["pin", Q("1"), ["uuid", Q(stable_uuid(u, "1"))]],
                           ["instances", ["project", Q(self.d.name),
                                          ["path", Q(self.path), ["reference", Q(ref)], ["unit", 1]]]]])
        # what it takes up: the symbol out along its pin, and its name
        ex, ey = x + dx * 3.2, y + dy * 3.2     # out from the pin end only, 1.4 mm either side
        px, py = 1.4 * abs(dy), 1.4 * abs(dx)
        self.note_box((min(x, ex) - px, min(y, ey) - py, max(x, ex) + px, max(y, ey) + py),
                      f"power {net} {key}", key, True)
        tx, ty = x + ox, y + oy
        self.note_box((tx - w / 2, ty - TEXT_H / 2, tx + w / 2, ty + TEXT_H / 2), f"power name {net} {key}", key)

    def global_label(self, net, x, y, angle, key):
        just = "left" if angle in (0, 90) else "right"
        u = stable_uuid(self.d.name, self.name, "glbl", key)
        self.items.append(["global_label", Q(net), ["shape", "passive"], ["at", x, y, angle],
                           ["fields_autoplaced", "yes"], ["effects", font(), ["justify", just]],
                           ["uuid", Q(u)],
                           ["property", Q("Intersheetrefs"), Q("${INTERSHEET_REFS}"), ["at", x, y, 0],
                            ["effects", font(), ["hide", "yes"]]]])
        b = label_box(net, x, y, angle)
        dx, dy = AWAY[angle]           # the flag outline adds about 2 mm on its open end
        b = (b[0] - (2 if dx < 0 else 0), b[1] - (2 if dy < 0 else 0),
             b[2] + (2 if dx > 0 else 0), b[3] + (2 if dy > 0 else 0))
        self.note_box(b, f"global label {net} {key}", key.split(".")[0])

    # -- parts ------------------------------------------------------------------------------
    def place_unit(self, g, x, y):
        """A symbol from the column packer: as schgen's, but labels become power symbols /
        global labels and its instance sits on this page."""
        part, unit = g.part, g.unit
        sym = part.sym
        self.lib.setdefault(part.lib_id, sym)
        u = stable_uuid(self.d.name, part.ref, unit)
        fields = {s: f for s, *f in g.fields}
        hidden = part.ref not in fields

        def shown(name, value):
            fx, fy, just = fields[value] if value in fields else (0, 0, None)
            return prop(name, value, x + fx, y + fy, justify=just, angle=g.rot, hide=value not in fields)

        node = ["symbol", ["lib_id", Q(part.lib_id)], ["at", x, y, g.rot], ["unit", unit],
                ["exclude_from_sim", "no"],
                ["in_bom", "no" if part.lib_id.startswith(("power:", "Connector:TestPoint")) else "yes"],
                ["on_board", "yes"], ["dnp", "yes" if part.dnp else "no"], ["uuid", Q(u)],
                prop("Reference", part.ref, x, y, hide=True) if hidden else shown("Reference", part.ref),
                shown("Value", part.value),
                prop("Footprint", part.footprint, x, y, hide=True),
                prop("Datasheet", "", x, y, hide=True),
                prop("Description", "", x, y, hide=True)]
        for k, v in (("Note", part.note), ("LCSC", part.lcsc), ("MPN", part.mpn)):
            if v:
                node.append(prop(k, v, x, y, hide=True))
        for p in sym.unit_pins(unit):
            node.append(["pin", Q(p.number), ["uuid", Q(stable_uuid(u, p.number))]])
        node.append(["instances", ["project", Q(self.d.name),
                                   ["path", Q(self.path), ["reference", Q(part.ref)], ["unit", unit]]]])
        self.items.append(node)
        for p, px, py in g.no_connects:
            self.items.append(["no_connect", ["at", x + px, y + py], ["uuid", Q(stable_uuid(u, p.number, "nc"))]])
        for net, p, px, py, a in g.labels:
            lx, ly = g.anchor(px, py, a)
            if g.stubbed:
                self.items.append(["wire", ["pts", ["xy", x + px, y + py], ["xy", x + lx, y + ly]],
                                   ["stroke", ["width", 0], ["type", "default"]],
                                   ["uuid", Q(stable_uuid(u, p.number, "stub"))]])
            key = f"{part.ref}.{p.number}"
            # power symbols on two-pin parts (and the power flags); a rail on a connector or IC pin
            # gets a global label with the rail's name: its neighbours are only 2.54 mm away
            two_pin = len(part.sym.unit_pins(unit)) <= 2
            if net in RAILS and two_pin:
                self.power_symbol(net, x + lx, y + ly, a, key)
            else:
                self.global_label(net, x + lx, y + ly, a, key)
        self.note_box(shift(g.body, x, y), f"{part.ref} body", part.ref, True)
        for box in g.stub_boxes:
            self.note_box(shift(box, x, y), f"{part.ref} stub", part.ref, True)
        for s, fx, fy, just in g.fields:
            self.note_box(field_box(s, x + fx, y + fy, just), f"{part.ref} field {s}", part.ref)

    def layout(self, sheet_w, margin=20, y_top=35):
        """schgen's column packer over this page's blocks only. Labels are measured a little
        longer than schgen's: a global label's flag and a power symbol with its name stick out
        further than a plain label."""
        import schgen
        all_blocks, plain = self.d.blocks, schgen.label_box

        def padded(net, x, y, angle):
            b = plain(net, x, y, angle)
            dx, dy = AWAY[angle]
            pad = 5.0
            return (b[0] - (pad if dx < 0 else 0) - 2.0 * abs(dy), b[1] - (pad if dy < 0 else 0) - 2.0 * abs(dx),
                    b[2] + (pad if dx > 0 else 0) + 2.0 * abs(dy), b[3] + (pad if dy > 0 else 0) + 2.0 * abs(dx))
        self.d.blocks, schgen.label_box = self.blocks, padded
        try:
            super().layout(sheet_w=sheet_w, margin=margin, y_top=y_top)
        finally:
            self.d.blocks, schgen.label_box = all_blocks, plain

    def power_flags(self, nets, x=20.32, y=22.86):
        """One PWR_FLAG per rail, each on a short wire down to the rail's power symbol."""
        from design import Part
        for i, net in enumerate(sorted(nets)):
            self.power.flags += 1
            p = Part(f"#FLG{self.power.flags:02d}", "power:PWR_FLAG", "PWR_FLAG", "", {"1": net})
            g = Unit(p, 1)
            g.labels = []                      # no label: the wire below joins it to the symbol
            fx = x + i * 22.86
            self.place_unit(g, fx, y)
            self.items.append(["wire", ["pts", ["xy", fx, y], ["xy", fx, y + 5.08]],
                               ["stroke", ["width", 0], ["type", "default"]],
                               ["uuid", Q(stable_uuid(self.d.name, self.name, "flagwire", net))]])
            self.power_symbol(net, fx, y + 5.08, 270, f"{p.ref}.1")

    def extent(self):
        return union(b[0] for b in self.boxes)

    def write(self, path, title, rev, date, comments=()):
        tb = ["title_block", ["title", Q(f"{title}: {self.name}")], ["date", Q(date)], ["rev", Q(rev)],
              ["company", Q("OCTAVE")]]
        for i, c in enumerate(comments):
            tb.append(["comment", i + 1, Q(c)])
        doc = ["kicad_sch", ["version", "20250114"], ["generator", Q("octave_gen")],
               ["generator_version", Q("9.0")], ["uuid", Q(self.uuid)], ["paper", Q(self.paper)], tb,
               ["lib_symbols", *[s.node for s in self.lib.values()]],
               *self.items,
               ["embedded_fonts", "no"]]
        with open(path, "w") as f:
            f.write(dump(doc) + "\n")


def write_root(design, path, pages, title, rev, date, comments=()):
    """The top page: one sheet symbol per page in a block diagram, signal flowing left to right."""
    root = stable_uuid(design.name, "root")
    items = []
    # block-diagram positions (mm, top-left corner) on A4 landscape
    pos = {"Power": (25, 40), "Car plug and CAN": (25, 80), "J1850": (25, 115),
           "K-line and DLC switch": (25, 150), "STM32": (115, 80), "ESP32": (200, 80),
           "USB and expansion": (115, 130)}
    W, H = 65, 24
    for n, (name, suffix, _, what) in enumerate(SHEETS):
        x, y = pos[name]
        u = sheet_uuid(design, name)
        items.append(["sheet", ["at", x, y], ["size", W, H], ["exclude_from_sim", "no"], ["in_bom", "yes"],
                      ["on_board", "yes"], ["dnp", "no"], ["fields_autoplaced", "yes"],
                      ["stroke", ["width", 0.3], ["type", "solid"]], ["fill", ["color", 0, 0, 0, 0.0]],
                      ["uuid", Q(u)],
                      ["property", Q("Sheetname"), Q(name), ["at", x, y - 0.7, 0],
                       ["effects", font(2, True), ["justify", "left", "bottom"]]],
                      ["property", Q("Sheetfile"), Q(sheet_file(design, suffix)), ["at", x, y + H + 0.6, 0],
                       ["effects", font(), ["justify", "left", "top"], ["hide", "yes"]]],
                      ["instances", ["project", Q(design.name), ["path", Q(f"/{root}"), ["page", Q(str(n + 2))]]]]])
        # what is on it, wrapped inside the box
        words, lines, cur = what.split(), [], ""
        for wd in words:
            if text_w(cur + " " + wd) > W - 6 and cur:
                lines.append(cur)
                cur = wd
            else:
                cur = (cur + " " + wd).strip()
        lines.append(cur)
        for i, line in enumerate(lines):
            items.append(["text", Q(line), ["exclude_from_sim", "no"], ["at", x + 3, y + 5 + i * 3.5, 0],
                          ["effects", font(), ["justify", "left", "top"]],
                          ["uuid", Q(stable_uuid(design.name, "roottext", name, i))]])

    def arrow(pts, label, key):
        items.append(["polyline", ["pts", *[["xy", px, py] for px, py in pts]],
                      ["stroke", ["width", 0.4], ["type", "default"]], ["fill", ["type", "none"]],
                      ["uuid", Q(stable_uuid(design.name, "arrow", key))]])
        (ax, ay), (bx, by) = pts[-2], pts[-1]
        d = math.hypot(bx - ax, by - ay) or 1
        ux, uy = (bx - ax) / d, (by - ay) / d
        head = [(bx - 3 * ux + 1.5 * uy, by - 3 * uy - 1.5 * ux), (bx, by), (bx - 3 * ux - 1.5 * uy, by - 3 * uy + 1.5 * ux)]
        items.append(["polyline", ["pts", *[["xy", px, py] for px, py in head]],
                      ["stroke", ["width", 0.4], ["type", "default"]], ["fill", ["type", "none"]],
                      ["uuid", Q(stable_uuid(design.name, "arrowhead", key))]])
        mx, my = (pts[0][0] + pts[-1][0]) / 2, (pts[0][1] + pts[-1][1]) / 2
        items.append(["text", Q(label), ["exclude_from_sim", "no"], ["at", mx, my - 1.5, 0],
                      ["effects", font(), ["justify", "bottom"]],
                      ["uuid", Q(stable_uuid(design.name, "arrowtext", key))]])

    arrow([(57, 80), (57, 68)], "", "plug-power")
    items.append(["text", Q("VBAT (pin 16)"), ["exclude_from_sim", "no"], ["at", 59, 75, 0],
                  ["effects", font(), ["justify", "left"]], ["uuid", Q(stable_uuid(design.name, "t", "vbat"))]])
    arrow([(90, 54), (102, 54), (102, 88), (115, 88)], "5V / 3V3", "power-mcu")
    arrow([(90, 94), (115, 94)], "CAN", "can-mcu")
    arrow([(90, 129), (102, 129), (102, 100), (115, 100)], "J1850", "j1850-mcu")
    arrow([(90, 164), (106, 164), (106, 104), (115, 104)], "K / L", "iso-mcu")
    arrow([(180, 94), (200, 94)], "UART", "mcu-esp")
    arrow([(147, 130), (147, 108)], "USB, I2C", "usb-mcu")
    items.append(["text", Q(title), ["exclude_from_sim", "no"], ["at", 25, 25, 0],
                  ["effects", font(4, True), ["justify", "left", "bottom"]],
                  ["uuid", Q(stable_uuid(design.name, "roottitle"))]])
    items.append(["text", Q("Every net has the same name on every page and on the board: supply rails by "
                            "power symbol, other nets by global label."),
                  ["exclude_from_sim", "no"], ["at", 25, 30, 0], ["effects", font(), ["justify", "left", "top"]],
                  ["uuid", Q(stable_uuid(design.name, "rootnote"))]])

    tb = ["title_block", ["title", Q(title)], ["date", Q(date)], ["rev", Q(rev)], ["company", Q("OCTAVE")]]
    for i, c in enumerate(comments):
        tb.append(["comment", i + 1, Q(c)])
    inst = [["path", Q("/"), ["page", Q("1")]]]
    for n, (name, *_r) in enumerate(SHEETS):
        inst.append(["path", Q(f"/{sheet_uuid(design, name)}"), ["page", Q(str(n + 2))]])
    doc = ["kicad_sch", ["version", "20250114"], ["generator", Q("octave_gen")], ["generator_version", Q("9.0")],
           ["uuid", Q(root)], ["paper", Q("A4")], tb, ["lib_symbols"], *items,
           ["sheet_instances", *inst], ["embedded_fonts", "no"]]
    with open(path, "w") as f:
        f.write(dump(doc) + "\n")
    return root


def write_all(design, proj_dir, title, rev, date, comments=()):
    """Every page + the top page. Returns the problems found (text collisions, overflow)."""
    import os
    blocks = {b.name: b for b in design.blocks}
    missing = set(blocks) - {bn for _, _, names, _ in SHEETS for bn in names}
    if missing:
        return [f"blocks on no page: {sorted(missing)}"]
    root = stable_uuid(design.name, "root")
    power = PowerSymbols()
    problems = []
    # each rail's power flag goes on the first page that has pins on it (a flag on a page where its
    # rail has no pins is a pin on its own as far as KiCad's ERC can see)
    flags_on, done = {}, set()
    for name, _, names, _ in SHEETS:
        used = {n for bn in names for p in blocks[bn].parts for n in p.pin_net().values() if n}
        flags_on[name] = sorted((POWER_FLAGS & used) - done)
        done |= set(flags_on[name])
    if POWER_FLAGS - done:
        return [f"power flags with no page: {sorted(POWER_FLAGS - done)}"]
    import drawn
    design_parts = {p.ref: p for p in design.parts}
    drawn_units, drawn_pages_units = {}, set()
    for name, suffix, names, what in SHEETS:
        if name in drawn.PAGES:         # drawn by hand: parts placed, wires run (drawn.py)
            w = SheetWriter(design, name, [blocks[n] for n in names], root, power, "A3")
            if flags_on[name]:
                w.power_flags(flags_on[name], x=25.4 if name == "Power" else 271.78, y=223.52)
            D = drawn.Drawing(w, design)
            drawn.PAGES[name](D, design)
            for key in D.placed:
                if key in drawn_units:
                    problems.append(f"{name}: {key} drawn twice")
                drawn_units[key] = name
            own = {p.ref for n in names for p in blocks[n].parts}
            own = (own - {r for r, pg in drawn.PAGE_OF.items() if pg != name}) | \
                {r for r, pg in drawn.PAGE_OF.items() if pg == name}
            for r, _u in D.placed:
                if r not in own and r not in drawn.SPLIT:
                    problems.append(f"{name}: {r} is drawn here but its block is on another page (drawn.PAGE_OF)")
            want = {(r, u) for r in own for u in design_parts[r].sym.units}
            drawn_pages_units |= want
            problems += [f"{name}: {s}" for s in D.finish()]
            problems += [f"{name}: {s}" for s in w.check()]
            w.write(os.path.join(proj_dir, sheet_file(design, suffix)), title, rev, date, comments)
            continue
        for paper in ("A3", "A2", "A1"):
            power.n0, power.f0 = power.n, power.flags
            w = SheetWriter(design, name, [blocks[n] for n in names], root, power, paper)
            pw, ph = PAPER[paper]
            if len(names) > 1:
                w.text(f"{name}: {what}", 20, 14, size=3, bold=True)
            if flags_on[name]:
                w.power_flags(flags_on[name], y=25.4)
            w.layout(sheet_w=pw - 20, y_top=40 if flags_on[name] else 25)
            p = w.check()
            if not any("sheet content reaches" in s for s in p):
                break
            power.n, power.flags = power.n0, power.f0
        problems += [f"{name}: {s}" for s in p]
        w.write(os.path.join(proj_dir, sheet_file(design, suffix)), title, rev, date, comments)
    missing = sorted(drawn_pages_units - set(drawn_units))
    if missing:
        problems.append(f"units not drawn on any page: {missing}")
    write_root(design, os.path.join(proj_dir, design.name + ".kicad_sch"), None, title, rev, date, comments)
    return problems
