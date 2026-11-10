// F7.5's unit tests: the GTK bookmarks file read, changed and written (fs/bookmarks.c). Included by
// test.c; every file under the run's root, the guard on it.

static bool link_ok(const char *from, const char *to)
{
    return link(from, to) == 0;
}

static void test_bookmarks(void)
{
    char out[700];
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
    // A share's address (F16 step 5: Add Bookmark in a share), labelled; twice is refused, by its
    // address; written as GTK writes one.
    CHECK(bm_add_uri(a, &bm, "smb://nas/pub/sub%20x", S("sub x"), &err) && bm.n == 7 && bm.b[6].path[0] == 0);
    CHECK(bm_find_uri(&bm, "smb://nas/pub/sub%20x") == 6 && !bm_add_uri(a, &bm, "smb://nas/pub/sub%20x", S("y"), &err) && strstr(err.msg, "already") && bm.n == 7);
    CHECK(bm_add_uri(a, &bm, "nfs://box/srv", STR_EMPTY, &err) && bm.b[7].label.len == 0);
    CHECK(str_ends_with(bm_format(a, &bm), S("smb://nas/pub/sub%20x sub x\nnfs://box/srv\n")));
    bm.n = 6;
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
    chmod(file, 0666);   // (a mode the umask would cut: kept whole, file_replace's fchmod)
    mode_t um = umask(022);
    CHECK(bm_write(file, bm_format(a, &bm), &err) && stat(file, &st) == 0 && (st.st_mode & 0777) == 0666);
    umask(um), chmod(file, 0600);
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
    // Read for a change (review, MEDIUM): a file there but not read whole (unreadable) is
    // refused, so no change is written over it; one not there yet is read as empty.
    chmod(file, 0000);
    CHECK(!bm_read(a, file, &bm, &err) && strstr(err.msg, "can't be read"));
    chmod(file, 0600);
    CHECK(bm_read(a, file, &bm, &err) && bm.n == 2);
    char none[700];
    snprintf(none, sizeof none, "%s/none/bookmarks", dir);
    CHECK(bm_read(a, none, &bm, &err) && bm.n == 0);
    chmod(dir, 0000);   // its folder can't be searched: can't tell, refused
    CHECK(!bm_read(a, file, &bm, &err));
    chmod(dir, 0700);
    // A format that doesn't fit its arena: empty (the callers refuse to write it).
    static u8 small_mem[64];
    Arena small;
    arena_init(&small, "small", small_mem, sizeof small_mem);
    static Bookmarks two;
    bm_parse(a, S("file:///a/rather/long/path/that/fills/the/arena\nfile:///and/another/one/beside/it\n"), &two);
    CHECK(str_eq(bm_format(&small, &two), STR_EMPTY));
    // A link (dotfiles kept elsewhere): its target written, the link still a link; a file with two
    // names: written in place, both see it.
    char target[700], link[700];
    snprintf(target, sizeof target, "%s/bm/real-bookmarks", root);
    snprintf(link, sizeof link, "%s/bm/linked/bookmarks", root);
    mkdirs(at("bm/linked"));
    put(target, "file:///old\n");
    CHECK(symlink(target, link) == 0);
    CHECK(bm_write(link, S("file:///new\n"), &err));
    struct stat lst;
    CHECK(lstat(link, &lst) == 0 && S_ISLNK(lst.st_mode) && str_eq(read_file(a, target), S("file:///new\n")));
    char other[700];
    snprintf(other, sizeof other, "%s/bm/other-name", root);
    CHECK(link_ok(target, other));
    CHECK(bm_write(target, S("file:///both\n"), &err) && str_eq(read_file(a, other), S("file:///both\n")));
    snprintf(out, sizeof out, "%s/bm/linked/dangling", root);
    CHECK(symlink("/nonexistent/ofx-bm", out) == 0 && !bm_write(out, S("x\n"), &err) && strstr(err.msg, "link to nothing"));
    // The guard: a bookmarks file outside the root is refused before anything is made.
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
    CHECK(!ui_menu_sep_shown(&u, m, 2) && !ui_menu_enabled(&u, UM_ADD_BOOKMARK) && !ui_menu_shown(&u, UM_BM_MORE));
    u.can_bookmark = true;
    CHECK(ui_menu_enabled(&u, UM_ADD_BOOKMARK));
    u.view = UI_VIEW_TRASH;
    CHECK(!ui_menu_enabled(&u, UM_ADD_BOOKMARK) && menu_keys(&u, 1, UK_ADD_BOOKMARK).kind == UA_NOT_IN_TRASH);
    u.view = UI_VIEW_FOLDER;
    CHECK(menu_keys(&u, 1, UK_ADD_BOOKMARK).kind == UA_ADD_BOOKMARK);
    // Three: three slots and the line; the second is the folder shown.
    u.bm_n = 3, u.bm_here = 1;
    CHECK(ui_menu_shown(&u, UM_BM_2) && !ui_menu_shown(&u, UM_BM_3) && ui_menu_sep_shown(&u, m, 2));
    CHECK(ui_menu_mark(&u, UM_BM_1) == 1 && ui_menu_mark(&u, UM_BM_0) == 0 && ui_menu_mark(&u, UM_ADD_BOOKMARK) == -1);
    // Picked by Enter or the pointer: that bookmark; Down steps over the line and stops at the last.
    menu_keys(&u, 1, -'b');
    CHECK(u.menu_on && u.menu == m && u.menu_at == 0);
    menu_keys(&u, 2, UK_DOWN, UK_DOWN);   // Edit Bookmarks, then past the line
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

// Edit Bookmarks (F7.5): the panel's keys and pointer; what it asks gui/ to do to the file's lines.
static void test_bookmarks_panel(void)
{
    UiNames names = { name_at, NULL, NULL };
    Ui u = menu_ui(3);
    u.bm_total = 5, u.bmp_list.rows = 4;
    UiAction a = menu_keys(&u, 1, UK_EDIT_BOOKMARKS);
    CHECK(a.kind == UA_BM_EDIT && u.panel == UI_PANEL_BM && u.bmp_list.n == 5 && u.bmp_list.sel == 0);
    // It takes every key: t starts no terminal, F10 no menu.
    CHECK(menu_keys(&u, 2, 0x100 + 't', UK_F10).kind == UA_NONE && !u.menu_on && u.panel == UI_PANEL_BM);
    menu_keys(&u, 2, UK_DOWN, 0x100 + 'j');
    CHECK(u.bmp_list.sel == 2);
    // Alt+Up and Alt+Down move it, not past the ends; Delete removes it.
    a = menu_keys(&u, 1, UK_PARENT);
    CHECK(a.kind == UA_BM_UP && a.index == 2);
    a = menu_keys(&u, 1, UK_ALT_DOWN);
    CHECK(a.kind == UA_BM_DOWN && a.index == 2);
    menu_keys(&u, 1, UK_HOME);
    CHECK(menu_keys(&u, 1, UK_PARENT).kind == UA_NONE);
    menu_keys(&u, 1, UK_END);
    CHECK(u.bmp_list.sel == 4 && menu_keys(&u, 1, UK_ALT_DOWN).kind == UA_NONE);
    a = menu_keys(&u, 1, UK_DELETE);
    CHECK(a.kind == UA_BM_REMOVE && a.index == 4 && u.panel == UI_PANEL_BM);
    // F2: the field (gui/ puts the name in), typing and the caret, Enter renames; Esc in it keeps
    // the panel and the name.
    menu_keys(&u, 1, 0x100 + 'k');
    a = menu_keys(&u, 1, UK_F2);
    CHECK(a.kind == UA_BM_NAME && a.index == 3 && u.bmp_naming);
    memcpy(u.bmp_name.b, "Docs", 4), u.bmp_name.len = u.bmp_name.caret = u.bmp_name.anchor = 4;
    menu_keys(&u, 4, UK_BACKSPACE, UK_HOME, 0x100 + 'M', 0x100 + 'j');
    CHECK(u.bmp_name.len == 5 && memcmp(u.bmp_name.b, "MjDoc", 5) == 0 && u.bmp_list.sel == 3);   // j is text here
    a = menu_keys(&u, 1, UK_ENTER);
    CHECK(a.kind == UA_BM_RENAME && a.index == 3 && !u.bmp_naming && u.panel == UI_PANEL_BM);
    menu_keys(&u, 1, UK_F2);   // (a key that asks gui/ for something ends the frame's keys)
    menu_keys(&u, 1, UK_ESC);
    CHECK(!u.bmp_naming && u.panel == UI_PANEL_BM);
    // Enter goes there and closes it; Esc only closes.
    a = menu_keys(&u, 1, UK_ENTER);
    CHECK(a.kind == UA_BM_GO && a.index == 3 && u.panel != UI_PANEL_BM);
    menu_keys(&u, 1, UK_EDIT_BOOKMARKS);
    menu_keys(&u, 1, UK_ESC);
    CHECK(u.panel != UI_PANEL_BM);
    // The pointer: a row, a double click goes; the buttons as their keys; Close.
    menu_keys(&u, 1, UK_EDIT_BOOKMARKS);
    UiInput in = ui_input_none();
    in.bmp_row = 1;
    CHECK(ui_input_acts(&in));
    ui_handle(&u, names, &in);
    CHECK(u.bmp_list.sel == 1);
    in = ui_input_none(), in.bmp_btn = 1;
    CHECK(ui_input_acts(&in));
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_BM_REMOVE && a.index == 1);
    in.bmp_btn = 3;
    CHECK(ui_handle(&u, names, &in).kind == UA_BM_UP);
    in.bmp_btn = 4;
    CHECK(ui_handle(&u, names, &in).kind == UA_BM_DOWN);
    in.bmp_btn = 2;
    CHECK(ui_handle(&u, names, &in).kind == UA_BM_NAME && u.bmp_naming);
    in.bmp_btn = 1;   // the field open: the buttons wait
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    menu_keys(&u, 1, UK_ESC);
    in = ui_input_none(), in.bmp_row = 9;   // past the list: nothing
    ui_handle(&u, names, &in);
    CHECK(u.bmp_list.sel == 1);
    in = ui_input_none(), in.bmp_drow = 2;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_BM_GO && a.index == 2 && u.panel != UI_PANEL_BM);
    menu_keys(&u, 1, UK_EDIT_BOOKMARKS);
    in = ui_input_none(), in.modal = UI_MODAL_NO;
    ui_handle(&u, names, &in);
    CHECK(u.panel != UI_PANEL_BM);
    // None in the file: Enter, Delete and the buttons do nothing.
    u.bm_total = 0;
    menu_keys(&u, 1, UK_EDIT_BOOKMARKS);
    CHECK(u.panel == UI_PANEL_BM && menu_keys(&u, 3, UK_ENTER, UK_DELETE, UK_F2).kind == UA_NONE && u.panel == UI_PANEL_BM && !u.bmp_naming);
    menu_keys(&u, 1, UK_ESC);
    // The menu: Edit Bookmarks…, and with more than 8 "N more" opening it on the first not listed.
    u.bm_total = 12, u.bm_n = UI_MENU_BOOKMARKS, u.bm_more = 3, u.bm_more_at = 10;
    int m = ui_menu_of('b');
    CHECK(ui_menu_shown(&u, UM_BM_MORE) && ui_menu_enabled(&u, UM_BM_MORE) && ui_menu_enabled(&u, UM_EDIT_BOOKMARKS));
    menu_keys(&u, 1, -'b');
    u.menu_at = menu_place(m, UM_BM_MORE);
    a = menu_keys(&u, 1, UK_ENTER);
    CHECK(a.kind == UA_BM_EDIT && u.panel == UI_PANEL_BM && u.bmp_list.sel == 10 && !u.menu_on);
    menu_keys(&u, 1, UK_ESC);
    menu_keys(&u, 1, -'b');
    u.menu_at = menu_place(m, UM_EDIT_BOOKMARKS);
    menu_keys(&u, 1, UK_ENTER);
    CHECK(u.panel == UI_PANEL_BM && u.bmp_list.sel == 0);
}

// Edit Bookmarks' changes on the lines (fs/bookmarks.c): found again in the file as it is now.
static void test_bookmarks_edit(void)
{
    Arena *a = &ta;
    static Bookmarks was, now;
    bm_parse(a, S("file:///a A\nfile:///b\nfile:///a A\nsftp://h/x\n"), &was);
    // The same text twice: the second is the second again; a line gone, or one fewer: -1.
    bm_parse(a, S("file:///new\nfile:///a A\nfile:///b\nfile:///a A\nsftp://h/x\n"), &now);
    CHECK(bm_locate(&now, &was, 0) == 1 && bm_locate(&now, &was, 2) == 3 && bm_locate(&now, &was, 3) == 4);
    bm_parse(a, S("file:///a A\nsftp://h/x\n"), &now);
    CHECK(bm_locate(&now, &was, 0) == 0 && bm_locate(&now, &was, 1) == -1 && bm_locate(&now, &was, 2) == -1);
    CHECK(bm_locate(&now, &was, -1) == -1 && bm_locate(&now, &was, 9) == -1);
    bm_parse(a, S("file:///a Other\n"), &now);   // relabelled meanwhile: not the same line
    CHECK(bm_locate(&now, &was, 0) == -1);
    // Remove, swap, rename.
    bm_remove(&was, 1);
    CHECK(was.n == 3 && str_eq(bm_format(a, &was), S("file:///a A\nfile:///a A\nsftp://h/x\n")));
    bm_swap(&was, 1, 2);
    CHECK(str_eq(bm_format(a, &was), S("file:///a A\nsftp://h/x\nfile:///a A\n")));
    bm_rename(a, &was, 1, S("  Server one \n injected"));
    CHECK(str_eq(was.b[1].label, S("Server one")));
    bm_rename(a, &was, 0, S("   "));
    CHECK(str_eq(bm_format(a, &was), S("file:///a\nsftp://h/x Server one\nfile:///a A\n")));
    bm_remove(&was, 9), bm_swap(&was, 0, 9), bm_rename(a, &was, -1, S("x"));   // out of range: nothing
    CHECK(was.n == 3);
    bm_remove(&was, 2), bm_remove(&was, 0), bm_remove(&was, 0);
    CHECK(was.n == 0 && str_eq(bm_format(a, &was), STR_EMPTY));
}

// Keyboard Shortcuts (F7.5): the table agrees with the menus; the panel's filter, keys, Help menu.
static void test_shortcuts(void)
{
    // Every menu entry with a key has a row of its name with that key, so the two can't disagree.
    for (int it = 1; it < UM_ITEMS; it++) {
        if (!ui_menu_key((UiMenuItem)it)[0]) continue;
        bool found = false;
        for (int r = 0; r < UI_KEYS_N && !found; r++)
            found = strcmp(UI_KEYS[r].action, UI_MENU_ITEM[it].label) == 0 &&
                    (strcmp(ui_keys_key(r, 0), ui_menu_key((UiMenuItem)it)) == 0 || strcmp(ui_keys_key(r, 1), ui_menu_key((UiMenuItem)it)) == 0);
        if (!found) fprintf(stderr, "no shortcut row for the menu's %s (%s)\n", UI_MENU_ITEM[it].label, ui_menu_key((UiMenuItem)it));
        CHECK(found);
    }
    int per[KS_SECTIONS] = { 0 };
    for (int r = 0; r < UI_KEYS_N; r++) {
        CHECK(UI_KEYS[r].action[0] && (ui_keys_key(r, 0)[0] || UI_KEYS[r].cmd >= UK_ITEM0) && UI_KEYS[r].sec < KS_SECTIONS);   // (a menu entry's command: no key as ofx has them)
        per[UI_KEYS[r].sec]++;
    }
    for (int s = 0; s < KS_SECTIONS; s++) CHECK(per[s] > 0 && UI_KEY_SECTION[s][0]);
    // F1: every row under its heading; it takes every key (t starts no terminal, it filters).
    Ui u = menu_ui(3);
    u.keys_list.rows = 10;
    CHECK(menu_keys(&u, 1, UK_SHORTCUTS).kind == UA_NONE && u.panel == UI_PANEL_KEYS && u.keys_list.n == UI_KEYS_LINES);
    CHECK(u.keys_row[0] == -1 - KS_MOVE && u.keys_row[1] == 0);
    CHECK(menu_keys(&u, 1, 0x100 + 't').kind == UA_NONE && u.keys_filter.len == 1 && u.panel == UI_PANEL_KEYS);
    menu_keys(&u, 1, UK_BACKSPACE);
    CHECK(u.keys_filter.len == 0 && u.keys_list.n == UI_KEYS_LINES);
    // The filter: an action's words or a key, case folded; a section with nothing left out.
    UiInput in = chars("alt+end");
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.keys_list.n == 2 && u.keys_row[0] == -1 - KS_MOVE && strcmp(UI_KEYS[u.keys_row[1]].action, "Trash") == 0);
    while (u.keys_filter.len) menu_keys(&u, 1, UK_BACKSPACE);
    in = chars("TRASH");
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.keys_list.n == 4 && u.keys_row[2] == -1 - KS_CHANGE);   // Moving: Trash; Changing files: Move to Trash
    while (u.keys_filter.len) menu_keys(&u, 1, UK_BACKSPACE);
    in = chars("zzqq");
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.keys_list.n == 0);
    while (u.keys_filter.len) menu_keys(&u, 1, UK_BACKSPACE);
    in = ui_input_none();   // a character past ASCII (é, two bytes): Backspace takes both
    in.ev[in.nev++] = ev_ch(0xE9);
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.keys_filter.len == 2);
    menu_keys(&u, 1, UK_BACKSPACE);
    CHECK(u.keys_filter.len == 0);
    // Scrolling: Down, End, Home; it stays inside.
    menu_keys(&u, 1, UK_DOWN);
    CHECK(u.keys_list.top == 1);
    menu_keys(&u, 1, UK_END);
    CHECK(u.keys_list.top == UI_KEYS_LINES - 10);
    menu_keys(&u, 1, UK_PAGEUP);
    CHECK(u.keys_list.top == UI_KEYS_LINES - 19);
    menu_keys(&u, 1, UK_HOME);
    CHECK(u.keys_list.top == 0);
    in = ui_input_none(), in.panel_wheel = -3;   // the wheel, three notches down: it draws a frame (a live window waits on input that acts)
    CHECK(ui_input_acts(&in));
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.keys_list.top == 3);
    // Typing after a scroll: back at the top.
    in = chars("a");
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.keys_list.top == 0 && u.keys_filter.len == 1);
    // Up, it is the only thing a press reaches: no menu, no row of the files.
    u.list.sel = 0;
    in = ui_input_none(), in.menu_title = 0;
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    in = ui_input_none(), in.click = 2;
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(!u.menu_on && u.list.sel == 0 && u.panel == UI_PANEL_KEYS);
    // Esc, F1 and Close close it; opened again the filter is empty; Help → Keyboard Shortcuts opens it.
    menu_keys(&u, 1, UK_ESC);
    CHECK(u.panel != UI_PANEL_KEYS);
    menu_keys(&u, 1, UK_SHORTCUTS);
    CHECK(u.panel == UI_PANEL_KEYS && u.keys_filter.len == 0 && u.keys_list.n == UI_KEYS_LINES);
    menu_keys(&u, 1, UK_ESC);
    menu_keys(&u, 1, UK_SHORTCUTS);
    menu_keys(&u, 1, UK_SHORTCUTS);
    CHECK(u.panel != UI_PANEL_KEYS);
    menu_keys(&u, 1, UK_SHORTCUTS);
    in = ui_input_none(), in.modal = UI_MODAL_NO;
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.panel != UI_PANEL_KEYS);
    int m = ui_menu_of('h');
    CHECK(m == UI_MENUS - 1 && strcmp(UI_MENU[m].title, "Help") == 0 && UI_MENU[m].n == 3);
    menu_keys(&u, 2, -'h', UK_ENTER);
    CHECK(u.panel == UI_PANEL_KEYS && !u.menu_on);
    ui_keys_close(&u);
}

// Help → About (2026-10-27): the last entry; up, it takes every key (none reaches the list), Esc
// or Enter close it, and so does Close.
static void test_about(void)
{
    Ui u = menu_ui(5);
    CHECK(UI_MENU[UI_MENUS - 1].items[UI_MENU[UI_MENUS - 1].n - 1] == UM_ABOUT && ui_menu_enabled(&u, UM_ABOUT));
    menu_keys(&u, 3, -'h', UK_UP, UK_ENTER);   // (Up from the first: round to the last)
    CHECK(u.panel == UI_PANEL_ABOUT && !u.menu_on);
    UiAction a = menu_keys(&u, 4, UK_DOWN, 0x100 + 'j', UK_DELETE, UK_SHORTCUTS);
    CHECK(u.panel == UI_PANEL_ABOUT && a.kind == UA_NONE && u.list.sel == 0 && u.panel != UI_PANEL_KEYS);
    UiInput in = ui_input_none();   // nor any click under it
    in.click = 2, in.backs = 1;
    CHECK(ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in).kind == UA_NONE && u.list.sel == 0 && u.panel == UI_PANEL_ABOUT);
    menu_keys(&u, 1, UK_ESC);
    CHECK(u.panel != UI_PANEL_ABOUT);
    menu_keys(&u, 3, -'h', UK_UP, UK_ENTER);
    menu_keys(&u, 1, UK_ENTER);
    CHECK(u.panel != UI_PANEL_ABOUT);
    menu_keys(&u, 3, -'h', UK_UP, UK_ENTER);
    in = ui_input_none();
    in.modal = UI_MODAL_NO;
    CHECK(ui_input_acts(&in));
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.panel != UI_PANEL_ABOUT);
}
