# Builds LS_Intro: the opening cutscene camera on the airship deck. Keys are in deck space (airship mesh space, cm,
# before the airship scale); AFallIntro sets the sequence's transform origin to the deck every frame, so the shot
# follows the ship wherever it hangs and whatever it rolls. Idempotent.
import math
import unreal

eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()
PATH = '/Game/Freefall/Cinematics'
NAME = 'LS_Intro'
FPS = 30
LENGTH = 17.0
SCALE = 1.2  # airship scale in the art set; keys are scaled to match

# (time, camera position, look-at point, focal length mm)
KEYS = [
    (0.0, (-250, 650, 180), (300, -150, 150), 30),
    (3.8, (-200, 450, 190), (300, -150, 160), 32),
    (4.6, (-180, 430, 200), (250, -100, 260), 26),
    (6.5, (-100, 900, 200), (330, 400, 120), 26),
    (9.0, (50, 1700, 170), (450, 1250, 90), 30),
    (11.5, (500, 1800, 230), (900, 1320, 130), 28),
    (13.8, (700, 1750, 260), (1100, 1350, 140), 26),
    (15.0, (800, 1650, 330), (1400, 1350, 0), 24),
    (17.0, (1000, 1550, 380), (1900, 1350, -500), 22),
]

path = PATH + '/' + NAME
if eal.does_asset_exist(path):
    eal.delete_asset(path)
seq = at.create_asset(NAME, PATH, unreal.LevelSequence, unreal.LevelSequenceFactoryNew())
seq.set_display_rate(unreal.FrameRate(FPS, 1))
seq.set_playback_start_seconds(0.0)
seq.set_playback_end_seconds(LENGTH)

cam = seq.add_spawnable_from_class(unreal.CineCameraActor)
cam.set_display_name('IntroCamera')
tr = cam.add_track(unreal.MovieScene3DTransformTrack)
sec = tr.add_section()
sec.set_range_seconds(0.0, LENGTH)
ch = sec.get_all_channels()  # loc x y z, rot x(roll) y(pitch) z(yaw), scale x y z

focal_ch = None  # focal length is driven by AFallIntro from the same key table (FocalAt)

for t, c, l, f in KEYS:
    c = [v * SCALE for v in c]
    l = [v * SCALE for v in l]
    d = [l[i] - c[i] for i in range(3)]
    yaw = math.degrees(math.atan2(d[1], d[0]))
    pitch = math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
    fr = unreal.FrameNumber(int(round(t * FPS)))
    for i, v in enumerate((c[0], c[1], c[2], 0.0, pitch, yaw)):
        ch[i].add_key(fr, v, interpolation=unreal.MovieSceneKeyInterpolation.AUTO)
    if focal_ch:
        focal_ch.add_key(fr, float(f))

cut_track = seq.add_track(unreal.MovieSceneCameraCutTrack)
cut = cut_track.add_section()
cut.set_range_seconds(0.0, LENGTH)
cut.set_camera_binding_id(seq.get_binding_id(cam))
eal.save_loaded_asset(seq, False)
print('FALL_INTRO_OK', bool(focal_ch))
