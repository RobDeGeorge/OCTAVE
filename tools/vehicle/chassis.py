"""Correct the provisional twin's frame-to-floor spacing, in metres.

The boxed rails remain an approximation of the TJ chassis.
This is a visual packaging correction, not measured factory frame geometry.
"""
import bpy
import bmesh
from mathutils import Matrix, Vector


def correct_frame_height(body, lift):
    # Baseline rail top .460 -> .540, below the .594 floor underside.
    # Keep a dropped belly pan below the transfer case instead of translating
    # its central floor up through the case when raising the rails.
    rise = .080
    offset = -.9 + lift

    def replace_mesh(obj, verts, faces):
        data = bpy.data.meshes.new(obj.name + " corrected geometry")
        inverse = obj.matrix_world.inverted()
        data.from_pydata([inverse @ Vector((x,y,z+offset)) for x,y,z in verts], [], faces)
        data.materials.append(obj.data.materials[0])
        bm = bmesh.new()
        bm.from_mesh(data)
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        bm.to_mesh(data)
        bm.free()
        data.update()
        obj.data = data

    def set_center_height(obj, height):
        points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
        center = (min(p.z for p in points)+max(p.z for p in points))/2
        obj.matrix_world = Matrix.Translation((0,0,height+offset-center)) @ obj.matrix_world

    for obj in list(body.children):
        if obj.name.startswith("Boxed frame rail"):
            points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
            side = 1 if sum(p.x for p in points) > 0 else -1
            # Provisional underside relief over the axle travel envelopes;
            # the upper face stays below the floor and body mount stack.
            stations = [(-1.66,.460),(-.95,.460),(-.70,.400),
                        (.70,.400),(.95,.460),(1.66,.460)]
            verts = []
            for y,bottom in stations:
                # Narrow the rail around the axle stations so the coil sits
                # beside it, rather than passing through the old straight beam.
                center = side*(.350 if abs(y) >= .95 else .470)
                x0,x1 = center-.0475,center+.0475
                verts += [(x0,y,bottom),(x1,y,bottom),(x1,y,.540),(x0,y,.540)]
            faces = [(j*4+i,j*4+(i+1)%4,(j+1)*4+(i+1)%4,(j+1)*4+i)
                     for j in range(len(stations)-1) for i in range(4)]
            faces += [(3,2,1,0),(20,21,22,23)]
            replace_mesh(obj, verts, faces)
        elif obj.name.startswith("Frame crossmember"):
            set_center_height(obj, .505)
        elif obj.name.startswith("Skid reinforcing rib"):
            points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
            x = (min(p.x for p in points)+max(p.x for p in points))/2
            set_center_height(obj, .378 if abs(x) > .36 else .303)
        elif obj.name.startswith("Skid mounting bolt"):
            set_center_height(obj, .385)
        elif obj.name == "Transfer case skid":
            section = [(-.44, .405), (-.36, .405), (-.28, .330),
                       (.28, .330), (.36, .405), (.44, .405)]
            verts = [(x, y, z-depth) for depth in (0., .020)
                     for y in (-.27, .41) for x, z in section]
            faces = []
            for i in range(5):
                faces += [(i,i+1,i+7,i+6), (i+12,i+18,i+19,i+13),
                          (i,i+12,i+13,i+1), (i+6,i+7,i+19,i+18)]
            faces += [(0,6,18,12),(5,17,23,11)]
            replace_mesh(obj, verts, faces)
        elif obj.name.startswith(("Body mount", "Transfer case mount")):
            # Replace the old tall mount stack with a short flange, rubber
            # isolator and upper washer between the raised rail and floor.
            low, high = next(bounds for prefix, bounds in (
                ("Body mount outrigger", (.536, .546)),
                ("Body mount isolator", (.546, .586)),
                ("Body mount washer", (.586, .594)),
                ("Transfer case mount", (.330, .530)),
            ) if obj.name.startswith(prefix))
            world = obj.matrix_world.copy()
            inverse = world.inverted()
            points = [world @ vertex.co for vertex in obj.data.vertices]
            bottom, top = min(p.z for p in points), max(p.z for p in points)
            obj.data = obj.data.copy()
            for vertex, point in zip(obj.data.vertices, points):
                point.z = low + (point.z-bottom)/(top-bottom)*(high-low) - .9 + lift
                vertex.co = inverse @ point
            obj.data.update()
            if obj.name.startswith("Body mount outrigger"):
                points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
                if abs(sum(p.y for p in points)/len(points)) > .95:
                    # Reach the narrowed rear rail without moving the rubber
                    # body isolator away from its existing floor attachment.
                    side = 1 if sum(p.x for p in points) > 0 else -1
                    lo,hi = min(abs(p.x) for p in points),max(abs(p.x) for p in points)
                    for vertex,point in zip(obj.data.vertices,points):
                        point.x = side*(.325+(abs(point.x)-lo)/(hi-lo)*(.540-.325))
                        vertex.co = inverse @ point
                    obj.data.update()
    bpy.context.view_layer.update()
    body["frame_height_correction_m"] = rise
