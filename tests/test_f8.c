// F8 step 1 (docs/plan.md "F8 design"): the right-click menus of the files, an entry's and the folder
// shown's: what a right press or Shift+F10 opens, what they leave out, what their own entries do.
// Included by test.c after test_f75.c.

static void test_ctx_menus(void)
{
    static u8 mark[5];
    UiNames names = { name_at, NULL, dir_at };   // (row 2 is a folder)
    Ui u = menu_ui(5);
    u.mark = mark;
    // An unmarked row: the marks clear, the cursor goes there, its menu, nothing marked in it; a
    // file has no tab, and outside the trash no Restore.
    ui_mark(&u, 0, true);
    UiInput in = ui_input_none();
    in.ctx_row = 3;
    CHECK(ui_input_acts(&in));
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.menu_on && u.menu == UI_CTX_ENTRY && u.list.sel == 3 && u.marked == 0);
    CHECK(u.menu_at == -1 && !u.ctx_key && !u.ctx_dir);
    CHECK(!ui_menu_shown(&u, UM_OPEN_TAB) && ui_menu_shown(&u, UM_RENAME) && !ui_menu_shown(&u, UM_RESTORE) && ui_menu_shown(&u, UM_TRASH));
    // No neighbours: Left does nothing, nor a bar title under the pointer.
    menu_keys(&u, 1, UK_RIGHT);
    CHECK(u.menu_on && u.menu == UI_CTX_ENTRY);
    CHECK(!ui_menu_hover(&u, 1, -1) && u.menu == UI_CTX_ENTRY);
    menu_keys(&u, 1, UK_ESC);
    CHECK(!u.menu_on);
    // A folder: Open in New Tab, a tab behind on it.
    in = ui_input_none(), in.ctx_row = 2;
    ui_handle(&u, names, &in);
    CHECK(u.ctx_dir && ui_menu_shown(&u, UM_OPEN_TAB) && ui_menu_enabled(&u, UM_OPEN_TAB));
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_ENTRY, UM_OPEN_TAB);
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_TAB_BG_ROW && a.index == 2 && !u.menu_on);
    // A right press takes the keys from the sidebar.
    u.side_focus = true;
    in = ui_input_none(), in.ctx_row = 0;
    ui_handle(&u, names, &in);
    CHECK(u.menu_on && !u.side_focus);
    menu_keys(&u, 1, UK_ESC);
    // A marked row keeps the marks, the cursor on it; with two, no Rename and no tab. Cut runs
    // Ctrl+X, on the marks.
    ui_mark(&u, 1, true), ui_mark(&u, 2, true);
    in = ui_input_none(), in.ctx_row = 1;
    ui_handle(&u, names, &in);
    CHECK(u.marked == 2 && u.list.sel == 1 && !u.ctx_dir && !ui_menu_shown(&u, UM_RENAME) && !ui_menu_shown(&u, UM_OPEN_TAB));
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_ENTRY, UM_CUT);
    CHECK(ui_handle(&u, names, &in).kind == UA_CUT && !u.menu_on && u.marked == 2);
    // One marked, the cursor on it: one folder still.
    ui_mark(&u, 1, false);
    in = ui_input_none(), in.ctx_row = 2;
    ui_handle(&u, names, &in);
    CHECK(u.marked == 1 && u.ctx_dir && ui_menu_shown(&u, UM_RENAME));
    menu_keys(&u, 1, UK_ESC);
    // The empty space: the marks clear, the folder shown's menu. Its Properties are the folder's
    // (index 1), whatever the cursor.
    in = ui_input_none(), in.ctx_here = true;
    CHECK(ui_input_acts(&in));
    ui_handle(&u, names, &in);
    CHECK(u.menu_on && u.menu == UI_CTX_HERE && u.marked == 0 && u.list.sel == 2);
    CHECK(ui_menu_shown(&u, UM_NEW_FOLDER) && !ui_menu_shown(&u, UM_EMPTY_TRASH) && ui_menu_enabled(&u, UM_HERE_PROPS));
    int last = UI_MENU[UI_CTX_HERE].n - 1;
    CHECK(UI_MENU[UI_CTX_HERE].items[last] == UM_HERE_PROPS);
    in = ui_input_none(), in.menu_item = last;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_PROPS && a.index == 1 && u.props && !u.menu_on);
    // A panel up: no right-click menu.
    in = ui_input_none(), in.ctx_row = 0;
    ui_handle(&u, names, &in);
    CHECK(!u.menu_on);
    u.props = false;
    // The trash view: no new folder or file, Empty Trash (it asks); an entry's Restore, no Move to
    // Trash and no tab.
    u.in_trash = true;
    in = ui_input_none(), in.ctx_here = true;
    ui_handle(&u, names, &in);
    CHECK(!ui_menu_shown(&u, UM_NEW_FOLDER) && !ui_menu_shown(&u, UM_NEW_FILE) && ui_menu_shown(&u, UM_EMPTY_TRASH) && ui_menu_enabled(&u, UM_EMPTY_TRASH));
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_HERE, UM_EMPTY_TRASH);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.empty_asking && !u.menu_on);
    u.empty_asking = false;
    Ui none = menu_ui(0);   // an empty trash: Empty Trash greyed
    none.in_trash = true;
    in = ui_input_none(), in.ctx_here = true;
    ui_handle(&none, names, &in);
    CHECK(none.menu_on && ui_menu_shown(&none, UM_EMPTY_TRASH) && !ui_menu_enabled(&none, UM_EMPTY_TRASH));
    in = ui_input_none(), in.ctx_row = 2;
    ui_handle(&u, names, &in);
    CHECK(!ui_menu_shown(&u, UM_OPEN_TAB) && !ui_menu_shown(&u, UM_TRASH) && ui_menu_shown(&u, UM_RESTORE));
    menu_keys(&u, 1, UK_ESC);
    u.in_trash = false;
    // In the bar's menus New Folder stays, greyed where it can't (Empty Trash isn't there).
    u.in_trash = true, u.menu = 0;
    CHECK(ui_menu_shown(&u, UM_NEW_FOLDER) && !ui_menu_enabled(&u, UM_NEW_FOLDER));
    u.in_trash = false;
}

// Every entry a folder: one asked about where there is none says yes.
static bool dir_any(void *ctx, isize i)
{
    (void)ctx, (void)i;
    return true;
}

// Shift+F10 and the Menu key (gui/ maps both to UK_CONTEXT): the cursor's menu, its first entry
// marked; the folder shown's in an empty folder; nothing while the sidebar, an edit line or the
// find line has the keys.
static void test_ctx_keys(void)
{
    static u8 mark[5];
    UiNames names = { name_at, NULL, dir_at };
    Ui u = menu_ui(5);
    u.mark = mark;
    u.list.sel = 2;
    UiInput in = ui_input_none();
    in.ev[in.nev++] = (UiEvent){ UK_CONTEXT, 0 };
    ui_handle(&u, names, &in);
    CHECK(u.menu_on && u.menu == UI_CTX_ENTRY && u.ctx_key && u.ctx_dir && UI_MENU[UI_CTX_ENTRY].items[u.menu_at] == UM_OPEN);
    menu_keys(&u, 1, UK_DOWN);
    CHECK(UI_MENU[UI_CTX_ENTRY].items[u.menu_at] == UM_OPEN_TAB);
    // Enter picks, as in the bar's menus.
    UiAction a = menu_keys(&u, 1, UK_ENTER);
    CHECK(a.kind == UA_TAB_BG_ROW && a.index == 2 && !u.menu_on);
    // One marked elsewhere: the menu is the mark's, so no tab for the cursor's folder.
    ui_mark(&u, 0, true);
    in = ui_input_none(), in.ev[in.nev++] = (UiEvent){ UK_CONTEXT, 0 };
    ui_handle(&u, names, &in);   // (menu_keys hands no folders)
    CHECK(u.menu_on && u.list.sel == 2 && !u.ctx_dir);
    menu_keys(&u, 1, UK_ESC);
    ui_mark(&u, 0, false);
    // The sidebar has the keys: nothing (step 3 gives it its own).
    u.side_focus = true;
    menu_keys(&u, 1, UK_CONTEXT);
    CHECK(!u.menu_on);
    u.side_focus = false;
    // An edit line keeps the key.
    menu_keys(&u, 1, UK_F2);
    CHECK(u.editing);
    menu_keys(&u, 1, UK_CONTEXT);
    CHECK(!u.menu_on && u.editing);
    menu_keys(&u, 1, UK_ESC);
    // The find line with the keys too.
    menu_keys(&u, 1, UK_FIND);
    CHECK(u.finding && u.find_keys);
    menu_keys(&u, 1, UK_CONTEXT);
    CHECK(!u.menu_on);
    menu_keys(&u, 1, UK_ESC);
    // An empty folder: the folder shown's, and no entry asked whether it is a folder.
    Ui e = menu_ui(0);
    in = ui_input_none(), in.ev[in.nev++] = (UiEvent){ UK_CONTEXT, 0 };
    ui_handle(&e, (UiNames){ name_at, NULL, dir_any }, &in);
    CHECK(e.menu_on && e.menu == UI_CTX_HERE && e.ctx_key && !e.ctx_dir);
}

// F8 step 2: the entries on one folder (a window, a paste into it, a terminal, a bookmark), Follow
// Link on a link, Make Link (also Edit's, Ctrl+M).
static void test_ctx_folder_entries(void)
{
    static u8 mark[5];
    UiNames names = { name_at, NULL, dir_at };
    Ui u = menu_ui(5);
    u.mark = mark, u.can_paste = true;
    // A file: none of them, no Follow Link unless gui/ says it is a link.
    UiInput in = ui_input_none();
    in.ctx_row = 1;
    ui_handle(&u, names, &in);
    UiMenuItem one[] = { UM_OPEN_WINDOW, UM_PASTE_INTO, UM_TERMINAL_IN, UM_BOOKMARK_IT, UM_FOLLOW };
    for (int k = 0; k < COUNT(one); k++) CHECK(!ui_menu_shown(&u, one[k]));
    CHECK(ui_menu_shown(&u, UM_MAKE_LINK) && ui_menu_enabled(&u, UM_MAKE_LINK));
    u.sel_link = true;
    CHECK(ui_menu_shown(&u, UM_FOLLOW) && ui_menu_enabled(&u, UM_FOLLOW));
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_ENTRY, UM_FOLLOW);
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_FOLLOW && a.index == 1);
    // A folder: each on that folder (all set, index the entry); Add Bookmark hidden when it is one.
    u.sel_link = false;
    struct { UiMenuItem it; UiActionKind kind; } on[] = {
        { UM_OPEN_WINDOW, UA_NEW_WINDOW }, { UM_PASTE_INTO, UA_PASTE }, { UM_TERMINAL_IN, UA_TERMINAL }, { UM_BOOKMARK_IT, UA_ADD_BOOKMARK },
    };
    for (int k = 0; k < COUNT(on); k++) {
        in = ui_input_none(), in.ctx_row = 2;
        ui_handle(&u, names, &in);
        CHECK(ui_menu_shown(&u, on[k].it) && ui_menu_enabled(&u, on[k].it));
        in = ui_input_none(), in.menu_item = menu_place(UI_CTX_ENTRY, on[k].it);
        a = ui_handle(&u, names, &in);
        CHECK(a.kind == on[k].kind && a.index == 2 && a.all && !u.menu_on);
    }
    in = ui_input_none(), in.ctx_row = 2;
    ui_handle(&u, names, &in);
    u.sel_bm = true;
    CHECK(!ui_menu_shown(&u, UM_BOOKMARK_IT));
    u.sel_bm = false, u.can_paste = false;
    CHECK(ui_menu_shown(&u, UM_PASTE_INTO) && !ui_menu_enabled(&u, UM_PASTE_INTO));
    // Several marked: none of the one-folder entries, nor Follow Link.
    ui_mark(&u, 1, true), ui_mark(&u, 2, true);
    u.sel_link = true;
    in = ui_input_none(), in.ctx_row = 2;
    ui_handle(&u, names, &in);
    for (int k = 0; k < COUNT(one); k++) CHECK(!ui_menu_shown(&u, one[k]));
    // Make Link runs Ctrl+M, on the marks.
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_ENTRY, UM_MAKE_LINK);
    CHECK(ui_handle(&u, names, &in).kind == UA_MAKE_LINK && u.marked == 2);
    ui_mark_clear(&u);
    // The trash view: none of them, Ctrl+M says it doesn't work there.
    u.in_trash = true;
    in = ui_input_none(), in.ctx_row = 2;
    ui_handle(&u, names, &in);
    for (int k = 0; k < COUNT(one); k++) CHECK(!ui_menu_shown(&u, one[k]));
    CHECK(!ui_menu_enabled(&u, UM_MAKE_LINK));
    menu_keys(&u, 1, UK_ESC);
    CHECK(menu_keys(&u, 1, UK_MAKE_LINK).kind == UA_NOT_IN_TRASH);
    u.in_trash = false;
    // Edit has Make Link after Duplicate.
    CHECK(menu_place(1, UM_MAKE_LINK) == menu_place(1, UM_DUPLICATE) + 1);
}

// op_symlink and its undo (F8): "Link to a.txt", then "Link to a (2).txt"; the undo removes the
// links while they are those links.
static void test_links(void)
{
    arena_reset(&ta);
    ops_guard(root);
    mkdirs(at("f8ln"));
    put(at("f8ln/a.txt"), "a\n");
    char made[NAME_MAX + 1], made2[NAME_MAX + 1], buf[PATH_MAX], p[PATH_MAX];
    Err err;
    CHECK(op_symlink(at("f8ln"), at("f8ln/a.txt"), S("Link to a.txt"), made, sizeof made, &err) && strcmp(made, "Link to a.txt") == 0);
    ssize_t k = readlink(at("f8ln/Link to a.txt"), buf, sizeof buf - 1);
    CHECK(k > 0 && (buf[k] = 0, strcmp(buf, at("f8ln/a.txt")) == 0));
    CHECK(op_symlink(at("f8ln"), at("f8ln/a.txt"), S("Link to a.txt"), made2, sizeof made2, &err) && strcmp(made2, "Link to a (2).txt") == 0);
    CHECK(!op_symlink(at("f8ln"), at("f8ln/a.txt"), S("a/b"), made, sizeof made, &err) && strstr(err.msg, "can't hold a /"));
    CHECK(!op_symlink(at("f8ln/none"), at("f8ln/a.txt"), S("x"), made, sizeof made, &err) && strstr(err.msg, "No such file"));
    CHECK(!op_symlink(decoy, at("f8ln/a.txt"), S("x"), made, sizeof made, &err) && strstr(err.msg, "outside the test folder"));
    snprintf(p, sizeof p, "%s/x", decoy);
    struct stat st;
    CHECK(lstat(p, &st) != 0);
    // The undo: both go; the target stays.
    Undo un = { .arena = &ta };
    undo_begin(&un, UNDO_LINK);
    undo_add(&un, at("f8ln/a.txt"), at("f8ln/Link to a.txt"), NULL), undo_add(&un, at("f8ln/a.txt"), at("f8ln/Link to a (2).txt"), NULL);
    char what[64];
    undo_what(&un, what, sizeof what);
    CHECK_STR(str_from_c(what), "link of 2 items");
    isize done;
    CHECK(undo_do(&un, &done, &err) && done == 2 && lstat(at("f8ln/Link to a.txt"), &st) != 0 && lstat(at("f8ln/Link to a (2).txt"), &st) != 0);
    CHECK(stat(at("f8ln/a.txt"), &st) == 0);
    // A link pointed elsewhere since, or a file put in its place: kept, and said so.
    CHECK(symlink(at("f8ln/b"), at("f8ln/l")) == 0);
    undo_begin(&un, UNDO_LINK), undo_add(&un, at("f8ln/a.txt"), at("f8ln/l"), NULL);
    undo_what(&un, what, sizeof what);
    CHECK_STR(str_from_c(what), "link");
    CHECK(!undo_do(&un, &done, &err) && strstr(err.msg, "isn't that link now") && lstat(at("f8ln/l"), &st) == 0);
    put(at("f8ln/m"), "");
    undo_begin(&un, UNDO_LINK), undo_add(&un, at("f8ln/a.txt"), at("f8ln/m"), NULL);
    CHECK(!undo_do(&un, &done, &err) && strstr(err.msg, "isn't that link now") && stat(at("f8ln/m"), &st) == 0);
}
