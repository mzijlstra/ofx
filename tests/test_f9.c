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
    // Step 2 (2026-10-28): what the list does works on results, each in its own folder; what needs
    // a folder shown says no: a paste, a new folder, a bookmark of it.
    Ui u = menu_ui(5);
    u.mark = mark, u.in_search = true, u.can_write = false;
    const UiKey NO[] = { UK_PASTE, UK_NEWFOLDER, UK_ADD_BOOKMARK };
    for (int k = 0; k < COUNT(NO); k++) CHECK(menu_keys(&u, 1, NO[k]).kind == UA_NOT_IN_SEARCH);
    const struct { UiKey k; UiActionKind a; } YES[] = {
        { UK_DELETE, UA_TRASH }, { UK_MAKE_LINK, UA_MAKE_LINK }, { UK_NEW_WINDOW, UA_NEW_WINDOW }, { UK_TERMINAL, UA_TERMINAL },
        { UK_ENTER, UA_OPEN }, { UK_COPY, UA_COPY }, { UK_CUT, UA_CUT }, { UK_PARENT, UA_PARENT }, { UK_TAB_NEW, UA_TAB_NEW },
    };
    for (int k = 0; k < COUNT(YES); k++) CHECK(menu_keys(&u, 1, YES[k].k).kind == YES[k].a);
    UiInput in = chars("t");
    CHECK(ui_handle(&u, names, &in).kind == UA_TERMINAL);
    menu_keys(&u, 1, UK_F2);
    CHECK(u.editing);
    menu_keys(&u, 1, UK_ESC);
    menu_keys(&u, 1, UK_SHIFT_DELETE);
    CHECK(u.confirming && u.confirm_n == 1);
    u.confirming = false;
    // Ctrl+Enter: the cursor's result's folder; outside a search, nothing (every entry is here).
    u.list.sel = 3;
    UiAction a = menu_keys(&u, 1, UK_CONTAINING);
    CHECK(a.kind == UA_CONTAINING && a.index == 3);
    u.side_focus = true;
    CHECK(menu_keys(&u, 1, UK_CONTAINING).kind == UA_NONE);
    u.side_focus = false;
    // The menus: those three greyed; the rest there, Rename, Duplicate and Make Link whatever the
    // folder shown (each result's own folder decides).
    u.can_bookmark = true;
    const UiMenuItem GREY[] = { UM_NEW_FOLDER, UM_NEW_FILE, UM_PASTE, UM_ADD_BOOKMARK };
    for (int k = 0; k < COUNT(GREY); k++) CHECK(!ui_menu_enabled(&u, GREY[k]));
    const UiMenuItem ON[] = { UM_NEW_WINDOW, UM_RENAME, UM_DUPLICATE, UM_MAKE_LINK, UM_TRASH, UM_DELETE, UM_TERMINAL, UM_OPEN, UM_COPY, UM_CUT, UM_SEARCH };
    for (int k = 0; k < COUNT(ON); k++) CHECK(ui_menu_enabled(&u, ON[k]));
    // A result's menu: Open Containing Folder; on a folder, Search in Folder…, a search below it.
    in = ui_input_none(), in.ctx_row = 3;
    ui_handle(&u, names, &in);
    CHECK(u.menu_on && u.menu == UI_CTX_ENTRY && ui_menu_shown(&u, UM_CONTAINING) && ui_menu_enabled(&u, UM_CONTAINING) && !ui_menu_shown(&u, UM_SEARCH_IN));
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_ENTRY, UM_CONTAINING);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_CONTAINING && a.index == 3 && !u.menu_on);
    in = ui_input_none(), in.ctx_row = 2;   // (dir_at: row 2 is a folder)
    ui_handle(&u, names, &in);
    CHECK(u.ctx_dir && ui_menu_shown(&u, UM_SEARCH_IN) && ui_menu_enabled(&u, UM_SEARCH_IN));
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_ENTRY, UM_SEARCH_IN);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_SEARCH && a.all && a.index == 2);
    // The empty space's menu: no Properties of a folder shown (a search isn't one).
    in = ui_input_none(), in.ctx_here = true;
    ui_handle(&u, names, &in);
    CHECK(u.menu_on && u.menu == UI_CTX_HERE && !ui_menu_enabled(&u, UM_HERE_PROPS));
    menu_keys(&u, 1, UK_ESC);
    // Outside it: no Open Containing Folder; Search in Folder… on a folder still; the rest as before.
    u.in_search = false, u.can_write = true, u.can_paste = true;
    in = ui_input_none(), in.ctx_row = 2;
    ui_handle(&u, names, &in);
    CHECK(!ui_menu_shown(&u, UM_CONTAINING) && ui_menu_shown(&u, UM_SEARCH_IN));
    menu_keys(&u, 1, UK_ESC);
    CHECK(menu_keys(&u, 1, UK_CONTAINING).kind == UA_NONE);
    for (int k = 0; k < COUNT(GREY); k++) CHECK(ui_menu_enabled(&u, GREY[k]));
    CHECK(menu_keys(&u, 1, UK_NEWFOLDER).kind == UA_NEWFOLDER && menu_keys(&u, 1, UK_PASTE).kind == UA_PASTE);
    // The trash view: no Search in Folder… on its folders (they're in the trash).
    u.in_trash = true;
    in = ui_input_none(), in.ctx_row = 2;
    ui_handle(&u, names, &in);
    CHECK(!ui_menu_shown(&u, UM_SEARCH_IN));
    menu_keys(&u, 1, UK_ESC);
    // A folder elsewhere (a sidebar row, a path button): Search in Folder…, not on Trash.
    u.in_trash = false;
    u.menu = UI_CTX_FOLDER;
    CHECK(ui_menu_shown(&u, UM_SEARCH_IN) && ui_menu_enabled(&u, UM_SEARCH_IN));
    u.ctx_trash = true;
    CHECK(!ui_menu_shown(&u, UM_SEARCH_IN));
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

// The search field edits as the rename line does (Michael, 2026-10-28): Ctrl+A selects its text, not
// the results; typing replaces the selection; Ctrl+V pastes (its first line, no '/'); Ctrl+C copies;
// Left, Home, Delete edit there.
static void test_search_field_edit(void)
{
    static u8 mark[5];
    UiNames names = { name_at, NULL, dir_at };
    Ui u = menu_ui(5);
    u.mark = mark, u.in_search = true, u.search_keys = true;
    UiInput in = chars("index");
    ui_handle(&u, names, &in);
    CHECK(u.search_len == 5 && u.search_caret == 5 && u.search_anchor == 5);
    u.search_edited = false;
    menu_keys(&u, 1, UK_ALL);
    CHECK(u.marked == 0 && u.search_anchor == 0 && u.search_caret == 5 && !u.search_edited);
    in = chars("ts");
    ui_handle(&u, names, &in);
    CHECK(u.search_len == 2 && memcmp(u.search, "ts", 2) == 0 && u.search_edited);
    // Text of the same length pasted over the selection is a change too.
    u.search_edited = false;
    menu_keys(&u, 1, UK_ALL);
    in = keys(1, UK_PASTE);
    in.paste = S("zz");
    ui_handle(&u, names, &in);
    CHECK(memcmp(u.search, "zz", 2) == 0 && u.search_len == 2 && u.search_edited);
    menu_keys(&u, 1, UK_ALL);
    in = chars("ts");
    ui_handle(&u, names, &in);
    // Ctrl+V at the caret: the first line, '/' left out.
    u.search_edited = false;
    in = keys(1, UK_PASTE);
    in.paste = S("a/b\nsecond");
    ui_handle(&u, names, &in);
    CHECK(u.search_len == 4 && memcmp(u.search, "tsab", 4) == 0 && u.search_edited && u.search_keys);
    // Home, Delete; Left, Shift+End, Ctrl+C: the selection copied, the text as it was.
    u.search_edited = false;
    menu_keys(&u, 2, UK_HOME, UK_DELETE);
    CHECK(u.search_len == 3 && memcmp(u.search, "sab", 3) == 0 && u.search_caret == 0 && u.search_edited);
    u.search_edited = false;
    menu_keys(&u, 3, UK_RIGHT, UK_SEL_END, UK_COPY);
    CHECK(u.copied && u.copy_len == 2 && memcmp(u.copy, "ab", 2) == 0 && u.search_len == 3 && !u.search_edited);
    u.copied = false;
    // Ctrl+X: copied and gone.
    menu_keys(&u, 1, UK_CUT);
    CHECK(u.copied && u.search_len == 1 && u.search_edited);
    // A move that changes nothing isn't a change; nor is a paste with nothing that fits.
    u.search_edited = false;
    menu_keys(&u, 2, UK_LEFT, UK_END);
    in = keys(1, UK_PASTE);
    in.paste = S("///");
    ui_handle(&u, names, &in);
    CHECK(!u.search_edited && u.search_len == 1);
    // A caret past the text (gui/ set a shorter one) is put back inside it first.
    u.search_caret = u.search_anchor = 40;
    in = chars("z");
    ui_handle(&u, names, &in);
    CHECK(u.search_len == 2 && u.search[1] == 'z');
    // A question up takes the keys before the field (Shift+Delete from the results, then n).
    u.search_keys = false;
    menu_keys(&u, 1, UK_SHIFT_DELETE);
    CHECK(u.confirming);
    u.search_keys = true;
    int len = u.search_len;
    in = chars("n");
    ui_handle(&u, names, &in);
    CHECK(!u.confirming && u.search_len == len);
    // In the results, Ctrl+A is the list's again: every result selected.
    menu_keys(&u, 1, UK_ENTER);
    CHECK(!u.search_keys);
    menu_keys(&u, 1, UK_ALL);
    CHECK(u.marked == 5);
}

// The find bar's field edits as the rename line does too (Michael, 2026-10-28), each change found
// again; Up and Down (Shift too) still step through the matches.
static void test_find_field_edit(void)
{
    UiNames names = { name_at, NULL, NULL };
    Ui u = { 0 };
    u.list.rows = 10;
    ui_set(&u.list, COUNT(find_names), 0);
    ui_find_open(&u);
    CHECK(u.find_caret == 0 && u.find_anchor == 0);
    UiInput in = chars("gam");
    ui_handle(&u, names, &in);
    isize at = u.list.sel;
    CHECK(u.find_len == 3 && u.find_caret == 3 && !u.find_miss);
    // Ctrl+A selects the text, not the files; Ctrl+V pastes over it (first line, no '/').
    menu_keys(&u, 1, UK_ALL);
    CHECK(u.find_anchor == 0 && u.find_caret == 3 && u.marked == 0 && u.finding && u.find_keys);
    in = keys(1, UK_PASTE);
    in.paste = S("zz/q\nx");
    ui_handle(&u, names, &in);
    CHECK(u.find_len == 3 && memcmp(u.find, "zzq", 3) == 0 && u.find_miss);
    // Home, Delete: found again; Left, Shift+End, Ctrl+X: copied and gone, found again.
    menu_keys(&u, 2, UK_HOME, UK_DELETE);
    CHECK(u.find_len == 2 && u.find_caret == 0 && memcmp(u.find, "zq", 2) == 0);
    in = keys(1, UK_ALL);
    ui_handle(&u, names, &in);
    in = keys(1, UK_PASTE);
    in.paste = S("gam");
    ui_handle(&u, names, &in);
    CHECK(!u.find_miss && u.list.sel == at);
    menu_keys(&u, 3, UK_LEFT, UK_SEL_END, UK_CUT);
    CHECK(u.copied && u.copy_len == 1 && u.copy[0] == 'm' && u.find_len == 2);
    u.copied = false;
    // A move changes nothing: the cursor stays where the match put it.
    isize sel = u.list.sel;
    menu_keys(&u, 2, UK_HOME, UK_END);
    CHECK(u.list.sel == sel && u.find_keys);
    // Up and Down, with Shift too, step through the matches and keep the keys.
    menu_keys(&u, 1, UK_ALL);
    in = chars("m");   // gamma.txt, BETAMAX, omega
    ui_handle(&u, names, &in);
    isize first = u.list.sel;
    CHECK(ui_find_count(&u, names, &at) == 3);
    menu_keys(&u, 1, UK_SEL_DOWN);
    CHECK(u.find_keys && u.list.sel != first);
    menu_keys(&u, 1, UK_SEL_UP);
    CHECK(u.list.sel == first && u.find_keys);
    // A caret past the text (the field set from outside) is put back inside first.
    u.find_caret = u.find_anchor = 90;
    in = chars("x");
    ui_handle(&u, names, &in);
    CHECK(u.find_len == 2 && u.find[1] == 'x');
    // A question takes the keys first.
    u.confirming = true, u.confirm_n = 1;
    in = chars("n");
    ui_handle(&u, names, &in);
    CHECK(!u.confirming && u.find_len == 2);
    ui_matcher_set(&u.find_m, STR_EMPTY, u.find_opts);
}
