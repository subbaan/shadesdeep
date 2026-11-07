#include "font.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

#include <stdlib.h>
#include <string.h>

#define CACHE_CP 0x3000     /* codepoints below this are cached */

typedef struct {
    bool done;
    int w, h, left, top, adv;
    uint8_t *a;
} Glyph;

struct Font {
    FT_Face face;
    int px;
    int ascent, line;
    Glyph *g;               /* CACHE_CP entries for the current size */
};

static FT_Library lib;
static int lib_refs;

Font *font_open(const char *path, int weight)
{
    if (!lib_refs && FT_Init_FreeType(&lib)) return NULL;
    lib_refs++;
    Font *f = calloc(1, sizeof *f);
    if (!f || FT_New_Face(lib, path, 0, &f->face)) {
        free(f);
        if (--lib_refs == 0) FT_Done_FreeType(lib);
        return NULL;
    }
    if (weight > 0 && (f->face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS)) {
        FT_MM_Var *mm;
        if (!FT_Get_MM_Var(f->face, &mm)) {
            FT_Fixed coords[16];
            unsigned n = mm->num_axis < 16 ? mm->num_axis : 16;
            for (unsigned i = 0; i < n; i++) {
                coords[i] = mm->axis[i].def;
                if (mm->axis[i].tag == FT_MAKE_TAG('w', 'g', 'h', 't')) {
                    FT_Fixed w = (FT_Fixed)weight << 16;
                    if (w < mm->axis[i].minimum) w = mm->axis[i].minimum;
                    if (w > mm->axis[i].maximum) w = mm->axis[i].maximum;
                    coords[i] = w;
                }
            }
            FT_Set_Var_Design_Coordinates(f->face, n, coords);
            FT_Done_MM_Var(lib, mm);
        }
    }
    f->g = calloc(CACHE_CP, sizeof *f->g);
    font_size(f, 16);
    return f;
}

static void clear_cache(Font *f)
{
    for (int i = 0; i < CACHE_CP; i++) {
        free(f->g[i].a);
        f->g[i] = (Glyph){ 0 };
    }
}

void font_close(Font *f)
{
    if (!f) return;
    clear_cache(f);
    free(f->g);
    FT_Done_Face(f->face);
    free(f);
    if (--lib_refs == 0) FT_Done_FreeType(lib);
}

void font_size(Font *f, int px)
{
    if (px < 6) px = 6;
    if (px == f->px) return;
    clear_cache(f);
    f->px = px;
    FT_Set_Pixel_Sizes(f->face, 0, (FT_UInt)px);
    f->ascent = (int)(f->face->size->metrics.ascender >> 6);
    f->line = (int)(f->face->size->metrics.height >> 6);
}

int font_px(const Font *f) { return f->px; }
int font_ascent(const Font *f) { return f->ascent; }
int font_line(const Font *f) { return f->line; }

static const Glyph *glyph(Font *f, unsigned cp)
{
    static Glyph tmp;
    Glyph *g = cp < CACHE_CP ? &f->g[cp] : &tmp;
    if (g->done && g != &tmp) return g;
    if (g == &tmp) { free(tmp.a); tmp = (Glyph){ 0 }; }
    g->done = true;
    FT_UInt gi = FT_Get_Char_Index(f->face, cp);
    if (!gi && cp == 0x25c6) gi = FT_Get_Char_Index(f->face, '*');
    if (FT_Load_Glyph(f->face, gi, FT_LOAD_TARGET_LIGHT) ||
        FT_Render_Glyph(f->face->glyph, FT_RENDER_MODE_NORMAL))
        return g;
    FT_GlyphSlot s = f->face->glyph;
    g->w = (int)s->bitmap.width;
    g->h = (int)s->bitmap.rows;
    g->left = s->bitmap_left;
    g->top = s->bitmap_top;
    g->adv = (int)((s->advance.x + 32) >> 6);
    if (g->w && g->h) {
        g->a = malloc((size_t)g->w * g->h);
        for (int y = 0; y < g->h; y++)
            memcpy(g->a + (size_t)y * g->w, s->bitmap.buffer + (ptrdiff_t)y * s->bitmap.pitch, (size_t)g->w);
    }
    return g;
}

static unsigned utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    unsigned c = *s++;
    if (c >= 0xc0 && c < 0xe0 && (*s & 0xc0) == 0x80) {
        c = (c & 0x1f) << 6 | (*s++ & 0x3f);
    } else if (c >= 0xe0 && c < 0xf0 && (s[0] & 0xc0) == 0x80 && (s[1] & 0xc0) == 0x80) {
        c = (c & 0x0f) << 12 | (s[0] & 0x3f) << 6 | (s[1] & 0x3f);
        s += 2;
    } else if (c >= 0xf0 && (s[0] & 0xc0) == 0x80 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) {
        c = (c & 0x07) << 18 | (s[0] & 0x3f) << 12 | (s[1] & 0x3f) << 6 | (s[2] & 0x3f);
        s += 3;
    }
    *p = (const char *)s;
    return c;
}

static int kern(Font *f, unsigned a, unsigned b)
{
    if (!a || !FT_HAS_KERNING(f->face)) return 0;
    FT_Vector k;
    if (FT_Get_Kerning(f->face, FT_Get_Char_Index(f->face, a), FT_Get_Char_Index(f->face, b),
                       FT_KERNING_DEFAULT, &k))
        return 0;
    return (int)(k.x >> 6);
}

int font_width(Font *f, const char *s)
{
    int w = 0;
    unsigned prev = 0;
    while (*s) {
        unsigned cp = utf8_next(&s);
        w += kern(f, prev, cp) + glyph(f, cp)->adv;
        prev = cp;
    }
    return w;
}

int font_draw(Font *f, Canvas *cv, int x, int y, const char *s, uint32_t color, int alpha)
{
    unsigned prev = 0;
    while (*s) {
        unsigned cp = utf8_next(&s);
        x += kern(f, prev, cp);
        prev = cp;
        const Glyph *g = glyph(f, cp);
        int gx = x + g->left, gy = y - g->top;
        for (int j = 0; j < g->h; j++) {
            int yy = gy + j;
            if ((unsigned)yy >= (unsigned)cv->h) continue;
            uint32_t *row = cv->px + (size_t)yy * cv->w;
            const uint8_t *a = g->a + (size_t)j * g->w;
            for (int i = 0; i < g->w; i++) {
                int xx = gx + i;
                if ((unsigned)xx >= (unsigned)cv->w || !a[i]) continue;
                blend_px(&row[xx], color, a[i] * alpha / 255);
            }
        }
        x += g->adv;
    }
    return x;
}

int font_draw_shadow(Font *f, Canvas *cv, int x, int y, const char *s, uint32_t color, int alpha)
{
    int o = f->px / 14 + 1;
    font_draw(f, cv, x + o, y + o, s, 0x000000, alpha * 3 / 4);
    font_draw(f, cv, x, y + o, s, 0x000000, alpha / 3);
    return font_draw(f, cv, x, y, s, color, alpha);
}

int font_wrap(Font *f, Canvas *cv, int x, int y, int maxw, const char *s, uint32_t color, int alpha)
{
    char line[512];
    int ll = 0, used = 0;
    const char *p = s;
    int base = y + f->ascent;
    while (*p) {
        /* take the next word (plus its leading spaces) */
        const char *w = p;
        while (*w == ' ') w++;
        const char *e = w;
        while (*e && *e != ' ' && *e != '\n') e++;
        char trial[512];
        int tl = snprintf(trial, sizeof trial, "%.*s%s%.*s", ll, line, ll ? " " : "", (int)(e - w), w);
        if (tl >= (int)sizeof trial) tl = sizeof trial - 1;
        if (ll && font_width(f, trial) > maxw) {
            if (cv) font_draw(f, cv, x, base + used, line, color, alpha);
            used += f->line;
            ll = snprintf(line, sizeof line, "%.*s", (int)(e - w), w);
        } else {
            memcpy(line, trial, (size_t)tl + 1);
            ll = tl;
        }
        p = e;
        if (*p == '\n') {
            if (cv) font_draw(f, cv, x, base + used, line, color, alpha);
            used += f->line;
            ll = 0;
            line[0] = 0;
            p++;
        }
    }
    if (ll) {
        if (cv) font_draw(f, cv, x, base + used, line, color, alpha);
        used += f->line;
    }
    return used;
}
