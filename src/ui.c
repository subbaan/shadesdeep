#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "font.h"
#include "version.h"

#define MAX_BTN 48
#define MAX_PANEL 16

typedef struct { int x, y, w, h; UiAction a; int arg; } Btn;
typedef struct { int x, y, w, h; } Box;

struct UI {
    Font *head, *body, *small, *big, *title;
    Image icons[IT_COUNT];  /* not owned: set by ui_set_icons */
    Image icons_hi[IT_COUNT];   /* full resolution ones, px NULL when none */
    int cell_h;
    float scale;
    int px;                 /* body text size */
    Btn btn[MAX_BTN];
    int nbtn;
    Box panel[MAX_PANEL];
    int npanel;
    int banner_depth;
    float banner_t;
    float last_time;
    /* the title screen */
    const Image *title_art; /* not owned */
    Canvas title_cv;        /* title_art scaled to the screen */
    float title_t;
    struct { float x, y, vx, vy, life, max, r; } ember[90];
};

static const uint32_t GOLD = 0xc9a45c, GOLD_DIM = 0x6e5a34, INK = 0x0b0907;

UI *ui_new(const char *dir, char *err, size_t errn)
{
    UI *ui = calloc(1, sizeof *ui);
    char p[1024];
    snprintf(p, sizeof p, "%s/Cinzel.ttf", dir);
    ui->head = font_open(p, 700);
    ui->big = font_open(p, 700);
    ui->title = font_open(p, 900);
    snprintf(p, sizeof p, "%s/Alegreya.ttf", dir);
    ui->body = font_open(p, 500);
    ui->small = font_open(p, 500);
    if (!ui->head || !ui->body || !ui->small || !ui->big || !ui->title) {
        snprintf(err, errn, "cannot load fonts from %s", dir);
        ui_free(ui);
        return NULL;
    }
    ui->banner_depth = 0;
    ui_set_scale(ui, 20, 1.0f);
    return ui;
}

void ui_free(UI *ui)
{
    if (!ui) return;
    font_close(ui->head);
    font_close(ui->body);
    font_close(ui->small);
    font_close(ui->big);
    font_close(ui->title);
    free(ui->title_cv.px);
    free(ui);
}

void ui_set_scale(UI *ui, int cell_h, float scale)
{
    if (cell_h < 10) cell_h = 10;
    ui->cell_h = cell_h;
    ui->scale = scale;
    int px = (int)lroundf(cell_h * 1.05f * scale);
    if (px < 13) px = 13;
    ui->px = px;
    font_size(ui->body, px);
    font_size(ui->small, px * 82 / 100);
    font_size(ui->head, px * 92 / 100);
    font_size(ui->big, px * 17 / 10);
    font_size(ui->title, px * 9 / 2);
}

/* ------------------------------------------------------------- widgets */

static void add_btn_arg(UI *ui, int x, int y, int w, int h, UiAction a, int arg)
{
    if (ui->nbtn < MAX_BTN) ui->btn[ui->nbtn++] = (Btn){ x, y, w, h, a, arg };
}

static void add_btn(UI *ui, int x, int y, int w, int h, UiAction a) { add_btn_arg(ui, x, y, w, h, a, 0); }

void ui_set_icons(UI *ui, const Image *icons, const Image *icons_hi)
{
    for (int i = 0; i < IT_COUNT; i++) {
        ui->icons[i] = icons[i];
        ui->icons_hi[i] = icons_hi ? icons_hi[i] : (Image){ 0 };
    }
}

/* an item's picture, s times its tile size: from the full-resolution one when there is one */
static void item_icon(const UI *ui, Canvas *cv, int k, int x, int y, int s, int alpha)
{
    if (ui->icons_hi[k].px && s > 1) cv_image_fit(cv, &ui->icons_hi[k], x, y, TILE * s, alpha);
    else cv_image_scaled(cv, &ui->icons[k], x, y, s, alpha);
}

static void add_panel(UI *ui, int x, int y, int w, int h)
{
    if (ui->npanel < MAX_PANEL) ui->panel[ui->npanel++] = (Box){ x, y, w, h };
}

UiAction ui_hit(const UI *ui, int mx, int my, int *arg)
{
    for (int i = ui->nbtn - 1; i >= 0; i--) {
        const Btn *b = &ui->btn[i];
        if (mx >= b->x && my >= b->y && mx < b->x + b->w && my < b->y + b->h) {
            if (arg) *arg = b->arg;
            return b->a;
        }
    }
    return UA_NONE;
}

bool ui_over_panel(const UI *ui, int mx, int my)
{
    for (int i = 0; i < ui->npanel; i++) {
        const Box *b = &ui->panel[i];
        if (mx >= b->x && my >= b->y && mx < b->x + b->w && my < b->y + b->h) return true;
    }
    return ui_hit(ui, mx, my, NULL) != UA_NONE;
}

static void panel(UI *ui, Canvas *cv, int x, int y, int w, int h, int alpha, bool hit)
{
    cv_blend_rect(cv, x, y, w, h, INK, alpha);
    /* soft vertical sheen */
    for (int j = 0; j < h / 3; j++) cv_blend_rect(cv, x + 1, y + 1 + j, w - 2, 1, 0x3a2e1c, 40 * (h / 3 - j) / (h / 3 + 1));
    cv_frame(cv, x, y, w, h, GOLD_DIM, 230);
    cv_frame(cv, x + 2, y + 2, w - 4, h - 4, 0x2a2214, 200);
    /* corner studs */
    int s = ui->px / 5 + 2;
    cv_fill(cv, x - 1, y - 1, s, s, GOLD);
    cv_fill(cv, x + w - s + 1, y - 1, s, s, GOLD);
    cv_fill(cv, x - 1, y + h - s + 1, s, s, GOLD);
    cv_fill(cv, x + w - s + 1, y + h - s + 1, s, s, GOLD);
    if (hit) add_panel(ui, x, y, w, h);
}

static void diamond(Canvas *cv, int cx, int cy, int r, uint32_t c)
{
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++)
            if (abs(i) + abs(j) <= r) cv_blend_rect(cv, cx + i, cy + j, 1, 1, c, abs(i) + abs(j) == r ? 140 : 255);
}

/* ------------------------------------------------------------ stone */

static uint32_t h32(uint32_t a)
{
    a ^= a >> 16; a *= 0x7feb352du;
    a ^= a >> 15; a *= 0x846ca68bu;
    return a ^ (a >> 16);
}

static uint32_t str_hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; s && *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

typedef enum { STONE_PLAIN, STONE_HOT, STONE_DIM } StoneLook;

/* a slab's outline: how far each column's top and bottom, and each row's left
 * and right, are worn in from the box, in runs like weathered stone */
#define SLAB_MAX 400
typedef struct {
    int gw, gh;
    int8_t top[SLAB_MAX], bot[SLAB_MAX], lft[SLAB_MAX], rgt[SLAB_MAX];
    int c[4];               /* chipped corners */
} Slab;

static void slab_edge(int8_t *e, int n, uint32_t seed)
{
    int i = 0, k = 0;
    while (i < n) {
        uint32_t r = h32(seed ^ (uint32_t)k++ * 0x9e3779b9u);
        int len = 3 + (int)(r % 8), off = r >> 8 & 7;
        int depth = off < 5 ? 0 : off < 7 ? 1 : 2;
        if (depth == 2 && len > 3) len = 2 + (int)(r >> 12 & 1);    /* a deep chip is a short one */
        for (int j = 0; j < len && i < n; j++) e[i++] = (int8_t)depth;
    }
}

static void slab_make(Slab *sl, int gw, int gh, uint32_t seed)
{
    sl->gw = gw;
    sl->gh = gh;
    slab_edge(sl->top, gw, seed ^ 0x1111);
    slab_edge(sl->bot, gw, seed ^ 0x2222);
    slab_edge(sl->lft, gh, seed ^ 0x3333);
    slab_edge(sl->rgt, gh, seed ^ 0x4444);
    for (int i = 0; i < gh; i++) {          /* the short sides wear less */
        if (sl->lft[i] > 1) sl->lft[i] = 1;
        if (sl->rgt[i] > 1) sl->rgt[i] = 1;
    }
    bool small = gh < 14;                   /* a small slab: lighter wear, or it turns into a pebble */
    if (small) {
        for (int i = 0; i < gw; i++) {
            if (sl->top[i] > 1) sl->top[i] = 1;
            if (sl->bot[i] > 1) sl->bot[i] = 1;
        }
        for (int i = 0; i < gh; i++) sl->lft[i] = sl->rgt[i] = 0;
    }
    for (int k = 0; k < 4; k++) sl->c[k] = (small ? 1 : 2) + (int)(h32(seed ^ (uint32_t)(k + 5) * 0x51ed27u) % (small ? 2 : 3));
}

static bool slab_in(const Slab *sl, int i, int j)
{
    int gw = sl->gw, gh = sl->gh;
    if (i < 0 || j < 0 || i >= gw || j >= gh) return false;
    if (i + j < sl->c[0] || (gw - 1 - i) + j < sl->c[1] || i + (gh - 1 - j) < sl->c[2]
        || (gw - 1 - i) + (gh - 1 - j) < sl->c[3]) return false;
    return j >= sl->top[i] && j < gh - sl->bot[i] && i >= sl->lft[j] && i < gw - sl->rgt[j];
}

/* smooth-ish value noise over cells, -1..1 */
static float slab_blotch(int i, int j, uint32_t seed)
{
    int ci = i / 5, cj = j / 3;
    float fx = (i % 5) / 5.0f, fy = (j % 3) / 3.0f;
#define V(a, b) ((float)(h32(seed ^ (uint32_t)(a) * 73856093u ^ (uint32_t)(b) * 19349663u) & 1023) / 511.5f - 1)
    float top = V(ci, cj) + (V(ci + 1, cj) - V(ci, cj)) * fx;
    float bot = V(ci, cj + 1) + (V(ci + 1, cj + 1) - V(ci, cj + 1)) * fx;
#undef V
    return top + (bot - top) * fy;
}

/* A slab of dressed stone in the box (x, y, w, h), drawn in chunky pixels like
 * the dungeon's own art: ragged chipped edges, a lit lip along the top and left,
 * a shadowed one along the bottom and right, a carved groove inside, the odd
 * crack. seed keeps each slab's wear the same from frame to frame. */
static void stone_slab(UI *ui, Canvas *cv, int x, int y, int w, int h, uint32_t seed, StoneLook look)
{
    int P = ui->px / 7;                     /* the stone's "pixel", like the dungeon art's */
    if (P > h / 14) P = h / 14;
    if (P < 2) P = 2;
    int gw = w / P, gh = h / P;
    if (gw < 6 || gh < 4) { cv_blend_rect(cv, x, y, w, h, 0x2e2b27, 230); return; }
    if (gw > SLAB_MAX) gw = SLAB_MAX;
    if (gh > SLAB_MAX) gh = SLAB_MAX;
    int ox = x + (w - gw * P) / 2, oy = y + (h - gh * P) / 2;
    Slab sl;
    slab_make(&sl, gw, gh, seed);
    /* its shadow on whatever is behind */
    for (int j = 0; j < gh; j++)
        for (int i = 0; i < gw; i++)
            if (slab_in(&sl, i, j) && !slab_in(&sl, i + 1, j + 1))
                cv_blend_rect(cv, ox + (i + 1) * P, oy + (j + 1) * P, P, P, 0x000000, 150);
    uint32_t base = look == STONE_HOT ? 0x4e4439 : look == STONE_DIM ? 0x22242a : 0x353840;
    uint32_t lip = look == STONE_HOT ? 0xc9a45c : 0x6a6e7a;
    bool groove = gh >= 9;
    /* a crack or two, as runs of cells */
    int crack_n = (int)(h32(seed ^ 0x77) % 3);
    int ck[2][8][2];
    for (int c = 0; c < crack_n && c < 2; c++) {
        uint32_t r = h32(seed ^ (uint32_t)(c + 1) * 0x1234567u);
        int ci = 3 + (int)(r % (uint32_t)(gw > 8 ? gw - 6 : 1)), cj = (r >> 8) & 1 ? 0 : gh - 1, dj = cj ? -1 : 1;
        for (int k = 0; k < 8; k++) {
            ck[c][k][0] = ci;
            ck[c][k][1] = cj;
            cj += dj;
            ci += (int)((r >> (10 + k * 2)) % 3) - 1;
        }
    }
    for (int j = 0; j < gh; j++) {
        for (int i = 0; i < gw; i++) {
            if (!slab_in(&sl, i, j)) continue;
            /* how far in from each edge (0 = on it), up to 3 */
            int dt = 0, db = 0, dl = 0, dr = 0;
            while (dt < 3 && slab_in(&sl, i, j - dt - 1)) dt++;
            while (db < 3 && slab_in(&sl, i, j + db + 1)) db++;
            while (dl < 3 && slab_in(&sl, i - dl - 1, j)) dl++;
            while (dr < 3 && slab_in(&sl, i + dr + 1, j)) dr++;
            uint32_t n = h32(seed ^ (uint32_t)i * 0x632be5abu ^ (uint32_t)j * 0x85157af5u);
            int v = (int)(slab_blotch(i, j, seed) * 9) + (int)(n % 9) - 4;
            uint32_t c = add_sat(base, v, v, v + 1);
            if (dt == 0 || dl == 0) c = mix(c, lip, dt == 0 && dl == 0 ? 200 : 150);
            else if (dt == 1 || dl == 1) c = add_sat(c, 12, 11, 9);
            if (db == 0 || dr == 0) c = add_sat(c, -30, -30, -28);
            else if (db == 1 || dr == 1) c = add_sat(c, -13, -13, -12);
            if (groove) {
                int d = dt < db ? dt : db, e = dl < dr ? dl : dr, m = d < e ? d : e;
                if (m == 2) c = (dt == 2 || dl == 2) && !(db == 2 || dr == 2) ? add_sat(c, -16, -16, -15) : add_sat(c, 9, 8, 7);
            }
            for (int k = 0; k < crack_n && k < 2; k++)
                for (int q = 0; q < 5 + (int)(seed >> (k * 3) & 3); q++)
                    if (ck[k][q][0] == i && ck[k][q][1] == j) c = add_sat(c, -24, -24, -22);
            if (look == STONE_HOT) c = add_sat(c, 6 + (int)(n >> 12 & 3), 2, -4);    /* firelight on it */
            cv_fill(cv, ox + i * P, oy + j * P, P, P, c);
        }
    }
}

/* a button: key hint in gold, label in parchment */
static int button(UI *ui, Canvas *cv, int x, int y, int h, const char *key, const char *label, UiAction a,
                  int mx, int my, bool enabled)
{
    Font *f = ui->head, *fk = ui->small;
    int pad = ui->px / 2;
    int kw = key ? font_width(fk, key) + pad : 0;
    int w = kw + font_width(f, label) + pad * 2;
    bool hov = mx >= x && my >= y && mx < x + w && my < y + h;
    stone_slab(ui, cv, x, y, w, h, str_hash(label) ^ (uint32_t)w * 31u, !enabled ? STONE_DIM : hov ? STONE_HOT : STONE_PLAIN);
    int base = y + (h + font_ascent(f) * 7 / 10) / 2;
    if (key) font_draw_shadow(fk, cv, x + pad, base, key, enabled ? GOLD : GOLD_DIM, 255);
    font_draw_shadow(f, cv, x + pad + kw, base, label, enabled ? (hov ? 0xfff2d0 : 0xe2d4b4) : 0x6a6050, 255);
    add_btn(ui, x, y, w, h, a);
    return w;
}

static int button_width(UI *ui, const char *key, const char *label)
{
    int pad = ui->px / 2;
    return (key ? font_width(ui->small, key) + pad : 0) + font_width(ui->head, label) + pad * 2;
}

/* ---------------------------------------------------------------- parts */

static void hp_bar(Canvas *cv, int x, int y, int w, int h, int hp, int max)
{
    cv_blend_rect(cv, x, y, w, h, 0x000000, 200);
    float k = max > 0 ? clampf((float)hp / max, 0, 1) : 0;
    uint32_t c = k > 0.6f ? 0x6aa84a : k > 0.3f ? 0xd0a040 : 0xc84434;
    int fw = (int)(w * k);
    cv_fill(cv, x, y, fw, h, c);
    cv_blend_rect(cv, x, y, fw, h / 3 + 1, 0xffffff, 50);
    cv_frame(cv, x - 1, y - 1, w + 2, h + 2, GOLD_DIM, 255);
}

static void status_bar(UI *ui, Canvas *cv, Game *g, const UiFrame *f, int *top)
{
    int h = font_line(ui->head) * 2;
    Font *hf = ui->head;
    int pad = ui->px;
    Mob *p = &g->player;
    char buf[64];

    /* the actions, right-aligned; measured first to see whether everything fits on one row */
    bool has_torch = g->torch_fuel > 0;
    for (int i = 0; i < g->ninv; i++) if (g->inv[i].kind == IT_TORCH) has_torch = true;
    const char *tl = g->torch_lit ? "Snuff" : "Torch";
    struct { const char *k, *l; UiAction a; bool en; } B[] = {
        { "x", "Explore", UA_EXPLORE, true }, { "Z", "Rest", UA_REST, true }, { "s", "Search", UA_SEARCH, true },
        { "t", "Throw", UA_THROW, g->ninv > 0 },
        { "i", "Items", UA_INV, true }, { "T", tl, UA_TORCH, has_torch || g->torch_lit },
        { ">", game_final(g) ? "Hearth" : "Descend", UA_STAIRS, true },
        { "m", "Map", UA_MAP, true }, { NULL, "?", UA_HELP, true },
    };
    int nb = (int)(sizeof B / sizeof *B), gap = ui->px / 2, tw_full = 0, tw_compact = 0;
    for (int i = 0; i < nb; i++) {
        tw_full += button_width(ui, B[i].k, B[i].l) + gap;
        tw_compact += button_width(ui, NULL, B[i].l) + gap;
    }
    /* a very narrow window: leave out the buttons that have another way in
     * (searching has its key; throwing is on every item's card too) */
    static const UiAction SPARE[] = { UA_SEARCH, UA_THROW };
    for (int k = 0; k < (int)(sizeof SPARE / sizeof *SPARE) && tw_compact + pad > cv->w; k++)
        for (int i = 0; i < nb; i++) {
            if (B[i].a != SPARE[k]) continue;
            tw_full -= button_width(ui, B[i].k, B[i].l) + gap;
            tw_compact -= button_width(ui, NULL, B[i].l) + gap;
            for (int j = i; j < nb - 1; j++) B[j] = B[j + 1];
            nb--;
            break;
        }
    /* the readouts on the left: health, depth, torch, embers */
    snprintf(buf, sizeof buf, "HP %d/%d", p->hp > 0 ? p->hp : 0, game_max_hp(g));
    int need = pad + font_width(hf, buf) + pad / 2 + ui->px * 5 + pad;
    snprintf(buf, sizeof buf, "DEPTH %d", g->depth);
    need += 2 * pad + font_width(hf, buf);
    if (g->torch_lit || g->torch_fuel > 0) {
        snprintf(buf, sizeof buf, "TORCH %d", g->torch_fuel);
        need += 2 * pad + font_width(hf, buf);
    }
    if (g->embers > 0 || g->heart || g->lantern || g->weapon.plus || g->armour.plus) {
        snprintf(buf, sizeof buf, "EMBERS %d", g->embers);
        need += 2 * pad + font_width(hf, buf);
    }
    if (g->warm_left > 0) {
        snprintf(buf, sizeof buf, "WARM %d", g->warm_left);
        need += 2 * pad + font_width(hf, buf);
    }
    if (game_final(g) && g->kindle > 0) {
        snprintf(buf, sizeof buf, "FLAME %d/3", g->kindle);
        need += 2 * pad + font_width(hf, buf);
    }
    if (g->snare > 0) need += 2 * pad + font_width(hf, "SNARED");
    bool two_rows = cv->w - tw_compact - pad < need + pad;     /* too narrow: buttons go on a row of their own */
    int rows = two_rows ? 2 : 1;
    int y = cv->h - h * rows;
    *top = y;
    cv_blend_rect(cv, 0, y, cv->w, h * rows, INK, 225);
    cv_fill(cv, 0, y, cv->w, 1, GOLD_DIM);
    cv_fill(cv, 0, y + 2, cv->w, 1, 0x2a2214);
    add_panel(ui, 0, y, cv->w, h * rows);
    int base = y + (h + font_ascent(hf) * 7 / 10) / 2;

    /* left: health */
    int x = pad;
    int mhp = game_max_hp(g);
    uint32_t hc = p->hp * 3 <= p->maxhp ? 0xff6a50 : g->chill ? 0xb8e0ff : g->warm_left > 0 ? 0xffc890 : 0xe2d4b4;
    snprintf(buf, sizeof buf, "HP %d/%d", p->hp > 0 ? p->hp : 0, mhp);
    x = font_draw(hf, cv, x, base, buf, hc, 255) + pad / 2;
    int bw = ui->px * 5, hbh = ui->px / 3 + 2, byy = base - font_ascent(hf) * 6 / 10;
    hp_bar(cv, x, byy, bw, hbh, p->hp, p->maxhp);
    if (g->chill) {
        /* the part of the bar the chill has taken: frosted over */
        int cx0 = x + bw * mhp / p->maxhp;
        cv_fill(cv, cx0, byy, x + bw - cx0, hbh, 0x5a8cb0);
        for (int i = cx0; i < x + bw; i += 3) cv_fill(cv, i, byy, 1, hbh, 0xc8ecff);
    }
    x += bw + pad;

    diamond(cv, x, base - font_ascent(hf) * 3 / 10, ui->px / 4, GOLD);
    x += pad;
    snprintf(buf, sizeof buf, "DEPTH %d", g->depth);
    x = font_draw(hf, cv, x, base, buf, 0xe2d4b4, 255) + pad;
    diamond(cv, x, base - font_ascent(hf) * 3 / 10, ui->px / 4, GOLD);
    x += pad;
    if (g->torch_lit || g->torch_fuel > 0) {
        snprintf(buf, sizeof buf, "TORCH %d", g->torch_fuel);
        uint32_t tc = !g->torch_lit ? 0x8a7c62 : g->torch_fuel <= 25 ? 0xff7a40 : 0xffc060;
        x = font_draw(hf, cv, x, base, buf, tc, 255) + pad;
        diamond(cv, x, base - font_ascent(hf) * 3 / 10, ui->px / 4, GOLD);
        x += pad;
    }
    if (g->embers > 0 || g->heart || g->lantern || g->weapon.plus || g->armour.plus) {
        snprintf(buf, sizeof buf, "EMBERS %d", g->embers);
        x = font_draw(hf, cv, x, base, buf, 0xff9a48, 255) + pad;
        diamond(cv, x, base - font_ascent(hf) * 3 / 10, ui->px / 4, GOLD);
        x += pad;
    }
    if (g->warm_left > 0) {
        snprintf(buf, sizeof buf, "WARM %d", g->warm_left);
        x = font_draw(hf, cv, x, base, buf, g->warm_left <= 10 ? 0xc89070 : 0xffb070, 255) + pad;
        diamond(cv, x, base - font_ascent(hf) * 3 / 10, ui->px / 4, GOLD);
        x += pad;
    }
    if (game_final(g) && g->kindle > 0) {
        snprintf(buf, sizeof buf, "FLAME %d/3", g->kindle);
        x = font_draw(hf, cv, x, base, buf, g->kindle >= 3 ? 0xfff0b0 : 0xffd060, 255) + pad;
        diamond(cv, x, base - font_ascent(hf) * 3 / 10, ui->px / 4, GOLD);
        x += pad;
    }
    if (g->snare > 0) {
        x = font_draw(hf, cv, x, base, "SNARED", 0xff7a50, 255) + pad;
        diamond(cv, x, base - font_ascent(hf) * 3 / 10, ui->px / 4, GOLD);
        x += pad;
    }
    int turn_x = x;
    snprintf(buf, sizeof buf, "TURN %d", g->turn);
    x = font_width(hf, buf) + x + pad;
    int left_end = x;

    /* right (or the row below): actions */
    int row = two_rows ? y + h : y;
    int bh = h * 64 / 100, by = row + (h - bh) / 2;
    int room = two_rows ? 0 : turn_x;               /* the turn counter goes before the key hints do */
    bool compact = cv->w - tw_full - pad < room;
    int tw = compact ? tw_compact : tw_full;     /* narrow window: drop the key hints */
    if (two_rows ? turn_x + font_width(hf, buf) <= cv->w - pad
                 : cv->w - tw - pad >= left_end || cv->w - tw - pad >= turn_x + font_width(hf, buf))
        font_draw(hf, cv, turn_x, base, buf, 0xb8aa8c, 255);
    int bx = two_rows ? (cv->w - tw) / 2 : cv->w - tw - pad / 2;
    for (int i = 0; i < nb; i++) {
        bool en = g->state == GS_PLAY && B[i].en;
        bx += button(ui, cv, bx, by, bh, compact ? NULL : B[i].k, B[i].l, B[i].a, f->mx, f->my,
                     en || B[i].a == UA_HELP || B[i].a == UA_MAP) + gap;
    }
}

static void messages(UI *ui, Canvas *cv, Game *g, int bottom)
{
    Font *bf = ui->body;
    int line = font_line(bf);
    int n = g->nmsgs < 5 ? g->nmsgs : 5;
    int x = ui->px, y = bottom - line / 3;
    for (int k = 0; k < n; k++) {
        const Msg *m = &g->msgs[(g->nmsgs - 1 - k) % MAX_MSGS];
        float fade = k == 0 ? 1.0f : 0.85f - k * 0.13f;
        if (m->age > 12) fade *= clampf(1.0f - (m->age - 12) / 8.0f, 0.35f, 1);
        float in = clampf(m->age / 0.25f, 0, 1);
        int a = (int)(255 * fade * in);
        char buf[200];
        if (m->count > 1) snprintf(buf, sizeof buf, "%s (x%d)", m->text, m->count);
        else snprintf(buf, sizeof buf, "%s", m->text);
        font_draw_shadow(bf, cv, x, y - k * line, buf, m->color, a);
    }
}

static void sidebar(UI *ui, Canvas *cv, Game *g, const UiFrame *f)
{
    Level *lv = game_level(g);
    Mob *ms = game_mobs(g, g->depth);
    int idx[MAX_MOBS], n = 0;
    for (int i = 0; i < MAX_MOBS; i++)
        if (game_mob_noticed(g, &ms[i])) idx[n++] = i;
    if (!n) return;
    Font *bf = ui->body, *sf = ui->small;
    int line = font_line(bf), pad = ui->px / 2;
    int w = ui->px * 11, h = pad * 2 + n * (line + ui->px / 2);
    int x = pad, y = pad;
    panel(ui, cv, x, y, w, h, 180, true);
    int yy = y + pad;
    for (int k = 0; k < n; k++) {
        Mob *m = &ms[idx[k]];
        bool hov = f->hover_x == m->x && f->hover_y == m->y;
        if (hov) cv_blend_rect(cv, x + 3, yy - 2, w - 6, line + ui->px / 2, 0x4a3820, 160);
        char name[48];
        bool dark = !(lv->f[m->y][m->x] & F_VIS);
        snprintf(name, sizeof name, "%s", dark ? "eyes in the dark" : MOB_INFO[m->kind].name);
        if (name[0] >= 'a' && name[0] <= 'z') name[0] = (char)(name[0] - 32);
        font_draw(bf, cv, x + pad, yy + font_ascent(bf), name, 0xe2d4b4, 255);
        if (dark) font_draw(sf, cv, x + pad + font_width(bf, name) + pad, yy + font_ascent(bf), "watching", C_WARN, 255);
        else if (m->daze > 0) font_draw(sf, cv, x + pad + font_width(bf, name) + pad, yy + font_ascent(bf), "dazzled", 0xfff0a0, 255);
        else if (m->asleep) font_draw(sf, cv, x + pad + font_width(bf, name) + pad, yy + font_ascent(bf), "asleep", C_INFO, 255);
        else if (m->hunting) font_draw(sf, cv, x + pad + font_width(bf, name) + pad, yy + font_ascent(bf), "hunting", C_WARN, 255);
        if (!dark) hp_bar(cv, x + pad, yy + line, w - pad * 2, ui->px / 5 + 1, m->hp, m->maxhp);
        yy += line + ui->px / 2;
    }
}

static void gear_text(Game *g, const Slot *sl, const char *desc, char *out, size_t n);

static void hover_card(UI *ui, Canvas *cv, Game *g, const UiFrame *f)
{
    if (f->hover_x < 0 || (!f->targeting && (f->mx < 0 || !f->tip))) return;
    Level *lv = game_level(g);
    int x = f->hover_x, y = f->hover_y;
    if (!(lv->f[y][x] & F_SEEN) && !(game_mob_at(g, x, y) && game_mob_noticed(g, game_mob_at(g, x, y)))) return;
    char title[64], desc[480], hint[96] = "";
    Mob *m = game_mob_at(g, x, y);
    if (m && game_mob_hidden(m)) m = NULL;
    Light *l = level_light_on(lv, x, y);
    Tile t = (Tile)lv->t[y][x];
    bool vis = lv->f[y][x] & F_VIS;
    if (g->player.x == x && g->player.y == y) {
        snprintf(title, sizeof title, "You");
        if (g->chill)
            snprintf(desc, sizeof desc, "%s A shade's chill has taken %d from your max HP. Stand by a fire (a lit "
                     "sconce or a torch on the floor) to warm up%s.", MOB_INFO[MK_PLAYER].desc, g->chill,
                     g->torch_lit ? "; your torch helps, slowly" : "");
        else if (g->warm_left > 0)
            snprintf(desc, sizeof desc, "%s A warming draught burns in you: for %d more turns no shade's touch can chill you.",
                     MOB_INFO[MK_PLAYER].desc, g->warm_left);
        else snprintf(desc, sizeof desc, "%s", MOB_INFO[MK_PLAYER].desc);
        if (g->snare > 0) {
            size_t dn = strlen(desc);
            snprintf(desc + dn, sizeof desc - dn, " A snare holds your ankle: each step you try is a turn spent tearing at "
                     "it (%d more). You can still fight, throw and use things.", g->snare);
        }
        if (lv->burn[y][x]) {
            size_t dn = strlen(desc);
            snprintf(desc + dn, sizeof desc - dn, " You are standing in burning oil!");
        }
        snprintf(hint, sizeof hint, t == T_STAIRS_DOWN ? "Click to descend" : t == T_STAIRS_UP ? "Click to climb" : "Click to rest a turn");
    } else if (m && vis) {
        snprintf(title, sizeof title, "%s%s", m->kind == MK_HOLLOW ? "The " : "", MOB_INFO[m->kind].name);
        if (title[0] >= 'a' && title[0] <= 'z') title[0] = (char)(title[0] - 32);
        snprintf(desc, sizeof desc, "%s%s", MOB_INFO[m->kind].desc,
                 m->daze > 0 ? " It reels, dazzled: it can't act, and a blow will find it."
                 : m->kind == MK_SHADE && game_firelight(g, g->player.x, g->player.y) > SHADE_FEAR ? " It hangs back from your light."
                 : m->asleep ? " It is asleep." : m->hunting ? " It is hunting you." : " It hasn't noticed you.");
        snprintf(hint, sizeof hint, "Click to attack");
    } else if (m && game_mob_noticed(g, m)) {
        snprintf(title, sizeof title, "Eyes in the dark");
        snprintf(desc, sizeof desc, "Two pale eyes watch you from beyond your light. Whatever owns them is darker than the dark.");
        snprintf(hint, sizeof hint, "Throw a torch to see what it is");
    } else if (lv->trap[y][x] && (lv->f[y][x] & F_TRAP)) {
        snprintf(title, sizeof title, "%s", trap_name(lv->trap[y][x]));
        title[0] = (char)(title[0] - 32);
        snprintf(desc, sizeof desc, "%s Monsters of the deep know where their traps lie and step lightly over "
                 "them, but one knocked onto it will set it off.", trap_desc(lv->trap[y][x]));
        snprintf(hint, sizeof hint, "Travel goes around it");
    } else if (lv->burn[y][x] && vis) {
        snprintf(title, sizeof title, "Burning oil");
        snprintf(desc, sizeof desc, "Oil burning on the %s, for a few turns more. It's a fixed fire: shades can't bear it, "
                 "nothing will willingly walk through it, and it sears whatever stands in it.",
                 tile_water(t) ? "water" : "stone");
        snprintf(hint, sizeof hint, "Walking in will burn you");
    } else if (t == T_HEARTH) {
        snprintf(title, sizeof title, g->kindle >= 3 ? "The Heartflame" : g->kindle ? "The Heartflame, kindling" : "The cold hearth");
        if (g->kindle >= 3)
            snprintf(desc, sizeof desc, "The Heartflame burns again, gold and white, and the deep is warm.");
        else if (g->kindle)
            snprintf(desc, sizeof desc, "%s A young flame flickers in the stone (%d of 3). Hold your torch to it again "
                     "before the dark smothers it.", tile_desc(t), g->kindle);
        else
            snprintf(desc, sizeof desc, "%s Bring fire to it: walk into the hearth with a lit torch three times, "
                     "and the Heartflame will burn again.", tile_desc(t));
        if (g->kindle < 3) snprintf(hint, sizeof hint, g->torch_lit ? "Click to kindle it" : "Light a torch to kindle it");
    } else if (t == T_BRAZIER) {
        snprintf(title, sizeof title, "Forge brazier");
        snprintf(desc, sizeof desc, "%s You have %d ember%s.", tile_desc(t), g->embers, g->embers == 1 ? "" : "s");
        snprintf(hint, sizeof hint, "Click to use the forge");
    } else if (l && l->kind == L_SCONCE) {
        snprintf(title, sizeof title, l->lit ? "Burning sconce" : "Cold sconce");
        snprintf(desc, sizeof desc, l->lit ? "A torch in an iron bracket lights the room around it. You can be seen in its light, "
                                             "but its warmth drives out a shade's chill and no shade can bear it."
                                           : "An unlit torch in an iron bracket. Light it to see the room, but anything here will "
                                             "see you too. A shade caught in the sudden light will burn.");
        if (!l->lit) snprintf(hint, sizeof hint, "Click to walk over and light it");
    } else if (l && l->kind == L_FLOORTORCH) {
        snprintf(title, sizeof title, "Burning torch");
        snprintf(desc, sizeof desc, "A torch lies burning on the floor%s, lighting everything around it.%s",
                 l->fuel >= 0 && l->fuel < 25 ? ", nearly spent" : "",
                 g->torch_lit ? "" : " Your hand is free: you can take it up and carry it.");
        if (tile_walkable(t)) snprintf(hint, sizeof hint, g->torch_lit ? "Click to travel here" : "Click to take it up");
    } else if (game_item_at(g, x, y)) {
        FloorItem *it = game_item_at(g, x, y);
        char nm[64];
        game_item_name(it->kind, it->count, it->plus, nm, sizeof nm);
        snprintf(title, sizeof title, "%s", nm);
        if (title[0] >= 'a' && title[0] <= 'z') title[0] = (char)(title[0] - 32);
        Slot sl = { it->kind, it->count, it->plus };
        gear_text(g, &sl, ITEM_INFO[it->kind].desc, desc, sizeof desc);
        snprintf(hint, sizeof hint, "Click to pick it up");
    } else if (l && l->kind == L_MOSS) {
        snprintf(title, sizeof title, "Glowmoss");
        snprintf(desc, sizeof desc, "A carpet of moss that gives off a faint green-blue light.");
        if (tile_walkable(t)) snprintf(hint, sizeof hint, "Click to travel here");
    } else if (l && l->kind == L_CRYSTAL) {
        snprintf(title, sizeof title, "Glowcrystal");
        snprintf(desc, sizeof desc, "Pale blue crystals growing from the rock, shedding a cold, steady light.");
    } else if (lv->salt[y][x]) {
        snprintf(title, sizeof title, "Salt");
        snprintf(desc, sizeof desc, "A line of coarse salt on the floor. No shade can cross it, but anything else that "
                 "walks here will scuff it away, you included.");
        if (tile_walkable(t)) snprintf(hint, sizeof hint, "Walking here breaks the line");
    } else if (lv->scorch[y][x] == 1) {
        snprintf(title, sizeof title, "Burnt-out torch");
        snprintf(desc, sizeof desc, "A charred stub in a smear of soot: all that is left of a torch that burned here.");
        if (tile_walkable(t)) snprintf(hint, sizeof hint, "Click to travel here");
    } else if (lv->scorch[y][x] == 2) {
        snprintf(title, sizeof title, "Soot");
        snprintf(desc, sizeof desc, "Greasy black soot where burning oil spread and died.");
        if (tile_walkable(t)) snprintf(hint, sizeof hint, "Click to travel here");
    } else {
        if (t == T_ROCK) return;
        snprintf(title, sizeof title, "%s", tile_name(t));
        if (title[0] >= 'a' && title[0] <= 'z') title[0] = (char)(title[0] - 32);
        snprintf(desc, sizeof desc, "%s", tile_desc(t));
        if (t == T_CHASM) snprintf(hint, sizeof hint, "Click to jump down to depth %d", g->depth + 1);
        else if (t == T_DEEP) snprintf(hint, sizeof hint, g->torch_lit ? "Click to swim here (your torch will go out)" : "Click to swim here");
        else if (tile_walkable(t)) snprintf(hint, sizeof hint, "Click to travel here");
        if (!vis) {
            size_t n = strlen(desc);
            snprintf(desc + n, sizeof desc - n, "%sYou remember this place; it is out of sight.", n ? " " : "");
        }
    }
    Font *hf = ui->head, *bf = ui->small;
    int pad = ui->px * 6 / 10;
    int w = ui->px * 16;
    int dh = font_wrap(bf, NULL, 0, 0, w - pad * 2, desc, 0, 0);
    int h = pad * 2 + font_line(hf) + dh + (hint[0] ? font_line(bf) : 0) + pad / 2;
    int bx = f->mx + ui->px, by = f->my + ui->px;
    if (bx + w > cv->w - 4) bx = f->mx - w - ui->px / 2;
    if (by + h > cv->h - font_line(ui->head) * 2 - 4) by = f->my - h - ui->px / 2;
    if (bx < 4) bx = 4;
    if (by < 4) by = 4;
    panel(ui, cv, bx, by, w, h, 225, false);
    int yy = by + pad;
    font_draw(hf, cv, bx + pad, yy + font_ascent(hf), title, GOLD, 255);
    yy += font_line(hf);
    yy += font_wrap(bf, cv, bx + pad, yy, w - pad * 2, desc, 0xd8cbb0, 255);
    if (hint[0]) font_draw(bf, cv, bx + pad, yy + font_ascent(bf) + pad / 3, hint, 0x9ec8e8, 255);
}

static void floaters(UI *ui, Canvas *cv, Game *g, const UiFrame *f)
{
    for (int i = 0; i < MAX_FLOAT; i++) {
        Floater *fl = &g->fl[i];
        if (fl->t <= 0) continue;
        float age = 1 - fl->t;
        float rise = age * TILE * 0.9f;
        int sx = (int)((fl->x - f->camx) * f->zoom), sy = (int)((fl->y - rise - f->camy) * f->zoom);
        int a = (int)(255 * clampf(fl->t * 2.5f, 0, 1));
        int w = font_width(ui->head, fl->text);
        font_draw_shadow(ui->head, cv, sx - w / 2, sy, fl->text, fl->color, a);
    }
}

static int dialog_at(UI *ui, Canvas *cv, const char *title, const char *text, int *bx, int *by, int *bh, bool high, int minw);

static int dialog(UI *ui, Canvas *cv, const char *title, const char *text, int *bx, int *by, int *bh)
{
    return dialog_at(ui, cv, title, text, bx, by, bh, false, 0);
}

/* high: up out of the way of the middle of the view, where you are */
static int dialog_at(UI *ui, Canvas *cv, const char *title, const char *text, int *bx, int *by, int *bh, bool high, int minw)
{
    Font *bf = ui->body;
    int pad = ui->px;
    int w = ui->px * 22;
    if (w < minw + pad * 2) w = minw + pad * 2;
    if (w > cv->w - 20) w = cv->w - 20;
    int th = font_wrap(bf, NULL, 0, 0, w - pad * 2, text, 0, 0);
    int h = pad * 3 + font_line(ui->big) + th + font_line(ui->head) * 2;
    int x = (cv->w - w) / 2, y = high ? ui->px / 2 : (cv->h - h) / 2 - ui->px * 2;
    if (!high) cv_blend_rect(cv, 0, 0, cv->w, cv->h, 0, 90);
    panel(ui, cv, x, y, w, h, 240, true);
    int tw = font_width(ui->big, title);
    font_draw(ui->big, cv, x + (w - tw) / 2, y + pad + font_ascent(ui->big), title, GOLD, 255);
    int yy = y + pad + font_line(ui->big) + pad / 2;
    yy += font_wrap(bf, cv, x + pad, yy, w - pad * 2, text, 0xd8cbb0, 255);
    *bh = font_line(ui->head) * 3 / 2;
    *by = y + h - pad - *bh;
    *bx = x;
    return w;
}

static void help(UI *ui, Canvas *cv, const UiFrame *f)
{
    static const char *L[][2] = {
        { "Move / attack", "arrows, numpad, or h j k l y u b n" },
        { "Travel", "click a place; click a monster to attack it" },
        { "Explore", "x  (stops when something comes into view)" },
        { "Rest", "z or . a turn,  Z until healed" },
        { "Search", "s  five turns searching the walls close by for hidden doors (resting helps a little)" },
        { "Stairs", ">  travel to and descend,  <  climb" },
        { "Items", "i  your pack: pick an item to use, wield, throw or drop it  (a use, d drop)" },
        { "Throw", "t  then aim with the mouse or keys, Tab to cycle targets" },
        { "Torch", "T  light or snuff;  walk onto a burning torch to take it up" },
        { "Sconces", "walk into a cold one (or click it) to light it and take an ember" },
        { "Shades", "your torch keeps them back; a sconce lit around one, or a thrown torch, burns it" },
        { "Chill", "a shade's touch lowers max HP until you warm up by a fire" },
        { "Gear", "anything you find you can use; each weapon and armour plays differently" },
        { "Forge", "walk into a forge brazier to spend embers on your gear or yourself" },
        { "Chasms", "step in to jump down a level (it hurts; water below breaks the fall)" },
        { "Traps", "from depth 2, hidden in the floor; good light helps you spot them, and travel avoids known ones" },
        { "The goal", "reach depth 26 and walk into its cold hearth with a lit torch, three times" },
        { "Map", "m  the whole level; click a place on it to travel there" },
        { "Water", "shallows splash; deep water puts out a torch; goblins won't swim, eels lurk" },
        { "Zoom", "+ and -  or the mouse wheel;  text size is kitty's font zoom (ctrl+shift+= / -)" },
        { "Sound", "F2 turns it off and on" },
        { "Save and quit", "Esc twice (the next start carries on)" },
    };
    int n = sizeof L / sizeof *L;
    Font *hf = ui->head, *bf = ui->body;
    int pad = ui->px, line = font_line(bf) * 12 / 10;
    int kw = 0, vw = 0;
    for (int i = 0; i < n; i++) {
        int a = font_width(hf, L[i][0]), b = font_width(bf, L[i][1]);
        if (a > kw) kw = a;
        if (b > vw) vw = b;
    }
    int extra = pad * 3 + font_line(ui->big) + font_line(hf) * 2;
    /* close up the rows in a short window rather than run off the screen */
    if (extra + n * line > cv->h - 8) line = (cv->h - 8 - extra) / n;
    if (line < font_line(bf)) line = font_line(bf);
    int w = kw + vw + pad * 4, h = extra + n * line;
    if (w > cv->w - 10) w = cv->w - 10;
    int x = (cv->w - w) / 2, y = (cv->h - h) / 2;
    cv_blend_rect(cv, 0, 0, cv->w, cv->h, 0, 110);
    panel(ui, cv, x, y, w, h, 240, true);
    const char *title = "How to play";
    font_draw(ui->big, cv, x + (w - font_width(ui->big, title)) / 2, y + pad + font_ascent(ui->big), title, GOLD, 255);
    int yy = y + pad * 2 + font_line(ui->big);
    for (int i = 0; i < n; i++) {
        font_draw(hf, cv, x + pad, yy + font_ascent(bf), L[i][0], GOLD, 255);
        font_draw(bf, cv, x + pad * 2 + kw, yy + font_ascent(bf), L[i][1], 0xd8cbb0, 255);
        yy += line;
    }
    int bh = font_line(hf) * 3 / 2;
    int bw = button_width(ui, "Esc", "Close");
    button(ui, cv, x + (w - bw) / 2, y + h - pad - bh, bh, "Esc", "Close", UA_CLOSE, f->mx, f->my, true);
}

static int icon_scale(const UI *ui)
{
    int s = (font_line(ui->body) * 13 / 10 + TILE / 2) / TILE;
    return s < 1 ? 1 : s;
}

/* an item's description, with its numbers (and yours to compare) for gear */
static void gear_text(Game *g, const Slot *sl, const char *desc, char *out, size_t n)
{
    const ItemInfo *ii = &ITEM_INFO[sl->kind];
    if (ii->gear == GEAR_WEAPON) {
        int a, lo, hi, ca, clo, chi;
        game_weapon_stats(g, sl, &a, &lo, &hi);
        game_weapon_stats(g, &g->weapon, &ca, &clo, &chi);
        snprintf(out, n, "%s Hit %d, damage %d-%d (yours: %d, %d-%d).", desc, a, lo, hi, ca, clo, chi);
    } else if (ii->gear == GEAR_ARMOUR) {
        snprintf(out, n, "%s Armour %d (yours: %d).", desc, game_armour_def(sl), game_armour_def(&g->armour));
    } else {
        snprintf(out, n, "%s", desc);
    }
}

static const char *const FORGE_KEYS = "abcd";

static void forge_panel(UI *ui, Canvas *cv, Game *g, const UiFrame *f, int bottom)
{
    Font *hf = ui->head, *bf = ui->body, *sf = ui->small;
    int pad = ui->px * 7 / 10, w = ui->px * 19;
    int indent = font_width(sf, "a)") + ui->px / 3, rows[FORGE_COUNT], sum = 0;
    for (int o = 0; o < FORGE_COUNT; o++) {
        rows[o] = font_line(bf) + font_wrap(sf, NULL, 0, 0, w - indent - pad * 3, game_forge_desc((ForgeOffer)o), 0, 0) + pad / 2;
        sum += rows[o];
    }
    int h = pad * 2 + font_line(hf) + font_line(sf) + sum + font_line(sf) + pad;
    int x = cv->w - w - ui->px / 2, y = bottom - h - ui->px / 2;
    if (y < 4) y = 4;
    panel(ui, cv, x, y, w, h, 240, true);
    font_draw(hf, cv, x + pad, y + pad + font_ascent(hf), "The forge", GOLD, 255);
    char buf[160];
    snprintf(buf, sizeof buf, "%d ember%s", g->embers, g->embers == 1 ? "" : "s");
    font_draw(hf, cv, x + w - pad - font_width(hf, buf), y + pad + font_ascent(hf), buf, 0xff9a48, 255);
    int yy = y + pad + font_line(hf);
    font_draw(sf, cv, x + pad, yy + font_ascent(sf), "Feed it embers from lit sconces and burnt-out shades.", 0x8a7c62, 255);
    yy += font_line(sf) + pad / 2;
    for (int o = 0; o < FORGE_COUNT; o++) {
        int row = rows[o];
        int cost = game_forge_cost(g, (ForgeOffer)o);
        bool ok = cost >= 0 && g->embers >= cost;
        bool hov = ok && f->mx >= x && f->mx < x + w && f->my >= yy && f->my < yy + row;
        if (hov) cv_blend_rect(cv, x + 3, yy, w - 6, row, 0x4a3820, 170);
        int base = yy + font_ascent(bf) + pad / 4;
        char key[4] = { FORGE_KEYS[o], ')', 0 };
        int tx = font_draw(sf, cv, x + pad, base, key, ok ? GOLD : GOLD_DIM, 255) + ui->px / 3;
        game_forge_label(g, (ForgeOffer)o, buf, sizeof buf);
        font_draw(bf, cv, tx, base, buf, ok ? (hov ? 0xfff2d0 : 0xe2d4b4) : 0x7a7060, 255);
        if (cost >= 0) {
            char c[24];
            snprintf(c, sizeof c, "%d", cost);
            font_draw(bf, cv, x + w - pad - font_width(bf, c), base, c, ok ? 0xff9a48 : 0x7a5a40, 255);
        }
        font_wrap(sf, cv, tx, yy + font_line(bf) + pad / 6, w - indent - pad * 3, game_forge_desc((ForgeOffer)o),
                  ok ? 0xb8aa8c : 0x6a6050, 255);
        if (ok) add_btn_arg(ui, x, yy, w, row, UA_FORGE, o);
        yy += row;
    }
    font_draw(sf, cv, x + pad, y + h - pad / 2 - font_line(sf) / 4, "Pick with a letter or the mouse, Esc closes", 0x8a7c62, 255);
}

static void menu_panel(UI *ui, Canvas *cv, Game *g, const UiFrame *f, int bottom)
{
    static const char *TITLES[] = { "", "Pack", "Throw what?", "Use what?", "Drop what?", "", "" };
    Font *hf = ui->head, *bf = ui->body, *sf = ui->small;
    int pad = ui->px * 7 / 10, s = icon_scale(ui), row = TILE * s + ui->px / 4;
    int w = ui->px * 17;
    if (f->menu == MENU_FORGE) {
        forge_panel(ui, cv, g, f, bottom);
        return;
    }
    if (f->menu == MENU_ITEM && f->menu_slot >= 0 && f->menu_slot < g->ninv) {
        Slot *sl = &g->inv[f->menu_slot];
        const ItemInfo *ii = &ITEM_INFO[sl->kind];
        char gdesc[480];
        gear_text(g, sl, ii->desc, gdesc, sizeof gdesc);
        int dh = font_wrap(sf, NULL, 0, 0, w - pad * 2, gdesc, 0, 0);
        int bh = font_line(hf) * 3 / 2;
        int h = pad * 3 + TILE * s * 3 / 2 + dh + bh + pad;
        int x = cv->w - w - ui->px / 2, y = bottom - h - ui->px / 2;
        panel(ui, cv, x, y, w, h, 235, true);
        int big = s * 3 / 2 > s ? s * 3 / 2 : s + 1;
        item_icon(ui, cv, sl->kind, x + pad, y + pad, big, 255);
        char nm[64];
        game_item_name(sl->kind, sl->count, sl->plus, nm, sizeof nm);
        char title[80];
        snprintf(title, sizeof title, "%c) %s", 'a' + f->menu_slot, nm);
        font_draw(hf, cv, x + pad * 2 + TILE * big, y + pad + TILE * big / 2 + font_ascent(hf) / 3, title, GOLD, 255);
        int yy = y + pad + TILE * s * 3 / 2 + pad / 2;
        yy += font_wrap(sf, cv, x + pad, yy, w - pad * 2, gdesc, 0xd8cbb0, 255);
        int bx = x + pad, by = y + h - pad - bh;
        if (sl->kind == IT_TORCH) bx += button(ui, cv, bx, by, bh, "a", g->torch_lit ? "Fresh" : "Light", UA_ITEM_USE, f->mx, f->my, true) + pad / 2;
        if (sl->kind == IT_POTION || sl->kind == IT_WARMTH)
            bx += button(ui, cv, bx, by, bh, "q", "Drink", UA_ITEM_USE, f->mx, f->my, true) + pad / 2;
        if (sl->kind == IT_OIL) bx += button(ui, cv, bx, by, bh, "a", "Soak torch", UA_ITEM_USE, f->mx, f->my, g->torch_fuel > 0) + pad / 2;
        if (sl->kind == IT_SALT) bx += button(ui, cv, bx, by, bh, "a", "Pour", UA_ITEM_USE, f->mx, f->my, true) + pad / 2;
        if (ii->gear != GEAR_NONE)
            bx += button(ui, cv, bx, by, bh, "a", ii->gear == GEAR_WEAPON ? "Wield" : "Wear", UA_ITEM_USE, f->mx, f->my, true) + pad / 2;
        if (ii->range > 0) bx += button(ui, cv, bx, by, bh, "t", "Throw", UA_ITEM_THROW, f->mx, f->my, true) + pad / 2;
        button(ui, cv, bx, by, bh, "d", "Drop", UA_ITEM_DROP, f->mx, f->my, true);
        return;
    }
    int n = g->ninv > 0 ? g->ninv : 1;
    bool worn = f->menu == MENU_INV;
    int wl = font_line(sf);
    int h = pad * 2 + font_line(hf) + n * row + font_line(sf) + pad / 2 + (worn ? wl * 2 + pad / 2 : 0);
    int x = cv->w - w - ui->px / 2, y = bottom - h - ui->px / 2;
    if (y < 4) y = 4;
    panel(ui, cv, x, y, w, h, 235, true);
    font_draw(hf, cv, x + pad, y + pad + font_ascent(hf), TITLES[f->menu], GOLD, 255);
    int yy = y + pad + font_line(hf);
    if (worn) {
        char a[64], line[128];
        int acc, dmin, dmax;
        game_weapon_stats(g, &g->weapon, &acc, &dmin, &dmax);
        game_item_name(g->weapon.kind, 1, g->weapon.plus, a, sizeof a);
        snprintf(line, sizeof line, "Wielding %s  (hit %d, damage %d-%d)", a, acc, dmin, dmax);
        font_draw(sf, cv, x + pad, yy + font_ascent(sf), line, 0xc8b890, 255);
        yy += wl;
        game_item_name(g->armour.kind, 1, g->armour.plus, a, sizeof a);
        snprintf(line, sizeof line, "Wearing %s  (armour %d)", a, game_armour_def(&g->armour));
        font_draw(sf, cv, x + pad, yy + font_ascent(sf), line, 0xc8b890, 255);
        yy += wl + pad / 2;
    }
    if (!g->ninv) font_draw(bf, cv, x + pad, yy + font_ascent(bf), "Your pack is empty.", C_DIM, 255);
    for (int i = 0; i < g->ninv; i++) {
        Slot *sl = &g->inv[i];
        bool ok = menu_allows(f->menu, sl->kind);
        bool hov = ok && f->mx >= x && f->mx < x + w && f->my >= yy && f->my < yy + row;
        if (hov) cv_blend_rect(cv, x + 3, yy, w - 6, row, 0x4a3820, 170);
        item_icon(ui, cv, sl->kind, x + pad, yy + ui->px / 8, s, ok ? 255 : 90);
        char nm[64], line[96];
        game_item_name(sl->kind, sl->count, sl->plus, nm, sizeof nm);
        snprintf(line, sizeof line, "%s", nm);
        int base = yy + row / 2 + font_ascent(bf) / 3;
        int tx = x + pad * 2 + TILE * s;
        char key[4] = { (char)('a' + i), ')', 0 };
        tx = font_draw(sf, cv, tx, base, key, ok ? GOLD : GOLD_DIM, 255) + ui->px / 3;
        font_draw(bf, cv, tx, base, line, ok ? (hov ? 0xfff2d0 : 0xe2d4b4) : 0x6a6050, 255);
        if (sl->kind == IT_TORCH && g->torch_lit && f->menu == MENU_INV)
            font_draw(sf, cv, x + w - pad - font_width(sf, "one burning"), base, "one burning", 0xffb050, 255);
        if (ok) add_btn_arg(ui, x, yy, w, row, UA_PICK, i);
        yy += row;
    }
    const char *foot = f->menu == MENU_INV ? "Pick an item, Esc closes" : "Pick with a letter or the mouse";
    font_draw(sf, cv, x + pad, y + h - pad / 2 - font_line(sf) / 4, foot, 0x8a7c62, 255);
}

static void targeting_banner(UI *ui, Canvas *cv, Game *g, const UiFrame *f)
{
    if (!f->targeting || f->target_slot < 0 || f->target_slot >= g->ninv) return;
    char buf[160], nm[64];
    game_item_name(g->inv[f->target_slot].kind, 1, 0, nm, sizeof nm);
    snprintf(buf, sizeof buf, "Throw %s: click where, Tab cycles targets, Esc cancels", nm);
    Font *hf = ui->head;
    int w = font_width(hf, buf) + ui->px * 2, h = font_line(hf) * 3 / 2;
    int x = (cv->w - w) / 2, y = ui->px / 2;
    panel(ui, cv, x, y, w, h, 210, false);
    font_draw(hf, cv, x + ui->px, y + (h + font_ascent(hf) * 7 / 10) / 2, buf, 0xffe0a0, 255);
}

/* ---------------------------------------------------------------- draw */


/* ------------------------------------------------------------------ map */

void ui_map_layout(const UI *ui, Game *g, int sw, int sh, MapLayout *m)
{
    Level *lv = game_level(g);
    int x0 = MAP_W, y0 = MAP_H, x1 = -1, y1 = -1;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            if (lv->f[y][x] & F_SEEN) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    if (x1 < 0) { x0 = g->player.x; x1 = x0; y0 = g->player.y; y1 = y0; }
    x0 -= 1; y0 -= 1; x1 += 1; y1 += 1;
    /* not too zoomed in on a small explored patch */
    int tw = x1 - x0 + 1, th = y1 - y0 + 1;
    int mw = MAP_W / 2, mh = MAP_H / 2;
    if (tw < mw) { x0 -= (mw - tw) / 2; tw = mw; }
    if (th < mh) { y0 -= (mh - th) / 2; th = mh; }
    x0 = clampi(x0, 0, MAP_W - tw);
    y0 = clampi(y0, 0, MAP_H - th);
    int top = ui->px * 4, bottom = font_line(ui->head) * 2 + ui->px * 4, side = ui->px * 2;
    float aw = (float)(sw - side * 2), ah = (float)(sh - top - bottom);
    float cell = fminf(aw / tw, ah / th);
    float cap = fmaxf(12.0f, sh / 28.0f);
    if (cell > cap) cell = cap;
    if (cell < 2) cell = 2;
    m->tx = x0; m->ty = y0; m->tw = tw; m->th = th;
    m->cell = cell;
    m->w = (int)(cell * tw);
    m->h = (int)(cell * th);
    m->x = (sw - m->w) / 2;
    m->y = top + ((int)ah - m->h) / 2;
}

static void map_view(UI *ui, Canvas *cv, Game *g, const UiFrame *f)
{
    const MapLayout *m = &f->ml;
    Level *lv = game_level(g);
    int pad = ui->px;
    cv_blend_rect(cv, 0, 0, cv->w, cv->h, 0, 170);
    int th = font_line(ui->head) + pad;
    panel(ui, cv, m->x - pad, m->y - pad - th, m->w + pad * 2, m->h + pad * 2 + th + font_line(ui->body) + pad / 2, 245, true);
    char title[64];
    snprintf(title, sizeof title, "Map of depth %d", g->depth);
    font_draw(ui->head, cv, m->x + (m->w - font_width(ui->head, title)) / 2, m->y - pad / 2 - font_line(ui->head) / 3, title, GOLD, 255);
    if (f->mini && f->mini->px) {
        for (int y = 0; y < f->mini->h && m->y + y < cv->h; y++) {
            if (m->y + y < 0) continue;
            int n = f->mini->w;
            if (m->x + n > cv->w) n = cv->w - m->x;
            if (n > 0 && m->x >= 0) memcpy(cv->px + (size_t)(m->y + y) * cv->w + m->x, f->mini->px + (size_t)y * f->mini->w, (size_t)n * 4);
        }
    }
    cv_frame(cv, m->x - 1, m->y - 1, m->w + 2, m->h + 2, GOLD_DIM, 255);
    float c = m->cell;
    #define MX(X_) (m->x + ((X_) - m->tx) * c + c / 2)
    #define MY(Y_) (m->y + ((Y_) - m->ty) * c + c / 2)
    /* stairs */
    float dr = fmaxf(3, c * 0.45f);
    if (lv->f[lv->downy][lv->downx] & F_SEEN) {
        cv_disc(cv, MX(lv->downx), MY(lv->downy), dr + 2, 0, 200);
        cv_disc(cv, MX(lv->downx), MY(lv->downy), dr, GOLD, 255);
        cv_disc(cv, MX(lv->downx), MY(lv->downy), dr * 0.45f, 0x3a2810, 255);
    }
    if (lv->f[lv->upy][lv->upx] & F_SEEN) cv_ring(cv, MX(lv->upx), MY(lv->upy), dr, 1.6f, GOLD_DIM, 230);
    /* creatures you can make out */
    Mob *ms = game_mobs(g, g->depth);
    for (int i = 0; i < MAX_MOBS; i++) {
        if (!game_mob_noticed(g, &ms[i])) continue;
        bool eyes = !(lv->f[ms[i].y][ms[i].x] & F_VIS);
        cv_disc(cv, MX(ms[i].x), MY(ms[i].y), fmaxf(2.5f, c * 0.32f) + 1.5f, 0, 220);
        cv_disc(cv, MX(ms[i].x), MY(ms[i].y), fmaxf(2.5f, c * 0.32f), eyes ? 0xa8d0ff : 0xe04a3a, 255);
    }
    /* you, pulsing */
    float pulse = 0.5f + 0.5f * sinf(g->time * 4);
    float px = MX(g->player.x), py = MY(g->player.y);
    cv_ring(cv, px, py, c * 0.9f + 3 + pulse * c * 0.4f, 2.0f, 0xfff0c0, (int)(120 + 120 * (1 - pulse)));
    cv_disc(cv, px, py, fmaxf(3, c * 0.38f) + 1.5f, 0, 220);
    cv_disc(cv, px, py, fmaxf(3, c * 0.38f), 0xfff0c0, 255);
    /* the place under the mouse */
    const char *what = "Click a place to travel there.   m or Esc closes the map.";
    char buf[160];
    if (f->mx >= m->x && f->my >= m->y && f->mx < m->x + m->w && f->my < m->y + m->h) {
        int tx = m->tx + (int)((f->mx - m->x) / c), ty = m->ty + (int)((f->my - m->y) / c);
        if (in_map(tx, ty) && (lv->f[ty][tx] & F_SEEN)) {
            cv_frame(cv, (int)(m->x + (tx - m->tx) * c), (int)(m->y + (ty - m->ty) * c), (int)c + 1, (int)c + 1, 0xfff0c0, 230);
            Mob *mm = game_mob_at(g, tx, ty);
            const char *nm = tile_name((Tile)lv->t[ty][tx]);
            if (g->player.x == tx && g->player.y == ty) nm = "you";
            else if (mm && !game_mob_hidden(mm) && (lv->f[ty][tx] & F_VIS)) nm = MOB_INFO[mm->kind].name;
            else if (mm && game_mob_noticed(g, mm)) nm = "eyes in the dark";
            else if (tx == lv->downx && ty == lv->downy) nm = "stairs down";
            snprintf(buf, sizeof buf, "%c%s  \xc2\xb7  click to travel there", nm[0] >= 'a' && nm[0] <= 'z' ? nm[0] - 32 : nm[0], nm + 1);
            what = buf;
        }
    }
    font_draw(ui->body, cv, m->x + (m->w - font_width(ui->body, what)) / 2, m->y + m->h + pad / 2 + font_ascent(ui->body), what, 0xd8cbb0, 255);
    #undef MX
    #undef MY
}

void ui_draw(UI *ui, Game *g, Canvas *cv, const UiFrame *f)
{
    ui->nbtn = 0;
    ui->npanel = 0;
    float dt = g->time - ui->last_time;
    ui->last_time = g->time;
    if (ui->banner_depth != g->depth) {
        ui->banner_depth = g->depth;
        ui->banner_t = 3.0f;
    }
    if (ui->banner_t > 0) ui->banner_t -= dt;

    floaters(ui, cv, g, f);
    int bar_top;
    status_bar(ui, cv, g, f, &bar_top);
    messages(ui, cv, g, bar_top - ui->px / 3);
    sidebar(ui, cv, g, f);

    /* depth banner */
    if (ui->banner_t > 0) {
        float a = clampf(ui->banner_t > 2.4f ? (3.0f - ui->banner_t) / 0.6f : ui->banner_t / 1.2f, 0, 1);
        char buf[48];
        snprintf(buf, sizeof buf, "Depth %d", g->depth);
        int w = font_width(ui->big, buf);
        int y = cv->h / 5;
        font_draw_shadow(ui->big, cv, (cv->w - w) / 2, y, buf, GOLD, (int)(255 * a));
        cv_blend_rect(cv, (cv->w - w * 3 / 2) / 2, y + ui->px / 2, w * 3 / 2, 1, GOLD, (int)(200 * a));
    }

    /* version, top right */
    char ver[48];
    snprintf(ver, sizeof ver, "Shadesdeep %s", SHADESDEEP_VERSION);
    int vw = font_width(ui->small, ver);
    font_draw_shadow(ui->small, cv, cv->w - vw - ui->px / 2, font_ascent(ui->small) + ui->px / 3, ver, 0x8a7c62, 200);
    if (f->show_fps) {
        char fb[32];
        snprintf(fb, sizeof fb, "%.0f fps", f->fps);
        font_draw_shadow(ui->small, cv, cv->w - font_width(ui->small, fb) - ui->px / 2,
                         font_ascent(ui->small) + font_line(ui->small) + ui->px / 3, fb, 0x8a7c62, 200);
    }
    if (f->quit_armed) {
        const char *q = "Press Esc again to save and quit";
        int w = font_width(ui->head, q);
        font_draw_shadow(ui->head, cv, (cv->w - w) / 2, cv->h / 2, q, C_WARN, 255);
    }

    if (g->state == GS_JUMP_PROMPT) {
        char text[200];
        snprintf(text, sizeof text,
                 "The floor ends in a sheer drop. You can see depth %d far below. "
                 "Jumping is quick, but the fall will hurt.", g->depth + 1);
        int bx, by, bh;
        int w = dialog(ui, cv, "Leap into the chasm?", text, &bx, &by, &bh);
        int w1 = button_width(ui, "y", "Jump"), w2 = button_width(ui, "n", "Stay");
        int gap = ui->px;
        int x0 = bx + (w - w1 - w2 - gap) / 2;
        button(ui, cv, x0, by, bh, "y", "Jump", UA_JUMP_YES, f->mx, f->my, true);
        button(ui, cv, x0 + w1 + gap, by, bh, "n", "Stay", UA_JUMP_NO, f->mx, f->my, true);
    } else if (game_end_shown(g)) {
        char text[400];
        bool won = g->state == GS_WON;
        int n = won ? snprintf(text, sizeof text, "You carried fire down through all %d levels and rekindled the Heartflame, in %d turns.",
                               MAX_DEPTH, g->turn)
                    : snprintf(text, sizeof text, "You fell on depth %d after %d turns.", g->depth, g->turn);
        if (!won && g->deepest > g->depth && n < (int)sizeof text)
            n += snprintf(text + n, sizeof text - n, " You had been as deep as %d.", g->deepest);
        if (n < (int)sizeof text)
            n += snprintf(text + n, sizeof text - n, " You slew %d foe%s, gathered %d ember%s and lit %d sconce%s along the way.",
                     g->kills, g->kills == 1 ? "" : "s", g->embers_got, g->embers_got == 1 ? "" : "s",
                     g->sconces_lit, g->sconces_lit == 1 ? "" : "s");
        if (f->fame_rank > 0 && n < (int)sizeof text) {
            if (f->fame_rank == 1 && f->fame_total > 1)
                snprintf(text + n, sizeof text - n, " It is the finest run in the hall of fame.");
            else if (f->fame_total > 1)
                snprintf(text + n, sizeof text - n, " It stands #%d of %d in the hall of fame.", f->fame_rank, f->fame_total);
            else
                snprintf(text + n, sizeof text - n, " It is the first run in the hall of fame.");
        }
        static const struct { const char *k, *l; UiAction a; } B[] = {
            { "Enter", "New game", UA_NEW_GAME }, { "h", "Hall of fame", UA_FAME },
            { "Esc", "Title", UA_TITLE }, { "q", "Quit", UA_QUIT },
        };
        int gap = ui->px, bw[4], tot = -gap;
        for (int i = 0; i < 4; i++) tot += (bw[i] = button_width(ui, B[i].k, B[i].l)) + gap;
        int bx, by, bh;
        int w = dialog_at(ui, cv, won ? "The Heartflame burns again" : "You have perished", text, &bx, &by, &bh, won, tot);
        int x0 = bx + (w - tot) / 2;
        for (int i = 0; i < 4; i++) {
            button(ui, cv, x0, by, bh, B[i].k, B[i].l, B[i].a, f->mx, f->my, true);
            x0 += bw[i] + gap;
        }
    } else if (f->help) {
        help(ui, cv, f);
    } else if (f->map) {
        map_view(ui, cv, g, f);
    } else {
        if (f->menu != MENU_NONE) menu_panel(ui, cv, g, f, bar_top);
        targeting_banner(ui, cv, g, f);
        if (f->menu == MENU_NONE) hover_card(ui, cv, g, f);
    }
}

/* ------------------------------------------------------- title screen */

void ui_set_title_art(UI *ui, const Image *art)
{
    ui->title_art = art && art->px ? art : NULL;
    ui->title_cv.w = ui->title_cv.h = 0;     /* rescale on the next frame */
}

/* a wide menu button with the label centred; hot = highlighted from the keyboard */
static void menu_button(UI *ui, Canvas *cv, int x, int y, int w, int h, const char *key, const char *label,
                        UiAction a, int mx, int my, bool hot)
{
    bool hov = hot || (mx >= x && my >= y && mx < x + w && my < y + h);
    stone_slab(ui, cv, x, y, w, h, str_hash(label) ^ 0x5eed, hov ? STONE_HOT : STONE_PLAIN);
    int base = y + (h + font_ascent(ui->head) * 7 / 10) / 2;
    int lw = font_width(ui->head, label);
    font_draw_shadow(ui->head, cv, x + (w - lw) / 2, base, label, hov ? 0xfff2d0 : 0xe2d4b4, 255);
    if (key) font_draw_shadow(ui->small, cv, x + ui->px, base, key, hov ? GOLD : GOLD_DIM, 255);
    if (hov) {
        diamond(cv, x + (w - lw) / 2 - ui->px * 3 / 4, y + h / 2, ui->px / 5 + 1, GOLD);
        diamond(cv, x + (w + lw) / 2 + ui->px * 3 / 4, y + h / 2, ui->px / 5 + 1, GOLD);
    }
    add_btn(ui, x, y, w, h, a);
}

/* sparks rising from below, as off a fire */
static void embers(UI *ui, Canvas *cv, float dt, float rate)
{
    int n = (int)(sizeof ui->ember / sizeof ui->ember[0]);
    for (int i = 0; i < n; i++) {
        __typeof__(&ui->ember[0]) e = &ui->ember[i];
        if (e->life <= 0) {
            if ((float)rand() / RAND_MAX > rate * dt * 60) continue;
            e->x = cv->w * (0.15f + 0.7f * (float)rand() / RAND_MAX);
            e->y = cv->h + 4;
            e->vx = ((float)rand() / RAND_MAX - 0.5f) * cv->h * 0.04f;
            e->vy = -cv->h * (0.06f + 0.10f * (float)rand() / RAND_MAX);
            e->max = e->life = 3.5f + 4.5f * (float)rand() / RAND_MAX;
            e->r = 1.0f + 2.2f * (float)rand() / RAND_MAX * (cv->h / 700.0f);
            continue;
        }
        e->life -= dt;
        float age = e->max - e->life;
        e->x += (e->vx + sinf(age * 1.7f + i) * cv->h * 0.015f) * dt;
        e->y += e->vy * dt;
        if (e->y < -10) { e->life = 0; continue; }
        float k = clampf(e->life / e->max, 0, 1);
        float tw = 0.75f + 0.25f * sinf(age * 9 + i * 1.3f);
        uint32_t c = k > 0.6f ? 0xffd27a : k > 0.3f ? 0xff9a3a : 0xc8501e;
        cv_glow(cv, (int)e->x, (int)e->y, (int)(e->r * 6) + 2, c, 0.35f * k * tw);
        cv_disc(cv, e->x, e->y, e->r, c, (int)(255 * k * tw));
    }
}

/* the picture behind the title menu: the title art, or a fire in the dark. The
 * still part is drawn once per window size; only the flicker and the sparks move. */
static void title_backdrop(UI *ui, Canvas *cv, float dt)
{
    ui->title_t += dt;
    float t = ui->title_t;
    Canvas *bg = &ui->title_cv;
    if (bg->w != cv->w || bg->h != cv->h || !bg->px) {
        free(bg->px);
        bg->px = malloc(sizeof(uint32_t) * (size_t)cv->w * cv->h);
        bg->w = bg->px ? cv->w : 0;
        bg->h = bg->px ? cv->h : 0;
        if (bg->px && ui->title_art) {
            cv_cover(bg, ui->title_art);
            /* darken towards the bottom, where the menu sits */
            for (int y = bg->h / 2; y < bg->h; y++)
                cv_blend_rect(bg, 0, y, bg->w, 1, 0x000000, 170 * (y - bg->h / 2) / (bg->h / 2 + 1));
        } else if (bg->px) {
            for (int y = 0; y < bg->h; y++)
                cv_fill(bg, 0, y, bg->w, 1, mix(0x040302, 0x140b06, 256 * y / (bg->h > 1 ? bg->h - 1 : 1)));
            int cx = bg->w / 2, by = bg->h + bg->h / 6;
            cv_glow(bg, cx, by, bg->h * 3 / 4, 0xa0461a, 0.47f);
            cv_glow(bg, cx, by, bg->h / 2, 0xff8a30, 0.30f);
            cv_glow(bg, cx, bg->h * 3 / 10, bg->h / 3, 0x3a2410, 0.25f);
        }
    }
    if (bg->px) memcpy(cv->px, bg->px, sizeof(uint32_t) * (size_t)cv->w * cv->h);
    else cv_fill(cv, 0, 0, cv->w, cv->h, 0x040302);
    if (!ui->title_art) {
        float fl = 0.5f + 0.25f * sinf(t * 7.3f) + 0.15f * sinf(t * 13.1f + 1) + 0.1f * sinf(t * 23.7f);
        cv_glow(cv, cv->w / 2, cv->h + cv->h / 10, cv->h / 4, 0xff7a28, 0.16f * fl);
    }
    embers(ui, cv, dt, ui->title_art ? 0.25f : 0.45f);
}

int ui_title_menu(const TitleFrame *t, UiAction *out, int max)
{
    int n = 0;
    if (t->can_continue && n < max) out[n++] = UA_CONTINUE;
    if (n < max) out[n++] = UA_NEW_GAME;
    if (n < max) out[n++] = UA_FAME;
    if (n < max) out[n++] = UA_QUIT;
    return n;
}

void ui_draw_title(UI *ui, Canvas *cv, const TitleFrame *t)
{
    ui->nbtn = 0;
    ui->npanel = 0;
    title_backdrop(ui, cv, t->dt);
    int px = ui->px;

    UiAction items[4];
    int n = ui_title_menu(t, items, 4);
    char cont[64];
    snprintf(cont, sizeof cont, "Continue (depth %d)", t->save_depth);
    const char *label[4], *key[4];
    for (int i = 0; i < n; i++) {
        switch (items[i]) {
        case UA_CONTINUE: label[i] = cont; key[i] = "c"; break;
        case UA_NEW_GAME: label[i] = "New game"; key[i] = "n"; break;
        case UA_FAME:     label[i] = "Hall of fame"; key[i] = "h"; break;
        default:          label[i] = "Quit"; key[i] = "q"; break;
        }
    }
    int bw = px * 13, bh = font_line(ui->head) * 2, gap = px * 2 / 3;
    for (int i = 0; i < n; i++) {
        int w = font_width(ui->head, label[i]) + px * 5;
        if (w > bw) bw = w;
    }
    if (bw > cv->w - 20) bw = cv->w - 20;
    int total = n * bh + (n - 1) * gap;
    if (total > cv->h - px * 2) {           /* a very short window: pack the menu tighter */
        bh = font_line(ui->head) * 3 / 2;
        gap = px / 4;
        total = n * bh + (n - 1) * gap;
    }

    /* the name, unless the picture carries it, or there's no room for it above the menu */
    int menu_top = -1;
    const char *name = "Shadesdeep";
    int nw = font_width(ui->title, name), ny = cv->h * 34 / 100;
    if (!ui->title_art && nw < cv->w - px * 2 && ny + px * 5 + total <= cv->h - px * 2
        && ny - font_ascent(ui->title) >= 0) {
        cv_glow(cv, cv->w / 2, ny - font_ascent(ui->title) / 3, nw * 2 / 3, 0x5a3412, 0.3f);
        font_draw_shadow(ui->title, cv, (cv->w - nw) / 2, ny, name, GOLD, 255);
        int rw = nw * 11 / 10, ry = ny + px;
        cv_blend_rect(cv, (cv->w - rw) / 2, ry, rw, 1, GOLD, 200);
        diamond(cv, cv->w / 2, ry, px / 3 + 1, GOLD);
        const char *sub = "Carry fire down into the dark, and rekindle the Heartflame";
        int sw = font_width(ui->body, sub);
        if (sw < cv->w - px * 2) font_draw_shadow(ui->body, cv, (cv->w - sw) / 2, ry + px * 2, sub, 0xcbb894, 230);
        menu_top = ry + px * 4;
    }
    if (menu_top < 0 || menu_top + total > cv->h - px * 3) menu_top = cv->h - px * 3 - total;
    if (menu_top < px) menu_top = (cv->h - total) / 2;
    int bx = (cv->w - bw) / 2;
    if (!t->confirm_new)
        for (int i = 0; i < n; i++)
            menu_button(ui, cv, bx, menu_top + i * (bh + gap), bw, bh, key[i], label[i], items[i], t->mx, t->my,
                        i == t->sel);

    char ver[48];
    snprintf(ver, sizeof ver, "%s", SHADESDEEP_VERSION);
    int vw = font_width(ui->small, ver);
    font_draw_shadow(ui->small, cv, cv->w - vw - px / 2, cv->h - px / 2, ver, GOLD_DIM, 255);

    if (t->confirm_new) {
        char text[200];
        snprintf(text, sizeof text, "You have a game saved on depth %d. Starting afresh gives it up for good: "
                 "it goes into the hall of fame as it stands.", t->save_depth);
        int x, y, h;
        int w1 = button_width(ui, "y", "Give it up"), w2 = button_width(ui, "n", "Keep it");
        int w = dialog(ui, cv, "Abandon your game?", text, &x, &y, &h);
        int x0 = x + (w - w1 - w2 - px) / 2;
        button(ui, cv, x0, y, h, "y", "Give it up", UA_YES, t->mx, t->my, true);
        button(ui, cv, x0 + w1 + px, y, h, "n", "Keep it", UA_NO, t->mx, t->my, true);
    }
}

/* ------------------------------------------------------- hall of fame */

/* draw s in at most maxw pixels, shortening it with an ellipsis if need be */
static void draw_fit(Font *f, Canvas *cv, int x, int y, int maxw, const char *s, uint32_t c, int a)
{
    if (font_width(f, s) <= maxw) { font_draw(f, cv, x, y, s, c, a); return; }
    char buf[128];
    int n = (int)strlen(s);
    if (n > (int)sizeof buf - 4) n = (int)sizeof buf - 4;
    while (n > 0) {
        snprintf(buf, sizeof buf, "%.*s…", n, s);
        if (font_width(f, buf) <= maxw) break;
        do n--; while (n > 0 && (s[n] & 0xc0) == 0x80);    /* whole UTF-8 characters */
    }
    font_draw(f, cv, x, y, buf, c, a);
}

void ui_draw_fame(UI *ui, Canvas *cv, const FameFrame *f)
{
    ui->nbtn = 0;
    ui->npanel = 0;
    title_backdrop(ui, cv, f->dt);
    /* (the backdrop shows through the panel, dimmed by it: no need for a full-screen pass) */
    int px = ui->px, pad = px;
    Font *bf = ui->body, *sf = ui->small;

    /* the columns: rank, fate, then the numbers; the narrowest screens lose the date, then the kills */
    enum { C_RANK, C_FATE, C_DEPTH, C_TURNS, C_EMBERS, C_KILLS, C_DATE, NCOL };
    static const char *H[NCOL] = { "", "Fate", "Depth", "Turns", "Embers", "Kills", "Date" };
    const char *sample[NCOL] = { "999", "", "26", "99999", "999", "999", "2026-11-08" };
    int cw[NCOL];
    for (int c = 0; c < NCOL; c++) {
        int a = font_width(sf, H[c]), b = font_width(bf, sample[c]);
        cw[c] = (a > b ? a : b) + px;
    }
    int fate_min = px * 12, fate_max = px * 20;
    bool show[NCOL] = { true, true, true, true, true, true, true };
    int w = 0;
    for (int tries = 0; tries < 3; tries++) {
        w = pad * 2 + fate_min;
        for (int c = 0; c < NCOL; c++) if (show[c] && c != C_FATE) w += cw[c];
        if (w <= cv->w - 20) break;
        if (show[C_DATE]) show[C_DATE] = false;
        else show[C_KILLS] = false;
    }
    int fixed = w - fate_min;
    cw[C_FATE] = clampi(cv->w - 20 - fixed, fate_min, fate_max);
    w = fixed + cw[C_FATE];
    if (w > cv->w - 8) w = cv->w - 8;

    int rowh = font_line(bf) * 6 / 5;
    int head_h = pad + font_line(ui->big) + font_line(sf) + pad;
    int foot_h = font_line(ui->head) * 2 + pad;
    int avail = cv->h - px * 2 - head_h - foot_h - rowh;   /* rowh: the column titles */
    int fit = avail / rowh;
    if (fit < 1) fit = 1;
    bool mine_below = f->mine > 0 && f->mine > (f->n < fit ? f->n : fit);
    int shown = f->n < fit ? f->n : fit;
    if (mine_below && shown >= fit) shown = fit - 2 > 0 ? fit - 2 : 0;
    int rows = shown + (mine_below ? 2 : 0);
    if (f->n == 0) rows = 2;
    int h = head_h + rowh + rows * rowh + foot_h;
    int x = (cv->w - w) / 2, y = (cv->h - h) / 2;
    panel(ui, cv, x, y, w, h, 240, true);

    const char *title = "Hall of Fame";
    int tw = font_width(ui->big, title);
    font_draw(ui->big, cv, x + (w - tw) / 2, y + pad + font_ascent(ui->big), title, GOLD, 255);
    char sub[64];
    if (f->total == 1) snprintf(sub, sizeof sub, "one run so far");
    else snprintf(sub, sizeof sub, "%d runs, the best first", f->total);
    int sw = font_width(sf, sub);
    font_draw(sf, cv, x + (w - sw) / 2, y + pad + font_line(ui->big) + font_ascent(sf), sub, GOLD_DIM, 255);

    int cx[NCOL], xx = x + pad;
    for (int c = 0; c < NCOL; c++) { cx[c] = xx; if (show[c]) xx += cw[c]; }
    int yy = y + head_h;
    for (int c = 0; c < NCOL; c++)
        if (show[c]) font_draw(sf, cv, cx[c], yy + font_ascent(sf), H[c], GOLD_DIM, 255);
    cv_blend_rect(cv, x + pad, yy + rowh - px / 4, w - pad * 2, 1, GOLD_DIM, 200);
    yy += rowh;

    if (f->n == 0) {
        font_wrap(bf, cv, x + pad, yy + font_ascent(bf), w - pad * 2,
                  "No runs yet. Carry fire down into the deep, and the tale of it will be written here.", 0xd8cbb0, 255);
    }
    for (int i = 0; i < rows && f->n > 0; i++) {
        const FameEntry *e;
        int rank;
        if (i < shown) { e = &f->e[i]; rank = i + 1; }
        else if (i == shown) {      /* a gap in the ranks before the run just ended */
            for (int d = -1; d <= 1; d++)
                cv_disc(cv, cx[C_RANK] + px / 2, yy + rowh / 2 + d * (rowh / 4), px / 10 + 1.0f, GOLD_DIM, 255);
            yy += rowh;
            continue;
        }
        else { e = f->mine_e; rank = f->mine; }
        if (!e) break;
        bool mine = rank == f->mine;
        bool won = e->end == FAME_WON;
        if (mine) {
            cv_blend_rect(cv, x + pad / 2, yy, w - pad, rowh, 0x5a4220, 150);
            cv_frame(cv, x + pad / 2, yy, w - pad, rowh, GOLD, 200);
        } else if (i % 2) {
            cv_blend_rect(cv, x + pad / 2, yy, w - pad, rowh, 0x2a2014, 70);
        }
        int base = yy + (rowh + font_ascent(bf) * 7 / 10) / 2;
        uint32_t ink = mine ? 0xfff2d0 : won ? 0xf0d890 : 0xd8cbb0;
        char buf[32];
        snprintf(buf, sizeof buf, "%d.", rank);
        font_draw(bf, cv, cx[C_RANK], base, buf, rank <= 3 ? GOLD : GOLD_DIM, 255);
        draw_fit(bf, cv, cx[C_FATE], base, cw[C_FATE] - px / 2, e->cause, won ? GOLD : ink, 255);
        snprintf(buf, sizeof buf, "%d", e->deepest);
        font_draw(bf, cv, cx[C_DEPTH], base, buf, ink, 255);
        snprintf(buf, sizeof buf, "%d", e->turns);
        font_draw(bf, cv, cx[C_TURNS], base, buf, ink, 255);
        snprintf(buf, sizeof buf, "%d", e->embers);
        font_draw(bf, cv, cx[C_EMBERS], base, buf, ink, 255);
        if (show[C_KILLS]) {
            snprintf(buf, sizeof buf, "%d", e->kills);
            font_draw(bf, cv, cx[C_KILLS], base, buf, ink, 255);
        }
        if (show[C_DATE]) {
            time_t when = (time_t)e->when;
            struct tm tm;
            if (localtime_r(&when, &tm)) strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
            else snprintf(buf, sizeof buf, "?");
            font_draw(sf, cv, cx[C_DATE], base, buf, GOLD_DIM, 255);
        }
        yy += rowh;
    }

    int bh = font_line(ui->head) * 3 / 2;
    int bw = button_width(ui, "Esc", "Back");
    button(ui, cv, x + (w - bw) / 2, y + h - pad - bh, bh, "Esc", "Back", UA_CLOSE, f->mx, f->my, true);
}
