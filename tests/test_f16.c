// F16 step 2: a far folder's lister (fs/farlist.c), what it says and how ofx reads it back; the tree
// leaving a far drive's folders unread.

// What far_serve says about dir, with `questions` on its input, into out (a static buffer).
static Str far_said_of(const char *dir, bool hidden, const char *questions, isize asks_len)
{
    static char *buf;
    static size_t len;
    free(buf), buf = NULL, len = 0;
    FILE *out = open_memstream(&buf, &len);
    FILE *in = fmemopen((void *)(asks_len ? questions : " "), asks_len ? (size_t)asks_len : 1, "r");
    if (!asks_len) fgetc(in);   // (an empty input: at its end at once)
    static u8 mem[KiB(64)];
    Arena a;
    arena_init(&a, "far_serve", mem, sizeof mem);
    far_serve(in, out, &a, dir, hidden);
    fclose(in), fclose(out);
    return (Str){ buf, (isize)len };
}

// The listing as ofx-list says it, against fs_list's of the same folder, field by field.
static void test_far_serve(void)
{
    arena_reset(&ta);
    mkdirs(at("far/d/sub/inner"));
    put(at("far/d/a.txt"), "hello");
    put(at("far/d/sub/x"), "x");
    put(at("far/d/.dot"), "d");
    put(at("far/d/line\nbreak"), "n");
    CHECK(symlink("sub", at("far/d/ln")) == 0);
    CHECK(symlink("nowhere", at("far/d/broken")) == 0);
    CHECK(chmod(at("far/d/a.txt"), 0640) == 0);
    for (int hidden = 0; hidden < 2; hidden++) {
        Str b = far_said_of(at("far/d"), hidden, "", 0);
        Listing got, want;
        bool w, ended;
        isize after;
        Err err;
        CHECK(far_listing(&ta, b, at("far/d"), &got, &w, &after, &ended, &err) && ended && w && after == b.len);
        CHECK(fs_list(&ta, at("far/d"), hidden, (Sort){ SORT_NAME, false }, &want, &err));
        fs_sort(&got, (Sort){ SORT_NAME, false });
        CHECK(got.n == want.n && got.n == 5 + hidden && got.hidden == 1 && want.hidden == 1);
        for (isize i = 0; i < got.n && i < want.n; i++) {
            const Entry *g = &got.e[i], *x = &want.e[i];
            CHECK(str_eq(g->name, x->name) && g->kind == x->kind && g->size == x->size && g->mtime == x->mtime && g->mode == x->mode &&
                  g->uid == x->uid && g->gid == x->gid && str_eq(g->target, x->target));
        }
    }
    // The counts asked, each answered as count_entries would; a name with '/', or one not there, -1.
    char questions[512];
    isize n = far_count_ask(questions, sizeof questions, 7, 2, S("sub"));
    n += far_count_ask(questions + n, sizeof questions - (size_t)n, 7, 4, S("line\nbreak"));
    n += far_count_ask(questions + n, sizeof questions - (size_t)n, 8, 0, S("gone"));
    Str b = far_said_of(at("far/d"), false, questions, n);
    Listing l;
    bool w, ended;
    isize after;
    Err err;
    CHECK(far_listing(&ta, b, at("far/d"), &l, &w, &after, &ended, &err) && ended);
    FarMsg m;
    i64 want[][3] = { { 7, 2, 2 }, { 7, 4, -1 }, { 8, 0, -1 } };
    for (int i = 0; i < 3; i++) CHECK(far_next(b, &after, &m) == 1 && m.type == 'C' && m.v[0] == want[i][0] && m.v[1] == want[i][1] && m.v[2] == want[i][2]);
    CHECK(after == b.len);
    // A question it can't take: a NUL or a '/' in the name (-1, not sub's count), and the count
    // without dot-files unless hidden.
    put(at("far/d/sub/.h"), "h");
    n = far_count_ask(questions, sizeof questions, 1, 0, (Str){ "sub\0x", 5 });
    n += far_count_ask(questions + n, sizeof questions - (size_t)n, 1, 1, S("sub/inner"));
    n += far_count_ask(questions + n, sizeof questions - (size_t)n, 1, 2, S("sub"));
    for (int hidden = 0; hidden < 2; hidden++) {
        b = far_said_of(at("far/d"), hidden, questions, n);
        CHECK(far_listing(&ta, b, at("far/d"), &l, &w, &after, &ended, &err) && ended);
        i64 got[][3] = { { 1, 0, -1 }, { 1, 1, -1 }, { 1, 2, 2 + hidden } };
        for (int i = 0; i < 3; i++) CHECK(far_next(b, &after, &m) == 1 && m.type == 'C' && m.v[1] == got[i][1] && m.v[2] == got[i][2]);
        CHECK(after == b.len);
    }
    // A question with more on its line, or cut short at the input's end: not answered, and none
    // after it.
    const char *cut[] = { "C 1 2 3 9\nsubC 1 3 3\nsub", "C 1 2 5\nab" };
    for (int i = 0; i < COUNT(cut); i++) {
        b = far_said_of(at("far/d"), false, cut[i], (isize)strlen(cut[i]));
        CHECK(far_listing(&ta, b, at("far/d"), &l, &w, &after, &ended, &err) && after == b.len);
    }
    // A malformed question ends it: nothing more is answered.
    b = far_said_of(at("far/d"), false, "C 1 2 x\nsub", 11);
    CHECK(far_listing(&ta, b, at("far/d"), &l, &w, &after, &ended, &err) && after == b.len);
    // A folder it can't read: X with the reason, and far_listing says it.
    b = far_said_of(at("far/none"), false, "", 0);
    CHECK(str_eq(b, S("X No such file or directory\n")));
    CHECK(!far_listing(&ta, b, at("far/none"), &l, &w, &after, &ended, &err) && ended);
    CHECK(strstr(err.msg, "far/none: No such file or directory") != NULL);
    // A folder that can't be written: W 0.
    CHECK(chmod(at("far/d/sub"), 0555) == 0);
    b = far_said_of(at("far/d/sub"), false, "", 0);
    CHECK(far_listing(&ta, b, at("far/d/sub"), &l, &w, &after, &ended, &err) && !w && l.n == 2);
    CHECK(chmod(at("far/d/sub"), 0755) == 0);
}

// A folder of 300: past the first array's 256, every entry kept.
static void test_far_many(void)
{
    arena_reset(&ta);
    mkdirs(at("farmany"));
    for (int i = 0; i < 300; i++) put(at("farmany/f%03d", i), "x");
    Str b = far_said_of(at("farmany"), false, "", 0);
    Listing l;
    bool w, ended;
    isize after;
    Err err;
    CHECK(far_listing(&ta, b, at("farmany"), &l, &w, &after, &ended, &err) && l.n == 300);
    fs_sort(&l, (Sort){ SORT_NAME, false });
    bool all = l.n == 300;
    for (int i = 0; all && i < 300; i++) all = str_eq(l.e[i].name, str_from_c(arena_sprintf(&ta, "f%03d", i)));
    CHECK(all);
}

// Each message parsed, a part come and a malformed one told apart.
static void test_far_next(void)
{
    FarMsg m;
    isize at = 0;
    Str ok = S("W 1\nE 1 0 5 16877 1 2 3 0\nsubH 2\nL\nC 3 4 -1\nX gone away\n");
    CHECK(far_next(ok, &at, &m) == 1 && m.type == 'W' && m.v[0] == 1);
    CHECK(far_next(ok, &at, &m) == 1 && m.type == 'E' && m.v[0] == K_DIR && m.v[2] == 5 && m.v[3] == 16877 && m.v[4] == 1 && m.v[5] == 2 &&
          str_eq(m.name, S("sub")) && m.target.len == 0);
    CHECK(far_next(ok, &at, &m) == 1 && m.type == 'H' && m.v[0] == 2);
    CHECK(far_next(ok, &at, &m) == 1 && m.type == 'L');
    CHECK(far_next(ok, &at, &m) == 1 && m.type == 'C' && m.v[0] == 3 && m.v[1] == 4 && m.v[2] == -1);
    CHECK(far_next(ok, &at, &m) == 1 && m.type == 'X' && str_eq(m.text, S("gone away")));
    CHECK(at == ok.len && far_next(ok, &at, &m) == 0);
    // Not all come yet: 0, and *at kept.
    const char *part[] = { "W 1", "E 0 1 2 3 4 5 3 2\nab", "E 0 1 2 3 4 5 3 2\nabcd" };
    for (int i = 0; i < COUNT(part); i++) {
        at = 0;
        CHECK(far_next(str_from_c(part[i]), &at, &m) == 0 && at == 0);
    }
    // Malformed: -1.
    const char *bad[] = {
        "W 2\n", "W\n", "W 1 1\n", "W x\n", "W_1\n", "W  1\n", "W +1\n", "Q 1\n", "\n", "L 1\n", "X\n", "X \n", "H -1\n",
        "C -1 0 1\n", "C 1 -1 1\n", "C 1 2\n",
        "E 6 0 0 0 0 0 1 0\na",                 // a kind past K_OTHER
        "E 0 0 0 0 0 0 0 0\n",                  // no name
        "E 0 0 0 0 0 0 256 0\n",                // a name past NAME_MAX
        "E 0 0 0 0 0 0 1 4096\na",              // a target past PATH_MAX - 1
        "E 0 0 0 4294967296 0 0 1 0\na",        // a mode past 32 bits
        "E 0 0 0 0 0 0 3 0\na/b",               // a '/' in a name
        "E 0 0 0 0 0 0 1\na",                   // a number missing
        "E 0 -1 0 0 0 0 1 0\na",                // a negative size
    };
    for (int i = 0; i < COUNT(bad); i++) {
        at = 0;
        int r = far_next(str_from_c(bad[i]), &at, &m);
        if (r != -1) fprintf(stderr, "far_next took: %s\n", bad[i]);
        CHECK(r == -1);
    }
    at = 0;
    CHECK(far_next((Str){ "E 0 0 0 0 0 0 1 0\n\0", 19 }, &at, &m) == -1);   // a NUL in a name
    // A line that never ends, past FAR_LINE_MAX: malformed, not waited for.
    static char line[FAR_LINE_MAX + 8];
    memset(line, '9', sizeof line);
    line[0] = 'H', line[1] = ' ';
    at = 0;
    CHECK(far_next((Str){ line, FAR_LINE_MAX - 1 }, &at, &m) == 0);
    CHECK(far_next((Str){ line, FAR_LINE_MAX }, &at, &m) == -1);
    // far_listing: before its L, nothing; a malformed one before it, the reason.
    Listing l;
    bool w, ended;
    isize after;
    Err err;
    arena_reset(&ta);
    CHECK(!far_listing(&ta, S("W 1\nE 0 0 0 0 0 0 1 0\na"), "/x", &l, &w, &after, &ended, &err) && !ended);
    CHECK(!far_listing(&ta, S("W 1\nW 3\nL\n"), "/x", &l, &w, &after, &ended, &err) && ended && strstr(err.msg, "malformed"));
    CHECK(far_listing(&ta, S("E 2 9 1 33188 5 7 1 3\nkabcH 0\nL\nC 1"), "/x", &l, &w, &after, &ended, &err) && ended && !w);
    CHECK(l.n == 1 && l.e[0].kind == K_LINK_FILE && l.e[0].size == 9 && str_eq(l.e[0].name, S("k")) && str_eq(l.e[0].target, S("abc")) && after == 32);
    CHECK(l.e[0].mode == 33188 && l.e[0].uid == 5 && l.e[0].gid == 7 && l.e[0].mtime == 1);
    // A count's question: a name past NAME_MAX, or none, isn't asked.
    char buf[NAME_MAX + 64];
    static char longname[NAME_MAX + 2];
    memset(longname, 'n', NAME_MAX + 1);
    CHECK(far_count_ask(buf, sizeof buf, 1, 2, (Str){ longname, NAME_MAX + 1 }) == 0);
    CHECK(far_count_ask(buf, sizeof buf, 1, 2, STR_EMPTY) == 0);
    CHECK(far_count_ask(buf, sizeof buf, 1, 2, (Str){ longname, NAME_MAX }) == 10 + NAME_MAX);
    CHECK(far_count_ask(buf, 9, 1, 2, S("ab")) == 0 && far_count_ask(buf, 10, 1, 2, S("ab")) == 10 && memcmp(buf, "C 1 2 2\nab", 10) == 0);
}

// The tree with a far drive's folder open: shown, its subfolders not read.
static void test_tree_far(void)
{
    arena_reset(&ta);
    static Tree t;
    t = (Tree){ 0 };
    mkdirs(at("treefar/top/share/inside"));
    mkdirs(at("treefar/top/local/inside"));
    Str top = str_from_c(arena_cstr(&ta, str_from_c(at("treefar/top"))));
    tree_follow(&t, at("treefar/top/share/inside"));
    tree_follow(&t, at("treefar/top/local/inside"));
    t.mounts = str_from_c(arena_sprintf(&ta, "1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw - fuse.gvfsd-fuse gvfsd-fuse rw\n", at("treefar/top/share")));
    tree_build(&ta, &t, top, false);
    CHECK(t.n == 4 && str_eq(t.row[1].name, S("local")) && str_eq(t.row[2].name, S("inside")) && str_eq(t.row[3].name, S("share")) && t.row[3].open);
    t.mounts = STR_EMPTY;   // (none: everything read, as before F16)
    tree_build(&ta, &t, top, false);
    CHECK(t.n == 5 && str_eq(t.row[4].name, S("inside")));
}

// The footer while a far folder's listing comes: Opening, not "0 items".
static void test_far_footer(void)
{
    char buf[128];
    UiInfo in = { .n = 0, .avail = -1, .one_count = -1, .one_size = -1, .opening = true };
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "Opening\xE2\x80\xA6") == 0);
    in.opening = false;
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "0 items") == 0);
}

// spawn_fds: the child reads its input from the fd given (ofx-list's questions), not /dev/null.
static void test_spawn_fds_in(void)
{
    ops_guard(root);
    char prog[PATH_MAX];
    snprintf(prog, sizeof prog, "%s", at("f16/bin/echoin"));
    mkdirs(at("f16/bin"));
    put_exec(prog, "#!/bin/sh\nread -r line\necho \"got $line\"\n");
    int in[2], out[2];
    CHECK(pipe2(in, O_CLOEXEC) == 0 && pipe2(out, O_CLOEXEC) == 0);
    char *argv[] = { prog, NULL };
    pid_t pid;
    Err err;
    CHECK(spawn_fds(NULL, argv, in[0], out[1], false, &pid, &err));
    close(in[0]), close(out[1]);
    CHECK(write(in[1], "hello\n", 6) == 6);
    close(in[1]);
    char buf[64] = "";
    isize n = 0;
    for (ssize_t k; (k = read(out[0], buf + n, sizeof buf - 1 - (size_t)n)) > 0;) n += k;
    close(out[0]);
    buf[n] = 0;
    CHECK(strcmp(buf, "got hello\n") == 0);
}
