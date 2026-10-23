#include <signal.h>
#include <sys/resource.h>

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
    put(path, "# a comment\nwindow=big\nsidebar = tree\nsidebar=nonsense\nnoequals\nother=hidden\n");
    CHECK(state_load(&ta, path).side == SIDE_TREE);
    put(path, "sidebar=hidden\nsidebar=places\n");
    CHECK(state_load(&ta, path).side == SIDE_PLACES);
    put(path, "sidebar=hidden");   // no newline at the end
    CHECK(state_load(&ta, path).side == SIDE_HIDDEN);
    // An out of range mode is saved as places.
    s.side = SIDE_MODES;
    CHECK(state_save(path, &s, &err) && state_load(&ta, path).side == SIDE_PLACES);
    // A write that fails (no room: the file size limit at 0): the save fails, the old file stays,
    // no temporary file is left.
    s.side = SIDE_TREE;
    CHECK(state_save(path, &s, &err));
    struct rlimit was;
    getrlimit(RLIMIT_FSIZE, &was);
    signal(SIGXFSZ, SIG_IGN);
    struct rlimit none = { 0, was.rlim_max };
    setrlimit(RLIMIT_FSIZE, &none);
    s.side = SIDE_HIDDEN;
    bool wrote = state_save(path, &s, &err);
    setrlimit(RLIMIT_FSIZE, &was);
    signal(SIGXFSZ, SIG_DFL);
    CHECK(!wrote && strstr(err.msg, "cannot write it"));
    CHECK(state_load(&ta, path).side == SIDE_TREE && access(tmp, F_OK) != 0);
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

// The trash view's listing and restore (F6): the home trash and a drive's, read as one.
static void test_trash_view(void)
{
    arena_reset(&ta);
    ops_guard(root);
    Err err;
    env_set("XDG_DATA_HOME", at("tv/data"));
    mkdirs(at("tv/data"));
    mkdirs(at("tv/src/sub"));
    put(at("tv/src/a.txt"), "AAAA");
    put(at("tv/src/sub/b.txt"), "B");
    put(at("tv/src/.dot"), "D");
    mkdirs(at("tv/src/folder/in"));
    Trashed t;
    CHECK(trash_put(at("tv/src/a.txt"), &t, &err));
    CHECK(trash_put(at("tv/src/sub/b.txt"), &t, &err));
    CHECK(trash_put(at("tv/src/.dot"), &t, &err));
    CHECK(trash_put(at("tv/src/folder"), &t, &err));
    // A drive's own trash, by hand: a Path relative to the drive's top, one absolute.
    char tr[PATH_MAX];
    snprintf(tr, sizeof tr, "tv/drive/.Trash-%u", (unsigned)getuid());
    mkdirs(at("%s/files", tr));
    mkdirs(at("%s/info", tr));
    put(at("%s/files/c.txt", tr), "CC");
    put(at("%s/info/c.txt.trashinfo", tr), "[Trash Info]\nPath=docs/c%20d.txt\nDeletionDate=2026-01-02T03:04:05\n");
    put(at("%s/files/abs", tr), "Z");
    char body[PATH_MAX + 64];
    snprintf(body, sizeof body, "[Trash Info]\nDeletionDate=bad\nPath=%s\n", at("tv/drive/elsewhere/abs"));
    put(at("%s/info/abs.trashinfo", tr), body);
    // Left out: an info without its item, an item without its info, no Path, a Path outside the
    // group, a broken escape, a bare ".trashinfo".
    put(at("%s/info/orphan.trashinfo", tr), "[Trash Info]\nPath=x\n");
    put(at("%s/files/noinfo", tr), "x");
    put(at("%s/files/nopath", tr), "x");
    put(at("%s/info/nopath.trashinfo", tr), "[Trash Info]\nDeletionDate=2026-01-02T03:04:05\n");
    put(at("%s/files/other", tr), "x");
    put(at("%s/info/other.trashinfo", tr), "[Other]\nPath=x\n[Trash Info]\n");
    put(at("%s/files/esc", tr), "x");
    put(at("%s/info/esc.trashinfo", tr), "[Trash Info]\nPath=bad%zz\n");
    put(at("%s/info/.trashinfo", tr), "[Trash Info]\nPath=x\n");
    // Nor a Path that would leave the drive: a ".." part, an absolute one elsewhere (a stick's
    // .trashinfo can say anything).
    put(at("%s/files/up", tr), "x");
    put(at("%s/info/up.trashinfo", tr), "[Trash Info]\nPath=../escaped\n");
    put(at("%s/files/up2", tr), "x");
    put(at("%s/info/up2.trashinfo", tr), "[Trash Info]\nPath=docs/../../escaped\n");
    put(at("%s/files/away", tr), "x");
    snprintf(body, sizeof body, "[Trash Info]\nPath=%s\n", at("tv/away"));
    put(at("%s/info/away.trashinfo", tr), body);
    put(at("%s/files/dotdot", tr), "x");   // ".." only as a whole part: "a..b" is a name
    put(at("%s/info/dotdot.trashinfo", tr), "[Trash Info]\nPath=docs/a..b\n");
    // A relative Path in the home trash: nowhere to restore it to, left out.
    put(at("tv/data/Trash/files/rel"), "x");
    put(at("tv/data/Trash/info/rel.trashinfo"), "[Trash Info]\nPath=rel\n");
    // A mount point with a .Trash folder of its own but no info/ in it: not a trash folder.
    char tr2[PATH_MAX];
    snprintf(tr2, sizeof tr2, "tv/drive2/.Trash/%u/files", (unsigned)getuid());
    mkdirs(at("%s", tr2));

    char m0[PATH_MAX], m2[PATH_MAX], m3[PATH_MAX], m4[PATH_MAX];
    snprintf(m0, sizeof m0, "%s", at("tv/drive")), snprintf(m2, sizeof m2, "%s", at("tv/drive2"));
    snprintf(m3, sizeof m3, "%s", at("tv/none")), snprintf(m4, sizeof m4, "%s", at("tv/drive3"));
    // A drive's .Trash/$uid with an info/ in it: its trash too.
    char tr3[PATH_MAX];
    snprintf(tr3, sizeof tr3, "tv/drive3/.Trash/%u", (unsigned)getuid());
    mkdirs(at("%s/files", tr3));
    mkdirs(at("%s/info", tr3));
    const char *mp[] = { m0, m0, m2, m3, m4 };
    static TrashDirs d;
    trash_dirs(&d, mp, 5);
    CHECK(d.n == 3 && strcmp(d.dir[0], at("tv/data/Trash")) == 0 && d.top[0][0] == 0);
    CHECK(strcmp(d.dir[1], at("%s", tr)) == 0 && strcmp(d.top[1], m0) == 0);
    CHECK(strcmp(d.dir[2], at("%s", tr3)) == 0 && strcmp(d.top[2], m4) == 0);
    Listing l;
    CHECK(trash_list(&ta, &d, false, (Sort){ SORT_NAME, false }, &l, &err));
    const char *want[] = { "folder", "a..b", "a.txt", "abs", "b.txt", "c d.txt" };
    CHECK(l.n == COUNT(want) && l.hidden == 1);
    for (isize i = 0; i < l.n && i < COUNT(want); i++) {
        if (!str_eq(l.e[i].name, str_from_c(want[i]))) fprintf(stderr, "trash %td: %.*s\n", i, STR_ARG(l.e[i].name));
        CHECK(str_eq(l.e[i].name, str_from_c(want[i])) && l.e[i].trash);
    }
    // "a..b" off the list for the rest: the indices as before.
    CHECK(strcmp(l.e[1].trash->from, at("tv/drive/docs/a..b")) == 0);
    CHECK(trash_restore_item(l.e[1].trash, &err) && there(at("tv/drive/docs/a..b")));
    CHECK(trash_list(&ta, &d, false, (Sort){ SORT_NAME, false }, &l, &err) && l.n == COUNT(want) - 1);
    CHECK(l.e[0].kind == K_DIR && l.e[1].size == 4 && l.e[1].kind == K_FILE);
    CHECK(strcmp(l.e[1].trash->from, at("tv/src/a.txt")) == 0 && strcmp(l.e[1].trash->file, at("tv/data/Trash/files/a.txt")) == 0);
    CHECK(strcmp(l.e[1].trash->info, at("tv/data/Trash/info/a.txt.trashinfo")) == 0);
    CHECK(strcmp(l.e[4].trash->from, at("tv/drive/docs/c d.txt")) == 0 && strcmp(l.e[4].trash->top, m0) == 0);
    CHECK(l.e[1].trash->top[0] == 0);   // the home trash's: no top
    CHECK(strcmp(l.e[2].trash->from, at("tv/drive/elsewhere/abs")) == 0 && l.e[2].mtime == 0);   // an unreadable date: 0
    struct tm tm = { .tm_year = 126, .tm_mon = 0, .tm_mday = 2, .tm_hour = 3, .tm_min = 4, .tm_sec = 5, .tm_isdst = -1 };
    CHECK(l.e[4].mtime == (i64)mktime(&tm));
    CHECK(!there(at("tv/escaped")) && !there(at("escaped")));
    CHECK(l.e[1].mtime > 0 && l.e[1].mtime <= (i64)time(NULL));
    // Shown hidden: the dot-file too; by date, newest last.
    CHECK(trash_list(&ta, &d, true, (Sort){ SORT_NAME, false }, &l, &err) && l.n == COUNT(want) && l.hidden == 1);
    CHECK(trash_list(&ta, &d, false, (Sort){ SORT_MTIME, false }, &l, &err) && str_eq(l.e[1].name, S("abs")));
    // Restore: back where it was; its folder made again when it is gone; never over a file there.
    CHECK(trash_list(&ta, &d, false, (Sort){ SORT_NAME, false }, &l, &err));
    CHECK(trash_restore_item(l.e[1].trash, &err) && there(at("tv/src/a.txt")) && !there(at("tv/data/Trash/info/a.txt.trashinfo")));
    rmdir(at("tv/src/sub"));
    CHECK(trash_restore_item(l.e[3].trash, &err) && there(at("tv/src/sub/b.txt")));
    put(at("tv/drive/elsewhere/abs"), "in the way");
    CHECK(!trash_restore_item(l.e[2].trash, &err) && strstr(err.msg, "is there again") && there(at("%s/files/abs", tr)));
    CHECK(trash_restore_item(l.e[4].trash, &err) && there(at("tv/drive/docs/c d.txt")));
    // A folder the guard refuses: nothing made, nothing moved.
    TrashItem out = { l.e[0].trash->file, l.e[0].trash->info, out_at("restored/folder"), "" };
    CHECK(!trash_restore_item(&out, &err) && strstr(err.msg, "outside the test folder") && !there(out_at("restored")));
    // Its folder can't be made (a file in the way).
    put(at("tv/blocker"), "x");
    TrashItem blocked = { l.e[0].trash->file, l.e[0].trash->info, at("tv/blocker/sub/folder"), "" };
    CHECK(!trash_restore_item(&blocked, &err) && strstr(err.msg, "blocker/sub") && there(l.e[0].trash->file));
    // From a drive's trash, never off the drive: through a symlink on it, by a "..", or to an
    // absolute path elsewhere (each made by hand here; the listing leaves the last two out).
    mkdirs(at("tv/outside"));
    CHECK(symlink(at("tv/outside"), at("tv/drive/link")) == 0);
    char top[PATH_MAX];
    snprintf(top, sizeof top, "%s", m0);
    TrashItem via = { l.e[0].trash->file, l.e[0].trash->info, at("tv/drive/link/folder"), top };
    CHECK(!trash_restore_item(&via, &err) && strstr(err.msg, "outside its drive") && !there(at("tv/outside/folder")));
    char climb[PATH_MAX + 32];
    snprintf(climb, sizeof climb, "%s/../outside/folder", m0);
    TrashItem up = { l.e[0].trash->file, l.e[0].trash->info, climb, top };
    CHECK(!trash_restore_item(&up, &err) && strstr(err.msg, "outside its drive") && !there(at("tv/outside/folder")));
    TrashItem away = { l.e[0].trash->file, l.e[0].trash->info, at("tv/outside/folder"), top };
    CHECK(!trash_restore_item(&away, &err) && !there(at("tv/outside/folder")) && there(l.e[0].trash->file));
    TrashItem gone_top = { l.e[0].trash->file, l.e[0].trash->info, at("tv/nodrive/folder"), at("tv/nodrive") };
    CHECK(!trash_restore_item(&gone_top, &err) && there(l.e[0].trash->file));
    // On the drive, under a folder that isn't there yet: fine.
    char ok_from[PATH_MAX];
    snprintf(ok_from, sizeof ok_from, "%s", at("tv/drive/new/deep/folder"));
    TrashItem on = { l.e[0].trash->file, l.e[0].trash->info, ok_from, top };
    CHECK(trash_restore_item(&on, &err) && there(at("tv/drive/new/deep/folder")));
    // The trash_path_climbs and trash_under edges.
    CHECK(trash_path_climbs("..") && trash_path_climbs("/a/..") && trash_path_climbs("a/../b") && !trash_path_climbs("a/..b/c"));
    CHECK(!trash_path_climbs("...") && !trash_path_climbs("/a/b/") && !trash_path_climbs("") && !trash_path_climbs("x/.a/y"));
    CHECK(trash_under("/m/x", "/m") && trash_under("/m", "/m") && !trash_under("/mx", "/m") && trash_under("/anything", "/"));
    // What is left: abs, and the dot-file.
    CHECK(trash_list(&ta, &d, false, (Sort){ SORT_NAME, false }, &l, &err) && l.n == 1 && str_eq(l.e[0].name, S("abs")));
    // No trash at all.
    env_set("XDG_DATA_HOME", at("tv/none"));
    trash_dirs(&d, NULL, 0);
    CHECK(d.n == 0 && trash_list(&ta, &d, false, (Sort){ SORT_NAME, false }, &l, &err) && l.n == 0);
    // "/" as a mount point: /.Trash-$uid, which isn't there.
    const char *slash[] = { "/" };
    trash_dirs(&d, slash, 1);
    CHECK(d.n == 0);
    env_restore();
    env_restore();
    ops_guard(NULL);
}

// The trash view's keys and panel (F6).
static void test_trash_keys(void)
{
    static Ui u;
    u = (Ui){ 0 };
    UiNames names = { name_at, NULL };
    ui_resize(&u.list, 10), ui_resize(&u.side, 5);
    ui_set(&u.list, 5, 1), ui_set(&u.side, 5, 0);
    u.in_trash = true;
    // r restores; Delete asks first, as Shift+Delete does.
    UiInput in = chars("r");
    CHECK(ui_handle(&u, names, &in).kind == UA_RESTORE);
    in = keys(1, UK_DELETE);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.confirming && u.confirm_n == 1);
    in = chars("y");
    CHECK(ui_handle(&u, names, &in).kind == UA_DELETE && !u.confirming);
    // What doesn't work there.
    UiKey refused[] = { UK_F2, UK_COPY, UK_CUT, UK_PASTE, UK_NEWFOLDER };
    for (int k = 0; k < COUNT(refused); k++) {
        in = keys(1, refused[k]);
        CHECK(ui_handle(&u, names, &in).kind == UA_NOT_IN_TRASH && !u.editing);
    }
    in = chars("t");
    CHECK(ui_handle(&u, names, &in).kind == UA_NOT_IN_TRASH);
    in = keys(1, UK_UNDO);   // undo still works
    CHECK(ui_handle(&u, names, &in).kind == UA_UNDO);
    // The buttons: Restore; Empty Trash asks, Yes empties, No and Esc don't.
    in = ui_input_none(), in.bar = UB_RESTORE;
    CHECK(ui_handle(&u, names, &in).kind == UA_RESTORE);
    in.bar = UB_EMPTY;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.empty_asking);
    in = chars("r");   // a panel is up: no restore under it
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.empty_asking);
    in = ui_input_none(), in.bar = UB_RESTORE;   // nor a click on Restore
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.empty_asking);
    in = keys(1, UK_ESC);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.empty_asking);
    u.empty_asking = true;
    in = chars("y");
    CHECK(ui_handle(&u, names, &in).kind == UA_EMPTY_TRASH && !u.empty_asking);
    u.empty_asking = true;
    in = ui_input_none(), in.modal = UI_MODAL_NO;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.empty_asking);
    u.empty_asking = true;
    in.modal = UI_MODAL_YES;
    CHECK(ui_handle(&u, names, &in).kind == UA_EMPTY_TRASH);
    // Left the trash view meanwhile: the panel goes, Yes empties nothing.
    u.empty_asking = true, u.in_trash = false;
    in = ui_input_none(), in.modal = UI_MODAL_YES;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.empty_asking);
    u.empty_asking = true;
    in = ui_input_none();
    ui_handle(&u, names, &in);
    CHECK(!u.empty_asking);
    // Outside the trash view: r is nothing, Delete trashes, the buttons do nothing.
    in = chars("r");
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    in = keys(1, UK_DELETE);
    CHECK(ui_handle(&u, names, &in).kind == UA_TRASH);
    in = ui_input_none(), in.bar = UB_EMPTY;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.empty_asking);
    in.bar = UB_RESTORE;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    in = keys(1, UK_COPY);
    CHECK(ui_handle(&u, names, &in).kind == UA_COPY);
    // An empty trash: nothing to restore.
    u.in_trash = true;
    ui_set(&u.list, 0, 0);
    in = chars("r");
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    in = ui_input_none(), in.bar = UB_RESTORE;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    // The path buttons in the trash view: one, Trash.
    static UiCrumbs c;
    ui_crumbs_set(&c, "/home/m/x", "/home/m");
    ui_crumbs_set(&c, TRASH_URI, "/home/m");
    CHECK(c.n == 1 && c.cur == 0 && str_eq(ui_crumb_label(&c, 0), S("Trash")) && str_eq(ui_crumb_path(&c, 0), S(TRASH_URI)));
    ui_crumbs_set(&c, "/home/m/y", "/home/m");   // and back
    CHECK(c.n == 2 && str_eq(ui_crumb_label(&c, 1), S("y")));
}
