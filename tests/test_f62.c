// F6.2's unit tests: the icon view's state word, its cursor and scroll, and the names under the
// icons. Included by test.c.

static void test_state_view(void)
{
    arena_reset(&ta);
    ops_guard(root);
    char path[PATH_MAX];
    env_set("XDG_STATE_HOME", at("state62"));
    CHECK(state_path(path, sizeof path));
    // No file: the list.
    CHECK(!state_load(&ta, path).icons);
    Err err;
    State s = { SIDE_TREE, true, 0, false };
    CHECK(state_save(path, &s, &err));
    State r = state_load(&ta, path);
    CHECK(r.icons && r.side == SIDE_TREE);
    CHECK(strcmp(read_file(&ta, path).p, "sidebar=tree\nview=icons\n") == 0);
    s.icons = false;
    CHECK(state_save(path, &s, &err) && !state_load(&ta, path).icons);
    // Either word, the last one winning; an unknown one is left out.
    put(path, "view=icons\nview=grid\n");
    CHECK(state_load(&ta, path).icons);
    put(path, "view = icons\nview=list\n");
    CHECK(!state_load(&ta, path).icons);
    put(path, "sidebar=hidden\nview=icons");
    r = state_load(&ta, path);
    CHECK(r.icons && r.side == SIDE_HIDDEN);
    env_restore();
}

// A grid of n entries, cols across and rows on screen, the cursor on sel.
static void grid(Ui *u, isize n, int cols, int rows, isize sel)
{
    *u = (Ui){ 0 };
    u->grid = true;
    ui_resize(&u->list, rows), ui_resize(&u->side, 5);
    ui_set_cols(&u->list, cols);
    ui_set(&u->list, n, sel);
    ui_set(&u->side, 5, 0);
}

static isize key_sel(Ui *u, UiKey k)
{
    UiInput in = keys(1, k);
    ui_handle(u, (UiNames){ name_at, NULL, NULL }, &in);
    return u->list.sel;
}

static void test_grid_keys(void)
{
    static Ui u;
    // 23 entries, 5 across: rows 0-4, 5-9, 10-14, 15-19, 20-22.
    grid(&u, 23, 5, 3, 7);
    CHECK(key_sel(&u, UK_RIGHT) == 8 && key_sel(&u, UK_LEFT) == 7);
    // With Shift held they are the same moves (Shift only selects in an edit line).
    CHECK(key_sel(&u, UK_SEL_RIGHT) == 8 && key_sel(&u, UK_SEL_LEFT) == 7);
    // Left and Right run on across the rows' ends.
    u.list.sel = 9;
    CHECK(key_sel(&u, UK_RIGHT) == 10 && key_sel(&u, UK_LEFT) == 9);
    // Up and Down a row, in the column.
    u.list.sel = 7;
    CHECK(key_sel(&u, UK_DOWN) == 12 && key_sel(&u, UK_UP) == 7 && key_sel(&u, UK_UP) == 2);
    // The first row: Up goes round to the last row's cell in its column (Michael, 2026-10-25).
    CHECK(key_sel(&u, UK_UP) == 22);
    u.list.sel = 4;   // a column the last row lacks: the row above it
    CHECK(key_sel(&u, UK_UP) == 19);
    u.list.sel = 2;
    // Down onto the last row's cell under it; from 18, whose column the last row lacks, the last.
    u.list.sel = 16;
    CHECK(key_sel(&u, UK_DOWN) == 21);
    CHECK(key_sel(&u, UK_DOWN) == 1);   // the last row: Down goes round to the top, in its column
    u.list.sel = 18;
    CHECK(key_sel(&u, UK_DOWN) == 22);
    // h l k j as the arrows.
    u.list.sel = 7;
    UiInput in = chars("ljkh");
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.list.sel == 7);
    in = chars("j");
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.list.sel == 12);
    // A page is the rows on screen less one (2 here): PgDn from 2 to 12, then 22, then the last row
    // stays put; PgUp back.
    u.list.sel = 2;
    CHECK(key_sel(&u, UK_PAGEDOWN) == 12 && key_sel(&u, UK_PAGEDOWN) == 22 && key_sel(&u, UK_PAGEDOWN) == 22);
    CHECK(key_sel(&u, UK_PAGEUP) == 12 && key_sel(&u, UK_PAGEUP) == 2 && key_sel(&u, UK_PAGEUP) == 2);
    u.list.sel = 19;
    CHECK(key_sel(&u, UK_PAGEDOWN) == 22);   // no cell under it two rows down: the last
    // Home and End.
    CHECK(key_sel(&u, UK_HOME) == 0 && key_sel(&u, UK_END) == 22);
    // Left from the first goes round to the last, Right from the last to the first; neither opens
    // nor goes up. Enter opens, Backspace goes up.
    u.list.sel = 0;
    in = keys(1, UK_LEFT);
    CHECK(ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in).kind == UA_NONE && u.list.sel == 22);
    in = keys(1, UK_RIGHT);
    CHECK(ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in).kind == UA_NONE && u.list.sel == 0);
    u.list.sel = 22;
    in = keys(1, UK_ENTER);
    UiAction a = ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(a.kind == UA_OPEN && a.index == 22);
    in = keys(1, UK_BACKSPACE);
    CHECK(ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in).kind == UA_PARENT);
    // The sidebar keeps its own keys: Down moves its cursor, Left in the tree is the tree's.
    u.list.sel = 7;
    in = keys(2, UK_TAB, UK_DOWN);
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.side_focus && u.side.sel == 1 && u.list.sel == 7);
    // In the list Down moves one; Left and Right do nothing there either (Michael, 2026-10-24).
    grid(&u, 23, 5, 3, 7);
    u.grid = false, ui_set_cols(&u.list, 1);
    CHECK(key_sel(&u, UK_DOWN) == 8);
    in = keys(2, UK_LEFT, UK_RIGHT);
    CHECK(ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in).kind == UA_NONE && u.list.sel == 8);
    // An empty folder: nothing moves, nothing breaks.
    grid(&u, 0, 5, 3, 0);
    CHECK(key_sel(&u, UK_DOWN) == 0 && key_sel(&u, UK_PAGEDOWN) == 0 && key_sel(&u, UK_RIGHT) == 0);
}

static void test_grid_scroll(void)
{
    static Ui u;
    // 23 entries, 5 across, 2 rows on screen: the top is a row's first entry, 15 at most (rows 3-4).
    grid(&u, 23, 5, 2, 0);
    CHECK(u.list.top == 0 && ui_max_top(&u.list) == 15);
    key_sel(&u, UK_DOWN);
    CHECK(u.list.top == 0);
    key_sel(&u, UK_DOWN);   // row 2: the scroll moves one row
    CHECK(u.list.sel == 10 && u.list.top == 5);
    key_sel(&u, UK_END);
    CHECK(u.list.top == 15);
    key_sel(&u, UK_HOME);
    CHECK(u.list.top == 0);
    // The wheel: a row a notch, the cursor left alone; not past either end.
    UiInput in = ui_input_none();
    in.wheel = -2;
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.list.top == 10 && u.list.sel == 0);
    in.wheel = -5;
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.list.top == 15);
    in.wheel = 9;
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.list.top == 0);
    // A click is a cell on screen, counted from the first shown.
    in = ui_input_none();
    in.wheel = -1;
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    in = ui_input_none();
    in.click = 3;
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.list.sel == 8);
    in = ui_input_none();
    in.click = 9;   // the last cell on screen: 5 + 9
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.list.sel == 14);
    // Fewer columns (a narrower window): the top row's first entry stays in view, the cursor too.
    grid(&u, 23, 5, 2, 12);
    CHECK(u.list.top == 5);
    ui_set_cols(&u.list, 3);   // rows of 3: 12 is on row 4
    CHECK(u.list.top % 3 == 0 && u.list.sel >= u.list.top && u.list.sel < u.list.top + 6);
    // Back to the list: one entry a row, the cursor shown.
    ui_set_cols(&u.list, 1);
    CHECK(u.list.top <= 12 && 12 < u.list.top + 2);
    // A top that isn't a row's first (from the list) snaps to its row.
    grid(&u, 23, 5, 2, 0);
    u.list.top = 7;
    ui_set(&u.list, 23, 7);
    CHECK(u.list.top == 5);
    // All of it fits: no scroll.
    grid(&u, 8, 5, 3, 7);
    CHECK(ui_max_top(&u.list) == 0 && u.list.top == 0);
}

static void test_view_switch(void)
{
    static Ui u;
    grid(&u, 23, 5, 3, 7);
    u.grid = false, ui_set_cols(&u.list, 1);
    // Ctrl+2 the icons, Ctrl+1 the list; the cursor stays.
    UiInput in = keys(1, UK_VIEW_ICONS);
    UiAction a = ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(a.kind == UA_VIEW && a.index == 1 && u.grid && u.list.sel == 7);
    ui_set_cols(&u.list, 5);   // as gui/ does at its next layout
    in = keys(1, UK_VIEW_LIST);
    a = ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(a.kind == UA_VIEW && a.index == 0 && !u.grid && u.list.sel == 7 && ui_cols(&u.list) == 1);
    // The top bar's buttons.
    in = ui_input_none();
    in.bar = UB_VIEW_ICONS;
    a = ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(a.kind == UA_VIEW && a.index == 1 && u.grid);
    in.bar = UB_VIEW_LIST;
    a = ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(a.kind == UA_VIEW && a.index == 0 && !u.grid);
    // From the sidebar's keyboard too; a prompt takes the keys first.
    in = keys(2, UK_TAB, UK_VIEW_ICONS);
    a = ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(a.kind == UA_VIEW && u.grid);
    u.confirming = true;
    in = keys(1, UK_VIEW_LIST);
    a = ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(a.kind == UA_NONE && u.grid);
    u.confirming = false;
    // A view button pressed is input that acts (it draws a frame).
    in = ui_input_none();
    in.bar = UB_VIEW_LIST;
    CHECK(ui_input_acts(&in));
}

static void test_wrap(void)
{
    isize f[8], t[8];
    bool cut;
    // It fits: one line.
    CHECK(ui_wrap(S("short"), 10, 1, adv_fixed, 2, f, t, &cut) == 1 && f[0] == 0 && t[0] == 5 && !cut);
    // Broken after a space, a '-', a '_' or a '.', the break kept on the first line.
    CHECK(ui_wrap(S("hello world"), 8, 1, adv_fixed, 2, f, t, &cut) == 2 && t[0] == 6 && f[1] == 6 && t[1] == 11 && !cut);
    CHECK(ui_wrap(S("report-2026.pdf"), 10, 1, adv_fixed, 2, f, t, &cut) == 2 && t[0] == 7 && t[1] == 15 && !cut);   // "report-" "2026.pdf"
    CHECK(ui_wrap(S("my_long_name"), 9, 1, adv_fixed, 2, f, t, &cut) == 2 && t[0] == 8);   // "my_long_" "name"
    // The last break that fits.
    CHECK(ui_wrap(S("a b c d e f"), 6, 1, adv_fixed, 2, f, t, &cut) == 2 && t[0] == 6);   // "a b c " "d e f"
    // No break: between code points, the line full.
    CHECK(ui_wrap(S("abcdefghij"), 4, 1, adv_fixed, 3, f, t, &cut) == 3 && t[0] == 4 && t[1] == 8 && f[2] == 8 && t[2] == 10 && !cut);
    // More than two lines' worth: the second cut with "…".
    CHECK(ui_wrap(S("abcdefghij"), 4, 1, adv_fixed, 2, f, t, &cut) == 2 && t[0] == 4 && f[1] == 4 && t[1] == 7 && cut);   // "abcd" "efg…"
    CHECK(ui_wrap(S("one two three four"), 8, 1, adv_fixed, 2, f, t, &cut) == 2 && t[0] == 8 && t[1] == 15 && cut);       // "one two " "three f…"
    // A break past the room on the first line doesn't count: "abcdefgh ij" breaks inside.
    CHECK(ui_wrap(S("abcdefgh ij"), 4, 1, adv_fixed, 2, f, t, &cut) == 2 && t[0] == 4 && cut);
    // Code points stay whole: é is two bytes, two wide.
    CHECK(ui_wrap(S("ab\xC3\xA9" "cd"), 3, 1, adv_fixed, 3, f, t, &cut) == 3 && t[0] == 2 && t[1] == 5 && t[2] == 6);   // "ab" "éc" "d"
    // Narrower than one code point: one a line anyway, so it ends.
    CHECK(ui_wrap(S("abc"), 0.5f, 1, adv_fixed, 8, f, t, &cut) == 3 && t[0] == 1 && t[1] == 2 && t[2] == 3);
    // One line allowed: ui_fit's cut.
    CHECK(ui_wrap(S("abcdefg"), 6, 1, adv_fixed, 1, f, t, &cut) == 1 && t[0] == 5 && cut);
    // Empty: one empty line.
    CHECK(ui_wrap(STR_EMPTY, 6, 1, adv_fixed, 2, f, t, &cut) == 1 && f[0] == 0 && t[0] == 0 && !cut);
    // A break as the very last character that fits: the line ends there.
    CHECK(ui_wrap(S("abc defg"), 4, 1, adv_fixed, 2, f, t, &cut) == 2 && t[0] == 4 && f[1] == 4 && t[1] == 8 && !cut);
}

static void test_tooltip(void)
{
    UiTip t = { -1, 0, false, false };
    // Nothing under the pointer: nothing due.
    CHECK(!ui_tip_hover(&t, -1, 100) && ui_tip_due(&t) == 0 && !ui_tip_tick(&t, 10000));
    // Onto a button: due UI_TIP_MS later, not before.
    CHECK(ui_tip_hover(&t, 7, 1000) && ui_tip_due(&t) == 1000 + UI_TIP_MS);
    CHECK(!ui_tip_tick(&t, 1000 + UI_TIP_MS - 1) && !t.shown);
    CHECK(ui_tip_tick(&t, 1000 + UI_TIP_MS) && t.shown);
    CHECK(!ui_tip_tick(&t, 5000) && t.shown && ui_tip_due(&t) == 0);   // it appears once
    // Moving on the same button changes nothing.
    CHECK(!ui_tip_hover(&t, 7, 2000) && t.shown);
    // To another: gone, and due from then.
    CHECK(ui_tip_hover(&t, 8, 3000) && !t.shown && ui_tip_due(&t) == 3000 + UI_TIP_MS);
    // A click before it shows: none while the pointer stays.
    CHECK(!ui_tip_act(&t) && ui_tip_due(&t) == 0 && !ui_tip_tick(&t, 9000) && !t.shown);
    // A click while it shows: gone (true: draw again), and it stays away.
    ui_tip_hover(&t, 9, 10000);
    ui_tip_tick(&t, 10000 + UI_TIP_MS);
    CHECK(ui_tip_act(&t) && !t.shown && !ui_tip_tick(&t, 20000));
    // Off the button and back: due again.
    CHECK(ui_tip_hover(&t, -1, 21000) && ui_tip_due(&t) == 0);
    CHECK(ui_tip_hover(&t, 9, 22000) && ui_tip_due(&t) == 22000 + UI_TIP_MS);
    // A key with nothing under the pointer: nothing to stay off.
    ui_tip_hover(&t, -1, 23000);
    CHECK(!ui_tip_act(&t) && !t.off);
}
