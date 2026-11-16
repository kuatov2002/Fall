# Splits the Fab "Zeppelin Environment" FBX into breakable chunks and a debris prop library.
#   blender -b -P split_zeppelin.py -- <sketchfab_export.fbx> <out_dir>
# Chunks keep the airship origin (they assemble in place in the engine); props are recentred.
# Material slot names are kept, so the engine reuses the materials the Fab import already made.
import bpy, bmesh, sys, os, json
from mathutils import Vector

src, out = sys.argv[sys.argv.index('--') + 1:][:2]
os.makedirs(out, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=src)
scene = bpy.context.scene


def descendants(o):
    r = [o]
    for c in o.children:
        r += descendants(c)
    return r


def meshes_of(names):
    res = []
    for n in names:
        o = bpy.data.objects.get(n)
        if o:
            res += [d for d in descendants(o) if d.type == 'MESH']
    return res


def merged_copy(objs, name):
    """Copies objs with world transforms applied and joins them into one mesh object."""
    copies = []
    for o in objs:
        c = o.copy()
        c.data = o.data.copy()
        c.parent = None
        c.matrix_world = o.matrix_world.copy()
        scene.collection.objects.link(c)
        copies.append(c)
    bpy.ops.object.select_all(action='DESELECT')
    for c in copies:
        c.select_set(True)
    bpy.context.view_layer.objects.active = copies[0]
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if len(copies) > 1:
        bpy.ops.object.join()
    m = bpy.context.view_layer.objects.active
    m.name = name
    return m


def bisect(obj, ymin, ymax):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    if ymin is not None:
        bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=(0, ymin, 0), plane_no=(0, 1, 0), clear_inner=True)
    if ymax is not None:
        bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=(0, ymax, 0), plane_no=(0, 1, 0), clear_outer=True)
    bm.to_mesh(obj.data)
    bm.free()


def export(obj, path, recenter):
    if recenter:
        lo = Vector([min(v.co[i] for v in obj.data.vertices) for i in range(3)])
        hi = Vector([max(v.co[i] for v in obj.data.vertices) for i in range(3)])
        c = (lo + hi) / 2
        obj.data.transform(__import__('mathutils').Matrix.Translation(-c))
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, mesh_smooth_type='FACE', add_leaf_bones=False, bake_anim=False)
    d = obj.dimensions
    return [round(d.x, 2), round(d.y, 2), round(d.z, 2)], len(obj.data.polygons)


manifest = {'chunks': {}, 'props': {}}
ship_level = bpy.data.objects['ship_level001']
balloon_parts = lambda b, p, g: [x for x in descendants(bpy.data.objects[b]) if x.type == 'MESH' and not any(x in descendants(bpy.data.objects[n]) for n in (p, g) if n in bpy.data.objects)]

# Envelopes, propellers and engines per side.
for side, b, blade, pivot, gen in (('L', 'balloon003', 'blade006', 'fan_pivot002', 'generator025'), ('R', 'balloon005', 'blade007', 'fan_pivot003', 'generator027')):
    env = [x for x in descendants(bpy.data.objects[b]) if x.type == 'MESH' and x.name not in (blade, pivot) and x not in descendants(bpy.data.objects[gen])]
    for name, objs in (('Envelope_' + side, env), ('Propeller_' + side, meshes_of([blade, pivot])), ('Engine_' + side, meshes_of([gen]))):
        m = merged_copy(objs, name)
        manifest['chunks'][name] = export(m, os.path.join(out, 'SM_Airship_%s.fbx' % name), False)

# Hull: the shell and everything on the decks, cut into bow, middle and stern.
deck = [x for x in descendants(ship_level) if x.type == 'MESH' and x.name != 'main_building001' and x not in descendants(bpy.data.objects['main_building001'])]
deck += meshes_of(['base_floor001', 'life_boat001', 'tarp_box001', 'pallet003', 'barrel005', 'barrel006', 'blast_door017', 'blast_door019', 'first_aid_bag002'] + ['crate%03d' % i for i in range(31, 43)])
for name, ymin, ymax in (('Hull_Bow', 8.0, None), ('Hull_Mid', -20.0, 8.0), ('Hull_Stern', None, -20.0)):
    m = merged_copy(deck, name)
    bisect(m, ymin, ymax)
    manifest['chunks'][name] = export(m, os.path.join(out, 'SM_Airship_%s.fbx' % name), False)
m = merged_copy(meshes_of(['main_building001']), 'Deckhouse')
manifest['chunks']['Deckhouse'] = export(m, os.path.join(out, 'SM_Airship_Deckhouse.fbx'), False)

# Debris props, each recentred.
props = {
    'Crate': ['crate031'], 'Barrel': ['barrel005'], 'Lifeboat': ['life_boat001'], 'BlastDoor': ['blast_door017'],
    'Tarp': ['tarp_box001'], 'Pallet': ['pallet003'], 'Generator': ['generator025'], 'Searchlight': ['search_light001'],
    'Propeller': ['blade006', 'fan_pivot002'], 'FrameRing': ['ring_attachment010'], 'FrameRingWide': ['ring_attachment008'],
    'SupplyPipe': ['supply_pipe004'], 'Crane': ['boat_crank002'], 'Railing': ['Loft003'], 'Stairs': ['stairs002'],
    'VentBox': ['air_vent_box007'], 'AnchorPoint': ['anchor points008'],
}
for name, roots in props.items():
    objs = meshes_of(roots)
    if not objs:
        print('MISSING prop', name)
        continue
    m = merged_copy(objs, name)
    manifest['props'][name] = export(m, os.path.join(out, 'SM_Zep_%s.fbx' % name), True)

json.dump(manifest, open(os.path.join(out, 'manifest.json'), 'w'), indent=1)
print('SPLIT_OK', json.dumps(manifest))
