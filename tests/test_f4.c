// F4's unit tests: the path buttons, the location line, folder item counts. Included by test.c.

static void crumbs_check(const UiCrumbs *c, const char *labels, int cur)
{
    char got[1024] = "";
    for (int i = 0; i < c->n; i++) {
        size_t k = strlen(got);
        snprintf(got + k, sizeof got - k, "%s%.*s", i ? "|" : "", STR_ARG(ui_crumb_label(c, i)));
    }
    checks++;
    if (strcmp(got, labels) != 0 || c->cur != cur) {
        failures++;
        fprintf(stderr, "crumbs: got \"%s\" cur %d, want \"%s\" cur %d\n", got, c->cur, labels, cur);
    }
}

static void test_crumbs(void)
{
    static UiCrumbs c;
    memset(&c, 0, sizeof c);
    // Under home: home first, the folders above it left out.
    ui_crumbs_set(&c, "/home/m/code/proj", "/home/m");
    crumbs_check(&c, "m|code|proj", 2);
    CHECK(c.seg[0].home && !c.seg[1].home);
    CHECK_STR(ui_crumb_path(&c, 1), "/home/m/code");
    CHECK_STR(ui_crumb_path(&c, 0), "/home/m");
    // Up keeps the deeper buttons; the current one moves.
    ui_crumbs_set(&c, "/home/m/code", "/home/m");
    crumbs_check(&c, "m|code|proj", 1);
    ui_crumbs_set(&c, "/home/m", "/home/m");
    crumbs_check(&c, "m|code|proj", 0);
    // Down the same path again: still the same bar.
    ui_crumbs_set(&c, "/home/m/code/proj", "/home/m");
    crumbs_check(&c, "m|code|proj", 2);
    // Off the path: anew.
    ui_crumbs_set(&c, "/home/m/code", "/home/m");
    ui_crumbs_set(&c, "/home/m/code/rust", "/home/m");
    crumbs_check(&c, "m|code|rust", 2);
    // A sibling whose name starts the same is off the path (code2 isn't under code).
    ui_crumbs_set(&c, "/home/m/code2", "/home/m");
    crumbs_check(&c, "m|code2", 1);
    // Above home: from /, home shown as an ordinary folder.
    ui_crumbs_set(&c, "/home", "/home/m");
    crumbs_check(&c, "/|home|m|code2", 1);
    CHECK(!c.seg[2].home);
    ui_crumbs_set(&c, "/", "/home/m");
    crumbs_check(&c, "/|home|m|code2", 0);
    CHECK_STR(ui_crumb_path(&c, 0), "/");
    // Elsewhere: from /.
    ui_crumbs_set(&c, "/mnt/sd", "/home/m");
    crumbs_check(&c, "/|mnt|sd", 2);
    CHECK_STR(ui_crumb_path(&c, 2), "/mnt/sd");
    // No home, a home with a trailing slash, a home that is a prefix of another name.
    ui_crumbs_set(&c, "/home/m/x", NULL);
    crumbs_check(&c, "/|home|m|x", 3);
    ui_crumbs_set(&c, "/home/m/x", "/home/m/");
    crumbs_check(&c, "m|x", 1);
    memset(&c, 0, sizeof c);
    ui_crumbs_set(&c, "/home/mm/x", "/home/m");
    crumbs_check(&c, "/|home|mm|x", 3);
    // Deeper than UI_CRUMBS_MAX: the deepest button shown is marked, not the root.
    char deep[4096] = "";
    for (int i = 0; i < UI_CRUMBS_MAX + 10; i++) strcat(deep, "/d");
    memset(&c, 0, sizeof c);
    ui_crumbs_set(&c, deep, NULL);
    CHECK(c.n == UI_CRUMBS_MAX && c.cur == UI_CRUMBS_MAX - 1);
    // The window: all fit, no arrows.
    float w[6] = { 10, 10, 10, 10, 10, 10 };
    memset(&c, 0, sizeof c);
    ui_crumbs_set(&c, "/a/b/c/d/e", NULL);
    crumbs_check(&c, "/|a|b|c|d|e", 5);
    int lo, hi;
    CHECK(!ui_crumbs_window(&c, w, 60, 5, &lo, &hi) && lo == 0 && hi == 6);
    // Too narrow: arrows, as many above the current one as fit, the current one shown.
    CHECK(ui_crumbs_window(&c, w, 40, 5, &lo, &hi) && lo == 3 && hi == 6);   // room 30: three
    ui_crumbs_set(&c, "/a/b", NULL);
    crumbs_check(&c, "/|a|b|c|d|e", 2);
    CHECK(ui_crumbs_window(&c, w, 40, 5, &lo, &hi) && lo == 0 && hi == 3);
    ui_crumbs_set(&c, "/a/b/c", NULL);
    CHECK(ui_crumbs_window(&c, w, 40, 5, &lo, &hi) && lo == 1 && hi == 4);
    // The arrows move it a button, within the ends; a folder change goes back to automatic.
    ui_crumbs_shift(&c, lo, 1);
    CHECK(ui_crumbs_window(&c, w, 40, 5, &lo, &hi) && lo == 2 && hi == 5);
    ui_crumbs_shift(&c, lo, 1), ui_crumbs_window(&c, w, 40, 5, &lo, &hi);
    ui_crumbs_shift(&c, lo, 1), ui_crumbs_window(&c, w, 40, 5, &lo, &hi);
    ui_crumbs_shift(&c, lo, 1), ui_crumbs_window(&c, w, 40, 5, &lo, &hi);
    CHECK(lo == 5 && hi == 6);
    ui_crumbs_shift(&c, -3, -1);
    CHECK(ui_crumbs_window(&c, w, 40, 5, &lo, &hi) && lo == 0 && hi == 3);
    ui_crumbs_set(&c, "/a/b/c", NULL);
    CHECK(ui_crumbs_window(&c, w, 40, 5, &lo, &hi) && lo == 1);
    // One button wider than the room still shows (cut by the layout).
    float wide[6] = { 10, 10, 10, 100, 10, 10 };
    ui_crumbs_set(&c, "/a/b/c", NULL);
    CHECK(ui_crumbs_window(&c, wide, 40, 5, &lo, &hi) && lo == 3 && hi == 4);
}

static void test_location(void)
{
    arena_reset(&ta);
    Str out;
    CHECK(ui_location(&ta, S("/mnt/sd"), "/home/m", "/home/m", &out));
    CHECK_STR(out, "/mnt/sd");
    CHECK(ui_location(&ta, S("  code/proj  "), "/home/m", "/home/m", &out));
    CHECK_STR(out, "/home/m/code/proj");
    CHECK(ui_location(&ta, S("~"), "/tmp", "/home/m", &out));
    CHECK_STR(out, "/home/m");
    CHECK(ui_location(&ta, S("~/code"), "/tmp", "/home/m", &out));
    CHECK_STR(out, "/home/m/code");
    CHECK(ui_location(&ta, S("~code"), "/tmp", "/home/m", &out));   // a folder called ~code, here
    CHECK_STR(out, "/tmp/~code");
    CHECK(ui_location(&ta, S(".."), "/home/m/code", "/home/m", &out));
    CHECK_STR(out, "/home/m");
    CHECK(ui_location(&ta, S("../.."), "/home/m", "/home/m", &out));
    CHECK_STR(out, "/");
    CHECK(ui_location(&ta, S("a/./b//c/../d/"), "/x", NULL, &out));
    CHECK_STR(out, "/x/a/b/d");
    CHECK(!ui_location(&ta, S("../../.."), "/home", "/home/m", &out));
    CHECK(!ui_location(&ta, S("   "), "/home", "/home/m", &out));
    CHECK(!ui_location(&ta, S("~/x"), "/home", NULL, &out));
    CHECK(ui_location(&ta, S("/"), "/home", NULL, &out));
    CHECK_STR(out, "/");
}

static void test_counts(void)
{
    mkdirs(at("cnt/empty"));
    put(at("cnt/some/a"), "");
    put(at("cnt/some/b"), "");
    put(at("cnt/some/.h"), "");
    mkdirs(at("cnt/some/sub"));
    CHECK(count_entries(at("cnt/some"), false) == 3);
    CHECK(count_entries(at("cnt/some"), true) == 4);
    CHECK(count_entries(at("cnt/empty"), true) == 0);
    CHECK(count_entries(at("cnt/none"), true) == -1);
    CHECK(count_entries(at("cnt/some/a"), true) == -1);   // a file
    mkdirs(at("cnt/locked"));
    chmod(at("cnt/locked"), 0300);
    if (geteuid() != 0) CHECK(count_entries(at("cnt/locked"), true) == -1);
    chmod(at("cnt/locked"), 0755);
}

static void test_ui_f4(void)
{
    UiNames names = { name_at, NULL };
    static Ui u;
    memset(&u, 0, sizeof u);
    ui_resize(&u.list, 5);
    ui_set(&u.list, 20, 0);
    UiInput in;
    // The bar's buttons.
    static const struct { UiBar b; UiActionKind k; isize idx; } B[] = {
        { UB_BACK, UA_BACK, 0 }, { UB_FORWARD, UA_FORWARD, 0 }, { UB_UP, UA_PARENT, 0 }, { UB_LOCATION, UA_LOCATION, 0 },
        { UB_LEFT, UA_CRUMB_SHIFT, -1 }, { UB_RIGHT, UA_CRUMB_SHIFT, 1 },
    };
    for (int i = 0; i < COUNT(B); i++) {
        in = ui_input_none(), in.bar = B[i].b;
        UiAction a = ui_handle(&u, names, &in);
        CHECK(a.kind == B[i].k && a.index == B[i].idx);
    }
    in = ui_input_none(), in.bar = UB_CRUMB, in.crumb = 3;
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_CRUMB && a.index == 3);
    in = ui_input_none(), in.bar = UB_SEARCH;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.finding);
    in = keys(1, UK_ESC);
    ui_handle(&u, names, &in);
    // Not while a prompt waits.
    u.confirming = true;
    in = ui_input_none(), in.bar = UB_BACK;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    u.confirming = false;
    // The keys.
    in = keys(1, UK_LOCATION);
    CHECK(ui_handle(&u, names, &in).kind == UA_LOCATION);
    in = keys(1, UK_PARENT);
    CHECK(ui_handle(&u, names, &in).kind == UA_PARENT);

    // The location line: all selected as it opens; typing replaces it, Backspace clears it.
    ui_location_open(&u, S("/home/m/code"));
    CHECK(u.editing && u.edit_path && u.anchor == 0 && u.caret == 12);
    in = chars("/tmp");
    ui_handle(&u, names, &in);
    CHECK(u.edit_len == 4 && memcmp(u.edit, "/tmp", 4) == 0 && u.anchor == 4);   // '/' allowed in a path
    in = keys(1, UK_ENTER);
    CHECK(ui_handle(&u, names, &in).kind == UA_GO && u.editing);
    ui_location_open(&u, S("/home/m"));
    in = keys(1, UK_BACKSPACE);
    ui_handle(&u, names, &in);
    CHECK(u.edit_len == 0 && u.editing);
    ui_location_open(&u, S("/home/m"));
    in = keys(1, UK_DELETE);
    ui_handle(&u, names, &in);
    CHECK(u.edit_len == 0);
    // A move of the caret keeps it, and ends the selection.
    ui_location_open(&u, S("/home/m"));
    in = keys(1, UK_END);
    ui_handle(&u, names, &in);
    in = chars("/x");
    ui_handle(&u, names, &in);
    CHECK(u.edit_len == 9 && memcmp(u.edit, "/home/m/x", 9) == 0);
    in = keys(1, UK_HOME);
    ui_handle(&u, names, &in);
    CHECK(u.caret == 0);
    // Ctrl+L or the toggle closes it; Esc too.
    in = keys(1, UK_LOCATION);
    ui_handle(&u, names, &in);
    CHECK(!u.editing);
    ui_location_open(&u, S("/a"));
    in = ui_input_none(), in.bar = UB_LOCATION;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.editing);
    ui_location_open(&u, S("/a"));
    in = keys(1, UK_ESC);
    ui_handle(&u, names, &in);
    CHECK(!u.editing);
    // A path longer than a name fits; a rename still stops at NAME_MAX and refuses '/'.
    char longp[600];
    memset(longp, 'p', sizeof longp - 1), longp[sizeof longp - 1] = 0;
    ui_location_open(&u, str_from_c(longp));
    CHECK(u.edit_len == 599);
    ui_edit_close(&u);
    ui_edit_open(&u, S("name"));
    CHECK(!u.edit_path && u.anchor == 4);
    in = chars("/");
    ui_handle(&u, names, &in);
    CHECK(u.edit_len == 4);
    in = keys(1, UK_LOCATION);   // Ctrl+L doesn't close a rename
    ui_handle(&u, names, &in);
    CHECK(u.editing);
    in = keys(1, UK_ENTER);
    CHECK(ui_handle(&u, names, &in).kind == UA_RENAME);
    ui_edit_close(&u);
    // A bar click closes a rename (back, up, a path button).
    ui_edit_open(&u, S("name"));
    in = ui_input_none(), in.bar = UB_UP;
    CHECK(ui_handle(&u, names, &in).kind == UA_PARENT && !u.editing);
}
