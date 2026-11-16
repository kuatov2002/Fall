# blender -b -P tree_fbx.py -- <file.fbx> : top two levels of the hierarchy with merged bounds and triangle counts.
import bpy, sys
from mathutils import Vector
path = sys.argv[sys.argv.index('--') + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=path)
def desc(o):
    out = [o]
    for c in o.children: out += desc(c)
    return out
def info(o):
    lo = Vector((1e9,) * 3); hi = Vector((-1e9,) * 3); tris = 0; mats = set()
    for d in desc(o):
        if d.type != 'MESH': continue
        tris += len(d.data.polygons)
        mats |= {s.material.name for s in d.material_slots if s.material}
        for c in d.bound_box:
            w = d.matrix_world @ Vector(c); lo = Vector(map(min, lo, w)); hi = Vector(map(max, hi, w))
    return lo, hi, tris, mats
roots = [o for o in bpy.context.scene.objects if o.parent is None]
for r in roots:
    def walk(o, depth):
        lo, hi, tris, mats = info(o)
        if tris == 0: return
        print('%s%-34s size %6.1f %6.1f %6.1f  ctr %6.1f %6.1f %6.1f  polys %6d  %s' % ('  ' * depth, o.name, *(hi - lo), *((hi + lo) / 2), tris, ','.join(sorted(mats))[:70]))
        if depth < int(sys.argv[sys.argv.index("--") + 2]) and len(o.children) < 4000:
            for c in o.children: walk(c, depth + 1)
    walk(r, 0)
print('MATERIALS', sorted(m.name for m in bpy.data.materials))
