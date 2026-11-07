#include "map.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool tile_opaque(Tile t) { return t == T_ROCK || t == T_WALL || t == T_DOOR; }
bool tile_walkable(Tile t) { return t == T_FLOOR || t == T_DOOR_OPEN || t == T_DOOR || t == T_STAIRS_DOWN || t == T_STAIRS_UP || t == T_RUBBLE; }
bool tile_solid(Tile t) { return t == T_ROCK || t == T_WALL; }

const char *tile_name(Tile t)
{
    static const char *n[T_COUNT] = {
        "solid rock", "stone wall", "stone floor", "wooden door", "open door",
        "chasm", "stairs down", "stairs up", "rubble",
    };
    return t < T_COUNT ? n[t] : "?";
}

const char *tile_desc(Tile t)
{
    switch (t) {
    case T_WALL:        return "Rough-hewn blocks, cold to the touch.";
    case T_FLOOR:       return "Worn flagstones.";
    case T_DOOR:        return "A heavy door. It blocks sight and light until opened.";
    case T_DOOR_OPEN:   return "An open doorway.";
    case T_CHASM:       return "The floor falls away into the level below. Jumping is quick but the fall hurts.";
    case T_STAIRS_DOWN: return "A stairway leading deeper into the dark.";
    case T_STAIRS_UP:   return "Stairs climbing back toward the surface.";
    case T_RUBBLE:      return "Broken stone litters the floor.";
    default:            return "";
    }
}

/* ------------------------------------------------------------------ FOV */

/* Recursive shadowcasting over the eight octants. */
static const int OCT[8][4] = {
    { 1, 0, 0, 1 }, { 0, 1, 1, 0 }, { 0, -1, 1, 0 }, { -1, 0, 0, 1 },
    { -1, 0, 0, -1 }, { 0, -1, -1, 0 }, { 0, 1, -1, 0 }, { 1, 0, 0, -1 },
};

static void cast(const Level *lv, int ox, int oy, int row, float start, float end, int radius,
                 int xx, int xy, int yx, int yy, FovMark mark, void *ctx)
{
    if (start < end) return;
    float new_start = 0;
    for (int j = row; j <= radius; j++) {
        int dx = -j - 1, dy = -j;
        bool blocked = false;
        while (dx <= 0) {
            dx++;
            int X = ox + dx * xx + dy * xy, Y = oy + dx * yx + dy * yy;
            float ls = (dx - 0.5f) / (dy + 0.5f), rs = (dx + 0.5f) / (dy - 0.5f);
            if (start < rs) continue;
            if (end > ls) break;
            float d2 = (float)(dx * dx + dy * dy);
            if (d2 <= (float)radius * radius + radius && in_map(X, Y)) mark(ctx, X, Y, sqrtf(d2));
            bool op = !in_map(X, Y) || tile_opaque((Tile)lv->t[Y][X]);
            if (blocked) {
                if (op) { new_start = rs; continue; }
                blocked = false;
                start = new_start;
            } else if (op && j < radius) {
                blocked = true;
                cast(lv, ox, oy, j + 1, start, ls, radius, xx, xy, yx, yy, mark, ctx);
                new_start = rs;
            }
        }
        if (blocked) break;
    }
}

void fov_cast(const Level *lv, int ox, int oy, int radius, FovMark mark, void *ctx)
{
    if (!in_map(ox, oy)) return;
    mark(ctx, ox, oy, 0);
    for (int o = 0; o < 8; o++)
        cast(lv, ox, oy, 1, 1.0f, 0.0f, radius, OCT[o][0], OCT[o][1], OCT[o][2], OCT[o][3], mark, ctx);
}

/* -------------------------------------------------------------- lighting */

typedef struct { Level *lv; int li; } LightCtx;

static void light_mark(void *vctx, int x, int y, float d)
{
    LightCtx *c = vctx;
    Light *l = &c->lv->lights[c->li];
    if (d > l->radius) return;
    float k = 1.0f - d / l->radius;
    float w = k * k * (0.35f + 0.65f * k);  /* pools of light rather than an even fill */
    if (d < 0.5f) w = 1.0f;
    int n = c->lv->cn[c->li];
    if (n >= CONTRIB_MAX) return;
    c->lv->ci[c->li][n] = (uint16_t)(y * MAP_W + x);
    c->lv->cw[c->li][n] = w;
    c->lv->cn[c->li] = n + 1;
}

void level_relight(Level *lv)
{
    memset(lv->lr, 0, sizeof lv->lr);
    memset(lv->lg, 0, sizeof lv->lg);
    memset(lv->lb, 0, sizeof lv->lb);
    for (int i = 0; i < lv->nlights; i++) {
        Light *l = &lv->lights[i];
        lv->cn[i] = 0;
        if (!l->lit) continue;
        LightCtx c = { lv, i };
        int r = (int)ceilf(l->radius);
        if (r > LIGHT_MAXR) r = LIGHT_MAXR;
        fov_cast(lv, l->x, l->y, r, light_mark, &c);
        for (int k = 0; k < lv->cn[i]; k++) {
            int idx = lv->ci[i][k];
            float w = lv->cw[i][k];
            (&lv->lr[0][0])[idx] += l->r * w;
            (&lv->lg[0][0])[idx] += l->g * w;
            (&lv->lb[0][0])[idx] += l->b * w;
        }
    }
    lv->light_dirty = false;
}

float level_brightness(const Level *lv, int x, int y)
{
    if (!in_map(x, y)) return 0;
    return (lv->lr[y][x] + lv->lg[y][x] + lv->lb[y][x]) / 3.0f + lv->ambient;
}

float light_flicker(const Light *l, float t)
{
    if (l->kind == L_CRYSTAL || l->kind == L_MOSS) return 0.9f + 0.1f * sinf(t * 1.3f + l->phase);
    float p = l->phase;
    float f = 0.86f + 0.06f * sinf(t * 7.1f + p) + 0.05f * sinf(t * 12.7f + p * 2.3f) +
              0.03f * sinf(t * 23.0f + p * 4.1f);
    return f;
}

int level_add_light(Level *lv, LightKind k, int x, int y, int fx, int fy, bool lit, int room)
{
    if (lv->nlights >= MAX_LIGHTS) return -1;
    Light *l = &lv->lights[lv->nlights];
    memset(l, 0, sizeof *l);
    l->kind = k;
    l->x = x; l->y = y; l->fx = fx; l->fy = fy;
    l->lit = lit;
    l->room = room;
    l->phase = (float)(hash2(fx, fy, lv->seed) % 6283) / 1000.0f;
    switch (k) {
    case L_SCONCE:  l->r = 1.30f; l->g = 0.78f; l->b = 0.40f; l->radius = 8.0f; break;
    case L_BRAZIER: l->r = 1.35f; l->g = 0.80f; l->b = 0.38f; l->radius = 8.5f; break;
    case L_CRYSTAL: l->r = 0.32f; l->g = 0.78f; l->b = 1.50f; l->radius = 7.5f; break;
    case L_MOSS:    l->r = 0.30f; l->g = 0.95f; l->b = 0.80f; l->radius = 4.5f; break;
    case L_PLAYER:  l->r = 0.85f; l->g = 0.62f; l->b = 0.38f; l->radius = 3.5f; break;
    }
    lv->light_dirty = true;
    return lv->nlights++;
}

Light *level_light_on(Level *lv, int fx, int fy)
{
    for (int i = 0; i < lv->nlights; i++)
        if (lv->lights[i].fx == fx && lv->lights[i].fy == fy) return &lv->lights[i];
    return NULL;
}

/* ------------------------------------------------------------ generator */

static void carve(Level *lv, int x, int y, int room)
{
    if (x < 1 || y < 1 || x >= MAP_W - 1 || y >= MAP_H - 1) return;
    if (lv->t[y][x] == T_ROCK) {
        lv->t[y][x] = T_FLOOR;
        if (room < 0) lv->f[y][x] |= F_CORR;
        else lv->room[y][x] = (int8_t)room;
    }
}

static bool rect_clear(const Level *lv, int x, int y, int w, int h, int gap)
{
    if (x - gap < 1 || y - gap < 1 || x + w + gap > MAP_W - 1 || y + h + gap > MAP_H - 1) return false;
    for (int j = y - gap; j < y + h + gap; j++)
        for (int i = x - gap; i < x + w + gap; i++)
            if (lv->t[j][i] != T_ROCK) return false;
    return true;
}

static void room_center(const Level *lv, int r, Rng *rng, int *cx, int *cy)
{
    const Room *rm = &lv->rooms[r];
    if (!rm->cave) {
        *cx = rm->x + rm->w / 2;
        *cy = rm->y + rm->h / 2;
        return;
    }
    for (int tries = 0; tries < 400; tries++) {
        int x = rng_range(rng, rm->x, rm->x + rm->w - 1), y = rng_range(rng, rm->y, rm->y + rm->h - 1);
        if (lv->room[y][x] == r) { *cx = x; *cy = y; return; }
    }
    *cx = rm->x + rm->w / 2;
    *cy = rm->y + rm->h / 2;
}

static void corridor(Level *lv, Rng *rng, int ax, int ay, int bx, int by)
{
    bool hfirst = rng_chance(rng, 50);
    int x = ax, y = ay;
    if (hfirst) {
        while (x != bx) { carve(lv, x, y, -1); x += bx > x ? 1 : -1; }
        while (y != by) { carve(lv, x, y, -1); y += by > y ? 1 : -1; }
    } else {
        while (y != by) { carve(lv, x, y, -1); y += by > y ? 1 : -1; }
        while (x != bx) { carve(lv, x, y, -1); x += bx > x ? 1 : -1; }
    }
    carve(lv, x, y, -1);
}

/* A cavern covering the marked tiles (dilated and roughened). */
static void make_cave(Level *lv, Rng *rng, const uint8_t (*mask)[MAP_W])
{
    static uint8_t a[MAP_H][MAP_W], b[MAP_H][MAP_W];
    memset(a, 0, sizeof a);
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            if (!mask[y][x]) continue;
            for (int j = -3; j <= 3; j++)
                for (int i = -3; i <= 3; i++) {
                    int X = x + i, Y = y + j;
                    if (!in_map(X, Y)) continue;
                    int d = abs(i) > abs(j) ? abs(i) : abs(j);
                    if (d <= 1 || (d == 2 && rng_chance(rng, 80)) || (d == 3 && rng_chance(rng, 35)))
                        a[Y][X] = 1;
                }
        }
    for (int it = 0; it < 3; it++) {
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) {
                int n = 0;
                for (int j = -1; j <= 1; j++)
                    for (int i = -1; i <= 1; i++)
                        n += in_map(x + i, y + j) && a[y + j][x + i];
                b[y][x] = mask[y][x] || n >= 5;
            }
        memcpy(a, b, sizeof a);
    }
    int r = lv->nrooms++;
    Room *rm = &lv->rooms[r];
    int x0 = MAP_W, y0 = MAP_H, x1 = -1, y1 = -1;
    for (int y = 2; y < MAP_H - 2; y++)
        for (int x = 2; x < MAP_W - 2; x++) {
            if (!a[y][x]) continue;
            lv->t[y][x] = T_FLOOR;
            lv->room[y][x] = (int8_t)r;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
        }
    *rm = (Room){ x0, y0, x1 - x0 + 1, y1 - y0 + 1, false, true };
}

static bool is_door_spot(const Level *lv, int x, int y)
{
    Tile n = tile_at(lv, x, y - 1), s = tile_at(lv, x, y + 1), w = tile_at(lv, x - 1, y), e = tile_at(lv, x + 1, y);
    bool ns_open = tile_walkable(n) && tile_walkable(s), we_wall = w == T_WALL && e == T_WALL;
    bool we_open = tile_walkable(w) && tile_walkable(e), ns_wall = n == T_WALL && s == T_WALL;
    return (ns_open && we_wall) || (we_open && ns_wall);
}

/* breadth-first distances over walkable tiles (and closed doors) */
static void bfs(const Level *lv, int sx, int sy, int16_t (*dist)[MAP_W])
{
    static int qx[MAP_W * MAP_H], qy[MAP_W * MAP_H];
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) dist[y][x] = -1;
    int h = 0, t = 0;
    dist[sy][sx] = 0;
    qx[t] = sx; qy[t++] = sy;
    while (h < t) {
        int x = qx[h], y = qy[h++];
        for (int d = 0; d < 4; d++) {
            int X = x + (d == 0) - (d == 1), Y = y + (d == 2) - (d == 3);
            if (!in_map(X, Y) || dist[Y][X] >= 0 || !tile_walkable((Tile)lv->t[Y][X])) continue;
            dist[Y][X] = (int16_t)(dist[y][x] + 1);
            qx[t] = X; qy[t++] = Y;
        }
    }
}

static void pick_floor_in_room(const Level *lv, Rng *rng, int r, int *ox, int *oy)
{
    const Room *rm = &lv->rooms[r];
    for (int tries = 0; tries < 500; tries++) {
        int x = rng_range(rng, rm->x, rm->x + rm->w - 1), y = rng_range(rng, rm->y, rm->y + rm->h - 1);
        if (lv->room[y][x] == r && lv->t[y][x] == T_FLOOR) { *ox = x; *oy = y; return; }
    }
    *ox = rm->x; *oy = rm->y;
}

void level_generate(Level *lv, int depth, uint32_t seed, const uint8_t (*below)[MAP_W], uint8_t (*holes)[MAP_W])
{
    memset(lv, 0, sizeof *lv);
    lv->depth = depth;
    lv->seed = seed;
    lv->ambient = 0.015f;
    memset(lv->room, -1, sizeof lv->room);
    Rng rng = { seed * 2654435761u + 77 };

    if (below) {
        bool any = false;
        for (int y = 0; y < MAP_H && !any; y++)
            for (int x = 0; x < MAP_W; x++) if (below[y][x]) { any = true; break; }
        if (any) make_cave(lv, &rng, below);
    }

    int want = rng_range(&rng, 10, 14);
    for (int tries = 0; tries < 600 && lv->nrooms < want && lv->nrooms < MAX_ROOMS; tries++) {
        int w = rng_range(&rng, 5, 12), h = rng_range(&rng, 4, 8);
        int x = rng_range(&rng, 2, MAP_W - w - 3), y = rng_range(&rng, 2, MAP_H - h - 3);
        if (!rect_clear(lv, x, y, w, h, 3)) continue;
        int r = lv->nrooms++;
        lv->rooms[r] = (Room){ x, y, w, h, false, false };
        for (int j = y; j < y + h; j++)
            for (int i = x; i < x + w; i++) carve(lv, i, j, r);
    }

    /* connect: a spanning tree over the room centres, plus a few loops */
    int cx[MAX_ROOMS], cy[MAX_ROOMS];
    for (int r = 0; r < lv->nrooms; r++) room_center(lv, r, &rng, &cx[r], &cy[r]);
    bool in[MAX_ROOMS] = { true };
    for (int k = 1; k < lv->nrooms; k++) {
        int best = -1, ba = 0, bb = 0;
        for (int a = 0; a < lv->nrooms; a++) {
            if (!in[a]) continue;
            for (int b = 0; b < lv->nrooms; b++) {
                if (in[b]) continue;
                int d = abs(cx[a] - cx[b]) + abs(cy[a] - cy[b]);
                if (best < 0 || d < best) { best = d; ba = a; bb = b; }
            }
        }
        in[bb] = true;
        corridor(lv, &rng, cx[ba], cy[ba], cx[bb], cy[bb]);
    }
    for (int k = 0; k < 2 + lv->nrooms / 5; k++) {
        int a = rng_range(&rng, 0, lv->nrooms - 1), b = rng_range(&rng, 0, lv->nrooms - 1);
        if (a != b) corridor(lv, &rng, cx[a], cy[a], cx[b], cy[b]);
    }

    /* walls around everything open */
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            if (lv->t[y][x] != T_ROCK) continue;
            for (int j = -1; j <= 1; j++)
                for (int i = -1; i <= 1; i++) {
                    Tile n = tile_at(lv, x + i, y + j);
                    if (n != T_ROCK && n != T_WALL) lv->t[y][x] = T_WALL;
                }
        }

    /* doors where a corridor meets a room */
    for (int y = 1; y < MAP_H - 1; y++)
        for (int x = 1; x < MAP_W - 1; x++) {
            if (!(lv->f[y][x] & F_CORR) || lv->t[y][x] != T_FLOOR) continue;
            bool by_room = false;
            for (int d = 0; d < 4; d++) {
                int X = x + (d == 0) - (d == 1), Y = y + (d == 2) - (d == 3);
                if (lv->room[Y][X] >= 0 && !lv->rooms[lv->room[Y][X]].cave) by_room = true;
            }
            if (!by_room || !is_door_spot(lv, x, y)) continue;
            bool near_door = false;
            for (int j = -1; j <= 1; j++)
                for (int i = -1; i <= 1; i++)
                    if (lv->t[y + j][x + i] == T_DOOR || lv->t[y + j][x + i] == T_DOOR_OPEN) near_door = true;
            if (!near_door && rng_chance(&rng, 55)) lv->t[y][x] = rng_chance(&rng, 25) ? T_DOOR_OPEN : T_DOOR;
        }

    /* the start room: the first ordinary room */
    int start = 0;
    while (start < lv->nrooms - 1 && lv->rooms[start].cave) start++;
    pick_floor_in_room(lv, &rng, start, &lv->upx, &lv->upy);
    lv->t[lv->upy][lv->upx] = T_STAIRS_UP;

    /* down stairs: the farthest room */
    static int16_t dist[MAP_H][MAP_W];
    bfs(lv, lv->upx, lv->upy, dist);
    int far = start, fd = -1;
    for (int r = 0; r < lv->nrooms; r++) {
        if (lv->rooms[r].cave) continue;
        int d = dist[cy[r]][cx[r]];
        if (d > fd) { fd = d; far = r; }
    }
    pick_floor_in_room(lv, &rng, far, &lv->downx, &lv->downy);
    lv->t[lv->downy][lv->downx] = T_STAIRS_DOWN;

    /* chasms: open the middle of a big room onto the level below */
    if (holes) memset(holes, 0, sizeof(uint8_t) * MAP_W * MAP_H);
    int nch = depth >= 2 ? rng_range(&rng, 1, 2) : 1;
    for (int k = 0; k < nch; k++) {
        int pick = -1, area = 0;
        for (int r = 0; r < lv->nrooms; r++) {
            Room *rm = &lv->rooms[r];
            if (rm->cave || r == start || rm->w < 7 || rm->h < 6) continue;
            if (lv->room[lv->downy][lv->downx] == r) continue;
            bool has = false;
            for (int y = rm->y; y < rm->y + rm->h && !has; y++)
                for (int x = rm->x; x < rm->x + rm->w; x++) if (lv->t[y][x] == T_CHASM) { has = true; break; }
            if (has) continue;
            int a = rm->w * rm->h + rng_range(&rng, 0, 20);
            if (a > area) { area = a; pick = r; }
        }
        if (pick < 0) break;
        Room *rm = &lv->rooms[pick];
        float ex = (rm->w - 2) / 2.0f - 0.2f, ey = (rm->h - 2) / 2.0f - 0.2f;
        float ccx = rm->x + rm->w / 2.0f, ccy = rm->y + rm->h / 2.0f;
        float ph = rng_f(&rng) * 6.28f;
        for (int y = rm->y + 1; y < rm->y + rm->h - 1; y++)
            for (int x = rm->x + 1; x < rm->x + rm->w - 1; x++) {
                if (lv->t[y][x] != T_FLOOR) continue;
                float dx = (x + 0.5f - ccx) / ex, dy = (y + 0.5f - ccy) / ey;
                float ang = atan2f(dy, dx);
                float rr = 0.92f + 0.14f * sinf(ang * 3 + ph) + 0.08f * sinf(ang * 5 - ph);
                if (dx * dx + dy * dy <= rr * rr) {
                    lv->t[y][x] = T_CHASM;
                    if (holes) holes[y][x] = 1;
                }
            }
    }

    /* lighting: some rooms start dark, their sconces cold */
    for (int r = 0; r < lv->nrooms; r++) {
        Room *rm = &lv->rooms[r];
        if (rm->cave) {
            /* crystals grow from the cavern walls */
            int placed = 0;
            for (int tries = 0; tries < 600 && placed < 4 + rm->w * rm->h / 40; tries++) {
                int x = rng_range(&rng, rm->x - 1, rm->x + rm->w), y = rng_range(&rng, rm->y - 1, rm->y + rm->h);
                if (tile_at(lv, x, y) != T_WALL || tile_at(lv, x, y + 1) != T_FLOOR) continue;
                if (level_light_on(lv, x, y) || level_light_on(lv, x - 1, y) || level_light_on(lv, x + 1, y)) continue;
                level_add_light(lv, L_CRYSTAL, x, y + 1, x, y, true, r);
                placed++;
            }
            /* glowing moss right under the chasms above, so the view down has something to show */
            int moss = 0;
            for (int tries = 0; tries < 800 && moss < 2 + rm->w * rm->h / 50; tries++) {
                int x = rng_range(&rng, rm->x, rm->x + rm->w - 1), y = rng_range(&rng, rm->y, rm->y + rm->h - 1);
                if (lv->t[y][x] != T_FLOOR || lv->room[y][x] != r) continue;
                if (below && !below[y][x] && rng_chance(&rng, 75)) continue;
                bool near = false;
                for (int i = 0; i < lv->nlights; i++)
                    if (lv->lights[i].kind == L_MOSS && abs(lv->lights[i].x - x) + abs(lv->lights[i].y - y) < 4) near = true;
                if (near) continue;
                level_add_light(lv, L_MOSS, x, y, x, y, true, r);
                moss++;
            }
            continue;
        }
        rm->dark = r != start && rng_chance(&rng, 30 + depth * 4);
        int want_s = rm->w >= 9 ? 2 : 1, placed = 0;
        for (int tries = 0; tries < 60 && placed < want_s; tries++) {
            int x = rng_range(&rng, rm->x, rm->x + rm->w - 1), y = rm->y - 1;
            if (tile_at(lv, x, y) != T_WALL || !tile_walkable(tile_at(lv, x, y + 1))) continue;
            if (level_light_on(lv, x, y) || level_light_on(lv, x - 1, y) || level_light_on(lv, x + 1, y) ||
                level_light_on(lv, x - 2, y) || level_light_on(lv, x + 2, y))
                continue;
            level_add_light(lv, L_SCONCE, x, y + 1, x, y, !rm->dark, r);
            placed++;
        }
    }

    /* scattered rubble */
    for (int y = 1; y < MAP_H - 1; y++)
        for (int x = 1; x < MAP_W - 1; x++)
            if (lv->t[y][x] == T_FLOOR && (hash2(x, y, seed) % 100) < 3) lv->t[y][x] = T_RUBBLE;

    level_relight(lv);
}
