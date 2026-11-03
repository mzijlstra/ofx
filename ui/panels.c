// The panels' keys: Properties (F7.2), Open With (F7.3), Keyboard Shortcuts and Edit Bookmarks (F7.5),
// the branches (F12 step 6); Open With's and the branches' share a picker.
// Included by ui.c, before menus.c (whose entries open them).

// ---- Properties (F7.2) --------------------------------------------------------------------------

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
    e = ui_vi_key(e);
    if (e.key == UK_NONE && e.ch == ' ') e.key = UK_ENTER;
    int row = u->prop_at / 3, col = u->prop_at % 3;
    switch (ui_unshift(e.key)) {
    case UK_LEFT: if (col > 0) u->prop_at--; break;
    case UK_RIGHT: if (col < 2) u->prop_at++; break;
    case UK_UP: if (row > 0) u->prop_at -= 3; break;
    case UK_DOWN: if (row < 2) u->prop_at += 3; break;
    case UK_ENTER: case UK_SHIFT_ENTER:
        if (u->prop_can) return (UiAction){ UA_CHMOD, u->prop_at, false };
        break;
    case UK_ESC: case UK_PROPERTIES: ui_panel_close(u, UI_PANEL_PROPS); break;
    default: break;
    }
    return UI_NONE;
}

// ---- a picker: Open With's and the branches' filter field over its list -------------------------

// What a picker's key or press asks of its panel (ui_ow_do, ui_brp_do).
typedef enum { UI_PICK_NONE, UI_PICK_FILTER, UI_PICK_CHOOSE, UI_PICK_CLOSE } UiPickDo;

// Opened: the field empty with the keys; gui/ gives the names to filter.
static void ui_pick_open(UiPick *p)
{
    p->in_list = false, p->fresh = true, UI_TEXT_CLEAR(p->filter);
    p->list = (UiList){ .rows = p->list.rows };
}

// Closed with its panel: a compiled expression freed.
static void ui_pick_close(Ui *u, UiPanel panel, UiPick *p)
{
    ui_panel_close(u, panel);
    ui_matcher_set(&p->m, STR_EMPTY, (UiMatchOpts){ 0 });
}

// The names the field's text matches, their places into idx (max at most), in gui/'s order; the
// cursor on the first.
static void ui_pick_filter(UiPick *p, UiMatchOpts opts, const Str *names, isize n, isize *idx, isize max)
{
    ui_matcher_set(&p->m, (Str){ p->filter.b, p->filter.len }, opts);
    isize k = 0;
    for (isize i = 0; i < n && k < max; i++)
        if (ui_match(&p->m, names[i])) idx[k++] = i;
    p->list.n = k, p->list.sel = 0, p->list.top = 0;
    p->fresh = false;
}

// A key while its panel is up; it takes every key (Michael, 2026-10-26). Tab moves between the
// field and the list. In the field typing, Backspace, Delete, Left, Right, Home and End edit, Down
// goes to the list; in the list Up, Down, k, j, Home, End, PgUp and PgDn move, Up on the first
// row goes to the field, another character goes to the field and is typed there. The field edits as
// the rename line does (2026-10-28). Enter chooses the one under the cursor, Esc closes.
static UiPickDo ui_pick_key(Ui *u, UiPick *p, UiEvent e, const UiInput *in)
{
    UiList *l = &p->list;
    int page = l->rows > 1 ? l->rows - 1 : 1;
    switch (e.key) {
    case UK_ESC: return UI_PICK_CLOSE;
    case UK_TAB: p->in_list = !p->in_list; return UI_PICK_NONE;
    case UK_ENTER: case UK_SHIFT_ENTER: return UI_PICK_CHOOSE;
    default: break;
    }
    if (p->in_list) {
        e = ui_vi_key(e);
        switch (e.key) {
        case UK_DOWN: ui_move(l, 1); break;
        case UK_UP:
            if (l->sel == 0) p->in_list = false;
            else ui_move(l, -1);
            break;
        case UK_HOME: ui_move(l, -l->n); break;
        case UK_END: ui_move(l, l->n); break;
        case UK_PAGEUP: ui_move(l, -page); break;
        case UK_PAGEDOWN: ui_move(l, page); break;
        case UK_NONE:
        case UK_BACKSPACE:   // typing (a character there is) in the list: the field's end, then typed there
            if (e.key == UK_NONE && ui_utf8(e.ch, (char[4]){ 0 }) == 0) break;
            p->in_list = false, p->filter.caret = p->filter.anchor = p->filter.len;
            if (ui_line_key(u, ui_pick_line(p), e, in->paste) == UI_LINE_CHANGED) return UI_PICK_FILTER;
            break;
        default: break;
        }
        return UI_PICK_NONE;
    }
    UiLineHit h = ui_line_key(u, ui_pick_line(p), e, in->paste);
    if (h == UI_LINE_CHANGED) return UI_PICK_FILTER;
    if (!h && e.key == UK_DOWN && l->n > 0) p->in_list = true;
    return UI_PICK_NONE;
}

// A press while its panel is up: the field takes the keys, a row the cursor (a double click
// chooses it); the main button chooses, Cancel closes; the wheel scrolls the list.
static UiPickDo ui_pick_press(UiPick *p, const UiInput *in)
{
    if (in->pick_field) p->in_list = false;
    isize row = in->pick_drow >= 0 ? in->pick_drow : in->pick_row;
    if (row >= 0 && p->list.top + row < p->list.n) {
        p->list.sel = p->list.top + row, p->in_list = true;
        if (in->pick_drow >= 0) return UI_PICK_CHOOSE;
    }
    if (in->modal == UI_MODAL_YES) return UI_PICK_CHOOSE;
    if (in->modal == UI_MODAL_NO) return UI_PICK_CLOSE;
    if (in->panel_wheel) ui_scroll(&p->list, in->panel_wheel);
    return UI_PICK_NONE;
}

// ---- Open With (F7.3) ------------------------------------------------------------------------

// The panel opened: the field empty with the keys, the toggles as last time; gui/ lists the apps.
static UiAction ui_ow_open(Ui *u)
{
    u->panel = UI_PANEL_OW, u->ow_def = false;
    ui_pick_open(&u->ow);
    return (UiAction){ UA_OPEN_WITH, 0, false };
}

static void ui_ow_close(Ui *u)
{
    ui_pick_close(u, UI_PANEL_OW, &u->ow);
}

static void ui_ow_filter(Ui *u, const UiInput *in)
{
    ui_pick_filter(&u->ow, u->ow_opts, in->app_name, in->app_n, u->ow_idx, UI_APPS_MAX);
}

// The app under the cursor launched, when the filter shows any.
static UiAction ui_ow_launch(Ui *u)
{
    if (u->ow.list.n == 0) return UI_NONE;
    isize app = u->ow_idx[u->ow.list.sel];
    bool def = u->ow_def && u->ow_def_can;
    ui_ow_close(u);
    return (UiAction){ UA_LAUNCH, app, def };
}

// What the picker asked: the list filtered again, the app launched, or the panel closed.
static UiAction ui_ow_do(Ui *u, UiPickDo d, const UiInput *in)
{
    if (d == UI_PICK_FILTER) ui_ow_filter(u, in);
    else if (d == UI_PICK_CHOOSE) return ui_ow_launch(u);
    else if (d == UI_PICK_CLOSE) ui_ow_close(u);
    return UI_NONE;
}

// A key while the panel is up: the picker's (ui_pick_key: Enter opens the app under the cursor),
// and Alt+C, Alt+W and Alt+R flip the toggles, Alt+D the box that makes it the default (F7.3.1).
static UiAction ui_ow_event(Ui *u, UiEvent e, const UiInput *in)
{
    if (e.key == UK_MENU && e.ch == 'd') {
        if (u->ow_def_can) u->ow_def = !u->ow_def;
        return UI_NONE;
    }
    if (ui_toggle_key(e)) {
        u->ow_opts = ui_toggle(u->ow_opts, ui_toggle_key(e));
        ui_ow_filter(u, in);
        return UI_NONE;
    }
    return ui_ow_do(u, ui_pick_key(u, &u->ow, e, in), in);
}

// ---- the branches (F12 step 6) -------------------------------------------------------------------

// The panel opened from the branch menu's "All branches…": the field empty with the keys; gui/
// gives the names.
static UiAction ui_brp_open(Ui *u)
{
    u->panel = UI_PANEL_BRANCHES;
    ui_pick_open(&u->brp);
    return UI_NONE;
}

static void ui_brp_close(Ui *u)
{
    ui_pick_close(u, UI_PANEL_BRANCHES, &u->brp);
}

// The branches the field's text is in (case folded: a ticket id typed either way).
static void ui_brp_filter(Ui *u, const UiInput *in)
{
    ui_pick_filter(&u->brp, (UiMatchOpts){ 0 }, in->br_name, in->br_names, u->brp_idx, UI_BRANCHES_MAX);
}

// What the picker asked: the list filtered again, the branch under the cursor switched to as the
// menu's entry would (its place in gui/'s list), or the panel closed.
static UiAction ui_brp_do(Ui *u, UiPickDo d, const UiInput *in)
{
    if (d == UI_PICK_FILTER) ui_brp_filter(u, in);
    else if (d == UI_PICK_CHOOSE && u->brp.list.n > 0) {
        isize k = u->brp_idx[u->brp.list.sel];
        ui_brp_close(u);
        return (UiAction){ UA_BRANCH, k, false };
    } else if (d == UI_PICK_CLOSE) ui_brp_close(u);
    return UI_NONE;
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
    u->panel = UI_PANEL_KEYS, UI_TEXT_CLEAR(u->keys_filter);
    u->keys_list = (UiList){ .rows = u->keys_list.rows };
    ui_keys_filter(u);
    return UI_NONE;
}

static void ui_keys_close(Ui *u)
{
    ui_panel_close(u, UI_PANEL_KEYS);
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
    u->panel = UI_PANEL_BM, u->bmp_naming = false;
    u->bmp_list = (UiList){ .rows = u->bmp_list.rows };
    ui_set(&u->bmp_list, u->bm_total, at >= 0 && at < u->bm_total ? at : 0);
    return (UiAction){ UA_BM_EDIT, 0, false };
}

// What one of the panel's buttons (or its key) asks for the line under the cursor (UI_BMP_*):
// Remove (Delete), Rename (F2), Move Up (Alt+Up), Move Down (Alt+Down); nothing past the ends.
static UiAction ui_bmp_do(Ui *u, int what)
{
    UiList *l = &u->bmp_list;
    if (l->n == 0) return UI_NONE;
    switch (what) {
    case UI_BMP_REMOVE: return (UiAction){ UA_BM_REMOVE, l->sel, false };
    case UI_BMP_RENAME: u->bmp_naming = true; return (UiAction){ UA_BM_NAME, l->sel, false };
    case UI_BMP_UP: return l->sel > 0 ? (UiAction){ UA_BM_UP, l->sel, false } : UI_NONE;
    case UI_BMP_DOWN: return l->sel < l->n - 1 ? (UiAction){ UA_BM_DOWN, l->sel, false } : UI_NONE;
    default: return UI_NONE;
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
        if (ui_line_key(u, ui_bmp_line(u), e, paste)) return UI_NONE;
        switch (e.key) {
        case UK_ENTER: case UK_SHIFT_ENTER: u->bmp_naming = false; return (UiAction){ UA_BM_RENAME, l->sel, false };
        case UK_ESC: u->bmp_naming = false; break;
        default: break;
        }
        return UI_NONE;
    }
    e = ui_vi_key(e);
    switch (e.key) {
    case UK_DOWN: ui_move(l, 1); break;
    case UK_UP: ui_move(l, -1); break;
    case UK_HOME: ui_move(l, -l->n); break;
    case UK_END: ui_move(l, l->n); break;
    case UK_PAGEUP: ui_move(l, -page); break;
    case UK_PAGEDOWN: ui_move(l, page); break;
    case UK_DELETE: return ui_bmp_do(u, UI_BMP_REMOVE);
    case UK_F2: return ui_bmp_do(u, UI_BMP_RENAME);
    case UK_PARENT: return ui_bmp_do(u, UI_BMP_UP);
    case UK_ALT_DOWN: return ui_bmp_do(u, UI_BMP_DOWN);
    case UK_ENTER: case UK_SHIFT_ENTER:
        if (l->n == 0) break;
        ui_panel_close(u, UI_PANEL_BM);
        return (UiAction){ UA_BM_GO, l->sel, false };
    case UK_ESC: ui_panel_close(u, UI_PANEL_BM); break;
    default: break;
    }
    return UI_NONE;
}
