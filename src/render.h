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

/* hover: tile under the mouse (or -1); path: tiles of the preview path */
void render_world(Renderer *r, Game *g, Canvas *cv, float dt, int hover_x, int hover_y,
                  const int *path, int plen);

int render_dump_lower(const Renderer *r, const char *path);   /* developer aid */

#endif
