// What the keys, characters, wheel and clicks do: the list's cursor and scroll, the sidebar's,
// find, sort, the history, the selection, the edit line and the prompts, and the one action a
// frame may ask of gui/ (open, go somewhere, toggle hidden files, sort again, change files).
// raylib-free and unit-tested; gui/main.c feeds it (docs/plan.md, F1 "Keys", F2 and F3).

#include "../base/base.h"

#include <limits.h>
#include <locale.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wctype.h>

enum { UI_EVENTS_MAX = 64, UI_FIND_MAX = 256, UI_HISTORY_MAX = 64, UI_JOB_ROWS = 4, UI_APPS_MAX = 1024 };

// The keys without text, as gui/ reads them (a chord already named). Characters come apart.
typedef enum {
    UK_NONE,
    UK_UP, UK_DOWN, UK_PAGEUP, UK_PAGEDOWN, UK_HOME, UK_END,
    UK_ENTER, UK_RIGHT, UK_LEFT, UK_BACKSPACE, UK_ESC, UK_TAB,
    UK_BACK,       // Alt+Left
    UK_FORWARD,    // Alt+Right
    UK_FIND,       // Ctrl+F
    UK_HIDDEN,     // Ctrl+H
    UK_F2,         // rename
    UK_DELETE,     // to the trash; in the edit line, the character after the caret
    UK_SHIFT_DELETE,   // for good
    UK_ALL,        // Ctrl+A
    UK_COPY,       // Ctrl+C
    UK_CUT,        // Ctrl+X
    UK_PASTE,      // Ctrl+V
    UK_UNDO,       // Ctrl+Z
    UK_NEWFOLDER,  // Ctrl+Shift+N
    UK_LOCATION,   // Ctrl+L
    UK_PARENT,     // Alt+Up
    UK_TAB_NEW,    // Ctrl+T
    UK_NEW_WINDOW, // Ctrl+N (F7.1)
    UK_TAB_CLOSE,  // Ctrl+W
    UK_TAB_NEXT,   // Ctrl+Tab, Ctrl+PgDn
    UK_TAB_PREV,   // Ctrl+Shift+Tab, Ctrl+PgUp
    UK_TAB_LEFT,   // Ctrl+Shift+PgUp: the tab moves
    UK_TAB_RIGHT,  // Ctrl+Shift+PgDn
    UK_TAB_JUMP,   // Alt+1 .. Alt+9: the event's ch is the digit
    UK_SEL_LEFT,   // Shift+Left: in an edit line the selection grows; elsewhere as Left
    UK_SEL_RIGHT,  // Shift+Right
    UK_SEL_HOME,   // Shift+Home
    UK_SEL_END,    // Shift+End
    UK_SEL_UP,     // Shift+Up: in the files the selection grows that way (Michael, 2026-10-26); elsewhere as Up
    UK_SEL_DOWN,   // Shift+Down
    UK_SHIFT_ENTER,   // on a path button: a background tab; elsewhere as Enter
    UK_CRUMBS,     // Ctrl+K: the keyboard on the path buttons (F5.2)
    UK_JOBS,       // Ctrl+J: the keyboard on the job rows' × (F5.3)
    UK_QUIT,       // the window closed, or another window's Close All (F7.1)
    UK_QUIT_ALL,   // Ctrl+Q: every ofx window closes (F7.1)
    UK_F10,        // the menu bar's first menu (F7.1)
    UK_MENU,       // Alt and a letter: the menu it opens (F7.1); the event's ch is the letter, lower case
    UK_PROPERTIES, // Ctrl+I: the Properties panel (F7.2)
    UK_TERMINAL,   // Alt+Enter: a terminal here, as t (Michael, 2026-10-25)
    UK_F9,         // the sidebar's next mode (F6)
    UK_VIEW_ICONS, // Ctrl+2: the files as icons (F6.2)
    UK_VIEW_LIST,  // Ctrl+1: the files as the list
    UK_SIDE,       // Ctrl+Left: the keys to the sidebar (Tab goes there and back)
    UK_ZOOM_IN,    // Ctrl+= (Ctrl++, the keypad's +): the icon view's icons a size bigger (F6.3)
    UK_ZOOM_OUT,   // Ctrl+-
    UK_ZOOM_RESET, // Ctrl+0: the first size, 48 px
} UiKey;

// The top bar's buttons (F4).
typedef enum { UB_NONE, UB_BACK, UB_FORWARD, UB_UP, UB_LOCATION, UB_SEARCH, UB_LEFT, UB_RIGHT, UB_CRUMB,
               UB_RESTORE, UB_EMPTY,   // the trash view's (F6)
               UB_VIEW_LIST, UB_VIEW_ICONS,   // the view buttons (F6.2)
               UB_ZOOM_OUT, UB_ZOOM_IN } UiBar;   // the zoom slider's minus and plus (F6.3), in the footer

enum { UI_MOD_CTRL = 1, UI_MOD_SHIFT = 2 };

// One key or one typed character, in the order they came: a find needs `/`, then text, then Enter.
typedef struct {
    UiKey key;   // UK_NONE: the character ch
    u32   ch;
} UiEvent;

typedef struct {
    UiEvent ev[UI_EVENTS_MAX];
    int     nev;
    int     wheel;       // notches over the list, positive away from the user (up)
    int     click;       // the list row pressed (0 = the first row on screen; in the icon view the
                         // cell, counted across then down from the first on screen), -1 for none
    int     click_mods;  // UI_MOD_CTRL, UI_MOD_SHIFT held for that press
    int     dclick;      // a list row double-clicked, -1 for none
    int     side_click;  // a sidebar row pressed, -1 for none
    int     header;      // a column header clicked (a SortKey), -1 for none
    UiBar   bar;         // a top bar button clicked (UB_CRUMB: path button `crumb`)
    int     crumb;
    int     tab_click;   // a tab clicked (its place in the bar), -1 for none
    int     tab_close;   // a tab's × clicked, or the tab middle-clicked, -1 for none
    int     mid_row, mid_side, mid_crumb;   // a middle click on a list row, a place, a path button
    int     modal;       // a modal panel's button (F5.1): UI_MODAL_*, 0 for none
    int     backs, forwards;   // presses of the mouse's back and forward buttons
    Str     paste;       // the clipboard's text for a Ctrl+V in an edit line (gui/ reads it first)
    int     crumb_n;     // the path buttons there are, and the rightmost one shown (F5.2)
    int     crumb_last;
    // The job rows (F5.3): each running job's id, top row first; a row's × clicked (its place), -1
    // for none; and how many jobs there are in all, running and waiting.
    int     job_n;
    int     job_id[UI_JOB_ROWS];
    int     job_x;
    int     jobs_all;
    int     side_mode;   // a sidebar mode button clicked (a SideMode, F6), -1 for none
    int     side_twisty; // a tree row's open/close mark clicked (its row on screen), -1 for none
    int     side_eject;  // a drive's eject button clicked (its row on screen, F6.1), -1 for none
    int     zoom_wheel;  // Ctrl+wheel notches over the files, positive up: the icons bigger (F6.3)
    int     zoom_to;     // the zoom slider's track pressed or dragged: the level (UI_ZOOM_PX), -1 for none
    // The menu bar (F7.1): a menu's title pressed, an entry of the open menu pressed (its place in
    // the menu), each -1 for none; a press anywhere else while a menu is open.
    int     menu_title;
    int     menu_item;
    bool    menu_away;
    int     prop_box;    // a checkbox of the Properties panel's grid pressed (0..8, F7.2), -1 for none
    // Open With (F7.3): the apps gui/ lists, in its order (their names, which the filter
    // matches); a row pressed or double-clicked (its place on screen), the field pressed, a
    // toggle pressed (1 case, 2 whole word, 3 regular expression; for find too), each -1 or 0 for none.
    const Str *app_name;
    isize   app_n;
    int     ow_row, ow_drow;
    bool    ow_field;
    int     ow_wheel;    // notches over its list, as wheel
    int     toggle;      // 4: Open With's "Always open … with this app" box (F7.3.1)
    bool    find_x;      // the find bar's × pressed
    bool    find_field;  // its field pressed: the keys go there
    int     find_step;   // its chevrons: -1 the previous match, 1 the next, 0 none
} UiInput;

typedef enum {
    UA_NONE, UA_OPEN, UA_PARENT, UA_TERMINAL, UA_HOMEDIR, UA_BACK, UA_FORWARD, UA_HIDDEN, UA_SORT, UA_PLACE,
    UA_RENAME,      // the edit line's Enter: its text is the new name (the line stays open until
                    // gui/ closes it, so a refused name can be fixed)
    UA_NEWFOLDER, UA_TRASH, UA_DELETE, UA_COPY, UA_CUT, UA_PASTE, UA_UNDO,
    UA_ANSWER,      // a clash answered: index is the Answer, all "the rest the same"
    UA_CANCEL_JOB,  // index: the job's id, asked and answered
    UA_QUIT,        // asked first when jobs run or wait
    UA_LOCATION,    // the location line, open or closed (gui/ fills it with the folder)
    UA_GO,          // the location line's Enter: its text is where to go (open until gui/ closes it)
    UA_CRUMB,       // a path button: index
    UA_CRUMB_SHIFT, // the path buttons' arrows: index -1 or 1
    UA_TAB_NEW,     // Ctrl+T
    UA_TAB_CLOSE,   // index: the tab's place, -1 the current one
    UA_TAB_SHOW,    // index: the tab's place
    UA_TAB_STEP,    // index: -1 or 1, wrapping
    UA_TAB_MOVE,    // index: -1 or 1, the current tab moves
    UA_TAB_JUMP,    // index: 1..9 (9 the last)
    UA_TAB_BG_ROW,  // a middle click: entry index in a new tab in the background
    UA_TAB_BG_PLACE,
    UA_TAB_BG_CRUMB,
    UA_SIDE_MODE,   // the sidebar's mode changed (Ui.side_mode): gui/ remembers it
    UA_TREE_LEFT,   // index: the tree row Left was pressed on (gui/ closes it, or goes up a row)
    UA_TREE_RIGHT,  // index: the tree row Right was pressed on (opens it, or goes down into it)
    UA_TREE_TOGGLE, // index: the tree row whose mark was clicked
    UA_RESTORE,     // the trash view: the marked items (or the cursor's) back where they came from
    UA_EMPTY_TRASH, // the trash view: everything in it deleted for good, asked and answered
    UA_NOT_IN_TRASH,   // something that doesn't work in the trash view (gui/ says so)
    UA_EJECT,       // index: the sidebar row of the drive to eject (F6.1)
    UA_VIEW,        // the view changed (Ui.grid, F6.2): gui/ lays it out again and remembers it
    UA_ZOOM,        // the icon view's size changed (Ui.zoom, F6.3): gui/ refits it (ui_refit) and remembers it
    UA_NEW_WINDOW,  // another ofx on this folder (F7.1)
    UA_NEWFILE,     // an empty file, named in the row as a new folder is (F7.1)
    UA_CLOSE_ALL,   // the other ofx windows asked to close (F7.1); all: this one quits too, else it
                    // asks first (quit_asking), its jobs running or waiting
    UA_PROPS,       // the Properties panel opened (Ui.props): gui/ reads what it shows (F7.2)
    UA_CHMOD,       // index: the grid's place (0..8) whose permission bit flips
    UA_OPEN_WITH,   // the Open With panel opened (Ui.ow): gui/ lists the apps (F7.3)
    UA_LAUNCH,      // index: the app (its place in gui/'s list) to open the files with; all: made the
                    // type's default first (F7.3.1)
} UiActionKind;

typedef struct {
    UiActionKind kind;
    isize        index;   // UA_OPEN: the entry; UA_PLACE: the place; UA_ANSWER: the answer
    bool         all;     // UA_ANSWER: for all the rest; UA_OPEN: the marked entries, not only index (Enter, F7.3)
} UiAction;

// The clash answers, as fs/job.c numbers them (Answer); ui/ doesn't include it.
enum { UI_ANS_SKIP = 1, UI_ANS_REPLACE = 2, UI_ANS_KEEP = 3, UI_ANS_CANCEL = 4 };

// The modal panels' buttons (F5.1): the delete question's, then the clash question's (the answers
// above, and the "same for the rest" box).
// The cancel, quit and queue-full panels (F5.3) use YES and NO, and OK.
enum { UI_MODAL_YES = 1, UI_MODAL_NO, UI_MODAL_SKIP, UI_MODAL_REPLACE, UI_MODAL_KEEP, UI_MODAL_CANCEL, UI_MODAL_ALL, UI_MODAL_OK };

// The menu bar (F7.1, docs/plan.md "F7 design"): its menus, each a list of entries; UM_SEP is a
// line between groups.
typedef enum {
    UM_SEP, UM_NEW_TAB, UM_NEW_WINDOW, UM_NEW_FOLDER, UM_NEW_FILE, UM_OPEN, UM_OPEN_WITH, UM_PROPERTIES, UM_CLOSE_ALL, UM_CLOSE,
    UM_ITEMS
} UiMenuItem;

// Each entry's label, its key as the menu shows it, and its name in a script's state line.
static const struct { const char *label, *key, *word; } UI_MENU_ITEM[UM_ITEMS] = {
    [UM_SEP] = { "", "", "-" },
    [UM_NEW_TAB] = { "New Tab", "Ctrl+T", "new-tab" },
    [UM_NEW_WINDOW] = { "New Window", "Ctrl+N", "new-window" },
    [UM_NEW_FOLDER] = { "Create New Folder", "Ctrl+Shift+N", "new-folder" },
    [UM_NEW_FILE] = { "Create New Empty File", "", "new-file" },
    [UM_OPEN] = { "Open", "Enter", "open" },
    [UM_OPEN_WITH] = { "Open With Other Application\xE2\x80\xA6", "", "open-with" },
    [UM_PROPERTIES] = { "Properties\xE2\x80\xA6", "Ctrl+I", "properties" },
    [UM_CLOSE_ALL] = { "Close All Windows", "Ctrl+Q", "close-all" },
    [UM_CLOSE] = { "Close", "Ctrl+W", "close" },
};

static const UiMenuItem UI_MENU_FILE[] = {
    UM_NEW_TAB, UM_NEW_WINDOW, UM_NEW_FOLDER, UM_NEW_FILE, UM_SEP, UM_OPEN, UM_OPEN_WITH, UM_SEP, UM_PROPERTIES, UM_SEP,
    UM_CLOSE_ALL, UM_CLOSE,
};

// The menus in the bar's order: a title, its Alt letter, its entries. F7.4 on adds Edit, View, Go,
// Bookmarks and Help.
static const struct { const char *title; char letter; const UiMenuItem *items; int n; } UI_MENU[] = {
    { "File", 'f', UI_MENU_FILE, sizeof UI_MENU_FILE / sizeof UI_MENU_FILE[0] },
};
enum { UI_MENUS = sizeof UI_MENU / sizeof UI_MENU[0] };

// The icon view's sizes (F6.3, Michael 2026-10-24): Yaru's 32 and 48, and its 256 scaled down
// between them. The list doesn't zoom.
static const int UI_ZOOM_PX[] = { 32, 48, 64, 96, 128, 256 };
enum { UI_ZOOMS = sizeof UI_ZOOM_PX / sizeof UI_ZOOM_PX[0], UI_ZOOM_DEFAULT = 48 };

typedef struct {
    isize n;      // entries in the list
    isize sel;    // the cursor, 0..n-1; 0 for an empty list
    isize top;    // the first entry on screen (in a grid, the first of a row)
    int   rows;   // how many rows fit
    int   cols;   // entries a row: the icon view's columns (F6.2); 0 or 1 for a list
} UiList;

// The entries' names, for find: gui/ hands its listing in through this; and which are folders,
// for F2's selection (NULL: none are).
typedef struct {
    Str (*at)(void *ctx, isize i);
    void *ctx;
    bool (*dir)(void *ctx, isize i);
} UiNames;

// How find and Open With's filter match (F7.3, Michael 2026-10-26, as VS Code's three toggles):
// case as typed, whole words only, the text a regular expression (POSIX extended).
typedef struct {
    bool cased, word, regex;
} UiMatchOpts;

// A text to match, ready: a regular expression compiled once, or why it doesn't compile.
typedef struct {
    UiMatchOpts o;
    char        text[UI_FIND_MAX + 1];
    int         len;
    bool        re_on;     // re holds the compiled expression
    bool        bad;       // it doesn't compile: nothing matches, err says why
    regex_t     re;
    char        err[128];
} UiMatcher;

typedef struct {
    UiList list, side;
    SideMode side_mode;      // places, tree or hidden (F6)
    bool   grid;             // the files as icons in a grid, not the list (F6.2)
    int    zoom;             // the icon view's icon size in px, one of UI_ZOOM_PX; 0: UI_ZOOM_DEFAULT (F6.3)
    bool   side_focus;       // the keys move the sidebar's cursor
    const u8 *side_head;     // a byte per sidebar row, 1 for a heading (F6), never the cursor's; NULL: none
    bool   finding;          // the find bar is open (Michael, 2026-10-26: until Esc or its ×)
    bool   find_keys;        // and has the keys
    bool   find_miss;        // its text matches nothing
    char   find[UI_FIND_MAX];
    int    find_len;
    isize  find_from;        // the cursor when the find line opened
    char   last[UI_FIND_MAX];   // the last text found, for n and N
    int    last_len;
    UiMatchOpts find_opts;   // find's toggles, kept from one find to the next (F7.3)
    UiMatcher   find_m;      // the find line's text (or the last, for n and N) as it matches
    Sort   sort;

    // The selection: gui/ gives one byte per entry of the listing (in the listing's arena), and
    // keeps it by name across a reload.
    u8    *mark;
    isize  marked;           // how many are set
    // The edit line: a rename (a new folder is made at once and renamed with it), or the top bar's
    // location line (F4), which holds a path.
    bool   editing;
    bool   edit_path;        // the location line
    char   edit[PATH_MAX];
    int    edit_len, caret;  // bytes
    int    anchor;           // the selection's other end (F5.2): selected between it and the caret
    // A Ctrl+C or Ctrl+X in the edit line: the text gui/ puts on the clipboard, then clears copied.
    bool   copied;
    char   copy[PATH_MAX];
    int    copy_len;
    // The keyboard on the path buttons (Ctrl+K, F5.2): the one marked.
    bool   crumb_focus;
    int    crumb_at;
    // The prompts and the job: gui/ says when one runs, and when it asks about a clash.
    bool   confirming;       // "Delete … for good? Y/n"
    isize  confirm_n;
    bool   asking;           // a job waits for a clash answer
    bool   ask_all;          // its "same for the rest" box (the capitals too)
    // The job rows (F5.3): the keyboard on their × (Ctrl+J), the row marked; "Cancel this job?"
    // about job cancel_id; "Quit and cancel them?"; "No more jobs can be queued".
    bool   jobs_focus;
    int    job_at;
    bool   cancel_asking;
    int    cancel_id;
    bool   quit_asking;
    bool   full_told;
    // The trash view (F6): gui/ says when it is shown; "Empty the trash?" up.
    bool   in_trash;
    bool   empty_asking;
    // The menu bar (F7.1): a menu open, and its entry marked (-1: none, as a menu the pointer
    // opened). gui/ says whether the folder shown takes new files.
    bool   menu_on;
    int    menu;
    int    menu_at;
    bool   can_write;
    // The Properties panel (F7.2): up, its grid's place with the focus (0..8: owner read, write,
    // execute, then group's, others'), and whether gui/ lets its permissions change.
    bool   props;
    int    prop_at;
    bool   prop_can;
    // Open With (F7.3): up; the filter field's text and caret (bytes); the keys in the list, not the
    // field; the toggles; the apps the filter shows (their places in gui/'s list) and the cursor
    // over them; ow_fresh until the list is filtered from gui/'s apps.
    bool   ow;
    bool   ow_in_list;
    bool   ow_fresh;
    char   ow_text[UI_FIND_MAX];
    int    ow_len, ow_caret;
    UiMatchOpts ow_opts;
    UiMatcher   ow_m;
    UiList ow_list;
    isize  ow_idx[UI_APPS_MAX];
    // Set as default (F7.3.1): the box ticked, and whether gui/ lets it be (an empty file's type,
    // or one a list read first sets, can't).
    bool   ow_def;
    bool   ow_def_can;
} Ui;

static UiInput ui_input_none(void)
{
    return (UiInput){ .click = -1, .dclick = -1, .side_click = -1, .header = -1, .click_mods = 0, .bar = UB_NONE, .crumb = -1,
                      .tab_click = -1, .tab_close = -1, .mid_row = -1, .mid_side = -1, .mid_crumb = -1, .modal = 0, .job_x = -1, .side_mode = -1, .side_twisty = -1, .side_eject = -1,
                      .zoom_to = -1, .menu_title = -1, .menu_item = -1, .prop_box = -1, .ow_row = -1, .ow_drow = -1 };
}

static isize ui_clamp(isize v, isize lo, isize hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// Entries a row: 1 in a list.
static isize ui_cols(const UiList *l)
{
    return l->cols > 1 ? l->cols : 1;
}

// The scroll as far down as it goes: the first entry of the row that puts the last row at the bottom.
static isize ui_max_top(const UiList *l)
{
    isize rows = l->rows > 0 ? l->rows : 1, c = ui_cols(l), all = (l->n + c - 1) / c;
    return all > rows ? (all - rows) * c : 0;
}

// The scroll at top, in range. Its callers keep it on the first entry of a row: ui_scroll moves
// whole rows, and the rest call ui_show_sel after it, which puts it there.
static isize ui_top_at(const UiList *l, isize top)
{
    return ui_clamp(top, 0, ui_max_top(l));
}

// The cursor on screen: the scroll moves as little as it must, a row at a time.
static void ui_show_sel(UiList *l)
{
    isize rows = l->rows > 0 ? l->rows : 1, c = ui_cols(l), row = l->sel / c, top = l->top / c;
    if (row < top) top = row;
    if (row >= top + rows) top = row - rows + 1;
    l->top = ui_top_at(l, top * c);
}

// A new list (a folder entered, or the same one reloaded) of n entries, the cursor on sel. The
// scroll keeps its place where it can, so a reload doesn't jump.
static void ui_set(UiList *l, isize n, isize sel)
{
    l->n = n;
    l->sel = n > 0 ? ui_clamp(sel, 0, n - 1) : 0;
    l->top = ui_top_at(l, l->top);
    ui_show_sel(l);
}

// A new window height: rows fit.
static void ui_resize(UiList *l, int rows)
{
    l->rows = rows;
    l->top = ui_top_at(l, l->top);
    ui_show_sel(l);
}

// A new number of columns (the icon view's width changed, or the view itself: 1 for the list). The
// scroll keeps the row its first entry was on, the cursor on screen.
static void ui_set_cols(UiList *l, int cols)
{
    l->cols = cols;
    l->top = ui_top_at(l, l->top);
    ui_show_sel(l);
}

// New columns and rows after a zoom (F6.3): the cursor's cell stays on its row on screen where the
// new rows allow (its last row when there are fewer), not wherever the least scroll leaves it.
static void ui_refit(UiList *l, int cols, int rows)
{
    isize c = ui_cols(l), on = l->sel / c - l->top / c;
    l->cols = cols, l->rows = rows;
    c = ui_cols(l);
    l->top = (l->sel / c - on) * c;   // out of range here: ui_show_sel brings it in, the cursor on screen
    ui_show_sel(l);
}

// The wheel: notches rows up (positive) or down, the cursor left where it is.
static void ui_scroll(UiList *l, int notches)
{
    l->top = ui_top_at(l, l->top - (isize)notches * ui_cols(l));
}

static void ui_move(UiList *l, isize by)
{
    if (l->n == 0) return;
    l->sel = ui_clamp(l->sel + by, 0, l->n - 1);
    ui_show_sel(l);
}

// The icon view's cursor `rows` rows down (up when negative), in its column: past the first row it
// stays in the first; past the last, in the last row's cell under it, or on the last entry when
// that row ends before its column (as Nemo does).
static void ui_move_rows(UiList *l, isize rows)
{
    if (l->n == 0) return;
    isize c = ui_cols(l), col = l->sel % c, to = l->sel + rows * c;
    if (to < 0) to = col;
    if (to >= l->n) to = (l->n - 1) / c * c + col;
    if (to >= l->n) to = l->n - 1;
    l->sel = to;
    ui_show_sel(l);
}

static bool ui_side_head(const Ui *u, isize i)
{
    return u->side_head && i >= 0 && i < u->side.n && u->side_head[i];
}

// The sidebar's cursor off a heading: on the way it moved (dir 1 down, -1 up), else back the
// other way; a heading right above it kept in view.
static void ui_side_settle(Ui *u, int dir)
{
    UiList *s = &u->side;
    isize i = s->sel;
    while (ui_side_head(u, i)) i += dir;
    if (i < 0 || i >= s->n)
        for (i = s->sel; ui_side_head(u, i);) i -= dir;
    if (i >= 0 && i < s->n) s->sel = i;
    ui_show_sel(s);
    if (s->top == s->sel && ui_side_head(u, s->sel - 1)) s->top--;
}

// Whether in holds something the user did: a key, a character, a press of a button (not the
// wheel, nor the pointer moving). A status message gives way to the status line then (F6).
static bool ui_input_acts(const UiInput *in)
{
    return in->nev > 0 || in->click >= 0 || in->dclick >= 0 || in->side_click >= 0 || in->header >= 0 || in->bar != UB_NONE ||
           in->tab_click >= 0 || in->tab_close >= 0 || in->mid_row >= 0 || in->mid_side >= 0 || in->mid_crumb >= 0 || in->modal > 0 ||
           in->backs > 0 || in->forwards > 0 || in->job_x >= 0 || in->side_mode >= 0 || in->side_twisty >= 0 || in->side_eject >= 0 ||
           in->zoom_wheel != 0 || in->zoom_to >= 0 || in->menu_title >= 0 || in->menu_item >= 0 || in->menu_away || in->prop_box >= 0 ||
           in->ow_row >= 0 || in->ow_drow >= 0 || in->ow_field || in->toggle > 0 || in->find_x || in->find_field || in->find_step;
}

// Whether a status message gives way to the status line on input that acts (ui_input_acts): a
// plain one at once; an error only from a second after it came, so one that lands while keys are
// being pressed (a job failing) is seen (reviewer-2, F6 review).
enum { UI_ERROR_MIN_MS = 1000 };

static bool ui_status_clears(bool bad, i64 at_ms, i64 now_ms)
{
    return !bad || now_ms - at_ms >= UI_ERROR_MIN_MS;
}

// The sidebar in mode m: a hidden one gives the keys back to the list.
static UiAction ui_side_mode(Ui *u, SideMode m)
{
    u->side_mode = m;
    if (m == SIDE_HIDDEN) u->side_focus = false;
    return (UiAction){ UA_SIDE_MODE, m, false };
}

// The files' cursor one step (Michael, 2026-10-25): past either end it goes round, one step at a
// time only (PgUp, PgDn, Home and End stop at the ends). In the icon view by is a cell (Left,
// Right) or, rows true, a row (Up, Down): a row's move keeps its column, onto the last row that
// has it.
static void ui_step_round(UiList *l, isize by, bool rows)
{
    if (l->n == 0) return;
    isize c = rows ? ui_cols(l) : 1, col = l->sel % ui_cols(l), last_row = (l->n - 1) / ui_cols(l) * ui_cols(l);
    if (!rows) {
        l->sel = by < 0 ? (l->sel == 0 ? l->n - 1 : l->sel - 1) : (l->sel == l->n - 1 ? 0 : l->sel + 1);
    } else if (by < 0 && l->sel < c) {   // the top row: up to the bottom row's cell in this column
        l->sel = last_row + col < l->n ? last_row + col : last_row + col - c;
    } else if (by > 0 && l->sel >= last_row) {   // the bottom row: down to the top
        l->sel = col;
    } else {
        ui_move_rows(l, by);
        return;
    }
    ui_show_sel(l);
}

// A move of the list's cursor or the sidebar's.
static void ui_step(Ui *u, UiList *m, isize by)
{
    ui_move(m, by);
    if (m == &u->side) ui_side_settle(u, by < 0 ? -1 : 1);
}

// The level of px in UI_ZOOM_PX: the nearest (a state file written by hand may say any size).
static int ui_zoom_level(int px)
{
    if (px <= 0) px = UI_ZOOM_DEFAULT;
    int best = 0;
    for (int k = 1; k < UI_ZOOMS; k++)
        if (abs(UI_ZOOM_PX[k] - px) < abs(UI_ZOOM_PX[best] - px)) best = k;
    return best;
}

// The icon view's icon size now.
static int ui_zoom_px(const Ui *u)
{
    return UI_ZOOM_PX[ui_zoom_level(u->zoom)];
}

// The icon view's icons at level k (clamped); nothing in the list, or at the size it has.
static UiAction ui_zoom(Ui *u, int k)
{
    k = (int)ui_clamp(k, 0, UI_ZOOMS - 1);
    if (!u->grid || UI_ZOOM_PX[k] == ui_zoom_px(u)) return (UiAction){ UA_NONE, 0, false };
    u->zoom = UI_ZOOM_PX[k];
    return (UiAction){ UA_ZOOM, u->zoom, false };
}

// The view: icons in a grid, or the list. gui/ sets the columns at its next layout.
static UiAction ui_view(Ui *u, bool grid)
{
    u->grid = grid;
    if (!grid) ui_set_cols(&u->list, 1);
    return (UiAction){ UA_VIEW, grid, false };
}

// ---- find -------------------------------------------------------------------------------------

// ---- matching (F7.3) ---------------------------------------------------------------------------

// Regular expressions run in C.UTF-8 (glibc's own, whatever the user's locale): `.` is a character,
// not a byte, and case folds past ASCII (é and É, Michael 2026-10-26). Only around regcomp and
// regexec, on this thread: the rest of ofx keeps the C locale. (locale_t)0 when there is none:
// then bytes, as before.
static locale_t ui_utf8_locale(void)
{
    static locale_t loc;
    static bool tried;
    if (!tried) tried = true, loc = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0);
    return loc;
}

static locale_t ui_utf8_on(void)
{
    return ui_utf8_locale() ? uselocale(ui_utf8_locale()) : (locale_t)0;
}

static void ui_utf8_off(locale_t was)
{
    if (was) uselocale(was);
}

// m to match text with o: a regular expression compiled (the one before freed); one that doesn't
// compile leaves m bad, with regerror's words.
static void ui_matcher_set(UiMatcher *m, Str text, UiMatchOpts o)
{
    if (m->re_on) regfree(&m->re);
    m->re_on = false, m->bad = false, m->err[0] = 0, m->o = o;
    m->len = (int)(text.len < UI_FIND_MAX ? text.len : UI_FIND_MAX);
    memcpy(m->text, text.p, (size_t)m->len);
    m->text[m->len] = 0;
    if (!o.regex || m->len == 0) return;
    locale_t was = ui_utf8_on();
    int rc = regcomp(&m->re, m->text, REG_EXTENDED | (o.cased ? 0 : REG_ICASE));
    ui_utf8_off(was);
    if (rc == 0) m->re_on = true;
    else m->bad = true, regerror(rc, NULL, m->err, sizeof m->err);
}

// A byte of a word, for whole words: a letter, a digit, '_', or part of a character past ASCII.
static bool ui_word_byte(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c >= 0x80;
}

// Whether hay[from, to) stands as a whole word: no word byte just before or just after.
static bool ui_whole(Str hay, isize from, isize to)
{
    return (from == 0 || !ui_word_byte((unsigned char)hay.p[from - 1])) && (to >= hay.len || !ui_word_byte((unsigned char)hay.p[to]));
}

// Whether hay matches m's expression (compiled, in C.UTF-8). With whole words: some span from a
// boundary to a boundary that it matches in full. The longest match from a start may run past a
// boundary a shorter one stops at (`-*-` in "--_ ", reviewer-2, F7.3 review), so each span is
// tried: the leftmost-longest match within it is all of it exactly when the span matches. ^ and $
// keep meaning the name's start and end (NOTBOL, NOTEOL inside it).
static bool ui_match_re(const UiMatcher *m, Str hay)
{
    regmatch_t all = { .rm_so = 0, .rm_eo = (regoff_t)hay.len };
    if (regexec(&m->re, hay.p ? hay.p : "", 1, &all, REG_STARTEND) != 0) return false;
    if (!m->o.word) return true;
    for (isize from = 0; from <= hay.len; from++) {
        if (from > 0 && ui_word_byte((unsigned char)hay.p[from - 1])) continue;
        for (isize to = hay.len; to >= from; to--) {
            if (to < hay.len && ui_word_byte((unsigned char)hay.p[to])) continue;
            regmatch_t pm = { .rm_so = 0, .rm_eo = (regoff_t)(to - from) };
            int flags = REG_STARTEND | (from > 0 ? REG_NOTBOL : 0) | (to < hay.len ? REG_NOTEOL : 0);
            if (regexec(&m->re, hay.p ? hay.p + from : "", 1, &pm, flags) == 0 && pm.rm_so == 0 && pm.rm_eo == to - from) return true;
        }
    }
    return false;
}

// cp with its case folded as C.UTF-8 has it (towlower), so plain text folds é and É as a regular
// expression does (reviewer-2, F7.3.1 review); ASCII as before.
static u32 ui_fold_cp(u32 cp)
{
    if (cp < 0x80) return (u32)fold((unsigned char)cp);
    locale_t l = ui_utf8_locale();
    return l ? (u32)towlower_l((wint_t)cp, l) : cp;
}

// Whether m's text, case folded, is at hay[i]: *end where it ends there (a folded letter can be
// another length in bytes).
static bool ui_folded_at(const UiMatcher *m, Str hay, isize i, isize *end)
{
    isize a = i, b = 0;
    while (b < m->len) {
        if (a >= hay.len) return false;
        u32 x = utf8_next(hay.p, hay.len, &a), y = utf8_next(m->text, m->len, &b);
        if (ui_fold_cp(x) != ui_fold_cp(y)) return false;
    }
    *end = a;
    return true;
}

// Whether hay matches m. An empty text matches everything; a bad expression nothing.
static bool ui_match(const UiMatcher *m, Str hay)
{
    if (m->len == 0) return true;
    if (m->o.regex) {
        if (!m->re_on) return false;
        locale_t was = ui_utf8_on();
        bool hit = ui_match_re(m, hay);
        ui_utf8_off(was);
        return hit;
    }
    for (isize i = 0; i < hay.len; i++) {
        if (((unsigned char)hay.p[i] & 0xC0) == 0x80) continue;   // a match starts at a character
        isize end = i;
        if (m->o.cased) {
            if (i + m->len > hay.len || memcmp(hay.p + i, m->text, (size_t)m->len) != 0) continue;
            end = i + m->len;
        } else if (!ui_folded_at(m, hay, i, &end)) {
            continue;
        }
        if (!m->o.word || ui_whole(hay, i, end)) return true;
    }
    return false;
}

// A toggle flipped (1 case, 2 whole word, 3 regular expression; 0 none).
static UiMatchOpts ui_toggle(UiMatchOpts o, int which)
{
    if (which == 1) o.cased = !o.cased;
    if (which == 2) o.word = !o.word;
    if (which == 3) o.regex = !o.regex;
    return o;
}

// Alt and the toggle's letter (c, w, r) as its number, 0 for another key.
static int ui_toggle_key(UiEvent e)
{
    if (e.key != UK_MENU) return 0;
    return e.ch == 'c' ? 1 : e.ch == 'w' ? 2 : e.ch == 'r' ? 3 : 0;
}

// ---- find -------------------------------------------------------------------------------------

// The first entry from `from` on (dir 1) or back (dir -1), wrapping, whose name matches m; -1.
static isize ui_find_at(const UiList *l, UiNames names, const UiMatcher *m, isize from, int dir)
{
    if (l->n == 0 || m->len == 0) return -1;
    for (isize k = 0; k < l->n; k++) {
        isize i = ((from + dir * k) % l->n + l->n) % l->n;
        if (ui_match(m, names.at(names.ctx, i))) return i;
    }
    return -1;
}

// Where the find began, inside the list: the folder may have been listed again with fewer entries
// since (reviewer-2, find bar review).
static isize ui_find_back(const Ui *u)
{
    return ui_clamp(u->find_from, 0, u->list.n > 0 ? u->list.n - 1 : 0);
}

// The find line's text changed: the cursor goes to its first match from where the find began,
// or back there when the text is empty.
static void ui_find_update(Ui *u, UiNames names)
{
    Str text = { u->find, u->find_len };
    ui_matcher_set(&u->find_m, text, u->find_opts);
    isize i = ui_find_at(&u->list, names, &u->find_m, u->find_from, 1);
    u->find_miss = text.len > 0 && i < 0;
    u->list.sel = i >= 0 ? i : ui_find_back(u);
    ui_show_sel(&u->list);
}

// The find bar's next match after the cursor (dir 1) or before it (-1), round the list: Up and
// Down, and the chevrons.
static void ui_find_step(Ui *u, UiNames names, int dir)
{
    isize i = ui_find_at(&u->list, names, &u->find_m, u->list.sel + dir, dir);
    if (i >= 0) u->list.sel = i, ui_show_sel(&u->list);
}

// How many names the find bar's text matches, and *at the cursor's place among them from 1, 0 when
// it isn't on one: "2 of 5" (Michael, 2026-10-26). 0 without text.
static isize ui_find_count(const Ui *u, UiNames names, isize *at)
{
    *at = 0;
    if (!u->finding || u->find_len == 0 || u->find_m.len == 0) return 0;
    isize n = 0;
    for (isize i = 0; i < u->list.n; i++) {
        if (!ui_match(&u->find_m, names.at(names.ctx, i))) continue;
        n++;
        if (i == u->list.sel) *at = n;
    }
    return n;
}

// The keys leave the find bar (Enter, a command, the menu, the edit line); it stays open, its text
// kept for n and N.
static void ui_find_leave(Ui *u)
{
    u->find_keys = false;
    if (u->find_len) memcpy(u->last, u->find, (size_t)u->find_len), u->last_len = u->find_len;
}

static void ui_find_close(Ui *u)
{
    u->finding = false, u->find_keys = false, u->find_miss = false;
    if (u->find_len) memcpy(u->last, u->find, (size_t)u->find_len), u->last_len = u->find_len;
}

static void ui_find_open(Ui *u)
{
    u->finding = true, u->find_keys = true, u->find_miss = false, u->find_len = 0, u->find_from = u->list.sel, u->side_focus = false;
    u->jobs_focus = false, u->crumb_focus = false;
    ui_matcher_set(&u->find_m, STR_EMPTY, u->find_opts);   // Up and Down in an empty bar find nothing (reviewer-2)
}

// cp as UTF-8 into b: its length, or 0 for a control character or no code point at all.
static int ui_utf8(u32 cp, char b[4])
{
    if (cp < 0x20 || cp == 0x7F || cp > 0x10FFFF) return 0;
    return cp < 0x80 ? (b[0] = (char)cp, 1)
         : cp < 0x800 ? (b[0] = (char)(0xC0 | cp >> 6), b[1] = (char)(0x80 | (cp & 0x3F)), 2)
         : cp < 0x10000 ? (b[0] = (char)(0xE0 | cp >> 12), b[1] = (char)(0x80 | (cp >> 6 & 0x3F)), b[2] = (char)(0x80 | (cp & 0x3F)), 3)
         : (b[0] = (char)(0xF0 | cp >> 18), b[1] = (char)(0x80 | (cp >> 12 & 0x3F)), b[2] = (char)(0x80 | (cp >> 6 & 0x3F)), b[3] = (char)(0x80 | (cp & 0x3F)), 4);
}

// A character typed into the find line, as UTF-8; it is dropped when the line is full.
static void ui_find_char(Ui *u, u32 cp)
{
    char b[4];
    int n = ui_utf8(cp, b);
    if (n == 0 || u->find_len + n > UI_FIND_MAX) return;
    memcpy(u->find + u->find_len, b, (size_t)n);
    u->find_len += n;
}

// Backspace in the find line: its last code point goes.
static void ui_find_erase(Ui *u)
{
    if (u->find_len == 0) return;
    u->find_len--;
    while (u->find_len > 0 && ((unsigned char)u->find[u->find_len] & 0xC0) == 0x80) u->find_len--;
}

// n and N: the next or previous match of the last text, from the cursor.
static void ui_find_again(Ui *u, UiNames names, int dir)
{
    ui_matcher_set(&u->find_m, (Str){ u->last, u->last_len }, u->find_opts);
    isize i = ui_find_at(&u->list, names, &u->find_m, u->list.sel + dir, dir);
    if (i >= 0) u->list.sel = i, ui_show_sel(&u->list);
}

// ---- the selection ----------------------------------------------------------------------------

static bool ui_marked(const Ui *u, isize i)
{
    return u->mark && i >= 0 && i < u->list.n && u->mark[i];
}

static void ui_mark(Ui *u, isize i, bool on)
{
    if (!u->mark || i < 0 || i >= u->list.n || (bool)u->mark[i] == on) return;
    u->mark[i] = on;
    u->marked += on ? 1 : -1;
}

static void ui_mark_clear(Ui *u)
{
    for (isize i = 0; u->mark && i < u->list.n; i++) u->mark[i] = 0;
    u->marked = 0;
}

// A new listing's selection: mark (n bytes, all clear) is the array, marked recounted by gui/
// as it sets the names it kept.
static void ui_mark_set(Ui *u, u8 *mark)
{
    u->mark = mark, u->marked = 0;
}

// Rows from..to marked (either way round).
static void ui_mark_range(Ui *u, isize from, isize to)
{
    if (from > to) {
        isize t = from;
        from = to, to = t;
    }
    for (isize i = from; i <= to; i++) ui_mark(u, i, true);
}

// Shift and a move in the files (Michael, 2026-10-26): every entry the cursor passed over, from
// where it was (from) to where it is, joins the selection, as a Shift+click does. -1: none
// pending. Returns -1, for the next event.
static isize ui_extend(Ui *u, isize from)
{
    if (from >= 0) ui_mark_range(u, from, u->list.sel);   // (ui_mark keeps to the list)
    return -1;
}

// ---- the edit line -----------------------------------------------------------------------------

// How many bytes the edit line holds: a name, or a path.
static int ui_edit_max(const Ui *u)
{
    return u->edit_path ? PATH_MAX - 1 : NAME_MAX;
}

static void ui_edit_start(Ui *u, Str text, bool path)
{
    u->edit_path = path;
    isize max = ui_edit_max(u), n = text.len < max ? text.len : max;
    memcpy(u->edit, text.p, (size_t)n);
    u->edit_len = (int)n, u->caret = (int)n, u->anchor = path ? 0 : (int)n;
    ui_find_leave(u);
    u->editing = true, u->side_focus = false, u->crumb_focus = false, u->jobs_focus = false;
}

// The edit line opened on a name, the caret at its end.
static void ui_edit_open(Ui *u, Str text)
{
    ui_edit_start(u, text, false);
}

// The edit line opened on a name just made (a new folder or file), all of it selected, so typing
// replaces it (Michael, 2026-10-25).
static void ui_edit_open_all(Ui *u, Str text)
{
    ui_edit_start(u, text, false);
    u->anchor = 0;
}

// Where a file name's extension starts, as F2 selects the rest (Michael, 2026-10-25): at its last
// '.', or at ".tar" before it ("a.tar.gz" selects "a"); the length when there is none, or when the
// name only starts with a dot (".bashrc").
static int ui_name_stem(Str name)
{
    isize dot = name.len;
    while (dot > 0 && name.p[dot - 1] != '.') dot--;
    if (dot <= 1) return (int)name.len;
    dot--;
    if (dot > 4 && memcmp(name.p + dot - 4, ".tar", 4) == 0) dot -= 4;
    return (int)dot;
}

// F2's edit line: a file's name selected up to its extension, a folder's whole (as Nemo does).
static void ui_edit_rename(Ui *u, Str name, bool dir)
{
    ui_edit_start(u, name, false);
    u->anchor = 0;
    if (!dir) u->caret = ui_name_stem((Str){ u->edit, u->edit_len });
}

// The location line opened on a path, all of it selected, as browsers and Nemo do: typing a new one
// replaces it, a move of the caret ends the selection.
static void ui_location_open(Ui *u, Str path)
{
    ui_edit_start(u, path, true);
}

static void ui_edit_close(Ui *u)
{
    u->editing = false;
}

// The selection, [*lo, *hi): empty when the anchor is at the caret.
static void ui_edit_sel(const Ui *u, int *lo, int *hi)
{
    *lo = u->anchor < u->caret ? u->anchor : u->caret;
    *hi = u->anchor < u->caret ? u->caret : u->anchor;
}

static int ui_edit_back(const Ui *u, int at)
{
    if (at <= 0) return 0;
    at--;
    while (at > 0 && ((unsigned char)u->edit[at] & 0xC0) == 0x80) at--;
    return at;
}

static int ui_edit_fwd(const Ui *u, int at)
{
    if (at >= u->edit_len) return u->edit_len;
    at++;
    while (at < u->edit_len && ((unsigned char)u->edit[at] & 0xC0) == 0x80) at++;
    return at;
}

// Bytes [from, to) of the edit line removed, the caret where they were.
static void ui_edit_cut(Ui *u, int from, int to)
{
    memmove(u->edit + from, u->edit + to, (size_t)(u->edit_len - to));
    u->edit_len -= to - from;
    u->caret = u->anchor = from;
}

// Bytes b[0..n) in place of the selection, the caret after them; false (nothing changed) when they
// don't fit.
static bool ui_edit_put(Ui *u, const char *b, int n)
{
    int lo, hi;
    ui_edit_sel(u, &lo, &hi);
    if (u->edit_len - (hi - lo) + n > ui_edit_max(u)) return false;
    ui_edit_cut(u, lo, hi);
    memmove(u->edit + u->caret + n, u->edit + u->caret, (size_t)(u->edit_len - u->caret));
    memcpy(u->edit + u->caret, b, (size_t)n);
    u->edit_len += n, u->caret += n, u->anchor = u->caret;
    return true;
}

// The selection onto u->copy for gui/'s clipboard; nothing selected: nothing.
static bool ui_edit_copy(Ui *u)
{
    int lo, hi;
    ui_edit_sel(u, &lo, &hi);
    if (lo == hi) return false;
    memcpy(u->copy, u->edit + lo, (size_t)(hi - lo));
    u->copy_len = hi - lo, u->copied = true;
    return true;
}

// Ctrl+V: the clipboard's first line over the selection; in a name line without the characters a
// name can't hold ('/'), and never a control character. What doesn't fit is left off, whole
// characters only.
static void ui_edit_paste(Ui *u, Str text)
{
    char b[PATH_MAX];
    int n = 0, lo, hi;
    ui_edit_sel(u, &lo, &hi);
    int room = ui_edit_max(u) - (u->edit_len - (hi - lo));
    for (isize i = 0; i < text.len && text.p[i] != '\n' && text.p[i] != '\r';) {
        unsigned char c = (unsigned char)text.p[i];
        isize e = i + 1;
        while (e < text.len && ((unsigned char)text.p[e] & 0xC0) == 0x80) e++;
        bool drop = c < 0x20 || c == 0x7F || (c == '/' && !u->edit_path);
        if (n + (e - i) > room) break;   // a dropped one is a byte: none fits after it either
        if (!drop) memcpy(b + n, text.p + i, (size_t)(e - i)), n += (int)(e - i);
        i = e;
    }
    if (n > 0) ui_edit_put(u, b, n);
}

// A key or character in the edit line: an action for gui/ (UA_RENAME or UA_GO on Enter) or UA_NONE.
static UiAction ui_edit_event(Ui *u, UiEvent e, Str paste)
{
    if (e.key == UK_NONE) {
        char b[4];
        int n = ui_utf8(e.ch, b);
        if (n > 0 && (e.ch != '/' || u->edit_path)) ui_edit_put(u, b, n);
        return (UiAction){ UA_NONE, 0, false };
    }
    int lo, hi;
    ui_edit_sel(u, &lo, &hi);
    switch (e.key) {
    // A move ends the selection; Left and Right from one land at its ends.
    case UK_LEFT: u->caret = u->anchor = lo < hi ? lo : ui_edit_back(u, u->caret); break;
    case UK_RIGHT: u->caret = u->anchor = lo < hi ? hi : ui_edit_fwd(u, u->caret); break;
    case UK_HOME: u->caret = u->anchor = 0; break;
    case UK_END: u->caret = u->anchor = u->edit_len; break;
    // With Shift the caret moves and the anchor stays.
    case UK_SEL_LEFT: u->caret = ui_edit_back(u, u->caret); break;
    case UK_SEL_RIGHT: u->caret = ui_edit_fwd(u, u->caret); break;
    case UK_SEL_HOME: u->caret = 0; break;
    case UK_SEL_END: u->caret = u->edit_len; break;
    case UK_ALL: u->anchor = 0, u->caret = u->edit_len; break;
    case UK_BACKSPACE: ui_edit_cut(u, lo < hi ? lo : ui_edit_back(u, u->caret), lo < hi ? hi : u->caret); break;
    case UK_DELETE: ui_edit_cut(u, lo < hi ? lo : u->caret, lo < hi ? hi : ui_edit_fwd(u, u->caret)); break;
    case UK_COPY: ui_edit_copy(u); break;
    case UK_CUT:
        if (ui_edit_copy(u)) ui_edit_cut(u, lo, hi);
        break;
    case UK_PASTE: ui_edit_paste(u, paste); break;
    case UK_ENTER:
    case UK_SHIFT_ENTER: return (UiAction){ u->edit_path ? UA_GO : UA_RENAME, 0, false };
    case UK_ESC: ui_edit_close(u); break;
    case UK_LOCATION:   // Ctrl+L again: the location line closes; a rename doesn't take it
        if (u->edit_path) ui_edit_close(u);
        break;
    default: break;
    }
    return (UiAction){ UA_NONE, 0, false };
}

// ---- the path buttons by keyboard (F5.2) -------------------------------------------------------

// Ctrl+K: the keyboard on the path buttons, the rightmost shown marked (none there: nothing).
static void ui_crumbs_focus(Ui *u, const UiInput *in)
{
    if (in->crumb_n <= 0) return;
    u->crumb_focus = true, u->side_focus = false;   // the job rows' keyboard went with the key
    u->crumb_at = in->crumb_last < 0 ? 0 : in->crumb_last >= in->crumb_n ? in->crumb_n - 1 : in->crumb_last;
}

// A key while the path buttons have the keyboard (h and l as Left and Right): true when it was
// theirs (*a: what gui/ does).
// Any other key gives the keyboard back to the list and is the list's.
static bool ui_crumbs_event(Ui *u, const UiInput *in, UiEvent e, UiAction *a)
{
    *a = (UiAction){ UA_NONE, 0, false };
    if (e.key == UK_NONE && e.ch == 'h') e.key = UK_LEFT;    // as in the list (Michael, 2026-10-21)
    if (e.key == UK_NONE && e.ch == 'l') e.key = UK_RIGHT;
    switch (e.key) {
    case UK_LEFT: case UK_SEL_LEFT: if (u->crumb_at > 0) u->crumb_at--; return true;
    case UK_RIGHT: case UK_SEL_RIGHT: if (u->crumb_at < in->crumb_n - 1) u->crumb_at++; return true;
    case UK_HOME: case UK_SEL_HOME: u->crumb_at = 0; return true;
    case UK_END: case UK_SEL_END: u->crumb_at = in->crumb_n - 1; return true;
    case UK_ENTER: u->crumb_focus = false, *a = (UiAction){ UA_CRUMB, u->crumb_at, false }; return true;
    case UK_SHIFT_ENTER: *a = (UiAction){ UA_TAB_BG_CRUMB, u->crumb_at, false }; return true;
    case UK_CRUMBS: return true;
    case UK_ESC: case UK_TAB: u->crumb_focus = false; return true;
    default: u->crumb_focus = false; return false;
    }
}

// ---- the prompts -------------------------------------------------------------------------------

// A yes-or-no question (delete for good, cancel a job, quit with jobs): Enter and y say yes, n and Esc no; *answered
// when either.
static bool ui_yes_no(UiEvent e, bool *answered)
{
    bool yes = e.key == UK_ENTER || (e.key == UK_NONE && (e.ch == 'y' || e.ch == 'Y'));
    bool no = e.key == UK_ESC || (e.key == UK_NONE && (e.ch == 'n' || e.ch == 'N'));
    *answered = yes || no;
    return yes;
}

// A key or character while "Delete … for good? Y/n" is up: Enter and y go ahead, n and Esc don't,
// the rest is ignored.
static UiAction ui_confirm_event(Ui *u, UiEvent e)
{
    bool answered, yes = ui_yes_no(e, &answered);
    if (answered) u->confirming = false;
    return (UiAction){ yes ? UA_DELETE : UA_NONE, 0, false };
}

// A key or character while a clash waits: s skip, r replace (a folder: merge), k keep both, the
// capitals for all the rest; Esc cancels the job.
static UiAction ui_ask_event(Ui *u, UiEvent e)
{
    if (e.key == UK_ESC) return u->asking = false, (UiAction){ UA_ANSWER, UI_ANS_CANCEL, false };
    if (e.key != UK_NONE) return (UiAction){ UA_NONE, 0, false };
    int a = 0;
    switch (e.ch) {
    case 's': case 'S': a = UI_ANS_SKIP; break;
    case 'r': case 'R': a = UI_ANS_REPLACE; break;
    case 'k': case 'K': a = UI_ANS_KEEP; break;
    default: return (UiAction){ UA_NONE, 0, false };
    }
    u->asking = false;
    return (UiAction){ UA_ANSWER, a, (e.ch >= 'A' && e.ch <= 'Z') || u->ask_all };
}

// The job rows by keyboard: true when the key was theirs (*a: what gui/ does). Up, Down, k and j
// move the mark, Enter asks to cancel the marked job; any other key gives the keyboard back to the
// list and is the list's.
static bool ui_jobs_event(Ui *u, const UiInput *in, UiEvent e)
{
    if (e.key == UK_NONE && e.ch == 'k') e.key = UK_UP;
    if (e.key == UK_NONE && e.ch == 'j') e.key = UK_DOWN;
    switch (e.key) {
    case UK_UP: if (u->job_at > 0) u->job_at--; return true;
    case UK_DOWN: if (u->job_at < in->job_n - 1) u->job_at++; return true;
    case UK_ENTER: u->cancel_asking = true, u->cancel_id = in->job_id[u->job_at]; return true;
    case UK_JOBS: u->jobs_focus = false; return true;   // the third Ctrl+J: back to the list (list, find bar, jobs)
    case UK_ESC: case UK_TAB: u->jobs_focus = false; return true;
    default: u->jobs_focus = false; return false;
    }
}

// ---- the menu bar (F7.1) -----------------------------------------------------------------------

// Whether entry it can be picked now.
static bool ui_menu_enabled(const Ui *u, UiMenuItem it)
{
    switch (it) {
    case UM_NEW_TAB: case UM_CLOSE_ALL: case UM_CLOSE: case UM_PROPERTIES: return true;
    case UM_NEW_WINDOW: return !u->in_trash;   // ofx starts on a folder, not on the trash view
    case UM_NEW_FOLDER: case UM_NEW_FILE: return !u->in_trash && u->can_write;
    case UM_OPEN: return u->list.n > 0;
    case UM_OPEN_WITH: return u->list.n > 0 && !u->in_trash;
    default: return false;
    }
}

// The open menu's next entry that can be picked from place at, dir 1 down or -1 up, round the
// end; at itself when none other can, -1 when none at all.
static int ui_menu_next(const Ui *u, int at, int dir)
{
    int n = UI_MENU[u->menu].n;
    for (int k = 1; k <= n; k++) {
        int i = ((at + dir * k) % n + n) % n;
        if (ui_menu_enabled(u, UI_MENU[u->menu].items[i])) return i;
    }
    return -1;
}

// Menu m open: by a key, its first entry that can be picked marked; by the pointer, none. The edit
// line and the find line close (a new folder keeps its name).
static void ui_menu_open(Ui *u, int m, bool keys)
{
    u->menu_on = true, u->menu = m;
    u->menu_at = keys ? ui_menu_next(u, -1, 1) : -1;
    ui_edit_close(u), ui_find_leave(u);
}

// The Properties panel opened, the focus on the grid's first box (F7.2).
static UiAction ui_props_open(Ui *u)
{
    u->props = true, u->prop_at = 0;   // (the menu closed the edit and find lines; Ctrl+I in them is theirs)
    return (UiAction){ UA_PROPS, 0, false };
}

// A key while the Properties panel is up; it takes every key (a panel). Arrows and h, j, k, l move
// the focus in the 3 by 3 grid (stopping at its edges), Space or Enter flips that permission when
// gui/ allows it, Esc or Ctrl+I closes the panel.
static UiAction ui_props_event(Ui *u, UiEvent e)
{
    if (e.key == UK_NONE) {
        switch (e.ch) {
        case 'h': e.key = UK_LEFT; break;
        case 'j': e.key = UK_DOWN; break;
        case 'k': e.key = UK_UP; break;
        case 'l': e.key = UK_RIGHT; break;
        case ' ': e.key = UK_ENTER; break;
        default: break;
        }
    }
    int row = u->prop_at / 3, col = u->prop_at % 3;
    switch (e.key) {
    case UK_LEFT: case UK_SEL_LEFT: if (col > 0) u->prop_at--; break;
    case UK_RIGHT: case UK_SEL_RIGHT: if (col < 2) u->prop_at++; break;
    case UK_UP: if (row > 0) u->prop_at -= 3; break;
    case UK_DOWN: if (row < 2) u->prop_at += 3; break;
    case UK_ENTER: case UK_SHIFT_ENTER:
        if (u->prop_can) return (UiAction){ UA_CHMOD, u->prop_at, false };
        break;
    case UK_ESC: case UK_PROPERTIES: u->props = false; break;
    default: break;
    }
    return (UiAction){ UA_NONE, 0, false };
}

// ---- Open With (F7.3) ------------------------------------------------------------------------

// The panel opened: the field empty with the keys, the toggles as last time; gui/ lists the apps.
static UiAction ui_ow_open(Ui *u)
{
    u->ow = true, u->ow_in_list = false, u->ow_fresh = true, u->ow_len = u->ow_caret = 0, u->ow_def = false;
    u->ow_list = (UiList){ .rows = u->ow_list.rows };
    return (UiAction){ UA_OPEN_WITH, 0, false };
}

static void ui_ow_close(Ui *u)
{
    u->ow = false;
    ui_matcher_set(&u->ow_m, STR_EMPTY, u->ow_opts);   // (a compiled expression freed)
}

// The apps the field's text matches, in gui/'s order; the cursor on the first.
static void ui_ow_filter(Ui *u, const UiInput *in)
{
    ui_matcher_set(&u->ow_m, (Str){ u->ow_text, u->ow_len }, u->ow_opts);
    isize n = 0;
    for (isize i = 0; i < in->app_n && n < UI_APPS_MAX; i++)
        if (ui_match(&u->ow_m, in->app_name[i])) u->ow_idx[n++] = i;
    u->ow_list.n = n, u->ow_list.sel = 0, u->ow_list.top = 0;
    u->ow_fresh = false;
}

// A character into the field at the caret (dropped when it is full).
static void ui_ow_char(Ui *u, u32 cp)
{
    char b[4];
    int n = ui_utf8(cp, b);
    if (n == 0 || u->ow_len + n > UI_FIND_MAX) return;
    memmove(u->ow_text + u->ow_caret + n, u->ow_text + u->ow_caret, (size_t)(u->ow_len - u->ow_caret));
    memcpy(u->ow_text + u->ow_caret, b, (size_t)n);
    u->ow_len += n, u->ow_caret += n;
}

// The caret's place a code point before (dir -1) or after (1) at.
static int ui_ow_step(const Ui *u, int at, int dir)
{
    if (dir < 0) {
        if (at == 0) return 0;
        at--;
        while (at > 0 && ((unsigned char)u->ow_text[at] & 0xC0) == 0x80) at--;
        return at;
    }
    if (at >= u->ow_len) return u->ow_len;
    at++;
    while (at < u->ow_len && ((unsigned char)u->ow_text[at] & 0xC0) == 0x80) at++;
    return at;
}

// The field's text from a to b (a < b) removed.
static void ui_ow_cut(Ui *u, int a, int b)
{
    memmove(u->ow_text + a, u->ow_text + b, (size_t)(u->ow_len - b));
    u->ow_len -= b - a, u->ow_caret = a;
}

// The app under the cursor launched, when the filter shows any.
static UiAction ui_ow_launch(Ui *u)
{
    if (u->ow_list.n == 0) return (UiAction){ UA_NONE, 0, false };
    isize app = u->ow_idx[u->ow_list.sel];
    bool def = u->ow_def && u->ow_def_can;
    ui_ow_close(u);
    return (UiAction){ UA_LAUNCH, app, def };
}

// A key while the panel is up; it takes every key (Michael, 2026-10-26). Tab moves between the
// field and the list. In the field typing, Backspace, Delete, Left, Right, Home and End edit, Down
// goes to the list; in the list Up, Down, k, j, Home, End, PgUp and PgDn move, Up on the first
// row goes to the field, another character goes to the field and is typed there. Enter opens the
// app under the cursor, Esc closes, Alt+C, Alt+W and Alt+R flip the toggles, Alt+D the box that
// makes it the default (F7.3.1).
static UiAction ui_ow_event(Ui *u, UiEvent e, const UiInput *in)
{
    UiList *l = &u->ow_list;
    int page = l->rows > 1 ? l->rows - 1 : 1;
    if (e.key == UK_MENU && e.ch == 'd') {
        if (u->ow_def_can) u->ow_def = !u->ow_def;
        return (UiAction){ UA_NONE, 0, false };
    }
    if (ui_toggle_key(e)) {
        u->ow_opts = ui_toggle(u->ow_opts, ui_toggle_key(e));
        ui_ow_filter(u, in);
        return (UiAction){ UA_NONE, 0, false };
    }
    switch (e.key) {
    case UK_ESC: ui_ow_close(u); return (UiAction){ UA_NONE, 0, false };
    case UK_TAB: u->ow_in_list = !u->ow_in_list; return (UiAction){ UA_NONE, 0, false };
    case UK_ENTER: case UK_SHIFT_ENTER: return ui_ow_launch(u);
    default: break;
    }
    if (u->ow_in_list) {
        if (e.key == UK_NONE && e.ch == 'j') e.key = UK_DOWN;
        else if (e.key == UK_NONE && e.ch == 'k') e.key = UK_UP;
        switch (e.key) {
        case UK_DOWN: ui_move(l, 1); break;
        case UK_UP:
            if (l->sel == 0) u->ow_in_list = false;
            else ui_move(l, -1);
            break;
        case UK_HOME: ui_move(l, -l->n); break;
        case UK_END: ui_move(l, l->n); break;
        case UK_PAGEUP: ui_move(l, -page); break;
        case UK_PAGEDOWN: ui_move(l, page); break;
        case UK_NONE:
            if (ui_utf8(e.ch, (char[4]){ 0 }) == 0) break;
            u->ow_in_list = false, u->ow_caret = u->ow_len;
            ui_ow_char(u, e.ch), ui_ow_filter(u, in);
            break;
        case UK_BACKSPACE:
            u->ow_in_list = false, u->ow_caret = u->ow_len;
            if (u->ow_len) ui_ow_cut(u, ui_ow_step(u, u->ow_len, -1), u->ow_len), ui_ow_filter(u, in);
            break;
        default: break;
        }
        return (UiAction){ UA_NONE, 0, false };
    }
    int before = u->ow_len;
    switch (e.key) {
    case UK_NONE: ui_ow_char(u, e.ch); break;
    case UK_BACKSPACE: if (u->ow_caret > 0) ui_ow_cut(u, ui_ow_step(u, u->ow_caret, -1), u->ow_caret); break;
    case UK_DELETE: if (u->ow_caret < u->ow_len) ui_ow_cut(u, u->ow_caret, ui_ow_step(u, u->ow_caret, 1)); break;
    case UK_LEFT: case UK_SEL_LEFT: u->ow_caret = ui_ow_step(u, u->ow_caret, -1); break;
    case UK_RIGHT: case UK_SEL_RIGHT: u->ow_caret = ui_ow_step(u, u->ow_caret, 1); break;
    case UK_HOME: case UK_SEL_HOME: u->ow_caret = 0; break;
    case UK_END: case UK_SEL_END: u->ow_caret = u->ow_len; break;
    case UK_DOWN: if (l->n > 0) u->ow_in_list = true; break;
    default: break;
    }
    if (u->ow_len != before || e.key == UK_NONE) ui_ow_filter(u, in);
    return (UiAction){ UA_NONE, 0, false };
}

// Entry it picked: the menu closes, and gui/ does what it says.
static UiAction ui_menu_pick(Ui *u, UiMenuItem it)
{
    u->menu_on = false;
    switch (it) {
    case UM_NEW_TAB: return (UiAction){ UA_TAB_NEW, 0, false };
    case UM_NEW_WINDOW: return (UiAction){ UA_NEW_WINDOW, 0, false };
    case UM_NEW_FOLDER: return (UiAction){ UA_NEWFOLDER, 0, false };
    case UM_NEW_FILE: return (UiAction){ UA_NEWFILE, 0, false };
    case UM_OPEN: return (UiAction){ UA_OPEN, u->list.sel, u->marked > 0 };
    case UM_CLOSE_ALL: return (UiAction){ UA_CLOSE_ALL, 0, true };   // gui/ asks first with jobs (close_others)
    case UM_CLOSE: return (UiAction){ UA_TAB_CLOSE, -1, false };
    case UM_PROPERTIES: return ui_props_open(u);
    case UM_OPEN_WITH: return ui_ow_open(u);
    default: return (UiAction){ UA_NONE, 0, false };
    }
}

// The menu whose Alt letter is ch, -1 for none.
static int ui_menu_of(u32 ch)
{
    for (int m = 0; m < UI_MENUS; m++)
        if ((u32)UI_MENU[m].letter == ch) return m;
    return -1;
}

// A key while a menu is open: true when it was the menu's (*a: what gui/ does). Up, Down, k and j
// move the mark over what can be picked, round the end; Left, Right, h and l go to the menu that
// way; Enter picks; Esc and F10 close it, as its own Alt letter does. A typed character goes no
// further (t would start a terminal behind the menu). Any other key closes the menu and is the
// list's (a shortcut does what it does).
static bool ui_menu_event(Ui *u, UiEvent e, UiAction *a)
{
    *a = (UiAction){ UA_NONE, 0, false };
    if (e.key == UK_NONE) {
        switch (e.ch) {
        case 'j': e.key = UK_DOWN; break;
        case 'k': e.key = UK_UP; break;
        case 'h': e.key = UK_LEFT; break;
        case 'l': e.key = UK_RIGHT; break;
        default: return true;
        }
    }
    switch (e.key) {
    case UK_DOWN: case UK_UP: {
        int at = ui_menu_next(u, u->menu_at < 0 && e.key == UK_UP ? 0 : u->menu_at, e.key == UK_DOWN ? 1 : -1);
        u->menu_at = at;
        return true;
    }
    case UK_HOME: u->menu_at = ui_menu_next(u, -1, 1); return true;
    case UK_END: u->menu_at = ui_menu_next(u, 0, -1); return true;
    case UK_LEFT: case UK_SEL_LEFT: case UK_RIGHT: case UK_SEL_RIGHT: {
        int by = e.key == UK_LEFT || e.key == UK_SEL_LEFT ? -1 : 1;
        ui_menu_open(u, ((u->menu + by) % UI_MENUS + UI_MENUS) % UI_MENUS, true);
        return true;
    }
    case UK_ENTER: case UK_SHIFT_ENTER:
        if (u->menu_at >= 0 && ui_menu_enabled(u, UI_MENU[u->menu].items[u->menu_at])) *a = ui_menu_pick(u, UI_MENU[u->menu].items[u->menu_at]);
        return true;
    case UK_ESC: case UK_F10: case UK_TAB: u->menu_on = false; return true;
    case UK_MENU: {
        int m = ui_menu_of(e.ch);
        if (m == u->menu) u->menu_on = false;
        else if (m >= 0) ui_menu_open(u, m, true);
        return true;
    }
    default: u->menu_on = false; return false;
    }
}

// The pointer over the menu bar (F7.1): over another menu's title while one is open, that menu
// opens (nothing marked); over an entry, it is marked if it can be picked. True when anything
// changed.
static bool ui_menu_hover(Ui *u, int title, int item)
{
    if (!u->menu_on) return false;
    if (title >= 0 && title < UI_MENUS && title != u->menu) {
        u->menu = title, u->menu_at = -1;
        return true;
    }
    if (item < 0 || item >= UI_MENU[u->menu].n || !ui_menu_enabled(u, UI_MENU[u->menu].items[item]) || item == u->menu_at) return false;
    u->menu_at = item;
    return true;
}

// ---- input ------------------------------------------------------------------------------------

// The next sort for a header click (or `s`'s next column): the same column again reverses.
static Sort ui_sort_click(Sort s, SortKey k)
{
    return s.key == k ? (Sort){ k, !s.reverse } : (Sort){ k, false };
}

// A key or character while the find bar has the keys: true when it was the bar's (the rest of
// the event is then done). Typing jumps to the first match from where the find began; Up and Down
// go to the previous and next match; Enter keeps the cursor there and gives the keys back to the
// list (the bar stays); Esc closes the bar and puts the cursor back; Ctrl+J goes on to the job rows
// (Michael, 2026-10-26). A command gives the keys back and is the list's.
static bool ui_find_event(Ui *u, UiNames names, UiEvent e, const UiInput *in)
{
    if (ui_toggle_key(e)) {   // Alt+C, Alt+W, Alt+R: a toggle
        u->find_opts = ui_toggle(u->find_opts, ui_toggle_key(e));
        ui_find_update(u, names);
        return true;
    }
    if (e.key == UK_NONE) {
        if (e.ch == '/') return true;   // can't be in a name
        ui_find_char(u, e.ch);
        ui_find_update(u, names);
        return true;
    }
    switch (e.key) {
    case UK_BACKSPACE: ui_find_erase(u), ui_find_update(u, names); return true;
    case UK_ENTER: case UK_SHIFT_ENTER: ui_find_leave(u); return true;
    case UK_ESC:
        ui_find_close(u);
        u->list.sel = ui_find_back(u), ui_show_sel(&u->list);
        return true;
    case UK_UP: case UK_DOWN: ui_find_step(u, names, e.key == UK_UP ? -1 : 1); return true;
    case UK_JOBS:
        ui_find_leave(u);
        if (in->job_n > 0) u->jobs_focus = true, u->job_at = 0;
        return true;
    case UK_FIND: return true;
    default: ui_find_leave(u); return false;   // a command: the keys go back to the list, which does it
    }
}

// One frame's input. Clicks first, then the events in order up to the first that asks gui/ for
// something (after it, the list may be another folder's). The wheel scrolls the list without
// moving its cursor, one row a notch (Michael's choice for prdash-tui). A prompt (a clash, the
// delete confirm) takes every key until it is answered; the edit line takes the keys that edit.
static UiAction ui_handle(Ui *u, UiNames names, const UiInput *in)
{
    UiList *l = &u->list;
    // The window closed, or another window's Close All (SIGUSR1, F7.1): taken before anything that
    // ends the frame early (a click, the queue-full note), as gui/ queues it once (reviewer-2,
    // F7.1 review). At once, or asked first when jobs run or wait.
    for (int k = 0; k < in->nev && !u->quit_asking; k++)
        if (in->ev[k].key == UK_QUIT) {   // (asking closes a menu: below)
            if (in->jobs_all <= 0) return (UiAction){ UA_QUIT, 0, false };
            u->quit_asking = true, u->cancel_asking = false;
        }
    // The panels F5.3 adds: the queue full (any key or OK), quit, cancel a job.
    if (u->full_told && (in->modal == UI_MODAL_OK || in->nev > 0)) {
        u->full_told = false;
        return (UiAction){ UA_NONE, 0, false };
    }
    if (u->quit_asking && (in->modal == UI_MODAL_YES || in->modal == UI_MODAL_NO)) {
        u->quit_asking = false;
        return (UiAction){ in->modal == UI_MODAL_YES ? UA_QUIT : UA_NONE, 0, false };
    }
    if (u->cancel_asking && (in->modal == UI_MODAL_YES || in->modal == UI_MODAL_NO)) {
        u->cancel_asking = false;
        return (UiAction){ in->modal == UI_MODAL_YES ? UA_CANCEL_JOB : UA_NONE, u->cancel_id, false };
    }
    if (u->empty_asking && (in->modal == UI_MODAL_YES || in->modal == UI_MODAL_NO)) {
        u->empty_asking = false;
        return (UiAction){ in->modal == UI_MODAL_YES && u->in_trash ? UA_EMPTY_TRASH : UA_NONE, 0, false };
    }
    if (!u->in_trash) u->empty_asking = false;
    bool prompt = u->asking || u->confirming || u->cancel_asking || u->quit_asking || u->full_told || u->empty_asking || u->props || u->ow;
    if (prompt) u->menu_on = false;
    // Open With (F7.3): filtered once gui/ has listed the apps; the pointer: a row takes the
    // cursor (a double click opens it), the field the keys, a toggle flips, Open and Cancel.
    if (u->ow && u->ow_fresh && in->app_name) ui_ow_filter(u, in);
    if (u->ow && !u->quit_asking && !u->full_told && !u->asking && !u->confirming) {
        if (in->toggle == 4) {
            if (u->ow_def_can) u->ow_def = !u->ow_def;
            return (UiAction){ UA_NONE, 0, false };
        }
        if (in->toggle >= 1 && in->toggle <= 3) {
            u->ow_opts = ui_toggle(u->ow_opts, in->toggle);
            ui_ow_filter(u, in);
            return (UiAction){ UA_NONE, 0, false };
        }
        if (in->ow_field) u->ow_in_list = false;
        isize row = in->ow_drow >= 0 ? in->ow_drow : in->ow_row;
        if (row >= 0 && u->ow_list.top + row < u->ow_list.n) {
            u->ow_list.sel = u->ow_list.top + row, u->ow_in_list = true;
            if (in->ow_drow >= 0) return ui_ow_launch(u);
        }
        if (in->modal == UI_MODAL_YES) return ui_ow_launch(u);
        if (in->modal == UI_MODAL_NO) return ui_ow_close(u), (UiAction){ UA_NONE, 0, false };
        if (in->ow_wheel) ui_scroll(&u->ow_list, in->ow_wheel);
    } else if (!u->ow && !prompt && u->finding && (in->find_x || in->find_field || in->find_step)) {
        // A chevron steps with two matches or more (greyed under that) and leaves the keys where
        // they are.
        isize at;
        if (in->find_step) {
            if (ui_find_count(u, names, &at) >= 2) ui_find_step(u, names, in->find_step > 0 ? 1 : -1);
        } else if (in->find_x) ui_find_close(u);
        else ui_edit_close(u), u->find_keys = true, u->side_focus = false, u->crumb_focus = false, u->jobs_focus = false;   // (an open edit line would take the keys first: reviewer-2)
        return (UiAction){ UA_NONE, 0, false };
    } else if (!u->ow && in->toggle >= 1 && in->toggle <= 3 && u->finding) {
        u->find_opts = ui_toggle(u->find_opts, in->toggle);
        ui_find_update(u, names);
        return (UiAction){ UA_NONE, 0, false };
    }
    // The menu bar (F7.1): a title opens its menu or closes it; while a menu is open a press on one
    // of its entries picks it (one that can't be picked keeps the menu open), and a press anywhere
    // else only closes it.
    if (!prompt && in->menu_title >= 0 && in->menu_title < UI_MENUS) {
        if (u->menu_on && u->menu == in->menu_title) u->menu_on = false;
        else ui_menu_open(u, in->menu_title, false);
        return (UiAction){ UA_NONE, 0, false };
    }
    if (u->menu_on && in->menu_item >= 0 && in->menu_item < UI_MENU[u->menu].n) {
        UiMenuItem it = UI_MENU[u->menu].items[in->menu_item];
        return ui_menu_enabled(u, it) ? ui_menu_pick(u, it) : (UiAction){ UA_NONE, 0, false };
    }
    if (u->menu_on && in->menu_away) return u->menu_on = false, (UiAction){ UA_NONE, 0, false };
    // The Properties panel's pointer (F7.2): a checkbox takes the focus and flips; Close closes.
    if (u->props && !u->quit_asking && !u->full_told && in->prop_box >= 0 && in->prop_box < 9) {
        u->prop_at = in->prop_box;
        return u->prop_can ? (UiAction){ UA_CHMOD, in->prop_box, false } : (UiAction){ UA_NONE, 0, false };
    }
    if (u->props && in->modal == UI_MODAL_OK && !u->full_told) return u->props = false, (UiAction){ UA_NONE, 0, false };
    // The job rows: the mark kept on a row there is; none left, no keyboard there.
    if (u->job_at >= in->job_n) u->job_at = in->job_n - 1;
    if (u->job_at < 0) u->job_at = 0;
    if (in->job_n <= 0) u->jobs_focus = false;
    if (!prompt && in->job_x >= 0 && in->job_x < in->job_n) {
        u->cancel_asking = true, u->cancel_id = in->job_id[in->job_x];
        return (UiAction){ UA_NONE, 0, false };
    }
    // A click anywhere gives the path buttons' keyboard back; so does their going.
    bool clicked = in->click >= 0 || in->dclick >= 0 || in->side_click >= 0 || in->header >= 0 || in->bar != UB_NONE ||
                   in->tab_click >= 0 || in->tab_close >= 0 || in->mid_row >= 0 || in->mid_side >= 0 || in->mid_crumb >= 0;
    if (clicked || in->crumb_n <= 0) u->crumb_focus = false;
    if (clicked) u->jobs_focus = false;
    if (u->crumb_focus && u->crumb_at >= in->crumb_n) u->crumb_at = in->crumb_n - 1;
    // A modal panel's buttons: as the keys they stand for.
    if (u->confirming && (in->modal == UI_MODAL_YES || in->modal == UI_MODAL_NO)) {
        u->confirming = false;
        return (UiAction){ in->modal == UI_MODAL_YES ? UA_DELETE : UA_NONE, 0, false };
    }
    if (u->asking && in->modal == UI_MODAL_ALL) u->ask_all = !u->ask_all;
    if (u->asking && in->modal >= UI_MODAL_SKIP && in->modal <= UI_MODAL_CANCEL) {
        static const int ANSWER[] = { UI_ANS_SKIP, UI_ANS_REPLACE, UI_ANS_KEEP, UI_ANS_CANCEL };
        int a = ANSWER[in->modal - UI_MODAL_SKIP];
        u->asking = false;
        return (UiAction){ UA_ANSWER, a, a != UI_ANS_CANCEL && u->ask_all };
    }
    if (in->wheel) ui_scroll(l, in->wheel);
    if (!prompt && in->zoom_to >= 0) return ui_zoom(u, in->zoom_to);
    if (!prompt && in->zoom_wheel) return ui_zoom(u, ui_zoom_level(u->zoom) + (in->zoom_wheel > 0 ? 1 : -1));
    if (!prompt && in->header >= 0 && in->header < SORT_KEYS) {
        u->sort = ui_sort_click(u->sort, (SortKey)in->header);
        return (UiAction){ UA_SORT, 0, false };
    }
    if (!prompt && in->side_mode >= 0 && in->side_mode < SIDE_MODES) return ui_side_mode(u, (SideMode)in->side_mode);
    if (!prompt && in->side_eject >= 0 && u->side_mode == SIDE_PLACES && u->side.top + in->side_eject < u->side.n)
        return (UiAction){ UA_EJECT, u->side.top + in->side_eject, false };
    if (!prompt && in->side_twisty >= 0 && u->side_mode == SIDE_TREE && u->side.top + in->side_twisty < u->side.n)
        return ui_edit_close(u), (UiAction){ UA_TREE_TOGGLE, u->side.top + in->side_twisty, false };
    if (!prompt && in->side_click >= 0 && ui_side_head(u, u->side.top + in->side_click)) return (UiAction){ UA_NONE, 0, false };
    if (!prompt && in->side_click >= 0 && u->side.top + in->side_click < u->side.n) {
        u->side.sel = u->side.top + in->side_click;
        ui_edit_close(u);
        return (UiAction){ UA_PLACE, u->side.sel, false };
    }
    if (!prompt && in->tab_close >= 0) return ui_edit_close(u), (UiAction){ UA_TAB_CLOSE, in->tab_close, false };
    if (!prompt && in->tab_click >= 0) return ui_edit_close(u), (UiAction){ UA_TAB_SHOW, in->tab_click, false };
    if (!prompt && in->mid_row >= 0 && l->top + in->mid_row < l->n) return (UiAction){ UA_TAB_BG_ROW, l->top + in->mid_row, false };
    if (!prompt && in->mid_side >= 0 && u->side.top + in->mid_side < u->side.n && !ui_side_head(u, u->side.top + in->mid_side)) return (UiAction){ UA_TAB_BG_PLACE, u->side.top + in->mid_side, false };
    if (!prompt && in->mid_crumb >= 0) return (UiAction){ UA_TAB_BG_CRUMB, in->mid_crumb, false };
    if (!prompt && in->bar != UB_NONE) {
        switch (in->bar) {
        case UB_BACK: ui_edit_close(u); return (UiAction){ UA_BACK, 0, false };
        case UB_FORWARD: ui_edit_close(u); return (UiAction){ UA_FORWARD, 0, false };
        case UB_UP: ui_edit_close(u); return (UiAction){ UA_PARENT, 0, false };
        case UB_LOCATION:
            if (u->editing && u->edit_path) return ui_edit_close(u), (UiAction){ UA_NONE, 0, false };
            return (UiAction){ UA_LOCATION, 0, false };
        case UB_SEARCH:   // opens the find bar, or closes it
            ui_edit_close(u);
            if (u->finding) ui_find_close(u);
            else ui_find_open(u);
            return (UiAction){ UA_NONE, 0, false };
        case UB_LEFT: return (UiAction){ UA_CRUMB_SHIFT, -1, false };
        case UB_RIGHT: return (UiAction){ UA_CRUMB_SHIFT, 1, false };
        case UB_CRUMB: ui_edit_close(u); return (UiAction){ UA_CRUMB, in->crumb, false };
        case UB_RESTORE: if (u->in_trash && l->n > 0) return (UiAction){ UA_RESTORE, 0, false }; break;
        case UB_EMPTY: if (u->in_trash) u->empty_asking = true; break;
        case UB_VIEW_LIST: return ui_view(u, false);
        case UB_VIEW_ICONS: return ui_view(u, true);
        case UB_ZOOM_OUT: return ui_zoom(u, ui_zoom_level(u->zoom) - 1);
        case UB_ZOOM_IN: return ui_zoom(u, ui_zoom_level(u->zoom) + 1);
        default: break;
        }
    }
    if (!prompt && in->click >= 0 && l->top + in->click < l->n) {
        isize row = l->top + in->click;
        ui_edit_close(u);
        if (in->click_mods & UI_MOD_SHIFT) ui_mark_range(u, l->sel, row);
        else if (in->click_mods & UI_MOD_CTRL) ui_mark(u, row, !ui_marked(u, row));
        l->sel = row, u->side_focus = false;
        ui_show_sel(l);
    }
    if (!prompt && in->dclick >= 0 && l->top + in->dclick < l->n) {
        l->sel = l->top + in->dclick, u->side_focus = false;
        ui_edit_close(u);
        return (UiAction){ UA_OPEN, l->sel, false };
    }
    if (!prompt && in->backs > 0) return (UiAction){ UA_BACK, 0, false };
    if (!prompt && in->forwards > 0) return (UiAction){ UA_FORWARD, 0, false };
    isize extend = -1;   // a Shift move's start, marked to the cursor once the move is done (ui_extend)
    for (int k = 0; k < in->nev; k++, extend = ui_extend(u, extend)) {
        UiEvent e = in->ev[k];
        // Ctrl+Q (F7.1): every window closes, this one as Ctrl+Q closed it before (below).
        if (e.key == UK_QUIT_ALL && !u->quit_asking) {
            u->menu_on = false;
            if (in->jobs_all > 0) u->quit_asking = true, u->cancel_asking = false;
            return (UiAction){ UA_CLOSE_ALL, 0, in->jobs_all <= 0 };
        }
        if (u->quit_asking || u->cancel_asking) {
            bool answered, yes = ui_yes_no(e, &answered);
            if (!answered) continue;
            bool quit = u->quit_asking;
            u->quit_asking = u->cancel_asking = false;
            if (yes) return (UiAction){ quit ? UA_QUIT : UA_CANCEL_JOB, quit ? 0 : u->cancel_id, false };
            continue;
        }
        if (u->empty_asking) {
            bool answered, yes = ui_yes_no(e, &answered);
            if (!answered) continue;
            u->empty_asking = false;
            if (yes) return (UiAction){ UA_EMPTY_TRASH, 0, false };
            continue;
        }
        // The menu bar's keys (F7.1): F10 the first menu, Alt and a letter its menu; under a menu
        // open, its keys.
        if (!prompt && !u->menu_on && (e.key == UK_F10 || (e.key == UK_MENU && ui_menu_of(e.ch) >= 0))) {
            ui_menu_open(u, e.key == UK_F10 ? 0 : ui_menu_of(e.ch), true);
            continue;
        }
        if (u->menu_on && !prompt) {
            UiAction a;
            if (ui_menu_event(u, e, &a)) {
                if (a.kind != UA_NONE) return a;
                continue;
            }
        }
        if (u->props && !u->asking && !u->confirming) {   // (quit, cancel and empty took their keys above)
            UiAction a = ui_props_event(u, e);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        if (u->ow && !u->asking && !u->confirming) {
            UiAction a = ui_ow_event(u, e, in);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        if (u->editing && u->edit_path && !prompt && e.key == UK_CRUMBS) {   // from the location line: it closes
            ui_edit_close(u), ui_crumbs_focus(u, in);
            continue;
        }
        if (u->editing && !prompt) {
            UiAction a = ui_edit_event(u, e, in->paste);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        if (u->crumb_focus && !prompt) {
            UiAction a;
            if (ui_crumbs_event(u, in, e, &a)) {
                if (a.kind != UA_NONE) return a;
                continue;
            }
        }
        if (u->jobs_focus && !prompt && ui_jobs_event(u, in, e)) continue;   // before a clash's keys
        // Outside an edit line and the path buttons, Shift doesn't change Enter; with a move in the
        // files (arrows, Home, End, or H, J, K, L) it selects what the cursor passes over (below).
        bool shifted = e.key == UK_SEL_LEFT || e.key == UK_SEL_RIGHT || e.key == UK_SEL_HOME || e.key == UK_SEL_END ||
                       e.key == UK_SEL_UP || e.key == UK_SEL_DOWN;
        switch (e.key) {
        case UK_SEL_LEFT: e.key = UK_LEFT; break;
        case UK_SEL_RIGHT: e.key = UK_RIGHT; break;
        case UK_SEL_HOME: e.key = UK_HOME; break;
        case UK_SEL_END: e.key = UK_END; break;
        case UK_SEL_UP: e.key = UK_UP; break;
        case UK_SEL_DOWN: e.key = UK_DOWN; break;
        case UK_SHIFT_ENTER: e.key = UK_ENTER; break;
        default: break;
        }

        if (u->asking) {
            UiAction a = ui_ask_event(u, e);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        if (u->confirming) {
            UiAction a = ui_confirm_event(u, e);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        if (u->finding && u->find_keys && ui_find_event(u, names, e, in)) continue;
        // H, J, K and L (Shift and h, j, k, l) as the Shift arrows: here, after the questions and the
        // find bar have taken their letters (K keeps all in a clash; text in the find bar).
        if (e.key == UK_NONE && (e.ch == 'H' || e.ch == 'J' || e.ch == 'K' || e.ch == 'L')) {
            shifted = true;
            e = (UiEvent){ e.ch == 'H' ? UK_LEFT : e.ch == 'J' ? UK_DOWN : e.ch == 'K' ? UK_UP : UK_RIGHT, 0 };
        }
        if (shifted && !u->side_focus) extend = l->sel;
        // The keys and letters that move a cursor: the sidebar's when it has the keyboard. In the
        // icon view, Up and Down move a row and Left and Right a cell (F6.2).
        UiList *m = u->side_focus ? &u->side : l;
        isize page = m->rows > 1 ? m->rows - 1 : 1;
        bool grid = u->grid && m == l;
        UiKey key = e.key;
        if (key == UK_NONE) {
            switch (e.ch) {
            case 'j': key = UK_DOWN; break;
            case 'k': key = UK_UP; break;
            case 'l': key = UK_RIGHT; break;
            case 'h': key = UK_LEFT; break;
            case 'g': key = UK_HOME; break;
            case 'G': key = UK_END; break;
            case '/': key = UK_FIND; break;
            case '.': key = UK_HIDDEN; break;
            case 't': return (UiAction){ u->in_trash ? UA_NOT_IN_TRASH : UA_TERMINAL, 0, false };
            case 'r':
                if (u->in_trash && !u->side_focus && l->n > 0) return (UiAction){ UA_RESTORE, 0, false };
                continue;
            case '~': return (UiAction){ UA_HOMEDIR, 0, false };
            case 's': u->sort = (Sort){ (SortKey)((u->sort.key + 1) % SORT_KEYS), false }; return (UiAction){ UA_SORT, 0, false };
            case 'S': u->sort.reverse = !u->sort.reverse; return (UiAction){ UA_SORT, 0, false };
            case 'n': ui_find_again(u, names, 1); continue;
            case 'N': ui_find_again(u, names, -1); continue;
            case ' ':
                // Space marks the entry under the cursor, or unmarks it, and moves on.
                if (!u->side_focus && l->n > 0) ui_mark(u, l->sel, !ui_marked(u, l->sel)), ui_move(l, 1);
                continue;
            default: continue;
            }
        }
        if (grid && (key == UK_LEFT || key == UK_RIGHT)) {
            if (shifted) ui_move(l, key == UK_LEFT ? -1 : 1);   // a Shift move doesn't wrap
            else ui_step_round(l, key == UK_LEFT ? -1 : 1, false);
            continue;
        }
        if (grid && (key == UK_UP || key == UK_DOWN)) {
            if (shifted) ui_move_rows(l, key == UK_UP ? -1 : 1);
            else ui_step_round(l, key == UK_UP ? -1 : 1, true);
            continue;
        }
        if (grid && (key == UK_PAGEUP || key == UK_PAGEDOWN)) {
            ui_move_rows(l, key == UK_PAGEUP ? -page : page);
            continue;
        }
        switch (key) {
        case UK_UP:
            if (m == l && !shifted) ui_step_round(l, -1, false);
            else ui_step(u, m, -1);
            break;
        case UK_DOWN:
            if (m == l && !shifted) ui_step_round(l, 1, false);
            else ui_step(u, m, 1);
            break;
        case UK_PAGEUP: ui_step(u, m, -page); break;
        case UK_PAGEDOWN: ui_step(u, m, page); break;
        case UK_HOME: ui_step(u, m, -m->n); break;
        case UK_END: ui_step(u, m, m->n); break;
        case UK_VIEW_ICONS: return ui_view(u, true);
        case UK_VIEW_LIST: return ui_view(u, false);
        case UK_ZOOM_IN: return ui_zoom(u, ui_zoom_level(u->zoom) + 1);
        case UK_ZOOM_OUT: return ui_zoom(u, ui_zoom_level(u->zoom) - 1);
        case UK_ZOOM_RESET: return ui_zoom(u, ui_zoom_level(UI_ZOOM_DEFAULT));
        // Enter opens and Backspace goes up; Left and Right don't, in the list or the icon view
        // (Michael, 2026-10-24): they only open and close the tree's folders.
        case UK_ENTER:
            if (u->side_focus && u->side.n > 0) return (UiAction){ UA_PLACE, u->side.sel, false };
            if (!u->side_focus && l->n > 0) return (UiAction){ UA_OPEN, l->sel, u->marked > 0 };   // all: the marked ones (F7.3)
            break;
        case UK_RIGHT:
            if (u->side_focus && u->side.n > 0 && u->side_mode == SIDE_TREE) return (UiAction){ UA_TREE_RIGHT, u->side.sel, false };
            break;
        case UK_LEFT:
            if (u->side_focus && u->side.n > 0 && u->side_mode == SIDE_TREE) return (UiAction){ UA_TREE_LEFT, u->side.sel, false };
            break;
        case UK_BACKSPACE: return (UiAction){ UA_PARENT, 0, false };
        case UK_F9: return ui_side_mode(u, (SideMode)((u->side_mode + 1) % SIDE_MODES));
        case UK_TAB:
        case UK_SIDE:
            // Tab goes to the sidebar and back, Ctrl+Left there; a hidden one shows as places
            // (gui/ lists them for the mode, the cursor off a heading).
            if (key == UK_TAB && u->side_focus) {
                u->side_focus = false;
                break;
            }
            if (u->side_mode == SIDE_HIDDEN) {
                UiAction a = ui_side_mode(u, SIDE_PLACES);
                u->side_focus = true;
                return a;
            }
            u->side_focus = u->side.n > 0;
            if (u->side_focus) ui_side_settle(u, 1);
            break;
        case UK_ESC:
            // The selection, then the sidebar's keyboard (a job is cancelled from its row, F5.3).
            if (u->marked) ui_mark_clear(u);
            else u->side_focus = false;
            break;
        case UK_BACK: return (UiAction){ UA_BACK, 0, false };
        case UK_FORWARD: return (UiAction){ UA_FORWARD, 0, false };
        case UK_FIND: ui_find_open(u); break;
        case UK_HIDDEN: return (UiAction){ UA_HIDDEN, 0, false };
        case UK_ALL:
            for (isize i = 0; i < l->n; i++) ui_mark(u, i, true);
            break;
        case UK_F2:
            if (l->n > 0 && !u->side_focus && u->in_trash) return (UiAction){ UA_NOT_IN_TRASH, 0, false };
            if (l->n > 0 && !u->side_focus) ui_edit_rename(u, names.at(names.ctx, l->sel), names.dir && names.dir(names.ctx, l->sel));
            break;
        case UK_DELETE:
            // In the trash view Delete is for good, asked first.
            if (l->n > 0 && !u->side_focus && u->in_trash) u->confirming = true, u->confirm_n = u->marked ? u->marked : 1;
            else if (l->n > 0 && !u->side_focus) return (UiAction){ UA_TRASH, 0, false };
            break;
        case UK_SHIFT_DELETE:
            if (l->n > 0 && !u->side_focus) u->confirming = true, u->confirm_n = u->marked ? u->marked : 1;
            break;
        case UK_COPY: if (l->n > 0 && !u->side_focus) return (UiAction){ u->in_trash ? UA_NOT_IN_TRASH : UA_COPY, 0, false }; break;
        case UK_CUT: if (l->n > 0 && !u->side_focus) return (UiAction){ u->in_trash ? UA_NOT_IN_TRASH : UA_CUT, 0, false }; break;
        case UK_PASTE: return (UiAction){ u->in_trash ? UA_NOT_IN_TRASH : UA_PASTE, 0, false };
        case UK_UNDO: return (UiAction){ UA_UNDO, 0, false };
        case UK_NEWFOLDER: return (UiAction){ u->in_trash ? UA_NOT_IN_TRASH : UA_NEWFOLDER, 0, false };
        case UK_LOCATION: return (UiAction){ UA_LOCATION, 0, false };
        case UK_CRUMBS: ui_crumbs_focus(u, in); break;
        case UK_JOBS:   // the find bar first when it is open, then the job rows (Michael, 2026-10-26)
            if (u->finding) u->find_keys = true, u->crumb_focus = false, u->side_focus = false;
            else if (in->job_n > 0) u->jobs_focus = true, u->job_at = 0, u->crumb_focus = false, u->side_focus = false;
            break;
        case UK_PARENT: return (UiAction){ UA_PARENT, 0, false };
        case UK_TAB_NEW: return (UiAction){ UA_TAB_NEW, 0, false };
        case UK_NEW_WINDOW: return (UiAction){ u->in_trash ? UA_NOT_IN_TRASH : UA_NEW_WINDOW, 0, false };
        case UK_PROPERTIES: return ui_props_open(u);
        case UK_TERMINAL: return (UiAction){ u->in_trash ? UA_NOT_IN_TRASH : UA_TERMINAL, 0, false };
        case UK_TAB_CLOSE: return (UiAction){ UA_TAB_CLOSE, -1, false };
        case UK_TAB_NEXT: return (UiAction){ UA_TAB_STEP, 1, false };
        case UK_TAB_PREV: return (UiAction){ UA_TAB_STEP, -1, false };
        case UK_TAB_LEFT: return (UiAction){ UA_TAB_MOVE, -1, false };
        case UK_TAB_RIGHT: return (UiAction){ UA_TAB_MOVE, 1, false };
        case UK_TAB_JUMP: return (UiAction){ UA_TAB_JUMP, (isize)e.ch, false };
        default: break;
        }
    }
    return (UiAction){ UA_NONE, 0, false };
}

// ---- the path buttons (F4) ---------------------------------------------------------------------
//
// The top bar's path as buttons, one per folder. The bar keeps the deepest path it showed: going
// up keeps the deeper buttons (Nemo does), going anywhere off that path starts it anew. Under the
// home folder the first button is home, and the folders above it are left out.

enum { UI_CRUMBS_MAX = 128 };

typedef struct {
    isize end;    // the button's folder is deep[0..end)
    bool  home;   // the home folder's button
} UiCrumb;

typedef struct {
    char    deep[PATH_MAX];
    UiCrumb seg[UI_CRUMBS_MAX];
    int     n;
    int     cur;  // the button of the current folder
    int     lo;   // the first button shown when they don't all fit; -1: chosen to show cur
} UiCrumbs;

// p is dir or below it.
static bool ui_under(Str p, Str dir)
{
    if (str_eq(dir, S("/"))) return p.len > 0 && p.p[0] == '/';
    return str_starts_with(p, dir) && (p.len == dir.len || p.p[dir.len] == '/');
}

// The current folder is cwd (absolute); home is $HOME (NULL or empty: none).
static void ui_crumbs_set(UiCrumbs *c, const char *cwd, const char *home)
{
    Str cw = str_from_c(cwd), h = str_from_c(home);
    if (str_eq(cw, S(TRASH_URI))) {   // the trash view (F6): one button, "Trash"
        snprintf(c->deep, sizeof c->deep, "%s", TRASH_URI);
        c->seg[0] = (UiCrumb){ cw.len, false };
        c->n = 1, c->cur = 0, c->lo = -1;
        return;
    }
    while (h.len > 1 && h.p[h.len - 1] == '/') h.len--;
    if (!c->deep[0] || !ui_under(str_from_c(c->deep), cw)) snprintf(c->deep, sizeof c->deep, "%s", cwd);
    c->lo = -1;
    Str d = str_from_c(c->deep);
    c->n = 0;
    isize at;
    if (h.len > 1 && h.p[0] == '/' && ui_under(cw, h)) c->seg[c->n++] = (UiCrumb){ h.len, true }, at = h.len;
    else c->seg[c->n++] = (UiCrumb){ 1, false }, at = 1;
    while (at < d.len && c->n < UI_CRUMBS_MAX) {
        while (at < d.len && d.p[at] == '/') at++;
        isize e = at;
        while (e < d.len && d.p[e] != '/') e++;
        if (e > at) c->seg[c->n++] = (UiCrumb){ e, false };
        at = e;
    }
    // Past UI_CRUMBS_MAX buttons the current one may not be among them: the deepest shown stands
    // for it, never the root.
    c->cur = -1;
    for (int i = 0; i < c->n; i++)
        if (c->seg[i].end == cw.len) c->cur = i;
    if (c->cur < 0) c->cur = c->n - 1;
}

// Button i's folder.
static Str ui_crumb_path(const UiCrumbs *c, int i)
{
    return (Str){ c->deep, c->seg[i].end };
}

// Button i's label: "/" for the root, else its folder's own name (home's too).
static Str ui_crumb_label(const UiCrumbs *c, int i)
{
    if (!c->seg[i].home && c->seg[i].end == 1) return S("/");
    if (str_eq(ui_crumb_path(c, i), S(TRASH_URI))) return S("Trash");
    return path_base(ui_crumb_path(c, i));
}

// From lo on, how many buttons of widths w fit in room beside the two arrows: hi (at least lo + 1).
static int ui_crumbs_fit(const float *w, int n, float room, int lo)
{
    float used = 0;
    int hi = lo;
    while (hi < n && (hi == lo || used + w[hi] <= room)) used += w[hi++];
    return hi;
}

// The buttons shown, [*lo, *hi), and whether the arrows are (when not all fit). Without a scroll of
// its own (c->lo < 0) the window shows as many folders above the current one as it can, then the
// deeper ones, never leaving the current one out.
static bool ui_crumbs_window(const UiCrumbs *c, const float *w, float room, float arrow, int *lo, int *hi)
{
    float all = 0;
    for (int i = 0; i < c->n; i++) all += w[i];
    if (all <= room) return *lo = 0, *hi = c->n, false;
    room -= 2 * arrow;
    if (c->lo >= 0) {
        *lo = c->lo < c->n ? c->lo : c->n - 1;
    } else {
        *lo = 0;
        while (*lo < c->cur && ui_crumbs_fit(w, c->n, room, *lo) <= c->cur) (*lo)++;
    }
    *hi = ui_crumbs_fit(w, c->n, room, *lo);
    return true;
}

// The ‹ (by -1) and › (by 1) arrows: the window moves a button, from where it is now (lo).
static void ui_crumbs_shift(UiCrumbs *c, int lo, int by)
{
    int to = lo + by;
    c->lo = to < 0 ? 0 : to > c->n - 1 ? c->n - 1 : to;
}

// The window moved, if it must, so button at is shown (the one the keyboard marks, F5.2).
static void ui_crumbs_show(UiCrumbs *c, const float *w, float room, float arrow, int at)
{
    int lo, hi;
    ui_crumbs_window(c, w, room, arrow, &lo, &hi);   // all shown: at is among them
    if (at < lo) c->lo = at;
    while (at >= hi && lo < at) hi = ui_crumbs_fit(w, c->n, room - 2 * arrow, ++lo), c->lo = lo;
}

// ---- the location line (F4) -------------------------------------------------------------------

// path with home as "~" ("~/x" under it), into buf: the Properties panel's location (F7.2).
static void ui_tilde(char *buf, size_t cap, const char *path, const char *home)
{
    size_t n = home ? strlen(home) : 0;
    if (n > 1 && strncmp(path, home, n) == 0 && (path[n] == '/' || path[n] == 0)) snprintf(buf, cap, "~%s", path + n);
    else snprintf(buf, cap, "%s", path);
}

// What the location line says as a folder: "~" and "~/…" from home, a relative path from cwd, then
// "." and ".." taken out. False for an empty line, or one that climbs above "/".
static bool ui_location(Arena *a, Str text, const char *cwd, const char *home, Str *out)
{
    text = str_trim(text);
    if (text.len == 0) return false;
    Str base = str_from_c(cwd);
    if (text.p[0] == '~' && (text.len == 1 || text.p[1] == '/')) {
        if (!home || home[0] != '/') return false;
        base = str_from_c(home), text = str_cut(text, 1, text.len);
        while (text.len && text.p[0] == '/') text = str_cut(text, 1, text.len);
    }
    if (!path_join(a, base, text, out)) return false;
    if (out->len == 0) *out = S("/");
    return true;
}

// ---- tabs (F5) ---------------------------------------------------------------------------------
//
// The tab bar's order: place -> slot, where gui/ keeps each tab's folder, cursor and history.

enum { UI_TABS_MAX = 32 };

typedef struct {
    int slot[UI_TABS_MAX];
    int n;
    int cur;   // the place of the tab on screen
} UiTabs;

// A tab for slot, placed after the current one; shown when `show`. False when the bar is full.
static bool ui_tabs_open(UiTabs *t, int slot, bool show)
{
    if (t->n == UI_TABS_MAX) return false;
    int at = t->n == 0 ? 0 : t->cur + 1;
    memmove(t->slot + at + 1, t->slot + at, sizeof *t->slot * (size_t)(t->n - at));
    t->slot[at] = slot, t->n++;
    if (show) t->cur = at;   // the first tab is at 0, where cur already is
    return true;
}

// The tab at place `at` closed: its slot (now free), or -1 for no such tab. Closing the current
// one shows the tab to its right, or to its left at the end.
static int ui_tabs_close(UiTabs *t, int at)
{
    if (at < 0 || at >= t->n) return -1;
    int slot = t->slot[at];
    memmove(t->slot + at, t->slot + at + 1, sizeof *t->slot * (size_t)(t->n - at - 1));
    t->n--;
    if (at < t->cur || (at == t->cur && t->cur == t->n)) t->cur--;
    if (t->cur < 0) t->cur = 0;
    return slot;
}

// The next (1) or previous (-1) tab, wrapping.
static void ui_tabs_step(UiTabs *t, int by)
{
    if (t->n > 0) t->cur = ((t->cur + by) % t->n + t->n) % t->n;
}

// Alt+k: the k-th tab (1-based), Alt+9 the last; a tab that isn't there changes nothing.
static void ui_tabs_jump(UiTabs *t, int k)
{
    if (k == 9 && t->n > 0) t->cur = t->n - 1;
    else if (k >= 1 && k <= t->n) t->cur = k - 1;
}

// The current tab one place left (-1) or right (1), not past the ends.
static void ui_tabs_move(UiTabs *t, int by)
{
    int to = t->cur + by;
    if (to < 0 || to >= t->n) return;
    int s = t->slot[to];
    t->slot[to] = t->slot[t->cur], t->slot[t->cur] = s, t->cur = to;
}

// ---- history ----------------------------------------------------------------------------------

typedef struct {
    char dir[PATH_MAX];
    char sel[NAME_MAX + 1];   // the entry the cursor was on
} UiPlace;

typedef struct {
    UiPlace back[UI_HISTORY_MAX], fwd[UI_HISTORY_MAX];
    int     nback, nfwd;
} UiHistory;

static void hist_put(UiPlace *stack, int *n, const char *dir, Str sel)
{
    if (*n == UI_HISTORY_MAX) memmove(stack, stack + 1, sizeof *stack * (UI_HISTORY_MAX - 1)), (*n)--;   // the oldest goes
    snprintf(stack[*n].dir, sizeof stack[*n].dir, "%s", dir);
    snprintf(stack[*n].sel, sizeof stack[*n].sel, "%.*s", STR_ARG(sel));
    (*n)++;
}

// Leaving dir (the cursor on sel) for another folder: it goes on the back stack, and forward is
// cleared.
static void hist_leave(UiHistory *h, const char *dir, Str sel)
{
    hist_put(h->back, &h->nback, dir, sel);
    h->nfwd = 0;
}

// Back (dir -1) or forward (1) from dir: false when there is nowhere to go. *to is where to go;
// the folder left goes on the other stack.
static bool hist_step(UiHistory *h, int dir, const char *cur, Str sel, UiPlace *to)
{
    UiPlace *from = dir < 0 ? h->back : h->fwd, *other = dir < 0 ? h->fwd : h->back;
    int *nf = dir < 0 ? &h->nback : &h->nfwd, *no = dir < 0 ? &h->nfwd : &h->nback;
    if (*nf == 0) return false;
    *to = from[--*nf];
    hist_put(other, no, cur, sel);
    return true;
}

// A step that led nowhere (its folder is gone): the folder it left is taken back off the other
// stack, and the dead entry stays dropped.
static void hist_unstep(UiHistory *h, int dir)
{
    int *no = dir < 0 ? &h->nfwd : &h->nback;
    if (*no > 0) (*no)--;
}

// ---- timing -------------------------------------------------------------------------------

// A tooltip (Michael, 2026-10-24): shown once the pointer has rested UI_TIP_MS on one button (gui/
// numbers them, -1 for none); gone when the pointer moves to another, and after a click or a key
// until the pointer leaves that button.
enum { UI_TIP_MS = 600 };

typedef struct {
    int  target;   // the button under the pointer, -1 for none
    i64  since;    // when the pointer came onto it (ms)
    bool shown;
    bool off;      // a click or key on it: no tooltip until the pointer leaves
} UiTip;

// The pointer over target at now: true when that is another button than before.
static bool ui_tip_hover(UiTip *t, int target, i64 now)
{
    if (target == t->target) return false;
    *t = (UiTip){ target, now, false, false };
    return true;
}

// When the tooltip is due (ms), 0 when none is waiting.
static i64 ui_tip_due(const UiTip *t)
{
    return t->target >= 0 && !t->shown && !t->off ? t->since + UI_TIP_MS : 0;
}

// At now: true when the tooltip appears just now.
static bool ui_tip_tick(UiTip *t, i64 now)
{
    i64 due = ui_tip_due(t);
    if (!due || now < due) return false;
    return t->shown = true;
}

// A click or key: the tooltip goes, and stays away while the pointer stays. True when one was shown.
static bool ui_tip_act(UiTip *t)
{
    bool was = t->shown;
    t->shown = false, t->off = t->target >= 0;
    return was;
}

enum { UI_DCLICK_MS = 400 };

typedef struct {
    int row;      // the row of the last press, -1 for none
    i64 at;       // when
} UiPress;

// A left press on screen row `row` at `now` (ms): 2 for the second press on the same row within
// UI_DCLICK_MS (a double click, which then starts over), else 1. raylib reports presses, not
// double clicks, as terminals do for prdash-tui.
static int ui_press(UiPress *p, int row, i64 now)
{
    if (p->row == row && row >= 0 && now - p->at <= UI_DCLICK_MS) {
        p->row = -1;
        return 2;
    }
    p->row = row, p->at = now;
    return 1;
}

// When a batch of file system events ends: `quiet` ms after its last event, but never more than
// `most` after its first.
static i64 ui_batch_due(i64 first, i64 last, int quiet, int most)
{
    i64 q = last + quiet, m = first + most;
    return q < m ? q : m;
}

// ---- text that fits ------------------------------------------------------------------------

static float ui_width(Str s, float (*adv)(u32))
{
    float w = 0;
    for (isize i = 0; i < s.len;) w += adv(utf8_next(s.p, s.len, &i));
    return w;
}

// Whether s fits in width. When it doesn't, [*from, *to) is the part to keep beside an ellipsis
// of width `dots`: whole code points from the start (a name), or from the end when keep_end (a
// path, whose end is the part that matters). Nothing is kept when not even one fits.
static bool ui_fit(Str s, float width, float dots, float (*adv)(u32), bool keep_end, isize *from, isize *to)
{
    *from = 0, *to = s.len;
    if (ui_width(s, adv) <= width) return true;
    float room = width - dots, w = 0;
    if (keep_end) {
        *from = s.len;
        while (*from > 0) {
            isize j = *from - 1;
            while (j > 0 && ((unsigned char)s.p[j] & 0xC0) == 0x80) j--;
            isize k = j;
            float a = adv(utf8_next(s.p, s.len, &k));
            if (w + a > room) break;
            w += a, *from = j;
        }
        return false;
    }
    *to = 0;
    for (isize i = 0; i < s.len;) {
        float a = adv(utf8_next(s.p, s.len, &i));
        if (w + a > room) break;
        w += a, *to = i;
    }
    return false;
}

// Whether s breaks after byte i: after a space, '-', '_' or '.', where a name reads as words.
static bool ui_breaks_after(Str s, isize i)
{
    char c = s.p[i];
    return c == ' ' || c == '-' || c == '_' || c == '.';
}

// s in lines of `width` at most, for a name under its icon (F6.2): each line ends after the last
// break (ui_breaks_after) that fits, or between code points when none does, and holds one code
// point at least. At most `most` lines: the last holds the rest, cut to fit with an ellipsis of
// width `dots` when it doesn't (*cut true). Line k is [from[k], to[k]); returns how many.
static int ui_wrap(Str s, float width, float dots, float (*adv)(u32), int most, isize *from, isize *to, bool *cut)
{
    *cut = false;
    int n = 0;
    isize at = 0;
    while (n < most && (at < s.len || n == 0)) {
        Str rest = str_cut(s, at, s.len);
        isize f, t;
        if (n == most - 1 || ui_width(rest, adv) <= width) {
            *cut = !ui_fit(rest, width, dots, adv, false, &f, &t);
            from[n] = at, to[n] = at + t, n++;
            break;
        }
        // The longest run of whole code points that fits, then back to its last break.
        float w = 0;
        isize end = at, brk = -1;
        for (isize i = at; i < s.len;) {
            isize j = i;
            float a = adv(utf8_next(s.p, s.len, &j));
            if (w + a > width && end > at) break;
            w += a, end = j;
            if (ui_breaks_after(s, i)) brk = j;
            i = j;
        }
        isize e = brk > at ? brk : end;   // end < s.len: the rest fitting was the case above
        from[n] = at, to[n] = e, n++;
        at = e;
    }
    return n;
}

// ---- display --------------------------------------------------------------------------------

// A size as ls -h prints it, in binary units: "512 B", "4.0 KiB", "13 MiB", one decimal below 10.
static void ui_size(char *buf, size_t cap, i64 size)
{
    static const char *const UNITS[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
    if (size < 1024) {
        snprintf(buf, cap, "%lld B", (long long)size);
        return;
    }
    double v = (double)size;
    int u = 0;
    while (v >= 1024 && u < COUNT(UNITS) - 1) v /= 1024, u++;
    // As printed: 1023.7 KiB would show as "1024 KiB", so it goes up a unit ("1.0 MiB").
    if (v + 0.5 >= 1024 && u < COUNT(UNITS) - 1) v /= 1024, u++;
    if (v < 9.95) snprintf(buf, cap, "%.1f %s", v, UNITS[u]);
    else snprintf(buf, cap, "%.0f %s", v, UNITS[u]);
}

// A modification time: "14:03" today, "Oct 20" this year, "2025-10-20" before, as `now` sees it
// (local time).
// What the status line says when no message is up (F6): about the one entry marked, how many are
// marked, or else the folder's count and its drive's free space.
typedef struct {
    isize n, hidden;     // the listing's entries, and its dot-files (left out or not)
    isize marked;
    Str   one;           // marked == 1: its name,
    bool  one_dir;       //   whether it is a folder,
    isize one_count;     //   a folder's item count (< 0 unknown),
    i64   one_size;      //   a file's size (< 0 none: a broken link, a device)
    i64   avail;         // the drive's free space, < 0 unknown
} UiInfo;

static void ui_status_info(char *buf, size_t cap, const UiInfo *in)
{
    char size[32];
    if (in->marked == 1) {
        int k = snprintf(buf, cap, "\"%.*s\" selected", STR_ARG(in->one));
        if (k < 0 || (size_t)k >= cap) return;
        if (in->one_dir && in->one_count >= 0) snprintf(buf + k, cap - (size_t)k, " (%td item%s)", in->one_count, in->one_count == 1 ? "" : "s");
        else if (!in->one_dir && in->one_size >= 0) ui_size(size, sizeof size, in->one_size), snprintf(buf + k, cap - (size_t)k, " (%s)", size);
        return;
    }
    if (in->marked > 1) {
        snprintf(buf, cap, "%td items selected", in->marked);
        return;
    }
    int k = snprintf(buf, cap, "%td item%s", in->n, in->n == 1 ? "" : "s");
    if (k > 0 && in->hidden && (size_t)k < cap) k += snprintf(buf + k, cap - (size_t)k, " (%td hidden)", in->hidden);
    if (k > 0 && in->avail >= 0 && (size_t)k < cap) ui_size(size, sizeof size, in->avail), snprintf(buf + k, cap - (size_t)k, ", %s free", size);
}

static void ui_when(char *buf, size_t cap, i64 mtime, i64 now)
{
    time_t t = (time_t)mtime, n = (time_t)now;
    struct tm tm, tn;
    localtime_r(&t, &tm);
    localtime_r(&n, &tn);
    if (tm.tm_year == tn.tm_year && tm.tm_yday == tn.tm_yday) strftime(buf, cap, "%H:%M", &tm);
    else if (tm.tm_year == tn.tm_year) strftime(buf, cap, "%b %e", &tm);
    else strftime(buf, cap, "%Y-%m-%d", &tm);
}
