// What the keys, characters, wheel and clicks do: the list's cursor and scroll, the sidebar's,
// find and search, sort, the history and tabs, the selection, the text fields, the prompts, menus
// and panels, and the one action a frame may ask of gui/ (open, go somewhere, change files, ...).
// raylib-free and unit-tested; gui/main.c feeds it (docs/plan.md, from F1 "Keys" on).

#include "../base/base.h"

#include <fnmatch.h>
#include <limits.h>
#include <locale.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wctype.h>

enum { UI_EVENTS_MAX = 64, UI_FIND_MAX = 256, UI_HISTORY_MAX = 64, UI_JOB_ROWS = 4, UI_APPS_MAX = 1024, UI_BRANCHES_MAX = 1024, UI_SHARES_MAX = 64 };

// What a key means (UiEvent.key: the text fields', panels', menus' and questions' own meaning of a
// key, fixed) and the commands keys are bound to (UiEvent.cmd), in one list: many are both (Enter,
// Delete, Ctrl+C). ui/keys.c makes them of a key or a typed character: UI_FIXED the fixed meanings,
// UI_CMDS the commands by name, UI_BINDS_DEFAULT the keys as ofx has them, which the comments below
// name (ofx.conf's key lines may change them: F15). Some are named after their first key (UK_F2 is
// rename, UK_F9 the sidebar's next mode, UK_F10 the menus, UK_TAB the sidebar, UK_ESC unselect,
// UK_DELETE trash, UK_SHIFT_DELETE delete).
typedef enum {
    UK_NONE,
    UK_UP, UK_DOWN, UK_PAGEUP, UK_PAGEDOWN, UK_HOME, UK_END,
    UK_ENTER, UK_RIGHT, UK_LEFT, UK_BACKSPACE, UK_ESC, UK_TAB,
    UK_BACK,       // Alt+Left
    UK_FORWARD,    // Alt+Right
    UK_FIND,       // Ctrl+F, /
    UK_HIDDEN,     // Ctrl+H, .
    UK_F2,         // rename
    UK_DELETE,     // to the trash; in the edit line, the character after the caret
    UK_SHIFT_DELETE,   // for good
    UK_ALL,        // Ctrl+A
    UK_COPY,       // Ctrl+C
    UK_CUT,        // Ctrl+X
    UK_COPY_PATH,  // Ctrl+Shift+C: the paths as text (F13)
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
    UK_CRUMBS,     // Ctrl+K, Ctrl+Up: the keyboard on the path buttons (F5.2)
    UK_JOBS,       // Ctrl+J, Ctrl+Down: the keyboard on the job rows' × (F5.3)
    UK_QUIT,       // the window closed, or another window's Close All (F7.1)
    UK_QUIT_ALL,   // Ctrl+Q: every ofx window closes (F7.1)
    UK_F10,        // the menu bar's first menu (F7.1)
    UK_MENU,       // Alt and a letter: the menu it opens (F7.1); the event's ch is the letter, lower case
    UK_PROPERTIES, // Ctrl+I: the Properties panel (F7.2)
    UK_TERMINAL,   // Alt+Enter: a terminal here, as t (Michael, 2026-10-25)
    UK_F9,         // F9, Ctrl+S: the sidebar's next mode (F6)
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
    // The commands that were letters in the files before F15 (ui/keys.c binds them): r, s, S, n,
    // N, Space; and Ctrl+Left's outside a text field, the sidebar.
    UK_RESTORE, UK_SORT_NEXT, UK_SORT_REVERSE, UK_FIND_NEXT, UK_FIND_PREV, UK_MARK, UK_SIDE_GO,
    UK_CMD,   // a key with no meaning of its own, only the command bound to it (UiEvent.cmd)
    UK_ITEM0, // and from here a bar menu's entry as a command, UK_ITEM0 + its UiMenuItem (F15 step 2)
} UiKey;

// The top bar's buttons (F4).
typedef enum { UB_NONE, UB_BACK, UB_FORWARD, UB_UP, UB_LOCATION, UB_SEARCH, UB_LEFT, UB_RIGHT, UB_CRUMB,
               UB_RESTORE, UB_EMPTY,   // the trash view's (F6)
               UB_VIEW_LIST, UB_VIEW_ICONS,   // the view buttons (F6.2)
               UB_ZOOM_OUT, UB_ZOOM_IN,   // the zoom slider's minus and plus (F6.3), in the footer
               UB_BRANCH } UiBar;   // the git branch after the path buttons (F12 step 5b)

enum { UI_MOD_CTRL = 1, UI_MOD_SHIFT = 2, UI_MOD_ALT = 4, UI_MOD_SUPER = 8, UI_MOD_TYPED = 16 };   // (TYPED: ui/keys.c's UiChord)

// One key or one typed character, in the order they came: a find needs `/`, then text, then Enter.
// The key itself for the text fields, panels, menus and questions; the command bound to it for the
// files and the sidebar (F15, ui/keys.c).
typedef struct {
    UiKey key;   // UK_NONE: the character ch; UK_CMD: only a command
    u32   ch;
    UiKey cmd;   // the command bound to the key or character, UK_NONE for none
} UiEvent;

// The numbers a click carries (F11 review, 2026-11-02: written as 1..4 at both ends before).
// A toggle (the find bar's, the search's, Open With's), and Open With's "Always open" box; 0 none.
enum { UI_TOGGLE_CASE = 1, UI_TOGGLE_WORD, UI_TOGGLE_REGEX, UI_TOGGLES = UI_TOGGLE_REGEX, UI_TOGGLE_ALWAYS };
// Edit Bookmarks' buttons (and their keys); 0 none.
enum { UI_BMP_REMOVE = 1, UI_BMP_RENAME, UI_BMP_UP, UI_BMP_DOWN, UI_BMP_BTNS = UI_BMP_DOWN };
// UA_PROPS's index: what Properties shows.
enum { UI_PROPS_MARKED, UI_PROPS_HERE, UI_PROPS_CTX };   // the marked or the cursor's; the folder shown; the folder menu's

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
    // matches); a toggle pressed (UI_TOGGLE_*), 0 for none (its rows and field: pick_row below).
    const Str *app_name;
    isize   app_n;
    // The branches panel (F12 step 6): every branch gui/ has, in the menu's order (their names,
    // which the filter matches).
    const Str *br_name;
    isize   br_names;
    // A server's shares (F16 step 7a), as Connect to Server read them (their names).
    const Str *sh_name;
    isize   sh_names;
    // A picker panel's (Open With's or the branches'): a row pressed or double-clicked (its place on
    // screen), the field pressed (-1, false: none); and the wheel's notches over a panel's list
    // (theirs, or Keyboard Shortcuts'), as wheel.
    int     pick_row, pick_drow;
    bool    pick_field;
    int     panel_wheel;
    int     toggle;      // UI_TOGGLE_ALWAYS: Open With's "Always open … with this app" box (F7.3.1)
    bool    find_x;      // the find bar's × pressed
    // A text bar's field pressed (UiField: the find bar's, the search's name and Containing; -1
    // none), or with field_toggle one of its toggles (UI_TOGGLE_*).
    int     field, field_toggle;
    bool    inside_btn;  // the search bar's "In files" (F9 step 3)
    // Edit Bookmarks (F7.5): a row pressed or double-clicked (its place on screen), -1 for none; a
    // button pressed (1 Remove, 2 Rename, 3 Move Up, 4 Move Down), 0 for none.
    int     bmp_row, bmp_drow, bmp_btn;   // (bmp_btn: UI_BMP_*)
    int     find_step;   // its chevrons: -1 the previous match, 1 the next, 0 none
    int     login_field; // Connect's question (F16 step 4): a field pressed (UI_LOGIN_*), -1 for none
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
    UA_CLEAR_PREVIEWS,  // every preview in the cache deleted (ofx-clean --all), asked and answered (F12)
    UA_NOT_IN_TRASH,   // something that doesn't work in the trash view (gui/ says so)
    UA_READ_ONLY,      // a change in a folder that can't be written (gui/ says so; code-health review, 2026-11-03)
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
    UA_BRANCH,      // index: the branch menu's entry picked (F12 step 5b; gui/'s list of them)
    UA_SHARE,       // index: a share picked in a server's shares panel (F16 step 7a; gui/'s list)
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
    UA_COPY_PATH,   // the marked entries' paths (or the cursor's; none: the folder shown's) as text, one a line (F13);
                    // all: a folder's instead, index the sidebar row, or UI_COPY_SHOWN the folder shown
    // Connect to Server's question (F16 step 4): answered (Ui.login_*: gui/ hands them to gio and
    // wipes the password), as a guest, or not (gio stops)
    UA_LOGIN, UA_LOGIN_GUEST, UA_LOGIN_CANCEL,
} UiActionKind;
enum { UI_COPY_SHOWN = -1 };   // UA_COPY_PATH's index with all: the folder shown (the empty space's menu)

// A right-click menu's entry on a folder of the files (F8) acts on that folder, not the folder
// shown: UA_NEW_WINDOW, UA_TERMINAL, UA_PASTE and UA_ADD_BOOKMARK with `all` set; index is the entry.

typedef struct {
    UiActionKind kind;
    isize        index;   // UA_OPEN: the entry; UA_PLACE: the place; UA_ANSWER: the answer
    bool         all;     // UA_ANSWER: for all the rest; UA_OPEN: the marked entries, not only index (Enter, F7.3)
} UiAction;
#define UI_NONE ((UiAction){ UA_NONE, 0, false })   // (nothing for gui/ to do)

// The clash answers, as fs/job.c numbers them (Answer); ui/ doesn't include it.
enum { UI_ANS_SKIP = 1, UI_ANS_REPLACE = 2, UI_ANS_KEEP = 3, UI_ANS_CANCEL = 4 };

// The modal panels' buttons (F5.1): the delete question's, then the clash question's (the answers
// above, and the "same for the rest" box). The cancel, quit, Empty Trash, Bring back (F11) and
// Clear Previews (F12) questions use YES and NO, the queue-full note OK; the panels' Close is NO, Open With's Open YES.
enum { UI_MODAL_YES = 1, UI_MODAL_NO, UI_MODAL_SKIP, UI_MODAL_REPLACE, UI_MODAL_KEEP, UI_MODAL_CANCEL, UI_MODAL_ALL, UI_MODAL_OK, UI_MODAL_GUEST };
// Connect's question's fields (F16 step 4), in their order.
enum { UI_LOGIN_USER, UI_LOGIN_DOMAIN, UI_LOGIN_PASS, UI_LOGIN_FIELDS };

// The menu bar (F7.1, docs/plan.md "F7 design"; Edit, View and Go F7.4): its menus, each a list of
// entries; UM_SEP is a line between groups.
typedef enum {
    UM_SEP, UM_NEW_TAB, UM_TAB_REOPEN, UM_NEW_WINDOW, UM_NEW_FOLDER, UM_NEW_FILE, UM_OPEN, UM_OPEN_WITH, UM_PROPERTIES, UM_CLOSE_ALL, UM_CLOSE,
    UM_UNDO, UM_CUT, UM_COPY, UM_COPY_PATH, UM_PASTE, UM_SELECT_ALL, UM_SELECT_MATCH, UM_INVERT, UM_DUPLICATE, UM_MAKE_LINK, UM_RENAME, UM_TRASH, UM_DELETE, UM_RESTORE,
    UM_RELOAD, UM_SIDE_PLACES, UM_SIDE_TREE, UM_SIDE_NONE, UM_HIDDEN, UM_PERMS_COLS, UM_GROUP_COL, UM_COLORS, UM_SHOW_MENU, UM_SORT_NAME, UM_SORT_SIZE, UM_SORT_MTIME, UM_SORT_REVERSE,
    UM_ZOOM_IN, UM_ZOOM_OUT, UM_ZOOM_RESET, UM_VIEW_LIST, UM_VIEW_ICONS,
    UM_PARENT, UM_BACK, UM_FORWARD, UM_HOME, UM_TRASH_VIEW, UM_FIND, UM_SEARCH, UM_LOCATION, UM_CONNECT, UM_TERMINAL,
    UM_ADD_BOOKMARK, UM_EDIT_BOOKMARKS, UM_BM_0, UM_BM_1, UM_BM_2, UM_BM_3, UM_BM_4, UM_BM_5, UM_BM_6, UM_BM_7, UM_BM_MORE,
    UM_SHORTCUTS, UM_ABOUT, UM_PREFERENCES, UM_CLEAR_PREVIEWS,
    UM_OPEN_TAB, UM_EMPTY_TRASH, UM_HERE_PROPS, UM_OPEN_WINDOW, UM_PASTE_INTO, UM_TERMINAL_IN, UM_FOLLOW, UM_BOOKMARK_IT,
    UM_CONTAINING, UM_OPEN_LINE, UM_SEARCH_IN,
    UM_OPEN_IT, UM_CUT_IT, UM_COPY_IT, UM_COPY_PATH_IT, UM_COPY_PATH_HERE, UM_BM_RENAME_IT, UM_BM_UP_IT, UM_BM_DOWN_IT, UM_BM_REMOVE_IT, UM_BM_EDIT_IT, UM_EJECT_IT,
    UM_UNMOUNT_IT, UM_MOUNT_IT, UM_EMPTY_IT, UM_PROPS_IT, UM_TAB_LEFT, UM_TAB_RIGHT, UM_TAB_OTHERS,
    UM_BR_NOTE, UM_BR_0, UM_BR_1, UM_BR_2, UM_BR_3, UM_BR_4, UM_BR_5, UM_BR_6, UM_BR_7, UM_BR_ALL,
    UM_ITEMS
} UiMenuItem;

// The Bookmarks menu's: the first 8 of the file's, in its order (Michael, 2026-10-27).
enum { UI_MENU_BOOKMARKS = 8 };

// The branch menu's: the 8 checked out last, filled up by commit date (F12 step 5b).
enum { UI_MENU_BRANCHES = 8 };

// Which of a run of n slots (first, first + 1, …: a bookmark's, a branch's) entry it is, or -1.
static int ui_slot(UiMenuItem it, UiMenuItem first, int n)
{
    return it >= first && it < first + n ? (int)(it - first) : -1;
}

// A run of 8 slots' entries (bookmarks, branches): no label of their own (gui/'s), named <w>0..7
// in a script's state line, marked as one of a group.
#define UI_SLOT_ITEM(first, k, w) [first + k] = { "", w #k, .mark = 'r' }
#define UI_SLOT_ITEMS(first, w) UI_SLOT_ITEM(first, 0, w), UI_SLOT_ITEM(first, 1, w), UI_SLOT_ITEM(first, 2, w), UI_SLOT_ITEM(first, 3, w), \
                                UI_SLOT_ITEM(first, 4, w), UI_SLOT_ITEM(first, 5, w), UI_SLOT_ITEM(first, 6, w), UI_SLOT_ITEM(first, 7, w)
_Static_assert(UI_MENU_BOOKMARKS == 8 && UI_MENU_BRANCHES == 8, "UI_SLOT_ITEMS writes 8");

// Each entry's label and its name in a script's state line; the command whose first key the menu
// shows (ui/keys.c), and whether picking it runs that command (F7.4: so the two can't differ; else
// its action in ui_menu_pick); its mark: 'c' a check when on, 'r' one of a group, the one that is;
// and a hint shown where a key would be (no key: a click).
static const struct { const char *label, *word; UiKey cmd; bool runs; char mark; const char *hint; } UI_MENU_ITEM[UM_ITEMS] = {
    [UM_SEP] = { "", "-" },
    [UM_NEW_TAB] = { "New Tab", "new-tab", .cmd = UK_TAB_NEW, .runs = true },
    [UM_NEW_WINDOW] = { "New Window", "new-window", .cmd = UK_NEW_WINDOW, .runs = true },
    [UM_NEW_FOLDER] = { "Create New Folder", "new-folder", .cmd = UK_NEWFOLDER, .runs = true },
    [UM_NEW_FILE] = { "Create New Empty File", "new-file", .cmd = UK_ITEM0 + UM_NEW_FILE },
    [UM_OPEN] = { "Open", "open", .cmd = UK_ENTER },
    [UM_OPEN_WITH] = { "Open With Other Application\xE2\x80\xA6", "open-with", .cmd = UK_SHIFT_ENTER },   // (its own action: with the keys in the sidebar Shift+Enter is Enter there)
    [UM_PROPERTIES] = { "Properties\xE2\x80\xA6", "properties", .cmd = UK_PROPERTIES, .runs = true },
    [UM_CLOSE_ALL] = { "Close All Windows", "close-all", .cmd = UK_QUIT_ALL, .runs = true },
    [UM_CLOSE] = { "Close", "close", .cmd = UK_TAB_CLOSE, .runs = true },
    // Edit
    [UM_UNDO] = { "Undo", "undo", .cmd = UK_UNDO, .runs = true },
    [UM_CUT] = { "Cut", "cut", .cmd = UK_CUT, .runs = true },
    [UM_COPY] = { "Copy", "copy", .cmd = UK_COPY, .runs = true },
    [UM_COPY_PATH] = { "Copy Path", "copy-path", .cmd = UK_COPY_PATH, .runs = true },   // (F13)
    [UM_PASTE] = { "Paste", "paste", .cmd = UK_PASTE, .runs = true },
    [UM_SELECT_ALL] = { "Select All", "select-all", .cmd = UK_ALL, .runs = true },
    [UM_SELECT_MATCH] = { "Select Items Matching\xE2\x80\xA6", "select-match", .cmd = UK_SELECT_MATCH, .runs = true },
    [UM_INVERT] = { "Invert Selection", "invert", .cmd = UK_INVERT, .runs = true },
    [UM_DUPLICATE] = { "Duplicate", "duplicate", .cmd = UK_ITEM0 + UM_DUPLICATE },
    [UM_MAKE_LINK] = { "Make Link", "make-link", .cmd = UK_MAKE_LINK, .runs = true },
    [UM_RENAME] = { "Rename\xE2\x80\xA6", "rename", .cmd = UK_F2, .runs = true },
    [UM_TRASH] = { "Move to Trash", "trash", .cmd = UK_DELETE, .runs = true },
    [UM_DELETE] = { "Delete\xE2\x80\xA6", "delete", .cmd = UK_SHIFT_DELETE, .runs = true },
    [UM_RESTORE] = { "Restore", "restore", .cmd = UK_RESTORE, .runs = true },
    // View
    [UM_RELOAD] = { "Reload", "reload", .cmd = UK_RELOAD, .runs = true },
    [UM_SIDE_PLACES] = { "Places Sidebar", "side-places", .cmd = UK_ITEM0 + UM_SIDE_PLACES, .mark = 'r' },
    [UM_SIDE_TREE] = { "Tree Sidebar", "side-tree", .cmd = UK_ITEM0 + UM_SIDE_TREE, .mark = 'r' },
    [UM_SIDE_NONE] = { "No Sidebar", "side-none", .cmd = UK_ITEM0 + UM_SIDE_NONE, .mark = 'r' },
    [UM_HIDDEN] = { "Show Hidden Files", "hidden", .cmd = UK_HIDDEN, .runs = true, .mark = 'c' },
    [UM_PERMS_COLS] = { "Show Permissions", "perms-cols", .cmd = UK_PERMS_COLS, .runs = true, .mark = 'c' },   // the list's Owner and Permissions (2026-10-27)
    [UM_GROUP_COL] = { "Show Group", "group-col", .cmd = UK_GROUP_COL, .runs = true, .mark = 'c' },   // and Group, off by default as eza's (Michael, 2026-10-27)
    [UM_COLORS] = { "Show Colors", "colors", .cmd = UK_ITEM0 + UM_COLORS, .mark = 'c' },   // the list in eza's colours (2026-10-27)
    [UM_SHOW_MENU] = { "Show Menu", "show-menu", .cmd = UK_ITEM0 + UM_SHOW_MENU, .mark = 'c' },   // the menu bar (2026-10-27): F10 and Alt+letters open its menus either way
    [UM_SORT_NAME] = { "Sort by Name", "sort-name", .cmd = UK_ITEM0 + UM_SORT_NAME, .mark = 'r' },
    [UM_SORT_SIZE] = { "Sort by Size", "sort-size", .cmd = UK_ITEM0 + UM_SORT_SIZE, .mark = 'r' },
    [UM_SORT_MTIME] = { "Sort by Modified", "sort-mtime", .cmd = UK_ITEM0 + UM_SORT_MTIME, .mark = 'r' },
    [UM_SORT_REVERSE] = { "Reverse Order", "sort-reverse", .cmd = UK_SORT_REVERSE, .runs = true, .mark = 'c' },
    [UM_ZOOM_IN] = { "Zoom In", "zoom-in", .cmd = UK_ZOOM_IN, .runs = true },
    [UM_ZOOM_OUT] = { "Zoom Out", "zoom-out", .cmd = UK_ZOOM_OUT, .runs = true },
    [UM_ZOOM_RESET] = { "Normal Size", "zoom-reset", .cmd = UK_ZOOM_RESET, .runs = true },
    [UM_VIEW_LIST] = { "List", "view-list", .cmd = UK_VIEW_LIST, .runs = true, .mark = 'r' },
    [UM_VIEW_ICONS] = { "Icons", "view-icons", .cmd = UK_VIEW_ICONS, .runs = true, .mark = 'r' },
    // Go
    [UM_PARENT] = { "Open Parent", "parent", .cmd = UK_PARENT, .runs = true },
    [UM_BACK] = { "Back", "back", .cmd = UK_BACK, .runs = true },
    [UM_FORWARD] = { "Forward", "forward", .cmd = UK_FORWARD, .runs = true },
    [UM_HOME] = { "Home", "home", .cmd = UK_HOMEDIR, .runs = true },
    [UM_TRASH_VIEW] = { "Trash", "trash-view", .cmd = UK_TRASH_VIEW, .runs = true },
    [UM_FIND] = { "Find in Folder\xE2\x80\xA6", "find", .cmd = UK_FIND, .runs = true },
    [UM_SEARCH] = { "Search\xE2\x80\xA6", "search", .cmd = UK_SEARCH, .runs = true },   // below the folder shown (F9)
    [UM_LOCATION] = { "Location\xE2\x80\xA6", "location", .cmd = UK_LOCATION, .runs = true },
    [UM_CONNECT] = { "Connect to Server\xE2\x80\xA6", "connect", .cmd = UK_ITEM0 + UM_CONNECT },   // the location line on "smb://" (F16 step 4)
    [UM_TERMINAL] = { "Terminal Here", "terminal", .cmd = UK_TERMINAL, .runs = true },
    // Bookmarks (F7.5); a bookmark's label is gui/'s (the file's), marked when it is the folder shown
    [UM_ADD_BOOKMARK] = { "Add Bookmark", "add-bookmark", .cmd = UK_ADD_BOOKMARK, .runs = true },
    [UM_EDIT_BOOKMARKS] = { "Edit Bookmarks\xE2\x80\xA6", "edit-bookmarks", .cmd = UK_EDIT_BOOKMARKS, .runs = true },
    [UM_BM_MORE] = { "", "bm-more" },   // "N more in Edit Bookmarks…" (gui/'s label): the panel, on the first not listed
    // Help: no manual (Michael, 2026-10-27)
    [UM_SHORTCUTS] = { "Keyboard Shortcuts", "shortcuts", .cmd = UK_SHORTCUTS, .runs = true },
    [UM_ABOUT] = { "About", "about", .cmd = UK_ITEM0 + UM_ABOUT },
    [UM_PREFERENCES] = { "Preferences\xE2\x80\xA6", "preferences", .cmd = UK_ITEM0 + UM_PREFERENCES },   // (ofx.conf, F10)
    [UM_CLEAR_PREVIEWS] = { "Clear Previews\xE2\x80\xA6", "clear-previews", .cmd = UK_ITEM0 + UM_CLEAR_PREVIEWS },   // (every preview in the cache, asked first: F12)
    // The right-click menus' own (F8): a folder in a tab behind (its row's middle click); the trash
    // view's Empty Trash; the folder shown's Properties, whatever is marked
    [UM_OPEN_TAB] = { "Open in New Tab", "open-tab", .hint = "Middle click" },
    [UM_EMPTY_TRASH] = { "Empty Trash\xE2\x80\xA6", "empty-trash" },
    [UM_HERE_PROPS] = { "Properties\xE2\x80\xA6", "here-properties" },
    // and on one folder of the files, that folder: a window, a paste into it, a terminal, a bookmark;
    // on a link, its target
    [UM_OPEN_WINDOW] = { "Open in New Window", "open-window" },
    [UM_PASTE_INTO] = { "Paste Into Folder", "paste-into" },
    [UM_TERMINAL_IN] = { "Open in Terminal", "terminal-in" },
    [UM_FOLLOW] = { "Follow Link", "follow" },
    // the search view's (F9): a result's folder, it selected; and a folder searched below
    [UM_CONTAINING] = { "Open Containing Folder", "containing", .cmd = UK_CONTAINING, .runs = true },
    [UM_OPEN_LINE] = { "Open at Line", "open-line", .cmd = UK_OPEN_LINE, .runs = true },   // gui/ adds "in <editor>" (F9 step 4)
    [UM_SEARCH_IN] = { "Search in Folder\xE2\x80\xA6", "search-in" },
    [UM_BOOKMARK_IT] = { "Add Bookmark", "bookmark-it" },
    // and on a folder elsewhere (a sidebar row, a path button): that folder; a bookmark's own, as
    // Edit Bookmarks has them; a drive's eject; the trash's emptying
    [UM_OPEN_IT] = { "Open", "open-it" },
    [UM_CUT_IT] = { "Cut", "cut-it" },
    [UM_COPY_IT] = { "Copy", "copy-it" },
    [UM_COPY_PATH_IT] = { "Copy Path", "copy-path-it" },
    [UM_COPY_PATH_HERE] = { "Copy Path", "copy-path-here" },   // (the files' empty space: the folder shown's)
    [UM_BM_RENAME_IT] = { "Rename\xE2\x80\xA6", "bm-rename" },
    [UM_BM_UP_IT] = { "Move Up", "bm-up" },
    [UM_BM_DOWN_IT] = { "Move Down", "bm-down" },
    [UM_BM_REMOVE_IT] = { "Remove", "bm-remove" },
    [UM_BM_EDIT_IT] = { "Edit Bookmarks\xE2\x80\xA6", "bm-edit", .cmd = UK_EDIT_BOOKMARKS },
    [UM_EJECT_IT] = { "Eject", "eject" },
    [UM_MOUNT_IT] = { "Mount", "mount-it" },
    [UM_UNMOUNT_IT] = { "Unmount", "unmount-it" },
    [UM_EMPTY_IT] = { "Empty Trash\xE2\x80\xA6", "empty-it" },
    [UM_PROPS_IT] = { "Properties\xE2\x80\xA6", "properties-it" },
    // and on a tab
    [UM_TAB_LEFT] = { "Move Tab Left", "tab-left", .cmd = UK_TAB_LEFT, .runs = true },
    [UM_TAB_RIGHT] = { "Move Tab Right", "tab-right", .cmd = UK_TAB_RIGHT, .runs = true },
    [UM_TAB_OTHERS] = { "Close Other Tabs", "tab-others", .cmd = UK_ITEM0 + UM_TAB_OTHERS },
    // The branch menu's (F12 step 5b): a note while there are none to list ("Reading branches…"),
    // then the branches, gui/'s labels; the one checked out marked.
    [UM_BR_NOTE] = { "", "br-note" },
    [UM_BR_ALL] = { "", "br-all" },   // ("All branches… (N)": gui/'s label, step 6)
    UI_SLOT_ITEMS(UM_BR_0, "br-"),
    [UM_TAB_REOPEN] = { "Reopen Closed Tab", "reopen-tab", .cmd = UK_TAB_REOPEN, .runs = true },
    UI_SLOT_ITEMS(UM_BM_0, "bm-"),
};

static const UiMenuItem UI_MENU_FILE[] = {
    UM_NEW_TAB, UM_TAB_REOPEN, UM_NEW_WINDOW, UM_NEW_FOLDER, UM_NEW_FILE, UM_SEP, UM_OPEN, UM_OPEN_WITH, UM_SEP, UM_PROPERTIES, UM_SEP,
    UM_CLOSE_ALL, UM_CLOSE,
};
// Move to Trash shows outside the trash view, Restore in it (ui_menu_shown).
static const UiMenuItem UI_MENU_EDIT[] = {
    UM_UNDO, UM_SEP, UM_CUT, UM_COPY, UM_COPY_PATH, UM_PASTE, UM_SEP, UM_SELECT_ALL, UM_SELECT_MATCH, UM_INVERT, UM_SEP, UM_DUPLICATE, UM_MAKE_LINK, UM_RENAME, UM_SEP,
    UM_TRASH, UM_RESTORE, UM_DELETE, UM_SEP, UM_PREFERENCES,
};
static const UiMenuItem UI_MENU_VIEW[] = {
    UM_RELOAD, UM_SEP, UM_SIDE_PLACES, UM_SIDE_TREE, UM_SIDE_NONE, UM_SEP, UM_HIDDEN, UM_PERMS_COLS, UM_GROUP_COL, UM_COLORS, UM_SHOW_MENU, UM_SEP,
    UM_SORT_NAME, UM_SORT_SIZE, UM_SORT_MTIME, UM_SORT_REVERSE, UM_SEP, UM_ZOOM_IN, UM_ZOOM_OUT, UM_ZOOM_RESET, UM_SEP,
    UM_VIEW_LIST, UM_VIEW_ICONS, UM_SEP, UM_CLEAR_PREVIEWS,
};
static const UiMenuItem UI_MENU_GO[] = {
    UM_PARENT, UM_BACK, UM_FORWARD, UM_SEP, UM_HOME, UM_TRASH_VIEW, UM_SEP, UM_FIND, UM_SEARCH, UM_LOCATION, UM_CONNECT, UM_TERMINAL,
};
static const UiMenuItem UI_MENU_BOOKMARKS_[] = {
    UM_ADD_BOOKMARK, UM_EDIT_BOOKMARKS, UM_SEP, UM_BM_0, UM_BM_1, UM_BM_2, UM_BM_3, UM_BM_4, UM_BM_5, UM_BM_6, UM_BM_7, UM_BM_MORE,
};

static const UiMenuItem UI_MENU_HELP[] = { UM_SHORTCUTS, UM_SEP, UM_ABOUT };
// The right-click menus (F8, docs/plan.md "F8 design"): an entry of the files (the marks, or the
// cursor's), and the files' empty space (the folder shown).
static const UiMenuItem UI_MENU_CTX_ENTRY[] = {
    UM_OPEN, UM_CONTAINING, UM_OPEN_LINE, UM_OPEN_TAB, UM_OPEN_WINDOW, UM_OPEN_WITH, UM_SEP, UM_CUT, UM_COPY, UM_COPY_PATH, UM_PASTE_INTO, UM_DUPLICATE, UM_SEP,
    UM_MAKE_LINK, UM_RENAME, UM_SEP, UM_TERMINAL_IN, UM_SEARCH_IN, UM_FOLLOW, UM_BOOKMARK_IT, UM_SEP, UM_TRASH, UM_RESTORE, UM_DELETE, UM_SEP,
    UM_PROPERTIES,
};
// A folder elsewhere: a tree row, a path button, a place (Trash, a bookmark, a drive have their own).
static const UiMenuItem UI_MENU_CTX_FOLDER[] = {
    UM_OPEN_IT, UM_OPEN_TAB, UM_OPEN_WINDOW, UM_TERMINAL_IN, UM_SEARCH_IN, UM_SEP, UM_CUT_IT, UM_COPY_IT, UM_COPY_PATH_IT, UM_PASTE_INTO, UM_SEP, UM_BOOKMARK_IT,
    UM_BM_RENAME_IT, UM_BM_UP_IT, UM_BM_DOWN_IT, UM_BM_REMOVE_IT, UM_BM_EDIT_IT, UM_SEP, UM_UNMOUNT_IT, UM_EJECT_IT, UM_MOUNT_IT, UM_EMPTY_IT, UM_SEP, UM_PROPS_IT,
};
// A tab (the current one: a right press on another shows it first).
static const UiMenuItem UI_MENU_CTX_TAB[] = { UM_NEW_TAB, UM_TAB_REOPEN, UM_SEP, UM_TAB_LEFT, UM_TAB_RIGHT, UM_SEP, UM_CLOSE, UM_TAB_OTHERS };
// The branch button's (F12 step 5b).
static const UiMenuItem UI_MENU_CTX_BRANCH[] = { UM_BR_NOTE, UM_BR_0, UM_BR_1, UM_BR_2, UM_BR_3, UM_BR_4, UM_BR_5, UM_BR_6, UM_BR_7, UM_SEP, UM_BR_ALL };
static const UiMenuItem UI_MENU_CTX_HERE[] = {
    UM_NEW_FOLDER, UM_NEW_FILE, UM_EMPTY_TRASH, UM_SEP, UM_PASTE, UM_COPY_PATH_HERE, UM_SELECT_ALL, UM_SEP, UM_TERMINAL, UM_HIDDEN, UM_SEP,
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
    { "branch", 0, UI_MENU_CTX_BRANCH, sizeof UI_MENU_CTX_BRANCH / sizeof UI_MENU_CTX_BRANCH[0] },
};
// The bar's menus come first, then the right-click ones.
enum { UI_MENUS = 6, UI_CTX_ENTRY = UI_MENUS, UI_CTX_HERE, UI_CTX_FOLDER, UI_CTX_TAB, UI_CTX_BRANCH, UI_MENUS_ALL };
_Static_assert(sizeof UI_MENU / sizeof UI_MENU[0] == UI_MENUS_ALL, "UI_MENU: the bar's menus, then the right-click ones");

// ---- the Keyboard Shortcuts panel (F7.5, docs/plan.md "F7.5 decided") ----------------------------

// Its sections, by where the keys work, and its rows: an action, its key, a second key ("" none).
// A command's row (each command has one, F15 step 3) shows its keys from ui/keys.c, yours marked,
// and its name in ofx.conf (key NULL; key2 NULL its second key, else what the row says: a click);
// the rest are the fixed keys.
// A menu entry with a key has a row of the same name and key (test_f75.c checks).
typedef enum {
    KS_MOVE, KS_OPEN, KS_SELECT, KS_CHANGE, KS_TABS, KS_VIEW, KS_SIDE, KS_CRUMBS, KS_FIND, KS_SEARCH, KS_EDIT, KS_JOBS, KS_OW, KS_BM,
    KS_SECTIONS
} UiKeySection;

static const char *const UI_KEY_SECTION[KS_SECTIONS] = {
    "Moving", "Opening", "Selecting", "Changing files", "Tabs and windows", "View", "The sidebar", "The path buttons",
    "The find bar", "Search below the folder", "Text fields (rename, location, find, search, filters)", "The job rows", "Open With", "Edit Bookmarks",
};

static const struct { UiKeySection sec; const char *action; UiKey cmd; const char *key, *key2; } UI_KEYS[] = {
    { KS_MOVE, "Up", UK_UP, NULL, NULL },
    { KS_MOVE, "Down", UK_DOWN, NULL, NULL },
    { KS_MOVE, "Left (in the tree: close)", UK_LEFT, NULL, NULL },
    { KS_MOVE, "Right (in the tree: open)", UK_RIGHT, NULL, NULL },
    { KS_MOVE, "A page up", UK_PAGEUP, NULL, NULL },
    { KS_MOVE, "A page down", UK_PAGEDOWN, NULL, NULL },
    { KS_MOVE, "The first", UK_HOME, NULL, NULL },
    { KS_MOVE, "The last", UK_END, NULL, NULL },
    { KS_MOVE, "Back", UK_BACK, NULL, "Mouse back" },
    { KS_MOVE, "Forward", UK_FORWARD, NULL, "Mouse forward" },
    { KS_MOVE, "Open Parent", UK_PARENT, NULL, NULL },
    { KS_MOVE, "Home", UK_HOMEDIR, NULL, NULL },
    { KS_MOVE, "Trash", UK_TRASH_VIEW, NULL, NULL },
    { KS_MOVE, "Location\xE2\x80\xA6", UK_LOCATION, NULL, NULL },
    { KS_MOVE, "Connect to Server\xE2\x80\xA6", UK_ITEM0 + UM_CONNECT, NULL, NULL },
    { KS_OPEN, "Open", UK_ENTER, NULL, NULL },
    { KS_OPEN, "Open With Other Application\xE2\x80\xA6", UK_SHIFT_ENTER, NULL, NULL },
    { KS_OPEN, "Terminal Here", UK_TERMINAL, NULL, NULL },
    { KS_OPEN, "Properties\xE2\x80\xA6", UK_PROPERTIES, NULL, NULL },
    { KS_SELECT, "Select the entry, and on", UK_MARK, NULL, NULL },
    { KS_SELECT, "Select up", UK_SEL_UP, NULL, NULL },
    { KS_SELECT, "Select down", UK_SEL_DOWN, NULL, NULL },
    { KS_SELECT, "Select left", UK_SEL_LEFT, NULL, NULL },
    { KS_SELECT, "Select right", UK_SEL_RIGHT, NULL, NULL },
    { KS_SELECT, "Select to the first", UK_SEL_HOME, NULL, NULL },
    { KS_SELECT, "Select to the last", UK_SEL_END, NULL, NULL },
    { KS_SELECT, "Select All", UK_ALL, NULL, NULL },
    { KS_SELECT, "Select Items Matching\xE2\x80\xA6", UK_SELECT_MATCH, NULL, NULL },
    { KS_SELECT, "Invert Selection", UK_INVERT, NULL, NULL },
    { KS_SELECT, "Clear the selection", UK_ESC, NULL, NULL },
    { KS_CHANGE, "Cut", UK_CUT, NULL, NULL },
    { KS_CHANGE, "Copy", UK_COPY, NULL, NULL },
    { KS_CHANGE, "Copy Path", UK_COPY_PATH, NULL, NULL },
    { KS_CHANGE, "Paste", UK_PASTE, NULL, NULL },
    { KS_CHANGE, "Undo", UK_UNDO, NULL, NULL },
    { KS_CHANGE, "Make Link", UK_MAKE_LINK, NULL, NULL },
    { KS_CHANGE, "Rename\xE2\x80\xA6", UK_F2, NULL, NULL },
    { KS_CHANGE, "Create New Folder", UK_NEWFOLDER, NULL, NULL },
    { KS_CHANGE, "Create New Empty File", UK_ITEM0 + UM_NEW_FILE, NULL, NULL },
    { KS_CHANGE, "Duplicate", UK_ITEM0 + UM_DUPLICATE, NULL, NULL },
    { KS_CHANGE, "Move to Trash", UK_DELETE, NULL, NULL },
    { KS_CHANGE, "Delete\xE2\x80\xA6", UK_SHIFT_DELETE, NULL, NULL },
    { KS_CHANGE, "Restore", UK_RESTORE, NULL, NULL },
    { KS_CHANGE, "Add Bookmark", UK_ADD_BOOKMARK, NULL, NULL },
    { KS_CHANGE, "Edit Bookmarks\xE2\x80\xA6", UK_EDIT_BOOKMARKS, NULL, NULL },
    { KS_CHANGE, "A clash: skip, replace, keep both", UK_NONE, "s, r, k", "S, R, K the rest" },
    { KS_TABS, "New Tab", UK_TAB_NEW, NULL, NULL },
    { KS_TABS, "Reopen Closed Tab", UK_TAB_REOPEN, NULL, NULL },
    { KS_TABS, "New Window", UK_NEW_WINDOW, NULL, NULL },
    { KS_TABS, "Close", UK_TAB_CLOSE, NULL, NULL },
    { KS_TABS, "The next tab", UK_TAB_NEXT, NULL, NULL },
    { KS_TABS, "The previous tab", UK_TAB_PREV, NULL, NULL },
    { KS_TABS, "Move Tab Left", UK_TAB_LEFT, NULL, NULL },
    { KS_TABS, "Move Tab Right", UK_TAB_RIGHT, NULL, NULL },
    { KS_TABS, "A tab by its place", UK_NONE, "Alt+1 " "\xE2\x80\xA6" " Alt+9", "" },
    { KS_TABS, "Open in New Tab", UK_NONE, "Middle click", "" },
    { KS_TABS, "Close Other Tabs", UK_ITEM0 + UM_TAB_OTHERS, NULL, NULL },
    { KS_TABS, "Close All Windows", UK_QUIT_ALL, NULL, NULL },
    { KS_VIEW, "The menus", UK_F10, NULL, "Alt+F, E, V, G, B, H" },
    { KS_VIEW, "The right-click menu", UK_CONTEXT, NULL, NULL },
    { KS_VIEW, "Keyboard Shortcuts", UK_SHORTCUTS, NULL, NULL },
    { KS_VIEW, "Reload", UK_RELOAD, NULL, NULL },
    { KS_VIEW, "Show Hidden Files", UK_HIDDEN, NULL, NULL },
    { KS_VIEW, "Show Permissions", UK_PERMS_COLS, NULL, NULL },
    { KS_VIEW, "Show Group", UK_GROUP_COL, NULL, NULL },
    { KS_VIEW, "Sort by the next column", UK_SORT_NEXT, NULL, "a column's header" },
    { KS_VIEW, "Sort by Name", UK_ITEM0 + UM_SORT_NAME, NULL, NULL },
    { KS_VIEW, "Sort by Size", UK_ITEM0 + UM_SORT_SIZE, NULL, NULL },
    { KS_VIEW, "Sort by Modified", UK_ITEM0 + UM_SORT_MTIME, NULL, NULL },
    { KS_VIEW, "Reverse Order", UK_SORT_REVERSE, NULL, NULL },
    { KS_VIEW, "Show Colors", UK_ITEM0 + UM_COLORS, NULL, NULL },
    { KS_VIEW, "Show Menu", UK_ITEM0 + UM_SHOW_MENU, NULL, NULL },
    { KS_VIEW, "List", UK_VIEW_LIST, NULL, "" },
    { KS_VIEW, "Icons", UK_VIEW_ICONS, NULL, "" },
    { KS_VIEW, "Zoom In", UK_ZOOM_IN, NULL, "Ctrl+wheel" },
    { KS_VIEW, "Zoom Out", UK_ZOOM_OUT, NULL, "" },
    { KS_VIEW, "Normal Size", UK_ZOOM_RESET, NULL, "" },
    { KS_VIEW, "Clear Previews\xE2\x80\xA6", UK_ITEM0 + UM_CLEAR_PREVIEWS, NULL, NULL },
    { KS_VIEW, "Preferences\xE2\x80\xA6", UK_ITEM0 + UM_PREFERENCES, NULL, NULL },
    { KS_VIEW, "About", UK_ITEM0 + UM_ABOUT, NULL, NULL },
    { KS_SIDE, "To the sidebar and back", UK_TAB, NULL, NULL },
    { KS_SIDE, "To the sidebar", UK_SIDE_GO, NULL, NULL },
    { KS_SIDE, "The sidebar's next mode", UK_F9, NULL, NULL },
    { KS_SIDE, "Places Sidebar", UK_ITEM0 + UM_SIDE_PLACES, NULL, NULL },
    { KS_SIDE, "Tree Sidebar", UK_ITEM0 + UM_SIDE_TREE, NULL, NULL },
    { KS_SIDE, "No Sidebar", UK_ITEM0 + UM_SIDE_NONE, NULL, NULL },
    { KS_SIDE, "Go there", UK_NONE, "Enter", "" },
    { KS_CRUMBS, "To the path buttons (in a search, its field first)", UK_CRUMBS, NULL, NULL },
    { KS_CRUMBS, "The next button", UK_NONE, "Left, Right", "h, l" },
    { KS_CRUMBS, "Go there", UK_NONE, "Enter", "" },
    { KS_CRUMBS, "Open it in a tab behind", UK_NONE, "Shift+Enter", "" },
    { KS_CRUMBS, "Back to the files", UK_NONE, "Esc", "" },
    { KS_FIND, "Find in Folder\xE2\x80\xA6", UK_FIND, NULL, NULL },
    { KS_FIND, "The previous, next match", UK_NONE, "Up, Down", "the chevrons" },
    { KS_FIND, "Keep it, the keys to the files", UK_NONE, "Enter", "" },
    { KS_FIND, "Close it", UK_NONE, "Esc", "" },
    { KS_FIND, "Find it again", UK_FIND_NEXT, NULL, NULL },
    { KS_FIND, "Find it back", UK_FIND_PREV, NULL, NULL },
    { KS_FIND, "Match case", UK_NONE, "Alt+C", "" },
    { KS_FIND, "Whole words", UK_NONE, "Alt+W", "" },
    { KS_FIND, "A regular expression", UK_NONE, "Alt+R", "" },
    { KS_SEARCH, "Search\xE2\x80\xA6", UK_SEARCH, NULL, "the magnifier" },
    { KS_SEARCH, "Open Containing Folder", UK_CONTAINING, NULL, NULL },
    { KS_SEARCH, "Open at Line", UK_OPEN_LINE, NULL, "in the editor" },   // (as the menu entry's name: test_f75.c)
    { KS_SEARCH, "The keys to the results", UK_NONE, "Enter", "Esc, Down" },
    { KS_SEARCH, "Back to the search field", UK_SEARCH, NULL, "a click in it" },
    { KS_SEARCH, "A wildcard over the whole name", UK_NONE, "*, ?", "*.ts" },
    { KS_SEARCH, "Match case, whole words, a regular expression", UK_NONE, "Alt+C, W, R", "as find's" },
    { KS_SEARCH, "In files: a Containing field too", UK_NONE, "Alt+I", "the In files button" },
    { KS_SEARCH, "The other field, in files", UK_NONE, "Tab", "" },
    { KS_EDIT, "Select", UK_NONE, "Shift+Left, Right", "Shift+Home, End" },
    { KS_EDIT, "A word left, right", UK_NONE, "Ctrl+Left, Right", "" },
    { KS_EDIT, "Select a word", UK_NONE, "Ctrl+Shift+Left, Right", "" },
    { KS_EDIT, "Delete a word", UK_NONE, "Ctrl+Backspace", "Ctrl+Delete" },
    { KS_EDIT, "All of it", UK_NONE, "Ctrl+A", "" },
    { KS_EDIT, "Copy, cut, paste text", UK_NONE, "Ctrl+C, X, V", "" },
    { KS_EDIT, "Done", UK_NONE, "Enter", "" },
    { KS_EDIT, "Leave it as it was", UK_NONE, "Esc", "" },
    { KS_JOBS, "To the job rows", UK_JOBS, NULL, NULL },
    { KS_JOBS, "The next row", UK_NONE, "Up, Down", "k, j" },
    { KS_JOBS, "Cancel that job", UK_NONE, "Enter", "its \xC3\x97" },
    { KS_JOBS, "Back to the files", UK_JOBS, "Esc", NULL },   // (Esc, then the job rows' own key as bound)
    { KS_OW, "The field, the list", UK_NONE, "Tab", "Down" },
    { KS_OW, "Always open with this app", UK_NONE, "Alt+D", "" },
    { KS_OW, "Open", UK_NONE, "Enter", "a double click" },
    { KS_BM, "Remove", UK_NONE, "Delete", "" },
    { KS_BM, "Rename", UK_NONE, "F2", "" },
    { KS_BM, "Move it up, down", UK_NONE, "Alt+Up, Alt+Down", "" },
    { KS_BM, "Go there", UK_NONE, "Enter", "a double click" },
};
enum { UI_KEYS_N = sizeof UI_KEYS / sizeof UI_KEYS[0], UI_KEYS_LINES = UI_KEYS_N + KS_SECTIONS };


// The icon view's sizes (F6.3, Michael 2026-10-24): Yaru's 32 and 48, and its 256 scaled down
// between them. The list doesn't zoom.
#define UI_ZOOM_PX CONF_ZOOMS   // (fs/conf.c's, which ofx.conf's zoom is one of: written once, the F15 code-health review)
enum { UI_ZOOMS = COUNT(CONF_ZOOMS), UI_ZOOM_DEFAULT = CONF_ZOOM };

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

// A picker: a panel's filter field over a list of what it matches (Open With's apps, F7.3; the
// branches, F12 step 6). The keys in the list or the field; fresh until filtered from gui/'s names
// (panels.c, ui_pick_*).
typedef struct {
    bool   in_list;
    bool   fresh;
    UI_TEXT(UI_FIND_MAX + 1) filter;
    UiMatcher m;
    UiList list;
} UiPick;

// The panel up, under the questions (ui_on_top): Properties, Open With, Edit Bookmarks, Keyboard
// Shortcuts, About, the note that the menu bar is hidden, the branches, and Connect's question (F16
// step 4); UI_PANEL_NONE for none.
typedef enum { UI_PANEL_NONE, UI_PANEL_PROPS, UI_PANEL_OW, UI_PANEL_BM, UI_PANEL_KEYS, UI_PANEL_ABOUT, UI_PANEL_BAR, UI_PANEL_BRANCHES, UI_PANEL_LOGIN, UI_PANEL_SHARES, UI_PANELS } UiPanel;

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
    // Shift's selections (F12, ofx.conf's shift-select): range, from the anchor (where the last
    // plain move or click left the cursor; -1: where the next Shift starts) to the cursor, in place
    // of the marks, as Nemo and GTK do; shift_add, everything the cursor passes over joins them.
    // range_end: the cursor where the last range left it; moved since by anything else (a find,
    // the pointer, gui/), the anchor is where the next Shift starts.
    isize  anchor, range_end;
    bool   shift_add;
    // The edit line: a rename (a new folder is made at once and renamed with it), or the top bar's
    // location line (F4), which holds a path. Open while focus is UI_FOCUS_EDIT.
    bool   edit_path;        // the location line
    UI_TEXT(PATH_MAX) edit;  // (its own most: a name's or a path's, ui_edit_max)
    // A Ctrl+C or Ctrl+X in the edit line: the text gui/ puts on the clipboard, then clears copied.
    bool   copied;
    char   copy[PATH_MAX];
    int    copy_len;
    // The keyboard on the path buttons (Ctrl+K, F5.2): the one marked; crumb_n (one past them) is the
    // branch button while there is one (F12 step 5c).
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
    bool   clear_asking;     // "Delete all N previews?" up (View → Clear Previews…, F12)
    bool   clearing;         // gui/ says ofx-clean --all runs: Clear Previews… waits
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
    UiFocus menu_field;   // the search view's field it took the keys from (Esc gives them back), else LIST
    bool   can_write;
    // What only gui/ knows, for the Edit, View and Go entries it greys or marks (F7.4), set each
    // frame: an undo to make, history back and forward, a folder above, files to paste, hidden
    // files shown.
    bool   can_undo, can_back, can_forward, can_up, can_paste, shows_hidden;
    bool   perms_cols;   // the list's Owner and Permissions columns shown (gui/ reads it from the state)
    bool   group_col;    // and Group between them (View → Show Group, 2026-10-27; only with perms_cols)
    // The menu bar hidden (View → Show Menu unchecked, 2026-10-27; gui/ reads it from the state); the
    // panel that says how its menus still open is UI_PANEL_BAR.
    bool   bar_hidden;
    bool   colors;   // the list in eza's colours (View → Show Colors; gui/ reads it from the state)
    UiKey  menu_cmd;   // an entry picked whose command is still to run (ui_handle runs it next)
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
    bool   ctx_unmounted;   // a drive not mounted yet (2026-10-30), or a network bookmark (F16 step 5): Mount is all its menu has
    bool   ctx_share;       // a share gvfs has mounted (F16 step 5): Unmount, no Eject
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
    // The branch menu (F12 step 5b), from gui/ too: whether the button is there (a folder in a
    // repository), how many branches it lists (at most UI_MENU_BRANCHES), the one checked out (-1
    // none), and whether a note shows instead (reading them, or none found).
    bool   branch_on, br_note;
    int    br_n, br_here;
    int    br_total;   // all of them (the panel's; "All branches… (N)" when more than the menu's 8, step 6)
    // The panel up, one at most (F11 review, 2026-11-02: six flags before, kept apart by hand).
    UiPanel panel;
    // Edit Bookmarks (F7.5, UI_PANEL_BM): its list (the file's lines); its rename field open, the
    // text and the caret (bytes).
    UiList bmp_list;
    bool   bmp_naming;
    UI_TEXT(256) bmp_name;
    // Keyboard Shortcuts (F7.5, UI_PANEL_KEYS): its filter's text; the lines it shows (a row of
    // UI_KEYS, or -1 - a section for its heading), scrolled by keys_list.top.
    UI_TEXT(64) keys_filter;
    short  keys_row[UI_KEYS_LINES];
    UiList keys_list;
    UiMatcher keys_m;
    // The Properties panel (F7.2, UI_PANEL_PROPS): its grid's place with the focus (0..8: owner
    // read, write, execute, then group's, others'), and whether gui/ lets its permissions change.
    int    prop_at;
    bool   prop_can;
    // Open With (F7.3, UI_PANEL_OW): its picker; the toggles; the apps the filter shows (their places
    // in gui/'s list).
    UiPick ow;
    UiMatchOpts ow_opts;
    isize  ow_idx[UI_APPS_MAX];
    // Set as default (F7.3.1): the box ticked, and whether gui/ lets it be (an empty file's type,
    // or one a list read first sets, can't).
    bool   ow_def;
    bool   ow_def_can;
    // The branches panel (F12 step 6, UI_PANEL_BRANCHES): its picker, and the branches the filter
    // shows (their places in gui/'s list).
    UiPick brp;
    isize  brp_idx[UI_BRANCHES_MAX];
    // A server's shares (F16 step 7a, UI_PANEL_SHARES, Michael 2026-11-10: a picker): its picker,
    // and the shares the filter shows (their places in gui/'s list).
    UiPick shp;
    isize  shp_idx[UI_SHARES_MAX];
    // Connect to Server's question (F16 step 4, UI_PANEL_LOGIN, Michael 2026-11-09: asked each time,
    // with a Guest button): its fields, the one with the keys (UI_LOGIN_*), whether gio asks for a
    // user (not when the address names one), and whether it asks again (the last was refused). gui/
    // fills them as gio asks. The password is never shown, copied or kept: gui/ wipes it as it hands
    // it on, and when the panel goes.
    UI_TEXT(256) login_user;
    UI_TEXT(256) login_domain;
    UI_TEXT(256) login_pass;
    int    login_at;
    bool   login_no_user, login_again;
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
    UI_ON_CLEAR,    // clear_asking: Clear Previews (F12)
    UI_ON_RESTORE,  // restore_n: the other windows after Close All (F11)
    UI_ON_CLASH,    // asking: a job waits for a clash answer
    UI_ON_PROPS,
    UI_ON_OW,
    UI_ON_BM,       // Edit Bookmarks
    UI_ON_KEYS,     // Keyboard Shortcuts
    UI_ON_ABOUT,
    UI_ON_BAR,      // panel == UI_PANEL_BAR: the menu bar just hidden
    UI_ON_BRANCHES, // the branches panel (F12 step 6)
    UI_ON_LOGIN,    // Connect to Server's question: user, domain, password (F16 step 4)
    UI_ON_SHARES,   // a server's shares (F16 step 7a)
    UI_ON_KINDS     // (their count: tests/test_f14.c checks each has an owner for its keys)
} UiOnTop;

// Whether an action works in view v. The trash view holds files that are nowhere yet: what would
// open, copy, link or make from them, or go from there, waits until they are restored. A search's
// results are in many folders: what needs one folder to put something in (paste, a new folder, a
// bookmark of it) doesn't work there (F9). Restoring and emptying are the trash view's own.
static bool ui_view_allows(UiView v, UiActionKind k)
{
    switch (k) {
    case UA_COPY: case UA_COPY_PATH: case UA_CUT: case UA_MAKE_LINK: case UA_TERMINAL: case UA_NEW_WINDOW: case UA_SEARCH:
    case UA_RENAME: case UA_DUPLICATE: case UA_OPEN_WITH: return v != UI_VIEW_TRASH;
    case UA_PASTE: case UA_NEWFOLDER: case UA_NEWFILE: case UA_ADD_BOOKMARK: return v == UI_VIEW_FOLDER;
    case UA_RESTORE: case UA_EMPTY_TRASH: return v == UI_VIEW_TRASH;
    case UA_CONTAINING: return v == UI_VIEW_SEARCH;
    default: return true;
    }
}

// Whether kind writes in the folder shown, which must then be writable (Ui.can_write): paste and
// the new entries; a rename, a duplicate, a link, a trash, a delete and a cut (its move deletes)
// too, except in a search, where each is in its result's own folder (F9), and a delete in the
// trash view, which is the trash's (step 3 review, 2026-11-03).
static bool ui_view_writes(UiView v, UiActionKind k)
{
    switch (k) {
    case UA_PASTE: case UA_NEWFOLDER: case UA_NEWFILE: return true;
    case UA_RENAME: case UA_DUPLICATE: case UA_MAKE_LINK: case UA_TRASH: case UA_CUT: return v != UI_VIEW_SEARCH;
    case UA_DELETE: return v == UI_VIEW_FOLDER;   // (the trash view's is in the trash; a search's in each result's folder)
    default: return false;
    }
}

// Whether kind can be done now: the view allows it, and it writes only where it can. The menus
// grey by it and the keys refuse by it (ui_view_act), so the two can't differ (code-health review,
// 2026-11-03: Rename, Make Link and Paste were greyed in a folder that can't be written, while
// their keys went on and failed there).
static bool ui_allows(const Ui *u, UiActionKind k)
{
    return ui_view_allows(u->view, k) && (u->can_write || !ui_view_writes(u->view, k));
}

// a, or why it can't be done now (gui/ says so in the status line).
static UiAction ui_view_act(const Ui *u, UiAction a)
{
    if (ui_allows(u, a.kind)) return a;
    if (ui_view_allows(u->view, a.kind)) return (UiAction){ UA_READ_ONLY, 0, false };
    return (UiAction){ u->view == UI_VIEW_TRASH ? UA_NOT_IN_TRASH : u->view == UI_VIEW_SEARCH ? UA_NOT_IN_SEARCH : UA_NONE, 0, false };   // (a folder's: nothing to say)
}

// h, j, k and l as Left, Down, Up and Right (vi's, Michael 2026-10-21) where they are fixed: the
// menus, the path buttons, the job rows and the panels' lists (Michael, 2026-11-08: the files'
// moves are bindings, ui/keys.c, these aren't); any other event as it is. (A place that doesn't
// move that way ignores the arrow, as it ignored the letter.)
static UiEvent ui_vi_key(UiEvent e)
{
    if (e.key != UK_NONE) return e;
    switch (e.ch) {
    case 'h': e.key = UK_LEFT; break;
    case 'j': e.key = UK_DOWN; break;
    case 'k': e.key = UK_UP; break;
    case 'l': e.key = UK_RIGHT; break;
    default: break;
    }
    return e;
}

// A Shift move (UK_SEL_*) as its plain key; any other as it is.
static UiKey ui_unshift(UiKey k)
{
    switch (k) {
    case UK_SEL_LEFT: return UK_LEFT;
    case UK_SEL_RIGHT: return UK_RIGHT;
    case UK_SEL_HOME: return UK_HOME;
    case UK_SEL_END: return UK_END;
    case UK_SEL_UP: return UK_UP;
    case UK_SEL_DOWN: return UK_DOWN;
    default: return k;
    }
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
    if (u->clear_asking) return UI_ON_CLEAR;
    if (u->restore_n > 0) return UI_ON_RESTORE;
    if (u->asking) return UI_ON_CLASH;
    static const UiOnTop OF_PANEL[] = { [UI_PANEL_NONE] = UI_ON_NONE, [UI_PANEL_PROPS] = UI_ON_PROPS, [UI_PANEL_OW] = UI_ON_OW,
                                         [UI_PANEL_BM] = UI_ON_BM, [UI_PANEL_KEYS] = UI_ON_KEYS, [UI_PANEL_ABOUT] = UI_ON_ABOUT,
                                         [UI_PANEL_BAR] = UI_ON_BAR, [UI_PANEL_BRANCHES] = UI_ON_BRANCHES,
                                         [UI_PANEL_LOGIN] = UI_ON_LOGIN, [UI_PANEL_SHARES] = UI_ON_SHARES };
    _Static_assert(COUNT(OF_PANEL) == UI_PANELS, "every panel has its UiOnTop: a new one at the end without its entry would read past the table");
    return OF_PANEL[u->panel];
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

// Panel p closed, if it is the one up.
static void ui_panel_close(Ui *u, UiPanel p)
{
    if (u->panel == p) u->panel = UI_PANEL_NONE;
}

static UiInput ui_input_none(void)
{
    return (UiInput){ .click = -1, .dclick = -1, .side_click = -1, .header = -1, .click_mods = 0, .bar = UB_NONE, .crumb = -1,
                      .tab_click = -1, .tab_close = -1, .mid_row = -1, .mid_side = -1, .mid_crumb = -1, .modal = 0, .job_x = -1, .side_mode = -1, .side_twisty = -1, .side_eject = -1,
                      .zoom_to = -1, .menu_title = -1, .menu_item = -1, .ctx_row = -1, .ctx_side = -1, .ctx_crumb = -1, .ctx_tab = -1, .prop_box = -1, .pick_row = -1, .pick_drow = -1, .bmp_row = -1, .bmp_drow = -1,
                      .field = -1, .login_field = -1 };
}

#include "keys.c"
#include "cursor.c"
#include "match.c"
#include "find.c"
#include "edit.c"
#include "prompts.c"
#include "panels.c"
#include "menus.c"
#include "handle.c"
#include "helpers.c"
