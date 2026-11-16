# Downloads CC0 recordings by Joseph Sardin from BigSoundBank (https://bigsoundbank.com, license: CC0 1.0)
# into ThirdParty/BigSoundBank/<name>.wav. The list is what the slice uses; see CREDITS.md.
import os, urllib.error, urllib.request

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ThirdParty', 'BigSoundBank')
UA = {'User-Agent': 'Mozilla/5.0 asset-fetch-script'}
SOUNDS = {
    '0146': 'wind_strong_1', '0154': 'wind_strong_4', '0147': 'wind_whistle_1', '0155': 'wind_whistle_2',
    '0595': 'wind_base', '1714': 'wind_inside_1',
    '1632': 'flag_wind_1', '1633': 'flag_wind_2', '1787': 'flag_1', '1788': 'flag_2',
    '0437': 'shot_beretta_9mm', '0438': 'shot_357', '1987': 'pistol_cocking_4', '1988': 'gun_loading_1', '1989': 'gun_loading_2',
    '1355': 'casing_9mm_1', '1356': 'casing_9mm_2', '1357': 'casing_9mm_3',
    '1807': 'explosion_1', '1808': 'explosion_2', '1023': 'explosion_far_1', '1806': 'explosion_far_2',
    '1771': 'sheet_steel_1', '1772': 'sheet_steel_2', '1776': 'sheet_steel_3',
    '1796': 'whoosh_4', '1795': 'whoosh_3', '1801': 'whoosh_8', '0573': 'whoosh_2',
    '3411': 'evacuation_alarm_1', '2249': 'fire_alarm_3',
    '2478': 'propeller_plane_pass', '0263': 'helicopter_1',
    '0987': 'big_fire_1', '0616': 'creaking_metal_door',
}

os.makedirs(ROOT, exist_ok=True)
for sid, name in SOUNDS.items():
    path = os.path.join(ROOT, name + '.wav')
    if os.path.exists(path) and os.path.getsize(path) > 1000:
        continue
    for folder in ('bwf-en', 'wav'):
        try:
            with urllib.request.urlopen(urllib.request.Request('https://bigsoundbank.com/UPLOAD/%s/%s.wav' % (folder, sid), headers=UA), timeout=300) as r:
                open(path, 'wb').write(r.read())
            print('ok', sid, name)
            break
        except urllib.error.HTTPError as e:
            if folder == 'wav':
                print('MISSING', sid, name, e.code)
