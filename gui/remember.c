// What ofx remembers per folder (F11, fs/folders.c): the table read from the state file at the start,
// a folder's own view, icon size and sort applied as it is entered (else ofx.conf's defaults), its
// changes written at once, and the cursor's entry kept as a folder is left and written with the
// next write or at the quit. When ofx.conf's defaults change, the folders' choices for them go.
// Included by main.c, before folder.c.

// The state file written this long after the last change of the tabs (restore.c) or of a folder's
// view (here): 2 s (Michael, 2026-11-02), so a Ctrl+wheel spin or a run of sort clicks is one write,
// not one each (F11 review). OFX_TEST_WRITE_MS: shorter, for the GUI tests (0 to 2000).
static i64 write_delay_ms(void)
{
    static i64 ms = -1;
    if (ms < 0) ms = env_int("OFX_TEST_WRITE_MS", 2000, 0, 2000);
    return ms;
}

static struct {
    Folders t;
    u8      mem[2][MiB(2)];   // the table's strings: a write builds the new table in the other one
    Arena   a[2];
    int     cur;
    bool    dirty;            // a cursor kept, not written yet
    i64     due;              // a view's change to write at this time (now_ms), 0 none
} rem;

static bool fold_view_of(UiView v)
{
    return v == UI_VIEW_FOLDER || v == UI_VIEW_TRASH;   // (a search's results keep the view they have)
}

// The folders as the state file has them now.
static void remember_load(void)
{
    char path[PATH_MAX];
    rem.cur = 0;
    for (int i = 0; i < 2; i++) arena_init(&rem.a[i], "folders", rem.mem[i], sizeof rem.mem[i]);
    ArenaMark mark = arena_mark(&scratch);
    folders_parse(&rem.a[0], state_path(path, sizeof path) ? read_file(&scratch, path) : STR_EMPTY, &rem.t);
    arena_restore(&scratch, mark);
    rem.dirty = false;
}

// This window's changes into the state file (folders_write), the table then the file's.
static void remember_write(void)
{
    char path[PATH_MAX];
    Err err;
    if (!state_path(path, sizeof path)) return;
    int next = 1 - rem.cur;
    arena_reset(&rem.a[next]);
    CleanMounts ms = clean_mounts(&rem.a[next], read_file(&rem.a[next], mountinfo_path()));
    if (!folders_write(&rem.a[next], path, &rem.t, &ms, &err)) {
        set_status(true, "%s", err.msg);
        return;
    }
    rem.cur = next, rem.dirty = false;
}

// dir's own view, icon size and sort, or ofx.conf's defaults for what it hasn't (F11).
static void remember_apply(const char *dir)
{
    int i = folders_find(&rem.t, dir);
    const FolderPrefs *f = i >= 0 ? &rem.t.f[i] : NULL;
    bool grid = f && f->view >= 0 ? f->view == 1 : app.conf.icons;
    int zoom = f && f->zoom > 0 ? f->zoom : app.conf.zoom;
    Sort sort = { (SortKey)(f && f->sort >= 0 ? f->sort : app.conf.sort), f && f->rev >= 0 ? f->rev == 1 : app.conf.sort_rev };
    if (grid != app.ui.grid || zoom != ui_zoom_px(&app.ui)) app.icons_reset_armed = true, app.zoomed = true;
    app.ui.grid = grid, app.ui.zoom = zoom, app.ui.sort = sort;   // (the loop sets the columns before the next frame)
}

// The cursor's entry dir had when left, to be there when it is opened again (written later).
static void remember_cursor(const char *dir, Str name)
{
    FolderPrefs *f = folders_get(&rem.a[rem.cur], &rem.t, dir);
    if (!f) return;
    f->sel = name.len && name.len <= NAME_MAX ? folder_str(&rem.a[rem.cur], name) : NULL;
    f->at = (i64)time(NULL), f->changed |= FP_SEL | FP_AT, rem.dirty = true;
    if (rem.a[rem.cur].used > (isize)sizeof rem.mem[0] / 2) remember_write();   // (each write starts the strings afresh)
}

// The cursor's entry remembered for dir, NULL for none.
static const char *remember_sel(const char *dir)
{
    int i = folders_find(&rem.t, dir);
    return i >= 0 ? rem.t.f[i].sel : NULL;
}

// The folder shown's view, zoom or sort changed (mask: FP_VIEW and the others): its own now, unless
// it is the default again; written write_delay_ms after the last such change.
static void remember_view(u8 mask)
{
    if (!fold_view_of(app.ui.view)) return;
    FolderPrefs *f = folders_get(&rem.a[rem.cur], &rem.t, app.cwd);
    if (!f) return;
    if (mask & FP_VIEW) f->view = app.ui.grid == app.conf.icons ? -1 : app.ui.grid;
    if (mask & FP_ZOOM) f->zoom = ui_zoom_px(&app.ui) == app.conf.zoom ? 0 : ui_zoom_px(&app.ui);
    if (mask & FP_SORT) f->sort = (int)app.ui.sort.key == app.conf.sort ? -1 : (int)app.ui.sort.key;
    if (mask & FP_REV) f->rev = app.ui.sort.reverse == app.conf.sort_rev ? -1 : app.ui.sort.reverse;
    f->at = (i64)time(NULL), f->changed |= mask | FP_AT;
    rem.dirty = true, rem.due = now_ms() + write_delay_ms();   // (written by remember_tick, or at the quit)
}

// After each frame's input: a view's change written once its delay is up. The due time for the
// timer, 0 for none.
static i64 remember_tick(i64 now)
{
    if (rem.due && now >= rem.due) rem.due = 0, remember_write();
    return rem.due;
}

// ofx.conf's view defaults as the state file's folderdefaults= says them: "list 48 name off".
static void remember_stamp(char *out, size_t cap)
{
    snprintf(out, cap, "%s %d %s %s", app.conf.icons ? "icons" : "list", app.conf.zoom, SORT_NAMES[app.conf.sort], app.conf.sort_rev ? "on" : "off");
}

// At the start and when ofx.conf changes: a default that differs from the one the folders' choices
// were made against (folderdefaults=) forgets them, said in the status line. The table is read
// again first (another window may have done it).
static void remember_defaults(void)
{
    char path[PATH_MAX], now[64];
    Err err;
    if (!state_path(path, sizeof path)) return;
    if (rem.dirty) remember_write();
    remember_load();
    remember_stamp(now, sizeof now);
    State st = state_load(&scratch, path);
    if (strcmp(st.folder_defaults, now) == 0) return;
    u8 mask = 0;
    if (st.folder_defaults[0]) {   // (none: the first start with F11, nothing to forget)
        char view[16] = "", sort[16] = "", rev[8] = "";
        int zoom = 0;
        sscanf(st.folder_defaults, "%15s %d %15s %7s", view, &zoom, sort, rev);
        mask |= strcmp(view, app.conf.icons ? "icons" : "list") ? FP_VIEW : 0;
        mask |= zoom != app.conf.zoom ? FP_ZOOM : 0;
        mask |= strcmp(sort, SORT_NAMES[app.conf.sort]) ? FP_SORT : 0;
        mask |= strcmp(rev, app.conf.sort_rev ? "on" : "off") ? FP_REV : 0;
    }
    int n = mask ? folders_wipe(&rem.t, mask) : 0;
    if (n) remember_write();
    if (!state_put(&scratch, path, "folderdefaults", now, &err)) set_status(true, "%s", err.msg);
    if (n) set_status(false, "ofx.conf's default %s changed: %d folder%s had %s own, forgotten", mask & FP_VIEW ? "view" : mask & FP_ZOOM ? "zoom" : "sort", n,
                      n == 1 ? "" : "s", n == 1 ? "its" : "their");
}
