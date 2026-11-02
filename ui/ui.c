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
    UK_TAB_REOPEN, // Ctrl+Shift+T (F11)
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
    UK_OPEN_LINE,  // Ctrl+Shift+Enter: a result found inside files, in the editor at its line (F9 step 4)
    // In a text field a word at a time (Michael, 2026-10-28): Ctrl+Left and Ctrl+Right move (elsewhere
    // they are the sidebar's keys and the location line, UK_LOCATION), Ctrl+Shift+Left and
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
    // toggle pressed (1 case, 2 whole word, 3 regular expression), each -1 or 0 for none.
    const Str *app_name;
    isize   app_n;
    int     ow_row, ow_drow;
    bool    ow_field;
    int     ow_wheel;    // notches over its list (or Keyboard Shortcuts'), as wheel
    int     toggle;      // 4: Open With's "Always open … with this app" box (F7.3.1)
    bool    find_x;      // the find bar's × pressed
    // A text bar's field pressed (UiField: the find bar's, the search's name and Containing; -1
    // none), or with field_toggle one of its toggles (1 case, 2 whole word, 3 regular expression).
    int     field, field_toggle;
    bool    inside_btn;  // the search bar's "In files" (F9 step 3)
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
    UA_BM_RENAME,   // the field's Enter: Ui.bmp_name is its label (empty: none)
    UA_MAKE_LINK,   // a symbolic link "Link to <name>" beside each marked entry (or the cursor's) (F8)
    UA_FOLLOW,      // index: the entry, a link: to its target's folder, the target selected (F8)
    UA_TAB_OTHERS,  // every tab but the current one closed (F8)
    UA_TAB_REOPEN,  // the tab closed last, open again (F11)
    UA_RESTORE_WINDOWS,  // all: yes, the other windows back; no, forgotten (F11 step 3)
    UA_COLORS,      // the list's colours on or off (Ui.colors): gui/ remembers it
    UA_MENUBAR,     // the menu bar hidden or shown (Ui.bar_hidden): gui/ remembers it
    UA_PREFERENCES, // ofx.conf in the editor, written first when there is none (F10)
    UA_PERMS_COLS,  // the list's Owner and Permissions shown or not (Ui.perms_cols): gui/ remembers it
    UA_GROUP_COL,   // the list's Group beside them shown or not (Ui.group_col): gui/ remembers it
    UA_FOLDER_DO,   // index: a UiMenuItem of the folder menu, for the folder it opened on (Ui.ctx_side or ctx_crumb, F8)
    UA_SEARCH,      // a search below the folder shown (all: below the folder entry index), in a new tab (F9)
    UA_NOT_IN_SEARCH,   // something the search view can't do: paste, a new folder, a bookmark of it (F9; gui/ says so)
    UA_CONTAINING,  // index: a search result: its folder, it selected (F9)
    UA_OPEN_LINE,   // index: a search result found inside files: in the editor at its line (F9 step 4; gui/ says when it has none)
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
// above, and the "same for the rest" box). The cancel, quit, Empty Trash and Bring back (F11)
// questions use YES and NO, the queue-full note OK; the panels' Close is NO, Open With's Open YES.
enum { UI_MODAL_YES = 1, UI_MODAL_NO, UI_MODAL_SKIP, UI_MODAL_REPLACE, UI_MODAL_KEEP, UI_MODAL_CANCEL, UI_MODAL_ALL, UI_MODAL_OK };

// The menu bar (F7.1, docs/plan.md "F7 design"; Edit, View and Go F7.4): its menus, each a list of
// entries; UM_SEP is a line between groups.
typedef enum {
    UM_SEP, UM_NEW_TAB, UM_TAB_REOPEN, UM_NEW_WINDOW, UM_NEW_FOLDER, UM_NEW_FILE, UM_OPEN, UM_OPEN_WITH, UM_PROPERTIES, UM_CLOSE_ALL, UM_CLOSE,
    UM_UNDO, UM_CUT, UM_COPY, UM_PASTE, UM_SELECT_ALL, UM_SELECT_MATCH, UM_INVERT, UM_DUPLICATE, UM_MAKE_LINK, UM_RENAME, UM_TRASH, UM_DELETE, UM_RESTORE,
    UM_RELOAD, UM_SIDE_PLACES, UM_SIDE_TREE, UM_SIDE_NONE, UM_HIDDEN, UM_PERMS_COLS, UM_GROUP_COL, UM_COLORS, UM_SHOW_MENU, UM_SORT_NAME, UM_SORT_SIZE, UM_SORT_MTIME, UM_SORT_REVERSE,
    UM_ZOOM_IN, UM_ZOOM_OUT, UM_ZOOM_RESET, UM_VIEW_LIST, UM_VIEW_ICONS,
    UM_PARENT, UM_BACK, UM_FORWARD, UM_HOME, UM_TRASH_VIEW, UM_FIND, UM_SEARCH, UM_LOCATION, UM_TERMINAL,
    UM_ADD_BOOKMARK, UM_EDIT_BOOKMARKS, UM_BM_0, UM_BM_1, UM_BM_2, UM_BM_3, UM_BM_4, UM_BM_5, UM_BM_6, UM_BM_7, UM_BM_MORE,
    UM_SHORTCUTS, UM_ABOUT, UM_PREFERENCES,
    UM_OPEN_TAB, UM_EMPTY_TRASH, UM_HERE_PROPS, UM_OPEN_WINDOW, UM_PASTE_INTO, UM_TERMINAL_IN, UM_FOLLOW, UM_BOOKMARK_IT,
    UM_CONTAINING, UM_OPEN_LINE, UM_SEARCH_IN,
    UM_OPEN_IT, UM_CUT_IT, UM_COPY_IT, UM_BM_RENAME_IT, UM_BM_UP_IT, UM_BM_DOWN_IT, UM_BM_REMOVE_IT, UM_BM_EDIT_IT, UM_EJECT_IT,
    UM_UNMOUNT_IT, UM_MOUNT_IT, UM_EMPTY_IT, UM_PROPS_IT, UM_TAB_LEFT, UM_TAB_RIGHT, UM_TAB_OTHERS,
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
    [UM_BM_MORE] = { "", "", "bm-more" },   // "N more in Edit Bookmarks…" (gui/'s label): the panel, on the first not listed
    // Help: no manual (Michael, 2026-10-27)
    [UM_SHORTCUTS] = { "Keyboard Shortcuts", "F1", "shortcuts", { UK_SHORTCUTS, 0 } },
    [UM_ABOUT] = { "About", "", "about" },
    [UM_PREFERENCES] = { "Preferences\xE2\x80\xA6", "", "preferences" },   // (ofx.conf, F10)
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
    [UM_OPEN_LINE] = { "Open at Line", "Ctrl+Shift+Enter", "open-line", { UK_OPEN_LINE, 0 } },   // gui/ adds "in <editor>" (F9 step 4)
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
    [UM_MOUNT_IT] = { "Mount", "", "mount-it" },
    [UM_UNMOUNT_IT] = { "Unmount", "", "unmount-it" },
    [UM_EMPTY_IT] = { "Empty Trash\xE2\x80\xA6", "", "empty-it" },
    [UM_PROPS_IT] = { "Properties\xE2\x80\xA6", "", "properties-it" },
    // and on a tab
    [UM_TAB_LEFT] = { "Move Tab Left", "Ctrl+Shift+PgUp", "tab-left", { UK_TAB_LEFT, 0 } },
    [UM_TAB_RIGHT] = { "Move Tab Right", "Ctrl+Shift+PgDn", "tab-right", { UK_TAB_RIGHT, 0 } },
    [UM_TAB_OTHERS] = { "Close Other Tabs", "", "tab-others" },
    [UM_TAB_REOPEN] = { "Reopen Closed Tab", "Ctrl+Shift+T", "reopen-tab", { UK_TAB_REOPEN, 0 } },
    [UM_BM_0] = { "", "", "bm-0", .mark = 'r' }, [UM_BM_1] = { "", "", "bm-1", .mark = 'r' },
    [UM_BM_2] = { "", "", "bm-2", .mark = 'r' }, [UM_BM_3] = { "", "", "bm-3", .mark = 'r' },
    [UM_BM_4] = { "", "", "bm-4", .mark = 'r' }, [UM_BM_5] = { "", "", "bm-5", .mark = 'r' },
    [UM_BM_6] = { "", "", "bm-6", .mark = 'r' }, [UM_BM_7] = { "", "", "bm-7", .mark = 'r' },
};

static const UiMenuItem UI_MENU_FILE[] = {
    UM_NEW_TAB, UM_TAB_REOPEN, UM_NEW_WINDOW, UM_NEW_FOLDER, UM_NEW_FILE, UM_SEP, UM_OPEN, UM_OPEN_WITH, UM_SEP, UM_PROPERTIES, UM_SEP,
    UM_CLOSE_ALL, UM_CLOSE,
};
// Move to Trash shows outside the trash view, Restore in it (ui_menu_shown).
static const UiMenuItem UI_MENU_EDIT[] = {
    UM_UNDO, UM_SEP, UM_CUT, UM_COPY, UM_PASTE, UM_SEP, UM_SELECT_ALL, UM_SELECT_MATCH, UM_INVERT, UM_SEP, UM_DUPLICATE, UM_MAKE_LINK, UM_RENAME, UM_SEP,
    UM_TRASH, UM_RESTORE, UM_DELETE, UM_SEP, UM_PREFERENCES,
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
    UM_OPEN, UM_CONTAINING, UM_OPEN_LINE, UM_OPEN_TAB, UM_OPEN_WINDOW, UM_OPEN_WITH, UM_SEP, UM_CUT, UM_COPY, UM_PASTE_INTO, UM_DUPLICATE, UM_SEP,
    UM_MAKE_LINK, UM_RENAME, UM_SEP, UM_TERMINAL_IN, UM_SEARCH_IN, UM_FOLLOW, UM_BOOKMARK_IT, UM_SEP, UM_TRASH, UM_RESTORE, UM_DELETE, UM_SEP,
    UM_PROPERTIES,
};
// A folder elsewhere: a tree row, a path button, a place (Trash, a bookmark, a drive have their own).
static const UiMenuItem UI_MENU_CTX_FOLDER[] = {
    UM_OPEN_IT, UM_OPEN_TAB, UM_OPEN_WINDOW, UM_TERMINAL_IN, UM_SEARCH_IN, UM_SEP, UM_CUT_IT, UM_COPY_IT, UM_PASTE_INTO, UM_SEP, UM_BOOKMARK_IT,
    UM_BM_RENAME_IT, UM_BM_UP_IT, UM_BM_DOWN_IT, UM_BM_REMOVE_IT, UM_BM_EDIT_IT, UM_SEP, UM_UNMOUNT_IT, UM_EJECT_IT, UM_MOUNT_IT, UM_EMPTY_IT, UM_SEP, UM_PROPS_IT,
};
// A tab (the current one: a right press on another shows it first).
static const UiMenuItem UI_MENU_CTX_TAB[] = { UM_NEW_TAB, UM_TAB_REOPEN, UM_SEP, UM_TAB_LEFT, UM_TAB_RIGHT, UM_SEP, UM_CLOSE, UM_TAB_OTHERS };
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
    { KS_TABS, "Reopen Closed Tab", "Ctrl+Shift+T", "" },
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
    { KS_CRUMBS, "To the path buttons", "Ctrl+K", "Ctrl+Up; in a search, the search field first" },
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
    { KS_SEARCH, "Open at Line", "Ctrl+Shift+Enter", "a result found inside files, in the editor" },
    { KS_SEARCH, "The keys to the results", "Enter", "Esc, Down" },
    { KS_SEARCH, "Back to the search field", "Ctrl+Shift+F", "a click in it" },
    { KS_SEARCH, "A wildcard over the whole name", "*, ?", "*.ts" },
    { KS_SEARCH, "Match case, whole words, a regular expression", "Alt+C, W, R", "the name's shared with find; Containing's its own" },
    { KS_SEARCH, "In files: a Containing field too", "Alt+I", "the In files button" },
    { KS_SEARCH, "The other field, in files", "Tab", "" },
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

// A text field's text (2026-10-28: four loose variables a field before): its bytes, how many, the
// caret and the selection's other end (bytes; the selection is between them). n bytes hold at
// most n - 1 (no 0 is written after them: read it as b and len); UI_TEXT_LINE is the editor's view
// of one (edit.c).
#define UI_TEXT(n) struct { char b[n]; int len, caret, anchor; }
#define UI_TEXT_CLEAR(t) ((t).len = (t).caret = (t).anchor = 0)

// Which listing is shown. One value (2026-10-28: in_trash and in_search before, two flags tested
// apart at about 250 places, which could both be set).
typedef enum { UI_VIEW_FOLDER, UI_VIEW_TRASH, UI_VIEW_SEARCH } UiView;

// The text bars' fields (F9's review, 2026-10-29: one table for what was three of each): the find
// bar's, the search's name and its Containing. ui_field_shown says when each is there.
typedef enum { UI_FIELD_FIND, UI_FIELD_SEARCH, UI_FIELD_INSIDE, UI_FIELDS } UiField;

// Who has the keys: the files' list unless one of the others took them. One value, so two can't
// at once (2026-10-28: six flags before, kept apart by hand; opening the location line from the
// search view left the search field holding the keys too).
typedef enum {
    UI_FOCUS_LIST,
    UI_FOCUS_SIDE,     // the sidebar's cursor
    UI_FOCUS_CRUMBS,   // the path buttons (Ctrl+K, F5.2), crumb_at marked
    UI_FOCUS_JOBS,     // the job rows' × (Ctrl+J, F5.3), job_at marked
    UI_FOCUS_FIND,     // the find bar's field (while finding)
    UI_FOCUS_SEARCH,   // the search field (in the search view, F9)
    UI_FOCUS_INSIDE,   // its "Containing" field (searching inside files, F9 step 3)
    UI_FOCUS_EDIT,     // the edit line: a rename or the location line; it is open while it has them
} UiFocus;

typedef struct {
    UiList list, side;
    SideMode side_mode;      // places, tree or hidden (F6)
    bool   grid;             // the files as icons in a grid, not the list (F6.2)
    int    zoom;             // the icon view's icon size in px, one of UI_ZOOM_PX; 0: UI_ZOOM_DEFAULT (F6.3)
    UiFocus focus;           // who has the keys
    const u8 *side_head;     // a byte per sidebar row, 1 for a heading (F6), never the cursor's; NULL: none
    bool   finding;          // the find bar is open (Michael, 2026-10-26: until Esc or its ×)
    bool   find_select;      // Select Items Matching (F7.4): Enter selects every match, and closes it
    bool   find_miss;        // its text matches nothing
    UI_TEXT(UI_FIND_MAX + 1) find;
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
    // location line (F4), which holds a path. Open while focus is UI_FOCUS_EDIT.
    bool   edit_path;        // the location line
    UI_TEXT(PATH_MAX) edit;  // (its own most: a name's or a path's, ui_edit_max)
    // A Ctrl+C or Ctrl+X in the edit line: the text gui/ puts on the clipboard, then clears copied.
    bool   copied;
    char   copy[PATH_MAX];
    int    copy_len;
    // The keyboard on the path buttons (Ctrl+K, F5.2): the one marked.
    int    crumb_at;
    // The prompts and the job: gui/ says when one runs, and when it asks about a clash.
    bool   confirming;       // "Delete … for good? Y/n"
    isize  confirm_n;
    bool   asking;           // a job waits for a clash answer
    bool   ask_all;          // its "same for the rest" box (the capitals too)
    // The job rows (F5.3): the keyboard on their × (Ctrl+J), the row marked; "Cancel this job?"
    // about job cancel_id; "Quit and cancel them?"; "No more jobs can be queued".
    int    job_at;
    bool   cancel_asking;
    int    cancel_id;
    bool   quit_asking;
    bool   full_told;
    // The listing shown: a folder, the trash view (F6) or a search's results (F9); gui/ says which.
    UiView view;
    bool   empty_asking;     // "Empty the trash?" up
    int    restore_n;        // "Bring back the N other windows?" up (F11 step 3): their number, 0 none
    // The search view's field's text (find's toggles apply), and search_edited when the text or a
    // toggle changed (gui/ searches again 150 ms after the last change, and clears it).
    bool   search_edited;
    UI_TEXT(UI_FIND_MAX + 1) search;
    // Inside files (F9 step 3, Michael 2026-10-29): "In files" on, the "Containing" field shown
    // under the name's, with toggles of its own.
    bool   search_inside;
    UI_TEXT(UI_FIND_MAX + 1) inside;
    UiMatchOpts inside_opts;
    bool   search_was_inside;   // "Containing" had the keys last: Ctrl+K and Ctrl+Shift+F go back there
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
    // From gui/ each frame (menu_facts): the cursor's entry is a link that leads somewhere, a
    // folder that is a bookmark already (F8), a result found inside files (F9 step 4: it has a line).
    bool   sel_link, sel_bm, sel_hit;
    // From gui/ each frame: the sidebar row of the folder shown (its place, or its tree row), where
    // Tab puts the cursor; -1 for none (Michael, 2026-10-29).
    isize  side_here;
    // A folder elsewhere's menu (F8): the sidebar row (absolute) or path button it is on (the other
    // -1); and from gui/ as it opens (ctx_facts): it is the trash, a place (no Cut or Copy: a bookmark
    // isn't the folder to move), its bookmark's line in the file and how many lines (-1: not a
    // bookmark's row), a bookmark already, a drive with an eject button, a trash with something in it.
    int    ctx_side, ctx_crumb;
    bool   ctx_trash, ctx_place, ctx_bmed, ctx_eject, ctx_full;
    bool   ctx_unmounted;   // a drive not mounted yet (2026-10-30): Mount is all its menu has
    bool   ctx_w;   // the folder (either menu's, as it opens) takes new files: Paste Into Folder
    int    ctx_bmk, ctx_bms;
    // The tabs, from gui/ each frame (menu_facts): the current one's place, how many (F8's tab menu).
    int    tab_at, tab_n;
    bool   can_reopen;      // a tab closed in this session to open again (F11)
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
    UI_TEXT(256) bmp_name;
    // Keyboard Shortcuts (F7.5): up; its filter's text; the lines it shows (a row of UI_KEYS, or
    // -1 - a section for its heading), scrolled by keys_list.top.
    bool   keys;
    UI_TEXT(64) keys_filter;
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
    UI_TEXT(UI_FIND_MAX + 1) ow_filter;
    UiMatchOpts ow_opts;
    UiMatcher   ow_m;
    UiList ow_list;
    isize  ow_idx[UI_APPS_MAX];
    // Set as default (F7.3.1): the box ticked, and whether gui/ lets it be (an empty file's type,
    // or one a list read first sets, can't).
    bool   ow_def;
    bool   ow_def_can;
} Ui;

// The question or panel on top: the one that takes the keys and the pointer, and the one gui/
// draws. One order for all of them (2026-10-28: the keys took a clash before the delete question,
// the pointer and gui/ the other way round). The questions first, then the panels; F5.3's queue
// full note above all.
typedef enum {
    UI_ON_NONE,
    UI_ON_FULL,     // full_told: the job queue is full
    UI_ON_QUIT,     // quit_asking
    UI_ON_CANCEL,   // cancel_asking: a job's ×
    UI_ON_DELETE,   // confirming: delete for good
    UI_ON_EMPTY,    // empty_asking: Empty Trash
    UI_ON_RESTORE,  // restore_n: the other windows after Close All (F11)
    UI_ON_CLASH,    // asking: a job waits for a clash answer
    UI_ON_PROPS,
    UI_ON_OW,
    UI_ON_BM,       // Edit Bookmarks
    UI_ON_KEYS,     // Keyboard Shortcuts
    UI_ON_ABOUT,
    UI_ON_BAR,      // bar_told: the menu bar just hidden
} UiOnTop;

// Whether an action works in view v. The trash view holds files that are nowhere yet: what would
// open, copy, link or make from them, or go from there, waits until they are restored. A search's
// results are in many folders: what needs one folder to put something in (paste, a new folder, a
// bookmark of it) doesn't work there (F9). Restoring and emptying are the trash view's own.
static bool ui_view_allows(UiView v, UiActionKind k)
{
    switch (k) {
    case UA_COPY: case UA_CUT: case UA_MAKE_LINK: case UA_TERMINAL: case UA_NEW_WINDOW: case UA_SEARCH: return v != UI_VIEW_TRASH;
    case UA_PASTE: case UA_NEWFOLDER: case UA_ADD_BOOKMARK: return v == UI_VIEW_FOLDER;
    case UA_RESTORE: case UA_EMPTY_TRASH: return v == UI_VIEW_TRASH;
    case UA_CONTAINING: return v == UI_VIEW_SEARCH;
    default: return true;
    }
}

// a, or why the view shown refuses it (gui/ says so in the status line).
static UiAction ui_view_act(const Ui *u, UiAction a)
{
    if (ui_view_allows(u->view, a.kind)) return a;
    return (UiAction){ u->view == UI_VIEW_TRASH ? UA_NOT_IN_TRASH : u->view == UI_VIEW_SEARCH ? UA_NOT_IN_SEARCH : UA_NONE, 0, false };   // (a folder's: nothing to say)
}

// f gives the keys back to the list, if it has them.
static void ui_unfocus(Ui *u, UiFocus f)
{
    if (u->focus == f) u->focus = UI_FOCUS_LIST;
}

static UiOnTop ui_on_top(const Ui *u)
{
    if (u->full_told) return UI_ON_FULL;
    if (u->quit_asking) return UI_ON_QUIT;
    if (u->cancel_asking) return UI_ON_CANCEL;
    if (u->confirming) return UI_ON_DELETE;
    if (u->empty_asking) return UI_ON_EMPTY;
    if (u->restore_n > 0) return UI_ON_RESTORE;
    if (u->asking) return UI_ON_CLASH;
    if (u->props) return UI_ON_PROPS;
    if (u->ow) return UI_ON_OW;
    if (u->bmp) return UI_ON_BM;
    if (u->keys) return UI_ON_KEYS;
    if (u->about) return UI_ON_ABOUT;
    if (u->bar_told) return UI_ON_BAR;
    return UI_ON_NONE;
}

static UiInput ui_input_none(void)
{
    return (UiInput){ .click = -1, .dclick = -1, .side_click = -1, .header = -1, .click_mods = 0, .bar = UB_NONE, .crumb = -1,
                      .tab_click = -1, .tab_close = -1, .mid_row = -1, .mid_side = -1, .mid_crumb = -1, .modal = 0, .job_x = -1, .side_mode = -1, .side_twisty = -1, .side_eject = -1,
                      .zoom_to = -1, .menu_title = -1, .menu_item = -1, .ctx_row = -1, .ctx_side = -1, .ctx_crumb = -1, .ctx_tab = -1, .prop_box = -1, .ow_row = -1, .ow_drow = -1, .bmp_row = -1, .bmp_drow = -1,
                      .field = -1 };
}

#include "cursor.c"
#include "match.c"
#include "find.c"
#include "edit.c"
#include "prompts.c"
#include "menus.c"
#include "panels.c"
#include "handle.c"
#include "helpers.c"
