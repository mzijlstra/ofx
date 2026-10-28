// F9 step 1 (docs/plan.md "F9 design"): the search view's address, the walk below a folder (what
// it enters, skips and counts, its limit, a new search stopping it), the wildcard, and listing_find
// by folder and name. Included by test.c after test_f8.c.

static void test_search_uri(void)
{
    char u[256];
    Str r, t;
    CHECK(search_uri(u, sizeof u, "/home/m/code", S("a:1")));
    CHECK(strcmp(u, "search:12:/home/m/code" "a:1") == 0 && is_search(u));
    CHECK(search_parse(u, &r, &t));
    CHECK_STR(r, "/home/m/code");
    CHECK_STR(t, "a:1");
    // Empty text; a folder with digits and ':' in it.
    CHECK(search_uri(u, sizeof u, "/a:2", STR_EMPTY) && search_parse(u, &r, &t));
    CHECK_STR(r, "/a:2");
    CHECK(t.len == 0);
    // Too small a buffer.
    CHECK(!search_uri(u, 10, "/home/m/code", S("x")));
    // Not addresses.
    CHECK(!is_search("/search:1:/") && !search_parse("/home", &r, &t));
    CHECK(!search_parse("search:", &r, &t) && !search_parse("search:x:/a", &r, &t) && !search_parse("search:2/a", &r, &t));
    CHECK(!search_parse("search:9:/a", &r, &t));   // shorter than it says
    CHECK(!search_parse("search:0:", &r, &t) && !search_parse("search:2:ab", &r, &t));   // not a path
    CHECK(!search_parse("search:99999:/a", &r, &t));
    CHECK(!search_parse("search:9999999999999999999999999:/a", &r, &t));   // (no overflow)
    CHECK(!search_parse("search:2x/a", &r, &t) && !search_parse("search:", &r, &t));
    CHECK(!is_search("search") && !is_search("search/x") && !is_search("Search:2:/a"));
}

static struct {
    char  got[32][128];
    int   n, room;
    UiMatcher m;
} sw;

static bool sw_match(void *ctx, Str name)
{
    (void)ctx;
    return ui_search_match(&sw.m, name);
}

static bool sw_found(void *ctx, Str dir, Str name, int fd)
{
    (void)ctx;
    struct stat st;
    CHECK(fstatat(fd, name.p, &st, AT_SYMLINK_NOFOLLOW) == 0);   // fd is the folder it is in
    if (sw.n == sw.room || sw.n == 32) return false;
    snprintf(sw.got[sw.n++], sizeof sw.got[0], "%.*s|%.*s", STR_ARG(dir), STR_ARG(name));
    return true;
}

static bool sw_has(const char *want)
{
    for (int i = 0; i < sw.n; i++)
        if (strcmp(sw.got[i], want) == 0) return true;
    return false;
}

static SearchWalk sw_walk(const char *text, UiMatchOpts o, bool hidden)
{
    ui_matcher_set(&sw.m, str_from_c(text), o);
    sw.n = 0, sw.room = 32;
    return (SearchWalk){ .hidden = hidden, .match = sw_match, .found = sw_found };
}

static void test_search_walk(void)
{
    put(at("f9/a.ts"), "");
    put(at("f9/b.txt"), "");
    put(at("f9/sub/c.ts"), "");
    put(at("f9/sub/deep/d.TS"), "");
    put(at("f9/.hid/e.ts"), "");
    put(at("f9/.f.ts"), "");
    put(at("f9/.git/g.ts"), "");
    put(at("f9/node_modules/h.ts"), "");
    put(at("f9/sub/node_modules/i.ts"), "");
    CHECK(symlink("sub", at("f9/link")) == 0);
    CHECK(symlink("a.ts", at("f9/flink.ts")) == 0);
    CHECK(symlink("nowhere", at("f9/broken")) == 0);
    const char *root9 = at("f9");

    // A wildcard, case ignored: below every folder but the hidden ones, node_modules, and the link
    // to a folder; the link to a file matches by its name.
    SearchWalk w = sw_walk("*.ts", (UiMatchOpts){ 0 }, false);
    CHECK(search_walk(&w, root9));
    CHECK(sw.n == 4 && sw_has("|a.ts") && sw_has("sub|c.ts") && sw_has("sub/deep|d.TS") && sw_has("|flink.ts"));
    CHECK(w.s.hidden == 3 && w.s.skip[0] == 0 && w.s.skip[1] == 2 && w.s.links == 1 && w.s.unread == 0 && !w.s.full);
    CHECK(w.s.folders == 3);   // f9, sub, sub/deep
    char line[200];
    search_skips_text(&w.s, line, sizeof line);
    CHECK(strcmp(line, "skipped 2 node_modules, 3 hidden, 1 link to a folder") == 0);
    // Case as typed.
    w = sw_walk("*.TS", (UiMatchOpts){ .cased = true }, false);
    CHECK(search_walk(&w, root9) && sw.n == 1 && sw_has("sub/deep|d.TS"));
    // Hidden files shown: they are searched, and their folders entered, but .git isn't.
    w = sw_walk("*.ts", (UiMatchOpts){ 0 }, true);
    CHECK(search_walk(&w, root9));
    CHECK(sw.n == 6 && sw_has(".hid|e.ts") && sw_has("|.f.ts") && !sw_has(".git|g.ts"));
    CHECK(w.s.hidden == 0 && w.s.skip[0] == 1 && w.s.skip[1] == 2);
    search_skips_text(&w.s, line, sizeof line);
    CHECK(strcmp(line, "skipped 1 .git, 2 node_modules, 1 link to a folder") == 0);
    // A folder left out still matches by its own name; text without a wildcard matches anywhere in it.
    w = sw_walk("MODULE", (UiMatchOpts){ 0 }, false);
    CHECK(search_walk(&w, root9) && sw.n == 2 && sw_has("|node_modules") && sw_has("sub|node_modules"));
    w = sw_walk("link", (UiMatchOpts){ 0 }, false);
    CHECK(search_walk(&w, root9) && sw.n == 2 && sw_has("|link") && sw_has("|flink.ts"));
    w = sw_walk("?.ts", (UiMatchOpts){ 0 }, false);   // one character
    CHECK(search_walk(&w, root9) && sw.n == 3 && !sw_has("|flink.ts"));
    // Full: it stops at the first match there is no room for.
    w = sw_walk("*", (UiMatchOpts){ 0 }, false);
    sw.room = 2;
    CHECK(!search_walk(&w, root9) && w.s.full && sw.n == 2);
    // A new search stops it.
    atomic_uint gen = 5;
    w = sw_walk("*", (UiMatchOpts){ 0 }, false);
    w.gen = &gen, w.mine = 4;
    CHECK(!search_walk(&w, root9) && !w.s.full && sw.n == 0);
    w.mine = 5;
    CHECK(search_walk(&w, root9) && sw.n > 0);
    // A folder it can't read, and one that isn't there.
    put(at("f9/shut/x.ts"), "");
    chmod(at("f9/shut"), 0);
    w = sw_walk("*.ts", (UiMatchOpts){ 0 }, false);
    CHECK(search_walk(&w, root9) && w.s.unread == (geteuid() == 0 ? 0 : 1));
    search_skips_text(&w.s, line, sizeof line);
    CHECK(geteuid() == 0 || strstr(line, ", 1 unreadable folder"));
    chmod(at("f9/shut"), 0700);
    w = sw_walk("*.ts", (UiMatchOpts){ 0 }, false);
    CHECK(search_walk(&w, at("f9/gone")) && w.s.unread == 1 && sw.n == 0);
    // Nothing left out: no line.
    SearchSkips none = { .folders = 4 };
    search_skips_text(&none, line, sizeof line);
    CHECK(line[0] == 0);
    SearchSkips many = { .drives = 2, .links = 2, .unread = 2 };
    search_skips_text(&many, line, sizeof line);
    CHECK(strcmp(line, "skipped 2 other drives, 2 links to folders, 2 unreadable folders") == 0);
    search_skips_text(&many, line, 12);   // cut, ended
    CHECK(strlen(line) == 11);
    ui_matcher_set(&sw.m, STR_EMPTY, (UiMatchOpts){ 0 });
}

static void test_search_match(void)
{
    UiMatcher m = { 0 };
    ui_matcher_set(&m, S("*.ts"), (UiMatchOpts){ 0 });
    CHECK(ui_wild(&m) && ui_search_match(&m, S("a.ts")) && ui_search_match(&m, S("A.TS")) && !ui_search_match(&m, S("a.tsx")));
    CHECK(ui_search_match(&m, S(".ts")));   // a dot-file as any other
    ui_matcher_set(&m, S("*.TS"), (UiMatchOpts){ .cased = true });
    CHECK(!ui_search_match(&m, S("a.ts")) && ui_search_match(&m, S("a.TS")));
    ui_matcher_set(&m, S("é?"), (UiMatchOpts){ 0 });   // a character, not a byte; é folds
    CHECK(ui_search_match(&m, S("Éa")) && ui_search_match(&m, S("éé")) && !ui_search_match(&m, S("éab")));
    // Without a wildcard: as find (anywhere in the name, whole words when asked).
    ui_matcher_set(&m, S("ts"), (UiMatchOpts){ 0 });
    CHECK(!ui_wild(&m) && ui_search_match(&m, S("a.tsx")));
    ui_matcher_set(&m, S("ts"), (UiMatchOpts){ .word = true });
    CHECK(!ui_search_match(&m, S("a.tsx")) && ui_search_match(&m, S("a.ts")));
    // A regular expression: * and ? are its own.
    ui_matcher_set(&m, S("^a?b$"), (UiMatchOpts){ .regex = true });
    CHECK(!ui_wild(&m) && ui_search_match(&m, S("b")) && ui_search_match(&m, S("ab")) && !ui_search_match(&m, S("acb")));
    // A name too long for one: no match.
    char big[NAME_MAX + 2];
    memset(big, 'a', sizeof big - 1), big[sizeof big - 1] = 0;
    ui_matcher_set(&m, S("*"), (UiMatchOpts){ 0 });
    CHECK(!ui_search_match(&m, str_from_c(big)) && ui_search_match(&m, (Str){ big, NAME_MAX }));
    ui_matcher_set(&m, STR_EMPTY, (UiMatchOpts){ 0 });
}

static void test_search_keys_find(void)
{
    Entry e[3] = { { .name = S("index.ts") }, { .name = S("index.ts"), .dir = S("a/b") }, { .name = S("b"), .dir = S("a") } };
    Listing l = { e, 3, 0 };
    char buf[64];
    CHECK_STR(entry_key(&e[0], buf, sizeof buf), "index.ts");
    CHECK_STR(entry_key(&e[1], buf, sizeof buf), "a/b/index.ts");
    CHECK(listing_find(&l, S("index.ts")) == 0 && listing_find(&l, S("a/b/index.ts")) == 1 && listing_find(&l, S("a/b")) == 2);
    CHECK(listing_find(&l, S("b")) == -1 && listing_find(&l, S("a/index.ts")) == -1);
    CHECK(entry_key(&e[1], buf, 8).len == 0);   // doesn't fit
    // fs_list's names through entry_read (shared with the search's walk): an arena with room for the
    // entries but not their names says so.
    char longname[201];
    memset(longname, 'n', 200), longname[200] = 0;
    for (int k = 0; k < 3; k++) put(at("f9names/%c%s", 'a' + k, longname), "");
    static u8 small[sizeof(Entry) * 256 + 256];
    Arena sa;
    arena_init(&sa, "small", small, sizeof small);
    Listing sl;
    Err err;
    CHECK(!fs_list(&sa, at("f9names"), false, (Sort){ SORT_NAME, false }, &sl, &err) && strstr(err.msg, "too many entries"));
    // The same name in two folders: by folder, whichever way the names sort.
    Entry f[3] = { { .name = S("x"), .dir = S("b") }, { .name = S("x"), .dir = S("a") }, { .name = S("x") } };
    Listing fl = { f, 3, 0 };
    fs_sort(&fl, (Sort){ SORT_NAME, false });
    CHECK(f[0].dir.len == 0 && str_eq(f[1].dir, S("a")) && str_eq(f[2].dir, S("b")));
    fs_sort(&fl, (Sort){ SORT_NAME, true });
    CHECK(str_eq(f[0].dir, S("b")) && f[2].dir.len == 0);
}

// Another drive below: a folder whose drive isn't the walk's is counted and not entered, its name
// still matched (search_dir with a drive no folder is on stands in for a mount).
static void test_search_drives(void)
{
    put(at("f9d/sub/x.ts"), "");
    put(at("f9d/y.ts"), "");
    SearchWalk w = sw_walk("*", (UiMatchOpts){ 0 }, false);
    w.dev = (dev_t)-1;
    int fd = open(at("f9d"), O_RDONLY | O_DIRECTORY);
    CHECK(fd >= 0 && search_dir(&w, fd, 1));
    CHECK(w.s.drives == 1 && sw.n == 2 && sw_has("|sub") && sw_has("|y.ts"));
    char line[100];
    search_skips_text(&w.s, line, sizeof line);
    CHECK(strcmp(line, "skipped 1 other drive") == 0);
    // Too deep: counted as not read.
    w = sw_walk("*", (UiMatchOpts){ 0 }, false);
    fd = open(at("f9d"), O_RDONLY | O_DIRECTORY);
    CHECK(search_dir(&w, fd, SEARCH_DEPTH_MAX + 1) && w.s.unread == 1 && sw.n == 0);
    ui_matcher_set(&sw.m, STR_EMPTY, (UiMatchOpts){ 0 });
}

static void test_search_ui_keys(void)
{
    static u8 mark[5];
    UiNames names = { name_at, NULL, dir_at };
    // Ctrl+Shift+F, the magnifier and Go → Search… ask gui/ for a search tab; not from the trash.
    Ui u = menu_ui(5);
    u.mark = mark;
    CHECK(menu_keys(&u, 1, UK_SEARCH).kind == UA_SEARCH);
    UiInput in = ui_input_none();
    in.bar = UB_SEARCH;
    CHECK(ui_handle(&u, names, &in).kind == UA_SEARCH && !u.finding);
    in = ui_input_none(), in.menu_item = menu_place(3, UM_SEARCH);
    ui_menu_open(&u, 3, false);
    CHECK(ui_menu_enabled(&u, UM_SEARCH) && ui_handle(&u, names, &in).kind == UA_SEARCH);
    u.in_trash = true;
    CHECK(!ui_menu_enabled(&u, UM_SEARCH) && menu_keys(&u, 1, UK_SEARCH).kind == UA_NOT_IN_TRASH);
    u.in_trash = false;

    // The search view, its field with the keys (gui/ gives them when it opens empty): text and
    // Backspace change it, '/' doesn't go in; each change is marked for gui/.
    u.in_search = true, u.search_keys = true;
    in = chars("a/b");
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 0xE9 };   // é
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.search_len == 4 && memcmp(u.search, "ab\xC3\xA9", 4) == 0 && u.search_edited);
    CHECK(u.list.sel == 0);   // (no letter moved the cursor)
    u.search_edited = false;
    menu_keys(&u, 1, UK_BACKSPACE);
    CHECK(u.search_len == 2 && u.search_edited);
    u.search_edited = false, u.search_len = 0;
    menu_keys(&u, 1, UK_BACKSPACE);
    CHECK(u.search_len == 0 && !u.search_edited);
    // Full: no more.
    u.search_len = UI_FIND_MAX - 1;
    in = chars("x");
    ui_handle(&u, names, &in);
    CHECK(u.search_len == UI_FIND_MAX);
    in = chars("y");
    ui_handle(&u, names, &in);
    CHECK(u.search_len == UI_FIND_MAX);
    u.search_len = 1, u.search_edited = false;
    // Find's toggles, from the keys and the bar's buttons; each a change.
    in = ui_input_none(), in.ev[in.nev++] = (UiEvent){ UK_MENU, 'c' };
    ui_handle(&u, names, &in);
    CHECK(u.find_opts.cased && u.search_edited && !u.menu_on);
    u.search_edited = false;
    in = ui_input_none(), in.toggle = 3;
    CHECK(ui_input_acts(&in) && ui_handle(&u, names, &in).kind == UA_NONE && u.find_opts.regex && u.search_edited);
    u.find_opts = (UiMatchOpts){ 0 }, u.search_edited = false;
    // Enter, Esc and Down give the keys to the results; Ctrl+Shift+F and a press on the field take
    // them back.
    for (int k = 0; k < 4; k++) {
        UiKey key = (UiKey[]){ UK_ENTER, UK_SHIFT_ENTER, UK_ESC, UK_DOWN }[k];
        u.search_keys = true;
        CHECK(menu_keys(&u, 1, key).kind == UA_NONE && !u.search_keys && u.list.sel == 0);
    }
    u.side_focus = true;
    CHECK(menu_keys(&u, 1, UK_SEARCH).kind == UA_NONE && u.search_keys && !u.side_focus);
    CHECK(menu_keys(&u, 1, UK_SEARCH).kind == UA_NONE && u.search_keys);
    u.search_keys = false;
    in = ui_input_none(), in.search_field = true;
    CHECK(ui_input_acts(&in) && ui_handle(&u, names, &in).kind == UA_NONE && u.search_keys);
    in = ui_input_none(), in.search_field = true, in.bar = UB_BACK;
    u.search_keys = false, u.confirming = true;   // not under a question
    ui_handle(&u, names, &in);
    CHECK(!u.search_keys);
    u.confirming = false, u.search_keys = true;
    // Another command leaves the field and runs in the results.
    CHECK(menu_keys(&u, 1, UK_RELOAD).kind == UA_RELOAD && !u.search_keys);
    u.search_keys = true;
    CHECK(menu_keys(&u, 1, UK_HIDDEN).kind == UA_HIDDEN && !u.search_keys && !u.search_edited);
    // In the results: the letters are the list's again.
    in = chars("j");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && u.search_len == 1);
    // A toggle press outside the search view, the find bar closed: nothing.
    u.in_search = false;
    in = ui_input_none(), in.toggle = 1;
    ui_handle(&u, names, &in);
    CHECK(!u.find_opts.cased && !u.search_edited);
    in = ui_input_none(), in.search_field = true;
    ui_handle(&u, names, &in);
    CHECK(!u.search_keys);
}

static void test_search_ui_refused(void)
{
    static u8 mark[5];
    UiNames names = { name_at, NULL, dir_at };
    // What the results can't do yet (F9 step 2) says so; what works through the entries' own
    // paths stays (open, copy, cut, Properties, a folder's tab).
    Ui u = menu_ui(5);
    u.mark = mark, u.in_search = true, u.can_write = false;
    const UiKey NO[] = { UK_F2, UK_DELETE, UK_SHIFT_DELETE, UK_MAKE_LINK, UK_PASTE, UK_NEWFOLDER, UK_NEW_WINDOW, UK_TERMINAL, UK_ADD_BOOKMARK };
    for (int k = 0; k < COUNT(NO); k++) {
        CHECK(menu_keys(&u, 1, NO[k]).kind == UA_NOT_IN_SEARCH);
        CHECK(!u.editing && !u.confirming);
    }
    UiInput in = chars("t");
    CHECK(ui_handle(&u, names, &in).kind == UA_NOT_IN_SEARCH);
    CHECK(menu_keys(&u, 1, UK_ENTER).kind == UA_OPEN && menu_keys(&u, 1, UK_COPY).kind == UA_COPY && menu_keys(&u, 1, UK_CUT).kind == UA_CUT);
    CHECK(menu_keys(&u, 1, UK_PARENT).kind == UA_PARENT && menu_keys(&u, 1, UK_TAB_NEW).kind == UA_TAB_NEW);
    // The menus grey them.
    const UiMenuItem GREY[] = { UM_NEW_WINDOW, UM_NEW_FOLDER, UM_NEW_FILE, UM_PASTE, UM_RENAME, UM_DUPLICATE, UM_MAKE_LINK, UM_TRASH, UM_DELETE,
                                UM_TERMINAL, UM_ADD_BOOKMARK };
    u.can_bookmark = true;
    for (int k = 0; k < COUNT(GREY); k++) CHECK(!ui_menu_enabled(&u, GREY[k]));
    CHECK(ui_menu_enabled(&u, UM_OPEN) && ui_menu_enabled(&u, UM_COPY) && ui_menu_enabled(&u, UM_CUT) && ui_menu_enabled(&u, UM_SEARCH));
    // The empty space's menu: no Properties of a folder shown (a search isn't one).
    in = ui_input_none(), in.ctx_here = true;
    ui_handle(&u, names, &in);
    CHECK(u.menu_on && u.menu == UI_CTX_HERE && !ui_menu_enabled(&u, UM_HERE_PROPS));
    menu_keys(&u, 1, UK_ESC);
    // Outside it, as before.
    u.in_search = false, u.can_write = true, u.can_paste = true;
    for (int k = 0; k < COUNT(GREY); k++) CHECK(ui_menu_enabled(&u, GREY[k]));
    CHECK(menu_keys(&u, 1, UK_NEWFOLDER).kind == UA_NEWFOLDER && menu_keys(&u, 1, UK_TERMINAL).kind == UA_TERMINAL);
    // Restore, split from them: the trash view's, with the files' cursor.
    u = menu_ui(5);
    u.in_trash = true;
    CHECK(ui_menu_enabled(&u, UM_RESTORE));
    u.side_focus = true;
    CHECK(!ui_menu_enabled(&u, UM_RESTORE));
}

static void test_search_crumbs(void)
{
    UiCrumbs c = { 0 };
    char u[128];
    CHECK(search_uri(u, sizeof u, "/home/m/code", S("x")));
    ui_crumbs_set(&c, u, "/home/m");
    CHECK(c.n == 1 && c.cur == 0 && str_eq(ui_crumb_label(&c, 0), S("Search")) && str_eq(ui_crumb_path(&c, 0), str_from_c(u)));
    ui_crumbs_set(&c, "/home/m/code", "/home/m");   // off it again: a path's buttons
    CHECK(c.n == 2 && str_eq(ui_crumb_label(&c, 1), S("code")));
}

// A search's folder/name longer than the history keeps: none kept, not a cut one that could name
// another entry (reviewer-2, 2026-10-28).
static void test_search_hist_keys(void)
{
    static UiHistory h;
    char key[400];
    memset(key, 'x', 300), memcpy(key + 300, "/m2.txt", 8);
    hist_leave(&h, "search:2:/a", str_from_c(key));
    hist_leave(&h, "/b", S("dir/name"));
    CHECK(h.nback == 2 && h.back[0].sel[0] == 0 && strcmp(h.back[1].sel, "dir/name") == 0);
    char fits[NAME_MAX + 1];
    memset(fits, 'y', NAME_MAX), fits[NAME_MAX] = 0;
    hist_leave(&h, "/c", str_from_c(fits));
    CHECK(strlen(h.back[2].sel) == NAME_MAX);
    hist_leave(&h, "/d", STR_EMPTY);
    CHECK(h.back[3].sel[0] == 0);
    char over[NAME_MAX + 2];   // one byte too long
    memset(over, 'z', NAME_MAX + 1), over[NAME_MAX + 1] = 0;
    hist_leave(&h, "/e", str_from_c(over));
    CHECK(h.back[4].sel[0] == 0);
}
