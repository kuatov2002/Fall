"""Contact sheets and strips for self-review.

Usage:
  python Tools/contact_sheet.py sheet <dir> <out.jpg> [--glob "shot_*.png"] [--cols 6] [--width 320] [--every 1]
  python Tools/contact_sheet.py gallery <gallery_dir> <out_dir> [--every 3] [--width 200]
     one strip per entry and camera angle (front/side/game), frames in time order
"""
import argparse, glob, os, re
from PIL import Image, ImageDraw

def load_thumbs(files, width):
    out = []
    for f in files:
        im = Image.open(f).convert("RGB")
        h = int(im.height * width / im.width)
        out.append((os.path.basename(f), im.resize((width, h), Image.LANCZOS)))
    return out

def grid(thumbs, cols, label=True):
    if not thumbs:
        return None
    w, h = thumbs[0][1].size
    lab = 14 if label else 0
    rows = (len(thumbs) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * w, rows * (h + lab)), (20, 20, 20))
    d = ImageDraw.Draw(sheet)
    for i, (name, im) in enumerate(thumbs):
        x, y = (i % cols) * w, (i // cols) * (h + lab)
        sheet.paste(im, (x, y + lab))
        if label:
            d.text((x + 3, y + 1), os.path.splitext(name)[0][-28:], fill=(230, 230, 230))
    return sheet

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode")
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("--glob", default="*.png")
    ap.add_argument("--cols", type=int, default=6)
    ap.add_argument("--width", type=int, default=320)
    ap.add_argument("--every", type=int, default=1)
    a = ap.parse_args()
    if a.mode == "sheet":
        files = sorted(glob.glob(os.path.join(a.src, a.glob)))[:: a.every]
        s = grid(load_thumbs(files, a.width), a.cols)
        if s:
            s.save(a.out, quality=85)
            print(a.out, len(files))
    elif a.mode == "gallery":
        os.makedirs(a.out, exist_ok=True)
        for entry in sorted(os.listdir(a.src)):
            ed = os.path.join(a.src, entry)
            if not os.path.isdir(ed):
                continue
            for angle in ("front", "side", "game"):
                files = sorted(glob.glob(os.path.join(ed, angle + "_*.png")))[:: a.every]
                s = grid(load_thumbs(files, a.width), a.cols)
                if s:
                    p = os.path.join(a.out, f"{entry}_{angle}.jpg")
                    s.save(p, quality=85)
                    print(p, len(files))

if __name__ == "__main__":
    main()
