// F3's unit tests: the guard, names, rename, new folder, delete for good, the trash, the clipboard
// payloads, copy/move/delete jobs and undo. Included by test.c after its fixtures; everything
// happens under its mkdtemp root, with the guard set to it.

#include <fcntl.h>
#include <sys/time.h>

// A folder beside the root, outside the guard: a guard test aims at it, so a mutant that switches
// the guard off can only ever reach this decoy, never /tmp or anything of Michael's.
static char decoy[300];

static const char *out_at(const char *rel)
{
    static char buf[4][600];
    static int k;
    k = (k + 1) % 4;
    snprintf(buf[k], sizeof buf[k], "%s/%s", decoy, rel);
    return buf[k];
}

static bool there(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0;
}

static bool is_dir_at(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// A small file's text, into a static buffer ("" when missing).
static const char *slurp(const char *path)
{
    static char buf[4][8192];
    static int k;
    k = (k + 1) % 4;
    buf[k][0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return buf[k];
    size_t n = fread(buf[k], 1, sizeof buf[k] - 1, f);
    buf[k][n] = 0;
    fclose(f);
    return buf[k];
}

// Whether any ".fx-part-" entry is in dir.
static bool has_part(const char *dir)
{
    DIR *d = opendir(dir);
    bool found = false;
    for (struct dirent *de; d && (de = readdir(d));)
        if (strncmp(de->d_name, ".fx-part-", 9) == 0) found = true;
    if (d) closedir(d);
    return found;
}

static void set_mtime(const char *path, time_t t)
{
    struct timeval tv[2] = { { t, 0 }, { t, 0 } };
    lutimes(path, tv);
}

static void test_ops(void)
{
    ops_guard(root);
    Err err;
    mkdirs(at("ops"));
    CHECK(ops_allowed(at("ops/new.txt"), &err));
    CHECK(ops_allowed(at("ops"), &err));
    int refused = ops_refusals;
    CHECK(!ops_allowed(out_at("not-mine.txt"), &err));
    CHECK(strstr(err.msg, "outside the test folder") != NULL);
    CHECK(!ops_allowed(at("ops/../../escape"), &err));
    CHECK(!ops_allowed(root, &err));   // the root itself is not under it
    // A link inside that points out: its folder resolves outside.
    symlink(decoy, at("ops/out"));
    CHECK(!ops_allowed(at("ops/out/x"), &err));
    CHECK(ops_allowed(at("ops/out"), &err));   // the link itself is inside
    CHECK(ops_refusals == refused + 4);

    // A guard that can't be resolved refuses everything.
    ops_guard("/nonexistent/fx-root");
    CHECK(!ops_allowed(at("ops/x"), &err));
    ops_guard(NULL);
    CHECK(ops_allowed(out_at("anything"), &err));
    ops_guard(root);

    CHECK(name_valid(S("a.txt"), &err));
    CHECK(!name_valid(S(""), &err));
    CHECK(!name_valid(S("a/b"), &err));
    CHECK(!name_valid(S("."), &err));
    CHECK(!name_valid(S(".."), &err));
    CHECK(name_valid(S("..."), &err));
    char longname[NAME_MAX + 2];
    memset(longname, 'x', sizeof longname - 1), longname[NAME_MAX + 1] = 0;
    CHECK(!name_valid(str_from_c(longname), &err));
    longname[NAME_MAX] = 0;
    CHECK(name_valid(str_from_c(longname), &err));

    char out[NAME_MAX + 1];
    name_tagged(out, sizeof out, S("a.txt"), false, " (2)");
    CHECK(strcmp(out, "a (2).txt") == 0);
    name_tagged(out, sizeof out, S("a.tar.gz"), false, " (2)");
    CHECK(strcmp(out, "a.tar (2).gz") == 0);
    name_tagged(out, sizeof out, S(".bashrc"), false, " (2)");
    CHECK(strcmp(out, ".bashrc (2)") == 0);
    name_tagged(out, sizeof out, S("v1.2"), true, " (2)");
    CHECK(strcmp(out, "v1.2 (2)") == 0);
    name_tagged(out, sizeof out, S("plain"), false, " (copy)");
    CHECK(strcmp(out, "plain (copy)") == 0);

    put(at("ops/a.txt"), "a");
    CHECK(name_free(at("ops"), S("b.txt"), false, false, false, out, sizeof out) && strcmp(out, "b.txt") == 0);
    CHECK(name_free(at("ops"), S("a.txt"), false, false, false, out, sizeof out) && strcmp(out, "a (2).txt") == 0);
    put(at("ops/a (2).txt"), "a");
    CHECK(name_free(at("ops"), S("a.txt"), false, false, false, out, sizeof out) && strcmp(out, "a (3).txt") == 0);
    CHECK(name_free(at("ops"), S("b.txt"), false, false, true, out, sizeof out) && strcmp(out, "b (2).txt") == 0);
    CHECK(name_free(at("ops"), S("a.txt"), false, true, true, out, sizeof out) && strcmp(out, "a (copy).txt") == 0);
    put(at("ops/a (copy).txt"), "a");
    CHECK(name_free(at("ops"), S("a.txt"), false, true, true, out, sizeof out) && strcmp(out, "a (copy 2).txt") == 0);
    // A broken link takes its name too.
    symlink("nothing-here", at("ops/c.txt"));
    CHECK(name_free(at("ops"), S("c.txt"), false, false, false, out, sizeof out) && strcmp(out, "c (2).txt") == 0);

    // rename: never over something there.
    put(at("ops/r1"), "one");
    put(at("ops/r2"), "two");
    CHECK(op_rename(at("ops"), S("r1"), S("r3"), &err));
    CHECK(!there(at("ops/r1")) && strcmp(slurp(at("ops/r3")), "one") == 0);
    CHECK(!op_rename(at("ops"), S("r3"), S("r2"), &err));
    CHECK(strcmp(err.msg, "r2 exists") == 0);
    CHECK(strcmp(slurp(at("ops/r2")), "two") == 0 && strcmp(slurp(at("ops/r3")), "one") == 0);
    CHECK(!op_rename(at("ops"), S("r3"), S("x/y"), &err));
    CHECK(!op_rename(at("ops"), S("nope"), S("r9"), &err));
    CHECK(strstr(err.msg, "No such file") != NULL);
    // The guard: a rename out of the root is refused before it happens.
    CHECK(!move_noreplace(at("ops/r3"), out_at("escaped"), &err));
    CHECK(there(at("ops/r3")) && !there(out_at("escaped")));

    // new folder
    char made[NAME_MAX + 1];
    CHECK(op_mkdir(at("ops"), S("New folder"), made, sizeof made, &err) && strcmp(made, "New folder") == 0);
    CHECK(is_dir_at(at("ops/New folder")));
    CHECK(op_mkdir(at("ops"), S("New folder"), made, sizeof made, &err) && strcmp(made, "New folder (2)") == 0);
    CHECK(is_dir_at(at("ops/New folder (2)")));
    CHECK(!op_mkdir(at("ops"), S("a/b"), made, sizeof made, &err));
    CHECK(!op_mkdir(at("ops/missing"), S("x"), made, sizeof made, &err));
    CHECK(!op_mkdir(decoy, S("x"), made, sizeof made, &err) && !there(out_at("x")));   // the guard

    // delete for good: depth first, links removed and not followed.
    mkdirs(at("del/keep"));
    put(at("del/keep/precious"), "p");
    mkdirs(at("del/t/a/b/c"));
    put(at("del/t/a/b/c/f"), "f");
    put(at("del/t/a/g"), "g");
    symlink(at("del/keep"), at("del/t/link"));
    for (int i = 0; i < 300; i++) put(at("del/t/many/%03d", i), "m");
    isize n = 0;
    CHECK(delete_tree(at("del/t"), &n, &err));
    CHECK(!there(at("del/t")));
    CHECK(strcmp(slurp(at("del/keep/precious")), "p") == 0);
    CHECK(n == 1 + 300 + 1 + 1 + 1 + 1 + 1 + 1 + 1);   // many/*, many, a, b, c, f, g, link, t
    symlink(at("del/keep"), at("del/l2"));
    n = 0;
    CHECK(delete_tree(at("del/l2"), &n, &err) && n == 1);
    CHECK(!there(at("del/l2")) && there(at("del/keep/precious")));
    CHECK(!delete_tree(at("del/none"), &n, &err));
    put(out_at("keep.txt"), "k");
    CHECK(!delete_tree(out_at("keep.txt"), &n, &err) && there(out_at("keep.txt")));   // the guard
    // A folder it can't read: the error, and what it holds stays.
    mkdirs(at("del/locked/in"));
    chmod(at("del/locked"), 0300);
    n = 0;
    bool root_user = geteuid() == 0;
    if (!root_user) CHECK(!delete_tree(at("del/locked"), &n, &err));
    chmod(at("del/locked"), 0755);
    // Nested deeper than TREE_DEPTH_MAX: refused with a reason, not a crash (and the levels above
    // the limit stay).
    char deep[8192] = "";
    snprintf(deep, sizeof deep, "%s", at("deep"));
    mkdir(deep, 0755);
    for (int i = 0; i < TREE_DEPTH_MAX + 4; i++) {
        size_t k = strlen(deep);
        snprintf(deep + k, sizeof deep - k, "/d");
        mkdir(deep, 0755);
    }
    n = 0;
    CHECK(!delete_tree(at("deep"), &n, &err) && strstr(err.msg, "nested too deep") && there(at("deep/d")));
}

static void test_trash(void)
{
    ops_guard(root);
    Err err;
    env_set("XDG_DATA_HOME", at("data"));
    mkdirs(at("data"));
    put(at("tr/a.txt"), "A");
    Trashed t;
    CHECK(trash_put(at("tr/a.txt"), &t, &err));
    CHECK(!there(at("tr/a.txt")));
    CHECK(strcmp(t.file, at("data/Trash/files/a.txt")) == 0);
    CHECK(strcmp(t.info, at("data/Trash/info/a.txt.trashinfo")) == 0);
    CHECK(strcmp(slurp(t.file), "A") == 0);
    char want[1400];
    snprintf(want, sizeof want, "[Trash Info]\nPath=%s\nDeletionDate=", at("tr/a.txt"));
    const char *info = slurp(t.info);
    CHECK(strncmp(info, want, strlen(want)) == 0);
    // DeletionDate=YYYY-MM-DDThh:mm:ss
    const char *d = strstr(info, "DeletionDate=");
    CHECK(d && strlen(d) == strlen("DeletionDate=2026-10-20T12:00:00\n") && d[17] == '-' && d[23] == 'T');
    struct stat st;
    CHECK(stat(at("data/Trash/files"), &st) == 0 && (st.st_mode & 0777) == 0700);
    CHECK(strcmp(t.from, at("tr/a.txt")) == 0);

    // The same name again: "a.txt.2", picked by the info file.
    put(at("tr/a.txt"), "B");
    Trashed t2;
    CHECK(trash_put(at("tr/a.txt"), &t2, &err));
    CHECK(strcmp(t2.file, at("data/Trash/files/a.txt.2")) == 0 && strcmp(slurp(t2.file), "B") == 0);
    CHECK(strcmp(t2.info, at("data/Trash/info/a.txt.2.trashinfo")) == 0);
    // A stray file in files/ under a name takes it too.
    put(at("data/Trash/files/s.txt"), "stray");
    put(at("tr/s.txt"), "S");
    CHECK(trash_put(at("tr/s.txt"), &t2, &err) && strcmp(t2.file, at("data/Trash/files/s.txt.2")) == 0);
    // A stray .trashinfo without its file takes its name too (O_EXCL).
    put(at("data/Trash/info/z.txt.trashinfo"), "[Trash Info]\n");
    put(at("tr/z.txt"), "Z");
    CHECK(trash_put(at("tr/z.txt"), &t2, &err) && strcmp(t2.file, at("data/Trash/files/z.txt.2")) == 0);
    CHECK(strcmp(slurp(at("data/Trash/info/z.txt.trashinfo")), "[Trash Info]\n") == 0);
    // A file outside the guard is never trashed, even into a trash inside it.
    put(out_at("t.txt"), "t");
    CHECK(!trash_put(out_at("t.txt"), &t2, &err) && there(out_at("t.txt")));

    // Percent-encoded Path.
    put(at("tr/a b%%c\xC3\xA9.txt"), "x");
    CHECK(trash_put(at("tr/a b%%c\xC3\xA9.txt"), &t2, &err));
    CHECK(strstr(slurp(t2.info), "/tr/a%20b%25c%C3%A9.txt\n") != NULL);

    // A folder, and a link (not followed).
    mkdirs(at("tr/dir/in"));
    CHECK(trash_put(at("tr/dir"), &t2, &err) && is_dir_at(at("data/Trash/files/dir/in")));
    put(at("tr/target"), "T");
    symlink("target", at("tr/ln"));
    CHECK(trash_put(at("tr/ln"), &t2, &err));
    CHECK(there(at("tr/target")) && lstat(t2.file, &st) == 0 && S_ISLNK(st.st_mode));

    // Back again; and not over something there now.
    put(at("tr/back.txt"), "K");
    Trashed tb;
    CHECK(trash_put(at("tr/back.txt"), &tb, &err));
    CHECK(trash_restore(&tb, &err));
    CHECK(strcmp(slurp(at("tr/back.txt")), "K") == 0 && !there(tb.info) && !there(tb.file));
    CHECK(trash_put(at("tr/back.txt"), &tb, &err));
    put(at("tr/back.txt"), "new");
    CHECK(!trash_restore(&tb, &err) && strstr(err.msg, "is there again"));
    CHECK(strcmp(slurp(at("tr/back.txt")), "new") == 0 && there(tb.file) && there(tb.info));
    CHECK(!trash_put(at("tr/none"), &t2, &err));

    // Another drive: its own trash, Paths relative to its top.
    trash_test_topdir = at("drive");
    put(at("drive/sub/f"), "F");
    CHECK(trash_put(at("drive/sub/f"), &t2, &err));
    char own[1300];
    snprintf(own, sizeof own, "%s/.Trash-%u/files/f", at("drive"), (unsigned)getuid());
    CHECK(strcmp(t2.file, own) == 0);
    CHECK(strstr(slurp(t2.info), "\nPath=sub/f\n") != NULL);
    CHECK(stat(at("drive/.Trash-%u", (unsigned)getuid()), &st) == 0 && (st.st_mode & 0777) == 0700);
    // An admin .Trash with the sticky bit: $topdir/.Trash/$uid.
    mkdirs(at("drive2/.Trash"));
    chmod(at("drive2/.Trash"), 01777);
    trash_test_topdir = at("drive2");
    put(at("drive2/g"), "G");
    CHECK(trash_put(at("drive2/g"), &t2, &err));
    snprintf(own, sizeof own, "%s/.Trash/%u/files/g", at("drive2"), (unsigned)getuid());
    CHECK(strcmp(t2.file, own) == 0);
    CHECK(strstr(slurp(t2.info), "\nPath=g\n") != NULL);
    // Without the sticky bit it isn't trusted.
    mkdirs(at("drive3/.Trash"));
    chmod(at("drive3/.Trash"), 0777);
    trash_test_topdir = at("drive3");
    put(at("drive3/h"), "H");
    CHECK(trash_put(at("drive3/h"), &t2, &err));
    CHECK(strstr(t2.file, "/drive3/.Trash-") != NULL);
    // A symlinked .Trash neither.
    mkdirs(at("elsewhere"));
    chmod(at("elsewhere"), 01777);
    mkdirs(at("drive4"));
    symlink(at("elsewhere"), at("drive4/.Trash"));
    trash_test_topdir = at("drive4");
    put(at("drive4/i"), "I");
    CHECK(trash_put(at("drive4/i"), &t2, &err));
    CHECK(strstr(t2.file, "/drive4/.Trash-") != NULL);
    // A .Trash-$uid that is a link: refused.
    mkdirs(at("drive5"));
    char link5[1300];
    snprintf(link5, sizeof link5, "%s/.Trash-%u", at("drive5"), (unsigned)getuid());
    symlink(at("elsewhere"), link5);
    trash_test_topdir = at("drive5");
    put(at("drive5/j"), "J");
    CHECK(!trash_put(at("drive5/j"), &t2, &err) && there(at("drive5/j")));
    trash_test_topdir = NULL;

    // The top of a drive: the highest folder on the same device.
    char top[1200];
    CHECK(stat(root, &st) == 0);
    topdir_of(root, st.st_dev, top, sizeof top);
    struct stat ts, up;
    CHECK(stat(top, &ts) == 0 && ts.st_dev == st.st_dev);
    Str upd = path_dir(str_from_c(top));
    char ups[1200];
    snprintf(ups, sizeof ups, "%.*s", STR_ARG(upd.len ? upd : S("/")));
    CHECK(strcmp(top, "/") == 0 || (stat(ups, &up) == 0 && up.st_dev != st.st_dev));
    CHECK(strncmp(root, top, strlen(top)) == 0);
    dev_t dv;
    CHECK(dev_of(at("no/such/thing"), &dv) && dv == st.st_dev);

    // The home trash outside the guard: refused, the file stays.
    env_set("XDG_DATA_HOME", out_at("data"));
    put(at("tr/g.txt"), "g");
    CHECK(!trash_put(at("tr/g.txt"), &t2, &err) && there(at("tr/g.txt")) && !there(out_at("data/Trash")));
    env_restore();
}

static void test_clip(void)
{
    static char p1[] = "/home/m/a b.txt", p2[] = "/home/m/\xC3\xA9#?.txt";
    ClipFiles c = { .cut = false, .n = 2 };
    c.path[0] = p1, c.path[1] = p2;
    arena_reset(&ta);
    CHECK_STR(clip_payload(&ta, &c, CLIP_FMT_GNOME), "copy\nfile:///home/m/a%20b.txt\nfile:///home/m/%C3%A9%23%3F.txt");
    CHECK_STR(clip_payload(&ta, &c, CLIP_FMT_URIS), "file:///home/m/a%20b.txt\r\nfile:///home/m/%C3%A9%23%3F.txt\r\n");
    CHECK_STR(clip_payload(&ta, &c, CLIP_FMT_TEXT), "/home/m/a b.txt\n/home/m/\xC3\xA9#?.txt");
    c.cut = true;
    Str g = clip_payload(&ta, &c, CLIP_FMT_GNOME);
    CHECK(str_starts_with(g, S("cut\nfile://")));
    CHECK(g.p[g.len] == 0);

    ClipFiles back;
    CHECK(clip_parse(&ta, g, true, &back) && back.cut && back.n == 2);
    CHECK(back.n == 2 && strcmp(back.path[0], p1) == 0 && strcmp(back.path[1], p2) == 0);
    c.cut = false;
    CHECK(clip_parse(&ta, clip_payload(&ta, &c, CLIP_FMT_GNOME), true, &back) && !back.cut && back.n == 2);
    CHECK(clip_parse(&ta, clip_payload(&ta, &c, CLIP_FMT_URIS), false, &back) && !back.cut && back.n == 2);
    CHECK(back.n == 2 && strcmp(back.path[1], p2) == 0);
    // Nemo's with a trailing newline, localhost, comments, other schemes.
    CHECK(clip_parse(&ta, S("cut\nfile:///a\n"), true, &back) && back.cut && back.n == 1 && strcmp(back.path[0], "/a") == 0);
    CHECK(clip_parse(&ta, S("# c\r\nhttps://x/y\r\nfile://localhost/b%20c\r\nfile://host/d\r\n"), false, &back));
    CHECK(back.n == 1 && strcmp(back.path[0], "/b c") == 0);
    CHECK(!clip_parse(&ta, S("hello\nfile:///a"), true, &back));
    CHECK(!clip_parse(&ta, S("copy\n"), true, &back));
    CHECK(!clip_parse(&ta, S("https://example.com/\r\n"), false, &back));
    CHECK(!clip_parse(&ta, S("file:///bad%zz\r\n"), false, &back));
    // Too long for the arena's tail: empty, never cut.
    static u8 small[16];
    Arena sa;
    arena_init(&sa, "small", small, sizeof small);
    CHECK(clip_payload(&sa, &c, CLIP_FMT_TEXT).len == 0);
}

// ---- jobs ---------------------------------------------------------------------------------------

static u8 job_buf[JOB_BUF];

static struct {
    Answer answers[8];
    bool   rest[8];
    int    n, at;
    char   asked[8][1200];
    bool   both[8];
} asks;

static Answer test_ask(Job *j, const char *dst, bool both_dirs, bool *rest)
{
    (void)j;
    int k = asks.at++;
    if (k >= 8) return ANS_CANCEL;
    snprintf(asks.asked[k], sizeof asks.asked[k], "%s", dst);
    asks.both[k] = both_dirs;
    *rest = asks.rest[k];
    return k < asks.n ? asks.answers[k] : ANS_CANCEL;
}

static void asks_set(int n, ...)
{
    memset(&asks, 0, sizeof asks);
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) asks.answers[i] = (Answer)va_arg(ap, int);
    va_end(ap);
    asks.n = n;
}

static int cancel_after;   // cancel when bytes_done reaches this (0: never)

static void test_tick(Job *j)
{
    if (cancel_after && atomic_load(&j->bytes_done) >= cancel_after) atomic_store(&j->cancel, true);
}

static Job job;
static JobItem job_items[16];

// A job of kind over n (src, dst) pairs.
static Job *job_new(JobKind kind, int n, ...)
{
    memset(&job, 0, sizeof job);
    memset(job_items, 0, sizeof job_items);
    static char paths[32][1200];
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        const char *s = va_arg(ap, const char *), *d = va_arg(ap, const char *);
        snprintf(paths[2 * i], sizeof paths[0], "%s", s);
        job_items[i].src = paths[2 * i];
        if (d) snprintf(paths[2 * i + 1], sizeof paths[0], "%s", d), job_items[i].dst = paths[2 * i + 1];
    }
    va_end(ap);
    job.kind = kind, job.item = job_items, job.n = n, job.ask = test_ask, job.tick = test_tick, job.buf = job_buf;
    return &job;
}

static void test_job(void)
{
    ops_guard(root);
    // A plain copy: bytes, mode and times kept, no part file left.
    put(at("j/src/a.txt"), "hello");
    chmod(at("j/src/a.txt"), 0640);
    set_mtime(at("j/src/a.txt"), 1000000000);
    mkdirs(at("j/dst"));
    asks_set(0);
    Job *j = job_new(JOB_COPY, 1, at("j/src/a.txt"), at("j/dst/a.txt"));
    job_run(j);
    struct stat st;
    CHECK(strcmp(slurp(at("j/dst/a.txt")), "hello") == 0);
    CHECK(stat(at("j/dst/a.txt"), &st) == 0 && (st.st_mode & 07777) == 0640 && st.st_mtime == 1000000000);
    CHECK(there(at("j/src/a.txt")));
    CHECK(!has_part(at("j/dst")));
    CHECK(j->item[0].out == OUT_NEW && strcmp(j->item[0].final, "a.txt") == 0);
    CHECK(atomic_load(&j->files_total) == 1 && atomic_load(&j->files_done) == 1);
    CHECK(atomic_load(&j->bytes_total) == 5 && atomic_load(&j->bytes_done) == 5);
    CHECK(atomic_load(&j->phase) == JP_DONE && j->errors == 0 && asks.at == 0);

    // A tree: folders, a link copied as a link, an empty folder, modes.
    mkdirs(at("j/src/t/sub/empty"));
    put(at("j/src/t/sub/b"), "bb");
    put(at("j/src/t/c"), "ccc");
    symlink("../c", at("j/src/t/sub/ln"));
    symlink("/nonexistent", at("j/src/t/broken"));
    chmod(at("j/src/t/sub"), 0750);
    set_mtime(at("j/src/t/sub"), 1100000000);
    mkfifo(at("j/src/t/fifo"), 0600);
    j = job_new(JOB_COPY, 1, at("j/src/t"), at("j/dst/t"));
    job_run(j);
    CHECK(strcmp(slurp(at("j/dst/t/sub/b")), "bb") == 0 && strcmp(slurp(at("j/dst/t/c")), "ccc") == 0);
    char target[64] = "";
    CHECK(readlink(at("j/dst/t/sub/ln"), target, sizeof target - 1) == 4 && strncmp(target, "../c", 4) == 0);
    CHECK(lstat(at("j/dst/t/broken"), &st) == 0 && S_ISLNK(st.st_mode));
    CHECK(is_dir_at(at("j/dst/t/sub/empty")));
    CHECK(stat(at("j/dst/t/sub"), &st) == 0 && (st.st_mode & 07777) == 0750 && st.st_mtime == 1100000000);
    CHECK(!there(at("j/dst/t/fifo")) && j->skipped == 1 && strstr(j->note, "not a file, folder or link"));
    CHECK(!has_part(at("j/dst")) && j->errors == 0);
    CHECK(j->item[0].out == OUT_NEW);
    CHECK(atomic_load(&j->files_total) == 5 && atomic_load(&j->bytes_total) == 5);
    CHECK(atomic_load(&j->files_done) == 5);

    // Clashes: skip, replace, keep both; "all the rest".
    put(at("j/c/src/x"), "new");
    put(at("j/c/src/y"), "newy");
    put(at("j/c/dst/x"), "old");
    put(at("j/c/dst/y"), "oldy");
    asks_set(1, ANS_SKIP);
    j = job_new(JOB_COPY, 1, at("j/c/src/x"), at("j/c/dst/x"));
    job_run(j);
    CHECK(strcmp(slurp(at("j/c/dst/x")), "old") == 0 && j->item[0].out == OUT_SKIPPED && j->skipped == 1);
    CHECK(asks.at == 1 && strcmp(asks.asked[0], at("j/c/dst/x")) == 0 && !asks.both[0]);
    asks_set(1, ANS_REPLACE);
    j = job_new(JOB_COPY, 1, at("j/c/src/x"), at("j/c/dst/x"));
    job_run(j);
    CHECK(strcmp(slurp(at("j/c/dst/x")), "new") == 0 && j->item[0].out == OUT_REPLACED && !has_part(at("j/c/dst")));
    asks_set(1, ANS_KEEP);
    j = job_new(JOB_COPY, 1, at("j/c/src/x"), at("j/c/dst/x"));
    job_run(j);
    CHECK(strcmp(slurp(at("j/c/dst/x (2)")), "new") == 0 && j->item[0].out == OUT_NEW && strcmp(j->item[0].final, "x (2)") == 0);
    asks_set(1, ANS_SKIP);
    asks.rest[0] = true;
    put(at("j/c/dst/x"), "old");
    j = job_new(JOB_COPY, 2, at("j/c/src/x"), at("j/c/dst/x"), at("j/c/src/y"), at("j/c/dst/y"));
    job_run(j);
    CHECK(asks.at == 1 && strcmp(slurp(at("j/c/dst/x")), "old") == 0 && strcmp(slurp(at("j/c/dst/y")), "oldy") == 0);
    CHECK(j->item[1].out == OUT_SKIPPED && j->skipped == 2);
    // Cancel at the prompt: nothing after it happens.
    asks_set(1, ANS_CANCEL);
    put(at("j/c/src/z"), "z");
    j = job_new(JOB_COPY, 2, at("j/c/src/x"), at("j/c/dst/x"), at("j/c/src/z"), at("j/c/dst/z"));
    job_run(j);
    CHECK(atomic_load(&j->cancel) && !there(at("j/c/dst/z")) && j->item[1].out == OUT_NONE);
    // No ask at all: skip.
    j = job_new(JOB_COPY, 1, at("j/c/src/x"), at("j/c/dst/x"));
    j->ask = NULL;
    job_run(j);
    CHECK(strcmp(slurp(at("j/c/dst/x")), "old") == 0 && j->item[0].out == OUT_SKIPPED);

    // A folder onto a folder: replace merges, asking again inside.
    put(at("j/m/src/f/same"), "src");
    put(at("j/m/src/f/only"), "only");
    put(at("j/m/dst/f/same"), "dst");
    put(at("j/m/dst/f/mine"), "mine");
    asks_set(2, ANS_REPLACE, ANS_SKIP);
    j = job_new(JOB_COPY, 1, at("j/m/src/f"), at("j/m/dst/f"));
    job_run(j);
    CHECK(asks.at == 2 && asks.both[0] && !asks.both[1] && strcmp(asks.asked[1], at("j/m/dst/f/same")) == 0);
    CHECK(strcmp(slurp(at("j/m/dst/f/same")), "dst") == 0 && strcmp(slurp(at("j/m/dst/f/only")), "only") == 0);
    CHECK(strcmp(slurp(at("j/m/dst/f/mine")), "mine") == 0 && j->item[0].out == OUT_MERGED);
    // A file onto a folder: no replace.
    put(at("j/m/src/g"), "file");
    mkdirs(at("j/m/dst/g"));
    asks_set(1, ANS_REPLACE);
    j = job_new(JOB_COPY, 1, at("j/m/src/g"), at("j/m/dst/g"));
    job_run(j);
    CHECK(is_dir_at(at("j/m/dst/g")) && strstr(j->note, "can't replace a folder with a file") && j->item[0].out == OUT_SKIPPED);
    // "Keep both" for a folder: a new one beside it.
    asks_set(1, ANS_KEEP);
    j = job_new(JOB_COPY, 1, at("j/m/src/f"), at("j/m/dst/f"));
    job_run(j);
    CHECK(strcmp(slurp(at("j/m/dst/f (2)/only")), "only") == 0 && strcmp(j->item[0].final, "f (2)") == 0);

    // Cancel in the middle of a file: no part file, nothing under the name.
    static char big[3 * 1024 * 1024];
    memset(big, 'b', sizeof big - 1);
    big[sizeof big - 1] = 0;
    put(at("j/big/src/big"), big);
    mkdirs(at("j/big/dst"));
    cancel_after = 1;
    j = job_new(JOB_COPY, 1, at("j/big/src/big"), at("j/big/dst/big"));
    job_run(j);
    cancel_after = 0;
    CHECK(!there(at("j/big/dst/big")) && !has_part(at("j/big/dst")) && j->item[0].out == OUT_NONE);
    CHECK(atomic_load(&j->bytes_done) > 0 && atomic_load(&j->bytes_done) < (long long)sizeof big);
    // The same for a folder: the part folder goes.
    put(at("j/big/src/tree/big"), big);
    put(at("j/big/src/tree/z"), "z");
    cancel_after = 1;
    j = job_new(JOB_COPY, 1, at("j/big/src/tree"), at("j/big/dst/tree"));
    job_run(j);
    cancel_after = 0;
    CHECK(!there(at("j/big/dst/tree")) && !has_part(at("j/big/dst")));
    // Whole, by copy_file_range and by read/write (when the kernel can't, forced here).
    for (int plain = 0; plain < 2; plain++) {
        job_test_plain = plain;
        unlink(at("j/big/dst/big"));
        j = job_new(JOB_COPY, 1, at("j/big/src/big"), at("j/big/dst/big"));
        job_run(j);
        CHECK(j->errors == 0 && stat(at("j/big/dst/big"), &st) == 0 && st.st_size == (off_t)sizeof big - 1);
        FILE *f = fopen(at("j/big/dst/big"), "r");
        static char back[sizeof big];
        size_t n = f ? fread(back, 1, sizeof back, f) : 0;
        if (f) fclose(f);
        CHECK(n == sizeof big - 1 && memcmp(back, big, n) == 0);
        CHECK(atomic_load(&j->bytes_done) == (long long)sizeof big - 1);
    }
    job_test_plain = false;

    // A move on the same drive: a rename.
    put(at("j/mv/src/a"), "a");
    mkdirs(at("j/mv/src/d/in"));
    mkdirs(at("j/mv/dst"));
    j = job_new(JOB_MOVE, 2, at("j/mv/src/a"), at("j/mv/dst/a"), at("j/mv/src/d"), at("j/mv/dst/d"));
    job_run(j);
    CHECK(!there(at("j/mv/src/a")) && strcmp(slurp(at("j/mv/dst/a")), "a") == 0 && is_dir_at(at("j/mv/dst/d/in")));
    CHECK(j->item[0].out == OUT_NEW && j->item[1].out == OUT_NEW && atomic_load(&j->files_total) == 2);
    // A move onto a folder there: merged, and the source goes once empty.
    put(at("j/mv/src/d/x"), "x");
    put(at("j/mv/src/d/in/y"), "y");
    asks_set(2, ANS_REPLACE, ANS_REPLACE);
    j = job_new(JOB_MOVE, 1, at("j/mv/src/d"), at("j/mv/dst/d"));
    job_run(j);
    CHECK(strcmp(slurp(at("j/mv/dst/d/x")), "x") == 0 && strcmp(slurp(at("j/mv/dst/d/in/y")), "y") == 0);
    CHECK(!there(at("j/mv/src/d")) && j->item[0].out == OUT_MERGED);
    // A skip inside a merge keeps that part of the source.
    put(at("j/mv/src/e/k"), "k1");
    put(at("j/mv/src/e/l"), "l");
    put(at("j/mv/dst/e/k"), "k0");
    asks_set(2, ANS_REPLACE, ANS_SKIP);
    j = job_new(JOB_MOVE, 1, at("j/mv/src/e"), at("j/mv/dst/e"));
    job_run(j);
    CHECK(strcmp(slurp(at("j/mv/src/e/k")), "k1") == 0 && strcmp(slurp(at("j/mv/dst/e/k")), "k0") == 0);
    CHECK(strcmp(slurp(at("j/mv/dst/e/l")), "l") == 0 && !there(at("j/mv/src/e/l")));
    // Move clashes: replace a file, keep both.
    put(at("j/mv/src/r"), "r1");
    put(at("j/mv/dst/r"), "r0");
    asks_set(1, ANS_REPLACE);
    j = job_new(JOB_MOVE, 1, at("j/mv/src/r"), at("j/mv/dst/r"));
    job_run(j);
    CHECK(!there(at("j/mv/src/r")) && strcmp(slurp(at("j/mv/dst/r")), "r1") == 0 && j->item[0].out == OUT_REPLACED);
    put(at("j/mv/src/r"), "r2");
    asks_set(1, ANS_KEEP);
    j = job_new(JOB_MOVE, 1, at("j/mv/src/r"), at("j/mv/dst/r"));
    job_run(j);
    CHECK(strcmp(slurp(at("j/mv/dst/r (2)")), "r2") == 0 && j->item[0].out == OUT_NEW && strcmp(j->item[0].final, "r (2)") == 0);

    // Across drives (forced): a copy, then the source goes; not when part of it was skipped.
    job_test_cross = true;
    put(at("j/x/src/t/a"), "a");
    put(at("j/x/src/t/b"), "b");
    mkdirs(at("j/x/dst"));
    j = job_new(JOB_MOVE, 1, at("j/x/src/t"), at("j/x/dst/t"));
    job_run(j);
    CHECK(!there(at("j/x/src/t")) && strcmp(slurp(at("j/x/dst/t/b")), "b") == 0 && j->item[0].out == OUT_NEW);
    CHECK(atomic_load(&j->files_total) == 2 && atomic_load(&j->bytes_total) == 2);
    put(at("j/x/src/u/a"), "a1");
    put(at("j/x/src/u/b"), "b");
    put(at("j/x/dst/u/a"), "a0");
    asks_set(2, ANS_REPLACE, ANS_SKIP);
    j = job_new(JOB_MOVE, 1, at("j/x/src/u"), at("j/x/dst/u"));
    job_run(j);
    CHECK(there(at("j/x/src/u/a")) && there(at("j/x/src/u/b")) && strcmp(slurp(at("j/x/dst/u/b")), "b") == 0);
    CHECK(j->item[0].out == OUT_MERGED);
    // A new folder moved across with a part skipped (a fifo): the source stays, and it isn't "new"
    // (an undo couldn't move it back over the part left behind).
    put(at("j/x/src/v/a"), "a");
    mkfifo(at("j/x/src/v/pipe"), 0600);
    j = job_new(JOB_MOVE, 1, at("j/x/src/v"), at("j/x/dst/v"));
    job_run(j);
    CHECK(there(at("j/x/src/v/pipe")) && there(at("j/x/src/v/a")) && strcmp(slurp(at("j/x/dst/v/a")), "a") == 0);
    CHECK(j->item[0].out == OUT_MERGED);
    job_test_cross = false;

    // Delete for good, as a job.
    put(at("j/del/a/b"), "b");
    put(at("j/del/c"), "c");
    j = job_new(JOB_DELETE, 2, at("j/del/a"), NULL, at("j/del/c"), NULL);
    job_run(j);
    CHECK(!there(at("j/del/a")) && !there(at("j/del/c")) && is_dir_at(at("j/del")));
    CHECK(atomic_load(&j->files_done) == 3 && j->item[0].out == OUT_NEW && j->errors == 0);
    j = job_new(JOB_DELETE, 1, at("j/del/none"), NULL);
    job_run(j);
    CHECK(j->errors == 1 && j->item[0].out == OUT_FAILED);

    // The guard: a target outside the root fails, nothing is written.
    j = job_new(JOB_COPY, 1, at("j/src/a.txt"), out_at("escape.txt"));
    job_run(j);
    CHECK(j->item[0].out == OUT_FAILED && j->errors == 1 && !there(out_at("escape.txt")));
    put(out_at("victim.txt"), "v");
    j = job_new(JOB_DELETE, 1, out_at("victim.txt"), NULL);
    job_run(j);
    CHECK(j->item[0].out == OUT_FAILED && there(out_at("victim.txt")));
    j = job_new(JOB_MOVE, 1, out_at("victim.txt"), at("j/stolen.txt"));
    job_run(j);
    CHECK(j->item[0].out == OUT_FAILED && there(out_at("victim.txt")) && !there(at("j/stolen.txt")));

    // Into itself.
    mkdirs(at("j/self/a/b"));
    mkdirs(at("j/self/ab"));
    CHECK(into_itself(at("j/self/a"), at("j/self/a")));
    CHECK(into_itself(at("j/self/a"), at("j/self/a/b")));
    CHECK(!into_itself(at("j/self/a"), at("j/self/ab")));
    CHECK(!into_itself(at("j/self/a"), at("j/self")));
    CHECK(!into_itself(at("j/self/none"), at("j/self")));

    // Part names: by name, or numbered when too long.
    char part[NAME_MAX + 1], longname[NAME_MAX + 1];
    part_name(part, sizeof part, "a.txt");
    CHECK(strcmp(part, ".fx-part-a.txt") == 0);
    memset(longname, 'n', NAME_MAX), longname[NAME_MAX] = 0;
    part_name(part, sizeof part, longname);
    CHECK(strncmp(part, ".fx-part-", 9) == 0 && strlen(part) < 40);
}

static void test_undo(void)
{
    ops_guard(root);
    env_set("XDG_DATA_HOME", at("udata"));
    mkdirs(at("udata"));
    static u8 undo_mem[KiB(256)];
    static Arena ua;
    arena_init(&ua, "undo", undo_mem, sizeof undo_mem);
    static Undo u;
    u.arena = &ua;
    Err err;
    isize done;
    char what[64];

    CHECK(!undo_do(&u, &done, &err) && strcmp(err.msg, "nothing to undo") == 0);

    // rename
    put(at("u/a"), "a");
    CHECK(op_rename(at("u"), S("a"), S("b"), &err));
    undo_begin(&u, UNDO_RENAME);
    undo_add(&u, at("u/a"), at("u/b"), NULL);
    undo_what(&u, what, sizeof what);
    CHECK(strcmp(what, "rename") == 0);
    CHECK(undo_do(&u, &done, &err) && done == 1 && there(at("u/a")) && !there(at("u/b")));
    CHECK(u.kind == UNDO_NONE);
    // refused when the old name is taken again, or the new one is gone
    CHECK(op_rename(at("u"), S("a"), S("b"), &err));
    undo_begin(&u, UNDO_RENAME);
    undo_add(&u, at("u/a"), at("u/b"), NULL);
    put(at("u/a"), "other");
    CHECK(!undo_do(&u, &done, &err) && strstr(err.msg, "is there again") && there(at("u/b")) && done == 0);
    unlink(at("u/b"));
    undo_begin(&u, UNDO_RENAME);
    undo_add(&u, at("u/x"), at("u/b"), NULL);
    CHECK(!undo_check(&u, &err) && strstr(err.msg, "is gone"));

    // new folder: removed while empty
    char made[NAME_MAX + 1];
    CHECK(op_mkdir(at("u"), S("New folder"), made, sizeof made, &err));
    undo_begin(&u, UNDO_MKDIR);
    undo_add(&u, NULL, at("u/New folder"), NULL);
    CHECK(undo_do(&u, &done, &err) && !there(at("u/New folder")));
    CHECK(op_mkdir(at("u"), S("New folder"), made, sizeof made, &err));
    put(at("u/New folder/f"), "f");
    undo_begin(&u, UNDO_MKDIR);
    undo_add(&u, NULL, at("u/New folder"), NULL);
    CHECK(!undo_do(&u, &done, &err) && strstr(err.msg, "isn't empty") && there(at("u/New folder/f")));
    put(at("u/nf"), "file");
    undo_begin(&u, UNDO_MKDIR);
    undo_add(&u, NULL, at("u/nf"), NULL);
    CHECK(!undo_check(&u, &err) && strstr(err.msg, "isn't a folder"));

    // trash
    put(at("u/t1"), "1");
    put(at("u/t2"), "2");
    Trashed t1, t2;
    CHECK(trash_put(at("u/t1"), &t1, &err) && trash_put(at("u/t2"), &t2, &err));
    undo_begin(&u, UNDO_TRASH);
    undo_add(&u, t1.from, t1.file, t1.info);
    undo_add(&u, t2.from, t2.file, t2.info);
    undo_what(&u, what, sizeof what);
    CHECK(strcmp(what, "trash of 2 items") == 0);
    CHECK(undo_do(&u, &done, &err) && done == 2 && strcmp(slurp(at("u/t1")), "1") == 0 && strcmp(slurp(at("u/t2")), "2") == 0);
    CHECK(!there(t1.info) && !there(t2.info));

    // copy: the copies go to the trash
    put(at("u/c (copy)"), "c");
    undo_begin(&u, UNDO_COPY);
    undo_add(&u, NULL, at("u/c (copy)"), NULL);
    CHECK(undo_do(&u, &done, &err) && !there(at("u/c (copy)")) && there(at("udata/Trash/files/c (copy)")));

    // move
    mkdirs(at("u/m2"));
    put(at("u/m1"), "m");
    CHECK(move_noreplace(at("u/m1"), at("u/m2/m1"), &err));
    undo_begin(&u, UNDO_MOVE);
    undo_add(&u, at("u/m1"), at("u/m2/m1"), NULL);
    CHECK(!undo_is_job(&u));
    CHECK(undo_do(&u, &done, &err) && there(at("u/m1")) && !there(at("u/m2/m1")));

    // A new record replaces the old one.
    undo_begin(&u, UNDO_RENAME);
    undo_add(&u, at("u/p"), at("u/q"), NULL);
    undo_begin(&u, UNDO_COPY);
    CHECK(u.n == 0 && u.kind == UNDO_COPY);
    env_restore();
}

// ---- ui: the selection, the edit line, the prompts --------------------------------------------

static UiInput ev1(UiKey k, u32 ch)
{
    UiInput in = ui_input_none();
    in.ev[in.nev++] = (UiEvent){ k, ch };
    return in;
}

static void test_ui_f3(void)
{
    UiNames names = { name_at, NULL };
    static Ui u;
    static u8 marks[20];
    memset(&u, 0, sizeof u);
    memset(marks, 0, sizeof marks);
    ui_resize(&u.list, 5);
    ui_set(&u.list, 20, 0);
    ui_mark_set(&u, marks);
    UiInput in;

    // Space marks and moves on; again unmarks.
    in = chars("  ");
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    CHECK(u.marked == 2 && marks[0] && marks[1] && u.list.sel == 2);
    in = keys(1, UK_UP);
    ui_handle(&u, names, &in);
    in = chars(" ");
    ui_handle(&u, names, &in);
    CHECK(u.marked == 1 && marks[0] && !marks[1] && u.list.sel == 2);
    // The last row: marked, the cursor stays.
    in = keys(1, UK_END);
    ui_handle(&u, names, &in);
    in = chars(" ");
    ui_handle(&u, names, &in);
    CHECK(marks[19] && u.list.sel == 19 && u.marked == 2);
    // Esc clears; a second Esc doesn't change it.
    in = keys(1, UK_ESC);
    ui_handle(&u, names, &in);
    CHECK(u.marked == 0 && !marks[0] && !marks[19]);
    // Ctrl+A.
    in = keys(1, UK_ALL);
    ui_handle(&u, names, &in);
    CHECK(u.marked == 20 && marks[7]);
    ui_mark_clear(&u);
    CHECK(u.marked == 0 && !marks[7]);
    // Marks past the list, or without an array, do nothing.
    ui_mark(&u, 20, true);
    ui_mark(&u, -1, true);
    CHECK(u.marked == 0);
    ui_mark(&u, 3, true), ui_mark(&u, 3, true);
    CHECK(u.marked == 1);
    ui_mark(&u, 3, false), ui_mark(&u, 3, false);
    CHECK(u.marked == 0);

    // Clicks: Ctrl toggles, Shift marks the range from the cursor.
    in = keys(1, UK_HOME);
    ui_handle(&u, names, &in);
    in = ui_input_none(), in.click = 2, in.click_mods = UI_MOD_CTRL;
    ui_handle(&u, names, &in);
    CHECK(marks[2] && u.marked == 1 && u.list.sel == 2);
    in = ui_input_none(), in.click = 2, in.click_mods = UI_MOD_CTRL;
    ui_handle(&u, names, &in);
    CHECK(!marks[2] && u.marked == 0);
    in = ui_input_none(), in.click = 4, in.click_mods = UI_MOD_SHIFT;
    ui_handle(&u, names, &in);
    CHECK(marks[2] && marks[3] && marks[4] && !marks[1] && !marks[5] && u.marked == 3 && u.list.sel == 4);
    in = ui_input_none(), in.click = 1, in.click_mods = UI_MOD_SHIFT;   // backwards, overlapping
    ui_handle(&u, names, &in);
    CHECK(marks[1] && u.marked == 4 && u.list.sel == 1);
    in = ui_input_none(), in.click = 0;   // a plain click keeps them
    ui_handle(&u, names, &in);
    CHECK(u.marked == 4 && u.list.sel == 0);
    ui_mark_clear(&u);

    // The actions.
    in = keys(1, UK_DELETE);
    CHECK(ui_handle(&u, names, &in).kind == UA_TRASH);
    in = keys(1, UK_COPY);
    CHECK(ui_handle(&u, names, &in).kind == UA_COPY);
    in = keys(1, UK_CUT);
    CHECK(ui_handle(&u, names, &in).kind == UA_CUT);
    in = keys(1, UK_PASTE);
    CHECK(ui_handle(&u, names, &in).kind == UA_PASTE);
    in = keys(1, UK_UNDO);
    CHECK(ui_handle(&u, names, &in).kind == UA_UNDO);
    in = keys(1, UK_NEWFOLDER);
    CHECK(ui_handle(&u, names, &in).kind == UA_NEWFOLDER);
    // Not with the sidebar's keyboard.
    ui_set(&u.side, 3, 0);
    u.side_focus = true;
    in = keys(4, UK_DELETE, UK_COPY, UK_CUT, UK_F2);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.editing);
    in = keys(1, UK_SHIFT_DELETE);
    ui_handle(&u, names, &in);
    CHECK(!u.confirming);
    u.side_focus = false;
    // Nothing to act on in an empty folder (paste still can).
    ui_set(&u.list, 0, 0);
    in = keys(5, UK_DELETE, UK_COPY, UK_CUT, UK_SHIFT_DELETE, UK_F2);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.confirming && !u.editing);
    in = keys(1, UK_PASTE);
    CHECK(ui_handle(&u, names, &in).kind == UA_PASTE);
    ui_set(&u.list, 20, 0);

    // Shift+Delete: the confirm, Y by default; n and Esc say no; other keys wait.
    ui_mark(&u, 1, true), ui_mark(&u, 2, true);
    in = keys(1, UK_SHIFT_DELETE);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.confirming && u.confirm_n == 2);
    in = keys(3, UK_DOWN, UK_DELETE, UK_TAB);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.confirming && u.list.sel == 0 && !u.side_focus);
    in = chars("jx");
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.confirming && u.list.sel == 0);
    in = keys(1, UK_ENTER);
    CHECK(ui_handle(&u, names, &in).kind == UA_DELETE && !u.confirming);
    in = keys(1, UK_SHIFT_DELETE);
    ui_handle(&u, names, &in);
    in = chars("y");
    CHECK(ui_handle(&u, names, &in).kind == UA_DELETE && !u.confirming);
    in = keys(1, UK_SHIFT_DELETE);
    ui_handle(&u, names, &in);
    in = chars("Y");
    CHECK(ui_handle(&u, names, &in).kind == UA_DELETE);
    in = keys(1, UK_SHIFT_DELETE);
    ui_handle(&u, names, &in);
    in = chars("n");
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.confirming);
    in = keys(1, UK_SHIFT_DELETE);
    ui_handle(&u, names, &in);
    in = chars("N");
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.confirming);
    in = keys(1, UK_SHIFT_DELETE);
    ui_handle(&u, names, &in);
    in = keys(1, UK_ESC);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.confirming && u.marked == 2);   // Esc ends the prompt only
    // Clicks wait too.
    in = keys(1, UK_SHIFT_DELETE);
    ui_handle(&u, names, &in);
    in = ui_input_none(), in.click = 3, in.header = SORT_SIZE, in.side_click = 0, in.backs = 1;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.list.sel == 0 && u.sort.key == SORT_NAME);
    u.confirming = false;
    ui_mark_clear(&u);
    in = keys(1, UK_SHIFT_DELETE);
    ui_handle(&u, names, &in);
    CHECK(u.confirm_n == 1);
    u.confirming = false;

    // A clash: s r k, the capitals for the rest, Esc cancels; others wait.
    u.asking = true;
    in = chars("xj");
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.asking && u.list.sel == 0);
    in = keys(1, UK_ENTER);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.asking);
    static const struct { char c; int a; bool all; } ANS[] = {
        { 's', UI_ANS_SKIP, false }, { 'r', UI_ANS_REPLACE, false }, { 'k', UI_ANS_KEEP, false },
        { 'S', UI_ANS_SKIP, true }, { 'R', UI_ANS_REPLACE, true }, { 'K', UI_ANS_KEEP, true },
    };
    for (int i = 0; i < COUNT(ANS); i++) {
        u.asking = true;
        char c[2] = { ANS[i].c, 0 };
        in = chars(c);
        UiAction a = ui_handle(&u, names, &in);
        CHECK(a.kind == UA_ANSWER && a.index == ANS[i].a && a.all == ANS[i].all && !u.asking);
    }
    u.asking = true;
    in = keys(1, UK_ESC);
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_ANSWER && a.index == UI_ANS_CANCEL && !u.asking);

    // Esc with a job running cancels it, before it clears a selection.
    u.busy = true;
    ui_mark(&u, 4, true);
    in = keys(1, UK_ESC);
    CHECK(ui_handle(&u, names, &in).kind == UA_CANCEL_JOB && u.marked == 1);
    u.busy = false;
    ui_mark_clear(&u);

    // The edit line: F2 opens it on the name, the caret at the end.
    in = keys(1, UK_HOME);
    ui_handle(&u, names, &in);
    in = keys(1, UK_F2);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.editing);
    CHECK(u.edit_len == 5 && memcmp(u.edit, "alpha", 5) == 0 && u.caret == 5);
    // Letters edit, they don't move or act (j, t, Space, '/').
    in = chars("jt /");
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.list.sel == 0);
    CHECK(u.edit_len == 8 && memcmp(u.edit, "alphajt ", 8) == 0);
    in = keys(3, UK_BACKSPACE, UK_LEFT, UK_LEFT);
    ui_handle(&u, names, &in);
    CHECK(u.edit_len == 7 && u.caret == 5);
    in = keys(1, UK_DELETE);
    ui_handle(&u, names, &in);
    CHECK(u.edit_len == 6 && memcmp(u.edit, "alphat", 6) == 0 && u.caret == 5);
    in = keys(1, UK_HOME);
    ui_handle(&u, names, &in);
    in = chars("X");
    ui_handle(&u, names, &in);
    CHECK(memcmp(u.edit, "Xalphat", 7) == 0 && u.caret == 1);
    in = keys(2, UK_END, UK_RIGHT);
    ui_handle(&u, names, &in);
    CHECK(u.caret == 7);
    in = keys(1, UK_DELETE);   // nothing after the caret
    ui_handle(&u, names, &in);
    CHECK(u.edit_len == 7);
    in = keys(8, UK_HOME, UK_LEFT, UK_BACKSPACE, UK_UP, UK_DOWN, UK_TAB, UK_PAGEDOWN, UK_COPY);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.caret == 0 && u.edit_len == 7 && u.list.sel == 0 && !u.side_focus);
    // UTF-8: one code point at a time.
    in = keys(1, UK_END);
    ui_handle(&u, names, &in);
    in = ui_input_none();
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 0xE9 };
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 0x1F600 };
    ui_handle(&u, names, &in);
    CHECK(u.edit_len == 7 + 2 + 4 && u.caret == 13);
    in = keys(1, UK_LEFT);
    ui_handle(&u, names, &in);
    CHECK(u.caret == 9);
    in = keys(1, UK_BACKSPACE);
    ui_handle(&u, names, &in);
    CHECK(u.edit_len == 11 && u.caret == 7 && (unsigned char)u.edit[7] == 0xF0);
    in = keys(1, UK_DELETE);
    ui_handle(&u, names, &in);
    CHECK(u.edit_len == 7);
    // Controls are dropped; the line holds NAME_MAX bytes at most.
    in = ev1(UK_NONE, 0x7F);
    ui_handle(&u, names, &in);
    in = ev1(UK_NONE, 0x09);
    ui_handle(&u, names, &in);
    CHECK(u.edit_len == 7);
    // Enter asks gui/ to rename; the line stays open until gui/ closes it.
    in = keys(1, UK_ENTER);
    CHECK(ui_handle(&u, names, &in).kind == UA_RENAME && u.editing);
    ui_edit_close(&u);
    // Esc closes it, with nothing asked.
    in = keys(1, UK_F2);
    ui_handle(&u, names, &in);
    in = keys(1, UK_ESC);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.editing);
    // A click closes it.
    in = keys(1, UK_F2);
    ui_handle(&u, names, &in);
    in = ui_input_none(), in.click = 1;
    ui_handle(&u, names, &in);
    CHECK(!u.editing && u.list.sel == 1);
    // Full.
    char big[NAME_MAX + 1];
    memset(big, 'a', NAME_MAX - 1);
    ui_edit_open(&u, (Str){ big, NAME_MAX - 1 });
    in = ui_input_none();
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 0xE9 };   // 2 bytes: no room
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 'b' };
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 'c' };
    ui_handle(&u, names, &in);
    CHECK(u.edit_len == NAME_MAX && u.edit[NAME_MAX - 1] == 'b');
    ui_edit_close(&u);
    // F2 closes find; the find line doesn't take F2's edit keys after.
    in = chars("/al");
    ui_handle(&u, names, &in);
    CHECK(u.finding);
    in = keys(1, UK_F2);
    ui_handle(&u, names, &in);
    CHECK(!u.finding && u.editing);
    ui_edit_close(&u);
}
