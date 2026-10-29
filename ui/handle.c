// One frame's input (ui_handle): the pointer, then the keys, to the field, panel or list that has
// them; the search field's and the find bar's keys. Included by ui.c.

// ---- input ------------------------------------------------------------------------------------

// The next sort for a header click (or `s`'s next column): the same column again reverses.
static Sort ui_sort_click(Sort s, SortKey k)
{
    return s.key == k ? (Sort){ k, !s.reverse } : (Sort){ k, false };
}

// ---- the search view's field (F9) -----------------------------------------------------------

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
static bool ui_search_event(Ui *u, UiEvent e, Str paste)
{
    bool inside = u->focus == UI_FOCUS_INSIDE;
    if (ui_toggle_key(e)) {
        UiMatchOpts *o = inside ? &u->inside_opts : &u->find_opts;
        *o = ui_toggle(*o, ui_toggle_key(e)), u->search_edited = true;
        return true;
    }
    if (e.key == UK_TAB && u->search_inside) return u->focus = inside ? UI_FOCUS_SEARCH : UI_FOCUS_INSIDE, true;
    UiLine l = inside ? ui_inside_line(u) : ui_search_line(u);
    if (ui_line_copies_nothing(l, e)) return u->focus = UI_FOCUS_LIST, false;   // (the cursor's result)
    UiLineHit h = ui_line_key(u, l, e, paste);
    if (h == UI_LINE_CHANGED) u->search_edited = true;
    if (h) return true;
    switch (e.key) {
    case UK_ENTER: case UK_SHIFT_ENTER: case UK_ESC: case UK_DOWN: u->focus = UI_FOCUS_LIST; return true;
    default: u->focus = UI_FOCUS_LIST; return false;
    }
}

// Ctrl+Shift+F, Go → Search…, the magnifier (F9): in the search view the keys go back to its
// field; elsewhere gui/ opens a search below the folder shown in a new tab (the trash isn't one).
static UiAction ui_search_open(Ui *u)
{
    if (u->view != UI_VIEW_SEARCH) return ui_view_act(u, (UiAction){ UA_SEARCH, 0, false });
    ui_edit_close(u);
    u->focus = UI_FOCUS_SEARCH;
    return (UiAction){ UA_NONE, 0, false };
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
    case UK_JOBS:
        ui_find_leave(u);
        if (in->job_n > 0) u->focus = UI_FOCUS_JOBS, u->job_at = 0;
        return true;
    case UK_FIND: return true;
    default: ui_find_leave(u); return false;   // a command: the keys go back to the list, which does it
    }
}

// One frame's input. Clicks first, then the events in order up to the first that asks gui/ for
// something (after it, the list may be another folder's). The wheel scrolls the list without
// moving its cursor, one row a notch (Michael's choice for prdash-tui). A prompt (a clash, the
// delete confirm) takes every key until it is answered; the edit line takes the keys that edit.
static UiAction ui_handle(Ui *u, UiNames names, const UiInput *in)
{
    UiList *l = &u->list;
    // The window closed, or another window's Close All (SIGUSR1, F7.1): taken before anything that
    // ends the frame early (a click, the queue-full note), as gui/ queues it once (reviewer-2,
    // F7.1 review). At once, or asked first when jobs run or wait.
    for (int k = 0; k < in->nev && !u->quit_asking; k++)
        if (in->ev[k].key == UK_QUIT) {   // (asking closes a menu: below)
            if (in->jobs_all <= 0) return (UiAction){ UA_QUIT, 0, false };
            u->quit_asking = true, u->cancel_asking = false;
        }
    // The panels F5.3 adds: the queue full (any key or OK), quit, cancel a job; and Empty Trash.
    bool yes_no = in->modal == UI_MODAL_YES || in->modal == UI_MODAL_NO, said_yes = in->modal == UI_MODAL_YES;
    switch (ui_on_top(u)) {
    case UI_ON_FULL:
        if (in->modal == UI_MODAL_OK || in->nev > 0) return u->full_told = false, (UiAction){ UA_NONE, 0, false };
        break;
    case UI_ON_QUIT:
        if (yes_no) return u->quit_asking = false, (UiAction){ said_yes ? UA_QUIT : UA_NONE, 0, false };
        break;
    case UI_ON_CANCEL:
        if (yes_no) return u->cancel_asking = false, (UiAction){ said_yes ? UA_CANCEL_JOB : UA_NONE, u->cancel_id, false };
        break;
    case UI_ON_EMPTY:
        if (yes_no) return u->empty_asking = false, (UiAction){ said_yes && u->view == UI_VIEW_TRASH ? UA_EMPTY_TRASH : UA_NONE, 0, false };
        break;
    default: break;
    }
    if (u->view != UI_VIEW_TRASH) u->empty_asking = false;
    UiOnTop on = ui_on_top(u);
    bool prompt = on != UI_ON_NONE;
    if (prompt) u->menu_on = false;
    // Open With (F7.3): filtered once gui/ has listed the apps; the pointer: a row takes the
    // cursor (a double click opens it), the field the keys, a toggle flips, Open and Cancel.
    if (u->ow && u->ow_fresh && in->app_name) ui_ow_filter(u, in);
    if (on == UI_ON_OW) {
        if (in->toggle == 4) {
            if (u->ow_def_can) u->ow_def = !u->ow_def;
            return (UiAction){ UA_NONE, 0, false };
        }
        if (in->toggle >= 1 && in->toggle <= 3) {
            u->ow_opts = ui_toggle(u->ow_opts, in->toggle);
            ui_ow_filter(u, in);
            return (UiAction){ UA_NONE, 0, false };
        }
        if (in->ow_field) u->ow_in_list = false;
        isize row = in->ow_drow >= 0 ? in->ow_drow : in->ow_row;
        if (row >= 0 && u->ow_list.top + row < u->ow_list.n) {
            u->ow_list.sel = u->ow_list.top + row, u->ow_in_list = true;
            if (in->ow_drow >= 0) return ui_ow_launch(u);
        }
        if (in->modal == UI_MODAL_YES) return ui_ow_launch(u);
        if (in->modal == UI_MODAL_NO) return ui_ow_close(u), (UiAction){ UA_NONE, 0, false };
        if (in->ow_wheel) ui_scroll(&u->ow_list, in->ow_wheel);
    } else if (on == UI_ON_BM) {
        // Edit Bookmarks (F7.5): a row takes the cursor (a double click goes there), a button acts on
        // it as its key does, Close closes; the rename field takes no press (its own keys only).
        isize row = in->bmp_drow >= 0 ? in->bmp_drow : in->bmp_row;
        if (row >= 0 && u->bmp_list.top + row < u->bmp_list.n) {
            u->bmp_list.sel = u->bmp_list.top + row, u->bmp_naming = false;
            if (in->bmp_drow >= 0) return u->bmp = false, (UiAction){ UA_BM_GO, u->bmp_list.sel, false };
        }
        if (in->bmp_btn >= 1 && in->bmp_btn <= 4 && !u->bmp_naming) return ui_bmp_do(u, in->bmp_btn);
        if (in->modal == UI_MODAL_NO) return u->bmp = false, (UiAction){ UA_NONE, 0, false };
    } else if (on == UI_ON_KEYS) {
        // Keyboard Shortcuts: Close closes; the wheel scrolls it.
        if (in->modal == UI_MODAL_NO) return ui_keys_close(u), (UiAction){ UA_NONE, 0, false };
        if (in->ow_wheel) ui_scroll(&u->keys_list, in->ow_wheel);
    } else if (on == UI_ON_ABOUT) {
        if (in->modal == UI_MODAL_NO) return u->about = false, (UiAction){ UA_NONE, 0, false };
    } else if (on == UI_ON_BAR) {
        if (in->modal == UI_MODAL_OK) return u->bar_told = false, (UiAction){ UA_NONE, 0, false };
    } else if (!prompt && u->finding && (in->find_x || in->find_field || in->find_step)) {
        // A chevron steps with two matches or more (greyed under that) and leaves the keys where
        // they are.
        isize at;
        if (in->find_step) {
            if (ui_find_count(u, names, &at) >= 2) ui_find_step(u, names, in->find_step > 0 ? 1 : -1);
        } else if (in->find_x) ui_find_close(u);
        else u->focus = UI_FOCUS_FIND;   // (the edit line, if open, closes with it)
        return (UiAction){ UA_NONE, 0, false };
    } else if (!prompt && !u->menu_on && in->toggle >= 1 && in->toggle <= 3 && (u->finding || u->view == UI_VIEW_SEARCH)) {
        u->find_opts = ui_toggle(u->find_opts, in->toggle);   // (the search view's are find's, F9)
        if (u->finding) ui_find_update(u, names);
        if (u->view == UI_VIEW_SEARCH) u->search_edited = true;
        return (UiAction){ UA_NONE, 0, false };
    } else if (!prompt && u->view == UI_VIEW_SEARCH && in->search_field) {
        u->focus = UI_FOCUS_SEARCH;
        return (UiAction){ UA_NONE, 0, false };
    } else if (!prompt && !u->menu_on && u->view == UI_VIEW_SEARCH && (in->inside_btn || (u->search_inside && (in->inside_field || in->inside_toggle > 0)))) {
        if (in->inside_btn) ui_search_inside(u, !u->search_inside);
        else if (in->inside_toggle >= 1 && in->inside_toggle <= 3) u->inside_opts = ui_toggle(u->inside_opts, in->inside_toggle), u->search_edited = true;
        else u->focus = UI_FOCUS_INSIDE;
        return (UiAction){ UA_NONE, 0, false };
    }
    // The menu bar (F7.1): a title opens its menu or closes it; while a menu is open a press on one
    // of its entries picks it (one that can't be picked keeps the menu open), and a press anywhere
    // else only closes it.
    if (!prompt && in->menu_title >= 0 && in->menu_title < UI_MENUS) {
        if (u->menu_on && u->menu == in->menu_title) u->menu_on = false;
        else ui_menu_open(u, in->menu_title, false);
        return (UiAction){ UA_NONE, 0, false };
    }
    if (u->menu_on && in->menu_item >= 0 && in->menu_item < UI_MENU[u->menu].n) {
        UiMenuItem it = UI_MENU[u->menu].items[in->menu_item];
        if (!ui_menu_enabled(u, it)) return (UiAction){ UA_NONE, 0, false };
        UiAction a = ui_menu_pick(u, it);
        if (a.kind != UA_NONE || (!u->menu_key.key && !u->menu_key.ch)) return a;
        // (an entry that stands for a key: the loop below runs it first)
    }
    if (u->menu_on && in->menu_away) return u->menu_on = false, (UiAction){ UA_NONE, 0, false };
    // A right press (F8): on a row, its menu. An unmarked row becomes the only one (the marks clear,
    // as Nemo and GTK do); a marked one keeps them, the cursor on it. On the files' empty space, the
    // folder shown's menu, the marks cleared.
    if (!prompt && in->ctx_row >= 0 && u->list.top + in->ctx_row < u->list.n) {
        isize row = u->list.top + in->ctx_row;
        if (!ui_marked(u, row)) ui_mark_clear(u);
        u->list.sel = row;
        ui_ctx_open(u, UI_CTX_ENTRY, names, false);
        return (UiAction){ UA_NONE, 0, false };
    }
    if (!prompt && in->ctx_here) {
        ui_mark_clear(u);
        ui_ctx_open(u, UI_CTX_HERE, names, false);
        return (UiAction){ UA_NONE, 0, false };
    }
    // On a sidebar row (not a heading) or a path button: that folder's menu; the sidebar's cursor
    // goes to its row.
    if (!prompt && in->ctx_side >= 0 && u->side.top + in->ctx_side < u->side.n && !ui_side_head(u, u->side.top + in->ctx_side)) {
        u->side.sel = u->side.top + in->ctx_side;
        ui_folder_open(u, u->side.sel, -1, false);
        return (UiAction){ UA_NONE, 0, false };
    }
    if (!prompt && in->ctx_crumb >= 0 && in->ctx_crumb < in->crumb_n) {
        ui_folder_open(u, -1, in->ctx_crumb, false);
        return (UiAction){ UA_NONE, 0, false };
    }
    // On a tab: its menu, the tab shown (gui/ shows it; the menu acts on the current one).
    if (!prompt && in->ctx_tab >= 0 && in->ctx_tab < u->tab_n) {
        ui_menu_open(u, UI_CTX_TAB, false);   // (it closes an edit line, as the bar's menus do)
        return (UiAction){ UA_TAB_SHOW, in->ctx_tab, false };
    }
    // The Properties panel's pointer (F7.2): a checkbox takes the focus and flips; Close closes.
    if (on == UI_ON_PROPS && in->prop_box >= 0 && in->prop_box < 9) {
        u->prop_at = in->prop_box;
        return u->prop_can ? (UiAction){ UA_CHMOD, in->prop_box, false } : (UiAction){ UA_NONE, 0, false };
    }
    if (on == UI_ON_PROPS && in->modal == UI_MODAL_OK) return u->props = false, (UiAction){ UA_NONE, 0, false };
    // The job rows: the mark kept on a row there is; none left, no keyboard there.
    if (u->job_at >= in->job_n) u->job_at = in->job_n - 1;
    if (u->job_at < 0) u->job_at = 0;
    if (in->job_n <= 0) ui_unfocus(u, UI_FOCUS_JOBS);
    if (!prompt && in->job_x >= 0 && in->job_x < in->job_n) {
        u->cancel_asking = true, u->cancel_id = in->job_id[in->job_x];
        return (UiAction){ UA_NONE, 0, false };
    }
    // A click anywhere gives the path buttons' keyboard back; so does their going.
    bool clicked = in->click >= 0 || in->dclick >= 0 || in->side_click >= 0 || in->header >= 0 || in->bar != UB_NONE ||
                   in->tab_click >= 0 || in->tab_close >= 0 || in->mid_row >= 0 || in->mid_side >= 0 || in->mid_crumb >= 0;
    if (clicked || in->crumb_n <= 0) ui_unfocus(u, UI_FOCUS_CRUMBS);
    if (clicked) ui_unfocus(u, UI_FOCUS_JOBS);
    if (u->focus == UI_FOCUS_CRUMBS && u->crumb_at >= in->crumb_n) u->crumb_at = in->crumb_n - 1;
    // A modal panel's buttons: as the keys they stand for.
    if (on == UI_ON_DELETE && yes_no) return u->confirming = false, (UiAction){ said_yes ? UA_DELETE : UA_NONE, 0, false };
    if (on == UI_ON_CLASH && in->modal == UI_MODAL_ALL) u->ask_all = !u->ask_all;
    if (on == UI_ON_CLASH && in->modal >= UI_MODAL_SKIP && in->modal <= UI_MODAL_CANCEL) {
        static const int ANSWER[] = { UI_ANS_SKIP, UI_ANS_REPLACE, UI_ANS_KEEP, UI_ANS_CANCEL };
        int a = ANSWER[in->modal - UI_MODAL_SKIP];
        u->asking = false;
        return (UiAction){ UA_ANSWER, a, a != UI_ANS_CANCEL && u->ask_all };
    }
    if (in->wheel) ui_scroll(l, in->wheel);
    if (!prompt && in->zoom_to >= 0) return ui_zoom(u, in->zoom_to);
    if (!prompt && in->zoom_wheel) return ui_zoom(u, ui_zoom_level(u->zoom) + (in->zoom_wheel > 0 ? 1 : -1));
    if (!prompt && in->header >= 0 && in->header < SORT_KEYS) {
        u->sort = ui_sort_click(u->sort, (SortKey)in->header);
        return (UiAction){ UA_SORT, 0, false };
    }
    if (!prompt && in->side_mode >= 0 && in->side_mode < SIDE_MODES) return ui_side_mode(u, (SideMode)in->side_mode);
    if (!prompt && in->side_eject >= 0 && u->side_mode == SIDE_PLACES && u->side.top + in->side_eject < u->side.n)
        return (UiAction){ UA_EJECT, u->side.top + in->side_eject, false };
    if (!prompt && in->side_twisty >= 0 && u->side_mode == SIDE_TREE && u->side.top + in->side_twisty < u->side.n)
        return ui_edit_close(u), (UiAction){ UA_TREE_TOGGLE, u->side.top + in->side_twisty, false };
    if (!prompt && in->side_click >= 0 && ui_side_head(u, u->side.top + in->side_click)) return (UiAction){ UA_NONE, 0, false };
    if (!prompt && in->side_click >= 0 && u->side.top + in->side_click < u->side.n) {
        u->side.sel = u->side.top + in->side_click;
        ui_edit_close(u);
        return (UiAction){ UA_PLACE, u->side.sel, false };
    }
    if (!prompt && in->tab_close >= 0) return ui_edit_close(u), (UiAction){ UA_TAB_CLOSE, in->tab_close, false };
    if (!prompt && in->tab_click >= 0) return ui_edit_close(u), (UiAction){ UA_TAB_SHOW, in->tab_click, false };
    if (!prompt && in->mid_row >= 0 && l->top + in->mid_row < l->n) return (UiAction){ UA_TAB_BG_ROW, l->top + in->mid_row, false };
    if (!prompt && in->mid_side >= 0 && u->side.top + in->mid_side < u->side.n && !ui_side_head(u, u->side.top + in->mid_side)) return (UiAction){ UA_TAB_BG_PLACE, u->side.top + in->mid_side, false };
    if (!prompt && in->mid_crumb >= 0) return (UiAction){ UA_TAB_BG_CRUMB, in->mid_crumb, false };
    if (!prompt && in->bar != UB_NONE) {
        switch (in->bar) {
        case UB_BACK: ui_edit_close(u); return (UiAction){ UA_BACK, 0, false };
        case UB_FORWARD: ui_edit_close(u); return (UiAction){ UA_FORWARD, 0, false };
        case UB_UP: ui_edit_close(u); return (UiAction){ UA_PARENT, 0, false };
        case UB_LOCATION:
            if (u->focus == UI_FOCUS_EDIT && u->edit_path) return ui_edit_close(u), (UiAction){ UA_NONE, 0, false };
            return (UiAction){ UA_LOCATION, 0, false };
        case UB_SEARCH:   // as Ctrl+Shift+F (F9; the find bar's until then)
            ui_edit_close(u);
            return ui_search_open(u);
        case UB_LEFT: return (UiAction){ UA_CRUMB_SHIFT, -1, false };
        case UB_RIGHT: return (UiAction){ UA_CRUMB_SHIFT, 1, false };
        case UB_CRUMB: ui_edit_close(u); return (UiAction){ UA_CRUMB, in->crumb, false };
        case UB_RESTORE: if (u->view == UI_VIEW_TRASH && l->n > 0) return (UiAction){ UA_RESTORE, 0, false }; break;
        case UB_EMPTY: if (u->view == UI_VIEW_TRASH) u->empty_asking = true; break;
        case UB_VIEW_LIST: return ui_view(u, false);
        case UB_VIEW_ICONS: return ui_view(u, true);
        case UB_ZOOM_OUT: return ui_zoom(u, ui_zoom_level(u->zoom) - 1);
        case UB_ZOOM_IN: return ui_zoom(u, ui_zoom_level(u->zoom) + 1);
        default: break;
        }
    }
    if (!prompt && in->click >= 0 && l->top + in->click < l->n) {
        isize row = l->top + in->click;
        ui_edit_close(u);
        if (in->click_mods & UI_MOD_SHIFT) ui_mark_range(u, l->sel, row);
        else if (in->click_mods & UI_MOD_CTRL) ui_mark(u, row, !ui_marked(u, row));
        l->sel = row, ui_unfocus(u, UI_FOCUS_SIDE), ui_unfocus(u, UI_FOCUS_FIND);   // (the find bar stays open; reviewer-2, 2026-10-28)
        ui_show_sel(l);
    }
    if (!prompt && in->dclick >= 0 && l->top + in->dclick < l->n) {
        l->sel = l->top + in->dclick, ui_unfocus(u, UI_FOCUS_SIDE), ui_unfocus(u, UI_FOCUS_FIND);
        ui_edit_close(u);
        return (UiAction){ UA_OPEN, l->sel, false };
    }
    if (!prompt && in->backs > 0) return (UiAction){ UA_BACK, 0, false };
    if (!prompt && in->forwards > 0) return (UiAction){ UA_FORWARD, 0, false };
    isize extend = -1;   // a Shift move's start, marked to the cursor once the move is done (ui_extend)
    // An entry the pointer picked runs its key first (F7.4), then the frame's keys.
    for (int k = u->menu_key.key || u->menu_key.ch ? -1 : 0; k < in->nev; k++, extend = ui_extend(u, extend)) {
        UiEvent e = k < 0 ? u->menu_key : in->ev[k];
        if (k < 0) u->menu_key = (UiEvent){ UK_NONE, 0 };
        // Ctrl+Q (F7.1): every window closes, this one as Ctrl+Q closed it before (below).
        if (e.key == UK_QUIT_ALL && !u->quit_asking) {
            u->menu_on = false;
            if (in->jobs_all > 0) u->quit_asking = true, u->cancel_asking = false;
            return (UiAction){ UA_CLOSE_ALL, 0, in->jobs_all <= 0 };
        }
        on = ui_on_top(u), prompt = on != UI_ON_NONE;   // (an earlier key may have opened or closed one)
        if (on == UI_ON_QUIT || on == UI_ON_CANCEL) {
            bool answered, yes = ui_yes_no(e, &answered);
            if (!answered) continue;
            bool quit = u->quit_asking;
            u->quit_asking = u->cancel_asking = false;
            if (yes) return (UiAction){ quit ? UA_QUIT : UA_CANCEL_JOB, quit ? 0 : u->cancel_id, false };
            continue;
        }
        if (on == UI_ON_EMPTY) {
            bool answered, yes = ui_yes_no(e, &answered);
            if (!answered) continue;
            u->empty_asking = false;
            if (yes) return (UiAction){ UA_EMPTY_TRASH, 0, false };
            continue;
        }
        // The menu bar's keys (F7.1): F10 the first menu, Alt and a letter its menu; under a menu
        // open, its keys.
        if (!prompt && !u->menu_on && (e.key == UK_F10 || (e.key == UK_MENU && ui_menu_of(e.ch) >= 0))) {
            ui_menu_open(u, e.key == UK_F10 ? 0 : ui_menu_of(e.ch), true);
            continue;
        }
        // Shift+F10 and the Menu key (F8): the cursor's menu, or the folder shown's in an empty one,
        // while the files have the keys (an edit or find line keeps them); the sidebar's cursor's,
        // or the path button's, while they have them.
        if (!prompt && !u->menu_on && e.key == UK_CONTEXT && u->focus != UI_FOCUS_EDIT && !(u->focus == UI_FOCUS_FIND && u->finding) && u->focus != UI_FOCUS_JOBS) {
            if (u->focus == UI_FOCUS_CRUMBS) ui_folder_open(u, -1, u->crumb_at, true);
            else if (u->focus == UI_FOCUS_SIDE && u->side.n > 0 && !ui_side_head(u, u->side.sel)) ui_folder_open(u, u->side.sel, -1, true);
            else if (u->focus != UI_FOCUS_SIDE) ui_ctx_open(u, u->list.n > 0 ? UI_CTX_ENTRY : UI_CTX_HERE, names, true);
            continue;
        }
        if (u->menu_on && !prompt) {
            UiAction a;
            if (ui_menu_event(u, e, &a)) {
                if (a.kind != UA_NONE) return a;
                if (!u->menu_key.key && !u->menu_key.ch) continue;
                e = u->menu_key, u->menu_key = (UiEvent){ UK_NONE, 0 };   // an entry picked runs its key, here
            }
        }
        if (on == UI_ON_PROPS) {
            UiAction a = ui_props_event(u, e);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        if (on == UI_ON_OW) {
            UiAction a = ui_ow_event(u, e, in);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        if (on == UI_ON_BM) {
            UiAction a = ui_bmp_event(u, e, in->paste);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        if (on == UI_ON_KEYS) {
            ui_keys_event(u, e, in->paste);
            continue;
        }
        if (on == UI_ON_ABOUT || on == UI_ON_BAR) {   // they take every key: Esc and Enter close them
            if (e.key == UK_ESC || e.key == UK_ENTER) *(on == UI_ON_ABOUT ? &u->about : &u->bar_told) = false;
            continue;
        }
        if (u->focus == UI_FOCUS_EDIT && u->edit_path && !prompt && e.key == UK_CRUMBS) {   // from the location line: it closes
            ui_crumbs_focus(u, in);   // (no path buttons: the line stays)
            continue;
        }
        if (u->focus == UI_FOCUS_EDIT && !prompt) {
            UiAction a = ui_edit_event(u, e, in->paste);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        // The search field (F9) before Shift is taken off the moves: it selects with them.
        // Alt+I (F9 step 3): inside files or not, wherever the keys are in the search view.
        if (u->view == UI_VIEW_SEARCH && !prompt && e.key == UK_MENU && e.ch == 'i') {
            ui_search_inside(u, !u->search_inside);
            continue;
        }
        if ((u->focus == UI_FOCUS_SEARCH || (u->focus == UI_FOCUS_INSIDE && u->search_inside)) && u->view == UI_VIEW_SEARCH && !prompt &&
            ui_search_event(u, e, in->paste))
            continue;
        // The find bar's likewise (2026-10-28), after the questions: a clash or the delete question
        // takes its keys first.
        if (u->focus == UI_FOCUS_FIND && u->finding && !prompt && ui_find_event(u, names, e, in)) continue;
        if (u->focus == UI_FOCUS_CRUMBS && !prompt) {
            UiAction a;
            if (ui_crumbs_event(u, in, e, &a)) {
                if (a.kind != UA_NONE) return a;
                continue;
            }
        }
        if (u->focus == UI_FOCUS_JOBS && !prompt && ui_jobs_event(u, in, e)) continue;   // before a clash's keys
        // Outside an edit line and the path buttons, Shift doesn't change Enter; with a move in the
        // files (arrows, Home, End, or H, J, K, L) it selects what the cursor passes over (below).
        bool shifted = e.key == UK_SEL_LEFT || e.key == UK_SEL_RIGHT || e.key == UK_SEL_HOME || e.key == UK_SEL_END ||
                       e.key == UK_SEL_UP || e.key == UK_SEL_DOWN;
        bool shift_enter = e.key == UK_SHIFT_ENTER;   // Enter for the questions and the find bar; Open With in the files
        switch (e.key) {
        case UK_SEL_LEFT: e.key = UK_LEFT; break;
        case UK_SEL_RIGHT: e.key = UK_RIGHT; break;
        case UK_SEL_HOME: e.key = UK_HOME; break;
        case UK_SEL_END: e.key = UK_END; break;
        case UK_SEL_UP: e.key = UK_UP; break;
        case UK_SEL_DOWN: e.key = UK_DOWN; break;
        case UK_SHIFT_ENTER: e.key = UK_ENTER; break;
        default: break;
        }

        if (on == UI_ON_DELETE) {
            UiAction a = ui_confirm_event(u, e);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        if (on == UI_ON_CLASH) {
            UiAction a = ui_ask_event(u, e);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        // H, J, K and L (Shift and h, j, k, l) as the Shift arrows: here, after the questions and the
        // find bar have taken their letters (K keeps all in a clash; text in the find bar).
        if (e.key == UK_NONE && (e.ch == 'H' || e.ch == 'J' || e.ch == 'K' || e.ch == 'L')) {
            shifted = true;
            e = (UiEvent){ e.ch == 'H' ? UK_LEFT : e.ch == 'J' ? UK_DOWN : e.ch == 'K' ? UK_UP : UK_RIGHT, 0 };
        }
        if (shifted && u->focus != UI_FOCUS_SIDE) extend = l->sel;
        // The keys and letters that move a cursor: the sidebar's when it has the keyboard. In the
        // icon view, Up and Down move a row and Left and Right a cell (F6.2).
        UiList *m = u->focus == UI_FOCUS_SIDE ? &u->side : l;
        isize page = m->rows > 1 ? m->rows - 1 : 1;
        bool grid = u->grid && m == l;
        UiKey key = e.key;
        if (key == UK_NONE) {
            switch (e.ch) {
            case 'j': key = UK_DOWN; break;
            case 'k': key = UK_UP; break;
            case 'l': key = UK_RIGHT; break;
            case 'h': key = UK_LEFT; break;
            case 'g': key = UK_HOME; break;
            case 'G': key = UK_END; break;
            case '/': key = UK_FIND; break;
            case '.': key = UK_HIDDEN; break;
            case 't': return ui_view_act(u, (UiAction){ UA_TERMINAL, 0, false });
            case 'r':
                if (u->view == UI_VIEW_TRASH && u->focus != UI_FOCUS_SIDE && l->n > 0) return (UiAction){ UA_RESTORE, 0, false };
                continue;
            case '~': return (UiAction){ UA_HOMEDIR, 0, false };
            case 's': u->sort = (Sort){ (SortKey)((u->sort.key + 1) % SORT_KEYS), false }; return (UiAction){ UA_SORT, 0, false };
            case 'S': u->sort.reverse = !u->sort.reverse; return (UiAction){ UA_SORT, 0, false };
            case 'n': ui_find_again(u, names, 1); continue;
            case 'N': ui_find_again(u, names, -1); continue;
            case ' ':
                // Space marks the entry under the cursor, or unmarks it, and moves on.
                if (u->focus != UI_FOCUS_SIDE && l->n > 0) ui_mark(u, l->sel, !ui_marked(u, l->sel)), ui_move(l, 1);
                continue;
            default: continue;
            }
        }
        if (grid && (key == UK_LEFT || key == UK_RIGHT)) {
            if (shifted) ui_move(l, key == UK_LEFT ? -1 : 1);   // a Shift move doesn't wrap
            else ui_step_round(l, key == UK_LEFT ? -1 : 1, false);
            continue;
        }
        if (grid && (key == UK_UP || key == UK_DOWN)) {
            if (shifted) ui_move_rows(l, key == UK_UP ? -1 : 1);
            else ui_step_round(l, key == UK_UP ? -1 : 1, true);
            continue;
        }
        if (grid && (key == UK_PAGEUP || key == UK_PAGEDOWN)) {
            ui_move_rows(l, key == UK_PAGEUP ? -page : page);
            continue;
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
        case UK_VIEW_ICONS: return ui_view(u, true);
        case UK_VIEW_LIST: return ui_view(u, false);
        case UK_ZOOM_IN: return ui_zoom(u, ui_zoom_level(u->zoom) + 1);
        case UK_ZOOM_OUT: return ui_zoom(u, ui_zoom_level(u->zoom) - 1);
        case UK_ZOOM_RESET: return ui_zoom(u, ui_zoom_level(UI_ZOOM_DEFAULT));
        // Enter opens and Backspace goes up; Left and Right don't, in the list or the icon view
        // (Michael, 2026-10-24): they only open and close the tree's folders.
        case UK_ENTER:
            if (u->focus == UI_FOCUS_SIDE && u->side.n > 0) return (UiAction){ UA_PLACE, u->side.sel, false };
            if (u->focus != UI_FOCUS_SIDE && l->n > 0 && shift_enter) return u->view == UI_VIEW_TRASH ? (UiAction){ UA_NOT_IN_TRASH, 0, false } : ui_ow_open(u);
            if (u->focus != UI_FOCUS_SIDE && l->n > 0) return (UiAction){ UA_OPEN, l->sel, u->marked > 0 };   // all: the marked ones (F7.3)
            break;
        case UK_RIGHT:
            if (u->focus == UI_FOCUS_SIDE && u->side.n > 0 && u->side_mode == SIDE_TREE) return (UiAction){ UA_TREE_RIGHT, u->side.sel, false };
            break;
        case UK_LEFT:
            if (u->focus == UI_FOCUS_SIDE && u->side.n > 0 && u->side_mode == SIDE_TREE) return (UiAction){ UA_TREE_LEFT, u->side.sel, false };
            break;
        case UK_BACKSPACE: return (UiAction){ UA_PARENT, 0, false };
        case UK_F9: return ui_side_mode(u, (SideMode)((u->side_mode + 1) % SIDE_MODES));
        case UK_TAB:
        case UK_SIDE:
        case UK_WORD_LEFT:   // (Ctrl+Left: a word in a text field, the sidebar here)
            // Tab goes to the sidebar and back, Ctrl+Left there; a hidden one shows as places
            // (gui/ lists them for the mode, the cursor off a heading).
            if (key == UK_TAB && u->focus == UI_FOCUS_SIDE) {
                u->focus = UI_FOCUS_LIST;
                break;
            }
            if (u->side_mode == SIDE_HIDDEN) {
                UiAction a = ui_side_mode(u, SIDE_PLACES);
                u->focus = UI_FOCUS_SIDE;
                return a;
            }
            u->focus = u->side.n > 0 ? UI_FOCUS_SIDE : UI_FOCUS_LIST;
            if (u->focus == UI_FOCUS_SIDE) ui_side_settle(u, 1);
            break;
        case UK_ESC:
            // The selection, then the sidebar's keyboard (a job is cancelled from its row, F5.3).
            if (u->marked) ui_mark_clear(u);
            else ui_unfocus(u, UI_FOCUS_SIDE);
            break;
        case UK_BACK: return (UiAction){ UA_BACK, 0, false };
        case UK_FORWARD: return (UiAction){ UA_FORWARD, 0, false };
        case UK_FIND: ui_find_open(u); break;
        case UK_SELECT_MATCH:
            if (l->n > 0) ui_find_open(u), u->find_select = true;
            break;
        case UK_HIDDEN: return (UiAction){ UA_HIDDEN, 0, false };
        case UK_PERMS_COLS:   // (greyed in the icon view, as its entry)
            if (!u->grid) return u->perms_cols = !u->perms_cols, (UiAction){ UA_PERMS_COLS, 0, false };
            break;
        case UK_GROUP_COL:
            if (!u->grid && u->perms_cols) return u->group_col = !u->group_col, (UiAction){ UA_GROUP_COL, 0, false };
            break;
        case UK_ALL:
            for (isize i = 0; i < l->n; i++) ui_mark(u, i, true);
            break;
        case UK_INVERT:
            for (isize i = 0; i < l->n; i++) ui_mark(u, i, !ui_marked(u, i));
            break;
        case UK_RELOAD: return (UiAction){ UA_RELOAD, 0, false };
        case UK_HOMEDIR: return (UiAction){ UA_HOMEDIR, 0, false };
        case UK_TRASH_VIEW: return (UiAction){ UA_TRASH_VIEW, 0, false };
        case UK_ADD_BOOKMARK: return ui_view_act(u, (UiAction){ UA_ADD_BOOKMARK, 0, false });
        case UK_EDIT_BOOKMARKS: return ui_bmp_open(u, 0);
        case UK_SHORTCUTS: return ui_keys_open(u);
        case UK_F2:
            if (l->n > 0 && u->focus != UI_FOCUS_SIDE && u->view == UI_VIEW_TRASH) return (UiAction){ UA_NOT_IN_TRASH, 0, false };
            if (l->n > 0 && u->focus != UI_FOCUS_SIDE) ui_edit_rename(u, names.at(names.ctx, l->sel), names.dir && names.dir(names.ctx, l->sel));
            break;
        case UK_DELETE:
            // In the trash view Delete is for good, asked first.
            if (l->n > 0 && u->focus != UI_FOCUS_SIDE && u->view == UI_VIEW_TRASH) u->confirming = true, u->confirm_n = u->marked ? u->marked : 1;
            else if (l->n > 0 && u->focus != UI_FOCUS_SIDE) return (UiAction){ UA_TRASH, 0, false };
            break;
        case UK_SHIFT_DELETE:
            if (l->n > 0 && u->focus != UI_FOCUS_SIDE) u->confirming = true, u->confirm_n = u->marked ? u->marked : 1;
            break;
        case UK_COPY: if (l->n > 0 && u->focus != UI_FOCUS_SIDE) return ui_view_act(u, (UiAction){ UA_COPY, 0, false }); break;
        case UK_MAKE_LINK: if (l->n > 0 && u->focus != UI_FOCUS_SIDE) return ui_view_act(u, (UiAction){ UA_MAKE_LINK, 0, false }); break;
        case UK_CUT: if (l->n > 0 && u->focus != UI_FOCUS_SIDE) return ui_view_act(u, (UiAction){ UA_CUT, 0, false }); break;
        case UK_PASTE: return ui_view_act(u, (UiAction){ UA_PASTE, 0, false });
        case UK_UNDO: return (UiAction){ UA_UNDO, 0, false };
        case UK_NEWFOLDER: return ui_view_act(u, (UiAction){ UA_NEWFOLDER, 0, false });
        case UK_LOCATION: case UK_WORD_RIGHT: return (UiAction){ UA_LOCATION, 0, false };   // (Ctrl+Right too, outside a text field)
        case UK_CRUMBS: ui_crumbs_focus(u, in); break;
        case UK_JOBS:   // the find bar first when it is open, then the job rows (Michael, 2026-10-26)
            if (u->finding) u->focus = UI_FOCUS_FIND;
            else if (in->job_n > 0) u->focus = UI_FOCUS_JOBS, u->job_at = 0;
            break;
        case UK_PARENT: return (UiAction){ UA_PARENT, 0, false };
        case UK_TAB_NEW: return (UiAction){ UA_TAB_NEW, 0, false };
        case UK_NEW_WINDOW: return ui_view_act(u, (UiAction){ UA_NEW_WINDOW, 0, false });
        case UK_PROPERTIES: return ui_props_open(u);
        case UK_TERMINAL: return ui_view_act(u, (UiAction){ UA_TERMINAL, 0, false });
        case UK_SEARCH: return ui_search_open(u);
        case UK_CONTAINING:   // (the search view's: elsewhere every entry is in the folder shown)
            if (u->view == UI_VIEW_SEARCH && u->focus != UI_FOCUS_SIDE && l->n > 0) return (UiAction){ UA_CONTAINING, l->sel, false };
            break;
        case UK_TAB_CLOSE: return (UiAction){ UA_TAB_CLOSE, -1, false };
        case UK_TAB_NEXT: return (UiAction){ UA_TAB_STEP, 1, false };
        case UK_TAB_PREV: return (UiAction){ UA_TAB_STEP, -1, false };
        case UK_TAB_LEFT: return (UiAction){ UA_TAB_MOVE, -1, false };
        case UK_TAB_RIGHT: return (UiAction){ UA_TAB_MOVE, 1, false };
        case UK_TAB_JUMP: return (UiAction){ UA_TAB_JUMP, (isize)e.ch, false };
        default: break;
        }
    }
    return (UiAction){ UA_NONE, 0, false };
}
