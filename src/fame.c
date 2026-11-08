/* The hall of fame. One run per line, tab-separated, appended and never
 * rewritten, so a crash can't take the older runs with it:
 *
 *   when  seed  end  depth  deepest  turns  embers  kills  sconces  version  cause
 */
#include "fame.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "version.h"

bool fame_path_for(const char *save_path, char *out, size_t n)
{
    if (!save_path || !save_path[0]) return false;
    const char *slash = strrchr(save_path, '/');
    int w = slash ? snprintf(out, n, "%.*s/halloffame.txt", (int)(slash - save_path), save_path)
                  : snprintf(out, n, "halloffame.txt");
    return w > 0 && (size_t)w < n;
}

void fame_from_game(const Game *g, FameEnd end, FameEntry *e)
{
    memset(e, 0, sizeof *e);
    e->when = (long long)time(NULL);
    e->seed = g->seed;
    e->end = end;
    e->depth = g->depth;
    e->deepest = g->deepest > g->depth ? g->deepest : g->depth;
    e->turns = g->turn;
    e->embers = g->embers_got;
    e->kills = g->kills;
    e->sconces = g->sconces_lit;
    if (end == FAME_WON) snprintf(e->cause, sizeof e->cause, "Rekindled the Heartflame");
    else if (end == FAME_ABANDONED) snprintf(e->cause, sizeof e->cause, "Gave up on depth %d", g->depth);
    else {
        const char *c = game_death_cause(g);
        snprintf(e->cause, sizeof e->cause, "%s", c[0] ? c : "Perished");
    }
    snprintf(e->version, sizeof e->version, "%s", SHADESDEEP_VERSION);
}

int fame_cmp(const FameEntry *a, const FameEntry *b)
{
    int aw = a->end == FAME_WON, bw = b->end == FAME_WON;
    if (aw != bw) return bw - aw;
    if (a->deepest != b->deepest) return b->deepest - a->deepest;
    if (a->embers != b->embers) return b->embers - a->embers;
    if (a->kills != b->kills) return b->kills - a->kills;
    /* a win is better for being quick; a death for being put off longer */
    if (a->turns != b->turns) return aw ? a->turns - b->turns : b->turns - a->turns;
    return a->when < b->when ? -1 : a->when > b->when;
}

static int cmp_qsort(const void *a, const void *b) { return fame_cmp(a, b); }

/* parse one line; false for comments and anything malformed */
static bool parse(char *line, FameEntry *e)
{
    if (line[0] == '#' || line[0] == '\n' || !line[0]) return false;
    line[strcspn(line, "\r\n")] = 0;
    char *f[11];
    int nf = 0;
    for (char *p = line; nf < 11; ) {
        f[nf++] = p;
        char *t = strchr(p, '\t');
        if (!t) break;
        *t = 0;
        p = t + 1;
    }
    if (nf < 11) return false;
    memset(e, 0, sizeof *e);
    char *end;
    e->when = strtoll(f[0], &end, 10);
    if (*end) return false;
    e->seed = (uint32_t)strtoul(f[1], NULL, 10);
    int v = atoi(f[2]);
    e->end = v == FAME_WON ? FAME_WON : v == FAME_ABANDONED ? FAME_ABANDONED : FAME_DIED;
    e->depth = atoi(f[3]);
    e->deepest = atoi(f[4]);
    e->turns = atoi(f[5]);
    e->embers = atoi(f[6]);
    e->kills = atoi(f[7]);
    e->sconces = atoi(f[8]);
    snprintf(e->version, sizeof e->version, "%s", f[9]);
    snprintf(e->cause, sizeof e->cause, "%s", f[10]);
    return e->deepest > 0;
}

int fame_load(const char *path, FameEntry *out, int max, int *total)
{
    if (total) *total = 0;
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f) return 0;
    /* read everything, sort, keep the best `max` */
    int cap = 64, n = 0;
    FameEntry *all = malloc(sizeof *all * (size_t)cap);
    char line[512];
    while (all && fgets(line, sizeof line, f)) {
        FameEntry e;
        if (!parse(line, &e)) continue;
        if (n == cap) {
            FameEntry *bigger = realloc(all, sizeof *all * (size_t)(cap *= 2));
            if (!bigger) break;
            all = bigger;
        }
        all[n++] = e;
    }
    fclose(f);
    if (!all) return 0;
    if (total) *total = n;
    qsort(all, (size_t)n, sizeof *all, cmp_qsort);
    int k = n < max ? n : max;
    memcpy(out, all, sizeof *all * (size_t)k);
    free(all);
    return k;
}

/* make the directory the file goes in, if it's missing (one level, like the save's) */
static int make_dir_for(const char *path)
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

/* the text of a field, with nothing in it that would break the line apart */
static void clean(char *out, size_t n, const char *s)
{
    snprintf(out, n, "%s", s);
    for (char *p = out; *p; p++)
        if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
}

int fame_add(const char *path, const FameEntry *e, char *err, size_t n)
{
    if (!path || !path[0]) { snprintf(err, n, "nowhere to keep the hall of fame"); return 0; }
    /* rank it against the runs already there: ties go to the older run */
    int rank = 1;
    FILE *f = fopen(path, "r");
    bool fresh = !f;
    if (f) {
        char line[512];
        FameEntry o;
        while (fgets(line, sizeof line, f))
            if (parse(line, &o) && fame_cmp(&o, e) <= 0) rank++;
        fclose(f);
    }
    if (make_dir_for(path) != 0) { snprintf(err, n, "can't make the folder for %s: %s", path, strerror(errno)); return 0; }
    f = fopen(path, "a");
    if (!f) { snprintf(err, n, "can't write %s: %s", path, strerror(errno)); return 0; }
    char ver[16], cause[80];
    clean(ver, sizeof ver, e->version);
    clean(cause, sizeof cause, e->cause);
    if (fresh) fprintf(f, "# Shadesdeep hall of fame, one run per line (tab-separated):\n"
                          "# when\tseed\tend (0 died, 1 won, 2 gave up)\tdepth\tdeepest\tturns\tembers\tkills\tsconces\tversion\tcause\n");
    int w = fprintf(f, "%lld\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%s\n", e->when, e->seed, (int)e->end, e->depth,
                    e->deepest, e->turns, e->embers, e->kills, e->sconces, ver[0] ? ver : "?", cause[0] ? cause : "?");
    if (fclose(f) != 0 || w < 0) { snprintf(err, n, "can't write %s: %s", path, strerror(errno)); return 0; }
    return rank;
}
