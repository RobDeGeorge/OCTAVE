"""Report selected unintended surface intersections in the saved Blender rig."""
import argparse
import json
from pathlib import Path
import sys

import bpy
from mathutils.bvhtree import BVHTree

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--blend", required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args(sys.argv[sys.argv.index("--")+1:])
bpy.ops.wm.open_mainfile(filepath=args.blend)
scene = bpy.context.scene
checks = [
    ("parking_brake_console", ("Parking brake",), ("Cupholder console", "Console rear bridge", "Console storage", "Center armrest", "Shifter console")),
    ("roof_bows", ("Soft top continuous shell",), ("Soft top tubular support bow",)),
    ("tires_flares", ("31 inch tire carcass",), ("Front flare", "Rear flare")),
    ("shocks_springs", ("Fox style shock body", "Telescoping shock shaft"), ("Compressing lift spring",)),
    ("shocks_tires", ("Fox style shock body", "Telescoping shock shaft"), ("31 inch tire carcass",)),
    ("shocks_frame", ("Fox style shock body", "Telescoping shock shaft"), ("Boxed frame rail",)),
    ("hood_engine", ("Hood",), ("Valve cover", "Air intake tube", "Air cleaner", "Battery")),
    ("seats_console", ("Cabin driver cushion", "Cabin passenger cushion", "Cabin driver reclining", "Cabin passenger reclining"),
     ("Shifter console", "Cupholder console", "Console storage", "Center armrest", "Parking brake")),
    ("seats_body", ("Cabin driver cushion", "Cabin passenger cushion", "Cabin driver reclining", "Cabin passenger reclining", "Cabin rear bench"),
     ("Raised transmission tunnel", "Rear wheel housing", "Door interior card", "Dashboard lower")),
    ("steering_clearance", ("Cabin steering wheel rim", "Cabin steering airbag", "Cabin steering spoke"),
     ("Dashboard upper", "Dashboard lower", "Driver instrument binnacle", "Cabin driver cushion", "Cabin driver reclining")),
    ("manual_controls", ("Cabin manual shift knob", "Cabin manual bent shift lever", "Cabin Transfer case", "Cabin transfer case"),
     ("Dashboard lower", "Center dash stack", "Cupholder console", "Cabin driver cushion", "Cabin passenger cushion")),
    ("pedals_clearance", ("Clutch pedal pad", "Brake pedal pad", "Accelerator pedal pad"),
     ("Cabin driver cushion", "Raised transmission tunnel", "Front footwell pan")),
    ("seat_padding", ("Cabin driver reclining", "Cabin passenger reclining", "Cabin rear bench back"),
     ("Cabin driver cushion", "Cabin passenger cushion", "Cabin rear bench cushion")),
    ("column_aperture", ("Cabin steering column",), ("Dashboard lower", "Dashboard upper", "Dashboard fascia backing", "Driver instrument binnacle")),
]
report = {"scope":"Selected surface intersections, not exhaustive vehicle clearance", "poses":[]}
for frame in (1,40,80,120):
    scene.frame_set(frame)
    dg = bpy.context.evaluated_depsgraph_get()
    cache = {}
    def tree(obj):
        if obj.name not in cache:
            ev = obj.evaluated_get(dg)
            data = ev.to_mesh()
            verts = [ev.matrix_world @ v.co for v in data.vertices]
            cache[obj.name] = BVHTree.FromPolygons(verts, [tuple(p.vertices) for p in data.polygons])
            ev.to_mesh_clear()
        return cache[obj.name]
    intersections = []
    for label, left, right in checks:
        a = [o for o in scene.objects if o.type in {"MESH","CURVE"} and o.name.startswith(left)]
        b = [o for o in scene.objects if o.type in {"MESH","CURVE"} and o.name.startswith(right)]
        assert a and b, (label, "Empty audit selection")
        for x in a:
            for y in b:
                count = len(tree(x).overlap(tree(y)))
                if count:
                    intersections.append({"check":label,"a":x.name,"b":y.name,"triangle_pairs":count})
    report["poses"].append({"frame":frame,"intersections":intersections})
args.output.write_text(json.dumps(report,indent=2)+"\n")
print(json.dumps(report,indent=2))
raise SystemExit(1 if any(p["intersections"] for p in report["poses"]) else 0)
