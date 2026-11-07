#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "version.h"

#define MAX_BTN 48
#define MAX_PANEL 16

typedef struct { int x, y, w, h; UiAction a; int arg; } Btn;
typedef struct { int x, y, w, h; } Box;

struct UI {
    Font *head, *body, *small, *big;
    Image icons[IT_COUNT];  /* not owned: set by ui_set_icons */
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
};

static const uint32_t GOLD = 0xc9a45c, GOLD_DIM = 0x6e5a34, INK = 0x0b0907;

UI *ui_new(const char *dir, char *err, size_t errn)
{
    UI *ui = calloc(1, sizeof *ui);
    char p[1024];
    snprintf(p, sizeof p, "%s/Cinzel.ttf", dir);
    ui->head = font_open(p, 700);
    ui->big = font_open(p, 700);
    snprintf(p, sizeof p, "%s/Alegreya.ttf", dir);
    ui->body = font_open(p, 500);
    ui->small = font_open(p, 500);
    if (!ui->head || !ui->body || !ui->small || !ui->big) {
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
}

/* ------------------------------------------------------------- widgets */

static void add_btn_arg(UI *ui, int x, int y, int w, int h, UiAction a, int arg)
{
    if (ui->nbtn < MAX_BTN) ui->btn[ui->nbtn++] = (Btn){ x, y, w, h, a, arg };
}

static void add_btn(UI *ui, int x, int y, int w, int h, UiAction a) { add_btn_arg(ui, x, y, w, h, a, 0); }

void ui_set_icons(UI *ui, const Image *icons)
{
    for (int i = 0; i < IT_COUNT; i++) ui->icons[i] = icons[i];
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

/* a button: key hint in gold, label in parchment */
static int button(UI *ui, Canvas *cv, int x, int y, int h, const char *key, const char *label, UiAction a,
                  int mx, int my, bool enabled)
{
    Font *f = ui->head, *fk = ui->small;
    int pad = ui->px / 2;
    int kw = key ? font_width(fk, key) + pad : 0;
    int w = kw + font_width(f, label) + pad * 2;
    bool hov = mx >= x && my >= y && mx < x + w && my < y + h;
    cv_blend_rect(cv, x, y, w, h, hov ? 0x3a2c18 : 0x15110c, hov ? 235 : 210);
    cv_frame(cv, x, y, w, h, hov ? GOLD : GOLD_DIM, 255);
    int base = y + (h + font_ascent(f) * 7 / 10) / 2;
    if (key) font_draw(fk, cv, x + pad, base, key, enabled ? GOLD : GOLD_DIM, 255);
    font_draw(f, cv, x + pad + kw, base, label, enabled ? (hov ? 0xfff2d0 : 0xe2d4b4) : 0x6a6050, 255);
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
    int y = cv->h - h;
    *top = y;
    cv_blend_rect(cv, 0, y, cv->w, h, INK, 225);
    cv_fill(cv, 0, y, cv->w, 1, GOLD_DIM);
    cv_fill(cv, 0, y + 2, cv->w, 1, 0x2a2214);
    add_panel(ui, 0, y, cv->w, h);
    Font *hf = ui->head;
    int base = y + (h + font_ascent(hf) * 7 / 10) / 2;
    int pad = ui->px;
    Mob *p = &g->player;

    /* left: health */
    int x = pad;
    uint32_t hc = p->hp * 3 <= p->maxhp ? 0xff6a50 : 0xe2d4b4;
    char buf[64];
    snprintf(buf, sizeof buf, "HP %d/%d", p->hp, p->maxhp);
    x = font_draw(hf, cv, x, base, buf, hc, 255) + pad / 2;
    int bw = ui->px * 5;
    hp_bar(cv, x, base - font_ascent(hf) * 6 / 10, bw, ui->px / 3 + 2, p->hp, p->maxhp);
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
    int turn_x = x;
    snprintf(buf, sizeof buf, "TURN %d", g->turn);
    x = font_width(hf, buf) + x + pad;
    int left_end = x;

    /* right: actions */
    int bh = h * 64 / 100, by = y + (h - bh) / 2;
    bool has_torch = g->torch_fuel > 0;
    for (int i = 0; i < g->ninv; i++) if (g->inv[i].kind == IT_TORCH) has_torch = true;
    const char *tl = g->torch_lit ? "Snuff" : "Torch";
    struct { const char *k, *l; UiAction a; bool en; } B[] = {
        { "x", "Explore", UA_EXPLORE, true }, { "Z", "Rest", UA_REST, true }, { "t", "Throw", UA_THROW, g->ninv > 0 },
        { "i", "Items", UA_INV, true }, { "T", tl, UA_TORCH, has_torch || g->torch_lit }, { ">", "Descend", UA_STAIRS, true },
        { "m", "Map", UA_MAP, true }, { NULL, "?", UA_HELP, true },
    };
    int nb = 8, gap = ui->px / 2, tw = 0;
    for (int i = 0; i < nb; i++) tw += button_width(ui, B[i].k, B[i].l) + gap;
    bool compact = cv->w - tw - pad < turn_x;       /* the turn counter goes before the key hints do */
    if (compact) {     /* narrow window: drop the key hints */
        tw = 0;
        for (int i = 0; i < nb; i++) tw += button_width(ui, NULL, B[i].l) + gap;
    }
    if (cv->w - tw - pad >= left_end || cv->w - tw - pad >= turn_x + font_width(hf, buf))
        font_draw(hf, cv, turn_x, base, buf, 0xb8aa8c, 255);
    int bx = cv->w - tw - pad / 2;
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
        name[0] = (char)(name[0] - 32);
        font_draw(bf, cv, x + pad, yy + font_ascent(bf), name, 0xe2d4b4, 255);
        if (dark) font_draw(sf, cv, x + pad + font_width(bf, name) + pad, yy + font_ascent(bf), "watching", C_WARN, 255);
        else if (m->asleep) font_draw(sf, cv, x + pad + font_width(bf, name) + pad, yy + font_ascent(bf), "asleep", C_INFO, 255);
        else if (m->hunting) font_draw(sf, cv, x + pad + font_width(bf, name) + pad, yy + font_ascent(bf), "hunting", C_WARN, 255);
        if (!dark) hp_bar(cv, x + pad, yy + line, w - pad * 2, ui->px / 5 + 1, m->hp, m->maxhp);
        yy += line + ui->px / 2;
    }
}

static void hover_card(UI *ui, Canvas *cv, Game *g, const UiFrame *f)
{
    if (f->hover_x < 0 || f->mx < 0) return;
    Level *lv = game_level(g);
    int x = f->hover_x, y = f->hover_y;
    if (!(lv->f[y][x] & F_SEEN) && !(game_mob_at(g, x, y) && game_mob_noticed(g, game_mob_at(g, x, y)))) return;
    char title[64], desc[256], hint[96] = "";
    Mob *m = game_mob_at(g, x, y);
    Light *l = level_light_on(lv, x, y);
    Tile t = (Tile)lv->t[y][x];
    bool vis = lv->f[y][x] & F_VIS;
    if (g->player.x == x && g->player.y == y) {
        snprintf(title, sizeof title, "You");
        snprintf(desc, sizeof desc, "%s", MOB_INFO[MK_PLAYER].desc);
        snprintf(hint, sizeof hint, t == T_STAIRS_DOWN ? "Click to descend" : t == T_STAIRS_UP ? "Click to climb" : "Click to rest a turn");
    } else if (m && vis) {
        snprintf(title, sizeof title, "%s", MOB_INFO[m->kind].name);
        title[0] = (char)(title[0] - 32);
        snprintf(desc, sizeof desc, "%s%s", MOB_INFO[m->kind].desc,
                 m->kind == MK_SHADE && game_firelight(g, g->player.x, g->player.y) > SHADE_FEAR ? " It hangs back from your light."
                 : m->asleep ? " It is asleep." : m->hunting ? " It is hunting you." : " It hasn't noticed you.");
        snprintf(hint, sizeof hint, "Click to attack");
    } else if (m && game_mob_noticed(g, m)) {
        snprintf(title, sizeof title, "Eyes in the dark");
        snprintf(desc, sizeof desc, "Two pale eyes watch you from beyond your light. Whatever owns them is darker than the dark.");
        snprintf(hint, sizeof hint, "Throw a torch to see what it is");
    } else if (l && l->kind == L_SCONCE) {
        snprintf(title, sizeof title, l->lit ? "Burning sconce" : "Cold sconce");
        snprintf(desc, sizeof desc, l->lit ? "A torch in an iron bracket lights the room around it. You can be seen in its light."
                                           : "An unlit torch in an iron bracket. Light it to see the room, but anything here will see you too.");
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
        game_item_name(it->kind, it->count, nm, sizeof nm);
        snprintf(title, sizeof title, "%s", nm);
        if (title[0] >= 'a' && title[0] <= 'z') title[0] = (char)(title[0] - 32);
        snprintf(desc, sizeof desc, "%s", ITEM_INFO[it->kind].desc);
        snprintf(hint, sizeof hint, "Click to pick it up");
    } else if (l && l->kind == L_MOSS) {
        snprintf(title, sizeof title, "Glowmoss");
        snprintf(desc, sizeof desc, "A carpet of moss that gives off a faint green-blue light.");
        if (tile_walkable(t)) snprintf(hint, sizeof hint, "Click to travel here");
    } else if (l && l->kind == L_CRYSTAL) {
        snprintf(title, sizeof title, "Glowcrystal");
        snprintf(desc, sizeof desc, "Pale blue crystals growing from the rock, shedding a cold, steady light.");
    } else if (lv->scorch[y][x]) {
        snprintf(title, sizeof title, "Burnt-out torch");
        snprintf(desc, sizeof desc, "A charred stub in a smear of soot: all that is left of a torch that burned here.");
        if (tile_walkable(t)) snprintf(hint, sizeof hint, "Click to travel here");
    } else {
        if (t == T_ROCK) return;
        snprintf(title, sizeof title, "%s", tile_name(t));
        title[0] = (char)(title[0] - 32);
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

static int dialog(UI *ui, Canvas *cv, const char *title, const char *text, int *bx, int *by, int *bh)
{
    Font *bf = ui->body;
    int pad = ui->px;
    int w = ui->px * 22;
    if (w > cv->w - 20) w = cv->w - 20;
    int th = font_wrap(bf, NULL, 0, 0, w - pad * 2, text, 0, 0);
    int h = pad * 3 + font_line(ui->big) + th + font_line(ui->head) * 2;
    int x = (cv->w - w) / 2, y = (cv->h - h) / 2 - ui->px * 2;
    cv_blend_rect(cv, 0, 0, cv->w, cv->h, 0, 90);
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
        { "Stairs", ">  travel to and descend,  <  climb" },
        { "Light a sconce", "walk into it, or click it" },
        { "Items", "i  your pack (click an item for its actions)" },
        { "Throw", "t  then aim with the mouse or keys, Tab to cycle targets" },
        { "Use", "a  light a torch, drink a potion;  d  drop" },
        { "Torch", "T  light or snuff;  walk onto a burning torch to take it up" },
        { "Shades", "living darkness: firelight keeps them back and burns them" },
        { "Chasms", "step in to jump down a level (it hurts; water below breaks the fall)" },
        { "Map", "m  the whole level; click a place on it to travel there" },
        { "Water", "shallows splash; deep water puts out a torch and goblins won't swim" },
        { "Zoom", "+ and -  or the mouse wheel" },
        { "Sound", "F2 turns it off and on" },
        { "Text size", "kitty's own font zoom (ctrl+shift+= / -)" },
        { "Quit", "Esc twice" },
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
    int w = kw + vw + pad * 4, h = pad * 3 + font_line(ui->big) + n * line + font_line(hf) * 2;
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

static void menu_panel(UI *ui, Canvas *cv, Game *g, const UiFrame *f, int bottom)
{
    static const char *TITLES[] = { "", "Pack", "Throw what?", "Use what?", "Drop what?", "" };
    Font *hf = ui->head, *bf = ui->body, *sf = ui->small;
    int pad = ui->px * 7 / 10, s = icon_scale(ui), row = TILE * s + ui->px / 4;
    int w = ui->px * 15;
    if (f->menu == MENU_ITEM && f->menu_slot >= 0 && f->menu_slot < g->ninv) {
        Slot *sl = &g->inv[f->menu_slot];
        const ItemInfo *ii = &ITEM_INFO[sl->kind];
        int dh = font_wrap(sf, NULL, 0, 0, w - pad * 2, ii->desc, 0, 0);
        int bh = font_line(hf) * 3 / 2;
        int h = pad * 3 + TILE * s * 3 / 2 + dh + bh + pad;
        int x = cv->w - w - ui->px / 2, y = bottom - h - ui->px / 2;
        panel(ui, cv, x, y, w, h, 235, true);
        int big = s * 3 / 2 > s ? s * 3 / 2 : s + 1;
        cv_image_scaled(cv, &ui->icons[sl->kind], x + pad, y + pad, big, 255);
        char nm[64];
        game_item_name(sl->kind, sl->count, nm, sizeof nm);
        char title[80];
        snprintf(title, sizeof title, "%c) %s", 'a' + f->menu_slot, nm);
        font_draw(hf, cv, x + pad * 2 + TILE * big, y + pad + TILE * big / 2 + font_ascent(hf) / 3, title, GOLD, 255);
        int yy = y + pad + TILE * s * 3 / 2 + pad / 2;
        yy += font_wrap(sf, cv, x + pad, yy, w - pad * 2, ii->desc, 0xd8cbb0, 255);
        int bx = x + pad, by = y + h - pad - bh;
        if (sl->kind == IT_TORCH) bx += button(ui, cv, bx, by, bh, "a", g->torch_lit ? "Fresh" : "Light", UA_ITEM_USE, f->mx, f->my, true) + pad / 2;
        if (sl->kind == IT_POTION) bx += button(ui, cv, bx, by, bh, "q", "Drink", UA_ITEM_USE, f->mx, f->my, true) + pad / 2;
        bx += button(ui, cv, bx, by, bh, "t", "Throw", UA_ITEM_THROW, f->mx, f->my, true) + pad / 2;
        button(ui, cv, bx, by, bh, "d", "Drop", UA_ITEM_DROP, f->mx, f->my, true);
        return;
    }
    int n = g->ninv > 0 ? g->ninv : 1;
    int h = pad * 2 + font_line(hf) + n * row + font_line(sf) + pad / 2;
    int x = cv->w - w - ui->px / 2, y = bottom - h - ui->px / 2;
    if (y < 4) y = 4;
    panel(ui, cv, x, y, w, h, 235, true);
    font_draw(hf, cv, x + pad, y + pad + font_ascent(hf), TITLES[f->menu], GOLD, 255);
    int yy = y + pad + font_line(hf);
    if (!g->ninv) font_draw(bf, cv, x + pad, yy + font_ascent(bf), "Your pack is empty.", C_DIM, 255);
    for (int i = 0; i < g->ninv; i++) {
        Slot *sl = &g->inv[i];
        bool ok = menu_allows(f->menu, sl->kind);
        bool hov = ok && f->mx >= x && f->mx < x + w && f->my >= yy && f->my < yy + row;
        if (hov) cv_blend_rect(cv, x + 3, yy, w - 6, row, 0x4a3820, 170);
        cv_image_scaled(cv, &ui->icons[sl->kind], x + pad, yy + ui->px / 8, s, ok ? 255 : 90);
        char nm[64], line[96];
        game_item_name(sl->kind, sl->count, nm, sizeof nm);
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
    char buf[160];
    snprintf(buf, sizeof buf, "Throw %s %s: click where, Tab cycles targets, Esc cancels",
             g->inv[f->target_slot].kind == IT_POTION ? "a" : "a", ITEM_INFO[g->inv[f->target_slot].kind].name);
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
            else if (mm && (lv->f[ty][tx] & F_VIS)) nm = MOB_INFO[mm->kind].name;
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
    snprintf(ver, sizeof ver, "Lume %s", LUME_VERSION);
    int vw = font_width(ui->small, ver);
    font_draw_shadow(ui->small, cv, cv->w - vw - ui->px / 2, font_ascent(ui->small) + ui->px / 3, ver, 0x8a7c62, 200);
    if (f->show_fps) {
        char fb[32];
        snprintf(fb, sizeof fb, "%.0f fps", f->fps);
        font_draw_shadow(ui->small, cv, cv->w - font_width(ui->small, fb) - ui->px / 2,
                         font_ascent(ui->small) + font_line(ui->small) + ui->px / 3, fb, 0x8a7c62, 200);
    }
    if (f->quit_armed) {
        const char *q = "Press Esc again to quit";
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
    } else if (g->state == GS_DEAD) {
        char text[200];
        snprintf(text, sizeof text, "You fell on depth %d after %d turns, having slain %d foe%s.",
                 g->depth, g->turn, g->kills, g->kills == 1 ? "" : "s");
        int bx, by, bh;
        int w = dialog(ui, cv, "You have perished", text, &bx, &by, &bh);
        int w1 = button_width(ui, "Enter", "New game"), w2 = button_width(ui, "q", "Quit");
        int gap = ui->px;
        int x0 = bx + (w - w1 - w2 - gap) / 2;
        button(ui, cv, x0, by, bh, "Enter", "New game", UA_NEW_GAME, f->mx, f->my, true);
        button(ui, cv, x0 + w1 + gap, by, bh, "q", "Quit", UA_QUIT, f->mx, f->my, true);
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
