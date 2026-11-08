/* Saving and resuming a game: quitting saves, starting resumes, dying deletes. */
#ifndef SAVE_H
#define SAVE_H

#include <stdbool.h>
#include <stddef.h>

#include "game.h"

/* Where the save lives: $XDG_DATA_HOME/shadesdeep/save.gz, else ~/.local/share/shadesdeep/save.gz.
 * Returns false if there's no home to put it in. */
bool save_default_path(char *out, size_t n);

/* Write the game to path (atomically: a temporary file renamed into place).
 * Returns 0, or -1 with a reason in err. */
int save_write(const Game *g, const char *path, char *err, size_t n);

/* Read a game back. NULL with a reason in err if it can't; *incompatible is set
 * when the file is a save from a different build that this one can't read. */
Game *save_read(const char *path, bool *incompatible, char *err, size_t n);

bool save_exists(const char *path);
void save_remove(const char *path);

#endif
