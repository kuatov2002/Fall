# Creates the designer-facing data assets and places the fall director in the Freefall map. Idempotent.
#   Tools/run_python.sh Tools/make_data.py
import unreal

at = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
P = '/Game/Freefall/Data'


def make(name, cls):
    path = P + '/' + name
    if eal.does_asset_exist(path):
        return unreal.load_asset(path)
    f = unreal.DataAssetFactory()
    f.set_editor_property('data_asset_class', cls)
    return at.create_asset(name, P, cls, f)


seq = make('DA_FallSequence', unreal.FallSequence)
make('DA_FallMovement', unreal.FallMovementData)
make('DA_FallAnim', unreal.FallAnimData)
make('DA_FallWeapon', unreal.FallWeaponData)
for t in ['Shooter', 'Wingsuiter', 'Heavy', 'Target']:
    e = make('DA_Enemy_' + t, unreal.FallEnemyData)
    e.set_editor_property('type', getattr(unreal.FallEnemyType, t.upper()))
    e.reset_to_type_defaults()
eal.save_directory(P)

# Director in the map, pointing at the sequence asset.
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level('/Game/Freefall/Maps/Freefall')
directors = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.FallDirector)]
d = directors[0] if directors else eas.spawn_actor_from_class(unreal.FallDirector, unreal.Vector(0, 0, 0))
d.set_editor_property('sequence', seq)
d.set_actor_label('FallDirector')
les.save_current_level()
print('FALL_DATA_OK')
