# Builds the upper-body reload animation from a recording of the procedural reload:
#   Tools/run_review.sh /Game/Freefall/Maps/Freefall IT3/bake -FallBakeReload -FallNoIntro
#   Tools/run_python.sh Tools/make_reload_anim.py
# A_Reload_Freefall (AnimSequence, tracks for the spine_04/05, clavicles and everything below them) and
# AM_Reload_Freefall (montage, DefaultSlot). At runtime FFallAnimProxy layers the montage over the fall pose per bone.
import json, os
import unreal

L = lambda *a: unreal.log_warning('RELOAD_ANIM ' + ' '.join(str(x) for x in a))
src = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'Review', 'IT3', 'bake', 'reload_bake.json')
d = json.load(open(src))
names, frames = d['bones'], d['frames']
skel = unreal.load_asset(d['skeleton'].split('.')[0])
at = unreal.AssetToolsHelpers.get_asset_tools()
P = '/Game/Freefall/Anims'

# Bones the montage drives: the upper spine, both clavicles and everything below them (names, Manny skeleton).
upper = []
for i, n in enumerate(names):
    side = n.endswith('_l') or n.endswith('_r')
    if n in ('spine_04', 'spine_05', 'clavicle_l', 'clavicle_r'):
        upper.append(i)
    elif side and any(n.startswith(p) for p in ('upperarm', 'lowerarm', 'hand', 'thumb', 'index', 'middle', 'ring', 'pinky')):
        upper.append(i)
L('bones in the montage', len(upper), [names[i] for i in upper][:12])

for name in ('A_Reload_Freefall', 'AM_Reload_Freefall'):
    if unreal.EditorAssetLibrary.does_asset_exist(P + '/' + name):
        unreal.EditorAssetLibrary.delete_asset(P + '/' + name)

f = unreal.AnimSequenceFactory()
f.set_editor_property('target_skeleton', skel)
seq = at.create_asset('A_Reload_Freefall', P, unreal.AnimSequence, f)
ctrl = seq.get_editor_property('controller')
ctrl.open_bracket(unreal.Text('reload'), False)
ctrl.set_frame_rate(unreal.FrameRate(d['fps'], 1), False)
ctrl.set_number_of_frames(unreal.FrameNumber(len(frames) - 1), False)
for i in upper:
    pos, rot, scl = [], [], []
    for fr in frames:
        q = fr[i * 7:i * 7 + 4]
        t = fr[i * 7 + 4:i * 7 + 7]
        rot.append(unreal.Quat(q[0], q[1], q[2], q[3]))
        pos.append(unreal.Vector(t[0], t[1], t[2]))
        scl.append(unreal.Vector(1, 1, 1))
    ctrl.add_bone_curve(names[i], False)
    ctrl.set_bone_track_keys(names[i], pos, rot, scl, False)
ctrl.close_bracket(False)
unreal.EditorAssetLibrary.save_loaded_asset(seq, False)
L('sequence', seq.get_path_name(), 'length', seq.get_play_length())

mf = unreal.AnimMontageFactory()
mf.set_editor_property('target_skeleton', skel)
mf.set_editor_property('source_animation', seq)
mon = at.create_asset('AM_Reload_Freefall', P, unreal.AnimMontage, mf)
for prop, sec in (('blend_in', 0.12), ('blend_out', 0.2)):
    b = mon.get_editor_property(prop)
    b.set_editor_property('blend_time', sec)
    mon.set_editor_property(prop, b)
unreal.EditorAssetLibrary.save_loaded_asset(mon, False)
L('montage', mon.get_path_name(), 'length', mon.get_play_length())
