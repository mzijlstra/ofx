// The menu bar (F7.1) and the right-click menus (F8): what is on them, what can be picked, the keys. Included by ui.c.

// ---- the menu bar (F7.1) -----------------------------------------------------------------------

static bool ui_folder_shown(const Ui *u, UiMenuItem it);

// Whether entry it is in its menu now: Move to Trash outside the trash view, Restore in it (F7.4); a
// bookmark's place while there is one for it (F7.5).
static bool ui_menu_shown(const Ui *u, UiMenuItem it)
{
    if (u->menu == UI_CTX_FOLDER) return ui_folder_shown(u, it);
    if (it >= UM_BM_0 && it <= UM_BM_7) return (int)(it - UM_BM_0) < u->bm_n;
    if (it == UM_BM_MORE) return u->bm_more > 0;
    // A right-click menu leaves out what doesn't fit its target (F8): a tab for a folder only, a
    // rename for one entry, the new entries outside the trash view (Empty Trash in it).
    if (u->menu >= UI_MENUS && it == UM_OPEN_TAB) return u->ctx_dir && u->view != UI_VIEW_TRASH;
    if (u->menu >= UI_MENUS && it == UM_RENAME) return u->marked <= 1;
    if (it == UM_OPEN_WINDOW || it == UM_PASTE_INTO || it == UM_TERMINAL_IN || it == UM_SEARCH_IN) return u->ctx_dir && u->view != UI_VIEW_TRASH;
    if (it == UM_CONTAINING) return ui_view_allows(u->view, UA_CONTAINING);
    if (it == UM_OPEN_LINE) return u->ctx_one && u->sel_hit;   // (one result found inside files)
    if (it == UM_BOOKMARK_IT) return u->ctx_dir && u->view != UI_VIEW_TRASH && !u->sel_bm;
    if (it == UM_FOLLOW) return u->ctx_one && u->sel_link && u->view != UI_VIEW_TRASH;
    if (u->menu >= UI_MENUS && (it == UM_NEW_FOLDER || it == UM_NEW_FILE)) return u->view != UI_VIEW_TRASH;
    if (it == UM_EMPTY_TRASH) return ui_view_allows(u->view, UA_EMPTY_TRASH);
    return it == UM_TRASH ? u->view != UI_VIEW_TRASH : it == UM_RESTORE ? ui_view_allows(u->view, UA_RESTORE) : true;
}

// The folder menu's (F8): what fits that folder. Trash is gone to, not into; a place has no Cut or
// Copy; a bookmark's row has Edit Bookmarks' own.
static bool ui_folder_shown(const Ui *u, UiMenuItem it)
{
    if (u->ctx_unmounted) return it == UM_MOUNT_IT;   // no folder yet (2026-10-30)
    switch (it) {
    case UM_SEP: case UM_OPEN_IT: case UM_OPEN_TAB: return true;
    case UM_OPEN_WINDOW: case UM_TERMINAL_IN: case UM_SEARCH_IN: case UM_PASTE_INTO: case UM_PROPS_IT: return !u->ctx_trash;
    case UM_CUT_IT: case UM_COPY_IT: return !u->ctx_trash && !u->ctx_place;
    case UM_BOOKMARK_IT: return !u->ctx_trash && !u->ctx_bmed;
    case UM_BM_RENAME_IT: case UM_BM_UP_IT: case UM_BM_DOWN_IT: case UM_BM_REMOVE_IT: case UM_BM_EDIT_IT: return u->ctx_bmk >= 0;
    case UM_EJECT_IT: case UM_UNMOUNT_IT: return u->ctx_eject;   // (Unmount: the drive stays, to mount again, 2026-10-30)
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
    bool files = u->list.n > 0, here = files && u->focus != UI_FOCUS_SIDE;
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
    case UM_NEW_WINDOW: return ui_view_allows(u->view, UA_NEW_WINDOW);   // ofx starts on a folder, not on the trash view (from a search: the folder searched)
    case UM_NEW_FOLDER: case UM_NEW_FILE: return u->view != UI_VIEW_TRASH && u->can_write;
    case UM_OPEN: return files;
    case UM_OPEN_WITH: return files && u->view != UI_VIEW_TRASH;
    case UM_UNDO: return u->can_undo;
    case UM_CUT: case UM_COPY: return here && ui_view_allows(u->view, UA_COPY);
    case UM_PASTE: return u->can_paste && u->can_write;
    case UM_SELECT_ALL: case UM_SELECT_MATCH: case UM_INVERT: return files;
    case UM_RENAME: case UM_DUPLICATE: case UM_MAKE_LINK: return here && u->view != UI_VIEW_TRASH && (u->can_write || u->view == UI_VIEW_SEARCH);   // (a result's own folder: F9)
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
    case UM_SEARCH: return ui_view_allows(u->view, UA_SEARCH);
    case UM_TERMINAL: return ui_view_allows(u->view, UA_TERMINAL);
    case UM_ADD_BOOKMARK: return u->can_bookmark && ui_view_allows(u->view, UA_ADD_BOOKMARK);
    case UM_BM_0: case UM_BM_1: case UM_BM_2: case UM_BM_3: case UM_BM_4: case UM_BM_5: case UM_BM_6: case UM_BM_7: return true;
    case UM_EDIT_BOOKMARKS: case UM_BM_MORE: case UM_SHORTCUTS: case UM_ABOUT: case UM_PREFERENCES: return true;
    case UM_TAB_LEFT: return u->tab_at > 0;
    case UM_TAB_RIGHT: return u->tab_at < u->tab_n - 1;
    case UM_TAB_OTHERS: return u->tab_n > 1;
    case UM_TAB_REOPEN: return u->can_reopen;
    case UM_OPEN_TAB: case UM_EMPTY_TRASH: case UM_OPEN_WINDOW: case UM_TERMINAL_IN: case UM_SEARCH_IN: case UM_FOLLOW: case UM_BOOKMARK_IT: return files;
    case UM_CONTAINING: case UM_OPEN_LINE: return true;   // (on a result's menu only)
    case UM_PASTE_INTO: return files && u->can_paste && u->ctx_w;
    case UM_HERE_PROPS: return u->view != UI_VIEW_SEARCH;   // (a search is no folder)
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
    ui_unfocus(u, UI_FOCUS_SIDE), ui_unfocus(u, UI_FOCUS_CRUMBS), ui_unfocus(u, UI_FOCUS_JOBS);
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
    u->ctx_trash = u->ctx_place = u->ctx_bmed = u->ctx_eject = u->ctx_full = u->ctx_w = u->ctx_unmounted = false;
    u->ctx_bmk = -1, u->ctx_bms = 0;
    ui_menu_open(u, UI_CTX_FOLDER, keys);
}

// The Properties panel opened, the focus on the grid's first box (F7.2).
static UiAction ui_props_open(Ui *u)
{
    u->panel = UI_PANEL_PROPS, u->prop_at = 0;   // (the menu closed the edit and find lines; Ctrl+I in them is theirs)
    return (UiAction){ UA_PROPS, UI_PROPS_MARKED, false };
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
    case UK_ESC: case UK_PROPERTIES: ui_panel_close(u, UI_PANEL_PROPS); break;
    default: break;
    }
    return (UiAction){ UA_NONE, 0, false };
}
