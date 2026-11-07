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
    float x, y, vx, vy, life, max, size;
    uint32_t c;
} Particle;

struct Renderer {
    Tiles t;
    CF corner[CH][CW];
    float chasmc[CH][CW];
    float tl[MAP_H][MAP_W][3];
    Canvas lower;
    bool lower_ok;
    int lower_depth;
    float camx, camy;
    bool cam_ok;
    int cam_depth;
    Particle part[MAX_PART];
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
    r->rng.s = 1234;
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
    free(r->lower.px);
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
        float f = 0.93f + 0.04f * sinf(v->time * 5.3f) + 0.03f * sinf(v->time * 11.1f);
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) {
                float w = v->plight[y][x] * f;
                if (w <= 0) continue;
                r->tl[y][x][0] += 0.80f * w;
                r->tl[y][x][1] += 0.58f * w;
                r->tl[y][x][2] += 0.34f * w;
            }
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
            float mist = 0.18f + 0.30f * n * n2 * 1.6f;
            /* the walls of the pit shade what lies below them */
            float sh = 1.0f;
            if (oN) sh *= smooth01(0, 30, (float)j);
            else if (oNW && !oW) sh *= 0.25f + 0.75f * smooth01(0, 14, sqrtf((float)(i * i + j * j)));
            if (oNE && !oN && !oE) sh *= 0.25f + 0.75f * smooth01(0, 14, sqrtf((float)((31 - i) * (31 - i) + j * j)));
            if (oW) sh *= 0.2f + 0.8f * smooth01(0, 12, (float)i);
            if (oE) sh *= 0.2f + 0.8f * smooth01(0, 12, (float)(31 - i));
            if (oS) sh *= 0.55f + 0.45f * smooth01(0, 6, (float)(31 - j));
            c = mix(0x000000, c, (int)(256 * sh));
            c = mix(c, 0x000000, (int)(256 * (0.15f + 0.45f * (1 - rim))));
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
            uint32_t h = hash2(tx, ty, lv->seed);
            const Image *im = &r->t.floor[h % N_FLOOR];
            const Image *over = NULL;
            if (t == T_STAIRS_DOWN) im = &r->t.stairs_dn;
            else if (t == T_STAIRS_UP) im = &r->t.stairs_up;
            else if (t == T_DOOR_OPEN) im = &r->t.door_open;
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
        }
}

static void draw_wall(Renderer *r, Canvas *cv, const View *v, int tx, int ty)
{
    const Level *lv = v->lv;
    int sx = tx * TILE - v->camx, sy = ty * TILE - v->camy;
    uint32_t h = hash2(tx, ty, lv->seed + 1);
    bool nN = !tile_solid(tile_at(lv, tx, ty - 1)), nS = !tile_solid(tile_at(lv, tx, ty + 1));
    bool nW = !tile_solid(tile_at(lv, tx - 1, ty)), nE = !tile_solid(tile_at(lv, tx + 1, ty));
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
            if (nW) d = i == 0 ? -40 : i < 3 && d >= 0 ? 26 : d;
            if (nE) d = i == TILE - 1 ? -40 : i > TILE - 4 && d >= 0 ? 22 : d;
            top.px[j * TILE + i] = d ? (0xff000000u | add_sat(c, d, d, d * 9 / 10)) : c;
        }
    draw_lit(cv, sx, sy - WALL_R, &top, TILE, TILE, c00, c10, c01, c11, 0.62f, 0.78f, 0, v->fog, NULL, 0);

    if (nS) {
        /* the front face takes the light of the floor in front of it */
        CF a = *c01, b = *c11;
        draw_lit(cv, sx, sy + TILE - WALL_R, &r->t.wface[(h >> 4) % N_WALL], TILE, WALL_R, &a, &b, &a, &b,
                 0.7f, 1.0f, 0, v->fog, NULL, 0);
    }
}

static void draw_door(Renderer *r, Canvas *cv, const View *v, int tx, int ty)
{
    int sx = tx * TILE - v->camx, sy = ty * TILE - v->camy;
    const CF *c00 = &r->corner[ty][tx], *c10 = &r->corner[ty][tx + 1];
    const CF *c01 = &r->corner[ty + 1][tx], *c11 = &r->corner[ty + 1][tx + 1];
    draw_lit(cv, sx, sy - WALL_R, &r->t.door, TILE, TILE + WALL_R, c00, c10, c01, c11, 0.8f, 1.0f, 0, v->fog, NULL, 0);
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
                int x = sx + 10 + i, y = sy + TILE - im->h - 1 + j;
                if ((unsigned)x >= (unsigned)cv->w || (unsigned)y >= (unsigned)cv->h) continue;
                float lr = c.r + k, lg = c.g + k * 0.6f, lb = c.b + k * 0.3f;
                uint32_t lit = lightc(a & 0xffffff, lr, lg, lb);
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

static void draw_mob(Renderer *r, Canvas *cv, const View *v, const Mob *m, bool is_player)
{
    const Level *lv = v->lv;
    float alpha = 1.0f;
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
    int px = (int)lroundf(m->rx * TILE + ox) - v->camx;
    int py = (int)lroundf(m->ry * TILE + oy - 3 + breathe) - v->camy;
    CF c;
    light_at_px(r, m->rx * TILE + 16, m->ry * TILE + 20, &c);
    float floor_l = is_player ? 0.32f : 0.12f;
    float lr = c.r < floor_l ? floor_l : c.r, lg = c.g < floor_l ? floor_l : c.g, lb = c.b < floor_l * 1.3f ? floor_l * 1.3f : c.b;
    /* shadow */
    for (int j = -3; j <= 3; j++)
        for (int i = -10; i <= 10; i++) {
            float d = (i * i) / 100.0f + (j * j) / 9.0f;
            if (d > 1) continue;
            int x = px + 16 + i, y = py + 3 - (int)breathe + 29 + j;
            if ((unsigned)x < (unsigned)cv->w && (unsigned)y < (unsigned)cv->h)
                blend_px(&cv->px[(size_t)y * cv->w + x], 0, (int)(120 * (1 - d) * alpha));
        }
    const Image *im = &r->t.mob[m->kind], *gl = &r->t.mob_glow[m->kind];
    int fl = m->flash > 0 ? (int)(m->flash * 230) : 0;
    for (int j = 0; j < TILE; j++) {
        int y = py + j;
        if ((unsigned)y >= (unsigned)cv->h) continue;
        for (int i = 0; i < TILE; i++) {
            int x = px + i;
            if ((unsigned)x >= (unsigned)cv->w) continue;
            int si = m->flip ? TILE - 1 - i : i;
            uint32_t a = im->px[j * TILE + si];
            if (!CA(a)) continue;
            uint32_t g = gl->px[j * TILE + si];
            uint32_t col = CA(g) ? (g & 0xffffff) : lightc(a & 0xffffff, lr * 1.1f, lg * 1.1f, lb * 1.1f);
            if (m->asleep && !CA(g)) col = mix(col, 0x202838, 60);
            if (fl) col = mix(col, 0xffffff, fl);
            blend_px(&cv->px[(size_t)y * cv->w + x], col, (int)(255 * alpha));
        }
    }
    if (is_player) {
        /* the lantern's halo */
        int lx = px + (m->flip ? TILE - 7 : 7), ly = py + 21;
        cv_glow(cv, lx, ly, 18, 0xffb050, 0.30f);
    }
    if (m->kind == MK_GOBLIN || m->kind == MK_RAT) {
        /* eyes catch the light */
        (void)0;
    }
}

static void spawn(Renderer *r, float x, float y, float vx, float vy, float life, uint32_t c, float size)
{
    for (int i = 0; i < MAX_PART; i++) {
        Particle *p = &r->part[i];
        if (p->life > 0) continue;
        *p = (Particle){ x, y, vx, vy, life, life, size, c };
        return;
    }
}

static void draw_effects(Renderer *r, Canvas *cv, const View *v, float dt)
{
    Level *lv = v->lv;
    for (int i = 0; i < lv->nlights; i++) {
        const Light *l = &lv->lights[i];
        if (!l->lit) continue;
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
    /* particles */
    for (int i = 0; i < MAX_PART; i++) {
        Particle *p = &r->part[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vx += sinf(v->time * 3 + i) * 12 * dt;
        float k = p->life / p->max;
        if (k <= 0) continue;
        int x = (int)p->x - v->camx, y = (int)p->y - v->camy;
        if ((unsigned)x >= (unsigned)cv->w || (unsigned)y >= (unsigned)cv->h) continue;
        uint32_t c = p->c;
        float s = k * 0.9f;
        cv->px[(size_t)y * cv->w + x] = add_sat(cv->px[(size_t)y * cv->w + x], (int)(CR(c) * s), (int)(CG(c) * s), (int)(CB(c) * s));
        if (p->size > 0.85f && k > 0.5f) cv_glow(cv, x, y, 3, c, 0.25f * k);
    }
}

/* walls, doors, fixtures and creatures, row by row so nearer things cover farther ones */
static void draw_rows(Renderer *r, Canvas *cv, const View *v)
{
    const Level *lv = v->lv;
    int tx0 = (int)floorf((float)v->camx / TILE) - 1, ty0 = (int)floorf((float)v->camy / TILE) - 1;
    int tx1 = (v->camx + cv->w) / TILE + 1, ty1 = (v->camy + cv->h + WALL_R) / TILE + 1;
    for (int ty = ty0; ty <= ty1; ty++) {
        if (ty < 0 || ty >= MAP_H) continue;
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
        for (int i = 0; i < MAX_MOBS; i++) {
            const Mob *m = &v->mobs[i];
            if (m->alive && (int)floorf(m->ry + 0.5f) == ty) draw_mob(r, cv, v, m, false);
        }
        if (v->player && (int)floorf(v->player->ry + 0.5f) == ty) draw_mob(r, cv, v, v->player, true);
    }
}

static void render_lower(Renderer *r, Game *g)
{
    Level *lv = game_level_below(g);
    r->lower_ok = false;
    if (!lv) return;
    if (lv->light_dirty) level_relight(lv);
    View v = { lv, game_mobs(g, g->depth + 1), NULL, false, false, 0, 0, 0, NULL, NULL };
    compute_light(r, &v);
    memset(r->lower.px, 0, (size_t)r->lower.w * r->lower.h * 4);
    draw_ground(r, &r->lower, &v);
    draw_rows(r, &r->lower, &v);
    draw_effects(r, &r->lower, &v, 0);
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

void render_world(Renderer *r, Game *g, Canvas *cv, float dt, int hx, int hy, const int *path, int plen)
{
    if (g->level_changed || r->lower_depth != g->depth) {
        render_lower(r, g);
        g->level_changed = false;
        memset(r->part, 0, sizeof r->part);
    }
    Level *lv = game_level(g);
    if (lv->light_dirty) level_relight(lv);
    int camx = (int)lroundf(r->camx), camy = (int)lroundf(r->camy);
    if (g->shake > 0) {
        camx += (int)((rng_f(&r->rng) - 0.5f) * g->shake * 18);
        camy += (int)((rng_f(&r->rng) - 0.5f) * g->shake * 18);
    }
    View v = { lv, game_mobs(g, g->depth), &g->player, true, true, camx, camy, g->time, g->visf, g->plight };
    compute_light(r, &v);
    memset(cv->px, 0, (size_t)cv->w * cv->h * 4);
    draw_ground(r, cv, &v);
    draw_path(cv, &v, path, plen, -1, -1, g->time);
    draw_rows(r, cv, &v);
    draw_effects(r, cv, &v, dt);
    if (hx >= 0) draw_path(cv, &v, NULL, 0, hx, hy, g->time);
}

int render_dump_lower(const Renderer *r, const char *path)
{
    return r->lower_ok ? canvas_write_png(&r->lower, path) : -1;
}
