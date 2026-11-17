# Builds the few meshes nothing on Fab/Poly Haven covers, and exports them as FBX for Tools/import_meshes.py.
#   blender -b -P make_meshes.py -- <out_dir>
# - SM_FF_Quad, SM_FF_Beam, SM_FF_Ball: effect geometry only (sprites, tracers, lasers, flashes), always textured
#   with soft particle materials; never used as visible objects.
# - SM_ParachuteContainer: the sport rig container the carrier wears (pins, flaps, handle, harness).
# - SM_Canopy: 9-cell ram-air canopy with cell openings, ribs and a slight arc.
# - SM_Turret: heavy's pintle-mounted autocannon with a shield plate.
import bpy, bmesh, sys, os, math
from mathutils import Vector, Matrix

out = sys.argv[sys.argv.index('--') + 1]
os.makedirs(out, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
M = 1.0  # Blender metres; FBX export converts to centimetres


def new_obj(name, bm):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    return o


def mat(o, name):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    o.data.materials.append(m)
    return len(o.data.materials) - 1


def export(objs, name):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    o = bpy.context.view_layer.objects.active
    o.name = name
    for p in o.data.polygons:
        p.use_smooth = True
    bpy.ops.object.shade_auto_smooth(angle=math.radians(35)) if hasattr(bpy.ops.object, 'shade_auto_smooth') else None
    bpy.ops.export_scene.fbx(filepath=os.path.join(out, name + '.fbx'), use_selection=True, add_leaf_bones=False, bake_anim=False, mesh_smooth_type='FACE')
    bpy.data.objects.remove(o)


def box(name, size, loc=(0, 0, 0), rot=(0, 0, 0), bevel=0.0, material='M'):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    o.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel > 0:
        mod = o.modifiers.new('b', 'BEVEL'); mod.width = bevel; mod.segments = 2
        bpy.ops.object.modifier_apply(modifier='b')
    o.data.materials.clear(); mat(o, material)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return o


def cyl(name, r, depth, loc=(0, 0, 0), rot=(0, 0, 0), verts=24, material='M'):
    bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=depth, vertices=verts, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    o.data.materials.clear(); mat(o, material)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return o


# ---- effect geometry (1 m, UV 0..1)
bpy.ops.mesh.primitive_plane_add(size=1)
q = bpy.context.active_object; q.data.materials.clear(); mat(q, 'FX')
export([q], 'SM_FF_Quad')
q = box('beam', (1, 1, 1), material='FX')
# UVs like the engine cube: every face 0..1, sides with V running down the Z axis (streak and laser
# materials taper along V); a cross-shaped unwrap draws them as dashes.
uvl = q.data.uv_layers.active.data
for poly in q.data.polygons:
    n = poly.normal
    for li in poly.loop_indices:
        co = q.data.vertices[q.data.loops[li].vertex_index].co
        if abs(n.z) > 0.5:
            uvl[li].uv = (co.x + 0.5, co.y + 0.5)
        elif abs(n.x) > 0.5:
            uvl[li].uv = (co.y + 0.5, 0.5 - co.z)
        else:
            uvl[li].uv = (co.x + 0.5, 0.5 - co.z)
export([q], 'SM_FF_Beam')
bpy.ops.mesh.primitive_uv_sphere_add(radius=0.5, segments=24, ring_count=12)
q = bpy.context.active_object; q.data.materials.clear(); mat(q, 'FX')
export([q], 'SM_FF_Ball')

# ---- parachute container: ~42 x 30 x 18 cm, worn on the back (+Y is away from the back, +Z up)
parts = []
parts.append(box('body', (0.40, 0.17, 0.48), (0, 0.085, 0), bevel=0.04, material='Canvas'))
parts.append(box('flap_top', (0.38, 0.05, 0.12), (0, 0.17, 0.17), rot=(math.radians(-12), 0, 0), bevel=0.02, material='Canvas'))
parts.append(box('flap_bottom', (0.38, 0.05, 0.14), (0, 0.17, -0.16), rot=(math.radians(10), 0, 0), bevel=0.02, material='Canvas'))
for s in (-1, 1):
    parts.append(box('side_flap', (0.05, 0.12, 0.40), (s * 0.19, 0.12, 0), bevel=0.015, material='Canvas'))
    parts.append(box('strap', (0.05, 0.02, 0.62), (s * 0.12, -0.01, 0.02), material='Webbing'))
parts.append(box('belt', (0.46, 0.02, 0.05), (0, -0.01, -0.20), material='Webbing'))
parts.append(cyl('pin_cover', 0.025, 0.10, (0, 0.20, 0.05), rot=(0, math.radians(90), 0), verts=12, material='Metal'))
parts.append(cyl('handle', 0.018, 0.09, (0.21, 0.10, -0.18), rot=(0, 0, 0), verts=10, material='Handle'))
parts.append(box('grommet', (0.06, 0.01, 0.06), (0, 0.20, 0.10), material='Metal'))
export(parts, 'SM_ParachuteContainer')

# ---- canopy: 9 cells, span 8.6 m, chord 3.2 m, thickness 0.45 m at the front, arched 12 degrees
bm = bmesh.new()
span, chord, cells = 8.6, 3.2, 9
uv = bm.loops.layers.uv.new()
ns = cells * 4
def section(t):
    # airfoil-ish section along chord (x from leading edge 0 to trailing edge 1)
    pts = []
    for i in range(12):
        x = i / 11
        th = 0.45 * (1 - x) ** 1.4 * (1.0 if x > 0.06 else 0.75)
        pts.append((x, th))
    return pts
rows = []
for j in range(ns + 1):
    s = j / ns
    y = (s - 0.5) * span
    arch = -1.5 * ((2 * s - 1) ** 2)  # tips well below the middle (a real ram-air canopy is strongly arched)
    ang = (2 * s - 1) * math.radians(12)
    row_top, row_bot = [], []
    bulge = 0.22 * math.sin(math.pi * ((j % 4) / 4))  # cells puff out between the ribs: a flat top read as a box (critic, iteration 3)
    for x, th in section(s):
        X = (x - 0.3) * chord
        z_top = arch + th + bulge * (1 - x)
        z_bot = arch - 0.02 * (1 - x) - bulge * 0.35 * (1 - x)  # the underside sags between the ribs too
        row_top.append(bm.verts.new((X, y, z_top)))
        row_bot.append(bm.verts.new((X, y, z_bot)))
    rows.append((row_top, row_bot, s))
for j in range(ns):
    (t0, b0, s0), (t1, b1, s1) = rows[j], rows[j + 1]
    for i in range(11):
        for a, b, flip in ((t0, t1, False), (b0, b1, True)):
            vs = [a[i], a[i + 1], b[i + 1], b[i]]
            if flip:
                vs.reverse()
            f = bm.faces.new(vs)
            for l, (u, v) in zip(f.loops, [(i / 11, s0), ((i + 1) / 11, s0), ((i + 1) / 11, s1), (i / 11, s1)] if not flip else [(i / 11, s1), ((i + 1) / 11, s1), ((i + 1) / 11, s0), (i / 11, s0)]):
                l[uv].uv = (u * 4, v * 9)
    # closed tail, open cell mouths at the front
    f = bm.faces.new([t0[11], t1[11], b1[11], b0[11]])
# ribs (end caps between cells)
for j in range(0, ns + 1, 4):
    t, b, s = rows[j]
    for i in range(11):
        try:
            bm.faces.new([t[i], t[i + 1], b[i + 1], b[i]])
        except ValueError:
            pass
o = new_obj('canopy', bm)
mat(o, 'CanopyFabric')
export([o], 'SM_Canopy')

# ---- turret: pintle base, cradle, receiver, jacketed barrel, shield with a sight slot (gun points +X)
parts = []
parts.append(cyl('base', 0.22, 0.12, (0, 0, 0.06), verts=20, material='Gunmetal'))
parts.append(cyl('pintle', 0.07, 0.55, (0, 0, 0.38), verts=16, material='Gunmetal'))
parts.append(box('cradle', (0.30, 0.22, 0.10), (0, 0, 0.68), bevel=0.02, material='Gunmetal'))
parts.append(box('receiver', (0.62, 0.20, 0.24), (0.05, 0, 0.84), bevel=0.025, material='Gunmetal'))
parts.append(box('feed', (0.22, 0.30, 0.08), (0.02, 0, 0.99), bevel=0.01, material='Gunmetal'))
parts.append(box('ammo_can', (0.30, 0.14, 0.24), (-0.05, 0.22, 0.72), bevel=0.015, material='Olive'))
parts.append(cyl('jacket', 0.075, 0.9, (0.80, 0, 0.84), rot=(0, math.radians(90), 0), verts=18, material='Gunmetal'))
for k in range(7):
    parts.append(cyl('fin', 0.085, 0.025, (0.45 + k * 0.11, 0, 0.84), rot=(0, math.radians(90), 0), verts=18, material='Gunmetal'))
parts.append(cyl('barrel', 0.028, 0.55, (1.45, 0, 0.84), rot=(0, math.radians(90), 0), verts=12, material='Gunmetal'))
parts.append(cyl('muzzle', 0.05, 0.12, (1.75, 0, 0.84), rot=(0, math.radians(90), 0), verts=12, material='Gunmetal'))
parts.append(box('grip', (0.05, 0.05, 0.16), (-0.28, 0.10, 0.78), material='Gunmetal'))
parts.append(box('grip2', (0.05, 0.05, 0.16), (-0.28, -0.10, 0.78), material='Gunmetal'))
parts.append(box('shield_l', (0.04, 0.3, 0.4), (0.40, 0.2, 0.86), rot=(0, 0, math.radians(-14)), bevel=0.01, material='Olive'))
parts.append(box('shield_r', (0.04, 0.3, 0.4), (0.40, -0.2, 0.86), rot=(0, 0, math.radians(14)), bevel=0.01, material='Olive'))
parts.append(box('shield_top', (0.04, 0.12, 0.12), (0.43, 0, 1.0), bevel=0.01, material='Olive'))
parts.append(box('shield_bottom', (0.04, 0.12, 0.14), (0.43, 0, 0.72), bevel=0.01, material='Olive'))
export(parts, 'SM_Turret')
print('MESHES_OK')
