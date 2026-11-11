// The menu bar (F7.1) and the right-click menus (F8): what is on them, what can be picked, the keys. Included by ui.c.

// ---- the menu bar (F7.1) -----------------------------------------------------------------------

static bool ui_folder_shown(const Ui *u, UiMenuItem it);

// Whether entry it is in its menu now: Move to Trash outside the trash view, Restore in it (F7.4); a
// bookmark's place while there is one for it (F7.5).
static bool ui_menu_shown(const Ui *u, UiMenuItem it)
{
    if (u->menu == UI_CTX_FOLDER) return ui_folder_shown(u, it);
    int k;
    if ((k = ui_slot(it, UM_BM_0, UI_MENU_BOOKMARKS)) >= 0) return k < u->bm_n;
    if ((k = ui_slot(it, UM_BR_0, UI_MENU_BRANCHES)) >= 0) return k < u->br_n;
    // A right-click menu leaves out what doesn't fit its target (F8): a tab for a folder only, a
    // rename for one entry, the new entries outside the trash view (Empty Trash in it).
    bool ctx = u->menu >= UI_MENUS, trash = u->view == UI_VIEW_TRASH;
    // (a row of Browse Network's: a server or a share, no folder yet; its menu has what its view
    // allows, F16 step 7e's code-health review: a pick from a menu isn't checked again)
    if (u->menu == UI_CTX_ENTRY && u->view == UI_VIEW_NETWORK) return it == UM_SEP || it == UM_OPEN || it == UM_OPEN_TAB || it == UM_COPY_PATH;
    switch (it) {
    case UM_BM_MORE: return u->bm_more > 0;
    case UM_BR_NOTE: return u->br_note;
    case UM_BR_ALL: return u->br_total > UI_MENU_BRANCHES;   // (more than the menu shows: F12 step 6)
    case UM_OPEN_TAB: return !ctx || (u->ctx_dir && !trash);
    case UM_RENAME: return !ctx || u->marked <= 1;
    case UM_NEW_FOLDER: case UM_NEW_FILE: return !ctx || !trash;
    case UM_OPEN_WINDOW: case UM_PASTE_INTO: case UM_TERMINAL_IN: case UM_SEARCH_IN: return u->ctx_dir && !trash;
    case UM_CONTAINING: return ui_view_allows(u->view, UA_CONTAINING);
    case UM_OPEN_LINE: return u->ctx_one && u->sel_hit;   // (one result found inside files)
    case UM_BOOKMARK_IT: return u->ctx_dir && !trash && !u->sel_bm;
    case UM_FOLLOW: return u->ctx_one && u->sel_link && !trash;
    case UM_EMPTY_TRASH: return ui_view_allows(u->view, UA_EMPTY_TRASH);
    case UM_COPY_PATH_HERE: return ui_view_allows(u->view, UA_COPY_PATH);
    case UM_TRASH: return !trash;
    case UM_RESTORE: return ui_view_allows(u->view, UA_RESTORE);
    default: return true;
    }
}

// The folder menu's (F8): what fits that folder. Trash is gone to, not into; a place has no Cut or
// Copy; a bookmark's row has Edit Bookmarks' own.
static bool ui_folder_shown(const Ui *u, UiMenuItem it)
{
    UiCtxKind k = u->ctx_kind;
    if (k == UI_CTX_BRINGS) return it == UM_MOUNT_IT;   // no folder yet (2026-10-30)
    if (k == UI_CTX_NETWORK) return it == UM_OPEN_IT || it == UM_OPEN_TAB;   // (Browse Network's row: a view, not a folder; F16 step 7e)
    bool trash = k == UI_CTX_TRASH;
    switch (it) {
    case UM_SEP: case UM_OPEN_IT: case UM_OPEN_TAB: return true;
    case UM_OPEN_WINDOW: case UM_TERMINAL_IN: case UM_SEARCH_IN: case UM_PASTE_INTO: case UM_PROPS_IT: return !trash;
    case UM_CUT_IT: case UM_COPY_IT: return k == UI_CTX_DIR;   // (a place has none)
    case UM_COPY_PATH_IT: return !trash;   // (a place's too: a bookmark's, a drive's)
    case UM_BOOKMARK_IT: return !trash && !u->ctx_bmed;
    case UM_BM_RENAME_IT: case UM_BM_UP_IT: case UM_BM_DOWN_IT: case UM_BM_REMOVE_IT: case UM_BM_EDIT_IT: return u->ctx_bmk >= 0;
    case UM_EJECT_IT: return k == UI_CTX_DRIVE;
    case UM_UNMOUNT_IT: return k == UI_CTX_DRIVE || k == UI_CTX_SHARE;   // (Unmount: the drive stays, to mount again, 2026-10-30)
    case UM_EMPTY_IT: return trash;
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

// Whether entry it can be picked now. One that runs its command is greyed where the command would
// do nothing, or only say it can't (Copy in the trash view).
static bool ui_menu_enabled(const Ui *u, UiMenuItem it)
{
    bool files = u->list.n > 0, here = files && u->focus != UI_FOCUS_SIDE;
    if (!ui_menu_shown(u, it)) return false;
    if (ui_slot(it, UM_BM_0, UI_MENU_BOOKMARKS) >= 0 || ui_slot(it, UM_BR_0, UI_MENU_BRANCHES) >= 0 || it == UM_BR_ALL) return true;   // (a bookmark, a branch, All branches: shown, then picked; UM_BR_NOTE never)
    if (u->menu == UI_CTX_FOLDER) {   // (F8) the rest are always there to pick
        if (it == UM_PASTE_INTO) return u->can_paste && u->ctx_w;
        if (it == UM_BM_UP_IT) return u->ctx_bmk > 0;
        if (it == UM_BM_DOWN_IT) return u->ctx_bmk < u->ctx_bms - 1;
        if (it == UM_EMPTY_IT) return u->ctx_full;
        return true;
    }
    switch (it) {
    case UM_NEW_TAB: case UM_CLOSE_ALL: case UM_CLOSE: case UM_PROPERTIES: return true;
    case UM_NEW_WINDOW: return ui_view_allows(u->view, UA_NEW_WINDOW);   // ofx starts on a folder, not on the trash view (from a search: the folder searched)
    case UM_NEW_FOLDER: return ui_allows(u, UA_NEWFOLDER);
    case UM_NEW_FILE: return ui_allows(u, UA_NEWFILE);
    case UM_OPEN: return files;
    case UM_OPEN_WITH: return files && ui_allows(u, UA_OPEN_WITH);
    case UM_UNDO: return u->can_undo;
    case UM_CUT: return here && ui_allows(u, UA_CUT);
    case UM_COPY: return here && ui_view_allows(u->view, UA_COPY);
    case UM_COPY_PATH: return u->focus == UI_FOCUS_SIDE ? u->side.n > 0 : ui_view_allows(u->view, UA_COPY_PATH);   // (no entry: the folder's; the sidebar's keys: its cursor's, as the key)
    case UM_PASTE: return u->can_paste && ui_allows(u, UA_PASTE);
    case UM_SELECT_ALL: case UM_SELECT_MATCH: case UM_INVERT: return files;
    case UM_RENAME: return here && ui_allows(u, UA_RENAME);
    case UM_DUPLICATE: return here && ui_allows(u, UA_DUPLICATE);
    case UM_MAKE_LINK: return here && ui_allows(u, UA_MAKE_LINK);
    case UM_TRASH: return here && ui_allows(u, UA_TRASH);
    case UM_DELETE: return here && ui_allows(u, UA_DELETE);
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
    case UM_HOME: case UM_TRASH_VIEW: case UM_FIND: case UM_LOCATION: case UM_CONNECT: case UM_BROWSE: return true;
    case UM_SEARCH: return ui_view_allows(u->view, UA_SEARCH);
    case UM_TERMINAL: return ui_view_allows(u->view, UA_TERMINAL);
    case UM_ADD_BOOKMARK: return u->can_bookmark && ui_view_allows(u->view, UA_ADD_BOOKMARK);
    case UM_EDIT_BOOKMARKS: case UM_BM_MORE: case UM_SHORTCUTS: case UM_ABOUT: case UM_PREFERENCES: return true;
    case UM_CLEAR_PREVIEWS: return !u->clearing;
    case UM_TAB_LEFT: return u->tab_at > 0;
    case UM_TAB_RIGHT: return u->tab_at < u->tab_n - 1;
    case UM_TAB_OTHERS: return u->tab_n > 1;
    case UM_TAB_REOPEN: return u->can_reopen;
    case UM_OPEN_TAB: case UM_EMPTY_TRASH: case UM_OPEN_WINDOW: case UM_TERMINAL_IN: case UM_SEARCH_IN: case UM_FOLLOW: case UM_BOOKMARK_IT: return files;
    case UM_CONTAINING: case UM_OPEN_LINE: return true;   // (on a result's menu only)
    case UM_PASTE_INTO: return files && u->can_paste && u->ctx_w;
    case UM_HERE_PROPS: return u->view == UI_VIEW_FOLDER || u->view == UI_VIEW_TRASH;   // (a search, Browse Network: no folder)
    case UM_COPY_PATH_HERE: return true;   // (the folder searched, in a search)
    default: return false;
    }
}

// Entry it's mark: -1 none, else whether it is on (a check checked, one of a group the current).
static int ui_menu_mark(const Ui *u, UiMenuItem it)
{
    int k;
    if ((k = ui_slot(it, UM_BM_0, UI_MENU_BOOKMARKS)) >= 0) return u->bm_here == k;
    if ((k = ui_slot(it, UM_BR_0, UI_MENU_BRANCHES)) >= 0) return u->br_here == k;
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
// line and the find line close (a new folder keeps its name), and the search view's fields give
// the keys to the results (F12: a key the menu doesn't take, Ctrl+V, went on into the field); Esc
// gives them back.
static void ui_menu_open(Ui *u, int m, bool keys)
{
    if (!u->menu_on) u->menu_field = u->focus == UI_FOCUS_SEARCH || u->focus == UI_FOCUS_INSIDE ? u->focus : UI_FOCUS_LIST;   // (not from the next menu over)
    u->menu_on = true, u->menu = m;
    u->menu_at = keys ? ui_menu_next(u, -1, 1) : -1;
    ui_edit_close(u), ui_find_leave(u);
    ui_unfocus(u, UI_FOCUS_SEARCH), ui_unfocus(u, UI_FOCUS_INSIDE);
}

// A right-click menu m opened (F8): the files take the keys; keys: a key opened it.
static void ui_ctx_open(Ui *u, int m, UiNames names, bool keys)
{
    ui_unfocus(u, UI_FOCUS_SIDE), ui_unfocus(u, UI_FOCUS_CRUMBS), ui_unfocus(u, UI_FOCUS_JOBS);
    u->ctx_one = u->list.n > 0 && (u->marked == 0 || (u->marked == 1 && ui_marked(u, u->list.sel)));
    u->ctx_dir = u->ctx_one && names.dir && names.dir(names.ctx, u->list.sel);
    u->ctx_key = keys, u->ctx_w = false;   // (gui/ says, as it opens)
    ui_menu_open(u, m, keys);
}

// The branch button's menu opened (F12 step 5b), by the pointer or keys (Ctrl+K's on it): the keys
// stay where they were (on the button after Esc), as the folder menu's do. Nothing when there
// is no button.
static void ui_branch_open(Ui *u, bool keys)
{
    if (!u->branch_on) return;
    u->ctx_key = keys;
    ui_menu_open(u, UI_CTX_BRANCH, keys);
}

// The folder menu opened (F8) on sidebar row side or path button crumb (the other -1): what it is,
// gui/ says as it opens. The keys stay where they were.
static void ui_folder_open(Ui *u, isize side, int crumb, bool keys)
{
    u->ctx_side = (int)side, u->ctx_crumb = crumb, u->ctx_key = keys;
    u->ctx_kind = UI_CTX_DIR, u->ctx_bmed = u->ctx_full = u->ctx_w = false;
    u->ctx_bmk = -1, u->ctx_bms = 0;
    ui_menu_open(u, UI_CTX_FOLDER, keys);
}

// ---- an entry picked, the menus' keys and the pointer -------------------------------------------

// Entry it picked: the menu closes, and gui/ does what it says; one that runs its command leaves it
// in menu_cmd, for ui_handle to run as a key bound to it would (F7.4: the two can't differ).
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
        case UM_EMPTY_IT: return u->empty_asking = true, UI_NONE;
        case UM_PROPS_IT: return ui_props_open(u, UI_PROPS_CTX);
        default: return (UiAction){ UA_FOLDER_DO, it, false };
        }
    }
    if (UI_MENU_ITEM[it].runs) {
        u->menu_cmd = UI_MENU_ITEM[it].cmd;
        return UI_NONE;
    }
    int k;
    if ((k = ui_slot(it, UM_BM_0, UI_MENU_BOOKMARKS)) >= 0) return (UiAction){ UA_BOOKMARK, k, false };
    if ((k = ui_slot(it, UM_BR_0, UI_MENU_BRANCHES)) >= 0) return (UiAction){ UA_BRANCH, k, false };
    if (it == UM_BR_ALL) return ui_brp_open(u);
    switch (it) {
    case UM_SIDE_PLACES: return ui_side_mode(u, SIDE_PLACES);
    case UM_SIDE_TREE: return ui_side_mode(u, SIDE_TREE);
    case UM_SIDE_NONE: return ui_side_mode(u, SIDE_HIDDEN);
    case UM_SORT_NAME: case UM_SORT_SIZE: case UM_SORT_MTIME:
        u->sort.key = it == UM_SORT_NAME ? SORT_NAME : it == UM_SORT_SIZE ? SORT_SIZE : SORT_MTIME;
        return (UiAction){ UA_SORT, 0, false };
    case UM_DUPLICATE: return (UiAction){ UA_DUPLICATE, 0, false };
    case UM_COLORS: return u->colors = !u->colors, (UiAction){ UA_COLORS, 0, false };
    case UM_SHOW_MENU: return u->bar_hidden = !u->bar_hidden, u->panel = u->bar_hidden ? UI_PANEL_BAR : UI_PANEL_NONE,   // (a menu was open: no panel is)
                             (UiAction){ UA_MENUBAR, 0, false };
    case UM_BM_MORE: return ui_bmp_open(u, u->bm_more_at);
    case UM_NEW_FILE: return (UiAction){ UA_NEWFILE, 0, false };
    case UM_OPEN: return (UiAction){ UA_OPEN, u->list.sel, u->marked > 0 };
    case UM_OPEN_WITH: return ui_ow_open(u);
    case UM_ABOUT: return u->panel = UI_PANEL_ABOUT, UI_NONE;
    case UM_CONNECT:   // the location line on "smb://", the caret after it (F16 step 4: an address there connects)
        ui_location_open(u, S("smb://"));
        u->edit.anchor = u->edit.caret;
        return UI_NONE;
    case UM_BROWSE: return (UiAction){ UA_BROWSE, 0, false };
    case UM_PREFERENCES: return (UiAction){ UA_PREFERENCES, 0, false };
    case UM_CLEAR_PREVIEWS: return u->clear_asking = true, UI_NONE;   // (gui/ counts them as it opens)
    case UM_OPEN_TAB: return (UiAction){ UA_TAB_BG_ROW, u->list.sel, false };
    case UM_TAB_OTHERS: return (UiAction){ UA_TAB_OTHERS, 0, false };
    case UM_TAB_REOPEN: return (UiAction){ UA_TAB_REOPEN, 0, false };
    case UM_EMPTY_TRASH: return u->empty_asking = true, UI_NONE;
    case UM_COPY_PATH_HERE: return (UiAction){ UA_COPY_PATH, UI_COPY_SHOWN, true };
    case UM_HERE_PROPS: return ui_props_open(u, UI_PROPS_HERE);
    case UM_OPEN_WINDOW: return (UiAction){ UA_NEW_WINDOW, u->list.sel, true };
    case UM_PASTE_INTO: return (UiAction){ UA_PASTE, u->list.sel, true };
    case UM_TERMINAL_IN: return (UiAction){ UA_TERMINAL, u->list.sel, true };
    case UM_BOOKMARK_IT: return (UiAction){ UA_ADD_BOOKMARK, u->list.sel, true };
    case UM_FOLLOW: return (UiAction){ UA_FOLLOW, u->list.sel, false };
    case UM_SEARCH_IN: return (UiAction){ UA_SEARCH, u->list.sel, true };   // (all: below the folder entry index)
    default: return UI_NONE;
    }
}

// A menu entry run by a key bound to it (F15 step 2): as picked from its menu, when the menu would
// show it enabled; a bar menu's, or the tab menu's (on the current tab, as a right press on another
// shows it first). Greyed by its view, it says why, as Ctrl+Shift+N would (the code-health review).
// True when it asks gui/ for something (*out).
static bool ui_item_cmd(Ui *u, UiMenuItem it, UiAction *out)
{
    static const int MENUS[] = { 0, 1, 2, 3, 4, 5, UI_CTX_TAB };
    _Static_assert(UI_MENUS == 6, "MENUS: the bar's, then the tab menu");
    for (int k = 0; k < COUNT(MENUS); k++) {
        int m = MENUS[k];
        for (int i = 0; i < UI_MENU[m].n; i++) {
            if (UI_MENU[m].items[i] != it) continue;
            int was = u->menu;
            u->menu = m;   // (what is shown and enabled is the bar menu's, for this pick only)
            if (!ui_menu_enabled(u, it)) {   // (greyed: what its view says, as a key's action would)
                u->menu = was;
                UiActionKind kind = it == UM_NEW_FILE ? UA_NEWFILE : it == UM_DUPLICATE ? UA_DUPLICATE : UA_NONE;
                *out = kind && !ui_allows(u, kind) ? ui_view_act(u, (UiAction){ kind, 0, false }) : UI_NONE;
                return out->kind != UA_NONE;
            }
            *out = ui_menu_pick(u, it);
            u->menu = was;   // (no menu is open: the last one's stays, as before)
            return out->kind != UA_NONE;
        }
    }
    return false;
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
    *a = UI_NONE;
    e = ui_vi_key(e);
    if (e.key == UK_NONE) return true;   // (a letter the menu doesn't take: nothing)
    switch (ui_unshift(e.key)) {
    case UK_DOWN: case UK_UP: {
        int at = ui_menu_next(u, u->menu_at < 0 && e.key == UK_UP ? 0 : u->menu_at, e.key == UK_DOWN ? 1 : -1);
        u->menu_at = at;
        return true;
    }
    case UK_HOME: u->menu_at = ui_menu_next(u, -1, 1); return true;
    case UK_END: u->menu_at = ui_menu_next(u, 0, -1); return true;
    case UK_LEFT: case UK_RIGHT: {
        if (u->menu >= UI_MENUS) return true;   // a right-click menu has no neighbours (F8)
        int by = ui_unshift(e.key) == UK_LEFT ? -1 : 1;
        ui_menu_open(u, ((u->menu + by) % UI_MENUS + UI_MENUS) % UI_MENUS, true);
        return true;
    }
    case UK_ENTER: case UK_SHIFT_ENTER:
        if (u->menu_at >= 0 && ui_menu_enabled(u, UI_MENU[u->menu].items[u->menu_at])) *a = ui_menu_pick(u, UI_MENU[u->menu].items[u->menu_at]);
        return true;
    case UK_ESC:
        u->menu_on = false;
        if (u->menu_field != UI_FOCUS_LIST) u->focus = u->menu_field;
        return true;
    case UK_TAB: u->menu_on = false; return true;
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
