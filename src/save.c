/* Saving and resuming a game.
 *
 * The save is the Game and each generated Level written as they are in memory
 * (they hold no pointers apart from the level table, which is rebuilt), gzip
 * compressed. A build can read a save only if the structures are laid out the
 * same, so the header carries a fingerprint of the layout; SAVE_FORMAT must be
 * bumped by hand when the meaning of saved data changes without its layout
 * changing (enum values reordered, a field reused). */
#include "save.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include "version.h"

#define SAVE_MAGIC  "SHADESDP"
#define SAVE_FORMAT 2u

typedef struct {
    char magic[8];
    uint32_t format;
    uint32_t layout;        /* fingerprint of the structures' sizes and offsets */
    char version[16];       /* the build that wrote it, for messages */
    uint32_t game_size, level_size;
} Header;

static uint32_t mix32(uint32_t h, uint32_t v)
{
    h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
}

static uint32_t layout_print(void)
{
    uint32_t h = 2166136261u;
    const size_t parts[] = {
        sizeof(Game), sizeof(Level), sizeof(Mob), sizeof(FloorItem), sizeof(Slot), sizeof(Msg), sizeof(Light),
        offsetof(Game, player), offsetof(Game, inv), offsetof(Game, weapon), offsetof(Game, embers),
        offsetof(Game, items), offsetof(Game, mobs), offsetof(Game, rng), offsetof(Mob, lvl), offsetof(Mob, surfaced),
        offsetof(Level, lights), offsetof(Level, lr), MAX_DEPTH, MAX_MOBS, MAX_ITEMS, MAX_INV, MK_COUNT, IT_COUNT, T_COUNT,
    };
    for (size_t i = 0; i < sizeof parts / sizeof *parts; i++) h = mix32(h, (uint32_t)parts[i]);
    return h;
}

bool save_default_path(char *out, size_t n)
{
    const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    int w;
    if (xdg && xdg[0] == '/') w = snprintf(out, n, "%s/shadesdeep/save.gz", xdg);
    else if (home && home[0]) w = snprintf(out, n, "%s/.local/share/shadesdeep/save.gz", home);
    else return false;
    return w > 0 && (size_t)w < n;
}

/* make the directory the save goes in (and its parent), if they're missing */
static int make_dirs(const char *path)
{
    char d[PATH_MAX];
    snprintf(d, sizeof d, "%s", path);
    char *slash = strrchr(d, '/');
    if (!slash || slash == d) return 0;
    *slash = 0;
    struct stat st;
    if (stat(d, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
    char *up = strrchr(d, '/');
    if (up && up != d) {
        *up = 0;
        if (stat(d, &st) != 0 && mkdir(d, 0755) != 0 && errno != EEXIST) return -1;
        *up = '/';
    }
    return mkdir(d, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static bool put(gzFile f, const void *p, size_t n)
{
    const char *c = p;
    while (n > 0) {
        unsigned chunk = n > (1u << 30) ? (1u << 30) : (unsigned)n;
        if (gzwrite(f, c, chunk) != (int)chunk) return false;
        c += chunk;
        n -= chunk;
    }
    return true;
}

static bool get(gzFile f, void *p, size_t n)
{
    char *c = p;
    while (n > 0) {
        unsigned chunk = n > (1u << 30) ? (1u << 30) : (unsigned)n;
        if (gzread(f, c, chunk) != (int)chunk) return false;
        c += chunk;
        n -= chunk;
    }
    return true;
}

int save_write(const Game *g, const char *path, char *err, size_t n)
{
    char tmp[PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (make_dirs(path) != 0) {
        snprintf(err, n, "can't make the save folder for %s: %s", path, strerror(errno));
        return -1;
    }
    gzFile f = gzopen(tmp, "wb6");
    if (!f) {
        snprintf(err, n, "can't write %s: %s", tmp, strerror(errno));
        return -1;
    }
    Header h = { .format = SAVE_FORMAT, .layout = layout_print(),
                 .game_size = sizeof(Game), .level_size = sizeof(Level) };
    memcpy(h.magic, SAVE_MAGIC, 8);
    snprintf(h.version, sizeof h.version, "%s", SHADESDEEP_VERSION);
    bool ok = put(f, &h, sizeof h) && put(f, g, sizeof *g);
    for (int d = 0; ok && d < MAX_DEPTH + 2; d++) {
        uint8_t have = g->lv[d] != NULL;
        ok = put(f, &have, 1) && (!have || put(f, g->lv[d], sizeof(Level)));
    }
    if (gzclose(f) != Z_OK) ok = false;
    if (!ok || rename(tmp, path) != 0) {
        snprintf(err, n, "couldn't write the save %s", path);
        unlink(tmp);
        return -1;
    }
    return 0;
}

Game *save_read(const char *path, bool *incompatible, char *err, size_t n)
{
    *incompatible = false;
    gzFile f = gzopen(path, "rb");
    if (!f) {
        snprintf(err, n, "can't open %s: %s", path, strerror(errno));
        return NULL;
    }
    Header h;
    if (!get(f, &h, sizeof h) || memcmp(h.magic, SAVE_MAGIC, 8) != 0) {
        gzclose(f);
        *incompatible = true;
        snprintf(err, n, "%s isn't a save this game can read", path);
        return NULL;
    }
    h.version[sizeof h.version - 1] = 0;
    if (h.format != SAVE_FORMAT || h.layout != layout_print() || h.game_size != sizeof(Game)
        || h.level_size != sizeof(Level)) {
        gzclose(f);
        *incompatible = true;
        snprintf(err, n, "the saved game is from Shadesdeep %s, which this version (%s) can't read", h.version, SHADESDEEP_VERSION);
        return NULL;
    }
    Game *g = malloc(sizeof *g);
    if (!g || !get(f, g, sizeof *g)) {
        free(g);
        gzclose(f);
        snprintf(err, n, "the save %s is cut short", path);
        return NULL;
    }
    bool ok = true;
    for (int d = 0; d < MAX_DEPTH + 2; d++) g->lv[d] = NULL;
    for (int d = 0; ok && d < MAX_DEPTH + 2; d++) {
        uint8_t have;
        ok = get(f, &have, 1) && have <= 1;
        if (ok && have) {
            g->lv[d] = malloc(sizeof(Level));
            ok = g->lv[d] && get(f, g->lv[d], sizeof(Level));
        }
    }
    gzclose(f);
    /* sanity: the things the game indexes with */
    ok = ok && g->depth >= 1 && g->depth <= MAX_DEPTH && g->lv[g->depth] && g->ninv >= 0 && g->ninv <= MAX_INV
         && in_map(g->player.x, g->player.y) && g->state <= GS_WON && g->nmsgs >= 0 && g->nfx >= 0 && g->nfx <= MAX_FX
         && g->nsnd >= 0 && g->nsnd <= MAX_SND;
    if (!ok) {
        game_free(g);
        snprintf(err, n, "the save %s is damaged", path);
        return NULL;
    }
    return g;
}

bool save_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

void save_remove(const char *path)
{
    if (save_exists(path)) unlink(path);
}
