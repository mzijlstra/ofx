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
