# LookDev map: the soldier in a neutral grey studio (key, fill, rim, fixed exposure) to compare the materials with
# the asset's own previews. Left: the pack's material (MI_RetroSoldier_Player), middle: our player material
# (MI_Char_Player), right: our carrier material. Capture:
#   Tools/run_review.sh /Game/Freefall/Maps/LookDev IT3/lookdev/shots -FallShot=0.5 -FallDuration=3
import unreal

eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
MAP = '/Game/Freefall/Maps/LookDev'

grey_path = '/Game/Freefall/Materials/MI_LookDev_Grey'
grey = unreal.load_asset(grey_path) if eal.does_asset_exist(grey_path) else at.create_asset('MI_LookDev_Grey', '/Game/Freefall/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
grey.set_editor_property('parent', unreal.load_asset('/Game/Freefall/Materials/M_FF_Lit'))
mel.set_material_instance_vector_parameter_value(grey, 'Color', unreal.LinearColor(0.18, 0.18, 0.18, 1))
mel.set_material_instance_scalar_parameter_value(grey, 'Roughness', 0.95)
mel.set_material_instance_scalar_parameter_value(grey, 'Metallic', 0.0)
eal.save_loaded_asset(grey, False)

if eal.does_asset_exist(MAP):
    eal.delete_asset(MAP)
les.new_level(MAP)
world = unreal.EditorLevelLibrary.get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode', unreal.GameModeBase)

quad = unreal.load_asset('/Game/Freefall/Assets/Meshes/SM_FF_Quad')
def card(loc, rot, scale):
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, loc, rot)
    c = a.static_mesh_component
    c.set_static_mesh(quad)
    c.set_material(0, grey)
    a.set_actor_scale3d(scale)
    return a
card(unreal.Vector(0, 0, 0), unreal.Rotator(pitch=0, yaw=0, roll=0), unreal.Vector(40, 40, 1))                     # floor
card(unreal.Vector(-600, 0, 1000), unreal.Rotator(pitch=-90, yaw=0, roll=0), unreal.Vector(30, 40, 1))             # back wall (plane faces +X)

mesh = unreal.load_asset('/Game/RetroFuturisticSoldier/Meshes/SKM_Soldier_Manny')
idle = unreal.load_asset('/Game/RetroFuturisticSoldier/Anims/Unarmed/MM_Idle')
mats = [unreal.load_asset('/Game/RetroFuturisticSoldier/Materials/MI_RetroSoldier_Player'),
        unreal.load_asset('/Game/Freefall/Materials/MI_Char_Player'),
        unreal.load_asset('/Game/Freefall/Materials/MI_Char_Carrier')]
for i, m in enumerate(mats):
    a = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, (1 - i) * 110, 0), unreal.Rotator(pitch=0, yaw=-90, roll=0))  # faces the camera; pack material on the left
    c = a.skeletal_mesh_component
    c.set_skeletal_mesh_asset(mesh)
    for s in range(c.get_num_materials()):
        c.set_material(s, m)
    c.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
    d = c.get_editor_property('animation_data')
    d.set_editor_property('anim_to_play', idle)
    d.set_editor_property('saved_looping', True)
    d.set_editor_property('saved_playing', True)
    c.set_editor_property('animation_data', d)

# Studio light: warm-neutral key from the front left above, soft fill from the right, cool rim from behind.
key = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(pitch=-35, yaw=-40, roll=0))
key.light_component.set_intensity(6.0)
key.light_component.set_light_color(unreal.LinearColor(1.0, 0.97, 0.92, 1))
fill = eas.spawn_actor_from_class(unreal.PointLight, unreal.Vector(350, 300, 180), unreal.Rotator(pitch=0, yaw=0, roll=0))
# Illuminance comparable to the game's sun (9 lux): key 6 lux, fill ~3 lux and rim ~8 lux at the subject.
fill.point_light_component.set_editor_property('intensity_units', unreal.LightUnits.CANDELAS)
fill.point_light_component.set_intensity(37.0)
fill.point_light_component.set_editor_property('source_radius', 120.0)
fill.point_light_component.set_editor_property('attenuation_radius', 2000.0)
rim = eas.spawn_actor_from_class(unreal.SpotLight, unreal.Vector(-350, -120, 320), unreal.Rotator(pitch=-25, yaw=10, roll=0))
rim.spot_light_component.set_editor_property('intensity_units', unreal.LightUnits.CANDELAS)
rim.spot_light_component.set_intensity(110.0)
rim.spot_light_component.set_light_color(unreal.LinearColor(0.85, 0.92, 1.0, 1))
rim.spot_light_component.set_editor_property('outer_cone_angle', 50.0)
rim.spot_light_component.set_editor_property('attenuation_radius', 2500.0)
sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 300), unreal.Rotator(pitch=0, yaw=0, roll=0))
sky.light_component.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
sky.light_component.set_editor_property('cubemap', unreal.load_asset('/Engine/EngineResources/GrayDefaultCubemap') if eal.does_asset_exist('/Engine/EngineResources/GrayDefaultCubemap') else None)
sky.light_component.set_intensity(0.6)

pp = eas.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), unreal.Rotator(pitch=0, yaw=0, roll=0))
pp.set_editor_property('unbound', True)
s = pp.get_editor_property('settings')
for k, v in (('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL), ('auto_exposure_bias', 1.3), ('auto_exposure_apply_physical_camera_exposure', False),
             ('bloom_intensity', 0.2), ('vignette_intensity', 0.0), ('motion_blur_amount', 0.0)):
    s.set_editor_property('override_' + k, True)
    s.set_editor_property(k, v)
pp.set_editor_property('settings', s)

cam = eas.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(520, 0, 120), unreal.Rotator(pitch=-4, yaw=180, roll=0))
cam.set_editor_property('auto_activate_for_player', unreal.AutoReceiveInput.PLAYER0)
cam.camera_component.set_field_of_view(42.0)
eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(1500, 0, 100), unreal.Rotator(pitch=0, yaw=180, roll=0))  # the default pawn spawns out of shot
eas.spawn_actor_from_class(unreal.FallReview, unreal.Vector(0, 0, 0), unreal.Rotator(pitch=0, yaw=0, roll=0))

les.save_current_level()
print('LOOKDEV_OK')
