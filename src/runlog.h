/* The run log: a line of numbers each time a run reaches a new depth, and one
 * when it ends, appended to runlog.txt next to the save. It's for tuning the
 * game's difficulty from real runs; the game never reads it back. */
#ifndef RUNLOG_H
#define RUNLOG_H

#include <stdbool.h>
#include <stddef.h>

#include "game.h"

/* $dir/runlog.txt, where $dir holds the save at save_path */
bool runlog_path_for(const char *save_path, char *out, size_t n);

/* Append one line about g: event is "start", "level", "died", "won" or "gaveup".
 * Returns false (with the reason in err) if it couldn't be written. */
bool runlog_write(const char *path, const Game *g, const char *event, char *err, size_t n);

#endif
