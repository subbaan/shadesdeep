/* On-screen interface drawn at native resolution: status bar, action
 * buttons, message log, hover cards, dialogs and help. */
#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stddef.h>

#include "fame.h"
#include "game.h"
#include "gfx.h"

typedef enum {
    UA_NONE, UA_EXPLORE, UA_REST, UA_STAIRS, UA_HELP, UA_ZOOM_IN, UA_ZOOM_OUT,
    UA_JUMP_YES, UA_JUMP_NO, UA_NEW_GAME, UA_QUIT, UA_CLOSE,
    UA_INV, UA_THROW, UA_TORCH, UA_PICK, UA_ITEM_THROW, UA_ITEM_USE, UA_ITEM_DROP, UA_MENU_BACK,
    UA_MAP, UA_FORGE,
    UA_CONTINUE, UA_FAME, UA_TITLE, UA_YES, UA_NO,
} UiAction;

/* where the map overview sits on screen, and which tiles it shows */
typedef struct {
    int x, y, w, h;         /* the map picture, screen pixels */
    int tx, ty, tw, th;     /* tile rectangle shown */
    float cell;             /* screen pixels per tile */
} MapLayout;

/* item menus, Brogue style: pick from the pack, then act */
typedef enum { MENU_NONE, MENU_INV, MENU_THROW, MENU_USE, MENU_DROP, MENU_ITEM, MENU_FORGE } MenuKind;

static inline bool menu_allows(MenuKind m, ItemKind k)
{
    if (m == MENU_USE) return ITEM_INFO[k].usable;
    if (m == MENU_THROW) return ITEM_INFO[k].range > 0;
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
    bool tip;               /* show the hover card for it (the mouse moved there lately) */
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
    int fame_rank;          /* where the run that just ended stands in the hall of fame (0 = unrecorded) */
    int fame_total;         /* how many runs the hall holds */
} UiFrame;

void ui_map_layout(const UI *ui, Game *g, int sw, int sh, MapLayout *m);

void ui_draw(UI *ui, Game *g, Canvas *cv, const UiFrame *f);
void ui_set_icons(UI *ui, const Image *icons, const Image *icons_hi);   /* item pictures for menus (hi may be NULL) */
UiAction ui_hit(const UI *ui, int mx, int my, int *arg);    /* button under the mouse */
bool ui_over_panel(const UI *ui, int mx, int my);   /* mouse is over interface, not the map */

/* ---- the title screen and the hall of fame */

/* the title picture (not owned; NULL for the drawn one) */
void ui_set_title_art(UI *ui, const Image *art);

typedef struct {
    bool can_continue;      /* there's a saved game to go back to */
    int save_depth, save_turn;
    bool confirm_new;       /* asking before a new game abandons the saved one */
    int sel;                /* keyboard-highlighted entry, an index into ui_title_menu's list */
    int mx, my;
    float dt;
} TitleFrame;

/* the title menu's entries, top to bottom; returns how many */
int  ui_title_menu(const TitleFrame *t, UiAction *out, int max);
void ui_draw_title(UI *ui, Canvas *cv, const TitleFrame *t);

typedef struct {
    const FameEntry *e;     /* best first */
    int n;                  /* entries in e */
    int total;              /* runs in the hall (may be more than n) */
    int mine;               /* rank of the run just ended, to pick it out (0 = none) */
    const FameEntry *mine_e;    /* that run, for when it ranks below the ones shown */
    int mx, my;
    float dt;
} FameFrame;

void ui_draw_fame(UI *ui, Canvas *cv, const FameFrame *f);

#endif
