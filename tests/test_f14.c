// F14 (drag and drop with other programs): the decisions gui/dnd.c and gui/input.c take from ui/
// (code-health review, 2026-11-06: sway's order of events never shows some of them).

// What a drop asks of the source's actions: Nemo's rule's move or copy, a move the source doesn't
// allow as a copy, the Trash only as a move.
static void test_dnd_action(void)
{
    enum { C = UI_DND_COPY, M = UI_DND_MOVE };
    CHECK(ui_dnd_action(UI_DROP_MOVE, C | M) == M && ui_dnd_action(UI_DROP_COPY, C | M) == C);
    CHECK(ui_dnd_action(UI_DROP_MOVE, C) == C && ui_dnd_action(UI_DROP_COPY, M) == 0);
    CHECK(ui_dnd_action(UI_DROP_TRASH, C | M) == M && ui_dnd_action(UI_DROP_TRASH, C) == 0);
    CHECK(ui_dnd_action(UI_DROP_MOVE, 0) == 0);
}

// A move only when the window and the compositor both say so; either alone when the other has none.
static void test_dnd_merge(void)
{
    enum { C = UI_DND_COPY, M = UI_DND_MOVE };
    CHECK(ui_dnd_merge(M, M) == M && ui_dnd_merge(C, C) == C);
    CHECK(ui_dnd_merge(C, M) == C && ui_dnd_merge(M, C) == C);   // (Hyprland's stale move after Ctrl's copy)
    CHECK(ui_dnd_merge(0, M) == M && ui_dnd_merge(M, 0) == M && ui_dnd_merge(0, 0) == 0);
}

// What the drop then does: nothing without an action, the Trash's only with a move.
static void test_dnd_drop(void)
{
    UiDropOp op = UI_DROP_COPY;
    CHECK(!ui_dnd_drop(false, 0, &op) && !ui_dnd_drop(true, UI_DND_COPY, &op) && !ui_dnd_drop(true, 0, &op));
    CHECK(ui_dnd_drop(true, UI_DND_MOVE, &op) && op == UI_DROP_TRASH);
    CHECK(ui_dnd_drop(false, UI_DND_MOVE, &op) && op == UI_DROP_MOVE);
    CHECK(ui_dnd_drop(false, UI_DND_COPY, &op) && op == UI_DROP_COPY);
}

// The search fields as the view changes (ui_view_set, from gui/folder.c, code-health review,
// 2026-11-06): out of the search view they lose the keys; a new search shows the address's text,
// the first field with the keys when both are empty, "Containing" on when there is inside text and
// not the last to have the keys; the search shown again keeps its fields.
static void test_view_set(void)
{
    Ui u = menu_ui(3);
    ui_view_set(&u, UI_VIEW_SEARCH, false, STR_EMPTY, STR_EMPTY);
    CHECK(u.view == UI_VIEW_SEARCH && u.focus == UI_FOCUS_SEARCH && u.search.len == 0 && !u.search_inside);
    u.search_edited = true;
    ui_view_set(&u, UI_VIEW_FOLDER, false, STR_EMPTY, STR_EMPTY);
    CHECK(u.view == UI_VIEW_FOLDER && u.focus != UI_FOCUS_SEARCH && u.focus != UI_FOCUS_INSIDE);
    u.search_was_inside = true, u.search_edited = true;
    ui_view_set(&u, UI_VIEW_SEARCH, false, S("abc"), S("needle"));
    CHECK(u.search.len == 3 && memcmp(u.search.b, "abc", 3) == 0 && u.inside.len == 6 && u.search_inside);
    CHECK(!u.search_was_inside && !u.search_edited && u.focus != UI_FOCUS_SEARCH && u.focus != UI_FOCUS_INSIDE);
    u.focus = UI_FOCUS_INSIDE, u.search_edited = true, u.search.len = 1;
    ui_view_set(&u, UI_VIEW_SEARCH, true, S("other"), STR_EMPTY);   // (the same one read again)
    CHECK(u.focus == UI_FOCUS_INSIDE && u.search_edited && u.search.len == 1 && u.search_inside);
    ui_view_set(&u, UI_VIEW_TRASH, false, STR_EMPTY, STR_EMPTY);
    CHECK(u.view == UI_VIEW_TRASH && u.focus != UI_FOCUS_INSIDE);
}

// The clipboard's and the drags' pipes (fs/clip.c, code-health review, 2026-11-06): a read to the
// end, one cut short by its room or its time, a write; the MIME types' formats.
static void test_fd_timed(void)
{
    int p[2];
    char buf[8];
    bool whole;
    CHECK(pipe(p) == 0);
    CHECK(fd_write_timed(p[1], S("abc"), 1000) == 3);
    close(p[1]);
    CHECK(fd_read_timed(p[0], buf, sizeof buf, 1000, &whole) == 3 && whole && strcmp(buf, "abc") == 0);
    close(p[0]);
    CHECK(pipe(p) == 0);
    CHECK(write(p[1], "abcdef", 6) == 6);
    CHECK(fd_read_timed(p[0], buf, 4, 1000, &whole) == 3 && !whole && strcmp(buf, "abc") == 0);   // (its room: 3 and the NUL)
    i64 t0 = now_ms();
    CHECK(fd_read_timed(p[0], buf, sizeof buf, 60, &whole) == 3 && !whole && strcmp(buf, "def") == 0);   // (the writer still there: its time)
    CHECK(now_ms() - t0 >= 60);
    close(p[0]), close(p[1]);
    CHECK(clip_fmt_of("text/uri-list") == CLIP_FMT_URIS && clip_fmt_of("x-special/gnome-copied-files") == CLIP_FMT_GNOME);
    CHECK(clip_fmt_of("text/plain") == CLIP_FMT_TEXT && clip_fmt_of("text/plain;charset=utf-8") == CLIP_FMT_TEXT);
    CHECK(clip_fmt_of("application/x-ofx-drag-1") == -1 && clip_fmt_of("text/html") == -1);
}

// The pointer's tidying runs whatever the press does after it (code-health review, 2026-11-06): a
// press on a menu title, which returns at once, still takes the keys from job rows that are gone.
static void test_pointer_tidy(void)
{
    static u8 mark[3];
    UiNames names = { name_at, NULL, dir_at };
    Ui u = menu_ui(3);
    u.mark = mark, u.focus = UI_FOCUS_JOBS, u.job_at = 4;
    UiInput in = ui_input_none();
    in.menu_title = 0, in.job_n = 0;
    ui_handle(&u, names, &in);
    CHECK(u.menu_on && u.focus != UI_FOCUS_JOBS && u.job_at == 0);
}

// Every kind of thing on top has an owner for the keys (code-health review, 2026-11-06): the key
// loop gives a clash its keys once the questions and panels have taken theirs, so a new panel
// without a case in ui_panel_key would hand the fields its keys instead.
static void test_on_top_owner(void)
{
    UiInput in = ui_input_none();
    for (int k = UI_ON_NONE; k < UI_ON_KINDS; k++) {
        Ui u = menu_ui(3);
        UiAction out;
        UiOnTop on = (UiOnTop)k;
        bool owned = on == UI_ON_NONE || on == UI_ON_FULL || on == UI_ON_CLASH || ui_is_question(on) ||
                     ui_panel_key(&u, on, (UiEvent){ UK_NONE, 0 }, &in, &out);
        checks++;
        if (!owned) failures++, fprintf(stderr, "UiOnTop %d: no owner for its keys\n", k);
    }
}
