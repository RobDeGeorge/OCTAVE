"""The design model: parts with pin -> net maps, grouped in blocks.

One model feeds both the schematic (schgen.py) and the board (pcbgen.py),
so the two always carry the same netlist.
"""

import uuid as _uuid

from kilib import Symbol, footprint_exists

NS = _uuid.UUID("5b7c0c2e-6f0b-4d7e-9b1a-0c7a0e0b1d2a")


def stable_uuid(*parts):
    return str(_uuid.uuid5(NS, "/".join(str(p) for p in parts)))


class Part:
    def __init__(self, ref, lib_id, value, footprint, nets, lcsc="", mpn="", dnp=False, note="",
                 unit_nets=None):
        self.ref = ref
        self.lib_id = lib_id
        self.value = value
        self.footprint = footprint
        self.nets = nets  # {pin number or pin name: net name or None (no connect)}
        self.lcsc = lcsc
        self.mpn = mpn
        self.dnp = dnp
        self.note = note
        self.block = None
        self.sym = Symbol(lib_id)

    def pin_net(self):
        """{pin number: net or None} for every pin; unlisted pins are an error."""
        out = {}
        for key, net in self.nets.items():
            pins = self.sym.pins_of(str(key))
            if not pins:
                raise KeyError(f"{self.ref}: no pin {key!r} on {self.lib_id}")
            for p in pins:
                if p.number in out and out[p.number] != net:
                    raise ValueError(f"{self.ref}: pin {p.number} assigned twice")
                out[p.number] = net
        missing = [p for p in self.sym.pins if p.number not in out]
        if missing:
            raise KeyError(f"{self.ref} ({self.lib_id}): unassigned pins {missing}")
        return out


class Block:
    def __init__(self, name, title):
        self.name, self.title = name, title
        self.parts = []
        self.notes = []


class Design:
    def __init__(self, name):
        self.name = name
        self.blocks = []
        self.power_nets = set()

    def block(self, name, title):
        b = Block(name, title)
        self.blocks.append(b)
        return b

    def add(self, block, part):
        part.block = block
        block.parts.append(part)
        return part

    @property
    def parts(self):
        return [p for b in self.blocks for p in b.parts]

    def check(self):
        refs = set()
        nets = {}
        problems = []
        for p in self.parts:
            if p.ref in refs:
                problems.append(f"duplicate ref {p.ref}")
            refs.add(p.ref)
            if p.footprint and not footprint_exists(p.footprint):
                problems.append(f"{p.ref}: footprint {p.footprint} not found")
            for pin, net in p.pin_net().items():
                if net:
                    nets.setdefault(net, []).append(f"{p.ref}.{pin}")
        for net, pins in nets.items():
            if len(pins) < 2 and not net.startswith("TP_"):
                problems.append(f"net {net} has one pin: {pins}")
        return nets, problems
