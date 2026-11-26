"""Load symbols from KiCad's stock libraries and report their pins."""

import copy
import os

from sexpr import Q, find, findall, parse

SYM_DIRS = [os.path.join(os.path.dirname(__file__), "..", "lib"), "/usr/share/kicad/symbols"]
FP_DIRS = [os.path.join(os.path.dirname(__file__), "..", "lib"), "/usr/share/kicad/footprints"]

_libs = {}


def _lib(name):
    if name not in _libs:
        for d in SYM_DIRS:
            p = os.path.join(d, name + ".kicad_sym")
            if os.path.exists(p):
                _libs[name] = parse(open(p).read())
                break
        else:
            raise FileNotFoundError(f"symbol library {name}")
    return _libs[name]


def _raw(lib, name):
    for s in findall(_lib(lib), "symbol"):
        if s[1] == name:
            return s
    raise KeyError(f"{lib}:{name}")


class Pin:
    def __init__(self, number, name, etype, unit, x, y, rot, length):
        self.number, self.name, self.etype = number, name, etype
        self.unit, self.x, self.y, self.rot, self.length = unit, x, y, rot, length

    def __repr__(self):
        return f"Pin({self.number} {self.name} u{self.unit} {self.etype})"


class Symbol:
    """A flattened symbol ready to embed in a schematic's lib_symbols."""

    def __init__(self, lib_id):
        lib, name = lib_id.split(":")
        node = copy.deepcopy(_raw(lib, name))
        ext = find(node, "extends")
        if ext:
            parent = copy.deepcopy(_raw(lib, ext[1]))
            # child keeps its own properties; units and graphics come from the parent
            props = {p[1]: p for p in findall(node, "property")}
            merged = [node[0], name]
            for item in parent[2:]:
                if isinstance(item, list) and item[0] == "property" and item[1] in props:
                    merged.append(props.pop(item[1]))
                elif isinstance(item, list) and item[0] == "symbol":
                    sub = copy.deepcopy(item)
                    sub[1] = Q(name + sub[1][len(ext[1]):])
                    merged.append(sub)
                else:
                    merged.append(item)
            merged.extend(props.values())
            node = merged
        node[1] = Q(lib_id)
        self.lib_id = lib_id
        self.node = node
        self.pins = []
        self.units = set()
        self.power = find(node, "power") is not None
        for sub in findall(node, "symbol"):
            parts = str(sub[1]).rsplit("_", 2)
            unit, style = int(parts[-2]), int(parts[-1])
            if style > 1:
                continue  # De Morgan alternate body
            if unit:
                self.units.add(unit)
            for p in findall(sub, "pin"):
                at = find(p, "at")
                ln = find(p, "length")
                self.pins.append(Pin(str(find(p, "number")[1]), str(find(p, "name")[1]), p[1], unit,
                                     float(at[1]), float(at[2]), float(at[3]) if len(at) > 3 else 0.0,
                                     float(ln[1])))
        if not self.units:
            self.units = {1}

    def pins_of(self, key):
        """Pins whose number or name is key (name may repeat, e.g. VSS)."""
        hits = [p for p in self.pins if p.number == key]
        return hits or [p for p in self.pins if p.name == key]

    def unit_pins(self, unit):
        return [p for p in self.pins if p.unit in (0, unit)]

    def bbox(self, unit):
        """Body + pin extents (symbol coords, y up)."""
        xs, ys = [], []
        for sub in findall(self.node, "symbol"):
            parts = str(sub[1]).rsplit("_", 2)
            if int(parts[-2]) not in (0, unit) or int(parts[-1]) > 1:
                continue
            for g in sub[2:]:
                if not isinstance(g, list):
                    continue
                for key in ("start", "mid", "end", "center", "at"):
                    pt = find(g, key)
                    if pt and g[0] != "pin":
                        r = float(find(g, "radius")[1]) if g[0] == "circle" else 0
                        xs += [float(pt[1]) - r, float(pt[1]) + r]
                        ys += [float(pt[2]) - r, float(pt[2]) + r]
                pts = find(g, "pts")
                if pts:
                    for xy in findall(pts, "xy"):
                        xs.append(float(xy[1]))
                        ys.append(float(xy[2]))
        for p in self.unit_pins(unit):
            xs.append(p.x)
            ys.append(p.y)
        if not xs:
            return (-2.54, -2.54, 2.54, 2.54)
        return (min(xs), min(ys), max(xs), max(ys))


def footprint_exists(fp):
    lib, name = fp.split(":")
    for d in FP_DIRS:
        if os.path.exists(os.path.join(d, lib + ".pretty", name + ".kicad_mod")):
            return True
    return False


def footprint_path(fp):
    lib, name = fp.split(":")
    for d in FP_DIRS:
        p = os.path.join(d, lib + ".pretty")
        if os.path.exists(os.path.join(p, name + ".kicad_mod")):
            return p, name
    raise FileNotFoundError(fp)
