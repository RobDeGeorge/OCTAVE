"""Export the lifted soft-top twin as the app's rigid-part GLB set.

blender -b --python-exit-code 1 --python tools/vehicle/export_jeep_twin.py -- \
  --blend dev/jeep-twin/<candidate>/jeep-twin.blend --output /new/dir/jeep_tj_2003.glb

The twin is posed at rest (frame 1), every mesh, curve and text object is
baked to world space, and each is assigned to the rig part of its nearest
named ancestor (doors, hood, tailgate, knuckles, wheels, spinners), else the
body. Pivots come from the twin's own joints, so the 3.5-inch lift carries
into the hinge, lamp and steering-box positions. The suspension, soft top and
cabin are static body geometry in the app; only the existing hinges, wheels and
spinners move. Driveshafts keep the twin's rigid lift and are not re-aimed at
the axles.
"""
import argparse
import json
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rig_export

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--blend", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True, help="Assembled GLB; parts go to its sibling jeep_tj/")
args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])

bpy.ops.wm.open_mainfile(filepath=str(args.blend.resolve()))
scene = bpy.context.scene
scene.frame_set(1)
ctrl = bpy.data.objects["JEEP_CONTROLS"]
lift = float(ctrl["lift_m"])
bpy.context.view_layer.update()

# Hinge parts the twin keeps (the soft top has no lifting rear glass).
hinges = {name: value for name, value in rig_export.HINGES.items() if name in bpy.data.objects}
spinner_names = ("crank_pulley", "engine_fan", "front_driveshaft", "rear_driveshaft")
parts = set(hinges) | set(spinner_names)
ground = Vector((0, 0, 0.9))

studio = bpy.data.collections.get("STUDIO - exclude from export")
excluded = set(studio.all_objects) if studio else set()


def owner(obj):
    while obj is not None:
        if obj.name in parts:
            return obj.name
        obj = obj.parent
    return "body"


# Bake the evaluated rest pose (drivers, constraints, modifiers, curve bevels).
depsgraph = bpy.context.evaluated_depsgraph_get()
baked = []
for obj in scene.objects:
    if obj in excluded or obj.type not in {"MESH", "CURVE", "FONT"}:
        continue
    evaluated = obj.evaluated_get(depsgraph)
    data = bpy.data.meshes.new_from_object(evaluated, depsgraph=depsgraph)
    if not data.polygons:
        continue
    # rig_export expects ground-based geometry (the twin's ground is z = -0.9).
    data.transform(Matrix.Translation(ground) @ evaluated.matrix_world)
    baked.append((obj.name, owner(obj), data))

joints = {}
for name in parts:
    joint = bpy.data.objects[name]
    joints[name] = (joint.matrix_world.translation.copy(), joint.matrix_world.to_quaternion())

for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj, do_unlink=True)
for name, part, data in baked:
    obj = bpy.data.objects.new(name, data)
    scene.collection.objects.link(obj)
    obj["rig_part"] = part

for name, (_, axis, limit) in hinges.items():
    rig_export.HINGES[name] = (tuple(joints[name][0] + ground), axis, limit)
for name in set(rig_export.HINGES) - set(hinges):
    del rig_export.HINGES[name]
spinners = {name: (tuple(joints[name][0] + ground), joints[name][1] @ Vector((0, -1, 0)))
            for name in spinner_names}
# The steering box rides on the lifted frame; knuckles stay on the axle.
for key in ("pitmanPivot", "pitmanEnd"):
    rig_export.STEERING[key] = [rig_export.STEERING[key][0], rig_export.STEERING[key][1] + lift,
                                rig_export.STEERING[key][2]]

rig_export.export_vehicle(args.output, None, spinners)

folder = args.output.parent / "jeep_tj"
manifest = json.loads((folder / "rig.json").read_text())
manifest["bodyLift"] = lift
manifest["source"] = "tools/vehicle/export_jeep_twin.py"
(folder / "rig.json").write_text(json.dumps(manifest, indent=2) + "\n")
with (folder / "Rig.js").open("a") as stream:
    stream.write(f"// Sprung-body lift (metres); body-mounted lamps rise by this.\nvar bodyLift = {lift};\n")
print(f"TWIN EXPORTED {args.output}: {len(baked)} objects, lift {lift:.4f} m", flush=True)
