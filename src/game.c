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
    [MK_SHADE]  = { "shade", "A scrap of living darkness that hunts by the warmth of the living. A carried torch keeps "
                    "it at bay, but it gnaws at the flame from the edge of the light. Only a fixed fire burns it: a "
                    "sconce lit around it, or a torch thrown at it. Its touch leaves a chill that only a fire drives "
                    "out, and darts pass straight through it.",
                    10, 80, 1, 3, 20, 9 },
    [MK_BAT]    = { "bat", "A cave bat as wide as a cloak. It flits about erratically, quick as thought, and light draws it "
                    "from far away. It flies over chasms and water alike.", 6, 75, 1, 3, 20, 12 },
    [MK_SLINGER] = { "goblin slinger", "A goblin with a sling and a pouch of stones. When it can see you clearly, in the "
                     "light, it pelts you from a distance; in the dark it has to come close.", 8, 70, 1, 4, 5, 8 },
    [MK_OGRE]   = { "ogre", "A hulking brute, slow but terribly strong. It moves and swings only every other turn, and it "
                    "is far too heavy to knock back.", 18, 70, 3, 8, 5, 6 },
    [MK_EEL]    = { "eel", "A long, pale-eyed eel that lurks unseen in deep water and lunges at anything in it or beside "
                    "it. It never leaves the water.", 12, 80, 2, 5, 10, 7 },
    [MK_HOLLOW] = { "Hollow", "The thing that put out the Heartflame: a shade grown vast on the warmth of everyone who ever "
                    "came down here. Your torch doesn't frighten it and salt won't stop it. A fixed fire burns it but can't "
                    "drive it off, a flash of powder dazzles it, darts pass through it, and its touch is bitterly cold. "
                    "While you kindle the Heartflame, every blow it lands smothers the young flame.",
                    50, 95, 5, 10, 30, 10 },
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
    [IT_SWORD]  = { "short sword", "short swords",
                    "A plain, well-balanced blade. It does nothing clever and nothing badly.",
                    0, true, GEAR_WEAPON, 85, 2, 5, 0 },
    [IT_DAGGER] = { "dagger", "daggers",
                    "A slim blade for quiet work. A foe that hasn't noticed you (asleep, or not hunting you) "
                    "takes triple damage. Weak in an open fight.",
                    0, true, GEAR_WEAPON, 95, 1, 3, 0 },
    [IT_SPEAR]  = { "spear", "spears",
                    "Long reach: every thrust also strikes whatever stands behind your target.",
                    0, true, GEAR_WEAPON, 80, 2, 5, 0 },
    [IT_AXE]    = { "axe", "axes",
                    "A wide, heavy swing that strikes every foe around you at once, though it's hard to aim.",
                    0, true, GEAR_WEAPON, 72, 2, 5, 0 },
    [IT_MACE]   = { "mace", "maces",
                    "A brutal iron head. A hit knocks the foe back a step: into a wall for extra harm, into "
                    "deep water, or over the edge of a chasm.",
                    0, true, GEAR_WEAPON, 70, 3, 6, 0 },
    [IT_LEATHER] = { "leather jerkin", "leather jerkins",
                    "Boiled leather. Some protection and no drawbacks.",
                    0, true, GEAR_ARMOUR, 0, 0, 0, 10 },
    [IT_CLOAK]  = { "shadow cloak", "shadow cloaks",
                    "Thin, dark wool: little protection, but in the dark things must come close to notice you, "
                    "and shades feel your warmth from only half as far.",
                    0, true, GEAR_ARMOUR, 0, 0, 0, 4 },
    [IT_MAIL]   = { "mail coat", "mail coats",
                    "Riveted iron rings: strong protection, but it rings with every step, waking what sleeps "
                    "nearby, and in deep water it drags you under.",
                    0, true, GEAR_ARMOUR, 0, 0, 0, 22 },
    [IT_OIL]    = { "oil flask", "oil flasks",
                    "A stoppered flask of lamp oil. Thrown, it bursts into a pool of burning oil: a fixed fire that "
                    "burns shades, sears anything standing in it, and that nothing will willingly walk through. It "
                    "even burns on water. Or pour it over your torch's head for 50 more turns of light.", 7, true },
    [IT_FLASH]  = { "twist of flash powder", "twists of flash powder",
                    "A paper twist of white powder. Thrown, it goes off in a blinding flash: everything that sees it "
                    "reels, dazzled, for a few turns and loses track of you, and shades are scorched. The flash also "
                    "shows you the room it lights. Water spoils it, and the bang carries.", 7, false },
    [IT_SALT]   = { "pouch of salt", "pouches of salt",
                    "Coarse grey salt. Pour it in a ring around you and no shade can cross it, though anything else "
                    "that walks through scuffs it away (you too, when you step out).", 0, true },
    [IT_WARMTH] = { "warming draught", "warming draughts",
                    "A peppery amber cordial that burns all the way down. It drives out a shade's chill at once, and "
                    "for 80 turns after, no shade's touch can chill you.", 0, true },
};

static bool stacks(ItemKind k) { return ITEM_INFO[k].gear == GEAR_NONE; }

void game_weapon_stats(const Game *g, const Slot *w, int *acc, int *dmin, int *dmax)
{
    (void)g;
    const ItemInfo *ii = &ITEM_INFO[w->kind];
    *acc = ii->acc + 3 * w->plus;
    *dmin = ii->dmin + w->plus / 2;
    *dmax = ii->dmax + w->plus;
}

int game_armour_def(const Slot *a) { return ITEM_INFO[a->kind].def + 3 * a->plus; }

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

/* ---------------------------------------------------------------- sound */

static void sfx_ex(Game *g, SoundKind k, int x, int y, bool global, float delay, float vol)
{
    if (g->nsnd >= MAX_SND) return;
    g->snd[g->nsnd++] = (SoundEv){ k, x, y, global, delay, vol };
}

static void sfx(Game *g, SoundKind k, int x, int y) { sfx_ex(g, k, x, y, false, 0, 1); }
static void push_fx(Game *g, FxKind k, int x, int y);

/* a creature's voice */
static void vocal(Game *g, const Mob *m)
{
    static const SoundKind V[MK_COUNT] = {
        [MK_PLAYER] = SND_COUNT, [MK_RAT] = SND_RAT, [MK_GOBLIN] = SND_GOBLIN, [MK_SHADE] = SND_SHADE,
        [MK_BAT] = SND_BAT, [MK_SLINGER] = SND_GOBLIN, [MK_OGRE] = SND_OGRE, [MK_EEL] = SND_EEL,
        [MK_HOLLOW] = SND_SHADE,
    };
    if (V[m->kind] != SND_COUNT) sfx(g, V[m->kind], m->x, m->y);
}

/* a shade, or the great one: things of the dark that hunt by warmth, chill with a touch and burn in fire */
static bool shadeish(MobKind k) { return k == MK_SHADE || k == MK_HOLLOW; }

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

int game_max_hp(const Game *g)
{
    int m = g->player.maxhp - g->chill;
    return m < 1 ? 1 : m;
}

bool game_by_fire(Game *g)
{
    return level_fire(game_level(g), g->player.x, g->player.y) > SHADE_FEAR;
}

#define TORCHES_PER_LEVEL 0.75f    /* the paced supply of torches on the floor */

/* who lives at depth d: rats thin out, goblins, bats, slingers and ogres come in */
static MobKind pick_kind(Rng *r, int d)
{
    int w[MK_COUNT] = { 0 };
    w[MK_RAT] = 60 - d * 6 > 10 ? 60 - d * 6 : 10;
    w[MK_GOBLIN] = 30 + d * 5;
    if (d >= 2) w[MK_BAT] = 15 + d * 2;
    if (d >= 3) w[MK_SLINGER] = 10 + d * 3;
    if (d >= 4) w[MK_OGRE] = 4 + d * 2;
    int sum = 0;
    for (int k = 0; k < MK_COUNT; k++) sum += w[k];
    int roll = rng_range(r, 0, sum - 1);
    for (int k = 0; k < MK_COUNT; k++) {
        if (roll < w[k]) return (MobKind)k;
        roll -= w[k];
    }
    return MK_RAT;
}

static int cheb(int ax, int ay, int bx, int by);

static void spawn_mobs(Game *g, int d)
{
    Level *lv = g->lv[d];
    Rng r = { lv->seed * 31u + 5 };
    int n = 3 + d + rng_range(&r, 0, 2), placed = 0;
    for (int tries = 0; tries < 2000 && placed < n && placed < MAX_MOBS; tries++) {
        int x = rng_range(&r, 1, MAP_W - 2), y = rng_range(&r, 1, MAP_H - 2);
        if (lv->t[y][x] != T_FLOOR || lv->room[y][x] < 0) continue;
        if (abs(x - lv->upx) + abs(y - lv->upy) < 9) continue;
        if (lv->room[y][x] == lv->hall && lv->hall >= 0) continue;     /* the hall has its own (below) */
        bool taken = false;
        for (int i = 0; i < placed; i++)
            if (g->mobs[d][i].x == x && g->mobs[d][i].y == y) taken = true;
        if (taken) continue;
        MobKind k = pick_kind(&r, d);
        /* shades gather in the dark rooms from depth 2 */
        int rm = lv->room[y][x];
        if (d >= 2 && level_fire(lv, x, y) < SHADE_FEAR * 0.5f
            && rng_chance(&r, lv->rooms[rm].dark ? 35 + d * 5 : 4 + d * 2))
            k = MK_SHADE;
        Mob *m = &g->mobs[d][placed++];
        mob_init(m, k, x, y);
        m->lvl = d;
        m->hp = m->maxhp = MOB_INFO[k].hp + MOB_INFO[k].hp * (d - 1) / 8;
        m->asleep = k != MK_SHADE && rng_chance(&r, 65);
        m->flip = rng_chance(&r, 50);
    }
    /* eels in the deeper pools */
    int deep = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) deep += lv->t[y][x] == T_DEEP;
    int eels = d < 2 || deep < 8 || !rng_chance(&r, 75) ? 0 : deep >= 30 && d >= 5 ? 2 : 1;
    for (int tries = 0; tries < 2000 && eels > 0 && placed < MAX_MOBS; tries++) {
        int x = rng_range(&r, 1, MAP_W - 2), y = rng_range(&r, 1, MAP_H - 2);
        if (lv->t[y][x] != T_DEEP || abs(x - lv->upx) + abs(y - lv->upy) < 6) continue;
        bool taken = false;
        for (int i = 0; i < placed; i++)
            if (g->mobs[d][i].x == x && g->mobs[d][i].y == y) taken = true;
        if (taken) continue;
        Mob *m = &g->mobs[d][placed++];
        mob_init(m, MK_EEL, x, y);
        m->lvl = d;
        m->hp = m->maxhp = MOB_INFO[MK_EEL].hp + MOB_INFO[MK_EEL].hp * (d - 1) / 8;
        eels--;
    }
    /* the last level: the Hollow broods by the cold hearth, its brood about the hall */
    if (d == MAX_DEPTH && lv->hall >= 0) {
        Room *hl = &lv->rooms[lv->hall];
        int want = 6;           /* the Hollow, then five shades */
        for (int tries = 0; tries < 4000 && want > 0 && placed < MAX_MOBS; tries++) {
            int x, y;
            if (want == 6 && tries < 40) { x = lv->downx + rng_range(&r, -1, 1); y = lv->downy + 2; }
            else { x = rng_range(&r, hl->x, hl->x + hl->w - 1); y = rng_range(&r, hl->y, hl->y + hl->h - 1); }
            if (!in_map(x, y) || lv->t[y][x] != T_FLOOR || lv->room[y][x] != lv->hall) continue;
            if (want < 6 && cheb(x, y, lv->downx, lv->downy) < 2) continue;
            bool taken = false;
            for (int i = 0; i < placed; i++)
                if (g->mobs[d][i].alive && g->mobs[d][i].x == x && g->mobs[d][i].y == y) taken = true;
            if (taken) continue;
            Mob *m = &g->mobs[d][placed++];
            if (want == 6) {
                mob_init(m, MK_HOLLOW, x, y);
                m->lvl = 1;                     /* its strength is its own, not the depth's */
            } else {
                mob_init(m, MK_SHADE, x, y);
                m->lvl = d;
                m->hp = m->maxhp = MOB_INFO[MK_SHADE].hp + MOB_INFO[MK_SHADE].hp * (d - 1) / 8;
            }
            m->flip = rng_chance(&r, 50);
            want--;
        }
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
        ItemKind k = roll < 22 ? IT_TORCH : roll < 54 ? IT_DART : roll < 75 ? IT_POTION : roll < 85 ? IT_OIL
                   : roll < 92 ? IT_FLASH : roll < 96 ? IT_SALT : IT_WARMTH;
        if ((k == IT_SALT || k == IT_WARMTH) && d < 2) k = IT_POTION;      /* no shades up there yet */
        /* torches are the clock: don't let luck run far ahead of the pace */
        if (k == IT_TORCH && g->torches_made + 1 > d * TORCHES_PER_LEVEL + 1.5f) k = IT_DART;
        if (k == IT_TORCH) g->torches_made++;
        FloorItem *it = &g->items[d][placed++];
        *it = (FloorItem){ true, k, 1, x, y, false, 0 };
        if (it->kind == IT_DART) it->count = rng_range(&r, 2, 4);
    }
    /* a piece of gear on most levels, sometimes already tempered deeper down */
    if (rng_chance(&r, 70)) {
        static const ItemKind GEAR[] = { IT_DAGGER, IT_SPEAR, IT_AXE, IT_MACE, IT_SWORD, IT_CLOAK, IT_MAIL, IT_LEATHER };
        for (int tries = 0; tries < 2000 && placed < MAX_ITEMS; tries++) {
            int x = rng_range(&r, 1, MAP_W - 2), y = rng_range(&r, 1, MAP_H - 2);
            if (lv->t[y][x] != T_FLOOR || lv->room[y][x] < 0) continue;
            bool taken = false;
            for (int i = 0; i < placed; i++)
                if (g->items[d][i].x == x && g->items[d][i].y == y) taken = true;
            if (taken) continue;
            ItemKind k = GEAR[rng_range(&r, 0, (int)(sizeof GEAR / sizeof *GEAR) - 1)];
            int plus = 0;
            for (int i = 0; i < d / 3; i++) if (rng_chance(&r, 40)) plus++;
            g->items[d][placed++] = (FloorItem){ true, k, 1, x, y, false, plus };
            break;
        }
    }
    /* ...nor fall far behind it */
    for (int tries = 0; tries < 2000 && g->torches_made < (int)(d * TORCHES_PER_LEVEL) && placed < MAX_ITEMS; tries++) {
        int x = rng_range(&r, 1, MAP_W - 2), y = rng_range(&r, 1, MAP_H - 2);
        if (lv->t[y][x] != T_FLOOR || lv->room[y][x] < 0) continue;
        bool taken = false;
        for (int i = 0; i < placed; i++)
            if (g->items[d][i].x == x && g->items[d][i].y == y) taken = true;
        if (taken) continue;
        g->items[d][placed++] = (FloorItem){ true, IT_TORCH, 1, x, y, false, 0 };
        g->torches_made++;
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
    float r = c->g->torch_lit ? 7.0f : 3.6f + c->g->lantern;
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

bool game_mob_hidden(const Mob *m) { return m->kind == MK_EEL && m->surfaced <= 0; }

bool game_burning(Game *g, int x, int y) { return in_map(x, y) && game_level(g)->burn[y][x] > 0; }

bool game_final(const Game *g) { return g->depth == MAX_DEPTH; }

/* a monster you can make out: in view, or a shade's eyes in the dark */
bool game_mob_noticed(Game *g, const Mob *m)
{
    Level *lv = game_level(g);
    if (!m->alive || game_mob_hidden(m)) return false;
    uint8_t f = lv->f[m->y][m->x];
    return (f & F_VIS) || (shadeish(m->kind) && (f & F_LOS));
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
    fov_cast(lv, g->player.x, g->player.y, g->torch_lit ? 8 : 4 + g->lantern, plight_mark, &c);
    fov_cast(lv, g->player.x, g->player.y, 24, view_mark, &c);
    int n = count_visible_mobs(g);
    bool fresh = false;
    Mob *all = g->mobs[g->depth];
    for (int i = 0; i < MAX_MOBS; i++)
        if (game_mob_noticed(g, &all[i]) && !all[i].known) {
            all[i].known = true;
            fresh = true;
            if (all[i].kind == MK_HOLLOW)
                game_msg(g, C_BAD, "something vast uncoils in the dark by the hearth: the Hollow, that put out the Heartflame.");
        }
    if (fresh) sfx_ex(g, SND_ALERT, 0, 0, true, 0, 1);     /* something new is here */
    if (n > g->seen_mobs && g->autom != AUTO_NONE) {
        Mob *m = g->mobs[g->depth];
        for (int i = 0; i < MAX_MOBS; i++)
            if (game_mob_noticed(g, &m[i])) {
                if (!(lv->f[m[i].y][m[i].x] & F_VIS)) game_msg(g, C_WARN, "two pale eyes watch you from the dark.");
                else game_msg(g, C_WARN, "you see %s %s%s.", m[i].kind == MK_HOLLOW ? "the" : "a", MOB_INFO[m[i].kind].name,
                              m[i].asleep ? " (asleep)" : "");
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
    g->inv[0] = (Slot){ IT_TORCH, 2, 0 };
    g->inv[1] = (Slot){ IT_DART, 5, 0 };
    g->inv[2] = (Slot){ IT_POTION, 1, 0 };
    g->inv[3] = (Slot){ IT_OIL, 1, 0 };
    g->ninv = 4;
    g->weapon = (Slot){ IT_SWORD, 1, 0 };
    g->armour = (Slot){ IT_LEATHER, 1, 0 };
    g->deepest = 1;
    game_msg(g, C_INFO, "far below, at depth %d, the Heartflame has gone cold. Carry fire down to it.", MAX_DEPTH);
    game_msg(g, C_INFO, "you descend into the dark. Light is precious down here.");
    game_msg(g, C_DIM, "click to travel, x explores, ? for help.");
    return g;
}

static void update_light(Game *g);

/* a game just read back from a save: rebuild what's worked out rather than stored */
void game_resume(Game *g)
{
    Level *lv = game_level(g);
    g->autom = AUTO_NONE;
    g->nfx = g->nsnd = 0;
    g->proj.active = false;
    g->forge_req = false;
    g->level_changed = true;
    g->lower_dirty = true;
    memset(g->fl, 0, sizeof g->fl);
    Mob *ms = g->mobs[g->depth];
    for (int i = 0; i <= MAX_MOBS; i++) {
        Mob *m = i < MAX_MOBS ? &ms[i] : &g->player;
        m->rx = m->ax = (float)m->x;
        m->ry = m->ay = (float)m->y;
        m->at = 1;
        m->bump = m->flash = 0;
    }
    for (int d = 0; d < MAX_DEPTH + 2; d++)
        if (g->lv[d]) g->lv[d]->light_dirty = true;
    update_light(g);
    compute_view(g);
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) g->visf[y][x] = (lv->f[y][x] & F_VIS) ? 1.0f : 0.0f;
    game_msg(g, C_INFO, "you take up where you left off, on depth %d.", g->depth);
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

static void ember(Game *g, int n, const char *why);

static void kill_mob(Game *g, Mob *m)
{
    m->alive = false;
    g->kills++;
    if (m->kind == MK_SHADE) ember(g, 1, "the shade leaves a cold ember behind as it goes");
    if (m->kind == MK_HOLLOW) {
        game_msg(g, C_GOOD, "the Hollow comes apart like smoke in a wind, with a sound like a long sigh.");
        ember(g, 5, "at its heart lies a knot of cold embers");
    }
}

static void mob_stats(Game *g, const Mob *m, int *acc, int *dmin, int *dmax, int *def)
{
    const MobInfo *mi = &MOB_INFO[m->kind];
    if (m == &g->player) {
        game_weapon_stats(g, &g->weapon, acc, dmin, dmax);
        *def = mi->def + game_armour_def(&g->armour);
        return;
    }
    int up = m->lvl > 1 ? m->lvl - 1 : 0;      /* deeper monsters are a little sharper */
    *acc = mi->acc + up * 2;
    *dmin = mi->dmin + up / 5;
    *dmax = mi->dmax + up / 3;
    *def = mi->def + up;
}

static void ember(Game *g, int n, const char *why)
{
    g->embers += n;
    g->embers_got += n;
    game_msg(g, C_GOOD, "%s (+%d ember%s, %d now).", why, n, n == 1 ? "" : "s", g->embers);
    floater(g, g->player.x, g->player.y, 0xffa040, "+ember");
}

static void hearth_light(Game *g);

/* returns true if it hit */
static bool attack(Game *g, Mob *a, Mob *d)
{
    const MobInfo *ai = &MOB_INFO[a->kind], *di = &MOB_INFO[d->kind];
    bool pl_att = a == &g->player, pl_def = d == &g->player;
    int acc, dmin, dmax, def, x0, x1, x2;
    mob_stats(g, a, &acc, &dmin, &dmax, &x0);
    mob_stats(g, d, &x0, &x1, &x2, &def);
    bool unaware = !pl_def && (d->asleep || !d->hunting || d->daze > 0);
    a->bump = 1.0f;
    a->bdx = d->x - a->x;
    a->bdy = d->y - a->y;
    if (!shadeish(a->kind) && a->kind != MK_EEL && a->kind != MK_BAT)
        sfx_ex(g, SND_SWING, a->x, a->y, false, 0, pl_att ? 1.0f : a->kind == MK_OGRE ? 1.0f : 0.7f);
    if (!pl_att && a->kind != MK_EEL && rng_chance(&g->rng, 30)) vocal(g, a);
    if (d->kind == MK_EEL && d->surfaced < 2) d->surfaced = 2;      /* struck: it shows itself */
    if (a->bdx) a->flip = a->bdx < 0;
    int chance = acc - def;
    if (d->asleep || d->daze > 0) chance = 100;
    d->asleep = false;
    if (!pl_def) { d->hunting = true; d->tx = a->x; d->ty = a->y; }
    if ((int)(rng_u32(&g->rng) % 100) >= chance) {
        if (pl_att) game_msg(g, C_DIM, "you miss the %s.", di->name);
        else if (pl_def) game_msg(g, C_DIM, "the %s misses you.", ai->name);
        floater(g, d->x, d->y, 0xb0b0b0, "miss");
        return false;
    }
    int dmg = rng_range(&g->rng, dmin, dmax);
    bool sneak = pl_att && g->weapon.kind == IT_DAGGER && unaware;
    if (sneak) dmg *= 3;
    int chill = 0;
    bool warded = false;
    if (shadeish(a->kind) && pl_def && g->warm_left > 0) {
        warded = true;                  /* the draught's warmth: the cold finds no hold */
    } else if (shadeish(a->kind) && pl_def) {
        /* the cold of its touch takes from your max HP until a fire warms you */
        chill = a->kind == MK_HOLLOW ? rng_range(&g->rng, 2, 3) : rng_range(&g->rng, 1, 2);
        if (g->chill + chill > d->maxhp / 2) chill = d->maxhp / 2 - g->chill;     /* it can take half, no more */
        if (chill < 0) chill = 0;
        g->chill += chill;
        g->warm_t = 0;
        if (d->hp > game_max_hp(g)) d->hp = game_max_hp(g);
    }
    d->hp -= dmg;
    d->flash = 1.0f;
    sfx_ex(g, pl_def ? SND_HURT : SND_HIT, d->x, d->y, false, 0.06f, 1);
    char t[16];
    snprintf(t, sizeof t, "%d", dmg);
    floater(g, d->x, d->y, pl_def ? 0xff5a4a : 0xffd27a, t);
    if (pl_def) {
        g->shake = a->kind == MK_OGRE ? 0.7f : 0.35f;
        if (a->kind == MK_OGRE) sfx_ex(g, SND_THUD, d->x, d->y, false, 0.08f, 0.9f);
        game_stop(g);
        if (d->hp <= 0) {
            game_msg(g, C_BAD, "the %s kills you...", ai->name);
            g->state = GS_DEAD;
            g->end_t = g->time;
            sfx_ex(g, SND_DIE, d->x, d->y, true, 0.25f, 1);
        } else if (chill) {
            game_msg(g, C_BAD, "the %s's touch chills you to the bone (max HP -%d until a fire warms you).", ai->name, chill);
            floater(g, d->x, d->y, 0x9ad8ff, "chill");
        } else if (warded) {
            game_msg(g, C_BAD, "the %s's touch stings, but its cold finds no hold in you.", ai->name);
        } else {
            game_msg(g, C_BAD, "the %s hits you.", ai->name);
        }
        if (a->kind == MK_HOLLOW && d->hp > 0 && game_final(g) && g->kindle > 0 && g->kindle < 3) {
            /* its cold breath falls on the young flame */
            g->kindle--;
            hearth_light(g);
            game_msg(g, C_BAD, g->kindle ? "the Hollow's breath gusts over the hearth, and the young flame shrinks."
                                         : "the Hollow's breath gusts over the hearth, and the flame dies back to embers.");
        }
        return true;
    }
    if (d->hp <= 0) {
        kill_mob(g, d);
        game_msg(g, C_GOOD, sneak ? "you strike the unaware %s dead!" : "you slay the %s!", di->name);
        if (shadeish(d->kind)) { push_fx(g, FX_DISSOLVE, d->x, d->y); sfx(g, SND_DISSOLVE, d->x, d->y); }
        else sfx_ex(g, SND_KILL, d->x, d->y, false, 0.1f, 1);
    } else {
        game_msg(g, C_TEXT, sneak ? "you strike the unaware %s!" : "you hit the %s.", di->name);
    }
    return true;
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

static void mob_go(Game *g, Mob *m, int nx, int ny);

/* can this monster step from where it is to (nx, ny)? Each kind has its own ground. */
static bool mob_can_enter(Game *g, const Mob *m, int nx, int ny)
{
    Level *lv = game_level(g);
    if (!in_map(nx, ny) || !diag_ok(lv, m->x, m->y, nx - m->x, ny - m->y)) return false;
    if ((g->player.x == nx && g->player.y == ny) || game_mob_at(g, nx, ny)) return false;
    Tile t = lv->t[ny][nx];
    if (lv->burn[ny][nx]) return false;                                         /* nothing walks into burning oil */
    switch (m->kind) {
    case MK_BAT:    return t == T_CHASM || (tile_walkable(t) && t != T_DOOR);  /* flies, but can't open doors */
    case MK_EEL:    return t == T_DEEP;                                         /* never leaves the water */
    case MK_SHADE:  return tile_walkable(t) && !lv->salt[ny][nx] && game_firelight(g, nx, ny) <= SHADE_FEAR;
    case MK_HOLLOW: return tile_walkable(t);                                    /* nothing but burning oil holds it back */
    case MK_GOBLIN: case MK_SLINGER: case MK_OGRE:
                    return tile_walkable(t) && t != T_DEEP;                     /* they won't swim */
    default:        return tile_walkable(t);
    }
}

/* a step in a random direction (wandering, or a bat's flitting) */
static void mob_wander(Game *g, Mob *m)
{
    Level *lv = game_level(g);
    for (int tries = 0; tries < 3; tries++) {
        int d = rng_range(&g->rng, 0, 7);
        int nx = m->x + DX8[d], ny = m->y + DY8[d];
        if (mob_can_enter(g, m, nx, ny) && lv->t[ny][nx] != T_DOOR) { mob_go(g, m, nx, ny); return; }
        if (m->kind != MK_BAT) return;
    }
}

static void mob_step_toward(Game *g, Mob *m, int tx, int ty)
{
    Level *lv = game_level(g);
    int best = -1, bd = cheb(m->x, m->y, tx, ty) * 10 + 5;
    for (int d = 0; d < 8; d++) {
        int nx = m->x + DX8[d], ny = m->y + DY8[d];
        if (!mob_can_enter(g, m, nx, ny)) continue;
        int dd = cheb(nx, ny, tx, ty) * 10 + abs(nx - tx) + abs(ny - ty);
        if (dd < bd) { bd = dd; best = d; }
    }
    if (best < 0) return;
    int nx = m->x + DX8[best], ny = m->y + DY8[best];
    if (lv->t[ny][nx] == T_DOOR) {
        lv->t[ny][nx] = T_DOOR_OPEN;
        lv->light_dirty = true;
        sfx(g, SND_DOOR, nx, ny);
        return;
    }
    mob_go(g, m, nx, ny);
}

static void push_fx(Game *g, FxKind k, int x, int y);
static void make_noise(Game *g, int x, int y, int radius);

/* a monster steps to (nx, ny), splashing if it's water */
static void mob_go(Game *g, Mob *m, int nx, int ny)
{
    start_tween(m, nx, ny);
    Level *lv = game_level(g);
    Tile t = tile_at(lv, nx, ny);
    if (m->kind == MK_BAT || m->kind == MK_EEL) return;   /* on the wing, or under the surface: silent */
    if (lv->salt[ny][nx] && m->kind != MK_SHADE) {
        lv->salt[ny][nx] = 0;                               /* scuffed away */
        if (lv->f[ny][nx] & F_VIS) game_msg(g, C_DIM, "the %s scuffs through the salt.", MOB_INFO[m->kind].name);
    }
    if (tile_water(t)) push_fx(g, FX_SPLASH, nx, ny);
    if (shadeish(m->kind)) return;          /* it makes no sound as it moves */
    float v = m->kind == MK_RAT ? 0.3f : m->kind == MK_OGRE ? 1.0f : 0.55f;
    sfx_ex(g, t == T_DEEP ? SND_SWIM : t == T_SHALLOW ? SND_WADE : SND_STEP, nx, ny, false, 0, v);
}

/* a shade caught in firelight burns and flees for the darkest ground near it */
static void shade_flee(Game *g, Mob *m);

static bool shade_burns(Game *g, Mob *m)
{
    Level *lv = game_level(g);
    if (game_firelight(g, m->x, m->y) <= SHADE_FEAR) return false;
    bool seen = lv->f[m->y][m->x] & (F_VIS | F_LOS);
    if (m->kind == MK_HOLLOW) {
        /* the great one: a torch means nothing to it, and a fixed fire only burns it as it comes on */
        if (level_fire(lv, m->x, m->y) <= SHADE_FEAR) return false;
        int dmg = rng_range(&g->rng, 3, 5);
        m->hp -= dmg;
        m->flash = 1;
        char t[16];
        snprintf(t, sizeof t, "%d", dmg);
        floater(g, m->x, m->y, 0xffa040, t);
        push_fx(g, FX_BURN, m->x, m->y);
        sfx(g, SND_BURN, m->x, m->y);
        if (m->hp <= 0) {
            push_fx(g, FX_DISSOLVE, m->x, m->y);
            sfx(g, SND_DISSOLVE, m->x, m->y);
            kill_mob(g, m);
            return true;
        }
        if (seen && rng_chance(&g->rng, 35)) game_msg(g, C_TEXT, "the Hollow smoulders in the firelight, and comes on regardless.");
        return false;
    }
    if (level_fire(lv, m->x, m->y) <= SHADE_FEAR) {
        /* only your torch's light: it can't hurt the shade, but it can't stay in it either */
        if (seen && rng_chance(&g->rng, 40)) game_msg(g, C_DIM, "the shade recoils from your torch.");
        shade_flee(g, m);
        return true;
    }
    int dmg = rng_range(&g->rng, 3, 5);
    m->hp -= dmg;
    m->flash = 1;
    char t[16];
    snprintf(t, sizeof t, "%d", dmg);
    floater(g, m->x, m->y, 0xffa040, t);
    push_fx(g, FX_BURN, m->x, m->y);
    sfx(g, SND_BURN, m->x, m->y);
    if (m->hp <= 0) {
        kill_mob(g, m);
        push_fx(g, FX_DISSOLVE, m->x, m->y);
        sfx(g, SND_DISSOLVE, m->x, m->y);
        if (seen) game_msg(g, C_GOOD, "the shade burns away to nothing.");
        return true;
    }
    if (m->stun > 0) {
        m->stun--;
        if (seen) game_msg(g, C_TEXT, "the shade writhes in the blaze, unable to flee!");
        return true;
    }
    if (seen) game_msg(g, C_TEXT, "the shade writhes in the firelight!");
    shade_flee(g, m);
    return true;
}

/* A fire just flared up (a sconce lit, a torch landing): shades caught in it are
 * held for a moment, so the blaze has time to burn them. */
static void flare(Game *g)
{
    Level *lv = game_level(g);
    if (lv->light_dirty) level_relight(lv);
    Mob *ms = g->mobs[g->depth];
    for (int i = 0; i < MAX_MOBS; i++)
        if (ms[i].alive && ms[i].kind == MK_SHADE && level_fire(lv, ms[i].x, ms[i].y) > SHADE_FEAR) ms[i].stun = 2;
}

/* step to the darkest ground next to it */
static void shade_flee(Game *g, Mob *m)
{
    Level *lv = game_level(g);
    int best = -1;
    float bf = game_firelight(g, m->x, m->y);
    for (int d = 0; d < 8; d++) {
        int nx = m->x + DX8[d], ny = m->y + DY8[d];
        if (!mob_free(g, nx, ny) || !diag_ok(lv, m->x, m->y, DX8[d], DY8[d]) || lv->t[ny][nx] == T_DOOR) continue;
        if (lv->salt[ny][nx]) continue;
        float f = game_firelight(g, nx, ny);
        if (f < bf) { bf = f; best = d; }
    }
    if (best >= 0) mob_go(g, m, m->x + DX8[best], m->y + DY8[best]);
}

/* bring the level's light and the torch's glow up to date (a sconce just lit,
 * a torch just struck) before the monsters judge them */
static void update_light(Game *g)
{
    Level *lv = game_level(g);
    if (lv->light_dirty) level_relight(lv);
    memset(g->plight, 0, sizeof g->plight);
    ViewCtx c = { g, lv };
    fov_cast(lv, g->player.x, g->player.y, g->torch_lit ? 8 : 4 + g->lantern, plight_mark, &c);
}

/* a clear line for a sling stone: nothing solid and nobody else in the way */
static bool clear_shot(Game *g, const Mob *m)
{
    Level *lv = game_level(g);
    int x0 = m->x, y0 = m->y, x1 = g->player.x, y1 = g->player.y;
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
        if (x0 == x1 && y0 == y1) return true;
        Tile t = tile_at(lv, x0, y0);
        if (tile_opaque(t) || tile_solid(t) || t == T_BRAZIER) return false;
        Mob *o = game_mob_at(g, x0, y0);
        if (o && !game_mob_hidden(o)) return false;
    }
}

static void push_fx_from(Game *g, FxKind k, int sx, int sy, int x, int y);

/* a goblin slinger looses a stone at you */
static void sling(Game *g, Mob *m)
{
    Mob *p = &g->player;
    int ddx = p->x - m->x, ddy = p->y - m->y;
    if (ddx) m->flip = ddx < 0;
    m->bump = 0.5f;
    m->bdx = ddx > 0 ? 1 : ddx < 0 ? -1 : 0;
    m->bdy = ddy > 0 ? 1 : ddy < 0 ? -1 : 0;
    sfx(g, SND_SLING, m->x, m->y);
    push_fx_from(g, FX_STONE, m->x, m->y, p->x, p->y);
    int acc, dmin, dmax, def, x0, x1, x2;
    mob_stats(g, m, &acc, &dmin, &dmax, &x0);
    mob_stats(g, p, &x0, &x1, &x2, &def);
    if ((int)(rng_u32(&g->rng) % 100) >= acc - 10 - def) {
        game_msg(g, C_DIM, "a sling stone whizzes past you.");
        floater(g, p->x, p->y, 0xb0b0b0, "miss");
        sfx_ex(g, SND_CLINK, p->x, p->y, false, 0.25f, 0.6f);
        return;
    }
    int dmg = rng_range(&g->rng, dmin, dmax);
    p->hp -= dmg;
    p->flash = 1.0f;
    g->shake = 0.3f;
    sfx_ex(g, SND_HURT, p->x, p->y, false, 0.25f, 1);
    char t[16];
    snprintf(t, sizeof t, "%d", dmg);
    floater(g, p->x, p->y, 0xff5a4a, t);
    game_stop(g);
    if (p->hp <= 0) {
        game_msg(g, C_BAD, "a sling stone fells you...");
        g->state = GS_DEAD;
        g->end_t = g->time;
        sfx_ex(g, SND_DIE, p->x, p->y, true, 0.45f, 1);
    } else {
        game_msg(g, C_BAD, "a sling stone hits you.");
    }
}

/* An eel feels anything moving in the water and lunges at whatever is beside its pool. */
static void eel_turn(Game *g, Mob *m)
{
    Level *lv = game_level(g);
    Mob *p = &g->player;
    int dist = cheb(m->x, m->y, p->x, p->y);
    if (m->surfaced > 0) m->surfaced--;
    bool senses = dist == 1 || (tile_water(tile_at(lv, p->x, p->y)) && dist <= 6 && los(lv, m->x, m->y, p->x, p->y));
    if (senses) { m->hunting = true; m->tx = p->x; m->ty = p->y; }
    if (dist == 1 && diag_ok(lv, m->x, m->y, p->x - m->x, p->y - m->y)) {
        if (m->surfaced <= 0 && (lv->f[m->y][m->x] & F_VIS)) game_msg(g, C_WARN, "an eel lunges out of the water!");
        m->surfaced = 2;
        sfx(g, SND_EEL, m->x, m->y);
        push_fx(g, FX_SPLASH, m->x, m->y);
        attack(g, m, p);
        return;
    }
    if (m->hunting) {
        mob_step_toward(g, m, m->tx, m->ty);
        if (m->x == m->tx && m->y == m->ty && !senses) m->hunting = false;
    } else if (rng_chance(&g->rng, 25)) {
        mob_wander(g, m);
    }
}

/* one monster's turn; pb is how brightly the player stands out */
static void mob_turn(Game *g, Mob *m, float pb, int *gnaw)
{
    Level *lv = game_level(g);
    Mob *p = &g->player;
    const MobInfo *mi = &MOB_INFO[m->kind];
    if (m->kind == MK_EEL) { eel_turn(g, m); return; }
    if (m->kind != MK_SHADE && lv->burn[m->y][m->x]) {
        /* caught in burning oil: get out of it, whatever else is going on */
        int start = rng_range(&g->rng, 0, 7);
        for (int k = 0; k < 8; k++) {
            int d = (start + k) % 8, nx = m->x + DX8[d], ny = m->y + DY8[d];
            if (mob_can_enter(g, m, nx, ny) && lv->t[ny][nx] != T_DOOR) { mob_go(g, m, nx, ny); return; }
        }
    }
    int dist = cheb(m->x, m->y, p->x, p->y);
    bool cloak = g->armour.kind == IT_CLOAK;
    bool lit = pb > 0.30f;
    float range = lit ? (float)mi->perception : cloak ? 1.5f : 2.5f;
    bool shade = shadeish(m->kind), bat = m->kind == MK_BAT;
    if (shade) {
        /* a shade within your torch's glow gnaws at the flame (the Hollow, greedily) */
        if (g->torch_lit && g->plight[m->y][m->x] > 0 && dist <= 8) *gnaw += m->kind == MK_HOLLOW ? 2 : 1;
        if (shade_burns(g, m) || !m->alive) return;
        range = (float)mi->perception * (cloak ? 0.5f : 1.0f);   /* it feels your warmth, lit or not */
    }
    bool sees = dist <= range && los(lv, m->x, m->y, p->x, p->y);
    if (m->asleep) {
        int pct = !sees ? 0 : dist <= 2 ? 45 : 20;
        if (pct && (int)(rng_u32(&g->rng) % 100) < pct) {
            m->asleep = false;
            m->hunting = true;
            m->tx = p->x; m->ty = p->y;
            if (lv->f[m->y][m->x] & F_VIS) game_msg(g, C_WARN, "the %s wakes up!", mi->name);
            vocal(g, m);
        }
        return;
    }
    if (sees) {
        if (!m->hunting) vocal(g, m);           /* it has spotted you */
        m->hunting = true;
        m->tx = p->x; m->ty = p->y;
    } else if (shade && m->hunting && dist <= 7 && rng_chance(&g->rng, 6)) {
        vocal(g, m);                            /* whispering in the dark */
    }
    /* a slinger with a clear view of you in the light keeps its distance and shoots */
    if (m->kind == MK_SLINGER && sees && lit && dist >= 2 && dist <= 7 && clear_shot(g, m)) {
        if (rng_chance(&g->rng, 65)) sling(g, m);
        return;                                 /* or fits another stone */
    }
    if (dist == 1 && diag_ok(lv, m->x, m->y, p->x - m->x, p->y - m->y) && (sees || m->hunting)
        && !(m->kind == MK_SHADE && game_firelight(g, p->x, p->y) > SHADE_FEAR)) {
        if (bat && rng_chance(&g->rng, 35)) mob_wander(g, m);     /* flits off instead */
        else attack(g, m, p);
        return;
    }
    if (m->hunting) {
        if (bat && rng_chance(&g->rng, 40)) mob_wander(g, m);
        else mob_step_toward(g, m, m->tx, m->ty);
        if (m->x == m->tx && m->y == m->ty && !sees) m->hunting = false;
    } else if (rng_chance(&g->rng, bat ? 70 : 35)) {
        mob_wander(g, m);
    }
}

static void monsters_act(Game *g)
{
    update_light(g);
    Level *lv = game_level(g);
    Mob *p = &g->player;
    int gnaw = 0;
    /* the player stands out when a lamp or torch is on them; their own glow is dim */
    float pb = level_brightness(lv, p->x, p->y) + (g->torch_lit ? 1.0f : 0.0f);
    Mob *ms = g->mobs[g->depth];
    for (int i = 0; i < MAX_MOBS && g->state != GS_DEAD; i++) {
        Mob *m = &ms[i];
        if (!m->alive) continue;
        if (m->daze > 0) {                                                  /* reeling from a flash */
            m->daze--;
            if (shadeish(m->kind)) shade_burns(g, m);
            continue;
        }
        if (m->kind == MK_OGRE && (m->rest = !m->rest)) continue;          /* slow: every other turn */
        mob_turn(g, m, pb, &gnaw);
        if (m->kind == MK_BAT && m->alive && !m->asleep && !m->daze && g->state != GS_DEAD)
            mob_turn(g, m, pb, &gnaw);                                    /* quick: twice a turn */
    }
    if (gnaw && g->torch_lit && g->state != GS_DEAD) {
        if (gnaw > 3) gnaw = 3;
        g->torch_fuel -= gnaw;
        if (g->torch_fuel < 1) g->torch_fuel = 1;
        if (!g->gnaw_told) {
            g->gnaw_told = true;
            game_msg(g, C_WARN, "your torch gutters: something in the dark is feeding on its flame.");
        }
    }
}

static void burn_floor_torches(Game *g, int d)
{
    Level *lv = g->lv[d];
    if (!lv) return;
    for (int i = lv->nlights - 1; i >= 0; i--) {
        Light *l = &lv->lights[i];
        if (l->kind == L_OILFIRE) {
            if (--l->fuel <= 0) {
                level_remove_light(lv, i);
                if (d == g->depth + 1) g->lower_dirty = true;
            }
            continue;
        }
        if (l->kind != L_FLOORTORCH || l->fuel < 0) continue;
        if (--l->fuel > 0) continue;
        if (d == g->depth && (lv->f[l->y][l->x] & F_VIS))
            game_msg(g, C_DIM, "a torch on the floor gutters and goes out, leaving a charred stub.");
        lv->scorch[l->y][l->x] = 1;
        if (d == g->depth) sfx_ex(g, SND_SNUFF, l->x, l->y, false, 0, 0.7f);
        level_remove_light(lv, i);
        if (d == g->depth + 1) g->lower_dirty = true;
    }
}

/* burning oil on a level dies down a turn; where it burned out on stone, soot is left */
static void burn_tick(Game *g, int d)
{
    Level *lv = g->lv[d];
    if (!lv) return;
    bool out = false;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            if (!lv->burn[y][x] || --lv->burn[y][x]) continue;
            out = true;
            if (!tile_water((Tile)lv->t[y][x]) && !lv->scorch[y][x]) lv->scorch[y][x] = 2;
        }
    if (out && d == g->depth + 1) g->lower_dirty = true;
}

/* burning oil sears whatever stands in it (shades have the firelight to fear instead) */
static void fire_harm(Game *g)
{
    Level *lv = game_level(g);
    Mob *ms = g->mobs[g->depth];
    char t[16];
    for (int i = 0; i < MAX_MOBS; i++) {
        Mob *m = &ms[i];
        if (!m->alive || shadeish(m->kind) || game_mob_hidden(m) || !lv->burn[m->y][m->x]) continue;
        int dmg = rng_range(&g->rng, 2, 4);
        bool seen = lv->f[m->y][m->x] & F_VIS;
        m->hp -= dmg;
        m->flash = 1;
        m->asleep = false;
        snprintf(t, sizeof t, "%d", dmg);
        floater(g, m->x, m->y, 0xffa040, t);
        push_fx(g, FX_BURN, m->x, m->y);
        if (m->hp <= 0) {
            kill_mob(g, m);
            sfx(g, SND_KILL, m->x, m->y);
            if (seen) game_msg(g, C_GOOD, "the %s burns to death in the oil!", MOB_INFO[m->kind].name);
        } else if (seen) {
            game_msg(g, C_TEXT, "the %s burns!", MOB_INFO[m->kind].name);
            vocal(g, m);
        }
    }
    Mob *p = &g->player;
    if (g->state == GS_DEAD || !lv->burn[p->y][p->x]) return;
    int dmg = rng_range(&g->rng, 2, 4);
    p->hp -= dmg;
    p->flash = 1;
    g->shake = 0.25f;
    snprintf(t, sizeof t, "%d", dmg);
    floater(g, p->x, p->y, 0xff5a4a, t);
    push_fx(g, FX_BURN, p->x, p->y);
    sfx_ex(g, SND_HURT, p->x, p->y, false, 0.05f, 1);
    game_stop(g);
    if (p->hp <= 0) {
        game_msg(g, C_BAD, "you burn to death in the oil...");
        g->state = GS_DEAD;
        g->end_t = g->time;
        sfx_ex(g, SND_DIE, p->x, p->y, true, 0.25f, 1);
    } else {
        game_msg(g, C_BAD, "the burning oil sears you!");
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
            sfx(g, SND_SNUFF, g->player.x, g->player.y);
            game_stop(g);
        }
    }
    burn_floor_torches(g, g->depth);
    burn_floor_torches(g, g->depth + 1);
    monsters_act(g);
    if (g->state != GS_DEAD) fire_harm(g);
    burn_tick(g, g->depth);
    burn_tick(g, g->depth + 1);
    if (g->warm_left > 0 && --g->warm_left == 0) game_msg(g, C_WARN, "the warming draught's glow fades; the cold can find you again.");
    else if (g->warm_left == 10) game_msg(g, C_DIM, "the warmth of the draught is fading.");
    Mob *p = &g->player;
    if (g->chill > 0 && g->state != GS_DEAD) {
        /* a fixed fire warms you quickly, a torch in hand slowly, the dark not at all */
        int need = game_by_fire(g) ? 2 : g->torch_lit ? 12 : 0;
        if (need && ++g->warm_t >= need) {
            g->warm_t = 0;
            g->chill--;
            if (!g->chill) game_msg(g, C_GOOD, "the last of the chill leaves your bones.");
        }
    }
    if (++g->regen >= (game_by_fire(g) ? 5 : 9)) {
        g->regen = 0;
        if (p->hp < game_max_hp(g) && g->state != GS_DEAD) p->hp++;
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
static void nearest_floor(Level *lv, int *x, int *y);

/* ------------------------------------------------------- the Heartflame */

/* the hearth's light follows the kindling: a glow, a flame, then the Heartflame itself */
static void hearth_light(Game *g)
{
    Level *lv = game_level(g);
    Light *l = game_final(g) ? level_light_on(lv, lv->downx, lv->downy) : NULL;
    if (!l || l->kind != L_HEART) return;
    static const float R[4] = { 0, 3.5f, 6.0f, 9.0f }, K[4] = { 0, 0.7f, 0.9f, 1.0f };
    int k = g->kindle < 0 ? 0 : g->kindle > 3 ? 3 : g->kindle;
    l->lit = k > 0;
    l->radius = R[k];
    l->r = 1.50f * K[k];
    l->g = 0.95f * K[k];
    l->b = 0.46f * K[k];
    lv->light_dirty = true;
}

/* the Heartflame burns again: light floods the last level and the dark comes apart */
static void heartflame_lit(Game *g)
{
    Level *lv = game_level(g);
    g->kindle = 3;
    hearth_light(g);
    int lit = 0;
    for (int i = 0; i < lv->nlights; i++)
        if (lv->lights[i].kind == L_SCONCE && !lv->lights[i].lit) { lv->lights[i].lit = true; lit++; }
    for (int r = 0; r < lv->nrooms; r++) lv->rooms[r].dark = false;
    lv->light_dirty = true;
    int gone = 0;
    bool hollow = false;
    Mob *ms = g->mobs[g->depth];
    for (int i = 0; i < MAX_MOBS; i++) {
        Mob *m = &ms[i];
        if (!m->alive || !shadeish(m->kind)) continue;
        if (m->kind == MK_HOLLOW) hollow = true;
        m->alive = false;
        push_fx(g, FX_DISSOLVE, m->x, m->y);
        gone++;
    }
    g->chill = 0;
    g->shake = 0.8f;
    g->state = GS_WON;
    g->end_t = g->time;
    game_stop(g);
    sfx_ex(g, SND_IGNITE, 0, 0, true, 0, 1.2f);
    sfx_ex(g, SND_SCONCE, 0, 0, true, 0.15f, 1.2f);
    if (gone) sfx_ex(g, SND_DISSOLVE, 0, 0, true, 0.4f, 1.2f);
    game_msg(g, C_WARN, "the Heartflame roars up out of the stone, gold and then white!");
    if (hollow) game_msg(g, C_GOOD, "the Hollow shrieks once and comes apart, and every shade with it, like mist in the sun.");
    else if (gone) game_msg(g, C_GOOD, "the shades come apart like mist in the sun.");
    if (lit) game_msg(g, C_GOOD, "one by one the cold sconces of the deep catch, and light runs up the walls.");
    game_msg(g, C_GOOD, "you have rekindled the Heartflame. The deep is warm again.");
    compute_view(g);
}

/* walking into the hearth with fire in hand: three turns to bring the Heartflame back */
static bool kindle(Game *g)
{
    if (g->kindle >= 3) return false;
    if (!g->torch_lit) {
        game_msg(g, C_WARN, g->kindle ? "the young flame needs feeding: light a torch and hold it to the hearth again."
                                      : "the hearth is cold and dead. It needs fire: light a torch and bring it here.");
        return false;
    }
    Level *lv = game_level(g);
    Mob *p = &g->player;
    p->bump = 1.0f;
    p->bdx = lv->downx - p->x;
    p->bdy = lv->downy - p->y;
    if (p->bdx) p->flip = p->bdx < 0;
    g->kindle++;
    if (g->kindle >= 3) { heartflame_lit(g); return true; }
    hearth_light(g);
    sfx(g, SND_IGNITE, lv->downx, lv->downy);
    push_fx(g, FX_SPARKS, lv->downx, lv->downy);
    if (g->kindle == 1)
        game_msg(g, C_WARN, "you thrust your torch into the hearth. Deep in the cold stone something catches, and glows.");
    else
        game_msg(g, C_WARN, "a flame climbs out of the stone, small and blue, then gold. Once more!");
    /* everything of the dark on the level feels it, and comes */
    Mob *ms = g->mobs[g->depth];
    for (int i = 0; i < MAX_MOBS; i++)
        if (ms[i].alive && shadeish(ms[i].kind)) { ms[i].hunting = true; ms[i].tx = p->x; ms[i].ty = p->y; }
    if (g->kindle == 1) game_msg(g, C_BAD, "a cold wind rises in the hall. The dark has felt the flame.");
    flare(g);
    return true;
}

/* a level reached for the first time, perhaps the last one */
static void arrive(Game *g)
{
    if (g->depth <= g->deepest) return;
    g->deepest = g->depth;
    if (game_final(g)) {
        game_msg(g, C_WARN, "the cold here bites to the bone. This is the bottom of the world: somewhere on this level "
                 "lies the Heartflame's hearth.");
        game_msg(g, C_DIM, "light a torch and walk into the hearth to kindle it. It takes three turns.");
    }
}

/* the mace's knockback: a step back, a wall, deep water or a fall */
static void knock_back(Game *g, Mob *m, int dx, int dy)
{
    Level *lv = game_level(g);
    int nx = m->x + dx, ny = m->y + dy;
    Tile t = tile_at(lv, nx, ny);
    const char *nm = MOB_INFO[m->kind].name;
    bool seen = lv->f[m->y][m->x] & F_VIS;
    if (m->kind == MK_EEL) {
        /* it only ever goes deeper into its pool */
        if (t == T_DEEP && mob_free(g, nx, ny)) mob_go(g, m, nx, ny);
        return;
    }
    if (t == T_CHASM && m->kind == MK_BAT) {
        if (!game_mob_at(g, nx, ny)) {
            mob_go(g, m, nx, ny);
            if (seen) game_msg(g, C_TEXT, "the bat tumbles out over the chasm, and rights itself.");
        }
        return;
    }
    if (t == T_CHASM) {
        /* over the edge: it lands, hurt, on the level below */
        int d = g->depth + 1, bx = nx, by = ny;
        if (seen) game_msg(g, C_GOOD, "the blow knocks the %s over the edge of the chasm!", nm);
        push_fx(g, FX_FALL, nx, ny);
        sfx_ex(g, SND_FALL, nx, ny, false, 0.3f, 0.6f);
        m->alive = false;
        if (g->lv[d]) {
            nearest_floor(g->lv[d], &bx, &by);
            Mob *ms = g->mobs[d];
            for (int i = 0; i < MAX_MOBS; i++)
                if (!ms[i].alive) {
                    Mob keep = *m;
                    mob_init(&ms[i], keep.kind, bx, by);
                    ms[i].lvl = keep.lvl;
                    ms[i].maxhp = keep.maxhp;
                    ms[i].hp = keep.hp - rng_range(&g->rng, 3, 6);
                    ms[i].alive = ms[i].hp > 0;
                    break;
                }
            g->lower_dirty = true;
        }
        return;
    }
    bool salt_wall = m->kind == MK_SHADE && in_map(nx, ny) && lv->salt[ny][nx];   /* it can't cross, even flung */
    if (tile_solid(t) || t == T_DOOR || t == T_BRAZIER || t == T_HEARTH || !in_map(nx, ny) || salt_wall) {
        int dmg = rng_range(&g->rng, 1, 3);
        m->hp -= dmg;
        m->flash = 1;
        char tx[16];
        snprintf(tx, sizeof tx, "%d", dmg);
        floater(g, m->x, m->y, 0xffd27a, tx);
        sfx_ex(g, SND_THUD, m->x, m->y, false, 0.1f, 0.8f);
        if (m->hp <= 0) {
            kill_mob(g, m);
            if (seen) game_msg(g, C_GOOD, "the blow smashes the %s against the %s!", nm, salt_wall ? "salt" : t == T_DOOR ? "door" : "wall");
            if (shadeish(m->kind)) push_fx(g, FX_DISSOLVE, m->x, m->y);
        } else if (seen) {
            if (salt_wall) game_msg(g, C_TEXT, "the %s is flung against the salt and recoils.", nm);
            else game_msg(g, C_TEXT, "the %s slams into the %s.", nm, t == T_DOOR ? "door" : "wall");
        }
        return;
    }
    if (!mob_free(g, nx, ny)) return;
    if (m->kind == MK_SHADE && game_firelight(g, nx, ny) > SHADE_FEAR) {
        /* knocked into the fire it fears */
        mob_go(g, m, nx, ny);
        return;
    }
    mob_go(g, m, nx, ny);
    if (seen) game_msg(g, C_TEXT, t == T_DEEP ? "the %s is knocked into the deep water." : "the %s staggers back.", nm);
}

/* you attack the monster at m with whatever you wield */
static void player_strike(Game *g, Mob *m)
{
    Mob *p = &g->player;
    int dx = m->x - p->x, dy = m->y - p->y;
    Level *lv = game_level(g);
    switch (g->weapon.kind) {
    case IT_AXE: {
        /* everything around you, the one you swung at first */
        Mob *around[8];
        int n = 0;
        around[n++] = m;
        for (int d = 0; d < 8; d++) {
            Mob *o = game_mob_at(g, p->x + DX8[d], p->y + DY8[d]);
            if (o && o != m && diag_ok(lv, p->x, p->y, DX8[d], DY8[d]) && game_mob_noticed(g, o)) around[n++] = o;
        }
        for (int i = 0; i < n; i++)
            if (around[i]->alive) attack(g, p, around[i]);
        if (n > 1) sfx_ex(g, SND_SWING, p->x, p->y, false, 0.1f, 0.8f);
        break;
    }
    case IT_SPEAR: {
        int bx = m->x + dx, by = m->y + dy;
        attack(g, p, m);
        Mob *b = game_mob_at(g, bx, by);
        if (b && b->alive && !tile_opaque(tile_at(lv, m->x, m->y))) attack(g, p, b);
        break;
    }
    case IT_MACE:
        if (attack(g, p, m) && m->alive) {
            if (m->kind == MK_OGRE || m->kind == MK_HOLLOW) {
                if (lv->f[m->y][m->x] & F_VIS)
                    game_msg(g, C_DIM, m->kind == MK_OGRE ? "the ogre barely rocks back." : "the Hollow only billows back, and closes again.");
            } else {
                knock_back(g, m, dx, dy);
            }
        }
        break;
    default:
        attack(g, p, m);
        break;
    }
}

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
        player_strike(g, m);
        end_turn(g);
        return true;
    }
    if (t == T_BRAZIER) {
        g->forge_req = true;
        game_stop(g);
        return false;
    }
    if (t == T_HEARTH) {
        game_stop(g);
        if (!kindle(g)) return false;
        if (g->state == GS_PLAY) end_turn(g);
        return true;
    }
    if (tile_solid(t)) {
        Light *l = level_light_on(lv, nx, ny);
        if (l && !l->lit && l->kind == L_SCONCE && l->x == p->x && l->y == p->y) {
            l->lit = true;
            lv->light_dirty = true;
            if (l->room >= 0) lv->rooms[l->room].dark = false;
            game_msg(g, C_WARN, "you light the sconce. Warm light spills across the room.");
            g->sconces_lit++;
            ember(g, 1, "you take an ember from the new flame");
            flare(g);
            sfx(g, SND_SCONCE, l->fx, l->fy);
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
        sfx(g, SND_DOOR, nx, ny);
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
    Tile here = (Tile)lv->t[p->y][p->x];
    bool mail = g->armour.kind == IT_MAIL;
    if (t == T_DEEP && here != T_DEEP && (g->torch_lit || mail) && (g->wade_x != nx || g->wade_y != ny)) {
        g->wade_x = nx;
        g->wade_y = ny;
        game_stop(g);
        game_msg(g, C_WARN, g->torch_lit && mail ? "that water is deep: it will put out your torch, and your mail will drag you under. Move again to swim."
                            : mail ? "that water is deep, and your mail will drag you under. Move again to swim."
                            : "that water is deep: swimming will put out your torch. Move again to swim.");
        return false;
    }
    g->wade_x = g->wade_y = -1;
    if (lv->burn[ny][nx] && !lv->burn[p->y][p->x] && (g->fire_x != nx || g->fire_y != ny)) {
        g->fire_x = nx;
        g->fire_y = ny;
        game_stop(g);
        game_msg(g, C_WARN, "that oil is burning, and it will sear you. Move again to walk through the flames.");
        return false;
    }
    g->fire_x = g->fire_y = -1;
    start_tween(p, nx, ny);
    if (lv->salt[ny][nx]) {
        lv->salt[ny][nx] = 0;
        game_msg(g, C_DIM, "you scuff through the salt, breaking the line there.");
    }
    sfx(g, t == T_DEEP ? SND_SWIM : t == T_SHALLOW ? SND_WADE : SND_STEP, nx, ny);
    if (tile_water(t)) {
        push_fx(g, FX_SPLASH, nx, ny);
        if (t == T_DEEP) {
            if (g->torch_lit) {
                g->torch_lit = false;
                g->torch_fuel = 0;
                push_fx(g, FX_STEAM, nx, ny);
                sfx(g, SND_HISS, nx, ny);
                game_msg(g, C_WARN, "your torch hisses out as you swim. It's soaked through.");
            } else if (here != T_DEEP && !mail) {
                game_msg(g, C_DIM, "you swim out into the deep water.");
            }
            if (mail) {
                int dmg = rng_range(&g->rng, 2, 3);
                p->hp -= dmg;
                p->flash = 1;
                char tx[16];
                snprintf(tx, sizeof tx, "%d", dmg);
                floater(g, nx, ny, 0xff5a4a, tx);
                if (p->hp <= 0) {
                    game_msg(g, C_BAD, "your mail drags you down into the dark water, and you do not come up.");
                    g->state = GS_DEAD;
                    g->end_t = g->time;
                    sfx_ex(g, SND_DIE, nx, ny, true, 0.25f, 1);
                    return true;
                }
                game_msg(g, C_BAD, "your mail drags you under; you choke on cold water.");
            }
            make_noise(g, nx, ny, 3);
        } else if (rng_chance(&g->rng, 50)) {
            make_noise(g, nx, ny, 4);
        }
    } else if (mail && rng_chance(&g->rng, 60)) {
        make_noise(g, nx, ny, 3);       /* the rings jingle */
    }
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
    sfx_ex(g, SND_STAIRS, 0, 0, true, 0, 1);
    arrive(g);
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
    sfx_ex(g, SND_STAIRS, 0, 0, true, 0, 0.8f);
    return true;
}

void game_jump_answer(Game *g, bool yes)
{
    if (g->state != GS_JUMP_PROMPT) return;
    g->state = GS_PLAY;
    if (!yes) return;
    int dmg = 3 + rng_range(&g->rng, 0, 3) + g->depth / 3;
    game_msg(g, C_WARN, "you leap into the chasm and plunge into the darkness below!");
    sfx_ex(g, SND_FALL, 0, 0, true, 0, 1);
    change_level(g, g->depth + 1, g->jx, g->jy);
    Mob *p = &g->player;
    bool wet = tile_water(tile_at(game_level(g), p->x, p->y));
    sfx_ex(g, wet ? SND_SPLASH : SND_LAND, 0, 0, true, 0.75f, 1);
    if (wet) {
        push_fx(g, FX_SPLASH, p->x, p->y);
        if (game_level(g)->t[p->y][p->x] == T_DEEP) dmg = dmg / 3 + 1;
        else dmg = dmg * 2 / 3 + 1;
        if (g->torch_lit) {
            g->torch_lit = false;
            g->torch_fuel = 0;
            game_msg(g, C_WARN, "you plunge into the water below and your torch is drowned.");
        } else {
            game_msg(g, C_INFO, "you plunge into the water below, which breaks your fall.");
        }
    }
    p->hp -= dmg;
    p->flash = 1.0f;
    g->shake = 0.6f;
    /* the landing is hard on what you carry: tempered gear may lose an edge */
    if ((g->weapon.plus > 0 || g->armour.plus > 0) && rng_chance(&g->rng, wet ? 25 : 60)) {
        Slot *s = g->weapon.plus > 0 && (g->armour.plus == 0 || rng_chance(&g->rng, 50)) ? &g->weapon : &g->armour;
        s->plus--;
        game_msg(g, C_WARN, "the fall %s your %s (now +%d).", s == &g->weapon ? "blunts" : "buckles", ITEM_INFO[s->kind].name, s->plus);
    }
    char t[16];
    snprintf(t, sizeof t, "%d", dmg);
    floater(g, p->x, p->y, 0xff5a4a, t);
    if (p->hp <= 0) {
        game_msg(g, C_BAD, "you hit the ground at depth %d and do not get up.", g->depth);
        g->state = GS_DEAD;
        g->end_t = g->time;
    } else {
        game_msg(g, C_BAD, wet ? "you surface, gasping, on depth %d (%d damage)." : "you land hard on depth %d (%d damage).", g->depth, dmg);
        arrive(g);
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
            if ((lv->t[Y][X] == T_CHASM || lv->t[Y][X] == T_DEEP || lv->burn[Y][X]) && !(X == gx && Y == gy)) continue;
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
    if (g->player.hp >= game_max_hp(g) && !(g->chill && (game_by_fire(g) || g->torch_lit))) {
        game_msg(g, C_DIM, g->chill ? "you are as well as you'll get without a fire to warm you." : "you are already at full health.");
        return;
    }
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
        game_msg(g, C_DIM, game_final(g) ? "you haven't found the Heartflame's hearth yet." : "you haven't found the way down yet.");
        return;
    }
    if (game_final(g) && cheb(g->player.x, g->player.y, lv->downx, lv->downy) == 1) {
        game_move(g, lv->downx - g->player.x, lv->downy - g->player.y);    /* right there: kindle it */
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
        if ((p->hp >= game_max_hp(g) && !(g->chill && (game_by_fire(g) || g->torch_lit))) || ++g->pi > 200) {
            if (p->hp >= game_max_hp(g)) game_msg(g, C_GOOD, "you feel rested.");
            game_stop(g);
            return;
        }
        game_rest(g);
        return;
    case AUTO_EXPLORE: {
        int n = find_path(g, -1, -1, path, MAX_PATH);
        if (!n) {
            game_msg(g, C_INFO, game_final(g) ? "you have explored everything you can reach. Press > to go to the hearth."
                                              : "you have explored everything you can reach. Press > to head for the stairs.");
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
    if (k == MK_EEL && game_level(g)->t[y][x] != T_DEEP) return false;
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

/* developer: stand at the edge of a pool (in it, with `wade`) */
bool game_debug_to_water(Game *g, int wade)
{
    Level *lv = game_level(g);
    for (int y = 1; y < MAP_H - 3; y++)
        for (int x = 1; x < MAP_W - 1; x++) {
            if (lv->t[y][x] != T_DEEP) continue;
            int py = y;
            while (py < MAP_H - 1 && tile_water((Tile)lv->t[py][x])) py++;
            if (!tile_walkable((Tile)lv->t[py][x])) continue;
            if (wade == 1) py--;            /* in the shallows */
            else if (wade == 2) py = y;     /* swimming */
            Mob *ms = g->mobs[g->depth];
            for (int i = 0; i < MAX_MOBS; i++)
                if (ms[i].alive && ms[i].x == x && ms[i].y == py) ms[i].alive = false;
            place_player(g, x, py);
            for (int j = 0; j < MAP_H; j++)
                for (int i = 0; i < MAP_W; i++) lv->f[j][i] |= F_SEEN;
            compute_view(g);
            return true;
        }
    return false;
}

static bool place_item(Game *g, int depth, ItemKind k, int count, int x, int y, bool dropped, int plus);
/* developer: 0 stand in an east-west corridor with a rat and a dart beside you,
 * 1 stand beside a door in a side (north-south) wall, 2 stand just north of a room's south wall */
bool game_debug_to_spot(Game *g, int kind)
{
    Level *lv = game_level(g);
    if (kind == 5) {
        /* in the Heartflame's hall, a few steps from the hearth, the hall revealed */
        if (!game_final(g) || lv->hall < 0) return false;
        Room *hl = &lv->rooms[lv->hall];
        int bx = -1, by = -1;
        for (int j = 2; j <= 4 && bx < 0; j++)
            for (int i = -1; i <= 1; i++) {
                int x = lv->downx + i * 2, y = lv->downy + j;
                if (in_map(x, y) && tile_walkable((Tile)lv->t[y][x]) && mob_free(g, x, y)) { bx = x; by = y; break; }
            }
        if (bx < 0) return false;
        place_player(g, bx, by);
        for (int y = hl->y - 1; y <= hl->y + hl->h; y++)
            for (int x = hl->x - 1; x <= hl->x + hl->w; x++)
                if (in_map(x, y)) lv->f[y][x] |= F_SEEN;
        compute_view(g);
        return true;
    }
    if (kind == 3 || kind == 4) {
        /* in front of a cold sconce, a shade two steps away; 4 also lights the sconce */
        for (int i = 0; i < lv->nlights; i++) {
            Light *l = &lv->lights[i];
            if (l->kind != L_SCONCE || l->lit) continue;
            int px = l->x, py = l->y;
            if (!tile_walkable((Tile)lv->t[py + 2][px]) || !tile_walkable((Tile)lv->t[py + 1][px])) continue;
            Mob *ms = g->mobs[g->depth];
            for (int j = 0; j < MAX_MOBS; j++)
                if (ms[j].alive && abs(ms[j].x - px) < 6 && abs(ms[j].y - py) < 6) ms[j].alive = false;
            place_player(g, px, py);
            for (int j = 0; j < MAX_MOBS; j++)
                if (!ms[j].alive) { mob_init(&ms[j], MK_SHADE, px, py + 2); ms[j].hunting = true; ms[j].tx = px; ms[j].ty = py; break; }
            for (int y = 0; y < MAP_H; y++)
                for (int x = 0; x < MAP_W; x++) lv->f[y][x] |= F_SEEN;
            compute_view(g);
            if (kind == 4) game_move(g, 0, -1);
            return true;
        }
        return false;
    }
    for (int y = 2; y < MAP_H - 3; y++)
        for (int x = 3; x < MAP_W - 3; x++) {
            Tile t = (Tile)lv->t[y][x];
            int px = x, py = y;
            if (kind == 0) {
                if (t != T_FLOOR || !tile_solid((Tile)lv->t[y - 1][x]) || !tile_solid((Tile)lv->t[y + 1][x])) continue;
                bool run = true;
                for (int k = -2; k <= 2 && run; k++)
                    run = lv->t[y][x + k] == T_FLOOR && tile_solid((Tile)lv->t[y + 1][x + k]);
                if (!run) continue;
            } else if (kind == 1) {
                if (t != T_DOOR || !tile_solid((Tile)lv->t[y - 1][x]) || !tile_solid((Tile)lv->t[y + 1][x])) continue;
                if (!tile_walkable((Tile)lv->t[y][x - 1])) continue;
                px = x - 1;
            } else {
                if (t != T_FLOOR || !tile_solid((Tile)lv->t[y + 1][x]) || !tile_walkable((Tile)lv->t[y - 1][x])) continue;
                if (lv->t[y][x - 1] != T_FLOOR || lv->t[y][x + 1] != T_FLOOR || !tile_solid((Tile)lv->t[y + 1][x + 2]))
                    continue;
            }
            Mob *ms = g->mobs[g->depth];
            for (int i = 0; i < MAX_MOBS; i++)
                if (ms[i].alive && abs(ms[i].x - px) < 4 && abs(ms[i].y - py) < 4) ms[i].alive = false;
            place_player(g, px, py);
            if (kind != 1) {
                place_item(g, g->depth, IT_DART, 2, px - 1, py, true, 0);
                for (int i = 0; i < MAX_MOBS; i++)
                    if (!ms[i].alive) { mob_init(&ms[i], MK_RAT, px + 2, py); break; }
            }
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

const char *game_item_name(ItemKind k, int count, int plus, char *buf, size_t n)
{
    const ItemInfo *ii = &ITEM_INFO[k];
    if (plus > 0) snprintf(buf, n, "a +%d %s", plus, ii->name);
    else if (count == 1) snprintf(buf, n, "%s %s", strchr("aeiou", ii->name[0]) ? "an" : "a", ii->name);
    else snprintf(buf, n, "%d %s", count, ii->plural);
    return buf;
}

static void push_fx(Game *g, FxKind k, int x, int y)
{
    if (g->nfx < MAX_FX) g->fx[g->nfx++] = (Fx){ k, x, y, x, y };
}

static void push_fx_from(Game *g, FxKind k, int sx, int sy, int x, int y)
{
    if (g->nfx < MAX_FX) g->fx[g->nfx++] = (Fx){ k, x, y, sx, sy };
}

static bool add_item(Game *g, ItemKind k, int count, int plus)
{
    if (stacks(k))
        for (int i = 0; i < g->ninv; i++)
            if (g->inv[i].kind == k) { g->inv[i].count += count; return true; }
    if (g->ninv >= MAX_INV) return false;
    g->inv[g->ninv++] = (Slot){ k, count, plus };
    return true;
}

static void take_one(Game *g, int slot)
{
    if (--g->inv[slot].count > 0) return;
    for (int i = slot; i < g->ninv - 1; i++) g->inv[i] = g->inv[i + 1];
    g->ninv--;
}

static bool place_item(Game *g, int depth, ItemKind k, int count, int x, int y, bool dropped, int plus)
{
    if (g->lv[depth] && tile_at(g->lv[depth], x, y) == T_DEEP) return true;     /* sinks out of reach */
    FloorItem *it = g->items[depth];
    if (stacks(k))
        for (int i = 0; i < MAX_ITEMS; i++)
            if (it[i].alive && it[i].x == x && it[i].y == y && it[i].kind == k) { it[i].count += count; return true; }
    for (int i = 0; i < MAX_ITEMS; i++)
        if (!it[i].alive) { it[i] = (FloorItem){ true, k, count, x, y, dropped, plus }; return true; }
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
        sfx(g, SND_PICKUP, p->x, p->y);
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
    if (!add_item(g, it->kind, it->count, it->plus)) {
        game_msg(g, C_DIM, "your pack is full; you leave %s.", game_item_name(it->kind, it->count, it->plus, nm, sizeof nm));
        return;
    }
    int slot = 0;
    for (int i = 0; i < g->ninv; i++) if (g->inv[i].kind == it->kind) slot = i;
    game_msg(g, C_GOOD, "you pick up %s (%c).", game_item_name(it->kind, it->count, it->plus, nm, sizeof nm), 'a' + slot);
    sfx(g, SND_PICKUP, p->x, p->y);
    it->alive = false;
}

/* a burning torch now lies on (x, y) of `depth`; false if water put it out */
static bool floor_torch(Game *g, int depth, int x, int y)
{
    Level *lv = g->lv[depth];
    if (!lv) return false;
    if (tile_water(tile_at(lv, x, y))) {
        if (depth == g->depth) push_fx(g, FX_STEAM, x, y);
        return false;
    }
    for (int i = 0; i < lv->nlights; i++) {
        Light *l = &lv->lights[i];
        if (l->kind == L_FLOORTORCH && l->x == x && l->y == y) { l->fuel = FLOOR_TORCH_FUEL; return true; }
    }
    int i = level_add_light(lv, L_FLOORTORCH, x, y, x, y, true, lv->room[y][x]);
    if (i >= 0) lv->lights[i].fuel = FLOOR_TORCH_FUEL;
    level_relight(lv);
    if (depth == g->depth) flare(g);
    return true;
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
        Mob *hm = game_mob_at(g, x, y);
        if (hm && !game_mob_hidden(hm)) { *hits = true; break; }
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
    sfx(g, SND_THROW, p->x, p->y);
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

/* An oil flask bursts at (x, y) of `depth`: burning oil on it and most of the
 * ground around it (on water too; not past a wall's corner, not down a chasm). */
static void ignite_oil(Game *g, int depth, int x, int y)
{
    Level *lv = g->lv[depth];
    if (!lv) return;
    int centre = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int X = x + dx, Y = y + dy;
            Tile t = tile_at(lv, X, Y);
            if (!tile_walkable(t) || t == T_DOOR) continue;
            if ((dx || dy) && (!diag_ok(lv, x, y, dx, dy) || rng_chance(&g->rng, 25))) continue;   /* a ragged splash */
            int turns = dx || dy ? rng_range(&g->rng, 9, 13) : rng_range(&g->rng, 13, 16);
            if (!dx && !dy) centre = turns;
            if (lv->burn[Y][X] < turns) lv->burn[Y][X] = (uint8_t)turns;
        }
    if (!centre) return;
    Light *l = NULL;
    for (int i = 0; i < lv->nlights; i++)
        if (lv->lights[i].kind == L_OILFIRE && lv->lights[i].x == x && lv->lights[i].y == y) l = &lv->lights[i];
    if (!l) {
        int i = level_add_light(lv, L_OILFIRE, x, y, x, y, true, lv->room[y][x]);
        if (i >= 0) l = &lv->lights[i];
    }
    if (l && l->fuel < centre) l->fuel = centre;
    level_relight(lv);
    if (depth == g->depth) flare(g);
}

static void flash_mark(void *ctx, int x, int y, float d)
{
    (void)d;
    ((Level *)ctx)->f[y][x] |= F_SEEN;
}

/* flash powder goes off at (x, y): everything that sees it reels; shades scorch */
static void flash_burst(Game *g, int x, int y)
{
    Level *lv = game_level(g);
    push_fx(g, FX_FLASH, x, y);
    sfx_ex(g, SND_IGNITE, x, y, false, 0, 1.0f);
    sfx_ex(g, SND_THUD, x, y, false, 0.02f, 1.0f);
    g->shake = 0.25f;
    make_noise(g, x, y, 8);                     /* the bang carries */
    fov_cast(lv, x, y, 6, flash_mark, lv);      /* for an instant you see it all */
    int dazed = 0, burnt = 0;
    Mob *ms = g->mobs[g->depth];
    char t[16];
    for (int i = 0; i < MAX_MOBS; i++) {
        Mob *m = &ms[i];
        if (!m->alive || game_mob_hidden(m) || cheb(m->x, m->y, x, y) > 5 || !los(lv, x, y, m->x, m->y)) continue;
        if (shadeish(m->kind)) {
            int dmg = rng_range(&g->rng, 4, 8);
            m->hp -= dmg;
            m->flash = 1;
            m->hunting = false;
            snprintf(t, sizeof t, "%d", dmg);
            floater(g, m->x, m->y, 0xfff0c0, t);
            push_fx(g, FX_BURN, m->x, m->y);
            burnt++;
            if (m->hp <= 0) {
                push_fx(g, FX_DISSOLVE, m->x, m->y);
                sfx(g, SND_DISSOLVE, m->x, m->y);
                kill_mob(g, m);
            } else {
                sfx(g, SND_BURN, m->x, m->y);
                if (m->kind == MK_HOLLOW) {
                    m->daze = 2;        /* too vast to burn away in an instant: it reels instead, briefly */
                    dazed++;
                }
            }
            continue;
        }
        m->asleep = false;
        m->hunting = false;             /* it has lost sight of everything */
        m->daze = m->kind == MK_OGRE ? 2 : rng_range(&g->rng, 3, 4);
        floater(g, m->x, m->y, 0xfff0c0, "dazed");
        dazed++;
    }
    if (dazed && burnt) game_msg(g, C_GOOD, "a blinding white flash! %d creature%s reel%s, dazzled, and the light scorches the shades.",
                                 dazed, dazed == 1 ? "" : "s", dazed == 1 ? "s" : "");
    else if (dazed) game_msg(g, C_GOOD, "a blinding white flash! %d creature%s reel%s, dazzled.", dazed, dazed == 1 ? "" : "s",
                             dazed == 1 ? "s" : "");
    else if (burnt) game_msg(g, C_GOOD, "a blinding white flash scorches the dark, and the shades writhe in it!");
    else game_msg(g, C_TEXT, "a blinding white flash lights up every corner, just for an instant.");
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
        if (m && shadeish(m->kind)) {
            game_msg(g, C_DIM, "the dart passes straight through the %s.", mn);
            m->hunting = true;
            m = NULL;
        }
        if (m) {
            int chance = m->asleep || m->daze > 0 ? 100 : 88 - MOB_INFO[m->kind].def;
            m->asleep = false;
            if ((int)(rng_u32(&g->rng) % 100) < chance) {
                int dmg = rng_range(&g->rng, 2, 4);
                m->hp -= dmg;
                m->flash = 1;
                snprintf(t, sizeof t, "%d", dmg);
                floater(g, x, y, 0xffd27a, t);
                sfx_ex(g, SND_HIT, x, y, false, 0, 0.7f);
                if (m->hp <= 0) { kill_mob(g, m); game_msg(g, C_GOOD, "the dart kills the %s!", mn); sfx(g, SND_KILL, x, y); }
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
                place_item(g, g->depth + 1, IT_DART, 1, bx, by, false, 0);
            }
            game_msg(g, C_DIM, "the dart clatters away into the chasm.");
            sfx_ex(g, SND_CLINK, x, y, false, 0.7f, 0.3f);
        } else if (tile_water((Tile)lv->t[y][x])) {
            push_fx(g, FX_SPLASH, x, y);
            sfx_ex(g, SND_WADE, x, y, false, 0, 0.6f);
            if (lv->t[y][x] == T_DEEP) game_msg(g, C_DIM, "the dart sinks into the deep water.");
            else place_item(g, g->depth, IT_DART, 1, x, y, false, 0);
        } else if (rng_chance(&g->rng, 85)) {
            place_item(g, g->depth, IT_DART, 1, x, y, false, 0);
            if (!m) sfx(g, SND_CLINK, x, y);
        } else {
            game_msg(g, C_DIM, "the dart snaps.");
            sfx(g, SND_CLINK, x, y);
        }
        make_noise(g, x, y, 5);
        break;
    case IT_TORCH:
        if (m && shadeish(m->kind)) {
            int dmg = rng_range(&g->rng, 4, 8);
            m->hp -= dmg;
            m->flash = 1;
            snprintf(t, sizeof t, "%d", dmg);
            floater(g, x, y, 0xffa040, t);
            push_fx(g, FX_BURN, x, y);
            sfx(g, SND_BURN, x, y);
            if (m->hp <= 0) {
                push_fx(g, FX_DISSOLVE, x, y);
                sfx(g, SND_DISSOLVE, x, y);
                if (m->kind == MK_SHADE) game_msg(g, C_GOOD, "the torch sears the shade and it burns away to nothing!");
                kill_mob(g, m);
            } else {
                game_msg(g, C_TEXT, "the torch sears the %s!", mn);
            }
        } else if (m) {
            int dmg = rng_range(&g->rng, 1, 3);
            m->hp -= dmg;
            m->flash = 1;
            m->asleep = false;
            snprintf(t, sizeof t, "%d", dmg);
            floater(g, x, y, 0xffa040, t);
            sfx_ex(g, SND_HIT, x, y, false, 0, 0.6f);
            if (m->hp <= 0) { kill_mob(g, m); sfx(g, SND_KILL, x, y); game_msg(g, C_GOOD, "the torch bursts over the %s and it dies in the flames!", mn); }
            else game_msg(g, C_TEXT, "the torch bursts against the %s in a shower of sparks!", mn);
        }
        if (into_chasm) {
            int bx = x, by = y;
            bool burning = false;
            if (g->lv[g->depth + 1]) {
                nearest_floor(g->lv[g->depth + 1], &bx, &by);
                burning = floor_torch(g, g->depth + 1, bx, by);
                g->lower_dirty = true;
            }
            push_fx(g, FX_FALL, x, y);
            sfx_ex(g, burning ? SND_THUD : SND_HISS, x, y, false, 0.8f, 0.35f);
            if (burning) game_msg(g, C_INFO, "the torch tumbles into the chasm and lands far below, still burning.");
            else game_msg(g, C_DIM, "the torch tumbles into the chasm; far below, a faint hiss, and it is gone.");
        } else if (!floor_torch(g, g->depth, x, y)) {
            push_fx(g, FX_SPLASH, x, y);
            sfx(g, SND_HISS, x, y);
            game_msg(g, C_DIM, "the torch hisses out in the water.");
        } else {
            push_fx(g, FX_SPARKS, x, y);
            sfx(g, SND_THUD, x, y);
            sfx_ex(g, SND_IGNITE, x, y, false, 0.05f, 0.6f);
            if (!m) game_msg(g, C_TEXT, "the torch lands and burns on the floor.");
        }
        make_noise(g, x, y, 3);
        break;
    case IT_OIL: {
        push_fx(g, FX_OIL, x, y);
        sfx_ex(g, SND_SHATTER, x, y, false, into_chasm ? 0.8f : 0, into_chasm ? 0.4f : 1);
        if (into_chasm) {
            int bx = x, by = y;
            if (g->lv[g->depth + 1]) {
                nearest_floor(g->lv[g->depth + 1], &bx, &by);
                ignite_oil(g, g->depth + 1, bx, by);
                g->lower_dirty = true;
            }
            push_fx(g, FX_FALL, x, y);
            game_msg(g, C_INFO, "the flask tumbles into the chasm, and far below a pool of fire blooms.");
            break;
        }
        sfx_ex(g, SND_IGNITE, x, y, false, 0.05f, 1.0f);
        bool wet = tile_water((Tile)lv->t[y][x]);
        if (m && !shadeish(m->kind)) {
            int dmg = rng_range(&g->rng, 1, 2);
            m->hp -= dmg;
            m->flash = 1;
            m->asleep = false;
            snprintf(t, sizeof t, "%d", dmg);
            floater(g, x, y, 0xffa040, t);
            if (m->hp <= 0) { kill_mob(g, m); sfx(g, SND_KILL, x, y); game_msg(g, C_GOOD, "the flask bursts over the %s and it dies in the flames!", mn); m = NULL; }
            else game_msg(g, C_TEXT, "the flask bursts over the %s and it is engulfed in burning oil!", mn);
        } else {
            game_msg(g, C_TEXT, wet ? "the flask bursts, and burning oil spreads across the water." : "the flask bursts into a pool of burning oil.");
        }
        ignite_oil(g, g->depth, x, y);
        make_noise(g, x, y, 4);
        break;
    }
    case IT_FLASH:
        if (into_chasm) {
            push_fx(g, FX_FALL, x, y);
            game_msg(g, C_DIM, "the powder drops into the chasm and goes off far below, a white flicker in the dark.");
            sfx_ex(g, SND_THUD, x, y, false, 0.8f, 0.3f);
        } else if (tile_water((Tile)lv->t[y][x])) {
            push_fx(g, FX_STEAM, x, y);
            sfx(g, SND_HISS, x, y);
            game_msg(g, C_DIM, "the powder lands in the water and fizzles, spoilt.");
        } else {
            flash_burst(g, x, y);
        }
        m = NULL;      /* nobody is hunting you because of it */
        break;
    case IT_POTION:
        push_fx(g, FX_SHATTER, x, y);
        sfx_ex(g, SND_SHATTER, x, y, false, into_chasm ? 0.8f : 0, into_chasm ? 0.4f : 1);
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
        g->torch_fuel -= 5;          /* the charred head takes some burning to catch again */
        if (g->torch_fuel < 1) g->torch_fuel = 1;
        game_msg(g, C_DIM, "you snuff out your torch (%d turns of it left). The dark closes in.", g->torch_fuel);
        sfx(g, SND_SNUFF, g->player.x, g->player.y);
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
    sfx(g, SND_IGNITE, g->player.x, g->player.y);
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
            sfx(g, SND_IGNITE, p->x, p->y);
            end_turn(g);
        } else if (g->torch_fuel > 0) {
            game_torch_toggle(g);
        } else {
            game_torch_toggle(g);
        }
        return true;
    case IT_OIL:
        if (g->torch_fuel <= 0) {
            game_msg(g, C_DIM, "you have no torch going to soak in it. Light one first, or throw the flask.");
            return false;
        }
        take_one(g, slot);
        g->torch_fuel += 50;
        game_msg(g, C_GOOD, "you pour the oil over your torch's head (%d turns of light in it now).", g->torch_fuel);
        sfx(g, g->torch_lit ? SND_IGNITE : SND_PICKUP, p->x, p->y);
        end_turn(g);
        return true;
    case IT_SALT: {
        Level *lv = game_level(g);
        int n = 0;
        bool gap = false;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int X = p->x + dx, Y = p->y + dy;
                Tile t = tile_at(lv, X, Y);
                if (!tile_walkable(t) || tile_water(t) || t == T_DOOR) continue;    /* it won't lie on water */
                Mob *o = game_mob_at(g, X, Y);
                if (o && o->kind == MK_SHADE) { gap = true; continue; }
                lv->salt[Y][X] = 1;
                n++;
            }
        take_one(g, slot);
        push_fx(g, FX_SALT, p->x, p->y);
        sfx_ex(g, SND_PICKUP, p->x, p->y, false, 0, 0.6f);
        game_msg(g, C_GOOD, gap ? "you pour a ring of salt around you, but it breaks where a shade stands."
                                : n >= 9 ? "you pour a ring of salt around you. No shade can cross it."
                                         : "you pour salt around you where it will lie. No shade can cross it.");
        end_turn(g);
        return true;
    }
    case IT_WARMTH:
        take_one(g, slot);
        g->warm_left = 80;
        g->warm_t = 0;
        if (g->chill) {
            game_msg(g, C_GOOD, "you drink the draught. Fire runs through your veins and the chill is gone.");
            g->chill = 0;
        } else {
            game_msg(g, C_GOOD, "you drink the draught. Fire runs through your veins.");
        }
        floater(g, p->x, p->y, 0xffa040, "warm");
        sfx(g, SND_DRINK, p->x, p->y);
        end_turn(g);
        return true;
    case IT_POTION: {
        take_one(g, slot);
        int heal = 15;
        if (p->hp + heal > game_max_hp(g)) heal = game_max_hp(g) - p->hp;
        p->hp += heal;
        char t[16];
        snprintf(t, sizeof t, "+%d", heal);
        floater(g, p->x, p->y, 0x8ee070, t);
        game_msg(g, C_GOOD, "you drink the potion. Warmth floods through you.");
        sfx(g, SND_DRINK, p->x, p->y);
        end_turn(g);
        return true;
    }
    default: {
        const ItemInfo *ii = &ITEM_INFO[g->inv[slot].kind];
        if (ii->gear == GEAR_NONE) {
            game_msg(g, C_DIM, "you can't use that; try throwing it.");
            return false;
        }
        /* swap it with what you have on: the old piece goes into the pack in its place */
        Slot *cur = ii->gear == GEAR_WEAPON ? &g->weapon : &g->armour;
        Slot old = *cur;
        *cur = g->inv[slot];
        g->inv[slot] = old;
        char a[64], b[64];
        game_item_name(cur->kind, 1, cur->plus, a, sizeof a);
        game_item_name(old.kind, 1, old.plus, b, sizeof b);
        game_msg(g, C_INFO, ii->gear == GEAR_WEAPON ? "you now wield %s, and pack away %s." : "you now wear %s, and pack away %s.", a, b);
        sfx(g, SND_PICKUP, p->x, p->y);
        end_turn(g);
        return true;
    }
    }
}

bool game_drop(Game *g, int slot)
{
    if (g->state != GS_PLAY || slot < 0 || slot >= g->ninv) return false;
    Mob *p = &g->player;
    FloorItem *here = game_item_at(g, p->x, p->y);
    Slot s = g->inv[slot];
    if (here && (here->kind != s.kind || !stacks(s.kind))) { game_msg(g, C_DIM, "there's already something here."); return false; }
    if (!place_item(g, g->depth, s.kind, s.count, p->x, p->y, true, s.plus)) return false;
    for (int i = slot; i < g->ninv - 1; i++) g->inv[i] = g->inv[i + 1];
    g->ninv--;
    char nm[64];
    if (game_level(g)->t[p->y][p->x] == T_DEEP) {
        game_msg(g, C_DIM, "you let go of %s and it sinks out of sight.", game_item_name(s.kind, s.count, s.plus, nm, sizeof nm));
        sfx_ex(g, SND_SWIM, p->x, p->y, false, 0, 0.6f);
    } else {
        game_msg(g, C_TEXT, "you put down %s.", game_item_name(s.kind, s.count, s.plus, nm, sizeof nm));
        sfx_ex(g, SND_PICKUP, p->x, p->y, false, 0, 0.7f);
    }
    end_turn(g);
    return true;
}

/* ------------------------------------------------------------------ forge */

bool game_by_forge(Game *g)
{
    Level *lv = game_level(g);
    for (int d = 0; d < 8; d++)
        if (tile_at(lv, g->player.x + DX8[d], g->player.y + DY8[d]) == T_BRAZIER) return true;
    return false;
}

#define LANTERN_MAX 2

int game_forge_cost(const Game *g, ForgeOffer o)
{
    switch (o) {
    case FORGE_WEAPON:  return 3 + 2 * g->weapon.plus;
    case FORGE_ARMOUR:  return 3 + 2 * g->armour.plus;
    case FORGE_HEART:   return 4 + 2 * g->heart;
    case FORGE_LANTERN: return g->lantern >= LANTERN_MAX ? -1 : 5 + 3 * g->lantern;
    default:            return -1;
    }
}

const char *game_forge_label(const Game *g, ForgeOffer o, char *buf, size_t n)
{
    char nm[64];
    switch (o) {
    case FORGE_WEAPON:
        game_item_name(g->weapon.kind, 1, g->weapon.plus, nm, sizeof nm);
        snprintf(buf, n, "Temper your %s to +%d", ITEM_INFO[g->weapon.kind].name, g->weapon.plus + 1);
        break;
    case FORGE_ARMOUR:
        snprintf(buf, n, "Temper your %s to +%d", ITEM_INFO[g->armour.kind].name, g->armour.plus + 1);
        break;
    case FORGE_HEART:   snprintf(buf, n, "Kindle your heart: +3 max HP"); break;
    case FORGE_LANTERN:
        snprintf(buf, n, g->lantern >= LANTERN_MAX ? "Your lantern is as bright as it gets" : "Trim your lantern");
        break;
    default: buf[0] = 0;
    }
    return buf;
}

const char *game_forge_desc(ForgeOffer o)
{
    switch (o) {
    case FORGE_WEAPON:  return "More accurate, and it hits harder. Tempering stays with the weapon if you swap it out.";
    case FORGE_ARMOUR:  return "Harder to hit. Tempering stays with the armour if you change.";
    case FORGE_HEART:   return "Three more max HP, for good.";
    case FORGE_LANTERN: return "Your lantern's own dim glow (not a torch) reaches a tile further: you see more in the dark without being seen.";
    default:            return "";
    }
}

bool game_forge_buy(Game *g, ForgeOffer o)
{
    if (g->state != GS_PLAY) return false;
    int cost = game_forge_cost(g, o);
    if (cost < 0) return false;
    if (g->embers < cost) {
        game_msg(g, C_DIM, "the forge wants %d embers for that; you have %d.", cost, g->embers);
        return false;
    }
    g->embers -= cost;
    Mob *p = &g->player;
    switch (o) {
    case FORGE_WEAPON:
        g->weapon.plus++;
        game_msg(g, C_GOOD, "you feed the forge and work your %s in its heart. It comes out +%d.", ITEM_INFO[g->weapon.kind].name, g->weapon.plus);
        break;
    case FORGE_ARMOUR:
        g->armour.plus++;
        game_msg(g, C_GOOD, "you feed the forge and work your %s in its heat. It comes out +%d.", ITEM_INFO[g->armour.kind].name, g->armour.plus);
        break;
    case FORGE_HEART:
        g->heart++;
        p->maxhp += 3;
        p->hp += 3;
        game_msg(g, C_GOOD, "you hold your hands to the flames and something in you catches. (+3 max HP)");
        break;
    case FORGE_LANTERN:
        g->lantern++;
        game_msg(g, C_GOOD, "you trim your lantern's wick with a coal from the forge. Its glow reaches further.");
        break;
    default: return false;
    }
    sfx(g, SND_IGNITE, p->x, p->y);
    push_fx(g, FX_SPARKS, p->x, p->y);
    end_turn(g);
    return true;
}

/* developer: stand next to this level's forge */
bool game_debug_depth(Game *g, int depth)
{
    if (depth < 1 || depth > MAX_DEPTH) return false;
    for (int d = 1; d < depth; d++) ensure_level(g, d);
    change_level(g, depth, 0, 0);
    Level *lv = game_level(g);
    place_player(g, lv->upx, lv->upy);
    compute_view(g);
    arrive(g);
    return true;
}

bool game_debug_kindle(Game *g, int times)
{
    Level *lv = game_level(g);
    if (!game_final(g) || tile_at(lv, lv->downx, lv->downy) != T_HEARTH) return false;
    int bx = -1, by = -1;
    for (int d = 0; d < 8 && bx < 0; d++) {
        int x = lv->downx + DX8[(d + 2) % 8], y = lv->downy + DY8[(d + 2) % 8];
        if (tile_walkable(tile_at(lv, x, y)) && mob_free(g, x, y)) { bx = x; by = y; }
    }
    if (bx < 0) return false;
    place_player(g, bx, by);
    if (!g->torch_lit) game_torch_toggle(g);
    compute_view(g);
    Mob *ms = g->mobs[g->depth];
    for (int i = 0; i < times && g->state == GS_PLAY; i++) {
        for (int k = 0; k < MAX_MOBS; k++) if (ms[k].alive) ms[k].daze = 2;    /* hold everything still for the picture */
        game_move(g, lv->downx - bx, lv->downy - by);
        g->player.hp = game_max_hp(g);
        for (int k = 0; k < 30; k++) game_update(g, 1.0f / 30);
    }
    return true;
}

bool game_debug_to_forge(Game *g)
{
    Level *lv = game_level(g);
    for (int y = 1; y < MAP_H - 1; y++)
        for (int x = 1; x < MAP_W - 1; x++) {
            if (lv->t[y][x] != T_BRAZIER) continue;
            Mob *ms = g->mobs[g->depth];
            for (int i = 0; i < MAX_MOBS; i++)
                if (ms[i].alive && abs(ms[i].x - x) < 5 && abs(ms[i].y - y) < 5) ms[i].alive = false;
            place_player(g, x, y + 1);
            compute_view(g);
            return true;
        }
    return false;
}
