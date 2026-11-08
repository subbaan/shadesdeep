#include "tiles.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t A(uint32_t rgb) { return 0xff000000u | rgb; }

static float vnoise(float x, float y, uint32_t seed)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = (hash2(xi, yi, seed) & 1023) / 1023.0f, b = (hash2(xi + 1, yi, seed) & 1023) / 1023.0f;
    float c = (hash2(xi, yi + 1, seed) & 1023) / 1023.0f, d = (hash2(xi + 1, yi + 1, seed) & 1023) / 1023.0f;
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

static float fbm(float x, float y, uint32_t seed)
{
    return vnoise(x, y, seed) * 0.55f + vnoise(x * 2.1f, y * 2.1f, seed + 1) * 0.3f +
           vnoise(x * 4.3f, y * 4.3f, seed + 2) * 0.15f;
}

static uint32_t jitter(uint32_t c, int v)
{
    return RGB(clampi(CR(c) + v, 0, 255), clampi(CG(c) + v, 0, 255), clampi(CB(c) + v * 9 / 10, 0, 255));
}

static inline void put(Image *im, int x, int y, uint32_t argb)
{
    if ((unsigned)x < (unsigned)im->w && (unsigned)y < (unsigned)im->h) im->px[y * im->w + x] = argb;
}

static inline uint32_t get(const Image *im, int x, int y) { return im->px[y * im->w + x]; }

/* ------------------------------------------------------------ flagstones */

typedef struct { int x, y, w, h; } Rect;

static void stone_block(Image *im, Rect r, uint32_t base, uint32_t seed, float rough)
{
    int tone = (int)(hash2(r.x, r.y, seed) % 23) - 11;
    uint32_t c = jitter(base, tone);
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++) {
            float n = fbm(x * 0.22f, y * 0.22f, seed + (uint32_t)r.x * 7 + (uint32_t)r.y);
            int v = (int)((n - 0.5f) * 34 * rough);
            int sp = (hash2(x, y, seed + 99) % 100) < 6 ? -10 : 0;
            uint32_t px = jitter(c, v + sp);
            bool top = y == r.y, left = x == r.x;
            bool bot = y == r.y + r.h - 1, right = x == r.x + r.w - 1;
            if (top || left) px = jitter(px, 16);
            if (bot || right) px = jitter(px, -22);
            put(im, x, y, A(px));
        }
}

static void mortar(Image *im, uint32_t m)
{
    for (int i = 0; i < im->w * im->h; i++) im->px[i] = A(m);
}

static void crack(Image *im, uint32_t seed)
{
    Rng r = { seed };
    float x = rng_range(&r, 6, 25), y = rng_range(&r, 6, 25), a = rng_f(&r) * 6.28f;
    int len = rng_range(&r, 6, 14);
    for (int i = 0; i < len; i++) {
        int xi = (int)x, yi = (int)y;
        if (xi > 0 && yi > 0 && xi < im->w - 1 && yi < im->h - 1) {
            put(im, xi, yi, A(jitter(get(im, xi, yi) & 0xffffff, -38)));
            put(im, xi + 1, yi + 1, A(jitter(get(im, xi + 1, yi + 1) & 0xffffff, 10)));
        }
        a += (rng_f(&r) - 0.5f) * 1.2f;
        x += cosf(a);
        y += sinf(a);
    }
}

static void build_floor(Image *im, int v)
{
    image_alloc(im, TILE, TILE);
    const uint32_t base = 0x857a68, mort = 0x3a332c;
    mortar(im, mort);
    uint32_t seed = 1000 + (uint32_t)v * 17;
    static const Rect L[4][4] = {
        { { 0, 0, 15, 15 }, { 16, 0, 16, 15 }, { 0, 16, 15, 16 }, { 16, 16, 16, 16 } },
        { { 0, 0, 31, 15 }, { 0, 16, 11, 16 }, { 12, 16, 20, 16 }, { 0, 0, 0, 0 } },
        { { 0, 0, 13, 31 }, { 14, 0, 18, 13 }, { 14, 14, 18, 18 }, { 0, 0, 0, 0 } },
        { { 0, 0, 20, 11 }, { 21, 0, 11, 20 }, { 0, 12, 20, 20 }, { 21, 21, 11, 11 } },
    };
    const Rect *lay = L[v % 4];
    for (int i = 0; i < 4; i++)
        if (lay[i].w > 0) {
            Rect r = lay[i];
            /* mortar gaps on the right and bottom */
            if (r.x + r.w >= TILE) r.w = TILE - r.x;
            if (r.y + r.h >= TILE) r.h = TILE - r.y;
            stone_block(im, r, base, seed + (uint32_t)i * 31, 1.0f);
        }
    if (v >= 4) crack(im, seed + 5);
}

/* ------------------------------------------------------------- walls */

static void build_wall_face(Image *im, int v)
{
    image_alloc(im, TILE, WALL_R);
    const uint32_t base = 0x7a6d5d, mort = 0x2c2620;
    mortar(im, mort);
    uint32_t seed = 2000 + (uint32_t)v * 13;
    int rowh = WALL_R / 2;
    for (int row = 0; row < 2; row++) {
        int off = ((row + v) & 1) ? 8 : 0;
        for (int bx = -16 + off; bx < TILE; bx += 16) {
            Rect r = { bx < 0 ? 0 : bx, row * rowh, (bx < 0 ? bx + 15 : 15), rowh - 1 };
            if (r.x + r.w > TILE) r.w = TILE - r.x;
            if (r.w > 0) stone_block(im, r, base, seed + (uint32_t)(bx + 40) * 3 + (uint32_t)row, 1.2f);
        }
    }
    /* soot near the bottom, darker toward the floor */
    for (int y = 0; y < WALL_R; y++)
        for (int x = 0; x < TILE; x++) {
            float k = y >= WALL_R - 3 ? -20.0f : 0;
            put(im, x, y, A(jitter(get(im, x, y) & 0xffffff, (int)k)));
        }
}

static void build_wall_top(Image *im, int v)
{
    image_alloc(im, TILE, TILE);
    const uint32_t base = 0x5f574d, mort = 0x24201b;
    mortar(im, mort);
    uint32_t seed = 3000 + (uint32_t)v * 11;
    static const Rect L[2][4] = {
        { { 0, 0, 15, 15 }, { 16, 0, 16, 15 }, { 0, 16, 15, 16 }, { 16, 16, 16, 16 } },
        { { 0, 0, 20, 15 }, { 21, 0, 11, 15 }, { 0, 16, 11, 16 }, { 12, 16, 20, 16 } },
    };
    for (int i = 0; i < 4; i++) {
        Rect r = L[v & 1][i];
        if (r.x + r.w >= TILE) r.w = TILE - r.x;
        if (r.y + r.h >= TILE) r.h = TILE - r.y;
        stone_block(im, r, base, seed + (uint32_t)i * 7, 1.3f);
    }
}

static void build_cliff(Image *im)
{
    image_alloc(im, TILE, TILE);
    for (int y = 0; y < TILE; y++)
        for (int x = 0; x < TILE; x++) {
            float n = fbm(x * 0.15f, y * 0.45f, 4242);
            float strata = 0.5f + 0.5f * sinf(y * 0.9f + n * 4);
            int v = (int)(n * 50 + strata * 14);
            uint32_t c = RGB(48 + v, 42 + v, 38 + v * 9 / 10);
            float fade = 1.0f - y / 30.0f;
            if (fade < 0) fade = 0;
            int a = (int)(255 * fade);
            if (y < 2) c = jitter(c, 26);
            put(im, x, y, (uint32_t)a << 24 | c);
        }
}

/* ------------------------------------------------------------- features */

static void build_stairs(Image *im, bool down)
{
    build_floor(im, 1);
    int x0 = 5, x1 = 27, y0 = 4, y1 = 29;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            int step = (y - y0) / 5;                 /* 5 steps */
            int within = (y - y0) % 5;
            int lvl = down ? step : 4 - step;        /* 0 = deepest */
            int b = down ? 18 + lvl * 26 : 150 - lvl * 22;
            if (within == 0) b += down ? 18 : 28;    /* lit nosing */
            if (within == 4) b -= 14;
            float n = fbm(x * 0.3f, y * 0.3f, down ? 77 : 78);
            b += (int)((n - 0.5f) * 16);
            b = clampi(b, 0, 255);
            put(im, x, y, A(RGB(b, b * 92 / 100, b * 80 / 100)));
        }
    /* side walls */
    for (int y = y0 - 1; y < y1 + 1; y++) {
        put(im, x0 - 1, y, A(0x2a241e));
        put(im, x1, y, A(0x2a241e));
        put(im, x0 - 2, y, A(0x9a8c76));
        put(im, x1 + 1, y, A(0x6a5e50));
    }
    for (int x = x0 - 2; x < x1 + 2; x++) put(im, x, y0 - 1, A(down ? 0xa89a82 : 0x2a241e));
}

static void build_door(Image *im, int raise)
{
    image_alloc(im, TILE, TILE + raise);
    int H = TILE + raise;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < TILE; x++) {
            uint32_t c;
            if (x < 3 || x >= TILE - 3) {
                c = jitter(0x7a6d5d, (int)((fbm(x * 0.4f, y * 0.4f, 5) - 0.5f) * 30));   /* stone posts */
            } else if (y < 4) {
                c = jitter(0x6a5e50, (int)((fbm(x * 0.4f, y * 0.4f, 6) - 0.5f) * 30));   /* lintel */
            } else {
                int plank = (x - 3) / 6;
                float g = fbm(x * 0.2f + plank * 3, y * 0.05f, 7);
                c = jitter(0x6e4626, (int)((g - 0.5f) * 40) + (plank & 1) * 6);
                if ((x - 3) % 6 == 0) c = 0x2e1c10;
                if (y == 12 || y == H - 12) c = 0x3a3836;          /* iron bands */
                if (y == 13 || y == H - 11) c = 0x6a6866;
            }
            put(im, x, y, A(c));
        }
    put(im, 22, H / 2, A(0xb0a070));
    put(im, 22, H / 2 + 1, A(0x504830));
}

/* A door in a north-south wall, seen edge on: a thick oak slab standing across the
 * doorway, its top running up the tile and its end facing us, the floor of the
 * doorway showing either side. Drawn raised like a wall (top at y 0..TILE-1,
 * end face below). */
static void build_door_side(Image *im)
{
    const int H = TILE + WALL_R, x0 = 10, x1 = 22;      /* slab spans x0..x1-1 */
    image_alloc(im, TILE, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < TILE; x++) {
            bool face = y >= TILE;
            /* contact shadow on the doorway floor beside the slab */
            if (x < x0 || x >= x1) {
                int d = x < x0 ? x0 - x : x - x1 + 1;
                if (y >= WALL_R && d <= 4) put(im, x, y, (uint32_t)(150 - d * 30) << 24 | 0x000000);
                continue;
            }
            uint32_t c;
            int e = x - x0;                                     /* 0..11 across the slab */
            if (!face) {
                /* the top edge of the door: planks running north-south */
                float g = fbm(x * 0.5f, y * 0.12f, 17);
                c = jitter(0x8a5a30, (int)((g - 0.5f) * 44));
                if (e == 6) c = 0x3a2414;                        /* seam between planks */
                if (e == 0 || e == 11) c = jitter(c, -46);       /* rounded edges */
                else if (e == 1) c = jitter(c, 28);              /* catch the light */
                if (y == 6 || y == 7 || y == TILE - 8 || y == TILE - 7)   /* iron straps over the top */
                    c = y & 1 ? 0x4a4846 : 0x7a7874;
                if (y == 0) c = jitter(c, 30);
            } else {
                /* the end of the slab, facing us */
                int fy = y - TILE;
                float g = fbm(x * 0.3f, fy * 0.3f, 18);
                c = jitter(0x5a3a20, (int)((g - 0.5f) * 36));
                if (fy == 0) c = 0xa87444;                       /* lit arris */
                if (e == 0 || e == 11) c = jitter(c, -40);
                if (fy == 5 || fy == WALL_R - 6) c = 0x3a3836;   /* strap ends */
                if (fy >= WALL_R - 2) c = jitter(c, -30);
            }
            put(im, x, y, A(c));
        }
}

static void build_door_open(Image *im, bool side)
{
    build_floor(im, 2);
    for (int y = 0; y < TILE; y++)
        for (int x = 0; x < TILE; x++) {
            uint32_t c = get(im, x, y) & 0xffffff;
            int across = side ? y : x, along = side ? x : y;
            if (across < 3 || across >= TILE - 3) c = jitter(0x7a6d5d, (int)((fbm(x * 0.4f, y * 0.4f, 5) - 0.5f) * 30));
            else if (along > 12 && along < 16) c = jitter(c, -30);              /* threshold */
            put(im, x, y, A(c));
        }
}

/* A torch in an iron sconce: a riveted backplate on the wall, a cup on a short
 * arm, and the torch standing in it. 16 x 22; the torch head's top is at y 4. */
static void build_sconce(Image *im)
{
    const int W = 16, H = 22;
    image_alloc(im, W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t c = 0;
            /* backplate: a small iron shield, rounded at the foot */
            float dx = x - 7.5f;
            bool plate = y >= 9 && y <= 20 && fabsf(dx) <= 5.5f && !(y >= 17 && dx * dx + (y - 14.5f) * (y - 14.5f) * 1.6f > 40);
            if (plate) {
                bool edge = fabsf(dx) > 4.5f || y == 9 || (y >= 16 && dx * dx + (y - 14.5f) * (y - 14.5f) * 1.6f > 26);
                c = edge ? (dx < 0 || y == 9 ? 0x9a948c : 0x2a2624) : jitter(0x55504a, (int)((fbm(x * 0.5f, y * 0.5f, 31) - 0.5f) * 24));
                if ((x == 4 || x == 11) && (y == 11 || y == 17)) c = 0xc8c0b0;          /* rivets */
                c = A(c);
            }
            /* arm from the plate to the cup */
            if (y >= 14 && y <= 16 && x >= 7 && x <= 8) c = A(x == 7 ? 0x7a7672 : 0x3a3634);
            /* the torch handle, lit from the left */
            if (y >= 7 && y <= 14 && x >= 6 && x <= 9) c = A(x == 6 ? 0xa06a3a : x == 9 ? 0x4a3018 : 0x7a5028);
            /* the cup */
            if (y >= 11 && y <= 13 && x >= 5 && x <= 10) c = A(y == 11 ? 0x9a9690 : x == 5 ? 0x7a7672 : 0x3e3a38);
            /* the pitch-wrapped head */
            if (y >= 3 && y <= 7 && x >= 5 && x <= 10 && !(y == 3 && (x == 5 || x == 10))) {
                uint32_t h = (y + x) % 3 == 0 ? 0x4a3422 : 0x241a12;
                if (x == 5) h = 0x5a4430;
                c = A(h);
            }
            im->px[y * W + x] = c;
        }
}

/* An iron forge brazier: a wide bowl of coals on three splayed legs. */
static void build_brazier(Image *im, Image *glow)
{
    const int W = 28, H = 26;
    image_alloc(im, W, H);
    image_alloc(glow, W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t c = 0, gc = 0;
            float dx = x - 13.5f;
            /* legs: two splayed outer ones and a short one in front */
            float lx = 13.5f + (y - 14) * 0.55f, rx = 13.5f - (y - 14) * 0.55f;
            if (y >= 14 && y < H - 1 && (fabsf(dx - (lx - 13.5f + (y - 14) * 0.25f)) < 1.1f ||
                                         fabsf(dx + (lx - 13.5f + (y - 14) * 0.25f)) < 1.1f))
                c = (x < 14) ? 0x5a5450 : 0x2e2a28;
            (void)rx;
            if (y >= 15 && y < 22 && fabsf(dx) < 1.2f) c = 0x4a4440;
            if (y == H - 1 && (fabsf(dx) > 10 && fabsf(dx) < 13)) c = 0x1e1a18;       /* feet */
            /* the bowl: an ellipse, dark iron, rim lit from the left */
            float ex = dx / 12.5f, ey = (y - 9.5f) / 5.5f;
            float e = ex * ex + ey * ey;
            if (e <= 1.0f && y >= 7) {
                int sh = (int)(-dx * 2.2f) - (y - 9) * 3;
                c = jitter(0x4a4440, sh + (int)((fbm(x * 0.6f, y * 0.6f, 41) - 0.5f) * 20));
                if (y >= 14) c = jitter(c, -18);
                if (fabsf(dx) > 11.0f) c = jitter(c, -20);
            }
            /* the rim and the coals heaped inside it */
            if (y >= 4 && y <= 8) {
                float cx = dx / 12.0f, cy = (y - 7.0f) / 2.6f;
                float ce = cx * cx + cy * cy;
                if (ce <= 1.0f) {
                    float n = fbm(x * 0.7f, y * 1.1f, 43);
                    uint32_t coal = n > 0.62f ? 0xffd070 : n > 0.48f ? 0xff7a28 : n > 0.36f ? 0xb8381c : 0x3a1a12;
                    c = coal;
                    if (n > 0.36f) gc = coal;
                }
                if (ce > 0.78f && ce <= 1.12f && y >= 6) c = dx < 0 ? 0x9a948c : 0x5a5450;     /* rim */
            }
            if (c) put(im, x, y, A(c));
            if (gc) put(glow, x, y, A(gc));
        }
}

static void build_crystal(Image *im)
{
    image_alloc(im, 24, 26);
    /* a few tall shards leaning outward */
    static const float SH[4][4] = {     /* base x, height, half width, lean */
        { 12, 24, 3.6f, 0 }, { 7, 15, 2.8f, -3 }, { 17, 17, 3.0f, 3.5f }, { 4, 9, 2.2f, -2.5f },
    };
    for (int s = 0; s < 4; s++) {
        float bx = SH[s][0], h = SH[s][1], hw = SH[s][2], lean = SH[s][3];
        for (int y = 0; y < (int)h; y++) {
            float t = y / h;                         /* 0 base .. 1 tip */
            float cx = bx + lean * t;
            float w = hw * (t < 0.75f ? 1.0f : (1 - t) / 0.25f);
            int py = 25 - y;
            for (int x = (int)(cx - w); x <= (int)(cx + w); x++) {
                if (x < 0 || x >= 24) continue;
                float u = (x - cx) / (w + 0.01f);
                int b = (int)(150 + 90 * (1 - fabsf(u)) * (0.6f + 0.4f * t));
                if (u < -0.2f) b -= 40;
                uint32_t c = RGB(b * 45 / 100, b * 80 / 100, b);
                if (fabsf(u) > 0.8f) c = 0x1a3050;
                put(im, x, py, A(c));
            }
        }
    }
}

static void build_rubble(Image *im, int v)
{
    image_alloc(im, TILE, TILE);
    Rng r = { 5000u + (uint32_t)v };
    int n = rng_range(&r, 4, 7);
    for (int i = 0; i < n; i++) {
        float cx = rng_range(&r, 5, 26), cy = rng_range(&r, 6, 27), rad = 1.5f + rng_f(&r) * 2.8f;
        uint32_t base = jitter(0x7a6e5e, rng_range(&r, -14, 14));
        for (int y = (int)(cy - rad - 2); y <= (int)(cy + rad + 2); y++)
            for (int x = (int)(cx - rad - 2); x <= (int)(cx + rad + 2); x++) {
                float dx = x - cx, dy = (y - cy) * 1.3f, d = sqrtf(dx * dx + dy * dy);
                if (d <= rad) {
                    int sh = (int)((-dx - dy) / rad * 18);
                    put(im, x, y, A(jitter(base, sh)));
                } else if (d <= rad + 1.5f && dy > 0 && (CA(get(im, clampi(x, 0, 31), clampi(y, 0, 31))) == 0)) {
                    put(im, x, y, 0x90000000u);         /* shadow */
                }
            }
    }
}

/* ------------------------------------------------------------- sprites */

typedef struct { char k; uint32_t c; bool glow; } Pal;

static void build_sprite(Image *im, Image *glow, const char *const *rows, const Pal *pal, int npal)
{
    image_alloc(im, TILE, TILE);
    image_alloc(glow, TILE, TILE);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            char ch = rows[y][x];
            for (int p = 0; p < npal; p++) {
                if (pal[p].k != ch) continue;
                for (int j = 0; j < 2; j++)
                    for (int i = 0; i < 2; i++) {
                        /* a little shading inside each fat pixel keeps it from looking flat */
                        uint32_t c = pal[p].c;
                        if (ch != 'k' && j == 0 && i == 0) c = jitter(c, 8);
                        put(im, x * 2 + i, y * 2 + j, A(c));
                        if (pal[p].glow) put(glow, x * 2 + i, y * 2 + j, A(c));
                    }
            }
        }
}

static const char *const HERO[16] = {
    "................",
    "......kkkk......",
    ".....khhhhk.....",
    ".....khssshk....",
    ".....kssesk.....",
    "......kssk......",
    ".....kcttckk....",
    "....kcCttCckm...",
    "....kcCttCc.km..",
    "...ykcCttCck.km.",
    "..kykcCbbCck..k.",
    "..kykcCttCck....",
    "...k.kCttCk.....",
    ".....ktkktk.....",
    ".....kbk.kbk....",
    "....kkk..kkk....",
};
static const Pal HERO_PAL[] = {
    { 'k', 0x16110e, 0 }, { 'h', 0x6a4426, 0 }, { 's', 0xe0b48c, 0 }, { 'e', 0x2a1a10, 0 },
    { 'c', 0x3e6680, 0 }, { 'C', 0x2a465a, 0 }, { 't', 0x9a7448, 0 }, { 'b', 0x5a3e24, 0 },
    { 'm', 0xd0d4dc, 0 }, { 'y', 0xffd070, 1 },
};

static const char *const RAT[16] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "..........kk....",
    ".....kkkkkrrk...",
    "...kkrrrrrrrek..",
    "..krrRRrrrrrrnk.",
    "tkrrRRRrrrrrkk..",
    "t.krrrrrrrrk....",
    "...kpk..kpk.....",
    "................",
};
static const Pal RAT_PAL[] = {
    { 'k', 0x16110e, 0 }, { 'r', 0x7a685c, 0 }, { 'R', 0x5e4e44, 0 }, { 'e', 0xff4030, 1 },
    { 'n', 0xd89a9a, 0 }, { 't', 0xb08878, 0 }, { 'p', 0xc89a90, 0 },
};

static const char *const GOBLIN[16] = {
    "................",
    "..kk........kk..",
    "..kgk.kkkk.kgk..",
    "...kgkggggkgk...",
    "....kggggggk....",
    "....kgegeggk....",
    "....kggggggk....",
    ".....kgnngk.....",
    "....kkLLLLkk..m.",
    "...kgkLLLLkgkm..",
    "...kgkLDDLk.km..",
    "....kkLLLLk.k...",
    ".....kLkkLk.....",
    ".....kgk.kgk....",
    ".....kbk.kbk....",
    "....kkk...kkk...",
};
static const Pal GOBLIN_PAL[] = {
    { 'k', 0x121410, 0 }, { 'g', 0x6a9a48, 0 }, { 'e', 0xfff060, 1 }, { 'n', 0x4a6a30, 0 },
    { 'L', 0x6a4a2e, 0 }, { 'D', 0x4a3220, 0 }, { 'b', 0x3a2a1c, 0 }, { 'm', 0xa8a8b0, 0 },
};

static const char *const BAT[16] = {
    "................",
    "................",
    "................",
    "..k..........k..",
    "..kk..k..k..kk..",
    "..kwk.kkkk.kwk..",
    ".kwwwkbbbbkwwwk.",
    ".kwWwkebbekwWwk.",
    "kwwWwwkbbkwwWwwk",
    "kwWkWwkbbkwWkWwk",
    "kWk.kWkbbkWk.kWk",
    "kk...kkbbkk...kk",
    "......kkkk......",
    "................",
    "................",
    "................",
};
static const Pal BAT_PAL[] = {
    { 'k', 0x120e10, 0 }, { 'w', 0x4e3e44, 0 }, { 'W', 0x30262a, 0 }, { 'b', 0x6a5040, 0 }, { 'e', 0xff5040, 1 },
};

static const char *const SLINGER[16] = {
    "................",
    "..kk........kk..",
    "..kgk.kkkk.kgk..",
    "...kgkrrrrkgk...",
    "....kggggggk.ks.",
    "....kgegeggk.kc.",
    "....kggggggk..c.",
    ".....kgnngk..c..",
    "....kkLLLLkk.c..",
    "...kgkLLLLkgkc..",
    "...kgkLDDLk.kk..",
    "....kkLLLLk.....",
    ".....kLkkLk.....",
    ".....kgk.kgk....",
    ".....kbk.kbk....",
    "....kkk...kkk...",
};
static const Pal SLINGER_PAL[] = {
    { 'k', 0x121410, 0 }, { 'g', 0x7a9a50, 0 }, { 'e', 0xfff060, 1 }, { 'n', 0x4e6a34, 0 },
    { 'L', 0x5a5e3a, 0 }, { 'D', 0x3e4228, 0 }, { 'b', 0x3a2a1c, 0 }, { 'r', 0xa83028, 0 },
    { 'c', 0x9a8058, 0 }, { 's', 0xa8a49c, 0 },
};

static const char *const OGRE[16] = {
    "....kkkkkkk.....",
    "...kOOOOOOOk....",
    "..kOOeOOOeOOk...",
    "..kOOOOOOOOOk...",
    "..kOwkkkkkwOk...",
    "...kOOOOOOOk....",
    ".kkkOOOOOOOkkk..",
    "kOOOkBBBBBkOOOk.",
    "kOOkBBBBBBBkOOk.",
    "kOkkBBBBBBBkkOk.",
    "kOk.kBBBBBk.kOk.",
    "kkk.kbbbbbk.kkk.",
    "....kBBkBBk.....",
    "....kOOkOOk.....",
    "...kOOk.kOOk....",
    "...kkkk.kkkk....",
};
static const Pal OGRE_PAL[] = {
    { 'k', 0x14110c, 0 }, { 'O', 0x8a7c5a, 0 }, { 'e', 0xffb040, 1 }, { 'w', 0xe8e0c8, 0 },
    { 'B', 0x5e3e26, 0 }, { 'b', 0x3a2616, 0 },
};

static const char *const EEL[16] = {
    "................",
    "................",
    "................",
    "................",
    "......kkkk......",
    ".....kEEEEk.....",
    "....kEeEEEEk....",
    "....kEEEEEEEk...",
    "....kEEkkwwwk...",
    "....kEEk.ww.....",
    "....kEEkkk......",
    ".....kEPEk......",
    "......kEPk......",
    ".....kEPEk......",
    ".....kEPk.......",
    ".....kEPk.......",
};
static const Pal EEL_PAL[] = {
    { 'k', 0x0c1412, 0 }, { 'E', 0x3e5e54, 0 }, { 'P', 0x7a8a6a, 0 }, { 'e', 0xd8ff70, 1 }, { 'w', 0xe8e8d8, 0 },
};

static const char *const I_TORCH[16] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "..........kkk...",
    ".........kccck..",
    "........kcCcck..",
    ".......kwkcck...",
    "......kwWkkk....",
    ".....kwWk.......",
    "....kwWk........",
    "...kwWk.........",
    "...kWk..........",
    "....k...........",
};
static const Pal I_TORCH_PAL[] = {
    { 'k', 0x16110e, 0 }, { 'w', 0x6e4828, 0 }, { 'W', 0x9a6a3c, 0 }, { 'c', 0x3a2a1e, 0 }, { 'C', 0x5a4030, 0 },
};

static const char *const I_DART[16] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "..rr............",
    "...rrsssssssmm..",
    "..rr........k...",
    "................",
    "...rr...........",
    "....rrsssssssmm.",
    "...rr........k..",
    "................",
    "................",
};
static const Pal I_DART_PAL[] = {
    { 'k', 0x2a2a30, 0 }, { 'r', 0xb83a30, 0 }, { 's', 0x8a7a6a, 0 }, { 'm', 0xd8dce4, 0 },
};

static const char *const I_POTION[16] = {
    "................",
    "................",
    "................",
    "................",
    "......kkk.......",
    "......kck.......",
    "......kgk.......",
    ".....kgggk......",
    "....kgrrrgk.....",
    "...kgrRrrrgk....",
    "...kgRrrrrgk....",
    "...kgrrrrrgk....",
    "...kgrrrrdgk....",
    "....kgrddgk.....",
    ".....kkkkk......",
    "................",
};
static const Pal I_POTION_PAL[] = {
    { 'k', 0x1a1416, 0 }, { 'c', 0x8a6040, 0 }, { 'g', 0x9ab0b8, 0 }, { 'r', 0xc02838, 1 },
    { 'R', 0xff7a80, 1 }, { 'd', 0x801820, 1 },
};

static const char *const I_SWORD[16] = {
    "................",
    "..............k.",
    ".............kmk",
    "............kmMk",
    "...........kmMk.",
    "..........kmMk..",
    ".........kmMk...",
    "........kmMk....",
    "...kk..kmMk.....",
    "...kgkkmMk......",
    "....kgmMk.......",
    ".....kgk........",
    "....kwkgk.......",
    "...kwk.kgk......",
    "..kpk...kk......",
    "...k............",
};
static const Pal I_SWORD_PAL[] = {
    { 'k', 0x16110e, 0 }, { 'm', 0xd8dce4, 0 }, { 'M', 0x8a909a, 0 }, { 'g', 0xb08a3a, 0 }, { 'w', 0x5a3a20, 0 }, { 'p', 0xc8a050, 0 },
};

static const char *const I_DAGGER[16] = {
    "................",
    "................",
    "................",
    "................",
    "...........kk...",
    "..........kmk...",
    ".........kmMk...",
    "........kmMk....",
    ".......kmMk.....",
    "....kk.mMk......",
    "....kgkMk.......",
    ".....kgk........",
    "....kwkgk.......",
    "...kwk.kk.......",
    "...kk...........",
    "................",
};
static const Pal I_DAGGER_PAL[] = {
    { 'k', 0x16110e, 0 }, { 'm', 0xd8dce4, 0 }, { 'M', 0x8a909a, 0 }, { 'g', 0x8a8a90, 0 }, { 'w', 0x3a2a1c, 0 },
};

static const char *const I_SPEAR[16] = {
    "............kk..",
    "...........kmmk.",
    "...........mMmk.",
    "..........kmMk..",
    ".........kkkk...",
    "........kwk.....",
    ".......kwk......",
    "......kwk.......",
    ".....kwk........",
    "....kwk.........",
    "...kwk..........",
    "..kwk...........",
    ".kwk............",
    ".kk.............",
    "................",
    "................",
};
static const Pal I_SPEAR_PAL[] = {
    { 'k', 0x16110e, 0 }, { 'm', 0xd8dce4, 0 }, { 'M', 0x8a909a, 0 }, { 'w', 0x8a6034, 0 },
};

static const char *const I_AXE[16] = {
    "................",
    "........kkk.....",
    ".......kmmMk....",
    "......kmmMMMk...",
    "......kmMkkMMk..",
    ".......kkwkkMk..",
    "........kwk.kk..",
    ".......kwk......",
    "......kwk.......",
    ".....kwk........",
    "....kwk.........",
    "...kwk..........",
    "..kwk...........",
    "..kk............",
    "................",
    "................",
};
static const Pal I_AXE_PAL[] = {
    { 'k', 0x16110e, 0 }, { 'm', 0xd0d4dc, 0 }, { 'M', 0x7a808a, 0 }, { 'w', 0x7a5430, 0 },
};

static const char *const I_MACE[16] = {
    "................",
    "..........k.k...",
    ".........kMkMk..",
    "........kMmmMMk.",
    "........kmMMMMk.",
    ".......kkMMMMk..",
    "......kwkkMkk...",
    ".....kwk..k.....",
    "....kwk.........",
    "...kwk..........",
    "..kwk...........",
    ".kgk............",
    ".kk.............",
    "................",
    "................",
    "................",
};
static const Pal I_MACE_PAL[] = {
    { 'k', 0x16110e, 0 }, { 'm', 0xb0b4bc, 0 }, { 'M', 0x5a5e66, 0 }, { 'w', 0x5a3a20, 0 }, { 'g', 0x8a8a90, 0 },
};

static const char *const I_LEATHER[16] = {
    "................",
    "................",
    "....kkk..kkk....",
    "...klLkkkkLlk...",
    "..klLLlllllLLk..",
    "..klLLlllllLLk..",
    "..kkkLlllllLkk..",
    "....kLlbbblLk...",
    "....kLlllllLk...",
    "....kLlbbblLk...",
    "....kLlllllLk...",
    "....kLLLLLLLk...",
    ".....kkkkkkk....",
    "................",
    "................",
    "................",
};
static const Pal I_LEATHER_PAL[] = {
    { 'k', 0x16110e, 0 }, { 'l', 0x9a6a3e, 0 }, { 'L', 0x6a4426, 0 }, { 'b', 0xc8a050, 0 },
};

static const char *const I_CLOAK[16] = {
    "................",
    "......kkkk......",
    ".....kcggck.....",
    "....kccggcck....",
    "....kcCkkCck....",
    "...kcCk..kCck...",
    "...kcCk..kCck...",
    "..kcCck..kcCck..",
    "..kcCck..kcCck..",
    ".kcCCck..kcCCck.",
    ".kcCCck..kcCCck.",
    ".kcCCcckkccCCck.",
    "kcCCCcccccccCCck",
    "kkkkkkkkkkkkkkkk",
    "................",
    "................",
};
static const Pal I_CLOAK_PAL[] = {
    { 'k', 0x0e0c12, 0 }, { 'c', 0x3a3448, 0 }, { 'C', 0x262232, 0 }, { 'g', 0xa08a50, 0 },
};

static const char *const I_MAIL[16] = {
    "................",
    "................",
    "....kkk..kkk....",
    "...kmMkkkkMmk...",
    "..kmMmMmMmMmMk..",
    "..kMmMmMmMmMmk..",
    "..kkkmMmMmMkkk..",
    "....kMmMmMmk....",
    "....kmMmMmMk....",
    "....kMmMmMmk....",
    "....kmMmMmMk....",
    "....kMmMmMmk....",
    "....kmMmMmMk....",
    ".....kkkkkk.....",
    "................",
    "................",
};
static const Pal I_MAIL_PAL[] = {
    { 'k', 0x16110e, 0 }, { 'm', 0xb8bcc4, 0 }, { 'M', 0x6a6e78, 0 },
};

void tiles_build(Tiles *t)
{
    memset(t, 0, sizeof *t);
    for (int i = 0; i < N_FLOOR; i++) build_floor(&t->floor[i], i);
    for (int i = 0; i < N_WALL; i++) {
        build_wall_top(&t->wtop[i], i);
        build_wall_face(&t->wface[i], i);
    }
    build_cliff(&t->cliff);
    build_stairs(&t->stairs_dn, true);
    build_stairs(&t->stairs_up, false);
    build_door(&t->door, WALL_R);
    build_door(&t->door_low, LOW_R);
    build_door_side(&t->door_side);
    build_door_open(&t->door_open, false);
    build_door_open(&t->door_open_side, true);
    build_sconce(&t->sconce);
    build_brazier(&t->brazier, &t->brazier_glow);
    build_crystal(&t->crystal);
    for (int i = 0; i < 3; i++) build_rubble(&t->rubble[i], i);
    build_sprite(&t->mob[MK_PLAYER], &t->mob_glow[MK_PLAYER], HERO, HERO_PAL, sizeof HERO_PAL / sizeof *HERO_PAL);
    build_sprite(&t->mob[MK_RAT], &t->mob_glow[MK_RAT], RAT, RAT_PAL, sizeof RAT_PAL / sizeof *RAT_PAL);
    build_sprite(&t->mob[MK_GOBLIN], &t->mob_glow[MK_GOBLIN], GOBLIN, GOBLIN_PAL, sizeof GOBLIN_PAL / sizeof *GOBLIN_PAL);
    image_alloc(&t->mob[MK_SHADE], TILE, TILE);        /* drawn procedurally by the renderer */
    image_alloc(&t->mob_glow[MK_SHADE], TILE, TILE);
    build_sprite(&t->mob[MK_BAT], &t->mob_glow[MK_BAT], BAT, BAT_PAL, sizeof BAT_PAL / sizeof *BAT_PAL);
    build_sprite(&t->mob[MK_SLINGER], &t->mob_glow[MK_SLINGER], SLINGER, SLINGER_PAL, sizeof SLINGER_PAL / sizeof *SLINGER_PAL);
    build_sprite(&t->mob[MK_OGRE], &t->mob_glow[MK_OGRE], OGRE, OGRE_PAL, sizeof OGRE_PAL / sizeof *OGRE_PAL);
    build_sprite(&t->mob[MK_EEL], &t->mob_glow[MK_EEL], EEL, EEL_PAL, sizeof EEL_PAL / sizeof *EEL_PAL);
    build_sprite(&t->item[IT_TORCH], &t->item_glow[IT_TORCH], I_TORCH, I_TORCH_PAL, sizeof I_TORCH_PAL / sizeof *I_TORCH_PAL);
    build_sprite(&t->item[IT_DART], &t->item_glow[IT_DART], I_DART, I_DART_PAL, sizeof I_DART_PAL / sizeof *I_DART_PAL);
    build_sprite(&t->item[IT_POTION], &t->item_glow[IT_POTION], I_POTION, I_POTION_PAL, sizeof I_POTION_PAL / sizeof *I_POTION_PAL);
#define GEAR_SPRITE(K, A) build_sprite(&t->item[K], &t->item_glow[K], A, A##_PAL, sizeof A##_PAL / sizeof *A##_PAL)
    GEAR_SPRITE(IT_SWORD, I_SWORD);
    GEAR_SPRITE(IT_DAGGER, I_DAGGER);
    GEAR_SPRITE(IT_SPEAR, I_SPEAR);
    GEAR_SPRITE(IT_AXE, I_AXE);
    GEAR_SPRITE(IT_MACE, I_MACE);
    GEAR_SPRITE(IT_LEATHER, I_LEATHER);
    GEAR_SPRITE(IT_CLOAK, I_CLOAK);
    GEAR_SPRITE(IT_MAIL, I_MAIL);
#undef GEAR_SPRITE
}

void tiles_free(Tiles *t)
{
    Image *all = (Image *)t;
    for (size_t i = 0; i < sizeof *t / sizeof(Image); i++) image_free(&all[i]);
}
