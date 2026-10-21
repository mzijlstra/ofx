// F5's unit tests: the tab bar's order, and the keys and clicks for tabs. Included by test.c.

static void tabs_check(const UiTabs *t, const char *want, int cur)
{
    char got[256] = "";
    for (int i = 0; i < t->n; i++) {
        size_t k = strlen(got);
        snprintf(got + k, sizeof got - k, "%s%d", i ? "," : "", t->slot[i]);
    }
    checks++;
    if (strcmp(got, want) != 0 || t->cur != cur) {
        failures++;
        fprintf(stderr, "tabs: got %s cur %d, want %s cur %d\n", got, t->cur, want, cur);
    }
}

static void test_tabs(void)
{
    static UiTabs t;
    memset(&t, 0, sizeof t);
    CHECK(ui_tabs_open(&t, 0, false));   // the first is shown whatever `show` says
    tabs_check(&t, "0", 0);
    ui_tabs_open(&t, 1, true);
    tabs_check(&t, "0,1", 1);
    ui_tabs_open(&t, 2, false);          // after the current one, in the background
    tabs_check(&t, "0,1,2", 1);
    ui_tabs_open(&t, 3, false);
    tabs_check(&t, "0,1,3,2", 1);
    ui_tabs_step(&t, 1);
    tabs_check(&t, "0,1,3,2", 2);
    ui_tabs_step(&t, 1), ui_tabs_step(&t, 1);
    tabs_check(&t, "0,1,3,2", 0);        // wraps
    ui_tabs_step(&t, -1);
    tabs_check(&t, "0,1,3,2", 3);
    ui_tabs_jump(&t, 2);
    tabs_check(&t, "0,1,3,2", 1);
    ui_tabs_jump(&t, 9);
    tabs_check(&t, "0,1,3,2", 3);
    ui_tabs_jump(&t, 5);                 // no fifth: nothing
    tabs_check(&t, "0,1,3,2", 3);
    ui_tabs_jump(&t, 0);
    tabs_check(&t, "0,1,3,2", 3);
    ui_tabs_move(&t, 1);                 // at the end already
    tabs_check(&t, "0,1,3,2", 3);
    ui_tabs_move(&t, -1);
    tabs_check(&t, "0,1,2,3", 2);
    ui_tabs_jump(&t, 1), ui_tabs_move(&t, -1);
    tabs_check(&t, "0,1,2,3", 0);
    // Closing: the current shows its right neighbour, the last its left; others keep the current.
    ui_tabs_jump(&t, 2);
    CHECK(ui_tabs_close(&t, 1) == 1);
    tabs_check(&t, "0,2,3", 1);
    CHECK(ui_tabs_close(&t, 2) == 3);
    tabs_check(&t, "0,2", 1);
    ui_tabs_open(&t, 5, false), ui_tabs_open(&t, 6, false);
    tabs_check(&t, "0,2,6,5", 1);
    CHECK(ui_tabs_close(&t, 0) == 0);    // before the current: it keeps its tab
    tabs_check(&t, "2,6,5", 0);
    ui_tabs_jump(&t, 3);
    CHECK(ui_tabs_close(&t, 0) == 2);
    tabs_check(&t, "6,5", 1);
    CHECK(ui_tabs_close(&t, 7) == -1 && ui_tabs_close(&t, -1) == -1);
    CHECK(ui_tabs_close(&t, 1) == 5);
    tabs_check(&t, "6", 0);
    CHECK(ui_tabs_close(&t, 0) == 6);
    CHECK(t.n == 0 && t.cur == 0);
    ui_tabs_step(&t, 1);                 // nothing to step through
    CHECK(t.cur == 0);
    // Full.
    for (int i = 0; i < UI_TABS_MAX; i++) CHECK(ui_tabs_open(&t, i, true));
    CHECK(!ui_tabs_open(&t, 99, true) && t.n == UI_TABS_MAX);
}

static void test_ui_f5(void)
{
    UiNames names = { name_at, NULL };
    static Ui u;
    memset(&u, 0, sizeof u);
    ui_resize(&u.list, 5);
    ui_set(&u.list, 20, 0);
    ui_resize(&u.side, 5);
    ui_set(&u.side, 3, 0);
    UiInput in;
    static const struct { UiKey k; UiActionKind a; isize idx; } K[] = {
        { UK_TAB_NEW, UA_TAB_NEW, 0 }, { UK_TAB_CLOSE, UA_TAB_CLOSE, -1 }, { UK_TAB_NEXT, UA_TAB_STEP, 1 },
        { UK_TAB_PREV, UA_TAB_STEP, -1 }, { UK_TAB_LEFT, UA_TAB_MOVE, -1 }, { UK_TAB_RIGHT, UA_TAB_MOVE, 1 },
    };
    for (int i = 0; i < COUNT(K); i++) {
        in = keys(1, K[i].k);
        UiAction a = ui_handle(&u, names, &in);
        CHECK(a.kind == K[i].a && a.index == K[i].idx);
    }
    in = ui_input_none();
    in.ev[in.nev++] = (UiEvent){ UK_TAB_JUMP, 4 };
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_TAB_JUMP && a.index == 4);
    // Clicks on the bar, and middle clicks.
    in = ui_input_none(), in.tab_click = 2;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_TAB_SHOW && a.index == 2);
    in = ui_input_none(), in.tab_close = 1;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_TAB_CLOSE && a.index == 1);
    u.list.top = 3;
    in = ui_input_none(), in.mid_row = 2;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_TAB_BG_ROW && a.index == 5 && u.list.sel == 0);   // the cursor stays
    in = ui_input_none(), in.mid_row = 30;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    in = ui_input_none(), in.mid_side = 1;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_TAB_BG_PLACE && a.index == 1 && u.side.sel == 0);
    in = ui_input_none(), in.mid_side = 7;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    in = ui_input_none(), in.mid_crumb = 2;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_TAB_BG_CRUMB && a.index == 2);
    // A tab click closes an edit line; nothing on the bar works while a prompt waits.
    ui_edit_open(&u, S("x"));
    in = ui_input_none(), in.tab_click = 0;
    ui_handle(&u, names, &in);
    CHECK(!u.editing);
    u.confirming = true;
    in = ui_input_none(), in.tab_click = 0, in.mid_row = 0;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    in = keys(1, UK_TAB_NEW);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    u.confirming = false;
    // Nor while editing: the keys belong to the line.
    ui_edit_open(&u, S("x"));
    in = keys(2, UK_TAB_NEW, UK_TAB_CLOSE);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.editing);
    ui_edit_close(&u);
}
