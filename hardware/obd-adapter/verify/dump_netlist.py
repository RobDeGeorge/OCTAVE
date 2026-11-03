import sys, os, re
sys.path.insert(0, os.getcwd())
import obd_adapter as m
from kilib import footprint_path
d = m.d
out = []
nets = {}
for b in d.blocks:
    out.append(f"\n######## BLOCK {b.name}: {b.title}")
    for n in b.notes: out.append(f"#  note: {n}")
    for p in b.parts:
        pads = ""
        if p.footprint:
            dirp, name = footprint_path(p.footprint)
            txt = open(os.path.join(dirp, name + ".kicad_mod")).read()
            pads = sorted(set(re.findall(r'\(pad "([^"]*)"', txt)), key=lambda s: (len(s), s))
        out.append(f"\n{p.ref}  value={p.value}  symbol={p.lib_id}  footprint={p.footprint}  LCSC={p.lcsc}  MPN={p.mpn}  DNP={p.dnp}")
        if p.note: out.append(f"   note: {p.note}")
        out.append(f"   footprint pads: {pads}")
        pn = p.pin_net()
        for pin in p.sym.pins:
            net = pn[pin.number]
            out.append(f"   pin {pin.number:>4} {pin.name:<14} ({pin.etype}) -> {net if net else 'NO-CONNECT'}")
            if net: nets.setdefault(net, []).append(f"{p.ref}.{pin.number}({pin.name})")
out.append("\n\n######## NETS")
for n in sorted(nets):
    out.append(f"{n}: " + ", ".join(nets[n]))
open(sys.argv[1], "w").write("\n".join(out) + "\n")
