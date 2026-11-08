#!/usr/bin/env python3
"""Slice a SpriteCook creature sheet (3x3 grid) into the game's 32x32 mob sprites.

    python3 tools/slice_sprites.py SHEET.png assets/sprites [--method box|nearest]

Each creature is cropped to its bounds inside its grid cell, mirrored to face
right where needed, scaled to fit its box (premultiplied box filter, then the
colours are snapped back to the sheet's own palette and the alpha made hard),
and placed bottom-centre on a 32x32 canvas, the way the game expects.
It also writes NAME_hi.png: the same creature at the sheet's full resolution,
on a square canvas laid out like the 32x32 one, which the game draws instead
when it is zoomed in (with NAME_hi_glow.png for its glowing parts). With an
anim/NAME.png strip next to the sheet it slices an idle animation too, and
items-1.png (a 4x4 sheet) becomes item_NAME.png, _hi and _glow likewise.
Needs Pillow.
"""
import argparse, os, sys
from PIL import Image

VERSION = "1.3.0"
TILE = 32
# name: (col, row, max_w, max_h, mirror, keep_top, glow)
#   keep_top: the fraction of the creature kept from the top (crops a splash away)
#   glow: (x0, y0, x1, y1, colour) boxes in the cropped sheet's pixels (before
#   mirroring) whose bright pixels glow in the dark (eyes, the lantern); colour
#   None keeps the sheet's own, else the eye is painted that colour
CREATURES = {
    "player":  (0, 0, 30, 30, False, 1.0,  [(38, 26, 48, 40, None)]),
    "rat":     (1, 0, 27, 18, False, 1.0,  [(43, 19, 48, 24, (255, 70, 50))]),
    "goblin":  (2, 0, 26, 25, True,  1.0,  [(15, 5, 26, 9, (255, 210, 80))]),
    "bat":     (0, 1, 32, 17, False, 1.0,  [(32, 15, 40, 19, (255, 70, 50))]),
    "slinger": (1, 1, 26, 26, False, 1.0,  [(25, 18, 30, 22, (255, 210, 80))]),
    "ogre":    (2, 1, 29, 30, True,  1.0,  []),
    "eel":     (0, 2, 28, 30, False, 0.74, []),
}

# the item sheet (4x4, row by row): the game's file names, and which ones glow
ITEMS = ["torch", "dart", "potion", "sword", "dagger", "spear", "axe", "mace",
         "leather", "cloak", "mail", "oil", "flash", "salt", "warmth", "embers"]
ITEM_GLOW = {"potion", "warmth", "embers"}
ITEM_FIT = 22           # an item fits a 22x22 box on its 32x32 tile, a little above the middle

def slice_items(path, a):
    sheet = Image.open(path).convert("RGBA")
    cw, ch = sheet.size[0] / 4, sheet.size[1] / 4
    for i, name in enumerate(ITEMS):
        c, r = i % 4, i // 4
        b = bbox_in(sheet, int(c * cw), int(r * ch), int((c + 1) * cw), int((r + 1) * ch))
        if not b: sys.exit(f"{name}: empty cell")
        src = sheet.crop(b)
        sc = min(ITEM_FIT / src.size[0], ITEM_FIT / src.size[1])
        size = (max(1, round(src.size[0] * sc)), max(1, round(src.size[1] * sc)))
        ox, oy = (TILE - size[0]) // 2, (TILE - 2 - size[1]) // 2
        hs = round(TILE / sc)
        hox, hoy = round(ox / sc), round(oy / sc)
        # what glows: the liquid in a bottle (not its cork), the coals in the embers
        top = src.size[1] * 2 // 5 if name in ("potion", "warmth") else 0
        boxes = [(0, top, src.size[0], src.size[1], None)] if name in ITEM_GLOW else []
        low, lg, hi, hg = make(src, 0, 0, sc, ox, oy, hs, hox, hoy, "item_" + name, boxes, a, 0, src)
        low.save(f"{a.outdir}/item_{name}.png")
        hi.save(f"{a.outdir}/item_{name}_hi.png")
        if lg.getbbox(): lg.save(f"{a.outdir}/item_{name}_glow.png")
        if hg.getbbox(): hg.save(f"{a.outdir}/item_{name}_hi_glow.png")
        print(f"item {name}: crop {b} -> {size[0]}x{size[1]}, full size {hs}x{hs}")

# colour lift for creatures whose palette is too dark against the torch-lit floor
LIFT = {"goblin": 1.25, "slinger": 1.25}
# fliers: centred in the cell (their wings beat below the still pose) and not
# cut at the still's ground line
FLIERS = {"bat"}

def bbox_in(im, x0, y0, x1, y1):
    sub = im.crop((x0, y0, x1, y1))
    b = sub.getchannel("A").point(lambda a: 255 if a > 24 else 0).getbbox()
    if not b: return None
    return (x0 + b[0], y0 + b[1], x0 + b[2], y0 + b[3])

def premul_resize(im, size, method):
    if method == "nearest":
        return im.resize(size, Image.NEAREST)
    px = im.load(); w, h = im.size
    pm = Image.new("RGBA", im.size)
    q = pm.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            q[x, y] = (r * a // 255, g * a // 255, b * a // 255, a)
    s = pm.resize(size, Image.BOX)
    out = Image.new("RGBA", size)
    sp, op = s.load(), out.load()
    for y in range(size[1]):
        for x in range(size[0]):
            r, g, b, a = sp[x, y]
            op[x, y] = (min(255, r * 255 // a), min(255, g * 255 // a), min(255, b * 255 // a), a) if a else (0, 0, 0, 0)
    return out

def snap(im, src):
    """hard alpha, and every colour snapped to the nearest one the source sprite uses"""
    pal = sorted({p[:3] for p in src.get_flattened_data() if p[3] > 200})
    px = im.load(); cache = {}
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            r, g, b, a = px[x, y]
            if a < 110: px[x, y] = (0, 0, 0, 0); continue
            k = (r, g, b)
            if k not in cache:
                cache[k] = min(pal, key=lambda c: (c[0]-r)**2*3 + (c[1]-g)**2*4 + (c[2]-b)**2*2)
            px[x, y] = cache[k] + (255,)
    return im

def bright(c, painted):
    """a lantern's flame is bright and saturated; an eye to be painted only needs to be bright"""
    r, g, b = c[:3]
    hi, lo = max(r, g, b), min(r, g, b)
    return hi > 200 if painted else hi > 150 and (hi - lo) > 0.35 * hi

def glow_points(img, boxes, sc, ox, oy, grow=0):
    """{(x, y) on the output sprite: colour} for the bright pixels of img inside
    the glow boxes (img's own coordinates; grow widens them, for moving frames)"""
    px, (w, h) = img.load(), img.size
    pts = {}
    for x0, y0, x1, y1, col in boxes:
        for y in range(max(0, y0 - grow), min(y1 + grow, h)):
            for x in range(max(0, x0 - grow), min(x1 + grow, w)):
                c = px[x, y]
                if c[3] < 200 or not bright(c, col is not None): continue
                t = (int(ox + (x + 0.5) * sc), int(oy + (y + 0.5) * sc))
                c = col or c[:3]
                if t not in pts or sum(c) > sum(pts[t]): pts[t] = c
    return pts

def mirror_boxes(boxes, w):
    return [(w - x1, y0, w - x0, y1, col) for x0, y0, x1, y1, col in boxes]

def align(frame, src, reach=10):
    """(ax, ay): where src's top-left sits on frame, by the best colour match"""
    import numpy as np
    f = np.asarray(frame, dtype=np.int32); s = np.asarray(src, dtype=np.int32)
    sh, sw = s.shape[:2]; fh, fw = f.shape[:2]
    m = s[..., 3] > 128
    best, at = None, (0, 0)
    for ay in range(-reach, fh - sh + reach + 1):
        for ax in range(-reach, fw - sw + reach + 1):
            pad = np.zeros((sh, sw, 4), np.int32)
            y0, x0 = max(0, ay), max(0, ax)
            y1, x1 = min(fh, ay + sh), min(fw, ax + sw)
            if y1 <= y0 or x1 <= x0: continue
            pad[y0 - ay:y1 - ay, x0 - ax:x1 - ax] = f[y0:y1, x0:x1]
            d = np.abs(pad[..., :3] - s[..., :3]).sum(-1)
            d[pad[..., 3] < 128] = 400
            score = d[m].sum()
            if best is None or score < best: best, at = score, (ax, ay)
    return at

def make(img, ax, ay, sc, ox, oy, hs, hox, hoy, name, boxes, a, grow, palette_src):
    """one frame: img (mirrored) with src's top-left at (ax, ay) on it -> (low, low glow, hi, hi glow)"""
    size = (max(1, round(img.size[0] * sc)), max(1, round(img.size[1] * sc)))
    spr = premul_resize(img, size, a.method)
    if a.method == "box": spr = snap(spr, palette_src)
    spr = lift(spr, LIFT.get(name, 1))
    low = Image.new("RGBA", (TILE, TILE))
    lx, ly = ox - round(ax * sc), oy - round(ay * sc)
    low.paste(spr, (lx, ly), spr)
    despeck(low)
    if not a.no_outline: outline(low)
    lg = Image.new("RGBA", (TILE, TILE))
    for (x, y), col in glow_points(img, boxes, sc, lx, ly, grow).items():
        if 0 <= x < TILE and 0 <= y < TILE:
            low.putpixel((x, y), col + (255,)); lg.putpixel((x, y), col + (255,))
    hi = Image.new("RGBA", (hs, hs))
    spr = lift(img.copy(), LIFT.get(name, 1))
    hx, hy = hox - ax, hoy - ay
    hi.paste(spr, (hx, hy), spr)
    despeck(hi, round(5 / (sc * sc)))
    if not a.no_outline: outline(hi)
    hg = Image.new("RGBA", (hs, hs))
    for (x, y), col in glow_points(img, boxes, 1, hx, hy, grow).items():
        if 0 <= x < hs and 0 <= y < hs:
            hi.putpixel((x, y), col + (255,)); hg.putpixel((x, y), col + (255,))
    return low, lg, hi, hg

def strip(images):
    w, h = images[0].size
    out = Image.new("RGBA", (w * len(images), h))
    for i, im in enumerate(images): out.paste(im, (i * w, 0))
    return out

def despeck(im, smallest=5):
    """drop stray specks (bits of splash, background crumbs) under `smallest` pixels"""
    px, (w, h) = im.load(), im.size
    seen = set()
    for y0 in range(h):
        for x0 in range(w):
            if (x0, y0) in seen or not px[x0, y0][3]: continue
            blob, todo = [], [(x0, y0)]
            seen.add((x0, y0))
            while todo:
                x, y = todo.pop(); blob.append((x, y))
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        q = (x + dx, y + dy)
                        if 0 <= q[0] < w and 0 <= q[1] < h and q not in seen and px[q][3]:
                            seen.add(q); todo.append(q)
            if len(blob) < smallest:
                for q in blob: px[q] = (0, 0, 0, 0)
    return im

def outline(im, col=(26, 18, 16)):
    """a dark 1px rim around the silhouette, so it reads against a lit floor"""
    px, (w, h) = im.load(), im.size
    rim = [(x, y) for y in range(h) for x in range(w) if px[x, y][3] == 0 and any(
        0 <= x + dx < w and 0 <= y + dy < h and px[x + dx, y + dy][3]
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
    for x, y in rim: px[x, y] = col + (255,)
    return im

def lift(im, f):
    """brighten (f > 1) every colour but the darkest outline ones"""
    if f == 1: return im
    px = im.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            r, g, b, a = px[x, y]
            if a and max(r, g, b) > 40:
                px[x, y] = (min(255, round(r * f)), min(255, round(g * f)), min(255, round(b * f)), a)
    return im

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("sheet"); ap.add_argument("outdir")
    ap.add_argument("--anim", default=None, metavar="DIR",
                    help="animation strips NAME.png (frames side by side) to slice too "
                         "(default: anim/ next to the sheet)")
    ap.add_argument("--items", default=None, metavar="SHEET",
                    help="the 4x4 item sheet (default: items-1.png next to the creature sheet)")
    ap.add_argument("--method", choices=("box", "nearest"), default="box")
    ap.add_argument("--no-outline", action="store_true", help="don't add the dark rim")
    ap.add_argument("-V", "--version", action="version", version=f"%(prog)s {VERSION}")
    a = ap.parse_args()
    anim_dir = a.anim or os.path.join(os.path.dirname(a.sheet) or ".", "anim")
    sheet = Image.open(a.sheet).convert("RGBA")
    cw, ch = sheet.size[0] / 3, sheet.size[1] / 3
    for name, (c, r, mw, mh, mirror, keep, glow) in CREATURES.items():
        b = bbox_in(sheet, int(c * cw), int(r * ch), int((c + 1) * cw), int((r + 1) * ch))
        if not b: sys.exit(f"{name}: empty cell")
        x0, y0, x1, y1 = b
        y1 = y0 + round((y1 - y0) * keep)
        src = sheet.crop((x0, y0, x1, y1))
        if mirror: src = src.transpose(Image.FLIP_LEFT_RIGHT)
        boxes = mirror_boxes(glow, src.size[0]) if mirror else glow
        sc = min(mw / src.size[0], mh / src.size[1])
        size = (max(1, round(src.size[0] * sc)), max(1, round(src.size[1] * sc)))
        ox, oy = (TILE - size[0]) // 2, TILE - size[1]
        if name in FLIERS: oy = (TILE - size[1]) // 2
        hs = round(TILE / sc)
        hox, hoy = round(ox / sc), round(oy / sc) if name in FLIERS else hs - src.size[1]
        low, lg, hi, hg = make(src, 0, 0, sc, ox, oy, hs, hox, hoy, name, boxes, a, 0, src)
        low.save(f"{a.outdir}/{name}.png")
        if lg.getbbox(): lg.save(f"{a.outdir}/{name}_glow.png")
        hi.save(f"{a.outdir}/{name}_hi.png")
        if hg.getbbox(): hg.save(f"{a.outdir}/{name}_hi_glow.png")
        msg = f"{name}: crop {b} -> {size[0]}x{size[1]}, full size {hs}x{hs}"
        # an idle animation: frames side by side, each lined up on the still
        ap_ = os.path.join(anim_dir, f"{name}.png")
        if os.path.exists(ap_):
            st = Image.open(ap_).convert("RGBA")
            fh = st.size[1]; n = st.size[0] // fh
            frames = [st.crop((i * fh, 0, (i + 1) * fh, fh)) for i in range(n)]
            u = None
            for f in frames:
                bb = f.getchannel("A").point(lambda v: 255 if v > 24 else 0).getbbox()
                if bb: u = bb if u is None else (min(u[0], bb[0]), min(u[1], bb[1]), max(u[2], bb[2]), max(u[3], bb[3]))
            frames = [f.crop(u) for f in frames]
            if mirror: frames = [f.transpose(Image.FLIP_LEFT_RIGHT) for f in frames]
            ax, ay = align(frames[0], src)
            # nothing below the still's ground line (a splash, a shadow)
            if name not in FLIERS:
                frames = [f.crop((0, 0, f.size[0], max(1, min(f.size[1], ay + src.size[1])))) for f in frames]
            shifted = [(x0_ + ax, y0_ + ay, x1_ + ax, y1_ + ay, col) for x0_, y0_, x1_, y1_, col in boxes]
            outs = [make(f, ax, ay, sc, ox, oy, hs, hox, hoy, name, shifted, a, 3, f) for f in frames]
            strip([o[0] for o in outs]).save(f"{a.outdir}/{name}_anim.png")
            strip([o[2] for o in outs]).save(f"{a.outdir}/{name}_hi_anim.png")
            if any(o[1].getbbox() for o in outs):
                strip([o[1] for o in outs]).save(f"{a.outdir}/{name}_anim_glow.png")
                strip([o[3] for o in outs]).save(f"{a.outdir}/{name}_hi_anim_glow.png")
            msg += f", {n} frames (aligned at {ax},{ay})"
        print(msg)
    items = a.items or os.path.join(os.path.dirname(a.sheet) or ".", "items-1.png")
    if os.path.exists(items): slice_items(items, a)

if __name__ == "__main__":
    main()
