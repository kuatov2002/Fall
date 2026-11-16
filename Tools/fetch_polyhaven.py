# Downloads CC0 assets from Poly Haven (https://polyhaven.com, API https://api.polyhaven.com) into ThirdParty/PolyHaven/<id>/.
#   python Tools/fetch_polyhaven.py models <id> [<id> ...]   glTF with textures, 2k
#   python Tools/fetch_polyhaven.py textures <id> [...]      diffuse, normal (gl), arm (ao/rough/metal), 2k jpg/png
#   python Tools/fetch_polyhaven.py hdris <id> [...]         4k .hdr
# Skips files that already exist with the right size. Records every asset in ThirdParty/PolyHaven/manifest.json for CREDITS.md.
import json, os, sys, urllib.request

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ThirdParty', 'PolyHaven')
UA = {'User-Agent': 'Mozilla/5.0 asset-fetch-script'}


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120) as r:
        return r.read()


def save(url, path, size=None):
    if os.path.exists(path) and (size is None or os.path.getsize(path) == size):
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = get(url)
    with open(path, 'wb') as f:
        f.write(data)


def main():
    kind, ids = sys.argv[1], sys.argv[2:]
    manifest_path = os.path.join(ROOT, 'manifest.json')
    manifest = json.load(open(manifest_path)) if os.path.exists(manifest_path) else {}
    for aid in ids:
        info = json.loads(get('https://api.polyhaven.com/info/' + aid))
        files = json.loads(get('https://api.polyhaven.com/files/' + aid))
        out = os.path.join(ROOT, aid)
        if kind == 'models':
            res = '2k' if '2k' in files['gltf'] else sorted(files['gltf'])[0]
            g = files['gltf'][res]['gltf']
            save(g['url'], os.path.join(out, os.path.basename(g['url'])), g.get('size'))
            for rel, f in g.get('include', {}).items():
                save(f['url'], os.path.join(out, rel), f.get('size'))
        elif kind == 'textures':
            for m, key in (('Diffuse', 'jpg'), ('nor_gl', 'jpg'), ('arm', 'jpg'), ('Displacement', 'png')):
                if m in files and '2k' in files[m]:
                    f = files[m]['2k'].get(key) or next(iter(files[m]['2k'].values()))
                    save(f['url'], os.path.join(out, os.path.basename(f['url'])), f.get('size'))
        elif kind == 'hdris':
            f = files['hdri']['4k']['hdr']
            save(f['url'], os.path.join(out, os.path.basename(f['url'])), f.get('size'))
        manifest[aid] = {'kind': kind, 'name': info.get('name', aid), 'authors': list(info.get('authors', {}).keys()),
                         'license': 'CC0', 'url': 'https://polyhaven.com/a/' + aid}
        print('ok', aid)
    os.makedirs(ROOT, exist_ok=True)
    json.dump(manifest, open(manifest_path, 'w'), indent=1, sort_keys=True)


if __name__ == '__main__':
    main()
