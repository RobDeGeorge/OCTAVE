"""Photo-informed TJ cabin corrections in ground-relative Blender coordinates.

Positive Y is rearward. Seats therefore recline toward +Y; their upholstery
faces -Y. Dimensions remain estimates, not an ergonomic/production drawing.
"""
import math
import bpy
import bmesh
from mathutils import Vector


def rebuild_cabin(body, lift, attach, material):
    prefixes = ("Seat base", "Seat back", "Integrated headrest", "Seat center fabric insert",
                "Seat cushion fabric insert", "Seat side bolster", "Rear bench seat", "Rear bench back",
                "Gearshift", "Steering column", "Steering wheel", "Center dash control",
                "Dashboard", "Center dash stack", "Rectangular dashboard vent", "Vent slat",
                "Driver instrument binnacle", "Instrument dial", "Instrument needle", "Parking brake")
    for obj in list(bpy.context.scene.objects):
        if obj.name.startswith(prefixes):
            bpy.data.objects.remove(obj, do_unlink=True)
    upholstery = material("Cabin charcoal upholstery", (.018,.021,.025), 0, .84)
    cloth = material("Cabin woven seat insert", (.045,.05,.055), 0, .96)
    rubber = bpy.data.materials["Tire rubber"]
    steel = bpy.data.materials["Undercarriage steel"]
    trim = bpy.data.materials["Textured black trim"]
    lettering = material("Shift pattern ivory", (.65,.67,.62), 0, .6)
    created = []

    def finish(obj, name, mat):
        obj.name = name
        obj.data.materials.append(mat)
        obj.location.z += lift-.9
        attach(obj, body)
        obj["cabin_assembly"] = True
        created.append(obj)
        return obj

    def mesh(name, verts, faces, mat):
        data = bpy.data.meshes.new(name)
        data.from_pydata(verts, [], faces)
        data.update()
        bm = bmesh.new()
        bm.from_mesh(data)
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        bm.to_mesh(data)
        bm.free()
        obj = bpy.data.objects.new(name,data)
        bpy.context.scene.collection.objects.link(obj)
        for p in data.polygons:
            p.use_smooth = True
        return finish(obj,name,mat)

    def box(name, center, size, mat, bevel=.012):
        bpy.ops.mesh.primitive_cube_add(size=1, location=center)
        obj = bpy.context.object
        obj.scale = size
        bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
        mod = obj.modifiers.new("Rounded upholstery edge","BEVEL")
        mod.width = bevel
        mod.segments = 4
        bpy.ops.object.modifier_apply(modifier=mod.name)
        mod = obj.modifiers.new("Weighted normals","WEIGHTED_NORMAL")
        bpy.ops.object.modifier_apply(modifier=mod.name)
        return finish(obj,name,mat)

    def tube(name, points, radius, mat):
        data = bpy.data.curves.new(name,"CURVE")
        data.dimensions = "3D"
        data.bevel_depth = radius
        data.bevel_resolution = 3
        spline = data.splines.new("POLY")
        spline.points.add(len(points)-1)
        for p,co in zip(spline.points,points):
            p.co = (*co,1)
        obj = bpy.data.objects.new(name,data)
        bpy.context.scene.collection.objects.link(obj)
        return finish(obj,name,mat)

    def sphere(name, center, size, mat):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, location=center)
        obj = bpy.context.object
        obj.scale = size
        for p in obj.data.polygons:
            p.use_smooth = True
        return finish(obj,name,mat)

    def loft(name, rows, mat):
        n=len(rows[0])
        faces=[(j*n+i,j*n+(i+1)%n,(j+1)*n+(i+1)%n,(j+1)*n+i)
               for j in range(len(rows)-1) for i in range(n)]
        faces += [tuple(reversed(range(n))),tuple(range((len(rows)-1)*n,len(rows)*n))]
        return mesh(name,[v for row in rows for v in row],faces,mat)

    # One contoured shell per cushion/back; inserts are material regions on the
    # same surface, eliminating detached upholstery plates and stacked bolsters.
    n=64
    for x in (-.36,.36):
        side="driver" if x>0 else "passenger"
        rows=[]
        for z,wx,dy in ((.685,.185,.215),(.705,.218,.247),(.785,.225,.25),(.815,.205,.235)):
            rows.append([(x+wx*math.copysign(abs(math.cos(a))**.5,math.cos(a)),
                          .115+dy*math.copysign(abs(math.sin(a))**.5,math.sin(a)),
                          z + (.025*math.cos(a)**4 if z == .815 else 0))
                         for a in [i*math.tau/n for i in range(n)]])
        # Shrinking top rings close a gently dished surface without overlapping
        # separate side-bolster geometry.
        for scale in (.80,.55,.25,.01):
            rows.append([(x+.205*scale*math.copysign(abs(math.cos(a))**.5,math.cos(a)),
                          .115+.235*scale*math.copysign(abs(math.sin(a))**.5,math.sin(a)),
                          .803+scale*scale*(.037*math.cos(a)**4+.012*math.sin(a)**4))
                         for a in [i*math.tau/n for i in range(n)]])
        cushion=loft("Cabin "+side+" cushion",rows,upholstery)
        cushion.data.materials.append(cloth)
        for p in cushion.data.polygons:
            if p.center.z>.80:
                p.material_index=1
        controls=[(.835,.202,.058),(.875,.220,.063),(1.04,.222,.061),
                  (1.20,.220,.059),(1.24,.215,.057),(1.28,.202,.054),
                  (1.31,.180,.052),(1.34,.155,.050),(1.38,.138,.049),
                  (1.44,.132,.048),(1.49,.115,.040),(1.515,.070,.027),(1.522,.015,.012)]
        profiles=[]
        for j in range(len(controls)-1):
            p0,p1,p2,p3=[Vector(controls[min(max(k,0),len(controls)-1)]) for k in (j-1,j,j+1,j+2)]
            for step in range(8):
                t=step/8
                v=.5*((2*p1)+(-p0+p2)*t+(2*p0-5*p1+4*p2-p3)*t*t+(-p0+3*p1-3*p2+p3)*t*t*t)
                profiles.append(tuple(v))
        profiles.append(controls[-1])
        def front_shape(z,c):
            t=max(0.,min(1.,(1.36-z)/.16))
            blend=t*t*(3-2*t)
            return 1+blend*(-.40+2*c*c)
        rows=[]
        for z,w,d in profiles:
            cy=.35+.22*(z-.835)
            rows.append([(x+w*math.cos(a),cy+d*math.sin(a)*
                          (front_shape(z,math.cos(a)) if math.sin(a)<0 else 1),z)
                         for a in [i*math.tau/n for i in range(n)]])
        back=loft("Cabin "+side+" reclining seatback",rows,upholstery)
        back.data.materials.append(cloth)
        for p in back.data.polygons:
            c=p.center
            if c.z<1.32 and c.y < .35+.22*(c.z-.835)-.025 and abs(c.x-x)<.16:
                p.material_index=1
        # A seam follows the front-facing padding, not an independent insert.
        for s in (-1,1):
            tube("Cabin seat stitched seam",[(x+s*w*.67,.35+.22*(z-.835)-d*math.sqrt(1-.67**2)*
                                               front_shape(z,.67)-.0006,z)
                                             for z,w,d in profiles if z<=1.34],.0008,trim)
        box("Cabin seat recliner housing",(x+(.235 if x>0 else -.235),.32,.77),(.025,.10,.09),trim)
        box("Cabin seatbelt buckle",(x-math.copysign(.237,x),.30,.84),(.028,.045,.067),trim,.006)
    box("Cabin rear bench cushion",(0,1.12,.819),(1.04,.42,.16),upholstery,.052)
    rear=box("Cabin rear bench back",(0,1.405,1.08),(1.04,.13,.40),upholstery,.05)
    # Pivot around its center; top leans rearward, matching the front seats.
    rear.rotation_euler.x=math.radians(-10)

    # NV3550-style lever: bellows boot, bent stalk and round pattern insert.
    for name,cx,cy,width,depth,height in (("Manual",-.025,-.045,.066,.070,.075),
                                       ("Transfer case",.088,-.075,.028,.045,.047)):
        tube("Cabin "+name+" boot retaining bezel",
             [(cx+(width+.003)*math.copysign(abs(math.cos(a))**.5,math.cos(a)),
               cy+(depth+.003)*math.copysign(abs(math.sin(a))**.5,math.sin(a)),.913)
              for a in [i*math.tau/48 for i in range(49)]],.004,trim)
        rows=[]
        for j in range(13):
            t=j/12
            r=(1-.78*t)*(1 if j%2==0 else .84)
            rows.append([(cx+width*r*math.copysign(abs(math.cos(a))**.5,math.cos(a)),
                          cy+depth*r*math.copysign(abs(math.sin(a))**.5,math.sin(a)),.914+height*t)
                         for a in [i*math.tau/48 for i in range(48)]])
        loft("Cabin "+name+" bellows boot",rows,rubber)
    tube("Cabin manual bent shift lever",[(-.025,-.045,.965),(-.025,-.015,1.025),(-.025,.045,1.105)],.008,steel)
    loft("Cabin manual shift knob",[[(-.025+r*math.cos(a),.045+r*math.sin(a),z)
                                    for a in [i*math.tau/48 for i in range(48)]]
                                   for z,r in ((1.095,.014),(1.105,.027),(1.13,.029),(1.15,.026))],trim)
    # Read from the driver's seat: +X is left on the printed H-pattern.
    tube("Cabin shift H crossbar",[(-.040,.045,1.1505),(-.010,.045,1.1505)],.0006,lettering)
    for x in (-.040,-.025,-.010):
        tube("Cabin shift H gate",[(x,.037,1.1505),(x,.053,1.1505)],.0006,lettering)
    for row,y in (("135",.031),("24R",.060)):
        for character,x in zip(row,(-.010,-.025,-.040)):
            data=bpy.data.curves.new("Five speed pattern","FONT")
            data.body=character
            data.size=.006
            data.align_x="CENTER"
            data.align_y="CENTER"
            obj=bpy.data.objects.new("Cabin shift pattern "+character,data)
            bpy.context.scene.collection.objects.link(obj)
            obj.location=(x,y,1.1507)
            obj.rotation_euler.z=math.pi
            finish(obj,obj.name,lettering)
    tube("Cabin transfer case lever",[(.088,-.075,.95),(.088,-.020,1.015)],.007,steel)
    sphere("Cabin transfer case knob",(.088,-.015,1.025),(.020,.027,.022),trim)

    from steering_wheel import rebuild_steering
    rebuild_steering(body,lift,attach,material)

    from dashboard import rebuild_dashboard
    rebuild_dashboard(box, tube, sphere, finish, material, trim, steel, rubber, lettering, lift)
    return created
