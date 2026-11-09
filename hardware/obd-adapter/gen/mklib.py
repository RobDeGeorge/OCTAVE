"""Generate the project symbol library (lib/OCTAVE.kicad_sym) for parts KiCad doesn't ship.

Each symbol is a plain box: pins down the left and right sides, 2.54 mm pitch.
Pin types follow the datasheets so ERC can catch driver conflicts.
"""

import os

from sexpr import Q, dump

OUT = os.path.join(os.path.dirname(__file__), "..", "lib", "OCTAVE.kicad_sym")
PIN_LEN = 2.54


def _font():
    return ["effects", ["font", ["size", 1.27, 1.27]]]


def _prop(name, val, x, y, hide=False):
    p = ["property", Q(name), Q(val), ["at", x, y, 0]]
    if hide:
        p.append(["hide", "yes"])
    p.append(_font())
    return p


def ic(name, left, right, ref="U", footprint="", desc="", width=15.24):
    """left/right: lists of (number, name, type) or None for a gap."""
    rows = max(len(left), len(right))
    h = rows * 2.54
    top = round((h / 2) / 2.54) * 2.54
    w2 = width / 2
    body = ["symbol", Q(f"{name}_0_1"),
            ["rectangle", ["start", -w2, top + 1.27], ["end", w2, top - h - 1.27 + 2.54],
             ["stroke", ["width", 0.254], ["type", "default"]], ["fill", ["type", "background"]]]]
    pins = ["symbol", Q(f"{name}_1_1")]
    for side, lst in (("L", left), ("R", right)):
        for i, pin in enumerate(lst):
            if pin is None:
                continue
            num, pname, ptype = pin
            y = top - i * 2.54
            x = -w2 - PIN_LEN if side == "L" else w2 + PIN_LEN
            rot = 0 if side == "L" else 180
            pins.append(["pin", ptype, "line", ["at", x, y, rot], ["length", PIN_LEN],
                         ["name", Q(pname), _font()], ["number", Q(str(num)), _font()]])
    return ["symbol", Q(name), ["pin_names", ["offset", 1.016]], ["exclude_from_sim", "no"],
            ["in_bom", "yes"], ["on_board", "yes"],
            _prop("Reference", ref, 0, top + 3.81), _prop("Value", name, 0, top - h - 1.27),
            _prop("Footprint", footprint, 0, 0, True), _prop("Datasheet", "", 0, 0, True),
            _prop("Description", desc, 0, 0, True), body, pins, ["embedded_fonts", "no"]]


def write(symbols):
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    doc = ["kicad_symbol_lib", ["version", "20241209"], ["generator", Q("octave_gen")],
           ["generator_version", Q("9.0")], *symbols]
    with open(OUT, "w") as f:
        f.write(dump(doc) + "\n")


# ---------------------------------------------------------------- footprints (lib/OCTAVE.pretty)
FP_DIR = os.path.join(os.path.dirname(__file__), "..", "lib", "OCTAVE.pretty")


def _line(a, b, layer, w):
    return ["fp_line", ["start", *a], ["end", *b], ["stroke", ["width", w], ["type", "solid"]], ["layer", Q(layer)]]


def _text(kind, val, x, y, layer, hide=False, size=1.0):
    p = ["property", Q(kind), Q(val), ["at", x, y, 0], ["layer", Q(layer)]]
    if hide:
        p.append(["hide", "yes"])
    p.append(["effects", ["font", ["size", size, size], ["thickness", round(size * 0.15, 3)]]])
    return p


def _poly(pts, layer, w):
    return [_line(pts[i], pts[(i + 1) % len(pts)], layer, w) for i in range(len(pts))]


def _shroud(grow=0.0):
    """J1962 mating shroud outline (Jiatel CR-PG001): 40.26 long on the pins 1-8 side (y < 0), 31.8 on
    the 9-16 side, 17.8 across, straight ends 4.2 mm before they slope in. Same shape as gen/mkplug3d.py."""
    hw, hn, hy, ys = 40.26 / 2 + grow, 31.8 / 2 + grow, 17.8 / 2 + grow, -4.7
    return [(-hw, -hy), (hw, -hy), (hw, ys), (hn, hy), (-hn, hy), (-hw, ys)]


PLUG_MODEL = "${KIPRJMOD}/lib/3dmodels/J1962_Male_Straight_Jiatel_CR-PG001.step"
EDGE_MODEL = "${KIPRJMOD}/lib/3dmodels/J1962_Edge_Joint_Assembly.step"
ADAPTER = (40.0, 18.0, 1.6)       # adapter board w x h x thickness (gen/mkplug3d.py has the same)
SLOT = (36.0, 1.8)                # its slot for the main board's tab: length x width
TAB = 33.8                        # the main board's tab: width (it is ADAPTER[2] deep)


def _model(path):
    return ["model", Q(path), ["offset", ["xyz", 0, 0, 0]], ["scale", ["xyz", 1, 1, 1]], ["rotate", ["xyz", 0, 0, 0]]]


def _font(size=1.0, mirror=False):
    e = ["effects", ["font", ["size", size, size], ["thickness", round(size * 0.15, 3)]]]
    if mirror:
        e.append(["justify", "mirror"])
    return e


def j1962_plug_adapter(pitch=4.0, row=8.6, slot=(2.2, 1.2), pad=(3.1, 2.0), joint_w=2.5):
    """The J1962 plug on its adapter board (board obd-adapter-plug), seen from the plug side.

    Plug: Shenzhen Jiatel OBD-II-16M-12V, CR-PG001 rev A (jiatelcn.com "OBD-II 16 Pin Straight Male
    Connector", drawing OBDII-Male-16pin.pdf). Contacts 2 x 8, 4.00 mm pitch, rows 8.60 mm apart;
    0.8 x 1.8 mm blades whose tails stand 4.0 mm out of the back, so oval slots 2.2 x 1.2 (blade
    diagonal 1.97, drawing tolerance +-0.3) with 3.1 x 2.0 pads. The vehicle's socket, seen from the
    front, has 1-8 on the wide top row left to right and 9-16 below; the plug is its mirror image, so
    on this face pin 1 is top right and pin 9 under it.

    The main board's tab goes through the slot between the rows (Edge.Cuts, part of this footprint)
    and is soldered to the joint pads on the back (B.Cu): one per pin, from 0.25 mm off the slot edge
    out over the pin's own pad, so the tail's joint and the fillet to the main board are one."""
    name = "J1962_Plug_Adapter"
    items = []
    sl, sw = SLOT
    y0, y1 = sw / 2 + 0.25, row / 2 + pad[1] / 2
    for k in range(1, 9):
        x = round((4.5 - k) * pitch, 3)      # pin 1 at +14, pin 8 at -14
        for num, y, sgn in ((k, -row / 2, -1), (k + 8, row / 2, 1)):
            shape = "roundrect" if num == 1 else "oval"
            pd = ["pad", Q(str(num)), "thru_hole", shape, ["at", x, y], ["size", *pad],
                  ["drill", "oval", *slot], ["layers", Q("*.Cu"), Q("*.Mask")], ["remove_unused_layers", "no"]]
            if num == 1:
                pd.append(["roundrect_rratio", 0.15])
            items.append(pd)
            items.append(["pad", Q(str(num)), "smd", "rect", ["at", x, sgn * (y0 + y1) / 2], ["size", joint_w, y1 - y0],
                          ["layers", Q("B.Cu"), Q("B.Mask")]])
    # the slot: two straight sides and round ends
    hx, hy = (sl - sw) / 2, sw / 2
    items += [_line((-hx, -hy), (hx, -hy), "Edge.Cuts", 0.1), _line((-hx, hy), (hx, hy), "Edge.Cuts", 0.1),
              ["fp_arc", ["start", hx, -hy], ["mid", hx + hy, 0], ["end", hx, hy],
               ["stroke", ["width", 0.1], ["type", "solid"]], ["layer", Q("Edge.Cuts")]],
              ["fp_arc", ["start", -hx, hy], ["mid", -hx - hy, 0], ["end", -hx, -hy],
               ["stroke", ["width", 0.1], ["type", "solid"]], ["layer", Q("Edge.Cuts")]]]
    items += _poly(_shroud(), "F.Fab", 0.1)
    items += _poly([(-18.8, -8.6), (18.8, -8.6), (18.8, 8.6), (-18.8, 8.6)], "F.Fab", 0.1)    # flange
    items += _poly([(-6.3, 8.9), (-6.3, 10.6), (6.3, 10.6), (6.3, 8.9)], "F.Fab", 0.1)       # latch
    items += _poly([(-20.75, -9.4), (20.75, -9.4), (20.75, 11.1), (-20.75, 11.1)], "F.CrtYd", 0.05)
    x1 = (4.5 - 1) * pitch
    for txt, x, y in (("1", x1, -row / 2), ("9", x1, row / 2), ("8", -x1, -row / 2), ("16", -x1, row / 2)):
        items.append(["fp_text", "user", Q(txt), ["at", x + (3.6 if x > 0 else -3.6), y, 0],      # past the row's end
                      ["layer", Q("B.SilkS")], _font(0.8, mirror=True)])
    items.append(["fp_text", "user", Q("J1962 plug, mating face; wide side = pins 1-8"), ["at", 0, -6.5, 0],
                  ["layer", Q("F.Fab")], _font()])
    fp = ["footprint", Q(name), ["version", "20260206"], ["generator", Q("octave_gen")], ["layer", Q("F.Cu")],
          ["descr", Q("SAE J1962 (OBD-II) male plug Jiatel CR-PG001 (2x8, 4.00 mm pitch, rows 8.60 mm) on an "
                      "adapter board, slot for a 1.6 mm board between the rows, joint pads on the back")],
          ["tags", Q("OBD OBD2 OBD-II J1962 plug male adapter")],
          _text("Reference", "REF**", 0, -10.6, "F.Fab"), _text("Value", name, 0, 12.3, "F.Fab"),
          ["attr", "through_hole"], *items, ["embedded_fonts", "no"], _model(PLUG_MODEL)]
    return name, fp


def j1962_edge_joint(pitch=4.0, pad=(3.0, 2.5)):
    """Main-board end of the plug: the adapter board (J1962_Plug_Adapter) stands across the board's
    left edge, the board's tab through its slot; its joint pads are soldered to these with a fillet in
    the corner. Pins 9-16 on F.Cu, 1-8 on B.Cu, pin k and k + 8 at y = (k - 4.5) * 4: seen from the
    plug's mating face (from the left, top side up) 9..16 run left to right above the board, 1..8
    below, which is the plug's own face turned 180 degrees.

    Origin: the board's left edge (x = 0 is the adapter's back face) at the middle of the pins. The
    tab (x -1.6 to 0, TAB wide) is in the board outline (build.py). The courtyard covers the adapter
    and the plug beyond the edge, on both sides. 3D: plug + adapter (gen/mkplug3d.py)."""
    name = "J1962_Edge_Joint"
    items = []
    for k in range(1, 9):
        y = round((k - 4.5) * pitch, 3)
        for num, layers in ((k + 8, ("F.Cu", "F.Mask")), (k, ("B.Cu", "B.Mask"))):
            items.append(["pad", Q(str(num)), "smd", "rect", ["at", pad[0] / 2, y], ["size", *pad],
                          ["layers", *(Q(x) for x in layers)]])
    aw, ah, at = ADAPTER
    tip = -at - 20.0                                   # the plug's mating end
    for side in ("F", "B"):
        items += _poly([(-at, -aw / 2), (0, -aw / 2), (0, aw / 2), (-at, aw / 2)], f"{side}.Fab", 0.1)
        items += _poly([(tip, -20.13), (-at, -20.13), (-at, 20.13), (tip, 20.13)], f"{side}.Fab", 0.1)
        items += _poly([(tip - 0.3, -20.45), (pad[0] + 0.25, -20.45), (pad[0] + 0.25, 20.45), (tip - 0.3, 20.45)],
                       f"{side}.CrtYd", 0.05)
    for txt, y, layer, m in (("9", -14, "F.SilkS", False), ("16", 14, "F.SilkS", False),
                             ("1", -14, "B.SilkS", True), ("8", 14, "B.SilkS", True)):
        items.append(["fp_text", "user", Q(txt), ["at", -at - 1.2, y, 90], ["layer", Q(layer.replace("SilkS", "Fab"))],
                      _font(0.8, mirror=m)])
    items.append(["fp_text", "user", Q("OBD-II plug (adapter + Jiatel CR-PG001)"), ["at", tip / 2, 0, 90],
                  ["layer", Q("F.Fab")], _font()])
    fp = ["footprint", Q(name), ["version", "20260206"], ["generator", Q("octave_gen")], ["layer", Q("F.Cu")],
          ["descr", Q("J1962 plug on an adapter board standing across the board edge: 16 edge pads, 9-16 top, "
                      "1-8 bottom, 4.00 mm pitch")],
          ["tags", Q("OBD OBD2 OBD-II J1962 edge joint")],
          _text("Reference", "REF**", tip / 2, -22.0, "F.Fab"), _text("Value", name, tip / 2, 22.0, "F.Fab"),
          ["attr", "smd"], *items, ["embedded_fonts", "no"], _model(EDGE_MODEL)]
    return name, fp


def write_footprints(fps):
    os.makedirs(FP_DIR, exist_ok=True)
    for name, fp in fps:
        with open(os.path.join(FP_DIR, name + ".kicad_mod"), "w") as f:
            f.write(dump(fp) + "\n")
