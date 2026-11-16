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

# Soft sprite: cloud wisps, smoke, fire. Camera-facing plane, radial falloff times animated noise.
m = mk('M_FF_Sprite')
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('two_sided', True)
uv = e(m, unreal.MaterialExpressionTextureCoordinate, -1400, 0)
half = e(m, unreal.MaterialExpressionConstant2Vector, -1400, 120, r=0.5, g=0.5)
sub = bin(m, unreal.MaterialExpressionSubtract, uv, half, -1250, 40)
ln = e(m, unreal.MaterialExpressionLength, -1100, 40)
mel.connect_material_expressions(sub, '', ln, '')
two = mul(m, ln, const(m, 2.0, -1100, 140), -950, 40)
inv = e(m, unreal.MaterialExpressionOneMinus, -800, 40)
mel.connect_material_expressions(two, '', inv, '')
sat = e(m, unreal.MaterialExpressionSaturate, -680, 40)
mel.connect_material_expressions(inv, '', sat, '')
pw = e(m, unreal.MaterialExpressionPower, -550, 40)
mel.connect_material_expressions(sat, '', pw, 'Base')
mel.connect_material_expressions(sparam(m, 'Softness', 1.6, -700, 160), '', pw, 'Exp')
uvs = mul(m, uv, sparam(m, 'NoiseScale', 3.0, -1400, 300), -1250, 260)
tms = mul(m, e(m, unreal.MaterialExpressionTime, -1400, 480), sparam(m, 'NoiseSpeed', 0.15, -1400, 560), -1250, 480)
z = bin(m, unreal.MaterialExpressionAdd, sparam(m, 'Seed', 0.0, -1400, 400), tms, -1100, 440)
ap = e(m, unreal.MaterialExpressionAppendVector, -1000, 300)
mel.connect_material_expressions(uvs, '', ap, 'A')
mel.connect_material_expressions(z, '', ap, 'B')
nz = e(m, unreal.MaterialExpressionNoise, -850, 300, scale=1.0, levels=4, output_min=0.0, output_max=1.0, turbulence=True)
mel.connect_material_expressions(ap, '', nz, 'Position')
nl = e(m, unreal.MaterialExpressionLinearInterpolate, -650, 300)
mel.connect_material_expressions(const(m, 1.0, -800, 520), '', nl, 'A')
mel.connect_material_expressions(nz, '', nl, 'B')
mel.connect_material_expressions(sparam(m, 'NoiseAmount', 0.8, -850, 450), '', nl, 'Alpha')
shape = mul(m, pw, nl, -450, 150)
op = mul(m, shape, sparam(m, 'Opacity', 0.6, -600, 620), -300, 200)
op2 = mul(m, op, e(m, unreal.MaterialExpressionVertexColor, -600, 720), -200, 260, '', 'A')
df = e(m, unreal.MaterialExpressionDepthFade, -100, 300)
mel.connect_material_expressions(op2, '', df, 'Opacity')
mel.connect_material_expressions(sparam(m, 'FadeDistance', 300.0, -300, 420), '', df, 'FadeDistance')
out(m, df, MP.MP_OPACITY)
col = vparam(m, 'Color', unreal.LinearColor(0.9, 0.92, 0.97, 1), -600, -200)
shade = e(m, unreal.MaterialExpressionLinearInterpolate, -350, -120)
mel.connect_material_expressions(mul(m, col, sparam(m, 'Shadow', 0.75, -600, -60), -450, -60), '', shade, 'A')
mel.connect_material_expressions(col, '', shade, 'B')
mel.connect_material_expressions(shape, '', shade, 'Alpha')
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
