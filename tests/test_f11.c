// F11 step 1 (docs/plan.md "F11 design"): what ofx remembers per folder (fs/folders.c): the lines
// read and written back, two windows' changes both kept, the housekeeping (nothing said, gone, the
// cap; a folder on a drive that is out or on a network mount kept), the wipe; and ofx.conf's view
// defaults. Included by test.c after test_f10.c.

static u8 folders_test_mem[2][MiB(4)];

static void test_folders(void)
{
    Arena a[2];
    for (int i = 0; i < 2; i++) arena_init(&a[i], "folders-test", folders_test_mem[i], sizeof folders_test_mem[i]);
    static Folders t, w;
    // A line each: fields or "-", the path and the cursor %-encoded; bad lines left out.
    folders_parse(&a[0], S("sidebar=tree\nfolder /home/m/a%20b 100 icons 64 modified on x%20y.txt\nfolder /home/m/c 7 - - - - -\nfolder /r 1 list - - off -\n"
                           "folder relative 1 list - - - -\nfolder /short 1 list\nfolder /home/m/d x grid 4097 age maybe a%2Fb\nfolder trash%3A/// 2 icons - - - -\n"),
                  &t);
    CHECK(t.n == 5 && t.f[2].rev == 0 && t.f[2].view == 0 && folder_says(&t.f[2]));
    CHECK(strcmp(t.f[4].path, TRASH_URI) == 0 && t.f[4].view == 1);   // (the trash view's)
    char tl[96];
    CHECK(folder_line(&t.f[4], tl, sizeof tl) > 0 && strcmp(tl, "folder trash%3A/// 2 icons - - - -\n") == 0);
    t.f[2] = t.f[3], t.n = 3;
    const FolderPrefs *f = &t.f[0];
    CHECK(strcmp(f->path, "/home/m/a b") == 0 && f->at == 100 && f->view == 1 && f->zoom == 64 && f->sort == SORT_MTIME && f->rev == 1 &&
          strcmp(f->sel, "x y.txt") == 0);
    f = &t.f[1];
    CHECK(strcmp(f->path, "/home/m/c") == 0 && f->at == 7 && f->view < 0 && f->zoom == 0 && f->sort < 0 && f->rev < 0 && !f->sel && !folder_says(f));
    f = &t.f[2];   // (every field unknown: not set; a cursor with a / isn't one)
    CHECK(strcmp(f->path, "/home/m/d") == 0 && f->at == 0 && f->view < 0 && f->zoom == 0 && f->sort < 0 && f->rev < 0 && !f->sel);
    // Written back as read.
    char line[PATH_MAX * 4];
    CHECK(folder_line(&t.f[0], line, sizeof line) > 0 && strcmp(line, "folder /home/m/a%20b 100 icons 64 modified on x%20y.txt\n") == 0);
    CHECK(folder_line(&t.f[1], line, sizeof line) > 0 && strcmp(line, "folder /home/m/c 7 - - - - -\n") == 0);
    CHECK(folder_line(&t.f[0], line, 20) == 0);
    CHECK(folders_find(&t, "/home/m/c") == 1 && folders_find(&t, "/home/m") < 0);
    CHECK(folders_get(&a[0], &t, "/home/m/c") == &t.f[1] && folders_get(&a[0], &t, "/new") == &t.f[3] && t.n == 4 && t.f[3].view < 0);

    // The file: two windows, each with its own changes, both kept; other lines kept.
    ops_guard(root);
    char path[PATH_MAX], d1[PATH_MAX], d2[PATH_MAX], d3[PATH_MAX], mi[3 * PATH_MAX];
    snprintf(path, sizeof path, "%s", at("f11/state/ofx/state"));
    snprintf(d1, sizeof d1, "%s", at("f11/one")), snprintf(d2, sizeof d2, "%s", at("f11/two")), snprintf(d3, sizeof d3, "%s", at("f11/three"));
    mkdirs(d1), mkdirs(d2), mkdirs(d3);
    snprintf(mi, sizeof mi, "1 0 0:1 / / rw - ext4 a rw\n2 1 0:2 / %s rw - nfs4 b rw\n", at("f11/nas"));
    CleanMounts ms = clean_mounts(&a[0], str_from_c(mi));
    put(path, "sidebar=places\nhidden=on\n");
    Err err;
    folders_parse(&a[0], STR_EMPTY, &t), folders_parse(&a[0], STR_EMPTY, &w);
    FolderPrefs *p = folders_get(&a[0], &t, d1);   // window A: d1 as icons
    p->view = 1, p->at = 50, p->changed = FP_VIEW | FP_AT;
    p = folders_get(&a[0], &w, d1);   // window B: d1's cursor, d2 by size
    p->sel = "b.txt", p->at = 60, p->changed = FP_SEL | FP_AT;
    p = folders_get(&a[0], &w, d2);
    p->sort = SORT_SIZE, p->at = 40, p->changed = FP_SORT | FP_AT;
    CHECK(folders_write(&a[1], path, &t, &ms, &err));
    CHECK(folders_write(&a[1], path, &w, &ms, &err));   // (B read A's line again: its view stays)
    Str text = read_file(&a[0], path);
    char want[6 * PATH_MAX];
    snprintf(want, sizeof want, "sidebar=places\nhidden=on\nfolder %s 60 icons - - - b.txt\nfolder %s 40 - - size - -\n", d1, d2);
    if (strcmp(text.p, want) != 0) fprintf(stderr, "folders file:\n%s\nwanted:\n%s\n", text.p, want);
    CHECK(strcmp(text.p, want) == 0);
    CHECK(w.n == 2 && !w.f[0].changed && w.f[0].view == 1 && strcmp(w.f[0].sel, "b.txt") == 0);   // (B's table is the file's now)
    // state_put of a key keeps the folder lines.
    CHECK(state_put(&a[0], path, "hidden", "off", &err) && strstr(read_file(&a[0], path).p, "hidden=off\nfolder ") != NULL);

    // The housekeeping: gone ones out (d3 removed); one on a network mount, or a drive that is out,
    // kept though not there; one that says nothing out.
    rmdir(d3);
    char nas[PATH_MAX];
    snprintf(nas, sizeof nas, "%s", at("f11/nas/x"));
    snprintf(want, sizeof want, "folder %s 1 list - - - -\nfolder %s 1 list - - - -\nfolder /run/media/m/STICK/x 1 list - - - -\nfolder %s 9 - - - - -\nfolder %s 2 list - - - -\n",
             d3, nas, d2, d1);
    put(path, want);
    folders_parse(&a[0], STR_EMPTY, &w);
    CHECK(folders_write(&a[1], path, &w, &ms, &err));
    snprintf(want, sizeof want, "folder %s 1 list - - - -\nfolder /run/media/m/STICK/x 1 list - - - -\nfolder %s 2 list - - - -\n", nas, d1);
    text = read_file(&a[0], path);
    if (strcmp(text.p, want) != 0) fprintf(stderr, "tidied:\n%s\nwanted:\n%s\n", text.p, want);
    CHECK(strcmp(text.p, want) == 0);
    // An entry with only Reverse Order, or only a cursor, says something: kept; so is the trash view's. One the file has as
    // newer keeps its time when an older window changes a field. One whose folder can't be looked at
    // (EACCES) isn't gone.
    char locked[PATH_MAX], hidden[PATH_MAX + 8];
    snprintf(locked, sizeof locked, "%s", at("f11/locked")), snprintf(hidden, sizeof hidden, "%s/in", locked);
    mkdirs(hidden);
    snprintf(want, sizeof want, "folder %s 5 - - - on -\nfolder %s 900 - - - - b\nfolder %s 1 list - - - -\nfolder %s 3 - - - - c\nfolder trash%%3A/// 4 icons - - - -\n", d1, d2, hidden, root);
    put(path, want);
    CHECK(chmod(locked, 0) == 0);
    folders_parse(&a[0], STR_EMPTY, &w);
    p = folders_get(&a[0], &w, d2);
    p->view = 1, p->at = 100, p->changed = FP_VIEW | FP_AT;
    CHECK(folders_write(&a[1], path, &w, &ms, &err));
    chmod(locked, 0755);
    snprintf(want, sizeof want, "folder %s 5 - - - on -\nfolder %s 900 icons - - - b\nfolder %s 1 list - - - -\nfolder %s 3 - - - - c\nfolder trash%%3A/// 4 icons - - - -\n", d1, d2, hidden, root);
    text = read_file(&a[0], path);
    if (strcmp(text.p, want) != 0) fprintf(stderr, "kept:\n%s\nwanted:\n%s\n", text.p, want);
    CHECK(strcmp(text.p, want) == 0);
    // The cap: the most recently opened FOLDERS_MAX kept.
    static char many[(FOLDERS_ROOM + 10) * 64];
    size_t m = 0;
    for (int i = 0; i < FOLDERS_MAX + 10; i++) m += (size_t)snprintf(many + m, sizeof many - m, "folder /run/media/m/S/%d %d list - - - -\n", i, i + 1);
    put(path, many);
    folders_parse(&a[0], read_file(&a[0], path), &w);
    CHECK(w.n == FOLDERS_MAX + 10);
    w.f[0].changed = FP_SEL, w.f[0].sel = "x";   // (an old one changed: still the oldest, gone)
    CHECK(folders_write(&a[1], path, &w, &ms, &err));
    CHECK(w.n == FOLDERS_MAX && folders_find(&w, "/run/media/m/S/9") < 0 && folders_find(&w, "/run/media/m/S/10") >= 0 &&
          folders_find(&w, "/run/media/m/S/1009") >= 0 && folders_find(&w, "/run/media/m/S/0") < 0);
    CHECK(folders_tidy(&w, NULL) == 0);
    m = 0;   // exactly one over: one goes
    for (int i = 0; i < FOLDERS_MAX + 1; i++) m += (size_t)snprintf(many + m, sizeof many - m, "folder /x/%d %d list - - - -\n", i, i + 1);
    folders_parse(&a[0], str_from_c(many), &w);
    CHECK(folders_tidy(&w, NULL) == 1 && w.n == FOLDERS_MAX && folders_find(&w, "/x/0") < 0);
    // More lines than the table holds: the rest left out, said.
    m = 0;
    for (int i = 0; i < FOLDERS_ROOM + 1; i++) m += (size_t)snprintf(many + m, sizeof many - m, "folder /x/%d 1 list - - - -\n", i);
    folders_parse(&a[0], str_from_c(many), &w);
    CHECK(w.n == FOLDERS_ROOM && !folders_get(&a[0], &w, "/another"));

    // The wipe: only the fields asked, each entry that had one counted and marked.
    folders_parse(&a[0], S("folder /a 1 icons 64 size on -\nfolder /b 1 - 96 - - x\nfolder /c 1 - - - - y\n"), &w);
    CHECK(folders_wipe(&w, FP_ZOOM) == 2 && w.f[0].zoom == 0 && w.f[0].view == 1 && w.f[0].changed == FP_ZOOM && w.f[1].zoom == 0 && !w.f[2].changed);
    CHECK(folders_wipe(&w, FP_VIEW | FP_SORT | FP_REV) == 1 && w.f[0].view < 0 && w.f[0].sort < 0 && w.f[0].rev < 0);
    CHECK(folders_wipe(&w, FP_VIEW) == 0);
    ops_guard(NULL);
}

// ofx.conf's view defaults (F11): view, zoom, sort, sort-reverse; zoom one of the icon view's sizes.
static void test_conf_views(void)
{
    static Conf c;
    conf_defaults(&c);
    CHECK(!c.icons && c.zoom == 48 && c.sort == SORT_NAME && !c.sort_rev);
    conf_parse(S("view = icons\nzoom = 96\nsort = modified\nsort-reverse = on\n"), &c);
    CHECK(c.icons && c.zoom == 96 && c.sort == SORT_MTIME && c.sort_rev && !c.bad);
    conf_parse(S("sort = size\nview = list\n"), &c);
    CHECK(c.sort == SORT_SIZE && !c.icons && !c.bad);
    const char *bad[] = { "view = grid", "zoom = 50", "zoom = 0", "zoom = 2560", "zoom = 4x", "zoom =", "sort = date" };
    for (int k = 0; k < COUNT(bad); k++) {
        conf_parse(str_from_c(bad[k]), &c);
        if (c.bad != 1) fprintf(stderr, "conf: '%s' read\n", bad[k]);
        CHECK(c.bad == 1 && !c.icons && c.zoom == 48 && c.sort == SORT_NAME);
    }
    conf_parse(S("zoom = 50"), &c);
    CHECK(strcmp(c.why, "zoom wants 32, 48, 64, 96, 128 or 256") == 0);
    // The sizes are the icon view's (ui/'s), and the default is its default.
    CHECK(COUNT(CONF_ZOOMS) == UI_ZOOMS && CONF_ZOOM == UI_ZOOM_DEFAULT);
    for (int i = 0; i < UI_ZOOMS; i++) CHECK(CONF_ZOOMS[i] == UI_ZOOM_PX[i]);
}

// F11 step 2: the tree's open folders as the state file's tree= keeps them, and state_value.
static void test_tree_remember(void)
{
    static Tree t;
    t.nopen = 0;
    tree_open(&t, S("/")), tree_open(&t, S("/home/m/a b")), tree_open(&t, S("/x"));
    char text[256];
    CHECK(tree_open_text(&t, text, sizeof text) && strcmp(text, "/ /home/m/a%20b /x") == 0);
    CHECK(!tree_open_text(&t, text, 8));
    static Tree u;
    u.nopen = 0;
    tree_open_load(&u, S("  / /home/m/a%20b relative /x %zz  "));
    CHECK(u.nopen == 3 && strcmp(u.open[1], "/home/m/a b") == 0 && strcmp(u.open[2], "/x") == 0);
    t.nopen = 0;
    CHECK(tree_open_text(&t, text, sizeof text) && text[0] == 0);
    // 128 open, long and full of escapes: the room asked is enough, a byte less isn't.
    t.nopen = 0;
    for (int i = 0; i < TREE_OPEN_MAX; i++) {
        char p[200];
        snprintf(p, sizeof p, "/a b/%03d/%0150d", i, 0);
        tree_open(&t, str_from_c(p));
    }
    size_t cap = tree_open_cap(&t);
    char *buf = arena_push(&ta, (isize)cap, 1);
    CHECK(t.nopen == TREE_OPEN_MAX && cap < KiB(64) && tree_open_text(&t, buf, cap) && strlen(buf) < cap);
    t.nopen = 1;
    CHECK(tree_open_cap(&t) == 1 + 3 * strlen(t.open[0]) + 1 && tree_open_text(&t, buf, strlen(t.open[0]) + 3));
    // A key's value, the last one said; none.
    ops_guard(root);
    put(at("f11t/state"), "a=1\ntree = / /x\ntree=/y\n");
    CHECK(str_eq(state_value(&ta, at("f11t/state"), "tree"), S("/y")) && state_value(&ta, at("f11t/state"), "b").len == 0);
    ops_guard(NULL);
}

// reviewer-2's LOWs on F11 step 1 (2026-11-01): a state file that can't be read whole isn't written
// over; writers at once (Ctrl+Q) lose nothing under the lock; autofs is a far mount.
static void test_state_writers(void)
{
    ops_guard(root);
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s", at("f11w/state"));
    static char big[8192];
    memset(big, 'x', sizeof big - 2), big[sizeof big - 2] = '\n', big[sizeof big - 1] = 0;
    put(path, big);
    static u8 small_mem[4096];
    Arena small;
    arena_init(&small, "state-small", small_mem, sizeof small_mem);
    Err err;
    CHECK(!state_put(&small, path, "hidden", "on", &err) && strstr(err.msg, "couldn't read it whole"));
    static Folders w;
    folders_parse(&small, STR_EMPTY, &w);
    FolderPrefs *p = folders_get(&small, &w, "/run/media/m/S/a");
    p->view = 1, p->changed = FP_VIEW;
    arena_init(&small, "state-small", small_mem, sizeof small_mem);
    CHECK(!folders_write(&small, path, &w, NULL, &err) && strstr(err.msg, "couldn't read it whole"));
    CHECK(strcmp(read_file(&ta, path).p, big) == 0);
    // Three writers at once, 40 entries each, one write each: all 120 there.
    unlink(path);
    pid_t kids[3];
    for (int k = 0; k < 3; k++) {
        if ((kids[k] = fork()) == 0) {
            static u8 mem[2][MiB(1)];
            Arena a[2];
            for (int i = 0; i < 2; i++) arena_init(&a[i], "writer", mem[i], sizeof mem[i]);
            static Folders t;
            folders_parse(&a[0], STR_EMPTY, &t);
            for (int i = 0; i < 40; i++) {
                char d[64];
                snprintf(d, sizeof d, "/run/media/m/S/%d-%d", k, i);
                FolderPrefs *f = folders_get(&a[0], &t, d);
                f->sel = "x", f->at = 1, f->changed = FP_SEL | FP_AT;
                Err e;
                arena_reset(&a[1]);
                if (!folders_write(&a[1], path, &t, NULL, &e)) _exit(1);
                Arena swap = a[0];
                a[0] = a[1], a[1] = swap;
            }
            _exit(0);
        }
    }
    int fails = 0;
    for (int k = 0; k < 3; k++) {
        int st;
        waitpid(kids[k], &st, 0);
        fails += !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }
    folders_parse(&ta, read_file(&ta, path), &w);
    if (w.n != 120) fprintf(stderr, "writers at once: %d of 120 kept\n", w.n);
    CHECK(fails == 0 && w.n == 120);
    // And state_put's: three writers, 30 keys each, all 90 there (the folders kept too).
    for (int k = 0; k < 3; k++) {
        if ((kids[k] = fork()) == 0) {
            static u8 mem[MiB(1)];
            Arena a;
            arena_init(&a, "putter", mem, sizeof mem);
            for (int i = 0; i < 30; i++) {
                char key[32];
                Err e;
                snprintf(key, sizeof key, "k%d-%d", k, i);
                arena_reset(&a);
                if (!state_put(&a, path, key, "1", &e)) _exit(1);
            }
            _exit(0);
        }
    }
    for (int k = 0; k < 3; k++) {
        int st;
        waitpid(kids[k], &st, 0);
        fails += !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }
    Str all = read_file(&ta, path);
    int keys = 0;
    for (isize i = 0; i + 1 < all.len; i++) keys += all.p[i] == '\n' && all.p[i + 1] == 'k';
    keys += all.len && all.p[0] == 'k';
    folders_parse(&ta, all, &w);
    if (keys != 90) fprintf(stderr, "state_put at once: %d of 90 kept\n", keys);
    CHECK(fails == 0 && keys == 90 && w.n == 120);
    ops_guard(NULL);
    // autofs: a look under it mounts what it stands for.
    CHECK(!thumb_local(S("1 0 0:1 / / rw - ext4 a rw\n2 1 0:2 / /net rw - autofs b rw\n"), "/net/nas/x"));
}

// F11 steps 3 and 3b: the window lines (fs/session.c): written, read back (old lines too), a
// running window's told from one that crashed, a window's own line put over, the newest taken but
// never a running window's, at most SESSION_KEEP kept besides those, one forgotten.
static void test_session(void)
{
    ops_guard(root);
    char path[PATH_MAX], line[PATH_MAX * 4], boot[16];
    snprintf(path, sizeof path, "%s", at("f11s/state"));
    CHECK(session_boot(boot, sizeof boot) && strlen(boot) == 8);
    SessionLive me = { .start = win_started(getpid()) };
    snprintf(me.boot, sizeof me.boot, "%s", boot);
    const char *dirs[] = { "/home/m/a b", "trash:///", "/x" };
    CHECK(session_line(77, 500, 2, NULL, dirs, 3, line, sizeof line) > 0 && strcmp(line, "window 77 500 2 closed /home/m/a%20b trash:/// /x\n") == 0);
    SessionLive l = { "abcd1234", 99 };
    CHECK(session_line(77, 500, 0, &l, dirs, 1, line, sizeof line) > 0 && strcmp(line, "window 77 500 0 open:abcd1234:99 /home/m/a%20b\n") == 0);
    int full = session_line(77, 500, 2, NULL, dirs, 3, line, sizeof line);
    CHECK(full > 0 && session_line(77, 500, 2, NULL, dirs, 3, line, (size_t)full) == 0 && session_line(77, 500, 2, NULL, dirs, 3, line, (size_t)full - 6) == 0);
    CHECK(session_line(77, 500, 2, NULL, dirs, 3, line, (size_t)full + 1) == full);
    SessionWin w;
    CHECK(session_parse_line(&ta, S("window 77 500 2 closed /home/m/a%20b trash:/// /x relative"), &w) && w.id == 77 && w.at == 500 && w.shown == 2 && w.n == 3 &&
          !w.open && strcmp(w.dir[0], "/home/m/a b") == 0 && strcmp(w.dir[1], "trash:///") == 0);
    CHECK(session_parse_line(&ta, S("window 77 500 0 open:abcd1234:99 /a"), &w) && w.open && strcmp(w.live.boot, "abcd1234") == 0 && w.live.start == 99 && w.n == 1);
    CHECK(session_parse_line(&ta, S("window 77 500 1 /a /b"), &w) && !w.open && w.n == 2 && w.shown == 1);   // (a line from before 3b)
    CHECK(!session_parse_line(&ta, S("window 7 1 0 open:x /a"), &w) && !session_parse_line(&ta, S("window 7 1 0 open::5 /a"), &w) &&
          !session_parse_line(&ta, S("window 7 1 0 open:b:-1 /a"), &w));
    CHECK(!session_parse_line(&ta, S("window 0 500 0 closed /x"), &w) && !session_parse_line(&ta, S("window 7 500 0 closed"), &w) && !session_parse_line(&ta, S("folder /x"), &w));
    CHECK(session_parse_line(&ta, S("window 7 1 9 closed /x"), &w) && w.shown == 0);   // (a tab past the end: the first)
    CHECK(!session_parse_line(&ta, S("window 7 1 99 closed /x"), &w));
    // Running or not: this process, open, this boot; not another start time, another boot, a pid gone, closed.
    SessionWin r = { .id = getpid(), .open = true, .live = me };
    CHECK(session_alive(&r, boot) && session_mine(&r, getpid(), &me));
    r.live.start++;
    CHECK(!session_alive(&r, boot) && !session_mine(&r, getpid(), &me));
    r.live = me, snprintf(r.live.boot, sizeof r.live.boot, "00000000");
    CHECK(!session_alive(&r, boot));
    r.live = me, r.id = 999999999;
    CHECK(!session_alive(&r, boot));
    r.id = getpid(), r.open = false;
    CHECK(!session_alive(&r, boot) && !session_alive(&r, NULL));

    // This window's line: put, put over (its own only: same id and mark), taken out; a stale line of
    // the same pid (another boot) stays.
    char mine[256], stale[256];
    snprintf(stale, sizeof stale, "window %d 5 0 open:00000000:%lld /old\n", (int)getpid(), (long long)me.start);
    put(path, "sidebar=tree\n");
    FILE *f = fopen(path, "a");
    fputs(stale, f), fclose(f);
    Err err;
    session_line(getpid(), 10, 0, &me, dirs, 1, mine, sizeof mine);
    CHECK(session_put(&ta, path, getpid(), &me, mine, &err));
    session_line(getpid(), 11, 0, &me, dirs + 2, 1, mine, sizeof mine);
    CHECK(session_put(&ta, path, getpid(), &me, mine, &err));
    Str text = read_file(&ta, path);
    CHECK(strstr(text.p, stale) && strstr(text.p, mine) && !strstr(text.p, " 10 0 open:"));
    // A start never takes a running window's line (this one's): the stale one, a crash, instead.
    SessionKey rest[16];
    int nrest;
    CHECK(session_take(&ta, path, 0, &w, rest, 16, &nrest, &err) && w.at == 5 && w.open && nrest == 0 && strstr(read_file(&ta, path).p, mine));
    CHECK(!session_take(&ta, path, 0, &w, rest, 16, &nrest, &err));
    CHECK(!session_take(&ta, path, getpid(), &w, rest, 16, &nrest, &err));   // (by id neither)
    CHECK(session_put(&ta, path, getpid(), &me, NULL, &err) && strcmp(read_file(&ta, path).p, "sidebar=tree\n") == 0);

    // Closed lines: the newest taken, the rest's keys (crashed marked), one forgotten by its key.
    snprintf(line, sizeof line, "window 9 20 0 closed /a\nwindow 11 30 1 closed trash:/// /x\nwindow 12 25 0 open:00000000:7 /c\n");
    put(path, line);
    CHECK(session_take(&ta, path, 0, &w, rest, 16, &nrest, &err) && w.id == 11 && w.shown == 1 && w.n == 2 && w.dir[1] && strcmp(w.dir[1], "/x") == 0 &&
          nrest == 2 && rest[0].id == 9 &&
          !rest[0].crashed && rest[1].id == 12 && rest[1].crashed && rest[1].at == 25);
    CHECK(session_forget(&ta, path, rest[1], &err) && strcmp(read_file(&ta, path).p, "window 9 20 0 closed /a\n") == 0);
    CHECK(session_take(&ta, path, 9, &w, rest, 16, &nrest, &err) && w.id == 9 && nrest == 0);
    // Two lines of one id: by id, the newer (first in the file).
    put(path, "window 8 50 0 closed /c\nwindow 7 9 0 closed /b\nwindow 7 3 0 closed /a\n");   // (8: newer, another id)
    CHECK(session_take(&ta, path, 7, &w, rest, 16, &nrest, &err) && w.at == 9 && strcmp(read_file(&ta, path).p, "window 8 50 0 closed /c\nwindow 7 3 0 closed /a\n") == 0);
    // The rest handed back: at most cap.
    put(path, "window 1 1 0 closed /a\nwindow 2 2 0 closed /a\nwindow 3 3 0 closed /a\nwindow 4 4 0 closed /a\n");
    CHECK(session_take(&ta, path, 0, &w, rest, 2, &nrest, &err) && w.id == 4 && nrest == 2);
    // Forgotten by key: only the line with that id and time.
    put(path, "window 3 1 0 closed /a\nwindow 3 2 0 closed /b\n");
    CHECK(session_forget(&ta, path, (SessionKey){ 3, 1, false }, &err) && strcmp(read_file(&ta, path).p, "window 3 2 0 closed /b\n") == 0);

    // At most SESSION_KEEP lines besides running windows' (reviewer-2, 2026-11-01: 80 piled up): 12
    // closed put, the 8 newest left; this window's running line kept besides them; ties: the later.
    put(path, "");
    session_line(getpid(), 1000, 0, &me, dirs, 1, mine, sizeof mine);   // (the newest: not counted, as a running window's)
    CHECK(session_put(&ta, path, getpid(), &me, mine, &err));
    for (int i = 1; i <= 12; i++) {
        session_line(100 + i, i, 0, NULL, dirs, 1, line, sizeof line);
        CHECK(session_put(&ta, path, 100 + i, NULL, line, &err));
    }
    text = read_file(&ta, path);
    int lines = 0;
    for (isize i = 0; i < text.len; i++) lines += text.p[i] == '\n';
    CHECK(lines == 1 + SESSION_KEEP && strstr(text.p, mine) && !strstr(text.p, "window 104 ") && strstr(text.p, "window 105 ") && strstr(text.p, "window 112 "));
    session_line(getpid(), 1001, 0, &me, dirs + 2, 1, mine, sizeof mine);   // (put over: still not counted)
    CHECK(session_put(&ta, path, getpid(), &me, mine, &err) && strstr(read_file(&ta, path).p, "window 105 ") && strstr(read_file(&ta, path).p, mine));
    session_line(getpid(), 0, 0, &me, dirs, 1, mine, sizeof mine);   // (the oldest: a running window's line is never dropped)
    CHECK(session_put(&ta, path, getpid(), &me, mine, &err));
    session_line(300, 2000, 0, NULL, dirs, 1, line, sizeof line);
    CHECK(session_put(&ta, path, 300, NULL, line, &err) && strstr(read_file(&ta, path).p, mine));
    put(path, "");
    for (int i = 1; i <= SESSION_KEEP + 1; i++) {
        session_line(200 + i, 7, 0, NULL, dirs, 1, line, sizeof line);
        CHECK(session_put(&ta, path, 200 + i, NULL, line, &err));
    }
    text = read_file(&ta, path), lines = 0;
    for (isize i = 0; i < text.len; i++) lines += text.p[i] == '\n';
    CHECK(lines == SESSION_KEEP && !strstr(text.p, "window 201 ") && strstr(text.p, "window 209 "));
    ops_guard(NULL);
}

// The question after Close All (F11 step 3): on top while restore_n is set; y or Yes says all, n,
// Esc or No not; restore-tabs read.
static void test_restore_question(void)
{
    Ui u = menu_ui(3);
    UiNames names = { name_at, NULL, NULL };
    u.restore_n = 2;
    CHECK(ui_on_top(&u) == UI_ON_RESTORE);
    UiInput in = chars("y");
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_RESTORE_WINDOWS && a.all && u.restore_n == 0 && ui_on_top(&u) == UI_ON_NONE);
    u.restore_n = 2, in = chars("n"), a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_RESTORE_WINDOWS && !a.all && u.restore_n == 0);
    u.restore_n = 1, in = keys(1, UK_ESC), a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_RESTORE_WINDOWS && !a.all && u.restore_n == 0);
    u.restore_n = 1, in = ui_input_none(), in.modal = UI_MODAL_YES, a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_RESTORE_WINDOWS && a.all && u.restore_n == 0);
    u.restore_n = 1, in = ui_input_none(), in.modal = UI_MODAL_NO, a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_RESTORE_WINDOWS && !a.all);
    static Conf c;
    conf_parse(S("restore-tabs = on\n"), &c);
    CHECK(c.restore_tabs && !c.sort_rev && !c.bad);
    conf_defaults(&c);
    CHECK(!c.restore_tabs);
}

// The F11 milestone review's bugs in fs/ (2026-11-02): a number too long for i64 isn't one; a mount
// list too big for its arena judges nothing; 80 window lines through a 1 MiB arena still keep 8.
static void test_review_fs(void)
{
    CHECK(folder_num(S("99999999999999999999"), INT64_MAX / 2) == -1 && folder_num(S("4611686018427387903"), INT64_MAX / 2) == 4611686018427387903LL);
    CHECK(folder_num(S("4611686018427387904"), INT64_MAX / 2) == -1);
    static Folders t;
    folders_parse(&ta, S("folder /a 99999999999999999999 list - - - -\n"), &t);
    CHECK(t.n == 1 && t.f[0].at == 0);
    static u8 tiny[64];
    Arena small;
    arena_init(&small, "mounts-tiny", tiny, sizeof tiny);
    CleanMounts ms = clean_mounts(&small, S("1 0 0:1 / / rw - ext4 a rw\n2 1 0:2 / /x rw - ext4 b rw\n3 1 0:3 / /y rw - ext4 c rw\n"));
    char real[PATH_MAX];
    CHECK(ms.unknown && !clean_real(&ms, "/home/m/x", real, sizeof real) && !folder_gone(&ms, "/nowhere/ofx-test"));
    static u8 room[120];   // (room for the table, not for the long mount points: unknown too)
    arena_init(&small, "mounts-room", room, sizeof room);
    ms = clean_mounts(&small, S("1 0 0:1 / /mnt/a-mount-point-with-a-rather-long-name-indeed rw - ext4 a rw\n2 1 0:2 / /mnt/another-mount-point-with-a-long-name-too rw - ext4 b rw\n"));
    CHECK(ms.unknown && !clean_real(&ms, "/home/m/x", real, sizeof real));
    ops_guard(root);
    char path[PATH_MAX], line[256];
    snprintf(path, sizeof path, "%s", at("f11r/state"));
    put(path, "");
    const char *dirs[] = { "/home/m/a folder with a long name", "/home/m/b", "/home/m/c", "/home/m/d", "/home/m/e", "/home/m/f", "/home/m/g", "/home/m/h" };
    FILE *f = fopen(path, "w");
    for (int i = 0; i < 80; i++) {
        session_line(1000 + i, i, 0, NULL, dirs, 8, line, sizeof line);
        fputs(line, f);
    }
    fclose(f);
    static u8 mem[MiB(1)];
    Arena a;
    arena_init(&a, "session-1m", mem, sizeof mem);
    Err err;
    session_line(2000, 500, 0, NULL, dirs, 8, line, sizeof line);
    CHECK(session_put(&a, path, 2000, NULL, line, &err));
    Str text = read_file(&ta, path);
    int lines = 0;
    for (isize i = 0; i < text.len; i++) lines += text.p[i] == '\n';
    CHECK(lines == SESSION_KEEP && strstr(text.p, "window 2000 ") && strstr(text.p, "window 1079 ") && !strstr(text.p, "window 1072 "));
    ops_guard(NULL);
}

// A writer whose lines pass the room it asked: a replacement (keep) or the new lines (tail).
static bool rw_prepare(void *ctx, Arena *a, Str text, isize *room, Err *err)
{
    (void)ctx, (void)a, (void)text, (void)err;
    *room = 0;
    return true;
}
static bool rw_keep(void *ctx, Str text, Str line, Str *out)
{
    (void)text, (void)line;
    if (*(int *)ctx == 1) *out = S("a line far longer than the file's own, past the room asked for it");
    return true;
}
static isize rw_tail(void *ctx, char *out, isize cap)
{
    (void)out;
    return *(int *)ctx == 2 ? -1 : *(int *)ctx == 3 ? (cap >= 2 ? (out[0] = 'z', out[1] = '\n', 2) : -1) : 0;
}

// Past its room, state_rewrite writes nothing and says so, never a file cut short (F11 review,
// 2026-11-02: the loop broke off and the tail dropped its line, both "succeeding").
static void test_state_rewrite_room(void)
{
    ops_guard(root);
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s", at("f11rr/state"));
    mkdirs(at("f11rr"));
    put(path, "a=1\nb=2\n");
    Err err;
    for (int how = 1; how <= 2; how++) {
        StateEdit e = { rw_prepare, rw_keep, rw_tail, &how, false };
        CHECK(!state_rewrite(&ta, path, &e, &err) && strstr(err.msg, "too big"));
        CHECK(strcmp(read_file(&ta, path).p, "a=1\nb=2\n") == 0);
    }
    int fits = 3;   // (the control: a tail that fits is written)
    StateEdit e = { rw_prepare, rw_keep, rw_tail, &fits, false };
    CHECK(state_rewrite(&ta, path, &e, &err) && strcmp(read_file(&ta, path).p, "a=1\nb=2\nz\n") == 0);
    ops_guard(NULL);
}

// The state file's writers against each other (state_rewrite, F11 review, 2026-11-02): a line one
// doesn't own or can't read stays as it is through every writer; a folder said twice is one.
static void test_state_rewrite(void)
{
    ops_guard(root);
    char path[PATH_MAX], d[PATH_MAX];
    snprintf(path, sizeof path, "%s", at("f11rw/state"));
    snprintf(d, sizeof d, "%s", at("f11rw/a"));
    mkdirs(d);
    char text[4 * PATH_MAX];
    snprintf(text, sizeof text, "x=1\nsomething from a later ofx\nfolder %%zz 1 list - - - -\nwindow 7 1 99 closed /x\nfolder %s 1 list - - - -\nfolder %s 5 icons - - - -\n", d, d);
    put(path, text);
    Err err;
    CHECK(state_put(&ta, path, "hidden", "on", &err));
    static Folders t;
    folders_parse(&ta, STR_EMPTY, &t);
    FolderPrefs *f = folders_get(&ta, &t, d);
    f->sort = SORT_SIZE, f->changed = FP_SORT;
    static u8 mem[MiB(1)];
    Arena a;
    arena_init(&a, "rewrite", mem, sizeof mem);
    CHECK(folders_write(&a, path, &t, NULL, &err));
    const char *dirs[] = { "/b" };
    char line[128];
    session_line(9, 3, 0, NULL, dirs, 1, line, sizeof line);
    CHECK(session_put(&ta, path, 9, NULL, line, &err));
    Str now = read_file(&ta, path);
    char want[2 * PATH_MAX];
    snprintf(want, sizeof want, "x=1\nsomething from a later ofx\nfolder %%zz 1 list - - - -\nwindow 7 1 99 closed /x\nhidden=on\nfolder %s 1 list - size - -\nwindow 9 3 0 closed /b\n", d);
    if (strcmp(now.p, want) != 0) fprintf(stderr, "rewrite:\n%s\nwanted:\n%s\n", now.p, want);
    CHECK(strcmp(now.p, want) == 0);
    ops_guard(NULL);
}
