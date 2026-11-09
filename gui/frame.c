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
    // (each with its command's first key as it is: ui_with_key, F15)
    static const struct { UiBar b; const char *text; UiKey cmd; } BAR[] = {
        { UB_BACK, "Back", UK_BACK }, { UB_FORWARD, "Forward", UK_FORWARD }, { UB_UP, "Up to the parent", UK_PARENT },
        { UB_LOCATION, "Type a location", UK_LOCATION }, { UB_SEARCH, "Search", UK_SEARCH }, { UB_LEFT, "Earlier folders in the path", UK_NONE },
        { UB_RIGHT, "Later folders in the path", UK_NONE }, { UB_VIEW_LIST, "List", UK_VIEW_LIST }, { UB_VIEW_ICONS, "Icons", UK_VIEW_ICONS },
        { UB_BRANCH, "Git branches", UK_NONE }, { UB_ZOOM_OUT, "Smaller icons", UK_ZOOM_OUT }, { UB_ZOOM_IN, "Bigger icons", UK_ZOOM_IN },
    };
    static const char *const MODES[SIDE_MODES] = { "Places", "Tree", "No sidebar" };   // (the sidebar's next mode's key)
    if (k >= TIP_FIND) {
        *id = k == TIP_FIND ? CLAY_ID("find-prev") : CLAY_ID("find-next");
        return app.ui.finding && snprintf(buf, cap, "%s", k == TIP_FIND ? "Previous match (Up)" : "Next match (Down)") > 0;
    }
    if (k == TIP_ZOOM) {
        *id = CLAY_ID("zoom-track");
        const char *zk = ui_cmd_key(UK_ZOOM_RESET, 0);
        return app.ui.grid && (zk[0] ? snprintf(buf, cap, "Icons %d px (%s: %d px)", ui_zoom_px(&app.ui), zk, UI_ZOOM_DEFAULT)
                                    : snprintf(buf, cap, "Icons %d px", ui_zoom_px(&app.ui))) > 0;
    }
    if (k >= TIP_TAB_X) {
        *id = CLAY_IDI("tab-x", k - TIP_TAB_X);
        return ui_with_key(buf, cap, "Close the tab", UK_TAB_CLOSE)[0];
    }
    if (k >= TIP_EJECT) {
        isize i = app.ui.side.top + (k - TIP_EJECT);
        if (app.ui.side_mode != SIDE_PLACES || i >= app.places.n) return false;
        *id = CLAY_IDI("side-eject", k - TIP_EJECT);
        return snprintf(buf, cap, "Eject %s", app.places.p[i].label) > 0;
    }
    if (k >= TIP_MODE) {
        *id = CLAY_IDI("mode", k - TIP_MODE);
        return ui_with_key(buf, cap, MODES[k - TIP_MODE], UK_F9)[0];
    }
    for (int j = 0; j < COUNT(BAR); j++)
        if (TIP_BAR + (int)BAR[j].b == k) return *id = bar_id(BAR[j].b), ui_with_key(buf, cap, BAR[j].text, BAR[j].cmd)[0];
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
static char        badge_icon[128];             // the badge's icon as drawn last, by its slot's key
static float       spring_fill = -1;           // the spring's bar, 0 to 1; -1 none drawn
static float       badge_x, badge_y;           // where the badge was put
static const char *badge_op;

// A drag of files (F13 step 3): the place it would drop in, outlined in the accent (nowhere it
// can't), and by the pointer a badge: the entry pressed, its icon and name, "+ N more", and what
// the drop does there ("Move", "Copy", "Trash").
// The badge's place, w x h: right of the pointer and below it; left of it, or above it, where the
// window's edge would cut it.
static void badge_place(float w, float h)
{
    float bx = app.drag_px + 14, by = app.drag_py + 16;
    if (bx + w > (float)GetScreenWidth()) bx = app.drag_px - 14 - w;
    if (by + h > (float)GetScreenHeight()) by = app.drag_py - 16 - h;
    badge_x = bx < 0 ? 0 : bx, badge_y = by < 0 ? 0 : by;
}

static void layout_drag(void)
{
    drop_marked = badge_drawn = false, badge_op = NULL, spring_fill = -1, badge_icon[0] = 0;
    Theme *t = &app.theme;
    if (app.in_nofiles && app.drag == DRAG_NONE) {   // (F14: a refused drag says why, by the pointer)
        static const char NOFILES[] = "Only files can be dropped here";
        badge_drawn = true;
        badge_place(str_width(str_from_c(NOFILES)) + 18, 30);   // (its size now: it is drawn once, the pointer still)
        CLAY(CLAY_ID("drag-badge"), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) }, .padding = { 8, 8, 5, 5 } },
                                      .backgroundColor = cc(t->bg_light), .cornerRadius = CLAY_CORNER_RADIUS(3),
                                      .border = { .color = rule_color(), .width = CLAY_BORDER_OUTSIDE(1) },
                                      .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 30, .offset = { badge_x, badge_y },
                                                    .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
            text(cz(NOFILES), cc(t->fg_dim));
        }
        return;
    }
    if (!files_over()) return;
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
    Clay_BoundingBox bb = Clay_GetElementData(CLAY_ID("drag-badge")).boundingBox;   // (its size the last frame's)
    badge_place(bb.width, bb.height);
    CLAY(CLAY_ID("drag-badge"), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) }, .padding = { 8, 8, 5, 5 }, .childGap = 8,
                                              .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                  .backgroundColor = cc(t->bg_light), .cornerRadius = CLAY_CORNER_RADIUS(3),
                                  .border = { .color = cc(t->accent), .width = CLAY_BORDER_OUTSIDE(1) },
                                  .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 30, .offset = { badge_x, badge_y },
                                                .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
        IconSlot *icon = drag_icon_slot();
        if (icon) snprintf(badge_icon, sizeof badge_icon, "%.*s", STR_ARG(icon->key));
        icon_box(CLAY_ID("drag-icon"), icon, SIDE_ICON_PX);
        text(fit(str_from_c(app.drag_name), 240, false), cc(t->fg));
        if (dragged.n > 1) text(cz(arena_sprintf(&frame, "+ %td more", dragged.n - 1)), cc(t->fg_dim));
        if (app.spring_tab >= 0) {   // (F13 step 4: the tab under it opens when the bar is full)
            float f = (float)(now_ms() - app.spring_at) / UI_SPRING_MS;
            spring_fill = f < 0 ? 0 : f > 1 ? 1 : f;
            CLAY(CLAY_ID("spring-bar"), { .layout = { .sizing = { CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(4) } }, .backgroundColor = rule_color() }) {
                CLAY(CLAY_ID("spring-fill"), { .layout = { .sizing = { CLAY_SIZING_FIXED(60 * spring_fill), CLAY_SIZING_GROW(0) } }, .backgroundColor = cc(t->accent) }) {}
            }
            text(cz("opening the tab\xE2\x80\xA6"), cc(t->fg_dim));
        }
        if (badge_op)
            CLAY(CLAY_ID("drag-op"), { .layout = { .padding = { 6, 6, 1, 1 } }, .backgroundColor = cc(t->accent), .cornerRadius = CLAY_CORNER_RADIUS(2) }) {
                text(cz(badge_op), cc(t->bg));
            }
    }
}

// The window, and the modal panel on top when one is up.
// The footer's hint for what has the keys now (a panel, a question, a menu, a field), NULL when
// nothing does (the status line or the folder's line then). One draw for all of them (the F15
// code-health review: 17 branches, each its own text() call, three without fit).
static const char *footer_hint(void)
{
    UiOnTop on = modal_kind();
    switch (on) {
    case UI_ON_PROPS: return "Properties: \xE2\x86\x90 \xE2\x86\x91 \xE2\x86\x93 \xE2\x86\x92 (h j k l) move \xC2\xB7 Space or Enter changes \xC2\xB7 Esc closes";
    case UI_ON_BAR: return "Menu bar hidden: Enter or Esc closes this";
    case UI_ON_ABOUT: return "About: Esc or Enter closes";
    case UI_ON_KEYS: {
        const char *k = ui_cmd_key(UK_SHORTCUTS, 0);   // (as bound, F15)
        return arena_sprintf(&frame, "Keyboard Shortcuts: type to filter \xC2\xB7 \xE2\x86\x91 \xE2\x86\x93 PgUp PgDn scroll \xC2\xB7 Esc%s%s closes", k[0] ? " or " : "", k);
    }
    case UI_ON_BM:
        return app.ui.bmp_naming ? "Rename: Enter renames (empty: the folder's name) \xC2\xB7 Esc keeps the name"
                                 : "Bookmarks: \xE2\x86\x91 \xE2\x86\x93 (k j) move \xC2\xB7 Enter goes there \xC2\xB7 Delete removes \xC2\xB7 F2 renames \xC2\xB7 "
                                   "Alt+\xE2\x86\x91 Alt+\xE2\x86\x93 move it \xC2\xB7 Esc closes";
    case UI_ON_BRANCHES: return "Branches: type to filter \xC2\xB7 Tab or \xE2\x86\x93 the list \xC2\xB7 Enter switches \xC2\xB7 Esc closes";
    case UI_ON_OW:
        return "Open With: type to filter \xC2\xB7 Tab or \xE2\x86\x93 the list \xC2\xB7 Enter opens \xC2\xB7 Esc closes \xC2\xB7 "
               "Alt+C/W/R case, words, regex \xC2\xB7 Alt+D default";
    case UI_ON_NONE: break;
    default: return "Answer the question above";
    }
    if (app.ui.menu_on && app.ui.menu >= UI_MENUS) return "Menu: \xE2\x86\x91 \xE2\x86\x93 (k j) move \xC2\xB7 Enter picks \xC2\xB7 Esc closes";
    if (app.ui.menu_on) return "Menu: \xE2\x86\x91 \xE2\x86\x93 (k j) move \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 the next menu \xC2\xB7 Enter picks \xC2\xB7 Esc closes";
    switch (app.ui.focus) {
    case UI_FOCUS_CRUMBS: return "Path: \xE2\x86\x90 \xE2\x86\x92 (h l) move \xC2\xB7 Enter goes there \xC2\xB7 Shift+Enter in a new tab \xC2\xB7 Esc back to the list";
    case UI_FOCUS_EDIT:
        return app.ui.edit_path             ? "Go to: Enter goes there \xC2\xB7 Esc cancels \xC2\xB7 ~ is home, a relative path starts here"
               : app.edit_new == UNDO_MKDIR  ? "New folder: Enter keeps the name \xC2\xB7 Esc keeps \"New folder\""
               : app.edit_new == UNDO_MKFILE ? "New file: Enter keeps the name \xC2\xB7 Esc keeps \"New file\""
                                             : "Rename: Enter renames \xC2\xB7 Esc cancels";
    case UI_FOCUS_JOBS: return "Jobs: \xE2\x86\x91 \xE2\x86\x93 (k j) move \xC2\xB7 Enter cancels the marked job \xC2\xB7 Esc back to the list";
    case UI_FOCUS_FIND:
        if (!app.ui.finding) return NULL;
        if (app.ui.find_select)
            return "Select: Enter selects every match \xC2\xB7 Esc cancels \xC2\xB7 \xE2\x86\x91 \xE2\x86\x93 previous and next \xC2\xB7 Alt+C/W/R case, words, regex";
        const char *k = ui_cmd_key(UK_JOBS, 0);   // (as bound, F15)
        return arena_sprintf(&frame, "Find: \xE2\x86\x91 \xE2\x86\x93 previous and next \xC2\xB7 Enter keeps \xC2\xB7 Esc closes \xC2\xB7 %s%sAlt+C/W/R case, words, regex",
                             k, k[0] ? " the jobs \xC2\xB7 " : "");
    default: return NULL;
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
            marked_name_drawn = NO_COLOR;
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
            const char *hint = footer_hint();
            if (hint) {
                text(fit(str_from_c(hint), foot_w, false), cc(t->fg_dim));
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
