/* On-screen interface drawn at native resolution: status bar, action
 * buttons, message log, hover cards, dialogs and help. */
#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stddef.h>

#include "game.h"
#include "gfx.h"

typedef enum {
    UA_NONE, UA_EXPLORE, UA_REST, UA_STAIRS, UA_HELP, UA_ZOOM_IN, UA_ZOOM_OUT,
    UA_JUMP_YES, UA_JUMP_NO, UA_NEW_GAME, UA_QUIT, UA_CLOSE,
} UiAction;

typedef struct UI UI;

UI  *ui_new(const char *font_dir, char *err, size_t errn);
void ui_free(UI *ui);
/* text size from the terminal's cell height (so kitty's font zoom scales it) */
void ui_set_scale(UI *ui, int cell_h, float scale);

typedef struct {
    float camx, camy;       /* world camera, in world pixels */
    int zoom;
    int mx, my;             /* mouse, or -1 */
    int hover_x, hover_y;   /* map tile under the mouse, or -1 */
    bool help;
    bool quit_armed;        /* Esc pressed once */
    float fps;
    bool show_fps;
} UiFrame;

void ui_draw(UI *ui, Game *g, Canvas *cv, const UiFrame *f);
UiAction ui_hit(const UI *ui, int mx, int my);      /* button under the mouse */
bool ui_over_panel(const UI *ui, int mx, int my);   /* mouse is over interface, not the map */

#endif
