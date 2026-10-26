// Open With Other Application (F7.3, docs/plan.md): a panel with a filter field over the apps, the
// default app for the files' type first, then the recommended ones, then the rest; and Enter on
// several marked files, each group with the same default app in one launch. Included by main.c.

enum { OW_ROWS_MAX = 10, OW_ROW_H = 30, OW_W = 560 };

static u8    ow_mem[MiB(16)];
static Arena owa;

static struct {
    Apps   apps;
    isize  shown;                 // the apps listed: the first `shown` of apps (apps_rank's order)
    Str    names[UI_APPS_MAX];    // their names, for ui/'s filter
    char  *paths[CLIP_PATHS_MAX]; // the files to open (in owa)
    isize  n;
    char   type[128];             // the first one's MIME type, as gio has it
    char   what[NAME_MAX + 32];   // "notes.txt", "3 files"
} ow;

// The panel opened (UA_OPEN_WITH): the marked entries, or the cursor's; their apps listed for the
// first one's type (gio's, its content read: what `gio open` would use).
static void ow_open(void)
{
    arena_reset(&owa);
    ow.n = targets(&owa, ow.paths, CLIP_PATHS_MAX), ow.shown = 0;
    if (ow.n == 0) {
        ui_ow_close(&app.ui);
        return;
    }
    if (!app.thumb_local || !spawn_gio_type(ow.paths[0], ow.type, sizeof ow.type, 1000)) {
        isize i = app.ui.marked ? -1 : app.ui.list.sel;
        for (isize k = 0; i < 0 && k < app.list.n; k++)
            if (app.ui.mark[k]) i = k;
        Str t = i >= 0 ? entry_mime(i) : S("application/octet-stream");
        snprintf(ow.type, sizeof ow.type, "%.*s", STR_ARG(t));
    }
    if (ow.n == 1) snprintf(ow.what, sizeof ow.what, "%.*s", STR_ARG(path_base(str_from_c(ow.paths[0]))));
    else snprintf(ow.what, sizeof ow.what, "%td items", ow.n);
    apps_all(&owa, &ow.apps);
    ow.shown = apps_rank(&owa, &ow.apps, str_from_c(ow.type));
    for (isize i = 0; i < ow.shown && i < UI_APPS_MAX; i++) ow.names[i] = ow.apps.a[i].name;
    // Filtered now: an idle window draws its next frame only on input, and showed "No app
    // matches" until then (Michael, 2026-10-26).
    UiInput in = ui_input_none();
    in.app_name = ow.names, in.app_n = ow.shown < UI_APPS_MAX ? ow.shown : UI_APPS_MAX;
    ui_ow_filter(&app.ui, &in);
}

// The app ui/ picked (UA_LAUNCH): it gets every file, SPAWN_FILES_MAX a launch.
static void ow_launch(isize i)
{
    if (i < 0 || i >= ow.shown) return;   // (ui/ hands only the listed ones)
    const App *p = &ow.apps.a[i];
    char desktop[PATH_MAX];
    snprintf(desktop, sizeof desktop, "%.*s", STR_ARG(p->path));
    Err err;
    for (isize at = 0; at < ow.n; at += SPAWN_FILES_MAX) {
        isize k = ow.n - at < SPAWN_FILES_MAX ? ow.n - at : SPAWN_FILES_MAX;
        if (!spawn_launch_all(desktop, ow.paths + at, k, &err)) {
            set_status(true, "%s", err.msg);
            return;
        }
    }
    set_status(false, "opened %s with %.*s", ow.what, STR_ARG(p->name));
}

// Enter (or Open) on several marked entries (F7.3, Michael 2026-10-26): the files, those with the
// same default app (by their type, as F7.1.1's empty files) in one launch; the ones no app is set
// for through `gio open` each, which says why it can't. Folders among them stay: Enter on a
// folder alone goes into it.
static void open_marked(void)
{
    enum { GROUPS_MAX = 64 };
    static char *paths[CLIP_PATHS_MAX];
    static char  desk[GROUPS_MAX][PATH_MAX];
    static isize group[CLIP_PATHS_MAX];
    arena_reset(&owa);
    isize n = 0, ngroups = 0, dirs = 0, opened = 0;
    Err err;
    bool bad = false;
    for (isize i = 0; i < app.list.n && n < CLIP_PATHS_MAX; i++) {
        if (!app.ui.mark[i]) continue;
        const Entry *e = &app.list.e[i];
        char p[PATH_MAX];
        if (kind_is_dir(e->kind) || e->kind == K_LINK_BROKEN || !entry_path(i, p, sizeof p)) {
            dirs++;
            continue;
        }
        char d[PATH_MAX];
        isize g = -1;
        if (apps_default(&scratch, entry_mime(i), d, sizeof d)) {
            for (isize k = 0; k < ngroups && g < 0; k++)
                if (strcmp(desk[k], d) == 0) g = k;
            if (g < 0 && ngroups < GROUPS_MAX) g = ngroups++, snprintf(desk[g], sizeof desk[g], "%s", d);
        }
        paths[n] = arena_cstr(&owa, str_from_c(p)), group[n++] = g;
    }
    if (n == 0) {   // only folders marked: the cursor's, as before
        open_entry(app.ui.list.sel);
        return;
    }
    for (isize g = 0; g < ngroups; g++) {
        static char *one[CLIP_PATHS_MAX];
        isize k = 0;
        for (isize i = 0; i < n; i++)
            if (group[i] == g) one[k++] = paths[i];
        for (isize at = 0; at < k; at += SPAWN_FILES_MAX)
            if (!spawn_launch_all(desk[g], one + at, k - at < SPAWN_FILES_MAX ? k - at : SPAWN_FILES_MAX, &err)) bad = true;
        opened += k;
    }
    for (isize i = 0; i < n; i++)
        if (group[i] < 0) bad |= !spawn_open(paths[i], &err), opened++;
    if (bad) set_status(true, "%s", err.msg);
    else set_status(false, "opened %td %s%s", opened, opened == 1 ? "file" : "files", dirs ? " (folders left)" : "");
}

// ---- the panel ----------------------------------------------------------------------------------

// An app's icon: Icon= a name the theme has (its Apps), a PNG of that name in a data dir's
// pixmaps/ (Spotify's, hyprmon's), or a PNG's own path; else the generic one. An SVG only can't
// be drawn (ofx loads PNG), and gets the generic one too.
static IconSlot *ow_icon(Str icon)
{
    Str generic = S("application-x-executable");
    if (icon.len == 0) return icon_for(generic, &generic, 1);
    Str key = str_from_c(arena_sprintf(&frame, "app:%.*s", STR_ARG(icon)));
    int k;
    IconSlot *s = strmap_get(&app.slot_of, key, &k) ? &app.slot[k] : NULL;
    if (!s) {
        ArenaMark mark = arena_mark(&scratch);
        const char *file = NULL;
        bool path = icon.p[0] == '/';
        if (path) {
            char *f = arena_cstr(&scratch, icon);
            if (str_ends_with(icon, S(".png")) && access(f, R_OK) == 0) file = f;
        } else {
            const char *dirs[MIME_DIRS_MAX];
            isize nd = data_dirs(&scratch, dirs, MIME_DIRS_MAX);
            for (isize d = 0; d < nd && !file; d++) {
                char *f = arena_sprintf(&scratch, "%s/pixmaps/%.*s.png", dirs[d], STR_ARG(icon));
                if (access(f, R_OK) == 0) file = f;
            }
        }
        s = icon_for_px_or(key, &icon, path ? 0 : 1, ICON_PX, file);   // (a path: no theme name to try)
        arena_restore(&scratch, mark);
    }
    return s && s->tex.id ? s : icon_for(generic, &generic, 1);   // none found: the generic one, every frame
}

// The rows the list shows for a window h high: OW_ROWS_MAX, fewer in a small window.
static int ow_rows(int h)
{
    int r = (h - 250) / OW_ROW_H;
    return r < 1 ? 1 : r > OW_ROWS_MAX ? OW_ROWS_MAX : r;
}

// A toggle (1 case, 2 whole word, 3 regular expression) as VS Code draws them: a small box, lit when on.
static void ow_toggle(int k, bool on)
{
    Theme *t = &app.theme;
    static const char *const LABEL[] = { "", "Aa", "ab", ".*" };
    CLAY(CLAY_IDI("toggle", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(28), CLAY_SIZING_FIXED(22) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                              .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                  .backgroundColor = on ? mix(t->accent, t->bg, 0.35f) : cc(t->bg),
                                  .cornerRadius = CLAY_CORNER_RADIUS(3),
                                  .border = { .color = on ? cc(t->accent) : cc(t->bg), .width = CLAY_BORDER_OUTSIDE(1) } }) {
        text(cz(LABEL[k]), on ? cc(t->fg) : cc(t->fg_dim));
        if (k == 2) CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(1) } }, .backgroundColor = on ? cc(t->fg) : cc(t->fg_dim) }) {}
    }
}

// The three toggles in a row (the find line's and the panel's field).
static void layout_toggles(UiMatchOpts o)
{
    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) }, .childGap = 2, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        ow_toggle(1, o.cased), ow_toggle(2, o.word), ow_toggle(3, o.regex);
    }
}

static void layout_ow_panel(void)
{
    Theme *t = &app.theme;
    Ui *u = &app.ui;
    static const char *const SECTION[] = { "Default", "Recommended", "Related", "Other applications" };
    CLAY(CLAY_ID("ow"), { .layout = { .sizing = { CLAY_SIZING_FIXED(OW_W), CLAY_SIZING_FIT(0) }, .padding = { 24, 24, 20, 20 }, .childGap = 10,
                                      .layoutDirection = CLAY_TOP_TO_BOTTOM },
                          .backgroundColor = cc(t->bg_light),
                          .border = { .color = cc(t->accent), .width = CLAY_BORDER_OUTSIDE(1) },
                          .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 20,
                                        .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER } } }) {
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 8 } }) {
            text(cz("Open"), cc(t->fg_dim));
            text(fit(str_from_c(ow.what), OW_W - 260, false), cc(t->fg));
            text(cz("with"), cc(t->fg_dim));
            CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
            text(fit(str_from_c(ow.type), 160, false), cc(t->fg_dim));
        }
        // The field: its text and caret, the toggles at its right end; red when the expression is bad.
        bool bad = u->ow_m.bad;
        Clay_Color edge = bad ? cc(t->red) : !u->ow_in_list ? cc(t->accent) : mix(t->fg_dim, t->bg_light, 0.45f);
        CLAY(CLAY_ID("ow-field"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(32) }, .padding = { 8, 4, 0, 0 }, .childGap = 6,
                                                .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                    .backgroundColor = cc(t->bg),
                                    .border = { .color = edge, .width = CLAY_BORDER_OUTSIDE(1) } }) {
            CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                           .clip = { .horizontal = true } }) {
                Str before = { u->ow_text, u->ow_caret }, after = { u->ow_text + u->ow_caret, u->ow_len - u->ow_caret };
                if (u->ow_len == 0 && u->ow_in_list) text(cz("Type to filter"), cc(t->fg_dim));
                text(fit(before, OW_W - 200, true), cc(t->fg));
                if (!u->ow_in_list)
                    CLAY(CLAY_ID("ow-caret"), { .layout = { .sizing = { CLAY_SIZING_FIXED(2), CLAY_SIZING_FIXED(FONT_PX + 2) } }, .backgroundColor = cc(t->accent) }) {}
                text(fit(after, OW_W - 200 - str_width(before), false), cc(t->fg));
            }
            layout_toggles(u->ow_opts);
        }
        if (bad) text(fit(str_from_c(u->ow_m.err), OW_W - 48, false), cc(t->red));
        // The list: a row an app, the cursor's lit (brighter with the keys there); the first of each
        // part says which part it starts.
        CLAY(CLAY_ID("ow-list"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(u->ow_list.rows * OW_ROW_H) }, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                                   .clip = { .vertical = true } }) {
            if (u->ow_list.n == 0)
                CLAY_AUTO_ID({ .layout = { .padding = { 8, 0, 8, 0 } } }) { text(cz(ow.shown ? "No app matches" : "No apps"), cc(t->fg_dim)); }
            for (int r = 0; r < u->ow_list.rows; r++) {
                isize v = u->ow_list.top + r;
                if (v >= u->ow_list.n) break;
                isize i = u->ow_idx[v];
                const App *p = &ow.apps.a[i];
                bool sel = v == u->ow_list.sel, first = v == 0 || ow.apps.a[u->ow_idx[v - 1]].section != p->section;
                Clay_Color bg = sel ? (u->ow_in_list ? mix(t->accent, t->bg_light, 0.45f) : mix(t->accent, t->bg_light, 0.22f)) : cc(t->bg_light);
                CLAY(CLAY_IDI("ow-row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(OW_ROW_H) }, .padding = { 8, 8, 0, 0 }, .childGap = 10,
                                                          .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                              .backgroundColor = bg,
                                              .border = { .color = first && v > 0 ? mix(t->fg_dim, t->bg_light, 0.3f) : bg, .width = { .top = first && v > 0 ? 1 : 0 } } }) {
                    icon_box(CLAY_IDI("ow-icon", r), ow_icon(p->icon), ICON_PX);
                    text(fit(p->name, OW_W - 260, false), cc(t->fg));
                    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
                    if (first && p->section < AS_NONE) text(cz(SECTION[p->section]), cc(t->fg_dim));
                }
            }
        }
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = { 0, 0, 6, 0 }, .childGap = 10,
                                   .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
            modal_button("modal-no", UI_MODAL_NO, "Cancel", false, false);
            modal_button("modal-yes", UI_MODAL_YES, "Open", true, false);
        }
    }
}
