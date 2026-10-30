// The panels' keys: Open With (F7.3), Keyboard Shortcuts and Edit Bookmarks (F7.5). Included by ui.c.

// ---- Open With (F7.3) ------------------------------------------------------------------------

// The panel opened: the field empty with the keys, the toggles as last time; gui/ lists the apps.
static UiAction ui_ow_open(Ui *u)
{
    u->ow = true, u->ow_in_list = false, u->ow_fresh = true, UI_TEXT_CLEAR(u->ow_filter), u->ow_def = false;
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
    ui_matcher_set(&u->ow_m, (Str){ u->ow_filter.b, u->ow_filter.len }, u->ow_opts);
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
        case UK_BACKSPACE:   // typing (a character there is) in the list: the field's end, then typed there
            if (e.key == UK_NONE && ui_utf8(e.ch, (char[4]){ 0 }) == 0) break;
            u->ow_in_list = false, u->ow_filter.caret = u->ow_filter.anchor = u->ow_filter.len;
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
    ui_matcher_set(&u->keys_m, (Str){ u->keys_filter.b, u->keys_filter.len }, (UiMatchOpts){ 0 });
    int n = 0;
    for (int s = 0; s < KS_SECTIONS; s++) {
        int head = n;
        u->keys_line[n++] = (short)(-1 - s);
        for (int r = 0; r < UI_KEYS_N; r++)
            if (UI_KEYS[r].sec == (UiKeySection)s && (!u->keys_filter.len || ui_match(&u->keys_m, str_from_c(UI_KEYS[r].action)) ||
                                                      ui_match(&u->keys_m, str_from_c(UI_KEYS[r].key)) || ui_match(&u->keys_m, str_from_c(UI_KEYS[r].key2))))
                u->keys_line[n++] = (short)r;
        if (n == head + 1) n = head;   // nothing under it
    }
    ui_set(&u->keys_list, n, 0);   // (its cursor on the first line: the top there too)
}

static UiAction ui_keys_open(Ui *u)
{
    u->keys = true, UI_TEXT_CLEAR(u->keys_filter);
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
    case UM_PREFERENCES: return (UiAction){ UA_PREFERENCES, 0, false };
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
