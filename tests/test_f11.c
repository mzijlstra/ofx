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
    CHECK(t.n == 5 && !t.full && t.f[2].rev == 0 && t.f[2].view == 0 && folder_says(&t.f[2]));
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
    CHECK(w.n == FOLDERS_MAX + 10 && !w.full);
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
    CHECK(w.n == FOLDERS_ROOM && w.full && !folders_get(&a[0], &w, "/another"));

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
    // A key's value, the last one said; none.
    ops_guard(root);
    put(at("f11t/state"), "a=1\ntree = / /x\ntree=/y\n");
    CHECK(str_eq(state_value(&ta, at("f11t/state"), "tree"), S("/y")) && state_value(&ta, at("f11t/state"), "b").len == 0);
    ops_guard(NULL);
}
