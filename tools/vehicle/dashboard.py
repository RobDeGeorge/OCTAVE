"""TJ dashboard and parking-brake rebuild, proportioned from cached reference 048.

Ground coordinates; dimensions are photographic estimates, not OEM CAD.
"""
import math
import bpy
from mathutils import Vector


def rebuild_dashboard(box, tube, sphere, finish, material, trim, steel, rubber, ivory, lift):
    fascia = material('TJ warm graphite dash plastic', (.095,.09,.081), 0, .76)
    red = material('TJ gauge red needles', (.65,.022,.012), 0, .5)
    green = material('TJ odometer green', (.12,.6,.27), 0, .5)

    def cut(obj, center, size, bevel=.005):
        cutter = box('Temporary dashboard cutter', center, size, trim, bevel)
        mod = obj.modifiers.new('Recess / clearance', 'BOOLEAN')
        mod.operation='DIFFERENCE'
        mod.object=cutter
        bpy.context.view_layer.objects.active=obj
        bpy.ops.object.modifier_apply(modifier=mod.name)
        bpy.data.objects.remove(cutter, do_unlink=True)

    def label(name, text, x, y, z, size, mat=ivory):
        data=bpy.data.curves.new(name,'FONT')
        data.body=text
        data.size=size
        data.align_x='CENTER'
        data.align_y='CENTER'
        obj=bpy.data.objects.new(name,data)
        bpy.context.scene.collection.objects.link(obj)
        obj.location=(x,y,z)
        # Text faces the occupants (+Y), with its top pointing up.
        obj.rotation_euler=(math.pi/2,0,math.pi)
        return finish(obj,name,mat)

    # Broad upper crash pad, separate lower knee bolster and passenger airbag.
    upper=box('Dashboard upper',(0,-.345,1.243),(1.32,.23,.11),fascia,.043)
    box('Dashboard fascia backing',(0,-.29,1.17),(1.28,.06,.18),fascia,.015)
    lower=box('Dashboard lower',(0,-.312,1.041),(1.28,.16,.165),fascia,.026)
    # Actual opening through the lower bolster for the angled column.
    normal=Vector((0,math.cos(math.radians(20)),math.sin(math.radians(20))))
    bpy.ops.mesh.primitive_cylinder_add(vertices=48,radius=.042,depth=.55,
        location=Vector((.36,.040,1.125))-normal*.20+Vector((0,0,lift-.9)))
    cutter=bpy.context.object
    cutter.rotation_mode='QUATERNION'
    cutter.rotation_quaternion=Vector((0,0,1)).rotation_difference(normal)
    mod=lower.modifiers.new('Column clearance','BOOLEAN'); mod.object=cutter; mod.operation='DIFFERENCE'
    bpy.context.view_layer.objects.active=lower
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(cutter,do_unlink=True)

    pod=box('Driver instrument binnacle',(.365,-.245,1.232),(.432,.142,.233),trim,.034)
    cut(pod,(.365,-.166,1.232),(.385,.095,.181),.021)
    box('Cabin recessed instrument face',(.365,-.208,1.232),(.382,.008,.178),rubber,.018)
    # Six instruments printed on one recessed face; no protruding metal rings.
    for x,z,r,maxval,title in ((.441,1.235,.061,6,'RPM x1000'),(.291,1.235,.061,100,'MPH'),
                              (.527,1.276,.025,100,'OIL'),(.527,1.194,.025,1,'FUEL'),
                              (.203,1.276,.025,260,'TEMP'),(.203,1.194,.025,19,'VOLTS')):
        steps=25 if title=='MPH' else 30
        for i in range(steps+1):
            a=math.radians(220-260*i/steps)
            rr=r*(.84 if i%5==0 else .92)
            tube('Cabin gauge tick',[(x-r*math.cos(a),-.201,z+r*math.sin(a)),
                                     (x-rr*math.cos(a),-.201,z+rr*math.sin(a))],.00065,ivory)
            if i%5==0 and r>.04:
                label('Cabin gauge numeral',str(round(maxval*i/steps)),x-r*.69*math.cos(a),-.200,z+r*.69*math.sin(a),.009)
        a=math.radians(160)
        tube('Cabin gauge needle',[(x,-.198,z),(x-r*.74*math.cos(a),-.198,z+r*.74*math.sin(a))],.0012,red)
        label('Cabin gauge legend',title,x,-.200,z-r*.36,.006 if r>.04 else .0045)
    label('Cabin odometer','138710',.365,-.199,1.169,.009,green)

    def vent(x,z,w,h):
        shell=box('Cabin vent bezel',(x,-.219,z),(w,.033,h),fascia,.009)
        cut(shell,(x,-.198,z),(w-.012,.031,h-.012),.004)
        box('Cabin vent dark throat',(x,-.213,z),(w-.013,.003,h-.013),rubber,.003)
        count=max(3,round(h/.016))
        for i in range(count):
            zz=z-h/2+.014+i*(h-.028)/(count-1)
            box('Cabin vent louver',(x,-.198,zz),(w-.019,.012,.003),trim,.001)
        box('Cabin vent adjuster',(x,-.188,z),(.009,.009,h*.45),fascia,.002)
    vent(.620,1.204,.072,.18)
    vent(-.620,1.204,.060,.18)
    stack=box('Center dash stack',(0,-.248,1.149),(.315,.085,.31),trim,.023)
    for x in (-.075,.075):
        cut(stack,(x,-.20,1.255),(.126,.10,.052),.004)
        vent(x,1.255,.138,.064)
    radio=box('Cabin radio fascia',(0,-.197,1.171),(.272,.015,.080),rubber,.006)
    box('Cabin radio display',(0,-.187,1.185),(.134,.004,.020),trim,.002)
    label('Cabin radio display digits','FM  98.5',0,-.184,1.185,.008,green)
    for x in (-.109,.109): sphere('Cabin radio knob',(x,-.182,1.171),(.013,.011,.013),trim)
    for i in range(6): box('Cabin radio preset',(-.075+i*.030,-.184,1.151),(.021,.006,.008),fascia,.002)
    box('Cabin HVAC fascia',(0,-.198,1.085),(.272,.018,.074),fascia,.006)
    for x,txt in ((.089,'FAN'),(0,'TEMP'),(-.089,'MODE')):
        sphere('Cabin HVAC dial',(x,-.177,1.081),(.022,.013,.022),trim)
        tube('Cabin HVAC pointer',[(x,-.162,1.081),(x,-.162,1.096)],.0013,ivory)
        label('Cabin HVAC legend',txt,x,-.186,1.111,.006)
    box('Cabin accessory switch panel',(0,-.202,1.025),(.265,.014,.033),rubber,.004)
    for x in (-.085,0,.085): box('Cabin accessory switch',(x,-.190,1.025),(.025,.012,.020),trim,.003)
    box('Cabin passenger airbag',(-.387,-.237,1.218),(.399,.033,.150),fascia,.017)
    label('Cabin airbag label','SRS AIRBAG',-.39,-.218,1.175,.006)
    tube('Cabin passenger grab handle',[(-.563,-.223,1.115),(-.563,-.148,1.115),
        (-.22,-.148,1.115),(-.22,-.223,1.115)],.018,fascia)
    box('Cabin glovebox panel',(-.39,-.222,1.013),(.40,.025,.134),fascia,.015)
    box('Cabin glovebox latch',(-.39,-.204,1.053),(.059,.013,.023),trim,.004)
    # Knee speaker grilles at the dash ends.
    for x in (-.586,.586):
        for i in range(7): box('Cabin dash speaker slot',(x,-.229,.985+i*.012),(.07,.006,.003),trim,.001)

    # Driver-side brake channel removes the deck underneath the lever, including
    # its old right wall. Cupholder bowls remain intact to the passenger side.
    for obj in list(bpy.context.scene.objects):
        if obj.type=='MESH' and obj.name.startswith(('Cupholder console deck','Cupholder console side','Console rear bridge')):
            cut(obj,(.117,.245,.885),(.074,.295,.17),.003)
    box('Cabin brake channel floor',(.103,.245,.816),(.040,.273,.009),rubber,.004)
    box('Cabin brake pivot housing',(.102,.355,.854),(.031,.043,.065),trim,.008)
    tube('Parking brake lever',[(.102,.350,.868),(.102,.240,.894),(.102,.115,.920)],.007,steel)
    tube('Parking brake grip',[(.102,.233,.896),(.102,.118,.920)],.012,trim)
    sphere('Parking brake release button',(.102,.108,.922),(.007,.007,.007),steel)
