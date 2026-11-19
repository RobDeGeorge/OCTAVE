"""Shared machinery for the vehicles built from a script (build_bmw_e46.py and the like).

A car here is one shell lofted through cross-sections, then cut into what a real car is
made of: doors, a hood and a trunk lid that are their own parts with a gap round them,
glass you can see through, an inner skin behind the outer one, and an interior. The
parts are exported as a rig the app's RigVehicle.qml assembles: one .glb per moving part,
each about its own pivot, and a rig.json that says where the pivots are.

Coordinates in a build script: s = metres back from the front bumper, lat = metres to the
car's left, z = metres up from the road. In Blender that is x = lat, y = s - length / 2,
z = z, which the glTF exporter turns into the app's +Z forward / +Y up / +X left.

Used from Blender only (bpy, bmesh): blender -b -P tools/vehicle/build_<car>.py
"""

import json
import math
from pathlib import Path

import bmesh
import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree
from mathutils.interpolate import poly_3d_calc

# ── Curves ──────────────────────────────────────────────────────────────────


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


def smoothstep(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


# ── The car's frame ─────────────────────────────────────────────────────────


class Frame:
    """Converts between a build script's (lat, s, z) and Blender's (x, y, z)."""

    def __init__(self, length):
        self.length = length

    def world(self, lat, s, z):
        return Vector((lat, s - self.length / 2, z))

    def car(self, co):
        return (co.x, co.y + self.length / 2, co.z)


# ── Objects and materials ───────────────────────────────────────────────────


def material(name, colour, roughness=0.5, metallic=0.0, alpha=1.0, emission=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*colour, 1)
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    if alpha < 1:
        bsdf.inputs["Alpha"].default_value = alpha
        m.surface_render_method = "BLENDED"
    if emission:
        bsdf.inputs["Emission Color"].default_value = (*emission, 1)
        bsdf.inputs["Emission Strength"].default_value = 0.6
    m.use_backface_culling = False
    return m


def new_object(name, bm, mats=None, smooth=True, sharp_angle=None):
    """A mesh object from a bmesh (which is freed). `mats` is one material or a list."""
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    for p in mesh.polygons:
        p.use_smooth = smooth
    if sharp_angle is not None:
        mesh.set_sharp_from_angle(angle=math.radians(sharp_angle))
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    if mats is not None:
        for m in (mats if isinstance(mats, (list, tuple)) else [mats]):
            mesh.materials.append(m)
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


def join(name, objects):
    objects = [o for o in objects if o is not None]
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    if len(objects) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.name = obj.data.name = name
    return obj


def slot(obj, mat):
    """The index of a material on an object, adding it if it is not there."""
    names = [m.name for m in obj.data.materials]
    if mat.name not in names:
        obj.data.materials.append(mat)
        return len(names)
    return names.index(mat.name)


# ── The shell ───────────────────────────────────────────────────────────────


def loft(frame, stations, section, creases=(), levels=3, cap_inset=(0.93, 0.92)):
    """A closed shell through section(s) (half a cross-section: (lat, z) points from the
    middle of the underside round to the middle of the top) at every station, rounded by
    a subdivision surface. `creases` are (index into section, weight) lines kept defined."""
    bm = bmesh.new()
    rings = []
    for i, s in enumerate(stations):
        half = section(s)
        # The two ends are a little smaller than the section just inside them,
        # so the caps come out as rounded faces, not a sharp rim
        if i in (0, len(stations) - 1):
            mid_z = (half[0][1] + half[-1][1]) / 2
            half = [(lat * cap_inset[0], mid_z + (z - mid_z) * cap_inset[1]) for lat, z in half]
        ring = [bm.verts.new(frame.world(lat, s, z)) for lat, z in half]
        ring += [bm.verts.new(frame.world(-lat, s, z)) for lat, z in reversed(half[1:-1])]
        rings.append(ring)
    n = len(rings[0])
    for a, b in zip(rings, rings[1:], strict=False):
        for j in range(n):
            bm.faces.new((a[j], a[(j + 1) % n], b[(j + 1) % n], b[j]))
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    layer = bm.edges.layers.float.new("crease_edge")
    for k, weight in creases:
        for j in (k, n - k):
            for a, b in zip(rings, rings[1:], strict=False):
                bm.edges.get((a[j], b[j]))[layer] = weight
    body = new_object("body", bm)
    sub = body.modifiers.new("subdivide", "SUBSURF")
    sub.levels = sub.render_levels = levels
    return baked(body)


def cut_arch(body, centre, radius, depth, liner_slot):
    """A wheel pocket: a cylinder across the car at `centre` (Blender coordinates) taken
    out of the body, what it leaves given the material in `liner_slot`."""
    cbm = bmesh.new()
    bmesh.ops.create_cone(cbm, cap_ends=True, segments=72, radius1=radius, radius2=radius, depth=depth)
    bmesh.ops.rotate(cbm, verts=cbm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.pi / 2, 3, "Y"))
    bmesh.ops.translate(cbm, verts=cbm.verts, vec=centre)
    cutter = new_object("cutter", cbm)
    for m in body.data.materials:
        cutter.data.materials.append(m)
    for p in cutter.data.polygons:
        p.material_index = liner_slot
    cut = body.modifiers.new("arch", "BOOLEAN")
    cut.operation = "DIFFERENCE"
    cut.solver = "EXACT"
    cut.object = cutter
    baked(body)
    bpy.data.objects.remove(cutter)


# ── Cutting the shell into parts ────────────────────────────────────────────

# view -> (index of the Blender axis u runs along, index of the axis v runs along,
#          whether u or v is an `s`, which needs length / 2 taken off)
_VIEW = {"side": (1, 2, True, False), "front": (0, 2, False, False), "top": (0, 1, False, True)}


def _uv(frame, view, co):
    lat, s, z = frame.car(co)
    return {"side": (s, z), "front": (lat, z), "top": (lat, s)}[view]


def _plane(frame, view, a, b):
    """The plane through the 2D edge a-b of a view's outline, along the view's depth.
    Its normal points to the right of a -> b (outward, for a counter-clockwise outline)."""
    iu, iv, u_is_s, v_is_s = _VIEW[view]
    co, no = [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]
    co[iu] = a[0] - (frame.length / 2 if u_is_s else 0)
    co[iv] = a[1] - (frame.length / 2 if v_is_s else 0)
    no[iu], no[iv] = b[1] - a[1], -(b[0] - a[0])
    return Vector(co), Vector(no)


def counter_clockwise(outline):
    area = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(outline, outline[1:] + outline[:1], strict=True))
    return list(outline) if area > 0 else list(outline)[::-1]


def inset(outline, d):
    """The outline moved inwards by d on every edge (for outlines without tiny edges)."""
    pts = counter_clockwise(outline)
    n = len(pts)
    lines = []
    for i in range(n):
        a, b = pts[i], pts[(i + 1) % n]
        ex, ey = b[0] - a[0], b[1] - a[1]
        length = math.hypot(ex, ey)
        nx, ny = -ey / length, ex / length          # inward, for counter-clockwise
        lines.append(((a[0] + nx * d, a[1] + ny * d), (ex, ey)))
    out = []
    for i in range(n):
        (p, r), (q, t) = lines[i - 1], lines[i]
        cross = r[0] * t[1] - r[1] * t[0]
        if abs(cross) < 1e-12:
            out.append(q)
            continue
        k = ((q[0] - p[0]) * t[1] - (q[1] - p[1]) * t[0]) / cross
        out.append((p[0] + k * r[0], p[1] + k * r[1]))
    return out


def inside(outline, u, v):
    hit = False
    n = len(outline)
    for i in range(n):
        (a, b), (c, d) = outline[i], outline[(i + 1) % n]
        if (b > v) != (d > v) and u < a + (v - b) * (c - a) / (d - b):
            hit = not hit
    return hit


def _candidates(bm, frame, view, outline, where, skip=(), pad=0.02):
    us, vs = [p[0] for p in outline], [p[1] for p in outline]
    u0, u1, v0, v1 = min(us) - pad, max(us) + pad, min(vs) - pad, max(vs) + pad
    out = []
    for f in bm.faces:
        if f.material_index in skip:
            continue
        c = f.calc_center_median()
        u, v = _uv(frame, view, c)
        if u0 <= u <= u1 and v0 <= v <= v1 and where(frame.car(c), f.normal):
            out.append(f)
    return out


def _cut(bm, frame, view, outline, faces):
    """Cut the given faces along every edge of an outline; returns the faces afterwards."""
    pts = counter_clockwise(outline)
    for a, b in zip(pts, pts[1:] + pts[:1], strict=True):
        co, no = _plane(frame, view, a, b)
        if no.length < 1e-9:
            continue
        geom = set(faces)
        for f in faces:
            geom.update(f.edges)
            geom.update(f.verts)
        result = bmesh.ops.bisect_plane(bm, geom=list(geom), dist=1e-6, plane_co=co, plane_no=no.normalized())
        faces = [g for g in result["geom"] if isinstance(g, bmesh.types.BMFace) and g.is_valid]
    return faces


def mark(bm, frame, view, outline, where, layer, value, gap=0.0, gap_layer=None, gap_value=1, skip=()):
    """Give the faces inside an outline (a 2D polygon of a view, among the faces `where`
    accepts) `value` in an integer face layer, cutting the mesh along the outline first so
    the edge is exact. With `gap`, the band of that width just inside the outline gets
    `gap_value` in `gap_layer` instead: the shut line round a panel. Faces whose material
    index is in `skip` are left alone."""
    bm.normal_update()
    outer = counter_clockwise(outline)
    faces = _cut(bm, frame, view, outer, _candidates(bm, frame, view, outer, where, skip))
    inner = outer
    if gap > 0:
        inner = inset(outer, gap)
        faces = _cut(bm, frame, view, inner, faces)
    count = 0
    for f in faces:
        u, v = _uv(frame, view, f.calc_center_median())
        if inside(inner, u, v):
            f[layer] = value
            count += 1
        elif gap > 0 and inside(outer, u, v):
            f[gap_layer] = gap_value
    return count


def add_inner_skin(bm, faces, depth, material_index, rim_index, skin_layer=None):
    """A second skin `depth` behind the given faces (their copy, pushed in along the
    normals and turned round), joined to the first along every open edge, so a panel has
    a back and an edge and an opening has a jamb. Face layers are copied with the faces;
    the new faces get 1 in `skin_layer` (the outer skin keeps 0)."""
    faces = [f for f in faces if f.is_valid]
    if not faces:
        return
    bm.normal_update()
    chosen = set(faces)
    normals = {}
    for f in faces:
        for v in f.verts:
            normals.setdefault(v, Vector((0, 0, 0)))
            normals[v] += f.normal
    rim = [e for e in {e for f in faces for e in f.edges} if sum(1 for lf in e.link_faces if lf in chosen) == 1]
    template = {e: next(lf for lf in e.link_faces if lf in chosen) for e in rim}
    dup = bmesh.ops.duplicate(bm, geom=faces)
    vmap, fmap = dup["vert_map"], dup["face_map"]
    for v, n in normals.items():
        if n.length > 1e-9:
            vmap[v].co = v.co - n.normalized() * depth
    for f in faces:
        new = fmap[f]
        new.normal_flip()
        new.material_index = material_index
        if skin_layer is not None:
            new[skin_layer] = 1
    for e in rim:
        a, b = e.verts
        try:
            quad = bm.faces.new((a, b, vmap[b], vmap[a]), template[e])
        except ValueError:
            continue
        quad.material_index = rim_index
        if skin_layer is not None:
            quad[skin_layer] = 1


def extract(bm, faces, layers=()):
    """A new bmesh holding copies of the given faces, with their material and the integer
    face layers named in `layers`."""
    out = bmesh.new()
    pairs = [(bm.faces.layers.int[name], out.faces.layers.int.new(name)) for name in layers]
    copies = {}
    for f in faces:
        verts = []
        for v in f.verts:
            if v not in copies:
                copies[v] = out.verts.new(v.co)
            verts.append(copies[v])
        try:
            nf = out.faces.new(verts)
        except ValueError:
            continue
        nf.material_index = f.material_index
        nf.smooth = True
        for src, dst in pairs:
            nf[dst] = f[src]
    return out


class Surface:
    """A smooth surface to read from: the shell as it was before it was cut up. Gives the
    cut parts their shading back (a cut leaves long thin faces, which shade in streaks),
    and says where the skin is, to put trim on it."""

    def __init__(self, obj):
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.triangulate(bm, faces=bm.faces[:])
        bm.normal_update()
        bm.faces.ensure_lookup_table()
        self.tree = BVHTree.FromBMesh(bm)
        self.corners = [[v.co.copy() for v in f.verts] for f in bm.faces]
        self.normals = [[v.normal.copy() for v in f.verts] for f in bm.faces]
        bm.free()

    def normal(self, co):
        location, _normal, index, _distance = self.tree.find_nearest(co)
        weights = poly_3d_calc(self.corners[index], location)
        n = Vector((0, 0, 0))
        for w, vn in zip(weights, self.normals[index], strict=True):
            n += w * vn
        return n.normalized()

    def hit(self, origin, direction):
        """Where a ray meets the skin (Blender coordinates), or None."""
        location, _normal, _index, _distance = self.tree.ray_cast(Vector(origin), Vector(direction))
        return location


def shade_from(obj, surface, outer_slots, skin_layer="skin"):
    """Give the outer skin of a cut part (faces with 0 in `skin_layer` and a material in
    `outer_slots`) the smooth surface's normals; the rest keeps its own."""
    mesh = obj.data
    skin = mesh.attributes.get(skin_layer)
    normals = [(0.0, 0.0, 0.0)] * len(mesh.loops)
    cache = {}
    for poly in mesh.polygons:
        if poly.material_index not in outer_slots or (skin and skin.data[poly.index].value):
            continue
        for li in poly.loop_indices:
            vi = mesh.loops[li].vertex_index
            if vi not in cache:
                cache[vi] = tuple(surface.normal(mesh.vertices[vi].co))
            normals[li] = cache[vi]
    mesh.normals_split_custom_set(normals)


def turned(bm, frame, pivot, axis, degrees, verts=None):
    """Turn geometry about an axis ("lat", "s" or "z") through a point in car coordinates."""
    vector = {"lat": (1, 0, 0), "s": (0, 1, 0), "z": (0, 0, 1)}[axis]
    bmesh.ops.rotate(bm, verts=verts or bm.verts[:], cent=frame.world(*pivot),
                     matrix=Matrix.Rotation(math.radians(degrees), 3, Vector(vector)))
    return bm


# ── Primitives for trim and the interior ────────────────────────────────────


def box(frame, lat0, lat1, s0, s1, z0, z1, bevel=0.0, segments=2):
    """A box between two corners in car coordinates, its edges rounded by `bevel`."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, verts=bm.verts, vec=(abs(lat1 - lat0), abs(s1 - s0), abs(z1 - z0)))
    bmesh.ops.translate(bm, verts=bm.verts, vec=frame.world((lat0 + lat1) / 2, (s0 + s1) / 2, (z0 + z1) / 2))
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=segments, profile=0.5, affect="EDGES")
    return bm


def lathe(bm, profile, steps=48, axis=(1, 0, 0)):
    """Spin an (axial, radius) profile round the X axis (or another through the origin)."""
    verts = [bm.verts.new((a, 0, r)) for a, r in profile]
    edges = [bm.edges.new((a, b)) for a, b in zip(verts, verts[1:], strict=False)]
    bmesh.ops.spin(bm, geom=verts + edges, cent=(0, 0, 0), axis=axis, angle=2 * math.pi, steps=steps)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)


def tube(bm, points, radius, segments=10):
    """A round bar through points (Blender coordinates), with flat ends."""
    rings = []
    for i, p in enumerate(points):
        p = Vector(p)
        a = Vector(points[max(0, i - 1)])
        b = Vector(points[min(len(points) - 1, i + 1)])
        along = (b - a).normalized()
        side = along.orthogonal().normalized()
        up = along.cross(side)
        rings.append([bm.verts.new(p + radius * (math.cos(2 * math.pi * k / segments) * side
                                                   + math.sin(2 * math.pi * k / segments) * up))
                      for k in range(segments)])
    for a, b in zip(rings, rings[1:], strict=False):
        for k in range(segments):
            bm.faces.new((a[k], a[(k + 1) % segments], b[(k + 1) % segments], b[k]))
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])


# ── Decals: flat shapes laid onto the shell ─────────────────────────────────


def _clipped_grid(bm, outline, cell):
    """Add a grid of quads cut to a convex outline, in the plane z = 0."""
    outline = counter_clockwise(outline)
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
        normal = Vector((b[1] - a[1], -(b[0] - a[0]), 0))
        if normal.length < 1e-9:
            continue
        result = bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-6, plane_co=(a[0], a[1], 0),
                                        plane_no=normal.normalized(), clear_outer=True)
        geom = [g for g in result["geom"] if g.is_valid]


def decal(frame, name, target, view, outlines, mat, offset=0.003, cell=0.03):
    """Flat shapes drawn in a view ("left", "right", "front", "rear", "top"), laid onto
    `target`. `outlines` is a list of convex polygons in the view's 2D coordinates; a
    larger `offset` puts one decal over another."""
    length = frame.length
    place, axis, positive = {
        "left": (lambda u, v: frame.world(1.6, u, v), "x", False),      # (s, z)
        "right": (lambda u, v: frame.world(-1.6, u, v), "x", True),
        "front": (lambda u, v: frame.world(u, -0.6, v), "y", True),     # (lat, z)
        "rear": (lambda u, v: frame.world(u, length + 0.6, v), "y", False),
        "top": (lambda u, v: frame.world(u, v, 2.2), "z", False),       # (lat, s)
    }[view]
    bm = bmesh.new()
    for outline in outlines:
        _clipped_grid(bm, outline, cell)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    for v in bm.verts:
        v.co = place(v.co.x, v.co.y)
    obj = new_object(name, bm, mat)
    wrap = obj.modifiers.new("lay", "SHRINKWRAP")
    wrap.target = target
    wrap.wrap_method = "PROJECT"
    wrap.use_project_x, wrap.use_project_y, wrap.use_project_z = axis == "x", axis == "y", axis == "z"
    wrap.use_positive_direction = positive
    wrap.use_negative_direction = not positive
    wrap.offset = offset
    start = {i: v.co.copy() for i, v in enumerate(obj.data.vertices)}
    baked(obj)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    missed = [v for v in bm.verts if (v.co - start[v.index]).length < 1e-4]
    if missed:
        print(f"decal {name}: {len(missed)} of {len(bm.verts)} points missed the body")
        bmesh.ops.delete(bm, geom=missed, context="VERTS")
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


# ── The rig: one .glb per moving part ───────────────────────────────────────


def to_app(v):
    """A Blender point or direction in the app's frame (+X left, +Y up, +Z forward)."""
    return [round(v[0], 5), round(v[2], 5), round(-v[1], 5)]


def export_rig(folder, parts, origin, extra):
    """Write the rig: <folder>/<part>.glb for every part and <folder>/rig.json.

    parts: {name: {"objects": [...], "pivot": Vector (Blender), "axis": "X" | "Y" | "Z"
            (the app's axes), "openAngle": degrees, "parent": name or None, ...}}.
    origin: the Blender point that becomes the rig's origin (the middle of the car).
    Every part's geometry is written about its own pivot; pivots are stored relative to
    the origin (a child's relative to its parent's pivot)."""
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    manifest = {"units": "metres", "forward": "+Z", "left": "+X", "parts": {}}
    closed = swept = 0.0
    triangles = 0
    for name, part in parts.items():
        obj = join(name, part["objects"])
        pivot = Vector(part["pivot"])
        obj.data.transform(Matrix.Translation(-pivot) @ obj.matrix_world)
        obj.matrix_world = Matrix.Identity(4)
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
        bm.to_mesh(obj.data)
        triangles += len(bm.faces)
        bm.free()
        # The envelope: the part as it sits, and through its swing
        axis = {"X": "X", "Y": "Z", "Z": "Y"}[part["axis"]]      # the app's axis as Blender's
        sign = -1 if part["axis"] == "Z" else 1
        base = pivot - origin
        # (measured from the vertices: bound_box is stale right after a transform)
        xs, ys, zs = zip(*((v.co.x, v.co.y, v.co.z) for v in obj.data.vertices), strict=True)
        corners = [Vector((x, y, z)) for x in (min(xs), max(xs)) for y in (min(ys), max(ys)) for z in (min(zs), max(zs))]
        closed = max(closed, max((v.co + base).length for v in obj.data.vertices))
        for step in range(13 if part.get("openAngle") else 1):
            turn = Matrix.Rotation(math.radians(sign * part.get("openAngle", 0) * step / 12), 3, axis)
            swept = max(swept, *((turn @ c + base).length for c in corners))
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.ops.export_scene.gltf(filepath=str(folder / (name + ".glb")), export_format="GLB", use_selection=True,
                                  export_yup=True, export_apply=True, export_cameras=False, export_lights=False,
                                  export_animations=False)
        parent = part.get("parent")
        relative = pivot - (Vector(parts[parent]["pivot"]) if parent else origin)
        entry = {"file": name + ".glb", "pivot": to_app(relative), "axis": part["axis"],
                 "openAngle": part.get("openAngle", 0)}
        for key in ("parent", "tap", "role", "label", "radius"):
            if part.get(key) is not None:
                entry[key] = part[key]
        manifest["parts"][name] = entry
        part["object"] = obj
    manifest["bounds"] = {"closedRadius": round(closed, 4), "sweptRadius": round(max(swept, closed) + 0.03, 4)}
    manifest.update(extra)
    (folder / "rig.json").write_text(json.dumps(manifest, indent=2) + "\n")
    size = sum(p.stat().st_size for p in folder.glob("*.glb"))
    print(f"Rig written to {folder}: {len(parts)} parts, {triangles} triangles, {size // 1024} KiB")
    return manifest


# ── Check pictures ──────────────────────────────────────────────────────────


def render(folder, prefix, shots, target=(0, 0, 0.62), samples=24, size=(1000, 600)):
    """Studio pictures of what is in the scene: {name: camera position} -> <prefix>-<name>.png."""
    scene = bpy.context.scene
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    sky = bpy.data.worlds.new("sky")
    sky.use_nodes = True
    sky.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.58, 0.62, 1)
    sky.node_tree.nodes["Background"].inputs[1].default_value = 1.0
    scene.world = sky
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 3.0
    sun.rotation_euler = (math.radians(50), 0, math.radians(-35))
    scene.collection.objects.link(sun)
    ground = bmesh.new()
    bmesh.ops.create_grid(ground, x_segments=1, y_segments=1, size=30)
    floor = new_object("ground", ground, material("ground", (0.35, 0.35, 0.36), 0.9), smooth=False)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.lens = 70
    scene.collection.objects.link(cam)
    scene.camera = cam
    scene.render.engine = "CYCLES"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = False
    scene.render.resolution_x, scene.render.resolution_y = size
    for name, shot in shots.items():
        position, look, lens = (shot + (None, None))[:3] if isinstance(shot[0], (tuple, list)) else (shot, None, None)
        cam.location = position
        cam.data.lens = lens or 70
        aim = Vector(look or target)
        cam.rotation_euler = (aim - cam.location).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = str(folder / f"{prefix}-{name}.png")
        bpy.ops.render.render(write_still=True)
    for o in (sun, cam, floor):
        bpy.data.objects.remove(o)
