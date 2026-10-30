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
    u.focus = UI_FOCUS_SIDE;
    in = ui_input_none(), in.ctx_row = 0;
    ui_handle(&u, names, &in);
    CHECK(u.menu_on && u.focus != UI_FOCUS_SIDE);
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
    u.view = UI_VIEW_TRASH;
    in = ui_input_none(), in.ctx_here = true;
    ui_handle(&u, names, &in);
    CHECK(!ui_menu_shown(&u, UM_NEW_FOLDER) && !ui_menu_shown(&u, UM_NEW_FILE) && ui_menu_shown(&u, UM_EMPTY_TRASH) && ui_menu_enabled(&u, UM_EMPTY_TRASH));
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_HERE, UM_EMPTY_TRASH);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.empty_asking && !u.menu_on);
    u.empty_asking = false;
    Ui none = menu_ui(0);   // an empty trash: Empty Trash greyed
    none.view = UI_VIEW_TRASH;
    in = ui_input_none(), in.ctx_here = true;
    ui_handle(&none, names, &in);
    CHECK(none.menu_on && ui_menu_shown(&none, UM_EMPTY_TRASH) && !ui_menu_enabled(&none, UM_EMPTY_TRASH));
    in = ui_input_none(), in.ctx_row = 2;
    ui_handle(&u, names, &in);
    CHECK(!ui_menu_shown(&u, UM_OPEN_TAB) && !ui_menu_shown(&u, UM_TRASH) && ui_menu_shown(&u, UM_RESTORE));
    menu_keys(&u, 1, UK_ESC);
    u.view = UI_VIEW_FOLDER;
    // In the bar's menus New Folder stays, greyed where it can't (Empty Trash isn't there).
    u.view = UI_VIEW_TRASH, u.menu = 0;
    CHECK(ui_menu_shown(&u, UM_NEW_FOLDER) && !ui_menu_enabled(&u, UM_NEW_FOLDER));
    u.view = UI_VIEW_FOLDER;
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
    // The sidebar has the keys, and no rows: nothing.
    u.focus = UI_FOCUS_SIDE;
    menu_keys(&u, 1, UK_CONTEXT);
    CHECK(!u.menu_on);
    ui_unfocus(&u, UI_FOCUS_SIDE);
    // An edit line keeps the key.
    menu_keys(&u, 1, UK_F2);
    CHECK((u.focus == UI_FOCUS_EDIT));
    menu_keys(&u, 1, UK_CONTEXT);
    CHECK(!u.menu_on && (u.focus == UI_FOCUS_EDIT));
    menu_keys(&u, 1, UK_ESC);
    // The find line with the keys too.
    menu_keys(&u, 1, UK_FIND);
    CHECK(u.finding && (u.focus == UI_FOCUS_FIND));
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
        CHECK(!u.ctx_w);   // (gui/ says, as it opens: writable)
        u.ctx_w = true;
        CHECK(ui_menu_shown(&u, on[k].it) && ui_menu_enabled(&u, on[k].it));
        in = ui_input_none(), in.menu_item = menu_place(UI_CTX_ENTRY, on[k].it);
        a = ui_handle(&u, names, &in);
        CHECK(a.kind == on[k].kind && a.index == 2 && a.all && !u.menu_on);
    }
    in = ui_input_none(), in.ctx_row = 2;
    ui_handle(&u, names, &in);
    u.sel_bm = true;
    CHECK(!ui_menu_shown(&u, UM_BOOKMARK_IT));
    u.sel_bm = false, u.can_paste = false, u.ctx_w = true;
    CHECK(ui_menu_shown(&u, UM_PASTE_INTO) && !ui_menu_enabled(&u, UM_PASTE_INTO));
    u.can_paste = true, u.ctx_w = false;   // a folder that doesn't take new files (F8 review)
    CHECK(!ui_menu_enabled(&u, UM_PASTE_INTO));
    u.can_paste = false;
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
    u.view = UI_VIEW_TRASH;
    in = ui_input_none(), in.ctx_row = 2;
    ui_handle(&u, names, &in);
    for (int k = 0; k < COUNT(one); k++) CHECK(!ui_menu_shown(&u, one[k]));
    CHECK(!ui_menu_enabled(&u, UM_MAKE_LINK));
    menu_keys(&u, 1, UK_ESC);
    CHECK(menu_keys(&u, 1, UK_MAKE_LINK).kind == UA_NOT_IN_TRASH);
    u.view = UI_VIEW_FOLDER;
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

// F8 step 3: a folder elsewhere's menu: a sidebar row (not a heading) or a path button, by a right
// press or Shift+F10 where the keys are; what it shows for Trash, a place, a bookmark's row, a drive;
// what its entries hand gui/ (UA_FOLDER_DO, or Edit Bookmarks' own on that line).
static void test_ctx_folder_menu(void)
{
    static const u8 HEAD[] = { 1, 0, 0, 0, 1, 0 };   // Places, Home, Trash, a bookmark, Drives, /
    UiNames names = { name_at, NULL, dir_at };
    Ui u = menu_ui(3);
    u.side_head = HEAD;
    ui_resize(&u.side, 6), ui_set(&u.side, 6, 1);
    // A heading: nothing. A row: its menu, the sidebar's cursor there, the keys where they were.
    UiInput in = ui_input_none();
    in.ctx_side = 0;
    ui_handle(&u, names, &in);
    CHECK(!u.menu_on);
    in = ui_input_none(), in.ctx_side = 5;
    CHECK(ui_input_acts(&in));
    ui_handle(&u, names, &in);
    CHECK(u.menu_on && u.menu == UI_CTX_FOLDER && u.ctx_side == 5 && u.ctx_crumb == -1 && u.side.sel == 5 && u.focus != UI_FOCUS_SIDE && !u.ctx_key);
    // As gui/ finds it: a place (no Cut or Copy), not a bookmark's row, no eject.
    u.ctx_place = true, u.ctx_bms = 3;
    CHECK(ui_menu_shown(&u, UM_OPEN_IT) && ui_menu_shown(&u, UM_OPEN_TAB) && ui_menu_shown(&u, UM_OPEN_WINDOW) && ui_menu_shown(&u, UM_TERMINAL_IN));
    CHECK(!ui_menu_shown(&u, UM_CUT_IT) && !ui_menu_shown(&u, UM_COPY_IT) && ui_menu_shown(&u, UM_PASTE_INTO) && ui_menu_shown(&u, UM_BOOKMARK_IT));
    CHECK(!ui_menu_shown(&u, UM_BM_RENAME_IT) && !ui_menu_shown(&u, UM_EJECT_IT) && !ui_menu_shown(&u, UM_EMPTY_IT) && ui_menu_shown(&u, UM_PROPS_IT));
    CHECK(!ui_menu_shown(&u, UM_OPEN) && !ui_menu_shown(&u, UM_CUT) && !ui_menu_shown(&u, UM_FOLLOW));   // (the entry menu's)
    CHECK(!ui_menu_enabled(&u, UM_PASTE_INTO));
    u.can_paste = true;
    CHECK(!ui_menu_enabled(&u, UM_PASTE_INTO) && !u.ctx_w);   // (not writable, until gui/ says)
    u.ctx_w = true;
    CHECK(ui_menu_enabled(&u, UM_PASTE_INTO) && ui_menu_enabled(&u, UM_OPEN_WINDOW));
    // Its entries go to gui/ for that folder.
    UiMenuItem to_gui[] = { UM_OPEN_IT, UM_OPEN_TAB, UM_OPEN_WINDOW, UM_TERMINAL_IN, UM_PASTE_INTO, UM_BOOKMARK_IT };
    for (int k = 0; k < COUNT(to_gui); k++) {
        u.menu_on = true;
        in = ui_input_none(), in.menu_item = menu_place(UI_CTX_FOLDER, to_gui[k]);
        UiAction a = ui_handle(&u, names, &in);
        CHECK(a.kind == UA_FOLDER_DO && a.index == to_gui[k] && !u.menu_on);
    }
    // A bookmark already: no Add Bookmark. A drive: Eject. Not a place (the tree): Cut and Copy.
    u.menu_on = true;
    u.ctx_bmed = true;
    CHECK(!ui_menu_shown(&u, UM_BOOKMARK_IT));
    u.ctx_eject = true;
    CHECK(ui_menu_shown(&u, UM_EJECT_IT) && ui_menu_enabled(&u, UM_EJECT_IT) && !ui_menu_shown(&u, UM_MOUNT_IT));
    // A drive not mounted yet (2026-10-30): Mount alone.
    u.ctx_unmounted = true;
    CHECK(ui_menu_shown(&u, UM_MOUNT_IT) && ui_menu_enabled(&u, UM_MOUNT_IT) && !ui_menu_shown(&u, UM_OPEN_IT) && !ui_menu_shown(&u, UM_EJECT_IT));
    CHECK(!ui_menu_shown(&u, UM_PROPS_IT) && !ui_menu_shown(&u, UM_OPEN_TAB));
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_FOLDER, UM_MOUNT_IT);
    CHECK(ui_handle(&u, names, &in).kind == UA_FOLDER_DO);
    u.menu_on = true, u.ctx_unmounted = false;
    u.ctx_place = false;
    CHECK(ui_menu_shown(&u, UM_CUT_IT) && ui_menu_shown(&u, UM_COPY_IT));
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_FOLDER, UM_COPY_IT);
    CHECK(ui_handle(&u, names, &in).kind == UA_FOLDER_DO);
    // Its Properties are that folder's (index 2).
    u.menu_on = true;
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_FOLDER, UM_PROPS_IT);
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_PROPS && a.index == 2 && u.props);
    u.props = false;
    // A bookmark's row (line 1 of 3): Edit Bookmarks' own on that line.
    in = ui_input_none(), in.ctx_side = 3;
    ui_handle(&u, names, &in);
    CHECK(u.ctx_bmk == -1 && !u.ctx_place && !u.ctx_bmed && !u.ctx_eject && !u.ctx_w);   // (reset as it opens, for gui/ to say)
    u.ctx_place = true, u.ctx_bmk = 1, u.ctx_bms = 3, u.bm_total = 3;
    UiMenuItem bm[] = { UM_BM_RENAME_IT, UM_BM_UP_IT, UM_BM_DOWN_IT, UM_BM_REMOVE_IT, UM_BM_EDIT_IT };
    for (int k = 0; k < COUNT(bm); k++) CHECK(ui_menu_shown(&u, bm[k]) && ui_menu_enabled(&u, bm[k]));
    struct { UiMenuItem it; UiActionKind kind; } line[] = { { UM_BM_UP_IT, UA_BM_UP }, { UM_BM_DOWN_IT, UA_BM_DOWN }, { UM_BM_REMOVE_IT, UA_BM_REMOVE } };
    for (int k = 0; k < COUNT(line); k++) {
        u.menu_on = true;
        in = ui_input_none(), in.menu_item = menu_place(UI_CTX_FOLDER, line[k].it);
        a = ui_handle(&u, names, &in);
        CHECK(a.kind == line[k].kind && a.index == 1 && !u.bmp);
    }
    u.menu_on = true;
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_FOLDER, UM_BM_EDIT_IT);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_BM_EDIT && u.bmp && u.bmp_list.sel == 1 && !u.bmp_naming);
    u.bmp = false, u.menu_on = true;
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_FOLDER, UM_BM_RENAME_IT);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_BM_NAME && a.index == 1 && u.bmp && u.bmp_list.sel == 1 && u.bmp_naming);
    u.bmp = u.bmp_naming = false;
    // The first line can't move up, the last can't move down.
    u.ctx_bmk = 0;
    CHECK(!ui_menu_enabled(&u, UM_BM_UP_IT) && ui_menu_enabled(&u, UM_BM_DOWN_IT));
    u.ctx_bmk = 2;
    CHECK(ui_menu_enabled(&u, UM_BM_UP_IT) && !ui_menu_enabled(&u, UM_BM_DOWN_IT));
    // Trash: gone to, not into; Empty Trash, greyed when empty, asks.
    u.ctx_bmk = -1, u.ctx_trash = true, u.ctx_eject = false, u.ctx_place = false;   // (its path button)
    CHECK(ui_menu_shown(&u, UM_OPEN_IT) && ui_menu_shown(&u, UM_OPEN_TAB) && !ui_menu_shown(&u, UM_OPEN_WINDOW) && !ui_menu_shown(&u, UM_TERMINAL_IN));
    CHECK(!ui_menu_shown(&u, UM_PASTE_INTO) && !ui_menu_shown(&u, UM_BOOKMARK_IT) && !ui_menu_shown(&u, UM_PROPS_IT) && !ui_menu_shown(&u, UM_CUT_IT));
    CHECK(ui_menu_shown(&u, UM_EMPTY_IT) && !ui_menu_enabled(&u, UM_EMPTY_IT));
    u.ctx_full = true, u.menu_on = true;
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_FOLDER, UM_EMPTY_IT);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.empty_asking && !u.menu_on);
    u.empty_asking = false, u.ctx_trash = u.ctx_full = false;
    // A path button: its menu, the sidebar's cursor left alone; one that isn't there: nothing.
    in = ui_input_none(), in.ctx_crumb = 1, in.crumb_n = 3;
    ui_handle(&u, names, &in);
    CHECK(u.menu_on && u.menu == UI_CTX_FOLDER && u.ctx_crumb == 1 && u.ctx_side == -1 && u.side.sel == 3);
    menu_keys(&u, 1, UK_ESC);
    in = ui_input_none(), in.ctx_crumb = 3, in.crumb_n = 3;
    ui_handle(&u, names, &in);
    CHECK(!u.menu_on);
    // By the keys: the sidebar's cursor's (not on a heading), the path button's; the keys stay there.
    u.focus = UI_FOCUS_SIDE;
    in = ui_input_none(), in.ev[in.nev++] = (UiEvent){ UK_CONTEXT, 0 };
    ui_handle(&u, names, &in);
    CHECK(u.menu_on && u.menu == UI_CTX_FOLDER && u.ctx_side == 3 && u.ctx_key && (u.focus == UI_FOCUS_SIDE));
    menu_keys(&u, 1, UK_ESC);
    CHECK(!u.menu_on && (u.focus == UI_FOCUS_SIDE));
    u.side.sel = 4;   // (a heading, as a test can put it)
    menu_keys(&u, 1, UK_CONTEXT);
    CHECK(!u.menu_on);
    ui_unfocus(&u, UI_FOCUS_SIDE), u.focus = UI_FOCUS_CRUMBS, u.crumb_at = 2;
    in = ui_input_none(), in.crumb_n = 3, in.crumb_last = 2, in.ev[in.nev++] = (UiEvent){ UK_CONTEXT, 0 };
    ui_handle(&u, names, &in);
    CHECK(u.menu_on && u.menu == UI_CTX_FOLDER && u.ctx_crumb == 2 && u.ctx_side == -1 && (u.focus == UI_FOCUS_CRUMBS));
    menu_keys(&u, 1, UK_ESC);
    // The job rows keep the key.
    ui_unfocus(&u, UI_FOCUS_CRUMBS), u.focus = UI_FOCUS_JOBS;
    in = ui_input_none(), in.job_n = 1, in.ev[in.nev++] = (UiEvent){ UK_CONTEXT, 0 };
    ui_handle(&u, names, &in);
    CHECK(!u.menu_on);   // (a key not theirs gives the files the keys back, as before F8)
}

// F8 step 4: a tab's menu. A right press on a tab shows it and opens the menu; Move Tab Left and
// Right run their keys (greyed at the ends), Close runs Ctrl+W, Close Other Tabs is its own.
static void test_ctx_tab_menu(void)
{
    UiNames names = { name_at, NULL, dir_at };
    Ui u = menu_ui(3);
    u.tab_at = 0, u.tab_n = 3;
    UiInput in = ui_input_none();
    in.ctx_tab = 3;   // not there
    ui_handle(&u, names, &in);
    CHECK(!u.menu_on);
    in = ui_input_none(), in.ctx_tab = 2;
    CHECK(ui_input_acts(&in));
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_TAB_SHOW && a.index == 2 && u.menu_on && u.menu == UI_CTX_TAB);
    u.tab_at = 2;   // (gui/ showed it)
    CHECK(ui_menu_enabled(&u, UM_TAB_LEFT) && !ui_menu_enabled(&u, UM_TAB_RIGHT) && ui_menu_enabled(&u, UM_TAB_OTHERS) && ui_menu_enabled(&u, UM_CLOSE));
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_TAB, UM_TAB_LEFT);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_TAB_MOVE && a.index == -1 && !u.menu_on);
    u.tab_at = 0, u.menu_on = true;
    CHECK(!ui_menu_enabled(&u, UM_TAB_LEFT) && ui_menu_enabled(&u, UM_TAB_RIGHT));
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_TAB, UM_TAB_RIGHT);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_TAB_MOVE && a.index == 1);
    u.menu_on = true;
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_TAB, UM_TAB_OTHERS);
    CHECK(ui_handle(&u, names, &in).kind == UA_TAB_OTHERS);
    u.menu_on = true;
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_TAB, UM_CLOSE);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_TAB_CLOSE && a.index == -1);
    u.menu_on = true;
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_TAB, UM_NEW_TAB);
    CHECK(ui_handle(&u, names, &in).kind == UA_TAB_NEW);
    // One tab: no other to close.
    u.tab_n = 1;
    CHECK(!ui_menu_enabled(&u, UM_TAB_OTHERS) && !ui_menu_enabled(&u, UM_TAB_RIGHT));
    // An edit line open closes as the menu opens (as the bar's menus do).
    u.tab_n = 2;
    menu_keys(&u, 1, UK_F2);
    CHECK((u.focus == UI_FOCUS_EDIT));
    in = ui_input_none(), in.ctx_tab = 1;
    ui_handle(&u, names, &in);
    CHECK(u.menu_on && u.focus != UI_FOCUS_EDIT);
}

// The list's Owner, Group and Permissions, and a link's target after its arrow (2026-10-27).
static void test_list_columns(void)
{
    char b[16];
    struct { u32 mode; const char *want; } perms[] = {
        { 0100644, "rw-r--r--" }, { 040755, "rwxr-xr-x" }, { 0100600, "rw-------" }, { 0104755, "rwsr-xr-x" }, { 0104644, "rwSr--r--" },
        { 0102755, "rwxr-sr-x" }, { 0102745, "rwxr-Sr-x" }, { 041777, "rwxrwxrwt" }, { 041776, "rwxrwxrwT" }, { 0, "-" },
    };
    for (int k = 0; k < COUNT(perms); k++) {
        ui_perms(b, sizeof b, perms[k].mode);
        CHECK_STR(str_from_c(b), perms[k].want);
    }
    // A listing: each entry's owner and group (the target's for a link), a link's target as written.
    arena_reset(&ta);
    mkdirs(at("f8cols/sub"));
    put(at("f8cols/a.txt"), "a");
    CHECK(symlink("sub", at("f8cols/rel")) == 0 && symlink(at("f8cols/a.txt"), at("f8cols/abs")) == 0 && symlink("gone", at("f8cols/broken")) == 0);
    Listing l;
    Err err;
    CHECK(fs_list(&ta, at("f8cols"), false, (Sort){ SORT_NAME, false }, &l, &err) && l.n == 5);
    for (isize i = 0; i < l.n; i++) {
        const Entry *e = &l.e[i];
        CHECK(e->uid == (u32)getuid() && e->gid == (u32)getgid());
        if (str_eq(e->name, S("rel"))) CHECK_STR(e->target, "sub");
        else if (str_eq(e->name, S("abs"))) CHECK_STR(e->target, at("f8cols/a.txt"));
        else if (str_eq(e->name, S("broken"))) CHECK(str_eq(e->target, S("gone")) && e->mode == 0);
        else CHECK(e->target.len == 0);
    }
    // View → Show Permissions, remembered: "perms=off" leaves them out; anything else, or none, shows them.
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s", at("f8cols/state"));
    Err e2;
    CHECK(!state_load(&ta, path).no_perms);
    CHECK(state_put(&ta, path, "perms", "off", &e2) && state_load(&ta, path).no_perms);
    CHECK(state_put(&ta, path, "perms", "on", &e2) && !state_load(&ta, path).no_perms);
    // The entry toggles it and asks gui/ to remember.
    Ui u = menu_ui(2);
    u.perms_cols = true;
    CHECK(ui_menu_mark(&u, UM_PERMS_COLS) == 1 && ui_menu_enabled(&u, UM_PERMS_COLS));
    u.menu_on = true, u.menu = 2;
    UiInput in = ui_input_none();
    in.menu_item = menu_place(2, UM_PERMS_COLS);
    CHECK(ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in).kind == UA_PERMS_COLS && !u.perms_cols && ui_menu_mark(&u, UM_PERMS_COLS) == 0);
    // Ctrl+P toggles it too (Michael, 2026-10-27), from the files and from the sidebar.
    CHECK(menu_keys(&u, 1, UK_PERMS_COLS).kind == UA_PERMS_COLS && u.perms_cols);
    u.focus = UI_FOCUS_SIDE;
    CHECK(menu_keys(&u, 1, UK_PERMS_COLS).kind == UA_PERMS_COLS && !u.perms_cols);
    ui_unfocus(&u, UI_FOCUS_SIDE);
    // View → Show Group, Ctrl+G (2026-10-27): off unless "group=on"; greyed, and the key does
    // nothing, while Show Permissions is off.
    CHECK(!state_load(&ta, path).group);
    CHECK(state_put(&ta, path, "group", "on", &e2) && state_load(&ta, path).group);
    CHECK(state_put(&ta, path, "group", "off", &e2) && !state_load(&ta, path).group);
    CHECK(!ui_menu_enabled(&u, UM_GROUP_COL) && ui_menu_mark(&u, UM_GROUP_COL) == 0);
    CHECK(menu_keys(&u, 1, UK_GROUP_COL).kind == UA_NONE && !u.group_col);
    u.perms_cols = true;
    CHECK(ui_menu_enabled(&u, UM_GROUP_COL));
    CHECK(menu_keys(&u, 1, UK_GROUP_COL).kind == UA_GROUP_COL && u.group_col && ui_menu_mark(&u, UM_GROUP_COL) == 1);
    u.menu_on = true, u.menu = 2;
    in = ui_input_none(), in.menu_item = menu_place(2, UM_GROUP_COL);
    CHECK(ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in).kind == UA_GROUP_COL && !u.group_col && !u.menu_on);
    // Show Permissions off keeps Show Group's check for when it comes back.
    u.group_col = true;
    CHECK(menu_keys(&u, 1, UK_PERMS_COLS).kind == UA_PERMS_COLS && !u.perms_cols && u.group_col && ui_menu_mark(&u, UM_GROUP_COL) == 1);
    // In the icon view (Michael, 2026-10-27) all three are greyed and the keys do nothing: no
    // columns there, nor colours; their checks stay for the list.
    u.grid = true, u.colors = true, u.perms_cols = true;
    CHECK(!ui_menu_enabled(&u, UM_PERMS_COLS) && !ui_menu_enabled(&u, UM_GROUP_COL) && !ui_menu_enabled(&u, UM_COLORS));
    CHECK(ui_menu_mark(&u, UM_PERMS_COLS) == 1 && ui_menu_mark(&u, UM_GROUP_COL) == 1 && ui_menu_mark(&u, UM_COLORS) == 1);
    CHECK(menu_keys(&u, 1, UK_PERMS_COLS).kind == UA_NONE && u.perms_cols);
    CHECK(menu_keys(&u, 1, UK_GROUP_COL).kind == UA_NONE && u.group_col);
    u.grid = false;
    CHECK(ui_menu_enabled(&u, UM_PERMS_COLS) && ui_menu_enabled(&u, UM_GROUP_COL) && ui_menu_enabled(&u, UM_COLORS));
    u.perms_cols = false, u.group_col = false;
    // View → Show Colors (2026-10-27): on unless "colors=off"; the entry toggles it.
    CHECK(!state_load(&ta, path).no_colors);
    CHECK(state_put(&ta, path, "colors", "off", &e2) && state_load(&ta, path).no_colors);
    u.colors = true, u.menu_on = true, u.menu = 2;
    CHECK(ui_menu_mark(&u, UM_COLORS) == 1 && ui_menu_enabled(&u, UM_COLORS));
    in = ui_input_none(), in.menu_item = menu_place(2, UM_COLORS);
    CHECK(ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in).kind == UA_COLORS && !u.colors && ui_menu_mark(&u, UM_COLORS) == 0);
    // The status line says when the permissions gave way for room, whatever else it says.
    char sl[256];
    UiInfo info = { .n = 3, .avail = -1, .cramped = true };
    ui_status_info(sl, sizeof sl, &info);
    CHECK_STR(str_from_c(sl), "3 items \xC2\xB7 Permissions hidden: the window is too narrow");
    info.marked = 2;
    ui_status_info(sl, sizeof sl, &info);
    CHECK_STR(str_from_c(sl), "2 items selected \xC2\xB7 Permissions hidden: the window is too narrow");
    info.cramped = false;
    ui_status_info(sl, sizeof sl, &info);
    CHECK_STR(str_from_c(sl), "2 items selected");
    // View → Show Menu (2026-10-27): unchecked, the bar hides and a note says how its menus still
    // open; it takes every key, Enter, Esc or OK close it; checked again, no note.
    CHECK(!state_load(&ta, path).no_bar);
    CHECK(state_put(&ta, path, "menubar", "off", &e2) && state_load(&ta, path).no_bar);
    CHECK(ui_menu_mark(&u, UM_SHOW_MENU) == 1 && ui_menu_enabled(&u, UM_SHOW_MENU));
    u.menu_on = true, u.menu = 2;
    in = ui_input_none(), in.menu_item = menu_place(2, UM_SHOW_MENU);
    CHECK(ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in).kind == UA_MENUBAR && u.bar_hidden && u.bar_told && ui_menu_mark(&u, UM_SHOW_MENU) == 0);
    UiAction a = menu_keys(&u, 2, UK_DOWN, UK_DELETE);
    CHECK(u.bar_told && a.kind == UA_NONE && u.list.sel == 0);
    in = ui_input_none(), in.click = 1;   // nor any click under it
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.bar_told && u.list.sel == 0);
    menu_keys(&u, 1, UK_ENTER);
    CHECK(!u.bar_told);
    u.bar_told = true;
    menu_keys(&u, 1, UK_ESC);
    CHECK(!u.bar_told);
    u.bar_told = true;
    in = ui_input_none(), in.modal = UI_MODAL_OK;
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(!u.bar_told);
    // Hidden, F10 still opens the menus; Show Menu again: shown, no note.
    menu_keys(&u, 1, UK_F10);
    CHECK(u.menu_on && u.menu == 0);
    menu_keys(&u, 1, UK_ESC);
    u.menu_on = true, u.menu = 2;
    in = ui_input_none(), in.menu_item = menu_place(2, UM_SHOW_MENU);
    CHECK(ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in).kind == UA_MENUBAR && !u.bar_hidden && !u.bar_told);
}
