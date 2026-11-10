// The panels' keys: Properties (F7.2), Open With (F7.3), Keyboard Shortcuts and Edit Bookmarks (F7.5),
// the branches (F12 step 6), a server's shares (F16 step 7a), Connect's question (F16 step 4); Open
// With's, the branches' and the shares' are pickers (ui_picker).
// Included by ui.c, before menus.c (whose entries open them).

// ---- Properties (F7.2) --------------------------------------------------------------------------

// The Properties panel opened, the focus on the grid's first box (F7.2), on which (UI_PROPS_*: the
// marked or the cursor's, a menu's entry, the folder shown).
static UiAction ui_props_open(Ui *u, int which)
{
    u->panel = UI_PANEL_PROPS, u->prop_at = 0;   // (the menu closed the edit and find lines; Ctrl+I in them is theirs)
    return (UiAction){ UA_PROPS, which, false };
}

// A key while the Properties panel is up; it takes every key (a panel). Arrows and h, j, k, l move
// the focus in the 3 by 3 grid (stopping at its edges), Space or Enter flips that permission when
// gui/ allows it, Esc or Ctrl+I closes the panel.
static UiAction ui_props_event(Ui *u, UiEvent e)
{
    e = ui_vi_key(e);
    if (e.key == UK_NONE && e.ch == ' ') e.key = UK_ENTER;
    if (e.cmd == UK_PROPERTIES) return ui_panel_close(u, UI_PANEL_PROPS), UI_NONE;
    int row = u->prop_at / 3, col = u->prop_at % 3;
    switch (ui_unshift(e.key)) {
    case UK_LEFT: if (col > 0) u->prop_at--; break;
    case UK_RIGHT: if (col < 2) u->prop_at++; break;
    case UK_UP: if (row > 0) u->prop_at -= 3; break;
    case UK_DOWN: if (row < 2) u->prop_at += 3; break;
    case UK_ENTER: case UK_SHIFT_ENTER:
        if (u->prop_can) return (UiAction){ UA_CHMOD, u->prop_at, false };
        break;
    case UK_ESC: ui_panel_close(u, UI_PANEL_PROPS); break;
    default: break;
    }
    return UI_NONE;
}

// ---- a picker: a filter field over its list (Open With's, the branches', a server's shares) -------

// What a picker's key or press asks of its panel (ui_picker_do).
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
    switch (e.key) {
    case UK_ESC: return UI_PICK_CLOSE;
    case UK_TAB: p->in_list = !p->in_list; return UI_PICK_NONE;
    case UK_ENTER: case UK_SHIFT_ENTER: return UI_PICK_CHOOSE;
    default: break;
    }
    if (p->in_list) {
        e = ui_vi_key(e);
        if (e.key == UK_UP && l->sel == 0) p->in_list = false;   // (above the first row: the field)
        else if (ui_list_nav(l, e.key)) {}
        else switch (e.key) {
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

// A picker panel's parts: its picker, the places its filter shows (in gui/'s list) and their room,
// the names gui/ gives this frame (none without in), how they match, and what choosing one asks.
typedef struct {
    UiPick      *pick;
    isize       *idx, max;
    const Str   *names;
    isize        n;
    UiMatchOpts  opts;
    UiActionKind chose;
} UiPicker;

// Picker panel p's parts (.pick NULL when p isn't one). A new picker is a panel, its fields in Ui,
// and a line here and in ui_pick_of.
static UiPicker ui_picker(Ui *u, UiPanel p, const UiInput *in)
{
    switch (p) {
    case UI_PANEL_OW:   // (Open With, F7.3: the apps, by gui/'s toggles; Enter launches)
        return (UiPicker){ &u->ow, u->ow_idx, UI_APPS_MAX, in ? in->app_name : NULL, in ? in->app_n : 0, u->ow_opts, UA_LAUNCH };
    case UI_PANEL_BRANCHES:   // (F12 step 6: the branch menu's, case folded; Enter switches)
        return (UiPicker){ &u->brp, u->brp_idx, UI_BRANCHES_MAX, in ? in->br_name : NULL, in ? in->br_names : 0, { 0 }, UA_BRANCH };
    case UI_PANEL_SHARES:   // (F16 step 7a: a server's, case folded; Enter connects)
        return (UiPicker){ &u->shp, u->shp_idx, UI_SHARES_MAX, in ? in->sh_name : NULL, in ? in->sh_names : 0, { 0 }, UA_SHARE };
    default: return (UiPicker){ 0 };
    }
}

// Picker panel p opened: the field empty with the keys; gui/ gives the names.
static void ui_picker_open(Ui *u, UiPanel p)
{
    u->panel = p;
    ui_pick_open(ui_pick_of(u, p));
}

static void ui_picker_close(Ui *u, UiPanel p)
{
    ui_pick_close(u, p, ui_pick_of(u, p));
}

// The names the field's text matches, by gui/'s names this frame.
static void ui_picker_filter(Ui *u, UiPanel p, const UiInput *in)
{
    UiPicker k = ui_picker(u, p, in);
    ui_pick_filter(k.pick, k.opts, k.names, k.n, k.idx, k.max);
}

// What the picker asked: the list filtered again, the one under the cursor chosen (its place in
// gui/'s list; Open With's with its "Use as default" box), or the panel closed.
static UiAction ui_picker_do(Ui *u, UiPanel p, UiPickDo d, const UiInput *in)
{
    UiPicker k = ui_picker(u, p, in);
    if (d == UI_PICK_FILTER) ui_picker_filter(u, p, in);
    else if (d == UI_PICK_CHOOSE && k.pick->list.n > 0) {
        isize i = k.idx[k.pick->list.sel];
        bool def = p == UI_PANEL_OW && u->ow_def && u->ow_def_can;
        ui_picker_close(u, p);
        return (UiAction){ k.chose, i, def };
    } else if (d == UI_PICK_CLOSE) ui_picker_close(u, p);
    return UI_NONE;
}

// ---- Open With (F7.3) ------------------------------------------------------------------------

// The panel opened: the field empty with the keys, the toggles as last time; gui/ lists the apps.
static UiAction ui_ow_open(Ui *u)
{
    u->ow_def = false;
    ui_picker_open(u, UI_PANEL_OW);
    return (UiAction){ UA_OPEN_WITH, 0, false };
}

// Open With's "Use as default" box flipped, when the type allows one (a key, or the box pressed).
static void ui_ow_def_flip(Ui *u)
{
    if (u->ow_def_can) u->ow_def = !u->ow_def;
}

// A key while the panel is up: the picker's (ui_pick_key: Enter opens the app under the cursor),
// and Alt+C, Alt+W and Alt+R flip the toggles, Alt+D the box that makes it the default (F7.3.1).
static UiAction ui_ow_event(Ui *u, UiEvent e, const UiInput *in)
{
    if (e.key == UK_MENU && e.ch == 'd') {
        ui_ow_def_flip(u);
        return UI_NONE;
    }
    if (ui_toggle_key(e)) {
        u->ow_opts = ui_toggle(u->ow_opts, ui_toggle_key(e));
        ui_picker_filter(u, UI_PANEL_OW, in);
        return UI_NONE;
    }
    return ui_picker_do(u, UI_PANEL_OW, ui_pick_key(u, &u->ow, e, in), in);
}

// ---- the branches (F12 step 6) and a server's shares (F16 step 7a) -------------------------------

// The branches panel opened from the branch menu's "All branches…".
static UiAction ui_brp_open(Ui *u)
{
    ui_picker_open(u, UI_PANEL_BRANCHES);
    return UI_NONE;
}

// The shares panel opened by gui/ once Connect to Server has read a server's shares, and no other
// panel is up.
static void ui_shp_open(Ui *u)
{
    ui_picker_open(u, UI_PANEL_SHARES);
    ui_edit_close(u), u->menu_on = false;
}

// ---- Keyboard Shortcuts (F7.5) -------------------------------------------------------------------

// The lines for the filter's text: each section's heading and its rows whose action, keys or
// command's name (F15 step 3) hold the text (case folded); a section with none left out.
static void ui_keys_filter(Ui *u)
{
    ui_matcher_set(&u->keys_m, (Str){ u->keys_filter.b, u->keys_filter.len }, (UiMatchOpts){ 0 });
    int n = 0;
    for (int s = 0; s < KS_SECTIONS; s++) {
        int head = n;
        u->keys_row[n++] = (short)(-1 - s);
        for (int r = 0; r < UI_KEYS_N; r++)
            if (UI_KEYS[r].sec == (UiKeySection)s && (!u->keys_filter.len || ui_match(&u->keys_m, str_from_c(UI_KEYS[r].action)) ||
                                                      ui_match(&u->keys_m, str_from_c(ui_keys_key(r, 0))) || ui_match(&u->keys_m, str_from_c(ui_keys_key(r, 1))) ||
                                                      ui_match(&u->keys_m, str_from_c(ui_keys_name(r)))))
                u->keys_row[n++] = (short)r;
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
    if (e.cmd == UK_SHORTCUTS) {
        ui_keys_close(u);
        return;
    }
    switch (e.key) {
    case UK_UP: case UK_DOWN: ui_scroll(l, e.key == UK_UP ? 1 : -1); break;   // (as the wheel's notches: up is +)
    case UK_PAGEUP: case UK_PAGEDOWN: ui_scroll(l, e.key == UK_PAGEUP ? page : -page); break;
    case UK_HOME: ui_scroll(l, (int)l->n); break;
    case UK_END: ui_scroll(l, (int)-l->n); break;
    case UK_ESC: case UK_ENTER: ui_keys_close(u); break;
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
    if (ui_list_nav(l, e.key)) return UI_NONE;
    switch (e.key) {
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

// ---- Connect to Server's question (F16 step 4) --------------------------------------------------

// The panel gone, and what gui/ does with its answer (k).
static UiAction ui_login_close(Ui *u, UiActionKind k)
{
    ui_panel_close(u, UI_PANEL_LOGIN);
    return (UiAction){ k, 0, false };
}

// Whether the question shows field f: the user's only when gio asks for one, the domain not for an
// SFTP server, only the password for a key's passphrase (F16 step 7c).
static bool ui_login_shown(const Ui *u, int f)
{
    if (u->login_kind == UI_LOGIN_KEY) return f == UI_LOGIN_PASS;
    return !(f == UI_LOGIN_USER && u->login_no_user) && !(f == UI_LOGIN_DOMAIN && u->login_kind == UI_LOGIN_SSH);
}

// Whether the question offers Guest: a share's only (no guest over SSH, step 7c).
static bool ui_login_guest(const Ui *u)
{
    return u->login_kind == UI_LOGIN_SHARE;
}

// The question put up, for kind: the user's field only when gio asks for one (ask_user), again when
// the last answer was refused (what was typed then stays, save the password; else the user field
// starts as def_user, the domain empty). The keys on the user's field when it is shown and empty,
// else the password's. The edit line and a menu close.
static void ui_login_open(Ui *u, UiLoginKind kind, bool ask_user, bool again, Str def_user)
{
    if (!again) {
        ui_line_set(ui_login_line(u, UI_LOGIN_USER), def_user);
        ui_line_set(ui_login_line(u, UI_LOGIN_DOMAIN), STR_EMPTY);
    }
    u->login_kind = kind, u->login_no_user = !ask_user, u->login_again = again;
    u->login_at = ask_user && kind != UI_LOGIN_KEY && u->login_user.len == 0 ? UI_LOGIN_USER : UI_LOGIN_PASS;
    u->panel = UI_PANEL_LOGIN;
    ui_edit_close(u), u->menu_on = false;
}

// The keys to the next field that way (d: 1 or -1), round, over the fields shown.
static void ui_login_step(Ui *u, int d)
{
    do u->login_at = (u->login_at + d + UI_LOGIN_FIELDS) % UI_LOGIN_FIELDS;
    while (!ui_login_shown(u, u->login_at));
}

// A key while the question is up; it takes every key. The field with the keys edits as the edit
// line does; Tab, Down and Up go from field to field; Enter connects, Alt+G as a guest, Esc doesn't.
static UiAction ui_login_event(Ui *u, UiEvent e, Str paste)
{
    if (ui_line_key(u, ui_login_line(u, u->login_at), e, paste)) return UI_NONE;
    switch (e.key) {
    case UK_TAB: case UK_DOWN: ui_login_step(u, 1); break;
    case UK_UP: ui_login_step(u, -1); break;
    case UK_ENTER: case UK_SHIFT_ENTER: return ui_login_close(u, UA_LOGIN);
    case UK_ESC: return ui_login_close(u, UA_LOGIN_CANCEL);
    case UK_MENU: if (e.ch == 'g' && ui_login_guest(u)) return ui_login_close(u, UA_LOGIN_GUEST); break;
    default: break;
    }
    return UI_NONE;
}
