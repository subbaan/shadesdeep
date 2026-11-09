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
#define MAX_FRAMES 16       /* frames in a creature's idle animation */
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
    Image door_open;        /* the doorway floor, an oak sill across it */
    Image door_open_side;
    Image door_leaf;        /* TILE x (TILE + WALL_R): an open door swung back against its left post, edge on */
    Image door_leaf_low;    /* TILE x (TILE + LOW_R): the same in a cut-down front wall */
    Image door_leaf_side;   /* TILE x (WALL_R + 4): an open door in a north-south wall, back against its north post */
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
    /* idle animations, frame by frame (px NULL past the last frame / when none):
     * small ones TILE x TILE, full-resolution ones the size of mob_hi */
    Image mob_fr[MK_COUNT][MAX_FRAMES], mob_fr_glow[MK_COUNT][MAX_FRAMES];
    Image mob_hi_fr[MK_COUNT][MAX_FRAMES], mob_hi_fr_glow[MK_COUNT][MAX_FRAMES];
    Image item[IT_COUNT];
    Image item_glow[IT_COUNT];
    Image item_hi[IT_COUNT];    /* full resolution, square, laid out like item[]; px NULL when none */
    Image item_hi_glow[IT_COUNT];
} Tiles;

void tiles_build(Tiles *t);
/* Swap in drawn creature sprites from DIR/sprites (player.png, rat.png, ...,
 * each TILE x TILE, with an optional NAME_glow.png of the parts that glow, and
 * optionally NAME_hi.png / NAME_hi_glow.png at full resolution); creatures
 * without a file keep their procedural sprite. NAME_anim.png (frames side by
 * side) and NAME_hi_anim.png, each with an optional _glow, are an idle
 * animation. Items likewise from item_NAME.png (and _glow, _hi, _hi_glow).
 * Returns how many creatures and items loaded. */
int tiles_load_sprites(Tiles *t, const char *dir);
void tiles_free(Tiles *t);
/* frames in creature k's idle animation (0: none) */
static inline int tiles_frames(const Tiles *t, int k)
{
    int n = 0;
    while (n < MAX_FRAMES && t->mob_fr[k][n].px) n++;
    return n;
}
static inline int tiles_hi_frames(const Tiles *t, int k)
{
    int n = 0;
    while (n < MAX_FRAMES && t->mob_hi_fr[k][n].px) n++;
    return n;
}

#endif
