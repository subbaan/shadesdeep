/* Antialiased text through FreeType, drawn straight onto a Canvas at native
 * resolution. Glyphs are cached per font and pixel size. */
#ifndef FONT_H
#define FONT_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

typedef struct Font Font;

Font *font_open(const char *path, int weight);      /* weight 0 = the font's default */
void  font_close(Font *f);
void  font_size(Font *f, int px);                   /* pixel height of the em */
int   font_px(const Font *f);
int   font_ascent(const Font *f);
int   font_line(const Font *f);                     /* line height */
int   font_width(Font *f, const char *utf8);
/* Draw at (x, baseline y). Returns the pen x after the text. */
int   font_draw(Font *f, Canvas *cv, int x, int y, const char *utf8, uint32_t color, int alpha);
/* With a soft dark shadow, for text over the map. */
int   font_draw_shadow(Font *f, Canvas *cv, int x, int y, const char *utf8, uint32_t color, int alpha);
/* Word-wrapped paragraph; returns the height used. Draws nothing if cv is NULL. */
int   font_wrap(Font *f, Canvas *cv, int x, int y, int maxw, const char *utf8, uint32_t color, int alpha);

#endif
