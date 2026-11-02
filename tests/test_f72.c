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
    // Hard links: every name counted, and each file once (a, its two other names, one in a sub
    // folder; c alone). Without the slots, nothing told apart; the slots cleared for each walk.
    static DuInode slots[DU_INODES];
    mkdirs(at("f72du/hl/in"));
    put(at("f72du/hl/a"), "0123456789");
    put(at("f72du/hl/c"), "012");
    CHECK(link(at("f72du/hl/a"), at("f72du/hl/b")) == 0 && link(at("f72du/hl/a"), at("f72du/hl/in/a2")) == 0);
    const char *hl[] = { at("f72du/hl") };
    DuWalk lw = { .inodes = slots };
    CHECK(du_walk(&lw, hl, 1, &s) && s.files == 4 && s.bytes == 33);
    CHECK(s.linked_names == 3 && s.linked_files == 1 && s.once_bytes == 13 && !s.once_over);
    CHECK(du_walk(&lw, hl, 1, &s) && s.linked_files == 1 && s.once_bytes == 13);
    CHECK(du_walk(&plain, hl, 1, &s) && s.linked_names == 0 && s.once_bytes == 33);
    // A root that is a linked file counts the same, and as a name of a file seen inside another root.
    const char *hl2[] = { at("f72du/hl/b"), at("f72du/hl") };
    CHECK(du_walk(&lw, hl2, 2, &s) && s.files == 5 && s.linked_names == 4 && s.linked_files == 1 && s.once_bytes == 13);
    // More linked files than slots: each one past them counted as new, and said.
    for (isize i = 0; i < DU_INODES; i++) slots[i] = (DuInode){ 1, (u64)i + 1 };
    DuSum full = { 0 };
    struct stat st;
    CHECK(lstat(at("f72du/hl/a"), &st) == 0);
    du_file(&lw, &st, &full), du_file(&lw, &st, &full);
    CHECK(full.linked_names == 2 && full.linked_files == 2 && full.once_bytes == 20 && full.once_over);
    // The same inode number on another drive is another file.
    memset(slots, 0, sizeof slots);
    DuSum two = { 0 };
    struct stat other = st;
    other.st_dev = st.st_dev + 1;
    du_file(&lw, &st, &two), du_file(&lw, &other, &two);
    CHECK(two.linked_files == 2 && two.once_bytes == 20);
    // Every slot holding its inode number on another drive: none of them is it.
    for (isize i = 0; i < DU_INODES; i++) slots[i] = (DuInode){ (u64)other.st_dev + 1, (u64)st.st_ino };
    DuSum taken = { 0 };
    du_file(&lw, &st, &taken);
    CHECK(taken.linked_files == 1 && taken.once_bytes == 10 && taken.once_over);
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
    stat(at("f72ch/f"), &st);
    char was[64];
    snprintf(was, sizeof was, "644 %llu %llu", (unsigned long long)st.st_dev, (unsigned long long)st.st_ino);
    undo_begin(&un, UNDO_CHMOD), undo_add(&un, NULL, at("f72ch/f"), was);
    char what[64];
    undo_what(&un, what, sizeof what);
    CHECK_STR(str_from_c(what), "permission change");
    isize done;
    CHECK(undo_do(&un, &done, &err) && done == 1 && stat(at("f72ch/f"), &st) == 0 && (st.st_mode & 0777) == 0644);
    undo_begin(&un, UNDO_CHMOD), undo_add(&un, NULL, at("f72ch/gone"), "644 1 1");
    CHECK(!undo_do(&un, &done, &err) && strstr(err.msg, "is gone"));
    // Another file at the path since (the review's case: replaced by a link to another file):
    // refused, nothing changed; a record without the inode, too.
    put(at("f72ch/other"), "o");
    chmod(at("f72ch/other"), 0600);
    chmod(at("f72ch/f"), 0640);
    undo_begin(&un, UNDO_CHMOD), undo_add(&un, NULL, at("f72ch/f"), was);
    unlink(at("f72ch/f"));
    CHECK(symlink("other", at("f72ch/f")) == 0);
    CHECK(!undo_do(&un, &done, &err) && strstr(err.msg, "another file now") && stat(at("f72ch/other"), &st) == 0 && (st.st_mode & 0777) == 0600);
    undo_begin(&un, UNDO_CHMOD), undo_add(&un, NULL, at("f72ch/other"), "644");
    CHECK(!undo_do(&un, &done, &err) && strstr(err.msg, "another file now"));
    stat(at("f72ch/other"), &st);
    snprintf(was, sizeof was, "644 %llu %llu", (unsigned long long)st.st_dev + 1, (unsigned long long)st.st_ino);
    undo_begin(&un, UNDO_CHMOD), undo_add(&un, NULL, at("f72ch/other"), was);
    CHECK(!undo_do(&un, &done, &err) && strstr(err.msg, "another file now"));
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
    CHECK(props_keys(&u, 1, UK_PROPERTIES).kind == UA_PROPS && u.panel == UI_PANEL_PROPS && u.prop_at == 0);
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
    CHECK(props_keys(&u, 2, UK_ENTER, 0x100 + ' ').kind == UA_NONE && u.panel == UI_PANEL_PROPS);
    // Every other key is the panel's: no terminal, no trash, no menu, no find.
    CHECK(props_keys(&u, 6, 0x100 + 't', UK_DELETE, UK_F10, UK_FIND, UK_TAB_NEW, UK_TERMINAL).kind == UA_NONE && u.panel == UI_PANEL_PROPS && !u.menu_on && !u.finding);
    // Esc closes it; so does Ctrl+I again.
    props_keys(&u, 1, UK_ESC);
    CHECK(u.panel != UI_PANEL_PROPS && u.prop_at == 8);
    props_keys(&u, 1, UK_PROPERTIES);   // open again: the focus back on the first box
    CHECK(u.panel == UI_PANEL_PROPS && u.prop_at == 0);
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
    CHECK(props_keys(&u, 1, 0x100 + 's').kind == UA_ANSWER && u.panel == UI_PANEL_PROPS);
    u.confirming = true;
    CHECK(props_keys(&u, 1, 0x100 + 'y').kind == UA_DELETE && u.panel == UI_PANEL_PROPS);
    props_keys(&u, 1, UK_ESC);
    CHECK(u.panel != UI_PANEL_PROPS);
    props_keys(&u, 1, UK_PROPERTIES);
    props_keys(&u, 1, UK_PROPERTIES);
    CHECK(u.panel != UI_PANEL_PROPS);
    // The window closing still closes it (asks first with jobs).
    props_keys(&u, 1, UK_PROPERTIES);
    CHECK(props_keys(&u, 1, UK_QUIT).kind == UA_QUIT);
    // The menu's entry, from a rename: the edit line closes (Ctrl+I there is the line's, as any key).
    u = menu_ui(3);
    ui_edit_open(&u, S("a.txt"));
    CHECK(props_keys(&u, 1, UK_PROPERTIES).kind == UA_NONE && (u.focus == UI_FOCUS_EDIT) && u.panel != UI_PANEL_PROPS);
    menu_keys(&u, 1, UK_F10);
    CHECK(u.focus != UI_FOCUS_EDIT);
    u.menu_at = file_at(UM_PROPERTIES);
    CHECK(menu_keys(&u, 1, UK_ENTER).kind == UA_PROPS && u.panel == UI_PANEL_PROPS && !u.menu_on);
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
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.panel != UI_PANEL_PROPS);
    in = ui_input_none(), in.prop_box = 1;   // the panel down: a box means nothing
    u.prop_can = true;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    // Under the queue-full note and a quit question the panel's keys wait.
    props_keys(&u, 1, UK_PROPERTIES);
    u.quit_asking = true;
    in = job_input(1, 1), in.ev[in.nev++] = (UiEvent){ UK_NONE, 'n' };
    ui_handle(&u, names, &in);
    CHECK(!u.quit_asking && u.panel == UI_PANEL_PROPS);
    // Alt+Enter: a terminal, as t; not in the trash view.
    u = menu_ui(3);
    CHECK(props_keys(&u, 1, UK_TERMINAL).kind == UA_TERMINAL);
    u.view = UI_VIEW_TRASH;
    CHECK(props_keys(&u, 1, UK_TERMINAL).kind == UA_NOT_IN_TRASH);
}

static void test_tilde(void)
{
    char b[64];
    ui_tilde(b, sizeof b, "/home/m/a", "/home/m");
    CHECK_STR(str_from_c(b), "~/a");
    ui_tilde(b, sizeof b, "/home/m", "/home/m");
    CHECK_STR(str_from_c(b), "~");
    ui_tilde(b, sizeof b, "/home/mx/a", "/home/m");   // a sibling that only starts the same
    CHECK_STR(str_from_c(b), "/home/mx/a");
    ui_tilde(b, sizeof b, "/a", "/");   // home "/" isn't shortened
    CHECK_STR(str_from_c(b), "/a");
    ui_tilde(b, sizeof b, "/", "/");
    CHECK_STR(str_from_c(b), "/");
    ui_tilde(b, sizeof b, "/a", NULL);
    CHECK_STR(str_from_c(b), "/a");
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
    u.focus = UI_FOCUS_SIDE;
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

// The Type line's words: the untranslated <comment>, entities read, the user's data dir first;
// a type that would leave mime/ is refused.
static void test_mime_comment(void)
{
    mkdirs(at("f72mc/home/mime/text"));
    mkdirs(at("f72mc/sys/mime/text"));
    mkdirs(at("f72mc/sys/mime/image"));
    put(at("f72mc/home/mime/text/x-mine.xml"), "<mime-type type=\"text/x-mine\">\n  <comment xml:lang=\"nl\">Mijn</comment>\n"
                                               "  <comment>Tom &amp; Jerry&apos;s &lt;notes&gt; &quot;x&quot; &bogus;</comment>\n</mime-type>\n");
    put(at("f72mc/sys/mime/text/x-mine.xml"), "<comment>shadowed</comment>");
    put(at("f72mc/sys/mime/image/x-cut.xml"), "<comment>never closed");
    put(at("f72mc/sys/mime/image/x-none.xml"), "<mime-type/>");
    // What a type with ".." would reach, outside mime/: never read.
    put(at("f72mc/sys/mime/x.xml"), "<comment>above</comment>");
    put(at("f72mc/sys/x.xml"), "<comment>outside</comment>");
    put(at("f72mc/sys/mime/text/x.xml"), "<comment>beside</comment>");
    env_set("XDG_DATA_HOME", at("f72mc/home"));
    env_set("XDG_DATA_DIRS", at("f72mc/sys"));
    char out[64];
    CHECK(mime_comment(&ta, S("text/x-mine"), out, sizeof out));
    CHECK(strcmp(out, "Tom & Jerry's <notes> \"x\" &bogus;") == 0);
    char tiny[5];
    memset(tiny, 'X', sizeof tiny);
    CHECK(mime_comment(&ta, S("text/x-mine"), tiny, sizeof tiny) && strcmp(tiny, "Tom ") == 0);
    CHECK(!mime_comment(&ta, S("image/x-cut"), out, sizeof out) && out[0] == 0);
    CHECK(!mime_comment(&ta, S("image/x-none"), out, sizeof out));
    CHECK(!mime_comment(&ta, S("image/x-gone"), out, sizeof out));
    const char *bad[] = { "x", "/x", "../x", "text/../../x", "../text/x-mine", "text/../x" };
    for (int i = 0; i < COUNT(bad); i++) CHECK(!mime_comment(&ta, str_from_c(bad[i]), out, sizeof out));
    env_restore();
}

// gio's type for a path: the content-type line of `gio info`, nothing when it lacks one, takes too
// long, or gives more than fits.
static void test_gio_type(void)
{
    char out[32];
    mkdirs(at("f72gio/ok"));
    mkdirs(at("f72gio/none"));
    mkdirs(at("f72gio/slow"));
    mkdirs(at("f72gio/long"));
    put(at("f72gio/ok/gio"), "#!/bin/sh\necho \"$*\" > \"${0%/*}/args\"\n"
                             "printf 'uri: file:///x\\nattributes:\\n  standard::content-type: application/x-zerosize\\n'\n");
    put(at("f72gio/none/gio"), "#!/bin/sh\nprintf 'uri: file:///x\\nattributes:\\n'\n");
    put(at("f72gio/slow/gio"), "#!/bin/sh\nprintf 'attributes:\\n  standard::content-type: text/plain\\n'\nexec /usr/bin/sleep 5\n");
    put(at("f72gio/long/gio"), "#!/bin/sh\nprintf 'attributes:\\n  standard::content-type: application/a-type-longer-than-the-room\\n'\n");
    const char *dirs[] = { "ok", "none", "slow", "long" };
    for (int i = 0; i < COUNT(dirs); i++) chmod(at("f72gio/%s/gio", dirs[i]), 0755);
    env_set("PATH", at("f72gio/ok"));
    CHECK(spawn_gio_type("/a b", out, sizeof out, 2000) && strcmp(out, "application/x-zerosize") == 0);
    char args[256];
    CHECK(wait_file(at("f72gio/ok/args"), args, sizeof args) && strcmp(args, "info -a standard::content-type -- /a b") == 0);
    env_restore();
    env_set("PATH", at("f72gio/none"));
    CHECK(!spawn_gio_type("/a", out, sizeof out, 2000) && out[0] == 0);
    env_restore();
    env_set("PATH", at("f72gio/slow"));
    i64 t0 = now_ms();
    CHECK(!spawn_gio_type("/a", out, sizeof out, 200) && out[0] == 0 && now_ms() - t0 < 2000);
    env_restore();
    env_set("PATH", at("f72gio/long"));
    CHECK(!spawn_gio_type("/a", out, sizeof out, 2000) && out[0] == 0);
    env_restore();
    env_set("PATH", at("f72gio/empty-dir-not-there"));
    CHECK(!spawn_gio_type("/a", out, sizeof out, 2000) && out[0] == 0);
    env_restore();
}
