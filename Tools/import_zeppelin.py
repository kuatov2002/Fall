# Imports the chunks and props made by Tools/blender/split_zeppelin.py and points their material slots at the
# materials the Fab import of "Zeppelin Environment" created (/Game/Fab/Zeppelin_Environment/<slot>).
# Run in the editor (execute_python) or with Tools/run_python.sh.
import os
import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJ, 'ThirdParty', 'Processed', 'Zeppelin')
MATS = '/Game/Fab/Zeppelin_Environment/'
eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()


def run():
    done = []
    for f in sorted(os.listdir(SRC)):
        if not f.endswith('.fbx'):
            continue
        name = os.path.splitext(f)[0]
        dest = '/Game/Freefall/Assets/Airship' if name.startswith('SM_Airship_') else '/Game/Freefall/Assets/Debris'
        path = dest + '/' + name
        if not eal.does_asset_exist(path):
            t = unreal.AssetImportTask()
            t.filename = os.path.join(SRC, f)
            t.destination_path = dest
            t.destination_name = name
            t.automated = True
            t.save = False
            t.replace_existing = True
            at.import_asset_tasks([t])
        sm = unreal.load_asset(path)
        if not isinstance(sm, unreal.StaticMesh):
            unreal.log_warning('not a static mesh: ' + path)
            continue
        slots = sm.static_materials
        for i, s in enumerate(slots):
            slot = str(s.material_slot_name).replace(' ', '_')
            base = slot.split('.')[0]
            mat = unreal.load_asset(MATS + base) if eal.does_asset_exist(MATS + base) else None
            if mat:
                sm.set_material(i, mat)
        eal.save_loaded_asset(sm, False)
        done.append(name)
    # The FBX importer may have created placeholder materials next to the meshes: remove them.
    for d in ('/Game/Freefall/Assets/Airship', '/Game/Freefall/Assets/Debris'):
        for p in eal.list_assets(d, recursive=False):
            a = unreal.load_asset(p)
            if isinstance(a, (unreal.Material, unreal.MaterialInstanceConstant)) and not eal.find_package_referencers_for_asset(p):
                eal.delete_asset(p)
    return done


result = run()
print('ZEP_IMPORT_OK', len(result))
