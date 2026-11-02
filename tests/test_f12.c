// F12 (docs/plan.md "F12 design"): shift-select's range mode (step 2) and its ofx.conf line.
// Included by test.c after test_f11.c.

// shift-select (F12): range, the default, or add; anything else is a bad line.
static void test_conf_shift(void)
{
    static Conf c;
    conf_defaults(&c);
    CHECK(!c.shift_add);
    conf_parse(S("shift-select = add\n"), &c);
    CHECK(c.shift_add && !c.bad);
    conf_parse(S("shift-select = range\n"), &c);
    CHECK(!c.shift_add && !c.bad);
    conf_parse(S("shift-select = Add\n"), &c);
    CHECK(!c.shift_add && c.bad == 1 && strcmp(c.why, "shift-select wants range or add") == 0);
}

// shift-select = range (F12, the default; Nemo's and GTK's): Shift marks exactly the anchor (where
// the last plain move or click left the cursor) to the cursor, in place of the other marks.
static void test_shift_range(void)
{
    UiNames names = { name_at, NULL, NULL };
    static u8 mark[20];
    Ui u = { 0 };
    ui_resize(&u.list, 10);
    ui_mark_set(&u, mark);
    ui_set(&u.list, 20, 2);
    CHECK(u.anchor == -1 && !u.shift_add);
    // No anchor yet: the first Shift starts at the cursor. Back again, the range shrinks.
    UiInput in = keys(2, UK_SEL_DOWN, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 4 && u.marked == 3 && mark[2] && mark[3] && mark[4] && u.anchor == 2);
    in = keys(1, UK_SEL_UP);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 3 && u.marked == 2 && !mark[4]);
    in = chars("KK");   // past the anchor: the other way
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && u.marked == 2 && mark[1] && mark[2] && !mark[3]);
    // A plain move keeps the marks and drops the anchor; the next Shift's range, from there,
    // replaces them.
    in = keys(3, UK_DOWN, UK_DOWN, UK_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 4 && u.marked == 2 && u.anchor == -1);
    in = chars("J");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 5 && u.marked == 2 && mark[4] && mark[5] && !mark[1]);
    in = keys(1, UK_SEL_END);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 19 && u.marked == 16 && mark[4] && !mark[3]);
    in = keys(1, UK_SEL_HOME);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 0 && u.marked == 5 && mark[0] && mark[4] && !mark[5]);
    // Keys that don't move the cursor leave the anchor (a copy between two Shift moves).
    in = keys(1, UK_COPY);
    CHECK(ui_handle(&u, names, &in).kind == UA_COPY);
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && u.marked == 4 && mark[1] && !mark[0] && u.anchor == 4);
    // A key that moves nothing, in the frame of a Shift move, keeps the anchor for the next.
    in = keys(1, UK_SEL_DOWN);
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 'x' };
    ui_handle(&u, names, &in);
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 3 && u.marked == 2 && mark[3] && mark[4] && u.anchor == 4);
    in = keys(2, UK_SEL_UP, UK_SEL_UP);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && u.marked == 4);
    // Clicks: a plain one moves the anchor, keeping the marks; Shift+click the anchor's range in
    // place of them; Ctrl+Shift+click adds it; Ctrl+click toggles and moves the anchor.
    in = ui_input_none(), in.click = 3;
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 3 && u.marked == 4 && u.anchor == -1);
    in = ui_input_none(), in.click = 5, in.click_mods = UI_MOD_SHIFT;
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 5 && u.marked == 3 && mark[3] && mark[5] && !mark[1] && u.anchor == 3);
    in = ui_input_none(), in.click = 1, in.click_mods = UI_MOD_SHIFT;
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && u.marked == 3 && mark[1] && mark[3] && !mark[4]);
    in = ui_input_none(), in.click = 8, in.click_mods = UI_MOD_SHIFT | UI_MOD_CTRL;
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 8 && u.marked == 8 && mark[1] && mark[8] && !mark[9] && u.anchor == 3);
    in = ui_input_none(), in.click = 9, in.click_mods = UI_MOD_CTRL;
    ui_handle(&u, names, &in);
    CHECK(u.marked == 9 && mark[9] && u.anchor == -1);
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 10 && u.marked == 2 && mark[9] && mark[10]);
    // A plain move back onto where the range ended still drops the anchor.
    in = keys(2, UK_DOWN, UK_UP);
    ui_handle(&u, names, &in);
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 11 && u.marked == 2 && mark[10] && mark[11] && !mark[9]);
    // The cursor moved by something other than the keys (a find, the pointer, gui/): the next
    // Shift starts there.
    u.list.sel = 14;
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 15 && u.marked == 2 && mark[14] && mark[15] && u.anchor == 14);
    u.list.sel = 12;
    in = ui_input_none(), in.click = (int)(13 - u.list.top), in.click_mods = UI_MOD_SHIFT;   // (a row on screen)
    ui_handle(&u, names, &in);
    CHECK(u.marked == 2 && mark[12] && mark[13] && u.anchor == 12);
    // A new listing forgets the anchor, and one past the list counts as none.
    ui_mark_set(&u, mark);
    memset(mark, 0, sizeof mark);
    ui_set(&u.list, 20, 6);
    in = keys(1, UK_SEL_UP);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 5 && u.marked == 2 && mark[5] && mark[6]);
    u.anchor = 10, u.range_end = 2;   // (just past the list, from where the last range ended)
    ui_set(&u.list, 10, 2);
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 3 && u.marked == 2 && mark[2] && mark[3] && u.anchor == 2);
    in = ui_input_none(), in.click = 7, in.click_mods = UI_MOD_SHIFT;   // (from the cursor, 3)
    u.anchor = 15;
    ui_handle(&u, names, &in);
    CHECK(u.marked == 5 && mark[3] && mark[7] && !mark[2] && u.anchor == 3);
    // The icon view: a row down marks the cells between in the list's order, and back up only the
    // anchor's.
    ui_mark_set(&u, mark);
    memset(mark, 0, sizeof mark);
    u.grid = true;
    ui_set_cols(&u.list, 4);
    ui_set(&u.list, 20, 1);
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 5 && u.marked == 5 && mark[1] && mark[5]);
    in = keys(1, UK_SEL_UP);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && u.marked == 1 && mark[1]);
}
