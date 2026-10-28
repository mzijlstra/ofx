// F5.2's unit tests: the edit lines' selection, copy, cut and paste, and the path buttons by
// keyboard. Included by test.c.

// The edit line's text is want, the selection [lo, hi) and the caret at caret.
static void edit_check(const Ui *u, const char *want, int lo, int hi, int caret)
{
    int l, h;
    ui_edit_sel(u, &l, &h);
    checks++;
    if (u->edit_len != (int)strlen(want) || memcmp(u->edit, want, (size_t)u->edit_len) != 0 || l != lo || h != hi || u->caret != caret) {
        failures++;
        fprintf(stderr, "edit: got \"%.*s\" sel %d-%d caret %d, want \"%s\" sel %d-%d caret %d\n", u->edit_len, u->edit, l, h, u->caret,
                want, lo, hi, caret);
    }
}

static void edit_keys(Ui *u, UiNames names, int n, ...)
{
    UiInput in = ui_input_none();
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) in.ev[in.nev++] = (UiEvent){ (UiKey)va_arg(ap, int), 0 };
    va_end(ap);
    ui_handle(u, names, &in);
}

static void edit_type(Ui *u, UiNames names, const char *s)
{
    UiInput in = chars(s);
    ui_handle(u, names, &in);
}

static void edit_paste(Ui *u, UiNames names, Str text)
{
    UiInput in = keys(1, UK_PASTE);
    in.paste = text;
    ui_handle(u, names, &in);
}

static void test_edit_select(void)
{
    UiNames names = { name_at, NULL, NULL };
    static Ui u;
    memset(&u, 0, sizeof u);
    ui_resize(&u.list, 5);
    ui_set(&u.list, 20, 0);
    static u8 mark[20];
    memset(mark, 0, sizeof mark), u.mark = mark;
    // A rename opens with the caret at the end, nothing selected.
    ui_edit_open(&u, S("report.txt"));
    edit_check(&u, "report.txt", 10, 10, 10);
    // Shift+Left grows the selection leftward from the caret, the anchor staying.
    edit_keys(&u, names, 4, UK_SEL_LEFT, UK_SEL_LEFT, UK_SEL_LEFT, UK_SEL_LEFT);
    edit_check(&u, "report.txt", 6, 10, 6);
    edit_keys(&u, names, 1, UK_SEL_RIGHT);
    edit_check(&u, "report.txt", 7, 10, 7);
    // Typing replaces the selection.
    edit_type(&u, names, "md");
    edit_check(&u, "report.md", 9, 9, 9);
    // Shift+Home selects to the start; Left from a selection lands at its start, Right at its end.
    edit_keys(&u, names, 1, UK_SEL_HOME);
    edit_check(&u, "report.md", 0, 9, 0);
    edit_keys(&u, names, 1, UK_RIGHT);
    edit_check(&u, "report.md", 9, 9, 9);
    edit_keys(&u, names, 2, UK_HOME, UK_SEL_END);
    edit_check(&u, "report.md", 0, 9, 9);
    edit_keys(&u, names, 1, UK_LEFT);
    edit_check(&u, "report.md", 0, 0, 0);
    // Without a selection Left and Right step a character (UTF-8 whole).
    ui_edit_open(&u, S("caf\xC3\xA9"));
    edit_keys(&u, names, 1, UK_LEFT);
    edit_check(&u, "caf\xC3\xA9", 3, 3, 3);
    edit_keys(&u, names, 1, UK_SEL_RIGHT);
    edit_check(&u, "caf\xC3\xA9", 3, 5, 5);
    edit_keys(&u, names, 1, UK_RIGHT);   // at the selection's end already
    edit_check(&u, "caf\xC3\xA9", 5, 5, 5);
    edit_keys(&u, names, 1, UK_SEL_LEFT);
    edit_check(&u, "caf\xC3\xA9", 3, 5, 3);
    edit_keys(&u, names, 1, UK_RIGHT);   // from a selection: its end, not a step
    edit_check(&u, "caf\xC3\xA9", 5, 5, 5);
    edit_keys(&u, names, 1, UK_SEL_LEFT);
    edit_keys(&u, names, 1, UK_SEL_RIGHT);   // back to nothing selected
    edit_check(&u, "caf\xC3\xA9", 5, 5, 5);
    edit_keys(&u, names, 1, UK_SEL_RIGHT);   // at the end: nothing
    edit_check(&u, "caf\xC3\xA9", 5, 5, 5);
    edit_keys(&u, names, 2, UK_HOME, UK_SEL_LEFT);   // at the start: nothing
    edit_check(&u, "caf\xC3\xA9", 0, 0, 0);
    // Backspace and Delete take the selection only.
    ui_edit_open(&u, S("abcdef"));
    edit_keys(&u, names, 3, UK_LEFT, UK_SEL_LEFT, UK_SEL_LEFT);
    edit_check(&u, "abcdef", 3, 5, 3);
    edit_keys(&u, names, 1, UK_BACKSPACE);
    edit_check(&u, "abcf", 3, 3, 3);
    edit_keys(&u, names, 2, UK_SEL_LEFT, UK_DELETE);
    edit_check(&u, "abf", 2, 2, 2);
    edit_keys(&u, names, 1, UK_BACKSPACE);
    edit_check(&u, "af", 1, 1, 1);
    edit_keys(&u, names, 1, UK_DELETE);
    edit_check(&u, "a", 1, 1, 1);
    // Ctrl+A selects all of it; the location line opens so.
    ui_edit_open(&u, S("name"));
    edit_keys(&u, names, 1, UK_ALL);
    edit_check(&u, "name", 0, 4, 4);
    CHECK(u.marked == 0);   // the edit line's, not the list's
    ui_location_open(&u, S("/home/m"));
    edit_check(&u, "/home/m", 0, 7, 7);
    edit_keys(&u, names, 1, UK_LEFT);
    edit_check(&u, "/home/m", 0, 0, 0);
    // Shift+Enter is Enter in an edit line.
    UiInput in = keys(1, UK_SHIFT_ENTER);
    CHECK(ui_handle(&u, names, &in).kind == UA_GO);
    ui_edit_open(&u, S("n"));
    in = keys(1, UK_SHIFT_ENTER);
    CHECK(ui_handle(&u, names, &in).kind == UA_RENAME);
    // A character that doesn't fit leaves the selection be; one that does replaces it.
    char full[NAME_MAX + 1];
    memset(full, 'x', NAME_MAX), full[NAME_MAX] = 0;
    ui_edit_open(&u, str_from_c(full));
    edit_type(&u, names, "y");
    CHECK(u.edit_len == NAME_MAX && u.edit[NAME_MAX - 1] == 'x');
    edit_keys(&u, names, 1, UK_SEL_LEFT);
    in = ui_input_none(), in.ev[in.nev++] = (UiEvent){ UK_NONE, 0xE9 };   // é: two bytes over one
    ui_handle(&u, names, &in);
    CHECK(u.edit_len == NAME_MAX && u.anchor == NAME_MAX && u.caret == NAME_MAX - 1);
    edit_type(&u, names, "y");
    CHECK(u.edit_len == NAME_MAX && u.edit[NAME_MAX - 1] == 'y' && u.caret == NAME_MAX);
    ui_edit_close(&u);
    // Outside an edit line Shift doesn't change a move: Shift+Left is Left (nothing in the list).
    // Shift+Enter in the files is Open With (Michael, 2026-10-27; test_f74.c).
    in = keys(1, UK_SEL_LEFT);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    in = keys(1, UK_SHIFT_ENTER);
    CHECK(ui_handle(&u, names, &in).kind == UA_OPEN_WITH && u.ow);
    ui_ow_close(&u);
    in = keys(1, UK_SEL_END);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 19);
    in = keys(1, UK_SEL_HOME);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 0);
    in = keys(1, UK_SEL_RIGHT);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    // The delete question takes Shift+Enter as Enter.
    u.confirming = true;
    in = keys(1, UK_SHIFT_ENTER);
    CHECK(ui_handle(&u, names, &in).kind == UA_DELETE && !u.confirming);
    // A prompt holds the keys even with the edit line open under it.
    ui_edit_open(&u, S("ab"));
    u.asking = true;
    in = keys(1, UK_BACKSPACE);
    ui_handle(&u, names, &in);
    edit_check(&u, "ab", 2, 2, 2);
    in = keys(1, UK_ESC);
    CHECK(ui_handle(&u, names, &in).kind == UA_ANSWER && !u.asking);
    ui_edit_close(&u);
}

static void test_edit_clipboard(void)
{
    UiNames names = { name_at, NULL, NULL };
    static Ui u;
    memset(&u, 0, sizeof u);
    ui_resize(&u.list, 5);
    ui_set(&u.list, 20, 0);
    static u8 mark[20];
    memset(mark, 0, sizeof mark), u.mark = mark;
    // Ctrl+C copies the selection as text; nothing selected: nothing.
    ui_edit_open(&u, S("holiday photos"));
    edit_keys(&u, names, 1, UK_COPY);
    CHECK(!u.copied);
    edit_keys(&u, names, 6, UK_SEL_LEFT, UK_SEL_LEFT, UK_SEL_LEFT, UK_SEL_LEFT, UK_SEL_LEFT, UK_SEL_LEFT);
    edit_keys(&u, names, 1, UK_COPY);
    CHECK(u.copied && u.copy_len == 6 && memcmp(u.copy, "photos", 6) == 0);
    edit_check(&u, "holiday photos", 8, 14, 8);   // the line as it was
    CHECK(u.marked == 0);   // not the list's copy
    u.copied = false;
    // Ctrl+X copies it and takes it out; with nothing selected it does neither.
    edit_keys(&u, names, 1, UK_CUT);
    CHECK(u.copied && u.copy_len == 6 && memcmp(u.copy, "photos", 6) == 0);
    edit_check(&u, "holiday ", 8, 8, 8);
    u.copied = false;
    edit_keys(&u, names, 1, UK_CUT);
    CHECK(!u.copied);
    edit_check(&u, "holiday ", 8, 8, 8);
    // Ctrl+V puts the text at the caret, over a selection.
    edit_paste(&u, names, S("snaps"));
    edit_check(&u, "holiday snaps", 13, 13, 13);
    edit_keys(&u, names, 1, UK_ALL);
    edit_paste(&u, names, S("trip"));
    edit_check(&u, "trip", 4, 4, 4);
    edit_keys(&u, names, 1, UK_HOME);
    edit_paste(&u, names, S("a "));
    edit_check(&u, "a trip", 2, 2, 2);
    // Only the first line; '/' and controls left out of a name, the rest kept.
    edit_keys(&u, names, 1, UK_ALL);
    edit_paste(&u, names, S("x/y\tz\x7F" "\xC3\xA9\nsecond line"));
    edit_check(&u, "xyz\xC3\xA9", 5, 5, 5);
    edit_keys(&u, names, 1, UK_ALL);
    edit_paste(&u, names, S("one\r\ntwo"));
    edit_check(&u, "one", 3, 3, 3);
    edit_keys(&u, names, 1, UK_ALL);
    edit_paste(&u, names, S("one\rtwo"));   // a lone CR ends the line too
    edit_check(&u, "one", 3, 3, 3);
    // Nothing left to paste (empty, a line break first, only '/'): the selection stays.
    edit_keys(&u, names, 1, UK_ALL);
    edit_paste(&u, names, STR_EMPTY);
    edit_paste(&u, names, S("\nlater"));
    edit_paste(&u, names, S("//"));
    edit_check(&u, "one", 0, 3, 3);
    // The location line keeps '/'; still only the first line, and no controls.
    ui_location_open(&u, S("/old"));
    edit_paste(&u, names, S("/home/m/code\tx\n/etc"));
    edit_check(&u, "/home/m/codex", 13, 13, 13);
    // What doesn't fit is left off, whole characters only.
    char full[NAME_MAX + 1];
    memset(full, 'x', NAME_MAX - 3), full[NAME_MAX - 3] = 0;
    ui_edit_open(&u, str_from_c(full));
    edit_paste(&u, names, S("ab\xC3\xA9" "cd"));   // room for 3 bytes: "ab", then é doesn't fit
    CHECK(u.edit_len == NAME_MAX - 1 && memcmp(u.edit + NAME_MAX - 3, "ab", 2) == 0 && u.caret == NAME_MAX - 1);
    edit_paste(&u, names, S("/c"));   // a dropped character doesn't take room
    CHECK(u.edit_len == NAME_MAX && u.edit[NAME_MAX - 1] == 'c');
    edit_keys(&u, names, 2, UK_SEL_LEFT, UK_SEL_LEFT);
    edit_paste(&u, names, S("\xC3\xA9z"));   // two selected bytes make room for é
    CHECK(u.edit_len == NAME_MAX && memcmp(u.edit + NAME_MAX - 2, "\xC3\xA9", 2) == 0);
    ui_edit_close(&u);
    // Outside an edit line Ctrl+C, X and V are still the files'.
    UiInput in = keys(1, UK_COPY);
    CHECK(ui_handle(&u, names, &in).kind == UA_COPY && !u.copied);
    in = keys(1, UK_CUT);
    CHECK(ui_handle(&u, names, &in).kind == UA_CUT);
    in = keys(1, UK_PASTE);
    CHECK(ui_handle(&u, names, &in).kind == UA_PASTE);
    in = keys(1, UK_ALL);
    ui_handle(&u, names, &in);
    CHECK(u.marked == 20);
}

static void test_crumb_keys(void)
{
    UiNames names = { name_at, NULL, NULL };
    static Ui u;
    memset(&u, 0, sizeof u);
    ui_resize(&u.list, 5);
    ui_set(&u.list, 20, 3);
    ui_resize(&u.side, 5);
    ui_set(&u.side, 3, 0);
    UiInput in = keys(1, UK_CRUMBS);
    in.crumb_n = 0;
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_CRUMBS);   // no buttons: nothing
    // Ctrl+K: the rightmost shown marked.
    in = keys(1, UK_CRUMBS), in.crumb_n = 5, in.crumb_last = 3;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && (u.focus == UI_FOCUS_CRUMBS) && u.crumb_at == 3);
    // Left, Right, Home, End (with Shift as without), the ends kept.
    static const struct { UiKey k; int at; } MOVES[] = {
        { UK_LEFT, 2 }, { UK_LEFT, 1 }, { UK_LEFT, 0 }, { UK_LEFT, 0 }, { UK_RIGHT, 1 }, { UK_END, 4 }, { UK_RIGHT, 4 },
        { UK_HOME, 0 }, { UK_SEL_RIGHT, 1 }, { UK_SEL_LEFT, 0 }, { UK_SEL_END, 4 }, { UK_SEL_HOME, 0 }, { UK_CRUMBS, 0 },
    };
    for (int i = 0; i < COUNT(MOVES); i++) {
        in = keys(1, MOVES[i].k), in.crumb_n = 5, in.crumb_last = 3;
        CHECK(ui_handle(&u, names, &in).kind == UA_NONE && (u.focus == UI_FOCUS_CRUMBS));
        CHECK(u.crumb_at == MOVES[i].at);
    }
    // h and l as Left and Right.
    in = chars("l"), in.crumb_n = 5, in.crumb_last = 3;
    ui_handle(&u, names, &in);
    CHECK((u.focus == UI_FOCUS_CRUMBS) && u.crumb_at == 1);
    in = chars("lhh"), in.crumb_n = 5, in.crumb_last = 3;
    ui_handle(&u, names, &in);
    CHECK((u.focus == UI_FOCUS_CRUMBS) && u.crumb_at == 0);
    in = chars("h"), in.crumb_n = 5, in.crumb_last = 3;   // at the start: stays
    ui_handle(&u, names, &in);
    CHECK((u.focus == UI_FOCUS_CRUMBS) && u.crumb_at == 0);
    CHECK(u.list.sel == 3);   // the list's cursor didn't move
    // Shift+Enter: a background tab, the keyboard staying; Enter goes and gives it back.
    in = keys(1, UK_SEL_RIGHT), in.crumb_n = 5, in.crumb_last = 3;
    ui_handle(&u, names, &in);
    in = keys(1, UK_SHIFT_ENTER), in.crumb_n = 5, in.crumb_last = 3;
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_TAB_BG_CRUMB && a.index == 1 && (u.focus == UI_FOCUS_CRUMBS));
    in = keys(1, UK_ENTER), in.crumb_n = 5, in.crumb_last = 3;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_CRUMB && a.index == 1 && u.focus != UI_FOCUS_CRUMBS);
    // Esc and Tab give it back and do nothing else (the selection kept, the sidebar not taken).
    static u8 mark[20];
    u.mark = mark, ui_mark(&u, 2, true);
    in = keys(2, UK_CRUMBS, UK_ESC), in.crumb_n = 5, in.crumb_last = 3;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.focus != UI_FOCUS_CRUMBS && u.marked == 1);
    ui_mark_clear(&u), u.mark = NULL;
    in = keys(2, UK_CRUMBS, UK_TAB), in.crumb_n = 5, in.crumb_last = 3;
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_CRUMBS && u.focus != UI_FOCUS_SIDE);
    // Any other key gives it back and is the list's.
    in = keys(2, UK_CRUMBS, UK_DOWN), in.crumb_n = 5, in.crumb_last = 3;
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_CRUMBS && u.list.sel == 4);
    in = keys(1, UK_CRUMBS), in.crumb_n = 5, in.crumb_last = 3;
    ui_handle(&u, names, &in);
    in = chars("j"), in.crumb_n = 5, in.crumb_last = 3;
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_CRUMBS && u.list.sel == 5);
    in = keys(2, UK_CRUMBS, UK_BACKSPACE), in.crumb_n = 5, in.crumb_last = 3;
    CHECK(ui_handle(&u, names, &in).kind == UA_PARENT && u.focus != UI_FOCUS_CRUMBS);
    // A click anywhere gives it back: a row, the bar, a tab, a middle click.
    static const int CLICKS = 10;
    for (int c = 0; c < CLICKS; c++) {
        in = keys(1, UK_CRUMBS), in.crumb_n = 5, in.crumb_last = 3;
        ui_handle(&u, names, &in);
        CHECK((u.focus == UI_FOCUS_CRUMBS));
        in = ui_input_none(), in.crumb_n = 5, in.crumb_last = 3;
        switch (c) {
        case 0: in.click = 1; break;
        case 1: in.dclick = 1; break;
        case 2: in.side_click = 1; break;
        case 3: in.header = 1; break;
        case 4: in.bar = UB_LEFT; break;
        case 5: in.tab_click = 0; break;
        case 6: in.tab_close = 0; break;
        case 7: in.mid_row = 0; break;
        case 8: in.mid_side = 0; break;
        case 9: in.mid_crumb = 0; break;
        }
        ui_handle(&u, names, &in);
        CHECK(u.focus != UI_FOCUS_CRUMBS);
    }
    // Without a click it stays; the buttons gone (another folder, fewer of them) move the mark.
    in = keys(1, UK_CRUMBS), in.crumb_n = 5, in.crumb_last = 4;
    ui_handle(&u, names, &in);
    in = ui_input_none(), in.crumb_n = 5;
    ui_handle(&u, names, &in);
    CHECK((u.focus == UI_FOCUS_CRUMBS) && u.crumb_at == 4);
    in = ui_input_none(), in.crumb_n = 2;
    ui_handle(&u, names, &in);
    CHECK((u.focus == UI_FOCUS_CRUMBS) && u.crumb_at == 1);
    in = ui_input_none(), in.crumb_n = 0;
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_CRUMBS);
    // The rightmost shown out of range: the last there is (or the first).
    in = keys(1, UK_CRUMBS), in.crumb_n = 3, in.crumb_last = 7;
    ui_handle(&u, names, &in);
    CHECK(u.crumb_at == 2);
    in = keys(2, UK_ESC, UK_CRUMBS), in.crumb_n = 3, in.crumb_last = -1;
    ui_handle(&u, names, &in);
    CHECK((u.focus == UI_FOCUS_CRUMBS) && u.crumb_at == 0);
    // From the location line: it closes, the buttons take the keyboard; a rename keeps Ctrl+K out.
    ui_location_open(&u, S("/a/b"));
    in = keys(1, UK_CRUMBS), in.crumb_n = 3, in.crumb_last = 2;
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_EDIT && (u.focus == UI_FOCUS_CRUMBS) && u.crumb_at == 2);
    in = keys(1, UK_ESC), in.crumb_n = 3;
    ui_handle(&u, names, &in);
    ui_edit_open(&u, S("name"));
    in = keys(1, UK_CRUMBS), in.crumb_n = 3, in.crumb_last = 2;
    ui_handle(&u, names, &in);
    CHECK((u.focus == UI_FOCUS_EDIT) && u.focus != UI_FOCUS_CRUMBS);
    ui_edit_close(&u);
    // Opening an edit line or the find line takes the keyboard from them.
    in = keys(2, UK_CRUMBS, UK_F2), in.crumb_n = 3, in.crumb_last = 2;
    ui_handle(&u, names, &in);
    CHECK((u.focus == UI_FOCUS_EDIT) && u.focus != UI_FOCUS_CRUMBS);
    ui_edit_close(&u);
    in = keys(1, UK_CRUMBS), in.crumb_n = 3, in.crumb_last = 2;
    ui_handle(&u, names, &in);
    ui_location_open(&u, S("/x"));
    CHECK(u.focus != UI_FOCUS_CRUMBS);
    ui_edit_close(&u);
    in = keys(2, UK_CRUMBS, UK_FIND), in.crumb_n = 3, in.crumb_last = 2;
    ui_handle(&u, names, &in);
    CHECK(u.finding && u.focus != UI_FOCUS_CRUMBS);
    in = keys(1, UK_ESC);
    ui_handle(&u, names, &in);
    // From the find bar Ctrl+K takes the keys to the path buttons; the bar stays.
    in = keys(2, UK_FIND, UK_CRUMBS), in.crumb_n = 3, in.crumb_last = 2;
    ui_handle(&u, names, &in);
    CHECK(u.finding && u.focus != UI_FOCUS_FIND && (u.focus == UI_FOCUS_CRUMBS));
    in = keys(1, UK_ESC), in.crumb_n = 3;
    ui_handle(&u, names, &in);
    // From the sidebar's keyboard: the buttons take it.
    u.focus = UI_FOCUS_SIDE;
    in = keys(1, UK_CRUMBS), in.crumb_n = 3, in.crumb_last = 2;
    ui_handle(&u, names, &in);
    CHECK((u.focus == UI_FOCUS_CRUMBS) && u.focus != UI_FOCUS_SIDE);
    in = keys(1, UK_ESC), in.crumb_n = 3;
    ui_handle(&u, names, &in);
    // A prompt keeps the keys: Ctrl+K does nothing while the delete question is up, the location
    // line open under it or not.
    ui_location_open(&u, S("/a"));
    u.confirming = true;
    in = keys(1, UK_CRUMBS), in.crumb_n = 3, in.crumb_last = 2;
    ui_handle(&u, names, &in);
    CHECK((u.focus == UI_FOCUS_EDIT) && u.focus != UI_FOCUS_CRUMBS && u.confirming);
    ui_edit_close(&u);
    u.confirming = true;
    in = keys(1, UK_CRUMBS), in.crumb_n = 3, in.crumb_last = 2;
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_CRUMBS && u.confirming);
    u.confirming = false;
    in = keys(1, UK_CRUMBS), in.crumb_n = 3, in.crumb_last = 2;
    ui_handle(&u, names, &in);
    u.asking = true;
    in = keys(1, UK_LEFT), in.crumb_n = 3;
    ui_handle(&u, names, &in);
    CHECK(u.crumb_at == 2 && u.asking);
    u.asking = false;
}

// ui_crumbs_show: the window moves only as far as it must to show the marked button.
static void test_crumbs_show(void)
{
    static UiCrumbs c;
    memset(&c, 0, sizeof c);
    ui_crumbs_set(&c, "/a/b/c/d/e/f/g", NULL);   // 8 buttons: / a b c d e f g, cur 7
    float w[UI_CRUMBS_MAX];
    for (int i = 0; i < c.n; i++) w[i] = 10;
    int lo, hi;
    // All fit: nothing moves.
    ui_crumbs_show(&c, w, 100, 5, 0);
    CHECK(c.lo == -1);
    // 3 fit beside the arrows (room 40 - 2 * 5): the window shows 5..8 for cur 7.
    ui_crumbs_window(&c, w, 40, 5, &lo, &hi);
    CHECK(lo == 5 && hi == 8);
    ui_crumbs_show(&c, w, 40, 5, 6);   // shown already
    CHECK(c.lo == -1);
    ui_crumbs_show(&c, w, 40, 5, 4);   // left of it: it becomes the first
    ui_crumbs_window(&c, w, 40, 5, &lo, &hi);
    CHECK(c.lo == 4 && lo == 4 && hi == 7);
    ui_crumbs_show(&c, w, 40, 5, 0);
    CHECK(c.lo == 0);
    ui_crumbs_show(&c, w, 40, 5, 3);   // right of it: the window moves until it is the last
    ui_crumbs_window(&c, w, 40, 5, &lo, &hi);
    CHECK(c.lo == 1 && lo == 1 && hi == 4);
    ui_crumbs_show(&c, w, 40, 5, 7);
    ui_crumbs_window(&c, w, 40, 5, &lo, &hi);
    CHECK(c.lo == 5 && hi == 8);
    // A button wider than the room: shown alone.
    w[2] = 100;
    c.lo = 0;
    ui_crumbs_show(&c, w, 40, 5, 2);
    ui_crumbs_window(&c, w, 40, 5, &lo, &hi);
    CHECK(c.lo == 2 && lo == 2 && hi == 3);
}

static void test_ui_f52(void)
{
    test_edit_select();
    test_edit_clipboard();
    test_crumb_keys();
    test_crumbs_show();
}
