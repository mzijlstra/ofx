// The folder shown: going to one (a listing, the search view, the trash view), its history, the
// places and the tree in the sidebar, reloads. Included by main.c.

// ---- the folder -----------------------------------------------------------------------------

// The folder shown: in the search view, the folder searched (F9).
static const char *here_dir(void)
{
    return app.ui.view == UI_VIEW_SEARCH ? app.search_root : app.cwd;
}

// The cursor's entry as listing_find finds it: its name, in the search view its folder and name (F9).
static Str sel_name(void)
{
    static char key[PATH_MAX];
    return app.have_list && app.ui.list.n > 0 ? entry_key(&app.list.e[app.ui.list.sel], key, sizeof key) : STR_EMPTY;
}

// Entry i's file: in the folder shown, in the trash view where the trash keeps it, or in the search
// view below the folder searched (F9). False when the path doesn't fit.
static bool entry_path(isize i, char *out, size_t cap)
{
    const Entry *e = &app.list.e[i];
    if (e->trash) {
        int n = snprintf(out, cap, "%s", e->trash->file);
        return n >= 0 && (size_t)n < cap;
    }
    if (app.ui.view == UI_VIEW_SEARCH) {
        char dir[PATH_MAX];
        if (e->dir.len && !path_put(dir, sizeof dir, app.search_root, e->dir)) return false;
        return path_put(out, cap, e->dir.len ? dir : app.search_root, e->name);
    }
    return path_put(out, cap, app.cwd, e->name);
}

// The folder an action is on: (all) folder entry index's, from its right-click menu (F8), else the
// folder shown; NULL when that entry is gone. buf holds an entry's path.
static const char *act_dir(UiAction a, char *buf, size_t cap)
{
    if (!a.all) return here_dir();
    return a.index < app.list.n && entry_path(a.index, buf, cap) ? buf : NULL;
}

static void tree_refresh(bool follow, int fallback);
static void icons_reset(void);

static TrashDirs trashd;   // the trash folders the trash view shows (F6)

// The trash folders there are now: the home trash, and each drive's but a far one's (F16 step 3: a
// dead share's would hold the window on every look at the trash, from any folder).
static void trash_scan(void)
{
    const char *mp[PLACES_MAX];
    int n = 0;
    const Mounts *mi = far_mounts();
    for (int i = 0; i < app.places.n; i++)
        if (place_is_drive(app.places.p[i].kind) && mounts_local(mi, app.places.p[i].path)) mp[n++] = app.places.p[i].path;
    trash_dirs(&trashd, mp, n);
}

// Whether any trash folder trash_scan found holds something (trash_state's, for the Trash icon and
// the folder menu's Empty Trash).
static bool trash_any(void)
{
    for (int i = 0; i < trashd.n; i++) {
        char files[PATH_MAX];
        if (snprintf(files, sizeof files, "%s/files", trashd.dir[i]) < (int)sizeof files && !dir_empty(files)) return true;
    }
    return false;
}

// How many items the trash folders hold (each trash's files/), for the Empty Trash question: counted
// once as it opens, the same in any view (review, 2026-11-02: from a folder's menu nothing
// showed how much goes).
static isize trash_count(void)
{
    isize n = 0;
    trash_scan();
    for (int i = 0; i < trashd.n; i++) {
        char files[PATH_MAX];
        if (snprintf(files, sizeof files, "%s/files", trashd.dir[i]) >= (int)sizeof files) continue;
        DIR *d = opendir(files);
        for (struct dirent *e; d && (e = readdir(d));) n += strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0;
        if (d) closedir(d);
    }
    return n;
}

// The Trash place's icon, full or empty (2026-10-30): looked at again at the start, when the mounts
// change, when the trash view lists (a trash folder made on a drive already mounted has no watch
// yet), after ofx's own trashing and jobs (the first one makes the home trash's info/), and when a
// trash folder's watch says something changed there; the watches follow the folders found. True
// when it changed.
static bool trash_state(void)
{
    trash_scan();
    watch_trashes(&trashd);   // armed before the look, so a change after it is an event (a review)
    bool full = trash_any();
    bool changed = full != app.trash_full;
    app.trash_full = full;
    return changed;
}

// The search an address names, with the toggles and hidden files as they are now (root: its
// folder, copied out).
static SearchQuery search_query(Str text, Str inside, const char *root)
{
    return (SearchQuery){ .root = root, .text = text, .inside = inside, .o = app.ui.find_opts, .io = app.ui.inside_opts, .hidden = app.show_hidden,
                          .skips = &app.conf.skip };
}

// The search view's listing (F9): the matches so far, sorted. A search starts first when another
// one runs or ran (entering the view always does: leaving it asked for none).
static bool search_list(Arena *a, const char *uri, Listing *l, Err *err)
{
    Str root, text, inside;
    char r[PATH_MAX];
    if (!search_parse(uri, &root, &text, &inside) || root.len >= PATH_MAX) return err_set(err, "Not a search"), false;
    snprintf(r, sizeof r, "%.*s", STR_ARG(root));
    SearchQuery q = search_query(text, inside, r);
    if (!search_is(q)) {
        search_ask(q);
        ui_matcher_set(&app.search_m, text, app.ui.find_opts);
        ui_matcher_set(&app.search_im, inside, app.ui.inside_opts);
    }
    if (!search_copy(a, l, &app.search_s, &app.skips_shown, &app.search_done)) return err_set(err, "%s: too many results for memory", r), false;
    fs_sort(l, app.ui.sort);
    snprintf(app.search_root, sizeof app.search_root, "%s", r);
    return true;
}

// ---- the views (2026-10-28) -------------------------------------------------------------------
//
// What differs between a folder, the trash view (F6) and a search's results (F9), asked here rather
// than by testing the view at each place: how one is listed and watched, its title, its bar, its
// date column, where Up goes, what can be put in it.

static const struct {
    const char *date_col;   // the date column's header
    int         bar_h;      // its bar under the top bar: the trash's buttons, the search field
} VIEWS[] = {
    [UI_VIEW_FOLDER] = { "Modified", 0 },
    [UI_VIEW_TRASH] = { "Deleted", TRASH_BAR_H },
    [UI_VIEW_SEARCH] = { "Modified", SEARCH_BAR_H },
};

// The view's bar's height: the search's grows by its "Containing" row inside files (step 3).
static int view_bar_h(void)
{
    return VIEWS[app.ui.view].bar_h + (app.ui.view == UI_VIEW_SEARCH && app.ui.search_inside ? SEARCH_ROW_H : 0);
}

static UiView view_of(const char *dir)
{
    return strcmp(dir, TRASH_URI) == 0 ? UI_VIEW_TRASH : is_search(dir) ? UI_VIEW_SEARCH : UI_VIEW_FOLDER;
}

static bool view_list(UiView v, Arena *a, const char *dir, Listing *l, Err *err)
{
    switch (v) {
    case UI_VIEW_TRASH: trash_state(); return trash_list(a, &trashd, app.show_hidden, app.ui.sort, l, err);
    case UI_VIEW_SEARCH: return search_list(a, dir, l, err);
    case UI_VIEW_FOLDER: break;
    }
    return fs_list_far(a, dir, app.show_hidden, app.ui.sort, far_mounts(), l, err);   // (its far links and mount points never looked at, F16)
}

// What a view's changes come in by: the folder, the home trash's info/ for the trash view (a
// drive's trash changes from ofx reload it), nothing for a search (Ctrl+R runs it again).
static void view_watch(UiView v)
{
    char tinfo[PATH_MAX + 8] = "";
    if (v == UI_VIEW_TRASH && trash_home(tinfo, sizeof tinfo - 8)) strcat(tinfo, "/info");
    watch_dir(v == UI_VIEW_TRASH ? tinfo : v == UI_VIEW_SEARCH || app.far_on ? NULL : app.cwd);   // (a far folder: no watch, its add would look it up; F16)
}

static void view_title(UiView v)
{
    char shown[PATH_MAX + 1];
    ui_tilde(shown, sizeof shown, app.search_root, getenv("HOME"));
    SetWindowTitle(v == UI_VIEW_TRASH ? "ofx: Trash" : v == UI_VIEW_SEARCH ? TextFormat("ofx: Search in %s", shown) : TextFormat("ofx: %s", app.cwd));
}

// Whether Up goes somewhere: from a search to the folder searched, from a folder but "/" to the one
// above; the trash view has no folder above (2026-10-28: its Up button and Backspace went to
// "trash:", which isn't one).
static bool view_can_up(void)
{
    return app.ui.view == UI_VIEW_SEARCH || (app.ui.view == UI_VIEW_FOLDER && strcmp(app.cwd, "/") != 0);
}

// After a change ofx made: in a search, the results whose file went are left out (no watch there).
static void view_changed(void)
{
    if (app.ui.view == UI_VIEW_SEARCH) search_prune(app.search_root);
}

// ---- a far folder (F16 step 2: gui/far.c) -------------------------------------------------------

enum { FAR_ANSWER_MS = 3000 };   // no answer by then: the list says the drive isn't answering

static bool go_to_in(const char *dir, Str select, isize fallback, GoHow how);

static isize far_scan;   // how far what the lister said was looked through for its listing's end

// dir's lister started (or, while one killed for it is still in the kernel, put off until it is
// gone: far_poll), its listing to come with the cursor on `select` (else row `fallback`), its
// refusal leading where how.then says. False, with the reason, when it can't start.
static bool far_begin(const char *dir, Str select, isize fallback, GoHow how, Err *err)
{
    // Listed again before its first answer came: a reload keeps how ofx got there (a link that may
    // be a file, a path typed; found under load, step 3) unless that was plain; another way there (a
    // new tab on it) sets its own, when it has one. (As the calls patched far_then after go_to did,
    // the step D review.)
    if (!(app.far_loading && strcmp(dir, app.cwd) == 0) || (how.reload ? app.far_then == FAR_THEN_SHOW : how.then != FAR_THEN_SHOW)) app.far_then = how.then;
    snprintf(app.far_select, sizeof app.far_select, "%.*s", STR_ARG(select));
    app.far_fallback = fallback, app.far_since = now_ms(), app.far_slow = false, app.far_note[0] = 0, far_scan = 0;
    // The last lister killed first: if it is still in the kernel (a reload of a folder that hangs),
    // it holds this one back too (step 2a review: asked before, a reload started one at once).
    far_drop();
    app.far_loading = true, app.far_waiting = far_busy(dir);
    if (!app.far_waiting && !far_start(app.exe, dir, app.show_hidden, err)) return app.far_loading = false, false;
    timer_at(wake_due());   // (one timer for all: wake_due holds the "isn't answering" time)
    return true;
}

// go_to_in's listing of far folder dir: the lister's answer when it came (how.far_take), into a;
// else none yet, the lister started.
static bool far_list(const char *dir, Arena *a, Listing *l, Str select, isize fallback, GoHow how, Err *err)
{
    if (!how.far_take) {
        *l = (Listing){ 0 }, app.far_can_write = false, app.far_kept = false;   // (not known until it says: not the last folder's)
        return far_begin(dir, select, fallback, how, err);
    }
    isize len;
    bool eof, full, ended;
    far_said(&len, &eof, &full);
    char name[NAME_MAX + 1];   // (said by its name: the crumbs say where)
    snprintf(name, sizeof name, "%.*s", STR_ARG(path_base(str_from_c(dir))));
    if (!far_listing(a, (Str){ (char *)far_mem, len }, name, l, &app.far_can_write, &app.far_at, &ended, err)) return false;
    fs_sort(l, app.ui.sort);
    return true;
}

// The far folder's lister can't give its listing: why, in the list and the status line.
static void far_failed(const char *why)
{
    app.far_loading = false;
    snprintf(app.far_note, sizeof app.far_note, "%s", why);
    set_status(true, "%s", why);
}

static void hist_go(int dir);
static void hist_go_from(int dir, const char *cur, Str cur_sel, bool back_to_cur);
static void go_up_from(const char *from);

// A link into a far drive, taken for a folder (F16, Michael 2026-11-09: no second look), was a file:
// back to the folder it is in, and the file opened from there.
static void far_link_file(void)
{
    char file[PATH_MAX];
    Err err;
    snprintf(file, sizeof file, "%s", app.cwd);
    hist_go(-1);
    if (spawn_open(file, &err)) set_status(false, "opened %.*s", STR_ARG(path_base(str_from_c(file))));
    else set_status(true, "%s", err.msg);
}

// A Back or Forward step onto a far folder that is gone (F16 step 3): skipped, as a local one is
// (hist_go), from the folder the step left; back there when there is nowhere further.
static void far_gone_step(int dir)
{
    char gone[PATH_MAX];
    static UiPlace was;   // (static: a UiPlace holds a path)
    snprintf(gone, sizeof gone, "%s", app.cwd);
    if (!hist_take(&hist, dir, &was)) return;
    hist_go_from(dir, was.dir, str_from_c(was.sel), true);
    set_status(true, "%s is gone: skipped", gone);
}

// The far folder's lister refused it, with errno e (0: not a refusal of its own, as a malformed
// answer): what that leads to by how ofx got there (app.far_then; F16 step 3), as a local folder's
// refusal would; else the reason stays in the list. (The errno is the lister's own word since the
// code-health review's step D: it was matched as strerror's text at the status line's end.)
static void far_refused(int e)
{
    FarThen then = app.far_then;   // (far_begin sets it again for the next listing)
    bool file = e == ENOTDIR, gone = e == ENOENT;
    if (file && then == FAR_THEN_OPEN) return far_link_file();
    if (file && then == FAR_THEN_SELECT) {   // (a path typed: its folder, it selected, in its place in the history)
        char up[PATH_MAX], name[NAME_MAX + 1];
        snprintf(name, sizeof name, "%.*s", STR_ARG(path_base(str_from_c(app.cwd))));
        path_parent(up, sizeof up, app.cwd);
        go_to_in(up, str_from_c(name), 0, (GoHow){ .stepping = true });
        return;
    }
    if (gone && (then == FAR_THEN_BACK || then == FAR_THEN_FORWARD)) return far_gone_step(then == FAR_THEN_BACK ? -1 : 1);
    if (gone && then == FAR_THEN_UP) return go_up_from(app.cwd);
    snprintf(app.far_note, sizeof app.far_note, "%s", app.status);
}

// Each frame, on a far folder: its listing taken when it has come (then the counts asked since),
// its lister started once a killed one is gone, and "isn't answering" said after FAR_ANSWER_MS.
// True when the window must draw again.
static bool far_poll(void)
{
    if (!app.far_on) return false;
    Err err;
    if (app.far_waiting && !far_busy(app.cwd)) {
        app.far_waiting = false;
        if (!far_start(app.exe, app.cwd, app.show_hidden, &err)) return far_failed(err.msg), true;
    }
    isize len;
    bool eof, full;
    far_said(&len, &eof, &full);
    Str b = { (char *)far_mem, len };
    FarMsg m;
    if (app.far_loading && !app.far_waiting) {
        int r;
        bool ended = false;
        while (!ended && (r = far_next(b, &far_scan, &m)) == 1) ended = m.type == 'L' || m.type == 'X';
        if (ended || r < 0) {
            int refused = ended && m.type == 'X' ? (int)m.v[0] : 0;
            // A reload kept its listing on screen: the cursor where it is now when it was moved
            // meanwhile (step 2a review), else on the name asked (a rename's new one, step 3b).
            char keep[NAME_MAX + 1];
            bool moved = app.far_kept && app.ui.list.sel != app.far_kept_sel;
            if (moved) snprintf(keep, sizeof keep, "%.*s", STR_ARG(sel_name()));
            Str select = moved ? str_from_c(keep) : str_from_c(app.far_select);
            bool ok = go_to_in(app.cwd, select, moved ? app.ui.list.sel : app.far_fallback, (GoHow){ .stepping = true, .far_take = true });
            app.far_loading = false;
            if (!ok) return far_refused(refused), true;
            if (app.far_slow) app.status[0] = 0;   // (its "isn't answering" said its last)
            app.far_note[0] = 0, app.far_slow = false;
            return true;
        }
        Str name = path_base(str_from_c(app.cwd));
        if (full) return far_failed(TextFormat("%.*s: too many entries for memory", STR_ARG(name))), true;
        if (eof) return far_failed(TextFormat("%.*s: its reader ended without an answer", STR_ARG(name))), true;
    }
    if (app.far_loading) {
        if (app.far_slow || now_ms() - app.far_since < FAR_ANSWER_MS) return false;
        app.far_slow = true;
        snprintf(app.far_note, sizeof app.far_note, "%.*s isn't answering", STR_ARG(far_who(app.cwd)));   // (its name, or its share's: the crumbs say where)
        set_status(true, "%s", app.far_note);
        return true;
    }
    bool any = false;
    while (far_next(b, &app.far_at, &m) == 1)
        if (m.type == 'C' && m.v[0] == app.far_cgen && m.v[1] < app.list.n) app.count[m.v[1]] = m.v[2], any = true;
    return any;
}

// Whether folder path on a far drive can be written to, never asking the drive (that would hold
// the window, F16 step 3b): the folder shown's lister's word for it and its folders, else yes (the
// action itself says if not). The menus' and a drop's one rule (the code-health review's step D: a
// drop onto a folder in a read-only far folder was taken while Paste Into there was greyed).
static bool far_writable(const char *path)
{
    char up[PATH_MAX];
    bool here = app.far_on && (strcmp(path, app.cwd) == 0 || (path_parent(up, sizeof up, path) && strcmp(up, app.cwd) == 0));
    return here ? !app.far_loading && app.far_can_write : true;
}

// Whether path (a folder) can be written to, for a menu's entries.
static bool can_write_at(const char *path)
{
    return far_dir(path) ? far_writable(path) : access(path, W_OK) == 0;
}

// Entry i's item count asked: of its folder's lister on a far folder (once its listing is shown),
// else of the count thread.
static bool row_count_ask(isize i, const char *path)
{
    if (app.list.e[i].far) return app.count[i] = -1, false;   // (a far link's target: never counted here, it would hold the count thread)
    if (app.far_on) return !app.far_loading && far_count_send(app.far_cgen, i, app.list.e[i].name);
    return count_ask(i, path, app.show_hidden);
}

// Shows dir, the cursor on the entry called `select` (else on row `fallback`). A move to another
// folder goes in the history, unless it is the history's own (`stepping`). False, with the reason
// in the status line, when dir can't be read: the old listing stays.
static bool go_to_in(const char *dir, Str select, isize fallback, GoHow how)
{
    int next = app.have_list ? 1 - app.cur : app.cur;
    arena_reset(&lists[next]);
    Listing l;
    Err err;
    UiView view = view_of(dir);
    bool same = strcmp(dir, app.cwd) == 0;
    bool on_far = view == UI_VIEW_FOLDER && far_dir(dir);
    // A far folder listed again (Ctrl+R, a change ofx made): a new lister, the listing shown kept
    // until its answer comes.
    if (on_far && !how.far_take && same && app.far_on && app.have_list) {
        if (far_begin(dir, select, fallback, how, &err)) return app.far_kept = true, app.far_kept_sel = app.ui.list.sel, true;
        return far_failed(err.msg), false;
    }
    // Another folder (F11): the one left keeps its cursor; this one's own view, size and sort, its
    // remembered cursor when none is asked for. All as they were if it can't be listed.
    bool grid = app.ui.grid;
    int zoom = app.ui.zoom;
    Sort sort = app.ui.sort;
    if (!same && app.have_list && fold_view_of(app.ui.view)) remember_cursor(app.cwd, sel_name());
    if (!same && fold_view_of(view)) {
        remember_apply(dir);
        const char *kept = remember_sel(dir);
        if (!select.len && fallback == 0 && kept) select = str_from_c(kept);
    }
    // A listing, and the arrays beside it: one way out when either fails, so f11b's folder that
    // can't be listed checks the restore for both (2026-11-04).
    Str *mime = NULL;
    u8 *mark = NULL;
    isize *count = NULL;
    bool ok = on_far ? far_list(dir, &lists[next], &l, select, fallback, how, &err) : view_list(view, &lists[next], dir, &l, &err);
    if (ok) {
        mime = arena_try_new(&lists[next], Str, l.n + 1);
        mark = arena_try_new(&lists[next], u8, l.n + 1);
        count = arena_try_new(&lists[next], isize, l.n + 1);
        if (!mime || !mark || !count) ok = false, err_set(&err, "%s: too many entries for memory", dir);
    }
    if (!ok) {
        app.ui.grid = grid, app.ui.zoom = zoom, app.ui.sort = sort;
        if (!app.have_list) snprintf(app.error, sizeof app.error, "%s", err.msg);
        set_status(true, "%s", err.msg);
        return false;
    }
    // The name to select lives in the old listing: find it before that arena is reused.
    isize sel = select.len ? listing_find(&l, select) : -1;
    if (sel < 0) sel = fallback;
    // The same folder again: the selection is kept by name.
    isize marked = 0;
    for (isize i = 0; same && app.have_list && app.ui.marked && i < app.list.n; i++) {
        if (!app.ui.mark[i]) continue;
        char key[PATH_MAX];
        isize k = listing_find(&l, entry_key(&app.list.e[i], key, sizeof key));
        if (k >= 0 && !mark[k]) mark[k] = 1, marked++;
    }
    // The folder left, with its cursor, for Back: its name is still in the old listing's arena.
    if (!same && app.have_list && !how.stepping) hist_leave(&hist, app.cwd, sel_name());
    app.cur = next, app.list = l, app.mime = mime, app.have_list = true, app.error[0] = 0, app.lists++;
    if (app.far_on && !on_far) far_drop(), app.far_loading = app.far_waiting = app.far_slow = false, app.far_note[0] = 0;   // (left: its lister killed)
    app.far_on = on_far;
    if (!same && app.ui.panel == UI_PANEL_BRANCHES) ui_picker_close(&app.ui, UI_PANEL_BRANCHES);   // (its list was the folder left's: 2026-11-03)
    if (!same && app.ui.menu_on && app.ui.menu == UI_CTX_BRANCH) app.ui.menu_on = false;
    if (app.ui.view == UI_VIEW_SEARCH && view != UI_VIEW_SEARCH) search_ask((SearchQuery){ .root = "", .skips = &app.conf.skip }), app.search_due = 0;   // (out of it: the walk stops)
    Str root, text = STR_EMPTY, inside = STR_EMPTY;
    if (view == UI_VIEW_SEARCH && !same) {   // (the fields show the address's text: ui_view_set)
        search_parse(dir, &root, &text, &inside);
        app.search_due = 0;
        snprintf(app.search_pick, sizeof app.search_pick, "%.*s", STR_ARG(select));
    }
    ui_view_set(&app.ui, view, same, text, inside);
    app.ui.can_write = view == UI_VIEW_FOLDER && (on_far ? app.far_can_write : access(dir, W_OK) == 0);   // the File menu's Create entries (F7.1); a far folder's lister says (F16)
    for (isize i = 0; i < l.n; i++) count[i] = COUNT_UNKNOWN;
    app.count = count;
    count_new_listing(), app.far_cgen++;
    app.here_local = !on_far && mounts_local(far_mounts(), view == UI_VIEW_SEARCH ? app.search_root : dir);   // (not here_dir: cwd is still the last one's; far through a link too, F16 step 3)
    ui_mark_set(&app.ui, mark);
    app.ui.marked = marked;
    if (!same) ui_edit_close(&app.ui);
    if (!same) {
        if (app.cwd[0]) app.status[0] = 0;   // what happened in the last folder (not at the start: what it said stays)
        snprintf(app.cwd, sizeof app.cwd, "%s", dir);
        crumbs_for(&app.crumbs, app.cwd);
        if (!far_share_uri(app.cwd, app.cwd_uri, sizeof app.cwd_uri)) app.cwd_uri[0] = 0;
        app.ui.list.top = 0;
        app.place_cur = places_current(&app.places, app.cwd);
        // OFX_TEST_WATCH_DELAY_MS widens the gap between the listing and the watch, so
        // tests/gui/watchgap.script can put a change in it.
        if (watch_delay_ms > 0) nanosleep(&(struct timespec){ watch_delay_ms / 1000, (watch_delay_ms % 1000) * 1000000L }, NULL);
        view_watch(view);
        // A change between the listing and the watch made no event: list once more (not a far
        // folder: it has no watch, and its listing hasn't come yet).
        if (!on_far) atomic_store(&watch.list_ready, true), glfwPostEmptyEvent();
        view_title(view);
    }
    ui_set(&app.ui.list, l.n, sel);
    // Each listing asks git (a change in the folder may be git's too; F12): a new folder at once, the
    // same one again at most once a second.
    if (view != UI_VIEW_FOLDER) git_ask(NULL);   // (a far folder: git_ask refuses it, F16)
    else if (!same) git_ask(app.cwd);
    else git_ask_soon();
    // The find bar stays open in another folder; Esc there goes back to where this one opened, not
    // to a place in the last one's list.
    if (!same) app.ui.find_from = app.ui.list.sel;
    // A full icon cache would leave every icon not in it blank until a theme change: emptied as
    // another folder is entered, where a frame holds none of its slots, it fills again with what is
    // on screen. (A reload of the same folder shows the same icons.)
    if (!same && app.nslots >= app.icons_max) icons_reset(), app.icon_full_resets++;
    tree_refresh(!same && view != UI_VIEW_SEARCH, app.ui.side.sel);
    // The drives' space moves with what changes on them; asked again with every listing.
    if (!same) app.cwd_avail = -1;
    space_ask(&app.places, app.cwd);
    return true;
}

// The search view's listing again, as more matches come (F9): the cursor stays on its entry, or
// goes to the one to pick when that comes.
static void search_relist(void)
{
    char keep[PATH_MAX];
    snprintf(keep, sizeof keep, "%.*s", STR_ARG(sel_name()));
    go_to_in(app.cwd, str_from_c(app.search_pick[0] ? app.search_pick : keep), app.ui.list.sel, (GoHow){ .stepping = true });
    if (app.search_done || (app.search_pick[0] && listing_find(&app.list, str_from_c(app.search_pick)) >= 0)) app.search_pick[0] = 0;
}

// The field's text, 150 ms after its last change (F9): searched for in place, the cursor on the
// first match (not a move in the history).
enum { SEARCH_TYPE_MS = 150 };

static void search_retype(void)
{
    char uri[PATH_MAX];
    Str inside = app.ui.search_inside ? (Str){ app.ui.inside.b, app.ui.inside.len } : STR_EMPTY;
    if (!search_uri(uri, sizeof uri, app.search_root, (Str){ app.ui.search.b, app.ui.search.len }, inside)) {
        set_status(true, "That search is too long");
        return;
    }
    snprintf(app.cwd, sizeof app.cwd, "%s", uri);
    app.search_pick[0] = 0;
    go_to_in(app.cwd, STR_EMPTY, 0, (GoHow){ .stepping = true });
}

// Ctrl+R in the search view: the same search again.
static void search_again(void)
{
    char keep[PATH_MAX];
    snprintf(keep, sizeof keep, "%.*s", STR_ARG(sel_name()));
    snprintf(app.search_pick, sizeof app.search_pick, "%s", keep);
    Str root, text, inside;
    if (search_parse(app.cwd, &root, &text, &inside)) search_ask(search_query(text, inside, app.search_root));
    search_relist();
}

// The tabs behind whose folder (or search) is on the drive at mp, which has gone: Home, their
// cursor at the top. The tab shown is the main loop's (app.drive_gone); its slot here is written
// over by tab_save when another tab is shown, so it needs no exception.
static void drive_gone_tabs(const char *mp)
{
    const char *home = getenv("HOME");
    for (int i = 0; home && home[0] && i < tabbar.n; i++) {
        Tab *t = &tabs[tabbar.slot[i]];
        Str root, text, inside;
        char r[PATH_MAX];
        const char *dir = t->dir;
        if (is_search(dir) && search_parse(dir, &root, &text, &inside) && root.len < PATH_MAX)
            snprintf(r, sizeof r, "%.*s", STR_ARG(root)), dir = r;
        if (!t->used || !path_within(dir, mp)) continue;
        snprintf(t->dir, sizeof t->dir, "%s", home);
        t->sel[0] = 0, t->row = 0, t->top = 0, t->marks_len = 0;
    }
}

// The places whose going sends the folders on them Home: a drive but /, and a share (F16 step 5:
// unmounted, by ofx or another program).
static bool place_gone_matters(PlaceKind k)
{
    return (place_is_drive(k) && k != PL_ROOT) || k == PL_SHARE;
}

// The drives in was (nwas, as they were before the places were read again) that have gone, whatever
// removed them (unmounted, ejected, pulled out) and before udisksctl answers: the tab shown, on one,
// goes Home when the main loop next comes round (Michael and the review, 2026-10-30: the folder's
// own watch reached the gone-folder fallback first, /run/media/<user>; Nemo crashes there), and so
// does every tab behind on it (Michael). A list with no / is a mount list read that failed
// (read_file's empty answer), not every drive gone at once: no tab moves for it (2026-11-01).
static void drive_gone_check(char (*was)[PATH_MAX], int nwas)
{
    bool read = false;
    for (int i = 0; i < app.places.n && !read; i++) read = app.places.p[i].kind == PL_ROOT;
    for (int k = 0; read && k < nwas; k++) {
        bool still = false;
        for (int i = 0; i < app.places.n && !still; i++) still = place_gone_matters(app.places.p[i].kind) && strcmp(app.places.p[i].path, was[k]) == 0;
        if (still) continue;
        if (path_within(here_dir(), was[k])) app.drive_gone = true;
        drive_gone_tabs(was[k]);
    }
}

// The places read again (at the start, a drive mounted or gone): the sidebar's cursor stays on its
// place, found by path and kind, else on the first row that isn't a heading; drive_gone_check.
static void places_reload(void)
{
    // A folder menu open on a place was resolved against these places (ctx_facts: its row, its
    // bookmark line); read again, its row may be another's, and then it closes (F8 review,
    // 2026-10-27: Nemo adding a bookmark above made Remove take the wrong line). The same place on
    // the same line keeps it: the echo of ofx's own bookmarks write closed a menu just opened
    // (f8c's flake, F13 code-health review, 2026-11-04).
    static Place menu_pl;   // (static: a Place holds its path)
    int menu_row = app.ui.menu_on && app.ui.menu == UI_CTX_FOLDER && app.ui.ctx_crumb < 0 && app.ui.side_mode == SIDE_PLACES ? app.ui.ctx_side : -1;
    if (menu_row >= app.places.n) menu_row = -1;
    if (menu_row >= 0) menu_pl = app.places.p[menu_row];
    Place keep = { .kind = PL_HEAD };   // in the tree mode the cursor is a tree row's: none kept
    if (app.ui.side_mode == SIDE_PLACES && app.ui.side.sel < app.places.n) keep = app.places.p[app.ui.side.sel];
    // The drives (not /), to see which are still there after.
    static char was[PLACES_MAX][PATH_MAX];
    int nwas = 0;
    for (int i = 0; i < app.places.n; i++)
        if (place_gone_matters(app.places.p[i].kind)) snprintf(was[nwas++], PATH_MAX, "%s", app.places.p[i].path);
    char bm[PATH_MAX];
    places_load(&scratch, &app.places, bookmarks_path(bm, sizeof bm), mountinfo_path(), sysblock_path(), udev_path());
    ArenaMark m = arena_mark(&scratch);
    static NetServer servers[NET_SERVERS_MAX];
    int nservers = disco_servers(servers);
    places_load_net(&scratch, &app.places, bm, watch_shares(&scratch), servers, nservers, app.ui.airplane);   // (F16 steps 5, 7b, 7d: after the drives)
    arena_restore(&scratch, m);
    // The Bookmarks menu's (F7.5). With Edit Bookmarks up its cursor stays on its line, found
    // again as a change finds it (bm_locate), wherever a change made elsewhere moved it; the lines
    // as they were are copied into bmwa, which no change holds by now (they reload after writing).
    static Bookmarks prev;
    int bm_keep = -1;
    if (app.ui.panel == UI_PANEL_BM && app.ui.bmp_list.sel < bmarks.n) {
        arena_reset(&bmwa);
        prev.n = bmarks.n, prev.full = bmarks.full;
        for (int i = 0; i < bmarks.n; i++) {
            prev.b[i] = bmarks.b[i];
            prev.b[i].uri = (Str){ arena_cstr(&bmwa, bmarks.b[i].uri), bmarks.b[i].uri.len };
            prev.b[i].label = (Str){ arena_cstr(&bmwa, bmarks.b[i].label), bmarks.b[i].label.len };
        }
        bm_keep = (int)app.ui.bmp_list.sel;
    }
    arena_reset(&bma);
    bm_parse(&bma, read_file(&bma, bm), &bmarks);
    if (app.ui.panel == UI_PANEL_BM) {
        int at = bm_keep >= 0 ? bm_locate(&bmarks, &prev, bm_keep) : -1;
        if (at < 0) at = (int)app.ui.bmp_list.sel;
        ui_set(&app.ui.bmp_list, bmarks.n, at >= bmarks.n ? bmarks.n - 1 : at);
    }
    if (menu_row >= 0) {
        const Place *now = menu_row < app.places.n ? &app.places.p[menu_row] : NULL;
        bool same = now && now->kind == menu_pl.kind && strcmp(now->path, menu_pl.path) == 0 &&
                    (now->kind != PL_BOOKMARK || bm_find(&bmarks, now->path) == app.ui.ctx_bmk);
        if (!same) app.ui.menu_on = false;
    }
    space_ask(&app.places, app.cwd);
    isize sel = 0;
    for (int i = 0; i < app.places.n; i++) {
        app.place_head[i] = app.places.p[i].kind == PL_HEAD;
        if (keep.kind != PL_HEAD && app.places.p[i].kind == keep.kind && strcmp(app.places.p[i].path, keep.path) == 0) sel = i;
    }
    app.place_cur = places_current(&app.places, app.cwd);
    drive_gone_check(was, nwas);
    if (app.ui.side_mode != SIDE_PLACES) return;
    app.ui.side_head = app.place_head;
    isize top = app.ui.side.top;
    ui_set(&app.ui.side, app.places.n, sel);
    app.ui.side.top = ui_clamp(top, 0, ui_max_top(&app.ui.side));
    ui_side_settle(&app.ui, 1);
}

// The menu's bookmark k: the k-th of the file's local ones (F7.5); NULL past them.
static const Bookmark *bm_menu(int k)
{
    for (int i = 0; i < bmarks.n; i++)
        if (bmarks.b[i].path[0] && k-- == 0) return &bmarks.b[i];
    return NULL;
}

// A bookmark's name in the menu: its label, else its folder's.
static Str bm_label(const Bookmark *b)
{
    return b->label.len ? b->label : strcmp(b->path, "/") == 0 ? S("/") : path_base(str_from_c(b->path));
}

// Bookmarks → Add Bookmark, Ctrl+D (F7.5): the folder shown (or dir, a folder's right-click menu,
// F8) at the end of the file, read again first so a change Nemo made meanwhile stays; the places
// (and the menu) read again after.
// The bookmarks file read for a change (its path into file): the lines as they are now, in bmwa;
// NULL, err said, when it can't be (bm_read). A change to them goes back with bm_commit.
static Bookmarks *bm_begin(char *file, size_t cap, Err *err)
{
    static Bookmarks now;
    bookmarks_path(file, cap);
    arena_reset(&bmwa);
    return bm_read(&bmwa, file, &now, err) ? &now : NULL;
}

// now (bm_begin's, changed) written back as file. Bookmark Add and Edit Bookmarks' changes (the F15
// code-health review: written twice, the length check two ways).
static bool bm_commit(const char *file, const Bookmarks *now, Err *err)
{
    Str text = bm_format(&bmwa, now);
    if (now->n > 0 && !text.len) return err_set(err, "the bookmarks are too long to write"), false;
    return bm_write(file, text, err);
}

static void bookmark_add(const char *dir)
{
    char file[PATH_MAX], uri[PATH_MAX], root[PATH_MAX], label[256];
    Err err = { 0 };
    // A folder in a share gvfs mounted: its address (far_share_uri), which connects when it isn't
    // mounted (F16 steps 5, 6, 7c; an AFP or WebDAV one is kept for other file managers, as Connect
    // doesn't take those yet); labelled with the share's name at its top, else the folder's (the sidebar shows the label).
    bool net = far_share_uri(dir, uri, sizeof uri) && far_share(dir, root, sizeof root, label, sizeof label);
    Str name = net && strcmp(dir, root) != 0 ? path_base(str_from_c(dir)) : str_from_c(label);
    Bookmarks *now = bm_begin(file, sizeof file, &err);
    if (!now || !(net ? bm_add_uri(&bmwa, now, uri, name, &err) : bm_add(&bmwa, now, dir, &err)) || !bm_commit(file, now, &err)) {
        set_status(true, "No bookmark: %s", err.msg);
        return;
    }
    watch_bookmarks();   // its folder may be new
    places_reload();
    set_status(false, "Bookmarked %.*s", STR_ARG(net ? name : path_base(str_from_c(dir))));
}

// The tree's open folders into the state file (tree=, F11), as they are now.
static void tree_remember(void)
{
    char path[PATH_MAX];
    Err err;
    ArenaMark mark = arena_mark(&scratch);
    size_t cap = tree_open_cap(&tree);   // (not nopen × PATH_MAX × 3: 128 open would pass the scratch arena; review, 2026-11-01)
    char *text = arena_try_push(&scratch, (isize)cap, 1);
    if (text && tree_open_text(&tree, text, cap) && state_path(path, sizeof path) && !state_put(&scratch, path, "tree", text, &err)) set_status(true, "%s", err.msg);
    arena_restore(&scratch, mark);
}

// The tree built again (tree mode only): opened down to the folder shown when `follow`, with the
// cursor on its row; else the cursor stays on its folder, or on the row `fallback`.
static void tree_refresh(bool follow, int fallback)
{
    if (app.ui.side_mode != SIDE_TREE) return;
    char keep[PATH_MAX] = "";
    if (!follow && app.ui.side.sel < tree.n) snprintf(keep, sizeof keep, "%.*s", STR_ARG(tree.row[app.ui.side.sel].path));
    if (follow) tree_follow(&tree, app.cwd);
    arena_reset(&treea);
    tree.mounts = mounts_parse(&treea, read_file(&treea, mountinfo_path()));
    tree_build(&treea, &tree, S("/"), app.show_hidden);
    int sel = tree_find(&tree, str_from_c(follow ? app.cwd : keep));
    if (sel < 0) sel = fallback < tree.n ? fallback : 0;
    app.ui.side_head = NULL;
    isize top = app.ui.side.top;
    ui_set(&app.ui.side, tree.n, sel);
    if (!follow) app.ui.side.top = ui_clamp(top, 0, ui_max_top(&app.ui.side)), ui_show_sel(&app.ui.side);
}

// The sidebar after a change of mode: the places' rows, or the tree's opened to the folder shown.
static void side_refresh(void)
{
    app.ui.side.top = 0;
    if (app.ui.side_mode == SIDE_PLACES) places_reload();
    else if (app.ui.side_mode == SIDE_TREE) tree_refresh(true, 0);
}

// The folder of sidebar row i: a place's, or a tree row's; NULL for a heading or none.
static const char *side_path(isize i, char *buf, size_t cap)
{
    if (app.ui.side_mode == SIDE_TREE) {
        if (i < 0 || i >= tree.n) return NULL;
        snprintf(buf, cap, "%.*s", STR_ARG(tree.row[i].path));
        return buf;
    }
    if (i < 0 || i >= app.places.n || app.places.p[i].kind == PL_HEAD || place_folderless(app.places.p[i].kind))
        return NULL;   // (a device, a network bookmark's address, Airplane Mode's row: no folder to open in a tab)
    return app.places.p[i].path;
}

static bool go_to(const char *dir, Str select, isize fallback)
{
    return go_to_in(dir, select, fallback, (GoHow){ 0 });
}

// go_to, and on a far folder its lister's refusal leads where `then` says (far_refused).
static bool go_to_then(const char *dir, Str select, isize fallback, FarThen then)
{
    return go_to_in(dir, select, fallback, (GoHow){ .then = then });
}

static void go_up_from(const char *from);

// The folder again (a change on disk): the cursor stays on its name, or on its row when that name
// is gone. A folder that is gone itself gives way to the nearest one above that is there.
static void reload(void)
{
    app.reloads++;
    view_changed();   // (what went since: trashed, deleted, moved, F9)
    char keep[PATH_MAX] = "";
    snprintf(keep, sizeof keep, "%.*s", STR_ARG(sel_name()));
    if (go_to_in(app.cwd, str_from_c(keep), app.ui.list.sel, (GoHow){ .then = FAR_THEN_UP, .reload = true })) return;   // (a far one: if its lister says it is gone, F16 step 3; not over another way's, far_begin)
    go_up_from(app.cwd);
}

// Folder `from` is gone: the nearest folder above it that is there, with a word in the status line.
static void go_up_from(const char *from)
{
    char gone[PATH_MAX], up[PATH_MAX];
    snprintf(gone, sizeof gone, "%s", from);
    snprintf(up, sizeof up, "%s", from);
    while (strcmp(up, "/") != 0) {
        // Cut in place: snprintf from up into up would copy a buffer onto itself.
        Str d = path_dir(str_from_c(up));
        if (d.len == 0) d = S("/");
        memmove(up, d.p, (size_t)d.len);
        up[d.len] = 0;
        if (go_to_then(up, STR_EMPTY, 0, FAR_THEN_UP)) {   // (on up from a far one gone too, F16 step 3)
            set_status(true, "%s is gone", gone);
            return;
        }
    }
}

static void go_parent(void)
{
    if (!view_can_up()) return;
    if (app.ui.view == UI_VIEW_SEARCH) {   // the folder searched (F9)
        go_to(app.search_root, STR_EMPTY, 0);
        return;
    }
    Str child = path_base(str_from_c(app.cwd));
    char child_buf[NAME_MAX + 1], up[PATH_MAX];
    snprintf(child_buf, sizeof child_buf, "%.*s", STR_ARG(child));
    path_parent(up, sizeof up, app.cwd);
    go_to(up, str_from_c(child_buf), 0);
}

static Str entry_mime(isize i);
static void ow_open(void);   // Open With (gui/openwith.c, F7.3)
static void ow_launch(isize i, bool def);
static void open_marked(void);

static void open_entry(isize i)
{
    const Entry *e = &app.list.e[i];
    char path[PATH_MAX];
    if (!entry_path(i, path, sizeof path)) {
        set_status(true, "%.*s: the path is too long", STR_ARG(e->name));
        return;
    }
    if (kind_is_dir(e->kind) && e->trash) {   // the trash view: a file opens, a folder doesn't
        set_status(true, "%.*s is in the trash: restore it to open it", STR_ARG(e->name));
        return;
    }
    if (e->far) {   // (F16) a far mount point, or a link into a far drive: its target, the far way
        char to[PATH_MAX];
        char real[PATH_MAX];   // (from the folder's real path, as the listing took it)
        bool reaches;
        if (e->kind == K_DIR) snprintf(to, sizeof to, "%s", path);
        else if (!far_path(here_dir(), real, sizeof real, &reaches) || !link_abs(real, e->target, to, sizeof to)) {
            set_status(true, "%.*s: its target is too long", STR_ARG(e->name));
            return;
        }
        go_to_then(to, STR_EMPTY, 0, e->kind == K_LINK_DIR ? FAR_THEN_OPEN : FAR_THEN_SHOW);   // (a link: a file after all is opened, far_refused)
        return;
    }
    if (kind_is_dir(e->kind)) {
        go_to(path, STR_EMPTY, 0);
        return;
    }
    if (e->kind == K_LINK_BROKEN) {
        set_status(true, "%.*s is a broken link", STR_ARG(e->name));
        return;
    }
    Err err;
    // An empty file: gio types it application/x-zerosize, for which no app is set, and refuses it.
    // Its name's type's default app instead (F7.1.1); none, and gio open says why.
    char desktop[PATH_MAX];
    if (e->size == 0 && (e->kind == K_FILE || e->kind == K_LINK_FILE) && apps_default(&scratch, entry_mime(i), desktop, sizeof desktop)) {
        if (spawn_launch(desktop, path, &err)) set_status(false, "opened %.*s", STR_ARG(e->name));
        else set_status(true, "%s", err.msg);
        return;
    }
    if (spawn_open(path, &err)) set_status(false, "opened %.*s", STR_ARG(e->name));
    else set_status(true, "%s", err.msg);
}

// Back (-1) or forward (1) through the history. A folder there that is gone now is dropped and
// the next one tried, with a word in the status line.
static void hist_go(int dir)
{
    char cur[PATH_MAX], cur_sel[NAME_MAX + 1];
    snprintf(cur, sizeof cur, "%s", app.cwd);
    snprintf(cur_sel, sizeof cur_sel, "%.*s", STR_ARG(sel_name()));
    hist_go_from(dir, cur, str_from_c(cur_sel), false);
}

// hist_go from cur (the cursor on cur_sel); back_to_cur: cur shown again when there is nowhere to
// go (far_gone_step: cur isn't the folder shown). A far folder stepped onto is skipped later, when
// its lister says it is gone (F16 step 3).
static void hist_go_from(int dir, const char *cur, Str cur_sel, bool back_to_cur)
{
    static UiPlace to;   // (static: a UiPlace holds a path)
    while (hist_step(&hist, dir, cur, cur_sel, &to)) {
        if (go_to_in(to.dir, str_from_c(to.sel), 0, (GoHow){ .stepping = true, .then = dir < 0 ? FAR_THEN_BACK : FAR_THEN_FORWARD })) return;
        hist_unstep(&hist, dir);
        set_status(true, "%s is gone: skipped", to.dir);
    }
    if (back_to_cur) go_to_in(cur, cur_sel, 0, (GoHow){ .stepping = true });
}

// The listing in a new order, the cursor kept on its entry. The MIME types, cached by row, go.
static void resort(void)
{
    char keep[PATH_MAX];
    snprintf(keep, sizeof keep, "%.*s", STR_ARG(sel_name()));
    fs_sort(&app.list, app.ui.sort);
    app.lists++;
    memset(app.mime, 0, sizeof *app.mime * (size_t)app.list.n);
    // The counts are by row too: asked again in the new order.
    for (isize i = 0; i < app.list.n; i++) app.count[i] = COUNT_UNKNOWN;
    count_new_listing(), app.far_cgen++;
    isize sel = listing_find(&app.list, str_from_c(keep));
    ui_set(&app.ui.list, app.list.n, sel >= 0 ? sel : 0);
}
