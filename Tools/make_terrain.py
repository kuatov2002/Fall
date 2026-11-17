# Bakes the height grids from Tools/fetch_terrain.py into DA_FallTerrain. Run: Tools/run_python.sh Tools/make_terrain.py
import array, json, os
import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJ, 'ThirdParty', 'Processed', 'Terrain')
eal = unreal.EditorAssetLibrary
path = '/Game/Freefall/Data/DA_FallTerrain'
if eal.does_asset_exist(path):
    d = unreal.load_asset(path)
else:
    f = unreal.DataAssetFactory()
    f.set_editor_property('data_asset_class', unreal.FallTerrainData)
    d = unreal.AssetToolsHelpers.get_asset_tools().create_asset('DA_FallTerrain', '/Game/Freefall/Data', unreal.FallTerrainData, f)
info = json.load(open(os.path.join(SRC, 'terrain.json')))
for name in ('near', 'mid', 'far'):
    g = info['grids'][name]
    a = array.array('f')
    with open(os.path.join(SRC, name + '.f32'), 'rb') as fh:
        a.frombytes(fh.read())
    grid = unreal.FallHeightGrid()
    grid.set_editor_property('size', g['size'])
    grid.set_editor_property('extent', g['extent_m'])
    grid.set_editor_property('heights', list(a))
    d.set_editor_property(name, grid)
d.set_editor_property('source', 'Terrarium tiles (Mapzen terrain tiles, AWS Open Data): SRTM, EU-DEM and others; centre %.3f N %.3f E, floor %d m' % (info['lat'], info['lon'], info['floor_m']))
eal.save_loaded_asset(d, False)
print('FALL_TERRAIN_OK')
