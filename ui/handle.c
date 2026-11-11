// One frame's input (ui_handle): the pointer, then the keys, to the field, panel or list that has
// them; the search field's and the find bar's keys. Included by ui.c.

// ---- input ------------------------------------------------------------------------------------

// The next sort for a header click (or `s`'s next column): the same column again reverses.
static Sort ui_sort_click(Sort s, SortKey k)
{
    return s.key == k ? (Sort){ k, !s.reverse } : (Sort){ k, false };
}

// ---- the search view's field (F9) -----------------------------------------------------------

// The text bars' fields (UiField): when each is there, its toggles, the focus that is its.
static const UiFocus UI_FIELD_FOCUS[UI_FIELDS] = { UI_FOCUS_FIND, UI_FOCUS_SEARCH, UI_FOCUS_INSIDE };

static bool ui_field_shown(const Ui *u, UiField f)
{
    switch (f) {
    case UI_FIELD_FIND: return u->finding;
    case UI_FIELD_SEARCH: return u->view == UI_VIEW_SEARCH;
    case UI_FIELD_INSIDE: return u->view == UI_VIEW_SEARCH && u->search_inside;
    default: return false;
    }
}

static UiMatchOpts *ui_field_opts(Ui *u, UiField f)
{
    return f == UI_FIELD_INSIDE ? &u->inside_opts : &u->find_opts;
}

// "In files" on or off (F9 step 3): on, the "Containing" field takes the keys; off, they go back to
// the name's from it.
static void ui_search_inside(Ui *u, bool on)
{
    u->search_inside = on, u->search_edited = true;
    if (on) u->focus = UI_FOCUS_INSIDE;
    else if (u->focus == UI_FOCUS_INSIDE) u->focus = UI_FOCUS_SEARCH;
}

// The fields' keys (the name's, and "Containing" inside files): each edits as the rename line does
// (a caret, Shift to select, Ctrl+A, Ctrl+C, X, V: Michael, 2026-10-28), plus its own toggles (the
// name's are find's), each change marked; Tab goes to the other field while both show; Enter, Esc
// and Down give the keys to the results; another command goes to them as well, and runs there
// (false; Ctrl+Shift+F gives them back).
static bool ui_search_event(Ui *u, UiEvent e, const UiInput *in)
{
    bool inside = u->focus == UI_FOCUS_INSIDE;
    u->search_was_inside = inside;
    if (ui_toggle_key(e)) {
        UiMatchOpts *o = inside ? &u->inside_opts : &u->find_opts;
        *o = ui_toggle(*o, ui_toggle_key(e)), u->search_edited = true;
        return true;
    }
    if (e.key == UK_TAB && u->search_inside) return u->focus = inside ? UI_FOCUS_SEARCH : UI_FOCUS_INSIDE, u->search_was_inside = !inside, true;
    if (e.cmd == UK_CRUMBS) return u->focus = UI_FOCUS_LIST, ui_crumbs_focus(u, in), true;   // (Ctrl+K again: on to the path buttons, as Ctrl+J from the find bar goes on to the jobs)
    UiLine l = inside ? ui_inside_line(u) : ui_search_line(u);
    if (ui_line_copies_nothing(l, e)) return u->focus = UI_FOCUS_LIST, false;   // (the cursor's result)
    UiLineHit h = ui_line_key(u, l, e, in->paste);
    if (h == UI_LINE_CHANGED) u->search_edited = true;
    if (h) return true;
    switch (e.key) {
    case UK_ENTER: case UK_SHIFT_ENTER: case UK_ESC: case UK_DOWN: u->focus = UI_FOCUS_LIST; return true;
    default: u->focus = UI_FOCUS_LIST; return false;
    }
}

// The keys back to the search view's field they were in last: "Containing" while it shows, else
// the name's (Michael, 2026-10-29: Ctrl+K and Ctrl+Up as Ctrl+J and Ctrl+Down go to the find bar).
static void ui_search_back(Ui *u)
{
    u->focus = u->search_inside && u->search_was_inside ? UI_FOCUS_INSIDE : UI_FOCUS_SEARCH;
}

// Ctrl+Shift+F, Go → Search…, the magnifier (F9): in the search view the keys go back to its
// field; elsewhere gui/ opens a search below the folder shown in a new tab (the trash isn't one).
static UiAction ui_search_open(Ui *u)
{
    if (u->view != UI_VIEW_SEARCH) return ui_view_act(u, (UiAction){ UA_SEARCH, 0, false });
    ui_edit_close(u);
    ui_search_back(u);
    return UI_NONE;
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
    // (the file found: typing a name and copying it is what the bar is for)
    if (ui_line_copies_nothing(ui_find_line(u), e)) return ui_find_leave(u), false;
    UiLineHit h = ui_line_key(u, ui_find_line(u), e, in->paste);
    if (h == UI_LINE_CHANGED) ui_find_update(u, names);
    if (h) return true;
    if (e.cmd == UK_JOBS) {
        ui_find_leave(u);
        if (in->job_n > 0) u->focus = UI_FOCUS_JOBS, u->job_at = 0;
        return true;
    }
    if (e.cmd == UK_FIND) return true;
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
    default: ui_find_leave(u); return false;   // a command: the keys go back to the list, which does it
    }
}

// The panels' and the bars' pointer (ui_handle): Open With's rows, toggles and buttons, the
// branches', Edit Bookmarks', Keyboard Shortcuts', About's and the bar note's; the find bar's
// chevrons; a text bar's field or toggle; the search's In files. True when it is the frame's
// (*out: what gui/ does).
static bool ui_handle_panels(Ui *u, UiNames names, const UiInput *in, UiOnTop on, bool prompt, UiAction *out)
{
    UiPickDo d;
    // Open With (F7.3): a toggle flips.
    if (on == UI_ON_OW) {
        if (in->toggle == UI_TOGGLE_ALWAYS) {
            ui_ow_def_flip(u);
            return *out = UI_NONE, true;
        }
        if (in->toggle >= 1 && in->toggle <= UI_TOGGLES) {
            u->ow_opts = ui_toggle(u->ow_opts, in->toggle);
            ui_picker_filter(u, UI_PANEL_OW, in);
            return *out = UI_NONE, true;
        }
    }
    // A picker's press (ui_pick_press: a double click chooses).
    UiPanel pk = ui_picker_on(on);
    if (pk != UI_PANEL_NONE) {
        if ((d = ui_pick_press(ui_pick_of(u, pk), in))) return *out = ui_picker_do(u, pk, d, in), true;
    } else if (on == UI_ON_BM) {
        // Edit Bookmarks (F7.5): a row takes the cursor (a double click goes there), a button acts on
        // it as its key does, Close closes; the rename field takes no press (its own keys only).
        isize row = in->bmp_drow >= 0 ? in->bmp_drow : in->bmp_row;
        if (row >= 0 && u->bmp_list.top + row < u->bmp_list.n) {
            u->bmp_list.sel = u->bmp_list.top + row, u->bmp_naming = false;
            if (in->bmp_drow >= 0) return *out = (ui_panel_close(u, UI_PANEL_BM), (UiAction){ UA_BM_GO, u->bmp_list.sel, false }), true;
        }
        if (in->bmp_btn >= 1 && in->bmp_btn <= UI_BMP_BTNS && !u->bmp_naming) return *out = ui_bmp_do(u, in->bmp_btn), true;
        if (in->modal == UI_MODAL_NO) return ui_panel_close(u, UI_PANEL_BM), *out = UI_NONE, true;
    } else if (on == UI_ON_KEYS) {
        // Keyboard Shortcuts: Close closes; the wheel scrolls it.
        if (in->modal == UI_MODAL_NO) return ui_keys_close(u), *out = UI_NONE, true;
        if (in->panel_wheel) ui_scroll(&u->keys_list, in->panel_wheel);
    } else if (on == UI_ON_LOGIN) {
        // Connect's question (F16 step 4): a field takes the keys; Connect, Guest and Cancel answer.
        if (in->login_field >= 0 && in->login_field < UI_LOGIN_FIELDS && ui_login_shown(u, in->login_field)) u->login_at = in->login_field;   // (only a field shown: the F16 code-health review)
        if (in->modal == UI_MODAL_YES) return *out = ui_login_close(u, UA_LOGIN), true;
        if (in->modal == UI_MODAL_GUEST && ui_login_guest(u)) return *out = ui_login_close(u, UA_LOGIN_GUEST), true;
        if (in->modal == UI_MODAL_NO) return *out = ui_login_close(u, UA_LOGIN_CANCEL), true;
        if (in->login_field >= 0) return *out = UI_NONE, true;
    } else if (on == UI_ON_ABOUT) {
        if (in->modal == UI_MODAL_NO) return ui_panel_close(u, UI_PANEL_ABOUT), *out = UI_NONE, true;
    } else if (on == UI_ON_BAR) {
        if (in->modal == UI_MODAL_OK) return ui_panel_close(u, UI_PANEL_BAR), *out = UI_NONE, true;
    } else if (!prompt && u->finding && (in->find_x || in->find_step)) {
        // A chevron steps with two matches or more (greyed under that) and leaves the keys where
        // they are.
        isize at;
        if (in->find_step) {
            if (ui_find_count(u, names, &at) >= 2) ui_find_step(u, names, in->find_step > 0 ? 1 : -1);
        } else ui_find_close(u);
        return *out = UI_NONE, true;
    } else if (!prompt && !u->menu_on && in->field >= 0 && in->field < UI_FIELDS && ui_field_shown(u, (UiField)in->field)) {
        // A text bar's field takes the keys (the edit line, if open, closes with it); a toggle
        // flips its field's (the name's are find's): find matches again, a search runs again.
        if (in->field_toggle >= 1 && in->field_toggle <= UI_TOGGLES) {
            UiMatchOpts *o = ui_field_opts(u, (UiField)in->field);
            *o = ui_toggle(*o, in->field_toggle);
            if (u->finding) ui_find_update(u, names);   // (Containing's change nothing there)
            if (u->view == UI_VIEW_SEARCH) u->search_edited = true;
        } else u->focus = UI_FIELD_FOCUS[in->field];
        return *out = UI_NONE, true;
    } else if (!prompt && !u->menu_on && u->view == UI_VIEW_SEARCH && in->inside_btn) {
        ui_search_inside(u, !u->search_inside);
        return *out = UI_NONE, true;
    }
    return false;
}

// The rest of the pointer (ui_handle): the menus, the right-click menus, the Properties panel's,
// the job rows, the clash buttons, the wheel and the zoom, the sort headers, the sidebar, the tabs,
// the top bar's buttons, a click and a double click in the files, Back and Forward. True when it
// is the frame's (*out: what gui/ does).
static bool ui_handle_pointer(Ui *u, UiNames names, const UiInput *in, UiOnTop on, bool prompt, UiAction *out)
{
    UiList *l = &u->list;
    // First, whatever returns below (code-health review, 2026-11-06: a menu's or a panel's press
    // returned before them):
    // The job rows: the mark kept on a row there is; none left, no keyboard there.
    if (u->job_at >= in->job_n) u->job_at = in->job_n - 1;
    if (u->job_at < 0) u->job_at = 0;
    if (in->job_n <= 0) ui_unfocus(u, UI_FOCUS_JOBS);
    // A click anywhere gives the path buttons' keyboard back; so does their going.
    bool clicked = in->click >= 0 || in->dclick >= 0 || in->side_click >= 0 || in->header >= 0 || in->bar != UB_NONE ||
                   in->tab_click >= 0 || in->tab_close >= 0 || in->mid_row >= 0 || in->mid_side >= 0 || in->mid_crumb >= 0;
    if (clicked || in->crumb_n <= 0) ui_unfocus(u, UI_FOCUS_CRUMBS);
    if (clicked) ui_unfocus(u, UI_FOCUS_JOBS);
    if (u->focus == UI_FOCUS_CRUMBS && u->crumb_at > in->crumb_n - 1 + u->branch_on) u->crumb_at = in->crumb_n - 1 + u->branch_on;   // (crumb_n: the branch button, F12 step 5c)
    // The menu bar (F7.1): a title opens its menu or closes it; while a menu is open a press on one
    // of its entries picks it (one that can't be picked keeps the menu open), and a press anywhere
    // else only closes it.
    if (!prompt && in->menu_title >= 0 && in->menu_title < UI_MENUS) {
        if (u->menu_on && u->menu == in->menu_title) u->menu_on = false;
        else ui_menu_open(u, in->menu_title, false);
        return *out = UI_NONE, true;
    }
    if (u->menu_on && in->menu_item >= 0 && in->menu_item < UI_MENU[u->menu].n) {
        UiMenuItem it = UI_MENU[u->menu].items[in->menu_item];
        if (!ui_menu_enabled(u, it)) return *out = UI_NONE, true;
        UiAction a = ui_menu_pick(u, it);
        if (a.kind != UA_NONE || !u->menu_cmd) return *out = a, true;
        // (an entry that runs its command: the loop below runs it first)
    }
    if (u->menu_on && in->menu_away) return u->menu_on = false, *out = UI_NONE, true;
    // A right press (F8): on a row, its menu. An unmarked row becomes the only one (the marks clear,
    // as Nemo and GTK do); a marked one keeps them, the cursor on it. On the files' empty space, the
    // folder shown's menu, the marks cleared.
    if (!prompt && in->ctx_row >= 0 && u->list.top + in->ctx_row < u->list.n) {
        isize row = u->list.top + in->ctx_row;
        if (!ui_marked(u, row)) ui_mark_clear(u);
        u->list.sel = row;
        ui_ctx_open(u, UI_CTX_ENTRY, names, false);
        return *out = UI_NONE, true;
    }
    if (!prompt && in->ctx_here) {
        ui_mark_clear(u);
        ui_ctx_open(u, UI_CTX_HERE, names, false);
        return *out = UI_NONE, true;
    }
    // On a sidebar row (not a heading) or a path button: that folder's menu; the sidebar's cursor
    // goes to its row.
    if (!prompt && in->ctx_side >= 0 && u->side.top + in->ctx_side < u->side.n && !ui_side_head(u, u->side.top + in->ctx_side)) {
        u->side.sel = u->side.top + in->ctx_side;
        ui_folder_open(u, u->side.sel, -1, false);
        return *out = UI_NONE, true;
    }
    if (!prompt && in->ctx_crumb >= 0 && in->ctx_crumb < in->crumb_n) {
        ui_folder_open(u, -1, in->ctx_crumb, false);
        return *out = UI_NONE, true;
    }
    // On a tab: its menu, the tab shown (gui/ shows it; the menu acts on the current one).
    if (!prompt && in->ctx_tab >= 0 && in->ctx_tab < u->tab_n) {
        ui_menu_open(u, UI_CTX_TAB, false);   // (it closes an edit line, as the bar's menus do)
        return *out = (UiAction){ UA_TAB_SHOW, in->ctx_tab, false }, true;
    }
    // The Properties panel's pointer (F7.2): a checkbox takes the focus and flips; Close closes.
    if (on == UI_ON_PROPS && in->prop_box >= 0 && in->prop_box < 9) {
        u->prop_at = in->prop_box;
        return *out = u->prop_can ? (UiAction){ UA_CHMOD, in->prop_box, false } : UI_NONE, true;
    }
    if (on == UI_ON_PROPS && in->modal == UI_MODAL_OK) return ui_panel_close(u, UI_PANEL_PROPS), *out = UI_NONE, true;
    if (!prompt && in->job_x >= 0 && in->job_x < in->job_n) {
        u->cancel_asking = true, u->cancel_id = in->job_id[in->job_x];
        return *out = UI_NONE, true;
    }
    // A modal panel's buttons: as the keys they stand for.
    if (on == UI_ON_CLASH && in->modal == UI_MODAL_ALL) u->ask_all = !u->ask_all;
    if (on == UI_ON_CLASH && in->modal >= UI_MODAL_SKIP && in->modal <= UI_MODAL_CANCEL) {
        static const int ANSWER[] = { UI_ANS_SKIP, UI_ANS_REPLACE, UI_ANS_KEEP, UI_ANS_CANCEL };
        int a = ANSWER[in->modal - UI_MODAL_SKIP];
        u->asking = false;
        return *out = (UiAction){ UA_ANSWER, a, a != UI_ANS_CANCEL && u->ask_all }, true;
    }
    if (!prompt && in->wheel) ui_scroll(l, in->wheel);   // (not under a question or a panel: the group D review)
    if (!prompt && in->zoom_to >= 0) return *out = ui_zoom(u, in->zoom_to), true;
    if (!prompt && in->zoom_wheel) return *out = ui_zoom_by(u, in->zoom_wheel > 0 ? 1 : -1), true;
    if (!prompt && in->header >= 0 && in->header < SORT_KEYS) {
        u->sort = ui_sort_click(u->sort, (SortKey)in->header);
        return *out = (UiAction){ UA_SORT, 0, false }, true;
    }
    if (!prompt && in->side_mode >= 0 && in->side_mode < SIDE_MODES) return *out = ui_side_mode(u, (SideMode)in->side_mode), true;
    if (!prompt && in->side_eject >= 0 && u->side_mode == SIDE_PLACES && u->side.top + in->side_eject < u->side.n)
        return *out = (UiAction){ UA_EJECT, u->side.top + in->side_eject, false }, true;
    if (!prompt && in->side_twisty >= 0 && u->side_mode == SIDE_TREE && u->side.top + in->side_twisty < u->side.n)
        return *out = (ui_edit_close(u), (UiAction){ UA_TREE_TOGGLE, u->side.top + in->side_twisty, false }), true;
    if (!prompt && in->side_click >= 0 && ui_side_head(u, u->side.top + in->side_click)) return *out = UI_NONE, true;
    if (!prompt && in->side_click >= 0 && u->side.top + in->side_click < u->side.n) {
        u->side.sel = u->side.top + in->side_click;
        ui_edit_close(u);
        return *out = (UiAction){ UA_PLACE, u->side.sel, false }, true;
    }
    if (!prompt && in->tab_close >= 0) return *out = (ui_edit_close(u), (UiAction){ UA_TAB_CLOSE, in->tab_close, false }), true;
    if (!prompt && in->tab_click >= 0) return *out = (ui_edit_close(u), (UiAction){ UA_TAB_SHOW, in->tab_click, false }), true;
    if (!prompt && in->mid_row >= 0 && l->top + in->mid_row < l->n) return *out = (UiAction){ UA_TAB_BG_ROW, l->top + in->mid_row, false }, true;
    if (!prompt && in->mid_side >= 0 && u->side.top + in->mid_side < u->side.n && !ui_side_head(u, u->side.top + in->mid_side)) return *out = (UiAction){ UA_TAB_BG_PLACE, u->side.top + in->mid_side, false }, true;
    if (!prompt && in->mid_crumb >= 0) return *out = (UiAction){ UA_TAB_BG_CRUMB, in->mid_crumb, false }, true;
    if (!prompt && in->bar != UB_NONE) {
        switch (in->bar) {
        case UB_BACK: ui_edit_close(u); return *out = (UiAction){ UA_BACK, 0, false }, true;
        case UB_FORWARD: ui_edit_close(u); return *out = (UiAction){ UA_FORWARD, 0, false }, true;
        case UB_UP: ui_edit_close(u); return *out = (UiAction){ UA_PARENT, 0, false }, true;
        case UB_LOCATION:
            if (u->focus == UI_FOCUS_EDIT && u->edit_path) return ui_edit_close(u), *out = UI_NONE, true;
            return *out = (UiAction){ UA_LOCATION, 0, false }, true;
        case UB_SEARCH:   // as Ctrl+Shift+F (F9; the find bar's until then)
            ui_edit_close(u);
            return *out = ui_search_open(u), true;
        case UB_LEFT: return *out = (UiAction){ UA_CRUMB_SHIFT, -1, false }, true;
        case UB_RIGHT: return *out = (UiAction){ UA_CRUMB_SHIFT, 1, false }, true;
        case UB_CRUMB: ui_edit_close(u); return *out = (UiAction){ UA_CRUMB, in->crumb, false }, true;
        case UB_RESTORE: if (u->view == UI_VIEW_TRASH && l->n > 0) return *out = (UiAction){ UA_RESTORE, 0, false }, true; break;
        case UB_EMPTY: if (u->view == UI_VIEW_TRASH) u->empty_asking = true; break;
        case UB_VIEW_LIST: return *out = ui_view(u, false), true;
        case UB_VIEW_ICONS: return *out = ui_view(u, true), true;
        case UB_ZOOM_OUT: return *out = ui_zoom_by(u, -1), true;
        case UB_ZOOM_IN: return *out = ui_zoom_by(u, 1), true;
        case UB_BRANCH:   // its menu, under it (F12 step 5b); a second press closes it (gui/'s menu_away), as a title's does
            ui_branch_open(u, false);
            return *out = UI_NONE, true;
        default: break;
        }
    }
    if (!prompt && in->click >= 0 && l->top + in->click < l->n) {
        isize row = l->top + in->click;
        ui_edit_close(u);
        // Shift+click: in add mode a range from the cursor joins the marks; in range mode the
        // anchor's range replaces them, or with Ctrl joins them. A click without Shift moves the
        // anchor.
        bool shift = in->click_mods & UI_MOD_SHIFT, ctrl = in->click_mods & UI_MOD_CTRL;
        if (shift && u->shift_add) ui_mark_range(u, l->sel, row);
        else if (shift) ui_range(u, l->sel, row, ctrl);
        else {
            if (ctrl) ui_mark(u, row, !ui_marked(u, row));
            u->anchor = -1;   // (the next Shift starts at row)
        }
        l->sel = row, ui_unfocus(u, UI_FOCUS_SIDE), ui_unfocus(u, UI_FOCUS_FIND);   // (the find bar stays open; review, 2026-10-28)
        ui_show_sel(l);
    }
    if (!prompt && in->dclick >= 0 && l->top + in->dclick < l->n) {
        l->sel = l->top + in->dclick, ui_unfocus(u, UI_FOCUS_SIDE), ui_unfocus(u, UI_FOCUS_FIND);
        ui_edit_close(u);
        return *out = (UiAction){ UA_OPEN, l->sel, false }, true;
    }
    if (!prompt && in->backs > 0) return *out = (UiAction){ UA_BACK, 0, false }, true;
    if (!prompt && in->forwards > 0) return *out = (UiAction){ UA_FORWARD, 0, false }, true;
    return false;
}

// The keys in the files that only ask gui/ for an action (code-health review, 2026-11-06: a new
// one is a line here): its kind and argument, through the view's rules (ui_view_act) when gated,
// and only with entries to act on when files.
static const struct { UiKey key; UiActionKind kind; int arg; bool gated, files; } UI_KEY_ACTS[] = {
    { UK_PARENT, UA_PARENT, 0, false, false },
    { UK_BACK, UA_BACK, 0, false, false }, { UK_FORWARD, UA_FORWARD, 0, false, false },
    { UK_HIDDEN, UA_HIDDEN, 0, false, false }, { UK_RELOAD, UA_RELOAD, 0, false, false },
    { UK_HOMEDIR, UA_HOMEDIR, 0, false, false }, { UK_TRASH_VIEW, UA_TRASH_VIEW, 0, false, false },
    { UK_UNDO, UA_UNDO, 0, false, false },
    { UK_LOCATION, UA_LOCATION, 0, false, false },
    { UK_TAB_NEW, UA_TAB_NEW, 0, false, false }, { UK_TAB_REOPEN, UA_TAB_REOPEN, 0, false, false },
    { UK_TAB_CLOSE, UA_TAB_CLOSE, -1, false, false },
    { UK_TAB_NEXT, UA_TAB_STEP, 1, false, false }, { UK_TAB_PREV, UA_TAB_STEP, -1, false, false },
    { UK_TAB_LEFT, UA_TAB_MOVE, -1, false, false }, { UK_TAB_RIGHT, UA_TAB_MOVE, 1, false, false },
    { UK_ADD_BOOKMARK, UA_ADD_BOOKMARK, 0, true, false }, { UK_PASTE, UA_PASTE, 0, true, false },
    { UK_NEWFOLDER, UA_NEWFOLDER, 0, true, false }, { UK_NEW_WINDOW, UA_NEW_WINDOW, 0, true, false },
    { UK_TERMINAL, UA_TERMINAL, 0, true, false },
    { UK_COPY, UA_COPY, 0, true, true }, { UK_MAKE_LINK, UA_MAKE_LINK, 0, true, true }, { UK_CUT, UA_CUT, 0, true, true },
};

// A command in the files or the sidebar (ui_handle), Shift taken off the moves (shifted, and
// shift_enter for Shift+Enter); ch the key's character (Alt+1 .. Alt+9's digit). True when it asks
// gui/ for something (*out); *extend is where a Shift move starts.
static bool ui_list_key(Ui *u, UiNames names, const UiInput *in, UiKey key, u32 ch, bool shifted, bool shift_enter, isize *extend, UiAction *out)
{
    UiList *l = &u->list;
    bool files = l->n > 0 && u->focus != UI_FOCUS_SIDE;
    if (shifted && u->focus != UI_FOCUS_SIDE) *extend = l->sel;
    // The keys and letters that move a cursor: the sidebar's when it has the keyboard. In the
    // icon view, Up and Down move a row and Left and Right a cell (F6.2).
    UiList *m = u->focus == UI_FOCUS_SIDE ? &u->side : l;
    isize page = m->rows > 1 ? m->rows - 1 : 1;
    bool grid = u->grid && m == l;
    if (key >= UK_ITEM0) return ui_item_cmd(u, (UiMenuItem)(key - UK_ITEM0), out);
    switch (key) {
    case UK_RESTORE:
        if (u->view == UI_VIEW_TRASH && files) return *out = (UiAction){ UA_RESTORE, 0, false }, true;
        return false;
    case UK_SORT_NEXT: u->sort = (Sort){ (SortKey)((u->sort.key + 1) % SORT_KEYS), false }; return *out = (UiAction){ UA_SORT, 0, false }, true;
    case UK_SORT_REVERSE: u->sort.reverse = !u->sort.reverse; return *out = (UiAction){ UA_SORT, 0, false }, true;
    case UK_FIND_NEXT: ui_find_again(u, names, 1); return false;
    case UK_FIND_PREV: ui_find_again(u, names, -1); return false;
    case UK_MARK:
        // Space marks the entry under the cursor, or unmarks it, and moves on.
        if (files) ui_mark(u, l->sel, !ui_marked(u, l->sel)), ui_move(l, 1);
        return false;
    default: break;
    }
    if (grid && (key == UK_LEFT || key == UK_RIGHT)) {
        if (shifted) ui_move(l, key == UK_LEFT ? -1 : 1);   // a Shift move doesn't wrap
        else ui_step_round(l, key == UK_LEFT ? -1 : 1, false);
        return false;
    }
    if (grid && (key == UK_UP || key == UK_DOWN)) {
        if (shifted) ui_move_rows(l, key == UK_UP ? -1 : 1);
        else ui_step_round(l, key == UK_UP ? -1 : 1, true);
        return false;
    }
    if (grid && (key == UK_PAGEUP || key == UK_PAGEDOWN)) {
        ui_move_rows(l, key == UK_PAGEUP ? -page : page);
        return false;
    }
    for (int i = 0; i < COUNT(UI_KEY_ACTS); i++)
        if (UI_KEY_ACTS[i].key == key) {
            if (UI_KEY_ACTS[i].files && !files) return false;
            UiAction a = { UI_KEY_ACTS[i].kind, UI_KEY_ACTS[i].arg, false };
            return *out = UI_KEY_ACTS[i].gated ? ui_view_act(u, a) : a, true;
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
    case UK_VIEW_ICONS: return *out = ui_view(u, true), true;
    case UK_VIEW_LIST: return *out = ui_view(u, false), true;
    case UK_ZOOM_IN: return *out = ui_zoom_by(u, 1), true;
    case UK_ZOOM_OUT: return *out = ui_zoom_by(u, -1), true;
    case UK_ZOOM_RESET: return *out = ui_zoom(u, ui_zoom_level(UI_ZOOM_DEFAULT)), true;
    // Enter opens and Backspace goes up; Left and Right don't, in the list or the icon view
    // (Michael, 2026-10-24): they only open and close the tree's folders.
    case UK_ENTER:
        if (u->focus == UI_FOCUS_SIDE && u->side.n > 0) return *out = (UiAction){ UA_PLACE, u->side.sel, false }, true;
        if (files && shift_enter) return *out = ui_allows(u, UA_OPEN_WITH) ? ui_ow_open(u) : ui_view_act(u, (UiAction){ UA_OPEN_WITH, 0, false }), true;
        if (files) return *out = (UiAction){ UA_OPEN, l->sel, u->marked > 0 }, true;   // all: the marked ones (F7.3)
        break;
    case UK_RIGHT:
        if (u->focus == UI_FOCUS_SIDE && u->side.n > 0 && u->side_mode == SIDE_TREE) return *out = (UiAction){ UA_TREE_RIGHT, u->side.sel, false }, true;
        break;
    case UK_LEFT:
        if (u->focus == UI_FOCUS_SIDE && u->side.n > 0 && u->side_mode == SIDE_TREE) return *out = (UiAction){ UA_TREE_LEFT, u->side.sel, false }, true;
        break;
    case UK_F9: return *out = ui_side_mode(u, (SideMode)((u->side_mode + 1) % SIDE_MODES)), true;
    case UK_TAB:
    case UK_SIDE_GO:   // (Ctrl+Left: a word in a text field, the sidebar here)
        // Tab goes to the sidebar and back, Ctrl+Left there; a hidden one shows as places
        // (gui/ lists them for the mode, the cursor off a heading).
        if (key == UK_TAB && u->focus == UI_FOCUS_SIDE) {
            u->focus = UI_FOCUS_LIST;
            break;
        }
        if (u->side_mode == SIDE_HIDDEN) {
            UiAction a = ui_side_mode(u, SIDE_PLACES);
            u->focus = UI_FOCUS_SIDE;
            return *out = a, true;
        }
        // The cursor on the folder shown's row, else the first row (Michael, 2026-10-29).
        u->focus = u->side.n > 0 ? UI_FOCUS_SIDE : UI_FOCUS_LIST;
        if (u->focus == UI_FOCUS_SIDE) u->side.sel = u->side_here > 0 && u->side_here < u->side.n ? u->side_here : 0, ui_side_settle(u, 1);
        break;
    case UK_ESC:
        // The selection, then the sidebar's keyboard (a job is cancelled from its row, F5.3).
        if (u->marked) ui_mark_clear(u);
        else ui_unfocus(u, UI_FOCUS_SIDE);
        break;
    case UK_FIND: ui_find_open(u); break;
    case UK_SELECT_MATCH:
        if (l->n > 0) ui_find_open(u), u->find_select = true;
        break;
    case UK_PERMS_COLS:   // (greyed in the icon view, as its entry)
        if (!u->grid) return *out = (u->perms_cols = !u->perms_cols, (UiAction){ UA_PERMS_COLS, 0, false }), true;
        break;
    case UK_GROUP_COL:
        if (!u->grid && u->perms_cols) return *out = (u->group_col = !u->group_col, (UiAction){ UA_GROUP_COL, 0, false }), true;
        break;
    case UK_ALL:
        for (isize i = 0; i < l->n; i++) ui_mark(u, i, true);
        break;
    case UK_INVERT:
        for (isize i = 0; i < l->n; i++) ui_mark(u, i, !ui_marked(u, i));
        break;
    case UK_EDIT_BOOKMARKS: return *out = ui_bmp_open(u, 0), true;
    case UK_SHORTCUTS: return *out = ui_keys_open(u), true;
    case UK_F2:
        if (files && !ui_allows(u, UA_RENAME)) return *out = ui_view_act(u, (UiAction){ UA_RENAME, 0, false }), true;
        if (files) ui_edit_rename(u, names.at(names.ctx, l->sel), names.dir && names.dir(names.ctx, l->sel));
        break;
    case UK_DELETE:
        // In the trash view Delete is for good, asked first.
        if (files && u->view == UI_VIEW_TRASH) u->confirming = true, u->confirm_n = u->marked ? u->marked : 1;
        else if (files) return *out = ui_view_act(u, (UiAction){ UA_TRASH, 0, false }), true;
        break;
    case UK_SHIFT_DELETE:
        if (files && !ui_allows(u, UA_DELETE)) return *out = ui_view_act(u, (UiAction){ UA_DELETE, 0, false }), true;
        if (files) u->confirming = true, u->confirm_n = u->marked ? u->marked : 1;
        break;
    case UK_COPY_PATH:   // (an empty folder: its own path; the sidebar's keys: its cursor's folder, as its menu)
        if (u->focus == UI_FOCUS_SIDE) return *out = (UiAction){ UA_COPY_PATH, u->side.sel, true }, u->side.n > 0;
        return *out = ui_view_act(u, (UiAction){ UA_COPY_PATH, 0, false }), true;
    case UK_CRUMBS:   // in a search, its field first (then on to the path buttons from there)
        if (u->view == UI_VIEW_SEARCH) ui_search_back(u);
        else ui_crumbs_focus(u, in);
        break;
    case UK_JOBS:   // the find bar first when it is open, then the job rows (Michael, 2026-10-26)
        if (u->finding) u->focus = UI_FOCUS_FIND;
        else if (in->job_n > 0) u->focus = UI_FOCUS_JOBS, u->job_at = 0;
        break;
    case UK_PROPERTIES: return *out = ui_props_open(u, UI_PROPS_MARKED), true;
    case UK_SEARCH: return *out = ui_search_open(u), true;
    case UK_CONTAINING:   // (the search view's: elsewhere every entry is in the folder shown)
        if (u->view == UI_VIEW_SEARCH && files) return *out = (UiAction){ UA_CONTAINING, l->sel, false }, true;
        break;
    case UK_OPEN_LINE:   // (F9 step 4: gui/ says so when the result has no line)
        if (u->view == UI_VIEW_SEARCH && files) return *out = (UiAction){ UA_OPEN_LINE, l->sel, false }, true;
        break;
    case UK_TAB_JUMP: return *out = (UiAction){ UA_TAB_JUMP, (isize)ch, false }, true;
    default: break;
    }
    return false;
}
// A key while a panel is up (on): it takes every key; *out what gui/ does. False with none up.
static bool ui_panel_key(Ui *u, UiOnTop on, UiEvent e, const UiInput *in, UiAction *out)
{
    *out = UI_NONE;
    switch (on) {
    case UI_ON_PROPS: *out = ui_props_event(u, e); return true;
    case UI_ON_OW: *out = ui_ow_event(u, e, in); return true;
    case UI_ON_BRANCHES: *out = ui_picker_do(u, ui_picker_on(on), ui_pick_key(u, ui_pick_of(u, ui_picker_on(on)), e, in), in); return true;
    case UI_ON_BM: *out = ui_bmp_event(u, e, in->paste); return true;
    case UI_ON_KEYS: ui_keys_event(u, e, in->paste); return true;
    case UI_ON_LOGIN: *out = ui_login_event(u, e, in->paste); return true;
    case UI_ON_ABOUT: case UI_ON_BAR:   // Esc and Enter close them
        if (e.key == UK_ESC || e.key == UK_ENTER) u->panel = UI_PANEL_NONE;
        return true;
    default: return false;
    }
}

// Ctrl+Q, File → Close All Windows (F7.1): every window closes, this one at once, or asked first
// while jobs run or wait (the others asked anyway).
static UiAction ui_quit_all(Ui *u, const UiInput *in)
{
    u->menu_on = false;
    if (in->jobs_all > 0) u->quit_asking = true, u->cancel_asking = false;
    return (UiAction){ UA_CLOSE_ALL, 0, in->jobs_all <= 0 };
}

// ui_handle's frame, between the pickers' first filtering.
static UiAction ui_handle_frame(Ui *u, UiNames names, const UiInput *in)
{
    UiList *l = &u->list;
    // The window closed, or another window's Close All (SIGUSR1, F7.1): taken before anything that
    // ends the frame early (a click, the queue-full note), as gui/ queues it once (review,
    // F7.1 review). At once, or asked first when jobs run or wait.
    for (int k = 0; k < in->nev && !u->quit_asking; k++)
        if (in->ev[k].key == UK_QUIT) {   // (asking closes a menu: below)
            if (in->jobs_all <= 0) return (UiAction){ UA_QUIT, 0, false };
            u->quit_asking = true, u->cancel_asking = false;
        }
    // The panels F5.3 adds: the queue full (any key or OK), quit, cancel a job; Empty Trash, the
    // Bring back question (F11), and Clear Previews (F12).
    bool yes_no = in->modal == UI_MODAL_YES || in->modal == UI_MODAL_NO, said_yes = in->modal == UI_MODAL_YES;
    switch (ui_on_top(u)) {
    case UI_ON_FULL:
        if (in->modal == UI_MODAL_OK || in->nev > 0) return u->full_told = false, UI_NONE;
        break;
    default:
        if (yes_no && ui_is_question(ui_on_top(u))) return ui_answer(u, ui_on_top(u), said_yes);
        break;
    }
    UiOnTop on = ui_on_top(u);
    bool prompt = on != UI_ON_NONE;
    if (prompt) u->menu_on = false;
    UiAction out;
    if (ui_handle_panels(u, names, in, on, prompt, &out) || ui_handle_pointer(u, names, in, on, prompt, &out)) return out;
    isize extend = -1, was = l->sel;   // a Shift move's start, and the cursor before the event (ui_extend)
    // An entry the pointer picked runs its command first (F7.4), then the frame's keys.
    for (int k = u->menu_cmd ? -1 : 0; k < in->nev; k++, extend = ui_extend(u, extend, was)) {
        UiEvent e = k < 0 ? (UiEvent){ UK_CMD, 0, u->menu_cmd } : in->ev[k];
        was = l->sel;
        if (k < 0) u->menu_cmd = UK_NONE;
        // A character typed into a text field, a question, a panel or an open menu is theirs (its
        // text, an answer, a menu's letter), whatever command it is bound to (the F15 code-health
        // review: `key = q close-all` and a q in the rename line, or y at "Delete for good?", closed
        // every window).
        if (e.key == UK_NONE && e.ch && (ui_line_active(u, NULL) || ui_on_top(u) != UI_ON_NONE || u->menu_on)) e.cmd = UK_NONE;
        if (e.cmd == UK_QUIT_ALL && !u->quit_asking) return ui_quit_all(u, in);
        on = ui_on_top(u), prompt = on != UI_ON_NONE;   // (an earlier key may have opened or closed one)
        if (ui_is_question(on)) {   // Enter and y say yes, Esc and n no; the rest is ignored
            bool answered, yes = ui_yes_no(e, &answered);
            if (!answered) continue;
            UiAction a = ui_answer(u, on, yes);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        // The menu bar's keys (F7.1): F10 the first menu, Alt and a letter its menu; under a menu
        // open, its keys.
        if (!prompt && !u->menu_on && (e.cmd == UK_F10 || (e.key == UK_MENU && ui_menu_of(e.ch) >= 0))) {
            ui_menu_open(u, e.cmd == UK_F10 ? 0 : ui_menu_of(e.ch), true);
            continue;
        }
        // Shift+F10 and the Menu key (F8): the cursor's menu, or the folder shown's in an empty one,
        // while the files have the keys (an edit or find line keeps them); the sidebar's cursor's,
        // or the path button's, while they have them.
        if (!prompt && !u->menu_on && e.cmd == UK_CONTEXT && u->focus != UI_FOCUS_EDIT && !(u->focus == UI_FOCUS_FIND && u->finding) && u->focus != UI_FOCUS_JOBS) {
            if (u->focus == UI_FOCUS_CRUMBS && u->crumb_at >= in->crumb_n) ui_branch_open(u, true);   // (the branch button's)
            else if (u->focus == UI_FOCUS_CRUMBS) ui_folder_open(u, -1, u->crumb_at, true);
            else if (u->focus == UI_FOCUS_SIDE && u->side.n > 0 && !ui_side_head(u, u->side.sel)) ui_folder_open(u, u->side.sel, -1, true);
            else if (u->focus != UI_FOCUS_SIDE) ui_ctx_open(u, u->list.n > 0 ? UI_CTX_ENTRY : UI_CTX_HERE, names, true);
            continue;
        }
        if (u->menu_on && !prompt) {
            UiAction a;
            if (ui_menu_event(u, e, &a)) {
                if (a.kind != UA_NONE) return a;
                if (!u->menu_cmd) continue;
                e = (UiEvent){ UK_CMD, 0, u->menu_cmd }, u->menu_cmd = UK_NONE;   // an entry picked runs its command, here
                if (e.cmd == UK_QUIT_ALL && !u->quit_asking) return ui_quit_all(u, in);   // (read above, before the menus)
            }
        }
        if (ui_panel_key(u, on, e, in, &out)) {   // a panel up takes every key
            if (out.kind != UA_NONE) return out;
            continue;
        }
        // A clash (the only thing left that can be on top: the questions and panels took theirs
        // above) takes its keys before any field's: K keeps all, the find bar's text doesn't type
        // into it (code-health review, 2026-11-06: six fields asked !prompt for this). Its keys are
        // letters and Esc, which Shift doesn't change.
        if (on == UI_ON_CLASH) {
            UiAction a = ui_ask_event(u, e);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        if (u->focus == UI_FOCUS_EDIT && u->edit_path && e.cmd == UK_CRUMBS) {   // from the location line: it closes
            ui_crumbs_focus(u, in);   // (no path buttons: the line stays)
            continue;
        }
        if (u->focus == UI_FOCUS_EDIT) {
            UiAction a = ui_edit_event(u, e, in->paste);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        // The search field (F9) before Shift is taken off the moves: it selects with them.
        // Alt+I (F9 step 3): inside files or not, wherever the keys are in the search view.
        if (u->view == UI_VIEW_SEARCH && e.key == UK_MENU && e.ch == 'i') {
            ui_search_inside(u, !u->search_inside);
            continue;
        }
        if ((u->focus == UI_FOCUS_SEARCH || (u->focus == UI_FOCUS_INSIDE && u->search_inside)) && u->view == UI_VIEW_SEARCH && ui_search_event(u, e, in))
            continue;
        // The find bar's likewise (2026-10-28), after the questions and a clash.
        if (u->focus == UI_FOCUS_FIND && u->finding && ui_find_event(u, names, e, in)) continue;
        if (u->focus == UI_FOCUS_CRUMBS) {
            UiAction a;
            if (ui_crumbs_event(u, in, e, &a)) {
                if (a.kind != UA_NONE) return a;
                continue;
            }
        }
        if (u->focus == UI_FOCUS_JOBS && ui_jobs_event(u, in, e)) continue;
        // The files and the sidebar read the command (F15). Outside an edit line and the path
        // buttons, Shift doesn't change Enter; with a move in the files (arrows, Home, End, or H, J,
        // K, L: here, after the questions, a clash and the find bar took their letters) it selects
        // what the cursor passes over (below).
        bool shifted = ui_unshift(e.cmd) != e.cmd;
        bool shift_enter = e.cmd == UK_SHIFT_ENTER;   // Open With in the files
        UiKey cmd = shift_enter ? UK_ENTER : ui_unshift(e.cmd);
        if (ui_list_key(u, names, in, cmd, e.ch, shifted, shift_enter, &extend, &out)) return out;
    }
    return UI_NONE;
}

// A picker panel up and not yet filtered: filtered once gui/ gives its names (Open With's apps come
// a frame or more after it opens).
static void ui_picks_fresh(Ui *u, const UiInput *in)
{
    UiPicker k = ui_picker(u, u->panel, in);
    if (k.pick && k.pick->fresh && k.names) ui_picker_filter(u, u->panel, in);
}

// One frame's input. Clicks first, then the events in order up to the first that asks gui/ for
// something (after it, the list may be another folder's). The wheel scrolls the list without
// moving its cursor, one row a notch (Michael's choice for prdash-tui). What is on top (ui_on_top: a
// question, a note, a panel, a menu) takes every key until it is answered or closed; the edit line
// takes the keys that edit. A picker is filtered before the frame and after it: the branch menu's
// All branches opens its panel within the frame, and no other event may come to show it
// (2026-11-03: it stayed blank until the pointer moved).
static UiAction ui_handle(Ui *u, UiNames names, const UiInput *in)
{
    ui_picks_fresh(u, in);
    UiAction a = ui_handle_frame(u, names, in);
    ui_picks_fresh(u, in);
    return a;
}
