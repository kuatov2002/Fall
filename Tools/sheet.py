# Contact sheet: python Tools/sheet.py <out.jpg> <cols> <cell_width> <png/jpg files...>
import os, sys
from PIL import Image, ImageDraw
out, cols, w = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
files = sys.argv[4:]
ims = [Image.open(f).convert('RGB') for f in files]
h = int(w * ims[0].height / ims[0].width)
rows = (len(ims) + cols - 1) // cols
s = Image.new('RGB', (cols * w, rows * h))
d = ImageDraw.Draw(s)
for i, (f, im) in enumerate(zip(files, ims)):
    x, y = (i % cols) * w, (i // cols) * h
    s.paste(im.resize((w, h)), (x, y))
    d.text((x + 4, y + 4), os.path.basename(f)[:40], fill=(255, 255, 0))
s.save(out, quality=85)
print(out, len(ims))
