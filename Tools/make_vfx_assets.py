# Wind and look assets: screen wind post process, terrain material on Poly Haven aerial textures, scrap material,
# recorded wind loops. Idempotent. Run: Tools/run_python.sh Tools/make_vfx_assets.py (editor closed) or in the editor.
import os
import unreal

mel = unreal.MaterialEditingLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
P = '/Game/Freefall/Materials'
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())


def mk(name):
    path = P + '/' + name
    if eal.does_asset_exist(path):
        m = unreal.load_asset(path)
        mel.delete_all_material_expressions(m)
        return m
    return at.create_asset(name, P, unreal.Material, unreal.MaterialFactoryNew())


def e(m, cls, x, y, **props):
    n = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def sparam(m, name, val, x, y):
    return e(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=val)


# ---- screen wind: radial smear at the edges with speed, frost and white veil at the edges inside clouds
m = mk('M_FF_ScreenWind')
m.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
m.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
st = e(m, unreal.MaterialExpressionSceneTexture, -900, -200, scene_texture_id=unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
uv = e(m, unreal.MaterialExpressionTextureCoordinate, -900, 0)
noise = e(m, unreal.MaterialExpressionTextureSample, -900, 150, texture=unreal.load_asset('/Game/Freefall/Assets/Textures/caban/caban_disp_2k'))
nuv = e(m, unreal.MaterialExpressionMultiply, -1050, 150)
mel.connect_material_expressions(uv, '', nuv, 'A')
nuv.set_editor_property('const_b', 2.5)
mel.connect_material_expressions(nuv, '', noise, 'UVs')
cu = e(m, unreal.MaterialExpressionCustom, -500, 0, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3, description='ScreenWind')
cu.set_editor_property('code', '''
float2 c = float2(0.5, 0.5);
float2 d = UV - c;
float r = length(d * float2(1.6, 1.0));
float k = Strength * smoothstep(0.45, 0.95, r);
float3 acc = 0;
[unroll] for (int i = 0; i < 10; i++)
{
    float s = 1.0 - k * 0.028 * i;
    acc += SceneTextureLookup(clamp(c + d * s, 0.001, 0.999), 14, false).rgb;
}
acc /= 10.0;
float edge = smoothstep(0.45, 0.95, r);
float frost = saturate(edge * Frost * (0.55 + 0.9 * Noise));
acc = lerp(acc, acc * 0.6 + float3(0.62, 0.66, 0.7), frost * 0.75);
acc = lerp(acc, float3(0.86, 0.87, 0.88), saturate(Veil * (0.35 + 0.65 * edge)) * 0.8);
return acc;
''')
ins = []
for n in ('UV', 'Strength', 'Frost', 'Veil', 'Noise', 'Dummy'):
    ci = unreal.CustomInput()
    ci.set_editor_property('input_name', n)
    ins.append(ci)
cu.set_editor_property('inputs', ins)
mel.connect_material_expressions(uv, '', cu, 'UV')
mel.connect_material_expressions(sparam(m, 'Strength', 0.0, -800, 300), '', cu, 'Strength')
mel.connect_material_expressions(sparam(m, 'Frost', 0.0, -800, 400), '', cu, 'Frost')
mel.connect_material_expressions(sparam(m, 'Veil', 0.0, -800, 500), '', cu, 'Veil')
mel.connect_material_expressions(noise, 'R', cu, 'Noise')
mel.connect_material_expressions(st, 'Color', cu, 'Dummy')  # keeps the scene texture bound for SceneTextureLookup
mel.connect_material_property(cu, '', MP.MP_EMISSIVE_COLOR)
mel.recompile_material(m)

# ---- terrain: vertex colour (meadow, forest, rock, snow by height and slope) times aerial photo detail
m = mk('M_FF_Terrain')
tc = e(m, unreal.MaterialExpressionTextureCoordinate, -1000, 0)
def tex(name, path, sampler, scale, x, y):
    t = e(m, unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name, texture=unreal.load_asset(path), sampler_type=sampler)
    mul = e(m, unreal.MaterialExpressionMultiply, x - 150, y)
    mel.connect_material_expressions(tc, '', mul, 'A')
    mul.set_editor_property('const_b', scale)
    mel.connect_material_expressions(mul, '', t, 'UVs')
    return t
TX = '/Game/Freefall/Assets/Textures/'
near = tex('DetailNear', TX + 'aerial_grass_rock/aerial_grass_rock_diff_2k', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 3.0, -700, -200)
far = tex('DetailFar', TX + 'aerial_rocks_02/aerial_rocks_02_diff_2k', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 0.35, -700, 100)
nrm = tex('Normal', TX + 'aerial_rocks_02/aerial_rocks_02_nor_gl_2k', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 0.35, -700, 400)
vc = e(m, unreal.MaterialExpressionVertexColor, -700, -450)
cu = e(m, unreal.MaterialExpressionCustom, -350, -150, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3, description='TerrainDetail')
cu.set_editor_property('code', '''
float lnear = dot(Near.rgb, float3(0.3, 0.59, 0.11));
float lfar = dot(Far.rgb, float3(0.3, 0.59, 0.11));
float detail = lerp(1.0, (lnear * 0.55 + lfar * 0.45) * 2.6, 0.8);
float snow = saturate((dot(VC.rgb, float3(0.33, 0.33, 0.33)) - 0.55) * 4.0);
float3 tinted = VC.rgb * detail * lerp(float3(1,1,1), Near.rgb / max(lnear, 0.05), 0.35);
return lerp(tinted, VC.rgb * lerp(0.9, 1.05, lfar), snow);
''')
ins = []
for n in ('VC', 'Near', 'Far'):
    ci = unreal.CustomInput(); ci.set_editor_property('input_name', n); ins.append(ci)
cu.set_editor_property('inputs', ins)
mel.connect_material_expressions(vc, '', cu, 'VC')
mel.connect_material_expressions(near, 'RGB', cu, 'Near')
mel.connect_material_expressions(far, 'RGB', cu, 'Far')
mel.connect_material_property(cu, '', MP.MP_BASE_COLOR)
mel.connect_material_property(nrm, 'RGB', MP.MP_NORMAL)
mel.connect_material_property(sparam(m, 'Roughness', 0.92, -350, 200), '', MP.MP_ROUGHNESS)
mel.recompile_material(m)

# ---- scraps of envelope and paper torn off the airship (two-sided canvas)
def mi(name, parent, scalars=None, vectors=None, textures=None):
    path = P + '/' + name
    x = unreal.load_asset(path) if eal.does_asset_exist(path) else at.create_asset(name, P, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    x.set_editor_property('parent', unreal.load_asset(parent))
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(x, k, v)
    for k, v in (vectors or {}).items():
        mel.set_material_instance_vector_parameter_value(x, k, unreal.LinearColor(*v))
    for k, v in (textures or {}).items():
        mel.set_material_instance_texture_parameter_value(x, k, unreal.load_asset(v))
    eal.save_loaded_asset(x, False)
mi('MI_Scrap', P + '/M_FF_PBRTwoSided', {'Tiling': 1.0, 'MetallicMul': 0.0}, {'Tint': (0.55, 0.42, 0.3, 1)},
   {'BaseColor': TX + 'hessian_380/hessian_380_diff_2k', 'Normal': TX + 'hessian_380/hessian_380_nor_gl_2k', 'ARM': TX + 'hessian_380/hessian_380_arm_2k'})

# ---- recorded wind loops (BigSoundBank, CC0)
SRC = os.path.join(PROJ, 'ThirdParty', 'BigSoundBank')
for f in sorted(os.listdir(SRC)):
    if not f.endswith('.wav'):
        continue
    name = 'SW_' + os.path.splitext(f)[0]
    path = '/Game/Freefall/Audio/' + name
    if not eal.does_asset_exist(path):
        t = unreal.AssetImportTask()
        t.filename = os.path.join(SRC, f); t.destination_path = '/Game/Freefall/Audio'; t.destination_name = name
        t.automated = True; t.replace_existing = True; t.save = True
        at.import_asset_tasks([t])
    w = unreal.load_asset(path)
    if w and (f.startswith('wind') or f.startswith('flag_wind') or f.startswith('big_fire')):
        w.set_editor_property('looping', True)
        eal.save_loaded_asset(w, False)
eal.save_directory(P)
print('FALL_VFX_ASSETS_OK')
