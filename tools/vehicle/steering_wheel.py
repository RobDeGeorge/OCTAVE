"""Photo-proportioned TJ steering wheel; not dimensionally certified OEM CAD."""
import math
import bpy
import bmesh
from mathutils import Vector


def rebuild_steering(body, lift, attach, material):
    for obj in list(bpy.context.scene.objects):
        if obj.name.startswith(('Cabin steering', 'Cabin wheel switch', 'Cabin column stalk')):
            bpy.data.objects.remove(obj, do_unlink=True)
    trim=material('TJ steering charcoal vinyl',(.009,.010,.011),0,.58)
    rubber=bpy.data.materials['Tire rubber']
    ink=material('TJ wheel subdued lettering',(.28,.29,.27),0,.8)
    normal=Vector((0,math.cos(math.radians(20)),math.sin(math.radians(20))))
    center=Vector((.36,.040,1.125))-normal*.080
    u=Vector((1,0,0)); up=Vector((0,-normal.z,normal.y))
    def pt(x,z,d=0): return center+u*x+up*z+normal*d
    def finish(obj,name,mat):
        obj.name=name; obj.data.materials.append(mat)
        obj.location.z+=lift-.9
        attach(obj,body)
        obj['cabin_assembly']=True
        return obj
    def tube(name,points,radius,mat=trim,cyclic=False):
        data=bpy.data.curves.new(name,'CURVE'); data.dimensions='3D'
        data.bevel_depth=radius; data.bevel_resolution=5; data.use_fill_caps=True
        sp=data.splines.new('POLY'); sp.points.add(len(points)-1)
        for p,co in zip(sp.points,points): p.co=(*co,1)
        sp.use_cyclic_u=cyclic
        obj=bpy.data.objects.new(name,data); bpy.context.scene.collection.objects.link(obj)
        return finish(obj,name,mat)
    def solid(name,outline,back,front,mat=trim,bevel=.008):
        n=len(outline)
        verts=[pt(x,z,d) for d in (back,front) for x,z in outline]
        faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
        faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
        data=bpy.data.meshes.new(name); data.from_pydata(verts,[],faces); data.update()
        bm=bmesh.new(); bm.from_mesh(data)
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces)); bm.to_mesh(data); bm.free()
        obj=bpy.data.objects.new(name,data); bpy.context.scene.collection.objects.link(obj)
        finish(obj,name,mat)
        bpy.context.view_layer.objects.active=obj
        if bevel:
            mod=obj.modifiers.new('Molded edge radii','BEVEL'); mod.width=bevel; mod.segments=5
            bpy.ops.object.modifier_apply(modifier=mod.name)
        for face in obj.data.polygons: face.use_smooth=True
        mod=obj.modifiers.new('Molded surface normals','WEIGHTED_NORMAL')
        bpy.ops.object.modifier_apply(modifier=mod.name)
        return obj
    def text(text,x,z,d,size,mat=ink):
        data=bpy.data.curves.new('Wheel lettering','FONT'); data.body=text
        data.align_x='CENTER'; data.align_y='CENTER'; data.size=size
        obj=bpy.data.objects.new('Cabin steering label '+text,data)
        bpy.context.scene.collection.objects.link(obj); obj.location=pt(x,z,d)
        obj.rotation_euler=(math.pi/2-math.radians(20),0,math.pi)
        return finish(obj,obj.name,mat)

    # A truly cyclic leather rim, rather than a duplicated uncapped endpoint.
    tube('Cabin steering wheel rim',[pt(.176*math.cos(i*math.tau/96),.176*math.sin(i*math.tau/96)) for i in range(96)],.0145)
    bpy.data.objects['Cabin steering wheel rim'].data.splines[0].use_cyclic_u=True
    tube('Cabin steering column',[center-normal*.24,center-normal*.025],.032)
    # Column shroud lies entirely behind the wheel.
    solid('Cabin steering column shroud',[(-.035,-.03),(.035,-.03),(.048,-.015),(.048,.025),(.028,.038),(-.028,.038),(-.048,.025),(-.048,-.015)],-.175,-.035,bevel=.012)
    # Broad molded wings, not round bars. Each wing flows into the rim.
    for sign in (-1,1):
        wing=[(.075,.018),(.120,.006),(.169,-.021),(.160,-.060),(.135,-.090),(.090,-.088)]
        solid('Cabin steering spoke wing',[(sign*x,z) for x,z in wing],-.020,.011,bevel=.010)
        lower=[(.055,-.070),(.092,-.093),(.092,-.144),(.066,-.165),(.047,-.137)]
        solid('Cabin steering spoke lower',[(sign*x,z) for x,z in lower],-.018,.007,bevel=.008)
    # Rounded top, tapered lower sides and a raised lower-center edge form the
    # distinctive TJ airbag silhouette and the opening above the lower rim.
    # The reference pad sits below the wheel center, leaving a large upper
    # opening. Earlier revisions incorrectly filled that opening with the pad.
    pad=[(-.111,.010),(-.106,.036),(-.087,.052),(-.047,.060),(0,.062),
         (.047,.060),(.087,.052),(.106,.036),(.111,.010),(.106,-.040),
         (.096,-.116),(.087,-.136),(.064,-.138),(.057,-.107),(.043,-.099),
         (-.043,-.099),(-.057,-.107),(-.064,-.138),(-.087,-.136),(-.096,-.116),(-.106,-.040)]
    # Smooth the perimeter and loft a padded face, rather than a flat plate.
    outline=[]
    for i in range(len(pad)):
        a,b,c,d=[Vector(pad[j%len(pad)]) for j in (i-1,i,i+1,i+2)]
        for k in range(4):
            t=k/4
            v=.5*(2*b+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+(-a+3*b-3*c+d)*t*t*t)
            outline.append(v)
    n=len(outline)
    verts=[pt(x,z,depth) for depth in (-.006,.025) for x,z in outline]
    faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
    faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    data=bpy.data.meshes.new('Padded TJ airbag'); data.from_pydata(verts,[],faces); data.update()
    bm=bmesh.new(); bm.from_mesh(data)
    front=[f for f in bm.faces if all((v.co-center).dot(normal)>.020 for v in f.verts)]
    bmesh.ops.triangulate(bm,faces=front)
    edges=[e for e in bm.edges if all((v.co-center).dot(normal)>.020 for v in e.verts)]
    bmesh.ops.subdivide_edges(bm,edges=edges,cuts=5,use_grid_fill=True)
    def dome(x,z): return .025+.028*max(0,1-(x/.122)**2-((z+.035)/.128)**2)
    for v in bm.verts:
        rel=v.co-center
        depth=rel.dot(normal)
        if depth>.020:
            v.co += normal*(dome(rel.dot(u),rel.dot(up))-depth)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces)); bm.to_mesh(data); bm.free()
    obj=bpy.data.objects.new('Cabin steering airbag',data); bpy.context.scene.collection.objects.link(obj)
    for face in data.polygons:
        face.use_smooth=all((data.vertices[i].co-center).dot(normal)>.020 for i in face.vertices)
    finish(obj,obj.name,trim)
    text('Jeep',0,-.020,dome(0,-.020)+.0006,.018,trim)
    text('SRS AIRBAG',0,-.068,dome(0,-.068)+.0006,.0055)
    for sign in (-1,1):
        outline=[(.109,.019),(.134,.010),(.138,-.044),(.114,-.052),(.107,-.037)]
        solid('Cabin wheel switch bezel',[(sign*(x+.007),z-.030) for x,z in outline],.009,.020,bevel=.004)
        inset=[(.113,.012),(.130,.005),(.133,-.038),(.117,-.044),(.112,-.033)]
        solid('Cabin wheel switch pad',[(sign*(x+.007),z-.030) for x,z in inset],.020,.023,rubber,.002)
        words=('ON-OFF','CRUISE','SET') if sign>0 else ('RESUME','COAST','CANCEL')
        for i,word in enumerate(words): text(word,sign*.129,-.028-i*.017,.0235,.0038)
        tube('Cabin column stalk',[pt(sign*.035,.005,-.105),pt(sign*.13,.015,-.095)],.006)
        tube('Cabin column stalk grip',[pt(sign*.115,.015,-.095),pt(sign*.15,.018,-.095)],.010)
