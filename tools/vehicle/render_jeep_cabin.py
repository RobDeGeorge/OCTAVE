"""Cabin inspection renders with roof and doors removed from the view only."""
import argparse
from pathlib import Path
import sys
import bpy
from mathutils import Vector

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--blend",required=True)
parser.add_argument("--output",type=Path,required=True)
parser.add_argument("--save-inspection",action="store_true",help="Save a separate cabin inspection file with hidden roof/doors")
parser.add_argument("--views",nargs="+",help="Render only these named views")
parser.add_argument("--focus-steering",action="store_true")
args=parser.parse_args(sys.argv[sys.argv.index("--")+1:])
bpy.ops.wm.open_mainfile(filepath=args.blend)
args.output.mkdir(parents=True,exist_ok=True)
scene=bpy.context.scene
scene.frame_set(1)
for obj in scene.objects:
    if obj.name.startswith(("Soft top", "Side window zipper", "Rear window zipper", "Door surround rail",
                            "Padded roll bar", "Roll bar cross member", "Rear roll bar brace")):
        obj.hide_render=True
        obj.hide_set(True)
    if obj.parent and obj.parent.name in {"driver_door","passenger_door"}:
        obj.hide_render=True
        obj.hide_set(True)
camera=scene.camera
for name,eye,target,size in (
    ("cabin-overview",(2.3,1.7,2.8),(0,.45,.25),2.5),
    ("cabin-driver",(2.5,.4,1.15),(.10,.12,.28),1.85),
    ("cabin-steering-face",(.36,.58,.52),(.36,-.035,.287),.62),
    ("cabin-steering-column",(1.40,.32,.67),(.36,-.13,.29),.72),
    ("cabin-dashboard",(0,.35,.51),(0,-.22,.36),1.43),
    ("cabin-parking-brake",(.45,.55,.65),(.09,.23,.08),.65),
    ("cabin-controls",(.25,.65,1.5),(.04,-.02,.26),1.05),
):
    if args.views and name not in args.views:
        continue
    hidden=[]
    if name in {"cabin-dashboard", "cabin-parking-brake", "cabin-steering-face"}:
        for obj in scene.objects:
            if obj.name.startswith(("Cabin driver cushion", "Cabin driver reclining", "Cabin passenger cushion", "Cabin passenger reclining", "Cabin seat stitched seam")):
                hidden.append((obj,obj.hide_render))
                obj.hide_render=True
    camera.location=eye
    camera.rotation_euler=(Vector(target)-camera.location).to_track_quat("-Z","Y").to_euler()
    camera.data.ortho_scale=size
    scene.render.filepath=str((args.output/(name+".png")).resolve())
    bpy.ops.render.render(write_still=True)
    for obj,state in hidden:
        obj.hide_render=state
if args.save_inspection:
    target=Vector((.36,-.03,.29) if args.focus_steering else (0,.40,.25))
    camera.location=(1.15,.85,.68) if args.focus_steering else (2.3,1.7,2.8)
    camera.rotation_euler=(target-camera.location).to_track_quat("-Z","Y").to_euler()
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=="VIEW_3D":
                region=area.spaces.active.region_3d
                region.view_location=target
                region.view_distance=.85 if args.focus_steering else 2.7
                region.view_rotation=camera.rotation_euler.to_quaternion()
                region.view_perspective="ORTHO"
    bpy.data.objects["JEEP_CONTROLS"].empty_display_type="PLAIN_AXES"
    bpy.data.objects["JEEP_CONTROLS"].empty_display_size=.10
    bpy.ops.wm.save_as_mainfile(filepath=str((args.output/"cabin-inspection.blend").resolve()))
