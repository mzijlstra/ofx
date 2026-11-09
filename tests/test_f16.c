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
    Str mounts = str_from_c(arena_sprintf(&ta, "1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw - fuse.gvfsd-fuse g rw\n3 1 0:3 / %s rw - nfs4 h:/x rw\n",
                                          at("flf/share"), at("flf/home/mp")));
    Listing l;
    Err err;
    CHECK(fs_list_far(&ta, at("flf/home"), false, (Sort){ SORT_NAME, false }, mounts, &l, &err) && l.n == 5);
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
    Str m2 = str_from_c(arena_sprintf(&ta, "1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw - fuse.sshfs s rw\n", at("flf/real/nas")));
    CHECK(fs_list_far(&ta, at("flf/proj"), false, (Sort){ SORT_NAME, false }, m2, &l, &err) && l.n == 1);
    CHECK(l.e[0].kind == K_LINK_DIR && l.e[0].far);
    // The tree from the same linked folder: nasf (a far "file") still a folder, nasd (a far folder
    // with a child) open but not read; both judged from flf/real/proj, not the path's text.
    put(at("flf/real/nasf"), "f");
    mkdirs(at("flf/real/nasd/c"));
    CHECK(symlink("../nasf", at("flf/real/proj/nasf")) == 0 && symlink("../nasd", at("flf/real/proj/nasd")) == 0);
    Str m3 = str_from_c(arena_sprintf(&ta, "1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw - fuse.sshfs s rw\n3 1 0:3 / %s rw - fuse.sshfs s rw\n",
                                      at("flf/real/nasf"), at("flf/real/nasd")));
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
    t.mounts = STR_EMPTY;
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
    Str mounts = str_from_c(arena_sprintf(&ta, "1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw - fuse.gvfsd-fuse g rw\n", at("fr/share")));
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
        bool ok = far_real(mounts, at(c[i].path), out, sizeof out, &far);
        char want[PATH_MAX + 16];
        snprintf(want, sizeof want, "%s%s", real, c[i].want);
        if (!ok || strcmp(out, want) != 0 || far != c[i].far) fprintf(stderr, "far_real %s: %d %s %d\n", c[i].path, ok, ok ? out : strerror(errno), far);
        CHECK(ok && strcmp(out, want) == 0 && far == c[i].far);
    }
    CHECK(far_real(mounts, "/", out, sizeof out, &far) && strcmp(out, "/") == 0 && !far);
    // Errors as realpath's, from the local parts.
    struct { const char *path; int e; } bad[] = {
        { "fr/home/missing/x", ENOENT }, { "fr/home/x.txt/y", ENOTDIR }, { "fr/home/loop", ELOOP },
        { "fr/home/x.txt/..", ENOTDIR }, { "fr/home/x.txt/", ENOTDIR },   // (a file isn't gone through, even by "..")
    };
    for (int i = 0; i < COUNT(bad); i++) {
        errno = 0;
        CHECK(!far_real(mounts, at(bad[i].path), out, sizeof out, &far) && errno == bad[i].e);
    }
    // No mount list: realpath's answer (share is a plain folder here).
    CHECK(far_real(STR_EMPTY, at("fr/home/mylinks/nas/d"), out, sizeof out, &far) && !far);
    char want[PATH_MAX + 16];
    snprintf(want, sizeof want, "%s/share/d", real);
    CHECK(strcmp(out, want) == 0);
    CHECK(!far_real(STR_EMPTY, at("fr/home/nas/missing"), out, sizeof out, &far) && errno == ENOENT);
    // A part longer than a path can be: refused before it is put together.
    char *longp = arena_sprintf(&ta, "%s/%0*d", at("fr/home"), 5000, 0);
    CHECK(!far_real(mounts, longp, out, sizeof out, &far) && errno == ENAMETOOLONG);
    // Relative: from the current folder (ofx's start, ".").
    int here = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(here >= 0 && chdir(at("fr/home")) == 0);
    CHECK(far_real(mounts, "mylinks/nas/q", out, sizeof out, &far) && far);
    snprintf(want, sizeof want, "%s/share/q", real);
    CHECK(strcmp(out, want) == 0);
    CHECK(far_real(mounts, ".", out, sizeof out, &far) && !far);
    snprintf(want, sizeof want, "%s/home", real);
    CHECK(strcmp(out, want) == 0);
    CHECK(here >= 0 && fchdir(here) == 0);
    if (here >= 0) close(here);
    // Too small an out: refused, not cut.
    char small[8];
    CHECK(!far_real(mounts, at("fr/home"), small, sizeof small, &far) && errno == ENAMETOOLONG);
    // A listing in fr/home: chain, a chain through a local link into the share, is far now
    // (step 2b's known gap), not followed.
    CHECK(symlink("mylinks/nas", at("fr/home/chain")) == 0);
    Listing l;
    Err err;
    CHECK(fs_list_far(&ta, at("fr/home"), false, (Sort){ SORT_NAME, false }, mounts, &l, &err));
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
    Str mounts = str_from_c(arena_sprintf(&ta, "1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw - fuse.gvfsd-fuse g rw\n", sh));
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
    CHECK(FD("free", d, "a\nb.txt", "0", "1", "1") && fd_r.ok && str_eq(fd_r.f[0], S("a\nb (copy).txt")));
    CHECK(FD("free", d, "z.txt", "0", "0", "0") && fd_r.ok && str_eq(fd_r.f[0], S("z.txt")));
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
    CHECK(!far_reply_parse(S("ok 9\n0\n0\n0\n0\n0\n0\n0\n0\n0\n"), &fd_r));   // (more fields than it holds)
    char *two = malloc(2);   // (a header too short, the last bytes there are: not read past)
    CHECK(two != NULL);
    if (two) {
        memcpy(two, "ok", 2);
        CHECK(!far_reply_parse((Str){ two, 2 }, &fd_r));
        free(two);
    }
}
