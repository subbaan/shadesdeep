/* The hall of fame: every finished run, kept as one line of text in a file next
 * to the save, ranked by how well it went. */
#ifndef FAME_H
#define FAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "game.h"

#define FAME_MAX 500        /* the most runs read back (the best ones are kept) */

typedef enum { FAME_DIED, FAME_WON, FAME_ABANDONED } FameEnd;

typedef struct {
    long long when;         /* unix time the run ended */
    uint32_t seed;
    FameEnd end;
    int depth;              /* where it ended */
    int deepest;            /* how far down it got */
    int turns, embers, kills, sconces;
    char cause[80];         /* "Killed by a goblin", "Rekindled the Heartflame" */
    char version[16];
} FameEntry;

/* $dir/halloffame.txt, where $dir holds the save at save_path */
bool fame_path_for(const char *save_path, char *out, size_t n);

/* Describe a finished (or abandoned) game. */
void fame_from_game(const Game *g, FameEnd end, FameEntry *e);

/* Read the hall, best first: up to max entries into out. Returns how many were
 * read (0 if there's no file); *total (may be NULL) gets the number of runs in all. */
int fame_load(const char *path, FameEntry *out, int max, int *total);

/* Add a run to the hall. Returns its rank among all the runs (1 = best), or 0 if
 * it couldn't be written (with the reason in err). */
int fame_add(const char *path, const FameEntry *e, char *err, size_t n);

/* <0 if a ranks above b */
int fame_cmp(const FameEntry *a, const FameEntry *b);

#endif
