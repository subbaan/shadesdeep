/* Software framebuffer helpers. Canvas pixels are 0xRRGGBB (top byte ignored);
 * images and sprites are 0xAARRGGBB. */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>

typedef struct {
    int w, h;
    uint32_t *px;
} Canvas;

typedef struct {
    int w, h;
    uint32_t *px;       /* 0xAARRGGBB */
} Image;

#define RGB(r, g, b) ((uint32_t)(((r) & 255) << 16 | ((g) & 255) << 8 | ((b) & 255)))
#define CR(c) ((int)((c) >> 16 & 255))
#define CG(c) ((int)((c) >> 8 & 255))
#define CB(c) ((int)((c) & 255))
#define CA(c) ((int)((c) >> 24 & 255))

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline uint32_t mix(uint32_t a, uint32_t b, int t)  /* t: 0 = a .. 256 = b */
{
    int r = CR(a) + ((CR(b) - CR(a)) * t >> 8);
    int g = CG(a) + ((CG(b) - CG(a)) * t >> 8);
    int bl = CB(a) + ((CB(b) - CB(a)) * t >> 8);
    return RGB(r, g, bl);
}

static inline uint32_t add_sat(uint32_t a, int r, int g, int b)
{
    return RGB(clampi(CR(a) + r, 0, 255), clampi(CG(a) + g, 0, 255), clampi(CB(a) + b, 0, 255));
}

static inline void blend_px(uint32_t *d, uint32_t c, int a)  /* a: 0..255 */
{
    if (a <= 0) return;
    if (a >= 255) { *d = c & 0xffffff; return; }
    *d = mix(*d, c, a + (a >> 7));
}

void cv_fill(Canvas *cv, int x, int y, int w, int h, uint32_t c);
void cv_blend_rect(Canvas *cv, int x, int y, int w, int h, uint32_t c, int a);
void cv_frame(Canvas *cv, int x, int y, int w, int h, uint32_t c, int a);
void cv_glow(Canvas *cv, int x, int y, int r, uint32_t c, float strength);    /* additive radial */
void cv_disc(Canvas *cv, float cx, float cy, float r, uint32_t c, int a);     /* soft-edged */
void cv_ring(Canvas *cv, float cx, float cy, float r, float width, uint32_t c, int a);
/* Draw an image scaled up by an integer factor (for interface icons). */
void cv_image_scaled(Canvas *cv, const Image *im, int x, int y, int s, int alpha);
void cv_line(Canvas *cv, int x0, int y0, int x1, int y1, uint32_t c, int a);
/* Draw an image (sprite rect sx,sy,sw,sh) with alpha at x,y; flip mirrors it. */
void cv_image(Canvas *cv, const Image *im, int sx, int sy, int sw, int sh, int x, int y, int flip);
/* Nearest-neighbour integer upscale of src into dst at (ox, oy). */
void cv_upscale(Canvas *dst, const Canvas *src, int s, int ox, int oy);

int  image_alloc(Image *im, int w, int h);
int  image_load(Image *im, const char *path);
void image_free(Image *im);
int  canvas_write_png(const Canvas *cv, const char *path);

#endif
