#include "game.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const MobInfo MOB_INFO[MK_COUNT] = {
    [MK_PLAYER] = { "you", "That's you: an adventurer with a dim lantern, a short sword and a pack.", 24, 85, 2, 5, 0, 0 },
    [MK_RAT]    = { "rat", "A rat the size of a dog. Quick to bite, quicker to die.", 5, 70, 1, 3, 0, 6 },
    [MK_GOBLIN] = { "goblin", "A wiry goblin with a rusty blade. It sees well in the dark, but even better in the light.",
                    11, 70, 2, 4, 10, 8 },
    [MK_SHADE]  = { "shade", "A scrap of living darkness that hunts by the warmth of the living. It cannot bear firelight: "
                    "a torch keeps it at bay and burns it, and darts pass straight through it.",
                    10, 80, 2, 5, 20, 9 },
};

const ItemInfo ITEM_INFO[IT_COUNT] = {
    [IT_TORCH]  = { "torch", "torches",
                    "A pitch-soaked torch. Light it and carry a wide circle of light for 200 turns, though everything "
                    "in the dark will see you coming. Thrown, it lands burning and lights the area for a while.", 7, true },
    [IT_DART]   = { "dart", "darts",
                    "A small iron dart balanced for throwing. It can usually be picked up again. "
                    "Anything that hears it land may come to look.", 9, false },
    [IT_POTION] = { "healing potion", "healing potions",
                    "A flask of ruby liquid that knits wounds closed when drunk.", 7, true },
};

static const int DX8[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };
static const int DY8[8] = { -1, 0, 1, 0, -1, 1, 1, -1 };

/* ------------------------------------------------------------- messages */

void game_msg(Game *g, uint32_t color, const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (buf[0] >= 'a' && buf[0] <= 'z') buf[0] -= 32;
    if (g->nmsgs > 0) {
        Msg *last = &g->msgs[(g->nmsgs - 1) % MAX_MSGS];
        if (!strcmp(last->text, buf) && last->age < 6) {
            last->count++;
            last->age = 0;
            return;
        }
    }
    Msg *m = &g->msgs[g->nmsgs % MAX_MSGS];
    snprintf(m->text, sizeof m->text, "%s", buf);
    m->color = color;
    m->age = 0;
    m->count = 1;
    g->nmsgs++;
}

static void floater(Game *g, int tx, int ty, uint32_t color, const char *text)
{
    for (int i = 0; i < MAX_FLOAT; i++) {
        Floater *f = &g->fl[i];
        if (f->t > 0) continue;
        f->x = tx * TILE + TILE / 2.0f + (rng_f(&g->rng) - 0.5f) * 8;
        f->y = ty * TILE + 4.0f;
        snprintf(f->text, sizeof f->text, "%s", text);
        f->color = color;
        f->t = 1.0f;
        return;
    }
}

/* ---------------------------------------------------------------- levels */

Level *game_level(Game *g) { return g->lv[g->depth]; }
Level *game_level_below(Game *g) { return g->depth + 1 <= MAX_DEPTH + 1 ? g->lv[g->depth + 1] : NULL; }
Mob *game_mobs(Game *g, int depth) { return g->mobs[depth]; }

Mob *game_mob_at(Game *g, int x, int y)
{
    Mob *m = g->mobs[g->depth];
    for (int i = 0; i < MAX_MOBS; i++)
        if (m[i].alive && m[i].x == x && m[i].y == y) return &m[i];
    return NULL;
}

static void mob_init(Mob *m, MobKind k, int x, int y)
{
    memset(m, 0, sizeof *m);
    m->alive = true;
    m->kind = k;
    m->x = x; m->y = y;
    m->rx = m->ax = (float)x;
    m->ry = m->ay = (float)y;
    m->at = 1;
    m->hp = m->maxhp = MOB_INFO[k].hp;
    m->idle = (float)(hash2(x, y, 9) % 628) / 100.0f;
}

/* firelight from a level's fixed lights: the warm part of the light, so the
 * cold glow of crystals and moss doesn't count */
static float level_fire(const Level *lv, int x, int y)
{
    if (!in_map(x, y)) return 0;
    float f = lv->lr[y][x] - 0.5f * lv->lb[y][x];
    return f > 0 ? f : 0;
}

float game_firelight(Game *g, int x, int y)
{
    if (!in_map(x, y)) return 0;
    return level_fire(game_level(g), x, y) + (g->torch_lit ? g->plight[y][x] : 0);
}

static void spawn_mobs(Game *g, int d)
{
    Level *lv = g->lv[d];
    Rng r = { lv->seed * 31u + 5 };
    int n = 3 + d + rng_range(&r, 0, 2), placed = 0;
    for (int tries = 0; tries < 2000 && placed < n && placed < MAX_MOBS; tries++) {
        int x = rng_range(&r, 1, MAP_W - 2), y = rng_range(&r, 1, MAP_H - 2);
        if (lv->t[y][x] != T_FLOOR || lv->room[y][x] < 0) continue;
        if (abs(x - lv->upx) + abs(y - lv->upy) < 9) continue;
        bool taken = false;
        for (int i = 0; i < placed; i++)
            if (g->mobs[d][i].x == x && g->mobs[d][i].y == y) taken = true;
        if (taken) continue;
        MobKind k = rng_chance(&r, d == 1 ? 30 : 30 + d * 12) ? MK_GOBLIN : MK_RAT;
        /* shades gather in the dark rooms from depth 2 */
        int rm = lv->room[y][x];
        if (d >= 2 && level_fire(lv, x, y) < SHADE_FEAR * 0.5f
            && rng_chance(&r, lv->rooms[rm].dark ? 35 + d * 5 : 4 + d * 2))
            k = MK_SHADE;
        Mob *m = &g->mobs[d][placed++];
        mob_init(m, k, x, y);
        m->asleep = k != MK_SHADE && rng_chance(&r, 65);
        m->flip = rng_chance(&r, 50);
    }
}

static void spawn_items(Game *g, int d)
{
    Level *lv = g->lv[d];
    Rng r = { lv->seed * 131u + 17 };
    int n = 3 + rng_range(&r, 0, 3), placed = 0;
    for (int tries = 0; tries < 2000 && placed < n && placed < MAX_ITEMS; tries++) {
        int x = rng_range(&r, 1, MAP_W - 2), y = rng_range(&r, 1, MAP_H - 2);
        if (lv->t[y][x] != T_FLOOR || lv->room[y][x] < 0) continue;
        bool taken = false;
        for (int i = 0; i < placed; i++)
            if (g->items[d][i].x == x && g->items[d][i].y == y) taken = true;
        if (taken) continue;
        int roll = rng_range(&r, 0, 99);
        FloorItem *it = &g->items[d][placed++];
        *it = (FloorItem){ true, roll < 45 ? IT_TORCH : roll < 75 ? IT_DART : IT_POTION, 1, x, y, false };
        if (it->kind == IT_DART) it->count = rng_range(&r, 2, 4);
    }
}

static void ensure_level(Game *g, int d)
{
    if (d < 1 || d > MAX_DEPTH + 1 || g->lv[d]) return;
    if (d > 1) ensure_level(g, d - 1);
    g->lv[d] = calloc(1, sizeof(Level));
    level_generate(g->lv[d], d, g->seed + (uint32_t)d * 7919u, d > 1 ? g->holes[d - 1] : NULL, g->holes[d]);
    spawn_mobs(g, d);
    spawn_items(g, d);
}

/* ------------------------------------------------------------------ view */

typedef struct { Game *g; Level *lv; } ViewCtx;

static void plight_mark(void *vc, int x, int y, float d)
{
    ViewCtx *c = vc;
    float r = c->g->torch_lit ? 7.0f : 3.6f;
    if (d > r) return;
    float k = 1.0f - d / r;
    c->g->plight[y][x] = d < 1 ? 1.0f : k * k * (3 - 2 * k);
}

static float tile_bright(Game *g, int x, int y)
{
    return level_brightness(g->lv[g->depth], x, y) + g->plight[y][x] * 0.7f;
}

static void view_mark(void *vc, int x, int y, float d)
{
    ViewCtx *c = vc;
    Level *lv = c->lv;
    Tile t = (Tile)lv->t[y][x];
    Light *fix = level_light_on(lv, x, y);
    bool see = d <= 1.5f || tile_bright(c->g, x, y) > 0.10f || t == T_CHASM || (fix && fix->lit);
    lv->f[y][x] |= F_LOS;
    if (see) lv->f[y][x] |= F_VIS | F_SEEN;
}

/* a monster you can make out: in view, or a shade's eyes in the dark */
bool game_mob_noticed(Game *g, const Mob *m)
{
    Level *lv = game_level(g);
    if (!m->alive) return false;
    uint8_t f = lv->f[m->y][m->x];
    return (f & F_VIS) || (m->kind == MK_SHADE && (f & F_LOS));
}

static int count_visible_mobs(Game *g)
{
    int n = 0;
    Mob *m = g->mobs[g->depth];
    for (int i = 0; i < MAX_MOBS; i++)
        if (game_mob_noticed(g, &m[i])) n++;
    return n;
}

static void compute_view(Game *g)
{
    Level *lv = game_level(g);
    if (lv->light_dirty) level_relight(lv);
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) lv->f[y][x] &= (uint8_t)~(F_VIS | F_LOS);
    memset(g->plight, 0, sizeof g->plight);
    ViewCtx c = { g, lv };
    fov_cast(lv, g->player.x, g->player.y, g->torch_lit ? 8 : 4, plight_mark, &c);
    fov_cast(lv, g->player.x, g->player.y, 24, view_mark, &c);
    int n = count_visible_mobs(g);
    if (n > g->seen_mobs && g->autom != AUTO_NONE) {
        Mob *m = g->mobs[g->depth];
        for (int i = 0; i < MAX_MOBS; i++)
            if (game_mob_noticed(g, &m[i])) {
                if (!(lv->f[m[i].y][m[i].x] & F_VIS)) game_msg(g, C_WARN, "two pale eyes watch you from the dark.");
                else game_msg(g, C_WARN, "you see a %s%s.", MOB_INFO[m[i].kind].name, m[i].asleep ? " (asleep)" : "");
                break;
            }
        game_stop(g);
    }
    g->seen_mobs = n;
}

/* ------------------------------------------------------------- creation */

static void place_player(Game *g, int x, int y)
{
    Mob *p = &g->player;
    p->x = x; p->y = y;
    p->rx = p->ax = (float)x;
    p->ry = p->ay = (float)y;
    p->at = 1;
}

Game *game_new(uint32_t seed)
{
    Game *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->seed = seed;
    g->rng.s = seed * 0x9e3779b97f4a7c15ull + 1;
    g->depth = 1;
    ensure_level(g, 1);
    ensure_level(g, 2);
    mob_init(&g->player, MK_PLAYER, 0, 0);
    Level *lv = game_level(g);
    place_player(g, lv->upx, lv->upy);
    g->level_changed = true;
    compute_view(g);
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) g->visf[y][x] = (lv->f[y][x] & F_VIS) ? 1.0f : 0.0f;
    g->inv[0] = (Slot){ IT_TORCH, 2 };
    g->inv[1] = (Slot){ IT_DART, 5 };
    g->inv[2] = (Slot){ IT_POTION, 1 };
    g->ninv = 3;
    game_msg(g, C_INFO, "you descend into the dark. Light is precious down here.");
    game_msg(g, C_DIM, "click to travel, x explores, ? for help.");
    return g;
}

void game_free(Game *g)
{
    if (!g) return;
    for (int d = 0; d < MAX_DEPTH + 2; d++) free(g->lv[d]);
    free(g);
}

/* ---------------------------------------------------------------- combat */

static void start_tween(Mob *m, int nx, int ny)
{
    m->ax = m->rx;
    m->ay = m->ry;
    m->at = 0;
    if (nx != m->x) m->flip = nx < m->x;
    m->x = nx;
    m->y = ny;
}

static bool diag_ok(const Level *lv, int x, int y, int dx, int dy)
{
    if (!dx || !dy) return true;
    Tile a = tile_at(lv, x + dx, y), b = tile_at(lv, x, y + dy);
    return !tile_solid(a) && a != T_DOOR && !tile_solid(b) && b != T_DOOR;
}

static void kill_mob(Game *g, Mob *m)
{
    m->alive = false;
    g->kills++;
}

static void attack(Game *g, Mob *a, Mob *d)
{
    const MobInfo *ai = &MOB_INFO[a->kind], *di = &MOB_INFO[d->kind];
    bool pl_att = a == &g->player, pl_def = d == &g->player;
    a->bump = 1.0f;
    a->bdx = d->x - a->x;
    a->bdy = d->y - a->y;
    if (a->bdx) a->flip = a->bdx < 0;
    int chance = ai->acc - di->def;
    if (d->asleep) chance = 100;
    d->asleep = false;
    if (!pl_def) { d->hunting = true; d->tx = a->x; d->ty = a->y; }
    if ((int)(rng_u32(&g->rng) % 100) >= chance) {
        if (pl_att) game_msg(g, C_DIM, "you miss the %s.", di->name);
        else if (pl_def) game_msg(g, C_DIM, "the %s misses you.", ai->name);
        floater(g, d->x, d->y, 0xb0b0b0, "miss");
        return;
    }
    int dmg = rng_range(&g->rng, ai->dmin, ai->dmax);
    d->hp -= dmg;
    d->flash = 1.0f;
    char t[16];
    snprintf(t, sizeof t, "%d", dmg);
    floater(g, d->x, d->y, pl_def ? 0xff5a4a : 0xffd27a, t);
    if (pl_def) {
        g->shake = 0.35f;
        game_stop(g);
        if (d->hp <= 0) {
            game_msg(g, C_BAD, "the %s kills you...", ai->name);
            g->state = GS_DEAD;
        } else {
            game_msg(g, C_BAD, "the %s hits you.", ai->name);
        }
        return;
    }
    if (d->hp <= 0) {
        kill_mob(g, d);
        game_msg(g, C_GOOD, "you slay the %s!", di->name);
    } else {
        game_msg(g, C_TEXT, "you hit the %s.", di->name);
    }
}

/* --------------------------------------------------------------- monsters */

static bool los(const Level *lv, int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 == x1 && y0 == y1) return true;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
        if ((x0 != x1 || y0 != y1) && tile_opaque(tile_at(lv, x0, y0))) return false;
    }
}

static int cheb(int ax, int ay, int bx, int by)
{
    int dx = abs(ax - bx), dy = abs(ay - by);
    return dx > dy ? dx : dy;
}

static bool mob_free(Game *g, int x, int y)
{
    Level *lv = game_level(g);
    Tile t = tile_at(lv, x, y);
    if (!tile_walkable(t)) return false;
    if (g->player.x == x && g->player.y == y) return false;
    return game_mob_at(g, x, y) == NULL;
}

static void mob_step_toward(Game *g, Mob *m, int tx, int ty)
{
    Level *lv = game_level(g);
    bool shade = m->kind == MK_SHADE;
    int best = -1, bd = cheb(m->x, m->y, tx, ty) * 10 + 5;
    for (int d = 0; d < 8; d++) {
        int nx = m->x + DX8[d], ny = m->y + DY8[d];
        if (!mob_free(g, nx, ny) || !diag_ok(lv, m->x, m->y, DX8[d], DY8[d])) continue;
        if (shade && game_firelight(g, nx, ny) > SHADE_FEAR) continue;
        int dd = cheb(nx, ny, tx, ty) * 10 + abs(nx - tx) + abs(ny - ty);
        if (dd < bd) { bd = dd; best = d; }
    }
    if (best < 0) return;
    int nx = m->x + DX8[best], ny = m->y + DY8[best];
    if (lv->t[ny][nx] == T_DOOR) {
        lv->t[ny][nx] = T_DOOR_OPEN;
        lv->light_dirty = true;
        return;
    }
    start_tween(m, nx, ny);
}

static void push_fx(Game *g, FxKind k, int x, int y);

/* a shade caught in firelight burns and flees for the darkest ground near it */
static bool shade_burns(Game *g, Mob *m)
{
    Level *lv = game_level(g);
    if (game_firelight(g, m->x, m->y) <= SHADE_FEAR) return false;
    bool seen = lv->f[m->y][m->x] & (F_VIS | F_LOS);
    int dmg = rng_range(&g->rng, 1, 3);
    m->hp -= dmg;
    m->flash = 1;
    char t[16];
    snprintf(t, sizeof t, "%d", dmg);
    floater(g, m->x, m->y, 0xffa040, t);
    push_fx(g, FX_BURN, m->x, m->y);
    if (m->hp <= 0) {
        kill_mob(g, m);
        push_fx(g, FX_DISSOLVE, m->x, m->y);
        if (seen) game_msg(g, C_GOOD, "the shade burns away to nothing.");
        return true;
    }
    if (seen) game_msg(g, C_TEXT, "the shade writhes in the light!");
    int best = -1;
    float bf = game_firelight(g, m->x, m->y);
    for (int d = 0; d < 8; d++) {
        int nx = m->x + DX8[d], ny = m->y + DY8[d];
        if (!mob_free(g, nx, ny) || !diag_ok(lv, m->x, m->y, DX8[d], DY8[d]) || lv->t[ny][nx] == T_DOOR) continue;
        float f = game_firelight(g, nx, ny);
        if (f < bf) { bf = f; best = d; }
    }
    if (best >= 0) start_tween(m, m->x + DX8[best], m->y + DY8[best]);
    return true;
}

static void monsters_act(Game *g)
{
    Level *lv = game_level(g);
    Mob *p = &g->player;
    /* the player stands out when a lamp or torch is on them; their own glow is dim */
    float pb = level_brightness(lv, p->x, p->y) + (g->torch_lit ? 1.0f : 0.0f);
    Mob *ms = g->mobs[g->depth];
    for (int i = 0; i < MAX_MOBS && g->state != GS_DEAD; i++) {
        Mob *m = &ms[i];
        if (!m->alive) continue;
        const MobInfo *mi = &MOB_INFO[m->kind];
        int dist = cheb(m->x, m->y, p->x, p->y);
        float range = pb > 0.30f ? (float)mi->perception : 2.5f;
        bool shade = m->kind == MK_SHADE;
        if (shade) {
            if (shade_burns(g, m)) continue;
            range = (float)mi->perception;          /* it feels your warmth, lit or not */
        }
        bool sees = dist <= range && los(lv, m->x, m->y, p->x, p->y);
        if (m->asleep) {
            int pct = !sees ? 0 : dist <= 2 ? 45 : 20;
            if (pct && (int)(rng_u32(&g->rng) % 100) < pct) {
                m->asleep = false;
                m->hunting = true;
                m->tx = p->x; m->ty = p->y;
                if (lv->f[m->y][m->x] & F_VIS) game_msg(g, C_WARN, "the %s wakes up!", mi->name);
            }
            continue;
        }
        if (sees) { m->hunting = true; m->tx = p->x; m->ty = p->y; }
        if (dist == 1 && diag_ok(lv, m->x, m->y, p->x - m->x, p->y - m->y) && (sees || m->hunting)
            && !(shade && game_firelight(g, p->x, p->y) > SHADE_FEAR)) {
            attack(g, m, p);
            continue;
        }
        if (m->hunting) {
            mob_step_toward(g, m, m->tx, m->ty);
            if (m->x == m->tx && m->y == m->ty && !sees) m->hunting = false;
        } else if (rng_chance(&g->rng, 35)) {
            int d = rng_range(&g->rng, 0, 7);
            int nx = m->x + DX8[d], ny = m->y + DY8[d];
            if (mob_free(g, nx, ny) && lv->t[ny][nx] != T_DOOR && diag_ok(lv, m->x, m->y, DX8[d], DY8[d])
                && !(shade && game_firelight(g, nx, ny) > SHADE_FEAR))
                start_tween(m, nx, ny);
        }
    }
}

static void burn_floor_torches(Game *g, int d)
{
    Level *lv = g->lv[d];
    if (!lv) return;
    for (int i = lv->nlights - 1; i >= 0; i--) {
        Light *l = &lv->lights[i];
        if (l->kind != L_FLOORTORCH || l->fuel < 0) continue;
        if (--l->fuel > 0) continue;
        if (d == g->depth && (lv->f[l->y][l->x] & F_VIS))
            game_msg(g, C_DIM, "a torch on the floor gutters and goes out, leaving a charred stub.");
        lv->scorch[l->y][l->x] = 1;
        level_remove_light(lv, i);
        if (d == g->depth + 1) g->lower_dirty = true;
    }
}

static void end_turn(Game *g)
{
    g->turn++;
    if (g->torch_lit) {
        g->torch_fuel--;
        if (g->torch_fuel == 25) game_msg(g, C_WARN, "your torch is burning low.");
        if (g->torch_fuel <= 0) {
            g->torch_lit = false;
            g->torch_fuel = 0;
            game_msg(g, C_WARN, "your torch burns out. Only your lantern's glow remains.");
            game_stop(g);
        }
    }
    burn_floor_torches(g, g->depth);
    burn_floor_torches(g, g->depth + 1);
    monsters_act(g);
    Mob *p = &g->player;
    if (++g->regen >= 9) {
        g->regen = 0;
        if (p->hp < p->maxhp && g->state != GS_DEAD) p->hp++;
    }
    compute_view(g);
}

/* ------------------------------------------------------------ commands */

static void change_level(Game *g, int depth, int x, int y)
{
    g->depth = depth;
    ensure_level(g, depth);
    ensure_level(g, depth + 1);
    Level *lv = game_level(g);
    if (!tile_walkable(tile_at(lv, x, y)) || game_mob_at(g, x, y)) {
        /* nearest open floor */
        int bx = lv->upx, by = lv->upy, bd = 1 << 30;
        for (int yy = 0; yy < MAP_H; yy++)
            for (int xx = 0; xx < MAP_W; xx++) {
                if (!tile_walkable((Tile)lv->t[yy][xx]) || game_mob_at(g, xx, yy)) continue;
                int d = (xx - x) * (xx - x) + (yy - y) * (yy - y);
                if (d < bd) { bd = d; bx = xx; by = yy; }
            }
        x = bx; y = by;
    }
    place_player(g, x, y);
    g->level_changed = true;
    g->autom = AUTO_NONE;
    g->seen_mobs = 0;
    lv->light_dirty = true;
    compute_view(g);
    g->seen_mobs = count_visible_mobs(g);
    for (int yy = 0; yy < MAP_H; yy++)
        for (int xx = 0; xx < MAP_W; xx++) g->visf[yy][xx] = 0;
}

static void pickup(Game *g);

bool game_move(Game *g, int dx, int dy)
{
    if (g->state != GS_PLAY) return false;
    Level *lv = game_level(g);
    Mob *p = &g->player;
    int nx = p->x + dx, ny = p->y + dy;
    if (!in_map(nx, ny)) return false;
    Tile t = (Tile)lv->t[ny][nx];
    Mob *m = game_mob_at(g, nx, ny);
    if (m && diag_ok(lv, p->x, p->y, dx, dy)) {
        attack(g, p, m);
        end_turn(g);
        return true;
    }
    if (tile_solid(t)) {
        Light *l = level_light_on(lv, nx, ny);
        if (l && !l->lit && l->kind == L_SCONCE && l->x == p->x && l->y == p->y) {
            l->lit = true;
            lv->light_dirty = true;
            if (l->room >= 0) lv->rooms[l->room].dark = false;
            game_msg(g, C_WARN, "you light the sconce. Warm light spills across the room.");
            game_stop(g);
            end_turn(g);
            return true;
        }
        if (l && !l->lit && l->kind == L_SCONCE)
            game_msg(g, C_DIM, "you need to stand right in front of the sconce to light it.");
        return false;
    }
    if (!diag_ok(lv, p->x, p->y, dx, dy)) return false;
    if (t == T_DOOR) {
        lv->t[ny][nx] = T_DOOR_OPEN;
        lv->light_dirty = true;
        end_turn(g);
        return true;
    }
    if (t == T_CHASM) {
        game_stop(g);
        g->state = GS_JUMP_PROMPT;
        g->jx = nx;
        g->jy = ny;
        return false;
    }
    start_tween(p, nx, ny);
    pickup(g);
    if (t == T_STAIRS_DOWN && g->autom != AUTO_STAIRS)
        game_msg(g, C_INFO, "stairs lead down into the dark (press > or click them again to descend).");
    end_turn(g);
    return true;
}

bool game_rest(Game *g)
{
    if (g->state != GS_PLAY) return false;
    end_turn(g);
    return true;
}

bool game_descend(Game *g)
{
    if (g->state != GS_PLAY) return false;
    Level *lv = game_level(g);
    if (lv->t[g->player.y][g->player.x] != T_STAIRS_DOWN) return false;
    if (g->depth >= MAX_DEPTH) { game_msg(g, C_WARN, "the stairs end in rubble."); return false; }
    change_level(g, g->depth + 1, 0, 0);
    Level *nl = game_level(g);
    place_player(g, nl->upx, nl->upy);
    compute_view(g);
    game_msg(g, C_INFO, "you descend to depth %d.", g->depth);
    return true;
}

bool game_ascend(Game *g)
{
    if (g->state != GS_PLAY) return false;
    Level *lv = game_level(g);
    if (lv->t[g->player.y][g->player.x] != T_STAIRS_UP) return false;
    if (g->depth == 1) { game_msg(g, C_DIM, "the way back up is blocked by fallen stone."); return false; }
    Level *up = g->lv[g->depth - 1];
    change_level(g, g->depth - 1, up->downx, up->downy);
    game_msg(g, C_INFO, "you climb back up to depth %d.", g->depth);
    return true;
}

void game_jump_answer(Game *g, bool yes)
{
    if (g->state != GS_JUMP_PROMPT) return;
    g->state = GS_PLAY;
    if (!yes) return;
    int dmg = 3 + rng_range(&g->rng, 0, 3) + g->depth / 3;
    game_msg(g, C_WARN, "you leap into the chasm and plunge into the darkness below!");
    change_level(g, g->depth + 1, g->jx, g->jy);
    Mob *p = &g->player;
    p->hp -= dmg;
    p->flash = 1.0f;
    g->shake = 0.6f;
    char t[16];
    snprintf(t, sizeof t, "%d", dmg);
    floater(g, p->x, p->y, 0xff5a4a, t);
    if (p->hp <= 0) {
        game_msg(g, C_BAD, "you hit the ground at depth %d and do not get up.", g->depth);
        g->state = GS_DEAD;
    } else {
        game_msg(g, C_BAD, "you land hard on depth %d (%d damage).", g->depth, dmg);
    }
}

/* ------------------------------------------------------------- pathing */

static bool path_ok(Game *g, int x, int y, int gx, int gy)
{
    Level *lv = game_level(g);
    if (!in_map(x, y) || !(lv->f[y][x] & F_SEEN)) return false;
    if (x == gx && y == gy) return true;
    if (!tile_walkable((Tile)lv->t[y][x])) return false;
    Mob *m = game_mob_at(g, x, y);
    return !(m && (lv->f[y][x] & F_VIS));
}

/* BFS over known ground from the player; returns the path to the goal (or to
 * the first frontier tile when gx < 0). */
static int find_path(Game *g, int gx, int gy, int *out, int max)
{
    static int16_t par[MAP_H][MAP_W];
    static int q[MAP_W * MAP_H];
    Level *lv = game_level(g);
    Mob *p = &g->player;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) par[y][x] = -2;
    int h = 0, t = 0, found = -1;
    par[p->y][p->x] = -1;
    q[t++] = p->y * MAP_W + p->x;
    while (h < t && found < 0) {
        int c = q[h++], x = c % MAP_W, y = c / MAP_W;
        if (gx < 0 && c != q[0]) {
            /* explore: an item worth picking up, or a known tile next to the unknown */
            FloorItem *it = game_item_at(g, x, y);
            if (it && !it->dropped && (lv->f[y][x] & F_SEEN)) { found = c; break; }
            for (int d = 0; d < 8; d++) {
                int X = x + DX8[d], Y = y + DY8[d];
                if (in_map(X, Y) && !(lv->f[Y][X] & F_SEEN) && lv->t[Y][X] != T_ROCK) { found = c; break; }
            }
            if (found >= 0) break;
        }
        if (gx >= 0 && x == gx && y == gy) { found = c; break; }
        /* a tile that is not walkable (the goal itself, e.g. a monster or sconce) ends the path */
        if (c != q[0] && !tile_walkable((Tile)lv->t[y][x])) continue;
        for (int d = 0; d < 8; d++) {
            int X = x + DX8[d], Y = y + DY8[d];
            if (!in_map(X, Y) || par[Y][X] != -2) continue;
            if (!path_ok(g, X, Y, gx, gy) || !diag_ok(lv, x, y, DX8[d], DY8[d])) continue;
            if (lv->t[Y][X] == T_CHASM && !(X == gx && Y == gy)) continue;
            par[Y][X] = (int16_t)d;
            q[t++] = Y * MAP_W + X;
        }
    }
    if (found < 0) return 0;
    int n = 0, c = found;
    static int rev[MAP_W * MAP_H];
    while (par[c / MAP_W][c % MAP_W] >= 0) {
        rev[n++] = c;
        int d = par[c / MAP_W][c % MAP_W];
        c = (c / MAP_W - DY8[d]) * MAP_W + (c % MAP_W - DX8[d]);
    }
    int k = n < max ? n : max;
    for (int i = 0; i < k; i++) out[i] = rev[n - 1 - i];
    return k;
}

int game_path_preview(Game *g, int x, int y, int *out, int max)
{
    if (!in_map(x, y)) return 0;
    Level *lv = game_level(g);
    if (!(lv->f[y][x] & F_SEEN)) return 0;
    Light *l = level_light_on(lv, x, y);
    if (l && !l->lit && l->kind == L_SCONCE) {
        /* path to the spot in front, then the bump lights it */
        int n = find_path(g, l->x, l->y, out, max - 1);
        if (n == 0 && !(g->player.x == l->x && g->player.y == l->y)) return 0;
        out[n++] = y * MAP_W + x;
        return n;
    }
    if (tile_solid(tile_at(lv, x, y))) return 0;
    return find_path(g, x, y, out, max);
}

void game_stop(Game *g)
{
    g->autom = AUTO_NONE;
    g->plen = g->pi = 0;
}

void game_travel_to(Game *g, int x, int y)
{
    if (g->state != GS_PLAY) return;
    Mob *p = &g->player;
    if (x == p->x && y == p->y) {
        Level *lv = game_level(g);
        if (lv->t[y][x] == T_STAIRS_DOWN) game_descend(g);
        else if (lv->t[y][x] == T_STAIRS_UP) game_ascend(g);
        else game_rest(g);
        return;
    }
    if (cheb(x, y, p->x, p->y) == 1 && !level_light_on(game_level(g), x, y)) {
        game_move(g, x - p->x, y - p->y);
        return;
    }
    g->plen = game_path_preview(g, x, y, g->path, MAX_PATH);
    if (!g->plen) { game_msg(g, C_DIM, "you don't know a way there."); return; }
    g->pi = 0;
    g->goalx = x;
    g->goaly = y;
    g->autom = AUTO_TRAVEL;
    g->auto_t = 0;
}

void game_explore(Game *g)
{
    if (g->state != GS_PLAY) return;
    g->autom = AUTO_EXPLORE;
    g->auto_t = 0;
}

void game_rest_long(Game *g)
{
    if (g->state != GS_PLAY) return;
    if (g->player.hp >= g->player.maxhp) { game_msg(g, C_DIM, "you are already at full health."); return; }
    game_msg(g, C_DIM, "you rest...");
    g->autom = AUTO_REST;
    g->auto_t = 0;
    g->pi = 0;
}

void game_to_stairs(Game *g)
{
    if (g->state != GS_PLAY) return;
    Level *lv = game_level(g);
    if (lv->t[g->player.y][g->player.x] == T_STAIRS_DOWN) { game_descend(g); return; }
    if (!(lv->f[lv->downy][lv->downx] & F_SEEN)) {
        game_msg(g, C_DIM, "you haven't found the way down yet.");
        return;
    }
    g->goalx = lv->downx;
    g->goaly = lv->downy;
    g->autom = AUTO_STAIRS;
    g->auto_t = 0;
}

static void auto_step(Game *g)
{
    Mob *p = &g->player;
    int path[MAX_PATH];
    switch (g->autom) {
    case AUTO_NONE: return;
    case AUTO_REST:
        if (p->hp >= p->maxhp || ++g->pi > 200) {
            if (p->hp >= p->maxhp) game_msg(g, C_GOOD, "you feel rested.");
            game_stop(g);
            return;
        }
        game_rest(g);
        return;
    case AUTO_EXPLORE: {
        int n = find_path(g, -1, -1, path, MAX_PATH);
        if (!n) {
            game_msg(g, C_INFO, "you have explored everything you can reach. Press > to head for the stairs.");
            game_stop(g);
            return;
        }
        int c = path[0];
        if (!game_move(g, c % MAP_W - p->x, c / MAP_W - p->y)) game_stop(g);
        return;
    }
    case AUTO_TRAVEL:
    case AUTO_STAIRS: {
        if (p->x == g->goalx && p->y == g->goaly) {
            AutoMode m = g->autom;
            game_stop(g);
            if (m == AUTO_STAIRS) game_descend(g);
            return;
        }
        int n = game_path_preview(g, g->goalx, g->goaly, path, MAX_PATH);
        if (!n) { game_stop(g); return; }
        int c = path[0];
        bool last = n == 1;
        bool moved = game_move(g, c % MAP_W - p->x, c / MAP_W - p->y);
        if (!moved || last) {
            if (last && g->autom == AUTO_STAIRS && p->x == g->goalx && p->y == g->goaly) {
                game_stop(g);
                game_descend(g);
            } else if (last || !moved) {
                game_stop(g);
            }
        }
        return;
    }
    }
}

/* developer: stand at the edge of the first chasm, with the level revealed */
bool game_debug_spawn(Game *g, MobKind k, int x, int y)
{
    if (!in_map(x, y) || !mob_free(g, x, y)) return false;
    Mob *ms = g->mobs[g->depth];
    for (int i = 0; i < MAX_MOBS; i++)
        if (!ms[i].alive) {
            mob_init(&ms[i], k, x, y);
            compute_view(g);
            return true;
        }
    return false;
}

bool game_debug_to_chasm(Game *g)
{
    Level *lv = game_level(g);
    for (int y = 1; y < MAP_H - 1; y++)
        for (int x = 1; x < MAP_W - 1; x++) {
            if (lv->t[y][x] != T_CHASM || !tile_walkable((Tile)lv->t[y + 2][x])) continue;
            if (lv->t[y + 1][x] == T_CHASM) continue;
            place_player(g, x, y + 2);
            for (int j = 0; j < MAP_H; j++)
                for (int i = 0; i < MAP_W; i++) lv->f[j][i] |= F_SEEN;
            compute_view(g);
            return true;
        }
    return false;
}

bool game_busy(const Game *g) { return g->player.at < 0.7f || g->proj.active; }

static void resolve_throw(Game *g);

static void anim_mob(Mob *m, float dt)
{
    if (m->at < 1) {
        m->at += dt / 0.11f;
        if (m->at > 1) m->at = 1;
    }
    float e = m->at * m->at * (3 - 2 * m->at);
    m->rx = m->ax + (m->x - m->ax) * e;
    m->ry = m->ay + (m->y - m->ay) * e;
    if (m->bump > 0) m->bump -= dt / 0.18f;
    if (m->flash > 0) m->flash -= dt / 0.25f;
}

void game_update(Game *g, float dt)
{
    g->time += dt;
    if (g->shake > 0) g->shake -= dt;
    anim_mob(&g->player, dt);
    Mob *ms = g->mobs[g->depth];
    for (int i = 0; i < MAX_MOBS; i++)
        if (ms[i].alive) anim_mob(&ms[i], dt);
    for (int i = 0; i < MAX_FLOAT; i++)
        if (g->fl[i].t > 0) g->fl[i].t -= dt / 1.1f;
    for (int i = 0; i < MAX_MSGS; i++) g->msgs[i].age += dt;

    Level *lv = game_level(g);
    float k = dt * 7.0f;
    if (k > 1) k = 1;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            float tgt = (lv->f[y][x] & F_VIS) ? 1.0f : 0.0f;
            g->visf[y][x] += (tgt - g->visf[y][x]) * k;
        }

    if (g->proj.active) {
        g->proj.pos += dt * 17.0f;
        if (g->proj.pos >= g->proj.n) {
            g->proj.active = false;
            resolve_throw(g);
        }
    }

    if (g->autom != AUTO_NONE && g->state == GS_PLAY && !g->proj.active) {
        g->auto_t -= dt;
        if (g->auto_t <= 0 && g->player.at >= 0.85f) {
            g->auto_t = g->autom == AUTO_REST ? 0.02f : 0.075f;
            auto_step(g);
        }
    }
}

/* ---------------------------------------------------------------- items */

FloorItem *game_items(Game *g, int depth) { return g->items[depth]; }

FloorItem *game_item_at(Game *g, int x, int y)
{
    FloorItem *it = g->items[g->depth];
    for (int i = 0; i < MAX_ITEMS; i++)
        if (it[i].alive && it[i].x == x && it[i].y == y) return &it[i];
    return NULL;
}

const char *game_item_name(ItemKind k, int count, char *buf, size_t n)
{
    const ItemInfo *ii = &ITEM_INFO[k];
    if (count == 1) snprintf(buf, n, "%s %s", k == IT_POTION ? "a" : "a", ii->name);
    else snprintf(buf, n, "%d %s", count, ii->plural);
    return buf;
}

static void push_fx(Game *g, FxKind k, int x, int y)
{
    if (g->nfx < MAX_FX) g->fx[g->nfx++] = (Fx){ k, x, y };
}

static bool add_item(Game *g, ItemKind k, int count)
{
    for (int i = 0; i < g->ninv; i++)
        if (g->inv[i].kind == k) { g->inv[i].count += count; return true; }
    if (g->ninv >= MAX_INV) return false;
    g->inv[g->ninv++] = (Slot){ k, count };
    return true;
}

static void take_one(Game *g, int slot)
{
    if (--g->inv[slot].count > 0) return;
    for (int i = slot; i < g->ninv - 1; i++) g->inv[i] = g->inv[i + 1];
    g->ninv--;
}

static bool place_item(Game *g, int depth, ItemKind k, int count, int x, int y, bool dropped)
{
    FloorItem *it = g->items[depth];
    for (int i = 0; i < MAX_ITEMS; i++)
        if (it[i].alive && it[i].x == x && it[i].y == y && it[i].kind == k) { it[i].count += count; return true; }
    for (int i = 0; i < MAX_ITEMS; i++)
        if (!it[i].alive) { it[i] = (FloorItem){ true, k, count, x, y, dropped }; return true; }
    return false;
}

/* a burning torch on the floor: take it up if your hand is free */
static void take_floor_torch(Game *g)
{
    Mob *p = &g->player;
    Level *lv = game_level(g);
    for (int i = 0; i < lv->nlights; i++) {
        Light *l = &lv->lights[i];
        if (l->kind != L_FLOORTORCH || l->x != p->x || l->y != p->y) continue;
        if (g->torch_lit) {
            game_msg(g, C_DIM, "a torch burns here on the floor.");
            return;
        }
        int fuel = l->fuel > g->torch_fuel ? l->fuel : g->torch_fuel;
        g->torch_lit = true;
        g->torch_fuel = fuel;
        level_remove_light(lv, i);
        level_relight(lv);
        game_msg(g, C_GOOD, "you take up the burning torch (%d turns left in it).", fuel);
        return;
    }
}

static void pickup(Game *g)
{
    Mob *p = &g->player;
    take_floor_torch(g);
    FloorItem *it = game_item_at(g, p->x, p->y);
    if (!it) return;
    char nm[64];
    if (!add_item(g, it->kind, it->count)) {
        game_msg(g, C_DIM, "your pack is full; you leave %s.", game_item_name(it->kind, it->count, nm, sizeof nm));
        return;
    }
    int slot = 0;
    for (int i = 0; i < g->ninv; i++) if (g->inv[i].kind == it->kind) slot = i;
    game_msg(g, C_GOOD, "you pick up %s (%c).", game_item_name(it->kind, it->count, nm, sizeof nm), 'a' + slot);
    it->alive = false;
}

/* a burning torch now lies on (x, y) of `depth` */
static void floor_torch(Game *g, int depth, int x, int y)
{
    Level *lv = g->lv[depth];
    if (!lv) return;
    for (int i = 0; i < lv->nlights; i++) {
        Light *l = &lv->lights[i];
        if (l->kind == L_FLOORTORCH && l->x == x && l->y == y) { l->fuel = FLOOR_TORCH_FUEL; return; }
    }
    int i = level_add_light(lv, L_FLOORTORCH, x, y, x, y, true, lv->room[y][x]);
    if (i >= 0) lv->lights[i].fuel = FLOOR_TORCH_FUEL;
    level_relight(lv);
}

/* nearest open floor to (x, y) on a level */
static void nearest_floor(Level *lv, int *x, int *y)
{
    if (tile_walkable(tile_at(lv, *x, *y))) return;
    int bx = *x, by = *y, bd = 1 << 30;
    for (int yy = 0; yy < MAP_H; yy++)
        for (int xx = 0; xx < MAP_W; xx++) {
            if (!tile_walkable((Tile)lv->t[yy][xx])) continue;
            int d = (xx - *x) * (xx - *x) + (yy - *y) * (yy - *y);
            if (d < bd) { bd = d; bx = xx; by = yy; }
        }
    *x = bx; *y = by;
}

int game_throw_path(Game *g, ItemKind k, int tx, int ty, int *out, int max, bool *hits)
{
    Level *lv = game_level(g);
    Mob *p = &g->player;
    *hits = false;
    if (tx == p->x && ty == p->y) return 0;
    int range = ITEM_INFO[k].range, n = 0;
    int x0 = p->x, y0 = p->y;
    int dx = abs(tx - x0), sx = x0 < tx ? 1 : -1;
    int dy = -abs(ty - y0), sy = y0 < ty ? 1 : -1;
    int err = dx + dy;
    int x = x0, y = y0;
    if (max > 32) max = 32;
    while (n < max && n < range) {
        if (x == tx && y == ty) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
        Tile t = tile_at(lv, x, y);
        if (tile_opaque(t) || tile_solid(t)) break;     /* bounces off: lands in front */
        out[n++] = y * MAP_W + x;
        if (game_mob_at(g, x, y)) { *hits = true; break; }
    }
    return n;
}

bool game_throw(Game *g, int slot, int tx, int ty)
{
    if (g->state != GS_PLAY || g->proj.active || slot < 0 || slot >= g->ninv) return false;
    Projectile *pr = &g->proj;
    bool hits;
    ItemKind k = g->inv[slot].kind;
    int n = game_throw_path(g, k, tx, ty, pr->path, 32, &hits);
    if (n == 0) { game_msg(g, C_DIM, "you can't throw there."); return false; }
    game_stop(g);
    pr->active = true;
    pr->kind = k;
    pr->sx = g->player.x;
    pr->sy = g->player.y;
    pr->n = n;
    pr->pos = 0;
    pr->hit = hits;
    take_one(g, slot);
    Mob *p = &g->player;
    int lx = pr->path[n - 1] % MAP_W;
    if (lx != p->x) p->flip = lx < p->x;
    p->bump = 0.6f;
    p->bdx = (lx > p->x) - (lx < p->x);
    p->bdy = (pr->path[n - 1] / MAP_W > p->y) - (pr->path[n - 1] / MAP_W < p->y);
    return true;
}

static void make_noise(Game *g, int x, int y, int radius)
{
    Mob *ms = g->mobs[g->depth];
    for (int i = 0; i < MAX_MOBS; i++) {
        Mob *m = &ms[i];
        if (!m->alive || cheb(m->x, m->y, x, y) > radius) continue;
        if (m->asleep) {
            if (!rng_chance(&g->rng, 60)) continue;
            m->asleep = false;
            if (game_level(g)->f[m->y][m->x] & F_VIS) game_msg(g, C_WARN, "the %s stirs at the noise.", MOB_INFO[m->kind].name);
        }
        m->hunting = true;      /* goes to look */
        m->tx = x;
        m->ty = y;
    }
}

static void resolve_throw(Game *g)
{
    Projectile *pr = &g->proj;
    Level *lv = game_level(g);
    int c = pr->path[pr->n - 1], x = c % MAP_W, y = c / MAP_W;
    Mob *m = pr->hit ? game_mob_at(g, x, y) : NULL;
    const char *mn = m ? MOB_INFO[m->kind].name : "";
    bool into_chasm = lv->t[y][x] == T_CHASM;
    char t[16];
    switch (pr->kind) {
    case IT_DART:
        if (m && m->kind == MK_SHADE) {
            game_msg(g, C_DIM, "the dart passes straight through the shade.");
            m->hunting = true;
            m = NULL;
        }
        if (m) {
            int chance = m->asleep ? 100 : 88 - MOB_INFO[m->kind].def;
            m->asleep = false;
            if ((int)(rng_u32(&g->rng) % 100) < chance) {
                int dmg = rng_range(&g->rng, 2, 4);
                m->hp -= dmg;
                m->flash = 1;
                snprintf(t, sizeof t, "%d", dmg);
                floater(g, x, y, 0xffd27a, t);
                if (m->hp <= 0) { kill_mob(g, m); game_msg(g, C_GOOD, "the dart kills the %s!", mn); }
                else game_msg(g, C_TEXT, "the dart hits the %s.", mn);
            } else {
                floater(g, x, y, 0xb0b0b0, "miss");
                game_msg(g, C_DIM, "the dart misses the %s.", mn);
            }
        }
        push_fx(g, FX_SPARKS, x, y);
        if (into_chasm) {
            int bx = x, by = y;
            if (g->lv[g->depth + 1]) {
                nearest_floor(g->lv[g->depth + 1], &bx, &by);
                place_item(g, g->depth + 1, IT_DART, 1, bx, by, false);
            }
            game_msg(g, C_DIM, "the dart clatters away into the chasm.");
        } else if (rng_chance(&g->rng, 85)) {
            place_item(g, g->depth, IT_DART, 1, x, y, false);
        } else {
            game_msg(g, C_DIM, "the dart snaps.");
        }
        make_noise(g, x, y, 5);
        break;
    case IT_TORCH:
        if (m && m->kind == MK_SHADE) {
            int dmg = rng_range(&g->rng, 4, 8);
            m->hp -= dmg;
            m->flash = 1;
            snprintf(t, sizeof t, "%d", dmg);
            floater(g, x, y, 0xffa040, t);
            push_fx(g, FX_BURN, x, y);
            if (m->hp <= 0) { kill_mob(g, m); push_fx(g, FX_DISSOLVE, x, y); game_msg(g, C_GOOD, "the torch sears the shade and it burns away to nothing!"); }
            else game_msg(g, C_TEXT, "the torch sears the shade!");
        } else if (m) {
            int dmg = rng_range(&g->rng, 1, 3);
            m->hp -= dmg;
            m->flash = 1;
            m->asleep = false;
            snprintf(t, sizeof t, "%d", dmg);
            floater(g, x, y, 0xffa040, t);
            if (m->hp <= 0) { kill_mob(g, m); game_msg(g, C_GOOD, "the torch bursts over the %s and it dies in the flames!", mn); }
            else game_msg(g, C_TEXT, "the torch bursts against the %s in a shower of sparks!", mn);
        }
        if (into_chasm) {
            int bx = x, by = y;
            if (g->lv[g->depth + 1]) {
                nearest_floor(g->lv[g->depth + 1], &bx, &by);
                floor_torch(g, g->depth + 1, bx, by);
                g->lower_dirty = true;
            }
            push_fx(g, FX_FALL, x, y);
            game_msg(g, C_INFO, "the torch tumbles into the chasm and lands far below, still burning.");
        } else {
            floor_torch(g, g->depth, x, y);
            push_fx(g, FX_SPARKS, x, y);
            if (!m) game_msg(g, C_TEXT, "the torch lands and burns on the floor.");
        }
        make_noise(g, x, y, 3);
        break;
    case IT_POTION:
        push_fx(g, FX_SHATTER, x, y);
        if (m) game_msg(g, C_DIM, "the flask shatters against the %s, wasted.", mn);
        else game_msg(g, C_DIM, "the flask shatters%s. Healing potions are better drunk.", into_chasm ? " far below" : "");
        make_noise(g, x, y, 6);
        break;
    default:
        break;
    }
    if (m && m->alive) { m->hunting = true; m->tx = g->player.x; m->ty = g->player.y; }
    end_turn(g);
}

void game_torch_toggle(Game *g)
{
    if (g->state != GS_PLAY) return;
    if (g->torch_lit) {
        g->torch_lit = false;
        game_msg(g, C_DIM, "you snuff out your torch (%d turns of it left). The dark closes in.", g->torch_fuel);
        end_turn(g);
        return;
    }
    if (g->torch_fuel <= 0) {
        int slot = -1;
        for (int i = 0; i < g->ninv; i++) if (g->inv[i].kind == IT_TORCH) slot = i;
        if (slot < 0) { game_msg(g, C_DIM, "you have no torches."); return; }
        take_one(g, slot);
        g->torch_fuel = TORCH_FUEL;
        game_msg(g, C_WARN, "you light a torch. Its glow pushes back the dark, and announces you.");
    } else {
        game_msg(g, C_WARN, "you relight your torch.");
    }
    g->torch_lit = true;
    end_turn(g);
}

bool game_use(Game *g, int slot)
{
    if (g->state != GS_PLAY || slot < 0 || slot >= g->ninv) return false;
    Mob *p = &g->player;
    switch (g->inv[slot].kind) {
    case IT_TORCH:
        if (g->torch_lit) {
            /* swap the burning stub for a fresh torch */
            take_one(g, slot);
            g->torch_fuel = TORCH_FUEL;
            game_msg(g, C_WARN, "you light a fresh torch from the old one.");
            end_turn(g);
        } else if (g->torch_fuel > 0) {
            game_torch_toggle(g);
        } else {
            game_torch_toggle(g);
        }
        return true;
    case IT_POTION: {
        take_one(g, slot);
        int heal = 15;
        if (p->hp + heal > p->maxhp) heal = p->maxhp - p->hp;
        p->hp += heal;
        char t[16];
        snprintf(t, sizeof t, "+%d", heal);
        floater(g, p->x, p->y, 0x8ee070, t);
        game_msg(g, C_GOOD, "you drink the potion. Warmth floods through you.");
        end_turn(g);
        return true;
    }
    default:
        game_msg(g, C_DIM, "you can't use that; try throwing it.");
        return false;
    }
}

bool game_drop(Game *g, int slot)
{
    if (g->state != GS_PLAY || slot < 0 || slot >= g->ninv) return false;
    Mob *p = &g->player;
    FloorItem *here = game_item_at(g, p->x, p->y);
    Slot s = g->inv[slot];
    if (here && here->kind != s.kind) { game_msg(g, C_DIM, "there's already something here."); return false; }
    if (!place_item(g, g->depth, s.kind, s.count, p->x, p->y, true)) return false;
    for (int i = slot; i < g->ninv - 1; i++) g->inv[i] = g->inv[i + 1];
    g->ninv--;
    char nm[64];
    game_msg(g, C_TEXT, "you put down %s.", game_item_name(s.kind, s.count, nm, sizeof nm));
    end_turn(g);
    return true;
}
