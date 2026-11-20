"""Open the exported hood through its travel and report what it passes through.

blender -b --python-exit-code 1 --python tools/vehicle/check_jeep_hood_swing.py -- ASSET_DIR

ASSET_DIR holds jeep_tj/ (rig.json, body.glb, hood.glb). The hood is turned
about its rig pivot from 10 degrees to its open angle and its triangles are
tested against the body's. Contacts at the hinge line itself (within 30 mm of
the axis) and with the hood's own latches and stops are not counted.
"""
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

folder = Path(sys.argv[sys.argv.index("--") + 1]) / "jeep_tj"
rig = json.loads((folder / "rig.json").read_text())
bpy.ops.wm.read_factory_settings(use_empty=True)


def load(part):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(folder / rig["parts"][part]["file"]))
    bpy.context.view_layer.update()
    return [o for o in bpy.data.objects if o not in before and o.type == "MESH"]


def to_blender(v):
    return Vector((v[0], -v[2], v[1]))


HINGE_CLEAR = float(sys.argv[sys.argv.index("--") + 2]) if len(sys.argv) > sys.argv.index("--") + 2 else 0.0
hood_part = rig["parts"]["hood"]
pivot = to_blender(hood_part["pivot"])
offset = pivot - to_blender(rig["parts"]["body"]["pivot"])
body = load("body")
hood = load("hood")
assert hood_part["axis"] == "X"
# Body triangles in hood-pivot space (each part's GLB is about its own pivot).
trees = []
for obj in body:
    verts = [obj.matrix_world @ v.co - offset for v in obj.data.vertices]
    trees.append((obj.name, BVHTree.FromPolygons(verts, [tuple(p.vertices) for p in obj.data.polygons])))
failures = {}
steps = [a for a in range(10, int(abs(hood_part["openAngle"])), 5)] + [abs(hood_part["openAngle"])]
for angle in steps:
    turn = Matrix.Rotation(math.radians(math.copysign(angle, hood_part["openAngle"])), 4, "X")
    for obj in hood:
        world = turn @ obj.matrix_world
        verts = [world @ v.co for v in obj.data.vertices]
        polygons = [tuple(p.vertices) for p in obj.data.polygons]
        # Leave out what lies along the hinge line: it turns in place there.
        keep = [p for p in polygons if min(math.hypot(verts[i].y, verts[i].z) for i in p) > HINGE_CLEAR]
        if not keep:
            continue
        tree = BVHTree.FromPolygons(verts, keep)
        for name, other in trees:
            pairs = len(tree.overlap(other))
            if pairs:
                key = (obj.name.split(".")[0], name.split(".")[0])
                failures.setdefault(key, []).append((angle, pairs))
for (a, b), hits in sorted(failures.items()):
    print(f"CONTACT {a} / {b}: " + ", ".join(f"{angle:g} deg ({pairs})" for angle, pairs in hits))
if failures:
    raise SystemExit(1)
print(f"PASS: hood clears the body at {len(steps)} angles up to {abs(hood_part['openAngle']):g} degrees")
