/* Lume: a graphical roguelike for the kitty terminal. The dungeon is drawn
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

#include "game.h"
#include "gfx.h"
#include "kgfx.h"
#include "render.h"
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

/* assets/ next to the executable, one level up (build/), ../share/lume, or ./assets */
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
        const char *rel[] = { "assets", "../assets", "../share/lume/assets" };
        for (int i = 0; i < 3; i++) {
            snprintf(out, n, "%s/%s", exe, rel[i]);
            if (dir_has_fonts(out)) return 0;
        }
    }
    snprintf(out, n, "assets");
    return dir_has_fonts(out) ? 0 : -1;
}

static void usage(FILE *f)
{
    fprintf(f,
        "Usage: %s [options]\n"
        "Lume, a graphical roguelike for the kitty terminal.\n"
        "\n"
        "  -s, --seed=N        dungeon seed (default: random)\n"
        "  -z, --zoom=N        pixel zoom (default: picked from the window height)\n"
        "  -t, --text=SCALE    text size relative to the terminal font (default 1.0)\n"
        "  -f, --fps=N         target frame rate (default 60)\n"
        "  -m, --mode=MODE     pixel transport: shm (default), direct, directz\n"
        "  -a, --assets=DIR    asset directory (default: found next to the binary)\n"
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
        "In the game press ? for the controls. Mouse: click to travel or attack,\n"
        "hover for descriptions, wheel to zoom. Esc twice quits.\n",
        LUME_NAME);
}

static void print_caps(FILE *o, const TermCaps *c, const TermSize *ts)
{
    fprintf(o, "%s %s capability probe\n", LUME_NAME, LUME_VERSION);
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
} Hover;

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

static void draw_frame(Renderer *r, UI *ui, Game *g, Frame *fr, Hover *hv, int mx, int my, float dt,
                       bool help, bool quit_armed, float fps, bool show_fps)
{
    hv->hx = hv->hy = -1;
    hv->plen = 0;
    bool over_ui = mx >= 0 && ui_over_panel(ui, mx, my);
    if (mx >= 0 && !over_ui && g->state == GS_PLAY && !help) {
        hover_tile(r, fr, mx, my, &hv->hx, &hv->hy);
        if (hv->hx >= 0 && g->autom == AUTO_NONE)
            hv->plen = game_path_preview(g, hv->hx, hv->hy, hv->path, MAX_PATH);
    }
    render_world(r, g, &fr->world, dt, hv->hx, hv->hy, hv->path, hv->plen);
    cv_upscale(&fr->screen, &fr->world, fr->zoom, 0, 0);
    float cx, cy;
    render_cam_pos(r, &cx, &cy);
    UiFrame uf = { (float)lroundf(cx), (float)lroundf(cy), fr->zoom, mx, my, hv->hx, hv->hy, help, quit_armed, fps, show_fps };
    ui_draw(ui, g, &fr->screen, &uf);
}

static int dump_frame(const char *assets, uint32_t seed, const char *path, int w, int h, int cellh,
                      int steps, int zoom, float tscale, int mx, int my)
{
    char err[256], fonts[PATH_MAX + 8];
    snprintf(fonts, sizeof fonts, "%s/fonts", assets);
    UI *ui = ui_new(fonts, err, sizeof err);
    if (!ui) { fprintf(stderr, "%s: %s\n", LUME_NAME, err); return 1; }
    ui_set_scale(ui, cellh, tscale);
    Game *g = game_new(seed);
    Renderer *r = render_new();
    Frame fr = { 0 };
    frame_resize(&fr, w, h, zoom ? zoom : auto_zoom(h));
    if (steps > 0) {
        game_explore(g);
        for (int i = 0; i < steps * 40 && g->state != GS_DEAD; i++) {
            if (g->state == GS_JUMP_PROMPT) game_jump_answer(g, false);
            if (g->autom == AUTO_NONE) {
                if (i % 40 == 0) game_explore(g);
            }
            game_update(g, 0.01f);
        }
        game_stop(g);
    }
    if (getenv("LUME_DUMP_CHASM") && !game_debug_to_chasm(g)) fprintf(stderr, "%s: no chasm on this level\n", LUME_NAME);
    /* LUME_DUMP_JUMP=N: jump down N chasms (developer test of level changes) */
    for (int j = getenv("LUME_DUMP_JUMP") ? atoi(getenv("LUME_DUMP_JUMP")) : 0; j > 0 && g->state == GS_PLAY; j--) {
        if (!game_debug_to_chasm(g)) break;
        game_move(g, 0, -1);
        game_move(g, 0, -1);
        if (g->state == GS_JUMP_PROMPT) game_jump_answer(g, true);
        if (g->state == GS_DEAD) break;
        g->player.hp = g->player.maxhp;
        for (int i = 0; i < 200; i++) game_update(g, 0.02f);
    }
    for (int i = 0; i < 120; i++) game_update(g, 1.0f / 60);
    Hover hv;
    render_camera(r, g, fr.world.w, fr.world.h, 0, true);
    /* two passes so the interface's hit areas exist for the hover test */
    draw_frame(r, ui, g, &fr, &hv, mx, my, 1.0f / 60, false, false, 60, false);
    for (int i = 0; i < 30; i++) {
        game_update(g, 1.0f / 60);
        draw_frame(r, ui, g, &fr, &hv, mx, my, 1.0f / 60, false, false, 60, false);
    }
    if (getenv("LUME_DUMP_LOWER")) render_dump_lower(r, getenv("LUME_DUMP_LOWER"));
    int rc = canvas_write_png(&fr.screen, path);
    if (rc) fprintf(stderr, "%s: cannot write %s\n", LUME_NAME, path);
    else printf("%s: wrote %s (%dx%d, zoom %d, seed %u, depth %d, turn %d)\n", LUME_NAME, path, w, h, fr.zoom,
                seed, g->depth, g->turn);
    free(fr.screen.px);
    free(fr.world.px);
    render_free(r);
    game_free(g);
    ui_free(ui);
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

int main(int argc, char **argv)
{
    TxMode tx = TX_SHM;
    uint32_t seed = 0;
    bool seed_given = false, probe_only = false, show_fps = false;
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
        { "probe",    no_argument,       0, 'P' },
        { "dump",     required_argument, 0, 'D' },
        { "size",     required_argument, 0, 'S' },
        { "cell",     required_argument, 0, 'C' },
        { "steps",    required_argument, 0, 'E' },
        { "hover",    required_argument, 0, 'H' },
        { "help",     no_argument,       0, 'h' },
        { "version",  no_argument,       0, 'V' },
        { 0, 0, 0, 0 }
    };
    int o;
    while ((o = getopt_long(argc, argv, "s:z:t:f:m:a:hV", opts, NULL)) != -1) {
        switch (o) {
        case 's': seed = (uint32_t)strtoul(optarg, NULL, 10); seed_given = true; break;
        case 'z': zoom_opt = atoi(optarg); if (zoom_opt < 1 || zoom_opt > 6) { fprintf(stderr, "%s: zoom must be 1-6\n", LUME_NAME); return 2; } break;
        case 't': tscale = (float)atof(optarg); if (tscale < 0.5f || tscale > 3) { fprintf(stderr, "%s: text scale must be 0.5-3\n", LUME_NAME); return 2; } break;
        case 'f': fps = atoi(optarg); if (fps < 10 || fps > 240) { fprintf(stderr, "%s: fps must be 10-240\n", LUME_NAME); return 2; } break;
        case 'm': if (kgfx_parse_tx(optarg, &tx)) { fprintf(stderr, "%s: bad mode '%s'\n", LUME_NAME, optarg); return 2; } break;
        case 'a': assets_opt = optarg; break;
        case 'F': show_fps = true; break;
        case 'P': probe_only = true; break;
        case 'D': dump_path = optarg; break;
        case 'S': if (sscanf(optarg, "%dx%d", &dump_w, &dump_h) != 2) { fprintf(stderr, "%s: bad size\n", LUME_NAME); return 2; } break;
        case 'C': dump_cell = atoi(optarg); break;
        case 'E': steps = atoi(optarg); break;
        case 'H': if (sscanf(optarg, "%d,%d", &hover_x, &hover_y) != 2) { fprintf(stderr, "%s: bad hover\n", LUME_NAME); return 2; } break;
        case 'h': usage(stdout); return 0;
        case 'V': printf("%s %s\n", LUME_NAME, LUME_VERSION); return 0;
        default:  usage(stderr); return 2;
        }
    }
    if (!seed_given) seed = (uint32_t)time(NULL) ^ (uint32_t)getpid() * 2654435761u;

    char assets[PATH_MAX];
    bool have_assets = find_assets(assets, sizeof assets, assets_opt) == 0;
    if (dump_path) {
        if (!have_assets) { fprintf(stderr, "%s: assets not found (use --assets DIR)\n", LUME_NAME); return 1; }
        return dump_frame(assets, seed, dump_path, dump_w, dump_h, dump_cell, steps, zoom_opt, tscale, hover_x, hover_y);
    }

    if (term_init()) {
        fprintf(stderr, "%s: stdin and stdout must be a terminal\n", LUME_NAME);
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
        fprintf(stderr, "%s: %s\n", LUME_NAME, fatal);
        return 1;
    }
    if (tx == TX_SHM && !caps.gfx_shm) tx = TX_DIRECTZ;
    ui_set_scale(ui, ts.cellh, tscale);

    Game *g = game_new(seed);
    Renderer *r = render_new();
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
    bool help = false, quit = false;
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

        /* Esc twice quits; once closes whatever is open */
        if (pr[K_ESC]) {
            if (help) help = false;
            else if (g->state == GS_JUMP_PROMPT) game_jump_answer(g, false);
            else if (g->autom != AUTO_NONE) game_stop(g);
            else if (t - quit_t < 1.5) quit = true;
            else quit_t = t;
        } else if (any_key) {
            quit_t = -10;
        }
        bool quit_armed = t - quit_t < 1.5;

        UiAction click = UA_NONE;
        if (g_input.mclick[0]) click = ui_hit(ui, mx, my);

        if (g->state == GS_DEAD) {
            if (pr[K_ENTER] || pr[K_KPENTER] || click == UA_NEW_GAME) {
                game_free(g);
                g = game_new((uint32_t)time(NULL) ^ seed++);
                render_camera(r, g, fr.world.w, fr.world.h, 0, true);
            } else if (PLAIN('q') || click == UA_QUIT) {
                quit = true;
            }
        } else if (g->state == GS_JUMP_PROMPT) {
            if (pr['y'] || pr[K_ENTER] || click == UA_JUMP_YES) game_jump_answer(g, true);
            else if (pr['n'] || click == UA_JUMP_NO) game_jump_answer(g, false);
        } else if (help) {
            if (click == UA_CLOSE || pr[K_ENTER] || pr['?'] || SHIFTED('/')) help = false;
        } else {
            int dx, dy;
            if (any_key && g->autom != AUTO_NONE && !pr[K_ESC]) game_stop(g);
            if (dir_key(rep, &dx, &dy)) {
                if (!game_busy(g) || dir_key(pr, &dx, &dy)) game_move(g, dx, dy);
            } else if (SHIFTED('z') || click == UA_REST) {
                game_rest_long(g);
            } else if (PLAIN('z') || (pr['.'] && !SHIFTED('.')) || pr[K_KP5] || PLAIN('s')) {
                if (!game_busy(g)) game_rest(g);
            } else if (PLAIN('x') || click == UA_EXPLORE) {
                game_explore(g);
            } else if (SHIFTED('.') || pr['>'] || click == UA_STAIRS) {
                game_to_stairs(g);
            } else if (SHIFTED(',') || pr['<']) {
                if (!game_ascend(g)) game_msg(g, C_DIM, "there are no stairs up here.");
            } else if (SHIFTED('/') || pr['?'] || click == UA_HELP) {
                help = true;
                game_stop(g);
            } else if (pr['='] || pr['+'] || pr[K_KPPLUS] || click == UA_ZOOM_IN || g_input.wheel > 0) {
                if (zoom < 6) zoom++;
                zoom_opt = zoom;
            } else if (pr['-'] || pr[K_KPMINUS] || click == UA_ZOOM_OUT || g_input.wheel < 0) {
                if (zoom > 1) zoom--;
                zoom_opt = zoom;
            } else if (g_input.mclick[0] && click == UA_NONE && hv.hx >= 0) {
                game_travel_to(g, hv.hx, hv.hy);
            }
        }
        if (help && g_input.mclick[0] && click == UA_CLOSE) help = false;
        #undef SHIFTED
        #undef PLAIN
        input_clear_edges();

        frame_resize(&fr, ts.xpix, ts.ypix, zoom);
        game_update(g, dt);
        render_camera(r, g, fr.world.w, fr.world.h, dt, false);

        unsigned inflight = kg.sent - (g_input.gfx_ok + g_input.gfx_err);
        if (inflight >= MAX_INFLIGHT && t - fps_t < 0.25) continue;     /* kitty is behind: skip a frame */
        draw_frame(r, ui, g, &fr, &hv, mx, my, dt, help, quit_armed, fps_now, show_fps);
        kgfx_present(&kg, &fr.screen, &ts);
        fps_frames++;
        if (t - fps_t >= 0.5) {
            fps_now = (float)(fps_frames / (t - fps_t));
            fps_frames = 0;
            fps_t = t;
        }
    }

    kgfx_cleanup(&kg);
    term_restore();
    free(fr.screen.px);
    free(fr.world.px);
    render_free(r);
    game_free(g);
    ui_free(ui);
    return 0;
}
