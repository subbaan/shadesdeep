#include "render.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tiles.h"

#define CW (MAP_W + 1)
#define CH (MAP_H + 1)
#define MAX_PART 700
#define LOWER_SCALE 0.72f       /* how much smaller the level below looks */

typedef struct { float r, g, b, v, s; } CF;     /* light, visibility, seen */

typedef struct {
    float x, y, vx, vy, life, max, size, grav;
    uint32_t c;
} Particle;

#define MAX_RIPPLE 48
#define STONE_T 0.24f       /* a sling stone's flight, seconds */
typedef struct { float x, y, age, life, str; } Ripple;

/* A creature drawn this frame that has a full-resolution sprite: after the
 * world canvas is zoomed up, render_hires draws it again from that sprite,
 * on the screen pixels where the small sprite is still what shows. */
typedef struct {
    int kind;
    int x, y, W;            /* canvas pixels: top-left and size of the drawn square */
    bool flip, asleep, sub;
    int wl;                 /* waterline (canvas y) when sub */
    float lr, lg, lb, alpha;
    int fl;                 /* white flash, 0..230 */
    bool stick;             /* the player's torch haft, hand (sx0, sy0) to head (sx1, sy1) */
    int sx0, sy0, sx1, sy1;
} HiCmd;
#define MAX_HICMD (MAX_MOBS + 1)

struct Renderer {
    Tiles t;
    /* full-resolution creature pass: per canvas pixel, which HiCmd (1-based) drew
     * it last, what was there before (bg) and what it wrote (wr) */
    HiCmd hic[MAX_HICMD];
    int nhic;
    const Canvas *rec_cv;       /* the world canvas being recorded, NULL when not recording */
    uint16_t *own;
    uint32_t *hbg, *hwr, *hsnap;
    size_t hcap;
    bool any_hi;
    int zoom;                   /* the screen's zoom: full-resolution sprites from 2 */
    int hand_x, hand_y;         /* the player's lantern hand on its sprite (facing right): a torch is held there */
    int hcv_w, hcv_h;           /* size of the canvas last recorded */
    CF corner[CH][CW];
    float chasmc[CH][CW];
    float tl[MAP_H][MAP_W][3];
    Canvas lower;
    Canvas full;                /* the whole current level, for the map */
    bool lower_ok;
    int lower_depth;
    float camx, camy;
    bool cam_ok;
    int cam_depth;
    Particle part[MAX_PART];
    Ripple rip[MAX_RIPPLE];
    struct { float x0, y0, x1, y1, t; } stone[4];      /* sling stones in flight (t: seconds, <0 idle) */
    float rip_acc[MAX_MOBS + 1];        /* idle ripples around anything standing in water */
    float flash_t, flash_x, flash_y;    /* flash powder going off: seconds left, world pixels */
    float ember_acc[MAX_LIGHTS];
    uint8_t noise[256 * 256];
    Rng rng;
};

typedef struct {
    Level *lv;
    Mob *mobs;
    Mob *player;            /* NULL for the level below */
    bool fog;
    bool through;           /* chasms show the level below */
    int camx, camy;         /* top-left of the canvas in world pixels */
    float time;
    float (*visf)[MAP_W];
    float (*plight)[MAP_W];
    FloorItem *items;
    bool torch;             /* the player carries a burning torch */
    bool projlight;         /* a thrown torch is in the air */
    float projx, projy;     /* its position, world pixels */
    int projtx, projty;
} View;

/* ------------------------------------------------------------ helpers */

/* soft shoulder so pools of light glow instead of clipping to flat colour */
static inline int tone(float v)
{
    if (v <= 150) return v < 0 ? 0 : (int)v;
    float d = v - 150;
    return (int)(150 + 105 * d / (105 + d));
}

static inline uint32_t lightc(uint32_t a, float lr, float lg, float lb)
{
    return RGB(tone(CR(a) * lr), tone(CG(a) * lg), tone(CB(a) * lb));
}

static inline uint32_t memtone(uint32_t a)
{
    int l = (CR(a) * 77 + CG(a) * 150 + CB(a) * 29) >> 8;
    return RGB(l * 15 / 100 + 3, l * 18 / 100 + 5, l * 27 / 100 + 10);
}

static inline uint32_t fogmix(uint32_t lit, uint32_t mem, float v, float s)
{
    uint32_t c = mix(mem, lit, (int)(v * 256));
    return mix(0, c, (int)(s * 256));
}

static inline void cf_lerp(CF *o, const CF *a, const CF *b, float t)
{
    o->r = a->r + (b->r - a->r) * t;
    o->g = a->g + (b->g - a->g) * t;
    o->b = a->b + (b->b - a->b) * t;
    o->v = a->v + (b->v - a->v) * t;
    o->s = a->s + (b->s - a->s) * t;
}

static float smooth01(float e0, float e1, float x)
{
    float t = clampf((x - e0) / (e1 - e0), 0, 1);
    return t * t * (3 - 2 * t);
}

static float noise_at(const Renderer *r, float x, float y)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    int x0 = xi & 255, y0 = yi & 255, x1 = (xi + 1) & 255, y1 = (yi + 1) & 255;
    float a = r->noise[y0 * 256 + x0], b = r->noise[y0 * 256 + x1];
    float c = r->noise[y1 * 256 + x0], d = r->noise[y1 * 256 + x1];
    return (a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy) / 255.0f;
}

/* ------------------------------------------------------------ lifecycle */

Renderer *render_new(void)
{
    Renderer *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    tiles_build(&r->t);
    r->hand_x = 7;
    r->hand_y = 21;
    r->rng.s = 1234;
    for (int k = 0; k < 4; k++) r->stone[k].t = STONE_T;
    /* tileable value noise: a coarse random grid smoothly upsampled */
    uint8_t grid[32][32];
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) grid[y][x] = (uint8_t)(hash2(x, y, 31337) & 255);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            float gx = x / 8.0f, gy = y / 8.0f;
            int xi = (int)gx, yi = (int)gy;
            float fx = gx - xi, fy = gy - yi;
            fx = fx * fx * (3 - 2 * fx);
            fy = fy * fy * (3 - 2 * fy);
            float a = grid[yi & 31][xi & 31], b = grid[yi & 31][(xi + 1) & 31];
            float c = grid[(yi + 1) & 31][xi & 31], d = grid[(yi + 1) & 31][(xi + 1) & 31];
            r->noise[y * 256 + x] = (uint8_t)(a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy);
        }
    r->lower.w = MAP_W * TILE;
    r->lower.h = MAP_H * TILE;
    r->lower.px = calloc((size_t)r->lower.w * r->lower.h, 4);
    r->lower_depth = -1;
    return r;
}

void render_free(Renderer *r)
{
    if (!r) return;
    tiles_free(&r->t);
    free(r->own);
    free(r->hbg);
    free(r->hwr);
    free(r->hsnap);
    free(r->lower.px);
    free(r->full.px);
    free(r);
}

void render_cam_pos(const Renderer *r, float *x, float *y)
{
    *x = r->camx;
    *y = r->camy;
}

void render_camera(Renderer *r, const Game *g, int ww, int wh, float dt, bool snap)
{
    const Mob *p = &g->player;
    float tx = p->rx * TILE + TILE / 2.0f - ww / 2.0f;
    float ty = p->ry * TILE + TILE / 2.0f - wh / 2.0f;
    float mw = MAP_W * TILE, mh = MAP_H * TILE, pad = TILE * 2;
    if (ww >= mw + pad * 2) tx = (mw - ww) / 2;
    else tx = clampf(tx, -pad, mw - ww + pad);
    if (wh >= mh + pad * 2) ty = (mh - wh) / 2;
    else ty = clampf(ty, -pad, mh - wh + pad);
    if (snap || !r->cam_ok || r->cam_depth != g->depth) {
        r->camx = tx;
        r->camy = ty;
        r->cam_ok = true;
        r->cam_depth = g->depth;
        return;
    }
    float k = 1.0f - expf(-dt * 7.0f);
    r->camx += (tx - r->camx) * k;
    r->camy += (ty - r->camy) * k;
}

/* --------------------------------------------------------------- light */

typedef struct { Renderer *r; float px, py, rad; } DynCtx;

/* a moving light: weighted by the distance from its true position, not its tile */
static void dyn_mark(void *vc, int x, int y, float d)
{
    (void)d;
    DynCtx *c = vc;
    float dx = (x * TILE + TILE / 2 - c->px) / TILE, dy = (y * TILE + TILE / 2 - c->py) / TILE;
    float dd = sqrtf(dx * dx + dy * dy);
    if (dd > c->rad) return;
    float k = 1.0f - dd / c->rad, w = k * k * 1.1f;
    c->r->tl[y][x][0] += 1.35f * w;
    c->r->tl[y][x][1] += 0.80f * w;
    c->r->tl[y][x][2] += 0.40f * w;
}

static void compute_light(Renderer *r, const View *v)
{
    const Level *lv = v->lv;
    float amb_r = lv->ambient * 0.8f, amb_g = lv->ambient, amb_b = lv->ambient * 1.6f;
    if (!v->fog) { amb_r += 0.05f; amb_g += 0.07f; amb_b += 0.11f; }
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            r->tl[y][x][0] = amb_r;
            r->tl[y][x][1] = amb_g;
            r->tl[y][x][2] = amb_b;
        }
    float *tl = &r->tl[0][0][0];
    for (int i = 0; i < lv->nlights; i++) {
        const Light *l = &lv->lights[i];
        if (!l->lit) continue;
        float f = v->fog ? light_flicker(l, v->time) : 1.0f;
        float cr = l->r * f, cg = l->g * f, cb = l->b * f;
        for (int k = 0; k < lv->cn[i]; k++) {
            int idx = lv->ci[i][k];
            float w = lv->cw[i][k];
            tl[idx * 3] += cr * w;
            tl[idx * 3 + 1] += cg * w;
            tl[idx * 3 + 2] += cb * w;
        }
    }
    if (v->plight) {
        float f, cr, cg, cb;
        if (v->torch) {
            f = 0.86f + 0.07f * sinf(v->time * 7.7f) + 0.05f * sinf(v->time * 13.3f + 1) + 0.03f * sinf(v->time * 27.0f);
            cr = 1.30f; cg = 0.78f; cb = 0.40f;
        } else {
            f = 0.93f + 0.04f * sinf(v->time * 5.3f) + 0.03f * sinf(v->time * 11.1f);
            cr = 0.80f; cg = 0.58f; cb = 0.34f;
        }
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) {
                float w = v->plight[y][x] * f;
                if (w <= 0) continue;
                r->tl[y][x][0] += cr * w;
                r->tl[y][x][1] += cg * w;
                r->tl[y][x][2] += cb * w;
            }
    }
    if (v->projlight) {
        DynCtx dc = { r, v->projx, v->projy, 5.5f };
        fov_cast(lv, v->projtx, v->projty, 6, dyn_mark, &dc);
    }
    for (int cy = 0; cy < CH; cy++)
        for (int cx = 0; cx < CW; cx++) {
            CF c = { 0 };
            int n = 0, nch = 0;
            for (int j = -1; j <= 0; j++)
                for (int i = -1; i <= 0; i++) {
                    int x = cx + i, y = cy + j;
                    if (!in_map(x, y)) continue;
                    Tile t = (Tile)lv->t[y][x];
                    if (t == T_CHASM) nch++;
                    if (tile_opaque(t)) continue;
                    c.r += r->tl[y][x][0];
                    c.g += r->tl[y][x][1];
                    c.b += r->tl[y][x][2];
                    if (v->fog) {
                        c.v += v->visf[y][x];
                        c.s += (lv->f[y][x] & F_SEEN) ? 1.0f : 0.0f;
                    } else {
                        c.v += 1;
                        c.s += 1;
                    }
                    n++;
                }
            if (n) {
                float inv = 1.0f / n;
                c.r *= inv; c.g *= inv; c.b *= inv; c.v *= inv; c.s *= inv;
            }
            r->corner[cy][cx] = c;
            r->chasmc[cy][cx] = nch / 4.0f;
        }
}

/* ------------------------------------------------------------- drawing */

enum { AO_N = 1, AO_W = 2, AO_E = 4, AO_NW = 8, AO_NE = 16 };

static inline float ao_at(int ao, int i, int j)
{
    float f = 1.0f;
    if ((ao & AO_N) && j < 9) f *= 0.42f + 0.58f * (j / 9.0f);
    if ((ao & AO_W) && i < 5) f *= 0.6f + 0.4f * (i / 5.0f);
    if ((ao & AO_E) && i > 26) f *= 0.6f + 0.4f * ((31 - i) / 5.0f);
    if ((ao & AO_NW) && i < 6 && j < 6) f *= 0.7f + 0.3f * ((i > j ? i : j) / 6.0f);
    if ((ao & AO_NE) && i > 25 && j < 6) f *= 0.7f + 0.3f * (((31 - i) > j ? (31 - i) : j) / 6.0f);
    return f;
}

/* Draw an albedo image lit by four corners (interpolated over w x h).
 * g0/g1 scale the light at the top/bottom; `over` is an optional overlay. */
static void draw_lit(Canvas *cv, int sx, int sy, const Image *im, int w, int h,
                     const CF *c00, const CF *c10, const CF *c01, const CF *c11,
                     float g0, float g1, int ao, bool fog, const Image *over, int edge)
{
    for (int j = 0; j < h; j++) {
        int y = sy + j;
        if ((unsigned)y >= (unsigned)cv->h) continue;
        float v = (j + 0.5f) / h;
        CF L, R;
        cf_lerp(&L, c00, c01, v);
        cf_lerp(&R, c10, c11, v);
        float gm = g0 + (g1 - g0) * v;
        uint32_t *row = cv->px + (size_t)y * cv->w;
        const uint32_t *src = im->px + (size_t)j * im->w;
        for (int i = 0; i < w; i++) {
            int x = sx + i;
            if ((unsigned)x >= (unsigned)cv->w) continue;
            uint32_t a = src[i];
            int alpha = CA(a);
            if (!alpha) continue;
            if (over) {
                uint32_t o = over->px[j * over->w + i];
                if (CA(o)) a = mix(a, o, CA(o));
            }
            a &= 0xffffff;
            if (edge) {
                /* lip where the floor meets a chasm: bevel the edge */
                if (((edge & 1) && j == 0) || ((edge & 2) && i == 0) || ((edge & 4) && i == w - 1)) a = add_sat(a, 34, 30, 24);
                if (((edge & 8) && j >= h - 2) || ((edge & 1) && j == 1)) a = add_sat(a, -30, -30, -30);
            }
            CF p;
            float u = (i + 0.5f) / w;
            cf_lerp(&p, &L, &R, u);
            float m = gm * (ao ? ao_at(ao, i, j) : 1.0f);
            uint32_t lit = lightc(a, p.r * m, p.g * m, p.b * m);
            uint32_t out = fog ? fogmix(lit, memtone(a), p.v, p.s) : lit;
            if (alpha >= 255) row[x] = out;
            else blend_px(&row[x], out, alpha);
        }
    }
}

static uint32_t sample_lower(const Renderer *r, float x, float y)
{
    const Canvas *lc = &r->lower;
    x -= 0.5f;
    y -= 0.5f;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > lc->w - 2) x = (float)(lc->w - 2);
    if (y > lc->h - 2) y = (float)(lc->h - 2);
    int xi = (int)x, yi = (int)y;
    int fx = (int)((x - xi) * 256), fy = (int)((y - yi) * 256);
    const uint32_t *p = lc->px + (size_t)yi * lc->w + xi;
    uint32_t top = mix(p[0], p[1], fx), bot = mix(p[lc->w], p[lc->w + 1], fx);
    return mix(top, bot, fy);
}

static void draw_chasm(Renderer *r, Canvas *cv, const View *v, int tx, int ty, int sx, int sy)
{
    const Level *lv = v->lv;
    const CF *c00 = &r->corner[ty][tx], *c10 = &r->corner[ty][tx + 1];
    const CF *c01 = &r->corner[ty + 1][tx], *c11 = &r->corner[ty + 1][tx + 1];
    float m00 = r->chasmc[ty][tx], m10 = r->chasmc[ty][tx + 1];
    float m01 = r->chasmc[ty + 1][tx], m11 = r->chasmc[ty + 1][tx + 1];
    float pcx = v->camx + cv->w * 0.5f, pcy = v->camy + cv->h * 0.5f;
    float inv = 1.0f / LOWER_SCALE;
    bool below = v->through && r->lower_ok;
    float t = v->time;
    bool oN = tile_at(lv, tx, ty - 1) != T_CHASM, oS = tile_at(lv, tx, ty + 1) != T_CHASM;
    bool oW = tile_at(lv, tx - 1, ty) != T_CHASM, oE = tile_at(lv, tx + 1, ty) != T_CHASM;
    bool oNW = tile_at(lv, tx - 1, ty - 1) != T_CHASM, oNE = tile_at(lv, tx + 1, ty - 1) != T_CHASM;
    for (int j = 0; j < TILE; j++) {
        int y = sy + j;
        if ((unsigned)y >= (unsigned)cv->h) continue;
        float vv = (j + 0.5f) / TILE;
        CF L, R;
        cf_lerp(&L, c00, c01, vv);
        cf_lerp(&R, c10, c11, vv);
        float ml = m00 + (m01 - m00) * vv, mr = m10 + (m11 - m10) * vv;
        float wy = (float)(v->camy + y);
        float ly = pcy + (wy - pcy) * inv;
        uint32_t *row = cv->px + (size_t)y * cv->w;
        for (int i = 0; i < TILE; i++) {
            int x = sx + i;
            if ((unsigned)x >= (unsigned)cv->w) continue;
            float u = (i + 0.5f) / TILE;
            float wx = (float)(v->camx + x);
            float lx = pcx + (wx - pcx) * inv;
            uint32_t c = below ? sample_lower(r, lx, ly) : 0x000000;
            /* depth: dim, misty, darker toward the rim */
            float rim = smooth01(0.3f, 1.0f, ml + (mr - ml) * u);
            float n = noise_at(r, lx * 0.035f + t * 0.9f, ly * 0.035f + t * 0.35f);
            float n2 = noise_at(r, lx * 0.08f - t * 1.7f + 50, ly * 0.08f + 90);
            float mist = 0.26f + 0.34f * n * n2 * 1.6f;
            /* the walls of the pit shade what lies below them */
            float sh = 1.0f;
            if (oN) sh *= smooth01(0, 30, (float)j);
            else if (oNW && !oW) sh *= 0.25f + 0.75f * smooth01(0, 14, sqrtf((float)(i * i + j * j)));
            if (oNE && !oN && !oE) sh *= 0.25f + 0.75f * smooth01(0, 14, sqrtf((float)((31 - i) * (31 - i) + j * j)));
            if (oW) sh *= 0.2f + 0.8f * smooth01(0, 12, (float)i);
            if (oE) sh *= 0.2f + 0.8f * smooth01(0, 12, (float)(31 - i));
            if (oS) sh *= 0.55f + 0.45f * smooth01(0, 6, (float)(31 - j));
            c = mix(0x000000, c, (int)(256 * sh));
            c = mix(c, 0x000000, (int)(256 * (0.30f + 0.40f * (1 - rim))));
            c = mix(c, RGB(26, 58, 70), (int)(256 * mist * (0.5f + 0.5f * rim)));
            if (v->fog) {
                CF p;
                cf_lerp(&p, &L, &R, u);
                c = fogmix(c, memtone(c), p.v, p.s);
            }
            row[x] = c;
        }
    }
    /* the cliff face under the edge we look over: rock lit a little by the room above */
    if (oN) {
        CF a = *c00, b = *c10;
        a.r = a.r * 0.8f + 0.10f; a.g = a.g * 0.8f + 0.11f; a.b = a.b * 0.8f + 0.14f;
        b.r = b.r * 0.8f + 0.10f; b.g = b.g * 0.8f + 0.11f; b.b = b.b * 0.8f + 0.14f;
        a.v = b.v = 1;
        CF a2 = a, b2 = b;
        draw_lit(cv, sx, sy, &r->t.cliff, TILE, TILE, &a, &b, &a2, &b2, 1.0f, 0.2f, 0, v->fog, NULL, 0);
    }
}

static void light_at_px(const Renderer *r, float wx, float wy, CF *o);

/* where a torch burned out: a smear of soot and a charred stub */
static void draw_scorch(Renderer *r, Canvas *cv, const View *v, int tx, int ty, int sx, int sy)
{
    uint32_t h = hash2(tx, ty, 0x5c0c);
    float cx = sx + 16 + (int)(h % 5) - 2, cy = sy + 17 + (int)((h >> 4) % 5) - 2;
    for (int j = -14; j <= 14; j++)
        for (int i = -15; i <= 15; i++) {
            int x = (int)cx + i, y = (int)cy + j;
            if ((unsigned)x >= (unsigned)cv->w || (unsigned)y >= (unsigned)cv->h) continue;
            float d = (i * i) / 225.0f + (j * j) / 150.0f;     /* squashed: it lies on the floor */
            if (d >= 1) continue;
            uint32_t n = hash2((tx * TILE + i + 15) / 3, (ty * TILE + j + 14) / 3, 0x50f7);
            float rough = 0.75f + 0.5f * (n & 255) / 255.0f;  /* blotchy edge */
            float k = clampf((1 - d * rough) * 1.6f, 0, 1);
            k *= 0.70f + 0.25f * ((n >> 8) & 255) / 255.0f;
            if (k <= 0.02f) continue;
            uint32_t *p = &cv->px[(size_t)y * cv->w + x];
            *p = mix(*p, 0x050404, (int)(k * 0.9f * 256));
        }
    if (v->lv->scorch[ty][tx] != 1) return;     /* burnt oil leaves only soot */
    /* the stub: the torch sprite, charred black with an ashen head */
    CF c;
    light_at_px(r, tx * TILE + 16.0f, ty * TILE + 16.0f, &c);
    float vis = v->fog ? c.v : 1, seen = v->fog ? c.s : 1;
    const Image *im = &r->t.item[IT_TORCH];
    int ox = sx + (int)(h >> 12) % 5 - 2, oy = sy + (int)(h >> 16) % 3 - 1;
    for (int j = 0; j < im->h; j++)
        for (int i = 0; i < im->w; i++) {
            uint32_t a = im->px[j * im->w + i];
            if (CA(a) < 128) continue;
            int x = ox + i, y = oy + j;
            if ((unsigned)x >= (unsigned)cv->w || (unsigned)y >= (unsigned)cv->h) continue;
            int R = CR(a), G = CG(a), B = CB(a), l = (R * 77 + G * 150 + B * 29) >> 8;
            uint32_t ch;
            float along = (float)(i + im->h - 1 - j) / (float)(im->w + im->h - 2);
            if (l > 40 && (R > G + 50 || along > 0.56f)) {   /* the head (top right): burnt to ash */
                int k = 150 + (int)(hash2(x, y, 9) % 60);
                ch = RGB(k, k - 4, k - 10);
            } else {                                /* the shaft: charcoal */
                ch = RGB(14 + l / 6, 11 + l / 8, 9 + l / 10);
            }
            uint32_t lit = lightc(ch, c.r + 0.15f, c.g + 0.15f, c.b + 0.15f);
            cv->px[(size_t)y * cv->w + x] = v->fog ? fogmix(lit, memtone(ch), vis, seen) : lit;
        }
}

/* a line of salt: pale grains scattered over the tile, catching what light there is */
static void draw_salt(Renderer *r, Canvas *cv, const View *v, int tx, int ty, int sx, int sy)
{
    CF c;
    light_at_px(r, tx * TILE + 16.0f, ty * TILE + 16.0f, &c);
    float vis = v->fog ? c.v : 1, seen = v->fog ? c.s : 1;
    for (int k = 0; k < 46; k++) {
        uint32_t h = hash2(tx * 53 + k, ty * 29 + k, 0x5a17);
        int x = sx + 2 + (int)(h % 28), y = sy + 3 + (int)((h >> 8) % 27);
        if ((unsigned)x >= (unsigned)cv->w - 1 || (unsigned)y >= (unsigned)cv->h) continue;
        int b = 200 + (int)((h >> 16) % 56);
        uint32_t col = RGB(b, b, b - 8);
        uint32_t lit = lightc(col, c.r + 0.22f, c.g + 0.22f, c.b + 0.25f);
        uint32_t *px = &cv->px[(size_t)y * cv->w + x];
        *px = v->fog ? fogmix(lit, memtone(col), vis, seen) : lit;
        if ((h >> 24) & 1) px[1] = mix(px[1], *px, 140);
    }
}

/* put one lit pixel of colour col on the floor at canvas (x, y) */
static inline void floor_px(Canvas *cv, const View *v, const CF *c, int x, int y, uint32_t col, float lift)
{
    if ((unsigned)x >= (unsigned)cv->w || (unsigned)y >= (unsigned)cv->h) return;
    uint32_t lit = lightc(col, c->r + lift, c->g + lift, c->b + lift);
    uint32_t *px = &cv->px[(size_t)y * cv->w + x];
    *px = v->fog ? fogmix(lit, memtone(col), c->v, c->s) : lit;
}

/* a trap you know about, marked on its tile */
static void draw_trap(Renderer *r, Canvas *cv, const View *v, int tx, int ty, int sx, int sy)
{
    CF c;
    light_at_px(r, tx * TILE + 16.0f, ty * TILE + 16.0f, &c);
    if (!v->fog) c.v = c.s = 1;
    int k = v->lv->trap[ty][tx];
    int cx = sx + 16, cy = sy + 17;
    if (k == TR_ALARM) {
        /* a square plate sunk in the floor, with a bell graven on it */
        for (int j = -7; j <= 7; j++)
            for (int i = -8; i <= 8; i++) {
                bool rim = abs(i) == 8 || abs(j) == 7;
                uint32_t col = rim ? 0x1c1a18 : (abs(i) == 7 || j == -6) ? 0x8a8070 : 0x5e5850;
                floor_px(cv, v, &c, cx + i, cy + j, col, 0.12f);
            }
        static const char *BELL[7] = { "..##..", ".####.", ".####.", ".####.", "######", "......", "..##.." };
        for (int j = 0; j < 7; j++)
            for (int i = 0; i < 6; i++)
                if (BELL[j][i] == '#') floor_px(cv, v, &c, cx - 3 + i, cy - 4 + j, 0xb89850, 0.2f);
    } else if (k == TR_GUST) {
        /* a round iron grating over a dark shaft */
        for (int j = -8; j <= 8; j++)
            for (int i = -9; i <= 9; i++) {
                float d = (i * i) / 81.0f + (j * j) / 64.0f;
                if (d > 1) continue;
                uint32_t col = d > 0.78f ? 0x3a3836 : (i + 9) % 3 == 0 ? 0x4e4c4a : 0x060708;
                floor_px(cv, v, &c, cx + i, cy + j, col, 0.1f);
            }
        /* frost about the rim */
        for (int n = 0; n < 10; n++) {
            uint32_t h = hash2(tx * 7 + n, ty * 5 + n, 0x6a57);
            float a = (h % 628) / 100.0f;
            floor_px(cv, v, &c, cx + (int)(cosf(a) * 11), cy + (int)(sinf(a) * 9), 0xb8d0e8, 0.15f);
        }
    } else if (k == TR_PIT) {
        /* the outline of a trapdoor, its planks and an iron hinge */
        for (int j = -10; j <= 10; j++)
            for (int i = -11; i <= 11; i++) {
                bool edge = abs(i) == 11 || abs(j) == 10;
                bool seam = !edge && (i + 11) % 6 == 0;
                if (!edge && !seam) continue;
                floor_px(cv, v, &c, cx + i, cy + j, edge ? 0x0c0a08 : 0x2a1e14, 0);
            }
        for (int i = -9; i <= -5; i++) floor_px(cv, v, &c, cx + i, cy - 9, 0x7a7060, 0.15f);
        for (int i = 5; i <= 9; i++) floor_px(cv, v, &c, cx + i, cy - 9, 0x7a7060, 0.15f);
    } else if (k == TR_SNARE) {
        /* a loop of wire and its peg */
        for (int a = 0; a < 48; a++) {
            float t = a * 6.28318f / 48;
            floor_px(cv, v, &c, cx + (int)lroundf(cosf(t) * 8), cy + (int)lroundf(sinf(t) * 5), 0xa8a8a0, 0.2f);
        }
        for (int i = 0; i < 6; i++) floor_px(cv, v, &c, cx + 8 + i, cy - i / 2, 0xa8a8a0, 0.2f);
        for (int j = -2; j <= 2; j++)
            for (int i = 0; i <= 1; i++) floor_px(cv, v, &c, cx + 14 + i, cy - 3 + j, 0x6a4a2a, 0.1f);
    }
}

/* what a special room leaves on its floor: bones in a nest, straw and splinters in a storeroom,
 * rust and broken blades in an armoury */
static void draw_room_floor(Renderer *r, Canvas *cv, const View *v, int tx, int ty, int sx, int sy, int kind)
{
    uint32_t h = hash2(tx, ty, 0x7007 + (uint32_t)kind);
    if (h % 100 >= (kind == RK_NEST ? 45u : kind == RK_STORE ? 40u : 30u)) return;
    CF c;
    light_at_px(r, tx * TILE + 16.0f, ty * TILE + 16.0f, &c);
    if (!v->fog) c.v = c.s = 1;
    int ox = sx + 6 + (int)((h >> 8) % 18), oy = sy + 8 + (int)((h >> 14) % 16);
    if (kind == RK_NEST) {
        if ((h >> 20) % 5 == 0) {
            /* a skull */
            static const char *SK[6] = { ".###.", "#####", "#.#.#", "#####", ".#.#.", "....." };
            for (int j = 0; j < 6; j++)
                for (int i = 0; i < 5; i++)
                    if (SK[j][i] == '#') floor_px(cv, v, &c, ox + i, oy + j, 0xc8c0a8, 0.1f);
            return;
        }
        /* a long bone, knobbed at both ends, lying at some angle */
        float a = ((h >> 22) % 314) / 100.0f, ca = cosf(a), sa = sinf(a);
        int len = 4 + (int)((h >> 3) % 4);
        for (int i = -len; i <= len; i++) floor_px(cv, v, &c, ox + (int)lroundf(ca * i), oy + (int)lroundf(sa * i * 0.6f), 0xbab09a, 0.08f);
        for (int e = -1; e <= 1; e += 2) {
            int ex = ox + (int)lroundf(ca * len * e), ey = oy + (int)lroundf(sa * len * e * 0.6f);
            floor_px(cv, v, &c, ex + 1, ey, 0xd0c8b0, 0.08f);
            floor_px(cv, v, &c, ex, ey + 1, 0xd0c8b0, 0.08f);
            floor_px(cv, v, &c, ex - 1, ey, 0xd0c8b0, 0.08f);
        }
    } else if (kind == RK_STORE) {
        /* straw, and now and then a splintered plank */
        for (int n = 0; n < 9; n++) {
            uint32_t q = hash2(tx * 13 + n, ty * 11 + n, 0x57a3);
            int x = ox + (int)(q % 12) - 6, y = oy + (int)((q >> 8) % 10) - 5, dx = (q >> 16) & 1 ? 1 : -1;
            for (int i = 0; i < 4; i++) floor_px(cv, v, &c, x + i * dx, y + i / 2, (q >> 20) & 1 ? 0xa89048 : 0x8a7438, 0.08f);
        }
        if ((h >> 21) % 3 == 0)
            for (int j = 0; j < 3; j++)
                for (int i = 0; i < 12; i++)
                    if (!(j == 0 && i > 9)) floor_px(cv, v, &c, ox - 6 + i, oy + 4 + j, j == 2 ? 0x3a2814 : 0x6a4a28, 0.05f);
    } else {
        /* rust flakes and a broken blade */
        for (int n = 0; n < 7; n++) {
            uint32_t q = hash2(tx * 17 + n, ty * 19 + n, 0x2057);
            floor_px(cv, v, &c, ox + (int)(q % 14) - 7, oy + (int)((q >> 8) % 10) - 5, 0x7a3a1a, 0.05f);
        }
        if ((h >> 21) % 2 == 0) {
            for (int i = 0; i < 8; i++) floor_px(cv, v, &c, ox - 4 + i, oy - i / 3, 0x9098a0, 0.12f);
            for (int j = -2; j <= 2; j++) floor_px(cv, v, &c, ox - 5, oy + j, 0x5a4a30, 0.05f);
        }
    }
}

/* ---------------------------------------------------------------- water */

static inline float tile_depth(const Level *lv, int x, int y)
{
    Tile t = tile_at(lv, x, y);
    return t == T_DEEP ? 1.0f : t == T_SHALLOW ? 0.45f : 0.0f;
}

static inline bool water_px(const View *v, int x, int y)
{
    int tx = (x + v->camx) >> 5, ty = (y + v->camy) >> 5;     /* TILE == 32 */
    return tile_water(tile_at(v->lv, tx, ty));
}

/* surface height and slope, world pixels */
static inline void waves(float wx, float wy, float t, float *hx, float *hy)
{
    float a = 0.31f * wx + 0.12f * wy + 1.7f * t, b = 0.27f * wy - 0.08f * wx - 1.3f * t, c = 0.17f * (wx + wy) + 0.9f * t;
    *hx = 0.31f * cosf(a) - 0.08f * cosf(b) + 0.17f * cosf(c) * 0.7f;
    *hy = 0.12f * cosf(a) + 0.27f * cosf(b) + 0.17f * cosf(c) * 0.7f;
}

static void draw_water(Renderer *r, Canvas *cv, const View *v, int tx, int ty, int sx, int sy)
{
    const Level *lv = v->lv;
    uint32_t h = hash2(tx, ty, lv->seed);
    const Image *bed = &r->t.floor[h % N_FLOOR];
    /* water presence and depth at the corners, averaged over the four tiles
     * around each, so shores round off and the deep fades in */
    float wc[2][2], dc[2][2];
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            float w = 0, d = 0;
            for (int b = -1; b <= 0; b++)
                for (int a = -1; a <= 0; a++) {
                    w += tile_water(tile_at(lv, tx + i + a, ty + j + b)) ? 1 : 0;
                    d += tile_depth(lv, tx + i + a, ty + j + b);
                }
            wc[j][i] = w / 4;
            dc[j][i] = d / 4;
        }
    float t = v->time;
    const CF *c00 = &r->corner[ty][tx], *c10 = &r->corner[ty][tx + 1];
    const CF *c01 = &r->corner[ty + 1][tx], *c11 = &r->corner[ty + 1][tx + 1];
    for (int j = 0; j < TILE; j++) {
        int y = sy + j;
        if ((unsigned)y >= (unsigned)cv->h) continue;
        float fv = (j + 0.5f) / TILE;
        CF L, Rr;
        cf_lerp(&L, c00, c01, fv);
        cf_lerp(&Rr, c10, c11, fv);
        uint32_t *row = cv->px + (size_t)y * cv->w;
        for (int i = 0; i < TILE; i++) {
            int x = sx + i;
            if ((unsigned)x >= (unsigned)cv->w) continue;
            float fu = (i + 0.5f) / TILE;
            CF p;
            cf_lerp(&p, &L, &Rr, fu);
            float wx = (float)(tx * TILE + i), wy = (float)(ty * TILE + j);
            float wf = (wc[0][0] * (1 - fu) + wc[0][1] * fu) * (1 - fv) + (wc[1][0] * (1 - fu) + wc[1][1] * fu) * fv;
            float d = (dc[0][0] * (1 - fu) + dc[0][1] * fu) * (1 - fv) + (dc[1][0] * (1 - fu) + dc[1][1] * fu) * fv;
            wf += 0.22f * sinf(wx * 0.19f + wy * 0.07f + 1.3f) * sinf(wy * 0.17f - wx * 0.09f + 0.4f);
            uint32_t b = bed->px[j * TILE + i] & 0xffffff;
            if (wf < 0.42f) {
                /* dry stone, darker where it's wet */
                float wet = clampf((wf - 0.30f) / 0.12f, 0, 1);
                uint32_t a = mix(b, 0x1c1a18, (int)(wet * 90));
                uint32_t lit = lightc(a, p.r, p.g, p.b);
                row[x] = v->fog ? fogmix(lit, memtone(a), p.v, p.s) : lit;
                continue;
            }
            float shore = clampf((wf - 0.42f) / 0.14f, 0, 1);
            float hx, hy;
            waves(wx, wy, t, &hx, &hy);
            /* the bed, seen through the water (refraction wobble) */
            int bi = clampi(i + (int)lroundf(hx * 2.2f), 0, TILE - 1), bj = clampi(j + (int)lroundf(hy * 2.2f), 0, TILE - 1);
            b = bed->px[bj * TILE + bi] & 0xffffff;
            float k = clampf(d * 1.5f, 0, 1);
            uint32_t under = mix(mix(b, 0x2a7c84, 150), 0x05283a, (int)(k * 236));
            /* caustics dancing on the shallow bed */
            float ca = sinf(wx * 0.23f + sinf(wy * 0.19f + t * 1.1f) * 2.2f + t * 1.6f) * sinf(wy * 0.21f - sinf(wx * 0.17f - t) * 2.0f - t * 1.3f);
            ca = ca > 0 ? ca * ca * ca * ca : 0;
            float caus = 1 + ca * (1 - k) * 1.8f;
            uint32_t lit = lightc(under, p.r * 0.82f * caus, p.g * 1.02f * caus, p.b * 1.12f * caus);
            /* light glancing off the ripples */
            float s = hy * 1.4f - hx * 0.5f;
            s = s > 0.25f ? (s - 0.25f) * 2.2f : 0;
            s = s * s;
            float spec = s * (0.35f + 0.65f * k) * 170;
            int ar = (int)(spec * p.r), ag = (int)(spec * p.g), ab = (int)(spec * p.b * 0.9f + s * 6);
            /* a lapping line where the water meets the stone */
            float lap = 0.45f + 0.35f * sinf(t * 1.8f + (wx + wy) * 0.21f);
            if (shore < lap) {
                float e = 1 - shore / lap;
                ar += (int)(60 * e * p.r); ag += (int)(75 * e * p.g); ab += (int)(78 * e * p.b);
            }
            /* a faint cold sheen of its own, so water reads as water even under firelight */
            float sh = 0.5f + 0.5f * fminf(1.0f, (p.r + p.g + p.b) * 0.6f);
            ar += (int)(3 * sh); ag += (int)(15 * sh); ab += (int)(19 * sh);
            lit = add_sat(lit, ar, ag, ab);
            if (v->fog) lit = fogmix(lit, mix(memtone(b), 0x184a5a, (int)(110 + 90 * k)), p.v, p.s);
            row[x] = lit;
        }
    }
    /* lily pads on the shallows */
    if (tile_at(lv, tx, ty) == T_SHALLOW && h % 5 == 0) {
        float cx = (float)(sx + 9 + (int)((h >> 8) % 14)), cy = (float)(sy + 9 + (int)((h >> 12) % 14));
        cx += sinf(t * 0.8f + (float)(h % 7)) * 0.6f;
        float rad = 4.5f + (h >> 16) % 3, notch = ((h >> 20) % 628) / 100.0f;
        CF p;
        light_at_px(r, cx + v->camx, cy + v->camy, &p);
        for (int j = -7; j <= 7; j++)
            for (int i = -7; i <= 7; i++) {
                float dx = i + 0.5f, dy = (j + 0.5f) * 1.25f, dd = sqrtf(dx * dx + dy * dy);
                if (dd > rad) continue;
                float ang = atan2f(dy, dx) - notch;
                ang = fabsf(atan2f(sinf(ang), cosf(ang)));
                if (ang < 0.35f && dd > 1.2f) continue;
                int x = (int)cx + i, y = (int)cy + j;
                if ((unsigned)x >= (unsigned)cv->w || (unsigned)y >= (unsigned)cv->h) continue;
                uint32_t a = dd > rad - 1.2f ? 0x2a4a1e : (i + j) % 5 == 0 ? 0x5a8a3a : 0x46772e;
                uint32_t lit = lightc(a, p.r, p.g, p.b);
                cv->px[(size_t)y * cv->w + x] = v->fog ? fogmix(lit, memtone(a), p.v, p.s) : lit;
            }
    }
}

/* long wavering streaks of firelight on the water under each flame */
static void reflect_one(Canvas *cv, const View *v, float fx, float fy, float gy, uint32_t col, float str, float width)
{
    float hgt = gy - fy;
    if (hgt < 2) hgt = 2;
    float cy = gy + hgt, len = hgt * 1.1f + 10;
    int x0 = (int)(fx - width * 4) - v->camx, x1 = (int)(fx + width * 4) - v->camx;
    int y0 = (int)gy - v->camy, y1 = (int)(cy + len * 1.6f) - v->camy;
    float t = v->time;
    for (int y = y0; y <= y1; y++) {
        if ((unsigned)y >= (unsigned)cv->h) continue;
        float wy = (float)(y + v->camy);
        float along = (wy - cy) / len;
        float fall = expf(-along * along);
        if (fall < 0.02f) continue;
        float wob = sinf(wy * 0.55f + t * 4.6f) * (1.0f + (wy - gy) * 0.05f);
        float band = 0.45f + 0.55f * fabsf(sinf(wy * 0.9f - t * 5.3f + sinf(wy * 0.21f + t)));
        float sw = width * (0.8f + (wy - gy) * 0.02f);
        for (int x = x0; x <= x1; x++) {
            if ((unsigned)x >= (unsigned)cv->w || !water_px(v, x, y)) continue;
            int tx = (x + v->camx) >> 5, ty = (y + v->camy) >> 5;
            float vis = v->fog ? v->visf[ty][tx] : 1;
            if (vis <= 0.02f) continue;
            float dx = (x + v->camx - fx - wob) / sw;
            float k = expf(-dx * dx) * fall * band * str * vis;
            if (k < 0.01f) continue;
            uint32_t *px = &cv->px[(size_t)y * cv->w + x];
            *px = add_sat(*px, (int)(CR(col) * k), (int)(CG(col) * k), (int)(CB(col) * k));
        }
    }
}

static void torch_tip(const Renderer *r, const Mob *m, float *x, float *y);

/* x on the player's sprite (canvas offset from its left) of a point dx past the
 * lantern hand, mirrored when it faces left */
static int hand_x(const Renderer *r, const Mob *m, int dx)
{
    int x = r->hand_x + dx;
    return m->flip ? TILE - 1 - x : x;
}
/* outward from the body, on the lantern's side */
static int hand_out(const Renderer *r) { return r->hand_x < TILE / 2 ? -1 : 1; }

static void draw_reflections(Renderer *r, Canvas *cv, const View *v)
{
    (void)r;
    const Level *lv = v->lv;
    for (int i = 0; i < lv->nlights; i++) {
        const Light *l = &lv->lights[i];
        if (!l->lit) continue;
        float f = light_flicker(l, v->time);
        float fx = l->fx * TILE + 16.0f;
        if (l->kind == L_SCONCE)
            reflect_one(cv, v, fx, l->fy * TILE + TILE - 23.0f, (l->fy + 1) * TILE + 2.0f, 0xffa850, 0.95f * f, 2.4f);
        else if (l->kind == L_FLOORTORCH)
            reflect_one(cv, v, fx, l->fy * TILE + 10.0f, l->fy * TILE + 24.0f, 0xffa850, 0.8f * f, 2.2f);
        else if (l->kind == L_BRAZIER)
            reflect_one(cv, v, fx, l->fy * TILE + TILE - 23.0f, (l->fy + 1) * TILE + 2.0f, 0xffa850, 1.0f * f, 3.0f);
        else if (l->kind == L_CRYSTAL)
            reflect_one(cv, v, fx, l->fy * TILE + 14.0f, (l->fy + 1) * TILE + 2.0f, 0x58a8ff, 0.55f * f, 3.0f);
    }
    if (v->player && v->torch) {
        float x, y;
        torch_tip(r, v->player, &x, &y);
        reflect_one(cv, v, x, y, v->player->ry * TILE + 30, 0xffa850, 0.9f, 2.2f);
    }
    if (v->projlight) reflect_one(cv, v, v->projx, v->projy, v->projy + 18, 0xffa850, 0.7f, 2.0f);
}

static void add_ripple(Renderer *r, float x, float y, float str, float life)
{
    int best = 0;
    for (int i = 0; i < MAX_RIPPLE; i++) {
        if (r->rip[i].age >= r->rip[i].life) { best = i; break; }
        if (r->rip[i].age / r->rip[i].life > r->rip[best].age / r->rip[best].life) best = i;
    }
    r->rip[best] = (Ripple){ x, y, 0, life, str };
}

static void draw_ripples(Renderer *r, Canvas *cv, const View *v, float dt)
{
    for (int k = 0; k < MAX_RIPPLE; k++) {
        Ripple *rp = &r->rip[k];
        if (rp->age >= rp->life) continue;
        rp->age += dt;
        float a = rp->age / rp->life;
        if (a >= 1) continue;
        float rad = 2 + a * 22 * (0.6f + rp->str * 0.5f), alpha = rp->str * (1 - a) * (1 - a);
        CF c;
        light_at_px(r, rp->x, rp->y, &c);
        float lv = (c.r + c.g + c.b) / 3 + 0.06f;
        int x0 = (int)(rp->x - rad - 2) - v->camx, x1 = (int)(rp->x + rad + 2) - v->camx;
        int y0 = (int)(rp->y - rad * 0.55f - 2) - v->camy, y1 = (int)(rp->y + rad * 0.55f + 2) - v->camy;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                if ((unsigned)x >= (unsigned)cv->w || (unsigned)y >= (unsigned)cv->h || !water_px(v, x, y)) continue;
                float dx = x + v->camx + 0.5f - rp->x, dy = (y + v->camy + 0.5f - rp->y) / 0.55f;
                float e = fabsf(sqrtf(dx * dx + dy * dy) - rad);
                if (e > 1.3f) continue;
                int tx = (x + v->camx) >> 5, ty = (y + v->camy) >> 5;
                float vis = v->fog ? v->visf[ty][tx] : 1;
                float s = (1.3f - e) / 1.3f * alpha * vis * 120 * lv;
                uint32_t *px = &cv->px[(size_t)y * cv->w + x];
                *px = add_sat(*px, (int)(s * (c.r + 0.2f)), (int)(s * (c.g + 0.3f)), (int)(s * (c.b + 0.35f)));
            }
    }
    /* anything standing in water sends out slow rings */
    for (int i = 0; i <= MAX_MOBS; i++) {
        const Mob *m = i < MAX_MOBS ? &v->mobs[i] : v->player;
        if (!m || !m->alive || m->kind == MK_SHADE || m->kind == MK_HOLLOW || m->kind == MK_BAT) continue;
        int tx = (int)lroundf(m->rx), ty = (int)lroundf(m->ry);
        if (!tile_water(tile_at(v->lv, tx, ty))) { r->rip_acc[i] = 0; continue; }
        if (v->fog && !(v->lv->f[ty][tx] & F_VIS)) continue;
        bool hid = game_mob_hidden(m);      /* an eel under the surface: now and then a ring gives it away */
        r->rip_acc[i] += dt * (hid ? (m->at < 1 ? 1.5f : 0.15f) : m->at < 1 ? 3.0f : 0.7f);
        if (r->rip_acc[i] >= 1) {
            r->rip_acc[i] = 0;
            add_ripple(r, m->rx * TILE + 16, m->ry * TILE + 27, m->at < 1 ? 0.7f : 0.45f, 1.6f);
        }
    }
}

static void draw_ground(Renderer *r, Canvas *cv, const View *v)
{
    const Level *lv = v->lv;
    int tx0 = (int)floorf((float)v->camx / TILE), ty0 = (int)floorf((float)v->camy / TILE);
    int tx1 = (v->camx + cv->w) / TILE, ty1 = (v->camy + cv->h) / TILE;
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++) {
            if (!in_map(tx, ty)) continue;
            Tile t = (Tile)lv->t[ty][tx];
            if (t == T_ROCK || t == T_WALL) continue;
            if (v->fog && !(lv->f[ty][tx] & F_SEEN)) continue;
            int sx = tx * TILE - v->camx, sy = ty * TILE - v->camy;
            if (t == T_CHASM) { draw_chasm(r, cv, v, tx, ty, sx, sy); continue; }
            if (tile_water(t)) { draw_water(r, cv, v, tx, ty, sx, sy); continue; }
            uint32_t h = hash2(tx, ty, lv->seed);
            const Image *im = &r->t.floor[h % N_FLOOR];
            const Image *over = NULL;
            if (t == T_STAIRS_DOWN) im = &r->t.stairs_dn;
            else if (t == T_STAIRS_UP) im = &r->t.stairs_up;
            else if (t == T_DOOR_OPEN)
                im = tile_solid(tile_at(lv, tx, ty - 1)) && tile_solid(tile_at(lv, tx, ty + 1)) ? &r->t.door_open_side
                                                                                              : &r->t.door_open;
            else if (t == T_RUBBLE) over = &r->t.rubble[(h >> 8) % 3];
            int ao = 0;
            if (tile_solid(tile_at(lv, tx, ty - 1)) || tile_at(lv, tx, ty - 1) == T_DOOR) ao |= AO_N;
            if (tile_solid(tile_at(lv, tx - 1, ty))) ao |= AO_W;
            if (tile_solid(tile_at(lv, tx + 1, ty))) ao |= AO_E;
            if (tile_solid(tile_at(lv, tx - 1, ty - 1))) ao |= AO_NW;
            if (tile_solid(tile_at(lv, tx + 1, ty - 1))) ao |= AO_NE;
            int edge = 0;
            if (tile_at(lv, tx, ty - 1) == T_CHASM) edge |= 1;
            if (tile_at(lv, tx - 1, ty) == T_CHASM) edge |= 2;
            if (tile_at(lv, tx + 1, ty) == T_CHASM) edge |= 4;
            if (tile_at(lv, tx, ty + 1) == T_CHASM) edge |= 8;
            draw_lit(cv, sx, sy, im, TILE, TILE, &r->corner[ty][tx], &r->corner[ty][tx + 1],
                     &r->corner[ty + 1][tx], &r->corner[ty + 1][tx + 1], 1, 1, ao, v->fog, over, edge);
            if (lv->scorch[ty][tx]) draw_scorch(r, cv, v, tx, ty, sx, sy);
            int rk = lv->room[ty][tx] >= 0 ? lv->rooms[lv->room[ty][tx]].kind : RK_PLAIN;
            if (rk != RK_PLAIN && t != T_DOOR_OPEN) draw_room_floor(r, cv, v, tx, ty, sx, sy, rk);
            if (lv->salt[ty][tx]) draw_salt(r, cv, v, tx, ty, sx, sy);
            if (lv->trap[ty][tx] && (lv->f[ty][tx] & F_TRAP)) draw_trap(r, cv, v, tx, ty, sx, sy);
        }
}

/* How far a wall or door is raised. A "front" wall, with open ground to its north,
 * is cut down to a low lip so it never hides that row (people, rats, items). */
static int wall_raise(const Level *lv, int tx, int ty)
{
    return tile_solid(tile_at(lv, tx, ty - 1)) ? WALL_R : LOW_R;
}

static bool is_wall_at(const Level *lv, int tx, int ty) { return tile_at(lv, tx, ty) == T_WALL; }

/* rows y0.. of an image, as an image of its own */
static Image image_rows(const Image *im, int y0, int h)
{
    return (Image){ im->w, h, im->px + (size_t)y0 * im->w };
}

static void draw_wall(Renderer *r, Canvas *cv, const View *v, int tx, int ty)
{
    const Level *lv = v->lv;
    int sx = tx * TILE - v->camx, sy = ty * TILE - v->camy;
    uint32_t h = hash2(tx, ty, lv->seed + 1);
    bool nN = !tile_solid(tile_at(lv, tx, ty - 1)), nS = !tile_solid(tile_at(lv, tx, ty + 1));
    bool nW = !tile_solid(tile_at(lv, tx - 1, ty)), nE = !tile_solid(tile_at(lv, tx + 1, ty));
    int rz = wall_raise(lv, tx, ty);
    /* a step down to a cut-down neighbour reads as an edge; a step up as a shadow */
    int rW = is_wall_at(lv, tx - 1, ty) ? wall_raise(lv, tx - 1, ty) : rz;
    int rE = is_wall_at(lv, tx + 1, ty) ? wall_raise(lv, tx + 1, ty) : rz;
    bool eW = nW || rW < rz, eE = nE || rE < rz;
    bool shW = rW > rz, shE = rE > rz;
    const CF *c00 = &r->corner[ty][tx], *c10 = &r->corner[ty][tx + 1];
    const CF *c01 = &r->corner[ty + 1][tx], *c11 = &r->corner[ty + 1][tx + 1];

    /* the top, with a bevelled rim along every open side */
    static Image top;
    if (!top.px) image_alloc(&top, TILE, TILE);
    const Image *src = &r->t.wtop[h % N_WALL];
    for (int j = 0; j < TILE; j++)
        for (int i = 0; i < TILE; i++) {
            uint32_t c = src->px[j * TILE + i];
            int d = 0;
            if (nN) d = j == 0 ? -40 : j < 3 ? 30 : d;
            if (nS && j >= TILE - 3) d = j == TILE - 1 ? 40 : 30;
            if (eW) d = i == 0 ? -40 : i < 3 && d >= 0 ? 26 : d;
            if (eE) d = i == TILE - 1 ? -40 : i > TILE - 4 && d >= 0 ? 22 : d;
            if (shW && i < 4) d -= (4 - i) * 12;
            if (shE && i > TILE - 5) d -= (i - (TILE - 5)) * 12;
            top.px[j * TILE + i] = d ? (0xff000000u | add_sat(c, d, d, d * 9 / 10)) : c;
        }
    draw_lit(cv, sx, sy - rz, &top, TILE, TILE, c00, c10, c01, c11, 0.62f, 0.78f, 0, v->fog, NULL, 0);

    if (nS) {
        /* the front face takes the light of the floor in front of it; a cut-down
         * wall shows only the foot of it */
        CF a = *c01, b = *c11;
        Image face = image_rows(&r->t.wface[(h >> 4) % N_WALL], WALL_R - rz, rz);
        draw_lit(cv, sx, sy + TILE - rz, &face, TILE, rz, &a, &b, &a, &b, 0.7f, 1.0f, 0, v->fog, NULL, 0);
    }
}

static void draw_door(Renderer *r, Canvas *cv, const View *v, int tx, int ty)
{
    const Level *lv = v->lv;
    int sx = tx * TILE - v->camx, sy = ty * TILE - v->camy;
    const CF *c00 = &r->corner[ty][tx], *c10 = &r->corner[ty][tx + 1];
    const CF *c01 = &r->corner[ty + 1][tx], *c11 = &r->corner[ty + 1][tx + 1];
    if (tile_solid(tile_at(lv, tx, ty - 1)) && tile_solid(tile_at(lv, tx, ty + 1))) {
        /* in a north-south wall: seen edge on, a little brighter so it stands out of the stone */
        draw_lit(cv, sx, sy - WALL_R, &r->t.door_side, TILE, TILE + WALL_R, c00, c10, c01, c11, 0.95f, 1.1f, 0,
                 v->fog, NULL, 0);
        return;
    }
    int rz = wall_raise(lv, tx, ty);
    const Image *im = rz == WALL_R ? &r->t.door : &r->t.door_low;
    draw_lit(cv, sx, sy - rz, im, TILE, TILE + rz, c00, c10, c01, c11, 0.8f, 1.0f, 0, v->fog, NULL, 0);
}

static void light_at_px(const Renderer *r, float wx, float wy, CF *o)
{
    float fx = wx / TILE, fy = wy / TILE;
    int cx = clampi((int)floorf(fx), 0, CW - 2), cy = clampi((int)floorf(fy), 0, CH - 2);
    float u = clampf(fx - cx, 0, 1), v = clampf(fy - cy, 0, 1);
    CF a, b;
    cf_lerp(&a, &r->corner[cy][cx], &r->corner[cy][cx + 1], u);
    cf_lerp(&b, &r->corner[cy + 1][cx], &r->corner[cy + 1][cx + 1], u);
    cf_lerp(o, &a, &b, v);
}

/* how far the Heartflame has come back: its light grows with each kindling (see hearth_light in game.c) */
static int heart_stage(const Light *l) { return !l->lit ? 0 : l->radius < 4.5f ? 1 : l->radius < 7.5f ? 2 : 3; }

/* the great hearth: a round stone plinth wider than its tile, its pit of ash around a dark heart-stone
 * that glows hotter as the flame comes back */
static void draw_hearth(Canvas *cv, const View *v, const Light *l, int sx, int sy, const CF *c, float vis, float seen)
{
    int st = heart_stage(l);
    float cx = sx + 16.0f, cy = sy + 18.0f;
    const float rx = 31, ry = 14, pr = 20, pry = 8.0f, rim = 10;
    float heat = st == 0 ? 0 : st == 1 ? 0.45f : st == 2 ? 0.75f : 1.0f;
    float pulse = 0.85f + 0.15f * sinf(v->time * 3.1f + l->phase);
    float lr = c->r < 1.2f ? c->r : 1.2f, lg = c->g < 1.0f ? c->g : 1.0f, lb = c->b < 0.9f ? c->b : 0.9f;
    if (lr < 0.34f) lr = 0.34f;
    if (lg < 0.33f) lg = 0.33f;
    if (lb < 0.36f) lb = 0.36f;
    for (int y = (int)(cy - ry - 1); y <= (int)(cy + ry + rim + 1); y++) {
        if ((unsigned)y >= (unsigned)cv->h) continue;
        for (int x = (int)(cx - rx - 1); x <= (int)(cx + rx + 1); x++) {
            if ((unsigned)x >= (unsigned)cv->w) continue;
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float top = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry);
            float pit = (dx * dx) / (pr * pr) + (dy * dy) / (pry * pry);
            uint32_t base, glow = 0;
            int ga = 0;
            uint32_t h = hash2(x - (int)cx + 64, y - (int)cy + 64, 4242);
            if (pit <= 1) {
                /* the pit: ash, and the heart-stone at its middle */
                float lump = (dx * dx) / 90.0f + ((dy + 2.0f) * (dy + 2.0f)) / 22.0f;
                if (lump <= 1) {
                    base = (h & 15) == 0 && st == 0 ? 0xb0c8e0                      /* frost on the dead stone */
                         : (h & 7) == 0 ? 0x6a7484 : lump < 0.4f && dy < -2 ? 0x5e6878 : 0x404856;
                    /* cracks through the stone that glow as it wakes */
                    bool crack = fabsf(sinf(dx * 0.9f + dy * 1.7f) + 0.6f * sinf(dx * 1.9f - dy * 0.7f)) < 0.22f;
                    if (crack && st > 0) { glow = st >= 2 ? 0xffd070 : 0xff7a28; ga = (int)(255 * pulse); }
                    else if (crack) base = 0x262c36;
                } else {
                    base = (h & 3) == 0 ? 0x3a3634 : (h & 3) == 1 ? 0x2c2a2a : 0x242222;
                    if (st > 0) {
                        /* the ash lights up from beneath */
                        float k = heat * (1 - pit * 0.7f) * pulse;
                        glow = mix(0x601808, 0xff8a30, (int)(clampf(k, 0, 1) * 255));
                        ga = (int)(clampf(k * 1.4f, 0, 1) * (h & 1 ? 255 : 180));
                    }
                }
                if (pit > 0.8f) base = mix(base, 0x141212, 140);     /* the inner lip throws a shadow */
            } else if (top <= 1) {
                /* the rim: dressed blocks set in a ring */
                float ang = atan2f(dy * rx / ry, dx);
                float seg = fmodf((ang + 3.14159f) * 12 / 6.28318f, 1.0f);
                base = seg < 0.07f ? 0x3e3a36 : (h & 7) == 0 ? 0x7a746a : (h & 7) == 1 ? 0x5e5850 : 0x6c665c;
                if (top > 0.86f) base = mix(base, 0x8a8478, 90);            /* the worn outer edge catches light */
            } else {
                /* the side of the plinth, below the rim */
                if (fabsf(dx) > rx) continue;
                float edge = cy + ry * sqrtf(1 - (dx * dx) / (rx * rx));
                if (dy < 0 || y + 0.5f > edge + rim) continue;
                bool seam = fmodf(dx + 40 + ((int)((y + 0.5f - edge) / 5.0f) & 1) * 5.5f, 11.0f) < 1 || fabsf(y + 0.5f - edge - rim / 2) < 0.5f;
                base = seam ? 0x2c2824 : (h & 3) == 0 ? 0x4c4640 : 0x423c36;
                if (y + 0.5f > edge + rim - 1.5f) base = 0x24201c;
            }
            uint32_t lit = lightc(base, lr, lg, lb);
            if (ga) lit = mix(lit, glow, ga);
            uint32_t *px = &cv->px[(size_t)y * cv->w + x];
            *px = v->fog ? fogmix(lit, memtone(base), vis, seen) : lit;
        }
    }
}

static void draw_fixture(Renderer *r, Canvas *cv, const View *v, const Light *l)
{
    const Level *lv = v->lv;
    if (v->fog && !(lv->f[l->fy][l->fx] & F_SEEN)) return;
    int sx = l->fx * TILE - v->camx, sy = l->fy * TILE - v->camy;
    CF c;
    light_at_px(r, l->fx * TILE + 16.0f, (l->fy + 1) * TILE + 1.0f, &c);
    float vis = v->fog ? c.v : 1, seen = v->fog ? c.s : 1;
    if (l->kind == L_SCONCE) {
        const Image *im = &r->t.sconce;
        float k = l->lit ? 1.0f : 0.0f;
        for (int j = 0; j < im->h; j++)
            for (int i = 0; i < im->w; i++) {
                uint32_t a = im->px[j * im->w + i];
                if (!CA(a)) continue;
                int x = sx + 8 + i, y = sy + TILE - im->h + j;
                if ((unsigned)x >= (unsigned)cv->w || (unsigned)y >= (unsigned)cv->h) continue;
                float lr = c.r + k, lg = c.g + k * 0.6f, lb = c.b + k * 0.3f;
                /* a cold sconce still catches a glint, so you can find it in a dark room */
                if (lr < 0.42f) lr = 0.42f;
                if (lg < 0.40f) lg = 0.40f;
                if (lb < 0.38f) lb = 0.38f;
                uint32_t lit = lightc(a & 0xffffff, lr, lg, lb);
                cv->px[(size_t)y * cv->w + x] = v->fog ? fogmix(lit, memtone(a), vis, seen) : lit;
            }
    } else if (l->kind == L_BRAZIER) {
        const Image *im = &r->t.brazier, *gl = &r->t.brazier_glow;
        for (int j = 0; j < im->h; j++)
            for (int i = 0; i < im->w; i++) {
                uint32_t a = im->px[j * im->w + i];
                if (!CA(a)) continue;
                int x = sx + 2 + i, y = sy + TILE - im->h - 2 + j;
                if ((unsigned)x >= (unsigned)cv->w || (unsigned)y >= (unsigned)cv->h) continue;
                uint32_t gc = gl->px[j * gl->w + i];
                /* the iron takes the room's light, but no more than full: it must stay dark against the coals */
                float lr = c.r < 1.1f ? c.r : 1.1f, lg = c.g < 0.9f ? c.g : 0.9f, lb = c.b < 0.75f ? c.b : 0.75f;
                if (lr < 0.35f) lr = 0.35f;
                if (lg < 0.3f) lg = 0.3f;
                if (lb < 0.28f) lb = 0.28f;
                uint32_t lit = CA(gc) ? (gc & 0xffffff) : lightc(a & 0xffffff, lr, lg, lb);
                cv->px[(size_t)y * cv->w + x] = v->fog ? fogmix(lit, memtone(a), vis, seen) : lit;
            }
    } else if (l->kind == L_HEART) {
        CF hc;
        light_at_px(r, l->fx * TILE + 16.0f, l->fy * TILE + 30.0f, &hc);   /* lit from the floor in front of it */
        draw_hearth(cv, v, l, sx, sy, &hc, v->fog ? hc.v : 1, v->fog ? hc.s : 1);
    } else if (l->kind == L_FLOORTORCH) {
        const Image *im = &r->t.item[IT_TORCH];
        for (int j = 0; j < im->h; j++)
            for (int i = 0; i < im->w; i++) {
                uint32_t a = im->px[j * im->w + i];
                if (!CA(a)) continue;
                int x = sx + i, y = sy + j;
                if ((unsigned)x >= (unsigned)cv->w || (unsigned)y >= (unsigned)cv->h) continue;
                uint32_t lit = lightc(a & 0xffffff, c.r + 0.3f, c.g + 0.2f, c.b + 0.1f);
                cv->px[(size_t)y * cv->w + x] = v->fog ? fogmix(lit, memtone(a), vis, seen) : lit;
            }
    } else if (l->kind == L_MOSS) {
        /* luminous specks scattered over the floor tile */
        float pulse = light_flicker(l, v->time);
        for (int k = 0; k < 22; k++) {
            uint32_t h = hash2(l->fx * 31 + k, l->fy * 17 + k, 77);
            int x = sx + 3 + (int)(h % 26), y = sy + 4 + (int)((h >> 8) % 25);
            if ((unsigned)x >= (unsigned)cv->w - 1 || (unsigned)y >= (unsigned)cv->h - 1) continue;
            uint32_t c = (h >> 16) & 1 ? 0x7dffd0 : 0x48d8a0;
            int a = (int)(255 * pulse * (v->fog ? (seen > 0 ? (vis > 0.4f ? vis : 0.4f) : 0) : 1));
            blend_px(&cv->px[(size_t)y * cv->w + x], c, a);
            if ((h >> 20) & 1) blend_px(&cv->px[(size_t)y * cv->w + x + 1], c, a / 2);
        }
    } else if (l->kind == L_CRYSTAL) {
        const Image *im = &r->t.crystal;
        float pulse = l->lit ? light_flicker(l, v->time) : 0.3f;
        for (int j = 0; j < im->h; j++)
            for (int i = 0; i < im->w; i++) {
                uint32_t a = im->px[j * im->w + i];
                if (!CA(a)) continue;
                int x = sx + 4 + i, y = sy + TILE - im->h + 2 + j;
                if ((unsigned)x >= (unsigned)cv->w || (unsigned)y >= (unsigned)cv->h) continue;
                uint32_t lit = lightc(a & 0xffffff, pulse, pulse, pulse);
                float vv = v->fog ? (vis > 0.5f ? vis : 0.5f) : 1;
                cv->px[(size_t)y * cv->w + x] = v->fog ? fogmix(lit, memtone(a), vv, seen) : lit;
            }
    }
}

/* a shade: a hooded knot of darkness with pale eyes. In the dark only the
 * eyes show; in light it is a hole in the light. The Hollow is the same, vast. */
static void draw_shade(Canvas *cv, const View *v, const Mob *m, float ox, float oy, float alpha, bool body)
{
    bool big = m->kind == MK_HOLLOW;
    float sc = big ? 2.2f : 1.0f;
    float t = v->time * (big ? 0.7f : 1.0f) + m->idle * 3;
    float bob = sinf(t * 1.7f) * 2.0f * sc;
    float cx = m->rx * TILE + ox + 16 - v->camx, cy = m->ry * TILE + oy + 16 + bob - (sc - 1) * 13 - v->camy;
    float sway = sinf(t * 1.1f) * 2.5f * sc;
    bool burn = m->flash > 0;
    if (body) {
        /* blobs: hood, shoulders, a body that thins into a swaying tail */
        float bx[7], by[7], br[7];
        static const float BY[7] = { -10, -4, -4, 2, 7, 11, 14 }, BR[7] = { 8.5f, 7, 7, 7.5f, 5.8f, 4.2f, 2.8f };
        static const float BX[7] = { 0, -5.5f, 5.5f, 0, 0, 0, 0 };
        /* it drinks the light around it */
        int dr = (int)(22 * sc);
        for (int j = -dr; j <= dr; j++)
            for (int i = -dr; i <= dr; i++) {
                int x = (int)cx + i, y = (int)cy + j;
                if ((unsigned)x >= (unsigned)cv->w || (unsigned)y >= (unsigned)cv->h) continue;
                float d = (i * i + j * j) / (float)(dr * dr);
                if (d >= 1) continue;
                blend_px(&cv->px[(size_t)y * cv->w + x], 0x020104, (int)((big ? 140 : 95) * (1 - d) * (1 - d) * alpha));
            }
        for (int k = 0; k < 7; k++) {
            float w = k >= 3 ? (k - 2) / 4.0f : 0;
            bx[k] = BX[k] * sc + sway * w * w * 1.6f + sinf(t * 2.3f + k * 1.7f) * 0.8f * sc;
            by[k] = BY[k] * sc + cosf(t * 1.9f + k) * 0.6f * sc;
            br[k] = BR[k] * sc * (1 + 0.08f * sinf(t * 3.1f + k * 2.1f));
        }
        int x0 = (int)(cx - 16 * sc), y0 = (int)(cy - 20 * sc), bw = (int)(32 * sc), bh = (int)(38 * sc);
        for (int j = 0; j < bh; j++) {
            int y = y0 + j;
            if ((unsigned)y >= (unsigned)cv->h) continue;
            for (int i = 0; i < bw; i++) {
                int x = x0 + i;
                if ((unsigned)x >= (unsigned)cv->w) continue;
                float fx = x - cx + 0.5f, fy = y - cy + 0.5f, dens = 0;
                for (int k = 0; k < 7; k++) {
                    float dx = fx - bx[k], dy = fy - by[k];
                    float q = 1 - (dx * dx + dy * dy) / (br[k] * br[k]);
                    if (q > 0) dens += q * q;
                }
                if (dens <= 0.02f) continue;
                /* churning edge */
                float n = sinf(fx * 0.9f / sc + t * 2.7f) * sinf(fy * 0.8f / sc - t * 2.1f);
                dens = dens * (0.85f + 0.3f * n) - 0.05f;
                if (dens <= 0) continue;
                float a = clampf(dens * 1.6f, 0, 0.94f) * alpha;
                uint32_t rim = burn ? mix(0x401c30, 0xff7020, (int)(m->flash * 256)) : big ? 0x1e2a44 : 0x2c2140;
                uint32_t col = mix(rim, 0x040306, (int)(clampf(dens * 1.3f, 0, 1) * 256));
                blend_px(&cv->px[(size_t)y * cv->w + x], col, (int)(a * 255));
            }
        }
        /* wisps curling off the hood */
        int nw = big ? 14 : 6;
        for (int k = 0; k < nw; k++) {
            float ph = fmodf(t * 0.6f + k / (float)nw, 1.0f);
            float wx = cx + sinf(k * 2.4f + t * 1.3f) * (3 + ph * 6) * sc, wy = cy - (15 + ph * 14) * sc;
            float a = (1 - ph) * 0.7f * alpha;
            cv_disc(cv, wx, wy, 1.6f * sc * (1 - ph * 0.5f), burn ? 0x60301c : 0x0a0710, (int)(a * 255));
        }
    }
    /* eyes, blinking now and then */
    if (fmodf(t * 0.7f, 4.3f) < 0.12f) return;
    float look = m->flip ? -1.5f * sc : 1.5f * sc;
    float ex = cx + look, ey = cy - 10 * sc;
    uint32_t ec = burn ? 0xffd090 : big ? 0xf0f8ff : 0xd4ecff;
    cv_glow(cv, (int)ex, (int)ey, (int)(9 * sc * (big ? 1.4f : 1)), burn ? 0xff8040 : 0x6aa8ff, big ? 0.36f : 0.28f);
    int es = big ? 3 : 2;
    for (int e = -1; e <= 1; e += 2) {
        int x = (int)(ex + e * 3.0f * sc) - (es - 2), y = (int)ey;
        for (int j = 0; j < es; j++)
            for (int i = 0; i < es; i++)
                if ((unsigned)(x + i) < (unsigned)cv->w && (unsigned)(y + j) < (unsigned)cv->h)
                    cv->px[(size_t)(y + j) * cv->w + x + i] = j == es - 1 ? mix(ec, 0x405870, 90) : ec;
    }
}

static void draw_mob(Renderer *r, Canvas *cv, const View *v, const Mob *m, bool is_player)
{
    const Level *lv = v->lv;
    float alpha = 1.0f;
    if (m->kind == MK_SHADE || m->kind == MK_HOLLOW) {
        bool body = !v->fog || (lv->f[m->y][m->x] & F_VIS);
        if (v->fog && !body && !(lv->f[m->y][m->x] & F_LOS)) return;
        float ox = 0, oy = 0;
        if (m->bump > 0) {
            float k = sinf(m->bump * 3.14159f) * 7;
            float len = sqrtf((float)(m->bdx * m->bdx + m->bdy * m->bdy));
            if (len > 0) { ox = m->bdx / len * k; oy = m->bdy / len * k; }
        }
        draw_shade(cv, v, m, ox, oy, v->fog && body ? fmaxf(v->visf[m->y][m->x], 0.3f) : 1.0f, body);
        return;
    }
    if (game_mob_hidden(m)) return;        /* under the water: only its ripples show */
    if (v->fog && !is_player) {
        if (!(lv->f[m->y][m->x] & F_VIS)) return;
        alpha = v->visf[m->y][m->x];
    }
    if (alpha <= 0.02f) return;
    float ox = 0, oy = 0;
    if (m->bump > 0) {
        float k = sinf(m->bump * 3.14159f) * 7;
        float len = sqrtf((float)(m->bdx * m->bdx + m->bdy * m->bdy));
        if (len > 0) { ox = m->bdx / len * k; oy = m->bdy / len * k; }
    }
    float breathe = (fmodf(v->time + m->idle, 1.6f) < 0.8f) ? 0 : 1;
    if (m->asleep) breathe = (fmodf(v->time * 0.5f + m->idle, 2.0f) < 1.0f) ? 0 : 1;
    bool bat = m->kind == MK_BAT;
    if (bat) breathe = m->asleep ? breathe : sinf(v->time * 9 + m->idle * 4) * 2.0f;
    int px = (int)lroundf(m->rx * TILE + ox) - v->camx;
    int py = (int)lroundf(m->ry * TILE + oy - 3 + breathe) - v->camy;
    int lift = bat && !m->asleep ? 9 : 0;         /* a bat on the wing, its shadow on the ground below */
    /* an ogre stands taller than a tile */
    int W = m->kind == MK_OGRE ? 42 : TILE;
    CF c;
    light_at_px(r, m->rx * TILE + 16, m->ry * TILE + 20, &c);
    float floor_l = is_player ? 0.32f : 0.12f;
    float lr = c.r < floor_l ? floor_l : c.r, lg = c.g < floor_l ? floor_l : c.g, lb = c.b < floor_l * 1.3f ? floor_l * 1.3f : c.b;
    /* standing in water: the legs (or most of it) are under the surface */
    Tile wt = tile_at(lv, (int)lroundf(m->rx), (int)lroundf(m->ry));
    int sub = bat ? 0 : wt == T_DEEP ? 14 : wt == T_SHALLOW ? 5 : 0;
    if (m->kind == MK_EEL) sub = 4;               /* rearing up out of its pool */
    int wl = (int)lroundf(m->ry * TILE) + 29 - sub - v->camy;     /* waterline, canvas y */
    if (sub && m->kind != MK_EEL) py += 2 + sub / 4;              /* sinks a little */
    /* shadow (none for a bat over a chasm) */
    int shw = W > TILE ? 14 : bat ? 7 : 10, sha = bat ? 70 : 120;
    for (int j = -3; j <= 3 && !sub && !(bat && wt == T_CHASM); j++)
        for (int i = -shw; i <= shw; i++) {
            float d = (float)(i * i) / (float)(shw * shw) + (j * j) / 9.0f;
            if (d > 1) continue;
            int x = px + 16 + i, y = py + 3 - (int)breathe + 29 + j;
            if ((unsigned)x < (unsigned)cv->w && (unsigned)y < (unsigned)cv->h)
                blend_px(&cv->px[(size_t)y * cv->w + x], 0, (int)(sha * (1 - d) * alpha));
        }
    py -= lift;
    px -= (W - TILE) / 2;
    py -= W - TILE;
    const Image *im = &r->t.mob[m->kind], *gl = &r->t.mob_glow[m->kind];
    int fl = m->flash > 0 ? (int)(m->flash * 230) : 0;
    /* a full-resolution sprite to draw over this one once zoomed: note what it covers */
    int hid = 0;
    if (cv == r->rec_cv && r->t.mob_hi[m->kind].px && r->nhic < MAX_HICMD) {
        hid = ++r->nhic;
        r->hic[hid - 1] = (HiCmd){ m->kind, px, py, W, m->flip, m->asleep, sub != 0, wl,
                                   lr * 1.1f, lg * 1.1f, lb * 1.1f, alpha, fl, false, 0, 0, 0, 0 };
    }
    for (int j = 0; j < W; j++) {
        int y = py + j;
        if ((unsigned)y >= (unsigned)cv->h || (sub && y > wl)) continue;
        int sj = j * TILE / W;
        for (int i = 0; i < W; i++) {
            int x = px + i;
            if ((unsigned)x >= (unsigned)cv->w) continue;
            size_t o = (size_t)y * cv->w + x;
            int si = i * TILE / W;
            if (m->flip) si = TILE - 1 - si;
            uint32_t a = im->px[sj * TILE + si];
            if (!CA(a)) {
                /* the big sprite may reach a little past the small one's outline */
                if (hid && !r->own[o]) { r->own[o] = (uint16_t)hid; r->hbg[o] = r->hwr[o] = cv->px[o]; }
                continue;
            }
            uint32_t g = gl->px[sj * TILE + si];
            uint32_t col = CA(g) ? (g & 0xffffff) : lightc(a & 0xffffff, lr * 1.1f, lg * 1.1f, lb * 1.1f);
            if (m->asleep && !CA(g)) col = mix(col, 0x202838, 60);
            if (fl) col = mix(col, 0xffffff, fl);
            if (hid) r->hbg[o] = cv->px[o];
            blend_px(&cv->px[o], col, (int)(255 * alpha));
            if (hid) { r->own[o] = (uint16_t)hid; r->hwr[o] = cv->px[o]; }
        }
    }
    if (sub) {
        /* a bright ring where the body meets the water */
        float lum = clampf((lr + lg + lb) / 3, 0.1f, 1.2f);
        px += (W - TILE) / 2;
        for (int i = -10; i <= 10; i++) {
            int x = px + 16 + i, y = wl + (i * i) / 45;
            if ((unsigned)x >= (unsigned)cv->w || (unsigned)y >= (unsigned)cv->h) continue;
            float e = 1 - fabsf((float)i) / 11;
            blend_px(&cv->px[(size_t)y * cv->w + x], lightc(0xb8e0e8, lr, lg, lb), (int)(150 * e * alpha * lum));
        }
    }
    if (m->daze > 0 && !is_player) {
        /* dazzled: little lights circling its head */
        float hx = px + (W > TILE ? (W - TILE) / 2.0f : 0) + 16, hy = (float)py - 2;
        for (int k = 0; k < 3; k++) {
            float a = v->time * 4.5f + k * 2.094f;
            cv_disc(cv, hx + cosf(a) * 7, hy + sinf(a) * 2.5f, 1.4f, 0xfff0a0, (int)(230 * alpha));
        }
    }
    if (is_player && v->torch) {
        /* a torch raised in the off hand */
        int hx = px + hand_x(r, m, 0), hy = py + r->hand_y;
        int tx = px + hand_x(r, m, 3 * hand_out(r)), ty = hy - 10;
        if (hid) {
            /* drawn sharp by render_hires instead */
            HiCmd *hc = &r->hic[hid - 1];
            hc->stick = true;
            hc->sx0 = hx; hc->sy0 = hy; hc->sx1 = tx; hc->sy1 = ty;
        } else {
            cv_line(cv, hx, hy, tx, ty, 0x5a3a20, 255);
            cv_line(cv, hx + 1, hy, tx + 1, ty, 0x7a5030, 255);
            cv_disc(cv, tx + 0.5f, ty - 0.5f, 2.2f, 0x3a2a1c, 255);
        }
    } else if (is_player) {
        /* the lantern's halo */
        int lx = px + hand_x(r, m, 0), ly = py + r->hand_y;
        cv_glow(cv, lx, ly, 18, 0xffb050, 0.30f);
    }
}

/* the tip of the player's torch, world pixels */
static void torch_tip(const Renderer *r, const Mob *m, float *x, float *y)
{
    float ox = 0, oy = 0;
    if (m->bump > 0) {
        float k = sinf(m->bump * 3.14159f) * 7;
        float len = sqrtf((float)(m->bdx * m->bdx + m->bdy * m->bdy));
        if (len > 0) { ox = m->bdx / len * k; oy = m->bdy / len * k; }
    }
    *x = m->rx * TILE + ox + hand_x(r, m, 3 * hand_out(r)) + 0.5f;
    *y = m->ry * TILE + oy - 3 + (r->hand_y - 10) - 2.0f;
}

static void flame(Canvas *cv, float x, float y, float size, float t, float phase, float vis)
{
    float wob = sinf(t * 9 + phase) * 0.8f * size;
    float f = 0.9f + 0.1f * sinf(t * 13 + phase);
    cv_glow(cv, (int)x, (int)y - 3, (int)(44 * size), 0xff9a40, 0.22f * f * vis);
    cv_glow(cv, (int)x, (int)y - 3, (int)(15 * size), 0xffc070, 0.5f * f * vis);
    cv_disc(cv, x + wob * 0.5f, y - 1.5f * size, 3.2f * size * f, 0xff8a28, (int)(230 * vis));
    cv_disc(cv, x + wob, y - 4.0f * size, 2.4f * size * f, 0xffc050, (int)(240 * vis));
    cv_disc(cv, x + wob * 1.3f, y - 6.5f * size, 1.3f * size * f, 0xffe8a0, (int)(220 * vis));
    cv_disc(cv, x + wob * 0.4f, y - 2.0f * size, 1.3f * size, 0xfff4d0, (int)(255 * vis));
}

/* a flame's body without its halo, for fires made of many flames (their halos would stack up white) */
static void flame_body(Canvas *cv, float x, float y, float size, float t, float phase, float vis)
{
    float wob = sinf(t * 9 + phase) * 0.8f * size;
    float f = 0.9f + 0.1f * sinf(t * 13 + phase);
    cv_disc(cv, x + wob * 0.5f, y - 1.5f * size, 3.2f * size * f, 0xff8a28, (int)(220 * vis));
    cv_disc(cv, x + wob, y - 4.0f * size, 2.4f * size * f, 0xffb848, (int)(230 * vis));
    cv_disc(cv, x + wob * 1.3f, y - 6.5f * size, 1.3f * size * f, 0xffe090, (int)(210 * vis));
    cv_disc(cv, x + wob * 0.4f, y - 2.0f * size, 1.2f * size, 0xfff0c8, (int)(240 * vis));
}

static void spawn_g(Renderer *r, float x, float y, float vx, float vy, float life, uint32_t c, float size, float grav)
{
    for (int i = 0; i < MAX_PART; i++) {
        Particle *p = &r->part[i];
        if (p->life > 0) continue;
        *p = (Particle){ x, y, vx, vy, life, life, size, grav, c };
        return;
    }
}

static void spawn(Renderer *r, float x, float y, float vx, float vy, float life, uint32_t c, float size)
{
    spawn_g(r, x, y, vx, vy, life, c, size, 0);
}

static void draw_effects(Renderer *r, Canvas *cv, const View *v, float dt)
{
    Level *lv = v->lv;
    for (int i = 0; i < lv->nlights; i++) {
        const Light *l = &lv->lights[i];
        if (!l->lit && l->kind != L_HEART) continue;
        float vis = v->fog ? v->visf[l->y][l->x] : 1.0f;
        if (v->fog && (lv->f[l->fy][l->fx] & F_VIS)) vis = vis > 0.6f ? vis : 0.6f;
        if (vis <= 0.02f) continue;
        int sx = l->fx * TILE - v->camx, sy = l->fy * TILE - v->camy;
        if (sx < -80 || sy < -80 || sx > cv->w + 80 || sy > cv->h + 80) continue;
        float f = light_flicker(l, v->time);
        if (l->kind == L_SCONCE) {
            float fx = sx + 16.0f, fy = sy + TILE - 18 - 1.0f;    /* top of the torch head */
            float wob = sinf(v->time * 9 + l->phase) * 0.8f;
            cv_glow(cv, (int)fx, (int)fy - 4, 46, 0xff9a40, 0.22f * f * vis);
            cv_glow(cv, (int)fx, (int)fy - 4, 16, 0xffc070, 0.55f * f * vis);
            cv_disc(cv, fx + wob * 0.5f, fy - 3, 3.4f * f, 0xff8a28, (int)(230 * vis));
            cv_disc(cv, fx + wob, fy - 5.5f, 2.5f * f, 0xffc050, (int)(240 * vis));
            cv_disc(cv, fx + wob * 1.3f, fy - 8.0f, 1.4f * f, 0xffe8a0, (int)(220 * vis));
            cv_disc(cv, fx + wob * 0.4f, fy - 3.5f, 1.4f, 0xfff4d0, (int)(255 * vis));
            if (v->fog && dt > 0) {
                r->ember_acc[i] += dt * 2.2f;
                while (r->ember_acc[i] >= 1) {
                    r->ember_acc[i] -= 1;
                    spawn(r, l->fx * TILE + 16.0f + (rng_f(&r->rng) - 0.5f) * 4, l->fy * TILE + TILE - 27.0f,
                          (rng_f(&r->rng) - 0.5f) * 10, -18 - rng_f(&r->rng) * 22, 0.8f + rng_f(&r->rng) * 0.9f,
                          rng_chance(&r->rng, 50) ? 0xffa040 : 0xffd080, 0.9f);
                }
            }
        } else if (l->kind == L_FLOORTORCH && v->fog) {
            float size = 0.85f * (l->fuel >= 0 && l->fuel < 25 ? 0.5f + 0.5f * l->fuel / 25.0f : 1.0f);
            flame(cv, sx + 23.5f, sy + 15.5f, size, v->time, l->phase, vis);
            if (v->fog && dt > 0) {
                r->ember_acc[i] += dt * 2.6f;
                while (r->ember_acc[i] >= 1) {
                    r->ember_acc[i] -= 1;
                    spawn(r, l->fx * TILE + 23.0f, l->fy * TILE + 12.0f, (rng_f(&r->rng) - 0.5f) * 12,
                          -16 - rng_f(&r->rng) * 20, 0.6f + rng_f(&r->rng) * 0.8f, rng_chance(&r->rng, 50) ? 0xffa040 : 0xffd080, 0.9f);
                }
            }
        } else if (l->kind == L_BRAZIER) {
            float by = sy + TILE - 2 - 26 + 5.0f;       /* top of the coals */
            cv_glow(cv, sx + 16, (int)by - 4, 48, 0xff9a40, 0.12f * f * vis);
            cv_glow(cv, sx + 16, (int)by - 4, 16, 0xffc070, 0.18f * f * vis);
            flame(cv, sx + 11.0f, by + 1, 0.8f, v->time, l->phase, vis);
            flame(cv, sx + 16.5f, by, 1.15f, v->time, l->phase + 1.7f, vis);
            flame(cv, sx + 21.0f, by + 1, 0.75f, v->time, l->phase + 3.1f, vis);
            if (v->fog && dt > 0) {
                r->ember_acc[i] += dt * 3.5f;
                while (r->ember_acc[i] >= 1) {
                    r->ember_acc[i] -= 1;
                    spawn(r, l->fx * TILE + 16.0f + (rng_f(&r->rng) - 0.5f) * 16, l->fy * TILE + TILE - 24.0f,
                          (rng_f(&r->rng) - 0.5f) * 12, -20 - rng_f(&r->rng) * 26, 0.9f + rng_f(&r->rng) * 1.0f,
                          rng_chance(&r->rng, 50) ? 0xffa040 : 0xffd080, 0.9f);
                }
            }
        } else if (l->kind == L_HEART) {
            int st = heart_stage(l);
            float hx = sx + 16.0f, hy = sy + 18.0f;      /* the heart-stone */
            float rate = 0;
            if (st == 0) {
                /* cold: a breath of frost rises off the dead stone now and then */
                if (v->fog && dt > 0 && rng_f(&r->rng) < dt * 1.2f)
                    spawn(r, l->fx * TILE + 10.0f + rng_f(&r->rng) * 12, l->fy * TILE + 16.0f, (rng_f(&r->rng) - 0.5f) * 4,
                          -5 - rng_f(&r->rng) * 4, 1.8f, 0x8ea8c8, 0.8f);
            } else if (st == 1) {
                cv_glow(cv, (int)hx, (int)hy - 2, 40, 0xff5a18, 0.20f * f * vis);
                cv_glow(cv, (int)hx, (int)hy - 1, 14, 0xff9a40, 0.35f * f * vis);
                flame(cv, hx - 1, hy, 0.55f, v->time, l->phase, vis);
                rate = 2.0f;
            } else if (st == 2) {
                cv_glow(cv, (int)hx, (int)hy - 6, 70, 0xff8a30, 0.18f * f * vis);
                flame(cv, hx - 6, hy + 1, 0.8f, v->time, l->phase + 1.1f, vis);
                flame(cv, hx + 6, hy + 1, 0.75f, v->time, l->phase + 2.6f, vis);
                flame(cv, hx, hy, 1.25f, v->time, l->phase, vis);
                rate = 5.0f;
            } else {
                /* the Heartflame itself: a column of gold with a white heart */
                cv_glow(cv, (int)hx, (int)hy - 14, 110, 0xffa850, 0.16f * f * vis);
                cv_glow(cv, (int)hx, (int)hy - 10, 30, 0xffd890, 0.22f * f * vis);
                static const float FX[9] = { -14, 14, -9, 9, -4, 4, 0, -2, 2 }, FY[9] = { 4, 4, 2, 2, 0, 0, -6, -14, -21 };
                static const float FS[9] = { 0.9f, 0.95f, 1.3f, 1.25f, 1.6f, 1.55f, 1.8f, 1.35f, 0.9f };
                for (int k = 0; k < 9; k++) {
                    float sway = sinf(v->time * 2.3f + k * 0.9f) * (FY[k] < 0 ? -FY[k] * 0.12f : 0.5f);
                    flame_body(cv, hx + FX[k] + sway, hy + FY[k], FS[k], v->time, l->phase + k * 1.37f, vis);
                }
                float wob = sinf(v->time * 7 + l->phase) * 1.4f;
                cv_disc(cv, hx + wob * 0.5f, hy - 6, 2.6f, 0xfffff0, (int)(235 * vis));
                cv_disc(cv, hx + wob, hy - 15, 1.6f, 0xfff8d8, (int)(210 * vis));
                rate = 11.0f;
            }
            if (rate > 0 && v->fog && dt > 0) {
                r->ember_acc[i] += dt * rate;
                while (r->ember_acc[i] >= 1) {
                    r->ember_acc[i] -= 1;
                    spawn(r, l->fx * TILE + 16.0f + (rng_f(&r->rng) - 0.5f) * 8 * st, l->fy * TILE + 14.0f,
                          (rng_f(&r->rng) - 0.5f) * 14, -22 - rng_f(&r->rng) * 14 * st, 0.8f + rng_f(&r->rng) * 0.6f * st,
                          rng_chance(&r->rng, 40) ? 0xffa040 : st == 3 && rng_chance(&r->rng, 40) ? 0xfff4d0 : 0xffd080, 0.9f + 0.2f * st);
                }
            }
        } else if (l->kind == L_MOSS) {
            cv_glow(cv, sx + 16, sy + 16, 26, 0x40e0b0, 0.16f * f * vis);
        } else if (l->kind == L_CRYSTAL) {
            cv_glow(cv, sx + 16, sy + TILE - 10, 34, 0x3a8cff, 0.20f * f * vis);
            cv_glow(cv, sx + 16, sy + TILE - 12, 12, 0x9ad0ff, 0.30f * f * vis);
            if (v->fog && dt > 0 && rng_f(&r->rng) < dt * 0.8f)
                spawn(r, l->fx * TILE + 6.0f + rng_f(&r->rng) * 20, l->fy * TILE + TILE - 6.0f - rng_f(&r->rng) * 16,
                      0, -4, 1.4f, 0xa8e0ff, 0.8f);
        }
    }
    /* burning oil: a carpet of small flames */
    {
        int tx0 = v->camx / TILE - 1, ty0 = v->camy / TILE - 1;
        int tx1 = (v->camx + cv->w) / TILE + 1, ty1 = (v->camy + cv->h) / TILE + 2;
        for (int ty = ty0 < 0 ? 0 : ty0; ty <= ty1 && ty < MAP_H; ty++)
            for (int tx = tx0 < 0 ? 0 : tx0; tx <= tx1 && tx < MAP_W; tx++) {
                int b = lv->burn[ty][tx];
                if (!b) continue;
                float vis = v->fog ? v->visf[ty][tx] : 1.0f;
                if (vis < 0.05f) continue;
                int sx = tx * TILE - v->camx, sy = ty * TILE - v->camy;
                float size = b >= 5 ? 1.0f : 0.45f + 0.11f * b;
                /* the oil itself glows where it burns */
                cv_glow(cv, sx + 16, sy + 20, 22, 0xff7a20, 0.13f * size * vis);
                for (int k = 0; k < 5; k++) {
                    uint32_t hk = hash2(tx * 7 + k, ty * 13 + k, 0xf1e);
                    float ph = (float)(hk % 628) / 100.0f;
                    /* each tongue of flame leaps and sinks on its own */
                    float leap = 0.55f + 0.45f * sinf(v->time * (2.3f + (hk >> 12) % 5 * 0.4f) + ph);
                    float fx = sx + 4 + (float)(hk % 24), fy = sy + 14 + (float)((hk >> 5) % 16);
                    flame(cv, fx, fy, size * (0.45f + 0.45f * leap), v->time, ph, vis);
                }
                if (v->fog && dt > 0 && rng_f(&r->rng) < dt * 2.0f)
                    spawn(r, tx * TILE + 6.0f + rng_f(&r->rng) * 20, ty * TILE + 10.0f + rng_f(&r->rng) * 10,
                          (rng_f(&r->rng) - 0.5f) * 14, -18 - rng_f(&r->rng) * 22, 0.5f + rng_f(&r->rng) * 0.7f,
                          rng_chance(&r->rng, 50) ? 0xffa040 : 0xffd080, 0.9f);
            }
    }
    /* sling stones: a short arc from the slinger to you */
    for (int k = 0; k < 4; k++) {
        if (r->stone[k].t < 0 || r->stone[k].t >= STONE_T) continue;
        r->stone[k].t += dt;
        float u = r->stone[k].t / STONE_T;
        if (u > 1) u = 1;
        float dx = r->stone[k].x1 - r->stone[k].x0, dy = r->stone[k].y1 - r->stone[k].y0;
        float hop = sqrtf(dx * dx + dy * dy) * 0.18f;
        float x = r->stone[k].x0 + dx * u - v->camx, y = r->stone[k].y0 + dy * u - hop * 4 * u * (1 - u) - v->camy;
        cv_disc(cv, x + 1, y + 2, 2.4f, 0x000000, 90);
        cv_disc(cv, x, y, 2.4f, 0x5a5650, 255);
        cv_disc(cv, x - 0.7f, y - 0.7f, 1.1f, 0xb0aca0, 220);
        if (r->stone[k].t >= STONE_T)
            for (int n = 0; n < 8; n++)
                spawn_g(r, x + v->camx, y + v->camy, (rng_f(&r->rng) - 0.5f) * 80, -20 - rng_f(&r->rng) * 40,
                        0.3f + rng_f(&r->rng) * 0.3f, 0x6a6458, 0.5f, 200);
    }
    /* particles */
    for (int i = 0; i < MAX_PART; i++) {
        Particle *p = &r->part[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vx += sinf(v->time * 3 + i) * 12 * dt * (p->grav == 0);
        p->vy += p->grav * dt;
        float k = p->life / p->max;
        if (k <= 0) continue;
        int x = (int)p->x - v->camx, y = (int)p->y - v->camy;
        if ((unsigned)x >= (unsigned)cv->w || (unsigned)y >= (unsigned)cv->h) continue;
        uint32_t c = p->c;
        float s = k * 0.9f;
        cv->px[(size_t)y * cv->w + x] = add_sat(cv->px[(size_t)y * cv->w + x], (int)(CR(c) * s), (int)(CG(c) * s), (int)(CB(c) * s));
        if (p->size > 0.85f && k > 0.5f) cv_glow(cv, x, y, 3, c, 0.25f * k);
    }
    if (v->player && v->torch) {
        float tx, ty;
        torch_tip(r, v->player, &tx, &ty);
        flame(cv, tx - v->camx, ty - v->camy, 0.8f, v->time, 1.7f, 1.0f);
        if (dt > 0) {
            r->ember_acc[MAX_LIGHTS - 1] += dt * 3.0f;
            while (r->ember_acc[MAX_LIGHTS - 1] >= 1) {
                r->ember_acc[MAX_LIGHTS - 1] -= 1;
                spawn(r, tx, ty - 3, (rng_f(&r->rng) - 0.5f) * 12, -16 - rng_f(&r->rng) * 20,
                      0.5f + rng_f(&r->rng) * 0.7f, rng_chance(&r->rng, 50) ? 0xffa040 : 0xffd080, 0.9f);
            }
        }
    }
}

static void draw_items(Renderer *r, Canvas *cv, const View *v)
{
    if (!v->items) return;
    const Level *lv = v->lv;
    for (int i = 0; i < MAX_ITEMS; i++) {
        const FloorItem *it = &v->items[i];
        if (!it->alive) continue;
        int tx = it->x, ty = it->y;
        if (v->fog && !(lv->f[ty][tx] & F_SEEN)) continue;
        int sx = tx * TILE - v->camx, sy = ty * TILE - v->camy;
        if (sx < -TILE || sy < -TILE || sx > cv->w || sy > cv->h) continue;
        const Image *im = &r->t.item[it->kind];
        /* a soft shadow, then the item lit like the floor */
        for (int j = -2; j <= 2; j++)
            for (int k = -9; k <= 9; k++) {
                float d = k * k / 81.0f + j * j / 4.0f;
                int x = sx + 16 + k, y = sy + 27 + j;
                if (d <= 1 && (unsigned)x < (unsigned)cv->w && (unsigned)y < (unsigned)cv->h)
                    blend_px(&cv->px[(size_t)y * cv->w + x], 0, (int)(70 * (1 - d)));
            }
        CF c00 = r->corner[ty][tx], c10 = r->corner[ty][tx + 1], c01 = r->corner[ty + 1][tx], c11 = r->corner[ty + 1][tx + 1];
        draw_lit(cv, sx, sy, im, TILE, TILE, &c00, &c10, &c01, &c11, 1.15f, 1.15f, 0, v->fog, NULL, 0);
        bool vis = !v->fog || (lv->f[ty][tx] & F_VIS);
        const Image *gl = &r->t.item_glow[it->kind];
        if (vis)
            for (int j = 0; j < TILE; j++)
                for (int k = 0; k < TILE; k++) {
                    uint32_t g = gl->px[j * TILE + k];
                    int x = sx + k, y = sy + j;
                    if (CA(g) && (unsigned)x < (unsigned)cv->w && (unsigned)y < (unsigned)cv->h)
                        blend_px(&cv->px[(size_t)y * cv->w + x], g & 0xffffff, 150);
                }
        /* now and then a glint catches the eye */
        if (vis && v->fog) {
            float ph = fmodf(v->time * 0.45f + (hash2(tx, ty, 5) % 100) / 100.0f, 1.0f);
            if (ph < 0.12f) {
                float k = sinf(ph / 0.12f * 3.14159f);
                int gx = sx + 10 + (int)(hash2(tx, ty, 6) % 12), gy = sy + 12 + (int)(hash2(tx, ty, 7) % 8);
                cv_glow(cv, gx, gy, 5, 0xffffff, 0.6f * k);
                cv_blend_rect(cv, gx - 2, gy, 5, 1, 0xffffff, (int)(200 * k));
                cv_blend_rect(cv, gx, gy - 2, 1, 5, 0xffffff, (int)(200 * k));
            }
        }
    }
}

/* walls, doors, fixtures and creatures, row by row so nearer things cover farther ones */
/* Walls, doors and fixtures in a nearer row are drawn over canvas rows y0..y1:
 * a creature they cover must stay covered. hires_occlude(.., false) notes the
 * creatures' pixels there first, (.., true) gives up the ones that changed. */
static void hires_occlude(Renderer *r, const Canvas *cv, int y0, int y1, bool after)
{
    for (int k = 0; k < r->nhic; k++) {
        const HiCmd *c = &r->hic[k];
        int ya = c->y > y0 ? c->y : y0, yb = c->y + c->W < y1 ? c->y + c->W : y1;
        if (ya < 0) ya = 0;
        if (yb > cv->h) yb = cv->h;
        int xa = c->x < 0 ? 0 : c->x, xb = c->x + c->W > cv->w ? cv->w : c->x + c->W;
        for (int y = ya; y < yb; y++)
            for (int x = xa; x < xb; x++) {
                size_t o = (size_t)y * cv->w + x;
                if (r->own[o] != k + 1) continue;
                if (!after) r->hsnap[o] = cv->px[o];
                else if (cv->px[o] != r->hsnap[o]) r->own[o] = 0;
            }
    }
}

static void draw_rows(Renderer *r, Canvas *cv, const View *v)
{
    const Level *lv = v->lv;
    int tx0 = (int)floorf((float)v->camx / TILE) - 1, ty0 = (int)floorf((float)v->camy / TILE) - 1;
    int tx1 = (v->camx + cv->w) / TILE + 1, ty1 = (v->camy + cv->h + WALL_R) / TILE + 1;
    for (int ty = ty0; ty <= ty1; ty++) {
        if (ty < 0 || ty >= MAP_H) continue;
        int oy0 = ty * TILE - v->camy - 3 * TILE, oy1 = ty * TILE - v->camy + TILE;
        if (cv == r->rec_cv) hires_occlude(r, cv, oy0, oy1, false);
        for (int tx = tx0; tx <= tx1; tx++) {
            if (!in_map(tx, ty)) continue;
            Tile t = (Tile)lv->t[ty][tx];
            if (t != T_WALL && t != T_DOOR) continue;
            if (v->fog && !(lv->f[ty][tx] & F_SEEN)) continue;
            if (t == T_WALL) draw_wall(r, cv, v, tx, ty);
            else draw_door(r, cv, v, tx, ty);
        }
        for (int i = 0; i < lv->nlights; i++)
            if (lv->lights[i].fy == ty && lv->lights[i].kind != L_PLAYER) draw_fixture(r, cv, v, &lv->lights[i]);
        if (cv == r->rec_cv) hires_occlude(r, cv, oy0, oy1, true);
        for (int i = 0; i < MAX_MOBS; i++) {
            const Mob *m = &v->mobs[i];
            if (m->alive && (int)floorf(m->ry + 0.5f) == ty) draw_mob(r, cv, v, m, false);
        }
        if (v->player && (int)floorf(v->player->ry + 0.5f) == ty) draw_mob(r, cv, v, v->player, true);
    }
}

/* soften the level below once, like a camera focused on the near floor */
static void blur_canvas(Canvas *cv)
{
    int w = cv->w, h = cv->h;
    uint32_t *tmp = malloc((size_t)w * h * 4);
    if (!tmp) return;
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < h; y++) {          /* horizontal, radius 2 */
            const uint32_t *row = cv->px + (size_t)y * w;
            for (int x = 0; x < w; x++) {
                int r = 0, g = 0, b = 0;
                for (int k = -2; k <= 2; k++) {
                    uint32_t c = row[clampi(x + k, 0, w - 1)];
                    r += CR(c); g += CG(c); b += CB(c);
                }
                tmp[(size_t)y * w + x] = RGB(r / 5, g / 5, b / 5);
            }
        }
        for (int y = 0; y < h; y++)            /* vertical */
            for (int x = 0; x < w; x++) {
                int r = 0, g = 0, b = 0;
                for (int k = -2; k <= 2; k++) {
                    uint32_t c = tmp[(size_t)clampi(y + k, 0, h - 1) * w + x];
                    r += CR(c); g += CG(c); b += CB(c);
                }
                cv->px[(size_t)y * w + x] = RGB(r / 5, g / 5, b / 5);
            }
    }
    free(tmp);
}

static void render_lower(Renderer *r, Game *g)
{
    Level *lv = game_level_below(g);
    r->lower_ok = false;
    if (!lv) return;
    if (lv->light_dirty) level_relight(lv);
    View v = { lv, game_mobs(g, g->depth + 1), NULL, false, false, 0, 0, 0, NULL, NULL,
               game_items(g, g->depth + 1), false, false, 0, 0, 0, 0 };
    compute_light(r, &v);
    memset(r->lower.px, 0, (size_t)r->lower.w * r->lower.h * 4);
    draw_ground(r, &r->lower, &v);
    draw_reflections(r, &r->lower, &v);
    draw_items(r, &r->lower, &v);
    draw_rows(r, &r->lower, &v);
    draw_effects(r, &r->lower, &v, 0);
    blur_canvas(&r->lower);
    r->lower_ok = true;
    r->lower_depth = g->depth;
}

static void draw_path(Canvas *cv, const View *v, const int *path, int plen, int hx, int hy, float t)
{
    for (int i = 0; i < plen; i++) {
        int x = path[i] % MAP_W, y = path[i] / MAP_W;
        int sx = x * TILE - v->camx + TILE / 2, sy = y * TILE - v->camy + TILE / 2;
        if (i == plen - 1) break;
        float pulse = 0.6f + 0.4f * sinf(t * 5 - i * 0.6f);
        cv_disc(cv, (float)sx, (float)sy + 2, 2.3f, 0xffd890, (int)(150 * pulse));
    }
    if (hx >= 0) {
        int sx = hx * TILE - v->camx, sy = hy * TILE - v->camy;
        uint32_t c = 0xffe0a0;
        int a = 200, L = 7;
        for (int k = 0; k < L; k++) {
            cv_blend_rect(cv, sx + k, sy, 1, 2, c, a);
            cv_blend_rect(cv, sx, sy + k, 2, 1, c, a);
            cv_blend_rect(cv, sx + TILE - 1 - k, sy, 1, 2, c, a);
            cv_blend_rect(cv, sx + TILE - 2, sy + k, 2, 1, c, a);
            cv_blend_rect(cv, sx + k, sy + TILE - 2, 1, 2, c, a);
            cv_blend_rect(cv, sx, sy + TILE - 1 - k, 2, 1, c, a);
            cv_blend_rect(cv, sx + TILE - 1 - k, sy + TILE - 2, 1, 2, c, a);
            cv_blend_rect(cv, sx + TILE - 2, sy + TILE - 1 - k, 2, 1, c, a);
        }
    }
}

/* where a thrown item is after travelling `pos` tiles along its path, and how high it arcs */
static void flight_point(const Projectile *pr, const int *path, int n, int sx0, int sy0, float pos,
                         float *x, float *y, float *h, ItemKind k)
{
    if (pos < 0) pos = 0;
    if (pos > n) pos = (float)n;
    int seg = (int)pos;
    float f = pos - seg;
    if (seg >= n) { seg = n - 1; f = 1; }
    int a = seg == 0 ? sy0 * MAP_W + sx0 : path[seg - 1], b = path[seg];
    float ax = (a % MAP_W) * TILE + TILE / 2.0f, ay = (a / MAP_W) * TILE + TILE / 2.0f;
    float bx = (b % MAP_W) * TILE + TILE / 2.0f, by = (b / MAP_W) * TILE + TILE / 2.0f;
    *x = ax + (bx - ax) * f;
    *y = ay + (by - ay) * f;
    float u = n > 0 ? pos / n : 1;
    float top = k == IT_DART ? 3.0f + n * 0.6f : 6.0f + n * 3.2f;
    if (top > 30) top = 30;
    *h = sinf(u * 3.14159f) * top;
    (void)pr;
}

static void draw_projectile(Renderer *r, Canvas *cv, const View *v, const Projectile *pr, float dt)
{
    float x, y, h;
    flight_point(pr, pr->path, pr->n, pr->sx, pr->sy, pr->pos, &x, &y, &h, pr->kind);
    float sx = x - v->camx, sy = y - v->camy;
    /* the shadow on the ground shows where it really is */
    cv_disc(cv, sx, sy + 8, 4.0f, 0x000000, 90);
    float px = sx, py = sy - h;
    float ex, ey, eh;
    flight_point(pr, pr->path, pr->n, pr->sx, pr->sy, pr->pos - 0.25f, &ex, &ey, &eh, pr->kind);
    float dx = px - (ex - v->camx), dy = py - (ey - v->camy - eh);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f) { dx = 1; dy = 0; len = 1; }
    dx /= len;
    dy /= len;
    switch (pr->kind) {
    case IT_DART:
        cv_line(cv, (int)(px - dx * 9), (int)(py - dy * 9), (int)px, (int)py, 0x8a7a6a, 255);
        cv_line(cv, (int)(px - dx * 3), (int)(py - dy * 3), (int)(px + dx * 2), (int)(py + dy * 2), 0xe8ecf4, 255);
        cv_line(cv, (int)(px - dx * 9), (int)(py - dy * 9 - 1), (int)(px - dx * 6), (int)(py - dy * 6 - 1), 0xb83a30, 255);
        break;
    case IT_TORCH: {
        float a = pr->pos * 7.0f;
        float cx = cosf(a), cy = sinf(a);
        cv_line(cv, (int)(px - cx * 6), (int)(py - cy * 6), (int)(px + cx * 6), (int)(py + cy * 6), 0x6e4828, 255);
        flame(cv, px + cx * 6, py + cy * 6 + 3, 0.75f, v->time, 0.3f, 1.0f);
        if (dt > 0)
            for (int k = 0; k < 3; k++)
                spawn(r, x + cx * 6 + (rng_f(&r->rng) - 0.5f) * 3, y - h + cy * 6, (rng_f(&r->rng) - 0.5f) * 20,
                      (rng_f(&r->rng) - 0.5f) * 20, 0.35f + rng_f(&r->rng) * 0.3f, 0xffb050, 0.9f);
        break;
    }
    default:
        cv_image(cv, &r->t.item[pr->kind], 0, 0, TILE, TILE, (int)px - 16, (int)py - 20, 0);
        break;
    }
}

/* burning torches on the level below, animated and seen through the chasms */
static void draw_flames_below(Renderer *r, Canvas *cv, const View *v, Game *g)
{
    Level *lo = game_level_below(g);
    if (!lo || !r->lower_ok) return;
    float pcx = v->camx + cv->w * 0.5f, pcy = v->camy + cv->h * 0.5f;
    for (int i = 0; i < lo->nlights; i++) {
        const Light *l = &lo->lights[i];
        if (l->kind != L_FLOORTORCH || !l->lit) continue;
        float wx = l->fx * TILE + 23.5f, wy = l->fy * TILE + 15.5f;
        float ux = pcx + (wx - pcx) * LOWER_SCALE, uy = pcy + (wy - pcy) * LOWER_SCALE;
        int tx = (int)floorf(ux / TILE), ty = (int)floorf(uy / TILE);
        if (tile_at(v->lv, tx, ty) != T_CHASM || !(v->lv->f[ty][tx] & F_SEEN)) continue;
        float vis = v->visf[ty][tx];
        if (vis < 0.05f) continue;
        float size = 0.6f * (l->fuel >= 0 && l->fuel < 25 ? 0.5f + 0.5f * l->fuel / 25.0f : 1.0f);
        flame(cv, ux - v->camx, uy - v->camy, size, v->time, l->phase, vis * 0.75f);
    }
}

static void draw_throw_preview(Canvas *cv, const View *v, const Overlay *ov, const Mob *p)
{
    if (!ov->tlen) return;
    Projectile fake = { 0 };
    float t = v->time;
    float total = (float)ov->tlen;
    uint32_t col = ov->thit ? 0xff7050 : 0xffd890;
    for (float d = 0.35f; d < total - 0.2f; d += 0.33f) {
        float x, y, h;
        flight_point(&fake, ov->tpath, ov->tlen, p->x, p->y, d, &x, &y, &h, ov->tkind);
        float pulse = 0.55f + 0.45f * sinf(t * 6 - d * 2.2f);
        cv_disc(cv, x - v->camx, y - h - v->camy, 1.7f, col, (int)(200 * pulse));
    }
    int c = ov->tpath[ov->tlen - 1];
    float lx = (c % MAP_W) * TILE + TILE / 2.0f - v->camx, ly = (c / MAP_W) * TILE + TILE / 2.0f - v->camy;
    bool chasm = tile_at(v->lv, c % MAP_W, c / MAP_W) == T_CHASM;
    uint32_t rc = ov->thit ? 0xff6040 : chasm ? 0x80c8ff : 0xffe0a0;
    float pr = 11 + sinf(t * 5) * 1.5f;
    cv_ring(cv, lx, ly + 2, pr, 2.0f, rc, 220);
    cv_ring(cv, lx, ly + 2, pr * 0.45f, 1.5f, rc, 160);
    if (chasm) return;
    int cx = c % MAP_W, cy = c / MAP_W;
    if (ov->tkind == IT_OIL) {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                Tile tt = tile_at(v->lv, cx + dx, cy + dy);
                if ((!dx && !dy) || !tile_walkable(tt) || tt == T_DOOR) continue;
                float x = lx + dx * TILE, y = ly + dy * TILE;
                cv_blend_rect(cv, (int)x - 12, (int)y - 12, 24, 24, 0xff7a30, 40);
                cv_ring(cv, x, y + 2, 6, 1.2f, 0xffa050, 150);
            }
    } else if (ov->tkind == IT_FLASH && !tile_water(tile_at(v->lv, cx, cy))) {
        cv_ring(cv, lx, ly + 2, 5.5f * TILE, 1.5f, 0xfff0c0, (int)(90 + 40 * sinf(t * 4)));
    }
}

void render_world(Renderer *r, Game *g, Canvas *cv, float dt, const Overlay *ov)
{
    if (g->level_changed || r->lower_depth != g->depth) {
        render_lower(r, g);
        g->level_changed = false;
        g->lower_dirty = false;
        memset(r->part, 0, sizeof r->part);
    } else if (g->lower_dirty) {
        render_lower(r, g);
        g->lower_dirty = false;
    }
    Level *lv = game_level(g);
    if (lv->light_dirty) level_relight(lv);
    int camx = (int)lroundf(r->camx), camy = (int)lroundf(r->camy);
    if (g->shake > 0) {
        camx += (int)((rng_f(&r->rng) - 0.5f) * g->shake * 18);
        camy += (int)((rng_f(&r->rng) - 0.5f) * g->shake * 18);
    }
    View v = { lv, game_mobs(g, g->depth), &g->player, true, true, camx, camy, g->time, g->visf, g->plight,
               game_items(g, g->depth), g->torch_lit, false, 0, 0, 0, 0 };
    if (g->proj.active && g->proj.kind == IT_TORCH) {
        float x, y, h;
        flight_point(&g->proj, g->proj.path, g->proj.n, g->proj.sx, g->proj.sy, g->proj.pos, &x, &y, &h, IT_TORCH);
        v.projlight = true;
        v.projx = x;
        v.projy = y - h * 0.5f;
        v.projtx = clampi((int)(x / TILE), 0, MAP_W - 1);
        v.projty = clampi((int)(y / TILE), 0, MAP_H - 1);
    }
    /* one-shot effects from the game */
    for (int i = 0; i < g->nfx; i++) {
        Fx *f = &g->fx[i];
        float x = f->x * TILE + TILE / 2.0f, y = f->y * TILE + TILE / 2.0f;
        if (f->kind == FX_STONE) {
            for (int k = 0; k < 4; k++)
                if (r->stone[k].t >= STONE_T || r->stone[k].t < 0) {
                    r->stone[k].x0 = f->sx * TILE + TILE / 2.0f;
                    r->stone[k].y0 = f->sy * TILE + 6.0f;
                    r->stone[k].x1 = x;
                    r->stone[k].y1 = y;
                    r->stone[k].t = 0;
                    break;
                }
            continue;
        }
        if (f->kind == FX_FLASH) {
            r->flash_t = 0.5f;
            r->flash_x = x;
            r->flash_y = y;
        }
        int n = f->kind == FX_SHATTER ? 26 : f->kind == FX_FALL ? 18 : f->kind == FX_DISSOLVE ? 40
              : f->kind == FX_FLASH ? 48 : f->kind == FX_OIL ? 30 : f->kind == FX_SALT ? 40 : 16;
        for (int k = 0; k < n; k++) {
            float a = rng_f(&r->rng) * 6.283f, sp = 20 + rng_f(&r->rng) * 70;
            switch (f->kind) {
            case FX_SPARKS:
                spawn_g(r, x, y, cosf(a) * sp, sinf(a) * sp - 30, 0.3f + rng_f(&r->rng) * 0.4f,
                        rng_chance(&r->rng, 50) ? 0xffc060 : 0xffe8b0, 0.9f, 160);
                break;
            case FX_SHATTER:
                spawn_g(r, x, y, cosf(a) * sp, sinf(a) * sp - 40, 0.4f + rng_f(&r->rng) * 0.5f,
                        k % 3 == 0 ? 0xd0e8f0 : 0xd02838, k % 3 == 0 ? 0.9f : 0.5f, 220);
                break;
            case FX_BURN:
                spawn_g(r, x + (rng_f(&r->rng) - 0.5f) * 18, y + (rng_f(&r->rng) - 0.5f) * 18, cosf(a) * sp * 0.4f,
                        -20 - rng_f(&r->rng) * 40, 0.3f + rng_f(&r->rng) * 0.5f,
                        k % 3 ? 0xff8030 : 0xffd080, 0.9f, -20);
                break;
            case FX_DISSOLVE:
                spawn_g(r, x + (rng_f(&r->rng) - 0.5f) * 20, y + (rng_f(&r->rng) - 0.5f) * 24, cosf(a) * 8,
                        -8 - rng_f(&r->rng) * 18, 1.0f + rng_f(&r->rng) * 1.2f,
                        k % 4 ? 0x3a3060 : 0x8a90c0, k % 4 ? 0.5f : 0.9f, -6);
                break;
            case FX_SPLASH: {
                if (k == 0) {
                    add_ripple(r, x, y + 6, 1.0f, 1.5f);
                    add_ripple(r, x, y + 6, 0.6f, 2.2f);
                }
                CF c;
                light_at_px(r, x, y, &c);
                float l = clampf((c.r + c.g + c.b) / 3 + 0.15f, 0, 1.2f);
                spawn_g(r, x + (rng_f(&r->rng) - 0.5f) * 10, y + 6, cosf(a) * sp * 0.35f, -40 - rng_f(&r->rng) * 50,
                        0.35f + rng_f(&r->rng) * 0.3f, mix(0, 0xa8d8e0, (int)(l * 200)), 0.5f, 260);
                break;
            }
            case FX_STEAM:
                spawn_g(r, x + (rng_f(&r->rng) - 0.5f) * 14, y + (rng_f(&r->rng) - 0.5f) * 8, (rng_f(&r->rng) - 0.5f) * 10,
                        -10 - rng_f(&r->rng) * 20, 0.8f + rng_f(&r->rng) * 1.0f, k % 3 ? 0x505860 : 0x8a9098, 0.6f, -8);
                break;
            case FX_FALL:
                spawn_g(r, x + (rng_f(&r->rng) - 0.5f) * 14, y, (rng_f(&r->rng) - 0.5f) * 10, 10 + rng_f(&r->rng) * 30,
                        0.6f + rng_f(&r->rng) * 0.8f, 0xffa040, 0.9f, 40);
                break;
            case FX_FLASH:
                spawn_g(r, x, y, cosf(a) * sp * 2.2f, sinf(a) * sp * 2.2f - 30, 0.35f + rng_f(&r->rng) * 0.5f,
                        k % 3 ? 0xfff8e0 : 0xffe090, 0.9f, 60);
                break;
            case FX_OIL:
                spawn_g(r, x, y, cosf(a) * sp, sinf(a) * sp - 40, 0.4f + rng_f(&r->rng) * 0.5f,
                        k % 4 == 0 ? 0xc8d8c0 : k % 2 ? 0xffa030 : 0xd07018, k % 4 == 0 ? 0.5f : 0.9f, 220);
                break;
            case FX_SALT: {
                float rr = 14 + rng_f(&r->rng) * 26;
                spawn_g(r, x + cosf(a) * rr, y + sinf(a) * rr * 0.7f - 14, cosf(a) * 6, 10 + rng_f(&r->rng) * 10,
                        0.4f + rng_f(&r->rng) * 0.4f, 0xe8e8e0, 0.5f, 90);
                break;
            }
            default:
                break;
            }
        }
    }
    g->nfx = 0;
    compute_light(r, &v);
    /* record the creatures that have full-resolution sprites */
    r->nhic = 0;
    r->rec_cv = NULL;
    size_t npx = (size_t)cv->w * cv->h;
    if (r->any_hi && r->zoom >= 2 && npx) {
        if (npx > r->hcap) {
            free(r->own); free(r->hbg); free(r->hwr); free(r->hsnap);
            r->own = malloc(npx * sizeof *r->own);
            r->hbg = malloc(npx * 4);
            r->hwr = malloc(npx * 4);
            r->hsnap = malloc(npx * 4);
            r->hcap = r->own && r->hbg && r->hwr && r->hsnap ? npx : 0;
        }
        if (r->hcap) {
            memset(r->own, 0, npx * sizeof *r->own);
            r->rec_cv = cv;
        }
    }
    memset(cv->px, 0, (size_t)cv->w * cv->h * 4);
    draw_ground(r, cv, &v);
    draw_reflections(r, cv, &v);
    draw_ripples(r, cv, &v, dt);
    draw_items(r, cv, &v);
    draw_flames_below(r, cv, &v, g);
    if (!ov->tlen) draw_path(cv, &v, ov->path, ov->plen, -1, -1, g->time);
    draw_rows(r, cv, &v);
    draw_effects(r, cv, &v, dt);
    if (g->proj.active) draw_projectile(r, cv, &v, &g->proj, dt);
    draw_throw_preview(cv, &v, ov, &g->player);
    if (ov->hx >= 0) draw_path(cv, &v, NULL, 0, ov->hx, ov->hy, g->time);
    if (r->flash_t > 0) {
        float k = r->flash_t / 0.5f;
        k *= k;
        int fx = (int)(r->flash_x - camx), fy = (int)(r->flash_y - camy);
        cv_glow(cv, fx, fy, 6 * TILE, 0xfff4e0, 1.1f * k);
        cv_glow(cv, fx, fy, 2 * TILE, 0xffffff, 1.4f * k);
        r->flash_t -= dt;
    }
    r->hcv_w = r->rec_cv ? cv->w : 0;
    r->hcv_h = r->rec_cv ? cv->h : 0;
    r->rec_cv = NULL;
}

/* How what was drawn over a creature (glows, shadows, flames) changed the pixel
 * it wrote: 0 added light (delta in d), 1 darkened it (by *k), 2 covered it. */
static int overlay_mode(uint32_t fin, uint32_t wr, int d[3], float *k)
{
    int f[3] = { CR(fin), CG(fin), CB(fin) }, w[3] = { CR(wr), CG(wr), CB(wr) };
    int lo = 255, hi = -255;
    for (int c = 0; c < 3; c++) {
        d[c] = f[c] - w[c];
        if (d[c] < lo) lo = d[c];
        if (d[c] > hi) hi = d[c];
    }
    if (lo >= -3 || (hi <= 8 && lo >= -8)) return 0;
    if (hi <= 3) {
        float kmin = 9, kmax = -1, ks = 0;
        int n = 0;
        for (int c = 0; c < 3; c++) {
            if (w[c] < 24) continue;
            float q = (float)f[c] / (float)w[c];
            if (q < kmin) kmin = q;
            if (q > kmax) kmax = q;
            ks += q;
            n++;
        }
        if (n && kmax - kmin < 0.15f) { *k = ks / n; return 1; }
    }
    return 2;
}

/* one texel of a full-resolution sprite, lit like the small one; alpha in *a */
static inline uint32_t hi_texel(const Image *im, const Image *gl, const HiCmd *c, int x, int y, int *a)
{
    if ((unsigned)x >= (unsigned)im->w || (unsigned)y >= (unsigned)im->h) { *a = 0; return 0; }
    uint32_t p = im->px[(size_t)y * im->w + x];
    *a = (int)CA(p);
    if (!*a) return 0;
    uint32_t g = gl->px[(size_t)y * gl->w + x];
    uint32_t col = CA(g) ? (g & 0xffffff) : lightc(p & 0xffffff, c->lr, c->lg, c->lb);
    if (c->asleep && !CA(g)) col = mix(col, 0x202838, 60);
    if (c->fl) col = mix(col, 0xffffff, c->fl);
    return col;
}

static float seg_dist(float px, float py, float ax, float ay, float bx, float by)
{
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? clampf(((px - ax) * dx + (py - ay) * dy) / l2, 0, 1) : 0;
    float ex = ax + dx * t - px, ey = ay + dy * t - py;
    return sqrtf(ex * ex + ey * ey);
}

/* the torch haft at screen resolution (two strands and its head), under
 * whatever was drawn over it in the world (the flame) */
static void hires_stick(Renderer *r, Canvas *scr, const Canvas *world, int z, int id, const HiCmd *c)
{
    float ax = (c->sx0 + 0.5f) * z, ay = (c->sy0 + 0.5f) * z, bx = (c->sx1 + 0.5f) * z, by = (c->sy1 + 0.5f) * z;
    float hx = (c->sx1 + 0.5f) * z, hy = (c->sy1 - 0.5f) * z, hr = 2.2f * z;
    int X0 = (int)fminf(ax, bx) - 3 * z, X1 = (int)fmaxf(ax, bx) + 4 * z;
    int Y0 = (int)fminf(ay, by) - 3 * z, Y1 = (int)fmaxf(ay, by) + 3 * z;
    for (int Y = Y0 < 0 ? 0 : Y0; Y < Y1 && Y < scr->h; Y++)
        for (int X = X0 < 0 ? 0 : X0; X < X1 && X < scr->w; X++) {
            int wx = X / z, wy = Y / z;
            if (wx >= world->w || wy >= world->h) continue;
            size_t o = (size_t)wy * world->w + wx;
            if (r->own[o] != id) continue;
            int d[3];
            float mk = 1;
            int mode = overlay_mode(world->px[o], r->hwr[o], d, &mk);
            if (mode == 2) continue;
            float p[2] = { X + 0.5f, Y + 0.5f };
            float cov[3] = {
                clampf(z * 0.5f + 0.5f - seg_dist(p[0], p[1], ax, ay, bx, by), 0, 1),
                clampf(z * 0.5f + 0.5f - seg_dist(p[0], p[1], ax + z, ay, bx + z, by), 0, 1),
                clampf(hr + 0.5f - sqrtf((p[0] - hx) * (p[0] - hx) + (p[1] - hy) * (p[1] - hy)), 0, 1),
            };
            if (cov[0] <= 0 && cov[1] <= 0 && cov[2] <= 0) continue;
            uint32_t *q = &scr->px[(size_t)Y * scr->w + X];
            /* take the overlay off, draw the haft, put the overlay back */
            float rr = CR(*q), gg = CG(*q), bb = CB(*q);
            if (mode == 0) { rr -= d[0]; gg -= d[1]; bb -= d[2]; }
            else if (mk > 0.01f) { rr /= mk; gg /= mk; bb /= mk; }
            static const uint32_t col[3] = { 0x5a3a20, 0x7a5030, 0x3a2a1c };
            for (int s = 0; s < 3; s++) {
                rr += (CR(col[s]) - rr) * cov[s];
                gg += (CG(col[s]) - gg) * cov[s];
                bb += (CB(col[s]) - bb) * cov[s];
            }
            if (mode == 0) { rr += d[0]; gg += d[1]; bb += d[2]; }
            else { rr *= mk; gg *= mk; bb *= mk; }
            *q = RGB(clampi((int)lroundf(rr), 0, 255), clampi((int)lroundf(gg), 0, 255), clampi((int)lroundf(bb), 0, 255));
        }
}

void render_set_zoom(Renderer *r, int zoom) { r->zoom = zoom; }

void render_hires(Renderer *r, Canvas *scr, const Canvas *world, int zoom)
{
    if (zoom < 2 || !r->nhic || world->w != r->hcv_w || world->h != r->hcv_h) return;
    for (int k = 0; k < r->nhic; k++) {
        const HiCmd *c = &r->hic[k];
        const Image *im = &r->t.mob_hi[c->kind], *gl = &r->t.mob_hi_glow[c->kind];
        int D = c->W * zoom;
        float s = (float)im->w / (float)D;          /* sprite texels per screen pixel */
        float sharp = fmaxf(1.0f / s, 1.5f);        /* "sharp bilinear": blend only near texel edges */
        for (int v = 0; v < D; v++) {
            int Y = c->y * zoom + v;
            if (Y < 0 || Y >= scr->h) continue;
            int wy = Y / zoom;
            if (wy >= world->h || (c->sub && wy > c->wl)) continue;
            float fy = (v + 0.5f) * s - 0.5f;
            int y0 = (int)floorf(fy);
            float ty = clampf((fy - y0 - 0.5f) * sharp + 0.5f, 0, 1);
            for (int u = 0; u < D; u++) {
                int X = c->x * zoom + u;
                if (X < 0 || X >= scr->w) continue;
                int wx = X / zoom;
                if (wx >= world->w) continue;
                size_t o = (size_t)wy * world->w + wx;
                if (r->own[o] != k + 1) continue;
                int d[3];
                float mk = 1;
                int mode = overlay_mode(world->px[o], r->hwr[o], d, &mk);
                if (mode == 2) continue;
                float fx = (u + 0.5f) * s - 0.5f;
                if (c->flip) fx = (float)im->w - 1 - fx;
                int x0 = (int)floorf(fx);
                float tx = clampf((fx - x0 - 0.5f) * sharp + 0.5f, 0, 1);
                /* premultiplied bilinear over the four texels */
                float wgt[4] = { (1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty };
                int qx[4] = { x0, x0 + 1, x0, x0 + 1 }, qy[4] = { y0, y0, y0 + 1, y0 + 1 };
                float ar = 0, ag = 0, ab = 0, aa = 0;
                for (int q = 0; q < 4; q++) {
                    if (wgt[q] <= 0) continue;
                    int ta;
                    uint32_t col = hi_texel(im, gl, c, qx[q], qy[q], &ta);
                    float w = wgt[q] * ta;
                    ar += CR(col) * w; ag += CG(col) * w; ab += CB(col) * w; aa += w;
                }
                uint32_t bg = r->hbg[o];
                float al = aa / 255.0f * c->alpha;
                float rr = CR(bg), gg = CG(bg), bb = CB(bg);
                if (aa > 0) {
                    rr += (ar / aa - rr) * al;
                    gg += (ag / aa - gg) * al;
                    bb += (ab / aa - bb) * al;
                }
                if (mode == 0) { rr += d[0]; gg += d[1]; bb += d[2]; }
                else { rr *= mk; gg *= mk; bb *= mk; }
                scr->px[(size_t)Y * scr->w + X] = RGB(clampi((int)lroundf(rr), 0, 255), clampi((int)lroundf(gg), 0, 255),
                                                      clampi((int)lroundf(bb), 0, 255));
            }
        }
        if (c->stick) hires_stick(r, scr, world, zoom, k + 1, c);
    }
}

int render_dump_lower(const Renderer *r, const char *path)
{
    return r->lower_ok ? canvas_write_png(&r->lower, path) : -1;
}

void render_map(Renderer *r, Game *g, Canvas *dst, int tx, int ty, int tw, int th)
{
    if (!r->full.px) {
        r->full.w = MAP_W * TILE;
        r->full.h = MAP_H * TILE;
        r->full.px = calloc((size_t)r->full.w * r->full.h, 4);
        if (!r->full.px) return;
    }
    Level *lv = game_level(g);
    if (lv->light_dirty) level_relight(lv);
    View v = { lv, game_mobs(g, g->depth), &g->player, true, true, 0, 0, g->time, g->visf, g->plight,
               game_items(g, g->depth), g->torch_lit, false, 0, 0, 0, 0 };
    Canvas *cv = &r->full;
    compute_light(r, &v);
    memset(cv->px, 0, (size_t)cv->w * cv->h * 4);
    draw_ground(r, cv, &v);
    draw_reflections(r, cv, &v);
    draw_items(r, cv, &v);
    draw_rows(r, cv, &v);
    draw_effects(r, cv, &v, 0);
    cv_downscale(dst, cv, tx * TILE, ty * TILE - WALL_R, tw * TILE, th * TILE);
    /* lift the shadows: a map should be readable */
    static uint8_t lift[256];
    if (!lift[255])
        for (int i = 0; i < 256; i++) lift[i] = (uint8_t)lroundf(255 * powf(i / 255.0f, 0.62f));
    for (size_t i = 0, n = (size_t)dst->w * dst->h; i < n; i++) {
        uint32_t c = dst->px[i];
        dst->px[i] = RGB(lift[CR(c)], lift[CG(c)], lift[CB(c)]);
    }
}

const Image *render_item_icons(const Renderer *r) { return r->t.item; }

int render_load_sprites(Renderer *r, const char *assets)
{
    int n = tiles_load_sprites(&r->t, assets);
    /* the lantern is what glows on the player: hold a torch in that hand */
    const Image *gl = &r->t.mob_glow[MK_PLAYER];
    long sx = 0, sy = 0, cnt = 0;
    for (int y = 0; y < gl->h; y++)
        for (int x = 0; x < gl->w; x++)
            if (CA(gl->px[y * gl->w + x])) { sx += x; sy += y; cnt++; }
    if (cnt) { r->hand_x = (int)(sx / cnt); r->hand_y = (int)(sy / cnt); }
    r->any_hi = false;
    for (int k = 0; k < MK_COUNT; k++) r->any_hi |= r->t.mob_hi[k].px != NULL;
    return n;
}
