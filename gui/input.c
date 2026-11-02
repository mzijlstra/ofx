// Input: GLFW's keys and buttons as ui/'s events, what a point of the window is over (hit_at),
// presses, the right-click menus' placing, the hover highlights. Included by main.c.

// ---- input ----------------------------------------------------------------------------------

// Shortcuts and the keys without text are read in GLFW's key callback, chained in front of
// raylib's; letters come from the character callback, so the layout and Shift are the
// compositor's (`G`, `~`). The pointer's callbacks are chained the same way.
static GLFWkeyfun         raylib_key;
static bool               script_on;     // a script drives the window: Ctrl+Q quits at once
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

static void ev_push(UiEvent e)
{
    if (evq.n < UI_EVENTS_MAX) evq.ev[evq.n++] = e;
}

// A key and its modifiers as ui/ names it; UK_NONE for the rest (letters come as characters).
static UiKey ui_key_of(int key, int mods)
{
    int m = mods & (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT | GLFW_MOD_ALT | GLFW_MOD_SUPER);
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_F) return UK_FIND;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_H) return UK_HIDDEN;
    if (m == GLFW_MOD_ALT && key == GLFW_KEY_LEFT) return UK_BACK;
    if (m == GLFW_MOD_ALT && key == GLFW_KEY_RIGHT) return UK_FORWARD;
    if (m == GLFW_MOD_ALT && key == GLFW_KEY_UP) return UK_PARENT;
    if (m == GLFW_MOD_ALT && key == GLFW_KEY_HOME) return UK_HOMEDIR;   // (F7.4, as Nemo)
    if (m == GLFW_MOD_ALT && key == GLFW_KEY_END) return UK_TRASH_VIEW;   // (Michael, 2026-10-27)
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_D) return UK_ADD_BOOKMARK;   // (F7.5, as Nemo)
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_B) return UK_EDIT_BOOKMARKS;
    if ((m == 0 || m == GLFW_MOD_CONTROL) && key == GLFW_KEY_F1) return UK_SHORTCUTS;   // (F7.5: F1 and Ctrl+F1, Nemo's two)
    if (m == GLFW_MOD_ALT && key == GLFW_KEY_DOWN) return UK_ALT_DOWN;   // (Edit Bookmarks' move down)
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_R) return UK_RELOAD;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_P) return UK_PERMS_COLS;   // (Michael, 2026-10-27)
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_G) return UK_GROUP_COL;
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_I) return UK_INVERT;
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_A) return UK_SELECT_MATCH;
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_F) return UK_SEARCH;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_T) return UK_TAB_NEW;
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_T) return UK_TAB_REOPEN;   // (F11)
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_N) return UK_NEW_WINDOW;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_I) return UK_PROPERTIES;
    if (m == GLFW_MOD_ALT && (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)) return UK_TERMINAL;
    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)) return UK_CONTAINING;
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)) return UK_OPEN_LINE;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_W) return UK_TAB_CLOSE;
    if ((m == GLFW_MOD_CONTROL && key == GLFW_KEY_TAB) || (m == GLFW_MOD_CONTROL && key == GLFW_KEY_PAGE_DOWN)) return UK_TAB_NEXT;
    if ((m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_TAB) || (m == GLFW_MOD_CONTROL && key == GLFW_KEY_PAGE_UP)) return UK_TAB_PREV;
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_PAGE_UP) return UK_TAB_LEFT;
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_PAGE_DOWN) return UK_TAB_RIGHT;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_L) return UK_LOCATION;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_K) return UK_CRUMBS;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_J) return UK_JOBS;
    // Ctrl and an arrow: the keys to the part of the window that way (Michael, 2026-10-24); Ctrl+S
    // the sidebar's next mode, as F9.
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_LEFT) return UK_WORD_LEFT;   // (outside a text field: the sidebar)
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_LEFT) return UK_SEL_WORD_LEFT;
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_RIGHT) return UK_SEL_WORD_RIGHT;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_BACKSPACE) return UK_WORD_BACKSPACE;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_DELETE) return UK_WORD_DELETE;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_UP) return UK_CRUMBS;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_DOWN) return UK_JOBS;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_RIGHT) return UK_WORD_RIGHT;   // (outside a text field: UK_LOCATION)
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_S) return UK_F9;
    // The views in the top bar's order, list first (Michael, 2026-10-24; Nemo has them the other way).
    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_1 || key == GLFW_KEY_KP_1)) return UK_VIEW_LIST;
    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_2 || key == GLFW_KEY_KP_2)) return UK_VIEW_ICONS;
    // The icon view's sizes (F6.3), as Nemo's: Ctrl+= or Ctrl++ (Shift+= on most layouts), Ctrl+-,
    // Ctrl+0, and the keypad's.
    if ((m == GLFW_MOD_CONTROL || m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT)) && (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD)) return UK_ZOOM_IN;
    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT)) return UK_ZOOM_OUT;
    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_0 || key == GLFW_KEY_KP_0)) return UK_ZOOM_RESET;
    if (m == GLFW_MOD_SHIFT) {
        switch (key) {
        case GLFW_KEY_LEFT: return UK_SEL_LEFT;
        case GLFW_KEY_RIGHT: return UK_SEL_RIGHT;
        case GLFW_KEY_HOME: return UK_SEL_HOME;
        case GLFW_KEY_END: return UK_SEL_END;
        case GLFW_KEY_UP: return UK_SEL_UP;
        case GLFW_KEY_DOWN: return UK_SEL_DOWN;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER: return UK_SHIFT_ENTER;
        default: break;
        }
    }
    if (m == GLFW_MOD_CONTROL) {
        switch (key) {
        case GLFW_KEY_A: return UK_ALL;
        case GLFW_KEY_C: return UK_COPY;
        case GLFW_KEY_X: return UK_CUT;
        case GLFW_KEY_V: return UK_PASTE;
        case GLFW_KEY_Z: return UK_UNDO;
        case GLFW_KEY_M: return UK_MAKE_LINK;   // (F8, as Nemo)
        default: break;
        }
    }
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_N) return UK_NEWFOLDER;
    if (key == GLFW_KEY_DELETE || key == GLFW_KEY_KP_DECIMAL) {
        if (key == GLFW_KEY_KP_DECIMAL) return UK_NONE;   // the keypad's . types a dot
        return m == GLFW_MOD_SHIFT ? UK_SHIFT_DELETE : m == 0 ? UK_DELETE : UK_NONE;
    }
    if ((m == GLFW_MOD_SHIFT && key == GLFW_KEY_F10) || (m == 0 && key == GLFW_KEY_MENU)) return UK_CONTEXT;   // the right-click menu (F8, as GTK)
    if (m & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER)) return UK_NONE;
    switch (key) {
    case GLFW_KEY_UP: return UK_UP;
    case GLFW_KEY_DOWN: return UK_DOWN;
    case GLFW_KEY_PAGE_UP: return UK_PAGEUP;
    case GLFW_KEY_PAGE_DOWN: return UK_PAGEDOWN;
    case GLFW_KEY_HOME: return UK_HOME;
    case GLFW_KEY_END: return UK_END;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER: return UK_ENTER;
    case GLFW_KEY_RIGHT: return UK_RIGHT;
    case GLFW_KEY_LEFT: return UK_LEFT;
    case GLFW_KEY_BACKSPACE: return UK_BACKSPACE;
    case GLFW_KEY_ESCAPE: return UK_ESC;
    case GLFW_KEY_TAB: return m == 0 || m == GLFW_MOD_SHIFT ? UK_TAB : UK_NONE;   // Shift+Tab: back, where there are two places (F7.3)
    case GLFW_KEY_F2: return m == 0 ? UK_F2 : UK_NONE;
    case GLFW_KEY_F9: return m == 0 ? UK_F9 : UK_NONE;
    case GLFW_KEY_F10: return m == 0 ? UK_F10 : UK_NONE;
    default: return UK_NONE;
    }
}

static void on_key(GLFWwindow *w, int key, int scancode, int action, int mods)
{
    if (debug_input) fprintf(stderr, "ofx: key %d action %d mods %#x\n", key, action, mods);
    // Ctrl+Q closes every ofx window (F7.1), each asking first when its jobs run or wait (F5.3);
    // a scripted window quits at once.
    if (action == GLFW_PRESS && key == GLFW_KEY_Q && (mods & GLFW_MOD_CONTROL)) {
        if (script_on) app.quit = true;
        else ev_push((UiEvent){ UK_QUIT_ALL, 0 });
    }
    UiKey k = action != GLFW_RELEASE ? ui_key_of(key, mods) : UK_NONE;
    if (k) ev_push((UiEvent){ k, 0 });
    // Alt+1 .. Alt+9: a tab by place (the digit rides in ch).
    int m = mods & (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT | GLFW_MOD_ALT | GLFW_MOD_SUPER);
    if (action == GLFW_PRESS && m == GLFW_MOD_ALT && key >= GLFW_KEY_1 && key <= GLFW_KEY_9) ev_push((UiEvent){ UK_TAB_JUMP, (u32)(key - GLFW_KEY_0) });
    // Alt and a letter: a menu (F7.1); ui/ knows which letters have one.
    if (action == GLFW_PRESS && m == GLFW_MOD_ALT && key >= GLFW_KEY_A && key <= GLFW_KEY_Z) ev_push((UiEvent){ UK_MENU, (u32)('a' + key - GLFW_KEY_A) });
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
    ev_push((UiEvent){ UK_NONE, cp });
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

// What is under a point of the last frame drawn: a list row, a sidebar row, a column header, a
// button, a field (each -1, or UB_NONE, when not).
static Hit hit_at(float x, float y)
{
    // (by name: a field added or moved shifted every value after it, 2026-10-28; the rest 0 or false)
    Hit h = HIT_NONE;
    // Open With's toggles (its panel's)
    for (int k = 1; k <= UI_TOGGLE_ALWAYS && !h.toggle && app.ui.panel == UI_PANEL_OW; k++)
        if (in_box(CLAY_IDI("toggle", k), x, y)) h.toggle = k;
    // A menu open: its titles and its entries are all there is to hit (F7.1). The entries first: a
    // long context menu on a low row is moved up over the menu bar, and a press on its top entry
    // opened the title under it (2026-10-29, Open on a bookmark below the drives).
    if (app.ui.menu_on && modal_kind() == MODAL_NONE)
        for (int i = 0; i < UI_MENU[app.ui.menu].n && h.menu_item < 0; i++)
            if (UI_MENU[app.ui.menu].items[i] != UM_SEP && in_box(CLAY_IDI("menu-item", i), x, y)) h.menu_item = i;   // (one not shown has no box)
    if (modal_kind() == MODAL_NONE)
        for (int m = 0; m < UI_MENUS && h.menu_title < 0 && h.menu_item < 0; m++)
            if (in_box(CLAY_IDI("menu-title", m), x, y)) h.menu_title = m;
    if (app.ui.menu_on && modal_kind() == MODAL_NONE) return h;
    if (modal_kind() != MODAL_NONE) {
        // A panel is up: its buttons are all there is to hit.
        for (int b = 1; b < COUNT(MODAL_IDS) && !h.modal; b++)
            if (in_box(Clay_GetElementId(cz(MODAL_IDS[b])), x, y)) h.modal = b;
        for (int k = 0; modal_kind() == MODAL_PROPS && k < 9 && h.prop_box < 0; k++)
            if (in_box(CLAY_IDI("prop-box", k), x, y)) h.prop_box = k;
        for (int r = 0; modal_kind() == MODAL_OW && r < app.ui.ow_list.rows && h.ow_row < 0; r++)
            if (in_box(CLAY_IDI("ow-row", r), x, y)) h.ow_row = r;
        h.ow_field = modal_kind() == MODAL_OW && !h.toggle && in_box(CLAY_ID("ow-field"), x, y);
        for (int r = 0; modal_kind() == MODAL_BM && r < app.ui.bmp_list.rows && h.bmp_row < 0; r++)
            if (in_box(CLAY_IDI("bmp-row", r), x, y)) h.bmp_row = r;
        for (int k = 1; modal_kind() == MODAL_BM && k <= UI_BMP_BTNS && !h.bmp_btn; k++)
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
    for (int r = 0; side_w() && r < app.ui.side.rows && h.side < 0; r++)
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
    if (h.ow_row >= 0) {
        if (ui_press(&app.panel_press, app.ui.ow_list.top + h.ow_row, now_ms()) == 2) in->ow_drow = h.ow_row;
        else in->ow_row = h.ow_row;
    }
    in->ow_field = h.ow_field;
    if (h.bmp_row >= 0) {
        if (ui_press(&app.panel_press, app.ui.bmp_list.top + h.bmp_row, now_ms()) == 2) in->bmp_drow = h.bmp_row;
        else in->bmp_row = h.bmp_row;
    }
    in->bmp_btn = h.bmp_btn;
    in->toggle = h.toggle;
    in->find_x = h.find_x, in->find_step = h.find_step;
    in->field = h.field, in->field_toggle = h.field_toggle, in->inside_btn = h.inside_btn;
    in->job_x = h.job_x;
    in->side_mode = h.mode;
    in->side_twisty = h.twisty;
    // The zoom track: the size nearest the press, and the knob held until the button is let go.
    if (h.zoom) in->zoom_to = zoom_level_at(x), app.zoom_drag = true;
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
    if (modal_kind() != MODAL_NONE) return;
    app.ctx_x = x, app.ctx_y = y;
    if (h.tab >= 0) in->ctx_tab = h.tab;
    else if (h.row >= 0) in->ctx_row = h.row;
    else if (h.side >= 0) in->ctx_side = h.side;
    else if (h.bar == UB_CRUMB) in->ctx_crumb = h.crumb;
    else if (in_box(CLAY_ID("list"), x, y)) in->ctx_here = true;
}

// A right-click menu's size as layout_menu draws it (F8), before it is drawn: its rows and lines,
// the widest label and key on one row (the gaps between them, the marks' column), its padding.
static Vector2 menu_size(int m)
{
    float row = 0, hgt = 8;
    bool marks = false;
    for (int i = 0; i < UI_MENU[m].n; i++) {
        UiMenuItem it = UI_MENU[m].items[i];
        if (it == UM_SEP ? !ui_menu_sep_shown(&app.ui, m, i) : !ui_menu_shown(&app.ui, it)) continue;   // (as layout_menu leaves them out)
        if (it == UM_SEP) {
            hgt += MENU_SEP_H;
            continue;
        }
        hgt += MENU_ROW_H;
        marks |= UI_MENU_ITEM[it].mark != 0;
        float lw = str_width(it == UM_OPEN_LINE ? open_line_label() : str_from_c(UI_MENU_ITEM[it].label)) + str_width(str_from_c(UI_MENU_ITEM[it].key));
        if (lw > row) row = lw;
    }
    // The menu's padding, an entry's, the marks' column and its gap, the widest label and key on one
    // row, two gaps of 32 round the 1-pixel spring between them: as layout_menu lays them out.
    float wid = 8 + 20 + (marks ? 14 + 6 : 0) + row + 2 * 32 + 1;
    return (Vector2){ wid > MENU_W_MIN ? wid : MENU_W_MIN, hgt + 2 };
}

// A right-click menu just opened (F8): its top left. By the pointer, at the press, flipped left or
// up where it would leave the window; by a key, under the cursor's entry (above it near the
// bottom), or the files' top left with none drawn. Kept inside the window either way.
static void ctx_place(void)
{
    Vector2 sz = menu_size(app.ui.menu);
    float w = (float)GetScreenWidth(), h = (float)GetScreenHeight(), x = app.ctx_x, y = app.ctx_y;
    if (app.ui.ctx_key) {
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
    u->ctx_w = app.ctx_path[0] && access(app.ctx_path, W_OK) == 0;
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
    case UM_PASTE_INTO: paste(dir); break;
    case UM_BOOKMARK_IT: bookmark_add(dir); break;
    case UM_EJECT_IT:
    case UM_UNMOUNT_IT:
        if (ctx_pl && place_ejectable(ctx_pl)) (it == UM_EJECT_IT ? eject_start : unmount_start)(ctx_pl);
        break;
    default: break;
    }
}

static float wheel_left;   // a touchpad's part of a notch, carried to the next frame

// The wheel's notches at x: the files scroll, or with Ctrl held their icons zoom (F6.3); the
// sidebar scrolls by itself. True when the sidebar moved.
static bool wheel_at(UiInput *in, float x, int notches, bool ctrl)
{
    if (app.ui.panel == UI_PANEL_OW || app.ui.panel == UI_PANEL_KEYS) return in->ow_wheel = notches, false;   // the panel's list (F7.3; Keyboard Shortcuts', F7.5)
    if (x >= side_w() && ctrl) in->zoom_wheel = notches;
    else if (x >= side_w()) in->wheel = notches;
    else if (notches) return app.ui.side.top = ui_clamp(app.ui.side.top - notches, 0, ui_max_top(&app.ui.side)), true;
    return false;
}

static int tip_target(Hit h)
{
    switch (h.bar) {
    case UB_BACK: case UB_FORWARD: case UB_UP: case UB_LOCATION: case UB_SEARCH: case UB_LEFT: case UB_RIGHT:
    case UB_VIEW_LIST: case UB_VIEW_ICONS: case UB_ZOOM_OUT: case UB_ZOOM_IN: return TIP_BAR + h.bar;
    default: break;
    }
    if (h.zoom) return TIP_ZOOM;
    if (h.find_step) return TIP_FIND + (h.find_step > 0);
    if (h.mode >= 0) return TIP_MODE + h.mode;
    if (h.eject >= 0) return TIP_EJECT + h.eject;
    if (h.tab_x >= 0) return TIP_TAB_X + h.tab_x;
    return -1;
}

// The zoom slider's knob held, the pointer at x (F6.3): the size nearest it; the button let go, the
// knob is dropped (true then: it is drawn unlit).
static bool zoom_drag_at(UiInput *in, float x, bool held)
{
    if (!app.zoom_drag) return false;
    if (!held) return app.zoom_drag = false, true;
    in->zoom_to = zoom_level_at(x);
    return false;
}

static bool hover_at(float x, float y);

// When the timer wakes the loop: a tooltip due, the search field's text to search for (F9), or a
// state write waiting (a folder's view, remember.c; this window's tabs, restore.c; F11).
static i64 wake_due(void)
{
    i64 a = ui_tip_due(&app.tip), b = app.search_due, c = !restore.due ? rem.due : !rem.due ? restore.due : restore.due < rem.due ? restore.due : rem.due;
    i64 ab = !a ? b : !b ? a : a < b ? a : b;
    return !ab ? c : !c ? ab : ab < c ? ab : c;
}

// The hover highlights for the pointer where it is now, not where it last moved: none once it has
// left the window (GLFW keeps its last position), and again after a frame that moved things under
// a still pointer (a folder entered, the view or the size changed). Else a cell stayed lit that
// the pointer wasn't on (Michael, 2026-10-24). True when any changed.
static bool hover_refresh(bool on, float x, float y)
{
    return on ? hover_at(x, y) : hover_at(-1e6f, -1e6f);
}

// What is under the pointer at x, y, for the hover highlights: true when any changed.
static bool hover_at(float x, float y)
{
    Hit h = hit_at(x, y), o = app.hov;
    // (what is drawn lit; a menu's entries are ui_menu_hover's)
    bool changed = h.row != o.row || h.side != o.side || h.bar != o.bar || h.crumb != o.crumb || h.tab != o.tab || h.tab_x != o.tab_x ||
                   h.mode != o.mode || h.eject != o.eject || h.modal != o.modal || h.job_x != o.job_x || h.zoom != o.zoom ||
                   h.menu_title != o.menu_title || h.bmp_btn != o.bmp_btn || h.find_x != o.find_x || h.find_step != o.find_step ||
                   h.inside_btn != o.inside_btn;   // (the search bar's In files, lit by footer.c: F11 review)
    app.hov = h;
    changed |= ui_menu_hover(&app.ui, h.menu_title, h.menu_item);
    // Onto another icon button (or off one): its tooltip waits UI_TIP_MS, the timer wakes the loop.
    if (ui_tip_hover(&app.tip, tip_target(h), now_ms())) {
        changed = true;
        timer_at(wake_due());
    }
    return changed;
}

// The user's input since the last frame. *seen: anything that could change what is on screen.
static UiInput input_real(bool *seen)
{
    UiInput in = ui_input_none();
    memcpy(in.ev, evq.ev, sizeof *evq.ev * (size_t)evq.n);
    in.nev = evq.n;
    *seen = evq.n > 0 || IsWindowResized();
    evq.n = 0;
    while (GetKeyPressed() != 0) *seen = true;
    while (GetCharPressed() != 0) {}
    Vector2 m = GetMousePosition(), d = GetMouseDelta();
    wheel_left += GetMouseWheelMove();
    int notches = (int)wheel_left;
    wheel_left -= (float)notches;
    *seen |= wheel_at(&in, m.x, notches, IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
    if (!script_on) *seen |= hover_refresh(IsCursorOnScreen(), m.x, m.y);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int mods = (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ? UI_MOD_CTRL : 0) |
                   (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? UI_MOD_SHIFT : 0);
        press_at(&in, m.x, m.y, mods);
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) middle_at(&in, m.x, m.y);
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) right_at(&in, m.x, m.y);
    bool held = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    // Off the window the drag ends: Wayland's leave releases no button, and a compositor grab
    // (a keybind, a screenshot) can take the release (F6.3 review).
    bool on = IsCursorOnScreen();
    if (d.x != 0 || !held || !on) *seen |= zoom_drag_at(&in, m.x, held && on);
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
