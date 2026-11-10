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
    // A folder it can't read: X with the errno and the reason, and far_listing says it.
    b = far_said_of(at("far/none"), false, "", 0);
    CHECK(str_eq(b, S("X 2: No such file or directory\n")));
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
    Str ok = S("W 1\nE 1 0 5 16877 1 2 3 0\nsubH 2\nL\nC 3 4 -1\nX 20: gone away\n");
    CHECK(far_next(ok, &at, &m) == 1 && m.type == 'W' && m.v[0] == 1);
    CHECK(far_next(ok, &at, &m) == 1 && m.type == 'E' && m.v[0] == K_DIR && m.v[2] == 5 && m.v[3] == 16877 && m.v[4] == 1 && m.v[5] == 2 &&
          str_eq(m.name, S("sub")) && m.target.len == 0);
    CHECK(far_next(ok, &at, &m) == 1 && m.type == 'H' && m.v[0] == 2);
    CHECK(far_next(ok, &at, &m) == 1 && m.type == 'L');
    CHECK(far_next(ok, &at, &m) == 1 && m.type == 'C' && m.v[0] == 3 && m.v[1] == 4 && m.v[2] == -1);
    CHECK(far_next(ok, &at, &m) == 1 && m.type == 'X' && m.v[0] == ENOTDIR && str_eq(m.text, S("gone away")));
    CHECK(at == ok.len && far_next(ok, &at, &m) == 0);
    // A file from before 1970: its mtime negative, the rest as any (the F16 code-health review).
    Str old = S("E 0 3 -86400 33188 1 2 1 0\na");
    at = 0;
    CHECK(far_next(old, &at, &m) == 1 && m.type == 'E' && m.v[1] == 3 && m.v[2] == -86400 && m.v[3] == 33188 && str_eq(m.name, S("a")));
    // Not all come yet: 0, and *at kept.
    const char *part[] = { "W 1", "E 0 1 2 3 4 5 3 2\nab", "E 0 1 2 3 4 5 3 2\nabcd" };
    for (int i = 0; i < COUNT(part); i++) {
        at = 0;
        CHECK(far_next(str_from_c(part[i]), &at, &m) == 0 && at == 0);
    }
    // Malformed: -1.
    const char *bad[] = {
        "W 2\n", "W\n", "W 1 1\n", "W x\n", "W_1\n", "W  1\n", "W +1\n", "Q 1\n", "\n", "L 1\n", "X\n", "X \n", "X 2\n", "X 2:\n", "X 2: \n", "X 2 y\n", "X 2:y\n", "X 2:yz\n", "X x: y\n", "X 0: y\n", "X 4096: y\n", "X -2: y\n", "X  2: y\n", "H -1\n",
        "C -1 0 1\n", "C 1 -1 1\n", "C 1 2\n",
        "E 6 0 0 0 0 0 1 0\na",                 // a kind past K_OTHER
        "E 0 0 0 0 0 0 0 0\n",                  // no name
        "E 0 0 0 0 0 0 256 0\n",                // a name past NAME_MAX
        "E 0 0 0 0 0 0 1 4096\na",              // a target past PATH_MAX - 1
        "E 0 0 0 4294967296 0 0 1 0\na",        // a mode past 32 bits
        "E 0 0 0 0 0 0 3 0\na/b",               // a '/' in a name
        "E 0 0 0 0 0 0 1\na",                   // a number missing
        "E 0 -1 0 0 0 0 1 0\na",                // a negative size
        "E -1 0 0 0 0 0 1 0\na", "E 0 0 0 -1 0 0 1 0\na", "E 0 0 0 0 -1 0 1 0\na", "E 0 0 0 0 0 -1 1 0\na",   // the others negative
        "E 0 0 0 0 0 0 -1 0\na", "E 0 0 0 0 0 0 1 -1\na",
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
    t.mounts = mounts_parse(&ta, str_from_c(arena_sprintf(&ta, "1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw - fuse.gvfsd-fuse gvfsd-fuse rw\n", at("treefar/top/share"))));
    tree_build(&ta, &t, top, false);
    CHECK(t.n == 4 && str_eq(t.row[1].name, S("local")) && str_eq(t.row[2].name, S("inside")) && str_eq(t.row[3].name, S("share")) && t.row[3].open);
    t.mounts = (Mounts){ 0 };   // (none: everything read, as before F16)
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
    CHECK(spawn_fds(NULL, argv, in[0], out[1], SPAWN_ERR_OURS, &pid, &err));
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

// spawn_fds' errors: /dev/null when asked (a lister that outlives ofx holds none of its fds), ofx's
// own when asked, or the fd given.
static void test_spawn_fds_errs(void)
{
    ops_guard(root);
    char prog[PATH_MAX];
    snprintf(prog, sizeof prog, "%s", at("f16/bin/errto"));
    mkdirs(at("f16/bin"));
    put_exec(prog, "#!/bin/sh\nreadlink /proc/self/fd/2\n");
    int want[] = { SPAWN_ERR_NULL, SPAWN_ERR_OURS, 1 };
    for (int i = 0; i < COUNT(want); i++) {
        int out[2];
        CHECK(pipe2(out, O_CLOEXEC) == 0);
        char *argv[] = { prog, NULL };
        pid_t pid;
        Err err;
        CHECK(spawn_fds(NULL, argv, -1, out[1], want[i] == 1 ? out[1] : want[i], &pid, &err));
        close(out[1]);
        char buf[PATH_MAX] = "";
        isize n = 0;
        for (ssize_t k; (k = read(out[0], buf + n, sizeof buf - 1 - (size_t)n)) > 0;) n += k;
        close(out[0]);
        buf[n] = 0;
        char ours[PATH_MAX] = "";
        ssize_t k = readlink("/proc/self/fd/2", ours, sizeof ours - 1);
        if (k > 0) ours[k] = 0;
        if (i == 0) CHECK(strcmp(buf, "/dev/null\n") == 0);
        if (i == 1) CHECK(strncmp(buf, ours, strlen(ours)) == 0 && buf[strlen(ours)] == '\n');
        if (i == 2) CHECK(strncmp(buf, "pipe:", 5) == 0);
    }
}

// A link's target by its text alone: ".", "..", repeated slashes; ".." at / stays at /.
static void test_link_abs(void)
{
    struct { const char *dir, *target, *want; } c[] = {
        { "/home/m", "nas", "/home/m/nas" },
        { "/home/m", "../x/./y//z", "/home/x/y/z" },
        { "/home/m", "/run/user/1/gvfs/s", "/run/user/1/gvfs/s" },
        { "/home/m", "../../../..", "/" },
        { "/", "a/..", "/" },
        { "/a", "b/../../c/", "/c" },
    };
    for (int i = 0; i < COUNT(c); i++) {
        char out[PATH_MAX];
        CHECK(link_abs(c[i].dir, str_from_c(c[i].target), out, sizeof out));
        if (strcmp(out, c[i].want) != 0) fprintf(stderr, "link_abs %s + %s: %s\n", c[i].dir, c[i].target, out);
        CHECK(strcmp(out, c[i].want) == 0);
    }
    char small[6];
    CHECK(!link_abs("/home", S("m"), small, sizeof small) && link_abs("/ho", S("m"), small, sizeof small) && strcmp(small, "/ho/m") == 0);
    CHECK(!link_abs("/ho", S("mm"), small, sizeof small));   // (6 characters: no room for its end)
}

// A local folder with far links and a far mount point (by the mount list): taken for folders, never
// looked at. The proof: a link to a far *file*, and a "mount point" that is a file, both listed as
// folders; a link to a local file stays one.
static void test_fs_list_far(void)
{
    arena_reset(&ta);
    mkdirs(at("flf/share/d"));
    put(at("flf/share/f.txt"), "f");
    mkdirs(at("flf/home"));
    put(at("flf/home/mp"), "a file standing in for a mount point");
    put(at("flf/home/local.txt"), "l");
    CHECK(symlink(at("flf/share/f.txt"), at("flf/home/tofile")) == 0);
    CHECK(symlink("../share", at("flf/home/nas")) == 0);
    CHECK(symlink("local.txt", at("flf/home/ll")) == 0);
    Mounts mounts = mounts_parse(&ta, str_from_c(arena_sprintf(&ta, "1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw - fuse.gvfsd-fuse g rw\n3 1 0:3 / %s rw - nfs4 h:/x rw\n",
                                          at("flf/share"), at("flf/home/mp"))));
    Listing l;
    Err err;
    CHECK(fs_list_far(&ta, at("flf/home"), false, (Sort){ SORT_NAME, false }, &mounts, &l, &err) && l.n == 5);
    struct { const char *name; Kind kind; bool far; } want[] = {
        { "mp", K_DIR, true }, { "nas", K_LINK_DIR, true }, { "tofile", K_LINK_DIR, true }, { "ll", K_LINK_FILE, false }, { "local.txt", K_FILE, false },
    };
    for (int i = 0; i < COUNT(want); i++) {
        isize k = listing_find(&l, str_from_c(want[i].name));
        CHECK(k >= 0 && l.e[k].kind == want[i].kind && l.e[k].far == want[i].far);
    }
    isize k = listing_find(&l, S("nas"));
    CHECK(k >= 0 && str_eq(l.e[k].target, S("../share")) && l.e[k].size == 0);
    // The folder reached through a local link: its entries' ".." go up from where it really is
    // (step 2b review). flf/proj is a link to flf/real/proj, whose nas is ../nas: really
    // flf/real/nas (far, a file here: not looked at), not flf/nas by the path's text.
    mkdirs(at("flf/real/proj"));
    put(at("flf/real/nas"), "a file standing in for a share's root");
    CHECK(symlink("../nas", at("flf/real/proj/nas")) == 0);
    CHECK(symlink("real/proj", at("flf/proj")) == 0);
    Mounts m2 = mounts_parse(&ta, str_from_c(arena_sprintf(&ta, "1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw - fuse.sshfs s rw\n", at("flf/real/nas"))));
    CHECK(fs_list_far(&ta, at("flf/proj"), false, (Sort){ SORT_NAME, false }, &m2, &l, &err) && l.n == 1);
    CHECK(l.e[0].kind == K_LINK_DIR && l.e[0].far);
    // The tree from the same linked folder: nasf (a far "file") still a folder, nasd (a far folder
    // with a child) open but not read; both judged from flf/real/proj, not the path's text.
    put(at("flf/real/nasf"), "f");
    mkdirs(at("flf/real/nasd/c"));
    CHECK(symlink("../nasf", at("flf/real/proj/nasf")) == 0 && symlink("../nasd", at("flf/real/proj/nasd")) == 0);
    Mounts m3 = mounts_parse(&ta, str_from_c(arena_sprintf(&ta, "1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw - fuse.sshfs s rw\n3 1 0:3 / %s rw - fuse.sshfs s rw\n",
                                      at("flf/real/nasf"), at("flf/real/nasd"))));
    static Tree tp;
    tp = (Tree){ 0 };
    tree_follow(&tp, at("flf/proj/nasd/c"));
    tp.mounts = m3;
    tree_build(&ta, &tp, str_from_c(arena_cstr(&ta, str_from_c(at("flf/proj")))), false);
    CHECK(tp.n == 3 && str_eq(tp.row[1].name, S("nasd")) && tp.row[1].open && str_eq(tp.row[2].name, S("nasf")));
    // Without the mount list (fs_list): followed as before.
    CHECK(fs_list(&ta, at("flf/home"), false, (Sort){ SORT_NAME, false }, &l, &err));
    k = listing_find(&l, S("tofile"));
    CHECK(k >= 0 && l.e[k].kind == K_LINK_FILE && !l.e[k].far);
    // The tree: nas is a folder, shown, but not read even when open.
    static Tree t;
    t = (Tree){ 0 };
    mkdirs(at("flf/share/d/inner"));
    tree_follow(&t, at("flf/home/nas/d"));
    t.mounts = mounts;
    tree_build(&ta, &t, str_from_c(arena_cstr(&ta, str_from_c(at("flf/home")))), false);
    bool found = false;
    for (int i = 0; i < t.n; i++) found |= str_eq(t.row[i].name, S("nas"));
    CHECK(found && t.n == 3 && t.row[1].open && str_eq(t.row[2].name, S("tofile")));   // (home, nas open, tofile: nothing below nas; mp, a file here, has no folder d_type)
    t.mounts = (Mounts){ 0 };
    tree_build(&ta, &t, str_from_c(arena_cstr(&ta, str_from_c(at("flf/home")))), false);
    CHECK(t.n == 3 && str_eq(t.row[2].name, S("d")) && t.row[2].depth == 2);   // (nas read: d below it; tofile followed to a file, gone)
}

// far_real (step 3): realpath that never goes past a far drive. Each local part looked at and its
// links followed, a chain through a local link into a share caught; past the share's mount, the
// rest by its text. The proof it isn't looked at: "share" here is a plain folder named in the
// mount list, and paths below it that don't exist still resolve.
static void test_far_real(void)
{
    arena_reset(&ta);
    mkdirs(at("fr/share/d"));
    mkdirs(at("fr/home"));
    mkdirs(at("fr/links"));
    put(at("fr/home/x.txt"), "x");
    CHECK(symlink("../share", at("fr/home/nas")) == 0);
    CHECK(symlink("../links", at("fr/home/mylinks")) == 0);   // (a chain: mylinks/nas, then ../share)
    CHECK(symlink("../share", at("fr/links/nas")) == 0);
    CHECK(symlink("loop", at("fr/home/loop")) == 0);
    CHECK(symlink(at("fr/home"), at("fr/abs")) == 0);
    Mounts mounts = mounts_parse(&ta, str_from_c(arena_sprintf(&ta, "1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw - fuse.gvfsd-fuse g rw\n", at("fr/share"))));
    char real[PATH_MAX], out[PATH_MAX];
    bool far;
    CHECK(realpath(at("fr"), real));   // (the test root may itself sit below a link)
    struct { const char *path, *want; bool far; } c[] = {
        { "fr/home/nas/missing/../q", "/share/q", true },   // (past the mount by the text: missing isn't looked up)
        { "fr/home/mylinks/nas/q", "/share/q", true },
        { "fr/abs/nas", "/share", true },
        { "fr/home/nas/..", "", false },                    // (back out: local again)
        { "fr/home/./x.txt", "/home/x.txt", false },
        { "fr/home/mylinks", "/links", false },
        { "fr/share", "/share", true },
    };
    for (int i = 0; i < COUNT(c); i++) {
        far = !c[i].far;
        bool ok = far_real(&mounts, at(c[i].path), out, sizeof out, &far);
        char want[PATH_MAX + 16];
        snprintf(want, sizeof want, "%s%s", real, c[i].want);
        if (!ok || strcmp(out, want) != 0 || far != c[i].far) fprintf(stderr, "far_real %s: %d %s %d\n", c[i].path, ok, ok ? out : strerror(errno), far);
        CHECK(ok && strcmp(out, want) == 0 && far == c[i].far);
    }
    CHECK(far_real(&mounts, "/", out, sizeof out, &far) && strcmp(out, "/") == 0 && !far);
    // Errors as realpath's, from the local parts.
    struct { const char *path; int e; } bad[] = {
        { "fr/home/missing/x", ENOENT }, { "fr/home/x.txt/y", ENOTDIR }, { "fr/home/loop", ELOOP },
        { "fr/home/x.txt/..", ENOTDIR }, { "fr/home/x.txt/", ENOTDIR },   // (a file isn't gone through, even by "..")
    };
    for (int i = 0; i < COUNT(bad); i++) {
        errno = 0;
        CHECK(!far_real(&mounts, at(bad[i].path), out, sizeof out, &far) && errno == bad[i].e);
    }
    // A chain of 41 links: one past the kernel's 40, refused as realpath refuses it.
    mkdirs(at("fr/chain"));
    for (int i = 0; i < 41; i++) {
        char from[PATH_MAX], to[32];
        snprintf(from, sizeof from, "%s/l%d", at("fr/chain"), i);
        snprintf(to, sizeof to, i == 40 ? "." : "l%d", i + 1);
        CHECK(symlink(to, from) == 0);
    }
    errno = 0;
    CHECK(!far_real(&mounts, at("fr/chain/l0"), out, sizeof out, &far) && errno == ELOOP);
    CHECK(far_real(&mounts, at("fr/chain/l1"), out, sizeof out, &far) && !far);   // (40: still followed)
    // No mount list: realpath's answer (share is a plain folder here).
    CHECK(far_real(NULL, at("fr/home/mylinks/nas/d"), out, sizeof out, &far) && !far);
    char want[PATH_MAX + 16];
    snprintf(want, sizeof want, "%s/share/d", real);
    CHECK(strcmp(out, want) == 0);
    CHECK(!far_real(NULL, at("fr/home/nas/missing"), out, sizeof out, &far) && errno == ENOENT);
    // A part longer than a path can be: refused before it is put together.
    char *longp = arena_sprintf(&ta, "%s/%0*d", at("fr/home"), 5000, 0);
    CHECK(!far_real(&mounts, longp, out, sizeof out, &far) && errno == ENAMETOOLONG);
    // Relative: from the current folder (ofx's start, ".").
    int here = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(here >= 0 && chdir(at("fr/home")) == 0);
    CHECK(far_real(&mounts, "mylinks/nas/q", out, sizeof out, &far) && far);
    snprintf(want, sizeof want, "%s/share/q", real);
    CHECK(strcmp(out, want) == 0);
    CHECK(far_real(&mounts, ".", out, sizeof out, &far) && !far);
    snprintf(want, sizeof want, "%s/home", real);
    CHECK(strcmp(out, want) == 0);
    CHECK(here >= 0 && fchdir(here) == 0);
    if (here >= 0) close(here);
    // Too small an out: refused, not cut.
    char small[8];
    CHECK(!far_real(&mounts, at("fr/home"), small, sizeof small, &far) && errno == ENAMETOOLONG);
    // A listing in fr/home: chain, a chain through a local link into the share, is far now
    // (step 2b's known gap), not followed.
    CHECK(symlink("mylinks/nas", at("fr/home/chain")) == 0);
    Listing l;
    Err err;
    CHECK(fs_list_far(&ta, at("fr/home"), false, (Sort){ SORT_NAME, false }, &mounts, &l, &err));
    isize k = listing_find(&l, S("chain"));
    CHECK(k >= 0 && l.e[k].kind == K_LINK_DIR && l.e[k].far);
    k = listing_find(&l, S("mylinks"));
    CHECK(k >= 0 && l.e[k].kind == K_LINK_DIR && !l.e[k].far);
}

// hist_take (step 3): a Back step found gone only later, when a far folder's lister says so. The
// folder the step left comes off the forward stack, as hist_unstep drops it; Forward the same way.
static void test_hist_take(void)
{
    static UiHistory h;
    static UiPlace to, was;
    h = (UiHistory){ 0 };
    hist_leave(&h, "/a", S("x"));
    hist_leave(&h, "/b", S("y"));
    CHECK(hist_step(&h, -1, "/c", S("z"), &to) && strcmp(to.dir, "/b") == 0 && h.nfwd == 1);
    CHECK(hist_take(&h, -1, &was) && strcmp(was.dir, "/c") == 0 && strcmp(was.sel, "z") == 0 && h.nfwd == 0 && h.nback == 1);
    CHECK(!hist_take(&h, -1, &was));
    CHECK(hist_step(&h, -1, "/c", S("z"), &to) && strcmp(to.dir, "/a") == 0);
    CHECK(hist_step(&h, 1, "/a", S("x"), &to) && strcmp(to.dir, "/c") == 0 && h.nback == 1);
    CHECK(hist_take(&h, 1, &was) && strcmp(was.dir, "/a") == 0 && h.nback == 0 && h.nfwd == 0);
}

// The search walk (step 3): a far drive's mount point, a link into one and a chain through a local
// link into one are matched by name but never entered or read; the walk counts them. A search in a
// far folder reads none of it. (share is a plain folder named in the mount list.)
static void test_search_far(void)
{
    arena_reset(&ta);
    char r[300], sh[300];   // (at's buffers turn over)
    snprintf(r, sizeof r, "%s", at("sf/top"));
    snprintf(sh, sizeof sh, "%s", at("sf/top/share"));
    put(at("sf/top/a.txt"), "a");
    put(at("sf/top/share/a.txt"), "a");
    put(at("sf/top/share/inner/a.txt"), "a");
    mkdirs(at("sf/links"));
    CHECK(symlink("share", at("sf/top/nas")) == 0);
    CHECK(symlink("../links", at("sf/top/mylinks")) == 0);
    CHECK(symlink("../top/share", at("sf/links/nas")) == 0);
    CHECK(symlink("mylinks/nas", at("sf/top/chain")) == 0);
    Mounts mounts = mounts_parse(&ta, str_from_c(arena_sprintf(&ta, "1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw - fuse.gvfsd-fuse g rw\n", sh)));
    SearchWalk w = sw_walk("a", (UiMatchOpts){ 0 }, false);
    w.mounts = mounts;
    CHECK(search_walk(&w, r));
    CHECK(sw.n == 4 && sw_has("|a.txt") && sw_has("|share") && sw_has("|nas") && sw_has("|chain") && w.s.far == 3);
    // Inside files: none of the far ones is a result (not read).
    w = sw_inside("*", "a");
    w.mounts = mounts;
    CHECK(search_walk(&w, r) && sw.n == 1 && sw_has("|a.txt") && w.s.far == 3);
    // Searched from a far folder (here through the link nas): nothing read.
    w = sw_walk("a", (UiMatchOpts){ 0 }, false);
    w.mounts = mounts;
    CHECK(search_walk(&w, at("sf/top/nas")) && sw.n == 0 && w.s.far == 1 && w.s.folders == 0);
    char skipped[256];
    search_skips_text(&w.s, &SEARCH_SKIP_DEFAULT, skipped, sizeof skipped);
    CHECK(strcmp(skipped, "skipped 1 network folder") == 0);
    // No mount list: the share is entered, as any folder.
    w = sw_walk("a", (UiMatchOpts){ 0 }, false);
    CHECK(search_walk(&w, r) && sw_has("share|a.txt") && sw_has("share/inner|a.txt") && w.s.far == 0);
}

// ofx-list --do (step 3b): each action run as ofx would run it, its answer parsed as ofx parses it.
static Str fd_buf;
static FarReply fd_r;
static bool fd_parsed;   // (the answer was a whole one, whatever its rc)

static bool fd_run(int argc, char **argv)
{
    static char *mem;
    static size_t len;
    free(mem), mem = NULL, len = 0;
    FILE *f = open_memstream(&mem, &len);
    if (!f) return false;
    int rc = far_do_serve(f, argc, argv);
    fclose(f);
    fd_buf = (Str){ arena_cstr(&ta, (Str){ mem, (isize)len }), (isize)len };
    fd_parsed = far_reply_parse(fd_buf, &fd_r);
    return fd_parsed && rc == 0;
}

#define FD(...) fd_run((int)COUNT(((char *[]){ __VA_ARGS__ })), (char *[]){ __VA_ARGS__ })

static void test_far_do(void)
{
    arena_reset(&ta);
    char d[300];
    snprintf(d, sizeof d, "%s", at("fdo"));
    mkdirs(d);
    CHECK(FD("mkdir", d, "New folder") && fd_r.ok && fd_r.n == 1 && str_eq(fd_r.f[0], S("New folder")));
    CHECK(FD("mkdir", d, "New folder") && fd_r.ok && str_eq(fd_r.f[0], S("New folder (2)")));
    CHECK(FD("mkfile", d, "New file") && fd_r.ok && str_eq(fd_r.f[0], S("New file")));
    CHECK(FD("rename", d, "New file", "a\nb.txt") && fd_r.ok && fd_r.n == 0);   // (a newline in a name: by length)
    CHECK(FD("rename", d, "gone", "x") && !fd_r.ok && fd_r.n == 1 && fd_r.f[0].len > 0);
    CHECK(FD("frees", d, "0", "a\nb.txt", d, "1", "New folder") && fd_r.ok && fd_r.n == 2 && str_eq(fd_r.f[0], S("a\nb (copy).txt")) && str_eq(fd_r.f[1], S("New folder (copy)")));
    CHECK(!FD("frees", d, "0") && fd_parsed && !fd_r.ok);   // (a triple short)
    CHECK(!FD("frees", d, "0", "x", d, "1") && fd_parsed && !fd_r.ok);   // (one whole, one short)
    char ab[PATH_MAX], gonep[PATH_MAX];
    snprintf(ab, sizeof ab, "%s/a\nb.txt", d), snprintf(gonep, sizeof gonep, "%s/none", d);
    CHECK(FD("kinds", ab, d, gonep) && fd_r.ok && fd_r.n == 3 && str_eq(fd_r.f[0], S("o")) && str_eq(fd_r.f[1], S("d")) && str_eq(fd_r.f[2], S("-")));
    CHECK(!FD("kinds") && fd_parsed && !fd_r.ok);
    char target[PATH_MAX];
    snprintf(target, sizeof target, "%s/a\nb.txt", d);
    CHECK(FD("symlink", d, target, "Link to it") && fd_r.ok && str_eq(fd_r.f[0], S("Link to it")));
    // stat: a link's lstat and its target's stat as bytes, its text, a birth time.
    char lp[PATH_MAX];
    snprintf(lp, sizeof lp, "%s/Link to it", d);
    CHECK(FD("stat", lp) && fd_r.ok && fd_r.n == 4 && fd_r.f[0].len == (isize)sizeof(struct stat) && fd_r.f[1].len == (isize)sizeof(struct stat));
    struct stat lst, st;
    memcpy(&lst, fd_r.f[0].p, sizeof lst), memcpy(&st, fd_r.f[1].p, sizeof st);
    CHECK(S_ISLNK(lst.st_mode) && S_ISREG(st.st_mode) && str_eq(fd_r.f[2], str_from_c(target)));
    unlink(target);
    CHECK(FD("stat", lp) && fd_r.ok && fd_r.f[1].len == 0);   // (broken: no stat)
    CHECK(FD("stat", target) && !fd_r.ok);
    // chmod: the mode it had.
    char nf[PATH_MAX];
    snprintf(nf, sizeof nf, "%s/New folder", d);
    chmod(nf, 0755);
    CHECK(FD("chmod", nf, "700") && fd_r.ok && str_eq(fd_r.f[0], S("755")));
    CHECK(stat(nf, &st) == 0 && (st.st_mode & 0777) == 0700);
    // trash, and its undo: back where it was.
    CHECK(FD("trash", nf) && fd_r.ok && fd_r.n == 3 && str_eq(fd_r.f[0], str_from_c(nf)) && str_ends_with(fd_r.f[2], S(".trashinfo")) && !str_ends_with(fd_r.f[1], S(".trashinfo")));
    char file[PATH_MAX], info[PATH_MAX], kind[8];
    snprintf(file, sizeof file, "%.*s", STR_ARG(fd_r.f[1]));
    snprintf(info, sizeof info, "%.*s", STR_ARG(fd_r.f[2]));
    CHECK(access(nf, F_OK) != 0 && access(file, F_OK) == 0);
    snprintf(kind, sizeof kind, "%d", UNDO_TRASH);
    CHECK(FD("undo", kind, nf, file, info) && fd_r.ok && str_eq(fd_r.f[0], S("done")) && str_eq(fd_r.f[1], S("1")));
    CHECK(stat(nf, &st) == 0 && S_ISDIR(st.st_mode));
    // An undo that can't: its reason, nothing done.
    snprintf(kind, sizeof kind, "%d", UNDO_MKDIR);
    char gone[PATH_MAX];
    snprintf(gone, sizeof gone, "%s/never", d);
    CHECK(FD("undo", kind, "", gone, "") && !fd_r.ok && fd_r.n == 2 && str_eq(fd_r.f[1], S("0")));
    // The guard: a write outside the test's root refused here too.
    CHECK(FD("mkdir", decoy, "fdo") && !fd_r.ok);
    char dfdo[PATH_MAX];
    snprintf(dfdo, sizeof dfdo, "%s/fdo", decoy);
    CHECK(access(dfdo, F_OK) != 0);
    // Wrong calls: refused, rc 2.
    CHECK(!FD("rename", d) && fd_parsed && !fd_r.ok);
    CHECK(!FD("rename", d, "a", "b", "c") && fd_parsed && !fd_r.ok);   // (one too many)
    CHECK(!FD("nonesuch") && fd_parsed && !fd_r.ok);
    CHECK(!FD("undo", kind, "a") && !fd_r.ok);
    // The parse: whole answers only.
    CHECK(!far_reply_parse(S("ok 1\n5\nabc"), &fd_r) && !far_reply_parse(S("ok 1\n3\nabcd"), &fd_r) && !far_reply_parse(S("no 0\n"), &fd_r));
    CHECK(!far_reply_parse(S("ok 9\n"), &fd_r) && !far_reply_parse(S("maybe 0\n"), &fd_r) && !far_reply_parse(S("ok x\n"), &fd_r));
    CHECK(far_reply_parse(S("ok 2\n0\n2\nab"), &fd_r) && fd_r.n == 2 && fd_r.f[0].len == 0 && str_eq(fd_r.f[1], S("ab")));
    CHECK(!far_reply_parse(S("ok /\n"), &fd_r) && !far_reply_parse(S("ok 1\n\n"), &fd_r));   // (digits only, and at least one)
    {   // (more fields than it holds: refused, not written past its end)
        static char many[16 + 2 * (FAR_DO_FIELDS + 1)];
        int at = snprintf(many, sizeof many, "ok %d\n", FAR_DO_FIELDS + 1);
        for (int i = 0; i <= FAR_DO_FIELDS; i++) many[at++] = '0', many[at++] = '\n';
        CHECK(!far_reply_parse((Str){ many, at }, &fd_r));
    }
    char *two = malloc(2);   // (a header too short, the last bytes there are: not read past)
    CHECK(two != NULL);
    if (two) {
        memcpy(two, "ok", 2);
        CHECK(!far_reply_parse((Str){ two, 2 }, &fd_r));
        free(two);
    }
}

// far_drive (step 3 review): the mount a path is on; under gvfs's one FUSE mount, the share too, so
// one share that doesn't answer isn't taken for another.
static void test_far_drive(void)
{
    Mounts mi = mounts_parse(&ta, S("1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / /run/u/gvfs rw - fuse.gvfsd-fuse g rw\n3 1 0:3 / /mnt/nas rw - nfs4 h:/x rw\n"));
    char d[PATH_MAX];
    bool gvfs;
    CHECK(far_drive(&mi, "/run/u/gvfs/smb-share:server=a,share=p/sub/x", d, sizeof d, &gvfs) && gvfs && strcmp(d, "/run/u/gvfs/smb-share:server=a,share=p") == 0);
    CHECK(far_drive(&mi, "/run/u/gvfs/smb-share:server=a,share=p", d, sizeof d, &gvfs) && gvfs && strcmp(d, "/run/u/gvfs/smb-share:server=a,share=p") == 0);
    CHECK(far_drive(&mi, "/run/u/gvfs", d, sizeof d, &gvfs) && gvfs && strcmp(d, "/run/u/gvfs") == 0);
    CHECK(far_drive(&mi, "/mnt/nas/a/b", d, sizeof d, &gvfs) && !gvfs && strcmp(d, "/mnt/nas") == 0);
    CHECK(far_drive(&mi, "/home/m", d, sizeof d, &gvfs) && !gvfs && strcmp(d, "/") == 0);
    CHECK(!far_drive(mt(S("1 0 0:2 / /mnt/nas rw - nfs4 h:/x rw\n")), "/home/m", d, sizeof d, &gvfs));
    // The longest mount point holding it, wherever its line is.
    CHECK(far_drive(mt(S("3 1 0:3 / /mnt/nas rw - nfs4 h:/x rw\n1 0 0:1 / / rw - ext4 /dev/x rw\n")), "/mnt/nas/a", d, sizeof d, &gvfs) && strcmp(d, "/mnt/nas") == 0);
    char small[16];   // (a share's name that doesn't fit: the mount point alone)
    CHECK(far_drive(mt(S("2 1 0:2 / /g rw - fuse.gvfsd-fuse g rw\n")), "/g/smb-share:server=a,share=p", small, sizeof small, &gvfs) && strcmp(small, "/g") == 0);
}

// Connect to Server's address (step 4): smb:// with a server and a share; no password in it.
static void test_connect_uri(void)
{
    char uri[64], host[32], share[32], why[128];
#define CU(t) connect_uri(S(t), uri, sizeof uri, host, sizeof host, share, sizeof share, why, sizeof why)
    CHECK(CU("/home/m") == CONNECT_NOT && CU("~/x") == CONNECT_NOT && CU("smb:/x/y") == CONNECT_NOT && CU("ftp://h/x") == CONNECT_NOT && CU("nfs:/h/x") == CONNECT_NOT && CU("") == CONNECT_NOT && CU("smb:/") == CONNECT_NOT);
    CHECK(CU(" smb://nas/pub ") == CONNECT_OK && strcmp(uri, "smb://nas/pub") == 0 && strcmp(host, "nas") == 0 && strcmp(share, "pub") == 0);
    CHECK(CU("SMB://127.0.0.1:4445/priv/sub/x") == CONNECT_OK && strcmp(host, "127.0.0.1") == 0 && strcmp(share, "priv") == 0 && strcmp(uri, "SMB://127.0.0.1:4445/priv/sub/x") == 0);
    CHECK(CU("smb://michael@nas/home") == CONNECT_OK && strcmp(host, "nas") == 0 && strcmp(share, "home") == 0);
    CHECK(CU("smb://dom;michael@nas:445/My Share") == CONNECT_OK && strcmp(host, "nas") == 0 && strcmp(share, "My Share") == 0);
    CHECK(CU("smb://michael:secret@nas/home") == CONNECT_BAD && strstr(why, "No password") && !strstr(why, "secret"));
    // A server's address alone (step 7a): its shares listed; a share named empty isn't one.
    CHECK(CU("smb://nas") == CONNECT_SERVER && strcmp(host, "nas") == 0 && share[0] == 0 && strcmp(uri, "smb://nas") == 0);
    CHECK(CU("smb://nas:445/") == CONNECT_SERVER && strcmp(host, "nas") == 0 && share[0] == 0);
    CHECK(CU("smb://me@nas/") == CONNECT_SERVER && share[0] == 0);
    // SFTP (step 7c): a user and a port taken, a path or none; named "user@host" (gvfs's); no
    // password; not a server's shares.
    CHECK(CU("sftp://me@box:2020/home/me") == CONNECT_OK && strcmp(uri, "sftp://me@box:2020/home/me") == 0 && strcmp(host, "me@box") == 0 && share[0] == 0);
    CHECK(CU(" SFTP://box ") == CONNECT_OK && strcmp(uri, "SFTP://box") == 0 && strcmp(host, "box") == 0 && share[0] == 0);
    CHECK(CU("sftp://box/") == CONNECT_OK && strcmp(host, "box") == 0);
    CHECK(CU("sftp://me:pw@box/") == CONNECT_BAD && strstr(why, "No password") && !strstr(why, "pw"));
    CHECK(CU("sftp:///x") == CONNECT_BAD && strcmp(why, "Name a server: sftp://server/") == 0);
    CHECK(CU("sftp://box:x/") == CONNECT_BAD && strstr(why, "Not a port: x"));
    CHECK(CU("sftp://0123456789012345678901234567890123@b/") == CONNECT_BAD && strstr(why, "too long"));   // (user@host past what host holds)
    CHECK(CU("sftp:/box") == CONNECT_NOT && CU("sftpx://box") == CONNECT_NOT);
    CHECK(CU("smb://nas//x") == CONNECT_BAD && strstr(why, "Name a share on nas"));
    CHECK(CU("smb://nas:445//") == CONNECT_BAD && strstr(why, "smb://nas:445/share"));
    CHECK(CU("smb:///pub") == CONNECT_BAD && strstr(why, "Name a server"));
    CHECK(CU("smb://u@/pub") == CONNECT_BAD && strstr(why, "Name a server"));
    CHECK(CU("smb://nas:x/pub") == CONNECT_BAD && strstr(why, "Not a port: x"));
    CHECK(CU("smb://nas:/pub") == CONNECT_BAD && strstr(why, "Not a port"));
    CHECK(CU("smb://nas:123456/pub") == CONNECT_BAD);
    CHECK(CU("smb://nas/p\tb") == CONNECT_BAD && CU("smb://nas/p\x7f") == CONNECT_BAD);
    CHECK(CU("smb://nas/0123456789012345678901234567890123456789012345678901234567890") == CONNECT_BAD && strstr(why, "too long"));
    CHECK(CU("smb://0123456789012345678901234567890123/p") == CONNECT_BAD && strstr(why, "too long"));
    CHECK(CU("smb://h/0123456789012345678901234567890123") == CONNECT_BAD && strstr(why, "too long"));
    CHECK(CU("smb://h/p/0123456789012345678901234567890123456789012345678901abcdefgh") == CONNECT_BAD && strstr(why, "too long"));   // (a folder in the share past what the address holds)
    // NFS (step 6): the share named by the path's last name, as gvfs names an export; no user, no
    // port (gvfs-nfs takes neither), no password
    CHECK(CU("nfs://127.0.0.1/srv/nfs") == CONNECT_OK && strcmp(uri, "nfs://127.0.0.1/srv/nfs") == 0 && strcmp(host, "127.0.0.1") == 0 && strcmp(share, "nfs") == 0);
    CHECK(CU(" NFS://nas/export/home/m/ ") == CONNECT_OK && strcmp(uri, "NFS://nas/export/home/m/") == 0 && strcmp(host, "nas") == 0 && strcmp(share, "m") == 0);
    CHECK(CU("nfs://nas/x") == CONNECT_OK && strcmp(share, "x") == 0);
    CHECK(CU("nfs://nas") == CONNECT_BAD && strstr(why, "Name a folder on nas: nfs://nas/folder"));
    CHECK(CU("nfs://nas/") == CONNECT_BAD && strstr(why, "Name a folder on nas"));
    CHECK(CU("nfs://nas//") == CONNECT_BAD && strstr(why, "Name a folder on nas"));
    CHECK(CU("nfs:///srv") == CONNECT_BAD && strstr(why, "Name a server: nfs://server/folder"));
    CHECK(CU("nfs://me@nas/srv") == CONNECT_BAD && strstr(why, "No user in an NFS address"));
    CHECK(CU("nfs://me:pw@nas/srv") == CONNECT_BAD && strstr(why, "No password") && !strstr(why, "pw"));
    CHECK(CU("nfs://nas:2049/srv") == CONNECT_BAD && strstr(why, "No port in an NFS address"));
    CHECK(CU("nfs://nas/s\x01") == CONNECT_BAD && CU("nfs://nas/s\x7f") == CONNECT_BAD && strstr(why, "control characters"));
    CHECK(CU("nfs://nas/a/0123456789012345678901234567890123") == CONNECT_BAD && strstr(why, "too long"));
#undef CU
}

// What gio says as it mounts (step 4), as the spike recorded it on the test Samba.
static void test_gio_ask(void)
{
    char d[32];
    int r;
    const char *head = "Authentication Required\nEnter user and password for share \xe2\x80\x9cpriv\xe2\x80\x9d on \xe2\x80\x9c" "127.0.0.1\xe2\x80\x9d:\n";
    char b[512];
    CHECK(gio_ask(S(""), d, sizeof d, &r) == GIO_ASK_NONE && r == 0 && d[0] == 0);
    snprintf(b, sizeof b, "%sUser [guest]: ", head);
    CHECK(gio_ask(str_from_c(b), d, sizeof d, &r) == GIO_ASK_USER && r == 1 && strcmp(d, "guest") == 0);
    snprintf(b, sizeof b, "%sUser [guest]: Domain [WORKGROUP]: ", head);
    CHECK(gio_ask(str_from_c(b), d, sizeof d, &r) == GIO_ASK_DOMAIN && strcmp(d, "WORKGROUP") == 0);
    snprintf(b, sizeof b, "%sUser [guest]: Domain [WORKGROUP]: Password: ", head);
    CHECK(gio_ask(str_from_c(b), d, sizeof d, &r) == GIO_ASK_PASSWORD && d[0] == 0 && r == 1);
    snprintf(b, sizeof b, "%sUser [guest]: Domain [WORKGROUP]: Password: \n%sUser [guest]: ", head, head);   // (a wrong one: asked again)
    CHECK(gio_ask(str_from_c(b), d, sizeof d, &r) == GIO_ASK_USER && r == 2);
    snprintf(b, sizeof b, "%sUser [guest]: Domain [WORKGROUP]: Password: \n", head);   // (answered: no question now)
    CHECK(gio_ask(str_from_c(b), d, sizeof d, &r) == GIO_ASK_NONE && r == 1);
    CHECK(gio_ask(S("Authentication Required\nEnter password for share \xe2\x80\x9cpriv\xe2\x80\x9d on \xe2\x80\x9ch\xe2\x80\x9d:\nDomain [W]: "), d, sizeof d, &r) == GIO_ASK_DOMAIN && strcmp(d, "W") == 0);
    CHECK(gio_ask(S("User [guest]: "), d, sizeof d, &r) == GIO_ASK_NONE);   // (no header: not gio's question)
    CHECK(gio_ask(S("Authentication Required\nUser[x]: "), d, sizeof d, &r) == GIO_ASK_NONE);
    CHECK(gio_ask(S("Authentication Required\nUsers[x]: "), d, sizeof d, &r) == GIO_ASK_NONE);   // (not "User" with a '[' after its last letter)
    CHECK(gio_ask(S("Authentication Required\nColour [x]: "), d, sizeof d, &r) == GIO_ASK_NONE);
    CHECK(gio_ask(S("Authentication Required\n[x]: "), d, sizeof d, &r) == GIO_ASK_NONE);
    CHECK(gio_ask(S("Authentication Required\nUser [a [b]: "), d, sizeof d, &r) == GIO_ASK_NONE);   // (its last '[' after a space only: "a [b" isn't read as b)
    char tiny[4];
    CHECK(gio_ask(S("Authentication Required\nUser [michael]: "), tiny, sizeof tiny, &r) == GIO_ASK_USER && strcmp(tiny, "mic") == 0);
    CHECK(gio_ask(S("Authentication RequiredAuthentication Required\nPassword: "), d, sizeof d, &r) == GIO_ASK_PASSWORD && r == 2);
    // SFTP (step 7c, measured on the test sshd and Michael's laptop): no domain; "User: " with no
    // default; a key's passphrase; the host-key question (numbered answers).
    CHECK(gio_ask(S("Authentication Required\nEnter user and password for \xe2\x80\x9c" "127.0.0.1\xe2\x80\x9d:\nUser: "), d, sizeof d, &r) == GIO_ASK_USER && d[0] == 0 && r == 1);
    CHECK(gio_ask(S("Authentication Required\nEnter user and password for \xe2\x80\x9ch\xe2\x80\x9d:\nUser: Password: "), d, sizeof d, &r) == GIO_ASK_PASSWORD);
    CHECK(gio_ask(S("Authentication Required\nEnter passphrase for secure key for \xe2\x80\x9cm\xe2\x80\x9d on \xe2\x80\x9ch\xe2\x80\x9d:\nPassword: "), d, sizeof d, &r) == GIO_ASK_KEY && r == 1);
    CHECK(gio_ask(S("Authentication Required\nEnter passphrase for secure key for \xe2\x80\x9cm\xe2\x80\x9d on \xe2\x80\x9ch\xe2\x80\x9d:\nPassword: \nAuthentication Required\nEnter password for \xe2\x80\x9cm\xe2\x80\x9d on \xe2\x80\x9ch\xe2\x80\x9d:\nPassword: "), d, sizeof d, &r) == GIO_ASK_PASSWORD && r == 2);   // (the last question's header decides)
    CHECK(gio_ask(S("Identity Verification Failed\nVerifying the identity of \xe2\x80\x9ch\xe2\x80\x9d failed\n[1] Log In Anyway\n[2] Cancel Login\nChoice: "), d, sizeof d, &r) == GIO_ASK_CHOICE && r == 0);
    CHECK(gio_ask(S("Authentication Required\nNameUser: "), d, sizeof d, &r) == GIO_ASK_NONE);   // (a label ending in User isn't "User: ")
    CHECK(gio_ask(S("Choice: "), d, sizeof d, &r) == GIO_ASK_NONE);   // (only after a line of its own)
}

static void test_gio_why(void)
{
    char w[64], p[64];
    const char *noise = "invoking IsSupported() failed for remote volume monitor with dbus name org.gtk.vfs.UDisks2VolumeMonitor:: GDBus.Error: x\n";
    char b[512];
    CHECK(!gio_why(S(""), w, sizeof w) && !gio_why(str_from_c(noise), w, sizeof w));
    snprintf(b, sizeof b, "%sgio: smb://127.0.0.1:1/pub/: Failed to mount Windows share: Connection refused\n", noise);
    CHECK(gio_why(str_from_c(b), w, sizeof w) && strcmp(w, "Connection refused") == 0);
    CHECK(gio_why(S("gio: smb://127.0.0.1:40933/priv/: Location is already mounted\n"), w, sizeof w) && strcmp(w, "Location is already mounted") == 0);
    CHECK(gio_why(S("gio: smb://h/priv/: Anonymous access denied"), w, sizeof w) && strcmp(w, "Anonymous access denied") == 0);
    CHECK(gio_why(S("gio: one\ngio: smb://h/x/: two\n"), w, sizeof w) && strcmp(w, "two") == 0);   // (the last)
    CHECK(gio_why(S("gio: no address here\n"), w, sizeof w) && strcmp(w, "no address here") == 0);
    CHECK(gio_why(S("gio: smb://h/x/: Failed to mount Windows share: "), w, sizeof w) && strcmp(w, "Failed to mount Windows share: ") == 0);
    CHECK(gio_why(S("xgio: smb://h/x/: a\n"), w, sizeof w) == false);
    CHECK(gio_local_path(S("display name: priv on h\nlocal path: /run/u/gvfs/smb-share:port=1,server=h,share=priv\nunix mount: x\n"), p, sizeof p) && strcmp(p, "/run/u/gvfs/smb-share:port=1,server=h,share=priv") == 0);
    CHECK(gio_local_path(S("local path: /a b/c"), p, sizeof p) && strcmp(p, "/a b/c") == 0);
    CHECK(!gio_local_path(S("display name: Windows shares on h\nuri: smb://h/\n"), p, sizeof p));
    CHECK(!gio_local_path(S("x local path: /a\n"), p, sizeof p));
    CHECK(!gio_local_path(S("local path: rel\n"), p, sizeof p) && !gio_local_path(S("local path: \n"), p, sizeof p));
    char small[4];
    CHECK(!gio_local_path(S("local path: /abcdef\n"), small, sizeof small));
}

// Connect's question's keys (step 4): typing goes to the field with the keys; the password is never
// copied or cut, and is one word; Tab, Down and Up go round the fields (the user's only when gio
// asks for one); Enter connects, Alt+G as a guest, Esc cancels, each closing the panel.
static void test_login_keys(void)
{
    Ui u = { 0 };
    u.panel = UI_PANEL_LOGIN, u.login_at = UI_LOGIN_PASS;
    ui_line_set(ui_login_line(&u, UI_LOGIN_USER), S("guest"));
    CHECK(ui_login_line(&u, UI_LOGIN_PASS).secret && !ui_login_line(&u, UI_LOGIN_USER).secret && !ui_login_line(&u, UI_LOGIN_DOMAIN).secret);
    ui_login_event(&u, ev_ch('p'), STR_EMPTY), ui_login_event(&u, ev_ch('w'), STR_EMPTY), ui_login_event(&u, ev_ch('.'), STR_EMPTY), ui_login_event(&u, ev_ch('x'), STR_EMPTY);
    CHECK(u.login_pass.len == 4 && memcmp(u.login_pass.b, "pw.x", 4) == 0 && u.login_user.len == 5);
    ui_login_event(&u, ev_key(UK_WORD_LEFT), STR_EMPTY);
    CHECK(u.login_pass.caret == 0);
    ui_login_event(&u, ev_key(UK_WORD_RIGHT), STR_EMPTY);
    CHECK(u.login_pass.caret == 4);
    ui_login_event(&u, ev_key(UK_ALL), STR_EMPTY);
    ui_login_event(&u, ev_key(UK_COPY), STR_EMPTY);
    CHECK(!u.copied);
    ui_login_event(&u, ev_key(UK_CUT), STR_EMPTY);
    CHECK(!u.copied && u.login_pass.len == 4);
    ui_login_event(&u, ev_key(UK_PASTE), S("se cret\nmore"));   // (a password manager's paste: its first line, over the selection)
    CHECK(u.login_pass.len == 7 && memcmp(u.login_pass.b, "se cret", 7) == 0);
    UiLine l;
    CHECK(ui_line_active(&u, &l) && l.b == u.login_pass.b && l.secret);
    // The user's field copies as any other.
    ui_login_event(&u, ev_key(UK_UP), STR_EMPTY);
    CHECK(u.login_at == UI_LOGIN_DOMAIN && ui_line_active(&u, &l) && l.b == u.login_domain.b);
    ui_login_event(&u, ev_key(UK_UP), STR_EMPTY);
    CHECK(u.login_at == UI_LOGIN_USER);
    ui_login_event(&u, ev_key(UK_ALL), STR_EMPTY), ui_login_event(&u, ev_key(UK_COPY), STR_EMPTY);
    CHECK(u.copied && u.copy_len == 5);
    u.copied = false;
    ui_login_event(&u, ev_key(UK_UP), STR_EMPTY);
    CHECK(u.login_at == UI_LOGIN_PASS);   // (round)
    ui_login_event(&u, ev_key(UK_TAB), STR_EMPTY);
    CHECK(u.login_at == UI_LOGIN_USER);
    ui_login_event(&u, ev_key(UK_DOWN), STR_EMPTY);
    CHECK(u.login_at == UI_LOGIN_DOMAIN);
    u.login_no_user = true;   // (the address names the user: no field for it)
    ui_login_event(&u, ev_key(UK_TAB), STR_EMPTY), ui_login_event(&u, ev_key(UK_TAB), STR_EMPTY);
    CHECK(u.login_at == UI_LOGIN_DOMAIN);
    ui_login_event(&u, ev_key(UK_UP), STR_EMPTY);
    CHECK(u.login_at == UI_LOGIN_PASS);
    // The answers, each closing the panel; Alt and another letter does nothing.
    UiEvent alt_f = { UK_MENU, 'f', UK_NONE }, alt_g = { UK_MENU, 'g', UK_NONE };
    CHECK(ui_login_event(&u, alt_f, STR_EMPTY).kind == UA_NONE && u.panel == UI_PANEL_LOGIN);
    CHECK(ui_login_event(&u, ev_key(UK_ENTER), STR_EMPTY).kind == UA_LOGIN && u.panel == UI_PANEL_NONE);
    u.panel = UI_PANEL_LOGIN;
    CHECK(ui_login_event(&u, alt_g, STR_EMPTY).kind == UA_LOGIN_GUEST && u.panel == UI_PANEL_NONE);
    u.panel = UI_PANEL_LOGIN;
    CHECK(ui_login_event(&u, ev_key(UK_ESC), STR_EMPTY).kind == UA_LOGIN_CANCEL && u.panel == UI_PANEL_NONE);
    // An SFTP server's (step 7c): no domain (Tab and Up skip it), no guest (Alt+G, the button); a
    // key's passphrase: the password alone.
    u.panel = UI_PANEL_LOGIN, u.login_no_user = false, u.login_ssh = true, u.login_at = UI_LOGIN_USER;
    ui_login_event(&u, ev_key(UK_TAB), STR_EMPTY);
    CHECK(u.login_at == UI_LOGIN_PASS && ui_login_shown(&u, UI_LOGIN_USER) && !ui_login_shown(&u, UI_LOGIN_DOMAIN));
    ui_login_event(&u, ev_key(UK_UP), STR_EMPTY);
    CHECK(u.login_at == UI_LOGIN_USER);
    CHECK(ui_login_event(&u, alt_g, STR_EMPTY).kind == UA_NONE && u.panel == UI_PANEL_LOGIN);
    UiInput gin = ui_input_none();
    gin.modal = UI_MODAL_GUEST;
    UiAction ga = { 0 };
    ui_handle_panels(&u, (UiNames){ 0 }, &gin, UI_ON_LOGIN, false, &ga);
    CHECK(ga.kind != UA_LOGIN_GUEST && u.panel == UI_PANEL_LOGIN);
    u.login_key = true, u.login_at = UI_LOGIN_PASS;
    ui_login_event(&u, ev_key(UK_TAB), STR_EMPTY);
    CHECK(u.login_at == UI_LOGIN_PASS && !ui_login_shown(&u, UI_LOGIN_USER) && !ui_login_shown(&u, UI_LOGIN_DOMAIN) && ui_login_shown(&u, UI_LOGIN_PASS));
    u.login_ssh = u.login_key = false;
    // By the pointer: a field takes the keys (not the user's when there is none); the buttons answer.
    u.panel = UI_PANEL_LOGIN, u.login_no_user = false;
    UiInput in = ui_input_none();
    in.login_field = UI_LOGIN_USER;
    UiAction a;
    CHECK(ui_handle_panels(&u, (UiNames){ 0 }, &in, UI_ON_LOGIN, false, &a) && a.kind == UA_NONE && u.login_at == UI_LOGIN_USER);
    u.login_no_user = true, in.login_field = UI_LOGIN_USER, u.login_at = UI_LOGIN_PASS;
    ui_handle_panels(&u, (UiNames){ 0 }, &in, UI_ON_LOGIN, false, &a);
    CHECK(u.login_at == UI_LOGIN_PASS);
    // Nor a field an SSH question hasn't (Domain), nor any but the passphrase for a key's (the F16
    // code-health review: the click's own check knew only the user field).
    u.login_no_user = false, u.login_ssh = true, in.login_field = UI_LOGIN_DOMAIN, u.login_at = UI_LOGIN_PASS;
    ui_handle_panels(&u, (UiNames){ 0 }, &in, UI_ON_LOGIN, false, &a);
    CHECK(u.login_at == UI_LOGIN_PASS);
    in.login_field = UI_LOGIN_USER;
    ui_handle_panels(&u, (UiNames){ 0 }, &in, UI_ON_LOGIN, false, &a);
    CHECK(u.login_at == UI_LOGIN_USER);
    u.login_key = true, u.login_at = UI_LOGIN_PASS;
    ui_handle_panels(&u, (UiNames){ 0 }, &in, UI_ON_LOGIN, false, &a);
    CHECK(u.login_at == UI_LOGIN_PASS);
    u.login_ssh = u.login_key = false;
    in = ui_input_none(), in.modal = UI_MODAL_GUEST;
    CHECK(ui_handle_panels(&u, (UiNames){ 0 }, &in, UI_ON_LOGIN, false, &a) && a.kind == UA_LOGIN_GUEST && u.panel == UI_PANEL_NONE);
    u.panel = UI_PANEL_LOGIN, in.modal = UI_MODAL_YES;
    CHECK(ui_handle_panels(&u, (UiNames){ 0 }, &in, UI_ON_LOGIN, false, &a) && a.kind == UA_LOGIN);
    u.panel = UI_PANEL_LOGIN, in.modal = UI_MODAL_NO;
    CHECK(ui_handle_panels(&u, (UiNames){ 0 }, &in, UI_ON_LOGIN, false, &a) && a.kind == UA_LOGIN_CANCEL && u.panel == UI_PANEL_NONE);
    // Go → Connect to Server…: the location line on "smb://", the caret after it, nothing selected.
    Ui g = { 0 };
    ui_menu_pick(&g, UM_CONNECT);
    CHECK(g.focus == UI_FOCUS_EDIT && g.edit_path && g.edit.len == 6 && memcmp(g.edit.b, "smb://", 6) == 0 && g.edit.caret == 6 && g.edit.anchor == 6);
}

// gvfs's share folders' names (step 5): the share's name as gvfs gives it.
static void test_gvfs_label(void)
{
    char l[64], v[16];
    gvfs_share_label(S("smb-share:port=4445,server=127.0.0.1,share=pub"), l, sizeof l);
    CHECK(strcmp(l, "pub on 127.0.0.1") == 0);
    gvfs_share_label(S("smb-share:domain=W,server=nas,share=My%20Docs,user=m"), l, sizeof l);
    CHECK(strcmp(l, "My Docs on nas") == 0);
    gvfs_share_label(S("afp-volume:host=mac,volume=Time"), l, sizeof l);
    CHECK(strcmp(l, "Time on mac") == 0);
    gvfs_share_label(S("sftp:host=box,user=me"), l, sizeof l);
    CHECK(strcmp(l, "me@box") == 0);
    gvfs_share_label(S("dav:host=web,ssl=true"), l, sizeof l);
    CHECK(strcmp(l, "web") == 0);
    gvfs_share_label(S("mtp:host=Phone"), l, sizeof l);
    CHECK(strcmp(l, "Phone") == 0);
    gvfs_share_label(S("nfs:host=127.0.0.1,prefix=%2Fsrv%2Fnfs"), l, sizeof l);   // (an NFS export: its path's last name, as gvfs names it)
    CHECK(strcmp(l, "nfs on 127.0.0.1") == 0);
    gvfs_share_label(S("nfs:host=nas,prefix=%2Fexport%2Fmy%20home%2F"), l, sizeof l);
    CHECK(strcmp(l, "my home on nas") == 0);
    gvfs_share_label(S("nfs:host=nas,prefix=%2F"), l, sizeof l);   // (the root: the server)
    CHECK(strcmp(l, "nas") == 0);
    gvfs_share_label(S("something"), l, sizeof l);   // (not one of gvfs's: its name)
    CHECK(strcmp(l, "something") == 0);
    gvfs_share_label(S("smb-share:server=,share=x"), l, sizeof l);   // (an empty value is none)
    CHECK(strcmp(l, "smb-share:server=,share=x") == 0);
    CHECK(gvfs_key(S("smb-share:servers=a,server=b"), "server", v, sizeof v) && strcmp(v, "b") == 0);   // (a key, not its prefix)
    CHECK(!gvfs_key(S("smb-share:a=1,server=b%2"), "server", v, sizeof v));   // (half an escape: not one of gvfs's, refused)
    CHECK(!gvfs_key(S("smb-share:server=a%00b"), "server", v, sizeof v));   // (%00: no NUL inside a name)
    CHECK(gvfs_key(S("smb-share:server=%4e%41s"), "server", v, sizeof v) && strcmp(v, "NAs") == 0);
    CHECK(!gvfs_key(S("noколон"), "server", v, sizeof v) && !gvfs_key(S("x:server=a"), "share", v, sizeof v));
    CHECK(!gvfs_key(S("server=a"), "server", v, sizeof v));   // (no "type:": not one of gvfs's)
    CHECK(!gvfs_key(str_cut(S("x:server=b%41"), 0, 12), "server", v, sizeof v));   // (an escape cut off at the name's end: not read past it)
    char tiny[3];
    CHECK(!gvfs_key(S("x:server=abcdef"), "server", tiny, sizeof tiny));   // (too long: false, never a cut value; the code-health review)
    CHECK(gvfs_key(S("x:server=ab"), "server", tiny, sizeof tiny) && strcmp(tiny, "ab") == 0);
}

// The Network section (step 5): the shares gvfs has mounted, then the smb:// bookmarks; no heading
// when there are none; a share's folder isn't a drive, so its space isn't asked.
static void test_places_net(void)
{
    put(at("places/netbm"), "file:///home/m/x\nsmb://nas/pub/sub Music\nsmb://nas/home\nsftp://box/x\nsmb://nas Server\nsmb://u:pw@nas/p\nnfs://box/srv/data\nnfs://box:2049/srv\nsmb://box/\n");
    Places pl = { 0 };
    // (servers found: one a bookmark has, by its address, which keeps the bookmark's label; step 7b)
    static const NetServer SV[] = { { "BOX", "smb://box" }, { "Yoga", "smb://yoga.local" } };
    places_load_net(&ta, &pl, at("places/netbm"), S("/run/u/gvfs/smb-share:server=nas,share=pub\nrel\n\n/run/u/gvfs/sftp:host=box,user=me\n/run/u/gvfs/smb-share:server=nas,share=pub\n"), SV, 2, false);
    const char *want[][3] = {
        { "", "Network", "h" }, { "/run/u/gvfs/smb-share:server=nas,share=pub", "pub on nas", "s" }, { "/run/u/gvfs/sftp:host=box,user=me", "me@box", "s" },
        { "smb://nas/pub/sub", "Music", "b" }, { "smb://nas/home", "home on nas", "b" }, { "sftp://box/x", "box", "b" },   // (an sftp:// one, step 7c: by its host)
        { "smb://nas", "Server", "b" },   // (a server's own, step 7a)
        { "nfs://box/srv/data", "data on box", "b" }, { "smb://box/", "box", "b" },   // (an nfs:// one too, step 6; with a port it isn't one; a server's without a label: its name)
        { "smb://box", "BOX", "v" }, { "smb://yoga.local", "Yoga", "v" },   // (smb://box/ is another address: both kept)
    };
    CHECK(pl.n == COUNT(want));
    for (int i = 0; i < pl.n && i < COUNT(want); i++) {
        PlaceKind k = want[i][2][0] == 'h' ? PL_HEAD : want[i][2][0] == 's' ? PL_SHARE : want[i][2][0] == 'v' ? PL_SERVER : PL_NETBM;
        CHECK(pl.p[i].kind == k && strcmp(pl.p[i].path, want[i][0]) == 0 && strcmp(pl.p[i].label, want[i][1]) == 0);
        CHECK(!place_is_drive(pl.p[i].kind));
    }
    CHECK(place_unmountable(&pl.p[1]) && !place_unmountable(&pl.p[3]) && !place_ejectable(&pl.p[1]));
    CHECK(places_current(&pl, "/run/u/gvfs/smb-share:server=nas,share=pub/a/b") == 1 && places_current(&pl, "smb://nas/home") == -1);
    Str names[3];
    CHECK(place_icon_names(&pl.p[1], &(UserDirs){ 0 }, false, names) == 2 && str_eq(names[0], S("folder-remote")));
    CHECK(place_icon_names(&pl.p[3], &(UserDirs){ 0 }, false, names) == 2 && str_eq(names[0], S("folder-remote")));
    Places none = { 0 };
    places_load_net(&ta, &none, at("places/none"), S(""), NULL, 0, false);
    CHECK(none.n == 0);
    // A share is a place in its own section: a Places bookmark to the same folder doesn't hide it.
    Places both = { 0 };
    places_add(&both, PL_BOOKMARK, "/run/u/gvfs/smb-share:server=nas,share=pub", S("b"));
    places_load_net(&ta, &both, at("places/none"), S("/run/u/gvfs/smb-share:server=nas,share=pub\n"), NULL, 0, false);
    // A server found that a bookmark has: the bookmark's, its label kept.
    Places dup = { 0 };
    put(at("places/dupbm"), "smb://box Office\n");
    places_load_net(&ta, &dup, at("places/dupbm"), S(""), (NetServer[]){ { "BOX", "smb://box" } }, 1, false);
    CHECK(dup.n == 2 && dup.p[1].kind == PL_NETBM && strcmp(dup.p[1].label, "Office") == 0);
    // Only servers: the section's heading and them; a server isn't a drive, has no folder, connects.
    Places sv = { 0 };
    places_load_net(&ta, &sv, at("places/none"), S(""), (NetServer[]){ { "BOX", "smb://box" } }, 1, false);
    CHECK(sv.n == 2 && sv.p[1].kind == PL_SERVER && place_connects(PL_SERVER) && place_connects(PL_NETBM) && !place_connects(PL_SHARE) && place_is_net(PL_SERVER) && !place_unmountable(&sv.p[1]));
    CHECK(places_current(&sv, "smb://box") == -1);
    CHECK(place_icon_names(&sv.p[1], &(UserDirs){ 0 }, false, names) == 3 && str_eq(names[0], S("network-server")) && str_eq(names[1], S("folder-remote")));
    CHECK(both.n == 3 && both.p[2].kind == PL_SHARE);
}

// A share's folder's smb:// address (step 5: Add Bookmark there).
// A server's shares (step 7a): what `gio list` says of them, and a share's address from the
// server's.
static void test_server_shares(void)
{
    static char n[SERVER_SHARES_MAX][SERVER_SHARE_NAME];
    // gio list -a standard::type's lines (measured on the test Samba), Windows' hidden ones, a file,
    // a line without tabs, a name with a control character, and gio's noise
    Str out = S("pub\t0\t(mountable)\nopen\t0\t(mountable)\nPriv\t0\t(mountable)\nIPC$\t0\t(mountable)\nC$\t0\t(mountable)\n"
                "a.txt\t12\t(regular)\njunk\nb\x01d\t0\t(mountable)\nMy Docs\t0\t(mountable)\textra=1\n\t0\t(mountable)\n"
                "invoking IsSupported() failed for remote volume monitor\n");
    int k = gio_server_shares(out, n, SERVER_SHARES_MAX);
    CHECK(k == 4 && strcmp(n[0], "My Docs") == 0 && strcmp(n[1], "open") == 0 && strcmp(n[2], "Priv") == 0 && strcmp(n[3], "pub") == 0);   // (sorted, case folded)
    CHECK(gio_server_shares(S(""), n, SERVER_SHARES_MAX) == 0);
    CHECK(gio_server_shares(S("gio: smb://h/: The specified location is not mounted\n"), n, SERVER_SHARES_MAX) == 0);
    CHECK(gio_server_shares(out, n, 2) == 2);   // (no more than max)
    char u[64];
    CHECK(server_share_uri("smb://127.0.0.1:4445/", "pub", u, sizeof u) && strcmp(u, "smb://127.0.0.1:4445/pub") == 0);   // (the port kept: gvfs's target-uri drops it)
    CHECK(server_share_uri("smb://nas", "My Docs", u, sizeof u) && strcmp(u, "smb://nas/My Docs") == 0);
    CHECK(server_share_uri("smb://me@nas//", "x", u, sizeof u) && strcmp(u, "smb://me@nas/x") == 0);
    char tiny[12];
    CHECK(!server_share_uri("smb://nas/", "pub", tiny, sizeof tiny));
    // and back: Connect takes it as a share's address
    char uri[64], host[32], share[32], why[64];
    CHECK(server_share_uri("smb://nas:445/", "My Docs", u, sizeof u) && connect_uri(str_from_c(u), uri, sizeof uri, host, sizeof host, share, sizeof share, why, sizeof why) == CONNECT_OK &&
          strcmp(host, "nas") == 0 && strcmp(share, "My Docs") == 0);
}

// Servers on the network (step 7b): avahi-browse -r -p's lines (measured on Michael's LAN), escapes,
// each server once, sorted; the lines that aren't one left out.
static void test_avahi_servers(void)
{
    static NetServer s[NET_SERVERS_MAX];
    Str out = S("+;eth0;IPv4;YOGA;_smb._tcp;local\n"
                "=;eth0;IPv6;YOGA;_smb._tcp;local;yoga.local;fe80::3e18:a0ff:fe04:68b;445;\"model=MacSamba\"\n"
                "=;eth0;IPv4;YOGA;_smb._tcp;local;yoga.local;192.168.1.165;445;\"model=MacSamba\"\n"
                "=;eth0;IPv4;My\\032NAS\\059x;_smb._tcp;local;nas-1.local;192.168.1.9;4445;\n"
                "=;eth0;IPv4;alpha;_smb._tcp;local;alpha.local;192.168.1.7;445;\n"
                "=;eth0;IPv4;Printer;_ipp._tcp;local;p.local;192.168.1.8;631;\n"
                "=;eth0;IPv4;Bad;_smb._tcp;local;bad host.local;192.168.1.10;445;\n"
                "=;eth0;IPv4;BadPort;_smb._tcp;local;bp.local;192.168.1.11;44x;\n"
                "=;eth0;IPv4;Zero;_smb._tcp;local;z.local;192.168.1.12;0;\n"
                "=;eth0;IPv4;Ctl\\001;_smb._tcp;local;c.local;192.168.1.13;445;\n"
                "=;eth0;IPv4;Short;_smb._tcp\n"
                "=;eth0;IPv4;NoK;Microsoft Windows Network;local;nok.local;192.168.1.14;445;\n"   // (without -k: its database name, not taken)
                "Failed to create client object: Daemon not running\n");
    int n = avahi_smb_servers(out, s, NET_SERVERS_MAX);
    CHECK(n == 3);
    CHECK(strcmp(s[0].name, "alpha") == 0 && strcmp(s[0].addr, "smb://alpha.local") == 0);
    CHECK(strcmp(s[1].name, "My NAS;x") == 0 && strcmp(s[1].addr, "smb://nas-1.local:4445") == 0);   // (\032 a space, \059 ';'; the port kept)
    CHECK(strcmp(s[2].name, "YOGA") == 0 && strcmp(s[2].addr, "smb://yoga.local") == 0);   // (once for IPv4 and IPv6; 445 left out)
    CHECK(avahi_smb_servers(S(""), s, NET_SERVERS_MAX) == 0);
    CHECK(avahi_smb_servers(out, s, 1) == 1);
    char u[32];
    avahi_unescape(S("A\\000B\\300C"), u, sizeof u);   // (\000 and past 255: no such byte, left out)
    CHECK(strcmp(u, "ABC") == 0);
    avahi_unescape(S("a\\\\b\\.c\\"), u, sizeof u);   // (\\ and \. : the character; a lone \ at the end kept)
    CHECK(strcmp(u, "a\\b.c\\") == 0);
    // and Connect takes a server's address
    char uri[64], host[32], share[32], why[64];
    CHECK(connect_uri(S("smb://nas-1.local:4445"), uri, sizeof uri, host, sizeof host, share, sizeof share, why, sizeof why) == CONNECT_SERVER && share[0] == 0 && strcmp(host, "nas-1.local") == 0);
}

// The shares panel (step 7a): the branches panel's picker over a server's shares.
static void test_shares_panel(void)
{
    UiNames names = { name_at, NULL, NULL };
    static const Str SH[] = { { "music", 5 }, { "open", 4 }, { "priv", 4 }, { "pub", 3 } };
    Ui u = { 0 };
    u.shp.list.rows = 3;
    ui_shp_open(&u);
    CHECK(u.panel == UI_PANEL_SHARES && ui_on_top(&u) == UI_ON_SHARES && u.shp.fresh);
    UiInput in = ui_input_none();
    in.sh_name = SH, in.sh_names = COUNT(SH);
    ui_handle(&u, names, &in);   // (filtered the frame it opens)
    CHECK(!u.shp.fresh && u.shp.list.n == 4);
    in = chars("pu"), in.sh_name = SH, in.sh_names = COUNT(SH);
    ui_handle(&u, names, &in);
    CHECK(u.shp.list.n == 1 && u.shp_idx[0] == 3);
    UiLine l;
    CHECK(ui_line_active(&u, &l) && l.b == u.shp.filter.b);   // (its field has the keys: a Ctrl+V pastes there)
    in = keys(1, UK_ENTER), in.sh_name = SH, in.sh_names = COUNT(SH);
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_SHARE && a.index == 3 && u.panel == UI_PANEL_NONE);
    ui_shp_open(&u);
    in = ui_input_none(), in.sh_name = SH, in.sh_names = COUNT(SH);
    ui_handle(&u, names, &in);
    in = keys(2, UK_DOWN, UK_DOWN), in.sh_name = SH, in.sh_names = COUNT(SH);
    ui_handle(&u, names, &in);
    CHECK(u.shp.in_list && u.shp.list.sel == 1);
    in = ui_input_none(), in.sh_name = SH, in.sh_names = COUNT(SH), in.pick_drow = 2;   // (a double click: the row under it)
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_SHARE && a.index == 2 && u.panel == UI_PANEL_NONE);
    ui_shp_open(&u);
    in = ui_input_none(), in.sh_name = SH, in.sh_names = COUNT(SH), in.modal = UI_MODAL_YES;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_SHARE && a.index == 0);
    ui_shp_open(&u);
    in = keys(1, UK_ESC), in.sh_name = SH, in.sh_names = COUNT(SH);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.panel == UI_PANEL_NONE);
    ui_shp_open(&u);
    in = ui_input_none(), in.sh_name = SH, in.sh_names = COUNT(SH), in.modal = UI_MODAL_NO;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.panel == UI_PANEL_NONE);
    // Nothing matches: Enter does nothing.
    ui_shp_open(&u);
    in = chars("zz"), in.sh_name = SH, in.sh_names = COUNT(SH);
    ui_handle(&u, names, &in);
    in = keys(1, UK_ENTER), in.sh_name = SH, in.sh_names = COUNT(SH);
    CHECK(u.shp.list.n == 0 && ui_handle(&u, names, &in).kind == UA_NONE && u.panel == UI_PANEL_SHARES);
}

static void test_gvfs_uri(void)
{
    char u[128];
    CHECK(gvfs_share_uri(S("smb-share:port=4445,server=127.0.0.1,share=pub"), "", u, sizeof u) && strcmp(u, "smb://127.0.0.1:4445/pub") == 0);
    CHECK(gvfs_share_uri(S("smb-share:server=nas,share=My%20Docs"), "/a b/c", u, sizeof u) && strcmp(u, "smb://nas/My%20Docs/a%20b/c") == 0);
    CHECK(gvfs_share_uri(S("smb-share:domain=W,server=nas,share=h,user=m"), "", u, sizeof u) && strcmp(u, "smb://W;m@nas/h") == 0);
    CHECK(gvfs_share_uri(S("smb-share:domain=W,server=nas,share=h"), "", u, sizeof u) && strcmp(u, "smb://nas/h") == 0);   // (a domain without a user: none)
    CHECK(gvfs_share_uri(S("sftp:host=box,user=me"), "", u, sizeof u) && strcmp(u, "sftp://me@box/") == 0 && !gvfs_share_uri(S("dav:server=a,share=b"), "", u, sizeof u));   // (sftp: step 7c)
    CHECK(!gvfs_share_uri(S("smb-share:server=nas"), "", u, sizeof u) && !gvfs_share_uri(S("smb-share:share=x"), "", u, sizeof u));
    // SFTP (step 7c): sftp://[user@]host[:port]/path, the path from the server's root
    CHECK(gvfs_share_uri(S("sftp:host=127.0.0.1,port=38165,user=tester"), "", u, sizeof u) && strcmp(u, "sftp://tester@127.0.0.1:38165/") == 0);
    CHECK(gvfs_share_uri(S("sftp:host=box"), "/home/me/a b", u, sizeof u) && strcmp(u, "sftp://box/home/me/a%20b") == 0);
    CHECK(!gvfs_share_uri(S("sftp:user=me"), "", u, sizeof u));
    // NFS (step 6): nfs://host/prefix/rest
    CHECK(gvfs_share_uri(S("nfs:host=127.0.0.1,prefix=%2Fsrv%2Fnfs"), "", u, sizeof u) && strcmp(u, "nfs://127.0.0.1/srv/nfs") == 0);
    CHECK(gvfs_share_uri(S("nfs:host=nas,prefix=%2Fsrv%2Fnfs"), "/a b/c", u, sizeof u) && strcmp(u, "nfs://nas/srv/nfs/a%20b/c") == 0);
    CHECK(gvfs_share_uri(S("nfs:host=nas,prefix=%2Fsrv%2F"), "/x", u, sizeof u) && strcmp(u, "nfs://nas/srv/x") == 0);   // (a prefix ending in /: one /)
    CHECK(gvfs_share_uri(S("nfs:host=nas,prefix=%2F"), "/x", u, sizeof u) && strcmp(u, "nfs://nas/x") == 0);   // (the root)
    CHECK(gvfs_share_uri(S("nfs:host=nas,prefix=%2F"), "", u, sizeof u) && strcmp(u, "nfs://nas/") == 0);
    CHECK(!gvfs_share_uri(S("nfs:prefix=%2Fsrv"), "", u, sizeof u) && !gvfs_share_uri(S("nfs:host=nas"), "", u, sizeof u) && !gvfs_share_uri(S("nfs:host=nas,prefix=srv"), "", u, sizeof u));
    char tiny[12];
    CHECK(!gvfs_share_uri(S("smb-share:server=nas,share=pub"), "/long", tiny, sizeof tiny));
    CHECK(!gvfs_share_uri(S("nfs:host=nas,prefix=%2Fsrv"), "/long", tiny, sizeof tiny));
    // and back: the address Connect takes, naming the same share
    char uri[128], host[32], share[32], why[64];
    CHECK(gvfs_share_uri(S("smb-share:port=4445,server=nas,share=pub,user=m"), "/s", u, sizeof u) && connect_uri(str_from_c(u), uri, sizeof uri, host, sizeof host, share, sizeof share, why, sizeof why) == CONNECT_OK &&
          strcmp(host, "nas") == 0 && strcmp(share, "pub") == 0);
    CHECK(gvfs_share_uri(S("nfs:host=nas,prefix=%2Fsrv%2Fnfs"), "/s", u, sizeof u) && connect_uri(str_from_c(u), uri, sizeof uri, host, sizeof host, share, sizeof share, why, sizeof why) == CONNECT_OK &&
          strcmp(host, "nas") == 0 && strcmp(share, "s") == 0);
}

// A share's path buttons (step 5): from the share, named as gvfs names it, not from / or home.
static void test_crumbs_share(void)
{
    UiCrumbs c = { 0 };
    const char *sh = "/run/u/gvfs/smb-share:server=nas,share=pub";
    ui_crumbs_set_share(&c, "/run/u/gvfs/smb-share:server=nas,share=pub/a/b", "/home/m", sh, "pub on nas");
    CHECK(c.n == 3 && c.seg[0].share && !c.seg[0].home && str_eq(ui_crumb_label(&c, 0), S("pub on nas")) && str_eq(ui_crumb_path(&c, 0), str_from_c(sh)));
    CHECK(str_eq(ui_crumb_label(&c, 1), S("a")) && str_eq(ui_crumb_label(&c, 2), S("b")) && c.cur == 2 && !c.seg[1].share);
    ui_crumbs_set_share(&c, sh, "/home/m", sh, "pub on nas");   // (up: the deeper ones kept, as anywhere)
    CHECK(c.n == 3 && c.cur == 0);
    ui_crumbs_set_share(&c, "/home/m/x", "/home/m", NULL, NULL);   // (out of it: from home again)
    CHECK(c.seg[0].home && !c.seg[0].share && c.n == 2);
    ui_crumbs_set_share(&c, "/run/u/gvfs/other", "/home/m", sh, "pub on nas");   // (a share not cwd's: none)
    CHECK(!c.seg[0].share && str_eq(ui_crumb_label(&c, 0), S("/")));
    ui_crumbs_set_share(&c, "/run/u/gvfs/smb-share:server=nas,share=pubx", "/home/m", sh, "pub on nas");   // (a prefix isn't the share)
    CHECK(!c.seg[0].share);
    ui_crumbs_set_share(&c, "/home/m/s", "/home/m/s", "/home/m/s", NULL);   // (no label: none)
    CHECK(!c.seg[0].share && c.seg[0].home);
}

// ofx-list --shares (step 5 review): gvfs's FUSE folders' shares, sorted, then "."; only gvfs's
// mounts and names of its form; nothing for a mount list that reads empty.
static void test_gvfs_shares(void)
{
    mkdir(at("gv"), 0755), mkdir(at("gv/g"), 0755), mkdir(at("gv/o"), 0755);
    // (made in order a, c, b: a folder lists in an order of its own, tmpfs newest first)
    mkdir(at("gv/g/smb-share:server=a,share=x"), 0755), mkdir(at("gv/g/smb-share:server=c,share=z"), 0755), mkdir(at("gv/g/smb-share:server=b,share=y"), 0755);
    mkdir(at("gv/g/junk"), 0755), mkdir(at("gv/g/My Notes:x=1"), 0755), mkdir(at("gv/g/a:b"), 0755);
    mkdir(at("gv/o/smb-share:server=no,share=no"), 0755);
    char mi[2048];
    snprintf(mi, sizeof mi, "1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw - fuse.gvfsd-fuse g rw\n", at("gv/g"));
    snprintf(mi + strlen(mi), sizeof mi - strlen(mi), "3 1 0:3 / %s rw - ext4 /dev/y rw\n", at("gv/o"));
    char *buf = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&buf, &len);
    CHECK(gvfs_shares(f, &ta, str_from_c(mi)));
    fclose(f);
    char want[3 * PATH_MAX + 256];
    char g[PATH_MAX];
    snprintf(g, sizeof g, "%s", at("gv/g"));
    snprintf(want, sizeof want, "%s/smb-share:server=a,share=x\n%s/smb-share:server=b,share=y\n%s/smb-share:server=c,share=z\n.\n", g, g, g);
    CHECK(buf && strcmp(buf, want) == 0);
    free(buf);
    f = open_memstream(&buf, &len);
    CHECK(!gvfs_shares(f, &ta, STR_EMPTY));
    fclose(f);
    CHECK(len == 0);
    free(buf);
    CHECK(gvfs_is_share("smb-share:server=a") && gvfs_is_share("sftp:host=h") && !gvfs_is_share("junk") && !gvfs_is_share(":x=1") &&
          !gvfs_is_share("Ab:x=1") && !gvfs_is_share("a b:x=1") && !gvfs_is_share("a:b"));
}

// Airplane Mode (step 7d): the state file's line, the Network section's one row, the menu entry, the
// sidebar row's menu and question; and the share poll's back-off.
static void test_airplane(void)
{
    put(at("air/state"), "sidebar=places\nairplane=on\n");
    CHECK(state_load(&ta, at("air/state")).airplane);
    put(at("air/state"), "airplane=off\n");
    CHECK(!state_load(&ta, at("air/state")).airplane);
    put(at("air/state"), "hidden=on\n");
    CHECK(!state_load(&ta, at("air/state")).airplane);
    // The Network section: its heading and the row, nothing else (bookmarks, shares, servers left out).
    put(at("air/bm"), "smb://nas/pub Pub\n");
    Places pl = { 0 };
    places_load_net(&ta, &pl, at("air/bm"), S("/run/u/gvfs/smb-share:server=nas,share=pub\n"), (NetServer[]){ { "BOX", "smb://box" } }, 1, true);
    CHECK(pl.n == 2 && pl.p[0].kind == PL_HEAD && pl.p[1].kind == PL_AIRPLANE && strcmp(pl.p[1].label, "Airplane mode") == 0);
    CHECK(place_folderless(PL_AIRPLANE) && place_folderless(PL_UNMOUNTED) && place_folderless(PL_NETBM) && place_folderless(PL_SERVER) &&
          !place_folderless(PL_SHARE) && !place_folderless(PL_MOUNT) && !place_connects(PL_AIRPLANE) && !place_is_drive(PL_AIRPLANE) && !place_unmountable(&pl.p[1]));
    CHECK(places_current(&pl, "airplane:") == -1);
    Str names[3];
    CHECK(place_icon_names(&pl.p[1], &(UserDirs){ 0 }, false, names) == 3 && str_eq(names[0], S("airplane-mode")) && str_eq(names[1], S("airplane-mode-symbolic")));
    Places off = { 0 };
    places_load_net(&ta, &off, at("air/bm"), S(""), NULL, 0, false);
    CHECK(off.n == 2 && off.p[1].kind == PL_NETBM);
    // Go → Airplane Mode: a check, on and off, each said to gui/ (bindable: its word).
    Ui u = { 0 };
    CHECK(ui_menu_shown(&u, UM_AIRPLANE) && ui_menu_enabled(&u, UM_AIRPLANE) && ui_menu_mark(&u, UM_AIRPLANE) == 0);
    UiAction a = ui_menu_pick(&u, UM_AIRPLANE);
    CHECK(a.kind == UA_AIRPLANE && u.airplane && ui_menu_mark(&u, UM_AIRPLANE) == 1);
    a = ui_menu_pick(&u, UM_AIRPLANE);
    CHECK(a.kind == UA_AIRPLANE && !u.airplane);
    CHECK(strcmp(UI_MENU_ITEM[UM_AIRPLANE].word, "airplane-mode") == 0);
    bool in_go = false;
    for (isize i = 0; i < COUNT(UI_MENU_GO); i++) in_go |= UI_MENU_GO[i] == UM_AIRPLANE && i > 0 && UI_MENU_GO[i - 1] == UM_CONNECT;
    CHECK(in_go);
    // The row's own menu: Turn Off Airplane Mode alone (no check, so the folder menu keeps no column for
    // one); picked there, it turns off as from Go.
    Ui f = { .airplane = true, .menu = UI_CTX_FOLDER, .ctx_airplane = true };
    CHECK(ui_folder_shown(&f, UM_AIRPLANE_OFF_IT) && !ui_folder_shown(&f, UM_OPEN_IT) && !ui_folder_shown(&f, UM_MOUNT_IT) && !ui_folder_shown(&f, UM_PROPS_IT));
    CHECK(UI_MENU_ITEM[UM_AIRPLANE_OFF_IT].mark == 0);
    f.ctx_airplane = false, f.ctx_unmounted = true;
    CHECK(!ui_folder_shown(&f, UM_AIRPLANE_OFF_IT));
    f.ctx_unmounted = false;
    CHECK(!ui_folder_shown(&f, UM_AIRPLANE_OFF_IT));
    f.ctx_airplane = true;
    ui_folder_open(&f, 3, -1, false);   // (another row's menu: gui/ says again what it is)
    CHECK(!f.ctx_airplane);
    f.ctx_airplane = true, f.menu = UI_CTX_FOLDER;
    a = ui_menu_pick(&f, UM_AIRPLANE_OFF_IT);
    CHECK(a.kind == UA_AIRPLANE && !f.airplane);
    // The row's question: Enter or y turns it off, n or Esc keeps it, and its buttons the same.
    Ui q = { .airplane = true, .airplane_asking = true };
    CHECK(ui_on_top(&q) == UI_ON_AIRPLANE && ui_is_question(UI_ON_AIRPLANE));
    UiInput in = ui_input_none();
    in.ev[in.nev++] = (UiEvent){ .key = UK_NONE, .ch = 'n' };
    a = ui_handle(&q, (UiNames){ 0 }, &in);
    CHECK(a.kind == UA_NONE && q.airplane && !q.airplane_asking);
    q.airplane_asking = true, in = ui_input_none(), in.ev[in.nev++] = (UiEvent){ .key = UK_ENTER };
    a = ui_handle(&q, (UiNames){ 0 }, &in);
    CHECK(a.kind == UA_AIRPLANE && !q.airplane && !q.airplane_asking);
    q.airplane = q.airplane_asking = true, in = ui_input_none(), in.modal = UI_MODAL_NO;
    a = ui_handle(&q, (UiNames){ 0 }, &in);
    CHECK(a.kind == UA_NONE && q.airplane && !q.airplane_asking);
    q.airplane_asking = true, in = ui_input_none(), in.modal = UI_MODAL_YES;
    a = ui_handle(&q, (UiNames){ 0 }, &in);
    CHECK(a.kind == UA_AIRPLANE && !q.airplane && !q.airplane_asking);
    // The share poll's waits: 2, 4, 8, 16, then 30 s; back to 2 at a reset.
    int w = 0, seen[7];
    for (int i = 0; i < 7; i++) seen[i] = w = share_poll_next(w, 2000, 30000, false);
    CHECK(seen[0] == 2000 && seen[1] == 4000 && seen[2] == 8000 && seen[3] == 16000 && seen[4] == 30000 && seen[5] == 30000 && seen[6] == 30000);
    CHECK(share_poll_next(30000, 2000, 30000, true) == 2000 && share_poll_next(16000, 2000, 30000, true) == 2000);
    CHECK(share_poll_next(4000, 2000, 5000, false) == 5000 && share_poll_next(2000, 2000, 2000, false) == 2000);
}
