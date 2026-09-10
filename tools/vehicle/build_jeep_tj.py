"""Build OCTAVE's original photo-referenced 2003 TJ Sahara real-time asset using Blender 4.2.

Run: blender --background --python tools/vehicle/build_jeep_tj.py
Or: python (with bpy==4.2.0 installed) tools/vehicle/build_jeep_tj.py
Optional: --preview /tmp/jeep.png --blend /tmp/jeep.blend
Coordinates: Blender X right, -Y forward, Z up; exported glTF +Z forward,
+Y up. Metres, with an attitude pivot 0.9 m above the axle midpoint.
"""

import argparse
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
roof = material("Dark Khaki hardtop", (0.145, 0.135, 0.105), 0, 0.58)
clear_glass = material("Windshield glass", (0.19, 0.25, 0.24), 0.10, 0.12)
clear_glass.node_tree.nodes["Principled BSDF"].inputs["Alpha"].default_value = 0.23
clear_glass.surface_render_method = "DITHERED"
black = material("Textured black trim", (0.022, 0.028, 0.032), 0, 0.72)
rubber = material("Tire rubber", (0.014, 0.018, 0.022), 0, 0.87)
steel = material("Undercarriage steel", (0.055, 0.067, 0.078), 0.65, 0.46)
alloy = material("Satin alloy", (0.48, 0.53, 0.57), 0.75, 0.27)
glass = material("Tinted hardtop glass", (0.035, 0.052, 0.049), 0.25, 0.12)
glass.node_tree.nodes["Principled BSDF"].inputs["Alpha"].default_value = 0.78
glass.surface_render_method = "DITHERED"
lens = material("Headlamp glass", (0.72, 0.83, 0.87), 0.35, 0.17)
red = material("Tail lamp red", (0.55, 0.012, 0.015), 0.15, 0.24)
amber = material("Amber marker", (0.95, 0.27, 0.015), 0.15, 0.28)
interior = material("Khaki upholstery", (0.24, 0.23, 0.185), 0, 0.85)
fabric = material("Seat fabric inserts", (0.145, 0.153, 0.129), 0, 0.95)


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
    return o


BOX_CACHE = {}


def box(name, loc, size, mat, bevel=0.015):
    key = (tuple(size), mat.name, bevel)
    if key in BOX_CACHE:
        o = bpy.data.objects.new(name, BOX_CACHE[key])
        bpy.context.collection.objects.link(o)
        o.location = loc
        return o
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o = bpy.context.object
    o.dimensions = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    finish(o, name, mat, bevel)
    BOX_CACHE[key] = o.data
    return o


def cylinder(name, loc, radius, depth, mat, axis=(0, 0, 1), vertices=48):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=loc)
    o = bpy.context.object
    o.rotation_mode = "QUATERNION"
    o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(Vector(axis))
    finish(o, name, mat, 0.003)
    for p in o.data.polygons:
        p.use_smooth = len(p.vertices) == 4
    return o


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
    return o


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
    return obj


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
    return bpy.context.object


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


# Stock short-wheelbase chassis, exposed solid axles, springs, and steering links.
print("Building chassis and body surfaces", flush=True)
for x in (-0.47, 0.47):
    box("Boxed frame rail", (x, 0, 0.39), (0.095, 3.32, 0.14), steel, 0.015)
for y in (-1.186, 1.186):
    cylinder("Axle tube", (0, y, 0.39), 0.056, 1.48, steel, (1, 0, 0), 32)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, location=(0.10, y, 0.39))
    o = bpy.context.object
    o.scale = (0.16, 0.13, 0.145)
    finish(o, "Differential housing", steel)
    for x in (-0.52, 0.52):
        bar("Shock absorber", (x, y - 0.1, 0.39), (x, y + 0.08, 0.76), 0.024, steel)
        bar("Shock piston", (x, y + 0.02, 0.59), (x, y + 0.08, 0.77), 0.012, alloy)
        coil = [
            (x + 0.067 * math.cos(t * 2 * math.pi * 5), y + 0.067 * math.sin(t * 2 * math.pi * 5), 0.44 + t * 0.30)
            for t in [i / 100 for i in range(101)]
        ]
        path("Coil spring", coil, 0.009, steel)
        bar("Trailing arm", (x, y, 0.35), (x, y + (0.51 if y < 0 else -0.51), 0.47), 0.024, steel)
    bar("Driveshaft", (0.1, y, 0.39), (0, 0, 0.43), 0.038, steel)
bar("Steering tie rod", (-0.67, -1.30, 0.42), (0.67, -1.30, 0.42), 0.018, steel)
box("Cabin floor", (0, 0.54, 0.56), (1.42, 2.39, 0.08), paint, 0.015)
box("Transfer case skid", (0, 0.07, 0.29), (0.88, 0.68, 0.04), steel)
box("Rear fuel tank skid", (0, 1.38, 0.41), (0.88, 0.50, 0.18), steel)
path(
    "Exhaust pipe",
    [(0.35, -0.5, 0.40), (0.38, 0.4, 0.40), (0.48, 0.8, 0.52), (0.51, 1.4, 0.38), (0.53, 1.79, 0.37)],
    0.025,
    steel,
)

# Rear tub sides; a flat-topped arch with slanted ends is characteristic of the TJ.
arch = [(-0.63, 0.55), (-0.38, 0.93), (-0.28, 0.97), (0.31, 0.97), (0.39, 0.92), (0.61, 0.55)]
arch = rounded(arch, 0.075, 7)
for s in (-1, 1):
    outer = [(-0.54, 0.56), (-0.54, 1.255), (1.71, 1.255), (1.73, 0.55)]
    # Follow the wheel opening back toward the front to form one concave side sheet.
    edge = outer + [(1.186 + y, z) for y, z in reversed(arch)]
    sheet("Tub side with wheel opening", edge, lambda y, z, s=s: (s * 0.735, y, z), paint)
    box("Rocker rail", (s * 0.705, 0.02, 0.555), (0.085, 1.21, 0.085), paint, 0.025)
    box("Rear wheel housing", (s * 0.53, 1.18, 0.89), (0.32, 0.90, 0.14), black, 0.055)
box("Tailgate panel", (0, 1.733, 0.916), (1.43, 0.065, 0.66), paint, 0.038)
box("Tailgate seam", (0, 1.771, 0.92), (0.91, 0.008, 0.585), black, 0.01)
box("Tailgate skin", (0, 1.778, 0.925), (0.897, 0.013, 0.568), paint, 0.01)

# Narrow grille and a gently crowned hood with rolled shoulders and tapered width.
hood_sections = []
for y, w, h in [
    (-1.715, 0.558, 1.265),
    (-1.67, 0.567, 1.295),
    (-1.50, 0.586, 1.313),
    (-0.81, 0.641, 1.32),
    (-0.64, 0.65, 1.31),
]:
    cross = []
    # Elliptical shoulders flow from vertical skirt into the crowned top.
    for i in range(33):
        a = math.pi * i / 32
        x = w * math.cos(a)
        z = 1.105 + (h - 1.105) * (math.sin(a) ** 0.27)
        cross.append((x, y, z))
    cross.extend([(-w, y, 1.085), (w, y, 1.085)])
    hood_sections.append(cross)
loft("Crowned tapered hood", hood_sections, paint, capped=False)
# Cowl widens toward the windshield; the seam remains a hairline.
cowl_sections = []
for y, w in [(-0.632, 0.651), (-0.57, 0.685), (-0.49, 0.736), (-0.37, 0.736)]:
    section = [
        (w * math.cos(math.pi * i / 32), y, 1.10 + 0.207 * (math.sin(math.pi * i / 32) ** 0.27)) for i in range(33)
    ]
    section.extend([(-w, y, 0.75), (w, y, 0.75)])
    cowl_sections.append(section)
loft("Cowl shoulder transition", cowl_sections, paint)
for x in (-0.25, 0.25):
    box("Hood rubber stop", (x, -0.77, 1.325), (0.035, 0.035, 0.022), rubber, 0.009)
    path(
        "Windshield tie down loop",
        [(x - 0.028, -0.91, 1.322), (x - 0.028, -0.91, 1.351), (x + 0.028, -0.91, 1.351), (x + 0.028, -0.91, 1.322)],
        0.006,
        steel,
    )
for x in [i * 0.022 for i in range(-11, 12)]:
    box("Cowl intake louver", (x, -0.523, 1.309), (0.007, 0.105, 0.009), black, 0.003)

# Inner engine-bay sides close the gap behind the front fender shoulders.
box("Engine block silhouette", (0, -1.10, 0.865), (0.86, 0.96, 0.40), black, 0.035)
for s in (-1, 1):
    sheet(
        "Engine bay side panel",
        [(-1.70, 1.107), (-0.53, 1.107), (-0.50, 0.57), (-0.55, 0.57), (-0.90, 1.01), (-1.70, 1.01)],
        lambda y, z, s=s: (s * (0.558 + 0.088 * (y + 1.70) / 1.17), y, z),
        paint,
    )
    sheet(
        "Cowl lower side panel",
        [(-0.64, 1.11), (-0.37, 1.25), (-0.37, 0.56), (-0.53, 0.56)],
        lambda y, z, s=s: (s * 0.736, y, z),
        paint,
    )

# Form the grille as a smooth panel and cut seven real, rounded, recessed openings.
outline = rounded([(-0.54, 0.765), (0.54, 0.765), (0.565, 1.115), (0.51, 1.25), (-0.51, 1.25), (-0.565, 1.115)], 0.055)
grille = sheet("Grille stamped steel surround", outline, lambda x, z: (x, -1.72, z), paint)
mod = grille.modifiers.new("Grille thickness", "SOLIDIFY")
mod.thickness = 0.045
bpy.context.view_layer.objects.active = grille
bpy.ops.object.modifier_apply(modifier=mod.name)
for x in [i * 0.081 for i in range(-3, 4)]:
    contour = rr(x - 0.025, 0.793, x + 0.025, 1.202, 0.024)
    cutter = loft("Grille slot cutter", [[(u, y, v) for u, v in contour] for y in (-1.80, -1.62)], black, smooth=False)
    bpy.context.view_layer.objects.active = grille
    mod = grille.modifiers.new("Open grille slot", "BOOLEAN")
    mod.object = cutter
    mod.operation = "DIFFERENCE"
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(cutter, do_unlink=True)
    path("Rolled grille slot edge", [(u, -1.728, v) for u, v in contour], 0.0045, paint, True)
box("Radiator behind open grille", (0, -1.655, 0.995), (0.63, 0.027, 0.455), black, 0.01)
for x in [i * 0.013 for i in range(-23, 24)]:
    box("Radiator core fin", (x, -1.677, 0.997), (0.003, 0.005, 0.385), black, 0)

# Flat-top front fenders roll down around the tire; rear flares have a trapezoid opening.
for s in (-1, 1):
    front = [(-1.77, 0.88), (-1.74, 1.075), (-1.55, 1.095), (-0.94, 1.095), (-0.84, 1.04), (-0.53, 0.56)]
    rear = [(0.54, 0.55), (0.81, 1.00), (0.91, 1.045), (1.49, 1.045), (1.59, 1.00), (1.80, 0.55)]
    for name, profile, basewidth in [("Front flare", front, 0.61), ("Rear flare", rear, 0.735)]:
        profile = rounded(profile, 0.072, 7)
        rows = []
        for y, z in profile:
            rows.append(
                [
                    (s * basewidth, y, z),
                    (s * 0.815, y, z + 0.013),
                    (s * 0.872, y, z - 0.022),
                    (s * 0.873, y, z - 0.075),
                    (s * 0.84, y, z - 0.09),
                ]
            )
        loft(name, rows, paint, capped=True)
        path(name + " lower trim", [(s * 0.845, y, z - 0.085) for y, z in profile], 0.009, black)
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
    box("Hood latch strap", (s * 0.594, -1.51, 1.16), (0.026, 0.029, 0.102), black, 0.006)
    cylinder("Latch hinge", (s * 0.61, -1.51, 1.194), 0.012, 0.022, steel, (1, 0, 0), 16)

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
    sheet("Door outer skin", door, lambda y, z, s=s: (s * 0.741, y, z), paint)
    path("Door shut line", [(s * 0.745, y, z) for y, z in door], 0.0025, black, True)
    outer = rounded([(-0.36, 1.256), (-0.11, 1.775), (0.54, 1.775), (0.66, 1.68), (0.65, 1.256)], 0.055)
    inner = rounded([(-0.299, 1.285), (-0.075, 1.727), (0.514, 1.727), (0.604, 1.655), (0.602, 1.285)], 0.055)
    ring("Full door upper frame", outer, inner, side, paint)
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
    box("Molded side step", (s * 0.80, 0.095, 0.526), (0.188, 0.99, 0.055), black, 0.022)
    for y in [i * 0.022 for i in range(-17, 22)]:
        box("Side step traction rib", (s * 0.80, y, 0.557), (0.155, 0.009, 0.006), rubber, 0.002)
    if s == -1:
        cylinder("Fuel filler bezel", (s * 0.749, 1.59, 1.054), 0.079, 0.014, black, (1, 0, 0), 48)
        cylinder("Fuel filler cap", (s * 0.761, 1.59, 1.054), 0.052, 0.024, steel, (1, 0, 0), 40)
        box("Fuel cap grip", (s * 0.782, 1.59, 1.054), (0.014, 0.018, 0.084), black, 0.007)
    # Model identification in geometry, without projecting any listing photography.
    text_mesh("Jeep side emblem", "Jeep", (s * 0.75, -0.53, 0.77), 0.048, alloy, (math.pi / 2, 0, s * math.pi / 2))
    text_mesh("Sahara nameplate", "SAHARA", (s * 0.75, -0.53, 0.89), 0.020, alloy, (math.pi / 2, 0, s * math.pi / 2))


# Windshield is a broad flat pane in a rounded steel frame, slightly raked aft.
def wind(x, z):
    return (x, -0.49 + (z - 1.275) * 0.52, z)


wout = rounded([(-0.734, 1.285), (0.734, 1.285), (0.691, 1.789), (-0.691, 1.789)], 0.055)
win = rounded([(-0.668, 1.355), (0.668, 1.355), (0.638, 1.744), (-0.638, 1.744)], 0.050)
ring("Windshield frame", wout, win, wind, paint)
path("Windshield outer seal", [wind(x, z) for x, z in wout], 0.007, black, True)
path("Windshield glass gasket", [wind(x, z) for x, z in win], 0.011, black, True)
sheet("Laminated windshield", win, lambda x, z: (x, wind(x, z)[1] - 0.003, z), clear_glass)
for x in (-0.36, 0.28):
    path("Windshield wiper arm", [(x, -0.503, 1.303), (x - 0.20, -0.477, 1.405)], 0.007, steel)
    path("Wiper blade", [(x - 0.41, -0.47, 1.405), (x - 0.02, -0.47, 1.405)], 0.009, black)
for x in (-0.66, 0.66):
    box("Windshield hinge block", (x, -0.481, 1.305), (0.073, 0.067, 0.043), paint, 0.01)
bar("Radio antenna", (0.787, -0.53, 1.17), (0.787, -0.53, 1.89), 0.0022, steel)

# Removable Dark Khaki hardtop: tapered shell, large rounded corner windows, rolled roof edge.
for s in (-1, 1):

    def mapping(y, z, s=s):
        return (s * (0.750 - 0.115 * (z - 1.25)), y, z)

    outer = rounded([(0.658, 1.25), (1.749, 1.25), (1.675, 1.79), (0.610, 1.79)], 0.070)
    inner = rounded([(0.728, 1.329), (1.637, 1.329), (1.590, 1.717), (0.720, 1.717)], 0.11)
    ring("Hardtop quarter shell", outer, inner, mapping, roof)
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
    (1.63, 0.700, 1.81),
    (1.72, 0.667, 1.745),
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
ring("Hardtop rear surround", outer, inner, rear_map, roof)
sheet("Rear liftgate glass", inner, rear_map, glass)
path("Rear glass seal", [rear_map(x, z) for x, z in inner], 0.008, black, True)
for x in (-0.40, 0.40):
    box("Liftglass hinge", (x, 1.717, 1.702), (0.048, 0.038, 0.129), black, 0.012)
path("Rear wiper", [(0.28, 1.743, 1.55), (-0.07, 1.742, 1.656), (-0.37, 1.742, 1.64)], 0.009, black)

# Rear lamps, hinges, tailgate handle, and a proper high-mounted brake light bracket.
for s in (-1, 1):
    box("Tail lamp base", (s * 0.61, 1.778, 1.018), (0.172, 0.065, 0.214), black, 0.010)
    box("Red tail lamp lens", (s * 0.61, 1.817, 1.045), (0.149, 0.038, 0.145), red, 0.009)
    box("Reverse light lens", (s * 0.61, 1.837, 0.951), (0.136, 0.011, 0.025), lens, 0.004)
    for xoff in (-0.055, -0.028, 0, 0.028, 0.055):
        box("Tail lamp optical rib", (s * 0.61 + xoff, 1.838, 1.045), (0.003, 0.004, 0.12), red, 0.001)
box("Tailgate latch", (-0.30, 1.815, 1.13), (0.12, 0.021, 0.07), black, 0.010)
for z in (0.82, 1.15):
    box("Tailgate hinge", (0.455, 1.789, z), (0.13, 0.024, 0.035), paint, 0.006)
box("Tailgate pressure vent", (0, 1.797, 0.814), (0.39, 0.012, 0.17), black, 0.014)
for z in (0.755, 0.792, 0.83, 0.866):
    box("Pressure vent louver", (0, 1.806, z), (0.36, 0.012, 0.014), black, 0.004)
box("License bracket", (-0.60, 1.82, 0.784), (0.19, 0.04, 0.113), black, 0.009)
box("Spare carrier mount", (0, 1.85, 1.03), (0.30, 0.12, 0.27), steel, 0.02)
box("Third brake light stalk", (0, 1.847, 1.315), (0.055, 0.06, 0.38), black, 0.009)
box("Third brake light", (0, 1.87, 1.49), (0.20, 0.052, 0.055), red, 0.010)

# Khaki cabin visible through the lowered door windows and tinted windshield.
print("Building interior and Canyon wheels", flush=True)
for x in (-0.36, 0.36):
    box("Seat base", (x, 0.12, 0.76), (0.43, 0.50, 0.15), interior, 0.065)
    back = box("Seat back", (x, 0.345, 1.046), (0.43, 0.16, 0.52), interior, 0.070)
    back.rotation_euler.x = math.radians(7)
    box("Integrated headrest", (x, 0.38, 1.359), (0.235, 0.14, 0.24), interior, 0.070)
    box("Seat center fabric insert", (x, 0.247, 1.064), (0.29, 0.022, 0.345), fabric, 0.043)
    box("Seat cushion fabric insert", (x, 0.07, 0.841), (0.30, 0.34, 0.017), fabric, 0.030)
    for side in (-1, 1):
        box("Seat side bolster", (x + side * 0.175, 0.15, 0.866), (0.077, 0.37, 0.068), interior, 0.030)
box("Rear bench seat", (0, 1.12, 0.819), (1.15, 0.42, 0.17), interior, 0.055)
box("Rear bench back", (0, 1.416, 1.067), (1.15, 0.15, 0.41), interior, 0.06)
box("Center console", (0, 0.38, 0.842), (0.24, 0.74, 0.31), interior, 0.035)
box("Center armrest", (0, 0.57, 1.022), (0.26, 0.35, 0.105), interior, 0.045)
for y in (0.01, 0.17):
    cylinder("Console cup recess", (0, y, 1.0), 0.049, 0.010, black, (0, 0, 1), 32)
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
# Steering wheel is in the left-hand seat, with four spokes.
steer = Vector((-0.36, -0.045, 1.222))
normal = Vector((0, 0.80, 0.60)).normalized()
cylinder("Steering column", (-0.36, -0.16, 1.11), 0.035, 0.23, black, normal, 24)
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
    box("Door interior card", (s * 0.705, 0.10, 1.006), (0.032, 0.82, 0.41), interior, 0.025)
    bar("Interior door pull", (s * 0.675, -0.04, 1.09), (s * 0.675, 0.17, 1.09), 0.016, black)
bar("Roll bar cross member", (-0.49, 0.72, 1.72), (0.49, 0.72, 1.72), 0.041, black)
bar("Gearshift", (0, -0.01, 0.83), (0, -0.04, 1.08), 0.010, steel)
box("Gearshift knob", (0, -0.04, 1.095), (0.053, 0.06, 0.045), black, 0.013)


# Radial tire profile and broad, dished Canyon spokes match photos 007 and 047.
def wheel(center, spare=False):
    center = Vector(center)
    axis = Vector((0, 1, 0) if spare else (1, 0, 0))
    u = Vector((1, 0, 0) if spare else (0, 1, 0))
    v = Vector((0, 0, 1))

    def world(a, r, d):
        return tuple(center + axis * d + r * (math.cos(a) * u + math.sin(a) * v))

    # This profile gives a tread belt and full sidewalls, rather than a donut tire.
    profile = [
        (-0.115, 0.198),
        (-0.132, 0.228),
        (-0.136, 0.29),
        (-0.127, 0.343),
        (-0.105, 0.377),
        (-0.083, 0.386),
        (0.083, 0.386),
        (0.105, 0.377),
        (0.127, 0.343),
        (0.136, 0.29),
        (0.132, 0.228),
        (0.115, 0.198),
    ]
    sections = [[world(i * 2 * math.pi / 80, r, d) for i in range(80)] for d, r in profile]
    loft("31 inch tire carcass", sections, rubber, capped=False)
    # Staggered chevron tread with a narrow center gap, open shoulder grooves.
    for i in range(52):
        for row in (-1, 1):
            a = (i + (0.28 if row == 1 else 0)) * 2 * math.pi / 52
            radial = math.cos(a) * u + math.sin(a) * v
            tangent = radial.cross(axis)
            pos = center + radial * 0.389 + axis * (row * 0.053)
            o = box("Chevron tread block", pos, (0.105, 0.043, 0.020), rubber, 0.003)
            rot = Matrix((axis, tangent, radial)).transposed()
            o.rotation_euler = (rot @ Matrix.Rotation(row * 0.30, 3, "Z")).to_euler()
            pos = center + radial * 0.369 + axis * (row * 0.109)
            o = box("Shoulder tread lug", pos, (0.030, 0.047, 0.028), rubber, 0.003)
            o.rotation_euler = rot.to_euler()
    for side in (1,) if spare else (-1, 1):
        # Machined outer rim lips, dark openings, broad five-spoke casting.
        lip = [(side * 0.139, 0.199), (side * 0.146, 0.197), (side * 0.148, 0.190), (side * 0.127, 0.182)]
        loft(
            "Alloy rim lip",
            [[world(i * 2 * math.pi / 80, r, d) for i in range(80)] for d, r in lip],
            alloy,
            capped=False,
        )
        cylinder("Wheel dark interior", center + axis * (side * 0.094), 0.181, 0.022, black, axis, 64)
        cylinder("Wheel center dish", center + axis * (side * 0.114), 0.085, 0.031, alloy, axis, 48)
        for i in range(5):
            a = i * 2 * math.pi / 5
            # Each spoke widens at the rim; the negative spaces form the Canyon holes.
            rows = []
            for r, width, d in [(0.065, 0.46, 0.126), (0.104, 0.42, 0.12), (0.149, 0.33, 0.121), (0.186, 0.40, 0.134)]:
                rows.append([world(a + off, r, side * d) for off in (-width, -width * 0.65, 0, width * 0.65, width)])
            # Surface grid does not wrap across the front of the adjacent holes.
            verts = [p for row in rows for p in row]
            faces = [
                (j * 5 + k, j * 5 + k + 1, (j + 1) * 5 + k + 1, (j + 1) * 5 + k) for j in range(3) for k in range(4)
            ]
            mesh_object("Broad dished Canyon spoke", verts, faces, alloy, True)
            radial = math.cos(a) * u + math.sin(a) * v
            cylinder(
                "Recessed lug well", center + axis * (side * 0.139) + radial * 0.057, 0.012, 0.006, steel, axis, 20
            )
            cylinder("Wheel lug nut", center + axis * (side * 0.145) + radial * 0.057, 0.008, 0.014, alloy, axis, 12)
        cylinder("Center wheel cap", center + axis * (side * 0.135), 0.035, 0.014, alloy, axis, 40)
        # Fine molding rings around the raised sidewall.
        for r in (0.213, 0.331):
            path(
                "Tire sidewall mold line",
                [world(i * 2 * math.pi / 80, r, side * 0.131) for i in range(80)],
                0.0014,
                rubber,
                True,
            )


for x in (-0.737, 0.737):
    for y in (-1.186, 1.186):
        wheel((x, y, 0.400))
wheel((0, 1.895, 1.04), True)

# Pivot at the chassis midpoint, without a baked ground plane or lights.
vehicle = [o for o in bpy.context.scene.objects if o.type == "MESH"]
for o in vehicle:
    o.location.z -= 0.9
if opts.blend:
    opts.blend.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(opts.blend.resolve()))
# Merge by material to keep draw calls low on Raspberry Pi / Android.
for mat in (paint, roof, black, rubber, steel, alloy, glass, clear_glass, lens, red, amber, interior, fabric):
    bpy.ops.object.select_all(action="DESELECT")
    group = [o for o in bpy.context.scene.objects if o.type == "MESH" and o.data.materials[0] == mat]
    if not group:
        continue
    for o in group:
        o.select_set(True)
    bpy.context.view_layer.objects.active = group[0]
    if len(group) > 1:
        bpy.ops.object.join()
    group[0].name = mat.name
bpy.ops.object.select_all(action="SELECT")
opts.output.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.export_scene.gltf(
    filepath=str(opts.output.resolve()),
    export_format="GLB",
    use_selection=True,
    export_yup=True,
    export_apply=True,
    export_cameras=False,
    export_lights=False,
    export_extras=False,
)
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
