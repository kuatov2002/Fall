# blender -b -P render_preview.py -- <in.fbx|glb> <out_prefix> [texdir]
# Workbench renders from 4 angles to check what a downloaded model looks like.
import bpy, sys, math, os
from mathutils import Vector
a = sys.argv[sys.argv.index('--') + 1:]
src, out = a[0], a[1]
texdir = a[2] if len(a) > 2 else None
bpy.ops.wm.read_factory_settings(use_empty=True)
if src.lower().endswith('.fbx'):
    bpy.ops.import_scene.fbx(filepath=src)
else:
    bpy.ops.import_scene.gltf(filepath=src)
if texdir:
    bpy.ops.file.find_missing_files(directory=texdir)
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
lo = Vector((1e9,) * 3); hi = Vector((-1e9,) * 3)
for o in meshes:
    for c in o.bound_box:
        w = o.matrix_world @ Vector(c)
        lo = Vector(map(min, lo, w)); hi = Vector(map(max, hi, w))
ctr = (lo + hi) / 2; size = (hi - lo).length
print('BOUNDS', tuple(round(x, 2) for x in lo), tuple(round(x, 2) for x in hi))
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
bpy.context.scene.collection.objects.link(cam)
bpy.context.scene.camera = cam
cam.data.lens = 35
cam.data.clip_end = size * 10
s = bpy.context.scene
s.render.engine = 'BLENDER_WORKBENCH'
s.display.shading.light = 'STUDIO'
s.display.shading.color_type = 'TEXTURE'
s.display.shading.show_shadows = True
s.render.resolution_x, s.render.resolution_y = 960, 640
for i, (yaw, pitch) in enumerate([(30, 20), (120, 10), (210, 35), (300, -15)]):
    d = Vector((math.cos(math.radians(yaw)) * math.cos(math.radians(pitch)), math.sin(math.radians(yaw)) * math.cos(math.radians(pitch)), math.sin(math.radians(pitch))))
    cam.location = ctr + d * size * 0.95
    cam.rotation_euler = (ctr - cam.location).to_track_quat('-Z', 'Y').to_euler()
    s.render.filepath = '%s_%d.png' % (out, i)
    bpy.ops.render.render(write_still=True)
