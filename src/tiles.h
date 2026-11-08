/* Placeholder art, drawn procedurally at startup until real tiles arrive.
 * Everything is albedo (unlit colour); the renderer applies the light. */
#ifndef TILES_H
#define TILES_H

#include "game.h"
#include "gfx.h"

#define WALL_R 18           /* how far wall tops are raised (= front face height) */
#define LOW_R  5            /* "front" walls (open floor to the north) are cut down to a lip this tall,
                             * so they never hide the row behind them */
#define N_FLOOR 8
#define N_WALL  4

/* nothing but Images: tiles_free walks it as an array of them */
typedef struct {
    Image floor[N_FLOOR];
    Image wtop[N_WALL];
    Image wface[N_WALL];    /* TILE x WALL_R */
    Image cliff;
    Image stairs_dn, stairs_up;
    Image door;             /* TILE x (TILE + WALL_R), drawn raised */
    Image door_low;         /* TILE x (TILE + LOW_R): a door in a cut-down front wall */
    Image door_side;        /* TILE x (TILE + WALL_R): a door in a north-south wall, seen edge on */
    Image door_open;
    Image door_open_side;
    Image sconce;           /* bracket and torch, unlit colours */
    Image brazier;          /* forge brazier on its legs, 28 x 26; feet at the bottom */
    Image brazier_glow;     /* its coals (emissive) */
    Image crystal;          /* emissive */
    Image rubble[3];
    Image mob[MK_COUNT];    /* TILE x TILE, feet at the bottom */
    Image mob_glow[MK_COUNT];   /* emissive parts (lantern, eyes) */
    Image mob_hi[MK_COUNT];     /* the same at full resolution (square, laid out like mob[]),
                                 * drawn instead when zoomed in; px NULL when there is none */
    Image mob_hi_glow[MK_COUNT];
    Image item[IT_COUNT];
    Image item_glow[IT_COUNT];
} Tiles;

void tiles_build(Tiles *t);
/* Swap in drawn creature sprites from DIR/sprites (player.png, rat.png, ...,
 * each TILE x TILE, with an optional NAME_glow.png of the parts that glow, and
 * optionally NAME_hi.png / NAME_hi_glow.png at full resolution); creatures
 * without a file keep their procedural sprite. Returns how many loaded. */
int tiles_load_sprites(Tiles *t, const char *dir);
void tiles_free(Tiles *t);

#endif
