#!/usr/bin/env python3
"""Slice a SpriteCook creature sheet (3x3 grid) into the game's 32x32 mob sprites.

    python3 tools/slice_sprites.py SHEET.png assets/sprites [--method box|nearest]

Each creature is cropped to its bounds inside its grid cell, mirrored to face
right where needed, scaled to fit its box (premultiplied box filter, then the
colours are snapped back to the sheet's own palette and the alpha made hard),
and placed bottom-centre on a 32x32 canvas, the way the game expects.
It also writes NAME_hi.png: the same creature at the sheet's full resolution,
on a square canvas laid out like the 32x32 one, which the game draws instead
when it is zoomed in (with NAME_hi_glow.png for its glowing parts).
Needs Pillow.
"""
import argparse, sys
from PIL import Image

VERSION = "1.1.1"
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
    "slinger": (1, 1, 26, 29, False, 1.0,  [(25, 18, 30, 22, (255, 210, 80))]),
    "ogre":    (2, 1, 32, 32, True,  1.0,  []),
    "eel":     (0, 2, 28, 30, False, 0.74, []),
}

# colour lift for creatures whose palette is too dark against the torch-lit floor
LIFT = {"goblin": 1.25, "slinger": 1.25}

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

def glow_points(src, boxes, mirror, sc, ox, oy):
    """{(x, y) on the 32x32 sprite: colour} for the bright pixels inside the glow boxes"""
    px, w = src.load(), src.size[0]
    pts = {}
    for x0, y0, x1, y1, col in boxes:
        for y in range(y0, min(y1, src.size[1])):
            for x in range(x0, min(x1, w)):
                c = px[w - 1 - x if mirror else x, y]
                if c[3] < 200 or not bright(c, col is not None): continue
                mx = w - 1 - x if mirror else x
                t = (ox + int((mx + 0.5) * sc), oy + int((y + 0.5) * sc))
                c = col or c[:3]
                if t not in pts or sum(c) > sum(pts[t]): pts[t] = c
    return pts

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
    ap.add_argument("--method", choices=("box", "nearest"), default="box")
    ap.add_argument("--no-outline", action="store_true", help="don't add the dark rim")
    ap.add_argument("-V", "--version", action="version", version=f"%(prog)s {VERSION}")
    a = ap.parse_args()
    sheet = Image.open(a.sheet).convert("RGBA")
    cw, ch = sheet.size[0] / 3, sheet.size[1] / 3
    for name, (c, r, mw, mh, mirror, keep, glow) in CREATURES.items():
        b = bbox_in(sheet, int(c * cw), int(r * ch), int((c + 1) * cw), int((r + 1) * ch))
        if not b: sys.exit(f"{name}: empty cell")
        x0, y0, x1, y1 = b
        y1 = y0 + round((y1 - y0) * keep)
        src = sheet.crop((x0, y0, x1, y1))
        if mirror: src = src.transpose(Image.FLIP_LEFT_RIGHT)
        sc = min(mw / src.size[0], mh / src.size[1])
        size = (max(1, round(src.size[0] * sc)), max(1, round(src.size[1] * sc)))
        spr = premul_resize(src, size, a.method)
        if a.method == "box": spr = snap(spr, src)
        spr = lift(spr, LIFT.get(name, 1))
        out = Image.new("RGBA", (TILE, TILE))
        ox, oy = (TILE - size[0]) // 2, TILE - size[1]
        out.paste(spr, (ox, oy), spr)
        despeck(out)
        if not a.no_outline: outline(out)
        gl = Image.new("RGBA", (TILE, TILE))
        for (x, y), col in glow_points(src, glow, mirror, sc, ox, oy).items():
            if 0 <= x < TILE and 0 <= y < TILE:
                out.putpixel((x, y), col + (255,)); gl.putpixel((x, y), col + (255,))
        out.save(f"{a.outdir}/{name}.png")
        if gl.getbbox(): gl.save(f"{a.outdir}/{name}_glow.png")
        # full resolution: the 32x32 layout scaled up by 1/sc, the sheet's own pixels
        hs = round(TILE / sc)
        hi = Image.new("RGBA", (hs, hs))
        hox, hoy = round(ox / sc), hs - src.size[1]
        spr = lift(src.copy(), LIFT.get(name, 1))
        hi.paste(spr, (hox, hoy), spr)
        despeck(hi, round(5 / (sc * sc)))
        if not a.no_outline: outline(hi)
        hg = Image.new("RGBA", (hs, hs))
        for (x, y), col in glow_points(src, glow, mirror, 1, hox, hoy).items():
            if 0 <= x < hs and 0 <= y < hs:
                hi.putpixel((x, y), col + (255,)); hg.putpixel((x, y), col + (255,))
        hi.save(f"{a.outdir}/{name}_hi.png")
        if hg.getbbox(): hg.save(f"{a.outdir}/{name}_hi_glow.png")
        print(f"{name}: crop {b} -> {size[0]}x{size[1]}, full size {hs}x{hs}")

if __name__ == "__main__":
    main()
