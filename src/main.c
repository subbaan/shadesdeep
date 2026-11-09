/* Shadesdeep: a graphical roguelike for the kitty terminal. The dungeon is drawn
 * into a software framebuffer and presented through the kitty graphics
 * protocol; input is kitty's keyboard protocol plus SGR-pixel mouse. */
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fame.h"
#include "game.h"
#include "gfx.h"
#include "kgfx.h"
#include "render.h"
#include "save.h"
#include "sound.h"
#include "term.h"
#include "ui.h"
#include "version.h"

#define MAX_INFLIGHT 2

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static bool dir_has_fonts(const char *dir)
{
    char p[PATH_MAX + 32];
    snprintf(p, sizeof p, "%s/fonts/Cinzel.ttf", dir);
    return access(p, R_OK) == 0;
}

/* assets/ next to the executable, one level up (build/), ../share/shadesdeep, or ./assets */
static int find_assets(char *out, size_t n, const char *override)
{
    if (override) {
        snprintf(out, n, "%s", override);
        return dir_has_fonts(out) ? 0 : -1;
    }
    char exe[PATH_MAX - 32];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (len > 0) {
        exe[len] = 0;
        char *slash = strrchr(exe, '/');
        if (slash) *slash = 0;
        const char *rel[] = { "assets", "../assets", "../share/shadesdeep/assets" };
        for (int i = 0; i < 3; i++) {
            snprintf(out, n, "%s/%s", exe, rel[i]);
            if (dir_has_fonts(out)) return 0;
        }
    }
    snprintf(out, n, "assets");
    return dir_has_fonts(out) ? 0 : -1;
}

/* save the game (not a finished one); false if there was nothing to save or it failed (err says) */
static bool save_now(Game *g, const char *path, char *err, size_t n)
{
    if (!path[0] || g->state == GS_DEAD || g->state == GS_WON) return false;
    while (g->proj.active) game_update(g, 1.0f / 60);     /* let a thrown item land first */
    if (save_write(g, path, err, n) == 0) return true;
    game_msg(g, C_BAD, "%s.", err);
    return false;
}

static void usage(FILE *f)
{
    fprintf(f,
        "Usage: %s [options]\n"
        "Shadesdeep, a graphical roguelike for the kitty terminal.\n"
        "\n"
        "  -s, --seed=N        dungeon seed (default: random)\n"
        "  -z, --zoom=N        pixel zoom (default: picked from the window height)\n"
        "  -t, --text=SCALE    text size relative to the terminal font (default 1.0)\n"
        "  -f, --fps=N         target frame rate (default 60)\n"
        "  -m, --mode=MODE     pixel transport: shm (default), direct, directz\n"
        "  -a, --assets=DIR    asset directory (default: found next to the binary)\n"
        "      --new           start a new game even if there is a saved one (it is\n"
        "                      replaced when you quit)\n"
        "      --save=FILE     where to keep the saved game\n"
        "                      (default ~/.local/share/shadesdeep/save.gz)\n"
        "      --no-save       don't resume or save a game, or record it in the hall\n"
        "                      of fame\n"
        "      --no-title      skip the title screen and go straight into the game\n"
        "      --hall-of-fame  print the hall of fame and exit\n"
        "      --mute          start with the sound off (F2 toggles it in the game)\n"
        "      --no-sound      don't open the audio device at all\n"
        "      --classic-art   draw the creatures with the old built-in sprites\n"
        "      --show-fps      show the frame rate under the version\n"
        "      --probe         report terminal capabilities and exit\n"
        "      --dump=FILE     render one frame to a PNG and exit (no terminal needed)\n"
        "      --size=WxH      window size in pixels for --dump (default 1300x700)\n"
        "      --cell=H        terminal cell height for --dump (default 20)\n"
        "      --steps=N       auto-explore N steps before the --dump frame\n"
        "      --hover=X,Y     mouse position (window pixels) for the --dump frame\n"
        "  -h, --help          show this help\n"
        "  -V, --version       show version\n"
        "\n"
        "In the game press ? for the controls, m for the map. Mouse: click to travel\n"
        "or attack, hover for descriptions, wheel to zoom. Esc twice saves and quits;\n"
        "the next start picks the game up again. Dying ends it for good. Relight the\n"
        "Heartflame at the bottom, on depth 26, to win.\n",
        SHADESDEEP_NAME);
}

static void print_caps(FILE *o, const TermCaps *c, const TermSize *ts)
{
    fprintf(o, "%s %s capability probe\n", SHADESDEEP_NAME, SHADESDEEP_VERSION);
    fprintf(o, "  window:              %dx%d px, %dx%d cells, cell %dx%d px\n",
            ts->xpix, ts->ypix, ts->cols, ts->rows, ts->cellw, ts->cellh);
    fprintf(o, "  keyboard protocol:   %s", c->kb_proto ? "yes" : "NO");
    if (c->kb_proto) fprintf(o, " (current flags %d)", c->kb_flags);
    fprintf(o, "\n  graphics protocol:   %s%s%s\n", c->gfx ? "yes" : "NO",
            c->gfx_err[0] ? " - " : "", c->gfx_err);
    fprintf(o, "  shared memory:       %s%s%s\n", c->gfx_shm ? "yes" : "NO",
            c->shm_err[0] ? " - " : "", c->shm_err);
    fprintf(o, "  synchronized output: %s\n", c->sync_output ? "yes" : "no");
}

static int auto_zoom(int h)
{
    int z = h / (TILE * 20);
    return z < 1 ? 1 : z > 6 ? 6 : z;
}

typedef struct {
    Canvas screen, world;
    int zoom;
} Frame;

static void frame_resize(Frame *fr, int w, int h, int zoom)
{
    if (w < 64) w = 64;
    if (h < 64) h = 64;
    if (fr->screen.w != w || fr->screen.h != h) {
        free(fr->screen.px);
        fr->screen = (Canvas){ w, h, calloc((size_t)w * h, 4) };
    }
    int ww = (w + zoom - 1) / zoom, wh = (h + zoom - 1) / zoom;
    if (fr->world.w != ww || fr->world.h != wh) {
        free(fr->world.px);
        fr->world = (Canvas){ ww, wh, calloc((size_t)ww * wh, 4) };
    }
    fr->zoom = zoom;
}

typedef struct {
    int path[MAX_PATH];
    int plen;
    int hx, hy;
    int tpath[32];
    /* The mouse "sleeps" once you play with the keys, so the tile highlight, path and
     * card stop following things that scroll under a resting pointer. A card shows only
     * for the tile the mouse last moved onto, and only for TIP_SECS after that. */
    bool awake;
    bool tip_live;          /* still within TIP_SECS of the last mouse movement */
    bool tip_arm;           /* the mouse just moved: remember the tile under it */
    int tipx, tipy;
} Hover;

#define TIP_SECS 4.0

/* interface state: open menus, aiming */
typedef struct {
    bool help;
    MenuKind menu;
    int menu_slot;
    bool targeting;
    int tslot, tx, ty;      /* item being thrown, aim cursor */
    int tcycle;             /* Tab position among visible monsters */
    bool quit_armed;
    bool map;               /* the map overview is open */
    bool map_stale;         /* redraw its picture */
    int fame_rank, fame_total;  /* where the run that just ended stands in the hall of fame */
} Ctl;

/* ---------------------------------------------------------------- sound */

/* play what the game asked for this frame: quieter with distance and out of
 * sight, panned by where it happened */
static void play_sounds(Game *g)
{
    if (!snd_ready()) { g->nsnd = 0; return; }
    Level *lv = game_level(g);
    const Mob *p = &g->player;
    for (int i = 0; i < g->nsnd; i++) {
        const SoundEv *e = &g->snd[i];
        float vol = e->vol, pan = 0;
        float pitch = 0.95f + (rand() % 100) / 1000.0f;
        if (e->kind == SND_STEP || e->kind == SND_WADE) pitch = 0.88f + (rand() % 240) / 1000.0f;
        if (!e->global) {
            float dx = e->x - p->rx, dy = e->y - p->ry, d = sqrtf(dx * dx + dy * dy);
            if (d > 15) continue;
            vol /= 1 + 0.22f * d;
            if (in_map(e->x, e->y) && !(lv->f[e->y][e->x] & (F_VIS | F_LOS))) vol *= 0.5f;     /* round a corner */
            pan = clampf(dx / 9, -0.8f, 0.8f);
        }
        snd_play(e->kind, vol, pitch, pan, e->delay);
    }
    g->nsnd = 0;
}

/* the layers under everything: the dungeon, your torch, fires and water nearby */
static void update_loops(Game *g)
{
    if (!snd_ready()) return;
    bool alive = g->state != GS_DEAD;
    Level *lv = game_level(g);
    const Mob *p = &g->player;
    float fire = 0;
    for (int i = 0; i < lv->nlights; i++) {
        const Light *l = &lv->lights[i];
        if (!l->lit || (l->kind != L_SCONCE && l->kind != L_FLOORTORCH && l->kind != L_OILFIRE && l->kind != L_HEART)) continue;
        float dx = l->fx - p->rx, dy = l->fy - p->ry, d = sqrtf(dx * dx + dy * dy);
        float w = clampf(1 - d / 8, 0, 1);
        w *= w;
        if (!(lv->f[l->y][l->x] & F_VIS)) w *= 0.4f;
        fire += w;
    }
    float water = 0;
    int px = (int)lroundf(p->rx), py = (int)lroundf(p->ry);
    for (int y = py - 6; y <= py + 6; y++)
        for (int x = px - 6; x <= px + 6; x++) {
            if (!tile_water(tile_at(lv, x, y)) || !(lv->f[y][x] & F_SEEN)) continue;
            float d = sqrtf((float)((x - px) * (x - px) + (y - py) * (y - py)));
            float w = clampf(1 - d / 6.5f, 0, 1);
            if (w > water) water = w;
        }
    snd_loop(LOOP_AMBIENCE, alive ? 1.0f : 0.5f);
    snd_loop(LOOP_TORCH, alive && g->torch_lit ? 1.0f : 0);
    snd_loop(LOOP_FIRE, alive ? clampf(fire, 0, 1) : 0);
    snd_loop(LOOP_WATER, alive ? water : 0);
}

/* the map's picture, redrawn when it opens or the window changes */
static Canvas g_mini;
static MapLayout g_ml;

static void map_update(Renderer *r, UI *ui, Game *g, const Canvas *screen, Ctl *ctl)
{
    MapLayout ml;
    ui_map_layout(ui, g, screen->w, screen->h, &ml);
    if (!ctl->map_stale && memcmp(&ml, &g_ml, sizeof ml) == 0 && g_mini.px) return;
    g_ml = ml;
    if (g_mini.w != ml.w || g_mini.h != ml.h) {
        free(g_mini.px);
        g_mini = (Canvas){ ml.w, ml.h, calloc((size_t)(ml.w > 0 ? ml.w : 1) * (ml.h > 0 ? ml.h : 1), 4) };
    }
    if (g_mini.px) render_map(r, g, &g_mini, ml.tx, ml.ty, ml.tw, ml.th);
    ctl->map_stale = false;
}

/* the level tile under a click on the map, or false */
static bool map_tile_at(Game *g, int mx, int my, int *tx, int *ty)
{
    const MapLayout *m = &g_ml;
    if (mx < m->x || my < m->y || mx >= m->x + m->w || my >= m->y + m->h) return false;
    *tx = m->tx + (int)((mx - m->x) / m->cell);
    *ty = m->ty + (int)((my - m->y) / m->cell);
    return in_map(*tx, *ty) && (game_level(g)->f[*ty][*tx] & F_SEEN);
}

/* the map tile under a window pixel */
static void hover_tile(const Renderer *r, const Frame *fr, int mx, int my, int *tx, int *ty)
{
    float cx, cy;
    render_cam_pos(r, &cx, &cy);
    float wx = mx / (float)fr->zoom + (float)lroundf(cx), wy = my / (float)fr->zoom + (float)lroundf(cy);
    *tx = (int)floorf(wx / TILE);
    *ty = (int)floorf(wy / TILE);
    if (!in_map(*tx, *ty)) *tx = *ty = -1;
}

static void draw_frame(Renderer *r, UI *ui, Game *g, Frame *fr, Hover *hv, Ctl *ctl, int mx, int my, float dt,
                       float fps, bool show_fps)
{
    hv->hx = hv->hy = -1;
    hv->plen = 0;
    bool over_ui = mx >= 0 && ui_over_panel(ui, mx, my);
    if (mx >= 0 && !over_ui && g->state == GS_PLAY && !ctl->help && !ctl->map) {
        hover_tile(r, fr, mx, my, &hv->hx, &hv->hy);
        if (hv->hx >= 0 && g->autom == AUTO_NONE && !ctl->targeting && ctl->menu == MENU_NONE && !g->proj.active)
            hv->plen = game_path_preview(g, hv->hx, hv->hy, hv->path, MAX_PATH);
    }
    if (hv->tip_arm) {
        hv->tip_arm = false;
        hv->tipx = hv->hx;
        hv->tipy = hv->hy;
    }
    bool show = hv->awake;
    bool tip = show && hv->tip_live && hv->hx == hv->tipx && hv->hy == hv->tipy;
    Overlay ov = { show ? hv->hx : -1, show ? hv->hy : -1, hv->path, show ? hv->plen : 0, NULL, 0, false, IT_DART };
    int ux = show ? hv->hx : -1, uy = show ? hv->hy : -1;
    if (ctl->targeting && ctl->tslot >= 0 && ctl->tslot < g->ninv && !g->proj.active) {
        ov.tkind = g->inv[ctl->tslot].kind;
        ov.tpath = hv->tpath;
        ov.tlen = game_throw_path(g, ov.tkind, ctl->tx, ctl->ty, hv->tpath, 32, &ov.thit);
        ov.hx = ux = ctl->tx;
        ov.hy = uy = ctl->ty;
    }
    render_set_zoom(r, fr->zoom);
    render_world(r, g, &fr->world, dt, &ov);
    cv_upscale(&fr->screen, &fr->world, fr->zoom, 0, 0);
    render_hires(r, &fr->screen, &fr->world, fr->zoom);
    float cx, cy;
    render_cam_pos(r, &cx, &cy);
    bool map = ctl->map && g->state == GS_PLAY;
    if (map) map_update(r, ui, g, &fr->screen, ctl);
    UiFrame uf = { (float)lroundf(cx), (float)lroundf(cy), fr->zoom, mx, my, ux, uy, tip, ctl->help, ctl->quit_armed,
                   fps, show_fps, ctl->menu, ctl->menu_slot, ctl->targeting, ctl->tslot, map, &g_mini, g_ml,
                   ctl->fame_rank, ctl->fame_total };
    ui_draw(ui, g, &fr->screen, &uf);
}

/* visible monsters, nearest first */
static int visible_mobs(Game *g, Mob **out, int max)
{
    Mob *ms = game_mobs(g, g->depth);
    int n = 0;
    for (int i = 0; i < MAX_MOBS && n < max; i++)
        if (game_mob_noticed(g, &ms[i])) out[n++] = &ms[i];
    Mob *p = &g->player;
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0; j--) {
            int da = abs(out[j]->x - p->x) + abs(out[j]->y - p->y), db = abs(out[j - 1]->x - p->x) + abs(out[j - 1]->y - p->y);
            if (da >= db) break;
            Mob *t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
        }
    return n;
}

static void start_aim(Game *g, Ctl *ctl, int slot, const Hover *hv)
{
    ctl->menu = MENU_NONE;
    ctl->targeting = true;
    ctl->tslot = slot;
    ctl->tcycle = 0;
    Mob *ms[MAX_MOBS];
    Mob *p = &g->player;
    int n = visible_mobs(g, ms, MAX_MOBS);
    if (n) { ctl->tx = ms[0]->x; ctl->ty = ms[0]->y; }
    else if (hv->hx >= 0) { ctl->tx = hv->hx; ctl->ty = hv->hy; }
    else { ctl->tx = p->x + (p->flip ? -3 : 3); ctl->ty = p->y; }
    ctl->tx = clampi(ctl->tx, 0, MAP_W - 1);
    ctl->ty = clampi(ctl->ty, 0, MAP_H - 1);
}

static bool classic_art;     /* --classic-art: keep the procedural creature sprites */

static int dump_frame(const char *assets, uint32_t seed, const char *path, int w, int h, int cellh,
                      int steps, int zoom, float tscale, int mx, int my)
{
    char err[256], fonts[PATH_MAX + 8];
    snprintf(fonts, sizeof fonts, "%s/fonts", assets);
    UI *ui = ui_new(fonts, err, sizeof err);
    if (!ui) { fprintf(stderr, "%s: %s\n", SHADESDEEP_NAME, err); return 1; }
    ui_set_scale(ui, cellh, tscale);
    Game *g = game_new(seed);
    Renderer *r = render_new();
    if (!classic_art) render_load_sprites(r, assets);
    Frame fr = { 0 };
    frame_resize(&fr, w, h, zoom ? zoom : auto_zoom(h));
    if (steps > 0) {
        int moved = 0, waded = 0;
        game_explore(g);
        for (int i = 0; i < steps * 40 && g->state != GS_DEAD; i++) {
            if (g->state == GS_JUMP_PROMPT) game_jump_answer(g, false);
            if (g->player.hp < g->player.maxhp / 2) g->player.hp = g->player.maxhp;  /* a sight-seeing tour */
            if (g->autom == AUTO_NONE) {
                if (i % 40 == 0) game_explore(g);
            }
            int ox = g->player.x, oy = g->player.y, od = g->depth;
            game_update(g, 0.01f);
            if (g->depth == od && (g->player.x != ox || g->player.y != oy)) {
                moved++;
                if (tile_at(game_level(g), g->player.x, g->player.y) == T_SHALLOW) waded++;
            }
        }
        game_stop(g);
        /* SHADESDEEP_DUMP_WADE: report how much of the tour was spent wading */
        if (getenv("SHADESDEEP_DUMP_WADE")) fprintf(stderr, "steps %d, wading %d\n", moved, waded);
    }
    if (getenv("SHADESDEEP_DUMP_CHASM") && !game_debug_to_chasm(g)) fprintf(stderr, "%s: no chasm on this level\n", SHADESDEEP_NAME);
    /* SHADESDEEP_DUMP_WATER=0|1|2: stand by a pool, in its shallows, or swimming in it */
    if (getenv("SHADESDEEP_DUMP_WATER") && !game_debug_to_water(g, atoi(getenv("SHADESDEEP_DUMP_WATER"))))
        fprintf(stderr, "%s: no deep water on this level\n", SHADESDEEP_NAME);
    /* SHADESDEEP_DUMP_JUMP=N: jump down N chasms (developer test of level changes) */
    /* SHADESDEEP_DUMP_DEPTH=N: go straight to depth N (26 is the Heartflame's) */
    if (getenv("SHADESDEEP_DUMP_DEPTH") && !game_debug_depth(g, atoi(getenv("SHADESDEEP_DUMP_DEPTH"))))
        fprintf(stderr, "%s: no such depth\n", SHADESDEEP_NAME);
    for (int j = getenv("SHADESDEEP_DUMP_JUMP") ? atoi(getenv("SHADESDEEP_DUMP_JUMP")) : 0; j > 0 && g->state == GS_PLAY; j--) {
        if (!game_debug_to_chasm(g)) break;
        game_move(g, 0, -1);
        game_move(g, 0, -1);
        if (g->state == GS_JUMP_PROMPT) game_jump_answer(g, true);
        if (g->state == GS_DEAD) break;
        g->player.hp = g->player.maxhp;
        for (int i = 0; i < 200; i++) game_update(g, 0.02f);
    }
    /* SHADESDEEP_DUMP_ROOM=store|armoury|nest: stand in such a room, with the level and its traps shown */
    if (getenv("SHADESDEEP_DUMP_ROOM")) {
        const char *k = getenv("SHADESDEEP_DUMP_ROOM");
        int rk = !strcmp(k, "store") ? RK_STORE : !strcmp(k, "armoury") ? RK_ARMOURY : !strcmp(k, "nest") ? RK_NEST
               : !strcmp(k, "hidden") ? RK_SECRET : -1;
        bool ok = !strcmp(k, "hidden-door") ? game_debug_to_secret_door(g) : rk >= 0 && game_debug_to_room(g, rk);
        if (!ok) fprintf(stderr, "%s: no %s room on this level\n", SHADESDEEP_NAME, k);
    }
    /* SHADESDEEP_DUMP_SPOT=0..5: an east-west corridor, a side-wall door, a room's south wall,
     * a cold sconce with a shade near (4: and light it), the Heartflame's hall (5). After SHADESDEEP_DUMP_JUMP */
    if (getenv("SHADESDEEP_DUMP_SPOT") && !game_debug_to_spot(g, atoi(getenv("SHADESDEEP_DUMP_SPOT"))))
        fprintf(stderr, "%s: no such spot on this level\n", SHADESDEEP_NAME);
    for (int i = 0; i < 120; i++) game_update(g, 1.0f / 60);
    static Hover hv;
    hv.awake = hv.tip_live = hv.tip_arm = mx >= 0;
    Ctl ctl = { .menu_slot = 0, .tslot = -1 };
    ui_set_icons(ui, render_item_icons(r), render_item_icons_hi(r));
    /* developer views: SHADESDEEP_DUMP_TORCH=1 lights a torch, SHADESDEEP_DUMP_MENU=inv|throw|use|item,
     * SHADESDEEP_DUMP_AIM="dx,dy" aims a torch, SHADESDEEP_DUMP_THROW="dx,dy,frac" freezes one mid-flight */
    if (getenv("SHADESDEEP_DUMP_TORCH")) game_torch_toggle(g);
    if (getenv("SHADESDEEP_DUMP_MAP")) { ctl.map = true; ctl.map_stale = true; }
    if (getenv("SHADESDEEP_DUMP_HELP")) ctl.help = true;
    /* SHADESDEEP_DUMP_FORGE=N: stand by the forge with N embers and open it ("N,0": don't open it) */
    if (getenv("SHADESDEEP_DUMP_FORGE")) {
        g->embers = atoi(getenv("SHADESDEEP_DUMP_FORGE"));
        if (game_debug_to_forge(g)) { if (!strstr(getenv("SHADESDEEP_DUMP_FORGE"), ",0")) ctl.menu = MENU_FORGE; }
        else fprintf(stderr, "%s: no forge on this level\n", SHADESDEEP_NAME);
    }
    const char *dm = getenv("SHADESDEEP_DUMP_MENU");
    if (dm) ctl.menu = !strcmp(dm, "throw") ? MENU_THROW : !strcmp(dm, "use") ? MENU_USE : !strcmp(dm, "item") ? MENU_ITEM : MENU_INV;
    if (getenv("SHADESDEEP_DUMP_SLOT")) ctl.menu_slot = atoi(getenv("SHADESDEEP_DUMP_SLOT"));     /* which item SHADESDEEP_DUMP_MENU=item shows */
    /* SHADESDEEP_DUMP_GEAR="kind,plus;...": put gear in the pack (kinds: sword dagger spear axe mace leather cloak mail) */
    if (getenv("SHADESDEEP_DUMP_GEAR")) {
        static const char *N[] = { "sword", "dagger", "spear", "axe", "mace", "leather", "cloak", "mail" };
        const char *q = getenv("SHADESDEEP_DUMP_GEAR");
        while (*q && g->ninv < MAX_INV) {
            for (int i = 0; i < 8; i++)
                if (!strncmp(q, N[i], strlen(N[i]))) {
                    const char *c = strchr(q, ',');
                    g->inv[g->ninv++] = (Slot){ (ItemKind)(IT_SWORD + i), 1, c ? atoi(c + 1) : 0 };
                    break;
                }
            const char *e = strchr(q, ';');
            if (!e) break;
            q = e + 1;
        }
    }
    /* SHADESDEEP_DUMP_WIELD=1: wield (or wear) the last piece SHADESDEEP_DUMP_GEAR added */
    if (getenv("SHADESDEEP_DUMP_WIELD") && g->ninv > 0) game_use(g, g->ninv - 1);
    /* SHADESDEEP_DUMP_ITEMS="kind,count;...": put things in the pack (kinds: oil flash salt warmth potion dart torch) */
    if (getenv("SHADESDEEP_DUMP_ITEMS")) {
        static const struct { const char *n; ItemKind k; } N[] = {
            { "oil", IT_OIL }, { "flash", IT_FLASH }, { "salt", IT_SALT }, { "warmth", IT_WARMTH },
            { "potion", IT_POTION }, { "dart", IT_DART }, { "torch", IT_TORCH },
        };
        const char *q = getenv("SHADESDEEP_DUMP_ITEMS");
        g->ninv = 0;
        while (*q && g->ninv < MAX_INV) {
            for (size_t i = 0; i < sizeof N / sizeof *N; i++)
                if (!strncmp(q, N[i].n, strlen(N[i].n))) {
                    const char *c = strchr(q, ',');
                    g->inv[g->ninv++] = (Slot){ N[i].k, c ? atoi(c + 1) : 1, 0 };
                    break;
                }
            const char *e = strchr(q, ';');
            if (!e) break;
            q = e + 1;
        }
    }
    /* SHADESDEEP_DUMP_SHADE="dx,dy[;dx,dy...]": shades near the player */
    if (getenv("SHADESDEEP_DUMP_SHADE")) {
        const char *s = getenv("SHADESDEEP_DUMP_SHADE");
        int sx, sy, used;
        while (sscanf(s, "%d,%d%n", &sx, &sy, &used) == 2) {
            if (!game_debug_spawn(g, MK_SHADE, g->player.x + sx, g->player.y + sy))
                fprintf(stderr, "%s: no room for a shade at %+d,%+d\n", SHADESDEEP_NAME, sx, sy);
            s += used;
            if (*s != ';') break;
            s++;
        }
    }
    /* SHADESDEEP_DUMP_MOBS="kind,dx,dy[;...]": any creatures near the player (eel+ is an eel above the water) */
    if (getenv("SHADESDEEP_DUMP_MOBS")) {
        const char *s = getenv("SHADESDEEP_DUMP_MOBS");
        char nm[24];
        int sx, sy, used;
        while (sscanf(s, "%23[^,],%d,%d%n", nm, &sx, &sy, &used) == 3) {
            bool up = nm[strlen(nm) - 1] == '+';
            if (up) nm[strlen(nm) - 1] = 0;
            bool zz = nm[0] && nm[strlen(nm) - 1] == '~';     /* "goblin~": asleep */
            if (zz) nm[strlen(nm) - 1] = 0;
            int k = MK_COUNT;
            for (int i = 1; i < MK_COUNT; i++)
                if (!strcmp(nm, MOB_INFO[i].name) || (i == MK_SLINGER && !strcmp(nm, "slinger"))) k = i;
            if (k == MK_COUNT || !game_debug_spawn(g, (MobKind)k, g->player.x + sx, g->player.y + sy))
                fprintf(stderr, "%s: can't put a %s at %+d,%+d\n", SHADESDEEP_NAME, nm, sx, sy);
            else if (up) game_mob_at(g, g->player.x + sx, g->player.y + sy)->surfaced = 3;
            if (zz && game_mob_at(g, g->player.x + sx, g->player.y + sy)) {
                Mob *zm = game_mob_at(g, g->player.x + sx, g->player.y + sy);
                zm->asleep = true;
                zm->hunting = false;
            }
            s += used;
            if (*s != ';') break;
            s++;
        }
    }
    /* SHADESDEEP_DUMP_STRIKE=dx,dy[,N]: step (attack) that way N times */
    {
        int sdx, sdy, sn = 1;
        if (getenv("SHADESDEEP_DUMP_STRIKE") && sscanf(getenv("SHADESDEEP_DUMP_STRIKE"), "%d,%d,%d", &sdx, &sdy, &sn) >= 2)
            for (int i = 0; i < sn && g->state == GS_PLAY; i++) {
                game_move(g, sdx, sdy);
                for (int k = 0; k < 30 && game_busy(g); k++) game_update(g, 1.0f / 30);
            }
    }
    /* SHADESDEEP_DUMP_HP=N: the player's hit points (to test dying) */
    if (getenv("SHADESDEEP_DUMP_HP")) g->player.hp = atoi(getenv("SHADESDEEP_DUMP_HP"));
    int adx, ady;
    float frac = 0.5f;
    if (getenv("SHADESDEEP_DUMP_AIM") && sscanf(getenv("SHADESDEEP_DUMP_AIM"), "%d,%d", &adx, &ady) == 2) {
        start_aim(g, &ctl, 0, &hv);
        ctl.tx = g->player.x + adx;
        ctl.ty = g->player.y + ady;
    }
    /* SHADESDEEP_DUMP_USE=1: use the first item in the pack (after the creatures are placed) */
    if (getenv("SHADESDEEP_DUMP_USE")) game_use(g, 0);
    if (getenv("SHADESDEEP_DUMP_THROW") && sscanf(getenv("SHADESDEEP_DUMP_THROW"), "%d,%d,%f", &adx, &ady, &frac) >= 2) {
        if (game_throw(g, 0, g->player.x + adx, g->player.y + ady))
            while (g->proj.active && g->proj.pos < g->proj.n * frac) game_update(g, 1.0f / 240);
        if (frac >= 1) for (int i = 0; i < 60; i++) game_update(g, 1.0f / 60);
    }
    /* SHADESDEEP_DUMP_KINDLE=N: stand at the hearth with a lit torch and kindle it N times (3 wins) */
    if (getenv("SHADESDEEP_DUMP_KINDLE") && !game_debug_kindle(g, atoi(getenv("SHADESDEEP_DUMP_KINDLE"))))
        fprintf(stderr, "%s: no hearth here (try SHADESDEEP_DUMP_DEPTH=26)\n", SHADESDEEP_NAME);
    /* SHADESDEEP_DUMP_REST=N: wait N turns (lets thrown torches burn out) */
    for (int n = getenv("SHADESDEEP_DUMP_REST") ? atoi(getenv("SHADESDEEP_DUMP_REST")) : 0; n > 0 && g->state == GS_PLAY; n--) {
        game_rest(g);
        for (int i = 0; i < 30 && game_busy(g); i++) game_update(g, 1.0f / 30);
    }
    /* SHADESDEEP_DUMP_SEARCH=N: search N times over (five turns each, stopping when something turns up) */
    for (int n = getenv("SHADESDEEP_DUMP_SEARCH") ? atoi(getenv("SHADESDEEP_DUMP_SEARCH")) : 0; n > 0 && g->state == GS_PLAY; n--) {
        game_search(g);
        for (int i = 0; i < 400 && g->autom == AUTO_SEARCH; i++) game_update(g, 1.0f / 30);
        if (getenv("SHADESDEEP_DUMP_BANDS")) break;
    }
    /* SHADESDEEP_DUMP_BANDS=1: say how many wandering bands are on the level */
    if (getenv("SHADESDEEP_DUMP_BANDS")) {
        int mem = 0, b = game_debug_count_bands(g, &mem);
        fprintf(stderr, "bands %d, members %d, turn %d\n", b, mem, g->turn);
    }
    /* SHADESDEEP_DUMP_FAMEADD=FILE: if the run is over, write it into that hall of fame */
    if (getenv("SHADESDEEP_DUMP_FAMEADD") && (g->state == GS_DEAD || g->state == GS_WON)) {
        FameEntry e;
        char ferr[PATH_MAX + 64];
        fame_from_game(g, g->state == GS_WON ? FAME_WON : FAME_DIED, &e);
        ctl.fame_rank = fame_add(getenv("SHADESDEEP_DUMP_FAMEADD"), &e, ferr, sizeof ferr);
        if (!ctl.fame_rank) fprintf(stderr, "%s: %s\n", SHADESDEEP_NAME, ferr);
        static FameEntry tmp[FAME_MAX];
        fame_load(getenv("SHADESDEEP_DUMP_FAMEADD"), tmp, FAME_MAX, &ctl.fame_total);
        fprintf(stderr, "recorded \"%s\": rank %d of %d\n", e.cause, ctl.fame_rank, ctl.fame_total);
    }
    render_camera(r, g, fr.world.w, fr.world.h, 0, true);
    /* show the closing screen at once (SHADESDEEP_DUMP_NOEND=1: not yet) */
    if ((g->state == GS_DEAD || g->state == GS_WON) && !getenv("SHADESDEEP_DUMP_NOEND")) g->end_t = g->time - 30;
    bool freeze = g->proj.active;
    /* two passes so the interface's hit areas exist for the hover test */
    draw_frame(r, ui, g, &fr, &hv, &ctl, mx, my, 1.0f / 60, 60, false);
    for (int i = 0; i < 30; i++) {
        if (!freeze) game_update(g, 1.0f / 60);
        draw_frame(r, ui, g, &fr, &hv, &ctl, mx, my, freeze ? 0 : 1.0f / 60, 60, false);
    }
    if (getenv("SHADESDEEP_DUMP_LOWER")) render_dump_lower(r, getenv("SHADESDEEP_DUMP_LOWER"));
    /* SHADESDEEP_DUMP_TITLE=0|1|2: the title screen (fresh, with a saved game, asking to give it up);
     * SHADESDEEP_DUMP_TITLEART=PNG: its picture (default: assets/title.png if there is one);
     * SHADESDEEP_DUMP_FAME=FILE[,RANK]: the hall of fame in FILE, with RANK picked out */
    Image art = { 0 };
    const char *dt_ = getenv("SHADESDEEP_DUMP_TITLE"), *df = getenv("SHADESDEEP_DUMP_FAME");
    if (dt_ || df) {
        char tp[PATH_MAX + 16];
        if (getenv("SHADESDEEP_DUMP_TITLEART")) snprintf(tp, sizeof tp, "%s", getenv("SHADESDEEP_DUMP_TITLEART"));
        else snprintf(tp, sizeof tp, "%s/title.png", assets);
        if (access(tp, R_OK) == 0 && image_load(&art, tp) == 0) ui_set_title_art(ui, &art);
        int mode = dt_ ? atoi(dt_) : 0;
        TitleFrame tf = { .can_continue = mode >= 1, .save_depth = 7, .save_turn = 1234, .confirm_new = mode == 2,
                          .mx = mx, .my = my, .dt = 1.0f / 30 };
        static FameEntry fl[FAME_MAX], mine;
        char fpath[PATH_MAX];
        int frank = 0, total = 0, n = 0;
        if (df) {
            snprintf(fpath, sizeof fpath, "%s", df);
            char *comma = strrchr(fpath, ',');
            if (comma) { frank = atoi(comma + 1); *comma = 0; }
            n = fame_load(fpath, fl, FAME_MAX, &total);
            if (frank > 0 && frank <= n) mine = fl[frank - 1];
        }
        FameFrame ff = { fl, n, total, frank, frank ? &mine : NULL, mx, my, 1.0f / 30 };
        for (int i = 0; i < 240; i++) {      /* let the embers rise */
            if (df) ui_draw_fame(ui, &fr.screen, &ff);
            else ui_draw_title(ui, &fr.screen, &tf);
        }
    }
    int rc = canvas_write_png(&fr.screen, path);
    if (rc) fprintf(stderr, "%s: cannot write %s\n", SHADESDEEP_NAME, path);
    else printf("%s: wrote %s (%dx%d, zoom %d, seed %u, depth %d, turn %d)\n", SHADESDEEP_NAME, path, w, h, fr.zoom,
                seed, g->depth, g->turn);
    /* SHADESDEEP_DUMP_FRAMES=N[,FPS]: then N more frames, FILE-001.png on, FPS a second (default 24) */
    int nframes = 0, ffps = 24;
    if (getenv("SHADESDEEP_DUMP_FRAMES")) sscanf(getenv("SHADESDEEP_DUMP_FRAMES"), "%d,%d", &nframes, &ffps);
    if (ffps < 1) ffps = 24;
    for (int i = 1; i <= nframes && !rc; i++) {
        game_update(g, 1.0f / ffps);
        draw_frame(r, ui, g, &fr, &hv, &ctl, mx, my, 1.0f / ffps, ffps, false);
        char fp[PATH_MAX + 16];
        const char *dot = strrchr(path, '.');
        int stem = dot && !strchr(dot, '/') ? (int)(dot - path) : (int)strlen(path);
        snprintf(fp, sizeof fp, "%.*s-%03d.png", stem, path, i);
        if ((rc = canvas_write_png(&fr.screen, fp))) fprintf(stderr, "%s: cannot write %s\n", SHADESDEEP_NAME, fp);
    }
    free(fr.screen.px);
    free(fr.world.px);
    render_free(r);
    game_free(g);
    ui_free(ui);
    image_free(&art);
    return rc ? 1 : 0;
}

static bool dir_key(const bool *rep, int *dx, int *dy)
{
    static const struct { int k, dx, dy; } M[] = {
        { K_LEFT, -1, 0 }, { K_RIGHT, 1, 0 }, { K_UP, 0, -1 }, { K_DOWN, 0, 1 },
        { 'h', -1, 0 }, { 'l', 1, 0 }, { 'k', 0, -1 }, { 'j', 0, 1 },
        { 'y', -1, -1 }, { 'u', 1, -1 }, { 'b', -1, 1 }, { 'n', 1, 1 },
        { K_KP4, -1, 0 }, { K_KP6, 1, 0 }, { K_KP8, 0, -1 }, { K_KP2, 0, 1 },
        { K_KP7, -1, -1 }, { K_KP9, 1, -1 }, { K_KP1, -1, 1 }, { K_KP3, 1, 1 },
        { K_HOME, -1, -1 }, { K_PGUP, 1, -1 }, { K_END, -1, 1 }, { K_PGDN, 1, 1 },
    };
    for (size_t i = 0; i < sizeof M / sizeof *M; i++)
        if (rep[M[i].k]) { *dx = M[i].dx; *dy = M[i].dy; return true; }
    return false;
}

typedef enum { SCR_TITLE, SCR_PLAY, SCR_FAME } Screen;

/* --hall-of-fame: the table, as text */
static int print_hall(const char *save_opt)
{
    char save_path[PATH_MAX] = "", path[PATH_MAX + 32];
    if (save_opt) snprintf(save_path, sizeof save_path, "%s", save_opt);
    else if (!save_default_path(save_path, sizeof save_path)) save_path[0] = 0;
    if (!fame_path_for(save_path, path, sizeof path)) { fprintf(stderr, "%s: nowhere to look for the hall of fame\n", SHADESDEEP_NAME); return 1; }
    static FameEntry e[FAME_MAX];
    int total, n = fame_load(path, e, FAME_MAX, &total);
    if (n == 0) { printf("The hall of fame is empty (%s).\n", path); return 0; }
    printf("%5s  %-34s %5s %6s %6s %5s  %s\n", "", "Fate", "Depth", "Turns", "Embers", "Kills", "Date");
    for (int i = 0; i < n; i++) {
        char date[16] = "?";
        time_t when = (time_t)e[i].when;
        struct tm tm;
        if (localtime_r(&when, &tm)) strftime(date, sizeof date, "%Y-%m-%d", &tm);
        printf("%4d.  %-34.34s %5d %6d %6d %5d  %s\n", i + 1, e[i].cause, e[i].deepest, e[i].turns, e[i].embers,
               e[i].kills, date);
    }
    if (total > n) printf("(and %d more)\n", total - n);
    return 0;
}

int main(int argc, char **argv)
{
    TxMode tx = TX_SHM;
    uint32_t seed = 0;
    bool new_game = false, no_save = false, no_title = false, print_fame = false;
    const char *save_opt = NULL;
    bool seed_given = false, probe_only = false, show_fps = false, start_muted = false, no_sound = false;
    int fps = 60, zoom_opt = 0, dump_w = 1300, dump_h = 700, dump_cell = 20, steps = 0, hover_x = -1, hover_y = -1;
    float tscale = 1.0f;
    const char *dump_path = NULL, *assets_opt = NULL;

    static const struct option opts[] = {
        { "seed",     required_argument, 0, 's' },
        { "zoom",     required_argument, 0, 'z' },
        { "text",     required_argument, 0, 't' },
        { "fps",      required_argument, 0, 'f' },
        { "mode",     required_argument, 0, 'm' },
        { "assets",   required_argument, 0, 'a' },
        { "show-fps", no_argument,       0, 'F' },
        { "mute",     no_argument,       0, 'Q' },
        { "no-sound", no_argument,       0, 'N' },
        { "probe",    no_argument,       0, 'P' },
        { "dump",     required_argument, 0, 'D' },
        { "size",     required_argument, 0, 'S' },
        { "cell",     required_argument, 0, 'C' },
        { "steps",    required_argument, 0, 'E' },
        { "hover",    required_argument, 0, 'H' },
        { "new",      no_argument,       0, 'W' },
        { "save",     required_argument, 0, 'L' },
        { "no-save",  no_argument,       0, 'X' },
        { "classic-art", no_argument,    0, 'O' },
        { "no-title", no_argument,       0, 'T' },
        { "hall-of-fame", no_argument,   0, 'A' },
        { "help",     no_argument,       0, 'h' },
        { "version",  no_argument,       0, 'V' },
        { 0, 0, 0, 0 }
    };
    int o;
    while ((o = getopt_long(argc, argv, "s:z:t:f:m:a:hV", opts, NULL)) != -1) {
        switch (o) {
        case 's': seed = (uint32_t)strtoul(optarg, NULL, 10); seed_given = true; break;
        case 'z': zoom_opt = atoi(optarg); if (zoom_opt < 1 || zoom_opt > 6) { fprintf(stderr, "%s: zoom must be 1-6\n", SHADESDEEP_NAME); return 2; } break;
        case 't': tscale = (float)atof(optarg); if (tscale < 0.5f || tscale > 3) { fprintf(stderr, "%s: text scale must be 0.5-3\n", SHADESDEEP_NAME); return 2; } break;
        case 'f': fps = atoi(optarg); if (fps < 10 || fps > 240) { fprintf(stderr, "%s: fps must be 10-240\n", SHADESDEEP_NAME); return 2; } break;
        case 'm': if (kgfx_parse_tx(optarg, &tx)) { fprintf(stderr, "%s: bad mode '%s'\n", SHADESDEEP_NAME, optarg); return 2; } break;
        case 'a': assets_opt = optarg; break;
        case 'F': show_fps = true; break;
        case 'Q': start_muted = true; break;
        case 'N': no_sound = true; break;
        case 'P': probe_only = true; break;
        case 'D': dump_path = optarg; break;
        case 'S': if (sscanf(optarg, "%dx%d", &dump_w, &dump_h) != 2) { fprintf(stderr, "%s: bad size\n", SHADESDEEP_NAME); return 2; } break;
        case 'C': dump_cell = atoi(optarg); break;
        case 'E': steps = atoi(optarg); break;
        case 'H': if (sscanf(optarg, "%d,%d", &hover_x, &hover_y) != 2) { fprintf(stderr, "%s: bad hover\n", SHADESDEEP_NAME); return 2; } break;
        case 'W': new_game = true; break;
        case 'L': save_opt = optarg; break;
        case 'X': no_save = true; break;
        case 'O': classic_art = true; break;
        case 'T': no_title = true; break;
        case 'A': print_fame = true; break;
        case 'h': usage(stdout); return 0;
        case 'V': printf("%s %s\n", SHADESDEEP_NAME, SHADESDEEP_VERSION); return 0;
        default:  usage(stderr); return 2;
        }
    }
    if (!seed_given) seed = (uint32_t)time(NULL) ^ (uint32_t)getpid() * 2654435761u;

    if (print_fame) return print_hall(save_opt);

    char assets[PATH_MAX];
    bool have_assets = find_assets(assets, sizeof assets, assets_opt) == 0;
    if (dump_path) {
        if (!have_assets) { fprintf(stderr, "%s: assets not found (use --assets DIR)\n", SHADESDEEP_NAME); return 1; }
        return dump_frame(assets, seed, dump_path, dump_w, dump_h, dump_cell, steps, zoom_opt, tscale, hover_x, hover_y);
    }

    if (term_init()) {
        fprintf(stderr, "%s: stdin and stdout must be a terminal\n", SHADESDEEP_NAME);
        return 1;
    }
    atexit(term_restore);
    TermCaps caps;
    TermSize ts;
    int probed = term_probe(&caps, 2.0);
    term_get_size(&ts);
    if (probe_only) {
        term_restore();
        if (probed) printf("warning: terminal did not answer the probe in time\n");
        print_caps(stdout, &caps, &ts);
        return caps.gfx ? 0 : 1;
    }
    const char *fatal = NULL;
    char err[512] = "";
    if (!caps.gfx) fatal = "this terminal does not support the kitty graphics protocol";
    else if (ts.xpix <= 0) fatal = "terminal does not report its pixel size";
    else if (!have_assets) fatal = "assets not found (use --assets DIR)";
    UI *ui = NULL;
    if (!fatal) {
        char fonts[PATH_MAX + 8];
        snprintf(fonts, sizeof fonts, "%s/fonts", assets);
        ui = ui_new(fonts, err, sizeof err);
        if (!ui) fatal = err;
    }
    if (fatal) {
        term_restore();
        fprintf(stderr, "%s: %s\n", SHADESDEEP_NAME, fatal);
        return 1;
    }
    if (tx == TX_SHM && !caps.gfx_shm) tx = TX_DIRECTZ;
    ui_set_scale(ui, ts.cellh, tscale);

    /* resume the saved game, if there is one */
    char save_path[PATH_MAX] = "", save_err[PATH_MAX + 160] = "";
    if (!no_save) {
        if (save_opt) snprintf(save_path, sizeof save_path, "%s", save_opt);
        else if (!save_default_path(save_path, sizeof save_path)) save_path[0] = 0;
    }
    Game *g = NULL;
    bool resumed = false;
    if (save_path[0] && !new_game && save_exists(save_path)) {
        bool incompatible;
        g = save_read(save_path, &incompatible, save_err, sizeof save_err);
        if (g) {
            game_resume(g);
            resumed = true;
        } else {
            /* keep it out of the way rather than overwrite it */
            char old[PATH_MAX + 8];
            snprintf(old, sizeof old, "%s.old", save_path);
            if (rename(save_path, old) == 0) {
                size_t l = strlen(save_err);
                snprintf(save_err + l, sizeof save_err - l, "; it was kept as %s and a new game begins", old);
            }
        }
    }
    if (!g) g = game_new(seed);
    if (save_err[0]) game_msg(g, C_WARN, "%s.", save_err);
    if (resumed && seed_given) game_msg(g, C_DIM, "(--seed is ignored when resuming; --new starts a fresh game)");
    else if (new_game && save_path[0] && save_exists(save_path))
        game_msg(g, C_DIM, "(a new game: your saved one will be replaced when you quit)");
    int saved_depth = g->depth;
    bool save_gone = false;
    char fame_path[PATH_MAX + 32] = "";
    if (save_path[0]) fame_path_for(save_path, fame_path, sizeof fame_path);
    static FameEntry fame_list[FAME_MAX], fame_mine;
    int fame_n = 0;
    Image title_art = { 0 };
    {
        char tp[PATH_MAX + 16];
        snprintf(tp, sizeof tp, "%s/title.png", assets);
        if (access(tp, R_OK) == 0 && image_load(&title_art, tp) == 0) ui_set_title_art(ui, &title_art);
    }
    Screen scr = no_title ? SCR_PLAY : SCR_TITLE, fame_back = SCR_TITLE;
    bool played = scr == SCR_PLAY;     /* this game has been played, so quitting saves it */
    TitleFrame tf = { .can_continue = resumed, .save_depth = g->depth, .save_turn = g->turn };
    Renderer *r = render_new();
    if (!classic_art) render_load_sprites(r, assets);
    ui_set_icons(ui, render_item_icons(r), render_item_icons_hi(r));
    if (!no_sound) {
        if (snd_init(assets) != 0) game_msg(g, C_DIM, "(no sound: %s)", snd_status());
        else if (start_muted) snd_toggle_mute();
    }
    Frame fr = { 0 };
    int zoom = zoom_opt ? zoom_opt : auto_zoom(ts.ypix);
    frame_resize(&fr, ts.xpix, ts.ypix, zoom);
    KGfx kg;
    kgfx_init(&kg, tx, SC_SELF);
    term_enter_game();
    render_camera(r, g, fr.world.w, fr.world.h, 0, true);

    static Hover hv;
    double last = now_s(), next = last, fps_t = last, quit_t = -10;
    int fps_frames = 0;
    float fps_now = 0;
    bool quit = false;
    Ctl ctl = { .tslot = -1 };
    int mx = -1, my = -1;

    while (!g_quit && !g_input.quit && !quit) {
        for (;;) {
            double rem = next - now_s();
            if (rem <= 0) break;
            struct timespec tw = { (time_t)rem, (long)((rem - (time_t)rem) * 1e9) };
            struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };
            if (ppoll(&pfd, 1, &tw, NULL) > 0) term_read_input();
            if (g_quit) break;
        }
        term_read_input();
        double t = now_s();
        float dt = (float)(t - last);
        if (dt > 0.1f) dt = 0.1f;
        last = t;
        next += 1.0 / fps;
        if (next < t) next = t + 1.0 / fps;

        if (g_resized) {
            g_resized = 0;
            term_get_size(&ts);
            ui_set_scale(ui, ts.cellh, tscale);
            if (!zoom_opt) zoom = auto_zoom(ts.ypix);
            out_str("\x1b[2J");
            out_flush();
        }
        frame_resize(&fr, ts.xpix, ts.ypix, zoom);

        if (g_input.mouse_seen) { mx = g_input.mx; my = g_input.my; }
        const bool *pr = g_input.pressed, *rep = g_input.rep;
        const uint8_t *pm = g_input.pmods;
        bool shift = g_input.down[K_SHIFT];
        #define SHIFTED(k) (pr[k] && (shift || (pm[k] & MOD_SHIFT)))
        #define PLAIN(k) (pr[k] && !(shift || (pm[k] & MOD_SHIFT)))
        bool any_key = false;
        for (int k = 0; k < K_MAX; k++)
            if (pr[k] && k != K_SHIFT && k != K_CTRL && k != K_ALT) any_key = true;

        /* the title screen and the hall of fame */
        if (scr != SCR_PLAY) {
            UiAction click = g_input.mclick[0] ? ui_hit(ui, mx, my, NULL) : UA_NONE;
            if (click != UA_NONE) snd_play(SND_CLICK, 1, 1, 0, 0);
            if (pr[K_F2]) snd_toggle_mute();
            if (snd_ready()) {      /* the hush of the deep, and a fire crackling somewhere close */
                snd_loop(LOOP_AMBIENCE, 0.6f);
                snd_loop(LOOP_FIRE, 0.55f);
                snd_loop(LOOP_TORCH, 0);
                snd_loop(LOOP_WATER, 0);
            }
            if (scr == SCR_TITLE) {
                UiAction items[4];
                int n = ui_title_menu(&tf, items, 4);
                if (tf.sel >= n) tf.sel = n - 1;
                if (tf.sel < 0) tf.sel = 0;
                UiAction act = UA_NONE;
                if (tf.confirm_new) {
                    if (pr['y'] || click == UA_YES) {
                        /* give up the saved game: into the hall of fame with it, then a fresh one */
                        if (fame_path[0]) {
                            FameEntry e;
                            char ferr[PATH_MAX + 64];
                            fame_from_game(g, FAME_ABANDONED, &e);
                            if (!fame_add(fame_path, &e, ferr, sizeof ferr)) fprintf(stderr, "%s: %s\n", SHADESDEEP_NAME, ferr);
                        }
                        save_remove(save_path);
                        game_free(g);
                        g = game_new((uint32_t)time(NULL) ^ seed++);
                        save_gone = false;
                        saved_depth = g->depth;
                        render_camera(r, g, fr.world.w, fr.world.h, 0, true);
                        tf.confirm_new = false;
                        tf.can_continue = false;
                        scr = SCR_PLAY;
                    } else if (pr['n'] || pr[K_ESC] || click == UA_NO || g_input.mclick[2]) {
                        tf.confirm_new = false;
                    }
                } else {
                    if (rep[K_UP] || rep[K_KP8] || rep['k']) tf.sel = (tf.sel + n - 1) % n;
                    if (rep[K_DOWN] || rep[K_KP2] || rep['j'] || pr[K_TAB]) tf.sel = (tf.sel + 1) % n;
                    if (g_input.moved) {
                        UiAction h = ui_hit(ui, mx, my, NULL);
                        for (int i = 0; i < n; i++) if (items[i] == h) tf.sel = i;
                    }
                    if (pr[K_ENTER] || pr[K_KPENTER] || pr[' ']) act = items[tf.sel];
                    if (PLAIN('c') && tf.can_continue) act = UA_CONTINUE;
                    if (PLAIN('n')) act = UA_NEW_GAME;
                    if (PLAIN('h')) act = UA_FAME;
                    if (PLAIN('q') || pr[K_ESC]) act = UA_QUIT;
                    if (click != UA_NONE) act = click;
                }
                switch (act) {
                case UA_CONTINUE: scr = SCR_PLAY; break;
                case UA_NEW_GAME:
                    if (tf.can_continue) tf.confirm_new = true;
                    else scr = SCR_PLAY;          /* the game waiting behind the title is a fresh one */
                    break;
                case UA_FAME:
                    fame_n = fame_load(fame_path[0] ? fame_path : NULL, fame_list, FAME_MAX, &ctl.fame_total);
                    fame_back = SCR_TITLE;
                    scr = SCR_FAME;
                    break;
                case UA_QUIT: quit = true; break;
                default: break;
                }
                if (scr == SCR_PLAY) {
                    played = true;
                    quit_t = -10;
                }
            } else if (pr[K_ESC] || pr[K_ENTER] || pr[K_KPENTER] || PLAIN('h') || PLAIN('q') || click == UA_CLOSE
                       || g_input.mclick[2]) {
                scr = fame_back;
            }
            input_clear_edges();
            if (scr != SCR_PLAY) {
                if (scr == SCR_TITLE) {
                    tf.mx = mx; tf.my = my; tf.dt = dt;
                    ui_draw_title(ui, &fr.screen, &tf);
                } else {
                    bool from_end = fame_back == SCR_PLAY;
                    FameFrame ff = { fame_list, fame_n, ctl.fame_total, from_end ? ctl.fame_rank : 0,
                                     from_end && ctl.fame_rank ? &fame_mine : NULL, mx, my, dt };
                    ui_draw_fame(ui, &fr.screen, &ff);
                }
                unsigned inflight = kg.sent - (g_input.gfx_ok + g_input.gfx_err);
                if (inflight >= MAX_INFLIGHT && t - fps_t < 0.25) continue;
                kgfx_present(&kg, &fr.screen, &ts);
                fps_frames++;
                if (t - fps_t >= 0.5) { fps_now = (float)(fps_frames / (t - fps_t)); fps_frames = 0; fps_t = t; }
                continue;
            }
        }

        bool end_screen = (g->state == GS_DEAD || g->state == GS_WON) && game_end_shown(g);
        /* Esc twice quits; once closes whatever is open */
        if (pr[K_ESC] && end_screen) {
            /* the closing screen's own: back to the title */
        } else if (pr[K_ESC]) {
            if (ctl.map) ctl.map = false;
            else if (ctl.help) ctl.help = false;
            else if (g->state == GS_JUMP_PROMPT) game_jump_answer(g, false);
            else if (ctl.targeting) ctl.targeting = false;
            else if (ctl.menu == MENU_ITEM) ctl.menu = MENU_INV;
            else if (ctl.menu != MENU_NONE) ctl.menu = MENU_NONE;
            else if (g->autom != AUTO_NONE) game_stop(g);
            else if (t - quit_t < 1.5) quit = true;
            else quit_t = t;
        } else if (any_key) {
            quit_t = -10;
        }
        ctl.quit_armed = t - quit_t < 1.5;

        /* wake the mouse on movement, clicks and the wheel; keys put it to sleep */
        static double mouse_t = -100;
        if (g_input.moved || g_input.mclick[0] || g_input.mclick[1] || g_input.mclick[2] || g_input.wheel) {
            mouse_t = t;
            hv.awake = true;
            hv.tip_arm = true;
        } else if (any_key) {
            hv.awake = false;
        }
        hv.tip_live = t - mouse_t < TIP_SECS;

        UiAction click = UA_NONE;
        int carg = 0;
        if (g_input.mclick[0]) click = ui_hit(ui, mx, my, &carg);
        if (click != UA_NONE) snd_play(SND_CLICK, 1, 1, 0, 0);
        if (pr[K_F2]) {
            snd_toggle_mute();
            if (snd_ready()) game_msg(g, C_DIM, snd_muted() ? "sound off (F2 turns it back on)." : "sound on.");
        }
        bool map_click = g_input.mclick[0] && click == UA_NONE && hv.hx >= 0 && !ui_over_panel(ui, mx, my);
        /* the status bar buttons work from any menu */
        if (click == UA_INV || click == UA_THROW || click == UA_TORCH || click == UA_EXPLORE || click == UA_REST ||
            click == UA_SEARCH || click == UA_STAIRS || click == UA_HELP || click == UA_MAP) {
            bool same = (click == UA_INV && ctl.menu == MENU_INV) || (click == UA_THROW && ctl.menu == MENU_THROW)
                     || (click == UA_MAP && ctl.map);
            ctl.menu = MENU_NONE;
            ctl.targeting = false;
            ctl.map = false;
            ctl.help = false;
            if (same) click = UA_NONE;
        }
        bool busy = game_busy(g);
        bool over = g->state == GS_DEAD || g->state == GS_WON;
        if (over && !save_gone) {
            save_remove(save_path);             /* death is final, and so is the end */
            save_gone = true;
            if (fame_path[0]) {
                char ferr[PATH_MAX + 64];
                fame_from_game(g, g->state == GS_WON ? FAME_WON : FAME_DIED, &fame_mine);
                ctl.fame_rank = fame_add(fame_path, &fame_mine, ferr, sizeof ferr);
                if (!ctl.fame_rank) game_msg(g, C_WARN, "%s.", ferr);
                fame_n = fame_load(fame_path, fame_list, FAME_MAX, &ctl.fame_total);
            }
        } else if (!over && g->depth != saved_depth && !busy) {
            save_now(g, save_path, save_err, sizeof save_err);   /* a new level: keep the run safe */
            saved_depth = g->depth;
        }

        if (over) {
            ctl.menu = MENU_NONE;
            ctl.targeting = false;
            if (!game_end_shown(g)) {
                /* a moment to watch it happen before the closing screen */
            } else if (pr[K_ENTER] || pr[K_KPENTER] || click == UA_NEW_GAME || pr[K_ESC] || click == UA_TITLE) {
                bool to_title = pr[K_ESC] || click == UA_TITLE;
                game_free(g);
                g = game_new((uint32_t)time(NULL) ^ seed++);
                save_gone = false;
                saved_depth = g->depth;
                ctl.fame_rank = 0;
                render_camera(r, g, fr.world.w, fr.world.h, 0, true);
                if (to_title) {
                    scr = SCR_TITLE;
                    played = false;
                    tf.can_continue = false;
                    tf.sel = 0;
                }
            } else if (PLAIN('h') || click == UA_FAME) {
                fame_n = fame_load(fame_path[0] ? fame_path : NULL, fame_list, FAME_MAX, &ctl.fame_total);
                fame_back = SCR_PLAY;
                scr = SCR_FAME;
            } else if (PLAIN('q') || click == UA_QUIT) {
                quit = true;
            }
        } else if (g->state == GS_JUMP_PROMPT) {
            if (pr['y'] || pr[K_ENTER] || click == UA_JUMP_YES) game_jump_answer(g, true);
            else if (pr['n'] || click == UA_JUMP_NO) game_jump_answer(g, false);
        } else if (ctl.help) {
            if (click == UA_CLOSE || pr[K_ENTER] || pr['?'] || SHIFTED('/')) ctl.help = false;
        } else if (ctl.map) {
            int tx, ty;
            if (pr['m'] || pr['M'] || pr[K_ENTER] || g_input.mclick[2]) {
                ctl.map = false;
            } else if (g_input.mclick[0] && click == UA_NONE) {
                if (map_tile_at(g, mx, my, &tx, &ty)) game_travel_to(g, tx, ty);
                ctl.map = false;
            }
        } else if (busy && g->proj.active) {
            /* something is in the air: wait for it */
        } else if (ctl.targeting) {
            int dx, dy;
            if (ctl.tslot >= g->ninv) ctl.targeting = false;
            if (g_input.moved && hv.hx >= 0) { ctl.tx = hv.hx; ctl.ty = hv.hy; }
            if (dir_key(rep, &dx, &dy)) {
                ctl.tx = clampi(ctl.tx + dx, 0, MAP_W - 1);
                ctl.ty = clampi(ctl.ty + dy, 0, MAP_H - 1);
            } else if (pr[K_TAB]) {
                Mob *ms[MAX_MOBS];
                int n = visible_mobs(g, ms, MAX_MOBS);
                if (n) {
                    ctl.tcycle = (ctl.tcycle + 1) % n;
                    ctl.tx = ms[ctl.tcycle]->x;
                    ctl.ty = ms[ctl.tcycle]->y;
                }
            } else if (g_input.mclick[2]) {
                ctl.targeting = false;
            } else if (map_click || pr[K_ENTER] || pr[K_KPENTER] || PLAIN('t')) {
                if (map_click) { ctl.tx = hv.hx; ctl.ty = hv.hy; }
                if (game_throw(g, ctl.tslot, ctl.tx, ctl.ty)) ctl.targeting = false;
            }
        } else if (ctl.menu != MENU_NONE) {
            int pick = -1;
            if (ctl.menu == MENU_FORGE) {
                int o = -1;
                for (int k = 0; k < FORGE_COUNT; k++)
                    if (PLAIN('a' + k)) o = k;
                if (click == UA_FORGE) o = carg;
                if (!game_by_forge(g)) ctl.menu = MENU_NONE;
                else if (o >= 0) game_forge_buy(g, (ForgeOffer)o);
                else if (map_click || g_input.mclick[2]) ctl.menu = MENU_NONE;
            } else if (ctl.menu == MENU_ITEM) {
                int sl = ctl.menu_slot;
                bool valid = sl >= 0 && sl < g->ninv;
                if (!valid) ctl.menu = MENU_INV;
                else if ((PLAIN('t') && ITEM_INFO[g->inv[sl].kind].range > 0) || click == UA_ITEM_THROW) start_aim(g, &ctl, sl, &hv);
                else if (((PLAIN('a') || PLAIN('q')) && ITEM_INFO[g->inv[sl].kind].usable) || click == UA_ITEM_USE) {
                    ctl.menu = MENU_NONE;
                    game_use(g, sl);
                } else if (PLAIN('d') || click == UA_ITEM_DROP) {
                    ctl.menu = MENU_NONE;
                    game_drop(g, sl);
                } else if (map_click || g_input.mclick[2]) {
                    ctl.menu = MENU_NONE;
                }
            } else {
                for (int k = 'a'; k <= 'z'; k++)
                    if (PLAIN(k) && k - 'a' < g->ninv && menu_allows(ctl.menu, g->inv[k - 'a'].kind)) pick = k - 'a';
                if (click == UA_PICK) pick = carg;
                if (ctl.menu == MENU_INV && PLAIN('i') && pick < 0) ctl.menu = MENU_NONE;
                else if (pick >= 0) {
                    switch (ctl.menu) {
                    case MENU_INV:   ctl.menu = MENU_ITEM; ctl.menu_slot = pick; break;
                    case MENU_THROW: start_aim(g, &ctl, pick, &hv); break;
                    case MENU_USE:   ctl.menu = MENU_NONE; game_use(g, pick); break;
                    case MENU_DROP:  ctl.menu = MENU_NONE; game_drop(g, pick); break;
                    default: break;
                    }
                } else if (map_click || g_input.mclick[2]) {
                    ctl.menu = MENU_NONE;
                }
            }
        } else {
            int dx, dy;
            if (any_key && g->autom != AUTO_NONE && !pr[K_ESC]) game_stop(g);
            if (dir_key(rep, &dx, &dy)) {
                if (!busy || dir_key(pr, &dx, &dy)) game_move(g, dx, dy);
            } else if (SHIFTED('z') || click == UA_REST) {
                game_rest_long(g);
            } else if (PLAIN('s') || click == UA_SEARCH) {
                game_search(g);
            } else if (PLAIN('z') || (pr['.'] && !SHIFTED('.')) || pr[K_KP5]) {
                if (!busy) game_rest(g);
            } else if (PLAIN('x') || click == UA_EXPLORE) {
                game_explore(g);
            } else if (SHIFTED('t') || click == UA_TORCH) {
                game_torch_toggle(g);
            } else if (PLAIN('i') || click == UA_INV) {
                ctl.menu = MENU_INV;
                game_stop(g);
            } else if (PLAIN('t') || click == UA_THROW) {
                if (g->ninv) ctl.menu = MENU_THROW;
                else game_msg(g, C_DIM, "you have nothing to throw.");
                game_stop(g);
            } else if (PLAIN('a') || PLAIN('q')) {
                ctl.menu = MENU_USE;
                game_stop(g);
            } else if (PLAIN('d')) {
                if (g->ninv) ctl.menu = MENU_DROP;
                game_stop(g);
            } else if (SHIFTED('.') || pr['>'] || click == UA_STAIRS) {
                game_to_stairs(g);
            } else if (SHIFTED(',') || pr['<']) {
                if (!game_ascend(g)) game_msg(g, C_DIM, "there are no stairs up here.");
            } else if (SHIFTED('/') || pr['?'] || click == UA_HELP) {
                ctl.help = true;
                game_stop(g);
            } else if (pr['m'] || pr['M'] || click == UA_MAP) {
                ctl.map = true;
                ctl.map_stale = true;
                game_stop(g);
            } else if (pr['='] || pr['+'] || pr[K_KPPLUS] || click == UA_ZOOM_IN || g_input.wheel > 0) {
                if (zoom < 6) zoom++;
                zoom_opt = zoom;
            } else if (pr['-'] || pr[K_KPMINUS] || click == UA_ZOOM_OUT || g_input.wheel < 0) {
                if (zoom > 1) zoom--;
                zoom_opt = zoom;
            } else if (map_click) {
                game_travel_to(g, hv.hx, hv.hy);
            }
        }
        if (ctl.help && g_input.mclick[0] && click == UA_CLOSE) ctl.help = false;
        if (g->forge_req) {
            g->forge_req = false;
            if (g->state == GS_PLAY && ctl.menu == MENU_NONE && !ctl.targeting) ctl.menu = MENU_FORGE;
        }
        #undef SHIFTED
        #undef PLAIN
        input_clear_edges();

        frame_resize(&fr, ts.xpix, ts.ypix, zoom);
        game_update(g, dt);
        play_sounds(g);
        update_loops(g);
        render_camera(r, g, fr.world.w, fr.world.h, dt, false);

        unsigned inflight = kg.sent - (g_input.gfx_ok + g_input.gfx_err);
        if (inflight >= MAX_INFLIGHT && t - fps_t < 0.25) continue;     /* kitty is behind: skip a frame */
        draw_frame(r, ui, g, &fr, &hv, &ctl, mx, my, dt, fps_now, show_fps);
        kgfx_present(&kg, &fr.screen, &ts);
        fps_frames++;
        if (t - fps_t >= 0.5) {
            fps_now = (float)(fps_frames / (t - fps_t));
            fps_frames = 0;
            fps_t = t;
        }
    }

    save_err[0] = 0;
    bool saved = played && save_now(g, save_path, save_err, sizeof save_err);
    snd_shutdown();
    kgfx_cleanup(&kg);
    term_restore();
    if (save_err[0]) fprintf(stderr, "%s: %s\n", SHADESDEEP_NAME, save_err);
    else if (saved) printf("Game saved (depth %d). Run %s again to carry on.\n", g->depth, SHADESDEEP_NAME);
    free(fr.screen.px);
    free(fr.world.px);
    render_free(r);
    game_free(g);
    ui_free(ui);
    image_free(&title_art);
    return 0;
}
