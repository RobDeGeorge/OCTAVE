"""Build a BMW E46 330i ZHP sedan (2003-2005) as a static .glb for OCTAVE's vehicle view.

    blender -b -P tools/vehicle/build_bmw_e46.py                      # writes the .glb
    blender -b -P tools/vehicle/build_bmw_e46.py -- --render DIR      # and four check pictures

Output: frontend/assets/vehicles/bmw-e46-330i-zhp/model.glb, one of the vehicles the app
ships with (kBuiltIns in src/managers/vehiclemanager.cpp). Render its picture afterwards:
    xvfb-run -a venv/bin/python tools/vehicle/render_preview.py bmw-e46-330i-zhp
It is a model of the app's "shown whole" kind: nothing moves. The wheels are separate
objects (wheel_fl / fr / rl / rr, origin on the axle) for a later rig.

How it is made, so it can be changed:
  - The body is one shell lofted through cross-sections along the car (STATIONS), each
    built by section() from four curves of the side view: the underside, the shoulder
    line, the top line down the middle, and the half-width seen from above. A subdivision
    surface rounds it. Wheel arches are cut out of it afterwards.
  - Everything flat on the body (glass, lamps, grilles, shut lines, handles) is a decal: a
    2D outline drawn in the side / front / rear / top view, cut from a grid and projected
    onto the shell (decal()). Outlines must be convex; a shape that is not is several.
  - Dimensions are the E46 sedan's: 4471 long, 1739 wide, 2725 wheelbase, 1481 / 1488
    track, about 1400 high on the ZHP's springs, 225/40 and 255/35 R18 tyres.

Coordinates in this file: s = metres back from the front bumper, lat = metres to the car's
left, z = metres up from the road. In Blender that is x = lat, y = s - LENGTH / 2, z = z,
which the glTF exporter turns into OCTAVE's +Z forward / +Y up.
"""

import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Matrix, Vector

LENGTH = 4.471
HALF_WIDTH = 0.8695
FRONT_AXLE = 0.752
REAR_AXLE = FRONT_AXLE + 2.725
TRACK_FRONT = 1.481 / 2
TRACK_REAR = 1.488 / 2
TYRE_RADIUS = 0.318
OUT = Path(__file__).resolve().parents[2] / "frontend/assets/vehicles/bmw-e46-330i-zhp/model.glb"


# ── Curves of the side and top views ────────────────────────────────────────

def curve(points):
    """A smooth function through (s, value) points: cubic Hermite with tangents
    limited so it never overshoots between two points (PCHIP)."""
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    n = len(xs)
    slopes = [(ys[i + 1] - ys[i]) / (xs[i + 1] - xs[i]) for i in range(n - 1)]
    tangents = [slopes[0]]
    for i in range(1, n - 1):
        a, b = slopes[i - 1], slopes[i]
        tangents.append(0.0 if a * b <= 0 else 2 * a * b / (a + b))
    tangents.append(slopes[-1])

    def f(x):
        if x <= xs[0]:
            return ys[0]
        if x >= xs[-1]:
            return ys[-1]
        i = max(j for j in range(n - 1) if xs[j] <= x)
        h = xs[i + 1] - xs[i]
        t = (x - xs[i]) / h
        return ((2 * t**3 - 3 * t**2 + 1) * ys[i] + (t**3 - 2 * t**2 + t) * h * tangents[i]
                + (-2 * t**3 + 3 * t**2) * ys[i + 1] + (t**3 - t**2) * h * tangents[i + 1])

    return f


# The top of the car down its middle: bumper, hood, windshield, roof, rear glass, deck
top_line = curve([(0, 0.665), (0.04, 0.72), (0.14, 0.765), (0.5, 0.855), (1.0, 0.93), (1.36, 0.965),
                  (1.46, 1.01), (1.75, 1.20), (2.02, 1.335), (2.25, 1.39), (2.62, 1.405), (3.02, 1.385),
                  (3.24, 1.335), (3.55, 1.185), (3.80, 1.07), (3.90, 1.05), (4.30, 1.05), (4.40, 1.035),
                  (4.45, 0.97), (LENGTH, 0.80)])
# Where the side turns into the hood, the window sill and the deck
shoulder = curve([(0, 0.62), (0.1, 0.70), (0.5, 0.80), (1.0, 0.87), (1.4, 0.90), (2.5, 0.93),
                  (3.6, 0.955), (4.0, 0.975), (4.35, 0.975), (4.44, 0.93), (LENGTH, 0.78)])
underside = curve([(0, 0.21), (0.25, 0.175), (3.85, 0.175), (4.25, 0.26), (LENGTH, 0.33)])
# Half the width seen from above: the nose and tail taper, the middle is barrelled
half_width = curve([(0, 0.67), (0.06, 0.745), (0.25, 0.83), (0.6, 0.860), (1.4, HALF_WIDTH), (3.0, HALF_WIDTH),
                    (3.8, 0.857), (4.2, 0.815), (4.40, 0.735), (LENGTH, 0.64)])

ROOF_HALF_WIDTH = 0.615


def smoothstep(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def cabin(s):
    """0 over the hood and the deck, 1 under the roof: how far the cabin has risen here."""
    return smoothstep((top_line(s) - shoulder(s) - 0.06) / 0.38)


def section(s):
    """Half a cross-section at s, from the middle of the underside round to the middle
    of the top: (lat, z) points. The other half is its mirror."""
    w, zb, zs, zt, f = half_width(s), underside(s), shoulder(s), top_line(s), cabin(s)
    rise = zt - zs
    edge = (1 - f) * 0.86 * w + f * ROOF_HALF_WIDTH   # roof edge / hood edge
    return [
        (0.0, zb),
        (w * 0.80, zb),
        (w * 0.975, zb + 0.05),
        (w, zb + 0.32 * (zs - zb)),
        (w, zb + 0.66 * (zs - zb)),
        (w * 0.985, zs - 0.045),
        (w * 0.955, zs),
        (w * (0.925 - 0.01 * f), zs + 0.014),      # window sill
        (edge, zs + 0.87 * rise),
        (edge * 0.88, zs + 0.958 * rise),          # a tight roof rail, then a nearly flat roof
        (edge * 0.50, zs + 0.992 * rise),
        (0.0, zt),
    ]


STATIONS = [0.0, 0.03, 0.12, 0.3, 0.5, FRONT_AXLE, 1.0, 1.2, 1.36, 1.46, 1.6, 1.8, 2.0, 2.15, 2.3, 2.6,
            2.9, 3.1, 3.25, 3.4, 3.6, 3.78, 3.88, 4.0, 4.2, 4.35, 4.43, LENGTH]


def world(lat, s, z):
    return Vector((lat, s - LENGTH / 2, z))


# ── Materials ───────────────────────────────────────────────────────────────

def material(name, colour, roughness=0.5, metallic=0.0, emission=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*colour, 1)
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    if emission:
        bsdf.inputs["Emission Color"].default_value = (*emission, 1)
        bsdf.inputs["Emission Strength"].default_value = 0.6
    m.use_backface_culling = False
    return m


def make_materials():
    return {
        "paint": material("paint_imola_red", (0.50, 0.012, 0.022), 0.22),
        "glass": material("glass", (0.015, 0.02, 0.025), 0.06),
        "black": material("black_trim", (0.012, 0.012, 0.014), 0.55),
        "shut": material("shut_line", (0.05, 0.004, 0.006), 0.6),
        "rubber": material("tyre", (0.02, 0.02, 0.022), 0.85),
        "rim": material("rim", (0.62, 0.63, 0.65), 0.28, 0.9),
        "disc": material("brake", (0.16, 0.16, 0.17), 0.5, 0.6),
        "chrome": material("chrome", (0.85, 0.85, 0.86), 0.08, 1.0),
        "lens": material("headlight", (0.80, 0.83, 0.86), 0.08, 0.3),
        "projector": material("projector", (0.30, 0.32, 0.35), 0.1, 0.9),
        "red": material("tail_red", (0.20, 0.0, 0.002), 0.10),
        "clear": material("tail_clear", (0.78, 0.76, 0.74), 0.12),
        "amber": material("amber", (0.9, 0.36, 0.02), 0.2),
        "plate": material("plate", (0.86, 0.86, 0.84), 0.5),
        "badge": material("badge", (0.10, 0.25, 0.55), 0.2),
    }


def new_object(name, bm, mat=None, smooth=True, sharp_angle=None):
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    for p in mesh.polygons:
        p.use_smooth = smooth
    if sharp_angle is not None:
        mesh.set_sharp_from_angle(angle=math.radians(sharp_angle))
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    if mat:
        mesh.materials.append(mat)
    return obj


def baked(obj):
    """Replace an object's mesh by what its modifiers make of it."""
    dg = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(dg))
    old = obj.data
    obj.modifiers.clear()
    obj.data = mesh
    bpy.data.meshes.remove(old)
    return obj


# ── Body ────────────────────────────────────────────────────────────────────

def build_body(mats):
    bm = bmesh.new()
    rings = []
    for i, s in enumerate(STATIONS):
        half = section(s)
        # The two ends are a little smaller than the section just inside them,
        # so the caps come out as rounded faces, not a sharp rim
        if i in (0, len(STATIONS) - 1):
            mid_z = (half[0][1] + half[-1][1]) / 2
            half = [(lat * 0.93, mid_z + (z - mid_z) * 0.92) for lat, z in half]
        ring = [bm.verts.new(world(lat, s, z)) for lat, z in half]
        ring += [bm.verts.new(world(-lat, s, z)) for lat, z in reversed(half[1:-1])]
        rings.append(ring)
    n = len(rings[0])
    for a, b in zip(rings, rings[1:], strict=False):
        for j in range(n):
            bm.faces.new((a[j], a[(j + 1) % n], b[(j + 1) % n], b[j]))
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    # A defined rocker, shoulder, window sill and roof rail on an otherwise soft
    # shell: creases along those lines of the loft (index into section())
    crease = bm.edges.layers.float.new("crease_edge")
    for k, weight in ((2, 0.5), (6, 0.55), (7, 0.6), (8, 0.4)):
        for j in (k, n - k):
            for a, b in zip(rings, rings[1:], strict=False):
                bm.edges.get((a[j], b[j]))[crease] = weight
    body = new_object("body", bm, mats["paint"])
    body.data.materials.append(mats["black"])   # slot 1: what the arch cutters leave
    sub = body.modifiers.new("subdivide", "SUBSURF")
    sub.levels = sub.render_levels = 3
    baked(body)

    # Wheel arches: a pocket per wheel, lined in black
    for s, track in ((FRONT_AXLE, TRACK_FRONT), (REAR_AXLE, TRACK_REAR)):
        for side in (1, -1):
            cbm = bmesh.new()
            bmesh.ops.create_cone(cbm, cap_ends=True, segments=72, radius1=0.362, radius2=0.362, depth=0.62)
            bmesh.ops.rotate(cbm, verts=cbm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.pi / 2, 3, "Y"))
            bmesh.ops.translate(cbm, verts=cbm.verts, vec=world(side * (track + 0.13), s, TYRE_RADIUS + 0.012))
            cutter = new_object("cutter", cbm, mats["black"])
            cutter.data.materials.clear()
            cutter.data.materials.append(mats["paint"])
            cutter.data.materials.append(mats["black"])
            for p in cutter.data.polygons:
                p.material_index = 1
            cut = body.modifiers.new("arch", "BOOLEAN")
            cut.operation = "DIFFERENCE"
            cut.solver = "EXACT"
            cut.object = cutter
            baked(body)
            bpy.data.objects.remove(cutter)
    for p in body.data.polygons:
        p.use_smooth = True
    body.data.set_sharp_from_angle(angle=math.radians(38))
    return body


# ── Decals ──────────────────────────────────────────────────────────────────

# view -> (2D point -> 3D point on the plane the decal starts from, shrinkwrap axis, positive direction)
VIEWS = {
    "left": (lambda u, v: world(1.6, u, v), "x", False),      # (s, z), seen from the car's left
    "right": (lambda u, v: world(-1.6, u, v), "x", True),
    "front": (lambda u, v: world(u, -0.6, v), "y", True),     # (lat, z)
    "rear": (lambda u, v: world(u, LENGTH + 0.6, v), "y", False),
    "top": (lambda u, v: world(u, v, 2.2), "z", False),       # (lat, s)
}


def clipped_grid(bm, outline, cell):
    """Add a grid of quads cut to a convex outline, in the plane z = 0."""
    area = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(outline, outline[1:] + outline[:1], strict=True))
    if area < 0:
        outline = outline[::-1]
    us, vs = [p[0] for p in outline], [p[1] for p in outline]
    u0, v0 = min(us), min(vs)
    nu = max(1, math.ceil((max(us) - u0) / cell))
    nv = max(1, math.ceil((max(vs) - v0) / cell))
    du, dv = (max(us) - u0) / nu, (max(vs) - v0) / nv
    grid = [[bm.verts.new((u0 + i * du, v0 + j * dv, 0)) for j in range(nv + 1)] for i in range(nu + 1)]
    made = []
    for i in range(nu):
        for j in range(nv):
            made.append(bm.faces.new((grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1])))
    geom = set(made)
    for row in grid:
        geom.update(row)
    for f in made:
        geom.update(f.edges)
    geom = list(geom)
    for a, b in zip(outline, outline[1:] + outline[:1], strict=True):
        normal = Vector((b[1] - a[1], -(b[0] - a[0]), 0))   # outward, for a counter-clockwise outline
        if normal.length < 1e-9:
            continue
        result = bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-6, plane_co=(a[0], a[1], 0),
                                        plane_no=normal.normalized(), clear_outer=True)
        geom = [g for g in result["geom"] if g.is_valid]


def decal(name, body, view, outlines, mat, offset=0.003, cell=0.03):
    """Flat shapes drawn in a view, laid onto the body. `outlines` is a list of convex
    polygons in the view's 2D coordinates; a larger `offset` puts one decal over another."""
    place, axis, positive = VIEWS[view]
    bm = bmesh.new()
    for outline in outlines:
        clipped_grid(bm, outline, cell)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    for v in bm.verts:
        v.co = place(v.co.x, v.co.y)
    obj = new_object(name, bm, mat)
    wrap = obj.modifiers.new("lay", "SHRINKWRAP")
    wrap.target = body
    wrap.wrap_method = "PROJECT"
    wrap.use_project_x, wrap.use_project_y, wrap.use_project_z = axis == "x", axis == "y", axis == "z"
    wrap.use_positive_direction = positive
    wrap.use_negative_direction = not positive
    wrap.offset = offset
    start = {i: v.co.copy() for i, v in enumerate(obj.data.vertices)}
    baked(obj)
    # A point whose ray met nothing is still on the starting plane: drop it
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    missed = [v for v in bm.verts if (v.co - start[v.index]).length < 1e-4]
    if missed:
        print(f"decal {name}: {len(missed)} of {len(bm.verts)} points missed the body")
        bmesh.ops.delete(bm, geom=missed, context="VERTS")
    # Face the viewer of that view, for the shading
    toward = {"x": Vector((1, 0, 0)), "y": Vector((0, 1, 0)), "z": Vector((0, 0, 1))}[axis] * (-1 if positive else 1)
    bm.normal_update()
    for f in bm.faces:
        if f.normal.dot(toward) < 0:
            f.normal_flip()
    bm.to_mesh(obj.data)
    bm.free()
    for p in obj.data.polygons:
        p.use_smooth = True
    return obj


def both_sides(name, body, outlines, mat, **kw):
    return [decal(f"{name}_{view}", body, view, outlines, mat, **kw) for view in ("left", "right")]


def mirrored(outlines):
    """Front / rear / top outlines given for the car's left, with their mirror for the right."""
    return outlines + [[(-u, v) for u, v in o] for o in outlines]


def ellipse(cu, cv, ru, rv, n=20):
    return [(cu + ru * math.cos(2 * math.pi * i / n), cv + rv * math.sin(2 * math.pi * i / n)) for i in range(n)]


def squircle(u0, v0, u1, v1, power=3.0, n=28):
    """A rectangle rounded all the way: a superellipse filling the box."""
    cu, cv, ru, rv = (u0 + u1) / 2, (v0 + v1) / 2, (u1 - u0) / 2, (v1 - v0) / 2
    out = []
    for i in range(n):
        c, d = math.cos(2 * math.pi * i / n), math.sin(2 * math.pi * i / n)
        out.append((cu + ru * math.copysign(abs(c) ** (2 / power), c), cv + rv * math.copysign(abs(d) ** (2 / power), d)))
    return out


def rounded(u0, v0, u1, v1, r=0.012):
    """A rectangle with its corners cut (still convex)."""
    return [(u0 + r, v0), (u1 - r, v0), (u1, v0 + r), (u1, v1 - r), (u1 - r, v1), (u0 + r, v1), (u0, v1 - r), (u0, v0 + r)]


def build_decals(body, mats):
    made = []
    # Side glass, front to back: front door, rear door, and the quarter light
    # with the kink forward at the foot of the C-pillar
    side_glass = [
        [(1.66, 0.958), (2.465, 0.972), (2.465, 1.300), (2.23, 1.303), (2.07, 1.262), (1.86, 1.125)],
        [(2.555, 0.974), (3.335, 0.990), (3.335, 1.245), (3.20, 1.283), (2.555, 1.300)],
        [(3.395, 0.992), (3.585, 0.997), (3.645, 1.052), (3.395, 1.222)],
    ]
    made += both_sides("side_glass", body, side_glass, mats["glass"], offset=0.006, cell=0.02)
    # The black pillars between them
    made += both_sides("pillars", body, [[(2.465, 0.972), (2.555, 0.974), (2.555, 1.300), (2.465, 1.300)],
                                         [(3.335, 0.990), (3.395, 0.992), (3.395, 1.222), (3.335, 1.245)]],
                       mats["black"], offset=0.006, cell=0.02)
    # Shut lines of the doors, hood and trunk, as seen from the side
    line = 0.007
    sill = 0.235
    shuts = [
        [(1.50, sill), (1.50 + line, sill), (1.56 + line, 0.93), (1.56, 0.93)],            # front door, leading edge
        [(2.51 - line / 2, sill), (2.51 + line / 2, sill), (2.51 + line / 2, 0.965), (2.51 - line / 2, 0.965)],
        [(3.37 - line / 2, 0.72), (3.37 + line / 2, 0.72), (3.37 + line / 2, 0.985), (3.37 - line / 2, 0.985)],
        [(1.50, sill), (3.02, sill), (3.02, sill + line), (1.50, sill + line)],            # along the sill
        [(3.02, sill), (3.02 + line, sill), (3.37 + line / 2, 0.72), (3.37 - line / 2, 0.72)],  # up round the rear arch
    ]
    made += both_sides("shut_lines", body, shuts, mats["shut"], offset=0.002, cell=0.05)
    made += both_sides("handles", body, [rounded(2.27, 0.868, 2.43, 0.896), rounded(3.13, 0.882, 3.29, 0.910)],
                       mats["black"], cell=0.05)
    made += both_sides("side_repeater", body, [rounded(1.18, 0.735, 1.27, 0.757, 0.008)], mats["amber"], cell=0.05)
    made += both_sides("rub_strip", body, [[(1.10, 0.555), (3.10, 0.575), (3.10, 0.590), (1.10, 0.570)]],
                       mats["shut"], offset=0.002, cell=0.06)

    # Windshield and rear glass
    made.append(decal("windshield", body, "front",
                      [[(-0.665, 1.02), (0.665, 1.02), (0.525, 1.338), (-0.525, 1.338)]], mats["glass"], offset=0.006, cell=0.02))
    made.append(decal("rear_glass", body, "rear",
                      [[(-0.62, 1.10), (0.62, 1.10), (0.51, 1.335), (-0.51, 1.335)]], mats["glass"], offset=0.006, cell=0.02))
    made.append(decal("sunroof", body, "top", [rounded(-0.38, 2.24, 0.38, 2.72, 0.04)], mats["glass"], offset=0.005, cell=0.025))

    # Seen from above: the black cowl under the wipers, the hood's and the trunk lid's shut lines
    made.append(decal("cowl", body, "top", [[(-0.70, 1.375), (0.70, 1.375), (0.70, 1.445), (-0.70, 1.445)]],
                      mats["black"], cell=0.03))
    lid = 0.007
    made.append(decal("lid_lines", body, "top", mirrored([
        [(0.70, 1.375), (0.70 + lid, 1.375), (0.60 + lid, 0.14), (0.60, 0.14)],          # hood, along the fender
        [(0.62, 3.925), (0.62 + lid, 3.925), (0.60 + lid, 4.40), (0.60, 4.40)],          # trunk lid
    ]) + [[(-0.62, 3.92), (0.62, 3.92), (0.62, 3.92 + lid), (-0.62, 3.92 + lid)]], mats["shut"], offset=0.002, cell=0.05))

    # Front: headlamps, the two grilles, the M bumper's intakes
    lamp = [(0.305, 0.662), (0.60, 0.644), (0.785, 0.680), (0.80, 0.740), (0.62, 0.766), (0.305, 0.752)]
    made.append(decal("headlamps", body, "front", mirrored([lamp]), mats["lens"], offset=0.005, cell=0.012))
    made.append(decal("projectors", body, "front",
                      mirrored([ellipse(0.41, 0.706, 0.050, 0.036), ellipse(0.575, 0.706, 0.056, 0.040)]),
                      mats["projector"], offset=0.009, cell=0.012))
    made.append(decal("front_signals", body, "front", mirrored([[(0.70, 0.694), (0.785, 0.699), (0.79, 0.738), (0.70, 0.752)]]),
                      mats["amber"], offset=0.009, cell=0.012))
    kidney = squircle(0.016, 0.668, 0.258, 0.752)
    inner = squircle(0.028, 0.679, 0.246, 0.741)
    made.append(decal("grille_surround", body, "front", mirrored([kidney]), mats["chrome"], cell=0.025))
    made.append(decal("grilles", body, "front", mirrored([inner]), mats["black"], offset=0.006, cell=0.025))
    made.append(decal("badge_front", body, "front", [ellipse(0, 0.786, 0.038, 0.020)], mats["badge"], cell=0.02))
    made.append(decal("intakes", body, "front",
                      [rounded(-0.33, 0.255, 0.33, 0.415, 0.03)]
                      + mirrored([[(0.40, 0.265), (0.70, 0.285), (0.735, 0.40), (0.40, 0.42)]]), mats["black"], cell=0.03))
    made.append(decal("fog_lamps", body, "front", mirrored([ellipse(0.565, 0.345, 0.050, 0.038)]),
                      mats["lens"], offset=0.006, cell=0.02))

    # Rear: lamps, plate, badge, the black valance
    tail = [(0.33, 0.800), (0.74, 0.785), (0.785, 0.86), (0.765, 0.972), (0.33, 0.985)]
    made.append(decal("tail_lamps", body, "rear", mirrored([tail]), mats["red"], cell=0.025))
    made.append(decal("tail_clear", body, "rear", mirrored([[(0.36, 0.915), (0.75, 0.908), (0.745, 0.948), (0.36, 0.955)]]),
                      mats["clear"], offset=0.006, cell=0.025))
    made.append(decal("plate_recess", body, "rear", [rounded(-0.29, 0.775, 0.29, 0.95, 0.02)], mats["shut"], cell=0.03))
    made.append(decal("plate", body, "rear", [rounded(-0.152, 0.79, 0.152, 0.94, 0.008)], mats["plate"], offset=0.006, cell=0.03))
    made.append(decal("badge_rear", body, "rear", [ellipse(0, 0.995, 0.038, 0.026)], mats["badge"], cell=0.02))
    made.append(decal("valance", body, "rear", [[(-0.63, 0.345), (0.63, 0.345), (0.56, 0.455), (-0.56, 0.455)]],
                      mats["black"], cell=0.03))
    # Side of the lamps, where they wrap the corners
    made += both_sides("tail_lamp_sides", body, [[(4.26, 0.825), (4.41, 0.805), (4.41, 0.965), (4.29, 0.975)]],
                       mats["red"], cell=0.025)
    made += both_sides("headlamp_sides", body, [[(0.10, 0.672), (0.24, 0.705), (0.24, 0.770), (0.11, 0.748)]],
                       mats["lens"], offset=0.005, cell=0.012)
    return made


# ── Wheels, mirrors, exhaust ────────────────────────────────────────────────

def lathe(bm, profile, steps=48):
    """Spin an (axial, radius) profile round the X axis."""
    verts = [bm.verts.new((a, 0, r)) for a, r in profile]
    edges = [bm.edges.new((a, b)) for a, b in zip(verts, verts[1:], strict=False)]
    bmesh.ops.spin(bm, geom=verts + edges, cent=(0, 0, 0), axis=(1, 0, 0), angle=2 * math.pi, steps=steps)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)


def build_wheel(name, mats, width):
    """One wheel, axle along X, outer face towards +X, origin at its centre."""
    h = width / 2
    tyre = bmesh.new()
    lathe(tyre, [(-h + 0.01, 0.232), (-h, 0.262), (-h + 0.004, 0.296), (-h + 0.028, 0.314), (0, TYRE_RADIUS),
                 (h - 0.028, 0.314), (h - 0.004, 0.296), (h, 0.262), (h - 0.01, 0.232)])
    bmesh.ops.recalc_face_normals(tyre, faces=tyre.faces)
    parts = [new_object(name + "_tyre", tyre, mats["rubber"])]

    rim = bmesh.new()
    lathe(rim, [(-h + 0.01, 0.234), (-h + 0.012, 0.218), (h - 0.05, 0.205), (h - 0.03, 0.212), (h - 0.012, 0.222),
                (h - 0.004, 0.236), (h - 0.012, 0.240)])
    lathe(rim, [(h - 0.035, 0.0), (h - 0.022, 0.03), (h - 0.022, 0.062), (h - 0.03, 0.072)], steps=32)   # hub
    # Ten spokes in five pairs, from the hub to the rim
    for i in range(5):
        for half in (-1, 1):
            angle = 2 * math.pi * i / 5 + half * math.radians(9)
            made = bmesh.ops.create_cube(rim, size=1.0)["verts"]
            bmesh.ops.scale(rim, verts=made, vec=(0.016, 0.020, 0.160))
            bmesh.ops.translate(rim, verts=made, vec=(h - 0.034, 0, 0.134))
            bmesh.ops.rotate(rim, verts=made, cent=(0, 0, 0), matrix=Matrix.Rotation(angle, 3, "X"))
    bmesh.ops.recalc_face_normals(rim, faces=rim.faces)
    parts.append(new_object(name + "_rim", rim, mats["rim"], sharp_angle=35))

    disc = bmesh.new()
    lathe(disc, [(h - 0.075, 0.0), (h - 0.075, 0.165), (h - 0.095, 0.165), (h - 0.095, 0.203), (-h + 0.02, 0.203)], steps=32)
    bmesh.ops.recalc_face_normals(disc, faces=disc.faces)
    parts.append(new_object(name + "_disc", disc, mats["disc"], sharp_angle=35))

    wheel = join(name, parts)
    return wheel


def join(name, objects):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.name = obj.data.name = name
    return obj


def build_wheels(mats):
    wheels = []
    for name, s, lat, width in (("wheel_fl", FRONT_AXLE, TRACK_FRONT, 0.225), ("wheel_fr", FRONT_AXLE, -TRACK_FRONT, 0.225),
                                ("wheel_rl", REAR_AXLE, TRACK_REAR, 0.255), ("wheel_rr", REAR_AXLE, -TRACK_REAR, 0.255)):
        wheel = build_wheel(name, mats, width)
        wheel.location = world(lat, s, TYRE_RADIUS)
        if lat < 0:
            wheel.rotation_euler = (0, 0, math.pi)
        wheels.append(wheel)
    return wheels


def build_mirrors(mats):
    parts = []
    for side in (1, -1):
        cap = bmesh.new()
        bmesh.ops.create_uvsphere(cap, u_segments=20, v_segments=12, radius=1.0)
        bmesh.ops.scale(cap, verts=cap.verts, vec=(0.085, 0.040, 0.050))
        bmesh.ops.translate(cap, verts=cap.verts, vec=world(side * 0.875, 1.70, 0.975))
        parts.append(new_object("mirror_cap", cap, mats["paint"]))
        stalk = bmesh.new()
        bmesh.ops.create_cube(stalk, size=1.0)
        bmesh.ops.scale(stalk, verts=stalk.verts, vec=(0.05, 0.05, 0.03))
        bmesh.ops.translate(stalk, verts=stalk.verts, vec=world(side * 0.80, 1.71, 0.955))
        parts.append(new_object("mirror_stalk", stalk, mats["black"], smooth=False))
    return parts


def build_exhaust(mats):
    parts = []
    for lat in (0.345, 0.435):   # two tips, on the left
        bm = bmesh.new()
        bmesh.ops.create_cone(bm, cap_ends=False, segments=24, radius1=0.036, radius2=0.036, depth=0.10)
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.pi / 2, 3, "X"))
        bmesh.ops.translate(bm, verts=bm.verts, vec=world(lat, LENGTH - 0.045, 0.315))
        parts.append(new_object("exhaust_tip", bm, mats["chrome"]))
    return parts


# ── Check pictures ──────────────────────────────────────────────────────────

def render(folder):
    scene = bpy.context.scene
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    world_ = bpy.data.worlds.new("sky")
    world_.use_nodes = True
    world_.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.58, 0.62, 1)
    world_.node_tree.nodes["Background"].inputs[1].default_value = 1.0
    scene.world = world_
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 3.0
    sun.rotation_euler = (math.radians(50), 0, math.radians(-35))
    scene.collection.objects.link(sun)
    ground = bmesh.new()
    bmesh.ops.create_grid(ground, x_segments=1, y_segments=1, size=30)
    new_object("ground", ground, material("ground", (0.35, 0.35, 0.36), 0.9), smooth=False)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.lens = 70
    scene.collection.objects.link(cam)
    scene.camera = cam
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 24
    scene.cycles.use_denoising = False
    scene.render.resolution_x, scene.render.resolution_y = 1000, 600
    target = Vector((0, 0, 0.62))
    for name, position in (("front34", (4.6, -6.8, 2.0)), ("rear34", (-4.6, 6.8, 2.0)), ("side", (9.0, 0, 0.9)),
                           ("front", (0, -9.0, 0.9)), ("top", (0.01, 0, 11.0))):
        cam.location = position
        cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = str(folder / f"bmw-{name}.png")
        bpy.ops.render.render(write_still=True)
    for o in (sun, cam, bpy.data.objects["ground"]):
        bpy.data.objects.remove(o)


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = make_materials()
    body = build_body(mats)
    decals = build_decals(body, mats)
    trim = join("trim", decals + build_mirrors(mats) + build_exhaust(mats))
    wheels = build_wheels(mats)
    for o in [trim, *wheels]:
        o.parent = body
    tris = sum(len(p.vertices) - 2 for o in bpy.data.objects if o.type == "MESH" for p in o.data.polygons)
    if "--render" in args:
        render(args[args.index("--render") + 1])
    OUT.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=str(OUT), export_format="GLB", use_selection=True, export_yup=True, export_apply=True)
    print(f"Wrote {OUT} ({OUT.stat().st_size // 1024} KiB, {tris} triangles)")


main()
