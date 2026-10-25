// F7.2's unit tests: the counts under a path (du_walk), the permission change and its undo, the
// Properties panel's keys and clicks, Alt+Enter. Included by test.c.

static int ticks;

static void stop_at_tick(void *ctx, const DuSum *s)
{
    (void)s;
    atomic_store((atomic_uint *)ctx, 99);
}
static void tick_count(void *ctx, const DuSum *s)
{
    (void)ctx, (void)s;
    ticks++;
}

static void test_du_walk(void)
{
    // d/: a (10 bytes), sub/ (b, 20 bytes; deeper/ empty), a link to a (counted as itself), a link
    // to sub (not followed); f (5 bytes) beside it.
    mkdirs(at("f72du/d/sub/deeper"));
    put(at("f72du/d/a"), "0123456789");
    put(at("f72du/d/sub/b"), "01234567890123456789");
    put(at("f72du/f"), "01234");
    CHECK(symlink("a", at("f72du/d/la")) == 0 && symlink("sub", at("f72du/d/lsub")) == 0);
    atomic_uint gen = 7;
    DuWalk w = { .gen = &gen, .mine = 7, .tick = tick_count };
    DuSum s;
    char d[1300], f[1300];
    snprintf(d, sizeof d, "%s", at("f72du/d"));
    snprintf(f, sizeof f, "%s", at("f72du/f"));
    const char *one[] = { d };
    ticks = 0;
    CHECK(du_walk(&w, one, 1, &s));
    CHECK(s.dirs == 3 && s.files == 4 && s.bytes == 10 + 20 + 1 + 3 && !s.cut && ticks == 1);   // d, sub, deeper; a, b, la, lsub
    const char *both[] = { d, f };
    CHECK(du_walk(&w, both, 2, &s) && s.dirs == 3 && s.files == 5 && s.bytes == 39);
    // A root that is gone, a folder that can't be read: what could be counted, and cut.
    const char *gone[] = { at("f72du/none"), f };
    CHECK(du_walk(&w, gone, 2, &s) && s.cut && s.files == 1 && s.bytes == 5);
    CHECK(chmod(at("f72du/d/sub"), 0) == 0);
    CHECK(du_walk(&w, one, 1, &s) && s.cut && s.dirs == 2 && s.files == 3);
    CHECK(chmod(at("f72du/d/sub"), 0755) == 0);
    // Stopped: another generation wanted.
    gen = 8;
    CHECK(!du_walk(&w, one, 1, &s));
    gen = 7;
    // A tick every DU_TICK entries, and one at the end.
    mkdirs(at("f72du/many"));
    for (int i = 0; i < DU_TICK + 10; i++) {
        char p[64];
        snprintf(p, sizeof p, "f72du/many/x%d", i);
        put(at("%s", p), "");
    }
    const char *many[] = { at("f72du/many") };
    ticks = 0;
    CHECK(du_walk(&w, many, 1, &s) && s.files == DU_TICK + 10 && ticks == 2);
    // Stopped in the middle (the tick moves the generation at DU_TICK entries), and between roots
    // that are only files.
    ticks = 0;
    DuWalk stopper = { .gen = &gen, .mine = 7, .tick = stop_at_tick, .ctx = &gen };
    const char *many_f[] = { at("f72du/many"), f };
    CHECK(!du_walk(&stopper, many_f, 2, &s) && s.files == DU_TICK);
    gen = 7;
    CHECK(!du_walk(&stopper, many_f, 1, &s));   // stopped in the last root too
    gen = 7;
    const char *files[] = { f, f };
    stopper.tick = NULL, gen = 9;
    CHECK(!du_walk(&stopper, files, 2, &s) && s.files == 0);
    gen = 7;
    // Nested past DU_DEPTH_MAX: counted that far, and cut.
    char deep[4096] = "f72du/deep";
    for (int i = 0; i < DU_DEPTH_MAX + 4; i++) strcat(deep, "/d");
    mkdirs(at("%s", deep));
    const char *deeproot[] = { at("f72du/deep") };
    CHECK(du_walk(&w, deeproot, 1, &s) && s.cut && s.dirs == DU_DEPTH_MAX + 1);
    // Without a generation to watch (no stop), without a tick.
    DuWalk plain = { 0 };
    CHECK(du_walk(&plain, one, 1, &s) && s.dirs == 3);
}

static void test_chmod(void)
{
    ops_guard(root);
    // The grid's places: owner read .. others execute.
    CHECK(perm_bit(0) == 0400 && perm_bit(1) == 0200 && perm_bit(2) == 0100 && perm_bit(4) == 0020 && perm_bit(8) == 0001);
    put(at("f72ch/f"), "x");
    chmod(at("f72ch/f"), 0644);
    mode_t old;
    Err err;
    struct stat st;
    CHECK(op_chmod(at("f72ch/f"), 0600, &old, &err) && old == 0644 && stat(at("f72ch/f"), &st) == 0 && (st.st_mode & 07777) == 0600);
    // The special bits stay: a folder's setgid.
    mkdirs(at("f72ch/g"));
    chmod(at("f72ch/g"), 02755);
    CHECK(op_chmod(at("f72ch/g"), 0750, &old, &err) && old == 02755 && stat(at("f72ch/g"), &st) == 0 && (st.st_mode & 07777) == 02750);
    // A link: its target changes, the link's own path guarded by the target's real one.
    CHECK(symlink("f", at("f72ch/lf")) == 0);
    CHECK(op_chmod(at("f72ch/lf"), 0640, &old, &err) && old == 0600 && stat(at("f72ch/f"), &st) == 0 && (st.st_mode & 0777) == 0640);
    char victim[1300];
    snprintf(victim, sizeof victim, "%s/victim", decoy);
    put(victim, "keep");
    chmod(victim, 0644);
    CHECK(symlink(victim, at("f72ch/out")) == 0);
    CHECK(!op_chmod(at("f72ch/out"), 0777, &old, &err) && strstr(err.msg, "outside the test folder"));
    CHECK(stat(victim, &st) == 0 && (st.st_mode & 0777) == 0644);
    CHECK(!op_chmod(victim, 0777, &old, &err) && stat(victim, &st) == 0 && (st.st_mode & 0777) == 0644);
    unlink(victim);
    CHECK(!op_chmod(at("f72ch/none"), 0644, &old, &err) && strstr(err.msg, "No such file"));
    CHECK(symlink("none", at("f72ch/broken")) == 0);
    CHECK(!op_chmod(at("f72ch/broken"), 0644, &old, &err) && strstr(err.msg, "f72ch/broken: No such file"));
    // Its undo puts the mode back, and says what it undoes.
    Undo un = { .arena = &ta };
    undo_begin(&un, UNDO_CHMOD), undo_add(&un, NULL, at("f72ch/f"), "644");
    char what[64];
    undo_what(&un, what, sizeof what);
    CHECK_STR(str_from_c(what), "permission change");
    isize done;
    CHECK(undo_do(&un, &done, &err) && done == 1 && stat(at("f72ch/f"), &st) == 0 && (st.st_mode & 0777) == 0644);
    undo_begin(&un, UNDO_CHMOD), undo_add(&un, NULL, at("f72ch/gone"), "644");
    CHECK(!undo_do(&un, &done, &err) && strstr(err.msg, "is gone"));
}

static UiAction props_keys(Ui *u, int n, ...)
{
    UiInput in = ui_input_none();
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        int k = va_arg(ap, int);
        in.ev[in.nev++] = k >= 0x100 ? (UiEvent){ UK_NONE, (u32)(k - 0x100) } : (UiEvent){ (UiKey)k, 0 };
    }
    va_end(ap);
    return ui_handle(u, (UiNames){ name_at, NULL, NULL }, &in);
}

static void test_props_keys(void)
{
    // Ctrl+I opens it, the focus on the grid's first box; the menu's entry too.
    Ui u = menu_ui(3);
    CHECK(props_keys(&u, 1, UK_PROPERTIES).kind == UA_PROPS && u.props && u.prop_at == 0);
    u.prop_can = true;
    // Arrows and h j k l move, stopping at the edges.
    props_keys(&u, 2, UK_LEFT, UK_UP);
    CHECK(u.prop_at == 0);
    props_keys(&u, 2, UK_RIGHT, UK_DOWN);
    CHECK(u.prop_at == 4);
    props_keys(&u, 3, UK_RIGHT, UK_RIGHT, UK_DOWN);
    CHECK(u.prop_at == 8);
    props_keys(&u, 2, UK_DOWN, UK_RIGHT);
    CHECK(u.prop_at == 8);
    props_keys(&u, 2, 0x100 + 'h', 0x100 + 'k');
    CHECK(u.prop_at == 4);
    props_keys(&u, 2, 0x100 + 'j', 0x100 + 'l');
    CHECK(u.prop_at == 8);
    props_keys(&u, 2, UK_SEL_LEFT, UK_SEL_RIGHT);
    CHECK(u.prop_at == 8);
    // Space and Enter flip the focused bit, when it may change.
    UiAction a = props_keys(&u, 1, 0x100 + ' ');
    CHECK(a.kind == UA_CHMOD && a.index == 8);
    a = props_keys(&u, 1, UK_ENTER);
    CHECK(a.kind == UA_CHMOD && a.index == 8);
    a = props_keys(&u, 1, UK_SHIFT_ENTER);
    CHECK(a.kind == UA_CHMOD);
    u.prop_can = false;
    CHECK(props_keys(&u, 2, UK_ENTER, 0x100 + ' ').kind == UA_NONE && u.props);
    // Every other key is the panel's: no terminal, no trash, no menu, no find.
    CHECK(props_keys(&u, 6, 0x100 + 't', UK_DELETE, UK_F10, UK_FIND, UK_TAB_NEW, UK_TERMINAL).kind == UA_NONE && u.props && !u.menu_on && !u.finding);
    // Esc closes it; so does Ctrl+I again.
    props_keys(&u, 1, UK_ESC);
    CHECK(!u.props && u.prop_at == 8);
    props_keys(&u, 1, UK_PROPERTIES);   // open again: the focus back on the first box
    CHECK(u.props && u.prop_at == 0);
    // Clicks behind the panel go nowhere: no row, no header, no bar button.
    UiInput behind = ui_input_none();
    behind.click = 2, behind.header = 1, behind.bar = UB_UP;
    CHECK(ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &behind).kind == UA_NONE && u.list.sel == 0 && u.sort.key == 0);
    // A question over it: its boxes take no press.
    u.quit_asking = true, u.prop_can = true;
    behind = ui_input_none(), behind.prop_box = 3;
    CHECK(ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &behind).kind == UA_NONE);
    u.quit_asking = false;
    // A clash or a delete question over it takes the keys: s skips, y deletes.
    u.asking = true;
    CHECK(props_keys(&u, 1, 0x100 + 's').kind == UA_ANSWER && u.props);
    u.confirming = true;
    CHECK(props_keys(&u, 1, 0x100 + 'y').kind == UA_DELETE && u.props);
    props_keys(&u, 1, UK_ESC);
    CHECK(!u.props);
    props_keys(&u, 1, UK_PROPERTIES);
    props_keys(&u, 1, UK_PROPERTIES);
    CHECK(!u.props);
    // The window closing still closes it (asks first with jobs).
    props_keys(&u, 1, UK_PROPERTIES);
    CHECK(props_keys(&u, 1, UK_QUIT).kind == UA_QUIT);
    // The menu's entry, from a rename: the edit line closes (Ctrl+I there is the line's, as any key).
    u = menu_ui(3);
    ui_edit_open(&u, S("a.txt"));
    CHECK(props_keys(&u, 1, UK_PROPERTIES).kind == UA_NONE && u.editing && !u.props);
    menu_keys(&u, 1, UK_F10);
    CHECK(!u.editing);
    u.menu_at = file_at(UM_PROPERTIES);
    CHECK(menu_keys(&u, 1, UK_ENTER).kind == UA_PROPS && u.props && !u.menu_on);
    // The pointer: a box takes the focus and flips; a greyed one only the focus; Close closes.
    UiNames names = { name_at, NULL, NULL };
    UiInput in = ui_input_none();
    in.prop_box = 5;
    u.prop_can = true;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_CHMOD && a.index == 5 && u.prop_at == 5);
    CHECK(ui_input_acts(&in));
    u.prop_can = false, in.prop_box = 2;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.prop_at == 2);
    in.prop_box = 9;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.prop_at == 2);
    in = ui_input_none(), in.modal = UI_MODAL_OK;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.props);
    in = ui_input_none(), in.prop_box = 1;   // the panel down: a box means nothing
    u.prop_can = true;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    // Under the queue-full note and a quit question the panel's keys wait.
    props_keys(&u, 1, UK_PROPERTIES);
    u.quit_asking = true;
    in = job_input(1, 1), in.ev[in.nev++] = (UiEvent){ UK_NONE, 'n' };
    ui_handle(&u, names, &in);
    CHECK(!u.quit_asking && u.props);
    // Alt+Enter: a terminal, as t; not in the trash view.
    u = menu_ui(3);
    CHECK(props_keys(&u, 1, UK_TERMINAL).kind == UA_TERMINAL);
    u.in_trash = true;
    CHECK(props_keys(&u, 1, UK_TERMINAL).kind == UA_NOT_IN_TRASH);
}

static void test_step_round(void)
{
    // The list: Up from the first to the last, Down from the last to the first; k and j too.
    Ui u = menu_ui(5);
    u.list.rows = 3;
    props_keys(&u, 1, UK_UP);
    CHECK(u.list.sel == 4 && u.list.top == 2);
    props_keys(&u, 1, 0x100 + 'j');
    CHECK(u.list.sel == 0 && u.list.top == 0);
    props_keys(&u, 1, 0x100 + 'k');
    CHECK(u.list.sel == 4);
    // PgUp, PgDn, Home and End stop at the ends.
    props_keys(&u, 1, UK_PAGEDOWN);
    CHECK(u.list.sel == 4);
    props_keys(&u, 2, UK_HOME, UK_PAGEUP);
    CHECK(u.list.sel == 0);
    // One entry: round to itself; none: nothing.
    Ui one = menu_ui(1);
    props_keys(&one, 2, UK_UP, UK_DOWN);
    CHECK(one.list.sel == 0);
    Ui none = menu_ui(0);
    props_keys(&none, 2, UK_UP, UK_DOWN);
    CHECK(none.list.sel == 0);
    // The sidebar doesn't go round.
    u.side.rows = 10;
    ui_set(&u.side, 3, 0);
    u.side_focus = true;
    props_keys(&u, 1, UK_UP);
    CHECK(u.side.sel == 0);
    u.side.sel = 2;
    props_keys(&u, 1, UK_DOWN);
    CHECK(u.side.sel == 2);
    // The icon view, 3 columns, 7 entries (rows 0-2, 3-5, 6): Left and Right round the ends; Up
    // from the top row to the bottom row's cell in its column, or the row above when it lacks one;
    // Down from the bottom row (and from a cell with none under it) to the top, in its column.
    Ui g = menu_ui(7);
    g.grid = true, g.list.rows = 2;
    ui_set_cols(&g.list, 3);
    props_keys(&g, 1, UK_LEFT);
    CHECK(g.list.sel == 6);
    props_keys(&g, 1, UK_RIGHT);
    CHECK(g.list.sel == 0);
    props_keys(&g, 1, UK_UP);
    CHECK(g.list.sel == 6);
    props_keys(&g, 1, UK_DOWN);
    CHECK(g.list.sel == 0);
    g.list.sel = 2;
    props_keys(&g, 1, UK_UP);
    CHECK(g.list.sel == 5);
    props_keys(&g, 1, UK_DOWN);   // row 1 to the last row: column 2 isn't there, the last entry
    CHECK(g.list.sel == 6);
    g.list.sel = 4;
    props_keys(&g, 1, 0x100 + 'j');
    CHECK(g.list.sel == 6);
    props_keys(&g, 2, 0x100 + 'j', 0x100 + 'l');
    CHECK(g.list.sel == 1);
    props_keys(&g, 1, 0x100 + 'h');
    CHECK(g.list.sel == 0);
    props_keys(&g, 1, 0x100 + 'h');
    CHECK(g.list.sel == 6);
    // PgUp and PgDn stop; past the end PgDn takes the last row's cell in the column.
    g.list.sel = 1;
    props_keys(&g, 1, UK_PAGEUP);
    CHECK(g.list.sel == 1);
    Ui g8 = menu_ui(8);   // rows 0-2, 3-5, 6-7
    g8.grid = true, g8.list.rows = 4;
    ui_set_cols(&g8.list, 3);
    props_keys(&g8, 1, UK_PAGEDOWN);   // a page of 3 rows from 0: past the end, column 0 of the last row
    CHECK(g8.list.sel == 6);
}
