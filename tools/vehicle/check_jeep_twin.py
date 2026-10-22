"""Check the Blender pose prototype, without claiming mechanical accuracy."""
import argparse
import math
import sys

import bpy
import bmesh
from mathutils import Vector
from mathutils.bvhtree import BVHTree

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--blend", required=True)
args = parser.parse_args(sys.argv[sys.argv.index("--")+1:])
bpy.ops.wm.open_mainfile(filepath=args.blend)
scene = bpy.context.scene
scene.frame_set(1)
objects = bpy.data.objects
ctrl = objects["JEEP_CONTROLS"]
# New catches must remain solid and keep the fixed lower assembly on the
# fender while only the upper catch follows the opening hood.
latches = [o for o in objects if o.name.startswith("Hood latch fender foot")]
if latches:
    assert len(latches) == 2
    for obj in [o for o in objects if o.name.startswith("Hood latch ")]:
        moving = obj.name.startswith(("Hood latch hood backing", "Hood latch upper catch", "Hood latch catch cross"))
        assert obj.parent == objects["hood" if moving else "body"], obj.name
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        assert all(e.is_manifold for e in bm.edges), f"Open latch mesh: {obj.name}"
        bm.free()
    caps = [o for o in objects if o.name.startswith("Hood latch upper catch cap")]
    assert len(caps) == 2
    closed = {o.name: o.matrix_world.translation.copy() for o in latches+caps}
    scene.frame_set(120)
    for obj in latches:
        assert (obj.matrix_world.translation-closed[obj.name]).length < 1e-6, "Fender latch moved with hood"
    for obj in caps:
        assert (obj.matrix_world.translation-closed[obj.name]).length > .2, "Upper catch did not follow hood"
    scene.frame_set(1)
assert not any(o.name.startswith(("Hardtop ", "Rear liftgate glass", "Liftglass hinge", "Rear wiper")) for o in objects)
assert sum(o.name.startswith("Soft top tubular support bow") for o in objects) == 2
shell = objects["Soft top continuous shell"]
assert sum(p.material_index == 1 for p in shell.data.polygons) > 0
# Verify welded joins on the unsolidified envelope. Its only open boundaries
# belong around the lower edge/door apertures/front header, never the roof seams.
bm = bmesh.new()
bm.from_mesh(shell.data)
roof_edges = [e for e in bm.edges if e.is_boundary and all((shell.matrix_world @ v.co).z > .9590 for v in e.verts)
              and not all(abs((shell.matrix_world @ v.co).y + .275) < 1e-5 for v in e.verts)]
assert not roof_edges, "Open boundary above door tops: roof/side/rear not welded"
remaining = set(bm.verts)
components = 0
while remaining:
    components += 1
    pending = [remaining.pop()]
    while pending:
        for edge in pending.pop().link_edges:
            for v in edge.verts:
                if v in remaining:
                    remaining.remove(v)
                    pending.append(v)
assert components == 1, f"Disconnected soft-top surfaces: {components}"
bm.free()
polygons = [set(p.vertices) for p in shell.data.polygons]
tree = BVHTree.FromPolygons([v.co for v in shell.data.vertices], [tuple(p.vertices) for p in shell.data.polygons])
crossings = [(a,b) for a,b in tree.overlap(tree) if a < b and not polygons[a].intersection(polygons[b])]
assert not crossings, f"Nonadjacent soft-top faces intersect: {crossings[:10]}"
evaluated = shell.evaluated_get(bpy.context.evaluated_depsgraph_get())
data = evaluated.to_mesh()
bm = bmesh.new()
bm.from_mesh(data)
assert all(e.is_manifold for e in bm.edges), "Fabric thickness has unclosed edges"
assert all(f.calc_area() > 1e-12 for f in bm.faces), "Degenerate fabric faces"
bm.free()
evaluated.to_mesh_clear()
assert abs(objects["body"].location.z-.0889) < 1e-6
# Cabin direction contract: front is -Y, so upper seatbacks must sit rearward
# of their lower edges. Check the built mesh, not merely a stored angle.
for side in ("driver", "passenger"):
    seat = objects[f"Cabin {side} reclining seatback"]
    points = [seat.matrix_world @ v.co for v in seat.data.vertices]
    low,high=min(p.z for p in points),max(p.z for p in points)
    lower=[p.y for p in points if p.z < low+.01]
    upper=[p.y for p in points if p.z > high-.02]
    assert sum(upper)/len(upper)-sum(lower)/len(lower) > .10, "Seat reclines forward"
    seat_bm=bmesh.new()
    seat_bm.from_mesh(seat.data)
    assert all(e.is_manifold for e in seat_bm.edges), "Open seat upholstery"
    seat_bm.free()
    polys=[set(p.vertices) for p in seat.data.polygons]
    seat_tree=BVHTree.FromPolygons([v.co for v in seat.data.vertices],[tuple(p.vertices) for p in seat.data.polygons])
    assert not [(a,b) for a,b in seat_tree.overlap(seat_tree) if a<b and not polys[a].intersection(polys[b])], "Self-intersecting seatback"
assert not any(o.name.startswith(("Gearshift", "Integrated headrest", "Seat center fabric insert")) for o in objects)
assert all("Cabin shift pattern "+s in objects for s in "12345R")
assert "Cabin Manual bellows boot" in objects and "Cabin Transfer case bellows boot" in objects
rim = objects["Cabin steering wheel rim"]
column = objects["Cabin steering column"]
assert rim.data.splines[0].use_cyclic_u, "Open steering rim seam"
for obj in objects:
    if obj.type == "MESH" and obj.name.startswith(("Cabin steering airbag", "Cabin steering spoke", "Cabin wheel switch")):
        bm=bmesh.new(); bm.from_mesh(obj.data)
        assert all(e.is_manifold for e in bm.edges), "Open steering component: "+obj.name
        bm.free()
rp = [rim.matrix_world @ Vector(p.co[:3]) for p in rim.data.splines[0].points]
cp = [column.matrix_world @ Vector(p.co[:3]) for p in column.data.splines[0].points]
rim_normal = (rp[24]-rp[0]).cross(rp[48]-rp[0]).normalized()
column_axis = (cp[-1]-cp[0]).normalized()
assert abs(rim_normal.dot(column_axis)) > .9999, "Wheel and column axes disagree"
assert abs(math.degrees(math.asin(column_axis.z))-20) < .01


def update():
    ctrl.update_tag()
    bpy.context.view_layer.update()


for side in ("driver", "passenger"):
    assert abs(objects[side+"_front_spin"].matrix_world.translation.z + .5) < 1e-5
    assert objects[side+"_front_spin"].parent == objects[side+"_wheel"]
    assert objects[side+"_rear_spin"].parent == objects["REAR_AXLE"]

front_rest = objects["driver_front_spin"].matrix_world.translation.copy()
rear_rest = objects["driver_rear_spin"].matrix_world.translation.copy()
ctrl["front_heave_m"] = .05
update()
assert abs(objects["driver_front_spin"].matrix_world.translation.z-front_rest.z-.05) < 1e-5
assert (objects["driver_rear_spin"].matrix_world.translation-rear_rest).length < 1e-5
ctrl["front_heave_m"] = 0.
ctrl["front_roll_deg"] = 7.
update()
assert abs(objects["driver_front_spin"].matrix_world.translation.z-objects["passenger_front_spin"].matrix_world.translation.z) > .17
for label in ("front", "rear"):
    for sign in (-1,1):
        a = objects[f"{label}_{sign}_shock_axle"].matrix_world.translation
        b = objects[f"{label}_{sign}_shock_frame"].matrix_world.translation
        axis = objects[f"{label}_{sign}_shock_axis"].matrix_world.to_quaternion() @ Vector((0,0,1))
        assert axis.dot((a-b).normalized()) > .99999
ctrl["steering_deg"] = 20.
ctrl["wheel_turns"] = .25
ctrl["driver_door"] = 1.
ctrl["crank_turns"] = .25
update()
assert abs(objects["driver_wheel"].rotation_euler.z-math.radians(20)) < 1e-5
assert abs(objects["driver_front_spin"].rotation_euler.x-math.pi/2) < 1e-5
assert abs(objects["driver_door"].rotation_euler.z-math.radians(-68)) < 1e-5
assert abs(objects["crank_pulley"].rotation_euler.y-math.pi/2) < 1e-5
for heave in (-.07, 0., .07):
    for roll in (-8., 0., 8.):
        for label in ("front", "rear"):
            ctrl[label+"_heave_m"] = heave
            ctrl[label+"_roll_deg"] = roll
        update()
        for label in ("front", "rear"):
            for sign in (-1, 1):
                a = objects[f"{label}_{sign}_shock_axle"].matrix_world.translation
                b = objects[f"{label}_{sign}_shock_frame"].matrix_world.translation
                length = (a-b).length
                # Keep the provisional fixed-length shaft inside the fixed
                # body throughout the pose envelope; do not scale either.
                assert length > .390 + .015, (label, sign, length)
                assert length < .345 + .390 - .025, (label, sign, length)
for obj in objects:
    assert all(math.isfinite(v) for row in obj.matrix_world for v in row), obj.name
print("PASS: connected roof with welded joins, manifold thickness, no nonadjacent shell crossings; lift separation, axle heave/roll, shock tracking, steering/spin hierarchy, panel and engine controls, finite transforms")
print("NOT CERTIFIED: fixed link lengths, collision clearance, vehicle measurements, Qt parity")
