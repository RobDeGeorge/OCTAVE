"""Verify coil support connections and end seating in the four demo poses."""
import argparse
import sys
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--blend", required=True)
args = parser.parse_args(sys.argv[sys.argv.index("--")+1:])
bpy.ops.wm.open_mainfile(filepath=args.blend)
scene = bpy.context.scene
for frame in (1,40,80,120):
    scene.frame_set(frame)
    dg = bpy.context.evaluated_depsgraph_get()
    cache = {}
    def tree(obj):
        if obj.name not in cache:
            evaluated = obj.evaluated_get(dg)
            data = evaluated.to_mesh()
            cache[obj.name] = BVHTree.FromPolygons(
                [evaluated.matrix_world @ v.co for v in data.vertices],
                [tuple(p.vertices) for p in data.polygons], epsilon=.0001)
            evaluated.to_mesh_clear()
        return cache[obj.name]
    rails = [o for o in scene.objects if o.name.startswith("Boxed frame rail")]
    axles = [o for o in scene.objects if o.name.startswith("Dana 44 axle tube")]
    for label in ("front","rear"):
        for sign in (-1,1):
            key = f"{label}_{sign}"
            a = bpy.data.objects[key+"_coil_axle"]
            b = bpy.data.objects[key+"_coil_frame"]
            seats = [next(o for o in p.children if o.name.startswith("Lift spring seat")) for p in (a,b)]
            pedestal = bpy.data.objects["Coil mount "+key+" axle pedestal"]
            assert any(tree(pedestal).overlap(tree(axle)) for axle in axles), (frame,key,"detached axle pedestal")
            assert tree(pedestal).overlap(tree(seats[0])), (frame,key,"detached lower seat")
            webs = [o for o in scene.objects if o.name.startswith("Coil mount "+key+" tower web")]
            bridges = [o for o in scene.objects if o.name.startswith("Coil mount "+key+" seat bridge")]
            assert len(webs) == len(bridges) == 2
            for web in webs:
                assert any(tree(web).overlap(tree(rail)) for rail in rails), (frame,key,"detached frame web")
                assert any(tree(web).overlap(tree(bridge)) for bridge in bridges), (frame,key,"detached bridge")
            assert any(tree(bridge).overlap(tree(seats[1])) for bridge in bridges), (frame,key,"detached upper seat")
            axis = bpy.data.objects[key+"_spring_axis"]
            spring = next(o for o in axis.children if o.name.startswith("Compressing lift spring"))
            evaluated = spring.evaluated_get(dg)
            points = evaluated.data.splines[0].points
            for point,seat in zip((points[0],points[-1]),seats):
                center = evaluated.matrix_world @ Vector(point.co[:3])
                distance = tree(seat).find_nearest(center)[3]
                assert distance <= spring.data.bevel_depth+.001, (frame,key,"coil end off seat",distance)
            assert all(abs(v-1) < 1e-6 for v in axis.scale), "Wire thickness scaled with travel"
print("PASS: four coils seated, axle pedestals and frame brackets connected in four poses; unscaled spring wire")
