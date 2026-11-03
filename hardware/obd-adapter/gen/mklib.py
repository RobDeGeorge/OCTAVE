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
