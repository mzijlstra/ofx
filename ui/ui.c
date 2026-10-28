// What the keys, characters, wheel and clicks do: the list's cursor and scroll, the sidebar's,
// find, sort, the history, the selection, the edit line and the prompts, and the one action a
// frame may ask of gui/ (open, go somewhere, toggle hidden files, sort again, change files).
// raylib-free and unit-tested; gui/main.c feeds it (docs/plan.md, F1 "Keys", F2 and F3).

#include "../base/base.h"

#include <fnmatch.h>
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
    UK_SHIFT_ENTER,   // on a path button: a background tab; in the files Open With (Michael,
                      // 2026-10-27); elsewhere as Enter
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
    UK_INVERT,     // Ctrl+Shift+I: the selection inverted (F7.4)
    UK_RELOAD,     // Ctrl+R: the folder listed again (F7.4)
    UK_HOMEDIR,    // Alt+Home: home, as ~ (F7.4)
    UK_SELECT_MATCH,   // Ctrl+Shift+A: Select Items Matching, the find bar's select mode (F7.4)
    UK_TRASH_VIEW, // Alt+End: the trash view, as Alt+Home is home (Michael, 2026-10-27)
    UK_ADD_BOOKMARK,   // Ctrl+D: the folder shown, bookmarked (F7.5)
    UK_EDIT_BOOKMARKS, // Ctrl+B: the Edit Bookmarks panel (F7.5)
    UK_ALT_DOWN,   // Alt+Down: in Edit Bookmarks a bookmark down (Alt+Up, UK_PARENT, up); elsewhere nothing
    UK_SHORTCUTS,  // F1, Ctrl+F1: the Keyboard Shortcuts panel (F7.5)
    UK_MAKE_LINK,  // Ctrl+M: Make Link (F8, as Nemo)
    UK_CONTEXT,    // Shift+F10, the Menu key: the right-click menu of the files' cursor, or of the folder shown (F8)
    UK_PERMS_COLS, // Ctrl+P: View → Show Permissions (Michael, 2026-10-27: no printing in ofx)
    UK_GROUP_COL,  // Ctrl+G: View → Show Group (2026-10-27)
    UK_SEARCH,     // Ctrl+Shift+F: a search below the folder shown, in a tab of its own (F9)
    UK_CONTAINING, // Ctrl+Enter: in the search view, the cursor's entry's folder, it selected (F9)
    // In a text field a word at a time (Michael, 2026-10-28): Ctrl+Left and Ctrl+Right move (elsewhere
    // they are the sidebar's keys, UK_SIDE, and the location line, UK_LOCATION), Ctrl+Shift+Left and
    // Ctrl+Shift+Right select, Ctrl+Backspace and Ctrl+Delete delete; outside a field the last four do
    // nothing.
    UK_WORD_LEFT, UK_WORD_RIGHT, UK_SEL_WORD_LEFT, UK_SEL_WORD_RIGHT, UK_WORD_BACKSPACE, UK_WORD_DELETE,
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
    // A right press (F8): a list row (its place on screen), or the files' empty space; a sidebar row
    // (on screen), a path button (its index), each -1 for none.
    int     ctx_row;
    bool    ctx_here;
    int     ctx_side, ctx_crumb;
    int     ctx_tab;     // a tab (its place in the bar), -1 for none (F8)
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
    bool    search_field;   // the search bar's field pressed (F9): the keys go there
    // Edit Bookmarks (F7.5): a row pressed or double-clicked (its place on screen), -1 for none; a
    // button pressed (1 Remove, 2 Rename, 3 Move Up, 4 Move Down), 0 for none.
    int     bmp_row, bmp_drow, bmp_btn;
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
    UA_RELOAD,      // the folder listed again (F7.4: a drive that sends no change events)
    UA_TRASH_VIEW,  // to the trash view (Go, F7.4)
    UA_DUPLICATE,   // the marked entries (or the cursor's) copied beside themselves (Edit, F7.4)
    UA_ADD_BOOKMARK,   // the folder shown, added to the bookmarks file (F7.5)
    UA_BOOKMARK,    // index: the menu's bookmark (0..UI_MENU_BOOKMARKS-1, gui/'s list of them) to go to
    // Edit Bookmarks (F7.5); index: the line in the file
    UA_BM_EDIT,     // the panel opened (Ui.bmp): gui/ reads the file again
    UA_BM_GO,       // to that bookmark; the panel closed
    UA_BM_REMOVE, UA_BM_UP, UA_BM_DOWN,
    UA_BM_NAME,     // its rename field opened: gui/ puts its label there
    UA_BM_RENAME,   // the field's Enter: Ui.bmp_text is its label (empty: none)
    UA_MAKE_LINK,   // a symbolic link "Link to <name>" beside each marked entry (or the cursor's) (F8)
    UA_FOLLOW,      // index: the entry, a link: to its target's folder, the target selected (F8)
    UA_TAB_OTHERS,  // every tab but the current one closed (F8)
    UA_COLORS,      // the list's colours on or off (Ui.colors): gui/ remembers it
    UA_MENUBAR,     // the menu bar hidden or shown (Ui.bar_hidden): gui/ remembers it
    UA_PERMS_COLS,  // the list's Owner and Permissions shown or not (Ui.perms_cols): gui/ remembers it
    UA_GROUP_COL,   // the list's Group beside them shown or not (Ui.group_col): gui/ remembers it
    UA_FOLDER_DO,   // index: a UiMenuItem of the folder menu, for the folder it opened on (Ui.ctx_side or ctx_crumb, F8)
    UA_SEARCH,      // a search below the folder shown (all: below the folder entry index), in a new tab (F9)
    UA_NOT_IN_SEARCH,   // something the search view can't do: paste, a new folder, a bookmark of it (F9; gui/ says so)
    UA_CONTAINING,  // index: a search result: its folder, it selected (F9)
} UiActionKind;
// A right-click menu's entry on a folder of the files (F8) acts on that folder, not the folder
// shown: UA_NEW_WINDOW, UA_TERMINAL, UA_PASTE and UA_ADD_BOOKMARK with `all` set; index is the entry.

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

// The menu bar (F7.1, docs/plan.md "F7 design"; Edit, View and Go F7.4): its menus, each a list of
// entries; UM_SEP is a line between groups.
typedef enum {
    UM_SEP, UM_NEW_TAB, UM_NEW_WINDOW, UM_NEW_FOLDER, UM_NEW_FILE, UM_OPEN, UM_OPEN_WITH, UM_PROPERTIES, UM_CLOSE_ALL, UM_CLOSE,
    UM_UNDO, UM_CUT, UM_COPY, UM_PASTE, UM_SELECT_ALL, UM_SELECT_MATCH, UM_INVERT, UM_DUPLICATE, UM_MAKE_LINK, UM_RENAME, UM_TRASH, UM_DELETE, UM_RESTORE,
    UM_RELOAD, UM_SIDE_PLACES, UM_SIDE_TREE, UM_SIDE_NONE, UM_HIDDEN, UM_PERMS_COLS, UM_GROUP_COL, UM_COLORS, UM_SHOW_MENU, UM_SORT_NAME, UM_SORT_SIZE, UM_SORT_MTIME, UM_SORT_REVERSE,
    UM_ZOOM_IN, UM_ZOOM_OUT, UM_ZOOM_RESET, UM_VIEW_LIST, UM_VIEW_ICONS,
    UM_PARENT, UM_BACK, UM_FORWARD, UM_HOME, UM_TRASH_VIEW, UM_FIND, UM_SEARCH, UM_LOCATION, UM_TERMINAL,
    UM_ADD_BOOKMARK, UM_EDIT_BOOKMARKS, UM_BM_0, UM_BM_1, UM_BM_2, UM_BM_3, UM_BM_4, UM_BM_5, UM_BM_6, UM_BM_7, UM_BM_MORE,
    UM_SHORTCUTS, UM_ABOUT,
    UM_OPEN_TAB, UM_EMPTY_TRASH, UM_HERE_PROPS, UM_OPEN_WINDOW, UM_PASTE_INTO, UM_TERMINAL_IN, UM_FOLLOW, UM_BOOKMARK_IT,
    UM_CONTAINING, UM_SEARCH_IN,
    UM_OPEN_IT, UM_CUT_IT, UM_COPY_IT, UM_BM_RENAME_IT, UM_BM_UP_IT, UM_BM_DOWN_IT, UM_BM_REMOVE_IT, UM_BM_EDIT_IT, UM_EJECT_IT,
    UM_EMPTY_IT, UM_PROPS_IT, UM_TAB_LEFT, UM_TAB_RIGHT, UM_TAB_OTHERS,
    UM_ITEMS
} UiMenuItem;

// The Bookmarks menu's: the first 8 of the file's, in its order (Michael, 2026-10-27).
enum { UI_MENU_BOOKMARKS = 8 };

// Each entry's label, its key as the menu shows it, and its name in a script's state line; the key
// event it stands for (F7.4: picked, the entry runs that key, so the two can't differ; none: its
// action in ui_menu_pick); and its mark: 'c' a check when on, 'r' one of a group, the one that is.
static const struct { const char *label, *key, *word; UiEvent ev; char mark; } UI_MENU_ITEM[UM_ITEMS] = {
    [UM_SEP] = { "", "", "-" },
    [UM_NEW_TAB] = { "New Tab", "Ctrl+T", "new-tab" },
    [UM_NEW_WINDOW] = { "New Window", "Ctrl+N", "new-window" },
    [UM_NEW_FOLDER] = { "Create New Folder", "Ctrl+Shift+N", "new-folder" },
    [UM_NEW_FILE] = { "Create New Empty File", "", "new-file" },
    [UM_OPEN] = { "Open", "Enter", "open" },
    [UM_OPEN_WITH] = { "Open With Other Application\xE2\x80\xA6", "Shift+Enter", "open-with" },   // (its own action: with the keys in the sidebar Shift+Enter is Enter there)
    [UM_PROPERTIES] = { "Properties\xE2\x80\xA6", "Ctrl+I", "properties" },
    [UM_CLOSE_ALL] = { "Close All Windows", "Ctrl+Q", "close-all" },
    [UM_CLOSE] = { "Close", "Ctrl+W", "close" },
    // Edit
    [UM_UNDO] = { "Undo", "Ctrl+Z", "undo", { UK_UNDO, 0 } },
    [UM_CUT] = { "Cut", "Ctrl+X", "cut", { UK_CUT, 0 } },
    [UM_COPY] = { "Copy", "Ctrl+C", "copy", { UK_COPY, 0 } },
    [UM_PASTE] = { "Paste", "Ctrl+V", "paste", { UK_PASTE, 0 } },
    [UM_SELECT_ALL] = { "Select All", "Ctrl+A", "select-all", { UK_ALL, 0 } },
    [UM_SELECT_MATCH] = { "Select Items Matching\xE2\x80\xA6", "Ctrl+Shift+A", "select-match", { UK_SELECT_MATCH, 0 } },
    [UM_INVERT] = { "Invert Selection", "Ctrl+Shift+I", "invert", { UK_INVERT, 0 } },
    [UM_DUPLICATE] = { "Duplicate", "", "duplicate" },
    [UM_MAKE_LINK] = { "Make Link", "Ctrl+M", "make-link", { UK_MAKE_LINK, 0 } },
    [UM_RENAME] = { "Rename\xE2\x80\xA6", "F2", "rename", { UK_F2, 0 } },
    [UM_TRASH] = { "Move to Trash", "Delete", "trash", { UK_DELETE, 0 } },
    [UM_DELETE] = { "Delete\xE2\x80\xA6", "Shift+Delete", "delete", { UK_SHIFT_DELETE, 0 } },
    [UM_RESTORE] = { "Restore", "r", "restore", { UK_NONE, 'r' } },
    // View
    [UM_RELOAD] = { "Reload", "Ctrl+R", "reload", { UK_RELOAD, 0 } },
    [UM_SIDE_PLACES] = { "Places Sidebar", "", "side-places", .mark = 'r' },
    [UM_SIDE_TREE] = { "Tree Sidebar", "", "side-tree", .mark = 'r' },
    [UM_SIDE_NONE] = { "No Sidebar", "", "side-none", .mark = 'r' },
    [UM_HIDDEN] = { "Show Hidden Files", "Ctrl+H", "hidden", { UK_HIDDEN, 0 }, 'c' },
    [UM_PERMS_COLS] = { "Show Permissions", "Ctrl+P", "perms-cols", { UK_PERMS_COLS, 0 }, 'c' },   // the list's Owner and Permissions (2026-10-27)
    [UM_GROUP_COL] = { "Show Group", "Ctrl+G", "group-col", { UK_GROUP_COL, 0 }, 'c' },   // and Group, off by default as eza's (Michael, 2026-10-27)
    [UM_COLORS] = { "Show Colors", "", "colors", .mark = 'c' },   // the list in eza's colours (2026-10-27)
    [UM_SHOW_MENU] = { "Show Menu", "", "show-menu", .mark = 'c' },   // the menu bar (2026-10-27): F10 and Alt+letters open its menus either way
    [UM_SORT_NAME] = { "Sort by Name", "", "sort-name", .mark = 'r' },
    [UM_SORT_SIZE] = { "Sort by Size", "", "sort-size", .mark = 'r' },
    [UM_SORT_MTIME] = { "Sort by Modified", "", "sort-mtime", .mark = 'r' },
    [UM_SORT_REVERSE] = { "Reverse Order", "S", "sort-reverse", { UK_NONE, 'S' }, 'c' },
    [UM_ZOOM_IN] = { "Zoom In", "Ctrl+=", "zoom-in", { UK_ZOOM_IN, 0 } },
    [UM_ZOOM_OUT] = { "Zoom Out", "Ctrl+-", "zoom-out", { UK_ZOOM_OUT, 0 } },
    [UM_ZOOM_RESET] = { "Normal Size", "Ctrl+0", "zoom-reset", { UK_ZOOM_RESET, 0 } },
    [UM_VIEW_LIST] = { "List", "Ctrl+1", "view-list", { UK_VIEW_LIST, 0 }, 'r' },
    [UM_VIEW_ICONS] = { "Icons", "Ctrl+2", "view-icons", { UK_VIEW_ICONS, 0 }, 'r' },
    // Go
    [UM_PARENT] = { "Open Parent", "Alt+Up", "parent", { UK_PARENT, 0 } },
    [UM_BACK] = { "Back", "Alt+Left", "back", { UK_BACK, 0 } },
    [UM_FORWARD] = { "Forward", "Alt+Right", "forward", { UK_FORWARD, 0 } },
    [UM_HOME] = { "Home", "Alt+Home", "home", { UK_HOMEDIR, 0 } },
    [UM_TRASH_VIEW] = { "Trash", "Alt+End", "trash-view", { UK_TRASH_VIEW, 0 } },
    [UM_FIND] = { "Find in Folder\xE2\x80\xA6", "Ctrl+F", "find", { UK_FIND, 0 } },
    [UM_SEARCH] = { "Search\xE2\x80\xA6", "Ctrl+Shift+F", "search", { UK_SEARCH, 0 } },   // below the folder shown (F9)
    [UM_LOCATION] = { "Location\xE2\x80\xA6", "Ctrl+L", "location", { UK_LOCATION, 0 } },
    [UM_TERMINAL] = { "Terminal Here", "Alt+Enter", "terminal", { UK_TERMINAL, 0 } },
    // Bookmarks (F7.5); a bookmark's label is gui/'s (the file's), marked when it is the folder shown
    [UM_ADD_BOOKMARK] = { "Add Bookmark", "Ctrl+D", "add-bookmark", { UK_ADD_BOOKMARK, 0 } },
    [UM_EDIT_BOOKMARKS] = { "Edit Bookmarks\xE2\x80\xA6", "Ctrl+B", "edit-bookmarks", { UK_EDIT_BOOKMARKS, 0 } },
    [UM_BM_MORE] = { "", "", "bm-more" },
    // Help: no manual (Michael, 2026-10-27)
    [UM_SHORTCUTS] = { "Keyboard Shortcuts", "F1", "shortcuts", { UK_SHORTCUTS, 0 } },   // "N more in Edit Bookmarks…" (gui/'s label): the panel, on the first not listed
    [UM_ABOUT] = { "About", "", "about" },
    // The right-click menus' own (F8): a folder in a tab behind (its row's middle click); the trash
    // view's Empty Trash; the folder shown's Properties, whatever is marked
    [UM_OPEN_TAB] = { "Open in New Tab", "Middle click", "open-tab" },
    [UM_EMPTY_TRASH] = { "Empty Trash\xE2\x80\xA6", "", "empty-trash" },
    [UM_HERE_PROPS] = { "Properties\xE2\x80\xA6", "", "here-properties" },
    // and on one folder of the files, that folder: a window, a paste into it, a terminal, a bookmark;
    // on a link, its target
    [UM_OPEN_WINDOW] = { "Open in New Window", "", "open-window" },
    [UM_PASTE_INTO] = { "Paste Into Folder", "", "paste-into" },
    [UM_TERMINAL_IN] = { "Open in Terminal", "", "terminal-in" },
    [UM_FOLLOW] = { "Follow Link", "", "follow" },
    // the search view's (F9): a result's folder, it selected; and a folder searched below
    [UM_CONTAINING] = { "Open Containing Folder", "Ctrl+Enter", "containing", { UK_CONTAINING, 0 } },
    [UM_SEARCH_IN] = { "Search in Folder\xE2\x80\xA6", "", "search-in" },
    [UM_BOOKMARK_IT] = { "Add Bookmark", "", "bookmark-it" },
    // and on a folder elsewhere (a sidebar row, a path button): that folder; a bookmark's own, as
    // Edit Bookmarks has them; a drive's eject; the trash's emptying
    [UM_OPEN_IT] = { "Open", "", "open-it" },
    [UM_CUT_IT] = { "Cut", "", "cut-it" },
    [UM_COPY_IT] = { "Copy", "", "copy-it" },
    [UM_BM_RENAME_IT] = { "Rename\xE2\x80\xA6", "", "bm-rename" },
    [UM_BM_UP_IT] = { "Move Up", "", "bm-up" },
    [UM_BM_DOWN_IT] = { "Move Down", "", "bm-down" },
    [UM_BM_REMOVE_IT] = { "Remove", "", "bm-remove" },
    [UM_BM_EDIT_IT] = { "Edit Bookmarks\xE2\x80\xA6", "Ctrl+B", "bm-edit" },
    [UM_EJECT_IT] = { "Eject", "", "eject" },
    [UM_EMPTY_IT] = { "Empty Trash\xE2\x80\xA6", "", "empty-it" },
    [UM_PROPS_IT] = { "Properties\xE2\x80\xA6", "", "properties-it" },
    // and on a tab
    [UM_TAB_LEFT] = { "Move Tab Left", "Ctrl+Shift+PgUp", "tab-left", { UK_TAB_LEFT, 0 } },
    [UM_TAB_RIGHT] = { "Move Tab Right", "Ctrl+Shift+PgDn", "tab-right", { UK_TAB_RIGHT, 0 } },
    [UM_TAB_OTHERS] = { "Close Other Tabs", "", "tab-others" },
    [UM_BM_0] = { "", "", "bm-0", .mark = 'r' }, [UM_BM_1] = { "", "", "bm-1", .mark = 'r' },
    [UM_BM_2] = { "", "", "bm-2", .mark = 'r' }, [UM_BM_3] = { "", "", "bm-3", .mark = 'r' },
    [UM_BM_4] = { "", "", "bm-4", .mark = 'r' }, [UM_BM_5] = { "", "", "bm-5", .mark = 'r' },
    [UM_BM_6] = { "", "", "bm-6", .mark = 'r' }, [UM_BM_7] = { "", "", "bm-7", .mark = 'r' },
};

static const UiMenuItem UI_MENU_FILE[] = {
    UM_NEW_TAB, UM_NEW_WINDOW, UM_NEW_FOLDER, UM_NEW_FILE, UM_SEP, UM_OPEN, UM_OPEN_WITH, UM_SEP, UM_PROPERTIES, UM_SEP,
    UM_CLOSE_ALL, UM_CLOSE,
};
// Move to Trash shows outside the trash view, Restore in it (ui_menu_shown).
static const UiMenuItem UI_MENU_EDIT[] = {
    UM_UNDO, UM_SEP, UM_CUT, UM_COPY, UM_PASTE, UM_SEP, UM_SELECT_ALL, UM_SELECT_MATCH, UM_INVERT, UM_SEP, UM_DUPLICATE, UM_MAKE_LINK, UM_RENAME, UM_SEP,
    UM_TRASH, UM_RESTORE, UM_DELETE,
};
static const UiMenuItem UI_MENU_VIEW[] = {
    UM_RELOAD, UM_SEP, UM_SIDE_PLACES, UM_SIDE_TREE, UM_SIDE_NONE, UM_SEP, UM_HIDDEN, UM_PERMS_COLS, UM_GROUP_COL, UM_COLORS, UM_SHOW_MENU, UM_SEP,
    UM_SORT_NAME, UM_SORT_SIZE, UM_SORT_MTIME, UM_SORT_REVERSE, UM_SEP, UM_ZOOM_IN, UM_ZOOM_OUT, UM_ZOOM_RESET, UM_SEP,
    UM_VIEW_LIST, UM_VIEW_ICONS,
};
static const UiMenuItem UI_MENU_GO[] = {
    UM_PARENT, UM_BACK, UM_FORWARD, UM_SEP, UM_HOME, UM_TRASH_VIEW, UM_SEP, UM_FIND, UM_SEARCH, UM_LOCATION, UM_TERMINAL,
};
static const UiMenuItem UI_MENU_BOOKMARKS_[] = {
    UM_ADD_BOOKMARK, UM_EDIT_BOOKMARKS, UM_SEP, UM_BM_0, UM_BM_1, UM_BM_2, UM_BM_3, UM_BM_4, UM_BM_5, UM_BM_6, UM_BM_7, UM_BM_MORE,
};

static const UiMenuItem UI_MENU_HELP[] = { UM_SHORTCUTS, UM_SEP, UM_ABOUT };
// The right-click menus (F8, docs/plan.md "F8 design"): an entry of the files (the marks, or the
// cursor's), and the files' empty space (the folder shown).
static const UiMenuItem UI_MENU_CTX_ENTRY[] = {
    UM_OPEN, UM_CONTAINING, UM_OPEN_TAB, UM_OPEN_WINDOW, UM_OPEN_WITH, UM_SEP, UM_CUT, UM_COPY, UM_PASTE_INTO, UM_DUPLICATE, UM_SEP,
    UM_MAKE_LINK, UM_RENAME, UM_SEP, UM_TERMINAL_IN, UM_SEARCH_IN, UM_FOLLOW, UM_BOOKMARK_IT, UM_SEP, UM_TRASH, UM_RESTORE, UM_DELETE, UM_SEP,
    UM_PROPERTIES,
};
// A folder elsewhere: a tree row, a path button, a place (Trash, a bookmark, a drive have their own).
static const UiMenuItem UI_MENU_CTX_FOLDER[] = {
    UM_OPEN_IT, UM_OPEN_TAB, UM_OPEN_WINDOW, UM_TERMINAL_IN, UM_SEARCH_IN, UM_SEP, UM_CUT_IT, UM_COPY_IT, UM_PASTE_INTO, UM_SEP, UM_BOOKMARK_IT,
    UM_BM_RENAME_IT, UM_BM_UP_IT, UM_BM_DOWN_IT, UM_BM_REMOVE_IT, UM_BM_EDIT_IT, UM_SEP, UM_EJECT_IT, UM_EMPTY_IT, UM_SEP, UM_PROPS_IT,
};
// A tab (the current one: a right press on another shows it first).
static const UiMenuItem UI_MENU_CTX_TAB[] = { UM_NEW_TAB, UM_SEP, UM_TAB_LEFT, UM_TAB_RIGHT, UM_SEP, UM_CLOSE, UM_TAB_OTHERS };
static const UiMenuItem UI_MENU_CTX_HERE[] = {
    UM_NEW_FOLDER, UM_NEW_FILE, UM_EMPTY_TRASH, UM_SEP, UM_PASTE, UM_SELECT_ALL, UM_SEP, UM_TERMINAL, UM_HIDDEN, UM_SEP,
    UM_ADD_BOOKMARK, UM_SEP, UM_HERE_PROPS,
};

// The menus in the bar's order: a title, its Alt letter, its entries.
static const struct { const char *title; char letter; const UiMenuItem *items; int n; } UI_MENU[] = {
    { "File", 'f', UI_MENU_FILE, sizeof UI_MENU_FILE / sizeof UI_MENU_FILE[0] },
    { "Edit", 'e', UI_MENU_EDIT, sizeof UI_MENU_EDIT / sizeof UI_MENU_EDIT[0] },
    { "View", 'v', UI_MENU_VIEW, sizeof UI_MENU_VIEW / sizeof UI_MENU_VIEW[0] },
    { "Go", 'g', UI_MENU_GO, sizeof UI_MENU_GO / sizeof UI_MENU_GO[0] },
    { "Bookmarks", 'b', UI_MENU_BOOKMARKS_, sizeof UI_MENU_BOOKMARKS_ / sizeof UI_MENU_BOOKMARKS_[0] },
    { "Help", 'h', UI_MENU_HELP, sizeof UI_MENU_HELP / sizeof UI_MENU_HELP[0] },
    // The right-click menus (F8): no title in the bar, no letter; the title names them in a script
    { "entry", 0, UI_MENU_CTX_ENTRY, sizeof UI_MENU_CTX_ENTRY / sizeof UI_MENU_CTX_ENTRY[0] },
    { "here", 0, UI_MENU_CTX_HERE, sizeof UI_MENU_CTX_HERE / sizeof UI_MENU_CTX_HERE[0] },
    { "folder", 0, UI_MENU_CTX_FOLDER, sizeof UI_MENU_CTX_FOLDER / sizeof UI_MENU_CTX_FOLDER[0] },
    { "tab", 0, UI_MENU_CTX_TAB, sizeof UI_MENU_CTX_TAB / sizeof UI_MENU_CTX_TAB[0] },
};
// The bar's menus come first, then the right-click ones.
enum { UI_MENUS = 6, UI_CTX_ENTRY = UI_MENUS, UI_CTX_HERE, UI_CTX_FOLDER, UI_CTX_TAB, UI_MENUS_ALL };
_Static_assert(sizeof UI_MENU / sizeof UI_MENU[0] == UI_MENUS_ALL, "UI_MENU: the bar's menus, then the right-click ones");

// ---- the Keyboard Shortcuts panel (F7.5, docs/plan.md "F7.5 decided") ----------------------------

// Its sections, by where the keys work, and its rows: an action, its key, a second key ("" none).
// A menu entry with a key has a row of the same name and key (test_f75.c checks).
typedef enum {
    KS_MOVE, KS_OPEN, KS_SELECT, KS_CHANGE, KS_TABS, KS_VIEW, KS_SIDE, KS_CRUMBS, KS_FIND, KS_SEARCH, KS_EDIT, KS_JOBS, KS_OW, KS_BM,
    KS_SECTIONS
} UiKeySection;

static const char *const UI_KEY_SECTION[KS_SECTIONS] = {
    "Moving", "Opening", "Selecting", "Changing files", "Tabs and windows", "View", "The sidebar", "The path buttons (Ctrl+K)",
    "The find bar", "Search below the folder", "Text fields (rename, location, find, search, filters)", "The job rows (Ctrl+J)", "Open With", "Edit Bookmarks",
};

static const struct { UiKeySection sec; const char *action, *key, *key2; } UI_KEYS[] = {
    { KS_MOVE, "Up, down", "Up, Down", "k, j" },
    { KS_MOVE, "A page up, down", "PgUp, PgDn", "" },
    { KS_MOVE, "The first, the last", "Home, End", "g, G" },
    { KS_MOVE, "Back", "Alt+Left", "Mouse back" },
    { KS_MOVE, "Forward", "Alt+Right", "Mouse forward" },
    { KS_MOVE, "Open Parent", "Alt+Up", "Backspace" },
    { KS_MOVE, "Home", "Alt+Home", "~" },
    { KS_MOVE, "Trash", "Alt+End", "" },
    { KS_MOVE, "Location\xE2\x80\xA6", "Ctrl+L", "Ctrl+Right" },
    { KS_OPEN, "Open", "Enter", "" },
    { KS_OPEN, "Open With Other Application\xE2\x80\xA6", "Shift+Enter", "" },
    { KS_OPEN, "Terminal Here", "Alt+Enter", "t" },
    { KS_OPEN, "Properties\xE2\x80\xA6", "Ctrl+I", "" },
    { KS_SELECT, "Select the entry, and on", "Space", "" },
    { KS_SELECT, "Select while moving", "Shift+arrows", "H, J, K, L" },
    { KS_SELECT, "Select All", "Ctrl+A", "" },
    { KS_SELECT, "Select Items Matching\xE2\x80\xA6", "Ctrl+Shift+A", "" },
    { KS_SELECT, "Invert Selection", "Ctrl+Shift+I", "" },
    { KS_SELECT, "Clear the selection", "Esc", "" },
    { KS_CHANGE, "Cut", "Ctrl+X", "" },
    { KS_CHANGE, "Copy", "Ctrl+C", "" },
    { KS_CHANGE, "Paste", "Ctrl+V", "" },
    { KS_CHANGE, "Undo", "Ctrl+Z", "" },
    { KS_CHANGE, "Make Link", "Ctrl+M", "" },
    { KS_CHANGE, "Rename\xE2\x80\xA6", "F2", "" },
    { KS_CHANGE, "Create New Folder", "Ctrl+Shift+N", "" },
    { KS_CHANGE, "Move to Trash", "Delete", "" },
    { KS_CHANGE, "Delete\xE2\x80\xA6", "Shift+Delete", "" },
    { KS_CHANGE, "Restore", "r", "" },
    { KS_CHANGE, "Add Bookmark", "Ctrl+D", "" },
    { KS_CHANGE, "Edit Bookmarks\xE2\x80\xA6", "Ctrl+B", "" },
    { KS_CHANGE, "A clash: skip, replace, keep both", "s, r, k", "S, R, K the rest" },
    { KS_TABS, "New Tab", "Ctrl+T", "" },
    { KS_TABS, "New Window", "Ctrl+N", "" },
    { KS_TABS, "Close", "Ctrl+W", "" },
    { KS_TABS, "The next tab", "Ctrl+Tab", "Ctrl+PgDn" },
    { KS_TABS, "The previous tab", "Ctrl+Shift+Tab", "Ctrl+PgUp" },
    { KS_TABS, "Move Tab Left", "Ctrl+Shift+PgUp", "" },
    { KS_TABS, "Move Tab Right", "Ctrl+Shift+PgDn", "" },
    { KS_TABS, "A tab by its place", "Alt+1 " "\xE2\x80\xA6" " Alt+9", "" },
    { KS_TABS, "Open in New Tab", "Middle click", "" },
    { KS_TABS, "Close All Windows", "Ctrl+Q", "" },
    { KS_VIEW, "The menus", "F10", "Alt+F, E, V, G, B, H" },
    { KS_VIEW, "The right-click menu", "Shift+F10", "Menu" },
    { KS_VIEW, "Keyboard Shortcuts", "F1", "Ctrl+F1" },
    { KS_VIEW, "Reload", "Ctrl+R", "" },
    { KS_VIEW, "Show Hidden Files", "Ctrl+H", "." },
    { KS_VIEW, "Show Permissions", "Ctrl+P", "" },
    { KS_VIEW, "Show Group", "Ctrl+G", "" },
    { KS_VIEW, "Sort by the next column", "s", "a column's header" },
    { KS_VIEW, "Reverse Order", "S", "" },
    { KS_VIEW, "List", "Ctrl+1", "" },
    { KS_VIEW, "Icons", "Ctrl+2", "" },
    { KS_VIEW, "Zoom In", "Ctrl+=", "Ctrl+wheel" },
    { KS_VIEW, "Zoom Out", "Ctrl+-", "" },
    { KS_VIEW, "Normal Size", "Ctrl+0", "" },
    { KS_SIDE, "To the sidebar and back", "Tab", "Ctrl+Left" },
    { KS_SIDE, "The sidebar's next mode", "F9", "Ctrl+S" },
    { KS_SIDE, "Go there", "Enter", "" },
    { KS_SIDE, "In the tree: close, open", "Left, Right", "h, l" },
    { KS_CRUMBS, "To the path buttons", "Ctrl+K", "Ctrl+Up" },
    { KS_CRUMBS, "The next button", "Left, Right", "h, l" },
    { KS_CRUMBS, "Go there", "Enter", "" },
    { KS_CRUMBS, "Open it in a tab behind", "Shift+Enter", "" },
    { KS_CRUMBS, "Back to the files", "Esc", "" },
    { KS_FIND, "Find in Folder\xE2\x80\xA6", "Ctrl+F", "/" },
    { KS_FIND, "The previous, next match", "Up, Down", "the chevrons" },
    { KS_FIND, "Keep it, the keys to the files", "Enter", "" },
    { KS_FIND, "Close it", "Esc", "" },
    { KS_FIND, "Find it again, back", "n, N", "" },
    { KS_FIND, "Match case", "Alt+C", "" },
    { KS_FIND, "Whole words", "Alt+W", "" },
    { KS_FIND, "A regular expression", "Alt+R", "" },
    { KS_SEARCH, "Search\xE2\x80\xA6", "Ctrl+Shift+F", "the magnifier" },
    { KS_SEARCH, "Open Containing Folder", "Ctrl+Enter", "" },
    { KS_SEARCH, "The keys to the results", "Enter", "Esc, Down" },
    { KS_SEARCH, "Back to the search field", "Ctrl+Shift+F", "a click in it" },
    { KS_SEARCH, "A wildcard over the whole name", "*, ?", "*.ts" },
    { KS_SEARCH, "Match case, whole words, a regular expression", "Alt+C, W, R", "shared with find" },
    { KS_EDIT, "Select", "Shift+Left, Right", "Shift+Home, End" },
    { KS_EDIT, "A word left, right", "Ctrl+Left, Right", "" },
    { KS_EDIT, "Select a word", "Ctrl+Shift+Left, Right", "" },
    { KS_EDIT, "Delete a word", "Ctrl+Backspace", "Ctrl+Delete" },
    { KS_EDIT, "All of it", "Ctrl+A", "" },
    { KS_EDIT, "Copy, cut, paste text", "Ctrl+C, X, V", "" },
    { KS_EDIT, "Done", "Enter", "" },
    { KS_EDIT, "Leave it as it was", "Esc", "" },
    { KS_JOBS, "To the job rows", "Ctrl+J", "Ctrl+Down" },
    { KS_JOBS, "The next row", "Up, Down", "k, j" },
    { KS_JOBS, "Cancel that job", "Enter", "its \xC3\x97" },
    { KS_JOBS, "Back to the files", "Esc", "Ctrl+J" },
    { KS_OW, "The field, the list", "Tab", "Down" },
    { KS_OW, "Always open with this app", "Alt+D", "" },
    { KS_OW, "Open", "Enter", "a double click" },
    { KS_BM, "Remove", "Delete", "" },
    { KS_BM, "Rename", "F2", "" },
    { KS_BM, "Move it up, down", "Alt+Up, Alt+Down", "" },
    { KS_BM, "Go there", "Enter", "a double click" },
};
enum { UI_KEYS_N = sizeof UI_KEYS / sizeof UI_KEYS[0], UI_KEYS_LINES = UI_KEYS_N + KS_SECTIONS };


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
    bool   find_select;      // Select Items Matching (F7.4): Enter selects every match, and closes it
    bool   find_miss;        // its text matches nothing
    char   find[UI_FIND_MAX];
    int    find_len, find_caret, find_anchor;   // (bytes; the selection between them, as the edit line's)
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
    // The search view (F9): gui/ says when it is shown. Its field's text (find's toggles apply), and
    // whether the field has the keys; search_edited when the text or a toggle changed (gui/ searches
    // again 150 ms after the last change, and clears it).
    bool   in_search;
    bool   search_keys;
    bool   search_edited;
    char   search[UI_FIND_MAX];
    int    search_len, search_caret, search_anchor;   // (bytes; the selection between them, as the edit line's)
    // The menu bar (F7.1): a menu open, and its entry marked (-1: none, as a menu the pointer
    // opened). gui/ says whether the folder shown takes new files.
    bool   menu_on;
    int    menu;
    int    menu_at;
    bool   can_write;
    // What only gui/ knows, for the Edit, View and Go entries it greys or marks (F7.4), set each
    // frame: an undo to make, history back and forward, a folder above, files to paste, hidden
    // files shown. And an entry picked whose key is still to run (ui_handle runs it next).
    bool   can_undo, can_back, can_forward, can_up, can_paste, shows_hidden;
    bool   perms_cols;   // the list's Owner and Permissions columns shown (gui/ reads it from the state)
    bool   group_col;    // and Group between them (View → Show Group, 2026-10-27; only with perms_cols)
    // The menu bar hidden (View → Show Menu unchecked, 2026-10-27; gui/ reads it from the state), and
    // the panel up that says how its menus still open.
    bool   bar_hidden, bar_told;
    bool   colors;   // the list in eza's colours (View → Show Colors; gui/ reads it from the state)
    UiEvent menu_key;
    // A right-click menu (F8): its entry is one folder (Open in New Tab), and a key opened it (gui/
    // places it under the cursor, not at the pointer).
    bool   ctx_dir, ctx_key, ctx_one;
    // From gui/ each frame (menu_facts): the cursor's entry is a link that leads somewhere, and a
    // folder that is a bookmark already (F8).
    bool   sel_link, sel_bm;
    // A folder elsewhere's menu (F8): the sidebar row (absolute) or path button it is on (the other
    // -1); and from gui/ as it opens (ctx_facts): it is the trash, a place (no Cut or Copy: a bookmark
    // isn't the folder to move), its bookmark's line in the file and how many lines (-1: not a
    // bookmark's row), a bookmark already, a drive with an eject button, a trash with something in it.
    int    ctx_side, ctx_crumb;
    bool   ctx_trash, ctx_place, ctx_bmed, ctx_eject, ctx_full;
    bool   ctx_w;   // the folder (either menu's, as it opens) takes new files: Paste Into Folder
    int    ctx_bmk, ctx_bms;
    // The tabs, from gui/ each frame (menu_facts): the current one's place, how many (F8's tab menu).
    int    tab_at, tab_n;
    // The Bookmarks menu (F7.5), from gui/ too: how many local bookmarks it lists (at most
    // UI_MENU_BOOKMARKS), the one that is the folder shown (-1 none), and whether that folder can be
    // bookmarked (not one already, not the trash view, the file not full).
    int    bm_n, bm_here;
    bool   can_bookmark;
    int    bm_total;         // the file's lines (Edit Bookmarks lists them all)
    int    bm_more, bm_more_at;   // the local bookmarks the menu doesn't list (0: none), the first one's line
    // Edit Bookmarks (F7.5): up; its list (the file's lines); its rename field open, the text and
    // the caret (bytes).
    bool   bmp;
    UiList bmp_list;
    bool   bmp_naming;
    char   bmp_text[256];
    int    bmp_len, bmp_caret, bmp_anchor;
    // Keyboard Shortcuts (F7.5): up; its filter's text; the lines it shows (a row of UI_KEYS, or
    // -1 - a section for its heading), scrolled by keys_list.top.
    bool   keys;
    char   keys_text[64];
    int    keys_len, keys_caret, keys_anchor;
    short  keys_line[UI_KEYS_LINES];
    UiList keys_list;
    UiMatcher keys_m;
    // About (Michael, 2026-10-27): up. gui/ draws it; Esc, Enter or Close close it.
    bool   about;
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
    int    ow_len, ow_caret, ow_anchor;
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
                      .zoom_to = -1, .menu_title = -1, .menu_item = -1, .ctx_row = -1, .ctx_side = -1, .ctx_crumb = -1, .ctx_tab = -1, .prop_box = -1, .ow_row = -1, .ow_drow = -1, .bmp_row = -1, .bmp_drow = -1 };
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
           in->zoom_wheel != 0 || in->zoom_to >= 0 || in->menu_title >= 0 || in->menu_item >= 0 || in->menu_away || in->ctx_row >= 0 || in->ctx_here || in->ctx_side >= 0 || in->ctx_crumb >= 0 || in->ctx_tab >= 0 || in->prop_box >= 0 ||
           in->ow_row >= 0 || in->ow_drow >= 0 || in->ow_field || in->toggle > 0 || in->find_x || in->find_field || in->find_step || in->search_field ||
           in->bmp_row >= 0 || in->bmp_drow >= 0 || in->bmp_btn || in->ow_wheel != 0;   // (a panel's wheel: Open With, Keyboard Shortcuts)
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

// Whether a search's text (F9) is a wildcard: a * or ? in it, the regular expression toggle off.
static bool ui_wild(const UiMatcher *m)
{
    return !m->o.regex && (memchr(m->text, '*', (size_t)m->len) || memchr(m->text, '?', (size_t)m->len));
}

// Whether a search (F9) matches name: a wildcard over the whole name, as a shell's (`*.ts`, case
// as the toggle says; whole words don't apply; a dot-file as any other, Show Hidden Files decides);
// any other text as find matches it.
static bool ui_search_match(const UiMatcher *m, Str name)
{
    if (!ui_wild(m)) return ui_match(m, name);
    char buf[NAME_MAX + 1];
    if (name.len > NAME_MAX) return false;
    memcpy(buf, name.p, (size_t)name.len), buf[name.len] = 0;
    locale_t was = ui_utf8_on();
    bool hit = fnmatch(m->text, buf, (m->o.cased ? 0 : FNM_CASEFOLD)) == 0;
    ui_utf8_off(was);
    return hit;
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

static void ui_find_close(Ui *u)
{
    u->finding = false, u->find_keys = false, u->find_miss = false, u->find_select = false;
    if (u->find_len) memcpy(u->last, u->find, (size_t)u->find_len), u->last_len = u->find_len;
}

// The keys leave the find bar (Enter, a command, the menu, the edit line); it stays open, its text
// kept for n and N. The select mode's bar closes: it is there for one Enter.
static void ui_find_leave(Ui *u)
{
    if (u->find_select) {
        if (u->finding) ui_find_close(u);
        return;
    }
    u->find_keys = false;
    if (u->find_len) memcpy(u->last, u->find, (size_t)u->find_len), u->last_len = u->find_len;
}

static void ui_mark(Ui *u, isize i, bool on);
static void ui_mark_clear(Ui *u);

// Select Items Matching's Enter (F7.4, as Nemo's): the selection becomes every name the text
// matches, and the bar closes; with no text it only closes. A regular expression that doesn't
// compile matches nothing: the selection stays and so does the bar, its reason in red (reviewer-2).
static void ui_find_select(Ui *u, UiNames names)
{
    if (u->find_m.bad && u->find_len > 0) return;
    if (u->find_len > 0) {
        ui_mark_clear(u);
        for (isize i = 0; i < u->list.n; i++)
            if (ui_match(&u->find_m, names.at(names.ctx, i))) ui_mark(u, i, true);
    }
    ui_find_close(u);
}

static void ui_find_open(Ui *u)
{
    u->finding = true, u->find_keys = true, u->find_miss = false, u->find_select = false, u->find_len = u->find_caret = u->find_anchor = 0, u->find_from = u->list.sel, u->side_focus = false;
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

// A line of text being edited (F5.2): the rename and location lines' (u->edit) and, since F9, the
// search field's. Its bytes and at most how many, the caret and the selection's other end (bytes),
// and whether a '/' may go in (the location line's paths).
typedef struct {
    char *b;
    int  *len, *caret, *anchor;
    int   max;
    bool  slash;
} UiLine;

static UiLine ui_edit_line(Ui *u)
{
    return (UiLine){ u->edit, &u->edit_len, &u->caret, &u->anchor, ui_edit_max(u), u->edit_path };
}

static UiLine ui_ow_line(Ui *u)
{
    return (UiLine){ u->ow_text, &u->ow_len, &u->ow_caret, &u->ow_anchor, UI_FIND_MAX, true };
}

static UiLine ui_bmp_line(Ui *u)
{
    return (UiLine){ u->bmp_text, &u->bmp_len, &u->bmp_caret, &u->bmp_anchor, (int)sizeof u->bmp_text - 1, true };
}

static UiLine ui_keys_line(Ui *u)
{
    return (UiLine){ u->keys_text, &u->keys_len, &u->keys_caret, &u->keys_anchor, (int)sizeof u->keys_text - 1, true };
}

static UiLine ui_find_line(Ui *u)
{
    return (UiLine){ u->find, &u->find_len, &u->find_caret, &u->find_anchor, UI_FIND_MAX, false };
}

static UiLine ui_search_line(Ui *u)
{
    return (UiLine){ u->search, &u->search_len, &u->search_caret, &u->search_anchor, UI_FIND_MAX, false };
}

// The selection, [*lo, *hi): empty when the anchor is at the caret.
static void ui_line_sel(UiLine l, int *lo, int *hi)
{
    *lo = *l.anchor < *l.caret ? *l.anchor : *l.caret;
    *hi = *l.anchor < *l.caret ? *l.caret : *l.anchor;
}

static void ui_edit_sel(const Ui *u, int *lo, int *hi)
{
    ui_line_sel(ui_edit_line((Ui *)u), lo, hi);
}

static int ui_line_back(UiLine l, int at)
{
    if (at <= 0) return 0;
    at--;
    while (at > 0 && ((unsigned char)l.b[at] & 0xC0) == 0x80) at--;
    return at;
}

static int ui_line_fwd(UiLine l, int at)
{
    if (at >= *l.len) return *l.len;
    at++;
    while (at < *l.len && ((unsigned char)l.b[at] & 0xC0) == 0x80) at++;
    return at;
}

// Bytes [from, to) removed, the caret where they were.
static void ui_line_cut(UiLine l, int from, int to)
{
    memmove(l.b + from, l.b + to, (size_t)(*l.len - to));
    *l.len -= to - from;
    *l.caret = *l.anchor = from;
}

// Bytes b[0..n) in place of the selection, the caret after them; false (nothing changed) when they
// don't fit.
static bool ui_line_put(UiLine l, const char *b, int n)
{
    int lo, hi;
    ui_line_sel(l, &lo, &hi);
    if (*l.len - (hi - lo) + n > l.max) return false;
    ui_line_cut(l, lo, hi);
    memmove(l.b + *l.caret + n, l.b + *l.caret, (size_t)(*l.len - *l.caret));
    memcpy(l.b + *l.caret, b, (size_t)n);
    *l.len += n, *l.caret += n, *l.anchor = *l.caret;
    return true;
}

// The selection onto u->copy for gui/'s clipboard; nothing selected: nothing.
static bool ui_line_copy(Ui *u, UiLine l)
{
    int lo, hi;
    ui_line_sel(l, &lo, &hi);
    if (lo == hi) return false;
    memcpy(u->copy, l.b + lo, (size_t)(hi - lo));
    u->copy_len = hi - lo, u->copied = true;
    return true;
}

// Ctrl+V: the clipboard's first line over the selection; without the characters a name can't
// hold ('/') unless the line takes them, and never a control character. What doesn't fit is left
// off, whole characters only.
static void ui_line_paste(UiLine l, Str text)
{
    char b[PATH_MAX];
    int n = 0, lo, hi;
    ui_line_sel(l, &lo, &hi);
    int room = l.max - (*l.len - (hi - lo));
    for (isize i = 0; i < text.len && text.p[i] != '\n' && text.p[i] != '\r';) {
        unsigned char c = (unsigned char)text.p[i];
        isize e = i + 1;
        while (e < text.len && ((unsigned char)text.p[e] & 0xC0) == 0x80) e++;
        bool drop = c < 0x20 || c == 0x7F || (c == '/' && !l.slash);
        if (n + (e - i) > room) break;   // a dropped one is a byte: none fits after it either
        if (!drop) memcpy(b + n, text.p + i, (size_t)(e - i)), n += (int)(e - i);
        i = e;
    }
    if (n > 0) ui_line_put(l, b, n);
}

// A word's edges for Ctrl and the arrows (2026-10-28): from at, back over what isn't a word (spaces,
// dots, dashes) and then over the word to its start (dir -1); or on over what isn't one and then the
// word to its end (1). A word is ui_word_byte's: letters, digits, '_', anything past ASCII.
static int ui_line_word(UiLine l, int at, int dir)
{
    if (dir < 0) {
        while (at > 0 && !ui_word_byte((unsigned char)l.b[at - 1])) at--;
        while (at > 0 && ui_word_byte((unsigned char)l.b[at - 1])) at--;
        return at;
    }
    while (at < *l.len && !ui_word_byte((unsigned char)l.b[at])) at++;
    while (at < *l.len && ui_word_byte((unsigned char)l.b[at])) at++;
    return at;
}

static bool ui_line_do(Ui *u, UiLine l, UiEvent e, Str paste)
{
    if (e.key == UK_NONE) {
        char b[4];
        int n = ui_utf8(e.ch, b);
        if (n > 0 && (e.ch != '/' || l.slash)) ui_line_put(l, b, n);
        return true;
    }
    int lo, hi;
    ui_line_sel(l, &lo, &hi);
    switch (e.key) {
    // A move ends the selection; Left and Right from one land at its ends.
    case UK_LEFT: *l.caret = *l.anchor = lo < hi ? lo : ui_line_back(l, *l.caret); return true;
    case UK_RIGHT: *l.caret = *l.anchor = lo < hi ? hi : ui_line_fwd(l, *l.caret); return true;
    case UK_WORD_LEFT: *l.caret = *l.anchor = ui_line_word(l, *l.caret, -1); return true;
    case UK_WORD_RIGHT: *l.caret = *l.anchor = ui_line_word(l, *l.caret, 1); return true;
    case UK_HOME: *l.caret = *l.anchor = 0; return true;
    case UK_END: *l.caret = *l.anchor = *l.len; return true;
    // With Shift the caret moves and the anchor stays.
    case UK_SEL_LEFT: *l.caret = ui_line_back(l, *l.caret); return true;
    case UK_SEL_RIGHT: *l.caret = ui_line_fwd(l, *l.caret); return true;
    case UK_SEL_WORD_LEFT: *l.caret = ui_line_word(l, *l.caret, -1); return true;
    case UK_SEL_WORD_RIGHT: *l.caret = ui_line_word(l, *l.caret, 1); return true;
    case UK_SEL_HOME: *l.caret = 0; return true;
    case UK_SEL_END: *l.caret = *l.len; return true;
    case UK_ALL: *l.anchor = 0, *l.caret = *l.len; return true;
    // Backspace and Delete take the selection, else a character (Ctrl: a word) that way.
    case UK_BACKSPACE: ui_line_cut(l, lo < hi ? lo : ui_line_back(l, *l.caret), lo < hi ? hi : *l.caret); return true;
    case UK_DELETE: ui_line_cut(l, lo < hi ? lo : *l.caret, lo < hi ? hi : ui_line_fwd(l, *l.caret)); return true;
    case UK_WORD_BACKSPACE: ui_line_cut(l, lo < hi ? lo : ui_line_word(l, *l.caret, -1), lo < hi ? hi : *l.caret); return true;
    case UK_WORD_DELETE: ui_line_cut(l, lo < hi ? lo : *l.caret, lo < hi ? hi : ui_line_word(l, *l.caret, 1)); return true;
    case UK_COPY: ui_line_copy(u, l); return true;
    case UK_CUT:
        if (ui_line_copy(u, l)) ui_line_cut(l, lo, hi);
        return true;
    case UK_PASTE: ui_line_paste(l, paste); return true;
    default: return false;
    }
}

// What a key did to a line: nothing (not its key), moved the caret or the selection, or changed the
// text (its owner filters or searches again on that).
typedef enum { UI_LINE_NOT, UI_LINE_MOVED, UI_LINE_CHANGED } UiLineHit;

// The keys that edit a line: text, the moves (with Shift the selection grows; with Ctrl a word at a
// time), Ctrl+A, Backspace, Delete, Ctrl+C, Ctrl+X, Ctrl+V. The caret and the anchor are put back
// inside the text first: its owner may have set a shorter one (a search's address, a bookmark's
// label).
static UiLineHit ui_line_key(Ui *u, UiLine l, UiEvent e, Str paste)
{
    *l.caret = (int)ui_clamp(*l.caret, 0, *l.len), *l.anchor = (int)ui_clamp(*l.anchor, 0, *l.len);
    char was[PATH_MAX];
    int was_len = *l.len;
    memcpy(was, l.b, (size_t)was_len);
    if (!ui_line_do(u, l, e, paste)) return UI_LINE_NOT;
    return *l.len != was_len || memcmp(was, l.b, (size_t)was_len) != 0 ? UI_LINE_CHANGED : UI_LINE_MOVED;
}

// The line that has the keys now, in ui_handle's order (a panel's field before the rest): into *out
// when given; false when none has. gui/ reads the clipboard for a Ctrl+V only then.
static bool ui_line_active(Ui *u, UiLine *out)
{
    UiLine l;
    if (u->ow && !u->ow_in_list) l = ui_ow_line(u);
    else if (u->bmp && u->bmp_naming) l = ui_bmp_line(u);
    else if (u->keys) l = ui_keys_line(u);
    else if (u->editing) l = ui_edit_line(u);
    else if (u->in_search && u->search_keys) l = ui_search_line(u);
    else if (u->finding && u->find_keys) l = ui_find_line(u);
    else return false;
    if (out) *out = l;
    return true;
}

// A key or character in the edit line: an action for gui/ (UA_RENAME or UA_GO on Enter) or UA_NONE.
static UiAction ui_edit_event(Ui *u, UiEvent e, Str paste)
{
    if (ui_line_key(u, ui_edit_line(u), e, paste)) return (UiAction){ UA_NONE, 0, false };
    switch (e.key) {
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

// Whether entry it is in its menu now: Move to Trash outside the trash view, Restore in it (F7.4); a
// bookmark's place while there is one for it (F7.5).
static bool ui_folder_shown(const Ui *u, UiMenuItem it);

static bool ui_menu_shown(const Ui *u, UiMenuItem it)
{
    if (u->menu == UI_CTX_FOLDER) return ui_folder_shown(u, it);
    if (it >= UM_BM_0 && it <= UM_BM_7) return (int)(it - UM_BM_0) < u->bm_n;
    if (it == UM_BM_MORE) return u->bm_more > 0;
    // A right-click menu leaves out what doesn't fit its target (F8): a tab for a folder only, a
    // rename for one entry, the new entries outside the trash view (Empty Trash in it).
    if (u->menu >= UI_MENUS && it == UM_OPEN_TAB) return u->ctx_dir && !u->in_trash;
    if (u->menu >= UI_MENUS && it == UM_RENAME) return u->marked <= 1;
    if (it == UM_OPEN_WINDOW || it == UM_PASTE_INTO || it == UM_TERMINAL_IN || it == UM_SEARCH_IN) return u->ctx_dir && !u->in_trash;
    if (it == UM_CONTAINING) return u->in_search;
    if (it == UM_BOOKMARK_IT) return u->ctx_dir && !u->in_trash && !u->sel_bm;
    if (it == UM_FOLLOW) return u->ctx_one && u->sel_link && !u->in_trash;
    if (u->menu >= UI_MENUS && (it == UM_NEW_FOLDER || it == UM_NEW_FILE)) return !u->in_trash;
    if (it == UM_EMPTY_TRASH) return u->in_trash;
    return it == UM_TRASH ? !u->in_trash : it == UM_RESTORE ? u->in_trash : true;
}

// The folder menu's (F8): what fits that folder. Trash is gone to, not into; a place has no Cut or
// Copy; a bookmark's row has Edit Bookmarks' own.
static bool ui_folder_shown(const Ui *u, UiMenuItem it)
{
    switch (it) {
    case UM_SEP: case UM_OPEN_IT: case UM_OPEN_TAB: return true;
    case UM_OPEN_WINDOW: case UM_TERMINAL_IN: case UM_SEARCH_IN: case UM_PASTE_INTO: case UM_PROPS_IT: return !u->ctx_trash;
    case UM_CUT_IT: case UM_COPY_IT: return !u->ctx_trash && !u->ctx_place;
    case UM_BOOKMARK_IT: return !u->ctx_trash && !u->ctx_bmed;
    case UM_BM_RENAME_IT: case UM_BM_UP_IT: case UM_BM_DOWN_IT: case UM_BM_REMOVE_IT: case UM_BM_EDIT_IT: return u->ctx_bmk >= 0;
    case UM_EJECT_IT: return u->ctx_eject;
    case UM_EMPTY_IT: return u->ctx_trash;
    default: return false;
    }
}

// Whether menu m's line at place i is drawn: not one with nothing shown after it (the Bookmarks
// menu with no bookmarks).
static bool ui_menu_sep_shown(const Ui *u, int m, int i)
{
    for (int k = i + 1; k < UI_MENU[m].n && UI_MENU[m].items[k] != UM_SEP; k++)
        if (ui_menu_shown(u, UI_MENU[m].items[k])) return true;
    return false;
}

// Whether entry it can be picked now. One that runs a key is greyed where that key would do
// nothing, or only say it can't (Copy in the trash view).
static bool ui_menu_enabled(const Ui *u, UiMenuItem it)
{
    bool files = u->list.n > 0, here = files && !u->side_focus;
    if (!ui_menu_shown(u, it)) return false;
    if (u->menu == UI_CTX_FOLDER) {   // (F8) the rest are always there to pick
        if (it == UM_PASTE_INTO) return u->can_paste && u->ctx_w;
        if (it == UM_BM_UP_IT) return u->ctx_bmk > 0;
        if (it == UM_BM_DOWN_IT) return u->ctx_bmk < u->ctx_bms - 1;
        if (it == UM_EMPTY_IT) return u->ctx_full;
        return true;
    }
    switch (it) {
    case UM_NEW_TAB: case UM_CLOSE_ALL: case UM_CLOSE: case UM_PROPERTIES: return true;
    case UM_NEW_WINDOW: return !u->in_trash;   // ofx starts on a folder, not on the trash view (from a search: the folder searched)
    case UM_NEW_FOLDER: case UM_NEW_FILE: return !u->in_trash && u->can_write;
    case UM_OPEN: return files;
    case UM_OPEN_WITH: return files && !u->in_trash;
    case UM_UNDO: return u->can_undo;
    case UM_CUT: case UM_COPY: return here && !u->in_trash;
    case UM_PASTE: return u->can_paste && u->can_write;
    case UM_SELECT_ALL: case UM_SELECT_MATCH: case UM_INVERT: return files;
    case UM_RENAME: case UM_DUPLICATE: case UM_MAKE_LINK: return here && !u->in_trash && (u->can_write || u->in_search);   // (a result's own folder: F9)
    case UM_TRASH: case UM_DELETE: return here;
    case UM_RESTORE: return here;
    case UM_RELOAD: case UM_SIDE_PLACES: case UM_SIDE_TREE: case UM_SIDE_NONE: case UM_HIDDEN: case UM_SHOW_MENU: return true;
    // The list's own (Michael, 2026-10-27): the icon view has no columns or colours; a group without
    // the permissions says little.
    case UM_PERMS_COLS: case UM_COLORS: return !u->grid;
    case UM_GROUP_COL: return !u->grid && u->perms_cols;
    case UM_SORT_NAME: case UM_SORT_SIZE: case UM_SORT_MTIME: case UM_SORT_REVERSE: return true;
    case UM_ZOOM_IN: return u->grid && ui_zoom_level(ui_zoom_px(u)) < UI_ZOOMS - 1;
    case UM_ZOOM_OUT: return u->grid && ui_zoom_level(ui_zoom_px(u)) > 0;
    case UM_ZOOM_RESET: return u->grid && ui_zoom_px(u) != UI_ZOOM_DEFAULT;
    case UM_VIEW_LIST: case UM_VIEW_ICONS: return true;
    case UM_PARENT: return u->can_up;
    case UM_BACK: return u->can_back;
    case UM_FORWARD: return u->can_forward;
    case UM_HOME: case UM_TRASH_VIEW: case UM_FIND: case UM_LOCATION: return true;
    case UM_SEARCH: return !u->in_trash;
    case UM_TERMINAL: return !u->in_trash;
    case UM_ADD_BOOKMARK: return u->can_bookmark && !u->in_trash && !u->in_search;
    case UM_BM_0: case UM_BM_1: case UM_BM_2: case UM_BM_3: case UM_BM_4: case UM_BM_5: case UM_BM_6: case UM_BM_7: return true;
    case UM_EDIT_BOOKMARKS: case UM_BM_MORE: case UM_SHORTCUTS: case UM_ABOUT: return true;
    case UM_TAB_LEFT: return u->tab_at > 0;
    case UM_TAB_RIGHT: return u->tab_at < u->tab_n - 1;
    case UM_TAB_OTHERS: return u->tab_n > 1;
    case UM_OPEN_TAB: case UM_EMPTY_TRASH: case UM_OPEN_WINDOW: case UM_TERMINAL_IN: case UM_SEARCH_IN: case UM_FOLLOW: case UM_BOOKMARK_IT: return files;
    case UM_CONTAINING: return true;   // (on a result's menu only)
    case UM_PASTE_INTO: return files && u->can_paste && u->ctx_w;
    case UM_HERE_PROPS: return !u->in_search;   // (a search is no folder)
    default: return false;
    }
}

// Entry it's mark: -1 none, else whether it is on (a check checked, one of a group the current).
static int ui_menu_mark(const Ui *u, UiMenuItem it)
{
    switch (it) {
    case UM_SIDE_PLACES: return u->side_mode == SIDE_PLACES;
    case UM_SIDE_TREE: return u->side_mode == SIDE_TREE;
    case UM_SIDE_NONE: return u->side_mode == SIDE_HIDDEN;
    case UM_HIDDEN: return u->shows_hidden;
    case UM_PERMS_COLS: return u->perms_cols;
    case UM_GROUP_COL: return u->group_col;
    case UM_SHOW_MENU: return !u->bar_hidden;
    case UM_COLORS: return u->colors;
    case UM_SORT_NAME: return u->sort.key == SORT_NAME;
    case UM_SORT_SIZE: return u->sort.key == SORT_SIZE;
    case UM_SORT_MTIME: return u->sort.key == SORT_MTIME;
    case UM_SORT_REVERSE: return u->sort.reverse;
    case UM_VIEW_LIST: return !u->grid;
    case UM_VIEW_ICONS: return u->grid;
    case UM_BM_0: case UM_BM_1: case UM_BM_2: case UM_BM_3: case UM_BM_4: case UM_BM_5: case UM_BM_6: case UM_BM_7:
        return u->bm_here == (int)(it - UM_BM_0);
    default: return -1;
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

// A right-click menu m opened (F8): the files take the keys; keys: a key opened it.
static void ui_ctx_open(Ui *u, int m, UiNames names, bool keys)
{
    u->side_focus = u->crumb_focus = u->jobs_focus = false;
    u->ctx_one = u->list.n > 0 && (u->marked == 0 || (u->marked == 1 && ui_marked(u, u->list.sel)));
    u->ctx_dir = u->ctx_one && names.dir && names.dir(names.ctx, u->list.sel);
    u->ctx_key = keys, u->ctx_w = false;   // (gui/ says, as it opens)
    ui_menu_open(u, m, keys);
}

// The folder menu opened (F8) on sidebar row side or path button crumb (the other -1): what it is,
// gui/ says as it opens. The keys stay where they were.
static void ui_folder_open(Ui *u, isize side, int crumb, bool keys)
{
    u->ctx_side = (int)side, u->ctx_crumb = crumb, u->ctx_key = keys;
    u->ctx_trash = u->ctx_place = u->ctx_bmed = u->ctx_eject = u->ctx_full = u->ctx_w = false;
    u->ctx_bmk = -1, u->ctx_bms = 0;
    ui_menu_open(u, UI_CTX_FOLDER, keys);
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
    u->ow = true, u->ow_in_list = false, u->ow_fresh = true, u->ow_len = u->ow_caret = u->ow_anchor = 0, u->ow_def = false;
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
// row goes to the field, another character goes to the field and is typed there. The field edits as
// the rename line does (2026-10-28). Enter opens the
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
            u->ow_in_list = false, u->ow_caret = u->ow_anchor = u->ow_len;
            if (ui_line_key(u, ui_ow_line(u), e, in->paste) == UI_LINE_CHANGED) ui_ow_filter(u, in);
            break;
        case UK_BACKSPACE:
            u->ow_in_list = false, u->ow_caret = u->ow_anchor = u->ow_len;
            if (ui_line_key(u, ui_ow_line(u), e, in->paste) == UI_LINE_CHANGED) ui_ow_filter(u, in);
            break;
        default: break;
        }
        return (UiAction){ UA_NONE, 0, false };
    }
    UiLineHit h = ui_line_key(u, ui_ow_line(u), e, in->paste);
    if (h == UI_LINE_CHANGED) ui_ow_filter(u, in);
    else if (!h && e.key == UK_DOWN && l->n > 0) u->ow_in_list = true;
    return (UiAction){ UA_NONE, 0, false };
}

// ---- Keyboard Shortcuts (F7.5) -------------------------------------------------------------------

// The lines for the filter's text: each section's heading and its rows whose action or keys hold
// the text (case folded); a section with none left out.
static void ui_keys_filter(Ui *u)
{
    ui_matcher_set(&u->keys_m, (Str){ u->keys_text, u->keys_len }, (UiMatchOpts){ 0 });
    int n = 0;
    for (int s = 0; s < KS_SECTIONS; s++) {
        int head = n;
        u->keys_line[n++] = (short)(-1 - s);
        for (int r = 0; r < UI_KEYS_N; r++)
            if (UI_KEYS[r].sec == (UiKeySection)s && (!u->keys_len || ui_match(&u->keys_m, str_from_c(UI_KEYS[r].action)) ||
                                                      ui_match(&u->keys_m, str_from_c(UI_KEYS[r].key)) || ui_match(&u->keys_m, str_from_c(UI_KEYS[r].key2))))
                u->keys_line[n++] = (short)r;
        if (n == head + 1) n = head;   // nothing under it
    }
    ui_set(&u->keys_list, n, 0);   // (its cursor on the first line: the top there too)
}

static UiAction ui_keys_open(Ui *u)
{
    u->keys = true, u->keys_len = u->keys_caret = u->keys_anchor = 0;
    u->keys_list = (UiList){ .rows = u->keys_list.rows };
    ui_keys_filter(u);
    return (UiAction){ UA_NONE, 0, false };
}

static void ui_keys_close(Ui *u)
{
    u->keys = false;
    ui_matcher_set(&u->keys_m, STR_EMPTY, (UiMatchOpts){ 0 });
}

// A key while the panel is up; it takes every key. Up, Down, PgUp, PgDn, Home and End scroll; Esc,
// F1 and Enter close; the rest edit the filter as the rename line does (2026-10-28), each change
// filtering again.
static void ui_keys_event(Ui *u, UiEvent e, Str paste)
{
    UiList *l = &u->keys_list;
    int page = l->rows > 1 ? l->rows - 1 : 1;
    switch (e.key) {
    case UK_UP: case UK_DOWN: ui_scroll(l, e.key == UK_UP ? 1 : -1); break;   // (as the wheel's notches: up is +)
    case UK_PAGEUP: case UK_PAGEDOWN: ui_scroll(l, e.key == UK_PAGEUP ? page : -page); break;
    case UK_HOME: ui_scroll(l, (int)l->n); break;
    case UK_END: ui_scroll(l, (int)-l->n); break;
    case UK_ESC: case UK_SHORTCUTS: case UK_ENTER: ui_keys_close(u); break;
    default:
        if (ui_line_key(u, ui_keys_line(u), e, paste) == UI_LINE_CHANGED) ui_keys_filter(u);
        break;
    }
}

// ---- Edit Bookmarks (F7.5) ---------------------------------------------------------------------

// The panel opened on the file's line at (the first when out of range).
static UiAction ui_bmp_open(Ui *u, int at)
{
    u->bmp = true, u->bmp_naming = false;
    u->bmp_list = (UiList){ .rows = u->bmp_list.rows };
    ui_set(&u->bmp_list, u->bm_total, at >= 0 && at < u->bm_total ? at : 0);
    return (UiAction){ UA_BM_EDIT, 0, false };
}

// What one of the panel's buttons (or its key) asks for the line under the cursor: 1 Remove
// (Delete), 2 Rename (F2), 3 Move Up (Alt+Up), 4 Move Down (Alt+Down); nothing past the ends.
static UiAction ui_bmp_do(Ui *u, int what)
{
    UiList *l = &u->bmp_list;
    if (l->n == 0) return (UiAction){ UA_NONE, 0, false };
    switch (what) {
    case 1: return (UiAction){ UA_BM_REMOVE, l->sel, false };
    case 2: u->bmp_naming = true; return (UiAction){ UA_BM_NAME, l->sel, false };
    case 3: return l->sel > 0 ? (UiAction){ UA_BM_UP, l->sel, false } : (UiAction){ UA_NONE, 0, false };
    case 4: return l->sel < l->n - 1 ? (UiAction){ UA_BM_DOWN, l->sel, false } : (UiAction){ UA_NONE, 0, false };
    default: return (UiAction){ UA_NONE, 0, false };
    }
}

// A key while the panel is up; it takes every key, as Open With's. In the list: Up, Down, k, j,
// Home, End, PgUp and PgDn move; Delete removes the bookmark, F2 renames it, Alt+Up and Alt+Down
// move it; Enter goes there; Esc closes. In the rename field: it edits as the rename line does
// (2026-10-28); Enter renames (an empty name: the folder's), Esc leaves the name as it was.
static UiAction ui_bmp_event(Ui *u, UiEvent e, Str paste)
{
    UiList *l = &u->bmp_list;
    int page = l->rows > 1 ? l->rows - 1 : 1;
    if (u->bmp_naming) {
        if (ui_line_key(u, ui_bmp_line(u), e, paste)) return (UiAction){ UA_NONE, 0, false };
        switch (e.key) {
        case UK_ENTER: case UK_SHIFT_ENTER: u->bmp_naming = false; return (UiAction){ UA_BM_RENAME, l->sel, false };
        case UK_ESC: u->bmp_naming = false; break;
        default: break;
        }
        return (UiAction){ UA_NONE, 0, false };
    }
    if (e.key == UK_NONE && e.ch == 'j') e.key = UK_DOWN;
    else if (e.key == UK_NONE && e.ch == 'k') e.key = UK_UP;
    switch (e.key) {
    case UK_DOWN: ui_move(l, 1); break;
    case UK_UP: ui_move(l, -1); break;
    case UK_HOME: ui_move(l, -l->n); break;
    case UK_END: ui_move(l, l->n); break;
    case UK_PAGEUP: ui_move(l, -page); break;
    case UK_PAGEDOWN: ui_move(l, page); break;
    case UK_DELETE: return ui_bmp_do(u, 1);
    case UK_F2: return ui_bmp_do(u, 2);
    case UK_PARENT: return ui_bmp_do(u, 3);
    case UK_ALT_DOWN: return ui_bmp_do(u, 4);
    case UK_ENTER: case UK_SHIFT_ENTER:
        if (l->n == 0) break;
        u->bmp = false;
        return (UiAction){ UA_BM_GO, l->sel, false };
    case UK_ESC: u->bmp = false; break;
    default: break;
    }
    return (UiAction){ UA_NONE, 0, false };
}

// Entry it picked: the menu closes, and gui/ does what it says; one that stands for a key leaves
// it in menu_key, for ui_handle to run as that key.
static UiAction ui_menu_pick(Ui *u, UiMenuItem it)
{
    u->menu_on = false;
    // The folder menu (F8): gui/ does each for that folder; ui/ keeps its panels and questions.
    if (u->menu == UI_CTX_FOLDER) {
        switch (it) {
        case UM_BM_EDIT_IT: return ui_bmp_open(u, u->ctx_bmk);
        case UM_BM_RENAME_IT: return ui_bmp_open(u, u->ctx_bmk), u->bmp_naming = true, (UiAction){ UA_BM_NAME, u->ctx_bmk, false };
        case UM_BM_UP_IT: return (UiAction){ UA_BM_UP, u->ctx_bmk, false };
        case UM_BM_DOWN_IT: return (UiAction){ UA_BM_DOWN, u->ctx_bmk, false };
        case UM_BM_REMOVE_IT: return (UiAction){ UA_BM_REMOVE, u->ctx_bmk, false };
        case UM_EMPTY_IT: return u->empty_asking = true, (UiAction){ UA_NONE, 0, false };
        case UM_PROPS_IT: return u->props = true, u->prop_at = 0, (UiAction){ UA_PROPS, 2, false };   // (2: the folder menu's)
        default: return (UiAction){ UA_FOLDER_DO, it, false };
        }
    }
    if (UI_MENU_ITEM[it].ev.key != UK_NONE || UI_MENU_ITEM[it].ev.ch) {
        u->menu_key = UI_MENU_ITEM[it].ev;
        return (UiAction){ UA_NONE, 0, false };
    }
    switch (it) {
    case UM_SIDE_PLACES: return ui_side_mode(u, SIDE_PLACES);
    case UM_SIDE_TREE: return ui_side_mode(u, SIDE_TREE);
    case UM_SIDE_NONE: return ui_side_mode(u, SIDE_HIDDEN);
    case UM_SORT_NAME: case UM_SORT_SIZE: case UM_SORT_MTIME:
        u->sort.key = it == UM_SORT_NAME ? SORT_NAME : it == UM_SORT_SIZE ? SORT_SIZE : SORT_MTIME;
        return (UiAction){ UA_SORT, 0, false };
    case UM_DUPLICATE: return (UiAction){ UA_DUPLICATE, 0, false };
    case UM_COLORS: return u->colors = !u->colors, (UiAction){ UA_COLORS, 0, false };
    case UM_SHOW_MENU: return u->bar_hidden = !u->bar_hidden, u->bar_told = u->bar_hidden, (UiAction){ UA_MENUBAR, 0, false };
    case UM_BM_0: case UM_BM_1: case UM_BM_2: case UM_BM_3: case UM_BM_4: case UM_BM_5: case UM_BM_6: case UM_BM_7:
        return (UiAction){ UA_BOOKMARK, it - UM_BM_0, false };
    case UM_BM_MORE: return ui_bmp_open(u, u->bm_more_at);
    case UM_NEW_TAB: return (UiAction){ UA_TAB_NEW, 0, false };
    case UM_NEW_WINDOW: return (UiAction){ UA_NEW_WINDOW, 0, false };
    case UM_NEW_FOLDER: return (UiAction){ UA_NEWFOLDER, 0, false };
    case UM_NEW_FILE: return (UiAction){ UA_NEWFILE, 0, false };
    case UM_OPEN: return (UiAction){ UA_OPEN, u->list.sel, u->marked > 0 };
    case UM_CLOSE_ALL: return (UiAction){ UA_CLOSE_ALL, 0, true };   // gui/ asks first with jobs (close_others)
    case UM_CLOSE: return (UiAction){ UA_TAB_CLOSE, -1, false };
    case UM_PROPERTIES: return ui_props_open(u);
    case UM_OPEN_WITH: return ui_ow_open(u);
    case UM_ABOUT: return u->about = true, (UiAction){ UA_NONE, 0, false };
    case UM_OPEN_TAB: return (UiAction){ UA_TAB_BG_ROW, u->list.sel, false };
    case UM_TAB_OTHERS: return (UiAction){ UA_TAB_OTHERS, 0, false };
    case UM_EMPTY_TRASH: return u->empty_asking = true, (UiAction){ UA_NONE, 0, false };
    case UM_HERE_PROPS: return u->props = true, u->prop_at = 0, (UiAction){ UA_PROPS, 1, false };   // (index 1: the folder shown)
    case UM_OPEN_WINDOW: return (UiAction){ UA_NEW_WINDOW, u->list.sel, true };
    case UM_PASTE_INTO: return (UiAction){ UA_PASTE, u->list.sel, true };
    case UM_TERMINAL_IN: return (UiAction){ UA_TERMINAL, u->list.sel, true };
    case UM_BOOKMARK_IT: return (UiAction){ UA_ADD_BOOKMARK, u->list.sel, true };
    case UM_FOLLOW: return (UiAction){ UA_FOLLOW, u->list.sel, false };
    case UM_SEARCH_IN: return (UiAction){ UA_SEARCH, u->list.sel, true };   // (all: below the folder entry index)
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
        if (u->menu >= UI_MENUS) return true;   // a right-click menu has no neighbours (F8)
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
    if (title >= 0 && title < UI_MENUS && title != u->menu && u->menu < UI_MENUS) {   // (not from a right-click menu, F8)
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
// ---- the search view's field (F9) -----------------------------------------------------------

// The field's keys: it edits as the rename line does (a caret, Shift to select, Ctrl+A, Ctrl+C, X,
// V: Michael, 2026-10-28), plus find's toggles, each change marked; Enter, Esc and Down give the
// keys to the results; another command goes to them as well, and runs there (false; Ctrl+Shift+F
// gives them back).
static bool ui_search_event(Ui *u, UiEvent e, Str paste)
{
    if (ui_toggle_key(e)) {
        u->find_opts = ui_toggle(u->find_opts, ui_toggle_key(e)), u->search_edited = true;
        return true;
    }
    UiLine l = ui_search_line(u);
    int lo, hi;   // (Ctrl+C, Ctrl+X with no text selected: the cursor's result, as before the field edited)
    ui_line_sel(l, &lo, &hi);
    if ((e.key == UK_COPY || e.key == UK_CUT) && lo == hi) return u->search_keys = false, false;
    UiLineHit h = ui_line_key(u, l, e, paste);
    if (h == UI_LINE_CHANGED) u->search_edited = true;
    if (h) return true;
    switch (e.key) {
    case UK_ENTER: case UK_SHIFT_ENTER: case UK_ESC: case UK_DOWN: u->search_keys = false; return true;
    default: u->search_keys = false; return false;
    }
}

// Ctrl+Shift+F, Go → Search…, the magnifier (F9): in the search view the keys go back to its
// field; elsewhere gui/ opens a search below the folder shown in a new tab (the trash isn't one).
static UiAction ui_search_open(Ui *u)
{
    if (!u->in_search) return (UiAction){ u->in_trash ? UA_NOT_IN_TRASH : UA_SEARCH, 0, false };
    ui_edit_close(u);
    u->search_keys = true, u->find_keys = false, u->side_focus = false, u->crumb_focus = false, u->jobs_focus = false;
    return (UiAction){ UA_NONE, 0, false };
}

// The find bar's keys: its field edits as the rename line does (a caret, Shift to select, Ctrl+A,
// C, X, V: Michael, 2026-10-28; no '/', which can't be in a name), each change found again; the
// toggles; Up and Down the previous and next match; Enter keeps (or, selecting, selects), Esc
// closes. Another command goes to the list (false).
static bool ui_find_event(Ui *u, UiNames names, UiEvent e, const UiInput *in)
{
    if (ui_toggle_key(e)) {   // Alt+C, Alt+W, Alt+R: a toggle
        u->find_opts = ui_toggle(u->find_opts, ui_toggle_key(e));
        ui_find_update(u, names);
        return true;
    }
    // Ctrl+C and Ctrl+X with no text selected copy or cut the file found, as before the bar edited:
    // typing a name and copying it is what the bar is for.
    int lo, hi;
    ui_line_sel(ui_find_line(u), &lo, &hi);
    if ((e.key == UK_COPY || e.key == UK_CUT) && lo == hi) return ui_find_leave(u), false;
    UiLineHit h = ui_line_key(u, ui_find_line(u), e, in->paste);
    if (h == UI_LINE_CHANGED) ui_find_update(u, names);
    if (h) return true;
    switch (e.key) {
    case UK_ENTER: case UK_SHIFT_ENTER:
        if (u->find_select) ui_find_select(u, names);
        else ui_find_leave(u);
        return true;
    case UK_ESC:
        ui_find_close(u);
        u->list.sel = ui_find_back(u), ui_show_sel(&u->list);
        return true;
    case UK_UP: case UK_DOWN: case UK_SEL_UP: case UK_SEL_DOWN: ui_find_step(u, names, e.key == UK_UP || e.key == UK_SEL_UP ? -1 : 1); return true;
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
    bool prompt = u->asking || u->confirming || u->cancel_asking || u->quit_asking || u->full_told || u->empty_asking || u->props || u->ow || u->bmp || u->keys || u->about || u->bar_told;
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
    } else if (u->bmp && !u->quit_asking && !u->full_told && !u->asking && !u->confirming) {
        // Edit Bookmarks (F7.5): a row takes the cursor (a double click goes there), a button acts on
        // it as its key does, Close closes; the rename field takes no press (its own keys only).
        isize row = in->bmp_drow >= 0 ? in->bmp_drow : in->bmp_row;
        if (row >= 0 && u->bmp_list.top + row < u->bmp_list.n) {
            u->bmp_list.sel = u->bmp_list.top + row, u->bmp_naming = false;
            if (in->bmp_drow >= 0) return u->bmp = false, (UiAction){ UA_BM_GO, u->bmp_list.sel, false };
        }
        if (in->bmp_btn >= 1 && in->bmp_btn <= 4 && !u->bmp_naming) return ui_bmp_do(u, in->bmp_btn);
        if (in->modal == UI_MODAL_NO) return u->bmp = false, (UiAction){ UA_NONE, 0, false };
    } else if (u->keys && !u->quit_asking && !u->full_told && !u->asking && !u->confirming) {
        // Keyboard Shortcuts: Close closes; the wheel scrolls it.
        if (in->modal == UI_MODAL_NO) return ui_keys_close(u), (UiAction){ UA_NONE, 0, false };
        if (in->ow_wheel) ui_scroll(&u->keys_list, in->ow_wheel);
    } else if (u->about && !u->quit_asking && !u->full_told && !u->asking && !u->confirming) {
        if (in->modal == UI_MODAL_NO) return u->about = false, (UiAction){ UA_NONE, 0, false };
    } else if (u->bar_told && !u->quit_asking && !u->full_told && !u->asking && !u->confirming) {
        if (in->modal == UI_MODAL_OK) return u->bar_told = false, (UiAction){ UA_NONE, 0, false };
    } else if (!u->ow && !prompt && u->finding && (in->find_x || in->find_field || in->find_step)) {
        // A chevron steps with two matches or more (greyed under that) and leaves the keys where
        // they are.
        isize at;
        if (in->find_step) {
            if (ui_find_count(u, names, &at) >= 2) ui_find_step(u, names, in->find_step > 0 ? 1 : -1);
        } else if (in->find_x) ui_find_close(u);
        else ui_edit_close(u), u->find_keys = true, u->side_focus = false, u->crumb_focus = false, u->jobs_focus = false;   // (an open edit line would take the keys first: reviewer-2)
        return (UiAction){ UA_NONE, 0, false };
    } else if (!u->ow && in->toggle >= 1 && in->toggle <= 3 && (u->finding || u->in_search)) {
        u->find_opts = ui_toggle(u->find_opts, in->toggle);   // (the search view's are find's, F9)
        if (u->finding) ui_find_update(u, names);
        if (u->in_search) u->search_edited = true;
        return (UiAction){ UA_NONE, 0, false };
    } else if (!prompt && u->in_search && in->search_field) {
        ui_edit_close(u), u->search_keys = true, u->find_keys = false, u->side_focus = false, u->crumb_focus = false, u->jobs_focus = false;
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
        if (!ui_menu_enabled(u, it)) return (UiAction){ UA_NONE, 0, false };
        UiAction a = ui_menu_pick(u, it);
        if (a.kind != UA_NONE || (!u->menu_key.key && !u->menu_key.ch)) return a;
        // (an entry that stands for a key: the loop below runs it first)
    }
    if (u->menu_on && in->menu_away) return u->menu_on = false, (UiAction){ UA_NONE, 0, false };
    // A right press (F8): on a row, its menu. An unmarked row becomes the only one (the marks clear,
    // as Nemo and GTK do); a marked one keeps them, the cursor on it. On the files' empty space, the
    // folder shown's menu, the marks cleared.
    if (!prompt && in->ctx_row >= 0 && u->list.top + in->ctx_row < u->list.n) {
        isize row = u->list.top + in->ctx_row;
        if (!ui_marked(u, row)) ui_mark_clear(u);
        u->list.sel = row;
        ui_ctx_open(u, UI_CTX_ENTRY, names, false);
        return (UiAction){ UA_NONE, 0, false };
    }
    if (!prompt && in->ctx_here) {
        ui_mark_clear(u);
        ui_ctx_open(u, UI_CTX_HERE, names, false);
        return (UiAction){ UA_NONE, 0, false };
    }
    // On a sidebar row (not a heading) or a path button: that folder's menu; the sidebar's cursor
    // goes to its row.
    if (!prompt && in->ctx_side >= 0 && u->side.top + in->ctx_side < u->side.n && !ui_side_head(u, u->side.top + in->ctx_side)) {
        u->side.sel = u->side.top + in->ctx_side;
        ui_folder_open(u, u->side.sel, -1, false);
        return (UiAction){ UA_NONE, 0, false };
    }
    if (!prompt && in->ctx_crumb >= 0 && in->ctx_crumb < in->crumb_n) {
        ui_folder_open(u, -1, in->ctx_crumb, false);
        return (UiAction){ UA_NONE, 0, false };
    }
    // On a tab: its menu, the tab shown (gui/ shows it; the menu acts on the current one).
    if (!prompt && in->ctx_tab >= 0 && in->ctx_tab < u->tab_n) {
        ui_menu_open(u, UI_CTX_TAB, false);   // (it closes an edit line, as the bar's menus do)
        return (UiAction){ UA_TAB_SHOW, in->ctx_tab, false };
    }
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
        case UB_SEARCH:   // as Ctrl+Shift+F (F9; the find bar's until then)
            ui_edit_close(u);
            return ui_search_open(u);
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
    // An entry the pointer picked runs its key first (F7.4), then the frame's keys.
    for (int k = u->menu_key.key || u->menu_key.ch ? -1 : 0; k < in->nev; k++, extend = ui_extend(u, extend)) {
        UiEvent e = k < 0 ? u->menu_key : in->ev[k];
        if (k < 0) u->menu_key = (UiEvent){ UK_NONE, 0 };
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
        // Shift+F10 and the Menu key (F8): the cursor's menu, or the folder shown's in an empty one,
        // while the files have the keys (an edit or find line keeps them); the sidebar's cursor's,
        // or the path button's, while they have them.
        if (!prompt && !u->menu_on && e.key == UK_CONTEXT && !u->editing && !(u->finding && u->find_keys) && !u->jobs_focus) {
            if (u->crumb_focus) ui_folder_open(u, -1, u->crumb_at, true);
            else if (u->side_focus && u->side.n > 0 && !ui_side_head(u, u->side.sel)) ui_folder_open(u, u->side.sel, -1, true);
            else if (!u->side_focus) ui_ctx_open(u, u->list.n > 0 ? UI_CTX_ENTRY : UI_CTX_HERE, names, true);
            continue;
        }
        if (u->menu_on && !prompt) {
            UiAction a;
            if (ui_menu_event(u, e, &a)) {
                if (a.kind != UA_NONE) return a;
                if (!u->menu_key.key && !u->menu_key.ch) continue;
                e = u->menu_key, u->menu_key = (UiEvent){ UK_NONE, 0 };   // an entry picked runs its key, here
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
        if (u->bmp && !u->asking && !u->confirming) {
            UiAction a = ui_bmp_event(u, e, in->paste);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        if (u->keys && !u->asking && !u->confirming) {
            ui_keys_event(u, e, in->paste);
            continue;
        }
        if (u->about && !u->asking && !u->confirming) {   // it takes every key: Esc and Enter close it
            if (e.key == UK_ESC || e.key == UK_ENTER) u->about = false;
            continue;
        }
        if (u->bar_told && !u->asking && !u->confirming) {   // likewise
            if (e.key == UK_ESC || e.key == UK_ENTER) u->bar_told = false;
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
        // The search field (F9) before Shift is taken off the moves: it selects with them.
        if (u->in_search && u->search_keys && !prompt && ui_search_event(u, e, in->paste)) continue;
        // The find bar's likewise (2026-10-28), after the questions: a clash or the delete question
        // takes its keys first.
        if (u->finding && u->find_keys && !prompt && ui_find_event(u, names, e, in)) continue;
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
        bool shift_enter = e.key == UK_SHIFT_ENTER;   // Enter for the questions and the find bar; Open With in the files
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
            if (!u->side_focus && l->n > 0 && shift_enter) return u->in_trash ? (UiAction){ UA_NOT_IN_TRASH, 0, false } : ui_ow_open(u);
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
        case UK_WORD_LEFT:   // (Ctrl+Left: a word in a text field, the sidebar here)
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
        case UK_SELECT_MATCH:
            if (l->n > 0) ui_find_open(u), u->find_select = true;
            break;
        case UK_HIDDEN: return (UiAction){ UA_HIDDEN, 0, false };
        case UK_PERMS_COLS:   // (greyed in the icon view, as its entry)
            if (!u->grid) return u->perms_cols = !u->perms_cols, (UiAction){ UA_PERMS_COLS, 0, false };
            break;
        case UK_GROUP_COL:
            if (!u->grid && u->perms_cols) return u->group_col = !u->group_col, (UiAction){ UA_GROUP_COL, 0, false };
            break;
        case UK_ALL:
            for (isize i = 0; i < l->n; i++) ui_mark(u, i, true);
            break;
        case UK_INVERT:
            for (isize i = 0; i < l->n; i++) ui_mark(u, i, !ui_marked(u, i));
            break;
        case UK_RELOAD: return (UiAction){ UA_RELOAD, 0, false };
        case UK_HOMEDIR: return (UiAction){ UA_HOMEDIR, 0, false };
        case UK_TRASH_VIEW: return (UiAction){ UA_TRASH_VIEW, 0, false };
        case UK_ADD_BOOKMARK: return (UiAction){ u->in_trash ? UA_NOT_IN_TRASH : u->in_search ? UA_NOT_IN_SEARCH : UA_ADD_BOOKMARK, 0, false };
        case UK_EDIT_BOOKMARKS: return ui_bmp_open(u, 0);
        case UK_SHORTCUTS: return ui_keys_open(u);
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
        case UK_MAKE_LINK: if (l->n > 0 && !u->side_focus) return (UiAction){ u->in_trash ? UA_NOT_IN_TRASH : UA_MAKE_LINK, 0, false }; break;
        case UK_CUT: if (l->n > 0 && !u->side_focus) return (UiAction){ u->in_trash ? UA_NOT_IN_TRASH : UA_CUT, 0, false }; break;
        case UK_PASTE: return (UiAction){ u->in_trash ? UA_NOT_IN_TRASH : u->in_search ? UA_NOT_IN_SEARCH : UA_PASTE, 0, false };
        case UK_UNDO: return (UiAction){ UA_UNDO, 0, false };
        case UK_NEWFOLDER: return (UiAction){ u->in_trash ? UA_NOT_IN_TRASH : u->in_search ? UA_NOT_IN_SEARCH : UA_NEWFOLDER, 0, false };
        case UK_LOCATION: case UK_WORD_RIGHT: return (UiAction){ UA_LOCATION, 0, false };   // (Ctrl+Right too, outside a text field)
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
        case UK_SEARCH: return ui_search_open(u);
        case UK_CONTAINING:   // (the search view's: elsewhere every entry is in the folder shown)
            if (u->in_search && !u->side_focus && l->n > 0) return (UiAction){ UA_CONTAINING, l->sel, false };
            break;
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
    if (str_eq(cw, S(TRASH_URI)) || is_search(cwd)) {   // the trash view (F6): one button, "Trash"; a search's (F9), "Search"
        snprintf(c->deep, sizeof c->deep, "%s", cwd);
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
    if (str_starts_with(ui_crumb_path(c, i), S(SEARCH_URI))) return S("Search");
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
    // (a search's folder/name too long for it: none, not a cut one naming another entry, F9)
    snprintf(stack[*n].sel, sizeof stack[*n].sel, "%.*s", sel.len < (isize)sizeof stack[*n].sel ? (int)sel.len : 0, sel.p ? sel.p : "");
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

// A mode's permissions as ls prints them, without the type: "rwxr-xr-x"; s/S for set-user and
// set-group IDs, t/T for the sticky bit (upper case: the x under it is off). "-" for mode 0 (a
// broken link: nothing to read). The list's Permissions column (2026-10-27).
static void ui_perms(char *buf, size_t cap, u32 mode)
{
    if (!mode) {
        snprintf(buf, cap, "-");
        return;
    }
    char p[10];
    static const char RWX[] = "rwx";
    for (int k = 0; k < 9; k++) p[k] = mode & (0400u >> k) ? RWX[k % 3] : '-';
    if (mode & 04000) p[2] = p[2] == 'x' ? 's' : 'S';
    if (mode & 02000) p[5] = p[5] == 'x' ? 's' : 'S';
    if (mode & 01000) p[8] = p[8] == 'x' ? 't' : 'T';
    p[9] = 0;
    snprintf(buf, cap, "%s", p);
}

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
    bool  cramped;       // Show Permissions on, but the window has no room for them: said (2026-10-27)
} UiInfo;

static void ui_status_base(char *buf, size_t cap, const UiInfo *in);

// That, and when the permissions' columns gave way for lack of room, so it says (Michael, 2026-10-27:
// an option set and nothing showing would confuse).
static void ui_status_info(char *buf, size_t cap, const UiInfo *in)
{
    ui_status_base(buf, cap, in);
    size_t k = strlen(buf);
    if (in->cramped && k < cap) snprintf(buf + k, cap - k, " \xC2\xB7 Permissions hidden: the window is too narrow");
}

static void ui_status_base(char *buf, size_t cap, const UiInfo *in)
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
