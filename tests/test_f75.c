// F7.5's unit tests: the GTK bookmarks file read, changed and written (fs/bookmarks.c). Included by
// test.c; every file under the run's root, the guard on it.

static void test_bookmarks(void)
{
    ops_guard(root);
    Arena *a = &ta;
    static Bookmarks bm;
    // Every line kept in order: labels, other schemes, a trailing slash dropped from the path (not
    // the URI), %XX decoded; blank lines dropped.
    Str text = S("file:///home/m/Docs Documents\n\nsftp://host/x Server\nfile:///home/m/a%20b/\nfile:///\n  file:///tmp/t  T two  \n");
    bm_parse(a, S("dav:////srv/x\n"), &bm);   // another scheme, though "/srv/x" follows its first 7
    CHECK(bm.n == 1 && bm.b[0].path[0] == 0);
    bm_parse(a, text, &bm);
    CHECK(bm.n == 5 && !bm.full);
    CHECK(strcmp(bm.b[0].path, "/home/m/Docs") == 0 && str_eq(bm.b[0].label, S("Documents")));
    CHECK(bm.b[1].path[0] == 0 && str_eq(bm.b[1].uri, S("sftp://host/x")) && str_eq(bm.b[1].label, S("Server")));
    CHECK(strcmp(bm.b[2].path, "/home/m/a b") == 0 && bm.b[2].label.len == 0 && str_eq(bm.b[2].uri, S("file:///home/m/a%20b/")));
    CHECK(strcmp(bm.b[3].path, "/") == 0);
    CHECK(strcmp(bm.b[4].path, "/tmp/t") == 0 && str_eq(bm.b[4].label, S("T two")));
    CHECK(bm_find(&bm, "/home/m/a b") == 2 && bm_find(&bm, "/home/m") == -1 && bm_find(&bm, "") == -1);
    // Written back as read (the blank line and the spaces aside).
    CHECK(str_eq(bm_format(a, &bm), S("file:///home/m/Docs Documents\nsftp://host/x Server\nfile:///home/m/a%20b/\nfile:///\nfile:///tmp/t T two\n")));
    // Add: at the end, the URI as GLib writes it; twice is refused.
    Err err;
    CHECK(bm_add(a, &bm, "/home/m/new dir #1", &err) && bm.n == 6);
    CHECK(str_eq(bm.b[5].uri, S("file:///home/m/new%20dir%20%231")) && strcmp(bm.b[5].path, "/home/m/new dir #1") == 0);
    CHECK(!bm_add(a, &bm, "/home/m/new dir #1", &err) && strstr(err.msg, "already") && bm.n == 6);
    CHECK(!bm_add(a, &bm, "/home/m/a b", &err) && bm.n == 6);   // found by path, written with a slash
    // Full: more lines than BM_MAX; nothing is added.
    char big[BM_MAX * 24];
    size_t o = 0;
    for (int i = 0; i <= BM_MAX; i++) o += (size_t)snprintf(big + o, sizeof big - o, "file:///d/%d\n", i);
    bm_parse(a, (Str){ big, (isize)o }, &bm);
    CHECK(bm.full && bm.n == BM_MAX && !bm_add(a, &bm, "/d/new", &err) && strstr(err.msg, "full"));
    // The file: its folder made (0700) when there is none, a new file 0644, a second write keeps
    // the mode it has; nothing left beside it.
    char file[640], dir[600];
    snprintf(dir, sizeof dir, "%s/bm/gtk-3.0", root);
    snprintf(file, sizeof file, "%s/bookmarks", dir);
    mkdirs(at("bm"));
    bm_parse(a, S("file:///x X\n"), &bm);
    CHECK(bm_write(file, bm_format(a, &bm), &err));
    struct stat st;
    CHECK(stat(dir, &st) == 0 && (st.st_mode & 0777) == 0700 && stat(file, &st) == 0 && (st.st_mode & 0777) == 0644);
    CHECK(str_eq(read_file(a, file), S("file:///x X\n")));
    chmod(file, 0600);
    CHECK(bm_add(a, &bm, "/y", &err) && bm_write(file, bm_format(a, &bm), &err));
    CHECK(stat(file, &st) == 0 && (st.st_mode & 0777) == 0600 && str_eq(read_file(a, file), S("file:///x X\nfile:///y\n")));
    DIR *dd = opendir(dir);
    int names = 0;
    for (struct dirent *de; dd && (de = readdir(dd));) names += de->d_name[0] != '.' || strncmp(de->d_name, ".bookmarks", 10) == 0;
    if (dd) closedir(dd);
    CHECK(names == 1);   // bookmarks alone, no .bookmarks.ofx-*
    // A rename that fails (a folder where the file would go): refused, nothing left beside it.
    char asdir[640];
    snprintf(asdir, sizeof asdir, "%s/sub", dir);
    mkdirs(asdir);
    CHECK(!bm_write(asdir, S("file:///z\n"), &err));
    dd = opendir(dir), names = 0;
    for (struct dirent *de; dd && (de = readdir(dd));) names += strncmp(de->d_name, ".bookmarks", 10) == 0;
    if (dd) closedir(dd);
    CHECK(names == 0);
    rmdir(asdir);
    // A file that can't be written (its folder read only): refused, the old one as it was.
    chmod(dir, 0500);
    CHECK(!bm_write(file, S("file:///z\n"), &err) && str_eq(read_file(a, file), S("file:///x X\nfile:///y\n")));
    chmod(dir, 0700);
    // The guard: a bookmarks file outside the root is refused before anything is made.
    char out[600];
    snprintf(out, sizeof out, "%s/gtk-3.0/bookmarks", decoy);
    CHECK(!bm_write(out, S("file:///x\n"), &err) && strstr(err.msg, "outside"));
    snprintf(out, sizeof out, "%s/gtk-3.0", decoy);
    CHECK(stat(out, &st) != 0);
}

// The Bookmarks menu (F7.5): Add Bookmark, then up to 8 of the file's, the folder shown marked.
static void test_bookmarks_menu(void)
{
    UiNames names = { name_at, NULL, NULL };
    Ui u = menu_ui(3);
    u.bm_here = -1;
    int m = ui_menu_of('b');
    CHECK(m == 4 && strcmp(UI_MENU[m].title, "Bookmarks") == 0 && UI_MENU[m].items[0] == UM_ADD_BOOKMARK);
    // None: the slots not shown, nor the line before them; Add greyed until gui/ allows it.
    for (int k = 0; k < UI_MENU_BOOKMARKS; k++) CHECK(!ui_menu_shown(&u, (UiMenuItem)(UM_BM_0 + k)));
    CHECK(!ui_menu_sep_shown(&u, m, 1) && !ui_menu_enabled(&u, UM_ADD_BOOKMARK));
    u.can_bookmark = true;
    CHECK(ui_menu_enabled(&u, UM_ADD_BOOKMARK));
    u.in_trash = true;
    CHECK(!ui_menu_enabled(&u, UM_ADD_BOOKMARK) && menu_keys(&u, 1, UK_ADD_BOOKMARK).kind == UA_NOT_IN_TRASH);
    u.in_trash = false;
    CHECK(menu_keys(&u, 1, UK_ADD_BOOKMARK).kind == UA_ADD_BOOKMARK);
    // Three: three slots and the line; the second is the folder shown.
    u.bm_n = 3, u.bm_here = 1;
    CHECK(ui_menu_shown(&u, UM_BM_2) && !ui_menu_shown(&u, UM_BM_3) && ui_menu_sep_shown(&u, m, 1));
    CHECK(ui_menu_mark(&u, UM_BM_1) == 1 && ui_menu_mark(&u, UM_BM_0) == 0 && ui_menu_mark(&u, UM_ADD_BOOKMARK) == -1);
    // Picked by Enter or the pointer: that bookmark; Down steps over the line and stops at the last.
    menu_keys(&u, 1, -'b');
    CHECK(u.menu_on && u.menu == m && u.menu_at == 0);
    menu_keys(&u, 1, UK_DOWN);
    CHECK(u.menu_at == menu_place(m, UM_BM_0));
    menu_keys(&u, 3, UK_DOWN, UK_DOWN, UK_DOWN);   // bm-1, bm-2, then round to Add
    CHECK(u.menu_at == 0);
    u.menu_at = menu_place(m, UM_BM_2);
    UiAction a = menu_keys(&u, 1, UK_ENTER);
    CHECK(a.kind == UA_BOOKMARK && a.index == 2 && !u.menu_on);
    UiInput in = ui_input_none();
    in.menu_title = m;
    ui_handle(&u, names, &in);
    in = ui_input_none(), in.menu_item = menu_place(m, UM_BM_0);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_BOOKMARK && a.index == 0);
    in = ui_input_none(), in.menu_title = m;
    ui_handle(&u, names, &in);
    in = ui_input_none(), in.menu_item = menu_place(m, UM_BM_5);   // not there: nothing
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    // Eight shown at most.
    u.bm_n = UI_MENU_BOOKMARKS;
    CHECK(ui_menu_shown(&u, UM_BM_7));
}
