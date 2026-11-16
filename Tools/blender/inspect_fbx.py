# blender -b -P inspect_fbx.py -- <file.fbx> : prints the object tree with sizes (meters) and materials.
import bpy, sys
path = sys.argv[sys.argv.index('--') + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=path)
objs = [o for o in bpy.context.scene.objects]
print('OBJECTS', len(objs))
for o in sorted(objs, key=lambda o: o.name):
    if o.type != 'MESH':
        print('  ', o.type, o.name, 'parent', o.parent.name if o.parent else '-')
        continue
    d = o.dimensions
    mats = sorted({s.material.name for s in o.material_slots if s.material})
    print('MESH %-40s %7.2f %7.2f %7.2f  tris~%d  parent=%s  mats=%s' % (o.name, d.x, d.y, d.z, len(o.data.polygons), o.parent.name if o.parent else '-', ','.join(mats)[:120]))
