// The files and the sidebar drawn: columns, rows, the icon view's cells, the edit line in a row,
// eza's colours. Included by main.c.

// ---- layout ---------------------------------------------------------------------------------

static void text(Clay_String s, Clay_Color color)
{
    CLAY_TEXT(s, { .textColor = color, .fontSize = FONT_PX, .wrapMode = CLAY_TEXT_WRAP_NONE });
}

static float advance(u32 cp);

static float str_width(Str s)
{
    return ui_width(s, advance);
}

static float advance(u32 cp)
{
    return font_advance(cp, FONT_PX);
}

// s cut to `width` (ui_fit), with "…" where it was cut; into the frame arena.
static Clay_String fit(Str s, float width, bool keep_end)
{
    Str dots = S("\xE2\x80\xA6");
    isize from, to;
    if (ui_fit(s, width, str_width(dots), advance, keep_end, &from, &to)) return cs(s);
    if (keep_end) return cs(str_from_c(arena_sprintf(&frame, "%.*s%.*s", STR_ARG(dots), (int)(to - from), s.p + from)));
    return cs(str_from_c(arena_sprintf(&frame, "%.*s%.*s", (int)(to - from), s.p + from, STR_ARG(dots))));
}

// The job rows' strip under the files: a row per running job, a line for those waiting.
static int jobs_h(void)
{
    return jobs.nrun * JOB_ROW_H + (jobs.nwait ? JOB_WAIT_H : 0);
}

// The list's rows that fit, or the icon view's rows of cells (no column headers there).
static int list_rows(int h)
{
    int room = h - top_h() - FOOTER_H - jobs_h() - view_bar_h() -
               (app.ui.finding ? FIND_BAR_H : 0);
    int r = app.ui.grid ? (room - PAD) / cell_h() : (room - COLS_H) / ROW_H;
    return r > 0 ? r : 0;
}

// The icon view's columns at window width w (the list: 1).
static int list_cols(int w)
{
    if (!app.ui.grid) return 1;
    int c = (w - side_w() - 2 * PAD) / cell_w();
    return c > 1 ? c : 1;
}

// The list's rows, or the icon view's cells, there are places for on screen.
static int list_cells(void)
{
    return app.ui.list.rows * (int)ui_cols(&app.ui.list);
}

// The list's rows and columns for the window as it is now.
static int  ow_rows(int h);   // Open With (gui/openwith.c, F7.3)
static int  brp_rows(int h);   // the branches panel (gui/branches.c, F12 step 6)
static int  bmp_rows(int h);  // Edit Bookmarks (gui/bookmarks.c, F7.5)
static int  keys_rows(int h); // Keyboard Shortcuts (gui/shortcuts.c, F7.5)

static void list_fit(void)
{
    ui_set_cols(&app.ui.list, list_cols(GetScreenWidth()));
    ui_resize(&app.ui.list, list_rows(GetScreenHeight()));
    ui_resize(&app.ui.ow.list, ow_rows(GetScreenHeight()));   // Open With's list (F7.3)
    ui_resize(&app.ui.brp.list, brp_rows(GetScreenHeight()));   // the branches panel's (F12 step 6)
    ui_resize(&app.ui.bmp_list, bmp_rows(GetScreenHeight()));   // Edit Bookmarks' (F7.5)
    ui_resize(&app.ui.keys_list, keys_rows(GetScreenHeight()));   // Keyboard Shortcuts'
}

static int side_rows(int h)
{
    int r = (h - bar_h() - HEADER_H - FOOTER_H - PAD) / SIDE_ROW_H;   // the tab bar isn't over the sidebar
    return r > 0 ? r : 0;
}

static void icon_box(Clay_ElementId id, IconSlot *icon, int px)
{
    CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(px), CLAY_SIZING_FIXED(px) } },
               .image = { .imageData = icon && icon->tex.id ? &icon->tex : NULL } }) {}
}

// a mixed into b: t of a.
static Clay_Color mix(Rgb a, Rgb b, float t)
{
    return (Clay_Color){ a.r * t + b.r * (1 - t), a.g * t + b.g * (1 - t), a.b * t + b.b * (1 - t), 255 };
}

// The theme's colour for a rule or an idle field's frame, and no colour at all.
static Clay_Color rule_color(void)
{
    return mix(app.theme.fg_dim, app.theme.bg_light, 0.45f);
}

static const Clay_Color NO_COLOR = { 0, 0, 0, 0 };

// How full a drive is, 0..1.
static float drive_used(const Place *p)
{
    float used = p->size > 0 ? 1.0f - (float)p->avail / (float)p->size : 0;
    return used < 0 ? 0 : used > 1 ? 1 : used;
}

// The bar's colours, which its numbers take too (F6.1): the used part in the accent (the warning
// from 90% full on), the free part dimmed; theme_fit picks them from the theme (2026-11-07), the
// track a shade of its own (drive_bar).
static Clay_Color drive_used_color(const Place *p)
{
    return drive_used(p) >= 0.9f ? cc(app.theme.drive_warn) : cc(app.theme.drive_used);
}

static Clay_Color drive_free_color(void)
{
    return cc(app.theme.drive_free);
}

// A drive's fullness: a thin bar under its name.
static void drive_bar(int r, const Place *p, float w)
{
    CLAY(CLAY_IDI("side-bar", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(3) } },
                                    .backgroundColor = cc(app.theme.drive_track) }) {
        CLAY(CLAY_IDI("side-used", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(w * drive_used(p)), CLAY_SIZING_GROW(0) } },
                                         .backgroundColor = drive_used_color(p) }) {}
    }
}

// Whether sidebar row i has a folder menu open on it (F8): lit as the cursor is, so the menu's
// folder shows.
static bool side_ctx(isize i)
{
    return app.ui.menu_on && app.ui.menu == UI_CTX_FOLDER && app.ui.ctx_crumb < 0 && app.ui.ctx_side == i;
}

// The sidebar's handle (F13): its right edge, over the rows, in the accent while the pointer is on it
// or drags it.
static bool edge_lit;   // as drawn last (a script's state line)

static void layout_side_edge(float h)
{
    bool lit = edge_lit = app.hov.side_edge || app.drag == DRAG_SIDE;
    CLAY(CLAY_ID("side-edge"), { .layout = { .sizing = { CLAY_SIZING_FIXED(SIDE_EDGE), CLAY_SIZING_FIXED(h) } },
                                 .backgroundColor = lit ? cc(app.theme.accent) : NO_COLOR,
                                 .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 5,
                                               .attachPoints = { CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP } } }) {}
}

// The tree mode (F6): a row a folder, indented by its depth, with its open (▾) or closed (▸)
// mark; the folder shown in the accent.
static void layout_tree(float h)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("side"), { .layout = { .sizing = { CLAY_SIZING_FIXED(side_w()), CLAY_SIZING_FIXED(h) },
                                        .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = { 0, 0, PAD / 2, 0 } },
                            .clip = { .horizontal = true }, .backgroundColor = cc(t->bg_light) }) {
        layout_side_edge(h);
        UiList *sl = &app.ui.side;
        Str cwd = str_from_c(app.cwd);
        for (int r = 0; r < sl->rows && sl->top + r < sl->n; r++) {
            int i = (int)(sl->top + r);
            const TreeRow *row = &tree.row[i];
            bool cursor = (app.ui.focus == UI_FOCUS_SIDE && i == sl->sel) || side_ctx(i), here = str_eq(row->path, cwd), hover = r == app.hov.side;
            Clay_Color bg = cursor ? cc(t->selection) : hover ? cc(t->bg) : cc(t->bg_light);
            int indent = row->depth * TREE_STEP;   // deep names are cut short with "…"
            CLAY(CLAY_IDI("side-row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(SIDE_ROW_H) },
                                                        .padding = { (uint16_t)(PAD / 2 + indent), PAD, 0, 0 }, .childGap = 4,
                                                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                            .backgroundColor = bg,
                                            .border = { .color = cc(t->accent), .width = { .left = cursor ? 3 : 0 } } }) {
                CLAY(CLAY_IDI("side-twisty", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(SIDE_ROW_H) },
                                                               .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
                    text(cz(row->open ? "\xE2\x96\xBE" : "\xE2\x96\xB8"), cc(t->fg_dim));   // ▾ ▸
                }
                Str names[2];
                isize n = 0;
                char path[PATH_MAX];
                snprintf(path, sizeof path, "%.*s", STR_ARG(row->path));
                Str special = row->depth == 0 ? S("drive-harddisk") : user_dir_icon(&app.user_dirs, path);
                if (special.len) names[n++] = special;
                names[n++] = S("folder");
                icon_box(CLAY_IDI("side-icon", r), icon_for(names[0], names, n), SIDE_ICON_PX);
                float room = side_w() - PAD / 2 - indent - 16 - 4 - SIDE_ICON_PX - 4 - PAD;
                if (room > 0) text(fit(row->name, room, false), here ? cc(t->accent) : cc(t->fg));
            }
        }
    }
}

// The places mode (F6): the two headings, the places under each; a drive with its free space on
// the right and its fullness under its name.
static void layout_side(float h)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("side"), { .layout = { .sizing = { CLAY_SIZING_FIXED(side_w()), CLAY_SIZING_FIXED(h) },
                                        .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = { 0, 0, PAD / 2, 0 } },
                            .backgroundColor = cc(t->bg_light) }) {
        layout_side_edge(h);
        UiList *sl = &app.ui.side;
        app.hover_num[0] = 0;
        for (int r = 0; r < sl->rows && sl->top + r < sl->n; r++) {
            int i = (int)(sl->top + r);
            const Place *p = &app.places.p[i];
            if (p->kind == PL_HEAD) {
                CLAY(CLAY_IDI("side-row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(SIDE_ROW_H) },
                                                            .padding = { PAD, PAD, 6, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM } } }) {
                    text(cz(p->label), cc(t->fg_dim));
                }
                continue;
            }
            bool cursor = (app.ui.focus == UI_FOCUS_SIDE && i == sl->sel) || side_ctx(i), here = i == app.place_cur, hover = r == app.hov.side;
            bool drive = place_is_drive(p->kind) && p->size > 0;
            Clay_Color bg = cursor ? cc(t->selection) : hover ? cc(t->bg) : cc(t->bg_light);
            // The free space; with the pointer over it, the used space (F6.1), each in its bar colour.
            char avail[32] = "";
            if (drive) ui_size(avail, sizeof avail, hover ? p->size - p->avail : p->avail);
            if (hover) snprintf(app.hover_num, sizeof app.hover_num, "%s", avail);
            Str free_s = str_from_c(arena_sprintf(&frame, "%s", avail));
            float room = side_w() - 2 * PAD - SIDE_ICON_PX - 8 - (drive ? str_width(free_s) + 8 : 0);
            CLAY(CLAY_IDI("side-row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(SIDE_ROW_H) },
                                                        .padding = { PAD, PAD, 0, 0 }, .childGap = 8,
                                                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                            .backgroundColor = bg,
                                            .border = { .color = cc(t->accent), .width = { .left = cursor ? 3 : 0 } } }) {
                if (hover && place_ejectable(p)) {   // in the icon's place while the pointer is over it
                    bool on = app.hov.eject == r;
                    CLAY(CLAY_IDI("side-eject", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(SIDE_ICON_PX), CLAY_SIZING_FIXED(SIDE_ICON_PX) },
                                                                  .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                                                      .backgroundColor = on ? cc(t->bg_light) : NO_COLOR,
                                                      .cornerRadius = CLAY_CORNER_RADIUS(3),
                                                      .border = { .color = on ? cc(t->accent) : mix(t->fg_dim, t->bg, 0.5f), .width = CLAY_BORDER_OUTSIDE(1) } }) {
                        text(cz("\xE2\x8F\x8F"), on ? cc(t->accent) : cc(t->fg));   // ⏏
                    }
                } else {
                    icon_box(CLAY_IDI("side-icon", r), place_icon(i), SIDE_ICON_PX);
                }
                CLAY(CLAY_IDI("side-text", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                                             .childGap = 3 } }) {
                    CLAY(CLAY_IDI("side-line", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 8 } }) {
                        // (a drive not mounted yet dim, 2026-10-30)
                        text(fit(str_from_c(p->label), room, false), here ? cc(t->accent) : p->kind == PL_UNMOUNTED ? cc(t->fg_dim) : cc(t->fg));
                        if (drive) {
                            CLAY(CLAY_IDI("side-gap", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } } }) {}
                            text(cs(free_s), hover ? drive_used_color(p) : drive_free_color());
                        }
                    }
                    if (drive) drive_bar(r, p, side_w() - 2 * PAD - SIDE_ICON_PX - 8);
                }
            }
        }
    }
}

// Each top bar and footer button's element id, by UiBar: the code that draws one, its tooltip and
// the hit test read it here (2026-10-28: each spelled them out, and a typo in one gave a button that
// draws but can't be clicked).
static const char *const BAR_IDS[] = {
    [UB_BACK] = "nav-back",          [UB_FORWARD] = "nav-fwd",       [UB_UP] = "nav-up",         [UB_LOCATION] = "loc-toggle",
    [UB_SEARCH] = "search",          [UB_LEFT] = "crumb-left",       [UB_RIGHT] = "crumb-right", [UB_RESTORE] = "trash-restore",
    [UB_EMPTY] = "trash-empty",      [UB_VIEW_LIST] = "view-list",   [UB_VIEW_ICONS] = "view-icons",
    [UB_ZOOM_OUT] = "zoom-out",      [UB_ZOOM_IN] = "zoom-in",       [UB_BRANCH] = "git-branch",
};

static Clay_ElementId bar_id(UiBar b)
{
    return Clay_GetElementId(cz(BAR_IDS[b]));
}

static void trash_button(UiBar b, const char *label, bool danger)
{
    Theme *t = &app.theme;
    bool hover = app.hov.bar == b;
    CLAY(bar_id(b), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(26) }, .padding = { 12, 12, 0, 0 },
                                                  .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                      .backgroundColor = hover ? cc(t->bg) : cc(t->bg_light),
                                      .border = { .color = danger ? cc(t->red) : rule_color(), .width = CLAY_BORDER_OUTSIDE(1) } }) {
        text(cz(label), danger && hover ? cc(t->red) : cc(t->fg));
    }
}

// The trash view's bar over the columns (F6): what it is, Restore and Empty Trash.
static void layout_trash_bar(void)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("trash-bar"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(TRASH_BAR_H) }, .padding = { PAD, PAD, 0, 0 },
                                             .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                 .backgroundColor = cc(t->bg_light) }) {
        text(cz("Trash"), cc(t->fg));
        text(cz("Restore puts the marked items back where they came from"), cc(t->fg_dim));
        CLAY(CLAY_ID("trash-gap"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } } }) {}
        if (app.list.n > 0) trash_button(UB_RESTORE, "Restore", false);
        if (app.list.n > 0 || app.list.hidden > 0) trash_button(UB_EMPTY, "Empty Trash", true);
    }
}

// The width Owner, Group (View → Show Group) and Permissions take in a row, their gaps included.
static float perms_w(void)
{
    return app.ui.group_col ? 3 * GAP + OWNER_W + GROUP_W + PERM_W : 2 * GAP + OWNER_W + PERM_W;
}

enum { NAME_MIN_W = 150 };   // Name's least width beside Owner and Permissions (200 until Michael found it hid them beside a second window)

// Entry e's date as the list shows it, in ofx.conf's date-format (F10; "" without one). The list
// and the scripts' state line both take it from here.
static void entry_when(const Entry *e, i64 now, char *buf, size_t cap)
{
    buf[0] = 0;
    if (e->mtime) ui_when(buf, cap, e->mtime, now, app.conf.date, app.conf.date_pattern);
}

// The date column's width: DATE_W for the short dates; for ofx.conf's longer ones (F10), a wide
// date in that format measured, so "2026-10-20T14:03:05" doesn't run into the Owner column
// (2026-10-30, a shot of f10 showed it did).
static float date_w(void)
{
    if (app.conf.date == DATE_SHORT) return DATE_W;
    static DateFormat f = DATE_SHORT;   // (measured once a format: each row asks every frame)
    static char pattern[CONF_DATE_MAX];
    static float cached;
    if (f == app.conf.date && strcmp(pattern, app.conf.date_pattern) == 0) return cached;
    f = app.conf.date, snprintf(pattern, sizeof pattern, "%s", app.conf.date_pattern);
    // Wide digits (8s) on a Saturday in August, and Wednesday in September: the C locale's widest
    // day and month names, for a pattern with %A or %B (review, 2026-10-30).
    static const struct tm WIDE[] = { { .tm_year = 2088 - 1900, .tm_mon = 7, .tm_mday = 28, .tm_hour = 20, .tm_min = 48, .tm_sec = 48, .tm_isdst = -1 },
                                      { .tm_year = 2088 - 1900, .tm_mon = 8, .tm_mday = 29, .tm_hour = 20, .tm_min = 48, .tm_sec = 48, .tm_isdst = -1 } };
    float w = DATE_W;
    for (int k = 0; k < COUNT(WIDE); k++) {
        char buf[96];
        struct tm t = WIDE[k];
        i64 at = (i64)mktime(&t);
        ui_when(buf, sizeof buf, at, at, app.conf.date, app.conf.date_pattern);
        float bw = str_width(str_from_c(buf)) + 4;
        if (bw > w) w = bw;
    }
    return cached = w;
}

// Whether the list shows Owner and Permissions (and Group): View → Show Permissions on, and Name
// keeps NAME_MIN_W beside them in a window w wide (Michael, 2026-10-27: they give way on a narrow
// one, and the status line says so).
static bool perms_shown(float w)
{
    float rest = w - side_w() - 2 * PAD - ICON_PX - 3 * GAP - SIZE_W - date_w();
    return app.ui.perms_cols && rest - perms_w() >= NAME_MIN_W;
}

// A user's or a group's name, cached (the list's Owner and Group, 2026-10-27): a getpwuid per row
// and frame would be a passwd read each; the number when it has no name.
static const char *id_name(bool group, u32 id)
{
    enum { IDS_MAX = 32 };
    static struct { bool used, group; u32 id; char name[64]; } cache[IDS_MAX];
    static int next;
    for (int k = 0; k < IDS_MAX; k++)
        if (cache[k].used && cache[k].group == group && cache[k].id == id) return cache[k].name;
    int k = next++ % IDS_MAX;
    cache[k].used = true, cache[k].group = group, cache[k].id = id;
    char buf[4096];
    struct passwd pw, *pwp = NULL;
    struct group gr, *grp = NULL;
    if (!group && getpwuid_r(id, &pw, buf, sizeof buf, &pwp) == 0 && pwp) snprintf(cache[k].name, sizeof cache[k].name, "%s", pw.pw_name);
    else if (group && getgrgid_r(id, &gr, buf, sizeof buf, &grp) == 0 && grp) snprintf(cache[k].name, sizeof cache[k].name, "%s", gr.gr_name);
    else snprintf(cache[k].name, sizeof cache[k].name, "%u", (unsigned)id);
    return cache[k].name;
}

// The column headers' element ids, by SortKey: drawn and hit-tested by the same.
static const char *const COL_IDS[SORT_KEYS] = { "col-name", "col-size", "col-date" };

// The column headers: Name, Size, Modified, the sorted one with an arrow; a click sorts. Then Owner,
// Group (when shown) and Permissions, which don't sort (Michael, 2026-10-27).

static void layout_cols(void)
{
    Theme *t = &app.theme;
    const char *const NAMES[SORT_KEYS] = { "Name", "Size", VIEWS[app.ui.view].date_col };
    CLAY(CLAY_ID("cols"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(COLS_H) },
                                        .padding = { PAD + ICON_PX + GAP, PAD, 0, 0 }, .childGap = GAP,
                                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                            .border = { .color = cc(t->bg_light), .width = { .bottom = 1 } } }) {
        for (int k = 0; k < SORT_KEYS; k++) {
            bool on = app.ui.sort.key == (SortKey)k;
            Clay_String label = cz(arena_sprintf(&frame, "%s%s", NAMES[k], !on ? "" : app.ui.sort.reverse ? " \xE2\x96\xBC" : " \xE2\x96\xB2"));
            Clay_Sizing size = k == SORT_NAME ? (Clay_Sizing){ CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }
                             : (Clay_Sizing){ CLAY_SIZING_FIXED(k == SORT_SIZE ? SIZE_W : date_w()), CLAY_SIZING_GROW(0) };
            CLAY(Clay_GetElementId(cz(COL_IDS[k])), { .layout = { .sizing = size, .childAlignment = { .x = k == SORT_NAME ? CLAY_ALIGN_X_LEFT : CLAY_ALIGN_X_RIGHT,
                                                                                                    .y = CLAY_ALIGN_Y_CENTER } } }) {
                text(label, on ? cc(t->fg) : cc(t->fg_dim));
            }
            if (k == SORT_NAME && git_shown()) CLAY(CLAY_ID("col-git"), { .layout = { .sizing = { CLAY_SIZING_FIXED(GIT_W), CLAY_SIZING_GROW(0) } } }) {}   // (over the letters)
        }
        static const struct { const char *id, *name; int w; } MORE[] = { { "col-owner", "Owner", OWNER_W }, { "col-group", "Group", GROUP_W },
                                                                         { "col-perms", "Permissions", PERM_W } };
        for (int k = 0; perms_shown((float)GetScreenWidth()) && k < COUNT(MORE); k++) {
            if (k == 1 && !app.ui.group_col) continue;
            CLAY(Clay_GetElementId(cz(MORE[k].id)), { .layout = { .sizing = { CLAY_SIZING_FIXED(MORE[k].w), CLAY_SIZING_GROW(0) },
                                                                  .padding = { k == 0 ? OWNER_PAD : 0, 0, 0, 0 },   // (clear of Modified, right-aligned)
                                                                  .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                text(cz(MORE[k].name), cc(t->fg_dim));
            }
        }
    }
}

// A run of the edit line's text: at most `most` wide (cut with "…" from the end, or from its
// start with keep_end), on the accent half mixed in when selected (the row under a rename has the
// selection colour already).
static void edit_run(Clay_ElementId sel_id, Str s, bool sel, float most, bool keep_end, Clay_Color fg)
{
    Theme *t = &app.theme;
    if (!s.len || most < 1) return;
    Clay_String txt = fit(s, most, keep_end);
    if (!sel) { text(txt, fg); return; }
    CLAY(sel_id, { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(FONT_PX + 6) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                   .backgroundColor = mix(t->accent, t->bg, 0.45f) }) {
        text(txt, fg);
    }
}

// The edit line's text in `room`: the selection marked (F5.2) and the caret at its place, kept in
// view (the text after it gets half the room at most, the ends cut with "…").
static void edit_text(const char *id, float room, UiLine line, Clay_Color fg)
{
    Theme *t = &app.theme;
    Str ed = { line.b, *line.len };
    int lo, hi, c = *line.caret;
    ui_line_sel(line, &lo, &hi);
    // Left of the caret the text before the selection and, when the caret is its right end, the
    // selection; right of it the rest.
    Str l1 = str_cut(ed, 0, c == hi ? lo : c), l2 = c == hi ? str_cut(ed, lo, hi) : STR_EMPTY;
    Str r1 = c == lo ? str_cut(ed, lo, hi) : STR_EMPTY, r2 = str_cut(ed, c == lo ? hi : c, ed.len);
    float rw = str_width(r1) + str_width(r2), lw = str_width(l1) + str_width(l2);
    float left = room - (rw < room / 2 ? rw : room / 2);
    float w2 = str_width(l2) < left ? str_width(l2) : left, w1 = left - w2;
    float right = room - (lw < left ? lw : left);
    float v1 = str_width(r1) < right ? str_width(r1) : right, v2 = right - v1;
    Clay_ElementId sel = Clay_GetElementId(cz(arena_sprintf(&frame, "%s-sel", id)));
    edit_run(sel, l1, false, w1, true, fg);
    edit_run(sel, l2, true, w2, true, fg);
    CLAY(Clay_GetElementId(cz(arena_sprintf(&frame, "%s-caret", id))), { .layout = { .sizing = { CLAY_SIZING_FIXED(2), CLAY_SIZING_FIXED(FONT_PX + 2) } },
                                                                          .backgroundColor = cc(t->accent) }) {}
    edit_run(sel, r1, true, v1, false, fg);
    edit_run(sel, r2, false, v2, false, fg);
}

// The edit line in place of the name.
static void layout_edit(Theme *t, float room)
{
    CLAY(CLAY_ID("edit"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ROW_H - 6) }, .padding = { 4, 4, 0, 0 },
                                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                            .clip = { .horizontal = true },
                            .backgroundColor = cc(t->bg), .border = { .color = cc(t->accent), .width = CLAY_BORDER_ALL(1) } }) {
        edit_text("edit", room - 12, ui_edit_line(&app.ui), cc(t->fg));
    }
}

// Entry i's Size cell: a file's size, a folder's item count ("" until known). ask: ask counts.c for
// a count not asked yet (the row is being drawn).
static void size_cell(isize i, char *size, size_t cap, bool ask)
{
    const Entry *e = &app.list.e[i];
    size[0] = 0;
    if (!kind_is_dir(e->kind) && e->kind != K_LINK_BROKEN && e->kind != K_OTHER) ui_size(size, cap, e->size);
    if (!kind_is_dir(e->kind)) return;
    char path[PATH_MAX];
    if (ask && app.count[i] == COUNT_UNKNOWN && entry_path(i, path, sizeof path) && count_ask(i, path, app.show_hidden))
        app.count[i] = COUNT_ASKED;
    if (app.count[i] >= 0) snprintf(size, cap, "%td item%s", app.count[i], app.count[i] == 1 ? "" : "s");
    else if (app.count[i] == -1) snprintf(size, cap, "?");
}

// ---- ls's colours, as eza gives them (a trial, Michael 2026-10-27: his `ls` is eza, with no theme
// of its own, so its built-in colours on the terminal's palette, which Omarchy sets from colors.toml)

// A name's colour by its kind, as eza's: folders blue, links cyan, executables green, build files
// (Makefile, README…) yellow; images and videos magenta, music cyan, archives red; the rest plain.
static Clay_Color ls_name_colour(const Entry *e, isize i)
{
    Theme *t = &app.theme;
    if (e->kind == K_LINK_BROKEN) return cc(t->red);
    if (!app.ui.colors) return cc(t->fg);   // (View → Show Colors off)
    if (kind_is_dir(e->kind)) return e->kind == K_LINK_DIR ? cc(t->cyan) : cc(t->blue);
    if (e->kind == K_LINK_FILE) return cc(t->cyan);
    static const char *const BUILD[] = { "Makefile", "makefile", "GNUmakefile", "CMakeLists.txt", "meson.build", "build.gradle", "Cargo.toml",
                                         "package.json", "Dockerfile", "Justfile", "justfile", "Rakefile", "BUILD", "configure" };
    for (int k = 0; k < COUNT(BUILD); k++)
        if (str_eq(e->name, str_from_c(BUILD[k]))) return cc(t->yellow);
    if (str_starts_with(e->name, S("README")) || str_starts_with(e->name, S("readme"))) return cc(t->yellow);
    if (e->mode & 0111) return cc(t->green);
    Str type = entry_mime(i);
    if (str_starts_with(type, S("image/")) || str_starts_with(type, S("video/"))) return cc(t->magenta);
    if (str_starts_with(type, S("audio/"))) return cc(t->cyan);
    static const char *const PACKED[] = { "application/zip", "application/x-tar", "application/gzip", "application/x-xz", "application/x-bzip2",
                                          "application/zstd", "application/x-7z-compressed", "application/vnd.rar", "application/x-compressed-tar" };
    for (int k = 0; k < COUNT(PACKED); k++)
        if (str_eq(type, str_from_c(PACKED[k]))) return cc(t->red);
    return cc(t->fg);
}

// ---- git status (F12 step 5, gui/git.c) -----------------------------------------------------------

// A git state's colour, VS Code's on the theme's palette: modified yellow, added and untracked
// green, renamed cyan, a conflict red; ignored dimmed.
static Clay_Color git_colour(GitState s)
{
    Theme *t = &app.theme;
    switch (s) {
    case GS_MODIFIED: return cc(t->yellow);
    case GS_STAGED: case GS_UNTRACKED: return cc(t->green);
    case GS_RENAMED: return cc(t->cyan);
    case GS_CONFLICT: return cc(t->red);
    default: return cc(t->fg_dim);
    }
}

// The git column's cell for entry i (state s): its letter, or for a folder a dot (VS Code's).
static void git_cell(int r, isize i, GitState s)
{
    bool dir = app.list.e[i].kind == K_DIR;
    char c = git_letter(s);
    CLAY(CLAY_IDI("git", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(GIT_W), CLAY_SIZING_FIT(0) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
        if (c) CLAY(CLAY_IDI("git-mark", r), {}) { text(cz(dir ? "\xE2\x97\x8F" : arena_sprintf(&frame, "%c", c)), git_colour(s)); }   // ● for a folder
    }
}

// A Permissions cell in eza's colours: r yellow, w red, x green, s/S/t/T magenta, - dim.
static void ls_perms_cell(Clay_ElementId id, u32 mode, int w)
{
    Theme *t = &app.theme;
    char p[16];
    ui_perms(p, sizeof p, mode);
    CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIT(0) } }, .clip = { .horizontal = true } }) {
        for (int k = 0; p[k]; k++) {
            char c = p[k];
            Clay_Color col = !app.ui.colors ? cc(t->fg_dim) : c == 'r' ? cc(t->yellow) : c == 'w' ? cc(t->red) : c == 'x' ? cc(t->green) : c == '-' ? mix(t->fg_dim, t->fg, 0.25f) : cc(t->magenta);
            text(cz(arena_sprintf(&frame, "%c", c)), col);
        }
    }
}

// One of a row's Owner, Group, Permissions cells, pad on its left: s (copied into the frame arena: Clay reads the
// text after layout_row has returned) cut to w.
static void info_cell(Clay_ElementId id, const char *s, int w, int pad, Clay_Color col)
{
    CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIT(0) }, .padding = { pad, 0, 0, 0 } }, .clip = { .horizontal = true } }) {
        text(fit(str_from_c(arena_sprintf(&frame, "%s", s)), (float)(w - pad), false), col);
    }
}

// Where a file searched inside matched (F9 step 3), in room px after its folder: the line's number,
// the line around the match with the match in the accent colour, "+N" for the other lines that
// match. The match is shown whole when it fits.
static void layout_hit(int r, const SearchHit *h, float room)
{
    Theme *t = &app.theme;
    Str num = str_from_c(arena_sprintf(&frame, "%lld:", (long long)h->line));
    Str more = h->more ? str_from_c(arena_sprintf(&frame, "+%lld", (long long)h->more)) : STR_EMPTY;
    room -= str_width(num) + 6 + (more.len ? str_width(more) + 6 : 0);
    if (room < 60) return;
    Str pre = { h->text.p, h->at }, mt = { h->text.p + h->at, h->len }, post = { mt.p + mt.len, h->text.len - h->at - h->len };
    float mw = str_width(mt) < room ? str_width(mt) : room, left = room - mw;
    // Before the match: what is left after the rest of the line, but at least a third, and no more
    // than it takes ("…" first when the line's start is left out).
    Str before = h->cut ? str_from_c(arena_sprintf(&frame, "\xE2\x80\xA6%.*s", STR_ARG(pre))) : pre;
    float prew = left - str_width(post);
    if (prew < left / 3) prew = left / 3;
    if (prew > str_width(before)) prew = str_width(before);
    CLAY(CLAY_IDI("hit", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 6 } }) {
        text(cs(num), cc(t->fg_dim));
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIT(0, room), CLAY_SIZING_FIT(0) } }, .clip = { .horizontal = true } }) {
            if (before.len && prew > 0) text(fit(before, prew, true), cc(t->fg_dim));
            text(fit(mt, mw, false), cc(t->accent));
            if (post.len && left - prew > 8) text(fit(post, left - prew, false), cc(t->fg_dim));
        }
        if (more.len) text(cs(more), cc(t->fg_dim));
    }
}

// A row's name colour: the cursor's row plain (blue on the selection is faint), marked the accent; a
// git change's over the type's, ignored dimmed (F12); else the type's.
static Clay_Color row_name_colour(isize i, bool sel, bool marked, GitState gs)
{
    Theme *t = &app.theme;
    const Entry *e = &app.list.e[i];
    return e->kind == K_LINK_BROKEN ? cc(t->red) : marked ? cc(t->accent) : sel ? cc(t->fg) : gs != GS_NONE ? git_colour(gs) : ls_name_colour(e, i);
}

static void layout_row(int r, isize i, float w, i64 now)
{
    const Entry *e = &app.list.e[i];
    Theme *t = &app.theme;
    bool sel = i == app.ui.list.sel, hover = r == app.hov.row, marked = ui_marked(&app.ui, i);
    // Marked rows: the accent mixed into the background; the cursor keeps the selection colour.
    Clay_Color bg = sel ? cc(t->selection) : marked ? mix(t->accent, t->bg, 0.28f) : hover ? cc(t->bg_light) : cc(t->bg);
    IconSlot *icon = entry_icon(i);
    char size[32] = "", when[96];
    size_cell(i, size, sizeof size, true);
    entry_when(e, now, when, sizeof when);
    bool link = e->kind == K_LINK_DIR || e->kind == K_LINK_FILE || e->kind == K_LINK_BROKEN;
    bool perms = perms_shown(w), in_git = git_shown() != NULL;
    GitState gs = entry_git(i);
    float name_w = w - side_w() - 2 * PAD - ICON_PX - 3 * GAP - SIZE_W - date_w() - (perms ? perms_w() : 0) - (link ? 16 : 0) - (in_git ? GIT_W + GAP : 0);
    CLAY(CLAY_IDI("row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ROW_H) },
                                           .padding = { PAD, PAD, 0, 0 }, .childGap = GAP,
                                           .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                               .backgroundColor = bg,
                               .border = { .color = cc(t->accent), .width = { .left = sel && app.ui.focus != UI_FOCUS_SIDE ? 3 : 0 } } }) {
        icon_box(CLAY_IDI("icon", r), icon, ICON_PX);
        CLAY(CLAY_IDI("name", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 6 } }) {
            if (sel && app.ui.focus == UI_FOCUS_EDIT && !app.ui.edit_path) {
                layout_edit(t, name_w);
            } else {
                text(fit(e->name, name_w, false), row_name_colour(i, sel, marked, gs));
                if (link) text(cz("\xE2\x86\x92"), cc(t->fg_dim));   // →
                if (link && e->target.len) {   // where it leads, as written, home as ~ (2026-10-27)
                    char to[PATH_MAX];
                    snprintf(to, sizeof to, "%.*s", STR_ARG(e->target));
                    char shown[PATH_MAX + 1];
                    ui_tilde(shown, sizeof shown, to, getenv("HOME"));
                    float room = name_w - str_width(e->name) - 12;
                    if (room > 40) text(fit(str_from_c(arena_sprintf(&frame, "%s", shown)), room, true), cc(t->fg_dim));
                }
                if (e->trash) {   // where it came from, home as ~
                    char from[PATH_MAX], shown[PATH_MAX + 2];
                    path_parent(from, sizeof from, e->trash->from);
                    ui_tilde(shown, sizeof shown, from, getenv("HOME"));
                    float room = name_w - str_width(e->name) - 12;
                    if (room > 40) text(fit(str_from_c(arena_sprintf(&frame, "%s", shown)), room, true), cc(t->fg_dim));
                }
                if (e->dir.len && !e->hit) {   // the search view's (F9): its folder below the one searched
                    float room = name_w - str_width(e->name) - 12;
                    if (room > 40) text(fit(e->dir, room, true), cc(t->fg_dim));
                }
                if (e->hit) {   // inside files (step 3): the folder gets at most a third, the line the rest
                    float room = name_w - str_width(e->name) - 12, dw = e->dir.len ? str_width(e->dir) : 0;
                    if (dw > room / 3) dw = room / 3;
                    bool dir = e->dir.len && (dw == str_width(e->dir) || dw > 40);   // (whole, or cut to something readable)
                    if (dir) text(fit(e->dir, dw, true), cc(t->fg_dim));
                    layout_hit(r, e->hit, room - (dir ? dw + 6 : 0));
                }
            }
        }
        if (in_git) git_cell(r, i, gs);   // (beside the name it marks, before Size: Michael, 2026-11-03)
        CLAY(CLAY_IDI("size", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(SIZE_W), CLAY_SIZING_FIT(0) },
                                                .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
            text(cz(arena_sprintf(&frame, "%s", size)), app.ui.colors ? cc(t->green) : cc(t->fg_dim));   // (eza's)
        }
        CLAY(CLAY_IDI("date", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(date_w()), CLAY_SIZING_FIT(0) },
                                                .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
            text(cz(arena_sprintf(&frame, "%s", when)), app.ui.colors ? cc(t->blue) : cc(t->fg_dim));   // (eza's)
        }
        // Owner, Group (View → Show Group), Permissions (2026-10-27): the target's for a link, as Size is.
        if (perms) {
            // (eza's: you and your group yellow, others plain)
            info_cell(CLAY_IDI("owner", r), id_name(false, e->uid), OWNER_W, OWNER_PAD, app.ui.colors && e->uid == (u32)geteuid() ? cc(t->yellow) : cc(t->fg_dim));
            if (app.ui.group_col) info_cell(CLAY_IDI("group", r), id_name(true, e->gid), GROUP_W, 0, app.ui.colors && e->gid == (u32)getegid() ? cc(t->yellow) : cc(t->fg_dim));
            ls_perms_cell(CLAY_IDI("perms", r), e->mode, PERM_W);
        }
    }
}

// Lines of a name under its icon, centred: the first `most` of them, the last cut with "…" when
// the name doesn't end there (ui_wrap). How many, and *cut_out whether it was cut.
static int name_lines(const char *id, int k, Str name, int most, float w, Clay_Color color, bool *cut_out)
{
    Str dots = S("\xE2\x80\xA6");
    isize from[CELL_LINES_FULL], to[CELL_LINES_FULL];
    bool cut;
    int n = ui_wrap(name, w, str_width(dots), advance, most, from, to, &cut);
    for (int j = 0; j < n; j++) {
        Str line = str_cut(name, from[j], to[j]);
        Clay_String txt = cut && j == n - 1 ? cz(arena_sprintf(&frame, "%.*s%.*s", STR_ARG(line), STR_ARG(dots))) : cs(line);
        CLAY(Clay_GetElementIdWithIndex(cz(id), (uint32_t)(k * CELL_LINES_FULL + j)),
             { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(CELL_LINE_H) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
            text(txt, color);
        }
    }
    *cut_out = cut;
    return n;
}

// Entry i in cell k of the icon view (F6.2): its icon over its name. The cursor's name shows in
// full on the selection colour, over the cells below (Nemo's way); a rename's edit line is there,
// wider than the cell.
static void layout_cell(int k, isize i)
{
    const Entry *e = &app.list.e[i];
    Theme *t = &app.theme;
    bool sel = i == app.ui.list.sel, hover = k == app.hov.row, marked = ui_marked(&app.ui, i);
    GitState gs = entry_git(i);   // (F12: the label tinted, a dot on the icon's corner)
    Clay_Color fg = e->kind == K_LINK_BROKEN ? cc(t->red) : marked ? cc(t->accent) : gs != GS_NONE ? git_colour(gs) : cc(t->fg);
    Clay_Color bg = marked ? mix(t->accent, t->bg, 0.28f) : hover ? cc(t->bg_light) : NO_COLOR;
    int cw = cell_w(), px = grid_px();
    float name_w = (float)(cw - 2 * CELL_PAD);
    CLAY(CLAY_IDI("row", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(cw), CLAY_SIZING_FIXED(cell_h()) }, .padding = { CELL_PAD, CELL_PAD, 6, CELL_PAD },
                                           .childGap = 4, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
                               .backgroundColor = sel ? mix(t->selection, t->bg, 0.5f) : bg, .cornerRadius = CLAY_CORNER_RADIUS(4) }) {
        Texture2D *th = entry_thumb(i, true);
        if (th) {
            // In the icon's square, its shape kept: a texel a device pixel.
            float s = (float)app.icon_scale;
            CLAY(CLAY_IDI("icon", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(px), CLAY_SIZING_FIXED(px) },
                                                    .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
                CLAY(CLAY_IDI("thumb", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(th->width / s), CLAY_SIZING_FIXED(th->height / s) } },
                                             .image = { .imageData = th } }) {}
            }
        } else {
            icon_box(CLAY_IDI("icon", k), entry_icon(i), px);
        }
        if (gs > GS_IGNORED) {   // (the list's ● on the icon's corner: the renderer draws no round boxes)
            CLAY(CLAY_IDI("git-dot", k), { .floating = { .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID, .parentId = CLAY_IDI("icon", k).id, .zIndex = 2,
                                                         .offset = { 4, -6 }, .attachPoints = { CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP } } }) {
                text(cz("\xE2\x97\x8F"), git_colour(gs));
            }
        }
        if (sel && app.ui.focus == UI_FOCUS_EDIT && !app.ui.edit_path) {
            // As wide as the name and a little, from a cell's width to two cells'.
            float ew = str_width((Str){ app.ui.edit.b, app.ui.edit.len }) + 24;
            ew = ew < cw ? cw : ew > 2 * cw ? 2 * cw : ew;
            CLAY(CLAY_ID("cell-edit"), { .layout = { .sizing = { CLAY_SIZING_FIXED(ew), CLAY_SIZING_FIT(0) } },
                                         .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 3, .offset = { 0, 6 + (float)px + 4 },
                                                       .attachPoints = { CLAY_ATTACH_POINT_CENTER_TOP, CLAY_ATTACH_POINT_CENTER_TOP },
                                                       .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT } }) {
                layout_edit(t, ew);
            }
        } else if (sel) {
            // Its two lines' room stays taken, so the cell keeps its size.
            CLAY(CLAY_IDI("cell-room", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(2 * CELL_LINE_H) } } }) {}
            CLAY(CLAY_ID("cell-full"), { .layout = { .sizing = { CLAY_SIZING_FIXED(cw), CLAY_SIZING_FIT(0) }, .padding = { CELL_PAD, CELL_PAD, 0, 2 },
                                                     .layoutDirection = CLAY_TOP_TO_BOTTOM },
                                         .backgroundColor = mix(t->selection, t->bg, 0.5f), .cornerRadius = CLAY_CORNER_RADIUS(4),
                                         .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 3, .offset = { 0, 6 + (float)px + 4 },
                                                       .attachPoints = { CLAY_ATTACH_POINT_CENTER_TOP, CLAY_ATTACH_POINT_CENTER_TOP },
                                                       .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT } }) {
                bool cut;
                app.grid_full_lines = name_lines("cell-full-line", 0, e->name, CELL_LINES_FULL, name_w, fg, &cut);
                if (hover) app.grid_hover_lines = app.grid_full_lines, app.grid_hover_cut = cut;
            }
        } else {
            bool cut;
            int n = name_lines("cell-line", k, e->name, 2, name_w, fg, &cut);
            if (hover) app.grid_hover_lines = n, app.grid_hover_cut = cut;
        }
    }
    if (k == 0) app.grid_first = i;
    app.grid_last = i, app.grid_cells++;
}

// The icon view: cells across, then down, from the scroll's row.
static void layout_grid(void)
{
    int cols = (int)ui_cols(&app.ui.list);
    for (int r = 0; r < app.ui.list.rows; r++) {
        isize first = app.ui.list.top + (isize)r * cols;
        if (first >= app.list.n) break;
        CLAY(CLAY_IDI("grid-row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(cell_h()) }, .padding = { PAD, PAD, 0, 0 } } }) {
            for (int c = 0; c < cols && first + c < app.list.n; c++) layout_cell(r * cols + c, first + c);
        }
    }
    // The previews of a page ahead are asked for after the screen's (F6.4).
    isize ahead = app.ui.list.top + (isize)app.ui.list.rows * cols;
    for (isize i = ahead; i < ahead + (isize)app.ui.list.rows * cols && i < app.list.n; i++) entry_thumb(i, false);
}
