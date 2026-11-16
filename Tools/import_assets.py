# Imports downloaded third-party sources (ThirdParty/...) into /Game/Freefall/Assets. Idempotent: skips assets that exist.
#   Tools/run_python.sh Tools/import_assets.py            (or run inside the editor)
# Models: glTF through Interchange, into /Game/Freefall/Assets/Models/<id>.
# Textures: jpg/png into /Game/Freefall/Assets/Textures/<id>, normal maps as normal maps, ARM/displacement linear.
import os
import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJ, 'ThirdParty', 'PolyHaven')
eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()


def import_files(files, dest):
    tasks = []
    for f in files:
        t = unreal.AssetImportTask()
        t.filename = f
        t.destination_path = dest
        t.automated = True
        t.save = True
        t.replace_existing = False
        tasks.append(t)
    if tasks:
        at.import_asset_tasks(tasks)
    return tasks


def fix_texture(path, name):
    tex = unreal.load_asset(path)
    if not tex:
        return
    n = name.lower()
    if '_nor_' in n:
        tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property('srgb', False)
        tex.set_editor_property('flip_green_channel', True)  # Poly Haven nor_gl is OpenGL, UE wants DirectX
    elif '_arm_' in n or '_disp_' in n or '_rough_' in n or '_metal_' in n:
        tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
        tex.set_editor_property('srgb', False)
    eal.save_loaded_asset(tex, False)


def main():
    import json
    manifest = json.load(open(os.path.join(SRC, 'manifest.json')))
    for aid, info in sorted(manifest.items()):
        folder = os.path.join(SRC, aid)
        if not os.path.isdir(folder):
            continue
        if info['kind'] == 'models':
            dest = '/Game/Freefall/Assets/Models/' + aid
            if eal.does_directory_have_assets(dest):
                continue
            gltf = [os.path.join(folder, f) for f in os.listdir(folder) if f.endswith('.gltf')]
            import_files(gltf, dest)
            unreal.log('IMPORTED model ' + aid)
        elif info['kind'] == 'textures':
            dest = '/Game/Freefall/Assets/Textures/' + aid
            if eal.does_directory_have_assets(dest):
                continue
            imgs = [os.path.join(folder, f) for f in os.listdir(folder) if f.lower().endswith(('.jpg', '.png'))]
            import_files(imgs, dest)
            for f in imgs:
                name = os.path.splitext(os.path.basename(f))[0]
                fix_texture(dest + '/' + name.replace('.', '_'), name)
            unreal.log('IMPORTED textures ' + aid)
    print('FALL_IMPORT_OK')


main()
