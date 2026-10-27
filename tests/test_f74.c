// F7.4's unit tests: the Edit, View and Go menus. Included by test.c after test_f7.c (menu_ui,
// menu_keys).

// The place of entry it in menu m, -1 when it isn't there.
static int menu_place(int m, UiMenuItem it)
{
    for (int i = 0; i < UI_MENU[m].n; i++)
        if (UI_MENU[m].items[i] == it) return i;
    return -1;
}

// The menu entry it is in.
static int menu_of_item(UiMenuItem it)
{
    for (int m = 0; m < UI_MENUS; m++)
        if (menu_place(m, it) >= 0) return m;
    return -1;
}

// A state where every entry can be picked (Restore aside, which needs the trash view): an undo, a
// history both ways, a folder above, files to paste, the icon view at its first size.
static Ui menu_all_on(u8 *mark)
{
    Ui u = menu_ui(4);
    u.mark = mark, u.can_undo = u.can_back = u.can_forward = u.can_up = u.can_paste = true;
    u.grid = true, u.zoom = 64;
    ui_set_cols(&u.list, 2);
    memset(mark, 0, 4);
    return u;
}

static void test_menu_f74(void)
{
    UiNames names = { name_at, NULL, NULL };
    static u8 m1[4], m2[4];
    Ui u;
    // The menus as decided (docs/plan.md "F7.4 decided"): their titles and Alt letters, every entry
    // in exactly one of them, none twice.
    CHECK(strcmp(UI_MENU[1].title, "Edit") == 0 && strcmp(UI_MENU[2].title, "View") == 0 && strcmp(UI_MENU[3].title, "Go") == 0);
    CHECK(ui_menu_of('v') == 2 && ui_menu_of('g') == 3);
    for (int it = 1; it < UM_ITEMS; it++) {
        int n = 0;
        for (int m = 0; m < UI_MENUS; m++)
            for (int i = 0; i < UI_MENU[m].n; i++) n += (int)UI_MENU[m].items[i] == it;
        CHECK(n == 1);
    }
    // Each entry that stands for a key does what the key does, picked by Enter or by the pointer:
    // the same action and the same state after (marks, find, the rename line, the questions).
    for (int it = 1; it < UM_ITEMS; it++) {
        UiEvent ev = UI_MENU_ITEM[it].ev;
        if (!ev.key && !ev.ch) continue;
        for (int pointer = 0; pointer < 2; pointer++) {
            Ui a = menu_all_on(m1), b = menu_all_on(m2);
            a.list.sel = b.list.sel = 1;
            a.in_trash = b.in_trash = it == UM_RESTORE;   // shown in the trash view only
            UiInput in = ui_input_none();
            in.ev[in.nev++] = ev;
            UiAction ka = ui_handle(&a, names, &in);
            int m = menu_of_item((UiMenuItem)it);
            UiAction ma;
            if (pointer) {
                in = ui_input_none(), in.menu_title = m;
                ui_handle(&b, names, &in);
                in = ui_input_none(), in.menu_item = menu_place(m, (UiMenuItem)it);
                ma = ui_handle(&b, names, &in);
            } else {
                menu_keys(&b, 1, -(int)UI_MENU[m].letter);
                b.menu_at = menu_place(m, (UiMenuItem)it);
                ma = menu_keys(&b, 1, UK_ENTER);
            }
            CHECK(ka.kind == ma.kind && ka.index == ma.index && ka.all == ma.all && !b.menu_on);
            CHECK(a.marked == b.marked && memcmp(m1, m2, 4) == 0 && a.finding == b.finding && a.editing == b.editing &&
                  a.confirming == b.confirming && a.sort.key == b.sort.key && a.sort.reverse == b.sort.reverse && a.list.sel == b.list.sel);
            CHECK(!b.menu_key.key && !b.menu_key.ch);   // run once
        }
    }
    // A menu's own keys stay the menu's: Down moves its mark, not the list's cursor.
    u = menu_all_on(m1);
    menu_keys(&u, 1, -'e');
    isize was = u.list.sel;
    menu_keys(&u, 2, UK_DOWN, UK_UP);
    CHECK(u.menu_on && u.list.sel == was);
    // The pointer's pick runs before the frame's own keys.
    u = menu_all_on(m1);
    UiInput in = ui_input_none();
    in.menu_title = 1;
    ui_handle(&u, names, &in);
    in = ui_input_none(), in.menu_item = menu_place(1, UM_SELECT_ALL);
    in.ev[in.nev++] = (UiEvent){ UK_INVERT, 0 };
    ui_handle(&u, names, &in);
    CHECK(u.marked == 0);
    // The entries without a key: sort, the sidebar's modes, the trash view.
    u = menu_all_on(m1);
    menu_keys(&u, 1, -'v');
    u.menu_at = menu_place(2, UM_SORT_SIZE);
    UiAction a = menu_keys(&u, 1, UK_ENTER);
    CHECK(a.kind == UA_SORT && u.sort.key == SORT_SIZE && ui_menu_mark(&u, UM_SORT_SIZE) == 1 && ui_menu_mark(&u, UM_SORT_NAME) == 0);
    u.sort.reverse = true;
    menu_keys(&u, 1, -'v');
    u.menu_at = menu_place(2, UM_SORT_MTIME);
    a = menu_keys(&u, 1, UK_ENTER);
    CHECK(a.kind == UA_SORT && u.sort.key == SORT_MTIME && u.sort.reverse);   // the order kept
    menu_keys(&u, 1, -'v');
    u.menu_at = menu_place(2, UM_SIDE_TREE);
    a = menu_keys(&u, 1, UK_ENTER);
    CHECK(a.kind == UA_SIDE_MODE && u.side_mode == SIDE_TREE && ui_menu_mark(&u, UM_SIDE_TREE) == 1 && ui_menu_mark(&u, UM_SIDE_PLACES) == 0);
    menu_keys(&u, 1, -'v');
    u.menu_at = menu_place(2, UM_SIDE_NONE);
    CHECK(menu_keys(&u, 1, UK_ENTER).kind == UA_SIDE_MODE && u.side_mode == SIDE_HIDDEN && ui_menu_mark(&u, UM_SIDE_NONE) == 1);
    menu_keys(&u, 1, -'v');
    u.menu_at = menu_place(2, UM_SIDE_PLACES);
    CHECK(menu_keys(&u, 1, UK_ENTER).kind == UA_SIDE_MODE && u.side_mode == SIDE_PLACES);
    menu_keys(&u, 1, -'e');
    u.menu_at = menu_place(1, UM_DUPLICATE);
    CHECK(menu_keys(&u, 1, UK_ENTER).kind == UA_DUPLICATE && !u.menu_on);
    menu_keys(&u, 1, -'g');
    u.menu_at = menu_place(3, UM_TRASH_VIEW);
    CHECK(menu_keys(&u, 1, UK_ENTER).kind == UA_TRASH_VIEW && !u.menu_on);
    // The marks: hidden files, the view, the order; none on the others.
    u = menu_ui(3);
    CHECK(ui_menu_mark(&u, UM_HIDDEN) == 0 && ui_menu_mark(&u, UM_VIEW_LIST) == 1 && ui_menu_mark(&u, UM_VIEW_ICONS) == 0);
    CHECK(ui_menu_mark(&u, UM_SORT_REVERSE) == 0 && ui_menu_mark(&u, UM_SORT_NAME) == 1 && ui_menu_mark(&u, UM_CUT) == -1);
    u.shows_hidden = true, u.grid = true, u.sort.reverse = true;
    CHECK(ui_menu_mark(&u, UM_HIDDEN) == 1 && ui_menu_mark(&u, UM_VIEW_ICONS) == 1 && ui_menu_mark(&u, UM_VIEW_LIST) == 0 && ui_menu_mark(&u, UM_SORT_REVERSE) == 1);
    for (int it = 1; it < UM_ITEMS; it++) CHECK((UI_MENU_ITEM[it].mark != 0) == (ui_menu_mark(&u, (UiMenuItem)it) >= 0));
    // Greyed where the key would do nothing: no undo, nothing to paste or a folder that takes
    // none, no history, at the top, an empty folder, the keys in the sidebar, the zoom in the list
    // view and at its ends.
    u = menu_ui(3);
    CHECK(!ui_menu_enabled(&u, UM_UNDO) && !ui_menu_enabled(&u, UM_PASTE) && !ui_menu_enabled(&u, UM_BACK) && !ui_menu_enabled(&u, UM_FORWARD));
    CHECK(!ui_menu_enabled(&u, UM_PARENT) && !ui_menu_enabled(&u, UM_ZOOM_IN) && !ui_menu_enabled(&u, UM_ZOOM_OUT) && !ui_menu_enabled(&u, UM_ZOOM_RESET));
    u.can_undo = u.can_back = u.can_forward = u.can_up = u.can_paste = true;
    CHECK(ui_menu_enabled(&u, UM_UNDO) && ui_menu_enabled(&u, UM_PASTE) && ui_menu_enabled(&u, UM_BACK) && ui_menu_enabled(&u, UM_FORWARD) && ui_menu_enabled(&u, UM_PARENT));
    u.can_back = false;   // each its own way
    CHECK(!ui_menu_enabled(&u, UM_BACK) && ui_menu_enabled(&u, UM_FORWARD));
    u.can_back = true, u.can_forward = false;
    CHECK(ui_menu_enabled(&u, UM_BACK) && !ui_menu_enabled(&u, UM_FORWARD));
    u.zoom = 64;   // a size of the icon view's, but the list shown: no zoom
    CHECK(!ui_menu_enabled(&u, UM_ZOOM_RESET) && !ui_menu_enabled(&u, UM_ZOOM_IN));
    u.zoom = 0;
    u.can_write = false;
    CHECK(!ui_menu_enabled(&u, UM_PASTE) && !ui_menu_enabled(&u, UM_RENAME) && !ui_menu_enabled(&u, UM_DUPLICATE) && ui_menu_enabled(&u, UM_COPY));
    u.can_write = true, u.side_focus = true;
    CHECK(!ui_menu_enabled(&u, UM_CUT) && !ui_menu_enabled(&u, UM_COPY) && !ui_menu_enabled(&u, UM_RENAME) && !ui_menu_enabled(&u, UM_TRASH));
    CHECK(!ui_menu_enabled(&u, UM_DUPLICATE));
    CHECK(ui_menu_enabled(&u, UM_SELECT_ALL) && ui_menu_enabled(&u, UM_INVERT));
    u = menu_ui(0);
    CHECK(!ui_menu_enabled(&u, UM_SELECT_ALL) && !ui_menu_enabled(&u, UM_INVERT) && !ui_menu_enabled(&u, UM_CUT) && !ui_menu_enabled(&u, UM_DELETE));
    CHECK(!ui_menu_enabled(&u, UM_DUPLICATE));
    CHECK(ui_menu_enabled(&u, UM_RELOAD) && ui_menu_enabled(&u, UM_FIND) && ui_menu_enabled(&u, UM_HOME));
    u = menu_ui(3);
    u.grid = true;   // the first size: in only
    CHECK(ui_menu_enabled(&u, UM_ZOOM_IN) && ui_menu_enabled(&u, UM_ZOOM_OUT) && !ui_menu_enabled(&u, UM_ZOOM_RESET));
    u.zoom = UI_ZOOM_PX[0];
    CHECK(!ui_menu_enabled(&u, UM_ZOOM_OUT) && ui_menu_enabled(&u, UM_ZOOM_RESET));
    u.zoom = UI_ZOOM_PX[UI_ZOOMS - 1];
    CHECK(!ui_menu_enabled(&u, UM_ZOOM_IN) && ui_menu_enabled(&u, UM_ZOOM_OUT));
    // The trash view: Move to Trash gone, Restore there; Copy, Cut, Rename and the terminal greyed.
    u = menu_ui(3);
    CHECK(ui_menu_shown(&u, UM_TRASH) && !ui_menu_shown(&u, UM_RESTORE) && !ui_menu_enabled(&u, UM_RESTORE));
    u.in_trash = true;
    CHECK(!ui_menu_shown(&u, UM_TRASH) && !ui_menu_enabled(&u, UM_TRASH) && ui_menu_shown(&u, UM_RESTORE) && ui_menu_enabled(&u, UM_RESTORE));
    CHECK(!ui_menu_enabled(&u, UM_COPY) && !ui_menu_enabled(&u, UM_CUT) && !ui_menu_enabled(&u, UM_RENAME) && !ui_menu_enabled(&u, UM_TERMINAL));
    CHECK(!ui_menu_enabled(&u, UM_DUPLICATE));
    CHECK(ui_menu_enabled(&u, UM_DELETE));
    // The keys step over what isn't shown: in the trash view from Rename past the line to Restore.
    menu_keys(&u, 1, -'e');
    u.menu_at = menu_place(1, UM_RENAME);
    u.can_write = true;
    u.in_trash = false;
    menu_keys(&u, 1, UK_DOWN);
    CHECK(u.menu_at == menu_place(1, UM_TRASH));
    u.in_trash = true;
    u.menu_at = menu_place(1, UM_SELECT_ALL);
    menu_keys(&u, 3, UK_DOWN, UK_DOWN, UK_DOWN);   // Select Items Matching, Invert, then (Rename greyed) Restore
    CHECK(u.menu_at == menu_place(1, UM_RESTORE));
    // The pointer can't pick one not shown, nor mark it.
    in = ui_input_none(), in.menu_item = menu_place(1, UM_TRASH);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.menu_on);
    CHECK(!ui_menu_hover(&u, -1, menu_place(1, UM_TRASH)));
    // The new keys: Ctrl+Shift+I inverts the selection, Ctrl+R lists the folder again, Alt+Home
    // goes home as ~ does.
    u = menu_ui(4);
    u.mark = m1, memset(m1, 0, 4);
    ui_mark(&u, 1, true);
    menu_keys(&u, 1, UK_INVERT);
    CHECK(u.marked == 3 && !m1[1] && m1[0] && m1[2] && m1[3]);
    menu_keys(&u, 1, UK_INVERT);
    CHECK(u.marked == 1 && m1[1]);
    CHECK(menu_keys(&u, 1, UK_RELOAD).kind == UA_RELOAD && menu_keys(&u, 1, UK_HOMEDIR).kind == UA_HOMEDIR);
}

// Select Items Matching (F7.4): the find bar's select mode. Enter makes the selection every match
// (as Nemo's), and closes it; Esc and an empty Enter leave the selection as it was.
static void test_select_match(void)
{
    UiNames names = { name_at, NULL, NULL };
    static u8 mark[7];
    Ui u = menu_ui(7);   // alpha Beta gamma.txt delta BETAMAX omega café
    u.mark = mark, memset(mark, 0, sizeof mark);
    ui_mark(&u, 0, true), ui_mark(&u, 5, true);
    menu_keys(&u, 1, UK_SELECT_MATCH);
    CHECK(u.finding && u.find_keys && u.find_select && u.marked == 2);
    UiInput in = chars("ta");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && u.find_select);
    menu_keys(&u, 1, UK_ENTER);
    CHECK(!u.finding && !u.find_select && u.marked == 3 && mark[1] && mark[3] && mark[4] && !mark[0] && !mark[5]);
    CHECK(u.last_len == 2);   // n and N find it again
    // Esc: the selection as it was, the cursor back.
    u.list.sel = 6;
    menu_keys(&u, 1, UK_SELECT_MATCH);
    in = chars("al");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 0);
    menu_keys(&u, 1, UK_ESC);
    CHECK(!u.finding && !u.find_select && u.marked == 3 && u.list.sel == 6);
    // An empty Enter only closes it.
    menu_keys(&u, 2, UK_SELECT_MATCH, UK_ENTER);
    CHECK(!u.finding && u.marked == 3);
    // The toggles: case kept, "Beta" alone.
    u.find_opts.cased = true;
    menu_keys(&u, 1, UK_SELECT_MATCH);
    in = chars("Beta");
    ui_handle(&u, names, &in);
    menu_keys(&u, 1, UK_ENTER);
    CHECK(u.marked == 1 && mark[1]);
    u.find_opts.cased = false;
    // A regular expression that doesn't compile: the selection stays, the bar too.
    u.find_opts.regex = true;
    menu_keys(&u, 1, UK_SELECT_MATCH);
    in = chars("(");
    ui_handle(&u, names, &in);
    menu_keys(&u, 1, UK_ENTER);
    CHECK(u.find_m.bad && u.finding && u.find_select && u.marked == 1 && mark[1]);
    menu_keys(&u, 1, UK_ESC);
    u.find_opts.regex = false;
    // No match: the selection empties (nothing matches).
    menu_keys(&u, 1, UK_SELECT_MATCH);
    in = chars("zz");
    ui_handle(&u, names, &in);
    menu_keys(&u, 1, UK_ENTER);
    CHECK(u.marked == 0 && !u.finding);
    // A command leaves the bar and is the list's: the select bar closes rather than staying.
    menu_keys(&u, 1, UK_SELECT_MATCH);
    in = chars("a");
    ui_handle(&u, names, &in);
    CHECK(menu_keys(&u, 1, UK_TAB_NEW).kind == UA_TAB_NEW && !u.finding && !u.find_select);
    // The menu opened over it closes it too.
    menu_keys(&u, 1, UK_SELECT_MATCH);
    menu_keys(&u, 1, -'e');
    CHECK(u.menu_on && !u.finding && !u.find_select);
    menu_keys(&u, 1, UK_ESC);
    // A plain find after it is a plain find; with the find bar open, Ctrl+Shift+A makes it select.
    menu_keys(&u, 1, UK_FIND);
    CHECK(u.finding && !u.find_select);
    menu_keys(&u, 1, UK_SELECT_MATCH);
    CHECK(u.finding && u.find_select && u.find_len == 0);
    menu_keys(&u, 1, UK_ESC);
    // An empty folder: nothing to select from, no bar.
    u = menu_ui(0);
    menu_keys(&u, 1, UK_SELECT_MATCH);
    CHECK(!u.finding);
    ui_matcher_set(&u.find_m, STR_EMPTY, (UiMatchOpts){ 0 });
}

// Michael's keys (2026-10-27): Alt+End the trash view, as Alt+Home is home; Shift+Enter in the files
// Open With, and Enter everywhere else it was (the sidebar, the find bar, a question).
static void test_f74_keys(void)
{
    UiNames names = { name_at, NULL, NULL };
    Ui u = menu_ui(3);
    CHECK(menu_keys(&u, 1, UK_TRASH_VIEW).kind == UA_TRASH_VIEW);
    CHECK(strcmp(UI_MENU_ITEM[UM_TRASH_VIEW].key, "Alt+End") == 0 && strcmp(UI_MENU_ITEM[UM_OPEN_WITH].key, "Shift+Enter") == 0);
    // In the files: Open With, on the cursor's entry or the marked ones (the panel takes the targets).
    UiAction a = menu_keys(&u, 1, UK_SHIFT_ENTER);
    CHECK(a.kind == UA_OPEN_WITH && u.ow);
    ui_ow_close(&u);
    // The trash view: Open With doesn't work there, as from the menu.
    u.in_trash = true;
    CHECK(menu_keys(&u, 1, UK_SHIFT_ENTER).kind == UA_NOT_IN_TRASH && !u.ow);
    u.in_trash = false;
    // An empty folder: nothing.
    Ui e = menu_ui(0);
    CHECK(menu_keys(&e, 1, UK_SHIFT_ENTER).kind == UA_NONE && !e.ow);
    // The sidebar with the keys: its Enter, the place.
    u.side.rows = 5;
    ui_set(&u.side, 3, 1);
    u.side_focus = true;
    a = menu_keys(&u, 1, UK_SHIFT_ENTER);
    CHECK(a.kind == UA_PLACE && a.index == 1 && !u.ow);
    u.side_focus = false;
    // The find bar with the keys: Enter's, the keys back to the list, no panel.
    UiInput in = chars("/a");
    ui_handle(&u, names, &in);
    CHECK(menu_keys(&u, 1, UK_SHIFT_ENTER).kind == UA_NONE && u.finding && !u.find_keys && !u.ow);
    menu_keys(&u, 1, UK_ESC);
    ui_find_close(&u);
    // A question: Enter's answer (the delete question's yes).
    u.confirming = true, u.confirm_n = 1;
    a = menu_keys(&u, 1, UK_SHIFT_ENTER);
    CHECK(a.kind == UA_DELETE && !u.confirming && !u.ow);
    ui_matcher_set(&u.find_m, STR_EMPTY, (UiMatchOpts){ 0 });
}
