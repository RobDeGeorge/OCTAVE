"""Connect the pose rig's coil seats to its frame and axle tubes.

Mount dimensions are provisional packaging estimates, not a verified lift kit.
"""
import bpy
import math
from mathutils import Vector


def build_spring_mounts(body, lift):
    scene = bpy.context.scene
    scene.frame_set(1)
    for obj in list(scene.objects):
        if obj.name.startswith("Coil mount "):
            bpy.data.objects.remove(obj, do_unlink=True)
    steel = bpy.data.materials["Undercarriage steel"]

    def attach(obj, name, parent):
        obj.name = "Coil mount " + name
        obj.data.materials.append(steel)
        world = obj.matrix_world.copy()
        obj.parent = parent
        obj.matrix_world = world
        obj["rig_part"] = "body"
        return obj

    def box(name, center, size, parent):
        bpy.ops.mesh.primitive_cube_add(size=1, location=center)
        obj = bpy.context.object
        obj.dimensions = size
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        return attach(obj, name, parent)

    for label in ("front", "rear"):
        for sign in (-1, 1):
            key = f"{label}_{sign}"
            lower = bpy.data.objects[key+"_coil_axle"]
            upper = bpy.data.objects[key+"_coil_frame"]
            world = upper.matrix_world.copy()
            world.translation.x = sign*(.460 if label == "front" else .480)
            upper.matrix_world = world
            if label == "rear":
                # Leave the upper seat below the .594 m floor underside plus
                # suspension lift; the earlier .740 m hardpoint pierced it.
                world = upper.matrix_world.copy()
                world.translation.z = .566 - .9 + lift
                upper.matrix_world = world
            # The old shock line occupied the newly narrowed rail. Keep the
            # provisional shock assembly inboard, separate from the coil.
            for suffix,x in (("_shock_axle",.260),("_shock_frame",.240)):
                mount = bpy.data.objects[key+suffix]
                world = mount.matrix_world.copy()
                world.translation.x = sign*x
                mount.matrix_world = world
            bpy.context.view_layer.update()
            a, b = lower.matrix_world.translation, upper.matrix_world.translation
            # Move curve points to compress the coil, rather than scaling the
            # whole tube, which flattened the wire's circular cross-section.
            axis = bpy.data.objects[key+"_spring_axis"]
            axis.driver_remove("scale", 2)
            axis.scale = (1,1,1)
            spring = next(o for o in axis.children if o.name.startswith("Compressing lift spring"))
            for i,point in enumerate(spring.data.splines[0].points):
                fraction = i/(len(spring.data.splines[0].points)-1)
                # Terminate on the seats' fore/aft centerline so axle roll
                # does not lift a cut end off its seat. Keep the wire center
                # inside the seating faces, rather than through the top plate.
                angle = fraction*math.tau*6 + math.pi/2
                point.co.x,point.co.y = .067*math.cos(angle),.067*math.sin(angle)
                driver = spring.data.driver_add(f"splines[0].points[{i}].co",2).driver
                for variable in list(driver.variables):
                    driver.variables.remove(variable)
                variable = driver.variables.new()
                variable.name,variable.type = "length","LOC_DIFF"
                variable.targets[0].id,variable.targets[1].id = lower,upper
                driver.expression = f".015+{fraction}*(length-.030)"
            # A welded axle pedestal fills the space below the lower seat.
            bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=.040, depth=.032,
                                               location=a+Vector((0,0,-.024)))
            attach(bpy.context.object, key+" axle pedestal", lower)

            # Two vertical webs, outside the coil envelope, rise from the rail
            # to an upper bridge. Their feet overlap the rail top intentionally.
            rail_top = .540 - .9 + lift
            bridge_bottom = b.z+.007
            for direction in (-1, 1):
                y = b.y+direction*.101
                web_top = bridge_bottom+.002
                box(key+" tower web", (sign*.360,y,(rail_top-.010+web_top)/2),
                    (.040,.014,web_top-(rail_top-.010)), body)
                box(key+" seat bridge", (sign*.445,b.y+direction*.050,bridge_bottom+.003),
                    (.210,.116,.006), body)
            # Braces are rigid to the frame; the existing seat and spring
            # endpoint still reference the same body-mounted upper hardpoint.
    bpy.context.view_layer.update()
