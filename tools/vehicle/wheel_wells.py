"""Body-mounted inner liners and a packaged exhaust route for the lifted rig.

Dimensions are visual packaging estimates, not fabrication measurements.
"""
import math
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

    for label, center, radius, inner, outer in (
            ("front", -1.186, .65, .56, .755),
            ("rear", 1.186, .62, .55, .755)):
        for side in (-1, 1):
            profile = [(center + (.57 if label == "front" else .53)*math.cos(math.radians(a)),
                        .40 + radius*math.sin(math.radians(a)))
                       for a in range(25, 156, 5)]
            n = len(profile)
            verts = [(side*x, y, z+offset) for x in (inner, outer) for y,z in profile]
            faces = [(i,i+1,n+i+1,n+i) for i in range(n-1)]
            # The inboard wall closes the view into the body above the spring
            # towers, while retaining the lower suspension opening.
            verts += [(side*inner,y,min(z-.008,.84 if label == "front" else .69)+offset)
                      for y,z in profile]
            faces += [(i,i+1,2*n+i+1,2*n+i) for i in range(n-1)]
            mesh = bpy.data.meshes.new("Wheel liner shell")
            mesh.from_pydata(verts, [], faces)
            mesh.update()
            obj = bpy.data.objects.new(f"Inner wheel well {label} {side}",mesh)
            bpy.context.collection.objects.link(obj)
            attach(obj,"Textured black trim")
            solid = obj.modifiers.new("Molded liner thickness", "SOLIDIFY")
            solid.thickness = .004
            for face in mesh.polygons:
                face.use_smooth = True

    # Stay inboard of the rear coil and rail, above the axle/tank, then turn
    # outboard behind the tank. Retain the original downpipe connection.
    muffler = bpy.data.objects["Exhaust muffler"]
    world = muffler.matrix_world.copy()
    world.translation.x = .27
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
