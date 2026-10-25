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
    State s = { SIDE_TREE, false, 0 };
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
    UiNames names = { name_at, NULL, NULL };
    ui_resize(&u.list, 10), ui_resize(&u.side, 5);
    ui_set(&u.list, 5, 0), ui_set(&u.side, 5, 0);
    // F9 cycles places, tree, hidden, places.
    UiInput in = keys(1, UK_F9);
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_SIDE_MODE && a.index == SIDE_TREE && u.side_mode == SIDE_TREE);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_SIDE_MODE && a.index == SIDE_HIDDEN && u.side_mode == SIDE_HIDDEN);
    // Hidden: Tab shows it as places and gives it the keys (Michael, 2026-10-24); Tab again takes
    // them back.
    in = keys(1, UK_TAB);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_SIDE_MODE && a.index == SIDE_PLACES && u.side_mode == SIDE_PLACES && u.side_focus);
    in = keys(1, UK_TAB);
    ui_handle(&u, names, &in);
    CHECK(!u.side_focus && u.side_mode == SIDE_PLACES);
    // Ctrl+Left goes there and stays; from hidden as Tab does.
    in = keys(2, UK_SIDE, UK_SIDE);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.side_focus);
    u.side_mode = SIDE_HIDDEN, u.side_focus = false;
    in = keys(1, UK_SIDE);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_SIDE_MODE && a.index == SIDE_PLACES && u.side_focus);
    // Hiding it takes the keys from it.
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
    UiNames names = { name_at, NULL, NULL };
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
    // In the places mode Left and Right do nothing (Enter goes there); a mark is no mark there.
    u.side_mode = SIDE_PLACES;
    in = keys(2, UK_LEFT, UK_RIGHT);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    in = ui_input_none();
    in.side_twisty = 1;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    // The keys in the list, in the tree mode too: Left and Right do nothing.
    u.side_mode = SIDE_TREE, u.side_focus = false;
    in = keys(2, UK_LEFT, UK_RIGHT);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    // An empty tree: nothing for Left or Right.
    u.side_focus = true;
    ui_set(&u.side, 0, 0);
    in = keys(1, UK_LEFT);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
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
    put(at("%s/files/enc", tr), "x");   // a ".." percent-encoded: decoded first
    put(at("%s/info/enc.trashinfo", tr), "[Trash Info]\nPath=%2E%2E/%2e%2e/escaped\n");
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
    chmod(at("tv/drive3/.Trash"), 01777);   // sticky, as the spec wants
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
    UiNames names = { name_at, NULL, NULL };
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

static void test_status_clears(void)
{
    CHECK(ui_status_clears(false, 1000, 1000) && ui_status_clears(false, 1000, 1001));
    CHECK(!ui_status_clears(true, 1000, 1000) && !ui_status_clears(true, 1000, 1999));
    CHECK(ui_status_clears(true, 1000, 2000) && ui_status_clears(true, 1000, 9000));
}

// Empty Trash's items (F6 review): each item with its .trashinfo after it, the strays, the home
// trash's directorysizes; the folders themselves stay.
static void test_trash_empty_items(void)
{
    arena_reset(&ta);
    static TrashDirs d;
    d.n = 2;
    snprintf(d.dir[0], PATH_MAX, "%s", at("te/home/Trash")), d.top[0][0] = 0;
    snprintf(d.dir[1], PATH_MAX, "%s", at("te/drive/.Trash-1")), snprintf(d.top[1], PATH_MAX, "%s", at("te/drive"));
    put(at("te/home/Trash/files/a"), "a");
    put(at("te/home/Trash/info/a.trashinfo"), "i");
    put(at("te/home/Trash/files/noinfo"), "x");
    put(at("te/home/Trash/info/orphan.trashinfo"), "i");
    put(at("te/home/Trash/info/stray"), "s");
    put(at("te/home/Trash/directorysizes"), "1 2 a\n");
    put(at("te/drive/.Trash-1/files/b"), "b");
    put(at("te/drive/.Trash-1/info/b.trashinfo"), "i");
    put(at("te/drive/.Trash-1/directorysizes"), "not ours to clear");   // only the home trash keeps one
    static TrashDoomed out[32];
    isize n;
    CHECK(trash_empty_items(&ta, &d, out, 32, &n) && n == 8);
    int afters = 0, seen_a = 0, seen_ai = 0, seen_cache = 0, seen_drive_cache = 0;
    for (isize i = 0; i < n; i++) {
        if (out[i].after_ok) {
            afters++;
            CHECK(i > 0 && strstr(out[i].path, ".trashinfo") && !out[i - 1].after_ok);
            char want[PATH_MAX];   // the item right before it is its own
            Str prev = path_base(str_from_c(out[i - 1].path));
            snprintf(want, sizeof want, "%.*s.trashinfo", STR_ARG(prev));
            CHECK(str_eq(path_base(str_from_c(out[i].path)), str_from_c(want)));
        }
        seen_a += strcmp(out[i].path, at("te/home/Trash/files/a")) == 0;
        seen_ai += strcmp(out[i].path, at("te/home/Trash/info/a.trashinfo")) == 0;
        seen_cache += strcmp(out[i].path, at("te/home/Trash/directorysizes")) == 0;
        seen_drive_cache += strcmp(out[i].path, at("te/drive/.Trash-1/directorysizes")) == 0;
    }
    CHECK(afters == 2 && seen_a == 1 && seen_ai == 1 && seen_cache == 1 && seen_drive_cache == 0);
    // Too little room: false, never past max, an item never without room for its .trashinfo.
    CHECK(!trash_empty_items(&ta, &d, out, 3, &n) && n <= 3);
    CHECK(!trash_empty_items(&ta, &d, out, 1, &n) && n == 0);
    CHECK(!trash_empty_items(&ta, &d, out, 7, &n) && n <= 7);
    CHECK(!trash_empty_items(&ta, &d, out, 5, &n) && n == 5);   // the home trash's all but its cache
    // An empty trash, a missing one: nothing.
    d.n = 1;
    snprintf(d.dir[0], PATH_MAX, "%s", at("te/none"));
    CHECK(trash_empty_items(&ta, &d, out, 32, &n) && n == 0);
}

// Which drive trash folders are listed (F6 review): the user's own, no symlinks; .Trash/$uid only
// in a sticky .Trash.
static void test_trash_dirs_owned(void)
{
    unsigned uid = (unsigned)getuid();
    char p[PATH_MAX];
    snprintf(p, sizeof p, "to/sticky/.Trash/%u/info", uid);
    mkdirs(at("%s", p));
    chmod(at("to/sticky/.Trash"), 01777);
    snprintf(p, sizeof p, "to/plain/.Trash/%u/info", uid);   // not sticky: not listed
    mkdirs(at("%s", p));
    mkdirs(at("to/real/info"));
    snprintf(p, sizeof p, "to/linked/.Trash-%u", uid);       // a symlink: not listed
    mkdirs(at("to/linked"));
    CHECK(symlink(at("to/real"), at("%s", p)) == 0);
    snprintf(p, sizeof p, "to/linkadmin/.Trash");             // .Trash a symlink to a sticky one
    mkdirs(at("to/linkadmin"));
    CHECK(symlink(at("to/sticky/.Trash"), at("%s", p)) == 0);
    char m1[PATH_MAX], m2[PATH_MAX], m3[PATH_MAX], m4[PATH_MAX];
    snprintf(m1, sizeof m1, "%s", at("to/sticky")), snprintf(m2, sizeof m2, "%s", at("to/plain"));
    snprintf(m3, sizeof m3, "%s", at("to/linked")), snprintf(m4, sizeof m4, "%s", at("to/linkadmin"));
    const char *mp[] = { m1, m2, m3, m4 };
    env_set("XDG_DATA_HOME", at("to/nohome"));
    static TrashDirs d;
    trash_dirs(&d, mp, 4);
    CHECK(d.n == 1 && strstr(d.dir[0], "to/sticky/.Trash/"));
    env_restore();
    CHECK(trash_dir_ours("/anything", TRASH_HOME));
    CHECK(!trash_dir_ours("/nonexistent/x", TRASH_USER));
}

// Eject's pieces (F6.1): a command's output read whole, and what eject's says.
static void test_eject(void)
{
    char out[64];
    Err err;
    bool late;
    char *sh[] = { "sh", "-c", "echo out; echo err >&2; exit 3", NULL };
    CHECK(spawn_capture(sh, out, sizeof out, 5000, &late, &err) && strcmp(out, "out\nerr\n") == 0);
    char small[5];
    char *lots[] = { "sh", "-c", "seq 1000", NULL };   // more than fits: cut, read to its end
    CHECK(spawn_capture(lots, small, sizeof small, 5000, &late, &err) && strcmp(small, "1\n2\n") == 0);
    char *none[] = { "/nonexistent/prog", NULL };
    CHECK(!spawn_capture(none, out, sizeof out, 5000, &late, &err) && strstr(err.msg, "/nonexistent/prog"));
    CHECK(!late);
    // One that doesn't end, nor its child holding the pipe: killed, group and all, at the deadline.
    put(at("ej-pid"), "");
    char sleeper[PATH_MAX + 64];
    snprintf(sleeper, sizeof sleeper, "echo started; sleep 30 & echo $! > '%s'; wait", at("ej-pid"));
    char *slow[] = { "sh", "-c", sleeper, NULL };
    i64 t0 = now_ms();
    CHECK(spawn_capture(slow, out, sizeof out, 300, &late, &err) && late && strcmp(out, "started\n") == 0);
    CHECK(now_ms() - t0 < 2000);
    int gpid = atoi(slurp(at("ej-pid")));
    for (int k = 0; k < 50 && gpid > 0 && kill(gpid, 0) == 0; k++) nanosleep(&(struct timespec){ 0, 20000000L }, NULL);
    CHECK(gpid > 0 && kill(gpid, 0) != 0);   // the grandchild went too
    CHECK(spawn_capture(sh, out, sizeof out, 5000, &late, &err) && !late);
    // The command, with a fake udisksctl first on PATH that logs and succeeds, or fails a step.
    mkdirs(at("ej/bin"));
    put(at("ej/bin/udisksctl"), "#!/bin/sh\necho \"$*\" >> \"$EJ_LOG\"\n"
                                "[ \"$1\" = \"$EJ_FAIL\" ] && { echo \"Error $1: target is busy\" >&2; exit 1; }\nexit 0\n");
    chmod(at("ej/bin/udisksctl"), 0755);
    char path[PATH_MAX * 2];
    snprintf(path, sizeof path, "%s:%s", at("ej/bin"), getenv("PATH"));
    env_set("PATH", path);
    env_set("EJ_LOG", at("ej/log"));
    const char *argv[6];
    eject_argv("/dev/sd x1", argv);   // a space: passed as one word
    char big[512], why[128];
    env_set("EJ_FAIL", "none");
    CHECK(spawn_capture((char *const *)argv, big, sizeof big, 5000, &late, &err) && eject_result(str_from_c(big), why, sizeof why) == EJECT_DONE && !why[0]);
    CHECK(strcmp(slurp(at("ej/log")), "unmount -b /dev/sd x1\npower-off -b /dev/sd x1\n") == 0);
    env_set("EJ_FAIL", "power-off");
    CHECK(spawn_capture((char *const *)argv, big, sizeof big, 5000, &late, &err) && eject_result(str_from_c(big), why, sizeof why) == EJECT_UNMOUNTED);
    CHECK(strstr(why, "power-off: target is busy"));
    env_set("EJ_FAIL", "unmount");
    put(at("ej/log"), "");
    CHECK(spawn_capture((char *const *)argv, big, sizeof big, 5000, &late, &err) && eject_result(str_from_c(big), why, sizeof why) == EJECT_FAILED);
    CHECK(strstr(why, "unmount: target is busy") && strcmp(slurp(at("ej/log")), "unmount -b /dev/sd x1\n") == 0);   // no power-off after
    env_restore(), env_restore(), env_restore(), env_restore(), env_restore();
    // Reading the output.
    CHECK(eject_result(S(""), why, sizeof why) == EJECT_FAILED && !why[0]);
    CHECK(eject_result(S("OFX-EJECTED\n"), why, sizeof why) == EJECT_FAILED);   // never without the unmount
    CHECK(eject_result(S("  OFX-UNMOUNTED \nOFX-EJECTED"), why, sizeof why) == EJECT_DONE);
    CHECK(eject_result(S("\nfirst\nsecond\nOFX-UNMOUNTED\n"), why, sizeof why) == EJECT_UNMOUNTED && strcmp(why, "first") == 0);
}

static void test_eject_keys(void)
{
    static Ui u;
    u = (Ui){ 0 };
    UiNames names = { name_at, NULL, NULL };
    ui_resize(&u.list, 10), ui_resize(&u.side, 4);
    ui_set(&u.list, 5, 0), ui_set(&u.side, 8, 0);
    u.side.top = 2;
    UiInput in = ui_input_none();
    CHECK(in.side_eject == -1);
    in.side_eject = 1;
    CHECK(ui_input_acts(&in));
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_EJECT && a.index == 3);
    in.side_eject = 6;   // past the end
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    in.side_eject = 1;
    u.side_mode = SIDE_TREE;   // no drives there
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    u.side_mode = SIDE_PLACES, u.confirming = true;   // not under a panel
    CHECK(ui_handle(&u, names, &in).kind != UA_EJECT);
}

static void test_trash_empty_chunk(void)
{
    // file, info, file, info, stray, file, info
    TrashDoomed d[] = { { "f1", false }, { "i1", true }, { "f2", false }, { "i2", true }, { "s", false }, { "f3", false }, { "i3", true } };
    CHECK(trash_empty_chunk(d, 7, 0, 10) == 7);   // all fit
    CHECK(trash_empty_chunk(d, 7, 0, 2) == 2);    // ends after i1
    CHECK(trash_empty_chunk(d, 7, 0, 3) == 2);    // would end on f2 without i2: f2 waits
    CHECK(trash_empty_chunk(d, 7, 2, 3) == 3);    // f2 i2 s
    CHECK(trash_empty_chunk(d, 7, 5, 3) == 2);    // the rest
    CHECK(trash_empty_chunk(d, 7, 4, 2) == 1);    // s, then f3 with i3 next time
    CHECK(trash_empty_chunk(d, 7, 7, 3) == 0);
}
