# Downloads CC0 packs from Kenney (https://kenney.nl) into ThirdParty/Kenney/<pack>/ and unzips them.
#   python Tools/fetch_kenney.py particle-pack smoke-particles impact-sounds
import http.cookiejar, io, json, os, sys, urllib.request, zipfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ThirdParty', 'Kenney')
UA = {'User-Agent': 'Mozilla/5.0 asset-fetch-script'}


OPENER = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))


def get(url, referer=None):
    h = dict(UA)
    if referer:
        h['Referer'] = referer
    with OPENER.open(urllib.request.Request(url, headers=h), timeout=300) as r:
        return r.read()


for pack in sys.argv[1:]:
    out = os.path.join(ROOT, pack)
    if os.path.isdir(out):
        print('have', pack)
        continue
    page = 'https://kenney.nl/assets/' + pack
    get(page)  # the signed link needs the page's session cookie
    link = json.loads(get('https://kenney.nl/download-link/' + ('assets/' + pack).encode().hex(), page))['url']
    data = get(link)
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for n in z.namelist():
            if '..' in n or n.startswith(('/', '\\')):
                raise SystemExit('unsafe path in zip: ' + n)
        z.extractall(out)
    print('ok', pack, len(data) // 1024, 'KB')

# Smoke atlas for M_FF_Sprite: whitePuff00-15 in a 4x4 grid, 256 px cells.
puffs = os.path.join(ROOT, 'smoke-particles', 'PNG', 'White puff')
if os.path.isdir(puffs):
    from PIL import Image
    atlas = Image.new('RGBA', (1024, 1024), (0, 0, 0, 0))
    for i in range(16):
        im = Image.open(os.path.join(puffs, f'whitePuff{i:02d}.png')).convert('RGBA')
        im.thumbnail((248, 248))
        atlas.paste(im, ((i % 4) * 256 + (256 - im.width) // 2, (i // 4) * 256 + (256 - im.height) // 2))
    dst = os.path.join(ROOT, '..', 'Processed', 'Textures')
    os.makedirs(dst, exist_ok=True)
    atlas.save(os.path.join(dst, 'T_SmokeAtlas.png'))
    print('atlas ok')
