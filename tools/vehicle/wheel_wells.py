"""Body-mounted inner liners and a packaged exhaust route for the lifted rig.

Dimensions are visual packaging estimates, not fabrication measurements.
"""
import math
import bmesh
import bpy


def build_wheel_wells(body, lift):
    for obj in list(bpy.context.scene.objects):
        if obj.name.startswith(("Inner wheel well", "Rear wheel housing")) or obj.name == "Exhaust pipe":
            bpy.data.objects.remove(obj, do_unlink=True)
    offset = -.9 + lift

    def attach(obj, material):
        obj.data.materials.append(bpy.data.materials[material])
        world = obj.matrix_world.copy()
        obj.parent = body
        obj.matrix_world = world
        obj["rig_part"] = "body"
        return obj

    # Derive the mouth from the actual seven-vertex flare cross-sections.
    # The old ellipse left a second, disconnected arch inside that opening.
    for label in ("front", "rear"):
        flares = [o for o in bpy.context.scene.objects
                  if o.type == "MESH" and o.name in
                  {label.title()+" flare", label.title()+" flare.001"}]
        for flare in flares:
            points = [flare.matrix_world @ v.co for v in flare.data.vertices]
            side = 1 if sum(p.x for p in points) > 0 else -1
            # Row 6 is the underside seam at the body side of the flare.
            mouth = [tuple(points[i]) for i in range(6,len(points),7)]
            from mathutils import Vector
            sampled = []
            for a,b in zip(mouth,mouth[1:]):
                a,b = Vector(a),Vector(b)
                count = max(1,math.ceil((b-a).length/.03))
                sampled.extend(tuple(a.lerp(b,i/count)) for i in range(count))
            mouth = sampled+[mouth[-1]]
            n = len(mouth)
            base = (.60 if label == "front" else .69)+offset
            start_y,end_y = (-1.70,-.65) if label == "front" else (.70,1.65)
            def inner_y(y):
                return start_y+(end_y-start_y)*(y-mouth[0][1])/(mouth[-1][1]-mouth[0][1])
            roof = [(side*(.56 if label == "front" else .55), inner_y(y),
                     z-.075*max(0.,min(1.,(z-offset-.65)/.30))) for x,y,z in mouth]
            knee_z = (.78 if label == "front" else .76)+offset
            def lower_x(y):
                if label == "rear":
                    return .55
                t = max(0.,min(1.,(abs(y+1.186)-.13)/.13))
                x = .31+.15*t*t*(3-2*t)
                nose = max(0.,min(1.,(-y-1.45)/.08))
                return x+(.58-x)*nose
            shoulder = [(x,y,min((.84 if label == "front" else .76)+offset,z)) for x,y,z in roof]
            knee = [(side*lower_x(y),y,min(knee_z,z)) for x,y,z in shoulder]
            def lower_z(y):
                if label == "rear": return base
                pocket = max(0.,min(1.,(.27-abs(y+1.186))/.14))
                nose = max(0.,min(1.,(-y-1.35)/.15))
                return base+.10*max(pocket,nose)
            bottom = [(x,y,min(lower_z(y),z)) for x,y,z in knee]
            verts = mouth+roof+shoulder+knee+bottom
            faces = [(r*n+i,r*n+i+1,(r+1)*n+i+1,(r+1)*n+i)
                     for r in range(4) for i in range(n-1)]
            for i in (0,n-1):
                j=len(verts)
                verts.append((mouth[i][0],mouth[i][1],bottom[i][2]))
                faces.append((i,n+i,2*n+i,3*n+i,4*n+i,j))
            mesh = bpy.data.meshes.new("Body fitted wheel housing")
            mesh.from_pydata(verts, [], faces)
            mesh.update()
            bm = bmesh.new()
            bm.from_mesh(mesh)
            bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-7)
            bmesh.ops.dissolve_degenerate(bm, edges=list(bm.edges), dist=1e-7)
            bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
            bm.to_mesh(mesh)
            bm.free()
            obj = bpy.data.objects.new(f"Inner wheel well {label} {side}",mesh)
            bpy.context.collection.objects.link(obj)
            attach(obj,"Textured black trim")
            obj["flare"] = flare.name
            obj["mouth_vertices"] = n
            obj["base_z"] = base
            solid = obj.modifiers.new("Molded liner thickness", "SOLIDIFY")
            solid.thickness = .003
            solid.offset = 0
            # Flat wall normals prevent lighting from smearing across corners.
            for face in mesh.polygons:
                face.use_smooth = False

    # Trim fitted joints against solid body hardware. The surrounding part
    # fills each trimmed region, so two surfaces do not occupy the same space.
    # Exterior sheet-metal seams stay shared with the flare/body outline.
    trims = ("Front footwell", "Rear passenger floor", "Rear passenger carpet",
             "Raised cargo floor", "Cargo carpet", "Door aperture sill",
             "Rocker rail", "Molded side step", "Rear bumper channel",
             "Padded roll bar", "Rear roll bar brace", "Fog lamp shell",
             "Cowl firewall")
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    cutters = []
    for source in list(bpy.context.scene.objects):
        if source.type not in {"MESH", "CURVE"} or not source.name.startswith(trims):
            continue
        evaluated = source.evaluated_get(dg)
        data = bpy.data.meshes.new_from_object(evaluated, depsgraph=dg)
        data.transform(evaluated.matrix_world)
        # A small fitted clearance prevents coincident faces at the joint.
        from mathutils import Vector
        center = sum((v.co for v in data.vertices), Vector())/len(data.vertices)
        for vertex in data.vertices:
            vertex.co = center+(vertex.co-center)*1.004
        bm = bmesh.new()
        bm.from_mesh(data)
        if source.name.startswith("Fog lamp shell"):
            # Use a closed clearance box for the lamp's open-backed shell.
            low = Vector(tuple(min(v.co[i] for v in bm.verts)-.003 for i in range(3)))
            high = Vector(tuple(max(v.co[i] for v in bm.verts)+.003 for i in range(3)))
            bm.clear()
            bmesh.ops.create_cube(bm, size=1)
            for vertex in bm.verts:
                vertex.co = Vector(tuple((low[i]+high[i])/2+vertex.co[i]*(high[i]-low[i]) for i in range(3)))
        # Rendered tubes can have open ends; a boolean cutter must be solid.
        bmesh.ops.holes_fill(bm, edges=[e for e in bm.edges if e.is_boundary], sides=0)
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        bm.to_mesh(data)
        bm.free()
        cutter = bpy.data.objects.new("Temporary well joint cutter", data)
        bpy.context.collection.objects.link(cutter)
        cutters.append(cutter)
    for obj in [o for o in bpy.context.scene.objects if o.name.startswith("Inner wheel well")]:
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier="Molded liner thickness")
        for cutter in cutters:
            # Broad-phase bounds avoid boolean work on distant parts.
            def bounds(o):
                from mathutils import Vector
                pts = [o.matrix_world @ Vector(p) for p in o.bound_box]
                return [(min(p[i] for p in pts),max(p[i] for p in pts)) for i in range(3)]
            a,b = bounds(obj),bounds(cutter)
            if not all(a[i][1]>=b[i][0] and b[i][1]>=a[i][0] for i in range(3)):
                continue
            mod = obj.modifiers.new("Fitted body joint", "BOOLEAN")
            mod.operation = "DIFFERENCE"
            mod.solver = "EXACT"
            mod.object = cutter
            previous = obj.data.copy()
            bpy.ops.object.modifier_apply(modifier=mod.name)
            # Reject a failed solid operation that removes the fender seam
            # (some legacy body panels are unsuitable boolean volumes).
            from mathutils.bvhtree import BVHTree
            vs = [obj.matrix_world @ v.co for v in obj.data.vertices]
            tree = BVHTree.FromPolygons(vs, [tuple(p.vertices) for p in obj.data.polygons]) if vs else None
            flare = bpy.data.objects[obj["flare"]]
            seam = [flare.matrix_world @ v.co for v in list(flare.data.vertices)[6::7]]
            if tree is None or any(tree.find_nearest(p)[3] > .02 for p in seam if p.z > .80+offset):
                failed = obj.data
                obj.data = previous
                bpy.data.meshes.remove(failed)
            else:
                bpy.data.meshes.remove(previous)
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-7)
        bmesh.ops.dissolve_degenerate(bm, edges=list(bm.edges), dist=1e-8)
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        bm.to_mesh(obj.data)
        bm.free()
    for cutter in cutters:
        data = cutter.data
        bpy.data.objects.remove(cutter, do_unlink=True)
        bpy.data.meshes.remove(data)

    # Both front housings have the same envelope and symmetric body joints.
    # Mirror the finished solid so numerical boolean differences cannot leave
    # a lamp recess closed on just one side.
    from mathutils import Matrix
    right = bpy.data.objects["Inner wheel well front 1"]
    left = bpy.data.objects["Inner wheel well front -1"]
    previous = left.data
    left.data = right.data.copy()
    left.data.transform(Matrix.Diagonal((-1.,1.,1.,1.)))
    bm = bmesh.new()
    bm.from_mesh(left.data)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(left.data)
    bm.free()
    bpy.data.meshes.remove(previous)

    # Stay inboard of the rear coil and rail, above the axle/tank, then turn
    # outboard behind the tank. Retain the original downpipe connection.
    muffler = bpy.data.objects["Exhaust muffler"]
    world = muffler.matrix_world.copy()
    points = [muffler.matrix_world @ v.co for v in muffler.data.vertices]
    center_x = (min(p.x for p in points)+max(p.x for p in points))/2
    world.translation.x += .27-center_x
    muffler.matrix_world = world
    points = [(.35,-.50,.40),(.35,-.20,.40),(.27,.25,.40),
              (.27,.64,.40),(.14,.93,.55),(.14,1.19,.55),
              (.20,1.55,.55),(.30,1.70,.54),(.51,1.80,.40),
              (.53,1.91,.37)]
    curve = bpy.data.curves.new("Exhaust smooth bends", "CURVE")
    curve.dimensions = "3D"
    curve.bevel_depth = .022
    curve.bevel_resolution = 4
    curve.resolution_u = 16
    spline = curve.splines.new("BEZIER")
    spline.bezier_points.add(len(points)-1)
    for p,(x,y,z) in zip(spline.bezier_points,points):
        p.co = (x,y,z+offset)
        p.handle_left_type = p.handle_right_type = "AUTO"
    obj = bpy.data.objects.new("Exhaust pipe",curve)
    bpy.context.collection.objects.link(obj)
    attach(obj,"Undercarriage steel")
