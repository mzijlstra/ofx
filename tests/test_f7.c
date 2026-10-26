#include <sys/prctl.h>
#include <sys/wait.h>

// F7.1's unit tests: the menu bar's keys and clicks, Create New Empty File and its undo, the state
// file's one-key save, New Window's launch, and the window registry. Included by test.c.

// A Ui with n entries in its list, in a folder that takes new files.
static Ui menu_ui(isize n)
{
    Ui u = { 0 };
    u.list.rows = 10, u.can_write = true;
    ui_set(&u.list, n, 0);
    return u;
}

// The keys given, one frame, through ui_handle.
static UiAction menu_keys(Ui *u, int n, ...)
{
    UiInput in = ui_input_none();
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        int k = va_arg(ap, int);
        in.ev[in.nev++] = k < 0 ? (UiEvent){ UK_MENU, (u32)-k } : k >= 0x100 ? (UiEvent){ UK_NONE, (u32)(k - 0x100) } : (UiEvent){ (UiKey)k, 0 };
    }
    va_end(ap);
    return ui_handle(u, (UiNames){ name_at, NULL, NULL }, &in);
}

// The place in the File menu of entry it.
static int file_at(UiMenuItem it)
{
    for (int i = 0; i < UI_MENU[0].n; i++)
        if (UI_MENU[0].items[i] == it) return i;
    return -1;
}

static void test_menu_keys(void)
{
    // The table: every entry has a label and a word; File's Alt letter is f; the groups as decided.
    for (int it = 1; it < UM_ITEMS; it++) CHECK(UI_MENU_ITEM[it].label[0] && UI_MENU_ITEM[it].word[0]);
    CHECK(UI_MENUS == 1 && strcmp(UI_MENU[0].title, "File") == 0 && ui_menu_of('f') == 0 && ui_menu_of('e') == -1);
    CHECK(UI_MENU[0].n == 12 && file_at(UM_NEW_TAB) == 0 && file_at(UM_CLOSE) == 11 && UI_MENU[0].items[4] == UM_SEP);

    // F10 opens File, its first entry marked; Down steps over a line and what can't be picked.
    Ui u = menu_ui(3);
    CHECK(menu_keys(&u, 1, UK_F10).kind == UA_NONE && u.menu_on && u.menu == 0 && u.menu_at == file_at(UM_NEW_TAB));
    menu_keys(&u, 4, UK_DOWN, UK_DOWN, UK_DOWN, UK_DOWN);
    CHECK(u.menu_at == file_at(UM_OPEN));
    menu_keys(&u, 1, UK_DOWN);
    CHECK(u.menu_at == file_at(UM_OPEN_WITH));
    menu_keys(&u, 1, UK_DOWN);   // past the line
    CHECK(u.menu_at == file_at(UM_PROPERTIES));
    menu_keys(&u, 1, UK_DOWN);
    CHECK(u.menu_at == file_at(UM_CLOSE_ALL));
    menu_keys(&u, 2, UK_DOWN, UK_DOWN);   // round the end
    CHECK(u.menu_at == file_at(UM_NEW_TAB));
    menu_keys(&u, 1, UK_UP);
    CHECK(u.menu_at == file_at(UM_CLOSE));
    menu_keys(&u, 2, 0x100 + 'k', 0x100 + 'k');   // k and j as Up and Down
    CHECK(u.menu_at == file_at(UM_PROPERTIES));
    menu_keys(&u, 1, 0x100 + 'j');
    CHECK(u.menu_at == file_at(UM_CLOSE_ALL));
    menu_keys(&u, 1, UK_HOME);
    CHECK(u.menu_at == file_at(UM_NEW_TAB));
    menu_keys(&u, 1, UK_END);
    CHECK(u.menu_at == file_at(UM_CLOSE));
    // Left and Right: the menu that way (one menu so far: File again, its first entry).
    menu_keys(&u, 1, UK_RIGHT);
    CHECK(u.menu_on && u.menu == 0 && u.menu_at == file_at(UM_NEW_TAB));
    menu_keys(&u, 2, UK_DOWN, 0x100 + 'h');
    CHECK(u.menu_on && u.menu_at == file_at(UM_NEW_TAB));
    // A typed character goes no further: t would start a terminal.
    CHECK(menu_keys(&u, 2, 0x100 + 't', 0x100 + '~').kind == UA_NONE && u.menu_on);
    // Esc, F10 and Alt+F close it; Alt+F opens it too, and an Alt letter with no menu does nothing.
    menu_keys(&u, 1, UK_ESC);
    CHECK(!u.menu_on);
    menu_keys(&u, 1, -'f');
    CHECK(u.menu_on && u.menu_at == 0);
    menu_keys(&u, 1, UK_F10);
    CHECK(!u.menu_on);
    menu_keys(&u, 1, -'z');
    CHECK(!u.menu_on);
    menu_keys(&u, 2, -'f', -'f');
    CHECK(!u.menu_on);
    u.side.rows = 10;
    ui_set(&u.side, 3, 0);
    menu_keys(&u, 2, -'f', UK_TAB);
    CHECK(!u.menu_on && !u.side_focus);   // Tab only closes it, the sidebar not reached

    // Enter picks: each entry's action, the menu closed.
    static const struct { UiMenuItem it; UiActionKind a; isize index; } PICK[] = {
        { UM_NEW_TAB, UA_TAB_NEW, 0 }, { UM_NEW_WINDOW, UA_NEW_WINDOW, 0 }, { UM_NEW_FOLDER, UA_NEWFOLDER, 0 },
        { UM_NEW_FILE, UA_NEWFILE, 0 }, { UM_OPEN, UA_OPEN, 2 }, { UM_OPEN_WITH, UA_OPEN_WITH, 0 }, { UM_PROPERTIES, UA_PROPS, 0 }, { UM_CLOSE_ALL, UA_CLOSE_ALL, 0 },
        { UM_CLOSE, UA_TAB_CLOSE, -1 },
    };
    for (int k = 0; k < COUNT(PICK); k++) {
        u = menu_ui(3);
        u.list.sel = 2;
        menu_keys(&u, 1, UK_F10);
        u.menu_at = file_at(PICK[k].it);
        UiAction a = menu_keys(&u, 1, UK_ENTER);
        CHECK(a.kind == PICK[k].a && a.index == PICK[k].index && !u.menu_on);
        if (PICK[k].it == UM_CLOSE_ALL) CHECK(a.all);
    }
    // One that can't be picked (Open With in the trash view): Enter does nothing, the menu stays.
    u = menu_ui(3);
    u.in_trash = true;
    menu_keys(&u, 1, UK_F10);
    u.menu_at = file_at(UM_OPEN_WITH);
    CHECK(menu_keys(&u, 1, UK_ENTER).kind == UA_NONE && u.menu_on);
    u.menu_at = -1;   // nothing marked (the pointer opened it)
    CHECK(menu_keys(&u, 1, UK_ENTER).kind == UA_NONE && u.menu_on);
    menu_keys(&u, 1, UK_DOWN);
    CHECK(u.menu_at == 0);
    u.menu_at = -1;
    menu_keys(&u, 1, UK_UP);
    CHECK(u.menu_at == file_at(UM_CLOSE));
    // Any other key closes it and does what it does.
    CHECK(menu_keys(&u, 1, UK_TAB_NEW).kind == UA_TAB_NEW && !u.menu_on);
    menu_keys(&u, 1, UK_F10);
    CHECK(menu_keys(&u, 1, UK_BACKSPACE).kind == UA_PARENT && !u.menu_on);

    // Opening it closes the edit line (a rename cancelled) and the find line.
    u = menu_ui(3);
    ui_edit_open(&u, S("a.txt"));
    menu_keys(&u, 1, UK_F10);
    CHECK(u.menu_on && !u.editing);
    menu_keys(&u, 2, UK_ESC, UK_FIND);
    CHECK(u.finding);
    menu_keys(&u, 1, -'f');
    CHECK(u.menu_on && u.finding && !u.find_keys);   // the bar stays, its keys go to the menu
    // A question up: no menu, and one open closes.
    u = menu_ui(3);
    u.confirming = true;
    menu_keys(&u, 1, UK_F10);
    CHECK(!u.menu_on);
    u.confirming = false;
    menu_keys(&u, 1, UK_F10);
    u.empty_asking = true, u.in_trash = true;
    menu_keys(&u, 1, UK_DOWN);
    CHECK(!u.menu_on);

    // Ctrl+N; in the trash view it says no.
    u = menu_ui(3);
    CHECK(menu_keys(&u, 1, UK_NEW_WINDOW).kind == UA_NEW_WINDOW);
    u.in_trash = true;
    CHECK(menu_keys(&u, 1, UK_NEW_WINDOW).kind == UA_NOT_IN_TRASH);
}

static void test_menu_enabled(void)
{
    // What can be picked: Open needs an entry; the Create entries a folder that takes them, not the
    // trash; New Window not the trash; Open With an entry, not in the trash (F7.3).
    Ui u = menu_ui(3);
    bool want[UM_ITEMS] = { [UM_NEW_TAB] = true, [UM_NEW_WINDOW] = true, [UM_NEW_FOLDER] = true, [UM_NEW_FILE] = true,
                            [UM_OPEN] = true, [UM_OPEN_WITH] = true, [UM_PROPERTIES] = true, [UM_CLOSE_ALL] = true, [UM_CLOSE] = true };
    for (int it = 0; it < UM_ITEMS; it++) CHECK(ui_menu_enabled(&u, (UiMenuItem)it) == want[it]);
    u = menu_ui(0);
    CHECK(!ui_menu_enabled(&u, UM_OPEN) && !ui_menu_enabled(&u, UM_OPEN_WITH) && ui_menu_enabled(&u, UM_NEW_FILE));
    u = menu_ui(3);
    u.can_write = false;
    CHECK(!ui_menu_enabled(&u, UM_NEW_FOLDER) && !ui_menu_enabled(&u, UM_NEW_FILE) && ui_menu_enabled(&u, UM_NEW_WINDOW));
    u = menu_ui(3);
    u.in_trash = true;
    CHECK(!ui_menu_enabled(&u, UM_NEW_FOLDER) && !ui_menu_enabled(&u, UM_NEW_FILE) && !ui_menu_enabled(&u, UM_NEW_WINDOW));
    CHECK(!ui_menu_enabled(&u, UM_OPEN_WITH));
    CHECK(ui_menu_enabled(&u, UM_OPEN) && ui_menu_enabled(&u, UM_CLOSE) && ui_menu_enabled(&u, UM_NEW_TAB));
    // Opened by a key with nothing at the top to pick: the first that can be.
    u = menu_ui(0);
    u.in_trash = true;
    menu_keys(&u, 1, UK_F10);
    CHECK(u.menu_at == file_at(UM_NEW_TAB));
}

static void test_menu_mouse(void)
{
    UiNames names = { name_at, NULL, NULL };
    Ui u = menu_ui(3);
    // A title opens its menu with nothing marked, and closes it again.
    UiInput in = ui_input_none();
    in.menu_title = 0;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.menu_on && u.menu_at == -1);
    CHECK(ui_input_acts(&in));
    ui_handle(&u, names, &in);
    CHECK(!u.menu_on);
    in.menu_title = 1;   // no such menu
    ui_handle(&u, names, &in);
    CHECK(!u.menu_on);
    // The pointer marks what can be picked, not a line or a greyed entry.
    in.menu_title = 0;
    ui_handle(&u, names, &in);
    CHECK(ui_menu_hover(&u, -1, file_at(UM_NEW_FILE)) && u.menu_at == file_at(UM_NEW_FILE));
    CHECK(!ui_menu_hover(&u, -1, file_at(UM_NEW_FILE)));   // no change
    u.in_trash = true;   // Open With greyed there
    CHECK(!ui_menu_hover(&u, -1, file_at(UM_OPEN_WITH)) && u.menu_at == file_at(UM_NEW_FILE));
    u.in_trash = false;
    CHECK(!ui_menu_hover(&u, -1, 4) && !ui_menu_hover(&u, -1, -1) && !ui_menu_hover(&u, -1, 99));
    CHECK(!ui_menu_hover(&u, 0, -1));   // its own title
    CHECK(ui_menu_hover(&u, -1, 0) && u.menu_at == 0);
    Ui closed = menu_ui(3);
    closed.menu_at = -1;
    CHECK(!ui_menu_hover(&closed, 0, 0) && !closed.menu_on && closed.menu_at == -1);
    // A press on an entry picks it; on one greyed nothing happens and the menu stays.
    in = ui_input_none(), in.menu_item = file_at(UM_OPEN_WITH);
    u.in_trash = true;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.menu_on);
    u.in_trash = false;
    in.menu_item = 99;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.menu_on);
    in.menu_item = file_at(UM_NEW_FOLDER);
    CHECK(ui_handle(&u, names, &in).kind == UA_NEWFOLDER && !u.menu_on);
    CHECK(ui_input_acts(&in));
    // A press anywhere else closes it, and goes no further.
    in = ui_input_none(), in.menu_title = 0;
    ui_handle(&u, names, &in);
    in = ui_input_none(), in.menu_away = true, in.bar = UB_UP;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.menu_on);
    in = ui_input_none(), in.menu_away = true;
    CHECK(ui_input_acts(&in));
    in = ui_input_none(), in.menu_item = 0;   // a menu closed: an entry's place means nothing
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    in = ui_input_none(), in.menu_away = true, in.bar = UB_UP;   // and away is only the menu's
    CHECK(ui_handle(&u, names, &in).kind == UA_PARENT);
    // Under a question the titles do nothing.
    u.quit_asking = true;
    in = ui_input_none(), in.menu_title = 0;
    ui_handle(&u, names, &in);
    CHECK(!u.menu_on);
}

static void test_close_all_keys(void)
{
    UiNames names = { name_at, NULL, NULL };
    // Ctrl+Q: the others asked, this one quits; with jobs it asks first, the others asked anyway.
    Ui u = menu_ui(3);
    UiInput in = job_input(0, 0);
    in.ev[in.nev++] = (UiEvent){ UK_QUIT_ALL, 0 };
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_CLOSE_ALL && a.all && !u.quit_asking);
    in = job_input(1, 2), in.ev[in.nev++] = (UiEvent){ UK_QUIT_ALL, 0 };
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_CLOSE_ALL && !a.all && u.quit_asking);
    // Again while asked: nothing more; n stays.
    in = job_input(1, 2), in.ev[in.nev++] = (UiEvent){ UK_QUIT_ALL, 0 };
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.quit_asking);
    in = job_input(1, 2), in.ev[in.nev++] = (UiEvent){ UK_NONE, 'n' };
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.quit_asking);
    // With a menu open it closes, and Ctrl+Q still works.
    menu_keys(&u, 1, UK_F10);
    in = job_input(0, 0), in.ev[in.nev++] = (UiEvent){ UK_QUIT_ALL, 0 };
    CHECK(ui_handle(&u, names, &in).kind == UA_CLOSE_ALL && !u.menu_on);
    // The window closed, or another window's Close All (SIGUSR1): as before, this one only.
    in = job_input(0, 0), in.ev[in.nev++] = (UiEvent){ UK_QUIT, 0 };
    CHECK(ui_handle(&u, names, &in).kind == UA_QUIT);
    // Not lost to a click in the same frame (gui/ queues it once: reviewer-2, F7.1 review): a bar
    // button, a header, a double click, a menu title or entry, a press away from a menu, a job's ×.
    for (int c = 0; c < 7; c++) {
        Ui v = menu_ui(3);
        if (c >= 4) menu_keys(&v, 1, UK_F10);
        for (int jobs = 0; jobs < 2; jobs++) {
            in = job_input(jobs, jobs), in.ev[in.nev++] = (UiEvent){ UK_QUIT, 0 };
            switch (c) {
            case 0: in.bar = UB_UP; break;
            case 1: in.header = 1; break;
            case 2: in.dclick = 1; break;
            case 3: in.menu_title = 0; break;
            case 4: in.menu_item = file_at(UM_NEW_TAB); break;
            case 5: in.menu_away = true; break;
            default: in.job_x = 0; break;
            }
            v.quit_asking = false;
            a = ui_handle(&v, names, &in);
            CHECK(jobs ? a.kind == UA_NONE && v.quit_asking && !v.menu_on : a.kind == UA_QUIT);
        }
    }
}

static bool dir_at(void *ctx, isize i)
{
    (void)ctx;
    return i == 2;   // name_at's "gamma.txt", as a folder
}

static void test_rename_select(void)
{
    // Where the extension starts: the last '.', ".tar" before it, none for a dot-file.
    static const struct { const char *name; int stem; } STEM[] = {
        { "test.txt", 4 }, { "a.b.c", 3 }, { "archive.tar.gz", 7 }, { "x.tar", 1 }, { ".tar.gz", 4 }, { "a.tar.gz", 1 },
        { ".bashrc", 7 }, { "README", 6 }, { "a.", 1 }, { "..", 1 }, { ".a.txt", 2 }, { "tar.gz", 3 }, { "", 0 },
    };
    for (int k = 0; k < COUNT(STEM); k++) {
        CHECK(ui_name_stem(str_from_c(STEM[k].name)) == STEM[k].stem);
        if (ui_name_stem(str_from_c(STEM[k].name)) != STEM[k].stem) fprintf(stderr, "  stem %s\n", STEM[k].name);
    }
    // F2: a file's name up to its extension selected, the caret there; a folder's whole name.
    Ui u = menu_ui(3);
    UiInput in = ui_input_none();
    in.ev[in.nev++] = (UiEvent){ UK_F2, 0 };
    u.list.sel = 2;
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.editing && u.anchor == 0 && u.caret == 5);   // "gamma" of gamma.txt
    ui_edit_close(&u);
    ui_handle(&u, (UiNames){ name_at, NULL, dir_at }, &in);
    CHECK(u.editing && u.anchor == 0 && u.caret == 9);   // a folder: all of it
    ui_edit_close(&u);
    // Typing replaces the name and keeps the extension.
    ui_edit_rename(&u, S("test.txt"), false);
    ui_edit_event(&u, (UiEvent){ UK_NONE, 'n' }, STR_EMPTY);
    CHECK(u.edit_len == 5 && memcmp(u.edit, "n.txt", 5) == 0 && u.caret == 1);
}

static void test_new_file(void)
{
    // The name just made, all of it selected: typing replaces it (a rename keeps the caret at the
    // end, nothing selected).
    Ui u = menu_ui(1);
    ui_edit_open_all(&u, S("New file"));
    int lo, hi;
    ui_edit_sel(&u, &lo, &hi);
    CHECK(u.editing && lo == 0 && hi == 8 && u.caret == 8);
    ui_edit_event(&u, (UiEvent){ UK_NONE, 'x' }, STR_EMPTY);
    CHECK(u.edit_len == 1 && u.edit[0] == 'x');
    ui_edit_open(&u, S("a.txt"));
    ui_edit_sel(&u, &lo, &hi);
    CHECK(lo == hi && u.caret == 5);

    arena_reset(&ta);
    ops_guard(root);
    mkdirs(at("f7new"));
    char made[NAME_MAX + 1], p[PATH_MAX];
    Err err;
    struct stat st;
    CHECK(op_mkfile(at("f7new"), S("New file"), made, sizeof made, &err) && strcmp(made, "New file") == 0);
    CHECK(stat(at("f7new/New file"), &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 0);
    CHECK(op_mkfile(at("f7new"), S("New file"), made, sizeof made, &err) && strcmp(made, "New file (2)") == 0);
    mkdirs(at("f7new/New file (3)"));   // a folder takes the name too
    CHECK(op_mkfile(at("f7new"), S("New file"), made, sizeof made, &err) && strcmp(made, "New file (4)") == 0);
    CHECK(!op_mkfile(at("f7new"), S("a/b"), made, sizeof made, &err));
    CHECK(!op_mkfile(at("f7new/none"), S("New file"), made, sizeof made, &err) && strstr(err.msg, "No such file"));
    // Outside the test folder: refused, nothing made.
    CHECK(!op_mkfile(decoy, S("New file"), made, sizeof made, &err) && strstr(err.msg, "outside the test folder"));
    snprintf(p, sizeof p, "%s/New file", decoy);
    CHECK(access(p, F_OK) != 0);

    // Its undo: removed while empty; kept, and said so, once written to or replaced.
    Undo un = { .arena = &ta };
    undo_begin(&un, UNDO_MKFILE), undo_add(&un, NULL, at("f7new/New file"), NULL);
    char what[64];
    undo_what(&un, what, sizeof what);
    CHECK_STR(str_from_c(what), "new file");
    isize done;
    CHECK(undo_do(&un, &done, &err) && done == 1 && access(at("f7new/New file"), F_OK) != 0);
    put(at("f7new/New file (2)"), "kept\n");
    undo_begin(&un, UNDO_MKFILE), undo_add(&un, NULL, at("f7new/New file (2)"), NULL);
    CHECK(!undo_do(&un, &done, &err) && strstr(err.msg, "isn't empty now") && access(at("f7new/New file (2)"), F_OK) == 0);
    undo_begin(&un, UNDO_MKFILE), undo_add(&un, NULL, at("f7new/New file (3)"), NULL);
    CHECK(!undo_do(&un, &done, &err) && strstr(err.msg, "isn't a file now") && access(at("f7new/New file (3)"), F_OK) == 0);
    undo_begin(&un, UNDO_MKFILE), undo_add(&un, NULL, at("f7new/gone"), NULL);
    CHECK(!undo_do(&un, &done, &err) && strstr(err.msg, "is gone"));
    // Outside the test folder: refused by the guard, kept.
    snprintf(p, sizeof p, "%s/empty", decoy);
    put(p, "");
    undo_begin(&un, UNDO_MKFILE), undo_add(&un, NULL, p, NULL);
    CHECK(!undo_do(&un, &done, &err) && access(p, F_OK) == 0);
    unlink(p);
    // Two new files are two undos' worth: "new file of 2 items" is never said.
    undo_begin(&un, UNDO_MKFILE), undo_add(&un, NULL, "/a", NULL), undo_add(&un, NULL, "/b", NULL);
    undo_what(&un, what, sizeof what);
    CHECK_STR(str_from_c(what), "new file");
}

static void test_state_put(void)
{
    arena_reset(&ta);
    ops_guard(root);
    char path[PATH_MAX];
    Err err;
    snprintf(path, sizeof path, "%s", at("f7state/ofx/state"));
    // No file yet: made, its folder too.
    CHECK(state_put(&ta, path, "view", "icons", &err));
    CHECK_STR(read_file(&ta, path), "view=icons\n");
    // Two windows, each saving its own key: both kept (the whole State would undo the other's).
    State a = { SIDE_PLACES, true, 0 };
    CHECK(state_save(path, &a, &err));
    CHECK(state_put(&ta, path, "sidebar", "tree", &err));   // window B
    CHECK(state_put(&ta, path, "zoom", "96", &err));        // window A
    State s = state_load(&ta, path);
    CHECK(s.side == SIDE_TREE && s.icons && s.zoom == 96);
    CHECK_STR(read_file(&ta, path), "sidebar=tree\nview=icons\nzoom=96\n");
    // A key ofx doesn't know is kept; one said twice is said once; spaces around = are its key.
    put(path, "future=1\n view = list\nview=list\n\nsidebar=hidden");
    CHECK(state_put(&ta, path, "view", "icons", &err));
    CHECK_STR(read_file(&ta, path), "future=1\nview=icons\nsidebar=hidden\n");
    CHECK(state_put(&ta, path, "zoom", "32", &err));
    CHECK_STR(read_file(&ta, path), "future=1\nview=icons\nsidebar=hidden\nzoom=32\n");
    // A key that is only a prefix of another's isn't it.
    put(path, "viewer=x\n");
    CHECK(state_put(&ta, path, "view", "list", &err));
    CHECK_STR(read_file(&ta, path), "viewer=x\nview=list\n");
    // No temporary file left; outside the test folder refused.
    char tmp[PATH_MAX + 32];
    snprintf(tmp, sizeof tmp, "%s.%d.tmp", path, (int)getpid());
    CHECK(access(tmp, F_OK) != 0);
    snprintf(path, sizeof path, "%s/state", decoy);
    CHECK(!state_put(&ta, path, "view", "list", &err) && strstr(err.msg, "outside the test folder") && access(path, F_OK) != 0);
}

static void test_spawn_window(void)
{
    char out[2048];
    Err err;
    mkdirs(at("bin7"));
    fake(at("bin7"), "uwsm-app", at("spawn7.log"));
    env_set("PATH", at("bin7"));
    CHECK(spawn_window("/opt/x/ofx", "/a dir", &err));
    CHECK(wait_file(at("spawn7.log"), out, sizeof out) && strncmp(out, "uwsm-app -- /opt/x/ofx /a dir sid=", 34) == 0);
    env_restore();
    // Without uwsm-app the binary itself, by its path; one not there is an error.
    mkdirs(at("bin7b"));
    fake(at("bin7b"), "myofx", at("spawn7b.log"));
    env_set("PATH", at("bin7"));
    unlink(at("bin7/uwsm-app"));
    CHECK(spawn_window(at("bin7b/myofx"), "/d", &err));
    CHECK(wait_file(at("spawn7b.log"), out, sizeof out) && strncmp(out, "myofx /d sid=", 13) == 0);
    CHECK(!spawn_window(at("bin7b/none"), "/d", &err) && strstr(err.msg, "is not installed"));
    put(at("bin7b/noexec"), "#!/bin/sh\n");
    CHECK(!spawn_window(at("bin7b/noexec"), "/d", &err));
    env_restore();
}

// A child that waits to be signalled; its exit says by what.
static pid_t waiting_child(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        for (;;) pause();
    }
    return pid;
}

static int child_signal(pid_t pid)
{
    int st = 0;
    for (int i = 0; i < 200; i++) {
        if (waitpid(pid, &st, WNOHANG) == pid) return WIFSIGNALED(st) ? WTERMSIG(st) : -1;
        struct timespec ts = { 0, 10 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    return 0;
}

static void test_windows(void)
{
    ops_guard(root);
    char dir[PATH_MAX], p[PATH_MAX + 16], out[64];
    Err err;
    env_set("OFX_TEST_WINDOWS", at("f7win"));
    CHECK(win_dir(dir, sizeof dir) && strcmp(dir, at("f7win")) == 0);
    env_set("OFX_TEST_WINDOWS", "relative");
    env_set("XDG_RUNTIME_DIR", "/run/user/7");
    CHECK(win_dir(dir, sizeof dir) && strcmp(dir, "/run/user/7/ofx") == 0);
    env_set("XDG_RUNTIME_DIR", "");
    CHECK(!win_dir(dir, sizeof dir));
    env_restore();
    snprintf(dir, sizeof dir, "%s", at("f7win"));
    // A start time: ours, a child's (later than ours), none for a process that isn't.
    i64 me = win_started(getpid());
    CHECK(me > 0 && win_started(1) >= 0 && win_started(0x3ffffff) == -1);
    // Registered: the folder made, the file holds the start time; gone again.
    CHECK(win_register(dir, getpid(), &err));
    snprintf(p, sizeof p, "%s/%d", dir, (int)getpid());
    snprintf(out, sizeof out, "%lld\n", (long long)me);
    CHECK_STR(read_file(&ta, p), out);
    struct stat st;
    CHECK(stat(dir, &st) == 0 && (st.st_mode & 0777) == 0700);
    CHECK(!win_register(dir, 0x3ffffff, &err));
    // Others signalled: a live window yes; this one no; one whose pid is another process's now
    // (another start time) no, its entry removed; a dead one's entry removed; junk left alone.
    pid_t live = waiting_child(), reused = waiting_child(), dead = waiting_child();
    CHECK(win_register(dir, live, &err) && win_register(dir, reused, &err) && win_register(dir, dead, &err));
    char rp[PATH_MAX + 48], dp[PATH_MAX + 16];
    snprintf(rp, sizeof rp, "%s/%d", dir, (int)reused);
    snprintf(dp, sizeof dp, "%s/%d", dir, (int)dead);
    snprintf(out, sizeof out, "%lld\n", (long long)win_started(reused) + 1);
    put(rp, out);
    kill(dead, SIGKILL);
    CHECK(child_signal(dead) == SIGKILL);
    put(at("f7win/notes"), "x");
    put(at("f7win/12x"), "x");
    put(at("f7win/0"), "x");
    CHECK(win_signal_others(dir, getpid(), SIGUSR1) == 1);
    CHECK(child_signal(live) == SIGUSR1);
    CHECK(kill(reused, 0) == 0 && access(rp, F_OK) != 0 && access(dp, F_OK) != 0);
    CHECK(access(p, F_OK) == 0 && access(at("f7win/notes"), F_OK) == 0 && access(at("f7win/0"), F_OK) == 0);
    // An entry still being written (empty): left, not signalled.
    put(rp, "");
    CHECK(win_signal_others(dir, getpid(), SIGUSR1) == 0 && access(rp, F_OK) == 0 && kill(reused, 0) == 0);
    kill(reused, SIGKILL);
    child_signal(reused);
    // The guard: a registry outside the test folder is never signalled, nor written.
    pid_t out_child = waiting_child();
    char od[PATH_MAX];
    snprintf(od, sizeof od, "%s/win", decoy);
    CHECK(!win_register(od, out_child, &err) && access(od, F_OK) != 0);
    mkdir(od, 0700);
    snprintf(out, sizeof out, "%lld\n", (long long)win_started(out_child));
    snprintf(rp, sizeof rp, "%s/%d", od, (int)out_child);
    put(rp, out);
    CHECK(win_signal_others(od, getpid(), SIGUSR1) == 0 && kill(out_child, 0) == 0);
    kill(out_child, SIGKILL);
    child_signal(out_child);
    unlink(rp), rmdir(od);
    // Not removed outside the test folder either.
    mkdir(od, 0700);
    put(rp, "1\n");
    win_unregister(od, out_child);
    CHECK(access(rp, F_OK) == 0);
    unlink(rp), rmdir(od);
    // A name that only starts with a pid isn't an entry, even holding that process's start time.
    pid_t other = waiting_child();
    snprintf(rp, sizeof rp, "%s/%dx", dir, (int)other);
    snprintf(out, sizeof out, "%lld\n", (long long)win_started(other));
    put(rp, out);
    CHECK(win_signal_others(dir, getpid(), SIGUSR1) == 0 && kill(other, 0) == 0 && access(rp, F_OK) == 0);
    kill(other, SIGKILL);
    child_signal(other);
    unlink(rp);
    // A process whose name holds ") ": its start time read from after the last ')'.
    int fds[2];
    CHECK(pipe(fds) == 0);
    pid_t named = fork();
    if (named == 0) {
        prctl(PR_SET_NAME, "x) 1 2 3", 0, 0, 0);
        char c = 1;
        if (write(fds[1], &c, 1) != 1) _exit(1);
        for (;;) pause();
    }
    char c;
    CHECK(read(fds[0], &c, 1) == 1);
    close(fds[0]), close(fds[1]);
    CHECK(win_started(named) >= me);
    kill(named, SIGKILL);
    child_signal(named);
    // Only names and bodies as win_register writes them (reviewer-2, F7.1 review): "+pid", " pid",
    // "0pid", or the time with more after it, are left alone; so is a FIFO, without blocking.
    pid_t odd = waiting_child();
    snprintf(out, sizeof out, "%lld\n", (long long)win_started(odd));
    const char *const ODD[] = { "+%d", " %d", "0%d" };
    for (int k = 0; k < COUNT(ODD); k++) {
        char name[32];
        snprintf(name, sizeof name, ODD[k], (int)odd);
        snprintf(rp, sizeof rp, "%s/%s", dir, name);
        put(rp, out);
        CHECK(win_signal_others(dir, getpid(), SIGUSR1) == 0 && kill(odd, 0) == 0);
        unlink(rp);
    }
    snprintf(rp, sizeof rp, "%s/%d", dir, (int)odd);
    char junk[80];
    snprintf(junk, sizeof junk, "%lld junk\n", (long long)win_started(odd));
    put(rp, junk);
    CHECK(win_signal_others(dir, getpid(), SIGUSR1) == 0 && kill(odd, 0) == 0 && access(rp, F_OK) == 0);
    snprintf(junk, sizeof junk, "%lld", (long long)win_started(odd));   // no newline yet
    put(rp, junk);
    CHECK(win_signal_others(dir, getpid(), SIGUSR1) == 0 && kill(odd, 0) == 0);
    unlink(rp);
    CHECK(mkfifo(rp, 0600) == 0);
    CHECK(win_signal_others(dir, getpid(), SIGUSR1) == 0 && kill(odd, 0) == 0);
    unlink(rp);
    // Not ofx's, so not removed either, even named after a pid that is gone (a FIFO, a folder).
    snprintf(rp, sizeof rp, "%s/%d", dir, (int)dead);
    CHECK(mkfifo(rp, 0600) == 0);
    snprintf(dp, sizeof dp, "%s/%d", dir, (int)reused);   // gone by now too
    CHECK(mkdir(dp, 0700) == 0);
    CHECK(win_signal_others(dir, getpid(), SIGUSR1) == 0 && access(rp, F_OK) == 0 && access(dp, F_OK) == 0);
    unlink(rp), rmdir(dp);
    CHECK(win_name("1") && win_name("4194304") && win_name("1234567890"));
    CHECK(!win_name("") && !win_name("0") && !win_name("12345678901") && !win_name("1 ") && !win_name("-1"));
    i64 t;
    CHECK(win_body("12\n", 3, &t) && t == 12 && !win_body("\n", 1, &t) && !win_body("1", 1, &t) && !win_body("1a\n", 3, &t));
    CHECK(!win_body("99999999999999999999\n", 21, &t));
    kill(odd, SIGKILL);
    child_signal(odd);
    // Unregistered: its file gone; a missing folder signals nobody.
    win_unregister(dir, getpid());
    CHECK(access(p, F_OK) != 0);
    CHECK(win_signal_others(at("f7win/none"), getpid(), SIGUSR1) == 0);
}

// ---- F7.1.1: the default app for a type ----------------------------------------------------------

static void app_file(const char *rel, const char *text)
{
    char p[1300];
    snprintf(p, sizeof p, "%s", at("f7apps/%s", rel));
    char d[1300];
    snprintf(d, sizeof d, "%s", p);
    *strrchr(d, '/') = 0;
    mkdirs(d);
    put(p, text);
}

// The default app for type as apps_default finds it: its file's path from f7apps/, or "-".
static const char *app_for(const char *type)
{
    static char out[PATH_MAX];
    char got[PATH_MAX];
    if (!apps_default(&ta, str_from_c(type), got, sizeof got)) return "-";
    const char *base = at("f7apps/");
    snprintf(out, sizeof out, "%s", strncmp(got, base, strlen(base)) == 0 ? got + strlen(base) : got);
    return out;
}

static void test_apps(void)
{
    arena_reset(&ta);
    env_set("XDG_CONFIG_HOME", at("f7apps/cfg"));
    env_set("XDG_CONFIG_DIRS", at("f7apps/etc"));
    env_set("XDG_DATA_HOME", at("f7apps/home"));
    env_set("XDG_DATA_DIRS", at("f7apps/sys"));
    env_set("XDG_CURRENT_DESKTOP", "Test:Other");
    const char *app = "[Desktop Entry]\nType=Application\nName=x\nExec=x %f\n";
    static const char *const APPS[] = { "edit", "viewer", "gone", "cached", "removed", "sub", "base", "mark" };
    for (int k = 0; k < COUNT(APPS); k++) {
        char rel[64];
        snprintf(rel, sizeof rel, "sys/applications/%s.desktop", APPS[k]);
        app_file(rel, app);
    }
    app_file("sys/applications/kde/foo.desktop", app);
    app_file("sys/applications/link.desktop", "[Desktop Entry]\nType=Link\nName=x\nURL=https://example.org\n");
    app_file("sys/applications/tryx.desktop", "[Desktop Entry]\nType=Application\nName=x\nTryExec=/ofx-test-none/prog\nExec=x\n");
    app_file("sys/applications/trysh.desktop", "[Desktop Entry]\nType=Application\nName=x\nTryExec=sh\nExec=x\n");
    app_file("sys/applications/tryy.desktop", "[Desktop Entry]\nType=Application\nName=x\nTryExec=ofx-test-no-such-prog\nExec=x\n");
    app_file("sys/applications/plain", app);   // not a .desktop name
    app_file("home/applications/gone.desktop", "[Desktop Entry]\nType=Application\nName=x\nExec=x\nHidden=true\n");   // the user's copy deletes it
    app_file("sys/mime/subclasses", "text/markdown text/plain\napplication/x-sub application/x-base\napplication/x-base application/x-root\n"
                                    "application/x-kid application/x-base\napplication/x-leaf application/x-mid\napplication/x-mid application/x-root\n"
                                    "application/x-new2 application/x-root\n");
    app_file("sys/mime/aliases", "application/x-old application/x-new\napplication/x-old2 application/x-new2\n");
    // The desktop's list before the plain one; the user's before the system's.
    app_file("cfg/test-mimeapps.list", "[Default Applications]\ntext/plain=edit.desktop\n");
    app_file("cfg/mimeapps.list",
             "[Default Applications]\ntext/plain=viewer.desktop\ntext/x-a=missing.desktop;link.desktop;viewer.desktop;\ntext/x-b=missing.desktop\n"
             "text/x-c=gone.desktop\ntext/x-d=tryx.desktop;tryy.desktop;trysh.desktop\ntext/x-e=kde-foo.desktop\ntext/x-h=../edit.desktop;edit\n"
             "application/x-root=edit.desktop\n"
             "[Added Associations]\napplication/x-f=removed.desktop;viewer.desktop;\n"
             "[Removed Associations]\napplication/x-f=removed.desktop\n");
    app_file("etc/mimeapps.list", "[Default Applications]\ntext/x-b=edit.desktop\napplication/x-old=viewer.desktop\n");
    app_file("sys/applications/mimeapps.list", "[Default Applications]\ntext/x-b=viewer.desktop\n");
    app_file("sys/applications/mimeinfo.cache",
             "[MIME Cache]\ntext/x-c=gone.desktop;cached.desktop;\napplication/x-f=removed.desktop;\napplication/x-g=cached.desktop;\n"
             "application/x-sub=sub.desktop;\napplication/x-base=base.desktop;\ntext/x-h=mark.desktop;\n");
    static const struct { const char *type, *app; } WANT[] = {
        { "text/plain", "sys/applications/edit.desktop" },       // the desktop's list
        { "text/x-a", "sys/applications/viewer.desktop" },       // the first installed of a list
        { "text/x-b", "sys/applications/edit.desktop" },         // the next list naming an installed one
        { "text/x-c", "sys/applications/cached.desktop" },       // hidden by the user's copy: the cache's next
        { "text/x-d", "sys/applications/trysh.desktop" },        // TryExec not there; TryExec on PATH
        { "text/x-e", "sys/applications/kde/foo.desktop" },      // a dash as a folder
        { "text/x-h", "sys/applications/mark.desktop" },         // ids that aren't ids: none of them
        { "application/x-f", "sys/applications/viewer.desktop" },   // added, less removed (the cache's too)
        { "application/x-g", "sys/applications/cached.desktop" },   // the cache
        { "text/markdown", "sys/applications/edit.desktop" },       // a parent's default
        { "application/x-sub", "sys/applications/sub.desktop" },    // its own association before its parent's
        { "application/x-base", "sys/applications/base.desktop" },  // its own association before its parent's default
        { "application/x-new", "sys/applications/viewer.desktop" }, // a list names it by its alias
        { "application/x-old", "sys/applications/viewer.desktop" }, // asked by the alias
        { "text/x-unknown", "sys/applications/edit.desktop" },      // text/* under text/plain
        { "application/x-kid", "sys/applications/base.desktop" },   // a parent's association
        { "application/x-leaf", "sys/applications/edit.desktop" },  // a grandparent's default
        { "application/x-old2", "sys/applications/edit.desktop" },  // an alias: its canonical type's parent
        { "application/x-none", "-" },
        { "image/png", "-" },
    };
    for (int k = 0; k < COUNT(WANT); k++) {
        const char *got = app_for(WANT[k].type);
        CHECK(strcmp(got, WANT[k].app) == 0);
        if (strcmp(got, WANT[k].app) != 0) fprintf(stderr, "  %s: %s, not %s\n", WANT[k].type, got, WANT[k].app);
    }
    // Without XDG_CURRENT_DESKTOP only the plain lists; a desktop's name in lower case.
    env_set("XDG_CURRENT_DESKTOP", "");
    CHECK(strcmp(app_for("text/plain"), "sys/applications/viewer.desktop") == 0);
    env_set("XDG_CURRENT_DESKTOP", "Other:TEST");
    CHECK(strcmp(app_for("text/plain"), "sys/applications/edit.desktop") == 0);
    // The order of the lists: config home, config dirs, data home, data dirs; desktop's first in each.
    const char *lists[APPS_LISTS_MAX];
    CHECK(apps_lists(&ta, lists, APPS_LISTS_MAX) == 12);
    CHECK(strcmp(lists[0], at("f7apps/cfg/other-mimeapps.list")) == 0 && strcmp(lists[1], at("f7apps/cfg/test-mimeapps.list")) == 0);
    CHECK(strcmp(lists[2], at("f7apps/cfg/mimeapps.list")) == 0 && strcmp(lists[5], at("f7apps/etc/mimeapps.list")) == 0);
    CHECK(strcmp(lists[8], at("f7apps/home/applications/mimeapps.list")) == 0 && strcmp(lists[11], at("f7apps/sys/applications/mimeapps.list")) == 0);
    char p[PATH_MAX];
    CHECK(!apps_find(&ta, S("a/b.desktop"), p, sizeof p) && !apps_find(&ta, S("edit"), p, sizeof p) && !apps_find(&ta, S(""), p, sizeof p));
    CHECK(!apps_find(&ta, S("../applications/edit.desktop"), p, sizeof p) && !apps_find(&ta, S("plain"), p, sizeof p));
    CHECK(apps_find(&ta, S("edit.desktop"), p, sizeof p) && !apps_find(&ta, S("edit.desktop"), p, 10));
    env_restore();
}

static void test_spawn_launch(void)
{
    char out[2048];
    Err err;
    mkdirs(at("bin7l"));
    fake(at("bin7l"), "uwsm-app", at("spawn7l.log"));
    env_set("PATH", at("bin7l"));
    CHECK(spawn_launch("/usr/share/applications/nvim.desktop", "/a b/test.txt", &err));
    CHECK(wait_file(at("spawn7l.log"), out, sizeof out) &&
          strncmp(out, "uwsm-app -- gio launch /usr/share/applications/nvim.desktop /a b/test.txt sid=", 78) == 0);
    env_restore();
}
