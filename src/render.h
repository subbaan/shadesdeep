/* Draws the dungeon into the world canvas: per-pixel coloured light, smooth
 * fog of war, raised 3/4 walls, and the level below seen through chasms. */
#ifndef RENDER_H
#define RENDER_H

#include "game.h"
#include "gfx.h"

typedef struct Renderer Renderer;

Renderer *render_new(void);
void render_free(Renderer *r);

/* Move the camera toward the player. world_w/h is the world canvas size. */
void render_camera(Renderer *r, const Game *g, int world_w, int world_h, float dt, bool snap);
void render_cam_pos(const Renderer *r, float *x, float *y);

/* what the interface wants drawn over the map */
typedef struct {
    int hx, hy;             /* tile under the mouse, or -1 */
    const int *path;        /* travel preview */
    int plen;
    const int *tpath;       /* throw preview: the flight, ending where it lands */
    int tlen;
    bool thit;              /* it would hit a creature */
    ItemKind tkind;
} Overlay;

void render_world(Renderer *r, Game *g, Canvas *cv, float dt, const Overlay *ov);

/* The map: render the whole level (as remembered and seen) and shrink the
 * tile rectangle (tx, ty, tw, th) of it into dst. */
void render_map(Renderer *r, Game *g, Canvas *dst, int tx, int ty, int tw, int th);

const Image *render_item_icons(const Renderer *r);        /* IT_COUNT pictures */
int render_dump_lower(const Renderer *r, const char *path);   /* developer aid */

#endif
