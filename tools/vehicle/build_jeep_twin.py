"""Create the lifted soft-top Blender development rig from an editable base.

blender -b --python-exit-code 1 --python tools/vehicle/build_jeep_twin.py -- \
  --source /path/to/jeep-tj.blend --output /new/candidate --render

This is a pose/inspection rig. Axle travel is prescribed, not a solved five-link
suspension. Approximate link endpoints and stretch are reported explicitly.
"""
import argparse
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Vector

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--render", action="store_true")
args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
args.output.mkdir(parents=True, exist_ok=False)
bpy.ops.wm.open_mainfile(filepath=str(args.source.resolve()))
scene = bpy.context.scene
scene.frame_set(1)
LIFT = 3.5 * 0.0254
target = json.loads(Path(__file__).with_name("target_vehicle.json").read_text())


def empty(name, point=(0, 0, 0), parent=None):
    obj = bpy.data.objects.new(name, None)
    scene.collection.objects.link(obj)
    obj.location = point
    obj.empty_display_size = 0.10
    obj.parent = parent
    return obj


def attach(obj, parent):
    bpy.context.view_layer.update()
    world = obj.matrix_world.copy()
    obj.parent = parent
    obj.matrix_parent_inverse = parent.matrix_world.inverted()
    obj.matrix_world = world


def material(name, color, metal=0, rough=.5):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1)
    bsdf.inputs["Metallic"].default_value = metal
    bsdf.inputs["Roughness"].default_value = rough
    return mat


def mesh(name, verts, faces, mat, parent=None):
    data = bpy.data.meshes.new(name)
    data.from_pydata(verts, [], faces)
    data.update()
    obj = bpy.data.objects.new(name, data)
    scene.collection.objects.link(obj)
    obj.data.materials.append(mat)
    for p in data.polygons:
        p.use_smooth = True
    if parent:
        attach(obj, parent)
    return obj


def tube(name, points, radius, mat, parent=None):
    curve = bpy.data.curves.new(name, "CURVE")
    curve.dimensions = "3D"
    curve.bevel_depth = radius
    curve.bevel_resolution = 2
    poly = curve.splines.new("POLY")
    poly.points.add(len(points)-1)
    for p, co in zip(poly.points, points):
        p.co = (*co, 1)
    obj = bpy.data.objects.new(name, curve)
    scene.collection.objects.link(obj)
    curve.materials.append(mat)
    if parent:
        attach(obj, parent)
    return obj


ctrl = empty("JEEP_CONTROLS")
ctrl.empty_display_type = "CUBE"
ctrl.empty_display_size = 1.0
properties = {
    "front_heave_m": (0., -.07, .07), "rear_heave_m": (0., -.07, .07),
    "front_roll_deg": (0., -8., 8.), "rear_roll_deg": (0., -8., 8.),
    "steering_deg": (0., -28., 28.), "wheel_turns": (0., -1000., 1000.),
    "crank_turns": (0., -1000., 1000.),
    "driver_door": (0., 0., 1.), "passenger_door": (0., 0., 1.),
    "hood": (0., 0., 1.), "tailgate": (0., 0., 1.),
}
for name, (value, low, high) in properties.items():
    ctrl[name] = value
    ctrl.id_properties_ui(name).update(min=low, max=high)
ctrl["status"] = "POSE PROTOTYPE: approximate mounts; not a five-link dynamics solver"
ctrl["lift_m"] = LIFT


def drive(obj, path, index, prop, expression="v"):
    f = obj.driver_add(path, index)
    d = f.driver
    var = d.variables.new()
    var.name = "v"
    var.type = "SINGLE_PROP"
    var.targets[0].id = ctrl
    var.targets[0].data_path = f'["{prop}"]'
    d.expression = expression


# Raise sprung assemblies, then recover unsprung geometry into axle parents.
roots = [o for o in scene.objects if o.type == "EMPTY" and o != ctrl and o.parent is None]
for obj in roots:
    obj.animation_data_clear()
    obj.location.z += LIFT
body = bpy.data.objects["body"]
bpy.context.view_layer.update()
axles = {}
for label, y in (("front", -1.186), ("rear", 1.186)):
    axle = empty(label.upper() + "_AXLE", (0, y, -.5))
    axle.rotation_mode = "XYZ"
    drive(axle, "location", 2, label + "_heave_m", "-.5 + v")
    drive(axle, "rotation_euler", 1, label + "_roll_deg", "v*pi/180")
    axles[label] = axle
for obj in list(body.children):
    if obj.name.startswith("Dana 44"):
        center = sum((obj.matrix_world @ Vector(c) for c in obj.bound_box), Vector()) / 8
        obj.location.z -= LIFT
        attach(obj, axles["front" if center.y < 0 else "rear"])
    elif obj.name.startswith(("Shock ", "Coil spring", "Trailing arm")):
        bpy.data.objects.remove(obj, do_unlink=True)

for side in ("driver", "passenger"):
    knuckle = bpy.data.objects[side + "_wheel"]
    knuckle.location.z -= LIFT
    attach(knuckle, axles["front"])
    knuckle.rotation_mode = "XYZ"
    # Inspection steering, deliberately separate from the runtime rod solver.
    drive(knuckle, "rotation_euler", 2, "steering_deg", "v*pi/180")
    for end in ("front", "rear"):
        wheel = bpy.data.objects[side + "_" + end + "_spin"]
        wheel.location.z -= LIFT
        attach(wheel, knuckle if end == "front" else axles["rear"])
        wheel.rotation_mode = "XYZ"
        drive(wheel, "rotation_euler", 0, "wheel_turns", "v*2*pi")

for name, angle, axis in (("driver_door", -68, 2), ("passenger_door", 68, 2),
                           ("hood", -65, 0), ("tailgate", 95, 2)):
    obj = bpy.data.objects[name]
    obj.rotation_mode = "XYZ"
    drive(obj, "rotation_euler", axis, name, f"v*{angle}*pi/180")
for name in ("crank_pulley", "engine_fan"):
    obj = bpy.data.objects[name]
    # Retain tilted parent orientation, spin in its own local forward frame.
    pivot = empty(name + "_AXIS", obj.location)
    pivot.rotation_mode = "QUATERNION"
    pivot.rotation_quaternion = obj.rotation_quaternion.copy()
    attach(obj, pivot)
    obj.matrix_parent_inverse = Matrix.Identity(4)
    obj.matrix_basis = Matrix.Identity(4)
    obj.rotation_mode = "XYZ"
    drive(obj, "rotation_euler", 1, "crank_turns", "v*2*pi")

steel = bpy.data.materials["Undercarriage steel"]
alloy = material("Shock brushed aluminum provisional Fox body", (.46, .49, .52), .8, .26)
chrome = material("Shock polished shaft", (.65, .69, .74), 1, .16)
black = bpy.data.materials["Textured black trim"]
fabric = material("Soft top black woven fabric", (.006, .008, .010), 0, .93)
nodes = fabric.node_tree.nodes
noise = nodes.new("ShaderNodeTexNoise")
noise.inputs["Scale"].default_value = 480
bump = nodes.new("ShaderNodeBump")
bump.inputs["Strength"].default_value = .18
bump.inputs["Distance"].default_value = .0008
fabric.node_tree.links.new(noise.outputs["Fac"], bump.inputs["Height"])
fabric.node_tree.links.new(bump.outputs["Normal"], nodes.get("Principled BSDF").inputs["Normal"])
vinyl = material("Soft top tinted flexible vinyl", (.045, .060, .065), .05, .20)
vinyl.node_tree.nodes.get("Principled BSDF").inputs["Alpha"].default_value = .55
vinyl.diffuse_color = (.045, .060, .065, .55)
vinyl.surface_render_method = "DITHERED"


def endpoint(name, p, parent):
    o = empty(name, p)
    attach(o, parent)
    return o


def aim(name, a, b):
    o = empty(name)
    c = o.constraints.new("COPY_LOCATION")
    c.target = a
    c = o.constraints.new("DAMPED_TRACK")
    c.target = b
    c.track_axis = "TRACK_Z"
    return o


def distance_driver(obj, a, b):
    d = obj.driver_add("scale", 2).driver
    v = d.variables.new()
    v.name = "length"
    v.type = "LOC_DIFF"
    v.targets[0].id, v.targets[1].id = a, b
    d.expression = "length"


def cylinder_local(name, radius, z0, z1, mat, parent):
    n = 24
    verts = [(radius*math.cos(i*2*math.pi/n), radius*math.sin(i*2*math.pi/n), z)
             for z in (z0, z1) for i in range(n)]
    faces = [(i, (i+1)%n, (i+1)%n+n, i+n) for i in range(n)]
    faces += [tuple(reversed(range(n))), tuple(range(n, 2*n))]
    obj = mesh(name, verts, faces, mat)
    obj.parent = parent
    return obj


links = []
shocks = []
for label, y in (("front", -1.186), ("rear", 1.186)):
    axle = axles[label]
    direction = 1 if label == "front" else -1
    for sign in (-1, 1):
        x = sign*.52
        # Keep the provisional shock envelope outside the coil, rather than
        # crossing through its upper turns. Actual Fox mounts remain unmeasured.
        lower = endpoint(f"{label}_{sign}_shock_axle", (x-sign*.12, y-.38, -.52), axle)
        upper = endpoint(f"{label}_{sign}_shock_frame", (x-sign*.17, y-.16, -.11+LIFT), body)
        axis = aim(f"{label}_{sign}_shock_axis", upper, lower)
        cylinder_local("Fox style shock body UNMEASURED", .027, .025, .340, alloy, axis)
        cylinder_local("Shock seal head", .030, .320, .345, black, axis)
        rod = aim(f"{label}_{sign}_shock_rod_axis", lower, upper)
        cylinder_local("Telescoping shock shaft", .009, .012, .390, chrome, rod)
        shocks.append((lower, upper))
        for p in (lower, upper):
            eye = cylinder_local("Shock eye", .029, -.022, .022, black, p)
            eye.rotation_euler.y = math.pi/2
        a = endpoint(f"{label}_{sign}_coil_axle", (x, y, -.46), axle)
        b = endpoint(f"{label}_{sign}_coil_frame", (x, y, -.16+LIFT), body)
        axis = aim(f"{label}_{sign}_spring_axis", a, b)
        points = [(.067*math.cos(t*math.tau*6), .067*math.sin(t*math.tau*6), t) for t in [i/180 for i in range(181)]]
        spring = tube("Compressing lift spring", points, .009, steel)
        spring.parent = axis
        distance_driver(axis, a, b)
        for p in (a, b):
            cylinder_local("Lift spring seat", .09, -.008, .008, steel, p)
        for level, width, height in (("lower", .51, -.55), ("upper", .32, -.36)):
            a = endpoint(f"{label}_{sign}_{level}_axle", (sign*width, y, height), axle)
            b = endpoint(f"{label}_{sign}_{level}_frame", (sign*width, y+direction*.51, height+.12+LIFT), body)
            axis = aim(f"{label}_{sign}_{level}_link_axis", a, b)
            cylinder_local("Control arm POSE ONLY", .022 if level == "lower" else .017, 0, 1, steel, axis)
            distance_driver(axis, a, b)
            links.append((a, b))
    a = endpoint(label+"_track_axle", (-.48, y+.09, -.39), axle)
    b = endpoint(label+"_track_frame", (.43, y+.09, -.25+LIFT), body)
    axis = aim(label+"_track_axis", a, b)
    cylinder_local("Track bar POSE ONLY", .017, 0, 1, steel, axis)
    distance_driver(axis, a, b)
    links.append((a, b))

# Remove the hard shell and its glass-specific hardware completely.
remove = ("Hardtop ", "Tinted quarter window", "Quarter glass rubber gasket", "Roof drip gutter",
          "Roof molded reinforcement rib", "Rear liftgate glass", "Rear glass seal", "Liftglass hinge", "Rear wiper")
for obj in list(scene.objects):
    if obj.name.startswith(remove) or obj.name == "rear_glass":
        bpy.data.objects.remove(obj, do_unlink=True)


def top_point(x, y, z):
    return (x, y, z-.9+LIFT)


def top_mesh(name, verts, faces, mat):
    o = mesh(name, [top_point(*p) for p in verts], faces, mat, body)
    mod = o.modifiers.new("Fabric thickness", "SOLIDIFY")
    mod.thickness = .002
    return o


def top_tube(name, points, radius, mat=black):
    return tube(name, [top_point(*p) for p in points], radius, mat, body)


# Build connected cloth/window surfaces from shared edge coordinates.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from chassis import correct_frame_height
correct_frame_height(body, LIFT)
from spring_mounts import build_spring_mounts
build_spring_mounts(body, LIFT)
from wheel_wells import build_wheel_wells
build_wheel_wells(body, LIFT)
from hood_latches import build_hood_latches
build_hood_latches(body, bpy.data.objects["hood"], -.9+LIFT)
from soft_top import build_top
soft_top = build_top(top_mesh, top_tube, fabric, vinyl, black)
from cabin import rebuild_cabin
rebuild_cabin(body, LIFT, attach, material)

# Preserve a readable instruction sheet inside the .blend itself.
note = bpy.data.texts.new("START HERE - Jeep rig")
note.write("Select JEEP_CONTROLS > Object Properties > Custom Properties.\n"
           "Front/rear heave and roll pose the solid axles; shocks telescope and springs compress.\n"
           "Door, hood and tailgate controls range 0..1. Wheel/crank turns are independent.\n"
           "Frames 1-120 demonstrate articulation and panels. Frame 1 is rest.\n"
           "PROTOTYPE LIMITS: links stretch; steering/driveshaft articulation not solved;\n"
           "no soft-top folding; rims remain generic; Fox body style is unmeasured.\n"
           "Lift treated provisionally as 3.5-inch suspension lift. Not app-ready.\n")

# Quantify the approximation rather than calling a pose rig a mechanical twin.
def update():
    ctrl.update_tag()
    bpy.context.view_layer.update()


update()
rest = [(a.matrix_world.translation-b.matrix_world.translation).length for a,b in links]
max_stretch = 0.
shock_lengths = []
for heave in (-.07,0,.07):
    for roll in (-8.,0,8.):
        for label in ("front","rear"):
            ctrl[label+"_heave_m"] = heave
            ctrl[label+"_roll_deg"] = roll
        update()
        for (a,b), length in zip(links,rest):
            max_stretch = max(max_stretch, abs((a.matrix_world.translation-b.matrix_world.translation).length-length))
        shock_lengths += [(a.matrix_world.translation-b.matrix_world.translation).length for a,b in shocks]
for name,(value,_,_) in properties.items():
    ctrl[name] = value
update()
for frame, values in ((1,{}),(40,{"front_roll_deg":7.,"rear_roll_deg":-7.,"front_heave_m":.03}),
                      (80,{"front_roll_deg":-7.,"rear_roll_deg":7.,"rear_heave_m":.03}),
                      (120,{"driver_door":1.,"passenger_door":1.,"hood":1.,"tailgate":1.})):
    for name,(value,_,_) in properties.items():
        ctrl[name] = values.get(name, value)
        ctrl.keyframe_insert(data_path=f'["{name}"]',frame=frame)
scene.frame_end = 120
scene.frame_set(1)
update()
report = {
    "status":"prototype_built_not_mechanically_validated", "target":target,
    "implemented":["framed fabric soft top and vinyl windows", "3.5-inch sprung/unsprung separation",
                   "separate solid axle pose controls", "telescoping shocks", "compressing springs",
                   "door/hood/tailgate controls", "wheel and crank rotation controls",
                   "contoured rearward-reclining seats", "manual and transfer-case boots/levers",
                   "coaxial steering wheel/column and dash aperture", "distinct cabin dash controls"],
    "suspension_sweep":{"poses":9,"max_control_link_length_error_m":max_stretch,
                        "shock_eye_distance_range_m":[min(shock_lengths),max(shock_lengths)]},
    "remaining":["fixed-length suspension solver", "steering at travel", "driveshaft articulation",
                 "Method wheel geometry", "verified Fox dimensions", "powertrain coupling",
                 "soft top folding and removable-window controls", "Qt export/integration", "collision validation"],
    "reference":"https://www.bestop.com/wp-content/uploads/Installation%20Instructions/BDKW_51709-INS_001.pdf"
}
(args.output/"report.json").write_text(json.dumps(report,indent=2)+"\n")

# Studio fixtures remain in a separate collection and are not vehicle geometry.
studio = bpy.data.collections.new("STUDIO - exclude from export")
scene.collection.children.link(studio)
def studio_object(obj):
    for collection in list(obj.users_collection):
        collection.objects.unlink(obj)
    studio.objects.link(obj)

bpy.ops.mesh.primitive_plane_add(size=200, location=(0,0,-.894))
floor = bpy.context.object
floor.name = "Studio floor"
floor.data.materials.append(material("Studio neutral",(.075,.09,.11),0,.8))
studio_object(floor)
scene.world.use_nodes = True
scene.world.node_tree.nodes["Background"].inputs[0].default_value = (.22,.25,.3,1)
scene.world.node_tree.nodes["Background"].inputs[1].default_value = .45
for loc,power,size in (((2,-4,6),1700,5),((-4,-1,3),1200,4),((1,4,4),2000,3)):
    bpy.ops.object.light_add(type="AREA", location=loc)
    lamp = bpy.context.object
    lamp.data.energy = power
    lamp.data.shape = "DISK"
    lamp.data.size = size
    lamp.rotation_euler = (-lamp.location).to_track_quat("-Z","Y").to_euler()
    studio_object(lamp)
bpy.ops.object.camera_add(location=(4.6,-6.8,2.5))
camera = bpy.context.object
camera.rotation_euler = (Vector((0,0,.15))-camera.location).to_track_quat("-Z","Y").to_euler()
camera.data.type = "ORTHO"
camera.data.ortho_scale = 5.3
scene.camera = camera
studio_object(camera)
scene.render.engine = "CYCLES"
scene.cycles.samples = 16
scene.cycles.use_denoising = True
scene.render.resolution_x,scene.render.resolution_y = 1200,850
scene.render.resolution_percentage = 100
bpy.ops.object.select_all(action="DESELECT")
ctrl.select_set(True)
bpy.context.view_layer.objects.active = ctrl
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type == "VIEW_3D":
            area.spaces.active.region_3d.view_distance = 5.5
            area.spaces.active.region_3d.view_location = (0,0,.1)
bpy.ops.wm.save_as_mainfile(filepath=str((args.output/"jeep-twin.blend").resolve()))
if args.render:
    for label,frame,location in (("rest",1,(4.6,-6.8,2.5)),("side",1,(7,0,.65)),
                                ("articulation",40,(4.6,-6.8,1.5)),("open",120,(-4.6,6.8,2.5))):
        scene.frame_set(frame)
        camera.location = location
        camera.rotation_euler = (Vector((0,0,.1))-camera.location).to_track_quat("-Z","Y").to_euler()
        scene.render.filepath = str((args.output/(label+".png")).resolve())
        bpy.ops.render.render(write_still=True)
print("TWIN PROTOTYPE BUILT", args.output, flush=True)
