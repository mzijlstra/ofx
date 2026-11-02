// The modal panels (F5.1): the questions, the clash, Properties (F7.2); Open With, Edit Bookmarks,
// Keyboard Shortcuts and About come in from their own files. Included by main.c.

// ---- the modal panels (F5.1) --------------------------------------------------------------------

typedef enum { MODAL_NONE, MODAL_DELETE, MODAL_CLASH, MODAL_CANCEL, MODAL_QUIT, MODAL_FULL, MODAL_EMPTY, MODAL_PROPS, MODAL_OW, MODAL_BM, MODAL_KEYS, MODAL_ABOUT, MODAL_BAR, MODAL_RESTORE } ModalKind;

// The panel on top, as ui_on_top has it.
static ModalKind modal_kind(void)
{
    switch (ui_on_top(&app.ui)) {   // (ui/'s order, which takes the keys)
    case UI_ON_FULL: return MODAL_FULL;
    case UI_ON_QUIT: return MODAL_QUIT;
    case UI_ON_CANCEL: return MODAL_CANCEL;
    case UI_ON_DELETE: return MODAL_DELETE;
    case UI_ON_EMPTY: return MODAL_EMPTY;
    case UI_ON_RESTORE: return MODAL_RESTORE;
    case UI_ON_CLASH: return jobs.asking >= 0 ? MODAL_CLASH : MODAL_NONE;
    case UI_ON_PROPS: return MODAL_PROPS;
    case UI_ON_OW: return MODAL_OW;
    case UI_ON_BM: return MODAL_BM;
    case UI_ON_KEYS: return MODAL_KEYS;
    case UI_ON_ABOUT: return MODAL_ABOUT;
    case UI_ON_BAR: return MODAL_BAR;
    case UI_ON_NONE: break;
    }
    return MODAL_NONE;
}

static JobRec *job_by_id(int id)
{
    for (int i = 0; i < jobs.nrun; i++)
        if (job_recs[jobs.run[i]].id == id) return &job_recs[jobs.run[i]];
    return NULL;
}

// A panel's buttons' element ids, by UI_MODAL_*: drawn and hit-tested by the same (2026-10-28).
// modal-all is the clash panel's "same for the rest" row.
static const char *const MODAL_IDS[] = {
    [UI_MODAL_YES] = "modal-yes",       [UI_MODAL_NO] = "modal-no",         [UI_MODAL_SKIP] = "modal-skip", [UI_MODAL_REPLACE] = "modal-replace",
    [UI_MODAL_KEEP] = "modal-keep",     [UI_MODAL_CANCEL] = "modal-cancel", [UI_MODAL_ALL] = "modal-all",   [UI_MODAL_OK] = "modal-ok",
};

// A panel's button; the main one (Enter's) filled, in red when it destroys something.
static void modal_button(int which, const char *label, bool main_one, bool danger)
{
    Theme *t = &app.theme;
    bool hover = app.hov.modal == which;
    Rgb fill = danger ? t->red : t->accent;
    Clay_Color bg = main_one ? (hover ? mix(fill, t->bg_light, 0.85f) : cc(fill)) : hover ? cc(t->bg) : cc(t->bg_light);
    CLAY(Clay_GetElementId(cz(MODAL_IDS[which])), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(32) }, .padding = { 16, 16, 0, 0 },
                                                  .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                      .backgroundColor = bg,
                                      .border = { .color = main_one ? cc(fill) : rule_color(), .width = CLAY_BORDER_OUTSIDE(1) } }) {
        text(cz(label), main_one ? cc(t->bg) : cc(t->fg));
    }
}

// The panel itself, in the window's middle over the rest.
static void layout_modal_panel(ModalKind k)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("modal"), { .layout = { .sizing = { CLAY_SIZING_FIXED(480), CLAY_SIZING_FIT(0) }, .padding = { 24, 24, 20, 20 }, .childGap = 10,
                                         .layoutDirection = CLAY_TOP_TO_BOTTOM },
                             .backgroundColor = cc(t->bg_light),
                             .border = { .color = k == MODAL_DELETE || k == MODAL_CANCEL || k == MODAL_QUIT || k == MODAL_EMPTY ? cc(t->red) : cc(t->accent), .width = CLAY_BORDER_OUTSIDE(1) },
                             .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 20,
                                           .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER } } }) {
        if (k == MODAL_FULL || k == MODAL_QUIT || k == MODAL_CANCEL) {
            const char *line, *hint;
            if (k == MODAL_FULL) {
                line = "No more jobs can be queued right now. Wait until one finishes.";
                hint = "Enter or Esc closes this";
            } else if (k == MODAL_QUIT) {
                line = arena_sprintf(&frame, "%d %s running and %d waiting. Quit and cancel them?", jobs.nrun, jobs.nrun == 1 ? "job is" : "jobs are", jobs.nwait);
                if (jobs.nwait == 0) line = arena_sprintf(&frame, "%d %s running. Quit and cancel %s?", jobs.nrun, jobs.nrun == 1 ? "job is" : "jobs are", jobs.nrun == 1 ? "it" : "them");
                hint = "Enter or y quits \xC2\xB7 n or Esc stays";
            } else {
                JobRec *r = job_by_id(app.ui.cancel_id);
                Str label = r ? str_from_c(r->label) : S("this job");
                // "Copying 12 items to bdir" asked as "Cancel copying 12 items to bdir?"
                char lower[sizeof r->label];
                snprintf(lower, sizeof lower, "%.*s", STR_ARG(label));
                if (lower[0] >= 'A' && lower[0] <= 'Z') lower[0] = (char)(lower[0] - 'A' + 'a');
                line = arena_sprintf(&frame, "Cancel %s?", lower);
                hint = "Enter or y cancels it \xC2\xB7 n or Esc keeps it going";
            }
            text(fit(str_from_c(line), 430, false), cc(t->fg));
            text(cz(hint), cc(t->fg_dim));
            CLAY(CLAY_ID("modal-buttons"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 10,
                                                         .childAlignment = { .x = CLAY_ALIGN_X_RIGHT }, .padding = { 0, 0, 8, 0 } } }) {
                if (k == MODAL_FULL) {
                    modal_button(UI_MODAL_OK, "OK", true, false);
                } else {
                    modal_button(UI_MODAL_NO, k == MODAL_QUIT ? "Stay" : "Keep going", false, false);
                    modal_button(UI_MODAL_YES, k == MODAL_QUIT ? "Quit" : "Cancel job", true, true);
                }
            }
        } else if (k == MODAL_RESTORE) {   // (F11 step 3)
            int n = app.ui.restore_n;
            text(cz(n == 1 ? "Bring back the other window?" : arena_sprintf(&frame, "Bring back the other %d windows?", n)), cc(t->fg));
            const char *why = restore.crashed ? (n == 1 ? "ofx didn't close it properly last time." : "ofx didn't close them all properly last time.")
                                              : (n == 1 ? "Close All (Ctrl+Q) closed it, with its tabs." : "Close All (Ctrl+Q) closed them, with their tabs.");
            text(fit(str_from_c(why), 430, false), cc(t->fg_dim));
            text(cz(n == 1 ? "Enter or y brings it back \xC2\xB7 n or Esc forgets it" : "Enter or y brings them back \xC2\xB7 n or Esc forgets them"), cc(t->fg_dim));
            CLAY(CLAY_ID("modal-buttons"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 10,
                                                         .childAlignment = { .x = CLAY_ALIGN_X_RIGHT }, .padding = { 0, 0, 8, 0 } } }) {
                modal_button(UI_MODAL_NO, n == 1 ? "Forget it" : "Forget them", false, false);
                modal_button(UI_MODAL_YES, "Bring back", true, true);
            }
        } else if (k == MODAL_EMPTY) {
            isize all = app.empty_n;   // (the trash folders' items, counted as it opened: any view)
            text(cz("Empty the trash?"), cc(t->fg));
            text(cz(all < 0 ? "Everything in it is deleted for good." : arena_sprintf(&frame, "All %td %s in it %s deleted for good.", all, items(all), all == 1 ? "is" : "are")),
                 cc(t->fg_dim));
            text(cz("This can't be undone."), cc(t->red));
            text(cz("Enter or y empties it \xC2\xB7 n or Esc cancels"), cc(t->fg_dim));
            CLAY(CLAY_ID("modal-buttons"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 10,
                                                         .childAlignment = { .x = CLAY_ALIGN_X_RIGHT }, .padding = { 0, 0, 8, 0 } } }) {
                modal_button(UI_MODAL_NO, "Cancel", false, false);
                modal_button(UI_MODAL_YES, "Empty Trash", true, true);
            }
        } else if (k == MODAL_DELETE) {
            text(cz(arena_sprintf(&frame, "Delete %td %s for good?", app.ui.confirm_n, items(app.ui.confirm_n))), cc(t->fg));
            CLAY(CLAY_ID("modal-names"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2, .padding = { 12, 0, 4, 4 } } }) {
                isize shown = doomed.n > 8 ? 7 : doomed.n;
                for (isize i = 0; i < shown; i++) text(fit(path_base(str_from_c(doomed.path[i])), 420, false), cc(t->fg_dim));
                if (doomed.n > shown) text(cz(arena_sprintf(&frame, "and %td more", doomed.n - shown)), cc(t->fg_dim));
            }
            text(cz("This can't be undone."), cc(t->red));
            text(cz("Enter or y deletes \xC2\xB7 n or Esc cancels"), cc(t->fg_dim));
            CLAY(CLAY_ID("modal-buttons"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 10,
                                                         .childAlignment = { .x = CLAY_ALIGN_X_RIGHT }, .padding = { 0, 0, 8, 0 } } }) {
                modal_button(UI_MODAL_NO, "Cancel", false, false);
                modal_button(UI_MODAL_YES, "Delete", true, true);
            }
        } else {
            // r->clash is safe to read here without jobs.mu: asking is set only after jobs_poll saw
            // the worker waiting (under the lock), and cleared before job_answer lets it go on.
            JobRec *r = &job_recs[jobs.asking];
            text(fit(str_from_c(r->label), 430, false), cc(t->fg_dim));   // which job asks (two may)
            Str name = path_base(str_from_c(r->clash)), dir = path_dir(str_from_c(r->clash));
            Str in_dir = path_base(dir.len ? dir : S("/"));
            text(fit(str_from_c(arena_sprintf(&frame, "%.*s already exists in %.*s", STR_ARG(name), STR_ARG(in_dir))), 430, false), cc(t->fg));
            text(cz(r->clash_dirs ? "Merge the folders, keep both, or skip it?" : "Replace it, keep both, or skip it?"), cc(t->fg_dim));
            CLAY(Clay_GetElementId(cz(MODAL_IDS[UI_MODAL_ALL])), { .layout = { .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }, .padding = { 0, 0, 4, 4 } } }) {
                CLAY(CLAY_ID("modal-all-box"), { .layout = { .sizing = { CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(16) } },
                                                 .backgroundColor = app.ui.ask_all ? cc(t->accent) : cc(t->bg),
                                                 .border = { .color = cc(t->fg_dim), .width = CLAY_BORDER_OUTSIDE(1) } }) {}
                text(cz("Do the same for the rest"), app.hov.modal == UI_MODAL_ALL ? cc(t->fg) : cc(t->fg_dim));
            }
            text(cz(r->clash_dirs ? "s skip \xC2\xB7 r merge \xC2\xB7 k keep both \xC2\xB7 Shift: for the rest \xC2\xB7 Esc cancels"
                                    : "s skip \xC2\xB7 r replace \xC2\xB7 k keep both \xC2\xB7 Shift: for the rest \xC2\xB7 Esc cancels"),
                 cc(t->fg_dim));
            CLAY(CLAY_ID("modal-buttons"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 10,
                                                         .childAlignment = { .x = CLAY_ALIGN_X_RIGHT }, .padding = { 0, 0, 8, 0 } } }) {
                modal_button(UI_MODAL_CANCEL, "Cancel", false, false);
                modal_button(UI_MODAL_SKIP, "Skip", false, false);
                modal_button(UI_MODAL_KEEP, "Keep both", false, false);
                modal_button(UI_MODAL_REPLACE, r->clash_dirs ? "Merge" : "Replace", false, false);   // no default: Enter doesn't answer
            }
        }
    }
}

// ---- the Properties panel (F7.2) ---------------------------------------------------------------
//
// One entry's details and permissions, or several entries' counts (docs/plan.md, "F7.2"). What it
// shows is read when it opens (props_open), and again after a permission change; a folder's
// contents are counted on walk.c's thread.

static u8    props_mem[MiB(2)];
static Arena propsa;
static char *props_path[WALK_ROOTS_MAX];   // in propsa

typedef struct {
    isize       n;          // the entries shown
    bool        cut;        // more were selected than it counts (WALK_ROOTS_MAX)
    isize       idx;        // the one entry's place in the list, -1 for the folder itself
    char        name[NAME_MAX + 1];
    char        where[PATH_MAX];   // its folder (or theirs), "~" for home
    char        drive[256];
    char        link[PATH_MAX];    // a link's target, "" for none
    bool        have;       // st holds it (a link: its target's; a broken one: the link's)
    bool        broken, walking, local;
    struct stat st;
    i64         btime;      // its creation (statx), -1 when the file system doesn't say
    char        owner[64], group[64];
    char        type[128], type_name[128];   // one entry's MIME type, as gio has it, and its words
    bool        type_by_name;                // the type from its name only (gio didn't say)
    DuSum       sum;
    bool        done;
} Props;

static Props props;

// t as 2026-10-25 14:03:12, local time.
static void props_when(char *buf, size_t cap, i64 t)
{
    struct tm tm;
    time_t tt = (time_t)t;
    if (!localtime_r(&tt, &tm) || !strftime(buf, cap, "%Y-%m-%d %H:%M:%S", &tm)) snprintf(buf, cap, "?");
}

// n with its thousands apart: 2,345,678.
static void props_digits(char *buf, size_t cap, i64 n)
{
    char d[32];
    int len = snprintf(d, sizeof d, "%lld", (long long)(n < 0 ? 0 : n)), o = 0;
    for (int i = 0; i < len && o + 2 < (int)cap; i++) {
        if (i && (len - i) % 3 == 0) buf[o++] = ',';
        buf[o++] = d[i];
    }
    buf[o] = 0;
}

// The drive holding path: the drive place with the longest path above it.
static const char *props_drive(const char *path)
{
    int best = -1;
    size_t bn = 0;
    for (int i = 0; i < app.places.n; i++) {
        const Place *p = &app.places.p[i];
        size_t n = strlen(p->path);
        if (place_is_drive(p->kind) && n && path_within(path, p->path) && n >= bn) best = i, bn = n;
    }
    return best >= 0 ? app.places.p[best].label : "-";
}

// The one entry's details, read again.
static void props_read(void)
{
    const char *path = props_path[0];
    struct stat lst;
    props.have = lstat(path, &lst) == 0;
    props.link[0] = 0, props.broken = false, props.btime = -1;
    if (!props.have) return;
    props.st = lst;
    if (S_ISLNK(lst.st_mode)) {
        ssize_t k = readlink(path, props.link, sizeof props.link - 1);
        props.link[k > 0 ? k : 0] = 0;
        props.broken = stat(path, &props.st) != 0;
        if (props.broken) props.st = lst;
    }
    struct statx sx;
    if (statx(AT_FDCWD, path, props.broken ? AT_SYMLINK_NOFOLLOW : 0, STATX_BTIME, &sx) == 0 && (sx.stx_mask & STATX_BTIME))
        props.btime = (i64)sx.stx_btime.tv_sec;
    snprintf(props.owner, sizeof props.owner, "%s", id_name(false, props.st.st_uid));   // (the list's columns' names, cached)
    snprintf(props.group, sizeof props.group, "%s", id_name(true, props.st.st_gid));
    // Its permissions change only when it is Michael's, not in the trash, and there (one entry:
    // props_read is only for one).
    app.ui.prop_can = app.ui.view != UI_VIEW_TRASH && !props.broken && props.st.st_uid == geteuid();
}

// The panel opened (ui_props_open): the marked entries, or the cursor's; the folder itself when it
// is empty; or path (a right-click menu's, F8: the folder shown, or a folder elsewhere). A folder
// among them is counted all the way down, on a local drive only.
static void props_open(const char *path)
{
    arena_reset(&propsa);
    props = (Props){ .idx = -1, .btime = -1 };
    bool here = path != NULL;
    isize n = !here && app.have_list && app.list.n > 0 ? targets(&propsa, props_path, WALK_ROOTS_MAX) : 0;
    if (here) props_path[0] = arena_cstr(&propsa, str_from_c(path)), n = 1;
    props.cut = !here && app.ui.marked > WALK_ROOTS_MAX;
    if (n == 0 && app.ui.view != UI_VIEW_TRASH && app.ui.view != UI_VIEW_SEARCH) props_path[0] = arena_cstr(&propsa, str_from_c(app.cwd)), n = 1;
    props.n = n;
    app.ui.prop_can = false;
    if (n == 0) {   // the trash view, empty
        snprintf(props.name, sizeof props.name, "Trash");
        snprintf(props.where, sizeof props.where, "-");
        return;
    }
    if (!here && app.have_list && app.list.n > 0 && n == 1)
        for (isize i = 0; i < app.list.n; i++)
            if (app.ui.marked ? app.ui.mark[i] : i == app.ui.list.sel) props.idx = i;
    Str base = path_base(str_from_c(props_path[0]));
    char d[PATH_MAX];
    path_parent(d, sizeof d, props_path[0]);
    if (n == 1) {
        snprintf(props.name, sizeof props.name, "%.*s", STR_ARG(base.len ? base : S("/")));
        if (app.ui.view == UI_VIEW_TRASH && props.idx >= 0) snprintf(props.name, sizeof props.name, "%.*s", STR_ARG(app.list.e[props.idx].name));
        // The type gio opens it as, its content read (Michael, 2026-10-26); on a network drive,
        // whose reads can hang (F6.4), or when gio doesn't say, the name's.
        props.type_by_name = !app.thumb_local || !spawn_gio_type(props_path[0], props.type, sizeof props.type, 1000);
        if (props.type_by_name) {
            Str type = props.idx >= 0 ? entry_mime(props.idx) : S("inode/directory");
            snprintf(props.type, sizeof props.type, "%.*s", STR_ARG(type));
        }
        mime_comment(&scratch, str_from_c(props.type), props.type_name, sizeof props.type_name);
    }
    if (app.ui.view == UI_VIEW_TRASH) snprintf(props.where, sizeof props.where, "Trash");
    else ui_tilde(props.where, sizeof props.where, n == 1 ? d : here_dir(), getenv("HOME"));
    snprintf(props.drive, sizeof props.drive, "%s", props_drive(props_path[0]));
    if (n == 1) props_read();
    // Counted all the way down, but not on a network drive (its reads can hang, F6.4).
    props.local = app.thumb_local;
    bool any_dir = false;
    for (isize i = 0; i < n; i++) {
        struct stat st;
        any_dir |= lstat(props_path[i], &st) == 0 && S_ISDIR(st.st_mode);
    }
    props.walking = props.local;
    if (props.local) walk_ask(props_path, n);
    else if (!any_dir) {   // files on a network drive: the listing's sizes
        for (isize i = 0; i < app.list.n; i++)
            if (app.ui.marked ? app.ui.mark[i] : i == app.ui.list.sel) props.sum.files++, props.sum.bytes += app.list.e[i].size;
        props.done = true;
    }
}

// The grid's place k flipped (UA_CHMOD): applied at once, Ctrl+Z puts it back (F7.2).
static void props_chmod(int k)
{
    if (!app.ui.prop_can || props.n != 1 || k < 0 || k > 8) return;
    mode_t old, perms = (props.st.st_mode & 0777) ^ perm_bit(k);
    Err err;
    if (jobs_refuse(props_path[0])) return;
    if (!op_chmod(props_path[0], perms, &old, &err)) {
        set_status(true, "%s", err.msg);
        props_read();
        return;
    }
    char was[64];   // the old mode, and the file's device and inode (UNDO_CHMOD)
    snprintf(was, sizeof was, "%o %llu %llu", (unsigned)old, (unsigned long long)props.st.st_dev, (unsigned long long)props.st.st_ino);
    undo_start(UNDO_CHMOD), undo_add(&undo, NULL, props_path[0], was);
    props_read();
    set_status(false, "%s: %04o", props.name, (unsigned)(props.st.st_mode & 07777));
}

// A label and its value, one line of the panel.
static void props_line(const char *label, const char *value)
{
    Theme *t = &app.theme;
    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 12 } }) {
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(96), CLAY_SIZING_FIT(0) } } }) { text(cz(label), cc(t->fg_dim)); }
        text(fit(str_from_c(value), 360, false), cc(t->fg));
    }
}

// The sums as "12 folders, 340 files, 1.2 GiB", "counting…" while the walk runs.
static const char *props_counts(bool less_root)
{
    char sz[32], b[32];
    ui_size(sz, sizeof sz, props.sum.bytes);
    props_digits(b, sizeof b, props.sum.bytes);
    i64 dirs = props.sum.dirs - (less_root && props.sum.dirs > 0);
    if (!props.local && !props.done) return "not counted on a network drive";
    return arena_sprintf(&frame, "%lld %s, %lld %s, %s (%s bytes)%s%s", (long long)dirs, dirs == 1 ? "folder" : "folders",
                         (long long)props.sum.files, props.sum.files == 1 ? "file" : "files", sz, b,
                         props.done ? "" : " \xE2\x80\xA6 counting", props.sum.cut || props.cut ? " (some unreadable)" : "");
}

// The hard links among the sums, when there are any (Michael, 2026-10-26): "5 names of 2 files;
// 1.1 GiB (… bytes) counting each once". NULL when there are none.
static const char *props_links(void)
{
    if (props.sum.linked_names == 0) return NULL;
    char sz[32], b[32];
    ui_size(sz, sizeof sz, props.sum.once_bytes);
    props_digits(b, sizeof b, props.sum.once_bytes);
    return arena_sprintf(&frame, "%lld names of %lld %s; %s%s (%s bytes) counting each once", (long long)props.sum.linked_names,
                         (long long)props.sum.linked_files, props.sum.linked_files == 1 ? "file" : "files",
                         props.sum.once_over ? "at most " : "", sz, b);
}

static void layout_props_panel(void)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("props"), { .layout = { .sizing = { CLAY_SIZING_FIXED(560), CLAY_SIZING_FIT(0) }, .padding = { 24, 24, 20, 20 }, .childGap = 8,
                                         .layoutDirection = CLAY_TOP_TO_BOTTOM },
                             .backgroundColor = cc(t->bg_light), .border = { .color = cc(t->accent), .width = CLAY_BORDER_OUTSIDE(1) },
                             .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 20,
                                           .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER } } }) {
        // The icon (or preview) and the name, or the count of several.
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                                   .padding = { 0, 0, 0, 8 } } }) {
            Texture2D *th = props.idx >= 0 ? entry_thumb(props.idx, true) : NULL;
            if (th) {
                float sc = 48.0f / (float)(th->width > th->height ? th->width : th->height);
                CLAY(CLAY_ID("props-thumb"), { .layout = { .sizing = { CLAY_SIZING_FIXED(th->width * sc), CLAY_SIZING_FIXED(th->height * sc) } }, .image = { .imageData = th } }) {}
            } else {
                IconSlot *ic = props.idx >= 0 ? entry_icon(props.idx) : named_icon(app.ui.view == UI_VIEW_TRASH ? "user-trash" : "folder");
                icon_box(CLAY_ID("props-icon"), ic, 48);
            }
            const char *title = props.n == 1 ? props.name : arena_sprintf(&frame, "%td selected%s", props.n, props.cut ? " (the first counted)" : "");
            text(fit(str_from_c(title), 420, false), cc(t->fg));
        }
        if (props.n == 0) {
            props_line("Contents", "empty");
        } else if (props.n > 1) {
            props_line("Contents", props_counts(false));
            if (props_links()) props_line("Hard links", props_links());
            props_line("Location", props.where);
            props_line("Drive", props.drive);
        } else {
            char when[64], sz[32], b[32];
            props_line("MIME type", arena_sprintf(&frame, "%s%s%s%s%s", props.type, props.type_name[0] ? " (" : "", props.type_name,
                                                  props.type_name[0] ? ")" : "", props.type_by_name ? ", by its name" : ""));
            if (props.have && S_ISDIR(props.st.st_mode)) {
                props_line("Contents", props_counts(true));
                if (props_links()) props_line("Hard links", props_links());
            } else if (props.have) {
                ui_size(sz, sizeof sz, props.st.st_size), props_digits(b, sizeof b, props.st.st_size);
                props_line("Size", arena_sprintf(&frame, "%s bytes (%s)", b, sz));
                if (props.st.st_nlink > 1) props_line("Hard links", arena_sprintf(&frame, "%llu names", (unsigned long long)props.st.st_nlink));
            }
            props_line("Location", props.where);
            props_line("Drive", props.drive);
            if (props.link[0]) props_line("Link to", arena_sprintf(&frame, "%s%s", props.link, props.broken ? " (not there)" : ""));
            if (props.have) {
                props_when(when, sizeof when, props.st.st_mtim.tv_sec), props_line("Modified", arena_sprintf(&frame, "%s", when));
                props_when(when, sizeof when, props.st.st_atim.tv_sec), props_line("Accessed", arena_sprintf(&frame, "%s", when));
                if (props.btime >= 0) props_when(when, sizeof when, props.btime), props_line("Created", arena_sprintf(&frame, "%s", when));
                // Owner and group, each with its label (Michael, 2026-10-25: "group" read as a value).
                CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 12 } }) {
                    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(96), CLAY_SIZING_FIT(0) } } }) { text(cz("Owner"), cc(t->fg_dim)); }
                    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(150), CLAY_SIZING_FIT(0) } } }) { text(fit(str_from_c(props.owner), 150, false), cc(t->fg)); }
                    text(cz("Group"), cc(t->fg_dim));
                    text(fit(str_from_c(props.group), 150, false), cc(t->fg));
                }
                // The permissions: the octal number, then the grid; greyed when they can't change.
                bool can = app.ui.prop_can;
                Clay_Color off = rule_color();
                CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 12, .padding = { 0, 0, 8, 0 } } }) {
                    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(96), CLAY_SIZING_FIT(0) } } }) { text(cz("Permissions"), cc(t->fg_dim)); }
                    text(cz(arena_sprintf(&frame, "%04o%s", (unsigned)(props.st.st_mode & 07777), can ? "" : app.ui.view == UI_VIEW_TRASH ? "  (in the trash)" : "  (not yours to change)")), cc(t->fg));
                }
                static const char *const WHO[] = { "Owner", "Group", "Others" }, *const WHAT[] = { "Read", "Write", "Execute" };
                CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = { 108, 0, 0, 0 } } }) {
                    for (int c = 0; c < 3; c++)
                        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(80), CLAY_SIZING_FIT(0) } } }) { text(cz(WHAT[c]), cc(t->fg_dim)); }
                }
                for (int r = 0; r < 3; r++) {
                    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(108), CLAY_SIZING_FIT(0) } } }) { text(cz(WHO[r]), can ? cc(t->fg) : off); }
                        for (int c = 0; c < 3; c++) {
                            int k = r * 3 + c;
                            bool on = (props.st.st_mode & perm_bit(k)) != 0, focus = can && app.ui.prop_at == k;
                            CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(80), CLAY_SIZING_FIT(0) } } }) {
                                // The focus ring around the box, then the box: filled when on.
                                CLAY_AUTO_ID({ .layout = { .padding = { 3, 3, 3, 3 } }, .cornerRadius = CLAY_CORNER_RADIUS(4),
                                               .border = { .color = focus ? cc(t->accent) : cc(t->bg_light), .width = CLAY_BORDER_OUTSIDE(2) } }) {
                                    CLAY(CLAY_IDI("prop-box", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(18), CLAY_SIZING_FIXED(18) },
                                                                               .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                                                   .backgroundColor = on ? (can ? cc(t->accent) : off) : cc(t->bg),
                                                                   .cornerRadius = CLAY_CORNER_RADIUS(3),
                                                                   .border = { .color = can ? cc(t->fg_dim) : off, .width = CLAY_BORDER_OUTSIDE(1) } }) {
                                        if (on) text(cz("\xE2\x9C\x93"), cc(t->bg));   // ✓
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
                props_line("", "can't be read now");
            }
        }
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = { 0, 0, 10, 0 },
                                   .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
            modal_button(UI_MODAL_OK, "Close", true, false);
        }
    }
}

#include "openwith.c"
#include "bookmarks.c"
#include "shortcuts.c"
#include "about.c"

// The panel over the window, the rest darkened but still readable (Michael's pick over a blur,
// 2026-10-21).
static void layout_modal(ModalKind k, float w, float h)
{
    CLAY(CLAY_ID("modal-backdrop"), { .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h) } },
                                      .backgroundColor = { 0, 0, 0, 150 },
                                      .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 10 } }) {}
    if (k == MODAL_PROPS) layout_props_panel();
    else if (k == MODAL_OW) layout_ow_panel();
    else if (k == MODAL_BM) layout_bmp_panel();
    else if (k == MODAL_KEYS) layout_keys_panel();
    else if (k == MODAL_ABOUT) layout_about_panel();
    else if (k == MODAL_BAR) layout_bar_panel();
    else layout_modal_panel(k);
}
