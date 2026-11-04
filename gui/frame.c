// One frame: the tooltips, the whole window's layout, and drawing it. Included by main.c.

// ---- tooltips (Michael, 2026-10-24) ------------------------------------------------------------
//
// The icon-only buttons, numbered for UiTip: a top bar button 100 + its UiBar, a sidebar mode
// button 200 + its SideMode, a drive's eject button 300 + its row, a tab's x 400 + its place.

// The zoom slider's track is 500 (F6.3), the find bar's chevrons 600 and 601.
enum { TIP_BAR = 100, TIP_MODE = 200, TIP_EJECT = 300, TIP_TAB_X = 400, TIP_ZOOM = 500, TIP_FIND = 600 };

// Target k's text, and the element it hangs from; false for none (gone since).
static bool tip_of(int k, char *buf, size_t cap, Clay_ElementId *id)
{
    static const struct { UiBar b; const char *text; } BAR[] = {
        { UB_BACK, "Back (Alt+Left)" }, { UB_FORWARD, "Forward (Alt+Right)" }, { UB_UP, "Up to the parent (Backspace)" },
        { UB_LOCATION, "Type a location (Ctrl+L)" }, { UB_SEARCH, "Search (Ctrl+Shift+F)" }, { UB_LEFT, "Earlier folders in the path" },
        { UB_RIGHT, "Later folders in the path" }, { UB_VIEW_LIST, "List (Ctrl+1)" }, { UB_VIEW_ICONS, "Icons (Ctrl+2)" }, { UB_BRANCH, "Git branches" },
        { UB_ZOOM_OUT, "Smaller icons (Ctrl+-)" }, { UB_ZOOM_IN, "Bigger icons (Ctrl+=)" },
    };
    static const char *const MODES[SIDE_MODES] = { "Places (F9)", "Tree (F9)", "No sidebar (F9)" };
    if (k >= TIP_FIND) {
        *id = k == TIP_FIND ? CLAY_ID("find-prev") : CLAY_ID("find-next");
        return app.ui.finding && snprintf(buf, cap, "%s", k == TIP_FIND ? "Previous match (Up)" : "Next match (Down)") > 0;
    }
    if (k == TIP_ZOOM) {
        *id = CLAY_ID("zoom-track");
        return app.ui.grid && snprintf(buf, cap, "Icons %d px (Ctrl+0: %d px)", ui_zoom_px(&app.ui), UI_ZOOM_DEFAULT) > 0;
    }
    if (k >= TIP_TAB_X) {
        *id = CLAY_IDI("tab-x", k - TIP_TAB_X);
        return snprintf(buf, cap, "Close the tab (Ctrl+W)") > 0;
    }
    if (k >= TIP_EJECT) {
        isize i = app.ui.side.top + (k - TIP_EJECT);
        if (app.ui.side_mode != SIDE_PLACES || i >= app.places.n) return false;
        *id = CLAY_IDI("side-eject", k - TIP_EJECT);
        return snprintf(buf, cap, "Eject %s", app.places.p[i].label) > 0;
    }
    if (k >= TIP_MODE) {
        *id = CLAY_IDI("mode", k - TIP_MODE);
        return snprintf(buf, cap, "%s", MODES[k - TIP_MODE]) > 0;
    }
    for (int j = 0; j < COUNT(BAR); j++)
        if (TIP_BAR + (int)BAR[j].b == k) return *id = bar_id(BAR[j].b), snprintf(buf, cap, "%s", BAR[j].text) > 0;
    return false;
}

// The tooltip, under a top bar button or a tab's x, over the bottom bar's, beside the sidebar's;
// lined up with the button's edge on the side the window has room.
static void layout_tip(int w)
{
    char txt[NAME_MAX + 64];
    Clay_ElementId id;
    if (!app.tip.shown || !tip_of(app.tip.target, txt, sizeof txt, &id)) return;
    Clay_ElementData d = Clay_GetElementData(id);
    if (!d.found) return;
    bool above = (app.tip.target >= TIP_MODE && app.tip.target < TIP_EJECT) || app.tip.target == TIP_ZOOM || app.tip.target >= TIP_FIND ||
                 app.tip.target == TIP_BAR + UB_ZOOM_OUT || app.tip.target == TIP_BAR + UB_ZOOM_IN;
    bool right = d.boundingBox.x + d.boundingBox.width / 2 > w / 2.0f;
    Clay_FloatingAttachPoints at = above && right ? (Clay_FloatingAttachPoints){ CLAY_ATTACH_POINT_RIGHT_BOTTOM, CLAY_ATTACH_POINT_RIGHT_TOP }
                                 : above ? (Clay_FloatingAttachPoints){ CLAY_ATTACH_POINT_LEFT_BOTTOM, CLAY_ATTACH_POINT_LEFT_TOP }
                                 : right ? (Clay_FloatingAttachPoints){ CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_BOTTOM }
                                         : (Clay_FloatingAttachPoints){ CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_BOTTOM };
    Theme *t = &app.theme;
    CLAY(CLAY_ID("tooltip"), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) }, .padding = { 8, 8, 4, 4 } },
                               .backgroundColor = cc(t->bg_light), .cornerRadius = CLAY_CORNER_RADIUS(3),
                               .border = { .color = mix(t->fg_dim, t->bg_light, 0.6f), .width = CLAY_BORDER_OUTSIDE(1) },
                               .floating = { .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID, .parentId = id.id, .zIndex = 40,
                                             .offset = { 0, above ? -4.0f : 4.0f }, .attachPoints = at,
                                             .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
        text(cz(arena_sprintf(&frame, "%s", txt)), cc(t->fg));
    }
}

// What the menus grey or mark that only gui/ knows (F7.4; the Bookmarks menu's, the cursor's entry
// for the right-click menus, the branch menu's since): set at each layout,
// which follows any change that alters it, before the next input. Paste: the system clipboard can't be read without asking it, so it
// counts as holding files; without one, ofx's own copy does.
static void menu_facts(void)
{
    Ui *u = &app.ui;
    u->can_undo = undo.kind != UNDO_NONE && undo.n > 0;
    u->can_back = hist.nback > 0, u->can_forward = hist.nfwd > 0;
    u->can_reopen = closed_tabs.n > 0;   // (F11)
    u->can_up = view_can_up();
    u->can_paste = clip.ok || own.n > 0;
    u->shows_hidden = app.show_hidden;
    u->bm_n = 0, u->bm_here = -1;
    for (int i = 0; i < bmarks.n && u->bm_n < UI_MENU_BOOKMARKS; i++) {
        if (!bmarks.b[i].path[0]) continue;
        if (strcmp(bmarks.b[i].path, app.cwd) == 0) u->bm_here = u->bm_n;
        u->bm_n++;
    }
    u->can_bookmark = u->view == UI_VIEW_FOLDER && !bmarks.full && bm_find(&bmarks, app.cwd) < 0;
    // The cursor's entry (F8): a link that leads somewhere (Follow Link); a folder bookmarked already.
    const Entry *e = app.have_list && u->list.n > 0 && u->list.sel < app.list.n ? &app.list.e[u->list.sel] : NULL;
    char p[PATH_MAX];
    u->sel_link = e && (e->kind == K_LINK_FILE || e->kind == K_LINK_DIR);
    u->sel_hit = e && e->hit;   // (F9 step 4: Open at Line)
    u->side_here = u->side_mode == SIDE_PLACES ? app.place_cur : u->side_mode == SIDE_TREE ? tree_find_near(&tree, str_from_c(app.cwd)) : -1;
    u->sel_bm = e && kind_is_dir(e->kind) && entry_path(u->list.sel, p, sizeof p) && bm_find(&bmarks, p) >= 0;
    u->tab_at = tabbar.cur, u->tab_n = tabbar.n;   // (the tab menu's moves, F8)
    u->bm_total = bmarks.n, u->bm_more = 0, u->bm_more_at = -1;
    for (int i = 0, local = 0; i < bmarks.n; i++) {
        if (!bmarks.b[i].path[0] || local++ < UI_MENU_BOOKMARKS) continue;
        if (u->bm_more++ == 0) u->bm_more_at = i;
    }
    // The branch button and its menu (F12 step 5b); the one checked out is the first or none
    // (git_branches_merge), so always among the menu's.
    u->branch_on = branch_label() != NULL;
    u->br_n = git.br.n < UI_MENU_BRANCHES ? git.br.n : UI_MENU_BRANCHES, u->br_here = git.br.cur, u->br_note = git.br.n == 0;
    u->br_total = git.br.total;   // (the panel's: "All branches… (N)", F12 step 6)
}

// The window, and the modal panel on top when one is up.
// The element a drop target is drawn over (F13 step 3).
static Clay_ElementId drop_elem(const Drop *d)
{
    switch (d->kind) {
    case DROP_ROW: return CLAY_IDI("row", d->at);
    case DROP_SIDE: return CLAY_IDI("side-row", d->at);
    case DROP_TAB: return CLAY_IDI("tab", d->at);
    case DROP_CRUMB: return CLAY_IDI("crumb", d->at);
    default: return CLAY_ID("list");
    }
}

static bool        drop_marked, badge_drawn;   // as drawn last (a script's state line)
static const char *badge_op;

// A drag of files (F13 step 3): the place it would drop in, outlined in the accent (nowhere it
// can't), and by the pointer a badge: the entry pressed, its icon and name, "+ N more", and what
// the drop does there ("Move", "Copy", "Trash").
static void layout_drag(void)
{
    drop_marked = badge_drawn = false, badge_op = NULL;
    if (app.drag != DRAG_FILES) return;
    Theme *t = &app.theme;
    Clay_ElementData e = Clay_GetElementData(drop_elem(&app.drop));
    if (app.drop.ok && e.found) {
        drop_marked = true;
        CLAY(CLAY_ID("drop-mark"), { .layout = { .sizing = { CLAY_SIZING_FIXED(e.boundingBox.width), CLAY_SIZING_FIXED(e.boundingBox.height) } },
                                     .cornerRadius = CLAY_CORNER_RADIUS(3),
                                     .border = { .color = cc(t->accent), .width = CLAY_BORDER_OUTSIDE(2) },
                                     .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 25, .offset = { e.boundingBox.x, e.boundingBox.y },
                                                   .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {}
    }
    static const char *const OPS[] = { "Move", "Copy", "Trash" };
    badge_drawn = true, badge_op = app.drop.ok ? OPS[drop_op(&app.drop)] : NULL;
    CLAY(CLAY_ID("drag-badge"), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) }, .padding = { 8, 8, 5, 5 }, .childGap = 8,
                                              .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                  .backgroundColor = cc(t->bg_light), .cornerRadius = CLAY_CORNER_RADIUS(3),
                                  .border = { .color = cc(t->accent), .width = CLAY_BORDER_OUTSIDE(1) },
                                  .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 30, .offset = { app.drag_px + 14, app.drag_py + 16 },
                                                .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
        icon_box(CLAY_ID("drag-icon"), app.drag_icon, SIDE_ICON_PX);
        text(fit(str_from_c(app.drag_name), 240, false), cc(t->fg));
        if (dragged.n > 1) text(cz(arena_sprintf(&frame, "+ %td more", dragged.n - 1)), cc(t->fg_dim));
        if (badge_op)
            CLAY(CLAY_ID("drag-op"), { .layout = { .padding = { 6, 6, 1, 1 } }, .backgroundColor = cc(t->accent), .cornerRadius = CLAY_CORNER_RADIUS(2) }) {
                text(cz(badge_op), cc(t->bg));
            }
    }
}

static Clay_RenderCommandArray layout(int w, int h)
{
    menu_facts();
    arena_reset(&frame);
    arena_reset(&scratch);
    app.grid_cells = app.grid_full_lines = app.grid_hover_lines = 0, app.grid_hover_cut = false;
    app.grid_first = app.grid_last = -1;
    Theme *t = &app.theme;
    Clay_SetLayoutDimensions((Clay_Dimensions){ (float)w, (float)h });
    Clay_BeginLayout();
    i64 now = (i64)time(NULL);
    CLAY(CLAY_ID("root"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                            .backgroundColor = cc(t->bg) }) {
        float foot_w = (float)w - modes_w() - zoom_w() - 2 * PAD;   // the status part of the bottom bar
        if (bar_shown()) layout_menubar();
        if (app.ui.menu_on && app.ui.menu >= UI_MENUS) layout_menu(app.ui.menu);   // a right-click menu (F8)
        layout_bar((float)w);
        float body_h = (float)(h - bar_h() - HEADER_H - FOOTER_H);
        CLAY(CLAY_ID("body"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(body_h) } } }) {
        if (app.ui.side_mode == SIDE_TREE) layout_tree(body_h);
        else if (side_w()) layout_side(body_h);
        CLAY(CLAY_ID("main"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
        // The tab bar over the files only, not the sidebar (Michael, 2026-10-21).
        if (tabbar.n >= 2) layout_tabs((float)(w - side_w()));
        if (app.ui.view == UI_VIEW_TRASH) layout_trash_bar();
        if (app.ui.view == UI_VIEW_SEARCH) layout_search_bar();
        if (!app.ui.grid) layout_cols();
        CLAY(CLAY_ID("list"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                            .padding = { 0, 0, app.ui.grid ? PAD : 0, 0 } },
                                .clip = { .vertical = true } }) {
            if (!app.have_list) {
                CLAY(CLAY_ID("error"), { .layout = { .padding = { PAD, PAD, PAD, PAD } } }) { text(cz(app.error), cc(t->red)); }
            } else if (app.list.n == 0) {
                CLAY(CLAY_ID("empty"), { .layout = { .padding = { PAD, PAD, PAD, PAD } } }) {
                    text(cz(app.ui.view == UI_VIEW_SEARCH ? (!app.ui.search.len ? "Type a name to search" : app.search_done ? "Nothing found" : "Searching\xE2\x80\xA6")
                            : app.list.hidden  ? "Only hidden files"
                                               : "Empty folder"),
                         cc(t->fg_dim));
                }
            }
            if (app.have_list && app.ui.grid) layout_grid();
            for (int r = 0; app.have_list && !app.ui.grid && r < app.ui.list.rows && app.ui.list.top + r < app.list.n; r++)
                layout_row(r, app.ui.list.top + r, (float)w, now);
        }
        if (app.ui.finding) layout_find_bar();
        layout_jobs();
        }
        }
        CLAY(CLAY_ID("footer"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(FOOTER_H) },
                                              .padding = { 0, PAD, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                  .backgroundColor = cc(t->bg_light),
                                  .border = { .color = mix(t->fg_dim, t->bg_light, 0.3f), .width = { .top = 1 } } }) {
            layout_modes();
            CLAY(CLAY_ID("footer-line"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                           .clip = { .horizontal = true } }) {
            if (modal_kind() == UI_ON_PROPS) {
                text(fit(S("Properties: \xE2\x86\x90 \xE2\x86\x91 \xE2\x86\x93 \xE2\x86\x92 (h j k l) move \xC2\xB7 Space or Enter changes \xC2\xB7 Esc closes"), foot_w, false),
                     cc(t->fg_dim));
            } else if (modal_kind() == UI_ON_BAR) {
                text(fit(S("Menu bar hidden: Enter or Esc closes this"), foot_w, false), cc(t->fg_dim));
            } else if (modal_kind() == UI_ON_ABOUT) {
                text(fit(S("About: Esc or Enter closes"), foot_w, false), cc(t->fg_dim));
            } else if (modal_kind() == UI_ON_KEYS) {
                text(fit(S("Keyboard Shortcuts: type to filter \xC2\xB7 \xE2\x86\x91 \xE2\x86\x93 PgUp PgDn scroll \xC2\xB7 Esc or F1 closes"), foot_w, false), cc(t->fg_dim));
            } else if (modal_kind() == UI_ON_BM) {
                text(fit(str_from_c(app.ui.bmp_naming ? "Rename: Enter renames (empty: the folder's name) \xC2\xB7 Esc keeps the name"
                                             : "Bookmarks: \xE2\x86\x91 \xE2\x86\x93 (k j) move \xC2\xB7 Enter goes there \xC2\xB7 Delete removes \xC2\xB7 F2 renames \xC2\xB7 "
                                               "Alt+\xE2\x86\x91 Alt+\xE2\x86\x93 move it \xC2\xB7 Esc closes"), foot_w, false), cc(t->fg_dim));
            } else if (modal_kind() == UI_ON_BRANCHES) {
                text(fit(S("Branches: type to filter \xC2\xB7 Tab or \xE2\x86\x93 the list \xC2\xB7 Enter switches \xC2\xB7 Esc closes"), foot_w, false), cc(t->fg_dim));
            } else if (modal_kind() == UI_ON_OW) {
                text(fit(S("Open With: type to filter \xC2\xB7 Tab or \xE2\x86\x93 the list \xC2\xB7 Enter opens \xC2\xB7 Esc closes \xC2\xB7 "
                           "Alt+C/W/R case, words, regex \xC2\xB7 Alt+D default"), foot_w, false), cc(t->fg_dim));
            } else if (modal_kind() != UI_ON_NONE) {
                text(cz("Answer the question above"), cc(t->fg_dim));
            } else if (app.ui.menu_on && app.ui.menu >= UI_MENUS) {
                text(fit(S("Menu: \xE2\x86\x91 \xE2\x86\x93 (k j) move \xC2\xB7 Enter picks \xC2\xB7 Esc closes"), foot_w, false), cc(t->fg_dim));
            } else if (app.ui.menu_on) {
                text(fit(S("Menu: \xE2\x86\x91 \xE2\x86\x93 (k j) move \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 the next menu \xC2\xB7 Enter picks \xC2\xB7 Esc closes"), foot_w, false),
                     cc(t->fg_dim));
            } else if (app.ui.focus == UI_FOCUS_CRUMBS) {
                text(fit(S("Path: \xE2\x86\x90 \xE2\x86\x92 (h l) move \xC2\xB7 Enter goes there \xC2\xB7 Shift+Enter in a new tab \xC2\xB7 Esc back to the list"), foot_w, false),
                     cc(t->fg_dim));
            } else if (app.ui.focus == UI_FOCUS_EDIT && app.ui.edit_path) {
                text(cz("Go to: Enter goes there \xC2\xB7 Esc cancels \xC2\xB7 ~ is home, a relative path starts here"), cc(t->fg_dim));
            } else if (app.ui.focus == UI_FOCUS_EDIT) {
                text(cz(app.edit_new == UNDO_MKDIR  ? "New folder: Enter keeps the name \xC2\xB7 Esc keeps \"New folder\""
                        : app.edit_new == UNDO_MKFILE ? "New file: Enter keeps the name \xC2\xB7 Esc keeps \"New file\""
                                                      : "Rename: Enter renames \xC2\xB7 Esc cancels"),
                     cc(t->fg_dim));
            } else if (app.ui.focus == UI_FOCUS_JOBS) {
                text(fit(S("Jobs: \xE2\x86\x91 \xE2\x86\x93 (k j) move \xC2\xB7 Enter cancels the marked job \xC2\xB7 Esc back to the list"), foot_w, false),
                     cc(t->fg_dim));
            } else if (app.ui.finding && app.ui.focus == UI_FOCUS_FIND && app.ui.find_select) {
                text(fit(S("Select: Enter selects every match \xC2\xB7 Esc cancels \xC2\xB7 \xE2\x86\x91 \xE2\x86\x93 previous and next \xC2\xB7 "
                           "Alt+C/W/R case, words, regex"), foot_w, false), cc(t->fg_dim));
            } else if (app.ui.finding && app.ui.focus == UI_FOCUS_FIND) {
                text(fit(S("Find: \xE2\x86\x91 \xE2\x86\x93 previous and next \xC2\xB7 Enter keeps \xC2\xB7 Esc closes \xC2\xB7 Ctrl+J the jobs \xC2\xB7 "
                           "Alt+C/W/R case, words, regex"), foot_w, false), cc(t->fg_dim));
            } else if (app.status[0]) {
                text(fit(str_from_c(app.status), foot_w, false), app.status_bad ? cc(t->red) : cc(t->fg));
            } else if (app.ui.view == UI_VIEW_SEARCH && !app.ui.marked) {
                char info[512];
                search_status(info, sizeof info);
                text(fit(str_from_c(arena_sprintf(&frame, "%s", info)), foot_w, false), cc(t->fg_dim));
            } else {
                char info[NAME_MAX + 64];
                status_info(info, sizeof info);
                text(fit(str_from_c(arena_sprintf(&frame, "%s", info)), foot_w, false), cc(t->fg_dim));
            }
            }
            if (app.ui.grid) layout_zoom();
        }
        layout_tip(w);
        layout_drag();
        UiOnTop mk = modal_kind();
        if (mk != UI_ON_NONE) layout_modal(mk, (float)w, (float)h);
    }
    return Clay_EndLayout(0);   // deltaTime drives transitions, which we do not use
}

static void draw(void)
{
    font_set_dpi(GetWindowScaleDPI().x);
    if (window_scale() != app.icon_scale) icons_reset();
    font_frame_begin();
    thumbs_frame();
    Clay_RenderCommandArray cmds = layout(GetScreenWidth(), GetScreenHeight());
    render_draw(cmds, cc(app.theme.bg));
}
