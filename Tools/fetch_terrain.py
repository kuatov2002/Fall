# Real elevation for the landing region: Terrarium tiles (Mapzen terrain tiles on AWS Open Data,
# https://registry.opendata.aws/terrain-tiles/ ; sources include SRTM and EU-DEM, see CREDITS.md).
# Builds three square height grids centred on the landing valley and writes them as raw float32 files
# (metres above the valley floor, rows go north to south, columns west to east):
#   near: 6.6 km at ~13 m, mid: 84 km at ~110 m, far: 440 km at ~860 m.
#   python Tools/fetch_terrain.py
import io, json, math, os, struct, urllib.request
import numpy as np
from PIL import Image

LAT0, LON0 = 46.112, 7.075  # Rhone valley at Martigny: a flat floor ~2 km wide under 3-4 km peaks
FLOOR = 470.0               # valley floor, metres above sea level
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ThirdParty', 'Processed', 'Terrain')
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ThirdParty', 'Terrarium')
UA = {'User-Agent': 'Mozilla/5.0 asset-fetch-script'}
GRIDS = {'near': (13, 6600.0, 512), 'mid': (10, 84000.0, 768), 'far': (7, 440000.0, 512)}


def tile_xy(lat, lon, z):
    n = 2 ** z
    x = (lon + 180.0) / 360.0 * n
    y = (1.0 - np.arcsinh(np.tan(np.radians(lat))) / math.pi) / 2.0 * n
    return x, y


def tile(z, x, y):
    path = os.path.join(CACHE, f'{z}_{x}_{y}.png')
    if not os.path.exists(path):
        os.makedirs(CACHE, exist_ok=True)
        url = f'https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png'
        with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120) as r:
            open(path, 'wb').write(r.read())
    a = np.asarray(Image.open(path).convert('RGB'), dtype=np.float64)
    return a[:, :, 0] * 256.0 + a[:, :, 1] + a[:, :, 2] / 256.0 - 32768.0


def grid(z, extent, size):
    half = extent / 2.0
    xs = np.linspace(-half, half, size)                # metres east
    ys = np.linspace(half, -half, size)                # metres north, first row is north
    X, Y = np.meshgrid(xs, ys)
    lat = LAT0 + Y / 111320.0
    lon = LON0 + X / (111320.0 * math.cos(math.radians(LAT0)))
    tx, ty = tile_xy(lat, lon, z)
    px, py = tx * 256.0, ty * 256.0
    x0, y0 = int(np.floor(px.min() / 256)), int(np.floor(py.min() / 256))
    x1, y1 = int(np.floor(px.max() / 256)), int(np.floor(py.max() / 256))
    mosaic = np.zeros(((y1 - y0 + 1) * 256, (x1 - x0 + 1) * 256))
    for ty_ in range(y0, y1 + 1):
        for tx_ in range(x0, x1 + 1):
            mosaic[(ty_ - y0) * 256:(ty_ - y0 + 1) * 256, (tx_ - x0) * 256:(tx_ - x0 + 1) * 256] = tile(z, tx_, ty_)
    lx, ly = px - x0 * 256 - 0.5, py - y0 * 256 - 0.5
    ix, iy = np.clip(np.floor(lx).astype(int), 0, mosaic.shape[1] - 2), np.clip(np.floor(ly).astype(int), 0, mosaic.shape[0] - 2)
    fx, fy = np.clip(lx - ix, 0, 1), np.clip(ly - iy, 0, 1)
    h = (mosaic[iy, ix] * (1 - fx) * (1 - fy) + mosaic[iy, ix + 1] * fx * (1 - fy)
         + mosaic[iy + 1, ix] * (1 - fx) * fy + mosaic[iy + 1, ix + 1] * fx * fy)
    return np.maximum(h, 0.0) - FLOOR                  # sea and lakes clamp to sea level


os.makedirs(OUT, exist_ok=True)
info = {}
for name, (z, extent, size) in GRIDS.items():
    h = grid(z, extent, size).astype(np.float32)
    h.tofile(os.path.join(OUT, name + '.f32'))
    info[name] = {'zoom': z, 'extent_m': extent, 'size': size, 'min': float(h.min()), 'max': float(h.max())}
    print(name, info[name])
json.dump({'lat': LAT0, 'lon': LON0, 'floor_m': FLOOR, 'grids': info}, open(os.path.join(OUT, 'terrain.json'), 'w'), indent=1)
