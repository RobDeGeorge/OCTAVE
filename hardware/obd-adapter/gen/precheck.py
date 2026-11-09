"""Pre-route check: lay the hand copper (prewire.py) on the placed board and run DRC, so clashes
between placement and the hand-laid power copper show up before a 20-minute route. Writes
../obd-adapter-pre.kicad_pcb (+ .json); run after `build.py pcb`."""
import json, os, subprocess, sys, collections
import pcbnew
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import obd_adapter, prewire
pcb = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "obd-adapter.kicad_pcb")
b = pcbnew.LoadBoard(pcb)
prewire.wire(b, obd_adapter.d)
out = pcb.replace(".kicad_pcb", "-pre.kicad_pcb")
b.Save(out)
subprocess.run(["kicad-cli", "pcb", "drc", "--severity-error", "--format", "json", "-o", out + ".json", out], capture_output=True)
r = json.load(open(out + ".json"))
c = collections.Counter(v["type"] for v in r["violations"])
print(dict(c))
for v in r["violations"]:
    if v["type"] not in ("solder_mask_bridge",):
        print(" ", v["type"], "|", " / ".join(i["description"][:50] for i in v["items"]))
