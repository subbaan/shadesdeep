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

static void build_door(Image *im)
{
    image_alloc(im, TILE, TILE + WALL_R);
    int H = TILE + WALL_R;
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

static void build_door_open(Image *im)
{
    build_floor(im, 2);
    for (int y = 0; y < TILE; y++)
        for (int x = 0; x < TILE; x++) {
            uint32_t c = get(im, x, y) & 0xffffff;
            if (x < 3 || x >= TILE - 3) c = jitter(0x7a6d5d, (int)((fbm(x * 0.4f, y * 0.4f, 5) - 0.5f) * 30));
            else if (y > 12 && y < 16) c = jitter(c, -30);              /* threshold */
            put(im, x, y, A(c));
        }
}

static void build_sconce(Image *im)
{
    image_alloc(im, 12, 18);
    /* iron bracket (bottom) and wooden torch (top) */
    for (int y = 0; y < 18; y++)
        for (int x = 0; x < 12; x++) {
            uint32_t c = 0;
            if (y >= 4 && y < 13 && x >= 5 && x <= 6) c = A(x == 5 ? 0x7a5230 : 0x50341c);   /* torch */
            if (y >= 1 && y < 5 && x >= 4 && x <= 7) c = A(0x3a2a1c);                        /* head (char) */
            if (y == 11 && x >= 3 && x <= 8) c = A(0x4a4848);                                /* ring */
            if (y >= 12 && y < 17 && x == 6) c = A(0x3a3838);                                /* arm */
            if (y == 16 && x >= 4 && x <= 8) c = A(0x2a2828);                                /* plate */
            if (y == 17 && x >= 3 && x <= 9) c = A(0x1a1818);
            im->px[y * 12 + x] = c;
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
    build_door(&t->door);
    build_door_open(&t->door_open);
    build_sconce(&t->sconce);
    build_crystal(&t->crystal);
    for (int i = 0; i < 3; i++) build_rubble(&t->rubble[i], i);
    build_sprite(&t->mob[MK_PLAYER], &t->mob_glow[MK_PLAYER], HERO, HERO_PAL, sizeof HERO_PAL / sizeof *HERO_PAL);
    build_sprite(&t->mob[MK_RAT], &t->mob_glow[MK_RAT], RAT, RAT_PAL, sizeof RAT_PAL / sizeof *RAT_PAL);
    build_sprite(&t->mob[MK_GOBLIN], &t->mob_glow[MK_GOBLIN], GOBLIN, GOBLIN_PAL, sizeof GOBLIN_PAL / sizeof *GOBLIN_PAL);
    image_alloc(&t->mob[MK_SHADE], TILE, TILE);        /* drawn procedurally by the renderer */
    image_alloc(&t->mob_glow[MK_SHADE], TILE, TILE);
    build_sprite(&t->item[IT_TORCH], &t->item_glow[IT_TORCH], I_TORCH, I_TORCH_PAL, sizeof I_TORCH_PAL / sizeof *I_TORCH_PAL);
    build_sprite(&t->item[IT_DART], &t->item_glow[IT_DART], I_DART, I_DART_PAL, sizeof I_DART_PAL / sizeof *I_DART_PAL);
    build_sprite(&t->item[IT_POTION], &t->item_glow[IT_POTION], I_POTION, I_POTION_PAL, sizeof I_POTION_PAL / sizeof *I_POTION_PAL);
}

void tiles_free(Tiles *t)
{
    Image *all = (Image *)t;
    for (size_t i = 0; i < sizeof *t / sizeof(Image); i++) image_free(&all[i]);
}
