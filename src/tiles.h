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
    Image crystal;          /* emissive */
    Image rubble[3];
    Image mob[MK_COUNT];    /* TILE x TILE, feet at the bottom */
    Image mob_glow[MK_COUNT];   /* emissive parts (lantern, eyes) */
    Image item[IT_COUNT];
    Image item_glow[IT_COUNT];
} Tiles;

void tiles_build(Tiles *t);
void tiles_free(Tiles *t);

#endif
