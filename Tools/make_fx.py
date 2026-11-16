# Copies the Niagara systems the slice uses from the Fab packs into /Game/Freefall/FX and makes every emitter
# simulate in local space. The fall is faked (the world moves past the player), so effects are attached either to
# the moving object (fire, sparks, muzzle flash) or to the air anchor that rises at the player's fall speed
# (explosions, smoke): local-space particles then travel with the air instead of falling with the player.
# Emitters that need ground or screen space (ground dust, decals, post process) are removed.
# Run in the editor: execute_python with exec(open(...).read()); top-level await is available there.
import json
import unreal

SRC = {
    'FX_Explosion': '/Game/NiagaraExamples/FX_Explosions/NS_Explosion',
    'FX_ExplosionMedium': '/Game/NiagaraExamples/FX_Explosions/NS_Explosion_Medium',
    'FX_ExplosionLarge': '/Game/Fire_EXP_Vol01_Free/Niagara/EXP/NS_Sub_EXP_Large_001_01',
    'FX_ExplosionMid': '/Game/Fire_EXP_Vol01_Free/Niagara/EXP/NS_Sub_EXP_Mid_002_02',
    'FX_Fire': '/Game/Fire_EXP_Vol01_Free/Niagara/Fire/Loop/NS_Sub_FireMid_Loop_001',
    'FX_FireLarge': '/Game/Fire_EXP_Vol01_Free/Niagara/Fire/Loop/NS_Sub_FireLarge_Loop_004',
    'FX_FireSmall': '/Game/Fire_EXP_Vol01_Free/Niagara/Fire/Loop/NS_Sub_FireSmall_Loop_001',
    'FX_Sparks': '/Game/NiagaraExamples/FX_Sparks/NS_Spark_Burst',
    'FX_Muzzle': '/Game/NiagaraExamples/FX_Weapons/MuzzleFlashes/NS_MuzzleFlash',
    'FX_ImpactMetal': '/Game/NiagaraExamples/FX_Weapons/Impacts/NS_Impact_Metal',
    'FX_Smoke': '/Game/NiagaraExamples/FX_Smoke/NS_Smoke_Plume',
}
DROP = ('GroundDust', 'Decal_Light_Flash', 'NE_PostProcess', 'Decal')
DEST = '/Game/Freefall/FX'
eal = unreal.EditorAssetLibrary
report = {}
for name, src in SRC.items():
    dst = DEST + '/' + name
    if not eal.does_asset_exist(dst):
        eal.duplicate_asset(src, dst)
    sys_ = unreal.load_asset(dst)
    summ = await call_tool('NiagaraToolsets.NiagaraToolset_System.GetSystemSummary', System=sys_)
    changed = []
    for em in summ['emitters']:
        en = em['emitterName']
        if any(d in en for d in DROP):
            await call_tool('NiagaraToolsets.NiagaraToolset_System.RemoveEmitter', emitterRef={'system': sys_, 'emitterName': en})
            changed.append('-' + en)
            continue
        data = await call_tool('NiagaraToolsets.NiagaraToolset_System.GetEmitterData', emitterRef={'system': sys_, 'emitterName': en})
        props = json.loads(data['propertyValues'])
        if not props.get('bLocalSpace', False):
            await call_tool('NiagaraToolsets.NiagaraToolset_System.SetEmitterData', emitterRef={'system': sys_, 'emitterName': en}, emitterData={'propertyValues': json.dumps({'bLocalSpace': True})})
            changed.append(en)
    eal.save_loaded_asset(sys_, False)
    report[name] = changed
    await asyncio.sleep(0)
print('FALL_FX_OK', json.dumps(report))
