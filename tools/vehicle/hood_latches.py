"""Photo-interpreted TJ rubber hood catches; dimensions are estimates in metres.

Shape/placement: cached reference 028, IMG_7900-71284-scaled.jpeg.
Assembly reference: Mopar 2003 Wrangler hood/hood-release catalog, catch
55176636AD / 68038118AA. This is not manufacturer CAD or a latch simulation.
"""
import math

import bpy
from mathutils import Vector


def build_hood_latches(body=None, hood=None, z_offset=0.):
    for obj in list(bpy.context.scene.objects):
        if obj.name.startswith(("Hood latch", "Latch hinge")):
            bpy.data.objects.remove(obj, do_unlink=True)
    plastic = bpy.data.materials["Textured black trim"]
    rubber = bpy.data.materials["Tire rubber"]
    metal = bpy.data.materials["Undercarriage steel"]
    created = []

    def finish(obj, name, mat, part, bevel=0.):
        obj.name = "Hood latch " + name
        obj.data.materials.append(mat)
        if bevel:
            modifier = obj.modifiers.new("Molded latch edges", "BEVEL")
            modifier.width, modifier.segments = bevel, 3
            bpy.context.view_layer.objects.active = obj
            bpy.ops.object.modifier_apply(modifier=modifier.name)
            modifier = obj.modifiers.new("Latch face normals", "WEIGHTED_NORMAL")
            bpy.ops.object.modifier_apply(modifier=modifier.name)
        obj["rig_part"] = part
        parent = hood if part == "hood" else body
        if parent:
            world = obj.matrix_world.copy()
            obj.parent = parent
            obj.matrix_world = world
        created.append(obj)
        return obj

    for side in (-1, 1):
        # The hood widens rearward; follow that surface rather than burying
        # a constant-X mounting plate in one end of the tapered skirt.
        def box(name, x, y, z, size, mat=plastic, part="body", bevel=.002):
            bpy.ops.mesh.primitive_cube_add(size=1, location=(side*x, y, z+z_offset))
            obj = bpy.context.object
            obj.dimensions = size
            bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
            obj.rotation_euler.z = -side*math.atan(.17)
            return finish(obj, name, mat, part, bevel)

        def pin(name, x, z, length, radius, part="body"):
            bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=radius, depth=length,
                                               location=(side*x,-1.51,z+z_offset))
            obj = bpy.context.object
            obj.rotation_euler = Vector((side*.17,1,0)).to_track_quat('Z','Y').to_euler()
            return finish(obj, name, metal, part, .0007)

        # Thin horizontal flange rests on the fender; ears carry the fore/aft pin.
        box("fender foot", .625,-1.51,1.088, (.048,.055,.010))
        for offset in (-.021,.021):
            box("lower pivot ear", .625+.17*offset,-1.51+offset,1.102, (.023,.009,.024))
        pin("lower pivot pin", .625,1.109,.052,.004)

        # Separate hood-side backing and upper catch, fixed to the moving hood.
        box("hood backing plate", .600,-1.51,1.184, (.010,.048,.068), part="hood", bevel=.004)
        box("upper catch cap", .615,-1.51,1.209, (.028,.047,.027), part="hood", bevel=.004)
        for offset in (-.019,.019):
            box("upper catch cheek", .615+.17*offset,-1.51+offset,1.183,
                (.025,.008,.037), part="hood", bevel=.0025)
        pin("catch cross pin", .620,1.194,.046,.004, "hood")

        # Narrow rubber tension strap with a wider pull head. The paired side
        # rails leave real openings around the strap instead of painted slots.
        box("rubber tension strap", .629,-1.51,1.150, (.012,.020,.078), rubber, bevel=.004)
        for offset in (-.017,.017):
            box("pull handle rail", .639+.17*offset,-1.51+offset,1.157,
                (.012,.006,.070), bevel=.0025)
        box("pull handle lower bridge", .639,-1.51,1.124, (.014,.040,.012), bevel=.003)
        box("pull handle head", .636,-1.51,1.192, (.020,.040,.016), bevel=.004)
    return created
