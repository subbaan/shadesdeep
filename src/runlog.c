/* The run log. One line per event, tab-separated, appended and never rewritten.
 * The columns are named in the file's header comment. */
#include "runlog.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "version.h"

bool runlog_path_for(const char *save_path, char *out, size_t n)
{
    if (!save_path || !save_path[0]) return false;
    const char *slash = strrchr(save_path, '/');
    int w = slash ? snprintf(out, n, "%.*s/runlog.txt", (int)(slash - save_path), save_path)
                  : snprintf(out, n, "runlog.txt");
    return w > 0 && (size_t)w < n;
}

static int count_of(const Game *g, ItemKind k)
{
    int c = 0;
    for (int i = 0; i < g->ninv; i++) if (g->inv[i].kind == k) c += g->inv[i].count;
    return c;
}

static const char *gear_name(ItemKind k)
{
    const char *s = ITEM_INFO[k].name;
    return s && s[0] ? s : "?";
}

bool runlog_write(const char *path, const Game *g, const char *event, char *err, size_t n)
{
    if (!path || !path[0]) { snprintf(err, n, "nowhere to keep the run log"); return false; }
    struct stat st;
    bool fresh = stat(path, &st) != 0;
    FILE *f = fopen(path, "a");
    if (!f) { snprintf(err, n, "can't write %s: %s", path, strerror(errno)); return false; }
    if (fresh) {
        fprintf(f, "# Shadesdeep run log, for tuning: a line when a run reaches a new depth, and one when it ends.\n"
                   "# Damage columns are totals over the run so far (dmg_other: falls, fire, snares, swimming).\n"
                   "# when\tseed\tversion\tevent\tdepth\tturn\thp\tmaxhp\tchill\tweapon\twplus\tarmour\taplus\theart"
                   "\tlantern\tembers\tembers_got\tkills\tpotions\tpotions_drunk\ttorches\thp_low");
        for (int k = MK_PLAYER + 1; k < MK_COUNT; k++) {
            char nm[32];
            snprintf(nm, sizeof nm, "%s", MOB_INFO[k].name);
            for (char *p = nm; *p; p++) if (*p == ' ') *p = '_';
            fprintf(f, "\tdmg_%s", nm);
        }
        fprintf(f, "\tdmg_other\tcause\n");
    }
    const char *cause = "";
    if (!strcmp(event, "died")) cause = game_death_cause(g);
    char cs[80];
    snprintf(cs, sizeof cs, "%s", cause && cause[0] ? cause : "-");
    for (char *p = cs; *p; p++) if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
    fprintf(f, "%lld\t%u\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%s\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
            (long long)time(NULL), g->seed, SHADESDEEP_VERSION, event, g->depth, g->turn,
            g->player.hp, game_max_hp(g), g->chill,
            gear_name(g->weapon.kind), g->weapon.plus, gear_name(g->armour.kind), g->armour.plus,
            g->heart, g->lantern, g->embers, g->embers_got, g->kills,
            count_of(g, IT_POTION), g->potions_drunk, count_of(g, IT_TORCH), g->hp_low);
    for (int k = MK_PLAYER + 1; k < MK_COUNT; k++) fprintf(f, "\t%d", g->dmg_by[k]);
    fprintf(f, "\t%d\t%s\n", g->dmg_other, cs);
    if (fclose(f) != 0) { snprintf(err, n, "can't write %s: %s", path, strerror(errno)); return false; }
    return true;
}
