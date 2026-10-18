"""Render roof closure and underside inspection views from a saved candidate."""
import argparse
from pathlib import Path
import sys
import bpy
from mathutils import Vector

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--blend", required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args(sys.argv[sys.argv.index("--")+1:])
bpy.ops.wm.open_mainfile(filepath=args.blend)
args.output.mkdir(parents=True, exist_ok=True)
scene = bpy.context.scene
camera = scene.camera
for name, frame, eye, target, size in (
    ("roof-rear-detail",1,(2.2,3.7,2.4),(0,.8,.8),2.8),
    ("roof-front-detail",1,(2.2,-2.5,2.1),(0,.0,.82),2.4),
    ("suspension-detail",1,(2.,-3.3,-1.6),(0,-.9,-.35),2.7),
):
    scene.frame_set(frame)
    bpy.data.objects["Studio floor"].hide_render = name.startswith("suspension")
    camera.location = eye
    camera.rotation_euler = (Vector(target)-camera.location).to_track_quat("-Z","Y").to_euler()
    camera.data.ortho_scale = size
    scene.render.filepath = str((args.output/(name+".png")).resolve())
    bpy.ops.render.render(write_still=True)
