// F9 step 1 (docs/plan.md "F9 design"): the search view's address, the walk below a folder (what
// it enters, skips and counts, its limit, a new search stopping it), the wildcard, and listing_find
// by folder and name. Included by test.c after test_f8.c.

static void test_search_uri(void)
{
    char u[256];
    Str r, t, in;
    CHECK(search_uri(u, sizeof u, "/home/m/code", S("a:1"), STR_EMPTY));
    CHECK(strcmp(u, "search:12:/home/m/code" "a:1") == 0 && is_search(u));
    CHECK(search_parse(u, &r, &t, &in));
    CHECK_STR(r, "/home/m/code");
    CHECK_STR(t, "a:1");
    // Empty text; a folder with digits and ':' in it.
    CHECK(search_uri(u, sizeof u, "/a:2", STR_EMPTY, STR_EMPTY) && search_parse(u, &r, &t, &in));
    CHECK_STR(r, "/a:2");
    CHECK(t.len == 0);
    // Too small a buffer.
    CHECK(!search_uri(u, 10, "/home/m/code", S("x"), STR_EMPTY));
    // Not addresses.
    CHECK(!is_search("/search:1:/") && !search_parse("/home", &r, &t, &in));
    CHECK(!search_parse("search:", &r, &t, &in) && !search_parse("search:x:/a", &r, &t, &in) && !search_parse("search:2/a", &r, &t, &in));
    CHECK(!search_parse("search:9:/a", &r, &t, &in));   // shorter than it says
    CHECK(!search_parse("search:0:", &r, &t, &in) && !search_parse("search:2:ab", &r, &t, &in));   // not a path
    CHECK(!search_parse("search:99999:/a", &r, &t, &in));
    CHECK(!search_parse("search:9999999999999999999999999:/a", &r, &t, &in));   // (no overflow)
    CHECK(!search_parse("search:2x/a", &r, &t, &in) && !search_parse("search:", &r, &t, &in));
    CHECK(!is_search("search") && !is_search("search/x") && !is_search("Search:2:/a"));
    // Inside files (step 3): the name's length after the folder's, the text inside last.
    CHECK(search_uri(u, sizeof u, "/a:2", S("*.ts"), S("use:x")));
    CHECK(strcmp(u, "search:4,4:/a:2*.tsuse:x") == 0 && search_parse(u, &r, &t, &in));
    CHECK_STR(r, "/a:2");
    CHECK_STR(t, "*.ts");
    CHECK_STR(in, "use:x");
    CHECK(search_uri(u, sizeof u, "/a", STR_EMPTY, S("x")) && strcmp(u, "search:2,0:/ax") == 0 && search_parse(u, &r, &t, &in));
    CHECK(t.len == 0);
    CHECK_STR(in, "x");
    CHECK(search_parse("search:2:/ax", &r, &t, &in) && in.len == 0);   // by name: nothing inside
    CHECK(!search_uri(u, 18, "/a", S("b"), S("cdefg")));
    CHECK(!search_parse("search:2,1:/ax", &r, &t, &in));    // nothing inside: written without the ','
    CHECK(!search_parse("search:2,2:/ax", &r, &t, &in));    // the name runs past the end
    CHECK(!search_parse("search:2,:/ax", &r, &t, &in) && !search_parse("search:2,x:/ax", &r, &t, &in));
    CHECK(!search_parse("search:,1:/ax", &r, &t, &in) && !search_parse("search:2,99999:/ax", &r, &t, &in));
}

static struct {
    char  got[32][128];
    int   n, room;
    UiMatcher m;
    SearchHit hit[32];        // inside files (step 3): each one's, its text copied to text[]
    char      text[32][SEARCH_SNIP + 1];
    const char *needle;       // what a line must hold (sw_line), case as written
    atomic_uint *bump;        // moved on by a match (a new search meanwhile)
} sw;

static bool sw_match(void *ctx, Str name)
{
    (void)ctx;
    return ui_search_match(&sw.m, name);
}

static bool sw_found(void *ctx, Str dir, Str name, int fd, const SearchHit *hit)
{
    (void)ctx;
    struct stat st;
    CHECK(fstatat(fd, name.p, &st, AT_SYMLINK_NOFOLLOW) == 0);   // fd is the folder it is in
    if (sw.n == sw.room || sw.n == 32) return false;
    CHECK((hit != NULL) == (sw.needle != NULL));
    if (hit) {
        sw.hit[sw.n] = *hit;
        snprintf(sw.text[sw.n], sizeof sw.text[0], "%.*s", STR_ARG(hit->text));
    }
    snprintf(sw.got[sw.n++], sizeof sw.got[0], "%.*s|%.*s", STR_ARG(dir), STR_ARG(name));
    return true;
}

static int sw_at(const char *want)
{
    for (int i = 0; i < sw.n; i++)
        if (strcmp(sw.got[i], want) == 0) return i;
    return -1;
}

static bool sw_has(const char *want)
{
    return sw_at(want) >= 0;
}

// Inside files: a line matches when it holds sw.needle (fs/ is tested apart from ui/'s matcher).
static bool sw_line(void *ctx, Str line, isize *from, isize *to)
{
    (void)ctx;
    const char *p = memmem(line.p, (size_t)line.len, sw.needle, strlen(sw.needle));
    if (!p) return false;
    *from = p - line.p, *to = *from + (isize)strlen(sw.needle);
    if (sw.bump) atomic_fetch_add(sw.bump, 1);
    return true;
}

static SearchWalk sw_walk(const char *text, UiMatchOpts o, bool hidden)
{
    ui_matcher_set(&sw.m, str_from_c(text), o);
    sw.n = 0, sw.room = 32, sw.needle = NULL;
    return (SearchWalk){ .hidden = hidden, .match = sw_match, .found = sw_found };
}

// A walk inside files for needle, below files whose names match name.
static SearchWalk sw_inside(const char *name, const char *needle)
{
    static char buf[SEARCH_BUF];
    SearchWalk w = sw_walk(name, (UiMatchOpts){ 0 }, false);
    sw.needle = needle, w.line = sw_line, w.buf = buf;
    return w;
}

static void test_search_inside(void)
{
    char r[300];   // (at's buffers turn over)
    snprintf(r, sizeof r, "%s", at("f9i"));
    put(at("f9i/a.txt"), "one\n  needle here\nnope\nwith needle again\n");
    put(at("f9i/sub/b.txt"), "\xC3\xA9\xC3\xA9 needle\r\n");   // a column in characters; CRLF
    put(at("f9i/c.md"), "needle");                                 // a name that doesn't match isn't read
    put(at("f9i/tab.txt"), "a\tneedle");                           // control characters as spaces; no last newline
    put(at("f9i/none.txt"), "nothing\n");
    put(at("f9i/empty.txt"), "");
    mkdirs(at("f9i/needle.txt"));                                  // a folder isn't a result inside files
    CHECK(symlink("a.txt", at("f9i/link.txt")) == 0);              // not followed, counted
    CHECK(mkfifo(at("f9i/fifo.txt"), 0600) == 0);                  // not opened (it would block)
    SearchWalk w = sw_inside("*.txt", "needle");
    CHECK(search_walk(&w, r));
    CHECK(sw.n == 3 && sw_has("|a.txt") && sw_has("sub|b.txt") && sw_has("|tab.txt"));
    int k = sw_at("|a.txt");
    CHECK(k >= 0 && sw.hit[k].line == 2 && sw.hit[k].col == 3 && sw.hit[k].more == 1);
    CHECK(k >= 0 && strcmp(sw.text[k], "needle here") == 0 && sw.hit[k].at == 0 && sw.hit[k].len == 6 && !sw.hit[k].cut);   // (the indent left out)
    k = sw_at("sub|b.txt");
    CHECK(k >= 0 && sw.hit[k].line == 1 && sw.hit[k].col == 4 && sw.hit[k].more == 0 && strcmp(sw.text[k], "\xC3\xA9\xC3\xA9 needle") == 0);
    CHECK(k >= 0 && sw.hit[k].at == 5);
    k = sw_at("|tab.txt");
    CHECK(k >= 0 && strcmp(sw.text[k], "a needle") == 0 && sw.hit[k].at == 2);
    CHECK(w.s.files == 5 && w.s.file_links == 1 && w.s.binary == 0 && w.s.big == 0 && w.s.unread_files == 0);   // a, b, tab, none, empty (needle.txt is a folder)

    // Binary: a NUL in the first 8 KB; past them, text.
    char head[SEARCH_HEAD + 16];
    memset(head, 'x', sizeof head);
    memcpy(head + 100, "needle", 6);
    head[SEARCH_HEAD - 1] = 0;
    put_bytes(at("f9b/bin.txt"), head, sizeof head);
    head[SEARCH_HEAD - 1] = 'x', head[SEARCH_HEAD] = 0;
    put_bytes(at("f9b/text.txt"), head, sizeof head);
    w = sw_inside("*", "needle");
    CHECK(search_walk(&w, at("f9b")) && sw.n == 1 && sw_has("|text.txt") && w.s.binary == 1);
    // Over 16 MB isn't read; 16 MB is (sparse: its NULs come after the first 8 KB).
    int fd = open(at("f9b/max.txt"), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0 && write(fd, head, SEARCH_HEAD) == SEARCH_HEAD && ftruncate(fd, SEARCH_FILE_MAX) == 0);
    close(fd);
    fd = open(at("f9b/big.txt"), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0 && write(fd, head, SEARCH_HEAD) == SEARCH_HEAD && ftruncate(fd, SEARCH_FILE_MAX + 1) == 0);
    close(fd);
    w = sw_inside("*", "needle");
    CHECK(search_walk(&w, at("f9b")) && sw.n == 2 && sw_has("|max.txt") && w.s.big == 1);
    char line[200];
    search_skips_text(&w.s, line, sizeof line);
    CHECK(strcmp(line, "skipped 1 binary, 1 over 16 MB") == 0);

    // A line longer than the buffer: a match across two pieces is found, its column counted
    // through them; a second match further on the same line counts the line once.
    static char lng[3 * SEARCH_BUF];
    memset(lng, 'y', sizeof lng);
    memcpy(lng, "top\n", 4);
    isize cross = 4 + SEARCH_BUF - 3;   // "needle" from 3 bytes before the first piece's end (the line's start, 4, at the front)
    memcpy(lng + cross, "needle", 6);
    memcpy(lng + 2 * SEARCH_BUF, "needle", 6);
    memcpy(lng + sizeof lng - 8, "\nneedle", 8);
    put_bytes(at("f9l/long.txt"), lng, sizeof lng);
    w = sw_inside("*", "needle");
    CHECK(search_walk(&w, at("f9l")) && sw.n == 1);
    CHECK(sw.n == 1 && sw.hit[0].line == 2 && sw.hit[0].col == cross - 4 + 1 && sw.hit[0].more == 1 && sw.hit[0].cut);
    CHECK(sw.n == 1 && sw.hit[0].text.len == SEARCH_SNIP && sw.hit[0].at == SEARCH_SNIP_BEFORE && sw.hit[0].len == 6);
    // Past the first piece, the column still counts characters before it (é is two bytes).
    memset(lng, 'y', sizeof lng);
    for (isize i = 0; i + 1 < SEARCH_CHUNK; i += 2) lng[i] = (char)0xC3, lng[i + 1] = (char)0xA9;
    memcpy(lng + SEARCH_BUF + 100, "needle", 6);
    put_bytes(at("f9l/long.txt"), lng, SEARCH_BUF + 200);
    w = sw_inside("*", "needle");
    CHECK(search_walk(&w, at("f9l")) && sw.n == 1 && sw.hit[0].line == 1 && sw.hit[0].col == SEARCH_CHUNK / 2 + (SEARCH_BUF - SEARCH_CHUNK) + 100 + 1);

    // The line after a long one counts its column from its own start.
    memset(lng, 'y', 2 * SEARCH_BUF);
    memcpy(lng + 2 * SEARCH_BUF, "\nab needle", 10);
    put_bytes(at("f9l/long.txt"), lng, 2 * SEARCH_BUF + 10);
    w = sw_inside("*", "needle");
    CHECK(search_walk(&w, at("f9l")) && sw.n == 1 && sw.hit[0].line == 2 && sw.hit[0].col == 4);
    // The line's start cut in a character: the text starts at the next one.
    char mb[200] = "x";   // then 40 € (three bytes each): 40 before the match is inside one
    for (int i = 0; i < 40; i++) memcpy(mb + 1 + 3 * i, "\xE2\x82\xAC", 3);
    memcpy(mb + 121, "needle", 7);
    put(at("f9m/mb.txt"), mb);
    w = sw_inside("*", "needle");
    CHECK(search_walk(&w, at("f9m")) && sw.n == 1 && sw.hit[0].at == 121 - 82 && memcmp(sw.text[0], "\xE2\x82\xAC", 3) == 0 && sw.hit[0].cut);
    // A new search while a file is read stops there, its match not kept (the line callback moves the
    // generation; the file goes on past its first piece).
    memset(lng, 'z', 2 * SEARCH_BUF);
    memcpy(lng, "needle\n", 7);
    for (isize i = 100; i < 2 * SEARCH_BUF; i += 100) lng[i] = '\n';
    put_bytes(at("f9s/stop.txt"), lng, 2 * SEARCH_BUF);
    atomic_uint moving = 1;
    w = sw_inside("*", "needle");
    w.gen = &moving, w.mine = 1, sw.bump = &moving;
    CHECK(!search_walk(&w, at("f9s")) && sw.n == 0 && !w.s.full);
    sw.bump = NULL;
    // Read without touching its access time (relatime would: it is older than the change).
    struct timespec t2[2] = { { 1000, 0 }, { 0, UTIME_OMIT } };
    CHECK(utimensat(AT_FDCWD, at("f9i/a.txt"), t2, 0) == 0);
    w = sw_inside("a.txt", "needle");
    CHECK(search_walk(&w, r) && sw.n == 1);
    struct stat st;
    CHECK(stat(at("f9i/a.txt"), &st) == 0 && st.st_atim.tv_sec == 1000);
    // A file it can't open.
    chmod(at("f9i/none.txt"), 0);
    w = sw_inside("none.txt", "needle");
    CHECK(search_walk(&w, r) && w.s.unread_files == (geteuid() == 0 ? 0 : 1));
    chmod(at("f9i/none.txt"), 0644);
    // Full, and stopped.
    w = sw_inside("*.txt", "needle");
    sw.room = 1;
    CHECK(!search_walk(&w, r) && w.s.full && sw.n == 1);
    atomic_uint gen = 2;
    w = sw_inside("*.txt", "needle");
    w.gen = &gen, w.mine = 1;
    CHECK(!search_walk(&w, r) && sw.n == 0 && !w.s.full);
    SearchSkips more = { .file_links = 2, .unread_files = 2 };
    search_skips_text(&more, line, sizeof line);
    CHECK(strcmp(line, "skipped 2 links to files, 2 unreadable files") == 0);
    ui_matcher_set(&sw.m, STR_EMPTY, (UiMatchOpts){ 0 });
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

// Inside files (F9 step 3): "In files" (Alt+I, the button) shows "Containing" with the keys; Tab
// goes between the fields; each has its own toggles (Michael, 2026-10-29); a press on the field or
// its toggles; off, the keys go back to the name's.
static void test_search_inside_keys(void)
{
    static u8 mark[5];
    UiNames names = { name_at, NULL, dir_at };
    Ui u = menu_ui(5);
    u.mark = mark, u.view = UI_VIEW_SEARCH, u.focus = UI_FOCUS_SEARCH;
    UiInput in = ui_input_none();
    in.ev[in.nev++] = (UiEvent){ UK_MENU, 'i' };
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.search_inside && u.focus == UI_FOCUS_INSIDE && u.search_edited && !u.menu_on);
    u.search_edited = false;
    in = chars("use");
    ui_handle(&u, names, &in);
    CHECK(u.inside.len == 3 && memcmp(u.inside.b, "use", 3) == 0 && u.search.len == 0 && u.search_edited);
    UiLine l;
    CHECK(ui_line_active(&u, &l) && l.b == u.inside.b);   // (the clipboard's keys act on it)
    // Its toggles are its own; the name field's are find's.
    u.search_edited = false;
    in = ui_input_none(), in.ev[in.nev++] = (UiEvent){ UK_MENU, 'c' };
    ui_handle(&u, names, &in);
    CHECK(u.inside_opts.cased && !u.find_opts.cased && u.search_edited);
    CHECK(menu_keys(&u, 1, UK_TAB).kind == UA_NONE && u.focus == UI_FOCUS_SEARCH);
    in = ui_input_none(), in.ev[in.nev++] = (UiEvent){ UK_MENU, 'w' };
    ui_handle(&u, names, &in);
    CHECK(u.find_opts.word && !u.inside_opts.word);
    in = chars("*.ts");
    ui_handle(&u, names, &in);
    CHECK(u.search.len == 4 && u.inside.len == 3);
    CHECK(menu_keys(&u, 1, UK_TAB).kind == UA_NONE && u.focus == UI_FOCUS_INSIDE);
    // Enter gives the keys to the results; Alt+I there turns it off and on again.
    CHECK(menu_keys(&u, 1, UK_ENTER).kind == UA_NONE && u.focus == UI_FOCUS_LIST);
    u.search_edited = false;
    in = ui_input_none(), in.ev[in.nev++] = (UiEvent){ UK_MENU, 'i' };
    ui_handle(&u, names, &in);
    CHECK(!u.search_inside && u.focus == UI_FOCUS_LIST && u.search_edited && u.inside.len == 3);
    // Off from its field: the keys go to the name's, and Tab there goes on as before (the sidebar).
    u.search_inside = true, u.focus = UI_FOCUS_INSIDE;
    in = ui_input_none(), in.ev[in.nev++] = (UiEvent){ UK_MENU, 'i' };
    ui_handle(&u, names, &in);
    CHECK(!u.search_inside && u.focus == UI_FOCUS_SEARCH);
    CHECK(menu_keys(&u, 1, UK_TAB).kind == UA_NONE && u.focus != UI_FOCUS_INSIDE && u.focus != UI_FOCUS_SEARCH);
    // Off, the field takes no keys even if it had them.
    u.focus = UI_FOCUS_INSIDE;
    in = chars("j");
    ui_handle(&u, names, &in);
    CHECK(u.inside.len == 3 && !ui_line_active(&u, NULL));
    // The pointer: the button, the field, a toggle.
    u.focus = UI_FOCUS_LIST, u.search_edited = false;
    in = ui_input_none(), in.inside_btn = true;
    CHECK(ui_input_acts(&in) && ui_handle(&u, names, &in).kind == UA_NONE && u.search_inside && u.focus == UI_FOCUS_INSIDE && u.search_edited);
    u.focus = UI_FOCUS_LIST, u.search_edited = false;
    in = ui_input_none(), in.inside_field = true;
    CHECK(ui_input_acts(&in) && ui_handle(&u, names, &in).kind == UA_NONE && u.focus == UI_FOCUS_INSIDE && !u.search_edited);
    in = ui_input_none(), in.inside_toggle = 3;
    CHECK(ui_input_acts(&in) && ui_handle(&u, names, &in).kind == UA_NONE && u.inside_opts.regex && !u.find_opts.regex && u.search_edited);
    // The button again: off.
    in = ui_input_none(), in.inside_btn = true;
    ui_handle(&u, names, &in);
    CHECK(!u.search_inside);
    u.search_inside = true;
    // Not under a menu, and not outside the search view.
    u.inside_opts = (UiMatchOpts){ 0 }, u.menu_on = true;
    in = ui_input_none(), in.inside_toggle = 1;
    ui_handle(&u, names, &in);
    CHECK(!u.inside_opts.cased);
    u.menu_on = false, u.view = UI_VIEW_FOLDER;
    in = ui_input_none(), in.inside_btn = true;
    ui_handle(&u, names, &in);
    CHECK(u.search_inside);
    in = ui_input_none(), in.ev[in.nev++] = (UiEvent){ UK_MENU, 'i' };
    ui_handle(&u, names, &in);
    CHECK(u.search_inside);
    // The field's and the toggles' presses while it is off: nothing.
    u.view = UI_VIEW_SEARCH, u.search_inside = false, u.focus = UI_FOCUS_LIST;
    in = ui_input_none(), in.inside_field = true, in.inside_toggle = 2;
    ui_handle(&u, names, &in);
    CHECK(u.focus == UI_FOCUS_LIST && !u.inside_opts.word);
}

// Where a match is (a line searched inside files shows it, F9 step 3): plain, with case, folded,
// whole words, a regular expression.
static void test_match_span(void)
{
    UiMatcher m = { 0 };
    isize a, b;
    ui_matcher_set(&m, S("ab"), (UiMatchOpts){ 0 });
    CHECK(ui_match_span(&m, S("xxAB ab"), &a, &b) && a == 2 && b == 4);
    ui_matcher_set(&m, S("ab"), (UiMatchOpts){ .cased = true });
    CHECK(ui_match_span(&m, S("xxAB ab"), &a, &b) && a == 5 && b == 7);
    ui_matcher_set(&m, S("ab"), (UiMatchOpts){ .cased = true, .word = true });
    CHECK(ui_match_span(&m, S("xab ab"), &a, &b) && a == 4 && b == 6);   // the first isn't a word
    CHECK(!ui_match_span(&m, S("abc"), &a, &b));
    ui_matcher_set(&m, S("\xC3\xA9t\xC3\xA9"), (UiMatchOpts){ 0 });   // été: folded past ASCII, bytes of another length
    CHECK(ui_match_span(&m, S("L'\xC3\x89T\xC3\x89!"), &a, &b) && a == 2 && b == 7);
    // A character past ASCII that folds to an ASCII letter still matches it (the Kelvin sign is k),
    // either way round.
    ui_matcher_set(&m, S("k"), (UiMatchOpts){ 0 });
    CHECK(ui_match_span(&m, S("x\xE2\x84\xAA"), &a, &b) && a == 1 && b == 4);
    ui_matcher_set(&m, S("\xE2\x84\xAA"), (UiMatchOpts){ 0 });
    CHECK(ui_match_span(&m, S("xk"), &a, &b) && a == 1 && b == 2);
    ui_matcher_set(&m, S("b+c"), (UiMatchOpts){ .regex = true });
    CHECK(ui_match_span(&m, S("abbbcd"), &a, &b) && a == 1 && b == 5);
    ui_matcher_set(&m, S("b+"), (UiMatchOpts){ .regex = true, .word = true });
    CHECK(ui_match_span(&m, S("abb bb"), &a, &b) && a == 4 && b == 6);
    ui_matcher_set(&m, STR_EMPTY, (UiMatchOpts){ 0 });
    CHECK(ui_match_span(&m, S("x"), &a, &b) && a == 0 && b == 0);
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
    u.view = UI_VIEW_TRASH;
    CHECK(!ui_menu_enabled(&u, UM_SEARCH) && menu_keys(&u, 1, UK_SEARCH).kind == UA_NOT_IN_TRASH);
    u.view = UI_VIEW_FOLDER;

    // The search view, its field with the keys (gui/ gives them when it opens empty): text and
    // Backspace change it, '/' doesn't go in; each change is marked for gui/.
    u.view = UI_VIEW_SEARCH, u.focus = UI_FOCUS_SEARCH;
    in = chars("a/b");
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 0xE9 };   // é
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.search.len == 4 && memcmp(u.search.b, "ab\xC3\xA9", 4) == 0 && u.search_edited);
    CHECK(u.list.sel == 0);   // (no letter moved the cursor)
    u.search_edited = false;
    menu_keys(&u, 1, UK_BACKSPACE);
    CHECK(u.search.len == 2 && u.search_edited);
    u.search_edited = false, u.search.len = 0;
    menu_keys(&u, 1, UK_BACKSPACE);
    CHECK(u.search.len == 0 && !u.search_edited);
    // Full: no more.
    u.search.len = UI_FIND_MAX - 1;
    in = chars("x");
    ui_handle(&u, names, &in);
    CHECK(u.search.len == UI_FIND_MAX);
    in = chars("y");
    ui_handle(&u, names, &in);
    CHECK(u.search.len == UI_FIND_MAX);
    u.search.len = 1, u.search_edited = false;
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
        u.focus = UI_FOCUS_SEARCH;
        CHECK(menu_keys(&u, 1, key).kind == UA_NONE && u.focus != UI_FOCUS_SEARCH && u.list.sel == 0);
    }
    u.focus = UI_FOCUS_SIDE;
    CHECK(menu_keys(&u, 1, UK_SEARCH).kind == UA_NONE && (u.focus == UI_FOCUS_SEARCH) && u.focus != UI_FOCUS_SIDE);
    CHECK(menu_keys(&u, 1, UK_SEARCH).kind == UA_NONE && (u.focus == UI_FOCUS_SEARCH));
    ui_unfocus(&u, UI_FOCUS_SEARCH);
    in = ui_input_none(), in.search_field = true;
    CHECK(ui_input_acts(&in) && ui_handle(&u, names, &in).kind == UA_NONE && (u.focus == UI_FOCUS_SEARCH));
    in = ui_input_none(), in.search_field = true, in.bar = UB_BACK;
    ui_unfocus(&u, UI_FOCUS_SEARCH), u.confirming = true;   // not under a question
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_SEARCH);
    u.confirming = false, u.focus = UI_FOCUS_SEARCH;
    // Another command leaves the field and runs in the results.
    CHECK(menu_keys(&u, 1, UK_RELOAD).kind == UA_RELOAD && u.focus != UI_FOCUS_SEARCH);
    u.focus = UI_FOCUS_SEARCH;
    CHECK(menu_keys(&u, 1, UK_HIDDEN).kind == UA_HIDDEN && u.focus != UI_FOCUS_SEARCH && !u.search_edited);
    // In the results: the letters are the list's again.
    in = chars("j");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && u.search.len == 1);
    // A toggle press outside the search view, the find bar closed: nothing.
    u.view = UI_VIEW_FOLDER;
    in = ui_input_none(), in.toggle = 1;
    ui_handle(&u, names, &in);
    CHECK(!u.find_opts.cased && !u.search_edited);
    in = ui_input_none(), in.search_field = true;
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_SEARCH);
}

static void test_search_ui_refused(void)
{
    static u8 mark[5];
    UiNames names = { name_at, NULL, dir_at };
    // Step 2 (2026-10-28): what the list does works on results, each in its own folder; what needs
    // a folder shown says no: a paste, a new folder, a bookmark of it.
    Ui u = menu_ui(5);
    u.mark = mark, u.view = UI_VIEW_SEARCH, u.can_write = false;
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
    CHECK((u.focus == UI_FOCUS_EDIT));
    menu_keys(&u, 1, UK_ESC);
    menu_keys(&u, 1, UK_SHIFT_DELETE);
    CHECK(u.confirming && u.confirm_n == 1);
    u.confirming = false;
    // Ctrl+Enter: the cursor's result's folder; outside a search, nothing (every entry is here).
    u.list.sel = 3;
    UiAction a = menu_keys(&u, 1, UK_CONTAINING);
    CHECK(a.kind == UA_CONTAINING && a.index == 3);
    u.focus = UI_FOCUS_SIDE;
    CHECK(menu_keys(&u, 1, UK_CONTAINING).kind == UA_NONE);
    ui_unfocus(&u, UI_FOCUS_SIDE);
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
    u.view = UI_VIEW_FOLDER, u.can_write = true, u.can_paste = true;
    in = ui_input_none(), in.ctx_row = 2;
    ui_handle(&u, names, &in);
    CHECK(!ui_menu_shown(&u, UM_CONTAINING) && ui_menu_shown(&u, UM_SEARCH_IN));
    menu_keys(&u, 1, UK_ESC);
    CHECK(menu_keys(&u, 1, UK_CONTAINING).kind == UA_NONE);
    for (int k = 0; k < COUNT(GREY); k++) CHECK(ui_menu_enabled(&u, GREY[k]));
    CHECK(menu_keys(&u, 1, UK_NEWFOLDER).kind == UA_NEWFOLDER && menu_keys(&u, 1, UK_PASTE).kind == UA_PASTE);
    // The trash view: no Search in Folder… on its folders (they're in the trash).
    u.view = UI_VIEW_TRASH;
    in = ui_input_none(), in.ctx_row = 2;
    ui_handle(&u, names, &in);
    CHECK(!ui_menu_shown(&u, UM_SEARCH_IN));
    menu_keys(&u, 1, UK_ESC);
    // A folder elsewhere (a sidebar row, a path button): Search in Folder…, not on Trash.
    u.view = UI_VIEW_FOLDER;
    u.menu = UI_CTX_FOLDER;
    CHECK(ui_menu_shown(&u, UM_SEARCH_IN) && ui_menu_enabled(&u, UM_SEARCH_IN));
    u.ctx_trash = true;
    CHECK(!ui_menu_shown(&u, UM_SEARCH_IN));
    // Restore, split from them: the trash view's, with the files' cursor.
    u = menu_ui(5);
    u.view = UI_VIEW_TRASH;
    CHECK(ui_menu_enabled(&u, UM_RESTORE));
    u.focus = UI_FOCUS_SIDE;
    CHECK(!ui_menu_enabled(&u, UM_RESTORE));
}

static void test_search_crumbs(void)
{
    UiCrumbs c = { 0 };
    char u[128];
    CHECK(search_uri(u, sizeof u, "/home/m/code", S("x"), STR_EMPTY));
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
    u.mark = mark, u.view = UI_VIEW_SEARCH, u.focus = UI_FOCUS_SEARCH;
    UiInput in = chars("index");
    ui_handle(&u, names, &in);
    CHECK(u.search.len == 5 && u.search.caret == 5 && u.search.anchor == 5);
    u.search_edited = false;
    menu_keys(&u, 1, UK_ALL);
    CHECK(u.marked == 0 && u.search.anchor == 0 && u.search.caret == 5 && !u.search_edited);
    in = chars("ts");
    ui_handle(&u, names, &in);
    CHECK(u.search.len == 2 && memcmp(u.search.b, "ts", 2) == 0 && u.search_edited);
    // Text of the same length pasted over the selection is a change too.
    u.search_edited = false;
    menu_keys(&u, 1, UK_ALL);
    in = keys(1, UK_PASTE);
    in.paste = S("zz");
    ui_handle(&u, names, &in);
    CHECK(memcmp(u.search.b, "zz", 2) == 0 && u.search.len == 2 && u.search_edited);
    menu_keys(&u, 1, UK_ALL);
    in = chars("ts");
    ui_handle(&u, names, &in);
    // Ctrl+V at the caret: the first line, '/' left out.
    u.search_edited = false;
    in = keys(1, UK_PASTE);
    in.paste = S("a/b\nsecond");
    ui_handle(&u, names, &in);
    CHECK(u.search.len == 4 && memcmp(u.search.b, "tsab", 4) == 0 && u.search_edited && (u.focus == UI_FOCUS_SEARCH));
    // Home, Delete; Left, Shift+End, Ctrl+C: the selection copied, the text as it was.
    u.search_edited = false;
    menu_keys(&u, 2, UK_HOME, UK_DELETE);
    CHECK(u.search.len == 3 && memcmp(u.search.b, "sab", 3) == 0 && u.search.caret == 0 && u.search_edited);
    u.search_edited = false;
    menu_keys(&u, 3, UK_RIGHT, UK_SEL_END, UK_COPY);
    CHECK(u.copied && u.copy_len == 2 && memcmp(u.copy, "ab", 2) == 0 && u.search.len == 3 && !u.search_edited);
    u.copied = false;
    // Ctrl+X: copied and gone.
    menu_keys(&u, 1, UK_CUT);
    CHECK(u.copied && u.search.len == 1 && u.search_edited);
    // A move that changes nothing isn't a change; nor is a paste with nothing that fits.
    u.search_edited = false;
    menu_keys(&u, 2, UK_LEFT, UK_END);
    in = keys(1, UK_PASTE);
    in.paste = S("///");
    ui_handle(&u, names, &in);
    CHECK(!u.search_edited && u.search.len == 1);
    // A caret past the text (gui/ set a shorter one) is put back inside it first.
    u.search.caret = u.search.anchor = 40;
    in = chars("z");
    ui_handle(&u, names, &in);
    CHECK(u.search.len == 2 && u.search.b[1] == 'z');
    // A question up takes the keys before the field (Shift+Delete from the results, then n).
    ui_unfocus(&u, UI_FOCUS_SEARCH);
    menu_keys(&u, 1, UK_SHIFT_DELETE);
    CHECK(u.confirming);
    u.focus = UI_FOCUS_SEARCH;
    int len = u.search.len;
    in = chars("n");
    ui_handle(&u, names, &in);
    CHECK(!u.confirming && u.search.len == len);
    // In the results, Ctrl+A is the list's again: every result selected.
    menu_keys(&u, 1, UK_ENTER);
    CHECK(u.focus != UI_FOCUS_SEARCH);
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
    CHECK(u.find.caret == 0 && u.find.anchor == 0);
    UiInput in = chars("gam");
    ui_handle(&u, names, &in);
    isize at = u.list.sel;
    CHECK(u.find.len == 3 && u.find.caret == 3 && !u.find_miss);
    // Ctrl+A selects the text, not the files; Ctrl+V pastes over it (first line, no '/').
    menu_keys(&u, 1, UK_ALL);
    CHECK(u.find.anchor == 0 && u.find.caret == 3 && u.marked == 0 && u.finding && (u.focus == UI_FOCUS_FIND));
    in = keys(1, UK_PASTE);
    in.paste = S("zz/q\nx");
    ui_handle(&u, names, &in);
    CHECK(u.find.len == 3 && memcmp(u.find.b, "zzq", 3) == 0 && u.find_miss);
    // Home, Delete: found again; Left, Shift+End, Ctrl+X: copied and gone, found again.
    menu_keys(&u, 2, UK_HOME, UK_DELETE);
    CHECK(u.find.len == 2 && u.find.caret == 0 && memcmp(u.find.b, "zq", 2) == 0);
    in = keys(1, UK_ALL);
    ui_handle(&u, names, &in);
    in = keys(1, UK_PASTE);
    in.paste = S("gam");
    ui_handle(&u, names, &in);
    CHECK(!u.find_miss && u.list.sel == at);
    menu_keys(&u, 3, UK_LEFT, UK_SEL_END, UK_CUT);
    CHECK(u.copied && u.copy_len == 1 && u.copy[0] == 'm' && u.find.len == 2);
    u.copied = false;
    // A move changes nothing: the cursor stays where the match put it.
    isize sel = u.list.sel;
    menu_keys(&u, 2, UK_HOME, UK_END);
    CHECK(u.list.sel == sel && (u.focus == UI_FOCUS_FIND));
    // Up and Down, with Shift too, step through the matches and keep the keys.
    menu_keys(&u, 1, UK_ALL);
    in = chars("m");   // gamma.txt, BETAMAX, omega
    ui_handle(&u, names, &in);
    isize first = u.list.sel;
    CHECK(ui_find_count(&u, names, &at) == 3);
    menu_keys(&u, 1, UK_SEL_DOWN);
    CHECK((u.focus == UI_FOCUS_FIND) && u.list.sel != first);
    menu_keys(&u, 1, UK_SEL_UP);
    CHECK(u.list.sel == first && (u.focus == UI_FOCUS_FIND));
    // A caret past the text (the field set from outside) is put back inside first.
    u.find.caret = u.find.anchor = 90;
    in = chars("x");
    ui_handle(&u, names, &in);
    CHECK(u.find.len == 2 && u.find.b[1] == 'x');
    // A question takes the keys first.
    u.confirming = true, u.confirm_n = 1;
    in = chars("n");
    ui_handle(&u, names, &in);
    CHECK(!u.confirming && u.find.len == 2);
    ui_matcher_set(&u.find_m, STR_EMPTY, u.find_opts);
}

// Ctrl+C and Ctrl+X in either field with no text selected go to the list: the file found (or the
// result) copied or cut, as before the fields edited (reviewer-2, 2026-10-28); with text selected,
// the text.
static void test_field_copy_fallthrough(void)
{
    UiNames names = { name_at, NULL, NULL };
    Ui u = { 0 };
    u.list.rows = 10;
    ui_set(&u.list, COUNT(find_names), 0);
    for (int k = 0; k < 2; k++) {
        UiKey key = k ? UK_CUT : UK_COPY;
        UiActionKind want = k ? UA_CUT : UA_COPY;
        ui_find_open(&u);
        UiInput in = chars("bet");
        ui_handle(&u, names, &in);
        u.copied = false;
        CHECK(menu_keys(&u, 1, key).kind == want && u.focus != UI_FOCUS_FIND && u.finding && !u.copied && u.find.len == 3);
        ui_find_close(&u);
        ui_find_open(&u);
        in = chars("bet");
        ui_handle(&u, names, &in);
        CHECK(menu_keys(&u, 2, UK_ALL, key).kind == UA_NONE && (u.focus == UI_FOCUS_FIND) && u.copied && u.copy_len == 3);
        ui_find_close(&u);
    }
    ui_matcher_set(&u.find_m, STR_EMPTY, u.find_opts);
    // The search field likewise.
    u = (Ui){ 0 };
    u.list.rows = 10;
    ui_set(&u.list, 3, 0);
    u.view = UI_VIEW_SEARCH, u.focus = UI_FOCUS_SEARCH;
    UiInput in = chars("ab");
    ui_handle(&u, names, &in);
    u.copied = false;
    CHECK(menu_keys(&u, 1, UK_COPY).kind == UA_COPY && u.focus != UI_FOCUS_SEARCH && !u.copied);
    u.focus = UI_FOCUS_SEARCH;
    CHECK(menu_keys(&u, 1, UK_CUT).kind == UA_CUT && u.focus != UI_FOCUS_SEARCH && u.search.len == 2);
    u.focus = UI_FOCUS_SEARCH;
    CHECK(menu_keys(&u, 2, UK_ALL, UK_COPY).kind == UA_NONE && (u.focus == UI_FOCUS_SEARCH) && u.copied && u.copy_len == 2);
}

// A word at a time in a text field (Michael, 2026-10-28): Ctrl+Left and Right move to a word's
// start and end, Ctrl+Shift selects, Ctrl+Backspace and Ctrl+Delete delete; spaces, dots and dashes
// end a word, letters past ASCII don't. Outside a field Ctrl+Left and Right keep their old meanings.
static void test_line_words(void)
{
    Ui u = { 0 };
    char b[64] = "my-file name.tär";
    int len = (int)strlen(b), caret = len, anchor = len;
    UiLine l = { b, &len, &caret, &anchor, 63, false };
    UiEvent wl = { UK_WORD_LEFT, 0 }, wr = { UK_WORD_RIGHT, 0 };
    CHECK(ui_line_key(&u, l, wl, STR_EMPTY) == UI_LINE_MOVED && caret == 13 && anchor == 13);   // tär (ä a word's)
    ui_line_key(&u, l, wl, STR_EMPTY);
    CHECK(caret == 8);   // name
    ui_line_key(&u, l, wl, STR_EMPTY), ui_line_key(&u, l, wl, STR_EMPTY);
    CHECK(caret == 0);
    CHECK(ui_line_key(&u, l, wl, STR_EMPTY) == UI_LINE_MOVED && caret == 0);   // (at the start: stays)
    ui_line_key(&u, l, wr, STR_EMPTY);
    CHECK(caret == 2);   // my|
    ui_line_key(&u, l, wr, STR_EMPTY);
    CHECK(caret == 7);   // file|
    // Ctrl+Shift: the anchor stays.
    ui_line_key(&u, l, (UiEvent){ UK_SEL_WORD_RIGHT, 0 }, STR_EMPTY);
    CHECK(anchor == 7 && caret == 12);
    ui_line_key(&u, l, (UiEvent){ UK_SEL_WORD_LEFT, 0 }, STR_EMPTY), ui_line_key(&u, l, (UiEvent){ UK_SEL_WORD_LEFT, 0 }, STR_EMPTY);
    CHECK(anchor == 7 && caret == 3);
    // Ctrl+Backspace, Ctrl+Delete: a word that way, or the selection.
    caret = anchor = 12;
    CHECK(ui_line_key(&u, l, (UiEvent){ UK_WORD_BACKSPACE, 0 }, STR_EMPTY) == UI_LINE_CHANGED && len == 13 && memcmp(b, "my-file .tär", 13) == 0 && caret == 8);
    ui_line_key(&u, l, (UiEvent){ UK_WORD_DELETE, 0 }, STR_EMPTY);
    CHECK(len == 8 && memcmp(b, "my-file ", 8) == 0 && caret == 8);
    caret = 0, anchor = 1;   // a selection: it goes, not the word
    ui_line_key(&u, l, (UiEvent){ UK_WORD_DELETE, 0 }, STR_EMPTY);
    CHECK(len == 7 && memcmp(b, "y-file ", 7) == 0);
    caret = 6, anchor = 5;
    ui_line_key(&u, l, (UiEvent){ UK_WORD_BACKSPACE, 0 }, STR_EMPTY);
    CHECK(len == 6 && memcmp(b, "y-fil ", 6) == 0);
    caret = 0, anchor = 1;
    ui_line_key(&u, l, (UiEvent){ UK_DELETE, 0 }, STR_EMPTY);
    CHECK(len == 5 && memcmp(b, "-fil ", 5) == 0);
    caret = anchor = 5;
    ui_line_key(&u, l, (UiEvent){ UK_WORD_BACKSPACE, 0 }, STR_EMPTY);
    CHECK(len == 1 && b[0] == '-');
    // In the list: Ctrl+Left is the sidebar's, Ctrl+Right the location line; the rest nothing.
    Ui v = menu_ui(3);
    v.side.n = 3;
    CHECK(menu_keys(&v, 1, UK_WORD_LEFT).kind == UA_NONE && (v.focus == UI_FOCUS_SIDE));
    ui_unfocus(&v, UI_FOCUS_SIDE);
    CHECK(menu_keys(&v, 1, UK_WORD_RIGHT).kind == UA_LOCATION);
    CHECK(menu_keys(&v, 4, UK_SEL_WORD_LEFT, UK_SEL_WORD_RIGHT, UK_WORD_BACKSPACE, UK_WORD_DELETE).kind == UA_NONE && v.marked == 0 && v.list.sel == 0);
    // In the rename line, Ctrl+Right is a word, not the location line's.
    ui_edit_rename(&v, S("a b.txt"), false);
    v.edit.caret = v.edit.anchor = 0;
    CHECK(menu_keys(&v, 1, UK_WORD_RIGHT).kind == UA_NONE && (v.focus == UI_FOCUS_EDIT) && v.edit.caret == 1);
    ui_edit_close(&v);
}

// Which field has the keys, for gui/'s clipboard read: a panel's field first, then the edit line,
// the search field, the find bar; none in the list, a panel's list, a bookmark not being renamed.
static void test_line_active(void)
{
    Ui u = { 0 };
    UiLine l;
    CHECK(!ui_line_active(&u, NULL));
    u.finding = true;
    CHECK(!ui_line_active(&u, NULL));
    u.focus = UI_FOCUS_FIND;
    CHECK(ui_line_active(&u, &l) && l.b == u.find.b);
    u.view = UI_VIEW_SEARCH;
    CHECK(ui_line_active(&u, &l) && l.b == u.find.b);
    u.focus = UI_FOCUS_SEARCH;
    CHECK(ui_line_active(&u, &l) && l.b == u.search.b);
    u.focus = UI_FOCUS_EDIT;
    CHECK(ui_line_active(&u, &l) && l.b == u.edit.b);
    u.keys = true;
    CHECK(ui_line_active(&u, &l) && l.b == u.keys_filter.b);
    // Only the panel on top counts (ui_on_top): Edit Bookmarks over Shortcuts, not naming, has none.
    u.bmp = true;
    CHECK(!ui_line_active(&u, NULL));
    u.bmp_naming = true;
    CHECK(ui_line_active(&u, &l) && l.b == u.bmp_name.b);
    u.ow = true, u.ow_in_list = true;
    CHECK(!ui_line_active(&u, NULL));
    u.ow_in_list = false;
    CHECK(ui_line_active(&u, &l) && l.b == u.ow_filter.b);
    // One focus (2026-10-28): the location line opened from the search view takes the keys from
    // its field, which kept them too before; Esc gives them to the list, not back to the field.
    {
        Ui w = { 0 };
        w.view = UI_VIEW_SEARCH, w.focus = UI_FOCUS_SEARCH;
        ui_location_open(&w, S("/tmp"));
        UiLine wl;
        CHECK(w.focus == UI_FOCUS_EDIT && ui_line_active(&w, &wl) && wl.b == w.edit.b);
        UiInput esc = keys(1, UK_ESC);
        ui_handle(&w, (UiNames){ name_at, NULL, NULL }, &esc);
        CHECK(w.focus == UI_FOCUS_LIST && !ui_line_active(&w, NULL));
    }
    // A click on a row takes the keys from the find bar, which stays open (as GTK's type-ahead
    // goes on a click; reviewer-2, 2026-10-28): Delete then is the list's.
    {
        Ui w = { 0 };
        w.list.n = 3, w.list.rows = 3, w.finding = true, w.focus = UI_FOCUS_FIND;
        UiInput c = ui_input_none();
        c.click = 1;
        ui_handle(&w, (UiNames){ name_at, NULL, NULL }, &c);
        CHECK(w.finding && w.focus == UI_FOCUS_LIST && w.list.sel == 1);
    }
    // ui_line_set: as much as the field holds, the caret at its end, nothing selected.
    {
        Ui w = { 0 };
        w.search.anchor = 1;
        ui_line_set(ui_search_line(&w), S("abc"));
        CHECK(w.search.len == 3 && memcmp(w.search.b, "abc", 3) == 0 && w.search.caret == 3 && w.search.anchor == 3);
        char big[UI_FIND_MAX + 10];
        memset(big, 'x', sizeof big);
        ui_line_set(ui_search_line(&w), (Str){ big, sizeof big });
        CHECK(w.search.len == UI_FIND_MAX && w.search.caret == UI_FIND_MAX);
        ui_line_set(ui_keys_line(&w), (Str){ big, sizeof big });
        CHECK(w.keys_filter.len == (int)sizeof w.keys_filter.b - 1);
    }
    // Find opened again from the list while Select Items Matching's bar is up: a plain find.
    {
        Ui w = { 0 };
        w.list.n = 2, w.list.rows = 2, w.finding = true, w.find_select = true;
        UiInput slash = chars("/");
        ui_handle(&w, (UiNames){ name_at, NULL, NULL }, &slash);
        CHECK(w.finding && w.focus == UI_FOCUS_FIND && !w.find_select);
    }
    // A question over a field: none, so a Ctrl+V reads no clipboard (reviewer-2, 2026-10-28).
    u.ow = u.bmp = u.keys = false;
    CHECK(ui_line_active(&u, &l) && l.b == u.edit.b);
    u.confirming = true;
    CHECK(!ui_line_active(&u, NULL));
    u.confirming = false, u.props = true;
    CHECK(!ui_line_active(&u, NULL));
}

// F9 step 4: Open at Line's editors. The built-in recipes, the user's file (adds, replaces, says its
// first bad line), the default (Omarchy's state file, $EDITOR, nvim), the command filled in, and
// the launch: through uwsm-app, a terminal editor in a terminal.
static void test_editors(void)
{
    char buf[512];
    const char *argv[EDITOR_WORDS + 2];
    const Editor *e = editor_find(NULL, "nvim");
    CHECK(e && e->term && strcmp(e->label, "Neovim") == 0);
    int n = e ? editor_argv(e, "/a b/x.ts", 12, 5, buf, sizeof buf, argv, COUNT(argv)) : -1;
    CHECK(n == 3 && strcmp(argv[0], "nvim") == 0 && strcmp(argv[1], "+call setcursorcharpos(12,5)") == 0 && strcmp(argv[2], "/a b/x.ts") == 0 && !argv[3]);
    e = editor_find(NULL, "code");
    n = e ? editor_argv(e, "/a b/x.ts", 12, 5, buf, sizeof buf, argv, COUNT(argv)) : -1;
    CHECK(e && !e->term && n == 3 && strcmp(argv[1], "--goto") == 0 && strcmp(argv[2], "/a b/x.ts:12:5") == 0);
    CHECK(!editor_find(NULL, "notepad") && !editor_find(NULL, ""));
    // Each built-in runs its own name, takes the file and the line, and has a label.
    for (isize i = 0; i < COUNT(EDITORS); i++) {
        bool file = false, line = false;
        for (int k = 0; EDITORS[i].argv[k]; k++) file |= strstr(EDITORS[i].argv[k], "{file}") != NULL, line |= strstr(EDITORS[i].argv[k], "{line}") != NULL;
        CHECK(strcmp(EDITORS[i].argv[0], EDITORS[i].name) == 0 && file && line && EDITORS[i].label[0]);
    }
    // Too small a buffer, or too few words for the file and the NULL.
    e = editor_find(NULL, "nvim");
    CHECK(editor_argv(e, "/a b/x.ts", 12, 5, buf, 20, argv, COUNT(argv)) == -1);
    CHECK(editor_argv(e, "/a b/x.ts", 12, 5, buf, sizeof buf, argv, 3) == -1);

    // The user's file: comments and blank lines skipped; a word in quotes; ~/ the home folder; a
    // recipe without {file} gets it last; one by a built-in's name replaces it; the first bad line said.
    env_set("HOME", "/home/t");
    Editors u;
    editors_parse(&ta, S("# mine\n\n  mine no ~/bin/ed \"--at {line}:{col}\" {file}\nnvim yes nvim +{line}\nbad maybe x y\nshort yes\nq no \"open\nok yes ok {file}\nnocmd no\n"), &u);
    CHECK(u.n == 3 && u.bad == 5);
    e = editor_find(&u, "mine");
    n = e ? editor_argv(e, "/x.ts", 7, 2, buf, sizeof buf, argv, COUNT(argv)) : -1;
    CHECK(e && !e->term && n == 3 && strcmp(argv[0], "/home/t/bin/ed") == 0 && strcmp(argv[1], "--at 7:2") == 0 && strcmp(argv[2], "/x.ts") == 0);
    CHECK(e && strcmp(e->label, "mine") == 0);
    e = editor_find(&u, "nvim");
    n = e ? editor_argv(e, "/x.ts", 7, 2, buf, sizeof buf, argv, COUNT(argv)) : -1;
    CHECK(e && e->term && n == 3 && strcmp(argv[1], "+7") == 0 && strcmp(argv[2], "/x.ts") == 0);
    CHECK(editor_find(&u, "ok") && editor_find(&u, "ok")->term && !editor_find(&u, "bad") && !editor_find(&u, "short") && !editor_find(&u, "q") && !editor_find(&u, "nocmd"));
    editors_parse(&ta, STR_EMPTY, &u);
    CHECK(u.n == 0 && u.bad == 0);
    char many[2048] = "x no";
    for (int i = 0; i < EDITOR_WORDS + 2; i++) strcat(many, " w");
    editors_parse(&ta, str_from_c(many), &u);
    CHECK(u.n == 0 && u.bad == 1);   // more words than a recipe holds
    char path[PATH_MAX];
    env_set("XDG_CONFIG_HOME", at("cfg94"));
    CHECK(editors_path(path, sizeof path) && strcmp(path, at("cfg94/ofx/editors")) == 0);

    // The default: Omarchy's state file, then $EDITOR's program, then nvim.
    char name[EDITOR_NAME_MAX];
    env_set("XDG_STATE_HOME", at("st94"));
    put(at("st94/omarchy/defaults/editor"), "  zeditor  \nmore\n");
    env_set("EDITOR", "/usr/bin/vim -p");
    editor_default(&ta, name, sizeof name);
    CHECK(strcmp(name, "zeditor") == 0);
    unlink(at("st94/omarchy/defaults/editor"));
    editor_default(&ta, name, sizeof name);
    CHECK(strcmp(name, "vim") == 0);
    env_set("EDITOR", "");
    editor_default(&ta, name, sizeof name);
    CHECK(strcmp(name, "nvim") == 0);

    // The launch: through uwsm-app; nvim in a terminal as omarchy-launch-tui runs it; code as it is;
    // an editor that isn't there says so.
    char out[2048];
    Err err;
    mkdirs(at("bin94"));
    fake(at("bin94"), "uwsm-app", at("spawn94.log"));
    fake(at("bin94"), "nvim", at("nvim94.log"));
    fake(at("bin94"), "code", at("code94.log"));
    env_set("PATH", at("bin94"));
    e = editor_find(NULL, "nvim");
    n = editor_argv(e, "/a b/x.ts", 12, 5, buf, sizeof buf, argv, COUNT(argv));
    CHECK(editor_open(e, argv, n, &err));
    CHECK(wait_file(at("spawn94.log"), out, sizeof out) &&
          strncmp(out, "uwsm-app -- xdg-terminal-exec --app-id=org.omarchy.nvim -e nvim +call setcursorcharpos(12,5) /a b/x.ts sid=", 106) == 0);
    unlink(at("spawn94.log"));
    e = editor_find(NULL, "code");
    n = editor_argv(e, "/a b/x.ts", 12, 5, buf, sizeof buf, argv, COUNT(argv));
    CHECK(editor_open(e, argv, n, &err));
    CHECK(wait_file(at("spawn94.log"), out, sizeof out) && strncmp(out, "uwsm-app -- code --goto /a b/x.ts:12:5 sid=", 43) == 0);
    unlink(at("spawn94.log"));
    e = editor_find(NULL, "helix");
    n = editor_argv(e, "/x.ts", 1, 1, buf, sizeof buf, argv, COUNT(argv));
    CHECK(!editor_open(e, argv, n, &err) && strcmp(err.msg, "Helix isn't installed (helix)") == 0 && access(at("spawn94.log"), F_OK) != 0);
    // A program by its path: there or not.
    put(at("bin94/ed"), "#!/bin/sh\n"), chmod(at("bin94/ed"), 0755);
    editors_parse(&ta, str_from_c(arena_sprintf(&ta, "p no %s {file}\nq no %s/none {file}\n", at("bin94/ed"), at("bin94"))), &u);
    n = editor_argv(editor_find(&u, "p"), "/x.ts", 1, 1, buf, sizeof buf, argv, COUNT(argv));
    CHECK(editor_open(editor_find(&u, "p"), argv, n, &err));
    CHECK(wait_file(at("spawn94.log"), out, sizeof out) && strstr(out, "bin94/ed /x.ts sid="));
    n = editor_argv(editor_find(&u, "q"), "/x.ts", 1, 1, buf, sizeof buf, argv, COUNT(argv));
    CHECK(!editor_open(editor_find(&u, "q"), argv, n, &err) && strstr(err.msg, "isn't installed"));
    env_restore();
}

// Open at Line's key and menu entry (F9 step 4): Ctrl+Shift+Enter and the entry on a result found
// inside files (sel_hit, gui/'s); not in a folder, nor from the sidebar or an empty list.
static void test_open_line_keys(void)
{
    static u8 mark[5];
    UiNames names = { name_at, NULL, dir_at };
    Ui u = menu_ui(5);
    u.mark = mark, u.view = UI_VIEW_SEARCH, u.list.sel = 3;
    UiAction a = menu_keys(&u, 1, UK_OPEN_LINE);
    CHECK(a.kind == UA_OPEN_LINE && a.index == 3);
    u.focus = UI_FOCUS_SIDE;
    CHECK(menu_keys(&u, 1, UK_OPEN_LINE).kind != UA_OPEN_LINE);
    u.focus = UI_FOCUS_LIST, u.view = UI_VIEW_FOLDER;
    CHECK(menu_keys(&u, 1, UK_OPEN_LINE).kind == UA_NONE);
    u.view = UI_VIEW_SEARCH, u.list.n = 0;
    CHECK(menu_keys(&u, 1, UK_OPEN_LINE).kind == UA_NONE);
    u.list.n = 5;
    // The entry: on one result with a line; picking it opens that one.
    UiInput in = ui_input_none();
    in.ctx_row = 1;
    ui_handle(&u, names, &in);
    CHECK(u.menu_on && u.menu == UI_CTX_ENTRY && u.ctx_one && !ui_menu_shown(&u, UM_OPEN_LINE));
    u.sel_hit = true;
    CHECK(ui_menu_shown(&u, UM_OPEN_LINE) && ui_menu_enabled(&u, UM_OPEN_LINE));
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_ENTRY, UM_OPEN_LINE);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_OPEN_LINE && a.index == 1 && !u.menu_on);
    // Several marked: not one result, so no line.
    ui_mark(&u, 0, true), ui_mark(&u, 2, true);
    in = ui_input_none(), in.ctx_row = 0;
    ui_handle(&u, names, &in);
    CHECK(u.menu_on && !u.ctx_one && !ui_menu_shown(&u, UM_OPEN_LINE));
}
