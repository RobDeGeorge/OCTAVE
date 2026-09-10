"""Export rigid vehicle hinges for both standard glTF editors and Qt 6.7+.

Qt uses separate hinge-local GLBs; the assembled GLB retains named pivot nodes
and opening animation clips. No private RuntimeLoader scene APIs are required.
"""

import json
import math
from pathlib import Path

import bmesh
import bpy
from mathutils import Matrix, Vector

# Blender: -Y forward, Z up, +X VEHICLE LEFT (right-handed coordinates).
# After glTF conversion: +Z forward, +Y up, +X vehicle left.
STEERING = json.loads(Path(__file__).with_name("steering_geometry.json").read_text())

HINGES = {
    "body": ((0, 0, 0.9), "Z", 0),
    "driver_door": ((0.767, -0.394, 0.96), "Z", -68),
    "passenger_door": ((-0.767, -0.394, 0.96), "Z", 68),
    "tailgate": ((-0.455, 1.79, 0.95), "Z", 95),
    "rear_glass": ((0, 1.717, 1.72), "X", 100),
    "hood": ((0, -0.635, 1.285), "X", -65),
    "driver_wheel": ((0.64, -1.186, 0.4), "Z", 0),
    "passenger_wheel": ((-0.64, -1.186, 0.4), "Z", 0),
    "driver_front_spin": ((0.737, -1.186, 0.4), "X", 0),
    "passenger_front_spin": ((-0.737, -1.186, 0.4), "X", 0),
    "driver_rear_spin": ((0.737, 1.186, 0.4), "X", 0),
    "passenger_rear_spin": ((-0.737, 1.186, 0.4), "X", 0),
    "steering_links": ((0, 0, 0.9), "Z", 0),
}


def export_vehicle(output: Path, blend: Path | None = None):
    bpy.context.view_layer.update()
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    joints = {}
    manifest = {"parts": {}, "bounds": {}, "units": "metres", "forward": "+Z", "left": "+X"}
    closed_radius = swept_radius = 0.0
    for name, (pivot_ground, axis, limit) in HINGES.items():
        pivot = Vector(pivot_ground) - Vector((0, 0, 0.9))
        joint = bpy.data.objects.new(name, None)
        bpy.context.collection.objects.link(joint)
        joint.location = pivot
        joint.empty_display_type = "ARROWS"
        joint.empty_display_size = 0.12
        joints[name] = joint
        group = [o for o in meshes if o.get("rig_part", "body") == name]
        if not group:
            raise ValueError(f"Empty hinge group: {name}")
        for obj in group:
            world = obj.matrix_world.copy()
            # Copy shared tread/primitive meshes before baking the local pivot.
            obj.data = obj.data.copy()
            obj.data.transform(Matrix.Translation(-Vector(pivot_ground)) @ world)
            obj.parent = joint
            obj.matrix_parent_inverse = Matrix.Identity(4)
            obj.matrix_basis = Matrix.Identity(4)
            # Explicit triangulation removes importer-dependent ngon tessellation.
            bm = bmesh.new()
            bm.from_mesh(obj.data)
            bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-7)
            bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-8)
            bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
            bm.to_mesh(obj.data)
            bm.free()
        bpy.context.view_layer.update()
        # Calculate a conservative envelope across each hinge's entire sweep.
        # Any simultaneous pose is a union of these independent rigid parts.
        for obj in group:
            for vertex in obj.data.vertices:
                closed_radius = max(closed_radius, (vertex.co + pivot).length)
        for step in range(19 if limit else 1):
            rotation = Matrix.Rotation(math.radians(limit * step / 18), 3, axis)
            for obj in group:
                # Bounding-box corners bound the full mesh and keep export quick.
                corners = [Vector(corner) for corner in obj.bound_box]
                swept_radius = max(swept_radius, *((rotation @ p + pivot).length for p in corners))
        manifest["parts"][name] = {
            "file": name + ".glb",
            "pivot": [pivot.x, pivot.z, -pivot.y],
            "axis": "Y" if axis == "Z" else axis,
            "openAngle": limit,
            "components": [o.name for o in group],
        }
        if limit:
            joint.rotation_mode = "XYZ"
            component = "XYZ".index(axis)
            joint.rotation_euler[component] = 0
            joint.keyframe_insert(data_path="rotation_euler", frame=1)
            joint.rotation_euler[component] = math.radians(limit)
            joint.keyframe_insert(data_path="rotation_euler", frame=31)
            joint.animation_data.action.name = "Open_" + name
            joint.rotation_euler[component] = 0
    bpy.context.scene.frame_start = 1
    bpy.context.scene.frame_end = 31
    bpy.context.scene.frame_set(1)
    bpy.context.view_layer.update()
    if blend:
        blend.parent.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(blend.resolve()))
    # Batch only within a rigid part, so no material merge destroys a hinge.
    for name, joint in joints.items():
        materials = {o.data.materials[0] for o in joint.children}
        for mat in sorted(materials, key=lambda m: m.name):
            group = [o for o in joint.children if o.data.materials[0] == mat]
            bpy.ops.object.select_all(action="DESELECT")
            for o in group:
                o.select_set(True)
            bpy.context.view_layer.objects.active = group[0]
            if len(group) > 1:
                bpy.ops.object.join()
            group[0].name = f"{name}/{mat.name}"
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(
        filepath=str(output.resolve()),
        export_format="GLB",
        use_selection=True,
        export_yup=True,
        export_apply=True,
        export_cameras=False,
        export_lights=False,
        export_extras=True,
        export_animations=True,
        export_animation_mode="ACTIONS",
    )
    folder = output.parent / "jeep_tj"
    folder.mkdir(exist_ok=True)
    for name, joint in joints.items():
        position = joint.location.copy()
        joint.location = (0, 0, 0)
        bpy.ops.object.select_all(action="DESELECT")
        joint.select_set(True)
        for o in joint.children:
            o.select_set(True)
        bpy.ops.export_scene.gltf(
            filepath=str((folder / (name + ".glb")).resolve()),
            export_format="GLB",
            use_selection=True,
            export_yup=True,
            export_apply=True,
            export_cameras=False,
            export_lights=False,
            export_extras=False,
            export_animations=False,
        )
        joint.location = position
    manifest["steering"] = STEERING
    manifest["bounds"] = {"closedRadius": closed_radius, "sweptRadius": swept_radius + 0.03}
    (folder / "rig.json").write_text(json.dumps(manifest, indent=2) + "\n")
    # Generate runtime coordinates from the same source as the mesh pivots.
    (folder / "Rig.js").write_text(
        ".pragma library\n// Generated by tools/vehicle/rig_export.py\n"
        + "var parts = "
        + json.dumps(
            {name: {k: v for k, v in part.items() if k != "components"} for name, part in manifest["parts"].items()},
            indent=2,
        )
        + ";\n"
        + "var bounds = "
        + json.dumps(manifest["bounds"])
        + ";\nvar steering = "
        + json.dumps(STEERING)
        + ";\n"
    )
    bpy.context.scene.frame_set(1)
    bpy.context.view_layer.update()
    print(f"Rig exported: {len(meshes)} components, {len(joints)} part pivots; bounds {manifest['bounds']}", flush=True)
    return joints
