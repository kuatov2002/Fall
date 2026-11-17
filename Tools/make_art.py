# Imports the Blender-made meshes, builds material instances from the Poly Haven texture sets and the character
# colour variants. Idempotent. Run in the editor after Tools/make_materials.py.
import os
import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
MP = '/Game/Freefall/Materials'
TX = '/Game/Freefall/Assets/Textures/'


def mi(name, parent, scalars=None, vectors=None, textures=None):
    path = MP + '/' + name
    m = unreal.load_asset(path) if eal.does_asset_exist(path) else at.create_asset(name, MP, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    m.set_editor_property('parent', unreal.load_asset(parent))
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(m, k, v)
    for k, v in (vectors or {}).items():
        mel.set_material_instance_vector_parameter_value(m, k, unreal.LinearColor(*v))
    for k, v in (textures or {}).items():
        mel.set_material_instance_texture_parameter_value(m, k, unreal.load_asset(v))
    eal.save_loaded_asset(m, False)
    return m


def ph(name, tiling=1.0, tint=(1, 1, 1, 1), rough=1.0, metal=1.0, two_sided=False, set_id=None):
    s = set_id or name
    return mi('MI_' + name, '/Game/Freefall/Materials/' + ('M_FF_PBRTwoSided' if two_sided else 'M_FF_PBR'),
              {'Tiling': tiling, 'RoughnessMul': rough, 'MetallicMul': metal}, {'Tint': tint},
              {'BaseColor': TX + s + '/' + s + '_diff_2k', 'Normal': TX + s + '/' + s + '_nor_gl_2k', 'ARM': TX + s + '/' + s + '_arm_2k'})


mats = {
    'Canvas': ph('Canvas', 3.0, (1.0, 0.62, 0.08, 1), set_id='hessian_380'),
    'Webbing': ph('Webbing', 6.0, (0.25, 0.22, 0.16, 1), set_id='hessian_380'),
    'Metal': ph('Metal', 1.0, (0.9, 0.9, 0.9, 1), set_id='metal_plate'),
    'Handle': ph('HandleRed', 1.0, (0.8, 0.05, 0.02, 1), set_id='metal_plate'),
    'CanopyFabric': ph('CanopyFabric', 8.0, (0.62, 0.12, 0.05, 1), two_sided=True, set_id='caban'),
    'Gunmetal': ph('Gunmetal', 1.0, (0.35, 0.35, 0.37, 1), set_id='metal_plate_02'),
    'Olive': ph('OlivePaint', 1.0, (0.2, 0.22, 0.18, 1), set_id='green_metal_rust'),
}
ph('Leather', 2.0, set_id='fabric_leather_02')
ph('RustPlate', 1.0, set_id='blue_metal_plate')

# Character colour variants on the Retro Futuristic Soldier master: fabric and accent light.
base = '/Game/RetroFuturisticSoldier/Materials/M_RetroSoldier'
mi('MI_Char_Player', base, {'EmissionStrength': 4.0}, {'FabricColor': (0.13, 0.09, 0.05, 1), 'PrimaryColor': (1.0, 0.45, 0.12, 1)})
mi('MI_Char_Shooter', base, {'EmissionStrength': 30.0}, {'FabricColor': (0.05, 0.06, 0.08, 1), 'PrimaryColor': (1.0, 0.02, 0.0, 1)})
mi('MI_Char_Heavy', base, {'EmissionStrength': 40.0}, {'FabricColor': (0.025, 0.025, 0.03, 1), 'PrimaryColor': (1.0, 0.0, 0.0, 1)})
mi('MI_Char_Wingsuit', base, {'EmissionStrength': 35.0}, {'FabricColor': (0.09, 0.1, 0.12, 1), 'PrimaryColor': (1.0, 0.05, 0.02, 1)})
mi('MI_Char_Carrier', base, {'EmissionStrength': 25.0}, {'FabricColor': (0.06, 0.06, 0.065, 1), 'PrimaryColor': (1.0, 0.55, 0.0, 1)})

# Blender meshes.
SRC = os.path.join(PROJ, 'ThirdParty', 'Processed', 'Meshes')
REIMPORT = ('SM_FF_Beam',)  # re-imported every run: its UVs were fixed after the first import
for f in sorted(os.listdir(SRC)):
    if not f.endswith('.fbx'):
        continue
    name = os.path.splitext(f)[0]
    path = '/Game/Freefall/Assets/Meshes/' + name
    if not eal.does_asset_exist(path) or name in REIMPORT:
        t = unreal.AssetImportTask()
        t.filename = os.path.join(SRC, f); t.destination_path = '/Game/Freefall/Assets/Meshes'; t.destination_name = name
        t.automated = True; t.replace_existing = True; t.save = False
        at.import_asset_tasks([t])
    sm = unreal.load_asset(path)
    for i, s in enumerate(sm.static_materials):
        slot = str(s.material_slot_name).split('.')[0]
        if slot in mats:
            sm.set_material(i, mats[slot])
        elif slot == 'FX':
            sm.set_material(i, unreal.load_asset('/Game/Freefall/Materials/M_FF_Sprite'))
    eal.save_loaded_asset(sm, False)
for p in eal.list_assets('/Game/Freefall/Assets/Meshes', recursive=False):
    a = unreal.load_asset(p)
    if isinstance(a, (unreal.Material, unreal.MaterialInstanceConstant)) and not eal.find_package_referencers_for_asset(p):
        eal.delete_asset(p)
print('FALL_ART_OK')
