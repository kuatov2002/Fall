# Creates every Freefall material. Idempotent: run with
#   UnrealEditor-Cmd.exe Fall.uproject -run=pythonscript -script=Tools/make_materials.py
import unreal
mel = unreal.MaterialEditingLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()
P = '/Game/Freefall/Materials'
MP = unreal.MaterialProperty


def mk(name):
    path = P + '/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        m = unreal.load_asset(path)
        mel.delete_all_material_expressions(m)
        return m
    return at.create_asset(name, P, unreal.Material, unreal.MaterialFactoryNew())


def e(m, cls, x, y, **props):
    n = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def vparam(m, name, val, x, y):
    return e(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=val)


def sparam(m, name, val, x, y):
    return e(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=val)


def const(m, v, x, y):
    return e(m, unreal.MaterialExpressionConstant, x, y, r=v)


def bin(m, cls, a, b, x, y, ao='', bo=''):
    n = mel.create_material_expression(m, cls, x, y)
    mel.connect_material_expressions(a, ao, n, 'A')
    mel.connect_material_expressions(b, bo, n, 'B')
    return n


def mul(m, a, b, x, y, ao='', bo=''):
    return bin(m, unreal.MaterialExpressionMultiply, a, b, x, y, ao, bo)


def out(m, node, prop, o=''):
    mel.connect_material_property(node, o, prop)


def finish(m):
    # Without usage flags an uncooked -game run silently falls back to the default material.
    for flag in ('used_with_skeletal_mesh', 'used_with_instanced_static_meshes', 'used_with_niagara_sprites', 'used_with_niagara_meshes', 'used_with_niagara_ribbons', 'used_with_nanite'):
        try:
            m.set_editor_property(flag, True)
        except Exception:
            pass
    mel.recompile_material(m)


# Lit: debris, props, trees
m = mk('M_FF_Lit')
out(m, vparam(m, 'Color', unreal.LinearColor(0.3, 0.3, 0.32, 1), -600, 0), MP.MP_BASE_COLOR)
out(m, sparam(m, 'Roughness', 0.6, -600, 200), MP.MP_ROUGHNESS)
out(m, sparam(m, 'Metallic', 0.2, -600, 300), MP.MP_METALLIC)
out(m, vparam(m, 'Emissive', unreal.LinearColor(0, 0, 0, 1), -600, 400), MP.MP_EMISSIVE_COLOR)
finish(m)

# Lit two-sided: straps, membranes, canopy
m = mk('M_FF_LitTwoSided')
m.set_editor_property('two_sided', True)
out(m, vparam(m, 'Color', unreal.LinearColor(0.2, 0.2, 0.2, 1), -600, 0), MP.MP_BASE_COLOR)
out(m, sparam(m, 'Roughness', 0.8, -600, 200), MP.MP_ROUGHNESS)
out(m, vparam(m, 'Emissive', unreal.LinearColor(0, 0, 0, 1), -600, 400), MP.MP_EMISSIVE_COLOR)
finish(m)

# Unlit opaque: lasers, tracer cores
m = mk('M_FF_Unlit')
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
out(m, mul(m, vparam(m, 'Color', unreal.LinearColor(1, 0.2, 0.1, 1), -600, 0), sparam(m, 'Intensity', 5.0, -600, 200), -300, 0), MP.MP_EMISSIVE_COLOR)
finish(m)

# Additive with a soft edge: streaks, flashes, sparks, rim flash overlay (Rim=1 lights the silhouette edge)
m = mk('M_FF_Additive')
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
m.set_editor_property('two_sided', True)
c = vparam(m, 'Color', unreal.LinearColor(1, 1, 1, 1), -900, 0)
i = sparam(m, 'Intensity', 1.0, -900, 200)
fr = e(m, unreal.MaterialExpressionFresnel, -900, 350, exponent=2.0)
one = e(m, unreal.MaterialExpressionOneMinus, -700, 350)
mel.connect_material_expressions(fr, '', one, '')
lerp = e(m, unreal.MaterialExpressionLinearInterpolate, -550, 400)
mel.connect_material_expressions(one, '', lerp, 'A')
mel.connect_material_expressions(fr, '', lerp, 'B')
mel.connect_material_expressions(sparam(m, 'Rim', 0.0, -900, 500), '', lerp, 'Alpha')
out(m, mul(m, mul(m, c, i, -600, 0), lerp, -350, 100), MP.MP_EMISSIVE_COLOR)
finish(m)

# Streak: additive, tapered at both ends and soft across, so speed lines read as motion, not rain.
m = mk('M_FF_Streak')
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
m.set_editor_property('two_sided', True)
uv = e(m, unreal.MaterialExpressionTextureCoordinate, -1400, 0)
def taper(chan, y):
    mk1 = e(m, unreal.MaterialExpressionComponentMask, -1250, y, r=(chan == 'r'), g=(chan == 'g'), b=False, a=False)
    mel.connect_material_expressions(uv, '', mk1, '')
    two = mul(m, mk1, const(m, 2.0, -1250, y + 80), -1100, y)
    sub = bin(m, unreal.MaterialExpressionSubtract, two, const(m, 1.0, -1100, y + 80), -950, y)
    ab = e(m, unreal.MaterialExpressionAbs, -820, y)
    mel.connect_material_expressions(sub, '', ab, '')
    inv = e(m, unreal.MaterialExpressionOneMinus, -700, y)
    mel.connect_material_expressions(ab, '', inv, '')
    return inv
t = mul(m, taper('r', 0), taper('g', 200), -550, 100)
pw = e(m, unreal.MaterialExpressionPower, -420, 100)
mel.connect_material_expressions(t, '', pw, 'Base')
mel.connect_material_expressions(const(m, 1.5, -550, 220), '', pw, 'Exp')
c = vparam(m, 'Color', unreal.LinearColor(0.8, 0.88, 1, 1), -600, -200)
i = sparam(m, 'Intensity', 1.5, -600, -60)
out(m, mul(m, mul(m, c, i, -400, -150), pw, -250, 0), MP.MP_EMISSIVE_COLOR)
finish(m)

# Invisible: hides material slots (the pistol is shown as two components, the slide moves on its own).
m = mk('M_FF_Invisible')
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
out(m, const(m, 0.0, -300, 0), MP.MP_OPACITY_MASK)
finish(m)

# Soft sprite: smoke, fire, wisps. A puff from a 4x4 atlas of real smoke puffs (Kenney, CC0), a different puff
# and rotation per particle (Seed), light noise on top. A radial disc read as a string of beads.
import os as _os
_atlas = '/Game/Freefall/Assets/Textures/T_SmokeAtlas'
if not unreal.EditorAssetLibrary.does_asset_exist(_atlas):
    _t = unreal.AssetImportTask()
    _t.filename = _os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), 'ThirdParty', 'Processed', 'Textures', 'T_SmokeAtlas.png')
    _t.destination_path = '/Game/Freefall/Assets/Textures'
    _t.destination_name = 'T_SmokeAtlas'
    _t.automated = True
    _t.save = True
    at.import_asset_tasks([_t])
m = mk('M_FF_Sprite')
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('two_sided', True)
uv = e(m, unreal.MaterialExpressionTextureCoordinate, -1400, 0)
seed = sparam(m, 'Seed', 0.0, -1400, 120)
cu = e(m, unreal.MaterialExpressionCustom, -1150, 0, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT2, description='PuffUV')
cu.set_editor_property('code', (
    'float a = Seed * 2.399;\n'
    'float2 d = UV - 0.5;\n'
    'float2 r = float2(d.x * cos(a) - d.y * sin(a), d.x * sin(a) + d.y * cos(a)) + 0.5;\n'
    'float v = floor(frac(Seed * 0.618) * 16.0);\n'
    'float2 cell = float2(fmod(v, 4.0), floor(v / 4.0));\n'
    'return (saturate(r) * 0.94 + 0.03 + cell) / 4.0;'))
_ins = []
for _n in ('UV', 'Seed'):
    _c = unreal.CustomInput()
    _c.set_editor_property('input_name', _n)
    _ins.append(_c)
cu.set_editor_property('inputs', _ins)
mel.connect_material_expressions(uv, '', cu, 'UV')
mel.connect_material_expressions(seed, '', cu, 'Seed')
# Tried in round 6: Epic's 8x8 smoke flipbook from NiagaraExamples (its channel packing is hidden behind material
# functions; on a plain quad the smoke vanished), back to the Kenney atlas.
tex = e(m, unreal.MaterialExpressionTextureSampleParameter2D, -900, 0, parameter_name='Puffs', texture=unreal.load_asset(_atlas))
mel.connect_material_expressions(cu, '', tex, 'UVs')
uvs = mul(m, uv, sparam(m, 'NoiseScale', 3.0, -1400, 300), -1250, 260)
tms = mul(m, e(m, unreal.MaterialExpressionTime, -1400, 480), sparam(m, 'NoiseSpeed', 0.15, -1400, 560), -1250, 480)
z = bin(m, unreal.MaterialExpressionAdd, seed, tms, -1100, 440)
ap = e(m, unreal.MaterialExpressionAppendVector, -1000, 300)
mel.connect_material_expressions(uvs, '', ap, 'A')
mel.connect_material_expressions(z, '', ap, 'B')
nz = e(m, unreal.MaterialExpressionNoise, -850, 300, scale=1.0, levels=3, output_min=0.0, output_max=1.0, turbulence=True)
mel.connect_material_expressions(ap, '', nz, 'Position')
nl = e(m, unreal.MaterialExpressionLinearInterpolate, -650, 300)
mel.connect_material_expressions(const(m, 1.0, -800, 520), '', nl, 'A')
mel.connect_material_expressions(nz, '', nl, 'B')
mel.connect_material_expressions(sparam(m, 'NoiseAmount', 0.5, -850, 450), '', nl, 'Alpha')
# Softer than the atlas: the puffs' own edges are drawn hard (cartoon smoke); a power on alpha and a radial falloff
# turn them into soft cloud (critic round 2: "white stickers with hard edges").
soft = e(m, unreal.MaterialExpressionCustom, -650, 120, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1, description='SoftPuff')
soft.set_editor_property('code', 'return pow(saturate(A), 1.8) * smoothstep(0.52, 0.22, length(UV - 0.5));')
_si = []
for _n in ('A', 'UV'):
    _c = unreal.CustomInput(); _c.set_editor_property('input_name', _n); _si.append(_c)
soft.set_editor_property('inputs', _si)
mel.connect_material_expressions(tex, 'A', soft, 'A')
mel.connect_material_expressions(uv, '', soft, 'UV')
shape = mul(m, soft, nl, -450, 150)
op = mul(m, shape, sparam(m, 'Opacity', 0.6, -600, 620), -300, 200)
op2 = mul(m, op, e(m, unreal.MaterialExpressionVertexColor, -600, 720), -200, 260, '', 'A')
df = e(m, unreal.MaterialExpressionDepthFade, -100, 300)
mel.connect_material_expressions(op2, '', df, 'Opacity')
mel.connect_material_expressions(sparam(m, 'FadeDistance', 300.0, -300, 420), '', df, 'FadeDistance')
out(m, df, MP.MP_OPACITY)
col = vparam(m, 'Color', unreal.LinearColor(0.9, 0.92, 0.97, 1), -600, -200)
shade = e(m, unreal.MaterialExpressionLinearInterpolate, -350, -120)
mel.connect_material_expressions(mul(m, col, sparam(m, 'Shadow', 0.55, -600, -60), -450, -60), '', shade, 'A')
mel.connect_material_expressions(col, '', shade, 'B')
mel.connect_material_expressions(tex, 'R', shade, 'Alpha')
out(m, mul(m, shade, sparam(m, 'Emissive', 1.0, -350, 0), -200, -100), MP.MP_EMISSIVE_COLOR)
finish(m)

# Terrain: vertex colour
m = mk('M_FF_Terrain')
out(m, e(m, unreal.MaterialExpressionVertexColor, -600, 0), MP.MP_BASE_COLOR)
out(m, sparam(m, 'Roughness', 0.92, -600, 200), MP.MP_ROUGHNESS)
finish(m)

# Gallery backdrop: unlit, two-sided, vertical gradient
m = mk('M_FF_Backdrop')
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('two_sided', True)
c = vparam(m, 'Color', unreal.LinearColor(0.32, 0.34, 0.37, 1), -600, 0)
wp = e(m, unreal.MaterialExpressionWorldPosition, -900, 200)
mk_ = e(m, unreal.MaterialExpressionComponentMask, -750, 200, r=False, g=False, b=True)
mel.connect_material_expressions(wp, '', mk_, '')
dv = e(m, unreal.MaterialExpressionDivide, -600, 200, const_b=400000.0)
mel.connect_material_expressions(mk_, '', dv, 'A')
ad = e(m, unreal.MaterialExpressionAdd, -450, 200, const_b=1.0)
mel.connect_material_expressions(dv, '', ad, 'A')
out(m, mul(m, c, ad, -300, 100), MP.MP_EMISSIVE_COLOR)
finish(m)

unreal.EditorAssetLibrary.save_directory(P)
print('FALL_MATERIALS_OK')


# PBR master for downloaded texture sets (Poly Haven: diffuse, OpenGL normal flipped on import, ARM = AO/rough/metal).
def tparam(m, name, tex, sampler, x, y, uv=None):
    n = e(m, unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name, texture=unreal.load_asset(tex), sampler_type=sampler)
    if uv is not None:
        mel.connect_material_expressions(uv, '', n, 'UVs')
    return n


# M_FF_Fabric: thin fabric (the canopy) with two-sided foliage shading: the low sun shines through it from behind
# instead of leaving the underside black.
for name, two_sided in (('M_FF_PBR', False), ('M_FF_PBRTwoSided', True), ('M_FF_Fabric', True)):
    m = mk(name)
    tc = e(m, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    uv = mul(m, tc, sparam(m, 'Tiling', 1.0, -900, 120), -750, 0)
    bc = tparam(m, 'BaseColor', '/Engine/EngineResources/WhiteSquareTexture', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -500, -200, uv)
    nm = tparam(m, 'Normal', '/Engine/EngineMaterials/FlatNormal', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -500, 100, uv)
    arm = tparam(m, 'ARM', '/Game/Freefall/Assets/Textures/metal_plate/metal_plate_arm_2k', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -500, 400, uv)
    tinted = mul(m, bc, vparam(m, 'Tint', unreal.LinearColor(1, 1, 1, 1), -300, -300), -150, -200, 'RGB')
    out(m, tinted, MP.MP_BASE_COLOR)
    if name == 'M_FF_Fabric':
        m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
        out(m, mul(m, tinted, sparam(m, 'Transmission', 0.9, -300, -380), -150, -330), MP.MP_SUBSURFACE_COLOR)
    out(m, nm, MP.MP_NORMAL, 'RGB')
    out(m, mul(m, arm, sparam(m, 'RoughnessMul', 1.0, -300, 500), -150, 450, 'G'), MP.MP_ROUGHNESS)
    out(m, mul(m, arm, sparam(m, 'MetallicMul', 1.0, -300, 600), -150, 550, 'B'), MP.MP_METALLIC)
    out(m, arm, MP.MP_AMBIENT_OCCLUSION, 'R')
    m.set_editor_property('two_sided', two_sided)
    finish(m)
unreal.EditorAssetLibrary.save_directory(P)
print('FALL_PBR_OK')
