/* Game state and rules: the player, monsters, turns, travel and messages. */
#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "map.h"

#define MAX_DEPTH 26
#define MAX_MOBS  48
#define MAX_MSGS  64
#define MAX_FLOAT 32
#define MAX_PATH  (MAP_W * MAP_H)

typedef enum { MK_PLAYER, MK_RAT, MK_GOBLIN, MK_COUNT } MobKind;

typedef struct {
    const char *name;
    const char *desc;
    int hp, acc, dmin, dmax, def;
    int perception;     /* tiles it notices a lit target from */
} MobInfo;

extern const MobInfo MOB_INFO[MK_COUNT];

typedef struct {
    bool alive;
    MobKind kind;
    int x, y;
    int hp, maxhp;
    bool asleep;
    bool hunting;       /* knows where the player is (or was) */
    int tx, ty;         /* last known player position */
    /* animation (tiles) */
    float rx, ry;       /* rendered position */
    float ax, ay;       /* tween start */
    float at;           /* tween progress 0..1 */
    float bump;         /* attack lunge timer */
    int bdx, bdy;
    float flash;        /* hit flash timer */
    bool flip;
    float idle;         /* phase for idle bob */
} Mob;

/* ---------------------------------------------------------------- items */

typedef enum { IT_TORCH, IT_DART, IT_POTION, IT_COUNT } ItemKind;

typedef struct {
    const char *name, *plural;
    const char *desc;
    int range;          /* throwing range in tiles */
    bool usable;        /* has an "apply" action */
} ItemInfo;

extern const ItemInfo ITEM_INFO[IT_COUNT];

#define MAX_INV    20
#define MAX_ITEMS  64
#define TORCH_FUEL 200  /* turns a carried torch burns */
#define FLOOR_TORCH_FUEL 120

typedef struct { ItemKind kind; int count; } Slot;

typedef struct {
    bool alive;
    ItemKind kind;
    int count;
    int x, y;
    bool dropped;       /* put down on purpose: explore leaves it alone */
} FloorItem;

typedef struct {
    bool active;
    ItemKind kind;
    int sx, sy;         /* thrown from */
    int path[32];       /* tiles it passes through, ending where it lands */
    int n;
    float pos;          /* tiles travelled */
    bool hit;           /* lands on a monster */
} Projectile;

/* one-shot visual events for the renderer (sparks, splashes) */
typedef enum { FX_SPARKS, FX_SHATTER, FX_THUD, FX_FALL } FxKind;
typedef struct { FxKind kind; int x, y; } Fx;
#define MAX_FX 16

typedef struct {
    char text[160];
    uint32_t color;
    float age;
    int count;
} Msg;

typedef struct {
    float x, y;         /* world pixels */
    char text[16];
    uint32_t color;
    float t;
} Floater;

typedef enum { GS_PLAY, GS_JUMP_PROMPT, GS_DEAD } GameState;
typedef enum { AUTO_NONE, AUTO_TRAVEL, AUTO_EXPLORE, AUTO_REST, AUTO_STAIRS } AutoMode;

typedef struct {
    uint32_t seed;
    int depth;                          /* 1-based current depth */
    Level *lv[MAX_DEPTH + 2];           /* generated levels, by depth */
    uint8_t holes[MAX_DEPTH + 2][MAP_H][MAP_W];
    Mob mobs[MAX_DEPTH + 2][MAX_MOBS];  /* monsters per level */
    Mob player;
    int turn;
    int regen;
    GameState state;
    int jx, jy;                         /* chasm tile the jump prompt is about */
    Rng rng;

    /* per-turn view */
    float plight[MAP_H][MAP_W];         /* the player's own glow */
    float visf[MAP_H][MAP_W];           /* smoothed visibility for drawing */

    /* automatic movement */
    AutoMode autom;
    int path[MAX_PATH];
    int plen, pi;
    int goalx, goaly;
    float auto_t;
    int seen_mobs;                      /* bitmask-ish count of visible mobs for interrupts */

    Msg msgs[MAX_MSGS];
    int nmsgs;
    Floater fl[MAX_FLOAT];

    float time;                         /* seconds, for animation */
    float shake;
    int kills;
    bool level_changed;                 /* renderer must rebuild the view below */
    bool lower_dirty;                   /* the level below changed (a torch fell in) */

    Slot inv[MAX_INV];
    int ninv;
    bool torch_lit;                     /* carrying a burning torch */
    int torch_fuel;                     /* turns left on the torch in hand (0 = none started) */
    FloorItem items[MAX_DEPTH + 2][MAX_ITEMS];
    Projectile proj;
    Fx fx[MAX_FX];
    int nfx;
} Game;

Game *game_new(uint32_t seed);
void  game_free(Game *g);
Level *game_level(Game *g);
Level *game_level_below(Game *g);
Mob  *game_mobs(Game *g, int depth);
Mob  *game_mob_at(Game *g, int x, int y);

void game_update(Game *g, float dt);                /* animation + automatic movement */
bool game_busy(const Game *g);                      /* the player's move is still animating */

/* Player commands. Each returns true if a turn passed. */
bool game_move(Game *g, int dx, int dy);
bool game_rest(Game *g);
void game_rest_long(Game *g);
void game_explore(Game *g);
void game_to_stairs(Game *g);
bool game_descend(Game *g);
bool game_ascend(Game *g);
void game_travel_to(Game *g, int x, int y);         /* path there (mouse click) */
void game_stop(Game *g);
void game_jump_answer(Game *g, bool yes);
/* Path from the player to (x, y) through known ground: length, or 0. */
int  game_path_preview(Game *g, int x, int y, int *out, int max);

bool game_debug_to_chasm(Game *g);

/* items */
FloorItem *game_item_at(Game *g, int x, int y);
FloorItem *game_items(Game *g, int depth);
const char *game_item_name(ItemKind k, int count, char *buf, size_t n);   /* "3 darts", "a torch" */
/* The flight of a thrown item toward (tx, ty): tiles until it lands. */
int  game_throw_path(Game *g, ItemKind k, int tx, int ty, int *out, int max, bool *hits);
bool game_throw(Game *g, int slot, int tx, int ty);
bool game_use(Game *g, int slot);       /* light a torch, drink a potion */
bool game_drop(Game *g, int slot);
void game_torch_toggle(Game *g);        /* light (using a torch if needed) or snuff */

void game_msg(Game *g, uint32_t color, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#define C_TEXT    0xd8cbb0
#define C_GOOD    0x9ed36a
#define C_BAD     0xe0644e
#define C_WARN    0xe8b04a
#define C_INFO    0x8fb8de
#define C_DIM     0x8a8070

#endif
