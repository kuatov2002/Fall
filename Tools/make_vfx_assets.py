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
nuv.set_editor_property('const_b', 0.7)  # low frequency: frost reads as patches, not film grain
mel.connect_material_expressions(nuv, '', noise, 'UVs')
cu = e(m, unreal.MaterialExpressionCustom, -500, 0, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3, description='ScreenWind')
cu.set_editor_property('code', '''
float2 c = float2(0.5, 0.5);
float2 d = UV - c;
float r = length(d * float2(1.6, 1.0));
float k = Strength * smoothstep(0.55, 1.0, r);
float3 acc = 0;
[unroll] for (int i = 0; i < 10; i++)
{
    float s = 1.0 - k * 0.022 * i;
    acc += SceneTextureLookup(clamp(c + d * s, 0.001, 0.999), 14, false).rgb;
}
acc /= 10.0;
float edge = smoothstep(0.62, 1.05, r);
// smooth procedural patches (a texture here read as film grain)
float2 q = UV * float2(5.0, 3.0);
float n = 0.5 + 0.25 * sin(q.x * 1.7 + sin(q.y * 2.3) * 1.5) + 0.25 * sin(q.y * 1.9 + sin(q.x * 1.3 + 2.0) * 1.7);
float frost = saturate(edge * Frost * smoothstep(0.35, 0.75, n));
acc = lerp(acc, acc * 0.7 + float3(0.55, 0.6, 0.66), frost * 0.6);
acc = lerp(acc, float3(0.8, 0.82, 0.84), saturate(Veil * (0.15 + 0.85 * edge)) * 0.6);
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
fine = tex('DetailFine', TX + 'aerial_grass_rock/aerial_grass_rock_diff_2k', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 28.0, -700, -350)
depth = e(m, unreal.MaterialExpressionPixelDepth, -700, -500)
far = tex('DetailFar', TX + 'aerial_rocks_02/aerial_rocks_02_diff_2k', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 0.35, -700, 100)
nrm = tex('Normal', TX + 'aerial_rocks_02/aerial_rocks_02_nor_gl_2k', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 0.35, -700, 400)
vc = e(m, unreal.MaterialExpressionVertexColor, -700, -450)
cu = e(m, unreal.MaterialExpressionCustom, -350, -150, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3, description='TerrainDetail')
cu.set_editor_property('code', '''
float lfine = dot(Fine.rgb, float3(0.3, 0.59, 0.11));
float nearW = 1.0 - smoothstep(20000.0, 60000.0, Depth); // the last few hundred metres get 3 m detail
float lnear = lerp(dot(Near.rgb, float3(0.3, 0.59, 0.11)), lfine * 0.5 + dot(Near.rgb, float3(0.3, 0.59, 0.11)) * 0.5, nearW);
float lfar = dot(Far.rgb, float3(0.3, 0.59, 0.11));
float detail = lerp(1.0, (lnear * 0.55 + lfar * 0.45) * 2.6, 0.8);
float snow = saturate((dot(VC.rgb, float3(0.33, 0.33, 0.33)) - 0.55) * 4.0);
float3 tinted = VC.rgb * detail * lerp(float3(1,1,1), Near.rgb / max(lnear, 0.05), 0.35);
return lerp(tinted, VC.rgb * lerp(0.9, 1.05, lfar), snow);
''')
ins = []
for n in ('VC', 'Near', 'Far', 'Fine', 'Depth'):
    ci = unreal.CustomInput(); ci.set_editor_property('input_name', n); ins.append(ci)
cu.set_editor_property('inputs', ins)
mel.connect_material_expressions(vc, '', cu, 'VC')
mel.connect_material_expressions(near, 'RGB', cu, 'Near')
mel.connect_material_expressions(far, 'RGB', cu, 'Far')
mel.connect_material_expressions(fine, 'RGB', cu, 'Fine')
mel.connect_material_expressions(depth, '', cu, 'Depth')
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

# ---- character: a copy of the soldier material with the gloss capped (the chrome armour mirrored the sky white)
src = '/Game/RetroFuturisticSoldier/Materials/M_RetroSoldier'
dst = P + '/M_Char'
if not eal.does_asset_exist(dst):
    eal.duplicate_asset(src, dst)
    m = unreal.load_asset(dst)
    rn = mel.get_material_property_input_node(m, MP.MP_ROUGHNESS)
    mn = mel.get_material_property_input_node(m, MP.MP_METALLIC)
    rmax = e(m, unreal.MaterialExpressionMax, rn.get_editor_property('material_expression_editor_x') + 250, rn.get_editor_property('material_expression_editor_y'))
    mel.connect_material_expressions(rn, mel.get_material_property_input_node_output_name(m, MP.MP_ROUGHNESS) if hasattr(mel, 'get_material_property_input_node_output_name') else 'R', rmax, 'A')
    rmax.set_editor_property('const_b', 0.5)
    mel.connect_material_property(rmax, '', MP.MP_ROUGHNESS)
    mmul = e(m, unreal.MaterialExpressionMultiply, mn.get_editor_property('material_expression_editor_x') + 250, mn.get_editor_property('material_expression_editor_y'))
    mel.connect_material_expressions(mn, 'R', mmul, 'A')
    mmul.set_editor_property('const_b', 0.5)
    mel.connect_material_property(mmul, '', MP.MP_METALLIC)
    mel.recompile_material(m)
    eal.save_loaded_asset(m, False)
for n in ('MI_Char_Player', 'MI_Char_Shooter', 'MI_Char_Heavy', 'MI_Char_Wingsuit', 'MI_Char_Carrier'):
    x = unreal.load_asset(P + '/' + n)
    if x:
        x.set_editor_property('parent', unreal.load_asset(dst))
        eal.save_loaded_asset(x, False)

# ---- broken cloud deck instead of a solid white plate
cpath = P + '/MI_FF_Clouds'
base = unreal.load_asset('/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst')
c = unreal.load_asset(cpath) if eal.does_asset_exist(cpath) else at.create_asset('MI_FF_Clouds', P, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
c.set_editor_property('parent', base)
mel.set_material_instance_scalar_parameter_value(c, 'Cloud_GlobalCoverage', -0.27)
mel.set_material_instance_scalar_parameter_value(c, 'Layout_CloudGlobalScale', 190.0)
# sunset cloud sea: warmer and darker than white, so it does not out-glow the sky above the horizon
mel.set_material_instance_vector_parameter_value(c, 'Cloud_AlbedoColor', unreal.LinearColor(0.5, 0.42, 0.36, 1))
eal.save_loaded_asset(c, False)

eal.save_directory(P)
print('FALL_VFX_ASSETS_OK')
