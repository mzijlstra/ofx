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
    UiNames names = { name_at, NULL, NULL };
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
    in.ev[in.nev++] = ui_event_of((UiChord){ '4', UI_MOD_ALT });
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
    CHECK(u.focus != UI_FOCUS_EDIT);
    u.confirming = true;
    in = ui_input_none(), in.tab_click = 0, in.mid_row = 0;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    in = ui_input_none(), in.tab_close = 0, in.mid_side = 0, in.mid_crumb = 0;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    in = keys(1, UK_TAB_NEW);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    u.confirming = false;
    // Nor while editing: the keys belong to the line.
    ui_edit_open(&u, S("x"));
    in = keys(2, UK_TAB_NEW, UK_TAB_CLOSE);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && (u.focus == UI_FOCUS_EDIT));
    ui_edit_close(&u);
}

// F5.1: the modal panels' buttons.
static void test_ui_modal(void)
{
    UiNames names = { name_at, NULL, NULL };
    static Ui u;
    memset(&u, 0, sizeof u);
    ui_resize(&u.list, 5);
    ui_set(&u.list, 20, 0);
    UiInput in;
    // The delete panel: Delete deletes, Cancel doesn't; neither does anything without the question.
    in = ui_input_none(), in.modal = UI_MODAL_YES;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    u.confirming = true;
    in = ui_input_none(), in.modal = UI_MODAL_NO;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.confirming);
    u.confirming = true;
    in = ui_input_none(), in.modal = UI_MODAL_YES;
    CHECK(ui_handle(&u, names, &in).kind == UA_DELETE && !u.confirming);
    // The clash panel: each answer; the box toggles "the same for the rest", which rides along on
    // an answer (not on Cancel), and on a lower-case key too.
    static const struct { int b; int a; } B[] = {
        { UI_MODAL_SKIP, UI_ANS_SKIP }, { UI_MODAL_REPLACE, UI_ANS_REPLACE }, { UI_MODAL_KEEP, UI_ANS_KEEP }, { UI_MODAL_CANCEL, UI_ANS_CANCEL },
    };
    for (int i = 0; i < COUNT(B); i++) {
        u.asking = true, u.ask_all = false;
        in = ui_input_none(), in.modal = B[i].b;
        UiAction a = ui_handle(&u, names, &in);
        CHECK(a.kind == UA_ANSWER && a.index == B[i].a && !a.all && !u.asking);
    }
    u.asking = true, u.ask_all = false;
    in = ui_input_none(), in.modal = UI_MODAL_ALL;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.ask_all && u.asking);
    in = ui_input_none(), in.modal = UI_MODAL_KEEP;
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_ANSWER && a.index == UI_ANS_KEEP && a.all);
    u.asking = true, u.ask_all = true;
    in = ui_input_none(), in.modal = UI_MODAL_CANCEL;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_ANSWER && a.index == UI_ANS_CANCEL && !a.all);
    u.asking = true, u.ask_all = true;
    in = chars("s");
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_ANSWER && a.index == UI_ANS_SKIP && a.all);
    u.asking = true, u.ask_all = false;
    in = ui_input_none(), in.modal = UI_MODAL_ALL;
    ui_handle(&u, names, &in);
    in = ui_input_none(), in.modal = UI_MODAL_ALL;
    ui_handle(&u, names, &in);
    CHECK(!u.ask_all);
    u.asking = false;
    // The box does nothing without a clash.
    in = ui_input_none(), in.modal = UI_MODAL_ALL;
    ui_handle(&u, names, &in);
    CHECK(!u.ask_all);
}
