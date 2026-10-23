// F6's unit tests: the remembered state, the status line, the sidebar's modes. Included by
// test.c.

static void test_state(void)
{
    arena_reset(&ta);
    ops_guard(root);
    char path[PATH_MAX];
    env_set("XDG_STATE_HOME", at("state"));
    CHECK(state_path(path, sizeof path) && strcmp(path, at("state/ofx/state")) == 0);
    // No file: places.
    CHECK(state_load(&ta, path).side == SIDE_PLACES);
    // Saved, its folder made: read back; no temporary file left.
    Err err;
    State s = { SIDE_TREE };
    CHECK(state_save(path, &s, &err));
    CHECK(state_load(&ta, path).side == SIDE_TREE);
    s.side = SIDE_HIDDEN;
    CHECK(state_save(path, &s, &err) && state_load(&ta, path).side == SIDE_HIDDEN);
    char tmp[PATH_MAX + 32];
    snprintf(tmp, sizeof tmp, "%s.%d.tmp", path, (int)getpid());
    CHECK(access(tmp, F_OK) != 0);
    struct stat st;
    CHECK(stat(at("state/ofx"), &st) == 0 && (st.st_mode & 0777) == 0700);
    // What it doesn't know is left out; spaces around the = are fine; the last word wins.
    put(path, "# a comment\nwindow=big\nsidebar = tree\nsidebar=nonsense\nnoequals\n");
    CHECK(state_load(&ta, path).side == SIDE_TREE);
    put(path, "sidebar=hidden\nsidebar=places\n");
    CHECK(state_load(&ta, path).side == SIDE_PLACES);
    put(path, "sidebar=hidden");   // no newline at the end
    CHECK(state_load(&ta, path).side == SIDE_HIDDEN);
    // An out of range mode is saved as places.
    s.side = SIDE_MODES;
    CHECK(state_save(path, &s, &err) && state_load(&ta, path).side == SIDE_PLACES);
    // A folder in the way: the save fails and says why, the old file stays.
    env_restore();
    env_set("XDG_STATE_HOME", at("state-blocked"));
    put(at("state-blocked/ofx"), "a file, not a folder");
    CHECK(state_path(path, sizeof path));
    s.side = SIDE_TREE;
    CHECK(!state_save(path, &s, &err) && err.msg[0]);
    env_restore();
    // Outside the guard's folder: refused.
    snprintf(path, sizeof path, "%s/state", decoy);
    CHECK(!state_save(path, &s, &err) && strstr(err.msg, "outside the test folder"));
    CHECK(access(path, F_OK) != 0);
    // No XDG_STATE_HOME: under HOME; a relative one is ignored; neither: no path.
    env_set("XDG_STATE_HOME", NULL);
    env_set("HOME", "/home/m");
    CHECK(state_path(path, sizeof path) && strcmp(path, "/home/m/.local/state/ofx/state") == 0);
    env_set("XDG_STATE_HOME", "relative");
    CHECK(state_path(path, sizeof path) && strcmp(path, "/home/m/.local/state/ofx/state") == 0);
    env_set("HOME", NULL);
    env_set("XDG_STATE_HOME", NULL);
    CHECK(!state_path(path, sizeof path));
    env_restore();
    char small[16];
    env_set("XDG_STATE_HOME", "/a/rather/long/state/home");
    CHECK(!state_path(small, sizeof small));   // doesn't fit
    env_restore();
    ops_guard(NULL);
}

static void test_status_info(void)
{
    char buf[128];
    UiInfo in = { .n = 14, .avail = 87LL << 30, .one_count = -1, .one_size = -1 };
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "14 items, 87 GiB free") == 0);
    in.hidden = 2;
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "14 items (2 hidden), 87 GiB free") == 0);
    in.n = 1, in.hidden = 0, in.avail = -1;
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "1 item") == 0);
    in.n = 0, in.avail = 0;
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "0 items, 0 B free") == 0);
    // One marked: a folder with its count once known, a file with its size.
    in = (UiInfo){ .n = 14, .marked = 1, .one = S("adir"), .one_dir = true, .one_count = 12, .avail = 5 };
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "\"adir\" selected (12 items)") == 0);
    in.one_count = 1;
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "\"adir\" selected (1 item)") == 0);
    in.one_count = 0;
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "\"adir\" selected (0 items)") == 0);
    in.one_count = -1;
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "\"adir\" selected") == 0);
    in = (UiInfo){ .marked = 1, .one = S("a.txt"), .one_size = 4300, .one_count = 7 };
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "\"a.txt\" selected (4.2 KiB)") == 0);
    in.one_size = 0;
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "\"a.txt\" selected (0 B)") == 0);
    in.one_size = -1;
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "\"a.txt\" selected") == 0);
    // Several.
    in = (UiInfo){ .n = 14, .marked = 3, .avail = 5 };
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "3 items selected") == 0);
    // Cut short, never past the buffer.
    char tiny[8];
    in = (UiInfo){ .n = 14, .hidden = 2, .avail = 5 };
    ui_status_info(tiny, sizeof tiny, &in);
    CHECK(strcmp(tiny, "14 item") == 0);
    in = (UiInfo){ .marked = 1, .one = S("a-long-name"), .one_dir = true, .one_count = 3 };
    ui_status_info(tiny, sizeof tiny, &in);
    CHECK(strlen(tiny) == 7);
}

static void test_side_modes(void)
{
    static Ui u;
    u = (Ui){ 0 };
    UiNames names = { name_at, NULL };
    ui_resize(&u.list, 10), ui_resize(&u.side, 5);
    ui_set(&u.list, 5, 0), ui_set(&u.side, 5, 0);
    // F9 cycles places, tree, hidden, places.
    UiInput in = keys(1, UK_F9);
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_SIDE_MODE && a.index == SIDE_TREE && u.side_mode == SIDE_TREE);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_SIDE_MODE && a.index == SIDE_HIDDEN && u.side_mode == SIDE_HIDDEN);
    // Hidden: Tab leaves the keys with the list.
    in = keys(1, UK_TAB);
    ui_handle(&u, names, &in);
    CHECK(!u.side_focus);
    in = keys(1, UK_F9);
    a = ui_handle(&u, names, &in);
    CHECK(a.index == SIDE_PLACES && u.side_mode == SIDE_PLACES);
    // Hiding it takes the keys from it.
    in = keys(1, UK_TAB);
    ui_handle(&u, names, &in);
    CHECK(u.side_focus);
    in = keys(2, UK_F9, UK_F9);
    ui_handle(&u, names, &in);   // the first F9 returns: tree, keys still on it
    CHECK(u.side_mode == SIDE_TREE && u.side_focus);
    in = keys(1, UK_F9);
    ui_handle(&u, names, &in);
    CHECK(u.side_mode == SIDE_HIDDEN && !u.side_focus);
    // A mode button clicked.
    in = ui_input_none();
    in.side_mode = SIDE_TREE;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_SIDE_MODE && a.index == SIDE_TREE && u.side_mode == SIDE_TREE);
    in.side_mode = SIDE_MODES;   // out of range: nothing
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.side_mode == SIDE_TREE);
    // Not while a question is up.
    u.confirming = true;
    in.side_mode = SIDE_HIDDEN;
    CHECK(ui_handle(&u, names, &in).kind != UA_SIDE_MODE && u.side_mode == SIDE_TREE);
    u.confirming = false;
    CHECK(ui_input_none().side_mode == -1);
}

static void test_input_acts(void)
{
    UiInput in = ui_input_none();
    CHECK(!ui_input_acts(&in));
    in.wheel = 3;   // the wheel scrolls; it doesn't act
    CHECK(!ui_input_acts(&in));
    int *fields[] = { &in.click, &in.dclick, &in.side_click, &in.header, &in.tab_click, &in.tab_close, &in.mid_row, &in.mid_side,
                      &in.mid_crumb, &in.job_x, &in.side_mode };
    for (int k = 0; k < COUNT(fields); k++) {
        in = ui_input_none();
        *fields[k] = 0;
        CHECK(ui_input_acts(&in));
    }
    in = keys(1, UK_DOWN);
    CHECK(ui_input_acts(&in));
    in = ui_input_none(), in.bar = UB_UP;
    CHECK(ui_input_acts(&in));
    in = ui_input_none(), in.modal = UI_MODAL_YES;
    CHECK(ui_input_acts(&in));
    in = ui_input_none(), in.backs = 1;
    CHECK(ui_input_acts(&in));
    in = ui_input_none(), in.forwards = 1;
    CHECK(ui_input_acts(&in));
}

// The tree from a scratch folder standing in for /.
static void test_tree(void)
{
    arena_reset(&ta);
    static Tree t;
    t = (Tree){ 0 };
    mkdirs(at("tree/top/b10/deep"));
    mkdirs(at("tree/top/b2"));
    mkdirs(at("tree/top/A"));
    mkdirs(at("tree/top/.dot"));
    put(at("tree/top/file.txt"), "x");
    put(at("tree/top/b2/inner.txt"), "x");
    CHECK(symlink("b2", at("tree/top/link")) == 0);
    CHECK(symlink("file.txt", at("tree/top/flink")) == 0);
    CHECK(symlink("nowhere", at("tree/top/broken")) == 0);
    Str top = str_from_c(arena_cstr(&ta, str_from_c(at("tree/top"))));
    // Nothing open: the root alone.
    tree_build(&ta, &t, top, false);
    CHECK(t.n == 1 && t.row[0].depth == 0 && !t.row[0].open && str_eq(t.row[0].name, S("top")) && str_eq(t.row[0].path, top));
    // Following b10/deep opens everything above it: the root, b10.
    tree_follow(&t, at("tree/top/b10/deep"));
    tree_build(&ta, &t, top, false);
    const char *want[] = { "top", "A", "b2", "b10", "deep", "link" };
    const int depth[] = { 0, 1, 1, 1, 2, 1 };
    CHECK(t.n == COUNT(want));
    for (int i = 0; i < t.n && i < COUNT(want); i++) {
        if (!str_eq(t.row[i].name, str_from_c(want[i])) || t.row[i].depth != depth[i])
            fprintf(stderr, "tree row %d: %.*s %d\n", i, STR_ARG(t.row[i].name), t.row[i].depth);
        CHECK(str_eq(t.row[i].name, str_from_c(want[i])) && t.row[i].depth == depth[i]);
    }
    CHECK(t.row[0].open && t.row[3].open && !t.row[2].open && !t.row[4].open);
    CHECK(tree_find(&t, str_from_c(at("tree/top/b10/deep"))) == 4 && tree_find(&t, str_from_c(at("tree/top/nope"))) == -1);
    CHECK(str_eq(t.row[5].path, str_from_c(at("tree/top/link"))));
    // Dot-folders when hidden files are shown.
    tree_build(&ta, &t, top, true);
    CHECK(t.n == COUNT(want) + 1 && str_eq(t.row[1].name, S(".dot")));
    // Parent rows; Left and Right.
    tree_build(&ta, &t, top, false);
    CHECK(tree_parent(&t, 4) == 3 && tree_parent(&t, 5) == 0 && tree_parent(&t, 0) == -1);
    bool tg;
    CHECK(tree_left(&t, 3, &tg) == 3 && tg);    // b10 is open: it closes
    CHECK(tree_left(&t, 4, &tg) == 3 && !tg);   // deep is closed: to b10
    CHECK(tree_left(&t, 0, &tg) == 0 && tg);    // the root is open: it closes
    CHECK(tree_right(&t, 2, &tg) == 2 && tg);   // b2 is closed: it opens
    CHECK(tree_right(&t, 3, &tg) == 4 && !tg);  // b10 is open: to deep
    CHECK(tree_right(&t, 0, &tg) == 1 && !tg);
    CHECK(tree_left(&t, -1, &tg) == -1 && !tg && tree_right(&t, t.n, &tg) == t.n && !tg);
    // An open folder with no subfolders: Right stays on it.
    tree_open(&t, str_from_c(at("tree/top/b10/deep")));
    tree_build(&ta, &t, top, false);
    CHECK(t.row[4].open && tree_right(&t, 4, &tg) == 4 && !tg);
    // The last row, closed: Left goes to its folder.
    CHECK(tree_left(&t, t.n - 1, &tg) == 0 && !tg);
    // Closing b10 hides deep; deep stays open for when b10 opens again.
    tree_close(&t, str_from_c(at("tree/top/b10")));
    tree_build(&ta, &t, top, false);
    CHECK(t.n == 5 && tree_find(&t, str_from_c(at("tree/top/b10/deep"))) == -1);
    tree_open(&t, str_from_c(at("tree/top/b10")));
    tree_build(&ta, &t, top, false);
    CHECK(t.n == 6 && t.row[4].open);
    tree_close(&t, S("/not/open"));   // nothing to close
    CHECK(t.nopen > 0);
    // A folder that can't be read: no rows under it.
    tree_open(&t, str_from_c(at("tree/top/A")));
    chmod(at("tree/top/A"), 0);
    mkdirs(at("tree/top/A2"));
    tree_build(&ta, &t, top, false);
    chmod(at("tree/top/A"), 0755);
    CHECK(t.row[1].open && str_eq(t.row[2].name, S("A2")));
    // Opening twice keeps one; the oldest gives way when the list is full.
    int before = t.nopen;
    tree_open(&t, str_from_c(at("tree/top/A")));
    CHECK(t.nopen == before);
    t.nopen = 0;
    for (int i = 0; i < TREE_OPEN_MAX + 2; i++) {
        char p[64];
        snprintf(p, sizeof p, "/o%d", i);
        tree_open(&t, str_from_c(p));
    }
    CHECK(t.nopen == TREE_OPEN_MAX && strcmp(t.open[0], "/o2") == 0 && strcmp(t.open[TREE_OPEN_MAX - 1], "/o129") == 0);
    tree_close(&t, S("/o5"));
    CHECK(t.nopen == TREE_OPEN_MAX - 1 && strcmp(t.open[3], "/o6") == 0 && strcmp(t.open[TREE_OPEN_MAX - 2], "/o129") == 0);
    // Follow: a relative path or an empty one opens nothing; / opens / alone.
    t.nopen = 0;
    tree_follow(&t, "rel/x");
    tree_follow(&t, "");
    CHECK(t.nopen == 0);
    tree_follow(&t, "/");
    CHECK(t.nopen == 1 && strcmp(t.open[0], "/") == 0);
    tree_follow(&t, "/a/b/c");
    CHECK(t.nopen == 3 && strcmp(t.open[1], "/a") == 0 && strcmp(t.open[2], "/a/b") == 0);
    // The real /: its row is "/", its subfolders under it.
    t.nopen = 0;
    tree_follow(&t, "/usr");
    tree_build(&ta, &t, S("/"), false);
    CHECK(str_eq(t.row[0].name, S("/")) && t.row[0].open && tree_find(&t, S("/usr")) > 0);
    // Full: cut.
    t.nopen = 0;
    mkdirs(at("tree/many"));
    tree_open(&t, str_from_c(at("tree/many")));
    for (int i = 0; i < TREE_ROWS_MAX + 1; i++) {
        char p[64];
        snprintf(p, sizeof p, "tree/many/d%d", i);
        mkdir(at("%s", p), 0755);
    }
    tree_build(&ta, &t, str_from_c(at("tree/many")), false);
    CHECK(t.n == TREE_ROWS_MAX && t.cut);
    tree_build(&ta, &t, top, false);
    CHECK(!t.cut);
}

// The tree's keys (F6): Left and Right ask gui/ to close, open or move; Enter goes there.
static void test_tree_keys(void)
{
    static Ui u;
    u = (Ui){ 0 };
    UiNames names = { name_at, NULL };
    ui_resize(&u.list, 10), ui_resize(&u.side, 5);
    ui_set(&u.list, 5, 0), ui_set(&u.side, 8, 3);
    u.side_mode = SIDE_TREE, u.side_focus = true;
    UiInput in = keys(1, UK_LEFT);
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_TREE_LEFT && a.index == 3);
    in = chars("h");
    CHECK(ui_handle(&u, names, &in).kind == UA_TREE_LEFT);
    in = keys(1, UK_RIGHT);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_TREE_RIGHT && a.index == 3);
    in = chars("l");
    CHECK(ui_handle(&u, names, &in).kind == UA_TREE_RIGHT);
    in = keys(1, UK_SEL_RIGHT);   // Shift+Right outside an edit line: as Right
    CHECK(ui_handle(&u, names, &in).kind == UA_TREE_RIGHT);
    in = keys(1, UK_ENTER);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_PLACE && a.index == 3);
    in = keys(1, UK_BACKSPACE);
    CHECK(ui_handle(&u, names, &in).kind == UA_PARENT);
    // A mark clicked: that row, top 0 here.
    in = ui_input_none();
    in.side_twisty = 2;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_TREE_TOGGLE && a.index == 2);
    in.side_twisty = 8;   // past the end
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    // In the places mode Left goes up and Right goes to the place; a mark is no mark there.
    u.side_mode = SIDE_PLACES;
    in = keys(1, UK_LEFT);
    CHECK(ui_handle(&u, names, &in).kind == UA_PARENT);
    in = keys(1, UK_RIGHT);
    CHECK(ui_handle(&u, names, &in).kind == UA_PLACE);
    in = ui_input_none();
    in.side_twisty = 1;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    // The keys in the list: Left is up, Right opens, in the tree mode too.
    u.side_mode = SIDE_TREE, u.side_focus = false;
    in = keys(1, UK_LEFT);
    CHECK(ui_handle(&u, names, &in).kind == UA_PARENT);
    in = keys(1, UK_RIGHT);
    CHECK(ui_handle(&u, names, &in).kind == UA_OPEN);
    // An empty tree: nothing for Left or Right.
    u.side_focus = true;
    ui_set(&u.side, 0, 0);
    in = keys(1, UK_LEFT);
    CHECK(ui_handle(&u, names, &in).kind == UA_PARENT);
    in = keys(1, UK_RIGHT);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
}
