#include "map.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool tile_opaque(Tile t) { return t == T_ROCK || t == T_WALL || t == T_DOOR; }
bool tile_walkable(Tile t)
{
    return t == T_FLOOR || t == T_DOOR_OPEN || t == T_DOOR || t == T_STAIRS_DOWN || t == T_STAIRS_UP || t == T_RUBBLE
        || t == T_SHALLOW || t == T_DEEP;
}
bool tile_solid(Tile t) { return t == T_ROCK || t == T_WALL; }

const char *tile_name(Tile t)
{
    static const char *n[T_COUNT] = {
        "solid rock", "stone wall", "stone floor", "wooden door", "open door",
        "chasm", "stairs down", "stairs up", "rubble", "shallow water", "deep water", "forge brazier", "Heartflame",
    };
    return t < T_COUNT ? n[t] : "?";
}

const char *tile_desc(Tile t)
{
    switch (t) {
    case T_WALL:        return "Rough-hewn blocks, cold to the touch.";
    case T_FLOOR:       return "Worn flagstones.";
    case T_DOOR:        return "A heavy door. It blocks sight and light until opened.";
    case T_DOOR_OPEN:   return "An open doorway. Standing beside it, you can close it (c).";
    case T_CHASM:       return "The floor falls away into the level below. Jumping is quick but the fall hurts.";
    case T_STAIRS_DOWN: return "A stairway leading deeper into the dark.";
    case T_STAIRS_UP:   return "Stairs climbing back toward the surface.";
    case T_RUBBLE:      return "Broken stone litters the floor.";
    case T_SHALLOW:     return "Cold, ankle-deep water. Wading through it splashes, and splashing carries.";
    case T_DEEP:        return "Dark water too deep to wade. Swimming will put out a torch, and anything dropped here sinks. "
                               "Goblins won't follow you in.";
    case T_BRAZIER:     return "An old forge's brazier, still burning on something that never runs out. Feed it embers "
                               "to temper your weapon or armour, or to strengthen yourself. No shade comes near it.";
    case T_HEARTH:      return "The great hearth at the bottom of the world. The Heartflame burned here once, and the "
                               "dark has crept up from it ever since it went out.";
    default:            return "";
    }
}

const char *theme_name(int th)
{
    static const char *N[N_THEME] = { "The Upper Halls", "The Mossed Works", "The Brick Vaults", "The Drowned Slate",
                                      "The Black Deeps", "The Hearth of the World" };
    return th >= 0 && th < N_THEME ? N[th] : N[0];
}

const char *trap_name(int k)
{
    static const char *n[TR_COUNT] = { "", "alarm plate", "draught vent", "trapdoor", "snare" };
    return k > 0 && k < TR_COUNT ? n[k] : "trap";
}

const char *trap_desc(int k)
{
    switch (k) {
    case TR_ALARM: return "A loose flagstone over a pressure plate. Step on it and a bell clangs somewhere in the walls, "
                          "waking everything near and bringing it to look.";
    case TR_GUST:  return "An iron grating over a shaft that breathes cold air. Step on it and an icy gust roars up: it "
                          "blows out a torch and leaves a chill in you.";
    case TR_PIT:   return "A trapdoor, its edges barely showing between the flagstones. It drops whatever steps on it to "
                          "the level below.";
    case TR_SNARE: return "A noose of wire laid in the dust and pegged to the floor. It catches an ankle and holds it "
                          "until you tear free; you can still fight while it holds you.";
    default:       return "";
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
    if (l->kind == L_OILFIRE && l->fuel >= 0 && l->fuel < 6) {
        /* the last of the oil */
        float k = 0.3f + 0.7f * (l->fuel / 6.0f);
        return k * (0.82f + 0.1f * sinf(t * 11.0f + l->phase) + 0.08f * sinf(t * 19.0f + l->phase * 2));
    }
    if (l->kind == L_FLOORTORCH && l->fuel >= 0 && l->fuel < 25) {
        /* guttering out */
        float k = 0.35f + 0.65f * (l->fuel / 25.0f);
        return k * (0.8f + 0.12f * sinf(t * 9.3f + l->phase) + 0.08f * sinf(t * 21.0f + l->phase * 3));
    }
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
    l->phase = (float)(hash2(fx, fy, lv->seed + (uint32_t)lv->nlights) % 6283) / 1000.0f;
    l->fuel = -1;
    switch (k) {
    case L_SCONCE:  l->r = 1.30f; l->g = 0.78f; l->b = 0.40f; l->radius = 8.0f; break;
    case L_BRAZIER: l->r = 0.95f; l->g = 0.58f; l->b = 0.28f; l->radius = 8.5f; break;
    case L_CRYSTAL: l->r = 0.32f; l->g = 0.78f; l->b = 1.50f; l->radius = 7.5f; break;
    case L_MOSS:    l->r = 0.30f; l->g = 0.95f; l->b = 0.80f; l->radius = 4.5f; break;
    case L_FLOORTORCH: l->r = 1.30f; l->g = 0.74f; l->b = 0.36f; l->radius = 6.5f; break;
    case L_PLAYER:  l->r = 0.85f; l->g = 0.62f; l->b = 0.38f; l->radius = 3.5f; break;
    case L_OILFIRE: l->r = 1.45f; l->g = 0.72f; l->b = 0.30f; l->radius = 6.0f; break;
    case L_HEART:   l->r = 1.50f; l->g = 0.95f; l->b = 0.46f; l->radius = 9.0f; break;   /* the game sets its size as it kindles */
    }
    lv->light_dirty = true;
    return lv->nlights++;
}

void level_remove_light(Level *lv, int i)
{
    if (i < 0 || i >= lv->nlights) return;
    for (int k = i; k < lv->nlights - 1; k++) {
        lv->lights[k] = lv->lights[k + 1];
        lv->cn[k] = lv->cn[k + 1];
        memcpy(lv->ci[k], lv->ci[k + 1], sizeof lv->ci[k]);
        memcpy(lv->cw[k], lv->cw[k + 1], sizeof lv->cw[k]);
    }
    lv->nlights--;
    lv->light_dirty = true;
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
    /* each separate piece is a cavern of its own, so the corridors find them all
     * (under two chasms far apart there are two caverns, not one with a gap in it) */
    static int q[MAP_W * MAP_H];
    for (int y = 0; y < 2; y++) memset(a[y], 0, MAP_W), memset(a[MAP_H - 1 - y], 0, MAP_W);
    for (int y = 0; y < MAP_H; y++) a[y][0] = a[y][1] = a[y][MAP_W - 1] = a[y][MAP_W - 2] = 0;
    for (int sy = 2; sy < MAP_H - 2; sy++)
        for (int sx = 2; sx < MAP_W - 2; sx++) {
            if (a[sy][sx] != 1) continue;
            int r = lv->nrooms < MAX_ROOMS ? lv->nrooms++ : lv->nrooms - 1;
            bool fresh = r == lv->nrooms - 1 && lv->rooms[r].w == 0;
            int x0 = MAP_W, y0 = MAP_H, x1 = -1, y1 = -1, h = 0, n = 0;
            a[sy][sx] = 2;
            q[n++] = sy * MAP_W + sx;
            while (h < n) {
                int c = q[h++], x = c % MAP_W, y = c / MAP_W;
                lv->t[y][x] = T_FLOOR;
                lv->room[y][x] = (int8_t)r;
                if (x < x0) x0 = x;
                if (y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
                for (int d = 0; d < 4; d++) {
                    int X = x + (d == 0) - (d == 1), Y = y + (d == 2) - (d == 3);
                    if (a[Y][X] != 1) continue;
                    a[Y][X] = 2;
                    q[n++] = Y * MAP_W + X;
                }
            }
            Room *rm = &lv->rooms[r];
            if (!fresh) {       /* out of rooms (never in practice): grow the last one */
                int ox1 = rm->x + rm->w - 1, oy1 = rm->y + rm->h - 1;
                x0 = x0 < rm->x ? x0 : rm->x; y0 = y0 < rm->y ? y0 : rm->y;
                x1 = x1 > ox1 ? x1 : ox1; y1 = y1 > oy1 ? y1 : oy1;
            }
            *rm = (Room){ x0, y0, x1 - x0 + 1, y1 - y0 + 1, false, true, RK_PLAIN, false };
        }
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

/* ---------------------------------------------------------------- lakes */

/* can the player cross this without swimming? */
static bool dry_way(Tile t) { return tile_walkable(t) && t != T_DEEP; }

/* mark everything reachable from (sx, sy) on foot, given the tiles t */
static int reach(const uint8_t (*t)[MAP_W], int sx, int sy, uint8_t (*seen)[MAP_W])
{
    static int q[MAP_W * MAP_H];
    memset(seen, 0, sizeof(uint8_t) * MAP_W * MAP_H);
    int h = 0, n = 0;
    seen[sy][sx] = 1;
    q[n++] = sy * MAP_W + sx;
    while (h < n) {
        int c = q[h++], x = c % MAP_W, y = c / MAP_W;
        for (int d = 0; d < 4; d++) {
            int X = x + (d == 0) - (d == 1), Y = y + (d == 2) - (d == 3);
            if (!in_map(X, Y) || seen[Y][X] || !dry_way((Tile)t[Y][X])) continue;
            seen[Y][X] = 1;
            q[n++] = Y * MAP_W + X;
        }
    }
    return n;
}

/* a lumpy blob, grown by a cellular automaton in a w x h box; returns its size */
static int blob(Rng *rng, int w, int h, uint8_t (*b)[MAP_W])
{
    static uint8_t a[MAP_H][MAP_W], c[MAP_H][MAP_W];
    memset(a, 0, sizeof a);
    for (int y = 1; y < h - 1; y++)
        for (int x = 1; x < w - 1; x++) a[y][x] = rng_chance(rng, 55);
    for (int it = 0; it < 5; it++) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int n = 0;
                for (int j = -1; j <= 1; j++)
                    for (int i = -1; i <= 1; i++)
                        if (x + i >= 0 && y + j >= 0 && x + i < w && y + j < h) n += a[y + j][x + i];
                c[y][x] = x > 0 && y > 0 && x < w - 1 && y < h - 1 && (n >= 5 || (a[y][x] && n >= 4));
            }
        memcpy(a, c, sizeof a);
    }
    /* keep the biggest piece */
    static int q[MAP_W * MAP_H];
    static uint8_t lab[MAP_H][MAP_W];
    memset(lab, 0, sizeof lab);
    int best = 0, best_l = 0;
    for (int y = 0, l = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (!a[y][x] || lab[y][x]) continue;
            l++;
            int qh = 0, qn = 0;
            lab[y][x] = (uint8_t)l;
            q[qn++] = y * MAP_W + x;
            while (qh < qn) {
                int cc = q[qh++], cx = cc % MAP_W, cy = cc / MAP_W;
                for (int d = 0; d < 4; d++) {
                    int X = cx + (d == 0) - (d == 1), Y = cy + (d == 2) - (d == 3);
                    if (X < 0 || Y < 0 || X >= w || Y >= h || !a[Y][X] || lab[Y][X]) continue;
                    lab[Y][X] = (uint8_t)l;
                    q[qn++] = Y * MAP_W + X;
                }
            }
            if (qn > best) { best = qn; best_l = l; }
        }
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) b[y][x] = y < h && x < w && best_l && lab[y][x] == best_l;
    return best;
}

/* Brogue-style lakes: a big blob of deep water with a wreath of shallows, laid
 * across rooms, corridors and the walls between them, wherever it doesn't cut
 * off any part of the level from the stairs. Returns how many were made. */
static int make_lakes(Level *lv, Rng *rng, int want)
{
    static uint8_t b[MAP_H][MAP_W], nt[MAP_H][MAP_W], seen[MAP_H][MAP_W], before[MAP_H][MAP_W];
    int made = 0;
    for (int tries = 0; tries < 30 && made < want; tries++) {
        int w = rng_range(rng, 10, 22), h = rng_range(rng, 7, 13);
        if (blob(rng, w, h, b) < w * h / 4) continue;
        int ox = rng_range(rng, 1, MAP_W - w - 1), oy = rng_range(rng, 1, MAP_H - h - 1);
        memcpy(nt, lv->t, sizeof nt);
        /* what may be flooded: plain ground, rubble, walls and bare rock; well away from the stairs and chasms */
#define FLOODABLE(X, Y) ((lv->t[Y][X] == T_FLOOR || lv->t[Y][X] == T_RUBBLE || lv->t[Y][X] == T_WALL || lv->t[Y][X] == T_ROCK) \
                         && (X) > 1 && (Y) > 1 && (X) < MAP_W - 2 && (Y) < MAP_H - 2)
        static uint8_t keep_off[MAP_H][MAP_W];
        memset(keep_off, 0, sizeof keep_off);
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) {
                Tile t = (Tile)lv->t[y][x];
                int r = t == T_STAIRS_UP || t == T_STAIRS_DOWN ? 3 : t == T_CHASM || t == T_BRAZIER || t == T_HEARTH
                        || t == T_DOOR || t == T_DOOR_OPEN ? 1 : -1;
                if (lv->room[y][x] >= 0 && lv->room[y][x] == lv->hall) r = 1;
                for (int j = -r; j <= r && r >= 0; j++)
                    for (int i = -r; i <= r; i++)
                        if (in_map(x + i, y + j)) keep_off[y + j][x + i] = 1;
            }
        int deep = 0;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int X = ox + x, Y = oy + y;
                if (!b[y][x] || !FLOODABLE(X, Y) || keep_off[Y][X]) continue;
                nt[Y][X] = T_DEEP;
                deep++;
            }
        if (deep < 12) continue;
        /* it has to lie mostly over open ground, and spill across at least two places
         * (rooms, or a room and a corridor): a lake that's all bare rock is a sealed pond */
        {
            int open = 0, places = 0, seen_place[MAX_ROOMS + 1] = { 0 };
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int X = ox + x, Y = oy + y;
                    if (nt[Y][X] != T_DEEP || lv->t[Y][X] == T_DEEP) continue;
                    for (int j = -1; j <= 1; j++)
                        for (int i = -1; i <= 1; i++) {
                            Tile t = tile_at(lv, X + i, Y + j);
                            if (t != T_FLOOR && t != T_RUBBLE) continue;
                            if (!i && !j) open++;
                            int rr = lv->room[Y + j][X + i], k = rr >= 0 ? rr : MAX_ROOMS;
                            if (!seen_place[k]) { seen_place[k] = 1; places++; }
                        }
                }
            if (open * 100 < deep * 40 || places < 2) continue;
        }
        /* the wreath: shallows round the deep water, a tile or two wide */
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) {
                if (nt[y][x] == T_DEEP || !FLOODABLE(x, y) || keep_off[y][x]) continue;
                int dmin = 9;
                for (int j = -2; j <= 2; j++)
                    for (int i = -2; i <= 2; i++) {
                        if (!in_map(x + i, y + j) || nt[y + j][x + i] != T_DEEP || lv->t[y + j][x + i] == T_DEEP) continue;
                        int dd = abs(i) > abs(j) ? abs(i) : abs(j);
                        if (dd < dmin) dmin = dd;
                    }
                if (dmin == 1 || (dmin == 2 && (hash2(x, y, lv->seed ^ 0x1a4e) % 100) < 45)) nt[y][x] = T_SHALLOW;
            }
        /* no slivers of water poking into the stone: where new water in what was wall or
         * rock is mostly closed in by stone, the stone stays */
        for (int it = 0; it < 3; it++)
            for (int y = 1; y < MAP_H - 1; y++)
                for (int x = 1; x < MAP_W - 1; x++) {
                    Tile was = (Tile)lv->t[y][x];
                    if (nt[y][x] == was || !tile_water((Tile)nt[y][x]) || (was != T_WALL && was != T_ROCK)) continue;
                    int stone = 0;
                    for (int d = 0; d < 4; d++) {
                        Tile n = (Tile)nt[y + (d == 2) - (d == 3)][x + (d == 0) - (d == 1)];
                        stone += n == T_WALL || n == T_ROCK;
                    }
                    if (stone >= 3) nt[y][x] = (uint8_t)was;
                }
        /* deep water never meets dry land without a shallow lip */
        for (int y = 1; y < MAP_H - 1; y++)
            for (int x = 1; x < MAP_W - 1; x++) {
                if (nt[y][x] != T_DEEP || lv->t[y][x] == T_DEEP) continue;
                for (int d = 0; d < 4; d++) {
                    int X = x + (d == 0) - (d == 1), Y = y + (d == 2) - (d == 3);
                    Tile n = (Tile)nt[Y][X];
                    if (dry_way(n) && n != T_SHALLOW) { nt[y][x] = T_SHALLOW; break; }
                }
            }
#undef FLOODABLE
        /* nothing that could be reached on foot before may be cut off now */
        int sx = lv->upx, sy = lv->upy;
        reach((const uint8_t (*)[MAP_W])lv->t, sx, sy, before);
        reach((const uint8_t (*)[MAP_W])nt, sx, sy, seen);
        bool ok = true;
        for (int y = 0; y < MAP_H && ok; y++)
            for (int x = 0; x < MAP_W; x++) {
                if (!dry_way((Tile)nt[y][x]) || seen[y][x]) continue;
                if (before[y][x]) { ok = false; break; }
                if (nt[y][x] == T_SHALLOW) nt[y][x] = T_DEEP;      /* a new shallow nobody can wade to: deepen it */
            }
        if (!ok) continue;
        /* take it, and wall in the rock it opened */
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++)
                if (nt[y][x] != lv->t[y][x]) {
                    lv->t[y][x] = nt[y][x];
                    lv->f[y][x] &= (uint8_t)~F_SECRET;
                }
        for (int y = 1; y < MAP_H - 1; y++)
            for (int x = 1; x < MAP_W - 1; x++) {
                if (lv->t[y][x] != T_ROCK) continue;
                for (int j = -1; j <= 1; j++)
                    for (int i = -1; i <= 1; i++)
                        if (tile_water((Tile)lv->t[y + j][x + i])) lv->t[y][x] = T_WALL;
            }
        made++;
    }
    return made;
}

/* -------------------------------------------------------- secret rooms */

/* A small room behind a hidden door: in the wall of a room or corridor, or at
 * the end of a short dead-end passage. Returns its room index, or -1. */
static int make_secret_room(Level *lv, Rng *rng)
{
    if (lv->nrooms >= MAX_ROOMS) return -1;
    static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
    for (int tries = 0; tries < 2000; tries++) {
        int x = rng_range(rng, 2, MAP_W - 3), y = rng_range(rng, 2, MAP_H - 3);
        if (lv->t[y][x] != T_WALL || (lv->f[y][x] & F_SECRET) || level_light_on(lv, x, y)) continue;
        int d = rng_range(rng, 0, 3), dx = DX[d], dy = DY[d], px = dy ? 1 : 0, py = dx ? 1 : 0;
        /* open ground behind the wall, rock beyond it, and wall to either side */
        Tile back = tile_at(lv, x - dx, y - dy);
        if (back != T_FLOOR || lv->trap[y - dy][x - dx]) continue;
        if (tile_at(lv, x + dx, y + dy) != T_ROCK) continue;
        if (tile_at(lv, x + px, y + py) != T_WALL || tile_at(lv, x - px, y - py) != T_WALL) continue;
        int stub = rng_chance(rng, 40) ? rng_range(rng, 2, 4) : 0;
        int doorx = x + dx * stub, doory = y + dy * stub;
        int rw = rng_range(rng, 3, 6), rh = rng_range(rng, 3, 4);      /* along, across the way in */
        int off = rng_range(rng, 0, rh - 1);
        /* the room's interior, as a rectangle */
        int ax = doorx + dx, ay = doory + dy;
        int x0, y0, w, h;
        if (dx) { w = rw; h = rh; x0 = dx > 0 ? ax : ax - rw + 1; y0 = doory - off; }
        else    { w = rh; h = rw; x0 = doorx - off; y0 = dy > 0 ? ay : ay - rw + 1; }
        if (x0 < 2 || y0 < 2 || x0 + w > MAP_W - 2 || y0 + h > MAP_H - 2) continue;
        /* all rock where it goes, and nothing but rock or wall around it */
        bool ok = true;
        for (int j = y0 - 1; j <= y0 + h && ok; j++)
            for (int i = x0 - 1; i <= x0 + w; i++) {
                Tile t = tile_at(lv, i, j);
                bool inside = i >= x0 && j >= y0 && i < x0 + w && j < y0 + h;
                if (inside ? t != T_ROCK : !(t == T_ROCK || (t == T_WALL && !(lv->f[j][i] & F_SECRET))) || level_light_on(lv, i, j))
                    { if (!(i == doorx && j == doory)) { ok = false; break; } }
            }
        /* the passage (and the door at its end) runs through rock, touching nothing */
        for (int k = 1; k <= stub && ok; k++) {
            int cx = x + dx * k, cy = y + dy * k;
            for (int s = -1; s <= 1; s++)
                if (tile_at(lv, cx + px * s, cy + py * s) != T_ROCK) { ok = false; break; }
        }
        if (!ok) continue;
        int r = lv->nrooms++;
        lv->rooms[r] = (Room){ x0, y0, w, h, true, false, RK_SECRET, false };
        for (int j = y0; j < y0 + h; j++)
            for (int i = x0; i < x0 + w; i++) { lv->t[j][i] = T_FLOOR; lv->room[j][i] = (int8_t)r; }
        for (int k = 0; k < stub; k++) {         /* the dead end: the wall opens onto it */
            int cx = x + dx * k, cy = y + dy * k;
            lv->t[cy][cx] = T_FLOOR;
            lv->f[cy][cx] |= F_CORR;
        }
        lv->t[doory][doorx] = T_WALL;
        lv->f[doory][doorx] |= F_SECRET;
        /* wall in what was opened up */
        for (int j = y0 - 2; j <= y0 + h + 1; j++)
            for (int i = x0 - 2; i <= x0 + w + 1; i++) {
                if (tile_at(lv, i, j) != T_ROCK) continue;
                for (int jj = -1; jj <= 1; jj++)
                    for (int ii = -1; ii <= 1; ii++) {
                        Tile n = tile_at(lv, i + ii, j + jj);
                        if (n != T_ROCK && n != T_WALL) lv->t[j][i] = T_WALL;
                    }
            }
        for (int k = 0; k < stub; k++)
            for (int s = -1; s <= 1; s += 2) {
                int cx = x + dx * k + px * s, cy = y + dy * k + py * s;
                if (tile_at(lv, cx, cy) == T_ROCK) lv->t[cy][cx] = T_WALL;
            }
        /* a cold sconce on its far wall, for whoever finds it */
        for (int i = x0; i < x0 + w; i++)
            if (tile_at(lv, i, y0 - 1) == T_WALL && !(lv->f[y0 - 1][i] & F_SECRET) && tile_solid(tile_at(lv, i, y0 - 2))
                && !(i == doorx && y0 - 1 == doory) && (hash2(i, y0, lv->seed) & 1)) {
                level_add_light(lv, L_SCONCE, i, y0, i, y0 - 1, false, r);
                break;
            }
        return r;
    }
    return -1;
}

/* ---------------------------------------------------------- room shapes */

/* Not every room is a box. The shape is only known while the level is made (Room has
 * no field for it, so saves stay as they are): what's left outside the shape is rock. */
typedef enum { RS_RECT, RS_CHAMFER, RS_OVAL, RS_CROSS, RS_PILLARS, RS_RUINED, RS_COUNT } RoomShape;

/* how often each shape turns up, in each stretch of the deep */
static const uint8_t SHAPE_W[N_THEME][RS_COUNT] = {
    /*              rect chamfer oval cross pillars ruined */
    [TH_HALLS]  = { 60,  15,     5,   10,   10,     0 },
    [TH_MOSS]   = { 30,  10,     15,  5,    10,     30 },
    [TH_BRICK]  = { 30,  10,     5,   20,   30,     5 },
    [TH_SLATE]  = { 25,  20,     30,  5,    10,     10 },
    [TH_BASALT] = { 20,  15,     25,  10,   10,     20 },
    [TH_HEARTH] = { 40,  20,     0,   10,   30,     0 },
};

static RoomShape pick_shape(Rng *rng, int depth, int w, int h)
{
    const uint8_t *wt = SHAPE_W[depth_theme(depth)];
    int sum = 0;
    for (int k = 0; k < RS_COUNT; k++) sum += wt[k];
    int roll = rng_range(rng, 0, sum - 1), k = 0;
    while (k < RS_COUNT - 1 && roll >= wt[k]) roll -= wt[k++];
    /* the small rooms can't carry the fancier shapes */
    if ((k == RS_CHAMFER && (w < 6 || h < 5)) || (k == RS_OVAL && (w < 7 || h < 5)) ||
        (k == RS_CROSS && (w < 7 || h < 6)) || (k == RS_PILLARS && (w < 7 || h < 6)) ||
        (k == RS_RUINED && (w < 6 || h < 5)))
        k = RS_RECT;
    return (RoomShape)k;
}

static bool in_shape(RoomShape s, int i, int j, int w, int h, int cut)
{
    switch (s) {
    case RS_CHAMFER: {
        int dx = i < w - 1 - i ? i : w - 1 - i, dy = j < h - 1 - j ? j : h - 1 - j;
        return dx + dy >= cut;
    }
    case RS_OVAL: {
        float u = (i + 0.5f - w / 2.0f) / (w / 2.0f), v = (j + 0.5f - h / 2.0f) / (h / 2.0f);
        return u * u + v * v <= 1.08f;
    }
    case RS_CROSS: {
        int ax = w / 4, ay = h / 4;
        return (i >= ax && i < w - ax) || (j >= ay && j < h - ay);
    }
    default: return true;
    }
}

/* floor in room r that can't be walked to from its middle goes back to rock */
static void room_trim(Level *lv, int r)
{
    const Room *rm = &lv->rooms[r];
    static uint8_t ok[MAP_H][MAP_W];
    static int q[MAP_W * MAP_H];
    memset(ok, 0, sizeof ok);
    int qh = 0, qn = 0, x = rm->x + rm->w / 2, y = rm->y + rm->h / 2;
    if (lv->room[y][x] != r) return;
    ok[y][x] = 1;
    q[qn++] = y * MAP_W + x;
    while (qh < qn) {
        int c = q[qh++];
        x = c % MAP_W;
        y = c / MAP_W;
        for (int d = 0; d < 4; d++) {
            int X = x + (d == 0) - (d == 1), Y = y + (d == 2) - (d == 3);
            if (ok[Y][X] || lv->room[Y][X] != r || lv->t[Y][X] != T_FLOOR) continue;
            ok[Y][X] = 1;
            q[qn++] = Y * MAP_W + X;
        }
    }
    for (int j = rm->y; j < rm->y + rm->h; j++)
        for (int i = rm->x; i < rm->x + rm->w; i++)
            if (lv->room[j][i] == r && lv->t[j][i] == T_FLOOR && !ok[j][i]) { lv->t[j][i] = T_ROCK; lv->room[j][i] = -1; }
}

/* can all the walkable ground of room r be walked between, and every way in (a corridor or
 * door touching it) still reach that ground? Walking may use the ground right round the room. */
static bool room_joined(const Level *lv, int r)
{
    const Room *rm = &lv->rooms[r];
    static uint8_t ok[MAP_H][MAP_W];
    static int q[MAP_W * MAP_H];
    memset(ok, 0, sizeof ok);
    int x0 = rm->x - 1, y0 = rm->y - 1, x1 = rm->x + rm->w, y1 = rm->y + rm->h, qh = 0, qn = 0;
    for (int y = rm->y; y < rm->y + rm->h && !qn; y++)
        for (int x = rm->x; x < rm->x + rm->w; x++)
            if (lv->room[y][x] == r && tile_walkable((Tile)lv->t[y][x])) { ok[y][x] = 1; q[qn++] = y * MAP_W + x; break; }
    while (qh < qn) {
        int c = q[qh++], x = c % MAP_W, y = c / MAP_W;
        for (int d = 0; d < 4; d++) {
            int X = x + (d == 0) - (d == 1), Y = y + (d == 2) - (d == 3);
            if (X < x0 || X > x1 || Y < y0 || Y > y1 || ok[Y][X] || !tile_walkable((Tile)lv->t[Y][X])) continue;
            ok[Y][X] = 1;
            q[qn++] = Y * MAP_W + X;
        }
    }
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (ok[y][x] || !tile_walkable((Tile)lv->t[y][x])) continue;
            if (lv->room[y][x] == r) return false;          /* ground of its own, cut off */
            for (int d = 0; d < 4; d++)                     /* a way in that leads nowhere now */
                if (tile_at(lv, x + (d == 0) - (d == 1), y + (d == 2) - (d == 3)) != T_ROCK
                    && lv->room[y + (d == 2) - (d == 3)][x + (d == 0) - (d == 1)] == r) return false;
        }
    return true;
}

static void carve_room(Level *lv, Rng *rng, int r, RoomShape s)
{
    const Room *rm = &lv->rooms[r];
    int cut = rm->w >= 9 && rm->h >= 7 && rng_chance(rng, 50) ? 2 : 1;
    for (int j = 0; j < rm->h; j++)
        for (int i = 0; i < rm->w; i++)
            if (in_shape(s, i, j, rm->w, rm->h, cut)) carve(lv, rm->x + i, rm->y + j, r);
    if (s == RS_RUINED) {
        /* the walls have slumped in here and there: bites out of the edge, a tile or two deep */
        int bites = rng_range(rng, 2, 4);
        for (int b = 0; b < bites; b++) {
            int side = rng_range(rng, 0, 3), len = rng_range(rng, 1, 3);
            int along = side < 2 ? rm->w : rm->h, at = rng_range(rng, 0, along - len);
            for (int k = 0; k < len; k++) {
                int deep = (k == 0 || k == len - 1) ? 1 : rng_range(rng, 1, 2);
                if ((side < 2 ? rm->h : rm->w) < 6) deep = 1;
                for (int dd = 0; dd < deep; dd++) {
                    int x = side == 0 ? rm->x + at + k : side == 1 ? rm->x + at + k : side == 2 ? rm->x + dd : rm->x + rm->w - 1 - dd;
                    int y = side == 0 ? rm->y + dd : side == 1 ? rm->y + rm->h - 1 - dd : rm->y + at + k;
                    if (abs(x - (rm->x + rm->w / 2)) <= 1 && abs(y - (rm->y + rm->h / 2)) <= 1) continue;
                    if (lv->room[y][x] == r) { lv->t[y][x] = T_ROCK; lv->room[y][x] = -1; }
                }
            }
        }
        room_trim(lv, r);
    }
}

/* Pillars in a room: two rows down its long sides (a nave and its aisles), or a grid of
 * them in a big hall. Each stands clear, with floor on all eight sides, so it can never
 * cut the room in two, and never in the middle, where corridors and braziers go. They
 * break line of sight: cover from slingers, and a place to lose what hunts you. */
static void room_pillars(Level *lv, Rng *rng, int r)
{
    const Room *rm = &lv->rooms[r];
    int cx = rm->x + rm->w / 2, cy = rm->y + rm->h / 2;
    bool grid = rm->w >= 9 && rm->h >= 7 && rng_chance(rng, 50), wide = rm->w >= rm->h;
    for (int y = rm->y + 1; y < rm->y + rm->h - 1; y++)
        for (int x = rm->x + 1; x < rm->x + rm->w - 1; x++) {
            int i = x - rm->x, j = y - rm->y;
            bool want = grid ? (i % 3 == 1 && j % 3 == 1)
                      : wide ? ((j == 1 || j == rm->h - 2) && (i & 1))
                             : ((i == 1 || i == rm->w - 2) && (j & 1));
            if (!want || (abs(x - cx) <= 1 && abs(y - cy) <= 1)) continue;
            bool clear = true;
            for (int dj = -1; dj <= 1 && clear; dj++)
                for (int di = -1; di <= 1; di++)
                    if (lv->t[y + dj][x + di] != T_FLOOR || lv->room[y + dj][x + di] != r) { clear = false; break; }
            if (clear) lv->t[y][x] = T_WALL;
        }
}

/* ------------------------------------------------------------- the level */

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

    /* the last level: the Heartflame's hall, a great pillared room, before anything else takes the space */
    bool final = depth == FINAL_DEPTH;
    lv->hall = -1;
    for (int tries = 0; final && tries < 600; tries++) {
        int w = 17, h = 11;
        int x = rng_range(&rng, 2, MAP_W - w - 3), y = rng_range(&rng, 2, MAP_H - h - 3);
        if (!rect_clear(lv, x, y, w, h, 3)) continue;
        int r = lv->nrooms++;
        lv->rooms[r] = (Room){ x, y, w, h, true, false, RK_PLAIN, false };
        for (int j = y; j < y + h; j++)
            for (int i = x; i < x + w; i++) carve(lv, i, j, r);
        lv->hall = r;
        break;
    }

    /* room shapes have their own generator, so the rest of the layout is as it was */
    uint8_t shape[MAX_ROOMS] = { 0 };
    Rng shrng = { seed * 5167u + 19 };
    int want = rng_range(&rng, 10, 14);
    for (int tries = 0; tries < 600 && lv->nrooms < want && lv->nrooms < MAX_ROOMS; tries++) {
        int w = rng_range(&rng, 5, 12), h = rng_range(&rng, 4, 8);
        int x = rng_range(&rng, 2, MAP_W - w - 3), y = rng_range(&rng, 2, MAP_H - h - 3);
        if (!rect_clear(lv, x, y, w, h, 3)) continue;
        int r = lv->nrooms++;
        lv->rooms[r] = (Room){ x, y, w, h, false, false, RK_PLAIN, false };
        shape[r] = pick_shape(&shrng, depth, w, h);
        carve_room(lv, &shrng, r, shape[r]);
    }
    for (int r = 0; r < lv->nrooms; r++)
        if (shape[r] == RS_PILLARS) room_pillars(lv, &shrng, r);

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
    while (start < lv->nrooms - 1 && (lv->rooms[start].cave || start == lv->hall)) start++;
    if (final) {
        /* no hall made (it never fails in practice): the biggest room will do */
        if (lv->hall < 0) {
            int best = 0;
            for (int r = 0; r < lv->nrooms; r++) {
                Room *rm = &lv->rooms[r];
                if (rm->cave || r == start || rm->w < 5 || rm->h < 5) continue;
                if (rm->w * rm->h > best) { best = rm->w * rm->h; lv->hall = r; }
            }
            if (lv->hall >= 0) lv->rooms[lv->hall].dark = true;
        }
        /* and the way in is as far from it as can be */
        if (lv->hall >= 0) {
            Room *hl = &lv->rooms[lv->hall];
            int hx = hl->x + hl->w / 2, hy = hl->y + hl->h / 2, bd = -1;
            for (int r = 0; r < lv->nrooms; r++) {
                if (lv->rooms[r].cave || r == lv->hall) continue;
                int d = (cx[r] - hx) * (cx[r] - hx) + (cy[r] - hy) * (cy[r] - hy);
                if (d > bd) { bd = d; start = r; }
            }
        }
    }
    pick_floor_in_room(lv, &rng, start, &lv->upx, &lv->upy);
    lv->t[lv->upy][lv->upx] = T_STAIRS_UP;

    if (final && lv->hall >= 0) {
        /* no way further down: the hearth stands at the heart of its hall, with room all round it */
        Room *hl = &lv->rooms[lv->hall];
        int bx = -1, by = -1, bd = 1 << 30;
        for (int y = hl->y + 1; y < hl->y + hl->h - 1; y++)
            for (int x = hl->x + 1; x < hl->x + hl->w - 1; x++) {
                bool ok = true;
                for (int j = -1; j <= 1 && ok; j++)
                    for (int i = -1; i <= 1; i++)
                        if (lv->t[y + j][x + i] != T_FLOOR || lv->room[y + j][x + i] != lv->hall) { ok = false; break; }
                int d = (2 * x - 2 * hl->x - hl->w + 1) * (2 * x - 2 * hl->x - hl->w + 1)
                      + (2 * y - 2 * hl->y - hl->h + 1) * (2 * y - 2 * hl->y - hl->h + 1);
                if (ok && d < bd) { bd = d; bx = x; by = y; }
            }
        if (bx < 0) { bx = hl->x + hl->w / 2; by = hl->y + hl->h / 2; }
        lv->t[by][bx] = T_HEARTH;
        lv->downx = bx;
        lv->downy = by;
        level_add_light(lv, L_HEART, bx, by, bx, by, false, lv->hall);
        /* two rows of pillars down the long sides */
        if (hl->w >= 15 && hl->h >= 9) {
            for (int k = 0; k < 3; k++)
                for (int s = 0; s < 2; s++) {
                    int px = s ? hl->x + hl->w - 4 : hl->x + 3, py = hl->y + 2 + k * (hl->h - 5) / 2;
                    if (lv->t[py][px] == T_FLOOR && abs(px - bx) > 2) lv->t[py][px] = T_WALL;
                }
        }
    } else {
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
    }

    /* chasms: open the middle of a big room onto the level below (none on the last level: there is no below) */
    if (holes) memset(holes, 0, sizeof(uint8_t) * MAP_W * MAP_H);
    int nch = final ? 0 : depth >= 2 ? rng_range(&rng, 1, 2) : 1;
    bool no_pit[MAX_ROOMS] = { false };
    for (int k = 0, tries = 0; k < nch && tries < 8; k++, tries++) {
        int pick = -1, area = 0;
        for (int r = 0; r < lv->nrooms; r++) {
            Room *rm = &lv->rooms[r];
            if (rm->cave || r == start || r == lv->hall || rm->w < 7 || rm->h < 6 || shape[r] == RS_PILLARS || no_pit[r]) continue;
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
        uint8_t was[MAP_H][MAP_W];      /* to put back if the pit would cut the room in two */
        for (int y = rm->y; y < rm->y + rm->h; y++) memcpy(&was[y][rm->x], &lv->t[y][rm->x], (size_t)rm->w);
        float ex = (rm->w - 2) / 2.0f - 0.2f, ey = (rm->h - 2) / 2.0f - 0.2f;
        float ccx = rm->x + rm->w / 2.0f, ccy = rm->y + rm->h / 2.0f;
        float ph = rng_f(&rng) * 6.28f, ph2 = rng_f(&rng) * 6.28f;
        int x0 = rm->x + 1, x1 = rm->x + rm->w - 2, y0 = rm->y + 1, y1 = rm->y + rm->h - 2;
        /* a lumpy hole, its rim crumbled away here and there */
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                if (lv->t[y][x] != T_FLOOR) continue;
                float dx = (x + 0.5f - ccx) / ex, dy = (y + 0.5f - ccy) / ey;
                float ang = atan2f(dy, dx);
                float rr = 0.88f + 0.15f * sinf(ang * 3 + ph) + 0.10f * sinf(ang * 5 - ph) + 0.07f * sinf(ang * 7 + ph2);
                float jit = ((float)(hash2(x, y, seed ^ 0xc4a5u) % 100) / 100.0f - 0.5f) * 0.45f;
                if (dx * dx + dy * dy <= rr * rr + jit) lv->t[y][x] = T_CHASM;
            }
        /* the edge crumbles back unevenly: bites out of the rim, and the floor fallen in beside it */
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                int n = 0;
                for (int d = 0; d < 4; d++) n += tile_at(lv, x + (d == 0) - (d == 1), y + (d == 2) - (d == 3)) == T_CHASM;
                uint32_t hh = hash2(x, y, seed ^ 0x6b1eu) % 100;
                if (lv->t[y][x] == T_CHASM && ((n <= 2 && hh < 60) || (n == 3 && hh < 30))) lv->t[y][x] = T_FLOOR;
                else if (lv->t[y][x] == T_FLOOR && n >= 1 && hh < 18 && x > x0 && y > y0 && x < x1 && y < y1) lv->t[y][x] = T_CHASM;
            }
        /* tidy: no lone pillars of floor in it, no lone pits off it */
        for (int it = 0; it < 2; it++)
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    int n = 0;
                    for (int d = 0; d < 4; d++) n += tile_at(lv, x + (d == 0) - (d == 1), y + (d == 2) - (d == 3)) == T_CHASM;
                    if (lv->t[y][x] == T_FLOOR && n >= 4) lv->t[y][x] = T_CHASM;
                    else if (lv->t[y][x] == T_CHASM && n <= 1) lv->t[y][x] = T_FLOOR;
                }
        /* floor left as an island out in the pit falls in with it */
        {
            static uint8_t ok[MAP_H][MAP_W];
            static int q[MAP_W * MAP_H];
            memset(ok, 0, sizeof ok);
            int qh = 0, qn = 0;
            for (int y = rm->y; y < rm->y + rm->h; y++)
                for (int x = rm->x; x < rm->x + rm->w; x++)
                    if ((x < x0 || x > x1 || y < y0 || y > y1) && lv->t[y][x] != T_CHASM && tile_walkable((Tile)lv->t[y][x])) { ok[y][x] = 1; q[qn++] = y * MAP_W + x; }
            while (qh < qn) {
                int c = q[qh++], x = c % MAP_W, y = c / MAP_W;
                for (int d = 0; d < 4; d++) {
                    int X = x + (d == 0) - (d == 1), Y = y + (d == 2) - (d == 3);
                    if (X < x0 || X > x1 || Y < y0 || Y > y1 || ok[Y][X] || lv->t[Y][X] != T_FLOOR) continue;
                    ok[Y][X] = 1;
                    q[qn++] = Y * MAP_W + X;
                }
            }
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++)
                    if (lv->t[y][x] == T_FLOOR && !ok[y][x]) lv->t[y][x] = T_CHASM;
        }
        /* where the floor gave way around it: a pit or two more, just off the edge */
        for (int k2 = 0, tries = 0; k2 < rng_range(&rng, 1, 3) && tries < 60; tries++) {
            int x = rng_range(&rng, x0, x1), y = rng_range(&rng, y0, y1);
            if (lv->t[y][x] != T_FLOOR) continue;
            bool clear = true, near = false;
            for (int j = -1; j <= 1 && clear; j++)
                for (int i = -1; i <= 1; i++) {
                    Tile t = tile_at(lv, x + i, y + j);
                    if (t == T_CHASM) clear = false;
                    else if (t != T_FLOOR) clear = false;
                }
            for (int j = -2; j <= 2 && clear; j++)
                for (int i = -2; i <= 2; i++) if (tile_at(lv, x + i, y + j) == T_CHASM) near = true;
            if (!clear || !near) continue;
            lv->t[y][x] = T_CHASM;
            k2++;
        }
        /* and the broken floor all round the rim */
        for (int y = rm->y; y < rm->y + rm->h; y++)
            for (int x = rm->x; x < rm->x + rm->w; x++) {
                if (lv->t[y][x] != T_FLOOR) continue;
                bool rim = false;
                for (int j = -1; j <= 1; j++)
                    for (int i = -1; i <= 1; i++) if (tile_at(lv, x + i, y + j) == T_CHASM) rim = true;
                if (rim && hash2(x, y, seed ^ 0x2b2bu) % 100 < 35) lv->t[y][x] = T_RUBBLE;
            }
        /* a box keeps its ring of floor round the pit, but the other shapes may not: all the
         * room's ground must still join up, or the floor goes back and another room is tried */
        if (!room_joined(lv, pick)) {
            for (int y = rm->y; y < rm->y + rm->h; y++) memcpy(&lv->t[y][rm->x], &was[y][rm->x], (size_t)rm->w);
            no_pit[pick] = true;
            k--;
            continue;
        }
        for (int y = rm->y; y < rm->y + rm->h; y++)
            for (int x = rm->x; x < rm->x + rm->w; x++)
                if (lv->t[y][x] == T_CHASM && holes) holes[y][x] = 1;
    }

    /* lakes, from depth 2: wide water spreading over rooms and the walls between them */
    Rng lrng = { seed * 4513u + 3 };
    int nlakes = final || depth < 2 ? 0 : rng_chance(&lrng, 60) ? (rng_chance(&lrng, depth >= 5 ? 35 : 15) ? 2 : 1) : 0;
    nlakes = make_lakes(lv, &lrng, nlakes);

    /* pools of water in some rooms (own generator, so the rest of the layout stays put) */
    Rng prng = { seed * 7919u + 13 };
    int npool = (rng_chance(&prng, 70) ? 1 : 0) + (depth >= 2) + (depth >= 5 && rng_chance(&prng, 50)) - nlakes;
    for (int k = 0, tries = 0; k < npool && tries < 60; tries++) {
        int r = rng_range(&prng, 0, lv->nrooms - 1);
        Room *rm = &lv->rooms[r];
        if (r == start || r == lv->hall || rm->w < 6 || rm->h < 5) continue;
        if (rm->w * rm->h < 48 && tries < 30) continue;        /* prefer the bigger rooms */
        if (lv->room[lv->downy][lv->downx] == r) continue;
        bool busy = false;
        for (int y = rm->y; y < rm->y + rm->h && !busy; y++)
            for (int x = rm->x; x < rm->x + rm->w; x++)
                if (lv->room[y][x] == r && (lv->t[y][x] == T_CHASM || tile_water((Tile)lv->t[y][x]))) { busy = true; break; }
        if (busy) continue;
        float ex = ((rm->w - 2) / 2.0f) * (0.85f + 0.25f * rng_f(&prng)), ey = ((rm->h - 2) / 2.0f) * (0.85f + 0.25f * rng_f(&prng));
        float ccx = rm->x + rm->w / 2.0f + rng_range(&prng, -1, 1) * 0.5f, ccy = rm->y + rm->h / 2.0f + rng_range(&prng, -1, 1) * 0.5f;
        float ph = rng_f(&prng) * 6.28f;
        bool deep = ex >= 2.2f && ey >= 1.8f;
        uint8_t want[MAP_H][MAP_W];
        memset(want, 0, sizeof want);
        for (int y = rm->y + 1; y < rm->y + rm->h - 1; y++)
            for (int x = rm->x + 1; x < rm->x + rm->w - 1; x++) {
                if (lv->room[y][x] != r || lv->t[y][x] != T_FLOOR) continue;
                float dx = (x + 0.5f - ccx) / ex, dy = (y + 0.5f - ccy) / ey;
                float ang = atan2f(dy, dx);
                float rr = 0.95f + 0.16f * sinf(ang * 2 + ph) + 0.09f * sinf(ang * 5 - ph);
                float d = (dx * dx + dy * dy) / (rr * rr);
                if (d <= 1) want[y][x] = deep && d < 0.55f ? T_DEEP : T_SHALLOW;
            }
        int made = 0;
        for (int y = rm->y + 1; y < rm->y + rm->h - 1; y++)
            for (int x = rm->x + 1; x < rm->x + rm->w - 1; x++) {
                if (!want[y][x]) continue;
                /* keep a dry ring next to walls and doors so the room stays crossable */
                bool ok = true;
                for (int j = -1; j <= 1 && ok; j++)
                    for (int i = -1; i <= 1; i++) {
                        Tile n = tile_at(lv, x + i, y + j);
                        if (!(n == T_FLOOR || tile_water(n))) { ok = false; break; }
                    }
                if (!ok) continue;
                lv->t[y][x] = want[y][x];
                made++;
            }
        /* deep water touching dry land gets a shallow lip */
        for (int y = rm->y; y < rm->y + rm->h; y++)
            for (int x = rm->x; x < rm->x + rm->w; x++) {
                if (lv->t[y][x] != T_DEEP) continue;
                for (int d = 0; d < 4; d++) {
                    static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
                    if (tile_at(lv, x + DX[d], y + DY[d]) == T_FLOOR) { lv->t[y][x] = T_SHALLOW; break; }
                }
            }
        if (made) k++;
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
        rm->dark = r == lv->hall || (r != start && rng_chance(&rng, 30 + depth * 4));
        int want_s = r == lv->hall ? 4 : rm->w >= 9 ? 2 : 1, placed = 0;
        for (int tries = 0; tries < (r == lv->hall ? 300 : 60) && placed < want_s; tries++) {
            int x = rng_range(&rng, rm->x, rm->x + rm->w - 1), y = rm->y - 1;
            if (tile_at(lv, x, y) != T_WALL || !tile_walkable(tile_at(lv, x, y + 1))) continue;
            if (!tile_solid(tile_at(lv, x, y - 1))) continue;      /* a cut-down front wall has no face to hang it on */
            if (level_light_on(lv, x, y) || level_light_on(lv, x - 1, y) || level_light_on(lv, x + 1, y) ||
                level_light_on(lv, x - 2, y) || level_light_on(lv, x + 2, y))
                continue;
            level_add_light(lv, L_SCONCE, x, y + 1, x, y, !rm->dark, r);
            placed++;
        }
    }

    /* a forge brazier, in a lit room away from the stairs up */
    for (int tries = 0; tries < 200; tries++) {
        int r = rng_range(&rng, 0, lv->nrooms - 1);
        Room *rm = &lv->rooms[r];
        if (r == start || r == lv->hall || rm->cave || rm->w < 5 || rm->h < 5 || (rm->dark && tries < 150)) continue;
        int x = rm->x + rm->w / 2, y = rm->y + rm->h / 2;
        bool ok = true;
        for (int j = -1; j <= 1 && ok; j++)
            for (int i = -1; i <= 1; i++)
                if (tile_at(lv, x + i, y + j) != T_FLOOR) { ok = false; break; }
        if (!ok) continue;
        lv->t[y][x] = T_BRAZIER;
        level_add_light(lv, L_BRAZIER, x, y, x, y, true, r);
        if (rm->dark) {
            rm->dark = false;
            for (int i = 0; i < lv->nlights; i++)
                if (lv->lights[i].room == r) lv->lights[i].lit = true;
        }
        break;
    }

    /* rooms with a purpose, from depth 2: a storeroom, an armoury, a shade's nest (own generator) */
    Rng srng = { seed * 6151u + 29 };
    int nspecial = 0;
    for (int kind = RK_NEST; depth >= 2 && kind >= RK_STORE; kind--) {
        int chance = kind == RK_STORE ? 40 : kind == RK_ARMOURY ? 25 : 35;
        if (kind == RK_NEST && (depth < 3 || final)) continue;
        if (nspecial >= 2 || !rng_chance(&srng, chance)) continue;
        int pick = -1, best = -1;
        for (int r = 0; r < lv->nrooms; r++) {
            Room *rm = &lv->rooms[r];
            if (rm->cave || r == start || r == lv->hall || rm->kind != RK_PLAIN) continue;
            int area = rm->w * rm->h;
            if (area < 20 || (kind == RK_STORE && area > 50) || (kind == RK_NEST && area < 30)) continue;
            bool busy = false;
            for (int y = rm->y; y < rm->y + rm->h && !busy; y++)
                for (int x = rm->x; x < rm->x + rm->w; x++) {
                    if (lv->room[y][x] != r) continue;
                    Tile t = (Tile)lv->t[y][x];
                    if (t == T_STAIRS_DOWN || t == T_STAIRS_UP || t == T_BRAZIER || t == T_CHASM || tile_water(t)) { busy = true; break; }
                }
            if (busy) continue;
            int score = (int)(rng_u32(&srng) % 100) + (kind == RK_STORE ? 60 - area : area);
            if (score > best) { best = score; pick = r; }
        }
        if (pick < 0) continue;
        Room *rm = &lv->rooms[pick];
        rm->kind = (uint8_t)kind;
        nspecial++;
        if (kind == RK_NEST) {
            /* the shades put out its lights long ago */
            rm->dark = true;
            for (int i = 0; i < lv->nlights; i++)
                if (lv->lights[i].room == pick && lv->lights[i].kind == L_SCONCE) lv->lights[i].lit = false;
        }
    }

    /* a hidden room behind a secret door, on some levels: whatever was hidden there is still there */
    Rng hrng = { seed * 9001u + 61 };
    if (!final && rng_chance(&hrng, depth >= 2 ? 40 : 20)) make_secret_room(lv, &hrng);

    /* traps, from depth 2: a few in the corridors and rooms, and at the way into a storeroom */
    Rng trng = { seed * 3779u + 41 };
    if (depth >= 2) {
        int want = 1 + depth / 5 + rng_range(&trng, 0, 2), placed = 0;
        if (want > 8) want = 8;
        for (int tries = 0; tries < 3000 && placed < want; tries++) {
            int x = rng_range(&trng, 1, MAP_W - 2), y = rng_range(&trng, 1, MAP_H - 2);
            if (lv->t[y][x] != T_FLOOR || lv->trap[y][x]) continue;
            if (abs(x - lv->upx) + abs(y - lv->upy) < 7 || (lv->room[y][x] >= 0 && lv->room[y][x] == lv->hall)) continue;
            if (lv->room[y][x] >= 0 && lv->rooms[lv->room[y][x]].kind == RK_SECRET) continue;
            bool near = false;
            for (int j = -1; j <= 1 && !near; j++)
                for (int i = -1; i <= 1; i++) {
                    Tile n = tile_at(lv, x + i, y + j);
                    if (n == T_STAIRS_DOWN || n == T_STAIRS_UP || n == T_CHASM || n == T_BRAZIER || n == T_HEARTH
                        || lv->trap[y + j][x + i]) { near = true; break; }
                }
            if (near) continue;
            int roll = rng_range(&trng, 0, 99);
            int k = roll < 30 ? TR_ALARM : roll < 55 ? TR_GUST : roll < 80 ? TR_SNARE : TR_PIT;
            if (k == TR_PIT && final) k = TR_SNARE;         /* no below to fall to */
            lv->trap[y][x] = (uint8_t)k;
            placed++;
        }
        /* storerooms were guarded: a trap just inside a way in */
        for (int r = 0; r < lv->nrooms; r++) {
            if (lv->rooms[r].kind != RK_STORE) continue;
            Room *rm = &lv->rooms[r];
            int n = 0;
            for (int y = rm->y; y < rm->y + rm->h && n < 2; y++)
                for (int x = rm->x; x < rm->x + rm->w && n < 2; x++) {
                    if (lv->room[y][x] != r || lv->t[y][x] != T_FLOOR || lv->trap[y][x]) continue;
                    bool entry = false;
                    for (int d = 0; d < 4; d++) {
                        int X = x + (d == 0) - (d == 1), Y = y + (d == 2) - (d == 3);
                        Tile t = tile_at(lv, X, Y);
                        if (t == T_DOOR || t == T_DOOR_OPEN || (lv->f[Y][X] & F_CORR)) entry = true;
                    }
                    if (!entry || !rng_chance(&trng, 70)) continue;
                    lv->trap[y][x] = (uint8_t)(rng_chance(&trng, 50) ? TR_ALARM : rng_chance(&trng, 50) || final ? TR_SNARE : TR_PIT);
                    n++;
                }
        }
    }

    /* scattered rubble */
    for (int y = 1; y < MAP_H - 1; y++)
        for (int x = 1; x < MAP_W - 1; x++)
            if (lv->t[y][x] == T_FLOOR && (hash2(x, y, seed) % 100) < (lv->room[y][x] >= 0 && shape[lv->room[y][x]] == RS_RUINED ? 16 : 3))
                lv->t[y][x] = T_RUBBLE;

    level_relight(lv);
}
