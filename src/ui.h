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
    UA_INV, UA_THROW, UA_TORCH, UA_PICK, UA_ITEM_THROW, UA_ITEM_USE, UA_ITEM_DROP, UA_MENU_BACK,
    UA_MAP,
} UiAction;

/* where the map overview sits on screen, and which tiles it shows */
typedef struct {
    int x, y, w, h;         /* the map picture, screen pixels */
    int tx, ty, tw, th;     /* tile rectangle shown */
    float cell;             /* screen pixels per tile */
} MapLayout;

/* item menus, Brogue style: pick from the pack, then act */
typedef enum { MENU_NONE, MENU_INV, MENU_THROW, MENU_USE, MENU_DROP, MENU_ITEM } MenuKind;

static inline bool menu_allows(MenuKind m, ItemKind k)
{
    if (m == MENU_USE) return ITEM_INFO[k].usable;
    return true;
}

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
    MenuKind menu;
    int menu_slot;          /* the item MENU_ITEM is about */
    bool targeting;         /* aiming a throw */
    int target_slot;
    bool map;               /* the map overview is open */
    const Canvas *mini;     /* its picture */
    MapLayout ml;
} UiFrame;

void ui_map_layout(const UI *ui, Game *g, int sw, int sh, MapLayout *m);

void ui_draw(UI *ui, Game *g, Canvas *cv, const UiFrame *f);
void ui_set_icons(UI *ui, const Image *icons);          /* item pictures for menus */
UiAction ui_hit(const UI *ui, int mx, int my, int *arg);    /* button under the mouse */
bool ui_over_panel(const UI *ui, int mx, int my);   /* mouse is over interface, not the map */

#endif
