// Unit tests for fs/ and ui/. `make test` runs them from the repo root. Every fixture is made under
// a mkdtemp directory, and every variable a test sets (HOME, XDG_*, PATH) is put back after it, so
// nothing reads or writes Michael's own files.
#include "../base/base.c"
#include "../fs/fs.c"
#include "../ui/ui.c"

// The edit line opened on a name, the caret at its end (the tests' way in; ofx opens it with
// ui_edit_rename and ui_edit_new).
static void ui_edit_open(Ui *u, Str text)
{
    ui_edit_start(u, text, false);
}

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int checks, failures;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        checks++;                                                                      \
        if (!(cond)) {                                                                 \
            failures++;                                                                \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);   \
        }                                                                              \
    } while (0)

#define CHECK_STR(got, want)                                                                         \
    do {                                                                                             \
        checks++;                                                                                    \
        Str g_ = (got);                                                                              \
        const char *w_ = (want);                                                                     \
        if (!str_eq(g_, str_from_c(w_))) {                                                           \
            failures++;                                                                              \
            fprintf(stderr, "%s:%d: got \"%.*s\", want \"%s\"\n", __FILE__, __LINE__, STR_ARG(g_), w_); \
        }                                                                                            \
    } while (0)

static u8 test_mem[MiB(16)];
static Arena ta;

// ---- fixtures -------------------------------------------------------------------------------

static char root[256];   // this run's mkdtemp directory

static const char *at(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static const char *at(const char *fmt, ...)
{
    static char buf[8][1200];
    static int k;
    char rel[900];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(rel, sizeof rel, fmt, ap);
    va_end(ap);
    k = (k + 1) % 8;
    snprintf(buf[k], sizeof buf[k], "%s/%s", root, rel);
    return buf[k];
}

// Writes text to path, making its directories.
static void put(const char *path, const char *text)
{
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    for (char *p = dir + 1; *p; p++)
        if (*p == '/') *p = 0, mkdir(dir, 0755), *p = '/';
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

static void mkdirs(const char *path)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/", path);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') *s = 0, mkdir(p, 0755), *s = '/';
}

// Environment variables a test changes, put back by env_restore.
static struct { const char *name; char *old; } saved[16];
static int nsaved;

static void env_set(const char *name, const char *value)
{
    const char *old = getenv(name);
    saved[nsaved].name = name;
    saved[nsaved++].old = old ? strdup(old) : NULL;   // test-only: the tests may malloc
    if (value) setenv(name, value, 1);
    else unsetenv(name);
}

static void env_restore(void)
{
    while (nsaved > 0) {
        nsaved--;
        if (saved[nsaved].old) setenv(saved[nsaved].name, saved[nsaved].old, 1), free(saved[nsaved].old);
        else unsetenv(saved[nsaved].name);
    }
}

// ---- natural order --------------------------------------------------------------------------

static void test_natural(void)
{
    struct { const char *a, *b; int want; } cases[] = {
        { "file2", "file10", -1 },
        { "file10", "file2", 1 },
        { "a", "B", -1 },          // case ignored
        { "B", "a", 1 },
        { "abc", "abd", -1 },
        { "ab", "abc", -1 },       // a prefix first
        { "abc", "ab", 1 },
        { "x007", "x7", 1 },       // equal numbers: the longer run (more zeros) after
        { "x7", "x007", -1 },
        { "x07", "x7", 1 },
        { "x9", "x10", -1 },
        { "2", "10", -1 },
        { "a1b2", "a1b10", -1 },
        { "Readme", "readme", -1 },   // equal when folded: the plain bytes decide ('R' < 'r')
        { "readme", "Readme", 1 },
        { "same", "same", 0 },
        { "", "a", -1 },
        { "a", "", 1 },
        { "1.10", "1.9", 1 },
        { "a_b", "a-b", 1 },          // '_' (0x5f) after '-' (0x2d)
        { "Z", "a", 1 },              // 'z' after 'a', whatever the case
        { "x0", "x00", -1 },          // zero: the runs left after the zeros tie, the shorter first
    };
    for (int i = 0; i < COUNT(cases); i++) {
        int got = natural_cmp(str_from_c(cases[i].a), str_from_c(cases[i].b));
        if (got != cases[i].want) fprintf(stderr, "natural_cmp(%s, %s) = %d\n", cases[i].a, cases[i].b, got);
        CHECK(got == cases[i].want);
    }
}

// ---- listing --------------------------------------------------------------------------------

static void test_list(void)
{
    arena_reset(&ta);
    put(at("list/b10.txt"), "0123456789");
    put(at("list/b2.txt"), "x");
    put(at("list/A.txt"), "");
    put(at("list/.hidden"), "");
    put(at("list/.hidden2"), "");
    mkdirs(at("list/zdir"));
    mkdirs(at("list/Adir"));
    mkdirs(at("list/.git"));
    CHECK(symlink("zdir", at("list/link-dir")) == 0);
    CHECK(symlink("b2.txt", at("list/link-file")) == 0);
    CHECK(symlink("nowhere", at("list/link-broken")) == 0);
    CHECK(mkfifo(at("list/pipe"), 0644) == 0);
    struct timespec ts[2] = { { 1700000000, 0 }, { 1700000000, 0 } };
    CHECK(utimensat(AT_FDCWD, at("list/b2.txt"), ts, 0) == 0);

    Listing l;
    Err err;
    CHECK(fs_list(&ta, at("list"), false, (Sort){ 0 }, &l, &err));
    const char *want[] = { "Adir", "link-dir", "zdir", "A.txt", "b2.txt", "b10.txt", "link-broken", "link-file", "pipe" };
    CHECK(l.n == COUNT(want));
    for (isize i = 0; i < l.n && i < COUNT(want); i++) CHECK_STR(l.e[i].name, want[i]);
    CHECK(l.hidden == 3);
    isize i;
    CHECK((i = listing_find(&l, S("Adir"))) >= 0 && l.e[i].kind == K_DIR && l.e[i].size == 0);
    CHECK((i = listing_find(&l, S("link-dir"))) >= 0 && l.e[i].kind == K_LINK_DIR && kind_is_dir(l.e[i].kind));
    CHECK((i = listing_find(&l, S("link-file"))) >= 0 && l.e[i].kind == K_LINK_FILE && l.e[i].size == 1);
    CHECK((i = listing_find(&l, S("link-broken"))) >= 0 && l.e[i].kind == K_LINK_BROKEN && l.e[i].size == 0 &&
          l.e[i].mtime == 0 && !kind_is_dir(l.e[i].kind));
    CHECK((i = listing_find(&l, S("pipe"))) >= 0 && l.e[i].kind == K_OTHER);
    CHECK((i = listing_find(&l, S("b10.txt"))) >= 0 && l.e[i].kind == K_FILE && l.e[i].size == 10 && S_ISREG(l.e[i].mode));
    CHECK((i = listing_find(&l, S("b2.txt"))) >= 0 && l.e[i].mtime == 1700000000);
    CHECK(listing_find(&l, S(".hidden")) < 0);
    CHECK(listing_find(&l, S("nothing")) == -1);

    // An empty folder, a missing one, one without read permission.
    mkdirs(at("empty"));
    CHECK(fs_list(&ta, at("empty"), false, (Sort){ 0 }, &l, &err) && l.n == 0 && l.hidden == 0);
    CHECK(!fs_list(&ta, at("missing"), false, (Sort){ 0 }, &l, &err) && strstr(err.msg, "No such file") && strstr(err.msg, "missing"));
    mkdirs(at("closed"));
    chmod(at("closed"), 0);
    bool can = access(at("closed"), R_OK) == 0;   // root reads anything
    if (!can) CHECK(!fs_list(&ta, at("closed"), false, (Sort){ 0 }, &l, &err) && strstr(err.msg, "Permission denied"));
    chmod(at("closed"), 0755);

    // More entries than the first array holds (256): it grows, and keeps them all in order.
    for (int k = 0; k < 600; k++) {
        char name[64];
        snprintf(name, sizeof name, "many/f%d", k);
        put(at("%s", name), "");
    }
    CHECK(fs_list(&ta, at("many"), false, (Sort){ 0 }, &l, &err) && l.n == 600);
    bool sorted = true;
    for (isize k = 0; k < l.n; k++) {
        char want_name[32];
        snprintf(want_name, sizeof want_name, "f%td", k);
        sorted &= str_eq(l.e[k].name, str_from_c(want_name));
    }
    CHECK(sorted);

    // A full arena is an error, not a crash.
    static u8 tiny_mem[KiB(8)];
    Arena tiny;
    arena_init(&tiny, "tiny", tiny_mem, sizeof tiny_mem);
    CHECK(!fs_list(&tiny, at("many"), false, (Sort){ 0 }, &l, &err) && strstr(err.msg, "too many"));
}

// ---- MIME -----------------------------------------------------------------------------------

static void test_mime(void)
{
    arena_reset(&ta);
    put(at("data1/mime/globs2"),
        "# comment\n"
        "#50:text/x-commented:*.commented\n"   // a comment that parses as a line
        "50:text/x-csrc:*.c\n"
        "50:application/x-rcs:*,v\n"            // a suffix without a dot: a pattern
        "40:text/x-short:READ*\n"               // the same weight as README*, shorter: loses
        "50:text/x-shorter:READ*\n"
        "50::*.shadow\n"                        // no type: skipped, so it can't hide the next
        "40:text/x-shadowed:*.shadow\n"
        "30:text/x-low-first:LOW*\n"            // a pattern listed first with less weight
        "60:text/x-high-later:LOW*\n"
        "50:text/x-c++src:*.C:cs\n"
        "50:application/gzip:*.gz\n"
        "50:application/x-compressed-tar:*.tar.gz\n"
        "50:text/x-makefile:Makefile\n"
        "50:text/x-readme:README*\n"
        "40:text/x-other-readme:README*\n"
        "50:text/x-log:*.log\n"
        "60:text/x-heavy-log:*.log\n"
        "50:application/x-backup:*~\n"
        "50:text/x-weird:*.[ch]pp\n"
        "50:bad-line-without-glob\n"
        "50::*.empty-type\n"
        "50:image/png:*.png\n"
        "50:text/x-tiebreak:*.tb\n");
    put(at("data2/mime/globs2"),
        "50:text/x-tiebreak-later:*.tb\n"
        "50:image/x-other-png:*.png\n");
    put(at("data1/mime/generic-icons"), "text/x-csrc:text-x-script\nimage/png:image-x-generic\n");
    put(at("data1/mime/icons"), "application/gzip:package-x-generic\n");
    // A kind of a kind (the first parent kept, the user's dir first), and a loop.
    put(at("data1/mime/subclasses"), "application/x-kid application/x-mom\napplication/x-loop application/x-loop\napplication/x-bare \n");
    put(at("data2/mime/subclasses"), "application/x-kid application/x-shadow\napplication/x-mom application/x-gran\n");
    put(at("data2/mime/generic-icons"), "text/x-csrc:shadowed\napplication/x-gran:x-office-document\n");
    env_set("XDG_DATA_HOME", at("data1"));
    env_set("XDG_DATA_DIRS", at("data2"));
    MimeDb db;
    mime_load(&ta, &db);
    struct { const char *name, *want; } cases[] = {
        { "main.c", "text/x-csrc" },
        { "MAIN.c", "text/x-csrc" },
        { "main.C", "text/x-c++src" },      // case-sensitive *.C before the folded *.c
        { "x.tar.gz", "application/x-compressed-tar" },   // the longer suffix
        { "x.gz", "application/gzip" },
        { "x.GZ", "application/gzip" },
        { "Makefile", "text/x-makefile" },
        { "makefile", "text/x-makefile" },  // literals fold too
        { "README.md", "text/x-readme" },   // a pattern: the higher weight
        { "README", "text/x-readme" },
        { "app.log", "text/x-heavy-log" },  // same suffix: the higher weight
        { "notes~", "application/x-backup" },
        { "a.cpp", "text/x-weird" },
        { "a.hpp", "text/x-weird" },
        { "a.png", "image/png" },           // the first data dir's
        { "a.tb", "text/x-tiebreak" },      // an exact tie: the first data dir's line
        { "nothing.xyz", "" },
        { "noext", "" },
        { "x.empty-type", "" },
        { ".c", "text/x-csrc" },
        { "x.commented", "" },
        { "file,v", "application/x-rcs" },
        { "READING", "text/x-shorter" },
        { "x.shadow", "text/x-shadowed" },
        { "LOWER", "text/x-high-later" },
    };
    for (int i = 0; i < COUNT(cases); i++) CHECK_STR(mime_of_name(&ta, &db, str_from_c(cases[i].name)), cases[i].want);

    Entry dir = { .name = S("x.c"), .kind = K_LINK_DIR }, broken = { .name = S("x.c"), .kind = K_LINK_BROKEN },
          other = { .name = S("x.c"), .kind = K_OTHER }, exe = { .name = S("run"), .kind = K_FILE, .mode = 0100755 },
          plain = { .name = S("run"), .kind = K_FILE, .mode = 0100644 }, c = { .name = S("x.c"), .kind = K_LINK_FILE, .mode = 0100755 };
    CHECK_STR(mime_of(&ta, &db, &dir), "inode/directory");
    CHECK_STR(mime_of(&ta, &db, &broken), "inode/symlink");
    CHECK_STR(mime_of(&ta, &db, &other), "inode/x-other");
    CHECK_STR(mime_of(&ta, &db, &exe), "application/x-executable");
    CHECK_STR(mime_of(&ta, &db, &plain), "application/octet-stream");
    CHECK_STR(mime_of(&ta, &db, &c), "text/x-csrc");   // a glob beats the execute bit

    Str names[MIME_ICON_NAMES];
    isize n = mime_icon_names(&ta, &db, S("text/x-csrc"), names);
    CHECK(n == 5);
    CHECK_STR(names[0], "text-x-csrc");
    CHECK_STR(names[1], "text-x-script");
    CHECK_STR(names[2], "text-x-generic");
    CHECK_STR(names[3], "application-x-generic");
    CHECK_STR(names[4], "empty");   // a blank page, last (2026-10-27)
    n = mime_icon_names(&ta, &db, S("application/gzip"), names);
    CHECK(n == 4);
    CHECK_STR(names[0], "package-x-generic");
    CHECK_STR(names[1], "application-gzip");
    CHECK_STR(names[2], "application-x-generic");
    CHECK_STR(names[3], "empty");
    n = mime_icon_names(&ta, &db, S("inode/directory"), names);
    CHECK(n == 4);
    CHECK_STR(names[0], "inode-directory");
    CHECK_STR(names[1], "inode-x-generic");
    CHECK_STR(names[2], "application-x-generic");
    // Up the subclasses after the type's own (2026-10-27): its parent's, the parent's parent's with
    // its generic icon; then the blank page.
    n = mime_icon_names(&ta, &db, S("application/x-kid"), names);
    CHECK(n == 6);
    CHECK_STR(names[0], "application-x-kid");
    CHECK_STR(names[1], "application-x-generic");
    CHECK_STR(names[2], "application-x-mom");
    CHECK_STR(names[3], "application-x-gran");
    CHECK_STR(names[4], "x-office-document");
    CHECK_STR(names[5], "empty");
    // A loop stops after MIME_PARENTS_MAX, within MIME_ICON_NAMES.
    n = mime_icon_names(&ta, &db, S("application/x-loop"), names);
    CHECK(n == 2 + MIME_PARENTS_MAX + 1 && n <= MIME_ICON_NAMES);
    CHECK_STR(names[n - 2], "application-x-loop");
    CHECK_STR(names[n - 1], "empty");
    n = mime_icon_names(&ta, &db, S("application/x-bare"), names);   // (a line with no parent)
    CHECK(n == 3);
    CHECK_STR(names[1], "application-x-generic");
    // No generic icon in generic-icons (the system's has none for image/png): the media type's,
    // the spec's.
    n = mime_icon_names(&ta, &db, S("image/webp"), names);
    CHECK(n == 4);
    CHECK_STR(names[0], "image-webp");
    CHECK_STR(names[1], "image-x-generic");
    CHECK_STR(names[2], "application-x-generic");
    env_restore();

    // A file that reads shorter than fstat said (it changed while read): nothing, not a part.
    // sysfs files say 4096 and hold less, which stands in for the race.
    const char *sys = "/sys/kernel/mm/transparent_hugepage/enabled";
    if (access(sys, R_OK) == 0) CHECK(read_file(&ta, sys).len == 0);
    CHECK(read_file(&ta, at("data1/mime/globs2")).len > 100);
    CHECK(read_file(&ta, at("data1")).len == 0);   // a directory
    CHECK(read_file(&ta, at("nothing-here")).len == 0);
    // procfs says 0 bytes: read to the end. An empty regular file is empty either way.
    Str mi = read_file(&ta, "/proc/self/mountinfo");
    CHECK(mi.len > 0 && mi.p[mi.len] == 0 && str_find(mi, '\n') > 0);
    CHECK(mkfifo(at("a-fifo"), 0644) == 0 && read_file(&ta, at("a-fifo")).len == 0);   // returns at once
    put(at("empty-file"), "");
    CHECK(read_file(&ta, at("empty-file")).len == 0);
    // A tail too small for the whole: nothing, and the arena keeps nothing.
    static u8 small_mem[64];
    Arena small;
    arena_init(&small, "small", small_mem, sizeof small_mem);
    CHECK(read_file(&small, "/proc/self/mountinfo").len == 0 && small.used == 0);

    // No data dirs at all: nothing matches, nothing breaks.
    env_set("XDG_DATA_HOME", at("none1"));
    env_set("XDG_DATA_DIRS", at("none2"));
    mime_load(&ta, &db);
    CHECK(db.n == 0);
    CHECK_STR(mime_of_name(&ta, &db, S("main.c")), "");
    env_restore();

    // The defaults: ~/.local/share, then /usr/local/share:/usr/share.
    env_set("XDG_DATA_HOME", NULL);
    env_set("XDG_DATA_DIRS", NULL);
    env_set("HOME", at("home"));
    const char *dirs[MIME_DIRS_MAX];
    isize nd = data_dirs(&ta, dirs, MIME_DIRS_MAX);
    CHECK(nd == 3 && strcmp(dirs[0], at("home/.local/share")) == 0 && strcmp(dirs[1], "/usr/local/share") == 0 &&
          strcmp(dirs[2], "/usr/share") == 0);
    env_restore();
    env_set("XDG_DATA_DIRS", "/a::/b:");   // empty items skipped
    env_set("XDG_DATA_HOME", "/h");
    nd = data_dirs(&ta, dirs, MIME_DIRS_MAX);
    CHECK(nd == 3 && strcmp(dirs[0], "/h") == 0 && strcmp(dirs[1], "/a") == 0 && strcmp(dirs[2], "/b") == 0);
    env_restore();
}

// ---- icons ----------------------------------------------------------------------------------

static void test_icons(void)
{
    arena_reset(&ta);
    // Child inherits Parent; hicolor last. Sizes: Child has 24 at scales 1 and 2 and 48; Parent
    // has 16, a Scalable 8-64 and a Threshold 32 (+-2 by default).
    put(at("icons/Child/index.theme"),
        "[Icon Theme]\nName=Child\nInherits=Parent\n"
        "Directories=24x24/places,24x24@2x/places,48x48/places,missing/places\n\n"
        "[24x24/places]\nSize=24\nType=Fixed\n\n"
        "[24x24@2x/places]\nSize=24\nScale=2\nType=Fixed\n\n"
        "[48x48/places]\nSize=48\nType=Fixed\n\n"
        "[missing/places]\nSize=24\nType=Fixed\n");
    put(at("icons/Child/24x24/places/folder.png"), "");
    put(at("icons/Child/24x24@2x/places/folder.png"), "");
    put(at("icons/Child/48x48/places/user-home.png"), "");
    put(at("icons/Child/24x24/places/not-an-icon.svg"), "");
    put(at("icons/Parent/index.theme"),
        "[Icon Theme]\nName=Parent\nInherits=hicolor, Child\n"   // a loop back to Child: ignored
        "Directories=16x16/mimetypes,scalable/mimetypes,32x32/mimetypes\n"
        "[16x16/mimetypes]\nSize=16\nType=Fixed\n"
        "[scalable/mimetypes]\nSize=16\nMinSize=8\nMaxSize=64\nType=Scalable\n"
        "[32x32/mimetypes]\nSize=32\n");
    put(at("icons/Parent/16x16/mimetypes/text-x-generic.png"), "");
    put(at("icons/Parent/scalable/mimetypes/text-x-csrc.png"), "");
    put(at("icons/Parent/scalable/mimetypes/only-svg.svg"), "");
    put(at("icons/Parent/32x32/mimetypes/image-png.png"), "");
    put(at("icons/Parent/16x16/mimetypes/folder.png"), "");
    put(at("icons/Child/index.theme.extra"), "");
    put(at("icons/Order/index.theme"),
        "[Icon Theme]\nDirectories=48x48/a,16x16/a\nScaledDirectories=24x24@2x/a\n"
        "[48x48/a]\nSize=48\n[16x16/a]\nSize=16\nType=Fixed\n[24x24@2x/a]\nSize=24\nScale=2\nType=Fixed\n");
    put(at("icons/Order/48x48/a/both.png"), "");
    put(at("icons/Order/16x16/a/both.png"), "");
    put(at("icons/Order/24x24@2x/a/both.png"), "");
    for (int k = 0; k < 100; k++) put(at("icons/Order/48x48/a/many%d.png", k), "");
    put(at("icons/hicolor/index.theme"),
        "[Icon Theme]\nName=Hicolor\nDirectories=48x48/mimetypes,48x48/apps,48x48/places\n"
        "[48x48/mimetypes]\nSize=48\nContext=MimeTypes\nType=Threshold\n"
        "[48x48/apps]\nSize=48\nContext=Applications\n"
        "[48x48/places]\nSize=48\nContext=Places\n");
    put(at("icons/hicolor/48x48/mimetypes/fallback.png"), "");
    put(at("icons/hicolor/48x48/apps/an-app.png"), "");
    put(at("icons/hicolor/48x48/places/a-place.png"), "");
    // A second base dir with more of Child's 24x24 (the spec: a theme's dirs in every base dir).
    put(at("home/.icons/Child/24x24/places/folder-download.png"), "");
    env_set("HOME", at("home"));
    env_set("XDG_DATA_HOME", at("datahome"));
    env_set("XDG_DATA_DIRS", root);
    IconDb db;
    icon_load(&ta, &db, S("Child"));
    CHECK(db.n == 3);
    CHECK_STR(db.t[0].name, "Child");
    CHECK_STR(db.t[1].name, "Parent");
    CHECK_STR(db.t[2].name, "hicolor");

#define FIND(size, scale, ...)                                                    \
    ({                                                                            \
        Str ns_[] = { __VA_ARGS__ };                                              \
        const char *f_ = icon_find(&ta, &db, ns_, COUNT(ns_), (size), (scale));   \
        f_ ? f_ : "";                                                             \
    })
    CHECK(strcmp(FIND(24, 1, S("folder")), at("icons/Child/24x24/places/folder.png")) == 0);
    CHECK(strcmp(FIND(24, 2, S("folder")), at("icons/Child/24x24@2x/places/folder.png")) == 0);
    // 16 has no exact match in Child: its closest (24 at scale 1 is 8 away, @2x 32), before
    // Parent's exact 16, since themes go first.
    CHECK(strcmp(FIND(16, 1, S("folder")), at("icons/Child/24x24/places/folder.png")) == 0);
    // No exact match: one at least as big before a closer smaller one (F6.3), so a bigger size is
    // scaled down, not up: 32 is 24@2x's 48 device pixels, not 24's; when all are smaller, the
    // biggest; when all are bigger, the smallest.
    CHECK(strcmp(FIND(32, 1, S("folder")), at("icons/Child/24x24@2x/places/folder.png")) == 0);
    CHECK(strcmp(FIND(64, 2, S("folder")), at("icons/Child/24x24@2x/places/folder.png")) == 0);
    CHECK(strcmp(FIND(20, 1, S("folder")), at("icons/Child/24x24/places/folder.png")) == 0);
    CHECK(strcmp(FIND(24, 1, S("folder-download")), at("home/.icons/Child/24x24/places/folder-download.png")) == 0);
    CHECK(strcmp(FIND(24, 1, S("user-home")), at("icons/Child/48x48/places/user-home.png")) == 0);
    // The first name the theme has, theme by theme: Child has neither text name, Parent has both.
    CHECK(strcmp(FIND(24, 1, S("text-x-csrc"), S("text-x-generic")), at("icons/Parent/scalable/mimetypes/text-x-csrc.png")) == 0);
    CHECK(strcmp(FIND(24, 1, S("text-x-nothing"), S("text-x-generic")), at("icons/Parent/16x16/mimetypes/text-x-generic.png")) == 0);
    // An earlier name in a later theme beats a later name (a fallback) in an earlier theme.
    CHECK(strcmp(FIND(24, 1, S("text-x-csrc"), S("folder")), at("icons/Parent/scalable/mimetypes/text-x-csrc.png")) == 0);
    CHECK(strcmp(FIND(48, 1, S("fallback")), at("icons/hicolor/48x48/mimetypes/fallback.png")) == 0);
    CHECK(strcmp(FIND(48, 1, S("a-place")), at("icons/hicolor/48x48/places/a-place.png")) == 0);
    CHECK(strcmp(FIND(48, 1, S("an-app")), "") == 0);          // Applications: not read at start
    CHECK(db.t[2].ndirs == 2);
    CHECK(icon_context_used(S("Mimetypes"), false) && !icon_context_used(S("Emotes"), false) && icon_context_used(STR_EMPTY, false));
    CHECK(icon_context_used(S("Status"), false));   // user-trash-full (2026-10-30)
    CHECK(!icon_context_used(S("Apps"), false) && !icon_context_used(S("Applications"), false));
    // The apps' own, apart (Open With, F7.3): only Apps folders, an SVG too, a PNG before an SVG.
    CHECK(icon_context_used(S("Apps"), true) && icon_context_used(S("Applications"), true) && !icon_context_used(S("Places"), true));
    CHECK(!icon_context_used(STR_EMPTY, true));
    put(at("icons/hicolor/48x48/apps/vec-app.svg"), "");
    put(at("icons/hicolor/48x48/apps/an-app.svg"), "");
    put(at("icons/hicolor/48x48/apps/both.svg"), "");   // made before its PNG: read in either order
    put(at("icons/hicolor/48x48/apps/both.png"), "");
    put(at("icons/hicolor/48x48/apps/not-an-icon.txt"), "");
    IconDb apps_db;
    icon_load_apps(&ta, &apps_db, S("Child"));
    CHECK(apps_db.n == 3 && apps_db.t[2].ndirs == 1);
    const char *af = icon_find(&ta, &apps_db, (Str[]){ S("an-app") }, 1, 48, 1);
    CHECK(af && strcmp(af, at("icons/hicolor/48x48/apps/an-app.png")) == 0);
    af = icon_find(&ta, &apps_db, (Str[]){ S("both") }, 1, 48, 1);
    CHECK(af && strcmp(af, at("icons/hicolor/48x48/apps/both.png")) == 0);
    af = icon_find(&ta, &apps_db, (Str[]){ S("vec-app") }, 1, 48, 1);
    CHECK(af && strcmp(af, at("icons/hicolor/48x48/apps/vec-app.svg")) == 0);
    CHECK(!icon_find(&ta, &apps_db, (Str[]){ S("a-place") }, 1, 48, 1) && !icon_find(&ta, &apps_db, (Str[]){ S("not-an-icon") }, 1, 48, 1));
    CHECK(strcmp(FIND(48, 1, S("vec-app")), "") == 0);   // the files' lookup takes no SVG
    CHECK(strcmp(FIND(24, 1, S("only-svg")), "") == 0);       // PNG only
    CHECK(strcmp(FIND(24, 1, S("not-an-icon")), "") == 0);
    CHECK(strcmp(FIND(24, 1, S("absent")), "") == 0);
    // Threshold 32 +-2: 30 and 34 match it.
    CHECK(strcmp(FIND(30, 1, S("image-png")), at("icons/Parent/32x32/mimetypes/image-png.png")) == 0);
    env_restore();

    // What index.theme said, as read.
    CHECK(db.t[1].ndirs == 3);
    const IconDir *pd = db.t[1].dirs;
    CHECK(pd[0].type == IT_FIXED && pd[0].size == 16 && pd[0].scale == 1);
    CHECK(pd[1].type == IT_SCALABLE && pd[1].min == 8 && pd[1].max == 64);
    CHECK(pd[2].type == IT_THRESHOLD && pd[2].threshold == 2 && pd[2].min == 32 && pd[2].max == 32);
    bool scaled = false;
    for (isize k = 0; k < db.t[0].ndirs; k++) scaled |= db.t[0].dirs[k].scale == 2 && db.t[0].dirs[k].size == 24;
    CHECK(scaled);

    // A theme that inherits nothing still falls back on hicolor; ScaledDirectories are read; an
    // exact match beats an earlier directory as close (48 px at scale 1 is 48 device pixels, as
    // 24 at scale 2 is); a directory of more than 64 icons keeps them all.
    env_set("HOME", at("home"));
    env_set("XDG_DATA_HOME", at("datahome"));
    env_set("XDG_DATA_DIRS", root);
    icon_load(&ta, &db, S("Order"));
    CHECK(db.n == 2);
    CHECK_STR(db.t[1].name, "hicolor");
    CHECK(db.t[0].ndirs == 3);
    CHECK(strcmp(FIND(24, 2, S("both")), at("icons/Order/24x24@2x/a/both.png")) == 0);
    CHECK(strcmp(FIND(24, 2, S("many0")), at("icons/Order/48x48/a/many0.png")) == 0);
    CHECK(strcmp(FIND(24, 2, S("many99")), at("icons/Order/48x48/a/many99.png")) == 0);
    // A smaller directory after a bigger one doesn't win for being closer (F6.3): 20 px is 48's.
    CHECK(strcmp(FIND(20, 1, S("both")), at("icons/Order/48x48/a/both.png")) == 0);
    env_restore();

    // A theme that isn't installed (F5.4): its name cut at the last '-' until one is, else
    // Adwaita; never hicolor alone.
    put(at("icons/Yaru/index.theme"), "[Icon Theme]\nDirectories=24x24/places\n[24x24/places]\nSize=24\nType=Fixed\n");
    put(at("icons/Yaru/24x24/places/folder.png"), "");
    put(at("icons/Yaru-sage/index.theme"), "[Icon Theme]\nInherits=Yaru\nDirectories=24x24/places\n[24x24/places]\nSize=24\n");
    put(at("icons/Adwaita/index.theme"), "[Icon Theme]\nDirectories=24x24/places\n[24x24/places]\nSize=24\n");
    env_set("HOME", at("home"));
    env_set("XDG_DATA_HOME", at("datahome"));
    env_set("XDG_DATA_DIRS", root);
    static const struct { const char *asked, *first; isize n; } FB[] = {
        { "Yaru-grey", "Yaru", 2 },               // Omarchy's white
        { "Yaru-gray", "Yaru", 2 },               // and vantablack
        { "Yaru-sage-dark", "Yaru-sage", 3 },     // two steps: Yaru-sage-dark, Yaru-sage (and its Yaru)
        { "Yaru-sage", "Yaru-sage", 3 },          // there: as before
        { "Yaru-sage-abc", "Yaru-sage", 3 },      // any length after the dash
        { "Nothing", "Yaru", 2 },                 // no dash, not there: plain Yaru
        { "Nothing-at-all", "Yaru", 2 },
        { "", "Yaru", 2 },                        // no icons.theme
        { "-", "Yaru", 2 },
        { "Yaru-", "Yaru", 2 },
    };
    for (int k = 0; k < COUNT(FB); k++) {
        icon_load(&ta, &db, str_from_c(FB[k].asked));
        checks++;
        if (db.n != FB[k].n || !str_eq(db.t[0].name, str_from_c(FB[k].first)) || !str_eq(db.t[db.n - 1].name, S("hicolor"))) {
            failures++;
            fprintf(stderr, "icon fallback for '%s': %td themes, first %.*s\n", FB[k].asked, db.n, STR_ARG(db.t[0].name));
        }
    }
    icon_load(&ta, &db, S("Yaru-grey"));
    CHECK(strcmp(FIND(24, 1, S("folder")), at("icons/Yaru/24x24/places/folder.png")) == 0);
    // No Yaru either: Adwaita.
    rename(at("icons/Yaru"), at("icons/Yaru-away"));
    icon_load(&ta, &db, S("Nothing"));
    CHECK(db.n == 2 && str_eq(db.t[0].name, S("Adwaita")));
    icon_load(&ta, &db, S("Yaru-grey"));
    CHECK(db.n == 2 && str_eq(db.t[0].name, S("Adwaita")));
    rename(at("icons/Yaru-away"), at("icons/Yaru"));
    env_restore();

    // The directory rules on their own.
    IconDir fixed = { .size = 24, .scale = 1, .min = 24, .max = 24, .threshold = 2, .type = IT_FIXED };
    IconDir fixed2 = { .size = 24, .scale = 2, .min = 24, .max = 24, .threshold = 2, .type = IT_FIXED };
    IconDir scal = { .size = 16, .scale = 1, .min = 8, .max = 64, .threshold = 2, .type = IT_SCALABLE };
    IconDir thr = { .size = 32, .scale = 1, .min = 32, .max = 32, .threshold = 2, .type = IT_THRESHOLD };
    CHECK(icon_dir_matches(&fixed, 24, 1) && !icon_dir_matches(&fixed, 23, 1) && !icon_dir_matches(&fixed, 25, 1));
    CHECK(!icon_dir_matches(&fixed, 24, 2) && icon_dir_matches(&fixed2, 24, 2));
    CHECK(icon_dir_matches(&scal, 8, 1) && icon_dir_matches(&scal, 64, 1) && !icon_dir_matches(&scal, 7, 1) && !icon_dir_matches(&scal, 65, 1));
    CHECK(icon_dir_matches(&thr, 30, 1) && icon_dir_matches(&thr, 34, 1) && !icon_dir_matches(&thr, 29, 1) && !icon_dir_matches(&thr, 35, 1));
    CHECK(icon_dir_distance(&fixed, 16, 1) == 8 && icon_dir_distance(&fixed, 32, 1) == 8 && icon_dir_distance(&fixed, 24, 1) == 0);
    CHECK(icon_dir_distance(&fixed2, 24, 1) == 24 && icon_dir_distance(&fixed2, 24, 2) == 0);
    CHECK(icon_dir_distance(&scal, 4, 1) == 4 && icon_dir_distance(&scal, 70, 1) == 6 && icon_dir_distance(&scal, 30, 1) == 0);
    CHECK(icon_dir_distance(&thr, 28, 1) == 2 && icon_dir_distance(&thr, 36, 1) == 2 && icon_dir_distance(&thr, 33, 1) == 0);
    CHECK(icon_dir_distance(&fixed2, 30, 2) == 12);   // 60 against 48
    CHECK(!icon_dir_short(&fixed, 24, 1) && icon_dir_short(&fixed, 25, 1) && !icon_dir_short(&fixed2, 24, 2) && icon_dir_short(&fixed2, 25, 2));
    CHECK(!icon_dir_short(&fixed2, 48, 1) && !icon_dir_short(&fixed, 12, 2) && icon_dir_short(&fixed, 13, 2));
    CHECK(!icon_dir_short(&scal, 64, 1) && icon_dir_short(&scal, 65, 1) && !icon_dir_short(&thr, 34, 1) && icon_dir_short(&thr, 35, 1));

    // ini parsing.
    Str ini = S("[A]\nx = 1\n y=two \n[B]\nx=3\nn=12a\nbig=1234567890\n");
    CHECK_STR(ini_get(ini, S("A"), S("x")), "1");
    CHECK_STR(ini_get(ini, S("A"), S("y")), "two");
    CHECK_STR(ini_get(ini, S("B"), S("x")), "3");
    CHECK_STR(ini_get(ini, S("B"), S("y")), "");
    CHECK_STR(ini_get(ini, S("C"), S("x")), "");
    CHECK(ini_int(ini, S("B"), S("x"), 9) == 3 && ini_int(ini, S("B"), S("n"), 9) == 9 && ini_int(ini, S("B"), S("big"), 9) == 9);
    CHECK(ini_int(ini, S("B"), S("absent"), 7) == 7);
    CHECK_STR(ini_get(S("[Ax\nk=1\n"), S("A"), S("k")), "");   // no closing ]: not a section
    Str item, list = S(" a, ,b ,,");
    isize iat = 0;
    CHECK(next_item(list, &iat, &item) && str_eq(item, S("a")));
    CHECK(next_item(list, &iat, &item) && str_eq(item, S("b")));
    CHECK(!next_item(list, &iat, &item));

    // No theme at all: only hicolor, when it is there.
    env_set("HOME", at("nohome"));
    env_set("XDG_DATA_HOME", at("nodata"));
    env_set("XDG_DATA_DIRS", at("nodata2"));
    icon_load(&ta, &db, S("Child"));
    CHECK(db.n == 0);
    CHECK(strcmp(FIND(24, 1, S("folder")), "") == 0);
    env_restore();
#undef FIND
}

// ---- theme ----------------------------------------------------------------------------------

static bool rgb_is(Rgb c, u8 r, u8 g, u8 b)
{
    return c.r == r && c.g == g && c.b == b;
}

static void test_theme(void)
{
    arena_reset(&ta);
    put(at("state/omarchy/current/theme/colors.toml"),
        "# a theme\n"
        "accent = \"#112233\"\n"
        "background=\"#AABBCC\"   # trailing comment\n"
        "foreground = '#010203'\n"
        "dark_foreground = \"#zz0000\"\n"     // bad: keeps the default
        "selection = \"#123\"\n"              // too short: default
        "lighter_background = #445566\n"      // unquoted is fine
        "red = \"#ff0000\"\n"
        "unknown = \"#ffffff\"\n"
        "# red = \"#000000\"\n"
        "[section]\n"
        "accent = \"#999999\"\n");
    put(at("state/omarchy/current/theme/icons.theme"), "Yaru-magenta\n");
    env_set("XDG_STATE_HOME", at("state"));
    Theme t = theme_load(&ta);
    CHECK(rgb_is(t.accent, 0x11, 0x22, 0x33));
    CHECK(rgb_is(t.bg, 0xaa, 0xbb, 0xcc));
    CHECK(rgb_is(t.fg, 1, 2, 3));
    CHECK(rgb_is(t.fg_dim, THEME_DEFAULT.fg_dim.r, THEME_DEFAULT.fg_dim.g, THEME_DEFAULT.fg_dim.b));
    CHECK(rgb_is(t.selection, THEME_DEFAULT.selection.r, THEME_DEFAULT.selection.g, THEME_DEFAULT.selection.b));
    CHECK(rgb_is(t.bg_light, 0x44, 0x55, 0x66));
    CHECK(rgb_is(t.red, 0xff, 0, 0));
    CHECK(t.ncolors == 5);
    CHECK(strcmp(t.icons, "Yaru-magenta") == 0);
    env_restore();

    // No theme: Tokyo Night, no icon theme.
    env_set("XDG_STATE_HOME", at("nostate"));
    t = theme_load(&ta);
    CHECK(memcmp(&t, &THEME_DEFAULT, sizeof t) == 0);
    env_restore();

    // XDG_STATE_HOME unset: under HOME.
    put(at("h2/.local/state/omarchy/current/theme/colors.toml"), "accent=\"#000001\"\n");
    put(at("h2/.local/state/omarchy/current/theme/icons.theme"), "../evil\n");   // a path: refused
    env_set("XDG_STATE_HOME", NULL);
    env_set("HOME", at("h2"));
    t = theme_load(&ta);
    CHECK(rgb_is(t.accent, 0, 0, 1) && t.ncolors == 1 && t.icons[0] == 0);
    env_restore();

    Rgb c;
    CHECK(parse_rgb(S("#aBcDeF"), &c) && rgb_is(c, 0xab, 0xcd, 0xef));
    CHECK(parse_rgb(S("\"#000000\""), &c) && rgb_is(c, 0, 0, 0));
    CHECK(!parse_rgb(S("\"#000000'"), &c));
    CHECK(!parse_rgb(S("#0000000"), &c) && !parse_rgb(S("000000"), &c) && !parse_rgb(S("#00000g"), &c));
    CHECK(!parse_rgb(S("x123456"), &c));
    CHECK(hex_digit('9') == 9 && hex_digit('a') == 10 && hex_digit('F') == 15 && hex_digit('g') == -1 && hex_digit('/') == -1);
}

static void test_user_dirs(void)
{
    arena_reset(&ta);
    put(at("uh/.config/user-dirs.dirs"),
        "# comment\n"
        "XDG_DESKTOP_DIR=\"$HOME/Desktop/\"\n"
        "XDG_DOWNLOAD_DIR=\"$HOME/Downloads\"\n"
        "XDG_TEMPLATES_DIR=\"$HOME/\"\n"        // home itself: skipped
        "XDG_MUSIC_DIR=\"/srv/music\"\n"        // absolute
        "XDG_VIDEOS_DIR=\"relative\"\n"         // neither: skipped
        "XDG_UNKNOWN_DIR=\"$HOME/x\"\n");
    env_set("HOME", at("uh"));
    env_set("XDG_CONFIG_HOME", NULL);
    UserDirs u;
    user_dirs_load(&ta, &u);
    CHECK_STR(user_dir_icon(&u, at("uh")), "user-home");
    CHECK_STR(user_dir_icon(&u, at("uh/Desktop")), "user-desktop");
    CHECK_STR(user_dir_icon(&u, at("uh/Downloads")), "folder-download");
    CHECK_STR(user_dir_icon(&u, "/srv/music"), "folder-music");
    CHECK_STR(user_dir_icon(&u, at("uh/x")), "");
    CHECK_STR(user_dir_icon(&u, "relative"), "");
    CHECK(u.n == 4);
    env_restore();
    put(at("cfg/user-dirs.dirs"), "XDG_PICTURES_DIR=\"$HOME/Pics\"\n");
    env_set("HOME", at("uh"));
    env_set("XDG_CONFIG_HOME", at("cfg"));
    user_dirs_load(&ta, &u);
    CHECK(u.n == 2);
    CHECK_STR(user_dir_icon(&u, at("uh/Pics")), "folder-pictures");
    env_restore();
}

// ---- spawning -------------------------------------------------------------------------------

// Waits until path holds a line, up to 2 s (the child runs detached).
static bool wait_file(const char *path, char *out, size_t cap)
{
    for (int i = 0; i < 200; i++) {
        FILE *f = fopen(path, "r");
        if (f) {
            size_t n = fread(out, 1, cap - 1, f);
            fclose(f);
            out[n] = 0;
            if (n > 0 && out[n - 1] == '\n') return out[n - 1] = 0, true;
        }
        struct timespec ts = { 0, 10 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    return false;
}

static void fake(const char *dir, const char *name, const char *log)
{
    char path[1024], text[2048];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    // The session id from /proc with builtins only: PATH holds just the fakes.
    snprintf(text, sizeof text, "#!/bin/sh\nread -r _ _ _ _ _ sid _ < /proc/$$/stat\necho \"%s $* sid=$sid pid=$$\" >> %s\n", name, log);
    put(path, text);
    chmod(path, 0755);
}

static void test_spawn(void)
{
    spawn_init();
    char out[2048], want[1200];
    // Without uwsm-app: straight to gio open, in its own session.
    mkdirs(at("bin1"));
    fake(at("bin1"), "gio", at("spawn1.log"));
    fake(at("bin1"), "xdg-open", at("spawn1-never.log"));
    fake(at("bin1"), "xdg-terminal-exec", at("spawn1.log"));
    env_set("PATH", at("bin1"));
    Err err;
    CHECK(spawn_open("/some/file with space", &err));
    CHECK(wait_file(at("spawn1.log"), out, sizeof out));
    CHECK(strncmp(out, "gio open /some/file with space sid=", 35) == 0);
    CHECK(access(at("spawn1-never.log"), F_OK) != 0);
    // Its own session: sid equals its pid.
    char *sid = strstr(out, "sid="), *pid = strstr(out, "pid=");
    CHECK(sid && pid && atoi(sid + 4) == atoi(pid + 4) && atoi(sid + 4) != getsid(0));
    unlink(at("spawn1.log"));
    CHECK(spawn_terminal("/a/dir", &err));
    CHECK(wait_file(at("spawn1.log"), out, sizeof out));
    CHECK(strncmp(out, "xdg-terminal-exec --dir=/a/dir sid=", 35) == 0);
    env_restore();

    // With uwsm-app: through it.
    mkdirs(at("bin2"));
    fake(at("bin2"), "uwsm-app", at("spawn2.log"));
    fake(at("bin2"), "gio", at("spawn2-never.log"));
    env_set("PATH", at("bin2"));
    CHECK(spawn_open("/f", &err));
    CHECK(wait_file(at("spawn2.log"), out, sizeof out));
    snprintf(want, sizeof want, "uwsm-app -- gio open /f sid=");
    CHECK(strncmp(out, want, strlen(want)) == 0);
    env_restore();

    // Nothing installed: a clear error, nothing run.
    mkdirs(at("bin3"));
    env_set("PATH", at("bin3"));
    CHECK(!spawn_open("/f", &err) && strcmp(err.msg, "gio is not installed") == 0);
    CHECK(!spawn_terminal("/d", &err) && strcmp(err.msg, "xdg-terminal-exec is not installed") == 0);
    env_restore();
    // uwsm-app without the command behind it: still an error for the command.
    mkdirs(at("bin4"));
    fake(at("bin4"), "uwsm-app", at("spawn4.log"));
    env_set("PATH", at("bin4"));
    CHECK(spawn_open("/f", &err));   // uwsm-app is what runs; it reports the missing gio itself
    env_restore();

    // on_path: empty items and a non-executable file.
    put(at("bin5/plain"), "");
    env_set("PATH", at("bin5"));
    CHECK(!on_path("plain") && !on_path("absent"));
    env_restore();
    env_set("PATH", NULL);
    CHECK(!on_path("sh"));
    env_restore();
    signal(SIGCHLD, SIG_DFL);   // system() at the end waits for its child
}

// ---- ui -------------------------------------------------------------------------------------

static UiInput keys(int n, ...)
{
    UiInput in = ui_input_none();
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) in.ev[in.nev++] = (UiEvent){ (UiKey)va_arg(ap, int), 0 };
    va_end(ap);
    return in;
}

// Each byte of s (ASCII) as a typed character.
static UiInput chars(const char *s)
{
    UiInput in = ui_input_none();
    for (; *s && in.nev < UI_EVENTS_MAX; s++) in.ev[in.nev++] = (UiEvent){ UK_NONE, (u32)(unsigned char)*s };
    return in;
}

// The names find searches: f0 .. f99, and a few with letters, for test_find.
static const char *const find_names[] = { "alpha", "Beta", "gamma.txt", "delta", "BETAMAX", "omega", "caf\xC3\xA9" };
static Str name_at(void *ctx, isize i)
{
    (void)ctx;
    static char buf[24];
    if (i < COUNT(find_names)) return str_from_c(find_names[i]);
    snprintf(buf, sizeof buf, "f%td", i);
    return str_from_c(buf);
}

static void test_ui(void)
{
    Ui u = { 0 };
    UiNames names = { name_at, NULL, NULL };
    ui_resize(&u.list, 10);
    ui_set(&u.list, 100, 0);
    CHECK(u.list.sel == 0 && u.list.top == 0);
    UiInput in = keys(1, UK_DOWN);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.list.sel == 1 && u.list.top == 0);
    in = keys(1, UK_UP);
    ui_handle(&u, names, &in);
    in = keys(1, UK_UP);   // at the top: round to the last (Michael, 2026-10-25), and back
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == u.list.n - 1 && u.list.top == u.list.n - u.list.rows);
    in = keys(1, UK_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 0 && u.list.top == 0);
    in = keys(1, UK_PAGEDOWN);   // a page is rows - 1, so a row stays in view
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 9 && u.list.top == 0);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 18 && u.list.top == 9);
    in = keys(1, UK_PAGEUP);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 9 && u.list.top == 9);
    in = keys(1, UK_END);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 99 && u.list.top == 90);
    in = keys(1, UK_HOME);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 0 && u.list.top == 0);

    // The wheel scrolls one row a notch and leaves the cursor; it stops at the ends.
    in = ui_input_none();
    in.wheel = -3;
    ui_handle(&u, names, &in);
    CHECK(u.list.top == 3 && u.list.sel == 0);
    in.wheel = 1;
    ui_handle(&u, names, &in);
    CHECK(u.list.top == 2);
    in.wheel = 5;
    ui_handle(&u, names, &in);
    CHECK(u.list.top == 0);
    in.wheel = -1000;
    ui_handle(&u, names, &in);
    CHECK(u.list.top == 90 && u.list.sel == 0);
    // A key brings the cursor back into view.
    in = keys(1, UK_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && u.list.top == 1);

    // Clicks: on a row, past the last row.
    ui_set(&u.list, 100, 0);
    in = ui_input_none();
    in.click = 4;
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 4);
    ui_set(&u.list, 3, 0);
    in.click = 5;
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 0);
    in = ui_input_none();
    in.dclick = 2;
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_OPEN && a.index == 2 && u.list.sel == 2);
    in.dclick = 3;   // past the end: nothing
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    in = ui_input_none();
    in.backs = 1;
    CHECK(ui_handle(&u, names, &in).kind == UA_BACK);
    in = ui_input_none();
    in.forwards = 1;
    CHECK(ui_handle(&u, names, &in).kind == UA_FORWARD);

    // Actions; keys after the first action wait for the next frame.
    ui_set(&u.list, 5, 3);
    in = keys(2, UK_ENTER, UK_DOWN);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_OPEN && a.index == 3 && u.list.sel == 3);
    in = keys(2, UK_DOWN, UK_ENTER);
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_OPEN && a.index == 4);
    in = keys(1, UK_BACKSPACE);
    CHECK(ui_handle(&u, names, &in).kind == UA_PARENT);
    in = chars("t");
    CHECK(ui_handle(&u, names, &in).kind == UA_TERMINAL);
    in = chars("~");
    CHECK(ui_handle(&u, names, &in).kind == UA_HOMEDIR);
    in = keys(1, UK_NONE);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);

    // An empty folder: nothing to open, Enter does nothing, moves are safe.
    ui_set(&u.list, 0, 5);
    CHECK(u.list.sel == 0 && u.list.top == 0);
    in = keys(1, UK_DOWN);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.list.sel == 0);
    in = keys(3, UK_DOWN, UK_END, UK_ENTER);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.list.sel == 0);
    in = keys(1, UK_BACKSPACE);
    CHECK(ui_handle(&u, names, &in).kind == UA_PARENT);

    // A reload keeps the scroll where it can; a shorter list pulls it back.
    ui_resize(&u.list, 10);
    ui_set(&u.list, 100, 50);
    CHECK(u.list.top == 41);
    ui_set(&u.list, 100, 45);
    CHECK(u.list.top == 41);
    ui_set(&u.list, 20, 15);
    CHECK(u.list.top == 10 && u.list.sel == 15);
    ui_set(&u.list, 20, 200);
    CHECK(u.list.sel == 19);
    ui_set(&u.list, 20, -3);
    CHECK(u.list.sel == 0 && u.list.top == 0);
    // A resize keeps the cursor in view.
    ui_set(&u.list, 100, 50);
    ui_resize(&u.list, 5);
    CHECK(u.list.sel == 50 && u.list.top <= 50 && u.list.top + 5 > 50);
    ui_resize(&u.list, 200);
    CHECK(u.list.top == 0);
    // No rows at all (a window too short): treated as one.
    ui_resize(&u.list, 0);
    CHECK(u.list.top == 50);
    in = keys(1, UK_PAGEDOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 51);
    ui_resize(&u.list, 2);
    ui_set(&u.list, 100, 10);
    in = keys(1, UK_PAGEDOWN);   // a page of 2 rows is 1
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 11);
}


static void test_find(void)
{
    Ui u = { 0 };
    UiNames names = { name_at, NULL, NULL };
    ui_resize(&u.list, 10);
    ui_set(&u.list, COUNT(find_names), 0);
    // "/" opens, letters jump to the first match from where it opened, ignoring case.
    UiInput in = chars("/bet");
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.finding && u.list.sel == 1 && !u.find_miss);
    in = chars("am");   // "betam": only BETAMAX
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 4);
    in = chars("z");    // no match: the cursor goes back to where find began, marked
    ui_handle(&u, names, &in);
    CHECK(u.find_miss && u.list.sel == 0);
    in = keys(1, UK_BACKSPACE);
    ui_handle(&u, names, &in);
    CHECK(!u.find_miss && u.list.sel == 4 && u.find.len == 5);
    in = keys(1, UK_ENTER);   // keeps the cursor, gives the keys back (the bar stays), remembers the text
    ui_handle(&u, names, &in);
    CHECK(u.finding && u.focus != UI_FOCUS_FIND && u.list.sel == 4 && u.last_len == 5);
    // n wraps to the next match of "betam" (only BETAMAX: stays), N likewise.
    in = chars("n");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 4);
    // Esc puts the cursor back.
    in = chars("/gam");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 2);
    in = keys(1, UK_ESC);
    ui_handle(&u, names, &in);
    CHECK(!u.finding && u.list.sel == 4 && u.last_len == 3);
    // n and N with "gam": gamma (2) both ways, from 4.
    in = chars("N");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 2);
    // A text that two names hold: n goes on, N goes back, both wrap.
    ui_set(&u.list, COUNT(find_names), 0);
    in = chars("/ta");   // delta (3) and Beta (1), BETAMAX (4)
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1);
    in = keys(1, UK_ENTER);
    ui_handle(&u, names, &in);
    in = chars("n");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 3);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 4);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1);   // wrapped
    in = chars("N");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 4);   // wrapped back
    // Ctrl+F opens it too; a typed "/" and Ctrl+F inside it do nothing; UTF-8 text and its erase.
    in = keys(1, UK_FIND);
    ui_handle(&u, names, &in);
    CHECK(u.finding && u.find.len == 0 && u.find_from == 4);
    in = chars("/");
    ui_handle(&u, names, &in);
    CHECK(u.finding && u.find.len == 0);
    in = keys(1, UK_FIND);
    ui_handle(&u, names, &in);
    CHECK(u.finding);
    in = ui_input_none();
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 0xE9 };   // é
    ui_handle(&u, names, &in);
    CHECK(u.find.len == 2 && u.list.sel == 6);
    in = keys(1, UK_BACKSPACE);
    ui_handle(&u, names, &in);
    CHECK(u.find.len == 0 && u.list.sel == 4 && !u.find_miss);   // empty: back where it began, no miss
    in = keys(1, UK_BACKSPACE);                  // empty: nothing to erase
    ui_handle(&u, names, &in);
    CHECK(u.find.len == 0 && u.finding);
    // Control characters are not text; a 4-byte one is.
    in = ui_input_none();
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 0x7F };
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 0x1F };
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 0x1F600 };
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 0x110000 };
    ui_handle(&u, names, &in);
    CHECK(u.find.len == 4 && memcmp(u.find.b, "\xF0\x9F\x98\x80", 4) == 0);
    in = ui_input_none();
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 0x20AC };   // €, 3 bytes
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 0x3B1 };    // α, 2 bytes
    ui_handle(&u, names, &in);
    CHECK(u.find.len == 9 && memcmp(u.find.b + 4, "\xE2\x82\xAC\xCE\xB1", 5) == 0);
    // A command gives the keys back and is the list's, keeping the text for n; the bar stays. (Home
    // edits the field since 2026-10-28: PgUp here.)
    isize was_sel = u.list.sel;
    in = keys(1, UK_PAGEUP);
    ui_handle(&u, names, &in);
    CHECK(u.finding && u.focus != UI_FOCUS_FIND && (u.list.sel < was_sel || was_sel == 0) && u.last_len == 9);
    // A full line drops what doesn't fit.
    ui_find_open(&u);
    for (int k = 0; k < UI_FIND_MAX + 10; k++) in = chars("x"), ui_handle(&u, names, &in);
    CHECK(u.find.len == UI_FIND_MAX);
    in = ui_input_none(), in.ev[in.nev++] = (UiEvent){ UK_NONE, 0xE9 };
    ui_handle(&u, names, &in);
    CHECK(u.find.len == UI_FIND_MAX);
    ui_find_close(&u);
    // Nothing to search: no match, no crash.
    UiMatcher fm = { 0 };
    ui_set(&u.list, 0, 0);
    ui_matcher_set(&fm, S("a"), (UiMatchOpts){ 0 });
    CHECK(ui_find_at(&u.list, names, &fm, 0, 1) == -1);
    ui_set(&u.list, 3, 0);
    ui_matcher_set(&fm, STR_EMPTY, (UiMatchOpts){ 0 });
    CHECK(ui_find_at(&u.list, names, &fm, 0, 1) == -1);
}

static void test_sort_keys(void)
{
    Ui u = { 0 };
    UiNames names = { name_at, NULL, NULL };
    ui_set(&u.list, 5, 0);
    UiInput in = chars("s");
    CHECK(ui_handle(&u, names, &in).kind == UA_SORT && u.sort.key == SORT_SIZE && !u.sort.reverse);
    in = chars("S");
    CHECK(ui_handle(&u, names, &in).kind == UA_SORT && u.sort.key == SORT_SIZE && u.sort.reverse);
    in = chars("s");   // the next column, not reversed
    ui_handle(&u, names, &in);
    CHECK(u.sort.key == SORT_MTIME && !u.sort.reverse);
    ui_handle(&u, names, &in);
    CHECK(u.sort.key == SORT_NAME);
    // Header clicks: a new column, then the same again reverses, then back.
    in = ui_input_none();
    in.header = SORT_MTIME;
    CHECK(ui_handle(&u, names, &in).kind == UA_SORT && u.sort.key == SORT_MTIME && !u.sort.reverse);
    ui_handle(&u, names, &in);
    CHECK(u.sort.reverse);
    ui_handle(&u, names, &in);
    CHECK(!u.sort.reverse);
    in.header = SORT_NAME;
    ui_handle(&u, names, &in);
    CHECK(u.sort.key == SORT_NAME && !u.sort.reverse);
    in.header = SORT_KEYS;   // not a column
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    // Hidden files: "." and Ctrl+H.
    in = chars(".");
    CHECK(ui_handle(&u, names, &in).kind == UA_HIDDEN);
    in = keys(1, UK_HIDDEN);
    CHECK(ui_handle(&u, names, &in).kind == UA_HIDDEN);
    in = keys(1, UK_BACK);
    CHECK(ui_handle(&u, names, &in).kind == UA_BACK);
    in = keys(1, UK_FORWARD);
    CHECK(ui_handle(&u, names, &in).kind == UA_FORWARD);
    // h, l, Left and Right neither go up nor open (Michael, 2026-10-24): Enter and Backspace do.
    in = chars("hl");
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    in = keys(2, UK_RIGHT, UK_LEFT);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    in = keys(1, UK_BACKSPACE);
    CHECK(ui_handle(&u, names, &in).kind == UA_PARENT);
    in = chars("xjq");   // unknown characters are skipped
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1);
}

static void test_side(void)
{
    Ui u = { 0 };
    UiNames names = { name_at, NULL, NULL };
    ui_resize(&u.list, 10), ui_resize(&u.side, 4);
    ui_set(&u.list, 20, 3), ui_set(&u.side, 6, 0);
    // Tab gives the sidebar the keys; moves go to it; Enter goes to the place.
    UiInput in = keys(1, UK_TAB);
    ui_handle(&u, names, &in);
    CHECK((u.focus == UI_FOCUS_SIDE));
    in = chars("jj");
    ui_handle(&u, names, &in);
    CHECK(u.side.sel == 2 && u.list.sel == 3);
    in = keys(1, UK_END);
    ui_handle(&u, names, &in);
    CHECK(u.side.sel == 5 && u.side.top == 2);
    in = keys(1, UK_PAGEUP);   // a page of 4 rows is 3
    ui_handle(&u, names, &in);
    CHECK(u.side.sel == 2);
    in = keys(1, UK_ENTER);
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_PLACE && a.index == 2);
    in = chars("l");   // Right and l don't go to a place: Enter does
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && (u.focus == UI_FOCUS_SIDE));
    // Tab and Esc give the keys back.
    in = keys(1, UK_TAB);
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_SIDE);
    in = keys(2, UK_TAB, UK_ESC);
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_SIDE);
    // Tab puts the cursor on the folder shown's row (gui/'s side_here), else on the first row
    // (Michael, 2026-10-29), whatever it was on before.
    u.side_here = 5;
    in = keys(1, UK_TAB);
    ui_handle(&u, names, &in);
    CHECK(u.focus == UI_FOCUS_SIDE && u.side.sel == 5 && u.side.top == 2);
    u.side_here = -1;
    in = keys(2, UK_TAB, UK_TAB);
    ui_handle(&u, names, &in);
    CHECK(u.focus == UI_FOCUS_SIDE && u.side.sel == 0 && u.side.top == 0);
    u.side_here = 6;   // past the end: the first row
    in = keys(2, UK_TAB, UK_TAB);
    ui_handle(&u, names, &in);
    CHECK(u.side.sel == 0);
    u.side_here = 2;
    in = keys(3, UK_TAB, UK_TAB, UK_END);
    ui_handle(&u, names, &in);
    CHECK(u.side.sel == 5 && u.side.top == 2);
    in = keys(1, UK_ESC);
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_SIDE);
    // A click in the sidebar goes there; a click in the list takes the keys back.
    in = ui_input_none();
    in.side_click = 1;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_PLACE && a.index == 3 && u.side.sel == 3);   // top is 2
    in.side_click = 4;   // past the end
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    u.focus = UI_FOCUS_SIDE;
    in = ui_input_none();
    in.click = 0;
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_SIDE);
    u.focus = UI_FOCUS_SIDE;
    in = ui_input_none();
    in.dclick = 1;
    CHECK(ui_handle(&u, names, &in).kind == UA_OPEN && u.focus != UI_FOCUS_SIDE);
    // Find takes the keys from the sidebar.
    u.focus = UI_FOCUS_SIDE;
    in = chars("/");
    ui_handle(&u, names, &in);
    CHECK(u.finding && u.focus != UI_FOCUS_SIDE);
    // No places: Tab keeps the keys in the list.
    ui_find_close(&u);
    ui_set(&u.side, 0, 0);
    in = keys(1, UK_TAB);
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_SIDE);
    u.focus = UI_FOCUS_SIDE;   // and Enter in an empty sidebar does nothing
    in = keys(1, UK_ENTER);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
}

// The sidebar's headings (F6): the cursor steps over them, a click on one does nothing.
static void test_side_heads(void)
{
    static Ui u;
    u = (Ui){ 0 };
    UiNames names = { name_at, NULL, NULL };
    // 0 Places, 1 Home, 2 Trash, 3 Drives, 4 /, 5 sd
    static const u8 HEAD[] = { 1, 0, 0, 1, 0, 0 };
    u.side_head = HEAD;
    ui_resize(&u.list, 10), ui_resize(&u.side, 3);
    ui_set(&u.list, 5, 0), ui_set(&u.side, 6, 0);
    UiInput in = keys(1, UK_TAB);   // the keys to the sidebar: off the first heading
    ui_handle(&u, names, &in);
    CHECK((u.focus == UI_FOCUS_SIDE) && u.side.sel == 1 && u.side.top == 0);
    in = keys(2, UK_DOWN, UK_DOWN);   // over Drives to /
    ui_handle(&u, names, &in);
    CHECK(u.side.sel == 4 && u.side.top == 2);
    in = keys(1, UK_UP);   // back over it
    ui_handle(&u, names, &in);
    CHECK(u.side.sel == 2);
    in = keys(1, UK_HOME);   // to the top: Home, with its heading in view
    ui_handle(&u, names, &in);
    CHECK(u.side.sel == 1 && u.side.top == 0);
    in = keys(1, UK_END);
    ui_handle(&u, names, &in);
    CHECK(u.side.sel == 5);
    in = keys(1, UK_UP);   // / with Drives above it in view
    ui_handle(&u, names, &in);
    CHECK(u.side.sel == 4 && u.side.top == 3);
    // Scrolling up onto the row under a heading brings the heading into view too.
    static const u8 LONG[] = { 1, 0, 0, 1, 0, 0, 0, 0 };
    u.side_head = LONG;
    ui_set(&u.side, 8, 1);
    in = keys(1, UK_END);
    ui_handle(&u, names, &in);
    CHECK(u.side.sel == 7 && u.side.top == 5);
    in = keys(3, UK_UP, UK_UP, UK_UP);
    ui_handle(&u, names, &in);
    CHECK(u.side.sel == 4 && u.side.top == 3);
    u.side_head = HEAD;
    ui_set(&u.side, 6, 4);
    in = keys(1, UK_END);
    ui_handle(&u, names, &in);
    in = keys(1, UK_UP);
    ui_handle(&u, names, &in);
    // A click on a heading does nothing; on a row it goes there.
    in = ui_input_none();
    in.side_click = 0;   // top 3: Drives
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.side.sel == 4);
    in.side_click = 2;
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_PLACE && a.index == 5);
    in = ui_input_none();
    in.mid_side = 0;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE);
    in.mid_side = 1;
    CHECK(ui_handle(&u, names, &in).kind == UA_TAB_BG_PLACE);
    // Headings at the end: the cursor goes back up off them.
    static const u8 TAIL[] = { 0, 0, 1, 1 };
    u.side_head = TAIL;
    ui_set(&u.side, 4, 1);
    u.focus = UI_FOCUS_SIDE;
    in = keys(1, UK_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.side.sel == 1);
    // All headings: the cursor stays where it is.
    static const u8 ALL[] = { 1, 1 };
    u.side_head = ALL;
    ui_set(&u.side, 2, 0);
    in = keys(1, UK_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.side.sel == 1);
}

static void test_history(void)
{
    static UiHistory h;
    UiPlace to;
    CHECK(!hist_step(&h, -1, "/a", S("x"), &to) && !hist_step(&h, 1, "/a", S("x"), &to));
    hist_leave(&h, "/a", S("one"));
    hist_leave(&h, "/b", S("two"));
    // At /c: back to /b (two), then /a (one); forward to /b, then /c (three).
    CHECK(hist_step(&h, -1, "/c", S("three"), &to) && strcmp(to.dir, "/b") == 0 && strcmp(to.sel, "two") == 0);
    CHECK(hist_step(&h, -1, "/b", S("two"), &to) && strcmp(to.dir, "/a") == 0 && strcmp(to.sel, "one") == 0);
    CHECK(!hist_step(&h, -1, "/a", S("one"), &to));
    CHECK(hist_step(&h, 1, "/a", S("one"), &to) && strcmp(to.dir, "/b") == 0);
    CHECK(hist_step(&h, 1, "/b", S("two"), &to) && strcmp(to.dir, "/c") == 0 && strcmp(to.sel, "three") == 0);
    CHECK(!hist_step(&h, 1, "/c", S("three"), &to));
    // Leaving for a new folder clears forward.
    CHECK(hist_step(&h, -1, "/c", S("three"), &to));
    hist_leave(&h, "/b", S("two"));
    CHECK(h.nfwd == 0 && !hist_step(&h, 1, "/d", S(""), &to));
    // A step to a folder that is gone: unstepped, the dead entry stays dropped and the one before
    // it is next.
    h = (UiHistory){ 0 };
    hist_leave(&h, "/a", S("one"));
    hist_leave(&h, "/dead", S("x"));
    CHECK(hist_step(&h, -1, "/c", S("three"), &to) && strcmp(to.dir, "/dead") == 0 && h.nfwd == 1);
    hist_unstep(&h, -1);
    CHECK(h.nfwd == 0 && h.nback == 1);
    CHECK(hist_step(&h, -1, "/c", S("three"), &to) && strcmp(to.dir, "/a") == 0 && h.nfwd == 1);
    CHECK(hist_step(&h, 1, "/a", S("one"), &to) && strcmp(to.dir, "/c") == 0 && h.nback == 1);
    hist_unstep(&h, 1);
    CHECK(h.nback == 0 && h.nfwd == 0);
    hist_unstep(&h, 1);   // nothing to take back: nothing happens
    CHECK(h.nback == 0);
    // Full: the oldest goes.
    h = (UiHistory){ 0 };
    for (int k = 0; k < UI_HISTORY_MAX + 5; k++) {
        char d[16];
        snprintf(d, sizeof d, "/d%d", k);
        hist_leave(&h, d, S("s"));
    }
    CHECK(h.nback == UI_HISTORY_MAX && strcmp(h.back[0].dir, "/d5") == 0 && strcmp(h.back[UI_HISTORY_MAX - 1].dir, "/d68") == 0);
}

// A partition as /sys/class/block has it (its folder inside its disk's, a link to it beside), and
// its udev database file.
static void fake_part(const char *disk, const char *part, int minor, const char *removable, const char *sectors, const char *udev, bool held)
{
    mkdirs(at("blk/sys/%s/%s/holders", disk, part));
    put(at("blk/sys/%s/removable", disk), removable);
    char devno[16];
    snprintf(devno, sizeof devno, "8:%d\n", minor);
    put(at("blk/sys/%s/%s/dev", disk, part), devno);
    put(at("blk/sys/%s/%s/size", disk, part), sectors);
    if (held) put(at("blk/sys/%s/%s/holders/dm-0", disk, part), "");
    char link[PATH_MAX];
    snprintf(link, sizeof link, "%s/%s", disk, part);
    CHECK(symlink(link, at("blk/sys/%s", part)) == 0);
    put(at("blk/udev/b8:%d", minor), udev);
}

// Unmounted drives (2026-10-30): which are listed, by what name, after the mounted ones.
static void test_unmounted(Places *pl)
{
    const char *fs = "E:ID_FS_USAGE=filesystem\nE:ID_FS_TYPE=vfat\n";
    fake_part("sdb", "sdb1", 17, "1\n", "62500000\n", "E:ID_FS_USAGE=filesystem\nE:ID_FS_LABEL_ENC=MY\\x20STICK\nE:ID_FS_LABEL=MY_STICK\n", false);
    fake_part("sdc", "sdc1", 33, "0\n", "62500000\n", "E:ID_BUS=usb\nE:ID_FS_USAGE=filesystem\n", false);   // USB: removable
    fake_part("sdd", "sdd1", 49, "1\n", "62500000\n", "E:ID_FS_USAGE=crypto\nE:ID_FS_TYPE=crypto_LUKS\n", false);   // encrypted: later
    fake_part("sde", "sde1", 65, "1\n", "62500000\n", fs, false);   // mounted
    fake_part("sdf", "sdf1", 81, "1\n", "62500000\n", fs, true);    // a device mapper on it
    fake_part("sdg", "sdg1", 97, "1\n", "62500000\n", "E:ID_FS_USAGE=filesystem\nE:UDISKS_IGNORE=1\n", false);
    fake_part("sdh", "sdh1", 113, "1\n", "0\n", fs, false);          // an empty card slot
    fake_part("sdi", "sdi1", 129, "1\n", "2000000\n", fs, false);    // 1 GB, no label
    fake_part("nvme", "nvme1", 145, "0\n", "62500000\n", fs, false); // internal
    // A whole disk with a file system on it (no partition table): its own removable.
    mkdirs(at("blk/sys/sdw/holders"));
    put(at("blk/sys/sdw/removable"), "1\n"), put(at("blk/sys/sdw/dev"), "8:160\n"), put(at("blk/sys/sdw/size"), "2000000\n");
    put(at("blk/udev/b8:160"), "E:ID_FS_USAGE=filesystem\nE:ID_FS_LABEL_ENC=WHOLE\n");
    put(at("blk/mountinfo"), "1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 8:65 / /boot rw - vfat /dev/sde1 rw\n");
    // Hundreds of loop devices (and ram, zram, dm, md, nbd) never crowd the drives out
    // (reviewer-2, 2026-10-30: 300 loops hid all four).
    for (int k = 0; k < 300; k++) mkdirs(at("blk/sys/loop%d", k));
    mkdirs(at("blk/sys/ram0")), mkdirs(at("blk/sys/zram0")), mkdirs(at("blk/sys/dm-0")), mkdirs(at("blk/sys/md0")), mkdirs(at("blk/sys/nbd0"));
    env_set("HOME", "/home/m");
    places_load(&ta, pl, at("places/none"), at("blk/mountinfo"), at("blk/sys"), at("blk/udev"));
    const char *want[][3] = { { "/dev/sdb1", "MY STICK" }, { "/dev/sdc1", "32 GB Drive" }, { "/dev/sdi1", "1.0 GB Drive" }, { "/dev/sdw", "WHOLE" } };
    CHECK(pl->n == 5 + COUNT(want) && pl->p[4].kind == PL_ROOT);
    for (int i = 0; i < COUNT(want) && 5 + i < pl->n; i++) {
        const Place *p = &pl->p[5 + i];
        if (strcmp(p->path, want[i][0]) != 0 || strcmp(p->label, want[i][1]) != 0) fprintf(stderr, "unmounted %d: %s (%s)\n", i, p->path, p->label);
        CHECK(p->kind == PL_UNMOUNTED && strcmp(p->path, want[i][0]) == 0 && strcmp(p->dev, want[i][0]) == 0 && strcmp(p->label, want[i][1]) == 0);
    }
    CHECK(pl->p[5].size == 62500000LL * 512 && !place_is_drive(PL_UNMOUNTED) && !place_ejectable(&pl->p[5]));
    CHECK(places_current(pl, "/dev/sdb1") == 4);   // File System, never the unmounted drive (not a folder)
    // udev's file: a key whole (not ID_FS_TYPE_X for ID_FS_TYPE); \\xNN only, and only what fits.
    CHECK(str_eq(udev_get(S("E:ID_FS_TYPE_X=no\nE:ID_FS_TYPE=vfat\n"), "ID_FS_TYPE"), S("vfat")) && !udev_get(S("E:ID_BUS\n"), "ID_BUS").len);
    char lb[8];
    CHECK(udev_unescape(S("a\\x41\\y41"), lb, sizeof lb) && strcmp(lb, "aA\\y41") == 0);
    CHECK(!udev_unescape(S("12345678"), lb, sizeof lb) && udev_unescape(S("1234567"), lb, sizeof lb));
    // The real /sys (read only): its files say 4096 bytes and hold a few, and every block device
    // there has a dev "major:minor" (2026-10-30: read_file gave nothing for them).
    DIR *sd = opendir("/sys/class/block");
    struct dirent *se;
    char sname[256] = "";
    while (sd && !sname[0] && (se = readdir(sd)))
        if (se->d_name[0] != '.') snprintf(sname, sizeof sname, "%s", se->d_name);
    if (sd) closedir(sd);
    if (sname[0]) {
        Str sdev = sys_line(&ta, "/sys/class/block", sname, "dev");
        CHECK(sdev.len >= 3 && str_find(sdev, ':') > 0);
    }
    // No folders given (the guard's way in a test without its own): none.
    places_load(&ta, pl, at("places/none"), at("blk/mountinfo"), NULL, at("blk/udev"));
    CHECK(pl->n == 5);
    env_restore();
}

static void test_places(void)
{
    arena_reset(&ta);
    put(at("places/bookmarks"),
        "file:///home/m/Pictures Pictures\n"
        "file:///home/m/My%20Docs\n"           // no label: its name, decoded
        "file:///home/m/Downloads/ Downloads\n"  // a trailing slash
        "file:/// /\n"
        "sftp://host/x Remote\n"                // not a file: skipped
        "trash:///x Trash\n"                    // nor this, though its 8th byte on is a path
        "file:///bad%zz Bad\n"                  // a broken escape: skipped
        "file:///short%2 Short\n"               // cut off: skipped
        "file:///home/m/Pictures Again\n"       // a duplicate: skipped
        "file://relative Rel\n");               // not absolute: skipped
    put(at("places/mountinfo"),
        "26 33 0:24 / /proc rw - proc proc rw\n"
        "33 1 0:31 /@ / rw shared:1 - btrfs /dev/mapper/root rw\n"
        "34 33 0:31 /@home /home rw - btrfs /dev/mapper/root rw\n"
        "40 33 254:2 / /mnt/sd rw - ext4 /dev/mapper/sd rw\n"
        "41 33 8:17 / /run/media/m/USB\\040Stick rw - vfat /dev/sdb1 rw\n"
        "42 33 8:18 / /media/cd rw - iso9660 /dev/sr0 ro\n"
        "46 33 8:21 / /media/nosrc rw - vfat\n"               // no source field
        "47 33 8:22 / /media/esc rw - vfat /dev/a\\040b rw\n"  // a space in it
        "43 33 8:19 / /mnt rw - ext4 /dev/sdc rw\n"           // /mnt itself: not under it
        "44 33 8:20 / /mntx/a rw - ext4 /dev/sdd rw\n"        // only looks like /mnt
        "45 short\n");
    env_set("HOME", "/home/m");
    Places pl;
    places_load(&ta, &pl, at("places/bookmarks"), at("places/mountinfo"), NULL, NULL);
    const char *want[][2] = {
        { "", "Places" }, { "/home/m", "Home" }, { "/home/m/Pictures", "Pictures" }, { "/home/m/My Docs", "My Docs" },
        { "/home/m/Downloads", "Downloads" }, { "/", "/" }, { TRASH_URI, "Trash" }, { "", "Drives" }, { "/", "File System" },
        { "/mnt/sd", "sd" }, { "/run/media/m/USB Stick", "USB Stick" }, { "/media/cd", "cd" }, { "/media/nosrc", "nosrc" },
        { "/media/esc", "esc" },
    };
    CHECK(pl.n == COUNT(want));
    for (int i = 0; i < pl.n && i < COUNT(want); i++) {
        if (strcmp(pl.p[i].path, want[i][0]) != 0 || strcmp(pl.p[i].label, want[i][1]) != 0)
            fprintf(stderr, "place %d: %s (%s)\n", i, pl.p[i].path, pl.p[i].label);
        CHECK(strcmp(pl.p[i].path, want[i][0]) == 0 && strcmp(pl.p[i].label, want[i][1]) == 0);
    }
    CHECK(pl.p[0].kind == PL_HEAD && pl.p[1].kind == PL_HOME && pl.p[2].kind == PL_BOOKMARK && pl.p[5].kind == PL_BOOKMARK);
    CHECK(pl.p[6].kind == PL_TRASH && pl.p[7].kind == PL_HEAD && pl.p[8].kind == PL_ROOT);   // "/" as a bookmark and as a drive
    CHECK(pl.p[9].kind == PL_MOUNT && pl.p[10].kind == PL_REMOVABLE && pl.p[11].kind == PL_REMOVABLE);
    // Each drive's source (F6.1, for eject): after the " - " that ends the optional fields.
    CHECK(strcmp(pl.p[8].dev, "/dev/mapper/root") == 0 && strcmp(pl.p[9].dev, "/dev/mapper/sd") == 0);
    CHECK(strcmp(pl.p[10].dev, "/dev/sdb1") == 0 && strcmp(pl.p[11].dev, "/dev/sr0") == 0 && pl.p[5].dev[0] == 0);
    CHECK(pl.p[12].dev[0] == 0 && strcmp(pl.p[13].dev, "/dev/a b") == 0);
    CHECK(place_ejectable(&pl.p[10]) && place_ejectable(&pl.p[11]) && !place_ejectable(&pl.p[12]));
    CHECK(!place_ejectable(&pl.p[9]) && !place_ejectable(&pl.p[8]));   // /mnt/sd and /: not removable
    Place fuse = { .kind = PL_REMOVABLE };
    snprintf(fuse.dev, sizeof fuse.dev, "sshfs#host:/x");
    CHECK(!place_ejectable(&fuse));
    CHECK(places_current(&pl, "/home/m/Pictures/2026") == 2);
    CHECK(places_current(&pl, "/home/m/Picturesque") == 1);   // a prefix that isn't a folder above
    CHECK(places_current(&pl, "/home/m") == 1);
    CHECK(places_current(&pl, "/etc") == 5);                  // the bookmark before the drive
    CHECK(places_current(&pl, "/mnt/sd/Downloads") == 9);
    CHECK(places_current(&pl, TRASH_URI) == 6);
    // The tests' media folder: mounts under it removable, the folder itself not.
    snprintf(places_test_media, sizeof places_test_media, "/scratch/media");
    put(at("places/mi2"), "1 1 0:1 / /scratch/media/STICK rw - vfat /dev/sdq1 rw\n2 1 0:1 / /scratch/media rw - tmpfs t rw\n"
                          "3 1 0:1 / /scratch/other rw - vfat /dev/sdq2 rw\n");
    places_load(&ta, &pl, at("places/none"), at("places/mi2"), NULL, NULL);
    places_test_media[0] = 0;
    CHECK(pl.n == 5 && pl.p[4].kind == PL_REMOVABLE && strcmp(pl.p[4].path, "/scratch/media/STICK") == 0 && place_ejectable(&pl.p[4]));
    env_restore();
    // The real mountinfo, through procfs: "/" is in it.
    env_set("HOME", "/home/m");
    places_load(&ta, &pl, at("places/none"), "/proc/self/mountinfo", NULL, NULL);
    bool root_seen = false;
    for (int i = 0; i < pl.n; i++) root_seen |= pl.p[i].kind == PL_ROOT;
    CHECK(root_seen);
    env_restore();
    // Without bookmarks: home, Trash, then "/" from mountinfo as File System.
    env_set("HOME", "/home/m");
    places_load(&ta, &pl, at("places/none"), at("places/mountinfo"), NULL, NULL);
    CHECK(pl.n == 10 && pl.p[2].kind == PL_TRASH && pl.p[4].kind == PL_ROOT && strcmp(pl.p[4].label, "File System") == 0);
    env_restore();
    // No HOME: no Home place, the headings and Trash.
    env_set("HOME", NULL);
    places_load(&ta, &pl, at("places/none"), at("places/none"), NULL, NULL);
    CHECK(pl.n == 3 && pl.p[1].kind == PL_TRASH && places_current(&pl, "/x") == -1);
    env_restore();
    // A bookmark to Home: one place, Home (places_current's tie is only ever a drive and a place).
    env_set("HOME", "/home/m");
    put(at("places/bm2"), "file:///home/m Me\n");
    places_load(&ta, &pl, at("places/bm2"), at("places/none"), NULL, NULL);
    CHECK(pl.n == 4 && pl.p[1].kind == PL_HOME && places_current(&pl, "/home/m/x") == 1);
    env_restore();
    test_unmounted(&pl);
    // Icons.
    UserDirs ud = { 0 };
    snprintf(ud.d[0].path, sizeof ud.d[0].path, "/home/m/Pictures");
    ud.d[0].icon = S("folder-pictures");
    ud.n = 1;
    Str names[4];
    Place p = { .kind = PL_BOOKMARK };
    snprintf(p.path, sizeof p.path, "/home/m/Pictures");
    CHECK(place_icon_names(&p, &ud, false, names) == 2 && str_eq(names[0], S("folder-pictures")) && str_eq(names[1], S("folder")));
    p.kind = PL_REMOVABLE;
    CHECK(place_icon_names(&p, &ud, false, names) == 3 && str_eq(names[0], S("drive-removable-media")) && str_eq(names[1], S("drive-harddisk")));
    p.kind = PL_ROOT;
    CHECK(place_icon_names(&p, &ud, false, names) == 2 && str_eq(names[0], S("drive-harddisk")));
    p.kind = PL_MOUNT;
    CHECK(place_icon_names(&p, &ud, false, names) == 2 && str_eq(names[0], S("drive-harddisk")));
    p.kind = PL_TRASH;
    CHECK(place_icon_names(&p, &ud, false, names) == 2 && str_eq(names[0], S("user-trash")));
    CHECK(place_icon_names(&p, &ud, true, names) == 3 && str_eq(names[0], S("user-trash-full")) && str_eq(names[1], S("user-trash")));
    p.kind = PL_MOUNT;   // full is the Trash place's alone
    CHECK(place_icon_names(&p, &ud, true, names) == 2 && str_eq(names[0], S("drive-harddisk")));
    p.kind = PL_TRASH;
    snprintf(p.path, sizeof p.path, "/elsewhere");
    p.kind = PL_BOOKMARK;
    CHECK(place_icon_names(&p, &ud, false, names) == 1 && str_eq(names[0], S("folder")));
    // The decoders.
    char buf[8];
    CHECK(percent_decode(S("a%41b"), buf, sizeof buf) && strcmp(buf, "aAb") == 0);
    CHECK(!percent_decode(S("abcdefgh"), buf, sizeof buf));   // too long for 8 with its 0
    CHECK(percent_decode(S("abcdefg"), buf, sizeof buf));
    CHECK(!percent_decode(S("a%00b"), buf, sizeof buf));   // a NUL would end the path early (2026-10-28)
    CHECK(file_uri_path(S("file://localhost/a%20b"), buf, sizeof buf) && strcmp(buf, "/a b") == 0);
    CHECK(!file_uri_path(S("file://host/a"), buf, sizeof buf) && !file_uri_path(S("file:a"), buf, sizeof buf));
    CHECK(octal_decode(S("a\\040b\\134"), buf, sizeof buf) && strcmp(buf, "a b\\") == 0);
    CHECK(octal_decode(S("a\\09"), buf, sizeof buf) && strcmp(buf, "a\\09") == 0);   // not octal: kept
    CHECK(octal_decode(S("\\04"), buf, sizeof buf) && strcmp(buf, "\\04") == 0);      // cut off: kept
    CHECK(octal_decode(S("\\477"), buf, sizeof buf) && strcmp(buf, "\\477") == 0);    // past \377: kept
    CHECK(!octal_decode(S("abcdefgh"), buf, sizeof buf));
    char bp[256];
    env_set("XDG_CONFIG_HOME", "/cfg");
    CHECK(strcmp(bookmarks_path(bp, sizeof bp), "/cfg/gtk-3.0/bookmarks") == 0);
    env_restore();
    env_set("XDG_CONFIG_HOME", NULL);
    env_set("HOME", "/h");
    CHECK(strcmp(bookmarks_path(bp, sizeof bp), "/h/.config/gtk-3.0/bookmarks") == 0);
    env_restore();
}

static void test_sort(void)
{
    arena_reset(&ta);
    put(at("sort/b.txt"), "12345");
    put(at("sort/A.txt"), "1");
    put(at("sort/c.txt"), "123");
    put(at("sort/d.txt"), "123");
    put(at("sort/.dot"), "12");
    mkdirs(at("sort/zdir"));
    mkdirs(at("sort/adir"));
    struct { const char *f; long t; } times[] = { { "b.txt", 300 }, { "A.txt", 100 }, { "c.txt", 200 }, { "d.txt", 200 }, { "zdir", 50 }, { "adir", 60 }, { ".dot", 400 } };
    for (int i = 0; i < COUNT(times); i++) {
        struct timespec ts[2] = { { 1700000000 + times[i].t, 0 }, { 1700000000 + times[i].t, 0 } };
        utimensat(AT_FDCWD, at("sort/%s", times[i].f), ts, 0);
    }
    struct { Sort s; bool hidden; const char *want; } cases[] = {
        { { SORT_NAME, false }, false, "adir zdir A.txt b.txt c.txt d.txt" },
        { { SORT_NAME, true }, false, "zdir adir d.txt c.txt b.txt A.txt" },
        { { SORT_SIZE, false }, false, "adir zdir A.txt c.txt d.txt b.txt" },   // c, d tie on size: by name
        { { SORT_SIZE, true }, false, "zdir adir b.txt d.txt c.txt A.txt" },
        { { SORT_MTIME, false }, false, "zdir adir A.txt c.txt d.txt b.txt" },
        { { SORT_MTIME, true }, false, "adir zdir b.txt d.txt c.txt A.txt" },
        { { SORT_NAME, false }, true, "adir zdir .dot A.txt b.txt c.txt d.txt" },
    };
    for (int i = 0; i < COUNT(cases); i++) {
        Listing l;
        Err err;
        CHECK(fs_list(&ta, at("sort"), cases[i].hidden, cases[i].s, &l, &err));
        char got[256] = "";
        for (isize k = 0; k < l.n; k++) snprintf(got + strlen(got), sizeof got - strlen(got), "%s%.*s", k ? " " : "", STR_ARG(l.e[k].name));
        if (strcmp(got, cases[i].want) != 0) fprintf(stderr, "sort case %d: %s\n", i, got);
        CHECK(strcmp(got, cases[i].want) == 0);
        CHECK(l.hidden == 1);
    }
}

static void test_timing(void)
{
    UiPress p = { -1, 0 };
    CHECK(ui_press(&p, 3, 1000) == 1);
    CHECK(ui_press(&p, 3, 1400) == 2);          // within 400 ms, inclusive
    CHECK(ui_press(&p, 3, 1500) == 1);          // starts over after a double
    p = (UiPress){ -1, 0 };
    CHECK(ui_press(&p, 3, 1000) == 1 && ui_press(&p, 3, 1100) == 2 && ui_press(&p, 3, 1200) == 1);   // a triple is a double, then a press
    CHECK(ui_press(&p, 3, 1901) == 1);          // 401 ms: too slow
    CHECK(ui_press(&p, 4, 1950) == 1);          // another row
    CHECK(ui_press(&p, 4, 2000) == 2);
    CHECK(ui_press(&p, -1, 2100) == 1 && ui_press(&p, -1, 2150) == 1);   // off the rows: never double
    CHECK(ui_batch_due(1000, 1000, 100, 1000) == 1100);
    CHECK(ui_batch_due(1000, 1950, 100, 1000) == 2000);   // capped a second after the first
    CHECK(ui_batch_due(1000, 1899, 100, 1000) == 1999);
}

static float adv_fixed(u32 cp)
{
    return cp == 0x2026 ? 1 : cp >= 0x80 ? 2 : 1;   // wide for anything past ASCII
}

static void test_fit(void)
{
    isize f, t;
    CHECK(ui_fit(S("abcdef"), 6, 1, adv_fixed, false, &f, &t) && f == 0 && t == 6);
    CHECK(!ui_fit(S("abcdefg"), 6, 1, adv_fixed, false, &f, &t) && f == 0 && t == 5);   // "abcde…"
    CHECK(!ui_fit(S("abcdefg"), 6, 1, adv_fixed, true, &f, &t) && f == 2 && t == 7);    // "…cdefg"
    CHECK(ui_width(S("a\xC3\xA9" "b"), adv_fixed) == 4);
    // Code points are never split: é is two bytes and two wide.
    CHECK(!ui_fit(S("ab\xC3\xA9" "cd"), 4, 1, adv_fixed, false, &f, &t) && t == 2);       // "ab…": é doesn't fit in 1
    CHECK(!ui_fit(S("ab\xC3\xA9" "cd"), 5, 1, adv_fixed, false, &f, &t) && t == 4);       // "abé…"
    CHECK(!ui_fit(S("ab\xC3\xA9" "cd"), 4, 1, adv_fixed, true, &f, &t) && f == 4);        // "…cd"
    CHECK(!ui_fit(S("ab\xC3\xA9" "cd"), 5, 1, adv_fixed, true, &f, &t) && f == 2);        // "…écd"
    // No room at all: nothing kept.
    CHECK(!ui_fit(S("abc"), 1, 1, adv_fixed, false, &f, &t) && t == 0);
    CHECK(!ui_fit(S("abc"), 1, 1, adv_fixed, true, &f, &t) && f == 3);
    CHECK(!ui_fit(S("abc"), 0, 1, adv_fixed, true, &f, &t) && f == 3);
    CHECK(ui_fit(STR_EMPTY, 0, 1, adv_fixed, false, &f, &t) && t == 0);
}

static void test_format(void)
{
    struct { i64 size; const char *want; } sizes[] = {
        { 0, "0 B" },          { 1, "1 B" },           { 1023, "1023 B" },        { 1024, "1.0 KiB" },
        { 1536, "1.5 KiB" },   { 10188, "9.9 KiB" },   { 10189, "10 KiB" },       { 10240, "10 KiB" },
        { 1048063, "1023 KiB" },                       { 1048064, "1.0 MiB" },    { 1048576, "1.0 MiB" },
        { 5LL << 30, "5.0 GiB" }, { 3LL << 40, "3.0 TiB" }, { INT64_MAX, "8.0 EiB" },
    };
    char buf[64];
    for (int i = 0; i < COUNT(sizes); i++) {
        ui_size(buf, sizeof buf, sizes[i].size);
        if (strcmp(buf, sizes[i].want) != 0) fprintf(stderr, "ui_size(%lld) = %s\n", (long long)sizes[i].size, buf);
        CHECK(strcmp(buf, sizes[i].want) == 0);
    }
    env_set("TZ", "UTC");
    tzset();
    i64 now = 1792483200;   // 2026-10-20 08:00 UTC
    ui_when(buf, sizeof buf, now - 3600, now, DATE_SHORT, "");
    CHECK(strcmp(buf, "07:00") == 0);
    ui_when(buf, sizeof buf, now - 9 * 3600, now, DATE_SHORT, "");   // yesterday 23:00
    CHECK(strcmp(buf, "Oct 19") == 0);
    ui_when(buf, sizeof buf, 1767225600, now, DATE_SHORT, "");       // 2026-01-01
    CHECK(strcmp(buf, "Jan  1") == 0);
    ui_when(buf, sizeof buf, now - 365 * 86400 - 3600, now, DATE_SHORT, "");   // the same day a year ago
    CHECK(strcmp(buf, "2025-10-20") == 0);
    ui_when(buf, sizeof buf, 1767225599, now, DATE_SHORT, "");       // 2025-12-31 23:59:59
    CHECK(strcmp(buf, "2025-12-31") == 0);
    // ofx.conf's other date formats (F10): the same whatever the day.
    ui_when(buf, sizeof buf, now - 3600, now, DATE_LONG, "");
    CHECK(strcmp(buf, "2026-10-20 07:00") == 0);
    ui_when(buf, sizeof buf, 1767225599, now, DATE_ISO, "");
    CHECK(strcmp(buf, "2025-12-31T23:59:59") == 0);
    ui_when(buf, sizeof buf, now - 3600, now, DATE_PATTERN, "%d.%m.%y");
    CHECK(strcmp(buf, "20.10.26") == 0);
    char tiny[4];
    ui_when(tiny, sizeof tiny, now, now, DATE_LONG, "");   // doesn't fit: ?
    CHECK(strcmp(tiny, "?") == 0);
    env_restore();
    tzset();
}

#include "test_f3.c"
#include "test_f4.c"
#include "test_f5.c"
#include "test_f52.c"
#include "test_f53.c"
#include "test_f6.c"
#include "test_f62.c"
#include "test_f63.c"
#include "test_f64.c"
#include "test_f65.c"
#include "test_f7.c"
#include "test_f72.c"
#include "test_f73.c"
#include "test_f74.c"
#include "test_f75.c"
#include "test_f8.c"
#include "test_f9.c"
#include "test_f10.c"
#include "test_fm1.c"

int main(void)
{
    arena_init(&ta, "test", test_mem, sizeof test_mem);
    const char *tmp = getenv("TMPDIR");
    snprintf(root, sizeof root, "%s/ofx-test-XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
    if (!mkdtemp(root)) return perror("mkdtemp"), 1;
    snprintf(decoy, sizeof decoy, "%s-decoy", root);
    if (mkdir(decoy, 0700) != 0) return perror("mkdir decoy"), 1;
    test_natural();
    test_list();
    test_mime();
    test_icons();
    test_theme();
    test_user_dirs();
    test_spawn();
    test_ui();
    test_find();
    test_sort_keys();
    test_side();
    test_history();
    test_places();
    test_side_heads();
    test_sort();
    test_timing();
    test_fit();
    test_format();
    test_ops();
    test_trash();
    test_clip();
    test_job();
    test_undo();
    test_ui_f3();
    test_crumbs();
    test_location();
    test_counts();
    test_ui_f4();
    test_tabs();
    test_ui_f5();
    test_ui_f52();
    test_ui_f53();
    test_ui_modal();
    test_state();
    test_status_info();
    test_side_modes();
    test_input_acts();
    test_tree();
    test_tree_keys();
    test_trash_view();
    test_trash_keys();
    test_status_clears();
    test_trash_empty_items();
    test_trash_dirs_owned();
    test_eject();
    test_eject_keys();
    test_trash_empty_chunk();
    test_state_view();
    test_grid_keys();
    test_grid_scroll();
    test_view_switch();
    test_wrap();
    test_tooltip();
    test_zoom_levels();
    test_zoom_keys();
    test_zoom_mouse();
    test_zoom_refit();
    test_state_zoom();
    test_md5();
    test_thumb_names();
    test_thumb_local();
    test_png_meta();
    test_thumb_decode();
    test_thumb_get();
    test_clean_names();
    test_clean_real();
    test_clean_run();
    test_menu_keys();
    test_menu_enabled();
    test_menu_f74();
    test_select_match();
    test_f74_keys();
    test_bookmarks();
    test_bookmarks_menu();
    test_bookmarks_panel();
    test_bookmarks_edit();
    test_shortcuts();
    test_about();
    test_ctx_menus();
    test_ctx_keys();
    test_ctx_folder_entries();
    test_links();
    test_ctx_folder_menu();
    test_ctx_tab_menu();
    test_list_columns();
    test_menu_mouse();
    test_close_all_keys();
    test_rename_select();
    test_new_file();
    test_state_put();
    test_spawn_window();
    test_windows();
    test_apps();
    test_spawn_launch();
    test_du_walk();
    test_mime_comment();
    test_gio_type();
    test_match();
    test_find_toggles();
    test_apps_rank();
    test_ow_keys();
    test_ow_mouse();
    test_spawn_launch_all();
    test_set_default();
    test_ow_default();
    test_find_bar();
    test_find_count();
    test_shift_select();
    test_chmod();
    test_props_keys();
    test_step_round();
    test_tilde();
    test_fm1();
    test_search_uri();
    test_search_walk();
    test_search_inside();
    test_search_inside_keys();
    test_match_span();
    test_editors();
    test_conf();
    test_conf_skips();
    test_conf_preview_max();
    test_conf_step2();
    test_open_line_keys();
    test_search_match();
    test_search_keys_find();
    test_search_drives();
    test_search_ui_keys();
    test_search_ui_refused();
    test_search_crumbs();
    test_search_hist_keys();
    test_search_field_edit();
    test_find_field_edit();
    test_field_copy_fallthrough();
    test_line_words();
    test_line_active();
    char cmd[1600];
    snprintf(cmd, sizeof cmd, "chmod -R u+rwx '%s' && rm -rf '%s' '%s'", root, root, decoy);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", root);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
