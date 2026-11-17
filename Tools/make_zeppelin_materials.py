# The Fab FBX import of "Zeppelin Environment" made untextured Phong materials. This builds MI_Zep_<slot> on
# M_FF_PBR from the pack's own textures and assigns them to the airship chunks and debris props. Idempotent.
import unreal

eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
ar = unreal.AssetRegistryHelpers.get_asset_registry()
TEX = '/Game/Fab/Zeppelin_Environment/'
MP = '/Game/Freefall/Materials'
SETS = {  # slot: (texture name prefix, metallic multiplier)
    'balloon': ('balloon_base', 0.0), 'barrel': ('barrel_', 0.3), 'barier': ('barrier_01', 0.6), 'box_light': ('box_light', 0.5),
    'door_light': ('door_light_01', 0.5), 'first_aid': ('fiarst_aid_01', 0.0), 'generator': ('generator_', 0.6),
    'hanging_light': ('hanging_light', 0.5), 'main_building': ('main_building', 0.4), 'spot_light': ('spot_light', 0.6),
    'stairs': ('stairs_', 0.6), 'tarp': ('tarp_', 0.0), 'air_vent': ('air_vents_01', 0.6), 'air_vent_box': ('air_vent_box', 0.6),
    'ancchor_point': ('anchor_point_01', 0.6), 'door': ('blast_door', 0.6), 'boot': ('boat_material_one', 0.0),
    'crate': ('box_crate_01', 0.0), 'crane': ('crane_', 0.6), 'pipe': ('large_pipe_01', 0.6), 'main_console': ('main_control_panel', 0.5),
    'pallet': ('pallet_01', 0.0), 'ship': ('ship_', 0.3), '15_-_Default': ('ship_floor_01', 0.2), 'valve': ('valve_01', 0.6),
}
texs = {str(a.asset_name).lower(): str(a.package_name) for a in ar.get_assets_by_path(TEX, recursive=True) if str(a.asset_class_path.asset_name) == 'Texture2D'}


def pick(prefix, kinds, exclude=()):
    for n, p in sorted(texs.items()):
        if n.startswith(prefix) and any(k in n for k in kinds) and not any(x in n for x in exclude):
            return p
    return None


def fix(path, normal):
    t = unreal.load_asset(path)
    want = unreal.TextureCompressionSettings.TC_NORMALMAP if normal else unreal.TextureCompressionSettings.TC_MASKS
    if t.get_editor_property('compression_settings') != want:
        t.set_editor_property('compression_settings', want)
        t.set_editor_property('srgb', False)
        eal.save_loaded_asset(t, False)
    return t


made = {}
for slot, (prefix, metal) in SETS.items():
    base = pick(prefix, ('base', 'colour', 'color'), ('normal', 'emissive'))
    nrm = pick(prefix, ('normal',))
    arm = pick(prefix, ('rough', 'roughness_ao'))
    if not base:
        continue
    name = 'MI_Zep_' + slot.replace('-', '').replace('__', '_')
    path = MP + '/' + name
    m = unreal.load_asset(path) if eal.does_asset_exist(path) else at.create_asset(name, MP, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    # The envelope is a thin shell: one-sided, its torn chunks showed loose triangles from inside.
    m.set_editor_property('parent', unreal.load_asset('/Game/Freefall/Materials/' + ('M_FF_PBRTwoSided' if slot == 'balloon' else 'M_FF_PBR')))
    mel.set_material_instance_texture_parameter_value(m, 'BaseColor', unreal.load_asset(base))
    if nrm:
        mel.set_material_instance_texture_parameter_value(m, 'Normal', fix(nrm, True))
    if arm:
        mel.set_material_instance_texture_parameter_value(m, 'ARM', fix(arm, False))
    mel.set_material_instance_scalar_parameter_value(m, 'MetallicMul', metal)
    eal.save_loaded_asset(m, False)
    made[slot] = m
plain = unreal.load_asset(MP + '/MI_Gunmetal')
count = 0
for folder in ('/Game/Freefall/Assets/Airship', '/Game/Freefall/Assets/Debris'):
    for p in eal.list_assets(folder, recursive=False):
        sm = unreal.load_asset(p)
        if not isinstance(sm, unreal.StaticMesh):
            continue
        for i, s in enumerate(sm.static_materials):
            slot = str(s.material_slot_name).replace(' ', '_')
            sm.set_material(i, made.get(slot, plain))
            count += 1
        eal.save_loaded_asset(sm, False)
print('FALL_ZEPMAT_OK', len(made), count)
