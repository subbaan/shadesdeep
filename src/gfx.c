#include "gfx.h"

#include <math.h>
#include <png.h>
#include <stdlib.h>
#include <string.h>

void cv_fill(Canvas *cv, int x, int y, int w, int h, uint32_t c)
{
    int x0 = clampi(x, 0, cv->w), x1 = clampi(x + w, 0, cv->w);
    int y0 = clampi(y, 0, cv->h), y1 = clampi(y + h, 0, cv->h);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = cv->px + (size_t)yy * cv->w;
        for (int xx = x0; xx < x1; xx++) row[xx] = c;
    }
}

void cv_blend_rect(Canvas *cv, int x, int y, int w, int h, uint32_t c, int a)
{
    int x0 = clampi(x, 0, cv->w), x1 = clampi(x + w, 0, cv->w);
    int y0 = clampi(y, 0, cv->h), y1 = clampi(y + h, 0, cv->h);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = cv->px + (size_t)yy * cv->w;
        for (int xx = x0; xx < x1; xx++) blend_px(&row[xx], c, a);
    }
}

void cv_frame(Canvas *cv, int x, int y, int w, int h, uint32_t c, int a)
{
    cv_blend_rect(cv, x, y, w, 1, c, a);
    cv_blend_rect(cv, x, y + h - 1, w, 1, c, a);
    cv_blend_rect(cv, x, y + 1, 1, h - 2, c, a);
    cv_blend_rect(cv, x + w - 1, y + 1, 1, h - 2, c, a);
}

void cv_glow(Canvas *cv, int x, int y, int r, uint32_t c, float strength)
{
    if (r <= 0) return;
    int x0 = clampi(x - r, 0, cv->w), x1 = clampi(x + r + 1, 0, cv->w);
    int y0 = clampi(y - r, 0, cv->h), y1 = clampi(y + r + 1, 0, cv->h);
    float inv = 1.0f / ((float)r * r);
    float cr = CR(c) * strength, cg = CG(c) * strength, cb = CB(c) * strength;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = cv->px + (size_t)yy * cv->w;
        float dy = (float)(yy - y);
        for (int xx = x0; xx < x1; xx++) {
            float dx = (float)(xx - x);
            float d = (dx * dx + dy * dy) * inv;
            if (d >= 1.0f) continue;
            float k = (1.0f - d);
            k *= k;
            row[xx] = add_sat(row[xx], (int)(cr * k), (int)(cg * k), (int)(cb * k));
        }
    }
}

void cv_disc(Canvas *cv, float cx, float cy, float r, uint32_t c, int a)
{
    int x0 = clampi((int)floorf(cx - r - 1), 0, cv->w), x1 = clampi((int)ceilf(cx + r + 1), 0, cv->w);
    int y0 = clampi((int)floorf(cy - r - 1), 0, cv->h), y1 = clampi((int)ceilf(cy + r + 1), 0, cv->h);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) {
            float dx = xx + 0.5f - cx, dy = yy + 0.5f - cy;
            float e = r - sqrtf(dx * dx + dy * dy) + 0.5f;
            if (e <= 0) continue;
            int aa = e >= 1 ? a : (int)(a * e);
            blend_px(&cv->px[(size_t)yy * cv->w + xx], c, aa);
        }
}

void cv_ring(Canvas *cv, float cx, float cy, float r, float width, uint32_t c, int a)
{
    float ro = r + width / 2, ri = r - width / 2;
    int x0 = clampi((int)floorf(cx - ro - 1), 0, cv->w), x1 = clampi((int)ceilf(cx + ro + 1), 0, cv->w);
    int y0 = clampi((int)floorf(cy - ro - 1), 0, cv->h), y1 = clampi((int)ceilf(cy + ro + 1), 0, cv->h);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) {
            float dx = xx + 0.5f - cx, dy = yy + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
            float e = fminf(ro - d, d - ri) + 0.5f;
            if (e <= 0) continue;
            blend_px(&cv->px[(size_t)yy * cv->w + xx], c, e >= 1 ? a : (int)(a * e));
        }
}

void cv_image_scaled(Canvas *cv, const Image *im, int x, int y, int s, int alpha)
{
    for (int j = 0; j < im->h * s; j++) {
        int yy = y + j;
        if ((unsigned)yy >= (unsigned)cv->h) continue;
        const uint32_t *src = im->px + (size_t)(j / s) * im->w;
        uint32_t *row = cv->px + (size_t)yy * cv->w;
        for (int i = 0; i < im->w * s; i++) {
            int xx = x + i;
            if ((unsigned)xx >= (unsigned)cv->w) continue;
            uint32_t c = src[i / s];
            blend_px(&row[xx], c, CA(c) * alpha / 255);
        }
    }
}

void cv_line(Canvas *cv, int x0, int y0, int x1, int y1, uint32_t c, int a)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if ((unsigned)x0 < (unsigned)cv->w && (unsigned)y0 < (unsigned)cv->h)
            blend_px(&cv->px[(size_t)y0 * cv->w + x0], c, a);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void cv_image(Canvas *cv, const Image *im, int sx, int sy, int sw, int sh, int x, int y, int flip)
{
    for (int j = 0; j < sh; j++) {
        int yy = y + j;
        if ((unsigned)yy >= (unsigned)cv->h) continue;
        const uint32_t *src = im->px + (size_t)(sy + j) * im->w + sx;
        uint32_t *row = cv->px + (size_t)yy * cv->w;
        for (int i = 0; i < sw; i++) {
            int xx = x + i;
            if ((unsigned)xx >= (unsigned)cv->w) continue;
            uint32_t c = src[flip ? sw - 1 - i : i];
            blend_px(&row[xx], c, CA(c));
        }
    }
}

void cv_upscale(Canvas *dst, const Canvas *src, int s, int ox, int oy)
{
    for (int y = 0; y < src->h; y++) {
        int dy0 = oy + y * s;
        if (dy0 >= dst->h) break;
        if (dy0 + s <= 0) continue;
        const uint32_t *sr = src->px + (size_t)y * src->w;
        int first = -1;
        for (int k = 0; k < s; k++) {
            int dy = dy0 + k;
            if (dy < 0 || dy >= dst->h) continue;
            uint32_t *dr = dst->px + (size_t)dy * dst->w;
            if (first >= 0) {
                memcpy(dr, dst->px + (size_t)first * dst->w, (size_t)dst->w * 4);
                continue;
            }
            for (int x = 0; x < src->w; x++) {
                uint32_t c = sr[x];
                int dx0 = ox + x * s;
                for (int i = 0; i < s; i++) {
                    int dx = dx0 + i;
                    if ((unsigned)dx < (unsigned)dst->w) dr[dx] = c;
                }
            }
            first = dy;
        }
    }
}

void cv_downscale(Canvas *dst, const Canvas *src, int sx, int sy, int sw, int sh)
{
    if (dst->w <= 0 || dst->h <= 0 || sw <= 0 || sh <= 0) return;
    for (int y = 0; y < dst->h; y++) {
        int y0 = sy + (int)((long)y * sh / dst->h), y1 = sy + (int)((long)(y + 1) * sh / dst->h);
        if (y1 <= y0) y1 = y0 + 1;
        y0 = clampi(y0, 0, src->h - 1);
        y1 = clampi(y1, y0 + 1, src->h);
        for (int x = 0; x < dst->w; x++) {
            int x0 = sx + (int)((long)x * sw / dst->w), x1 = sx + (int)((long)(x + 1) * sw / dst->w);
            if (x1 <= x0) x1 = x0 + 1;
            x0 = clampi(x0, 0, src->w - 1);
            x1 = clampi(x1, x0 + 1, src->w);
            unsigned r = 0, g = 0, b = 0, n = 0;
            for (int yy = y0; yy < y1; yy++) {
                const uint32_t *row = src->px + (size_t)yy * src->w;
                for (int xx = x0; xx < x1; xx++) {
                    uint32_t c = row[xx];
                    r += CR(c); g += CG(c); b += CB(c);
                    n++;
                }
            }
            dst->px[(size_t)y * dst->w + x] = RGB(r / n, g / n, b / n);
        }
    }
}

int image_alloc(Image *im, int w, int h)
{
    im->w = w;
    im->h = h;
    im->px = calloc((size_t)w * h, 4);
    return im->px ? 0 : -1;
}

int image_load(Image *im, const char *path)
{
    memset(im, 0, sizeof *im);
    png_image p;
    memset(&p, 0, sizeof p);
    p.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&p, path)) return -1;
    p.format = PNG_FORMAT_BGRA;     /* little-endian uint32 0xAARRGGBB */
    im->px = malloc(PNG_IMAGE_SIZE(p));
    if (!im->px) { png_image_free(&p); return -1; }
    if (!png_image_finish_read(&p, NULL, im->px, 0, NULL)) {
        free(im->px);
        im->px = NULL;
        return -1;
    }
    im->w = (int)p.width;
    im->h = (int)p.height;
    return 0;
}

void image_free(Image *im)
{
    free(im->px);
    memset(im, 0, sizeof *im);
}

int canvas_write_png(const Canvas *cv, const char *path)
{
    png_image p;
    memset(&p, 0, sizeof p);
    p.version = PNG_IMAGE_VERSION;
    p.width = (png_uint_32)cv->w;
    p.height = (png_uint_32)cv->h;
    p.format = PNG_FORMAT_BGRA;
    uint32_t *tmp = malloc((size_t)cv->w * cv->h * 4);
    if (!tmp) return -1;
    for (size_t i = 0, n = (size_t)cv->w * cv->h; i < n; i++) tmp[i] = cv->px[i] | 0xff000000u;
    int ok = png_image_write_to_file(&p, path, 0, tmp, 0, NULL);
    free(tmp);
    return ok ? 0 : -1;
}
