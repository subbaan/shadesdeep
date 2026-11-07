/* Dungeon levels: tiles, rooms, light sources, field of view and lighting. */
#ifndef LUME_MAP_H
#define LUME_MAP_H

#include <stdbool.h>
#include <stdint.h>

#define MAP_W 72
#define MAP_H 48
#define TILE  32
#define MAX_ROOMS  40
#define MAX_LIGHTS 96
#define LIGHT_MAXR 9
#define CONTRIB_MAX ((2 * LIGHT_MAXR + 1) * (2 * LIGHT_MAXR + 1))

typedef struct { uint64_t s; } Rng;

static inline uint32_t rng_u32(Rng *r)
{
    uint64_t z = (r->s += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return (uint32_t)((z ^ (z >> 31)) >> 32);
}
static inline int rng_range(Rng *r, int lo, int hi) { return hi <= lo ? lo : lo + (int)(rng_u32(r) % (uint32_t)(hi - lo + 1)); }
static inline bool rng_chance(Rng *r, int pct) { return (int)(rng_u32(r) % 100) < pct; }
static inline float rng_f(Rng *r) { return (rng_u32(r) >> 8) * (1.0f / 16777216.0f); }

/* hash for per-tile decoration that must not change between frames */
static inline uint32_t hash2(int x, int y, uint32_t seed)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

typedef enum {
    T_ROCK,             /* solid, never shown */
    T_WALL,
    T_FLOOR,
    T_DOOR,             /* closed */
    T_DOOR_OPEN,
    T_CHASM,
    T_STAIRS_DOWN,
    T_STAIRS_UP,
    T_RUBBLE,           /* floor with debris, walkable */
    T_COUNT
} Tile;

enum {
    F_SEEN    = 1,      /* remembered */
    F_VIS     = 2,      /* in view this turn */
    F_CORR    = 4,      /* corridor */
};

typedef enum { L_SCONCE, L_BRAZIER, L_CRYSTAL, L_MOSS, L_FLOORTORCH, L_PLAYER } LightKind;

typedef struct {
    int x, y;           /* tile the light shines from (for a sconce: the floor in front of it) */
    int fx, fy;         /* tile the fixture is drawn on (the wall for a sconce) */
    LightKind kind;
    bool lit;
    float r, g, b;      /* colour, 1.0 = full */
    float radius;
    float phase;
    int room;           /* room it belongs to, -1 none */
    int fuel;           /* turns left, or -1 for a light that never burns out */
} Light;

typedef struct {
    int x, y, w, h;     /* interior */
    bool dark;          /* started unlit */
    bool cave;
} Room;

typedef struct {
    int depth;
    uint32_t seed;
    uint8_t t[MAP_H][MAP_W];
    uint8_t f[MAP_H][MAP_W];
    int8_t  room[MAP_H][MAP_W];     /* room index or -1 */
    uint8_t scorch[MAP_H][MAP_W];   /* a torch burned out here: soot and a charred stub */
    Room rooms[MAX_ROOMS];
    int nrooms;
    Light lights[MAX_LIGHTS];
    int nlights;
    int upx, upy, downx, downy;
    /* light per tile, from the fixed lights (recomputed when they change) */
    float lr[MAP_H][MAP_W], lg[MAP_H][MAP_W], lb[MAP_H][MAP_W];
    /* each light's reach: tile index and weight, for animating the flicker */
    uint16_t ci[MAX_LIGHTS][CONTRIB_MAX];
    float cw[MAX_LIGHTS][CONTRIB_MAX];
    int cn[MAX_LIGHTS];
    float ambient;
    bool light_dirty;
} Level;

static inline bool in_map(int x, int y) { return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H; }
static inline Tile tile_at(const Level *lv, int x, int y) { return in_map(x, y) ? (Tile)lv->t[y][x] : T_ROCK; }
bool tile_opaque(Tile t);       /* blocks sight and light */
bool tile_walkable(Tile t);     /* can stand on without falling */
bool tile_solid(Tile t);        /* drawn as a raised block */
const char *tile_name(Tile t);
const char *tile_desc(Tile t);

/* Generate a level. `below_hole`, if given, marks tiles that must be open
 * floor (the area under the chasms of the level above). The chasms this level
 * makes are written to `holes_out` (may be NULL). */
void level_generate(Level *lv, int depth, uint32_t seed, const uint8_t (*below_hole)[MAP_W],
                    uint8_t (*holes_out)[MAP_W]);

/* Field of view from (ox, oy), calling mark for every tile seen (including walls). */
typedef void (*FovMark)(void *ctx, int x, int y, float dist);
void fov_cast(const Level *lv, int ox, int oy, int radius, FovMark mark, void *ctx);

/* Recompute the static light from the fixed lights. */
void level_relight(Level *lv);
/* Flicker multiplier of a light at time t (seconds). */
float light_flicker(const Light *l, float t);
/* Brightness 0..~2 at a tile (static lights only). */
float level_brightness(const Level *lv, int x, int y);

/* A sconce on wall (wx, wy) shining into the floor at (fx, fy). */
int level_add_light(Level *lv, LightKind k, int x, int y, int fx, int fy, bool lit, int room);
Light *level_light_on(Level *lv, int fx, int fy);   /* the fixture drawn on that tile */
void level_remove_light(Level *lv, int i);

#endif
