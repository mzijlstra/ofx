// Input: GLFW's keys and buttons as ui/'s events, what a point of the window is over (hit_at),
// presses, the right-click menus' placing, the hover highlights, and the left button's drags: the
// zoom knob, the sidebar's edge, files inside the window (F13), out to other programs and in from
// them (F14, with gui/dnd.c). Included by main.c.

// ---- input ----------------------------------------------------------------------------------

// Shortcuts and the keys without text are read in GLFW's key callback, chained in front of
// raylib's; letters come from the character callback, so the layout and Shift are the
// compositor's (`G`, `~`). The pointer's callbacks are chained the same way.
static GLFWkeyfun         raylib_key;
static bool               script_on;     // a script drives the window: Ctrl+Q quits at once
static bool               user_pointer;  // the script's `user-pointer`: the pointer's input as an unscripted window's
static bool               close_asked;   // the window is to close: the compositor asked, or a quit (Ctrl+Q, Close All's SIGUSR1) waits on the jobs question
static GLFWcharfun        raylib_char;
static GLFWmousebuttonfun raylib_button;
static bool               debug_input;   // OFX_GUI_DEBUG: log every key and button event

// Keys and characters in the order they came (find needs `/`, text, then Enter).
static struct {
    UiEvent ev[UI_EVENTS_MAX];
    int     n;
} evq;

static int mouse_backs, mouse_forwards;

// A key or character since the pointer last moved, clicked or scrolled: no hover highlights then.
// Omarchy's Hyprland hides the pointer on a key (cursor:hide_on_key_press) and sends no leave, so
// the row under where it was stayed lit (Michael, 2026-11-03).
static bool keys_since_pointer;

static void ev_push(UiEvent e)
{
    if (evq.n < UI_EVENTS_MAX) evq.ev[evq.n++] = e;
}

#include "glfwkeys.c"

static void on_key(GLFWwindow *w, int key, int scancode, int action, int mods)
{
    if (debug_input) fprintf(stderr, "ofx: key %d action %d mods %#x\n", key, action, mods);
    // The key's meaning and its command (ui/keys.c); Ctrl+Q's closes every ofx window (F7.1), each
    // asking first when its jobs run or wait (F5.3), a scripted window at once.
    UiEvent e = glfw_event(key, mods, action);
    if (e.cmd == UK_QUIT_ALL && script_on) app.quit = true;
    else if (e.key || e.cmd) ev_push(e);
    if (raylib_key) raylib_key(w, key, scancode, action, mods);
}

// The window's close button or the compositor's close: as Ctrl+Q (asked first with jobs), so
// raylib's own callback, which would end the loop, isn't called.
static void on_close(GLFWwindow *w)
{
    glfwSetWindowShouldClose(w, GLFW_FALSE);
    close_asked = true;
    glfwPostEmptyEvent();
}

static void on_char(GLFWwindow *w, unsigned int cp)
{
    if (debug_input) fprintf(stderr, "ofx: char %#x\n", cp);
    // GLFW sends no character with Alt (Mod1) held, so Alt+1 is a tab only; AltGr (Mod5) still
    // types (F5 review).
    ev_push(ui_event_of((UiChord){ cp, UI_MOD_TYPED }));
    if (raylib_char) raylib_char(w, cp);
}

// The mouse's back and forward (thumb) buttons walk the history. Linux reports them as BTN_SIDE
// and BTN_EXTRA (GLFW buttons 4 and 5) on most mice, BTN_BACK and BTN_FORWARD (7 and 6) on some
// (uml-viewer's finding).
static void on_button(GLFWwindow *w, int button, int action, int mods)
{
    if (debug_input) fprintf(stderr, "ofx: mouse button %d action %d mods %#x\n", button + 1, action, mods);
    if (action == GLFW_PRESS && (button == GLFW_MOUSE_BUTTON_4 || button == GLFW_MOUSE_BUTTON_7)) mouse_backs++;
    if (action == GLFW_PRESS && (button == GLFW_MOUSE_BUTTON_5 || button == GLFW_MOUSE_BUTTON_6)) mouse_forwards++;
    if (raylib_button) raylib_button(w, button, action, mods);
}

static bool in_box(Clay_ElementId id, float x, float y)
{
    Clay_ElementData d = Clay_GetElementData(id);
    return d.found && x >= d.boundingBox.x && x < d.boundingBox.x + d.boundingBox.width && y >= d.boundingBox.y &&
           y < d.boundingBox.y + d.boundingBox.height;
}

// The text bars' fields as layout_* draws them (bar_field's name, "-field"), and their toggles' base
// (layout_toggles): a field a row, as UiField counts them.
static const struct {
    const char *id;
    int         toggles;
} FIELD_IDS[UI_FIELDS] = {
    [UI_FIELD_FIND] = { "find-field", 0 },
    [UI_FIELD_SEARCH] = { "search-field", TOGGLE_SEARCH },
    [UI_FIELD_INSIDE] = { "inside-field", TOGGLE_INSIDE },
};

// The picker panel on top (Open With's, F7.3; the branches', F12 step 6) and its rows' and field's
// element ids; NULL for none.
static const UiPick *pick_on_top(const char **row_id, const char **field_id)
{
    UiOnTop on = modal_kind();
    if (on == UI_ON_OW) return *row_id = "ow-row", *field_id = "ow-field", &app.ui.ow;
    if (on == UI_ON_BRANCHES) return *row_id = "brp-row", *field_id = "brp-field", &app.ui.brp;
    return *row_id = *field_id = NULL, NULL;
}

// What is under a point of the last frame drawn: a list row, a sidebar row, a column header, a
// button, a field (each -1, or UB_NONE, when not).
static Hit hit_at(float x, float y)
{
    // (by name: a field added or moved shifted every value after it, 2026-10-28; the rest 0 or false)
    Hit h = HIT_NONE;
    // Open With's toggles (its panel's)
    for (int k = 1; k <= UI_TOGGLE_ALWAYS && !h.toggle && modal_kind() == UI_ON_OW; k++)   // (what is on top: a question over the panel has the pointer, the F15 code-health review)
        if (in_box(CLAY_IDI("toggle", k), x, y)) h.toggle = k;
    // A menu open: its titles and its entries are all there is to hit (F7.1). The entries first: a
    // long context menu on a low row is moved up over the menu bar, and a press on its top entry
    // opened the title under it (2026-10-29, Open on a bookmark below the drives).
    if (app.ui.menu_on && modal_kind() == UI_ON_NONE)
        for (int i = 0; i < UI_MENU[app.ui.menu].n && h.menu_item < 0; i++)
            if (UI_MENU[app.ui.menu].items[i] != UM_SEP && in_box(CLAY_IDI("menu-item", i), x, y)) h.menu_item = i;   // (one not shown has no box)
    if (modal_kind() == UI_ON_NONE)
        for (int m = 0; m < UI_MENUS && h.menu_title < 0 && h.menu_item < 0; m++)
            if (in_box(CLAY_IDI("menu-title", m), x, y)) h.menu_title = m;
    if (app.ui.menu_on && modal_kind() == UI_ON_NONE) return h;
    if (modal_kind() != UI_ON_NONE) {
        // A panel is up: its buttons are all there is to hit.
        for (int b = 1; b < COUNT(MODAL_IDS) && !h.modal; b++)
            if (in_box(Clay_GetElementId(cz(MODAL_IDS[b])), x, y)) h.modal = b;
        for (int k = 0; modal_kind() == UI_ON_PROPS && k < 9 && h.prop_box < 0; k++)
            if (in_box(CLAY_IDI("prop-box", k), x, y)) h.prop_box = k;
        const char *row_id, *field_id;
        const UiPick *pick = pick_on_top(&row_id, &field_id);
        for (int r = 0; pick && r < pick->list.rows && h.pick_row < 0; r++)
            if (in_box(Clay_GetElementIdWithIndex(cz(row_id), (uint32_t)r), x, y)) h.pick_row = r;
        h.pick_field = pick && !h.toggle && in_box(Clay_GetElementId(cz(field_id)), x, y);   // (Open With's toggles sit in its field)
        for (int r = 0; modal_kind() == UI_ON_BM && r < app.ui.bmp_list.rows && h.bmp_row < 0; r++)
            if (in_box(CLAY_IDI("bmp-row", r), x, y)) h.bmp_row = r;
        for (int f = 0; modal_kind() == UI_ON_LOGIN && f < UI_LOGIN_FIELDS && h.login_field < 0; f++)
            if (in_box(CLAY_IDI("login-field", f), x, y)) h.login_field = f;
        for (int k = 1; modal_kind() == UI_ON_BM && k <= UI_BMP_BTNS && !h.bmp_btn; k++)
            if (in_box(CLAY_IDI("bmp-btn", k), x, y)) h.bmp_btn = k;
        return h;
    }
    if (app.ui.finding) {
        h.find_x = in_box(CLAY_ID("find-x"), x, y);
        h.find_step = in_box(CLAY_ID("find-prev"), x, y) ? -1 : in_box(CLAY_ID("find-next"), x, y) ? 1 : 0;
    }
    // The text bars' fields (UiField) and their toggles, each where its bar shows (below a panel or
    // a menu, none: a click on Properties' OK above a toggle flipped it, 2026-10-26).
    for (int f = 0; f < UI_FIELDS && h.field < 0; f++) {
        if (!ui_field_shown(&app.ui, (UiField)f)) continue;
        for (int k = 1; k <= UI_TOGGLES && !h.field_toggle; k++)
            if (in_box(CLAY_IDI("toggle", FIELD_IDS[f].toggles + k), x, y)) h.field = f, h.field_toggle = k;
        if (h.field < 0 && in_box(Clay_GetElementId(cz(FIELD_IDS[f].id)), x, y)) h.field = f;
    }
    h.inside_btn = app.ui.view == UI_VIEW_SEARCH && in_box(CLAY_ID("search-inside"), x, y);
    for (int i = 0; tabbar.n >= 2 && i < tabbar.n && h.tab < 0; i++) {
        if (in_box(CLAY_IDI("tab-x", i), x, y)) h.tab_x = h.tab = i;
        else if (in_box(CLAY_IDI("tab", i), x, y)) h.tab = i;
    }
    for (int b = 0; b < COUNT(BAR_IDS) && h.bar == UB_NONE; b++)
        if (BAR_IDS[b] && in_box(bar_id((UiBar)b), x, y)) h.bar = (UiBar)b;
    for (int i = app.crumb_lo; h.bar == UB_NONE && i < app.crumb_hi; i++)
        if (in_box(CLAY_IDI("crumb", i), x, y)) h.bar = UB_CRUMB, h.crumb = i;
    // The cursor's full name in the icon view lies over the cells below it; a press there is the
    // cell's underneath, so a double click never opens the cursor's entry from another's place
    // (F6.2 review).
    for (int r = 0; r < list_cells() && h.row < 0; r++)
        if (in_box(CLAY_IDI("row", r), x, y)) h.row = r;
    for (int i = 0; i < jobs.nrun && h.job_x < 0; i++)
        if (in_box(CLAY_IDI("job-x", i), x, y)) h.job_x = i;
    h.side_edge = side_w() && in_box(CLAY_ID("side-edge"), x, y);   // (F13: before the rows it lies over)
    for (int r = 0; side_w() && !h.side_edge && r < app.ui.side.rows && h.side < 0; r++)
        if (in_box(CLAY_IDI("side-row", r), x, y)) h.side = r;
    if (h.side >= 0 && app.ui.side_mode == SIDE_TREE && in_box(CLAY_IDI("side-twisty", h.side), x, y)) h.twisty = h.side;
    if (h.side >= 0 && app.ui.side_mode == SIDE_PLACES && in_box(CLAY_IDI("side-eject", h.side), x, y)) h.eject = h.side;
    for (int k = 0; k < SIDE_MODES && h.mode < 0; k++)
        if (in_box(CLAY_IDI("mode", k), x, y)) h.mode = k;
    h.zoom = in_box(CLAY_ID("zoom-track"), x, y);   // only drawn in the icon view
    for (int k = 0; k < SORT_KEYS && h.header < 0; k++)
        if (in_box(Clay_GetElementId(cz(COL_IDS[k])), x, y)) h.header = k;
    return h;
}

#include "drag.c"   // (the drags: F13 inside the window, F14 with other programs)

// A left press at a point, into in: a list row's goes through the double-click timing (not with
// Ctrl or Shift held, which mark rows: UI_MOD_*).
static void press_at(UiInput *in, float x, float y, int mods)
{
    Hit h = hit_at(x, y);
    // The menu bar (F7.1): with a menu open a press elsewhere only closes it.
    in->menu_title = h.menu_title, in->menu_item = h.menu_item;
    if (app.ui.menu_on) {
        in->menu_away = h.menu_title < 0 && h.menu_item < 0;
        return;
    }
    if (h.row >= 0) {
        if (!mods && ui_press(&app.press, h.row, now_ms()) == 2) in->dclick = h.row;
        else in->click = h.row, in->click_mods = mods;
    }
    in->side_click = h.twisty >= 0 || h.eject >= 0 ? -1 : h.side;   // the mark opens or closes; the rest of the row goes there
    in->side_eject = h.eject;
    in->header = h.header;
    in->bar = h.bar, in->crumb = h.crumb;
    in->modal = h.modal;
    in->prop_box = h.prop_box;
    const char *row_id, *field_id;
    const UiPick *pick = pick_on_top(&row_id, &field_id);
    if (h.pick_row >= 0 && pick) {
        if (ui_press(&app.panel_press, pick->list.top + h.pick_row, now_ms()) == 2) in->pick_drow = h.pick_row;
        else in->pick_row = h.pick_row;
    }
    in->pick_field = h.pick_field;
    if (h.bmp_row >= 0) {
        if (ui_press(&app.panel_press, app.ui.bmp_list.top + h.bmp_row, now_ms()) == 2) in->bmp_drow = h.bmp_row;
        else in->bmp_row = h.bmp_row;
    }
    in->bmp_btn = h.bmp_btn;
    in->login_field = h.login_field;
    in->toggle = h.toggle;
    in->find_x = h.find_x, in->find_step = h.find_step;
    in->field = h.field, in->field_toggle = h.field_toggle, in->inside_btn = h.inside_btn;
    in->job_x = h.job_x;
    in->side_mode = h.mode;
    in->side_twisty = h.twisty;
    // A plain press on an entry, or on a tree row's folder, may become a drag of files (F13 step 3)
    // once it moves UI_DRAG_PX: not the second press of a double click, not in the trash view
    // (Restore is its own), not a drive's root.
    if (!mods && in->dclick < 0 && h.row >= 0 && app.ui.view != UI_VIEW_TRASH && app.have_list && app.ui.list.top + h.row < app.list.n)
        drag_arm_entry(x, y, app.ui.list.top + h.row);
    else if (!mods && h.side >= 0 && h.twisty < 0 && app.ui.side_mode == SIDE_TREE && app.ui.side.top + h.side < tree.n &&
             tree.row[app.ui.side.top + h.side].depth > 0) {
        // (a tree row opens when the button is let go, a drag's never: else a drag of it went into it first)
        drag_arm(x, y, tree.row[app.ui.side.top + h.side].path, false, true, S("folder"), true);
        app.drag_side = in->side_click, in->side_click = -1;
    }
    // The zoom track: the size nearest the press, and the knob held until the button is let go.
    if (h.zoom) in->zoom_to = zoom_level_at(x), app.drag = DRAG_ZOOM;
    // The sidebar's edge (F13): held, it follows the pointer; a double click gives UI_SIDE_W back.
    if (h.side_edge && ui_press(&app.edge_press, 0, now_ms()) == 2) side_keep(UI_SIDE_W), app.edge_press.row = -1;
    else if (h.side_edge) app.drag = DRAG_SIDE, app.drag_x0 = x, app.drag_w0 = side_w(), app.drag_moved = false;   // (the width kept as it is until it moves: a narrow window's fitted one isn't it)
    if (h.tab_x >= 0) in->tab_close = h.tab_x;
    else if (h.tab >= 0) in->tab_click = h.tab;
}

// A middle press at a point: a folder (a row, a place, a path button) in a background tab, or a
// tab closed.
static void middle_at(UiInput *in, float x, float y)
{
    Hit h = hit_at(x, y);   // a menu open: only its titles and entries, so nothing here (F7.1)
    if (h.tab >= 0) in->tab_close = h.tab;
    else if (h.row >= 0) in->mid_row = h.row;
    else if (h.side >= 0) in->mid_side = h.side;
    else if (h.bar == UB_CRUMB) in->mid_crumb = h.crumb;
}

// A right press at a point (F8): a row's menu, or the folder shown's on the files' empty space;
// with a menu open it only closes it, as a left press elsewhere does. Where it opens: here.
static void right_at(UiInput *in, float x, float y)
{
    Hit h = hit_at(x, y);
    if (app.ui.menu_on) {
        in->menu_away = true;
        return;
    }
    if (modal_kind() != UI_ON_NONE) return;
    app.ctx_x = x, app.ctx_y = y;
    if (h.tab >= 0) in->ctx_tab = h.tab;
    else if (h.row >= 0) in->ctx_row = h.row;
    else if (h.side >= 0) in->ctx_side = h.side;
    else if (h.bar == UB_CRUMB) in->ctx_crumb = h.crumb;
    else if (in_box(CLAY_ID("list"), x, y)) in->ctx_here = true;
}

// A right-click menu's size as layout_menu draws it (F8), before it is drawn: its rows and lines,
// the widest entry (its label as drawn, menu_label; the spring; its key, a gap each side when it has
// one), the marks' column, the paddings (topbar.c's MENU_* sizes, which layout_menu draws with), and
// its 1-pixel border round the box. The state line's menusized checks it against the drawn box.
static Vector2 menu_size(int m)
{
    float row = 0, hgt = 2 * MENU_PAD;
    for (int i = 0; i < UI_MENU[m].n; i++) {
        UiMenuItem it = UI_MENU[m].items[i];
        if (it == UM_SEP ? !ui_menu_sep_shown(&app.ui, m, i) : !ui_menu_shown(&app.ui, it)) continue;   // (as layout_menu leaves them out)
        if (it == UM_SEP) {
            hgt += MENU_SEP_H;
            continue;
        }
        hgt += MENU_ROW_H;
        Clay_String label = menu_label(it);
        const char *key = ui_menu_key(it);
        float w = str_width((Str){ label.chars, label.length }) + MENU_GAP + (key[0] ? MENU_GAP + str_width(str_from_c(key)) : 0);   // (the spring between them grows from 0)
        if (w > row) row = w;
    }
    float wid = 2 * MENU_PAD + 2 * MENU_ITEM_PAD + (menu_marks(m) ? MENU_MARK_W + MENU_MARK_GAP : 0) + row;
    return (Vector2){ (wid > MENU_W_MIN ? wid : MENU_W_MIN) + 2, hgt + 2 };   // (+2: its border, drawn outside the box)
}

// A right-click menu just opened (F8): its top left. By the pointer, at the press, flipped left or
// up where it would leave the window; by a key, under the cursor's entry (above it near the
// bottom), or the files' top left with none drawn. Kept inside the window either way.
static void ctx_place(void)
{
    Vector2 sz = menu_size(app.ui.menu);
    float w = (float)GetScreenWidth(), h = (float)GetScreenHeight(), x = app.ctx_x, y = app.ctx_y;
    Clay_ElementData br = app.ui.menu == UI_CTX_BRANCH ? Clay_GetElementData(Clay_GetElementId(cz(BAR_IDS[UB_BRANCH]))) : (Clay_ElementData){ 0 };
    if (br.found) {   // under the branch button, its left edge (F12 step 5b)
        x = br.boundingBox.x, y = br.boundingBox.y + br.boundingBox.height;
    } else if (app.ui.ctx_key) {
        Clay_ElementData r = Clay_GetElementData(CLAY_IDI("row", (int)(app.ui.list.sel - app.ui.list.top)));
        Clay_ElementData l = Clay_GetElementData(CLAY_ID("list"));
        Clay_ElementData f = app.ui.menu != UI_CTX_FOLDER ? (Clay_ElementData){ 0 }
                             : app.ui.ctx_crumb >= 0 ? Clay_GetElementData(CLAY_IDI("crumb", app.ui.ctx_crumb))
                                                     : Clay_GetElementData(CLAY_IDI("side-row", (int)(app.ui.ctx_side - app.ui.side.top)));
        if (f.found) {   // under the sidebar row or path button (F8)
            x = f.boundingBox.x + PAD, y = f.boundingBox.y + f.boundingBox.height;
            if (y + sz.y > h) y = f.boundingBox.y - sz.y;
        } else if (app.ui.menu == UI_CTX_ENTRY && r.found) {
            x = r.boundingBox.x + (app.ui.grid ? r.boundingBox.width / 2 : 2 * PAD + ICON_PX);
            y = r.boundingBox.y + r.boundingBox.height;
            if (y + sz.y > h) y = r.boundingBox.y - sz.y;
        } else if (l.found) {
            x = l.boundingBox.x + PAD, y = l.boundingBox.y + PAD;
        }
    } else {
        if (x + sz.x > w) x -= sz.x;
        if (y + sz.y > h) y -= sz.y;
    }
    if (x + sz.x > w) x = w - sz.x;
    if (y + sz.y > h) y = h - sz.y;
    app.ctx_x = x < 0 ? 0 : x, app.ctx_y = y < 0 ? 0 : y;
}

// A folder menu just opened (F8): its folder, and what ui/ shows for it (Ui.ctx_*): Trash; a place
// (a bookmark's line in the file, a drive's eject); a bookmark of it already.
static void ctx_facts(void)
{
    Ui *u = &app.ui;
    char buf[PATH_MAX];
    const char *p = u->ctx_crumb >= 0 && u->ctx_crumb < app.crumbs.n
                        ? (snprintf(buf, sizeof buf, "%.*s", STR_ARG(ui_crumb_path(&app.crumbs, u->ctx_crumb))), buf)
                        : side_path(u->ctx_side, buf, sizeof buf);
    if (p && is_search(p)) p = app.search_root;   // the "Search" button's: the folder searched (F9), never its address
    snprintf(app.ctx_path, sizeof app.ctx_path, "%s", p ? p : "");
    u->ctx_w = app.ctx_path[0] && can_write_at(app.ctx_path);
    u->ctx_trash = strcmp(app.ctx_path, TRASH_URI) == 0;
    u->ctx_full = u->ctx_trash && (trash_state(), app.trash_full);   // (the icon's answer, looked at again)
    u->ctx_bmed = bm_find(&bmarks, app.ctx_path) >= 0;
    u->ctx_bms = bmarks.n;
    if (u->ctx_crumb < 0 && u->side_mode == SIDE_PLACES && u->ctx_side >= 0 && u->ctx_side < app.places.n) {
        const Place *pl = &app.places.p[u->ctx_side];
        u->ctx_place = true;
        u->ctx_eject = place_ejectable(pl);
        u->ctx_unmounted = pl->kind == PL_UNMOUNTED;
        if (pl->kind == PL_BOOKMARK) u->ctx_bmk = bm_find(&bmarks, pl->path);
    }
}

// The folder menu's entry it (UA_FOLDER_DO, F8), for its folder: as the files' entries do for one of
// theirs.
static void folder_do(UiMenuItem it)
{
    const char *dir = app.ctx_path;
    const Place *ctx_pl = app.ui.ctx_side >= 0 && app.ui.ctx_side < app.places.n ? &app.places.p[app.ui.ctx_side] : NULL;
    if (it == UM_MOUNT_IT && ctx_pl && ctx_pl->kind == PL_UNMOUNTED) mount_start(ctx_pl);   // (no folder yet: before the check below)
    if (!dir[0]) return;
    switch (it) {
    case UM_OPEN_IT: go_to(dir, STR_EMPTY, 0); break;
    case UM_OPEN_TAB: tab_open_bg(dir); break;
    case UM_SEARCH_IN: search_in(dir); break;
    case UM_OPEN_WINDOW: window_on(dir); break;
    case UM_TERMINAL_IN: terminal_in(dir); break;
    case UM_CUT_IT: case UM_COPY_IT: copy_targets(it == UM_CUT_IT, dir); break;
    case UM_COPY_PATH_IT: path_copy(dir); break;
    case UM_PASTE_INTO: paste(dir); break;
    case UM_BOOKMARK_IT: bookmark_add(dir); break;
    case UM_EJECT_IT:
    case UM_UNMOUNT_IT:
        if (ctx_pl && place_ejectable(ctx_pl)) (it == UM_EJECT_IT ? eject_start : unmount_start)(ctx_pl);
        break;
    default: break;
    }
}

static int cursor_now = MOUSE_CURSOR_DEFAULT;   // the pointer's shape set last

// The pointer's shape (F13): the resize arrows over the sidebar's edge and while it is dragged.
static void cursor_shape(void)
{
    int want = app.hov.side_edge || app.drag == DRAG_SIDE ? MOUSE_CURSOR_RESIZE_EW : MOUSE_CURSOR_DEFAULT;
    if (want != cursor_now) SetMouseCursor(want), cursor_now = want;
}

static float wheel_left;   // a touchpad's part of a notch, carried to the next frame

// The wheel's notches at x: the files scroll, or with Ctrl held their icons zoom (F6.3); the
// sidebar scrolls by itself. True when the sidebar moved.
static bool wheel_at(UiInput *in, float x, int notches, bool ctrl)
{
    UiOnTop on = modal_kind();   // (what is on top, not the panel under a question: the F15 code-health review)
    if (on == UI_ON_OW || on == UI_ON_KEYS || on == UI_ON_BRANCHES) return in->panel_wheel = notches, false;   // the panel's list (Open With's, F7.3; Keyboard Shortcuts', F7.5; the branches', F12 step 6)
    if (on != UI_ON_NONE) return false;   // (a question or another panel: nothing under it scrolls, the group D review)
    if (x >= side_w() && ctrl) in->zoom_wheel = notches;
    else if (x >= side_w()) in->wheel = notches;
    else if (notches) return app.ui.side.top = ui_clamp(app.ui.side.top - notches, 0, ui_max_top(&app.ui.side)), true;
    return false;
}

static int tip_target(Hit h)
{
    switch (h.bar) {
    case UB_BACK: case UB_FORWARD: case UB_UP: case UB_LOCATION: case UB_SEARCH: case UB_LEFT: case UB_RIGHT:
    case UB_VIEW_LIST: case UB_VIEW_ICONS: case UB_ZOOM_OUT: case UB_ZOOM_IN: case UB_BRANCH: return TIP_BAR + h.bar;
    default: break;
    }
    if (h.zoom) return TIP_ZOOM;
    if (h.find_step) return TIP_FIND + (h.find_step > 0);
    if (h.mode >= 0) return TIP_MODE + h.mode;
    if (h.eject >= 0) return TIP_EJECT + h.eject;
    if (h.tab_x >= 0) return TIP_TAB_X + h.tab_x;
    return -1;
}

// The left button let go: a width that moved is kept (and its press is no double click's first),
// the files dropped where they are over, a press on a tree row that didn't drag opens it. True when
// what is drawn changed.
static bool drag_release(UiInput *in)
{
    if (app.drag == DRAG_NONE || drag_by_compositor()) return false;
    if (app.drag == DRAG_SIDE && app.drag_moved) side_keep(app.side_px), app.edge_press.row = -1;
    if (app.drag == DRAG_FILES && app.drop.ok) drop_do(&app.drop);
    if (app.drag == DRAG_PRESS && app.drag_tree) in->side_click = app.drag_side;
    bool drawn = app.drag != DRAG_PRESS;
    drag_end();
    return drawn;
}

// The sidebar's edge held at x: the width there (F13), kept as it was until it moves.
static bool side_drag_at(float x)
{
    int w = ui_side_fit(app.drag_w0 + (int)(x - app.drag_x0), GetScreenWidth());
    if (w == (app.drag_moved ? app.side_px : app.drag_w0)) return false;
    app.side_px = w, app.drag_moved = true;
    return true;
}

// Files dragged to x, y: the badge follows, and where they would land is found (F13 step 3); past
// the window's edge they go out to other programs (F14 step 2).
static bool files_drag_at(float x, float y)
{
    if ((x < 0 || y < 0 || x >= (float)GetScreenWidth() || y >= (float)GetScreenHeight()) && drag_out()) return true;
    app.drag_px = x, app.drag_py = y;
    drop_find(x, y);
    return true;
}

// What the left button holds, the pointer at x, y: the zoom slider's knob takes the size nearest it
// (F6.3), the sidebar's edge the width there (F13); a press on files past UI_DRAG_PX drags them,
// and the drag finds where they would land (F13 step 3). The button let go, it is dropped: a width
// that changed is remembered, the files dropped where they are over. True when what is drawn
// changed (a drop is drawn unlit).
static bool drag_at(UiInput *in, float x, float y, bool held)
{
    if (!held) return drag_release(in);
    switch (app.drag) {
    case DRAG_ZOOM: in->zoom_to = zoom_level_at(x); return false;
    case DRAG_SIDE: return side_drag_at(x);
    case DRAG_PRESS:
        if (!ui_drag_past(x - app.drag_x0, y - app.drag_y0)) return false;
        if (!drag_start()) return app.drag = DRAG_NONE, false;
        app.drag = DRAG_FILES, app.out_failed = false;
        drop_reset(DROP_AGAIN);
        app.press.row = -1;   // (a drag's press is no double click's first: the next press on that row would open it)
        return files_drag_at(x, y);
    case DRAG_FILES: return files_drag_at(x, y);
    default: return false;
    }
}

static bool hover_at(float x, float y);

// When the timer wakes the loop: a tooltip due, the search field's text to search for (F9), a
// state write waiting (a folder's view, remember.c; this window's tabs, restore.c; F11), a git
// status ask put off (git.c's once a second, F12), or a drag's tab to open (F13 step 4).
static i64 wake_due(void)
{
    i64 d[] = { ui_tip_due(&app.tip), app.search_due, rem.due, restore.due, app.spring_tab >= 0 ? app.spring_at + UI_SPRING_MS : 0, git.due,
                app.far_loading && !app.far_slow ? app.far_since + FAR_ANSWER_MS : 0 };
    return ui_due_min(d, COUNT(d));
}

// The hover highlights for the pointer where it is now, not where it last moved: none once it has
// left the window (GLFW keeps its last position), and again after a frame that moved things under
// a still pointer (a folder entered, the view or the size changed). Else a cell stayed lit that
// the pointer wasn't on (Michael, 2026-10-24). True when any changed.
static bool hover_refresh(bool on, float x, float y)
{
    return on && !keys_since_pointer ? hover_at(x, y) : hover_at(-1e6f, -1e6f);   // (a key since: none, for every caller; code-health review, 2026-11-04)
}

// What is under the pointer at x, y, for the hover highlights: true when any changed.
static bool hover_at(float x, float y)
{
    Hit h = hit_at(x, y);
    bool changed = !hit_same(h, app.hov);   // (a menu's entries: ui_menu_hover's, below)
    app.hov = h;
    changed |= ui_menu_hover(&app.ui, h.menu_title, h.menu_item);
    // Onto another icon button (or off one): its tooltip waits UI_TIP_MS, the timer wakes the loop.
    if (ui_tip_hover(&app.tip, tip_target(h), now_ms())) {
        changed = true;
        timer_at(wake_due());
    }
    return changed;
}

// Ctrl and Shift as held now (UI_MOD_*).
static int mods_held(void)
{
    return (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ? UI_MOD_CTRL : 0) |
           (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? UI_MOD_SHIFT : 0);
}

// The user's input since the last frame. *seen: anything that could change what is on screen.
static UiInput input_real(bool *seen)
{
    UiInput in = ui_input_none();
    memcpy(in.ev, evq.ev, sizeof *evq.ev * (size_t)evq.n);
    in.nev = evq.n;
    bool evq_keys = evq.n > 0;
    *seen = evq.n > 0 || IsWindowResized();
    evq.n = 0;
    while (GetKeyPressed() != 0) *seen = true;
    while (GetCharPressed() != 0) {}
    Vector2 m = GetMousePosition(), d = GetMouseDelta();
    if (evq_keys) keys_since_pointer = true;
    if (d.x != 0 || d.y != 0 || GetMouseWheelMove() != 0 || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) ||
        IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
        keys_since_pointer = false;
    wheel_left += GetMouseWheelMove();
    int notches = (int)wheel_left;
    wheel_left -= (float)notches;
    *seen |= wheel_at(&in, m.x, notches, mods_held() & UI_MOD_CTRL);
    bool real = !script_on || user_pointer;   // (the pointer's own hover, drags and keys held)
    if (real) *seen |= hover_refresh(IsCursorOnScreen(), m.x, m.y);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) press_at(&in, m.x, m.y, mods_held());
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) middle_at(&in, m.x, m.y);
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) right_at(&in, m.x, m.y);
    bool held = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    // Off the window the drag ends: Wayland's leave releases no button, and a compositor grab
    // (a keybind, a screenshot) can take the release (F6.3 review).
    bool on = IsCursorOnScreen();
    // (a script's own: press-here, move-to, release.) Off the window a drag of files is cancelled,
    // not dropped where it last was.
    if (real) app.drag_mods = mods_held();
    if (real && !on && drag_of_files() && !drag_by_compositor()) drag_cancel(), *seen = true;
    else if (real && (d.x != 0 || d.y != 0 || !held || !on)) *seen |= drag_at(&in, m.x, m.y, held && on);
    in.backs = mouse_backs, mouse_backs = 0;
    in.forwards = mouse_forwards, mouse_forwards = 0;
    // Anything the user did draws a frame: one list, ui_input_acts (unit-tested), so a new click
    // can't be left out. The mode buttons and the tree's marks were (F6, Michael 2026-10-23: a
    // mode click showed only when the pointer next moved).
    *seen |= in.wheel != 0 || ui_input_acts(&in);
    return in;
}

// For find: the name of entry i.
static Str list_name(void *ctx, isize i)
{
    (void)ctx;
    return app.list.e[i].name;
}

// For F2: whether entry i is a folder (its whole name selected).
static bool list_dir(void *ctx, isize i)
{
    (void)ctx;
    return kind_is_dir(app.list.e[i].kind);
}
