"""Check added panel returns for closed edges and opening-panel clearance.

Run with the builder's bpy Python after generating the editable .blend.
"""

from pathlib import Path
import math

import bpy
import bmesh
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[2]
bpy.ops.wm.open_mainfile(filepath=str(ROOT / "dev/jeep-tj-reference/jeep-tj-2003.blend"))
names = (
    "Tub rear corner return",
    "Door aperture recessed flange",
    "Windshield pillar side return",
    "Hardtop rear corner return",
)
parts = [o for o in bpy.data.objects if o.type == "MESH" and o.name.startswith(names)]
assert len(parts) == 8
for o in parts:
    bm = bmesh.new()
    bm.from_mesh(o.data)
    assert all(e.is_manifold for e in bm.edges), o.name
    bm.free()


def bvh(o):
    return BVHTree.FromPolygons(
        [o.matrix_world @ v.co for v in o.data.vertices], [tuple(p.vertices) for p in o.data.polygons]
    )


fixed = [(o.name, bvh(o)) for o in parts]
contacts = set()
for name, axis, angle in [
    ("driver_door", 2, -68),
    ("passenger_door", 2, 68),
    ("rear_glass", 0, 100),
    ("tailgate", 2, 95),
    ("hood", 0, -65),
]:
    joint = bpy.data.objects[name]
    for step in range(1, 21):
        joint.rotation_euler[axis] = math.radians(angle * step / 20)
        bpy.context.view_layer.update()
        for o in joint.children:
            if o.type != "MESH":
                continue
            moving = bvh(o)
            for fixed_name, surface in fixed:
                if moving.overlap(surface):
                    contacts.add((name, o.name, fixed_name, step))
    joint.rotation_euler[axis] = 0
    bpy.context.view_layer.update()
print("Panel contacts:", sorted(contacts))
assert not contacts
print("PASS: eight closed panel returns; five opening panels clear additions across 20 poses each")
