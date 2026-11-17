# Exposure metrics of display-referred frames (after tonemapping): luminance histogram, share of clipped pixels
# (luminance above 0.98), mean luminance, deep shadows (below 0.03), 1st/99th percentile.
#   python Tools/exposure.py <out.json> <group>=<glob> [<group>=<glob> ...]
import glob, json, os, sys
import numpy as np
from PIL import Image


def metrics(path):
    a = np.asarray(Image.open(path).convert('RGB'), dtype=np.float32) / 255.0
    h, w = a.shape[:2]
    if w > 960:
        a = np.asarray(Image.open(path).convert('RGB').resize((960, int(960 * h / w))), dtype=np.float32) / 255.0
    y = a[..., 0] * 0.2126 + a[..., 1] * 0.7152 + a[..., 2] * 0.0722
    hist, _ = np.histogram(y, bins=16, range=(0, 1))
    return {'clipped_pct': float((y > 0.98).mean() * 100), 'mean': float(y.mean()), 'shadow_pct': float((y < 0.03).mean() * 100),
            'p1': float(np.percentile(y, 1)), 'p99': float(np.percentile(y, 99)), 'hist16': (hist / hist.sum()).round(4).tolist()}


out = {}
for arg in sys.argv[2:]:
    group, pattern = arg.split('=', 1)
    files = sorted(glob.glob(pattern))
    per = {os.path.basename(f): metrics(f) for f in files}
    vals = list(per.values())
    out[group] = {'frames': per, 'summary': {k: {'min': min(v[k] for v in vals), 'median': float(np.median([v[k] for v in vals])), 'max': max(v[k] for v in vals)}
                                             for k in ('clipped_pct', 'mean', 'shadow_pct', 'p1', 'p99')} if vals else {}}
    print(group, len(files), json.dumps(out[group]['summary']))
json.dump(out, open(sys.argv[1], 'w'), indent=1)
