"""Build OCTAVE's original photo-referenced 2003 TJ Sahara real-time asset using Blender 4.2.

Run: blender --background --python tools/vehicle/build_jeep_tj.py
Or: python (with bpy==4.2.0 installed) tools/vehicle/build_jeep_tj.py
Optional: --preview /tmp/jeep.png --blend /tmp/jeep.blend
Coordinates: Blender X vehicle-left, -Y forward, Z up; exported glTF +Z forward,
+Y up. Metres, with an attitude pivot 0.9 m above the axle midpoint.
"""

import argparse
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Vector

args = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else [] if "--python" in sys.argv else sys.argv[1:]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--preview", type=Path)
parser.add_argument("--blend", type=Path)
parser.add_argument(
    "--output", type=Path, default=Path(__file__).resolve().parents[2] / "frontend/assets/jeep_tj_2003.glb"
)
opts = parser.parse_args(args)
steering_geometry = json.loads(Path(__file__).with_name("steering_geometry.json").read_text())
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)


def material(name, rgb, metal=0, rough=0.45):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*rgb, 1)
    m.use_nodes = True
    p = m.node_tree.nodes.get("Principled BSDF")
    p.inputs["Base Color"].default_value = (*rgb, 1)
    p.inputs["Metallic"].default_value = metal
    p.inputs["Roughness"].default_value = rough
    return m


paint = material("Silver metallic enamel", (0.45, 0.47, 0.50), 0.60, 0.27)
roof = material("Black hardtop", (0.018, 0.021, 0.025), 0, 0.58)
clear_glass = material("Windshield glass", (0.19, 0.25, 0.24), 0.10, 0.12)
clear_glass.node_tree.nodes["Principled BSDF"].inputs["Alpha"].default_value = 0.23
clear_glass.surface_render_method = "DITHERED"
black = material("Textured black trim", (0.022, 0.028, 0.032), 0, 0.72)
wheel_black = material("Satin black wheel finish", (0.016, 0.019, 0.023), 0.65, 0.32)
sidewall_letters = material("Raised black tire lettering", (0.038, 0.042, 0.046), 0, 0.80)
rubber = material("Tire rubber", (0.014, 0.018, 0.022), 0, 0.87)
steel = material("Undercarriage steel", (0.055, 0.067, 0.078), 0.65, 0.46)
alloy = material("Satin alloy", (0.48, 0.53, 0.57), 0.75, 0.27)
glass = material("Tinted hardtop glass", (0.035, 0.052, 0.049), 0.25, 0.12)
glass.node_tree.nodes["Principled BSDF"].inputs["Alpha"].default_value = 0.78
glass.surface_render_method = "DITHERED"
lens = material("Headlamp glass", (0.72, 0.83, 0.87), 0.35, 0.17)
red = material("Tail lamp red", (0.55, 0.012, 0.015), 0.15, 0.24)
amber = material("Amber marker", (0.95, 0.27, 0.015), 0.15, 0.28)
interior = material("Black upholstery", (0.024, 0.027, 0.031), 0, 0.85)
fabric = material("Seat fabric inserts", (0.038, 0.041, 0.046), 0, 0.95)


ACTIVE_PART = "body"


def tag(obj):
    obj["rig_part"] = ACTIVE_PART
    return obj


def finish(o, name, mat, bevel=0):
    o.name = name
    o.data.materials.append(mat)
    if bevel:
        mod = o.modifiers.new("Soft manufactured edges", "BEVEL")
        mod.width = bevel
        mod.segments = 1 if mat == rubber else 3
        bpy.context.view_layer.objects.active = o
        bpy.ops.object.modifier_apply(modifier=mod.name)
        mod = o.modifiers.new("Weighted panel normals", "WEIGHTED_NORMAL")
        bpy.ops.object.modifier_apply(modifier=mod.name)
    return tag(o)


BOX_CACHE = {}


def box(name, loc, size, mat, bevel=0.015):
    key = (tuple(size), mat.name, bevel)
    if key in BOX_CACHE:
        o = bpy.data.objects.new(name, BOX_CACHE[key])
        bpy.context.collection.objects.link(o)
        o.location = loc
        return tag(o)
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o = bpy.context.object
    o.dimensions = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    finish(o, name, mat, bevel)
    BOX_CACHE[key] = o.data
    return tag(o)


def cylinder(name, loc, radius, depth, mat, axis=(0, 0, 1), vertices=48):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=loc)
    o = bpy.context.object
    o.rotation_mode = "QUATERNION"
    o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(Vector(axis))
    finish(o, name, mat, 0.003)
    for p in o.data.polygons:
        p.use_smooth = len(p.vertices) == 4
    return tag(o)


def bar(name, start, end, radius, mat):
    a, b = Vector(start), Vector(end)
    return cylinder(name, (a + b) / 2, radius, (b - a).length, mat, b - a, 16)


def panel(name, verts, mat, thickness=0.018):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], [tuple(range(len(verts)))])
    mesh.update()
    o = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(o)
    o.data.materials.append(mat)
    mod = o.modifiers.new("Panel thickness", "SOLIDIFY")
    mod.thickness = thickness
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.modifier_apply(modifier=mod.name)
    return tag(o)


# Surface helpers. Photo-derived outlines use actual curves instead of stacked boxes.
def mesh_object(name, vertices, faces, mat, smooth=False):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.data.materials.append(mat)
    for poly in mesh.polygons:
        poly.use_smooth = smooth
    # Outward normals also matter on the mirrored side of the vehicle.
    import bmesh

    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(mesh)
    bm.free()
    return tag(obj)


def rounded(points, radius=0.06, steps=8):
    """Rounded polygon outline in local 2D coordinates, preserving corner order."""
    result = []
    for i, p in enumerate(points):
        p = Vector(p)
        prev = Vector(points[i - 1])
        nxt = Vector(points[(i + 1) % len(points)])
        d = min(radius, (prev - p).length * 0.45, (nxt - p).length * 0.45)
        a = p + (prev - p).normalized() * d
        b = p + (nxt - p).normalized() * d
        for j in range(steps):
            t = j / (steps - 1)
            result.append(tuple((1 - t) ** 2 * a + 2 * (1 - t) * t * p + t * t * b))
    return result


def rr(a, b, c, d, r=0.06):
    return rounded([(a, b), (c, b), (c, d), (a, d)], r)


def shell_thickness(obj, thickness=0.008):
    """Give open exterior skins an inner face and closed cut edges."""
    mod = obj.modifiers.new("Inner panel return", "SOLIDIFY")
    mod.thickness = thickness
    mod.offset = 0
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=mod.name)
    return obj


def sheet(name, outline, mapping, mat):
    return mesh_object(name, [mapping(*p) for p in outline], [tuple(range(len(outline)))], mat)


def ring(name, outer, inner, mapping, mat, smooth=False):
    assert len(outer) == len(inner)
    n = len(outer)
    return mesh_object(
        name,
        [mapping(*p) for p in outer + inner],
        [(i, (i + 1) % n, (i + 1) % n + n, i + n) for i in range(n)],
        mat,
        smooth,
    )


def path(name, points, radius, mat, closed=False):
    curve = bpy.data.curves.new(name, "CURVE")
    curve.dimensions = "3D"
    curve.resolution_u = 1
    curve.bevel_depth = radius
    curve.bevel_resolution = 2
    line = curve.splines.new("POLY")
    line.points.add(len(points) - 1)
    for p, v in zip(line.points, points, strict=True):
        p.co = (*v, 1)
    line.use_cyclic_u = closed
    o = bpy.data.objects.new(name, curve)
    bpy.context.collection.objects.link(o)
    o.data.materials.append(mat)
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.convert(target="MESH")
    return tag(bpy.context.object)


def loft(name, sections, mat, capped=True, smooth=True):
    n = len(sections[0])
    vertices = [p for row in sections for p in row]
    faces = [
        (j * n + i, j * n + (i + 1) % n, (j + 1) * n + (i + 1) % n, (j + 1) * n + i)
        for j in range(len(sections) - 1)
        for i in range(n)
    ]
    if capped:
        faces.extend([tuple(range(n - 1, -1, -1)), tuple(range((len(sections) - 1) * n, len(sections) * n))])
    return mesh_object(name, vertices, faces, mat, smooth)


def text_mesh(name, words, loc, size, mat, rotation):
    curve = bpy.data.curves.new(name, "FONT")
    curve.body = words
    curve.size = size
    curve.align_x = "CENTER"
    curve.extrude = 0.0005
    curve.resolution_u = 4
    o = bpy.data.objects.new(name, curve)
    bpy.context.collection.objects.link(o)
    o.location = loc
    o.rotation_euler = rotation
    o.data.materials.append(mat)
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.convert(target="MESH")
    tag(bpy.context.object)


# Stock short-wheelbase chassis, exposed solid axles, springs, and steering links.
print("Building chassis and body surfaces", flush=True)
for x in (-0.47, 0.47):
    box("Boxed frame rail", (x, 0, 0.39), (0.095, 3.32, 0.14), steel, 0.015)
for y in (-1.186, 1.186):
    # Dana 44: offset front carrier, centered rear carrier, angular ten-bolt cover.
    diff_x = 0.23 if y < 0 else 0
    direction = -1 if y < 0 else 1
    cylinder("Dana 44 axle tube", (0, y, 0.40), 0.034, 1.28 if y < 0 else 1.48, steel, (1, 0, 0), 32)
    cover_outline = [
        (-0.055, 0.142),
        (0.055, 0.142),
        (0.115, 0.102),
        (0.137, 0.035),
        (0.132, -0.053),
        (0.082, -0.127),
        (-0.025, -0.143),
        (-0.112, -0.107),
        (-0.14, -0.032),
        (-0.13, 0.067),
    ]
    outline = rounded(cover_outline, 0.018, 5)
    loft(
        "Dana 44 cast carrier",
        [
            [(diff_x + x * scale, y + direction * depth, 0.4 + z * scale) for x, z in outline]
            for depth, scale in [(-0.12, 0.48), (-0.07, 0.85), (0.02, 1.10), (0.105, 1.0)]
        ],
        steel,
    )
    loft(
        "Dana 44 ten bolt cover",
        [
            [(diff_x + x * scale, y + direction * depth, 0.4 + z * scale) for x, z in outline]
            for depth, scale in [(0.105, 1), (0.121, 1), (0.155, 0.78)]
        ],
        steel,
    )
    for x, z in cover_outline:
        cylinder(
            "Dana 44 cover bolt",
            (diff_x + x * 0.90, y + direction * 0.126, 0.4 + z * 0.90),
            0.008,
            0.014,
            alloy,
            (0, 1, 0),
            12,
        )
    cylinder("Dana 44 fill plug", (diff_x + 0.038, y + direction * 0.16, 0.425), 0.018, 0.014, alloy, (0, 1, 0), 6)
    cylinder("Dana 44 pinion nose", (diff_x, y - direction * 0.14, 0.375), 0.055, 0.15, steel, (0, 1, 0), 32)
    for x in (-0.52, 0.52):
        # Place dampers alongside the coils, with a continuous coaxial piston.
        lower = Vector((x + math.copysign(0.08, x), y - 0.16, 0.38))
        upper = Vector((x + math.copysign(0.03, x), y + 0.06, 0.79))
        delta = upper - lower
        bar("Shock absorber body", lower + delta * 0.07, lower + delta * 0.61, 0.029, black)
        bar("Shock chrome piston", lower + delta * 0.60, lower + delta * 0.96, 0.010, alloy)
        bar("Shock dust boot", lower + delta * 0.76, lower + delta * 0.91, 0.024, rubber)
        for t in (0.77, 0.80, 0.83, 0.86, 0.89):
            cylinder("Shock boot rib", lower + delta * t, 0.027, 0.009, rubber, delta, 20)
        for point in (lower, upper):
            cylinder("Shock eye bushing", point, 0.031, 0.060, rubber, (1, 0, 0), 24)
            cylinder("Shock mounting bolt", point, 0.013, 0.088, alloy, (1, 0, 0), 12)
            for offset in (-0.037, 0.037):
                box("Shock mounting bracket", point + Vector((offset, 0, 0.006)), (0.012, 0.078, 0.086), steel, 0.005)
        for z in (0.435, 0.745):
            cylinder("Coil spring perch", (x, y, z), 0.092, 0.018, steel, (0, 0, 1), 32)
        coil = [
            (x + 0.067 * math.cos(t * 2 * math.pi * 5), y + 0.067 * math.sin(t * 2 * math.pi * 5), 0.44 + t * 0.30)
            for t in [i / 100 for i in range(101)]
        ]
        path("Coil spring", coil, 0.009, steel)
        bar("Trailing arm", (x, y, 0.35), (x, y + (0.51 if y < 0 else -0.51), 0.47), 0.024, steel)
    bar("Driveshaft", (diff_x, y - direction * 0.20, 0.375), (0, 0, 0.43), 0.032, steel)


# Public rig geometry uses glTF/Qt coordinates; convert for the Blender source.
def from_qt(p):
    return Vector((p[0], -p[2], p[1] + 0.9))


ACTIVE_PART = "steering_links"
g = steering_geometry
left, right, pitman = [from_qt(g[k]) for k in ("driverEnd", "passengerEnd", "pitmanEnd")]
pickup = right.lerp(pitman, g["pickupFraction"])
delta = pickup - left
bow = Vector((-delta.y, delta.x, 0)).normalized() * g["tieBend"]
# Bend forward (-Y) around the differential cover.
if bow.y > 0:
    bow = -bow
knots = [left, left.lerp(pickup, 0.18) + bow, left.lerp(pickup, 0.82) + bow, pickup]
path("Bent tie rod neutral", knots, 0.016, steel)
bar("Drag link neutral", right, pitman, 0.019, steel)
bar("Pitman arm neutral", from_qt(g["pitmanPivot"]), pitman, 0.030, steel)
for point in (left, right, pitman, pickup):
    cylinder("Steering ball joint neutral", point, 0.018, 0.036, steel, vertices=24)
damper_start = from_qt(g["damperStart"])
damper_end = knots[1].lerp(knots[2], g["damperFraction"])
bar("Steering damper body neutral", damper_start, damper_start.lerp(damper_end, 0.60), 0.027, black)
bar("Steering damper piston neutral", damper_start.lerp(damper_end, 0.55), damper_end, 0.009, alloy)
ACTIVE_PART = "body"
box("Steering damper axle bracket", (-0.35, -1.186, 0.47), (0.08, 0.08, 0.13), steel, 0.008)
ACTIVE_PART = "body"
box("Steering gear box", from_qt(g["pitmanPivot"]) + Vector((0, 0, 0.06)), (0.13, 0.15, 0.15), steel, 0.015)
for sign in (-1, 1):
    for z in (0.31, 0.49):
        cylinder("Dana 44 steering ball joint", (sign * 0.64, -1.186, z), 0.024, 0.046, steel, vertices=24)

# Thin stamped pans follow the passenger tub, rather than a full-width slab.
# Keep seat mounting surfaces at .614 m while leaving room for the drivetrain.
for side in (-1, 1):
    box("Front footwell pan", (side * 0.415, 0.07, 0.600), (0.54, 1.26, 0.012), paint, 0.005)
    box("Front footwell carpet", (side * 0.415, 0.07, 0.610), (0.54, 1.26, 0.008), fabric, 0.003)
    panel(
        "Sloping toe board",
        [
            (side * 0.145, -0.56, 0.606),
            (side * 0.685, -0.56, 0.606),
            (side * 0.685, -0.632, 0.78),
            (side * 0.145, -0.632, 0.78),
        ],
        black,
        0.009,
    )
box("Rear passenger floor pan", (0, 1.085, 0.600), (1.10, 0.77, 0.012), paint, 0.005)
box("Rear passenger carpet", (0, 1.085, 0.610), (1.10, 0.77, 0.008), fabric, 0.003)
box("Raised cargo floor", (0, 1.595, 0.670), (1.10, 0.25, 0.012), paint, 0.005)
box("Cargo carpet", (0, 1.595, 0.680), (1.10, 0.25, 0.008), fabric, 0.003)
panel(
    "Cargo floor riser",
    [(-0.55, 1.47, 0.600), (0.55, 1.47, 0.600), (0.55, 1.47, 0.676), (-0.55, 1.47, 0.676)],
    black,
    0.009,
)
tunnel_sections = []
for y, height in [(-0.56, 0.77), (0.45, 0.75), (0.70, 0.606)]:
    tunnel_sections.append([(-0.145, y, 0.606), (-0.105, y, height), (0.105, y, height), (0.145, y, 0.606)])
mesh_object(
    "Raised transmission tunnel",
    [v for row in tunnel_sections for v in row],
    [(j * 4 + i, j * 4 + i + 1, (j + 1) * 4 + i + 1, (j + 1) * 4 + i) for j in range(2) for i in range(3)],
    black,
)
for side in (-1, 1):
    for y in (-0.35, 0.10, 0.55, 1.02, 1.34):
        box("Floor pan reinforcing rib", (side * 0.40, y, 0.578), (0.28, 0.030, 0.040), steel, 0.006)
        cylinder("Floor drain plug", (side * 0.35, y + 0.08, 0.592), 0.019, 0.006, rubber, vertices=24)
    for y in (-0.40, 0.50, 1.35):
        box("Body mount outrigger", (side * 0.47, y, 0.548), (0.14, 0.11, 0.105), steel, 0.008)
        cylinder("Body mount isolator", (side * 0.47, y, 0.487), 0.052, 0.054, rubber, vertices=24)
        cylinder("Body mount washer", (side * 0.47, y, 0.459), 0.048, 0.008, alloy, vertices=24)
box("Transfer case skid", (0, 0.07, 0.29), (0.88, 0.68, 0.04), steel)
box("Rear fuel tank skid", (0, 1.38, 0.41), (0.88, 0.50, 0.18), steel)
path(
    "Exhaust pipe",
    [(0.35, -0.5, 0.40), (0.38, 0.4, 0.40), (0.48, 0.8, 0.52), (0.51, 1.4, 0.38), (0.53, 1.79, 0.37)],
    0.025,
    steel,
)

# Real door apertures: the sill and rear quarter do not cover the moving doors.
for s in (-1, 1):
    edge = [
        (0.64, 1.255),
        (1.73, 1.255),
        (1.73, 0.55),
        (1.69, 0.55),
        (1.52, 0.94),
        (1.46, 0.97),
        (0.93, 0.97),
        (0.85, 0.94),
        (0.62, 0.55),
        (0.52, 0.55),
        (0.52, 0.66),
        (0.62, 0.76),
    ]
    shell_thickness(sheet("Rear quarter with wheel opening", edge, lambda y, z, s=s: (s * 0.735, y, z), paint))
    box("Tub beltline return", (s * 0.714, 1.184, 1.248), (0.046, 1.098, 0.010), paint, 0.003)
    # Rolled rear corner joins the side skin to the narrower tail-lamp panel.
    corner = []
    for z in (0.554, 1.253):
        corner.append(
            [
                (s * (0.689 + 0.046 * math.cos(a)), 1.727 + 0.038 * math.sin(a), z)
                for a in [math.pi * i / 24 for i in range(13)]
            ]
        )
    ncorner = len(corner[0])
    shell_thickness(
        mesh_object(
            "Tub rear corner return",
            corner[0] + corner[1],
            [(i, i + 1, ncorner + i + 1, ncorner + i) for i in range(ncorner - 1)],
            paint,
            True,
        )
    )
    box("Door aperture sill", (s * 0.718, 0.12, 0.605), (0.07, 1.01, 0.085), paint, 0.012)
    box("Rocker rail", (s * 0.705, 0.02, 0.555), (0.085, 1.21, 0.085), paint, 0.025)
    # Wheel housings rise from the narrowed rear floor and stay clear of the tires.
    well = [(0.66, 0.615), (0.86, 0.90), (0.94, 0.94), (1.45, 0.94), (1.55, 0.90), (1.72, 0.615)]
    sheet("Rear wheel housing inner wall", well, lambda y, z, s=s: (s * 0.55, y, z), black)
    n = len(well)
    mesh_object(
        "Rear wheel housing roof",
        [(s * x, y, z) for x in (0.55, 0.735) for y, z in well],
        [(i, i + 1, n + i + 1, n + i) for i in range(n - 1)],
        black,
    )
    box("Tailgate side post", (s * 0.59, 1.733, 0.916), (0.25, 0.065, 0.66), paint, 0.018)
box("Tailgate sill", (0, 1.733, 0.601), (0.94, 0.065, 0.055), paint, 0.009)
ACTIVE_PART = "tailgate"
box("Tailgate shell", (0, 1.755, 0.925), (0.897, 0.058, 0.568), paint, 0.012)
ACTIVE_PART = "body"

# Boxed crossmembers, drivetrain casings, fasteners and an exhaust silencer.
for y in (-1.53, -0.58, 0.48, 1.60):
    box("Frame crossmember", (0, y, 0.425), (0.99, 0.082, 0.09), steel, 0.009)
for name, loc, size in [
    ("Engine oil pan", (0, -1.05, 0.53), (0.35, 0.54, 0.18)),
    ("Transmission casing", (0, -0.38, 0.50), (0.29, 0.62, 0.22)),
    ("Transfer case", (0.09, 0.07, 0.49), (0.43, 0.30, 0.22)),
    ("Exhaust catalytic converter", (0.35, -0.39, 0.39), (0.15, 0.28, 0.12)),
    ("Exhaust muffler", (0.40, 0.64, 0.40), (0.23, 0.37, 0.14)),
]:
    box(name, loc, size, alloy, 0.035)
for x in (-0.38, 0, 0.38):
    box("Skid reinforcing rib", (x, 0.07, 0.262), (0.028, 0.60, 0.014), steel, 0.004)
for x in (-0.405, 0.405):
    for y in (-0.20, 0.07, 0.34):
        cylinder("Skid mounting bolt", (x, y, 0.264), 0.012, 0.012, alloy, vertices=12)
for y in (-1.186, 1.186):
    dx = 0.23 if y < 0 else 0
    direction = -1 if y < 0 else 1
    for t in (0.08, 0.90):
        point = Vector((dx, y - direction * 0.20, 0.375)).lerp(Vector((0, 0, 0.43)), t)
        cylinder("Driveshaft universal joint", point, 0.050, 0.08, steel, (1, 0, 0), 16)


# Broad shallow deck with a small rolled shoulder and a straight vertical skirt.
def hood_cross_section(y, width, height):
    radius = 0.045
    half = [(width, y, 1.105)]
    for i in range(9):
        angle = math.pi * i / 16
        half.append((width - radius + radius * math.cos(angle), y, height - 0.012 - radius + radius * math.sin(angle)))
    for i in range(1, 9):
        x = (width - radius) * (1 - i / 8)
        half.append((x, y, height - 0.012 * (x / (width - radius)) ** 2))
    return half + [(-x, y, z) for x, y, z in reversed(half[:-1])]


hood_sections = [
    hood_cross_section(y, w, h)
    for y, w, h in [
        (-1.715, 0.558, 1.265),
        (-1.67, 0.567, 1.295),
        (-1.50, 0.595, 1.313),
        (-0.81, 0.714, 1.32),
        (-0.64, 0.732, 1.31),
    ]
]
ACTIVE_PART = "hood"
n = len(hood_sections[0])
hood = mesh_object(
    "Crowned tapered hood",
    [v for row in hood_sections for v in row],
    [
        (j * n + i, j * n + i + 1, (j + 1) * n + i + 1, (j + 1) * n + i)
        for j in range(len(hood_sections) - 1)
        for i in range(n - 1)
    ],
    paint,
    True,
)
mod = hood.modifiers.new("Hood sheet thickness", "SOLIDIFY")
mod.thickness = 0.012
bpy.context.view_layer.objects.active = hood
bpy.ops.object.modifier_apply(modifier=mod.name)
for x in (-0.34, 0.34):
    bar("Hood inner brace", (x, -1.58, 1.24), (x, -0.72, 1.25), 0.018, steel)
ACTIVE_PART = "body"
# The cowl is full tub width, with flat sides flush to the door jamb.
# Keep only the shallow top roll here; the side sheet below is continuous.
cowl_sections = [hood_cross_section(y, 0.735, 1.307)[1:-1] for y in (-0.632, -0.37)]
n = len(cowl_sections[0])
mesh_object(
    "Shallow cowl top",
    [v for row in cowl_sections for v in row],
    [(i, i + 1, n + i + 1, n + i) for i in range(n - 1)],
    paint,
    True,
)
# A real firewall closes the cabin when the hood is lifted.
panel(
    "Cowl firewall",
    [(-0.735, -0.632, 0.56), (0.735, -0.632, 0.56), (0.735, -0.632, 1.250), (-0.735, -0.632, 1.250)],
    paint,
    0.012,
)
ACTIVE_PART = "hood"
for x in (-0.25, 0.25):
    box("Hood rubber stop", (x, -0.77, 1.325), (0.035, 0.035, 0.022), rubber, 0.009)
    path(
        "Windshield tie down loop",
        [(x - 0.028, -0.91, 1.322), (x - 0.028, -0.91, 1.351), (x + 0.028, -0.91, 1.351), (x + 0.028, -0.91, 1.322)],
        0.006,
        steel,
    )
ACTIVE_PART = "body"
for x in [i * 0.022 for i in range(-11, 12)]:
    box("Cowl intake louver", (x, -0.523, 1.309), (0.007, 0.105, 0.009), black, 0.003)

# Inner engine-bay sides close the gap behind the front fender shoulders.
box("Engine block silhouette", (0, -1.10, 0.865), (0.86, 0.96, 0.40), black, 0.035)
for s in (-1, 1):
    sheet(
        "Engine bay side panel",
        [(-1.70, 1.107), (-0.630, 1.107), (-0.630, 0.60), (-0.90, 0.97), (-1.70, 1.01)],
        lambda y, z, s=s: (s * min(0.735, 0.558 + 0.174 * (y + 1.70) / 1.06), y, z),
        paint,
    )
    sheet(
        "Cowl lower side panel",
        [(-0.632, 1.256), (-0.37, 1.256), (-0.37, 0.56), (-0.53, 0.56), (-0.632, 0.60)],
        lambda y, z, s=s: (s * 0.735, y, z),
        paint,
    )

# Explicit slot rings avoid overlapping Boolean faces in the runtime importer.
for x in [i * 0.081 for i in range(-3, 4)]:
    outer = rr(x - 0.0405, 0.765, x + 0.0405, 1.25, 0)
    inner = rr(x - 0.025, 0.793, x + 0.025, 1.202, 0.024)
    ring("Stamped grille slot surround", outer, inner, lambda u, v: (u, -1.722, v), paint)
    loft(
        "Recessed grille slot wall",
        [[(u, y, v) for u, v in inner] for y in (-1.722, -1.684)],
        steel,
        capped=False,
        smooth=False,
    )
for side in (-1, 1):
    outline = [
        (side * 0.2835, 0.765),
        (side * 0.54, 0.765),
        (side * 0.565, 1.115),
        (side * 0.51, 1.25),
        (side * 0.2835, 1.25),
    ]
    panel("Grille headlamp side panel", [(x, -1.722, z) for x, z in outline], paint, 0.022)
box("Radiator behind open grille", (0, -1.655, 0.995), (0.63, 0.027, 0.455), black, 0.01)
for x in [i * 0.013 for i in range(-23, 24)]:
    box("Radiator core fin", (x, -1.677, 0.997), (0.003, 0.005, 0.385), black, 0)

# Flat-top front fenders roll down around the tire; rear flares have a trapezoid opening.
for s in (-1, 1):
    front = [(-1.80, 0.90), (-1.76, 1.045), (-1.60, 1.083), (-1.04, 1.083), (-0.91, 1.055), (-0.54, 0.58)]
    rear = [(0.58, 0.56), (0.81, 1.00), (0.91, 1.045), (1.49, 1.045), (1.59, 1.00), (1.73, 0.56)]
    for name, profile, basewidth in [("Front flare", front, 0.61), ("Rear flare", rear, 0.735)]:
        profile = [profile[0]] + rounded(profile, 0.072, 7)[7:-7] + [profile[-1]]
        rows = []
        for y, z in profile:
            # Match the tapered engine-bay wall, avoiding a gap inside the flare.
            inner_width = basewidth
            if name == "Front flare":
                inner_width = min(0.725, 0.548 + 0.174 * (y + 1.70) / 1.06)
            # Full-thickness rolled lip; taper the heels into the tub instead of pointed ends.
            heel = max(0, min(1, (z - 0.58) / 0.38))
            outer_width = 0.785 + 0.09 * heel
            rows.append(
                [
                    (s * inner_width, y, z),
                    (s * (outer_width - 0.045), y, z + 0.006),
                    (s * outer_width, y, z - 0.018),
                    (s * outer_width, y, z - 0.065),
                    (s * (outer_width - 0.012), y, z - 0.072),
                    (s * (outer_width - 0.048), y, z - 0.010),
                    (s * inner_width, y, z - 0.016),
                ]
            )
        loft(name, rows, black, capped=True)
        path(
            name + " lower trim",
            [(s * (0.773 + 0.09 * max(0, min(1, (z - 0.58) / 0.38))), y, z - 0.070) for y, z in profile],
            0.009,
            black,
        )
    box("Front marker housing", (s * 0.722, -1.755, 1.00), (0.198, 0.039, 0.129), black, 0.024)
    box("Front amber lamp", (s * 0.722, -1.778, 1.004), (0.160, 0.013, 0.090), amber, 0.012)
    for xoff in [i * 0.011 for i in range(-6, 7)]:
        box("Amber lens rib", (s * 0.722 + xoff, -1.788, 1.004), (0.002, 0.002, 0.072), lens, 0)
    sheet(
        "Side amber marker",
        rounded([(-1.68, 0.932), (-1.66, 1.025), (-1.56, 1.025), (-1.595, 0.932)], 0.015),
        lambda y, z, s=s: (s * 0.879, y, z),
        amber,
    )
    box("Hood latch base", (s * 0.591, -1.51, 1.117), (0.029, 0.054, 0.042), black, 0.006)
    ACTIVE_PART = "hood"
    box("Hood latch strap", (s * 0.594, -1.51, 1.16), (0.026, 0.029, 0.102), black, 0.006)
    cylinder("Latch hinge", (s * 0.61, -1.51, 1.194), 0.012, 0.022, steel, (1, 0, 0), 16)
    ACTIVE_PART = "body"

# Headlamps are inset into the narrow grille, with fog lamps above the bumper.
for s in (-1, 1):
    x = s * 0.423
    cylinder("Painted headlamp bucket", (x, -1.737, 1.097), 0.133, 0.038, paint, (0, 1, 0), 64)
    cylinder("Headlamp chrome ring", (x, -1.76, 1.097), 0.118, 0.016, alloy, (0, 1, 0), 64)
    cylinder("Headlamp dark seal", (x, -1.773, 1.097), 0.107, 0.012, black, (0, 1, 0), 64)
    # Convex lens rather than a flat opaque disk.
    sections = []
    for depth, radius in [(-1.78, 0.103), (-1.788, 0.099), (-1.795, 0.079), (-1.799, 0.035)]:
        sections.append(
            [
                (x + radius * math.cos(a * 2 * math.pi / 64), depth, 1.097 + radius * math.sin(a * 2 * math.pi / 64))
                for a in range(64)
            ]
        )
    loft("Convex sealed beam lens", sections, lens)
    for dx in [i * 0.013 for i in range(-6, 7)]:
        half = math.sqrt(0.088**2 - dx**2)
        path("Headlamp optical flute", [(x + dx, -1.801, 1.097 - half), (x + dx, -1.801, 1.097 + half)], 0.0013, alloy)
    for dz in (-0.048, -0.018, 0.018, 0.048):
        half = math.sqrt(0.087**2 - dz**2)
        path(
            "Headlamp horizontal prism", [(x - half, -1.802, 1.097 + dz), (x + half, -1.802, 1.097 + dz)], 0.001, alloy
        )
    cylinder("Fog lamp shell", (s * 0.495, -1.815, 0.77), 0.092, 0.071, black, (0, 1, 0), 48)
    cylinder("Fog lamp rim", (s * 0.495, -1.856, 0.77), 0.081, 0.012, alloy, (0, 1, 0), 48)
    cylinder("Fog lamp lens", (s * 0.495, -1.867, 0.77), 0.073, 0.015, lens, (0, 1, 0), 48)
    box("Fog light foot", (s * 0.495, -1.805, 0.674), (0.055, 0.065, 0.09), steel, 0.009)
box("Front bumper channel", (0, -1.82, 0.594), (1.70, 0.18, 0.135), steel, 0.016)
box("Front center plate", (0, -1.918, 0.60), (0.68, 0.012, 0.136), black, 0.006)
for s in (-1, 1):
    box("Bumper end cap", (s * 0.803, -1.83, 0.594), (0.145, 0.198, 0.154), black, 0.022)
    box("Front bumper rubber buffer", (s * 0.409, -1.92, 0.60), (0.13, 0.068, 0.17), black, 0.019)
    path("Factory tow hook", [(s * 0.46, -1.77, 0.66), (s * 0.46, -1.86, 0.71), (s * 0.46, -1.85, 0.752)], 0.017, steel)
for x in (-0.29, 0.29):
    cylinder("Plate mounting screw", (x, -1.928, 0.633), 0.004, 0.006, alloy, (0, 1, 0), 12)
box("Front frame cover", (0, -1.74, 0.726), (0.68, 0.22, 0.043), black, 0.009)
text_mesh("Front Jeep emboss", "Jeep", (0, -1.857, 0.719), 0.059, black, (math.pi / 2, 0, 0))
box("Rear bumper channel", (0, 1.81, 0.575), (1.67, 0.18, 0.145), black, 0.020)
box("Receiver hitch", (0, 1.86, 0.467), (0.11, 0.17, 0.11), steel, 0.01)

# Beltline now meets the hood shoulder. Door seams and windows follow the reference outlines.
for s in (-1, 1):

    def side(y, z, s=s):
        return (s * (0.735 - 0.08 * max(0, z - 1.25)), y, z)

    door = rounded([(-0.38, 0.68), (-0.38, 1.26), (0.64, 1.26), (0.62, 0.76), (0.52, 0.66), (-0.29, 0.66)], 0.06)
    # Open U-shaped jamb: a closed door contour would leave a bar across the entry.
    jamb = [(0.64, 1.255), (0.62, 0.76), (0.52, 0.66), (-0.29, 0.66), (-0.38, 0.68), (-0.38, 1.255)]
    jamb = [jamb[0]] + rounded(jamb, 0.06)[8:-8] + [jamb[-1]]
    n = len(jamb)
    mesh_object(
        "Door opening jamb",
        [(s * x, y, z) for x in (0.692, 0.734) for y, z in jamb],
        [(i, i + 1, n + i + 1, n + i) for i in range(n - 1)],
        paint,
    )
    # Recessed flange backs the shut line without spanning the entry opening.
    outside = []
    for y, z in jamb:
        delta = Vector((y - 0.12, z - 1.25)).normalized() * 0.025
        outside.append((y + delta.x, z + delta.y))
    shell_thickness(
        mesh_object(
            "Door aperture recessed flange",
            [(s * 0.692, y, z) for y, z in jamb + outside],
            [(i, i + 1, n + i + 1, n + i) for i in range(n - 1)],
            paint,
        ),
        0.006,
    )
    path("Door aperture weatherstrip", [(s * 0.695, y, z) for y, z in jamb], 0.007, rubber)
    ACTIVE_PART = "driver_door" if s == 1 else "passenger_door"
    loft("Door shell", [[(s * x, y, z) for y, z in door] for x in (0.699, 0.741)], paint, smooth=False)
    path("Door shut line", [(s * 0.745, y, z) for y, z in door], 0.0025, black, True)
    outer = rounded([(-0.455, 1.256), (-0.185, 1.775), (0.54, 1.775), (0.66, 1.68), (0.65, 1.256)], 0.055)
    inner = rounded([(-0.394, 1.285), (-0.150, 1.727), (0.514, 1.727), (0.604, 1.655), (0.602, 1.285)], 0.055)
    frame = ring("Full door upper frame", outer, inner, side, paint)
    mod = frame.modifiers.new("Door window frame thickness", "SOLIDIFY")
    mod.thickness = 0.025
    bpy.context.view_layer.objects.active = frame
    bpy.ops.object.modifier_apply(modifier=mod.name)
    path("Door window seal", [side(y, z) for y, z in inner], 0.010, black, True)
    # The reference has both door windows lowered, exposing the cabin.
    box("Window sill", (s * 0.736, 0.14, 1.259), (0.045, 0.94, 0.015), black, 0.006)
    handle = rr(0.395, 1.09, 0.544, 1.198, 0.028)
    sheet("Recessed paddle handle surround", handle, lambda y, z, s=s: (s * 0.749, y, z), black)
    sheet("Paddle handle face", rr(0.454, 1.106, 0.522, 1.18, 0.009), lambda y, z, s=s: (s * 0.754, y, z), steel)
    cylinder("Door key cylinder", (s * 0.754, 0.564, 1.108), 0.012, 0.007, alloy, (1, 0, 0), 24)
    for z in (0.79, 1.135):
        hinge = rounded(
            [(-0.407, z - 0.027), (-0.407, z + 0.027), (-0.28, z + 0.017), (-0.255, z), (-0.28, z - 0.017)], 0.008
        )
        sheet("Exposed triangular door hinge", hinge, lambda y, z, s=s: (s * 0.751, y, z), paint)
        cylinder("Door hinge pin", (s * 0.767, -0.394, z), 0.010, 0.059, steel, (0, 0, 1), 16)
        for y in (-0.35, -0.30):
            cylinder("Hinge screw", (s * 0.757, y, z), 0.0038, 0.004, alloy, (1, 0, 0), 12)
    box("Mirror mounting bracket", (s * 0.776, -0.361, 1.238), (0.070, 0.075, 0.095), black, 0.012)
    path("Mirror stalk", [(s * 0.78, -0.36, 1.255), (s * 0.86, -0.42, 1.32), (s * 0.90, -0.42, 1.39)], 0.014, steel)
    box("Rounded mirror shell", (s * 0.911, -0.437, 1.44), (0.17, 0.080, 0.217), black, 0.042)
    box("Mirror reflective face", (s * 0.911, -0.39, 1.44), (0.137, 0.009, 0.185), alloy, 0.027)
    ACTIVE_PART = "body"
    box("Molded side step", (s * 0.80, 0.095, 0.526), (0.188, 0.99, 0.055), black, 0.022)
    for y in [i * 0.022 for i in range(-17, 22)]:
        box("Side step traction rib", (s * 0.80, y, 0.557), (0.155, 0.009, 0.006), rubber, 0.002)
    if s == 1:
        cylinder("Fuel filler bezel", (s * 0.749, 1.635, 1.132), 0.079, 0.014, black, (1, 0, 0), 48)
        cylinder("Fuel filler cap", (s * 0.761, 1.635, 1.132), 0.052, 0.024, steel, (1, 0, 0), 40)
        box("Fuel cap grip", (s * 0.782, 1.635, 1.132), (0.014, 0.018, 0.084), black, 0.007)
    # Model identification in geometry, without projecting any listing photography.
    text_mesh("Jeep side emblem", "Jeep", (s * 0.75, -0.53, 0.77), 0.048, alloy, (math.pi / 2, 0, s * math.pi / 2))
    text_mesh("Sahara nameplate", "SAHARA", (s * 0.75, -0.53, 0.89), 0.020, alloy, (math.pi / 2, 0, s * math.pi / 2))


# Windshield is a broad flat pane in a rounded steel frame, slightly raked aft.
def wind(x, z):
    return (x, -0.49 + (z - 1.275) * 0.52, z)


wout = rounded([(-0.734, 1.285), (0.734, 1.285), (0.691, 1.789), (-0.691, 1.789)], 0.055)
win = rounded([(-0.668, 1.355), (0.668, 1.355), (0.638, 1.744), (-0.638, 1.744)], 0.050)
shell_thickness(ring("Windshield frame", wout, win, wind, paint), 0.018)
# Side returns bridge the raked windshield to the door's front edge.
for s in (-1, 1):
    rows = []
    seal = []
    for i in range(17):
        t = i / 16
        z = 1.286 + 0.489 * t
        front_x = 0.734 - 0.043 * (z - 1.285) / 0.504
        front_y = wind(front_x, z)[1]
        back_y = -0.455 + (z - 1.256) * 0.270 / 0.519 - 0.004
        back_x = 0.735 - 0.08 * (z - 1.25) - 0.006
        rows.append([(s * front_x, front_y, z), (s * back_x, back_y, z)])
        seal.append((s * back_x, back_y, z))
    shell_thickness(
        mesh_object(
            "Windshield pillar side return",
            [p for row in rows for p in row],
            [(2 * i, 2 * i + 1, 2 * i + 3, 2 * i + 2) for i in range(len(rows) - 1)],
            paint,
        ),
        0.008,
    )
    path("Front door pillar seal", seal, 0.006, rubber)
path(
    "Windshield cowl bedding seal", [(-0.695, -0.482, 1.292), (0, -0.482, 1.292), (0.695, -0.482, 1.292)], 0.009, rubber
)
path("Windshield outer seal", [wind(x, z) for x, z in wout], 0.007, black, True)
path("Windshield glass gasket", [wind(x, z) for x, z in win], 0.011, black, True)
sheet("Laminated windshield", win, lambda x, z: (x, wind(x, z)[1] - 0.003, z), clear_glass)
for x in (-0.36, 0.28):
    path("Windshield wiper arm", [(x, -0.503, 1.303), (x - 0.20, -0.477, 1.405)], 0.007, steel)
    path("Wiper blade", [(x - 0.41, -0.47, 1.405), (x - 0.02, -0.47, 1.405)], 0.009, black)
for x in (-0.66, 0.66):
    box("Windshield hinge block", (x, -0.481, 1.305), (0.073, 0.067, 0.043), paint, 0.01)
bar("Radio antenna", (-0.787, -0.53, 1.17), (-0.787, -0.53, 1.89), 0.0022, steel)

# Removable black hardtop: tapered shell, large rounded corner windows, rolled roof edge.
for s in (-1, 1):

    def mapping(y, z, s=s):
        return (s * (0.750 - 0.115 * (z - 1.25)), y, z)

    outer = rounded([(0.658, 1.25), (1.749, 1.25), (1.675, 1.79), (0.610, 1.79)], 0.070)
    inner = rounded([(0.728, 1.329), (1.637, 1.329), (1.590, 1.717), (0.720, 1.717)], 0.11)
    shell_thickness(ring("Hardtop quarter shell", outer, inner, mapping, roof), 0.010)
    path("Quarter glass rubber gasket", [mapping(y, z) for y, z in inner], 0.007, black, True)
    sheet(
        "Tinted quarter window", inner, lambda y, z, mapping=mapping, s=s: (mapping(y, z)[0] + s * 0.002, y, z), glass
    )
    # Front header/sill and B-pillar surround the open door aperture.
    path(
        "Roof drip gutter", [(s * 0.697, -0.20, 1.774), (s * 0.707, 0.58, 1.779), (s * 0.697, 1.62, 1.775)], 0.009, roof
    )
    path("Hardtop mounting seam", [(s * 0.751, 0.65, 1.25), (s * 0.751, 1.72, 1.25)], 0.0025, black)
# Roof uses transverse curved sections, continuous rounded edges, and a subtle crown.
sections = []
for y, w, z in [
    (-0.275, 0.680, 1.792),
    (-0.22, 0.707, 1.824),
    (0.10, 0.712, 1.836),
    (1.48, 0.712, 1.835),
    (1.63, 0.710, 1.839),
    (1.735, 0.690, 1.828),
]:
    section = []
    for i in range(41):
        t = -1 + 2 * i / 40
        section.append((w * t, y, z - 0.057 * abs(t) ** 8))
    section.extend([(w, y, z - 0.070), (-w, y, z - 0.070)])
    sections.append(section)
loft("Hardtop continuous rounded roof", sections, roof)
for x in (-0.49, -0.245, 0, 0.245, 0.49):
    path(
        "Roof molded reinforcement rib",
        [(x, -0.07, 1.834), (x, 0.12, 1.838), (x, 1.38, 1.837), (x, 1.49, 1.831)],
        0.0025,
        roof,
    )


def rear_map(x, z):
    return (x, 1.769 - 0.13 * (z - 1.25), z)


outer = rounded([(-0.746, 1.25), (0.746, 1.25), (0.685, 1.78), (-0.685, 1.78)], 0.085)
inner = rounded([(-0.653, 1.277), (0.653, 1.277), (0.594, 1.723), (-0.594, 1.723)], 0.067)
shell_thickness(ring("Hardtop rear surround", outer, inner, rear_map, roof), 0.010)
# Recessed curved returns close the separated side/rear hardtop skins, including
# their rounded lower corners. They stay outside both glass apertures.
for s in (-1, 1):
    rows = []
    for i in range(17):
        z = 1.247 + (1.776 - 1.247) * i / 16
        width = 0.750 - 0.115 * (z - 1.25) - 0.003
        back = rear_map(0, z)[1] - 0.003
        rows.append(
            [
                (s * (width - 0.080 + 0.080 * math.cos(a)), back - 0.118 + 0.118 * math.sin(a), z)
                for a in [math.pi * j / 24 for j in range(13)]
            ]
        )
    ncorner = len(rows[0])
    shell_thickness(
        mesh_object(
            "Hardtop rear corner return",
            [p for row in rows for p in row],
            [
                (j * ncorner + i, j * ncorner + i + 1, (j + 1) * ncorner + i + 1, (j + 1) * ncorner + i)
                for j in range(len(rows) - 1)
                for i in range(ncorner - 1)
            ],
            roof,
            True,
        ),
        0.008,
    )
ACTIVE_PART = "rear_glass"
sheet("Rear liftgate glass", inner, rear_map, glass)
path("Rear glass seal", [rear_map(x, z) for x, z in inner], 0.008, black, True)
ACTIVE_PART = "body"
for x in (-0.40, 0.40):
    box("Liftglass hinge", (x, 1.717, 1.702), (0.048, 0.038, 0.129), black, 0.012)
ACTIVE_PART = "rear_glass"
path("Rear wiper", [(0.28, 1.743, 1.55), (-0.07, 1.742, 1.656), (-0.37, 1.742, 1.64)], 0.009, black)

ACTIVE_PART = "body"
# Rear lamps, hinges, tailgate handle, and a proper high-mounted brake light bracket.
for s in (-1, 1):
    box("Tail lamp base", (s * 0.61, 1.778, 1.018), (0.172, 0.065, 0.214), black, 0.010)
    box("Red tail lamp lens", (s * 0.61, 1.817, 1.045), (0.149, 0.038, 0.145), red, 0.009)
    box("Reverse light lens", (s * 0.61, 1.837, 0.951), (0.136, 0.011, 0.025), lens, 0.004)
    for xoff in (-0.055, -0.028, 0, 0.028, 0.055):
        box("Tail lamp optical rib", (s * 0.61 + xoff, 1.838, 1.045), (0.003, 0.004, 0.12), red, 0.001)
ACTIVE_PART = "tailgate"
box("Tailgate latch", (0.30, 1.815, 1.13), (0.12, 0.021, 0.07), black, 0.010)
for z in (0.82, 1.15):
    box("Tailgate hinge", (-0.455, 1.789, z), (0.13, 0.024, 0.035), paint, 0.006)
box("Tailgate pressure vent", (0, 1.797, 0.814), (0.39, 0.012, 0.17), black, 0.014)
for z in (0.755, 0.792, 0.83, 0.866):
    box("Pressure vent louver", (0, 1.806, z), (0.36, 0.012, 0.014), black, 0.004)
ACTIVE_PART = "body"
box("License bracket", (-0.60, 1.82, 0.784), (0.19, 0.04, 0.113), black, 0.009)
ACTIVE_PART = "tailgate"
box("Spare carrier mount", (0, 1.85, 1.03), (0.30, 0.12, 0.27), steel, 0.02)
box("Third brake light stalk", (0, 1.847, 1.315), (0.055, 0.06, 0.38), black, 0.009)
box("Third brake light", (0, 1.87, 1.49), (0.20, 0.052, 0.055), red, 0.010)

ACTIVE_PART = "body"
# Black cabin visible through the lowered door windows and tinted windshield.
print("Building interior and KO2 wheels", flush=True)
for x in (-0.36, 0.36):
    # Mounts connect the carpet/floor at .615 m to the cushion underside at .685 m.
    for offset in (-0.145, 0.145):
        rail_x = x + offset
        box("Front seat slide rail", (rail_x, 0.12, 0.68), (0.046, 0.43, 0.03), steel, 0.006)
        for y in (-0.055, 0.295):
            box("Front seat floor foot", (rail_x, y, 0.618), (0.09, 0.085, 0.018), steel, 0.005)
            box("Front seat pedestal bracket", (rail_x, y, 0.65), (0.035, 0.048, 0.062), black, 0.005)
            for bolt_x in (-0.028, 0.028):
                cylinder("Seat anchor bolt", (rail_x + bolt_x, y, 0.630), 0.008, 0.009, alloy, vertices=12)
    bar("Seat adjustment handle", (x - 0.14, -0.105, 0.68), (x + 0.14, -0.105, 0.68), 0.008, black)
    box("Seat base", (x, 0.12, 0.76), (0.43, 0.50, 0.15), interior, 0.065)
    back = box("Seat back", (x, 0.345, 1.046), (0.43, 0.16, 0.52), interior, 0.070)
    back.rotation_euler.x = math.radians(7)
    box("Integrated headrest", (x, 0.38, 1.359), (0.235, 0.14, 0.24), interior, 0.070)
    box("Seat center fabric insert", (x, 0.247, 1.064), (0.29, 0.022, 0.345), fabric, 0.043)
    box("Seat cushion fabric insert", (x, 0.07, 0.841), (0.30, 0.34, 0.017), fabric, 0.030)
    for side in (-1, 1):
        box("Seat side bolster", (x + side * 0.175, 0.15, 0.866), (0.077, 0.37, 0.068), interior, 0.030)
# Rear bench support legs and cross rails remain attached to the fixed body.
for x in (-0.43, 0.43):
    box("Rear bench support rail", (x, 1.12, 0.735), (0.048, 0.35, 0.03), steel, 0.006)
    for y in (0.985, 1.255):
        box("Rear bench floor foot", (x, y, 0.618), (0.105, 0.09, 0.018), steel, 0.005)
        box("Rear bench support leg", (x, y, 0.68), (0.04, 0.055, 0.125), black, 0.006)
        for offset in (-0.034, 0.034):
            cylinder("Rear bench anchor bolt", (x + offset, y, 0.63), 0.008, 0.009, alloy, vertices=12)
box("Rear bench seat", (0, 1.12, 0.819), (1.06, 0.42, 0.17), interior, 0.055)
box("Rear bench back", (0, 1.416, 1.067), (1.06, 0.15, 0.41), interior, 0.06)
# Console sequence: shifter, two recessed cups, then the armrest/storage bin.
box("Shifter console", (0, -0.06, 0.81), (0.24, 0.25, 0.20), interior, 0.025)
box("Cupholder console base", (0, 0.2175, 0.75), (0.24, 0.29, 0.12), interior, 0.014)
for x in (-0.115, 0.115):
    box("Cupholder console side", (x, 0.2175, 0.849), (0.01, 0.29, 0.088), interior, 0.004)
box("Console rear bridge", (0, 0.376, 0.848), (0.24, 0.027, 0.09), interior, 0.004)
box("Console storage bin", (0, 0.565, 0.82), (0.24, 0.35, 0.30), interior, 0.025)
box("Center armrest", (0, 0.57, 1.022), (0.26, 0.35, 0.105), interior, 0.045)
angles = sorted(
    set(
        [i * math.tau / 48 for i in range(48)]
        + [math.atan2(y, x) % math.tau for x in (-0.12, 0.12) for y in (-0.0725, 0.0725)]
    )
)
for y in (0.145, 0.290):
    outer = []
    inner = []
    for a in angles:
        c, t = math.cos(a), math.sin(a)
        r = min(0.12 / max(abs(c), 1e-9), 0.0725 / max(abs(t), 1e-9))
        outer.append((r * c, y + r * t))
        inner.append((0.051 * c, y + 0.051 * t))
    ring("Cupholder console deck", outer, inner, lambda x, y: (x, y, 0.893), interior)
    loft(
        "Recessed cupholder liner",
        [[(r * math.cos(a), y + r * math.sin(a), z) for a in angles] for r, z in [(0.051, 0.893), (0.041, 0.818)]],
        black,
        capped=False,
    )
    cylinder("Cupholder bottom", (0, y, 0.815), 0.042, 0.008, rubber, vertices=48)
    path("Cupholder lip", [(0.054 * math.cos(a), y + 0.054 * math.sin(a), 0.894) for a in angles], 0.003, black, True)
bar("Parking brake lever", (0.095, 0.35, 0.82), (0.095, 0.13, 0.96), 0.013, steel)
bar("Parking brake grip", (0.095, 0.22, 0.902), (0.095, 0.13, 0.96), 0.021, black)
box("Dashboard upper", (0, -0.28, 1.213), (1.33, 0.30, 0.125), black, 0.045)
box("Dashboard lower", (0, -0.30, 1.084), (1.29, 0.23, 0.23), interior, 0.035)
box("Center dash stack", (0, -0.159, 1.13), (0.31, 0.04, 0.25), black, 0.012)
for x in (-0.105, 0.105):
    for z in (1.06, 1.13, 1.20):
        box("Center dash control", (x, -0.131, z), (0.053, 0.018, 0.015), steel, 0.004)
for x in (-0.55, 0.50):
    box("Rectangular dashboard vent", (x, -0.122, 1.212), (0.16, 0.020, 0.064), black, 0.008)
    for z in (1.191, 1.208, 1.225):
        box("Vent slat", (x, -0.107, z), (0.141, 0.007, 0.003), steel, 0)
# Driver instrument binnacle and pedals on vehicle-left (+X).
box("Driver instrument binnacle", (0.36, -0.145, 1.239), (0.40, 0.055, 0.16), black, 0.025)
for x, z, r in [
    (0.275, 1.245, 0.044),
    (0.445, 1.245, 0.044),
    (0.205, 1.265, 0.022),
    (0.355, 1.285, 0.022),
    (0.385, 1.285, 0.022),
    (0.515, 1.265, 0.022),
]:
    cylinder("Instrument dial bezel", (x, -0.110, z), r, 0.012, alloy, (0, 1, 0), 32)
    cylinder("Instrument dial face", (x, -0.101, z), r * 0.87, 0.005, black, (0, 1, 0), 32)
    bar("Instrument needle", (x, -0.096, z), (x + r * 0.45, -0.096, z + r * 0.4), 0.0018, lens)
# Hanging pedals sit forward under the dash, clear of the seat and door entry.
# Vehicle-left is +X: clutch, brake, accelerator run from +X toward the tunnel.
box("Pedal hanger bracket", (0.365, -0.402, 0.957), (0.37, 0.045, 0.055), steel, 0.006)
for name, x, y, z, width, height in [
    ("Clutch", 0.52, -0.475, 0.765, 0.075, 0.075),
    ("Brake", 0.37, -0.475, 0.765, 0.075, 0.075),
    ("Accelerator", 0.20, -0.53, 0.705, 0.043, 0.12),
]:
    bar(name + " pedal arm", (x, -0.402, 0.965), (x, y - 0.014, z), 0.010, steel)
    box(name + " pedal pad", (x, y, z), (width, 0.025, height), rubber, 0.008)
    for offset in (-0.025, 0, 0.025):
        box(name + " pedal tread", (x, y + 0.014, z + offset), (width * 0.78, 0.006, 0.004), black, 0.001)
# Steering wheel is in the left-hand seat, with four spokes.
steer = Vector((0.36, -0.045, 1.222))
normal = Vector((0, 0.80, 0.60)).normalized()
cylinder("Steering column", (0.36, -0.16, 1.11), 0.035, 0.23, black, normal, 24)
bpy.ops.mesh.primitive_torus_add(
    major_radius=0.144, minor_radius=0.012, major_segments=48, minor_segments=8, location=steer
)
o = bpy.context.object
o.rotation_mode = "QUATERNION"
o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(normal)
finish(o, "Steering wheel rim", black)
u = Vector((1, 0, 0))
v = normal.cross(u)
for a in (0.5, 2.65, 3.70, 5.8):
    bar("Steering wheel spoke", steer, steer + 0.135 * (math.cos(a) * u + math.sin(a) * v), 0.014, black)
cylinder("Steering wheel airbag", steer, 0.061, 0.035, black, normal, 32)
for s in (-1, 1):
    path("Padded roll bar", [(s * 0.61, 0.70, 0.70), (s * 0.61, 0.70, 1.61), (s * 0.48, 0.72, 1.72)], 0.041, black)
    path("Rear roll bar brace", [(s * 0.59, 0.73, 1.60), (s * 0.59, 1.50, 0.94)], 0.035, black)
    ACTIVE_PART = "driver_door" if s == 1 else "passenger_door"
    box("Door interior card", (s * 0.705, 0.10, 1.006), (0.032, 0.82, 0.41), interior, 0.025)
    bar("Interior door pull", (s * 0.675, -0.04, 1.09), (s * 0.675, 0.17, 1.09), 0.016, black)
    ACTIVE_PART = "body"
bar("Roll bar cross member", (-0.49, 0.72, 1.72), (0.49, 0.72, 1.72), 0.041, black)
bar("Gearshift", (0, -0.01, 0.83), (0, -0.04, 1.08), 0.010, steel)
box("Gearshift knob", (0, -0.04, 1.095), (0.053, 0.06, 0.045), black, 0.013)


# Custom 31-inch KO2-inspired tires with 16-inch satin-black eight-spoke rims.
# Dimensions describe the visual asset, not a particular retail metric SKU.
TIRE_RADIUS = 31 * 0.0254 / 2
RIM_BEAD_RADIUS = 16 * 0.0254 / 2


def wheel(center, spare=False):
    center = Vector(center)
    axis = Vector((0, 1, 0) if spare else (1, 0, 0))
    u = Vector((1, 0, 0) if spare else (0, 1, 0))
    v = Vector((0, 0, 1))

    def world(a, r, d):
        return tuple(center + axis * d + r * (math.cos(a) * u + math.sin(a) * v))

    profile = [
        (-0.110, RIM_BEAD_RADIUS),
        (-0.127, 0.233),
        (-0.136, 0.290),
        (-0.127, 0.343),
        (-0.106, 0.373),
        (-0.081, TIRE_RADIUS - 0.011),
        (0.081, TIRE_RADIUS - 0.011),
        (0.106, 0.373),
        (0.127, 0.343),
        (0.136, 0.290),
        (0.127, 0.233),
        (0.110, RIM_BEAD_RADIUS),
    ]
    loft(
        "31 inch tire carcass",
        [[world(i * 2 * math.pi / 96, r, d) for i in range(96)] for d, r in profile],
        rubber,
        capped=False,
    )

    # Angular interlocking blocks follow the tire's cylindrical surface. The
    # split through each block is a real narrow groove, visible at close range.
    block = [
        (-0.022, -0.019),
        (0.005, -0.022),
        (0.023, -0.010),
        (0.018, -0.002),
        (0.023, 0.011),
        (-0.004, 0.021),
        (-0.021, 0.014),
        (-0.016, 0.003),
    ]
    halves = [
        [block[0], block[1], block[2], (0.019, -0.001), (-0.017, -0.001)],
        [(-0.017, 0.001), (0.019, 0.001), block[4], block[5], block[6], block[7]],
    ]
    for i in range(48):
        for row in range(-2, 3):
            a = (i + (0.46 if row % 2 else 0)) * 2 * math.pi / 48
            offset = row * 0.046
            outer = TIRE_RADIUS - (0.006 if abs(row) == 2 else 0)
            for outline in halves:
                rows = [[world(a + t / TIRE_RADIUS, r, offset + d) for d, t in outline] for r in (outer - 0.014, outer)]
                loft("KO2 interlocking siped tread block", rows, rubber, capped=True, smooth=False)
        for side in (-1, 1):
            a = (i + (0.3 if side == 1 else 0)) * 2 * math.pi / 48
            # Alternating upper-sidewall armor grows out of the shoulder.
            rows = []
            for radius, depth, width in [(0.328, 0.132, 0.020), (0.351, 0.126, 0.030), (0.375, 0.110, 0.036)]:
                rows.append(
                    [
                        world(a + t / radius, radius, side * d)
                        for t, d in [
                            (-width / 2, depth - 0.004),
                            (width / 2, depth - 0.004),
                            (width / 2, depth + 0.004),
                            (-width / 2, depth + 0.004),
                        ]
                    ]
                )
            loft("KO2 serrated sidewall lug", rows, rubber, capped=True, smooth=False)

    # Continuous barrel connects both bead seats. Nominal wheel size is measured
    # at the bead seat; the protective outer lip extends beyond that diameter.
    barrel = [
        (-0.113, 0.2032),
        (-0.096, 0.199),
        (0.096, 0.199),
        (0.113, 0.2032),
        (0.113, 0.208),
        (-0.113, 0.208),
        (-0.113, 0.2032),
    ]
    loft(
        "16 inch black rim barrel",
        [[world(i * 2 * math.pi / 80, r, d) for i in range(80)] for d, r in barrel],
        wheel_black,
        capped=False,
    )
    for side in (1,) if spare else (-1, 1):
        lip = [
            (side * 0.110, 0.2032),
            (side * 0.126, 0.216),
            (side * 0.135, 0.215),
            (side * 0.137, 0.207),
            (side * 0.120, 0.196),
            (side * 0.110, 0.2032),
        ]
        loft(
            "16 inch black rim lip",
            [[world(i * 2 * math.pi / 80, r, d) for i in range(80)] for d, r in lip],
            wheel_black,
            capped=False,
        )
        cylinder("Black wheel center dish", center + axis * (side * 0.103), 0.082, 0.026, wheel_black, axis, 48)
        for i in range(8):
            a = i * 2 * math.pi / 8
            rows = []
            for r, width, d in [(0.068, 0.18, 0.114), (0.125, 0.12, 0.109), (0.202, 0.105, 0.128)]:
                rows.append(
                    [
                        world(a + off, r, side * depth)
                        for off, depth in [(-width, d - 0.014), (width, d - 0.014), (width, d), (-width, d)]
                    ]
                )
            loft("Black eight spoke wheel casting", rows, wheel_black, capped=True, smooth=False)
        for i in range(5):
            a = i * 2 * math.pi / 5
            radial = math.cos(a) * u + math.sin(a) * v
            cylinder(
                "Recessed lug well", center + axis * (side * 0.119) + radial * 0.05715, 0.012, 0.008, black, axis, 20
            )
            cylinder("Wheel lug nut", center + axis * (side * 0.127) + radial * 0.05715, 0.008, 0.015, steel, axis, 12)
        cylinder("Black center wheel cap", center + axis * (side * 0.126), 0.037, 0.018, wheel_black, axis, 40)
        cylinder("Tire valve stem", Vector(world(0.35, 0.184, side * 0.131)), 0.004, 0.021, rubber, axis, 16)
        for r, d in [(0.224, 0.124), (0.320, 0.132)]:
            path(
                "Tire sidewall mold line",
                [world(i * 2 * math.pi / 96, r, side * d) for i in range(96)],
                0.0013,
                rubber,
                True,
            )
        # Raised black lettering stays on the sidewall, with readable orientation
        # on either vehicle side and on the differently oriented spare.
        normal = axis * side
        handed = u.cross(v).dot(normal)
        for words, mid, size in [("BFGoodrich", math.pi / 2, 0.024), ("ALL-TERRAIN T/A KO2", -math.pi / 2, 0.017)]:
            for i, char in enumerate(words):
                if char == " ":
                    continue
                a = mid - handed * (i - (len(words) - 1) / 2) * size * 0.69 / 0.272
                radial = math.cos(a) * u + math.sin(a) * v
                right = radial.cross(normal)
                rotation = Matrix((right, radial, normal)).transposed().to_euler()
                text_mesh(
                    "KO2 raised sidewall lettering",
                    char,
                    world(a, 0.272, side * 0.136),
                    size,
                    sidewall_letters,
                    rotation,
                )


for x in (-0.737, 0.737):
    for y in (-1.186, 1.186):
        ACTIVE_PART = ("driver_wheel" if x > 0 else "passenger_wheel") if y < 0 else "body"
        if y < 0:
            sign = 1 if x > 0 else -1
            ACTIVE_PART = "driver_front_spin" if x > 0 else "passenger_front_spin"
            cylinder("Front brake rotor", (sign * 0.69, y, 0.4), 0.145, 0.018, alloy, (1, 0, 0), 48)
            ACTIVE_PART = "driver_wheel" if x > 0 else "passenger_wheel"
            box("Front brake caliper", (sign * 0.695, y + 0.11, 0.44), (0.065, 0.085, 0.13), steel, 0.018)
            bar("Steering knuckle", (sign * 0.64, y, 0.31), (sign * 0.64, y, 0.49), 0.027, steel)
            bar(
                "Steering arm",
                (sign * 0.64, y, 0.4),
                from_qt(g["driverEnd" if x > 0 else "passengerEnd"]),
                0.024,
                steel,
            )
            ACTIVE_PART = "driver_front_spin" if x > 0 else "passenger_front_spin"
            cylinder("Front wheel hub", (sign * 0.70, y, 0.4), 0.049, 0.16, steel, (1, 0, 0), 32)
        ACTIVE_PART = ("driver_" if x > 0 else "passenger_") + ("front_spin" if y < 0 else "rear_spin")
        wheel((x, y, 0.400))
ACTIVE_PART = "body"
ACTIVE_PART = "tailgate"
wheel((0, 1.895, 1.04), True)
ACTIVE_PART = "body"

# Export independent rigid hinges as well as the complete animated model.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from rig_export import export_vehicle

joints = export_vehicle(opts.output, opts.blend)
triangles = sum(len(p.vertices) - 2 for o in bpy.context.scene.objects if o.type == "MESH" for p in o.data.polygons)
print(f"Exported {opts.output}: {triangles:,} triangles, {opts.output.stat().st_size:,} bytes")
if opts.preview:
    box("Studio floor", (0, 0, -0.94), (200, 200, 0.06), material("Studio", (0.055, 0.07, 0.09)), 0)
    world = bpy.context.scene.world
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.18, 0.22, 0.28, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 0.5
    for loc, power, size in [((2, -4, 6), 1500, 5), ((-4, -1, 3), 1000, 4), ((1, 4, 4), 1800, 3)]:
        bpy.ops.object.light_add(type="AREA", location=loc)
        light = bpy.context.object
        light.data.energy = power
        light.data.shape = "DISK"
        light.data.size = size
        light.rotation_euler = (-light.location).to_track_quat("-Z", "Y").to_euler()
    bpy.ops.object.camera_add(location=(4.6, -6.8, 2.35))
    camera = bpy.context.object
    camera.rotation_euler = (Vector((0, 0, 0)) - camera.location).to_track_quat("-Z", "Y").to_euler()
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = 5.3
    scene = bpy.context.scene
    scene.camera = camera
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 24
    scene.cycles.use_denoising = True
    scene.render.resolution_x = 1400
    scene.render.resolution_y = 1000
    scene.render.resolution_percentage = 100
    opts.preview.parent.mkdir(parents=True, exist_ok=True)
    scene.render.filepath = str(opts.preview.resolve())
    bpy.ops.render.render(write_still=True)

    for label, location in [("side", (7, 0, 0.35)), ("front", (0, -8, 0.35)), ("rear", (-4.6, 6.8, 2.1))]:
        camera.location = location
        camera.rotation_euler = (-camera.location).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = str(opts.preview.with_stem(opts.preview.stem + "-" + label).resolve())
        bpy.ops.render.render(write_still=True)
