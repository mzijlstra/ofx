// One folder's entries: read with readdir and fstatat, into an arena reset per folder, and sorted
// folders first, then by name in natural order ignoring case, or by size or modified time
// (docs/plan.md, "The listing" and F2's "Sort").

#include "../base/base.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum {
    K_FILE,
    K_DIR,
    K_LINK_FILE,     // a symlink to a file
    K_LINK_DIR,      // a symlink to a folder: entered like one
    K_LINK_BROKEN,   // a symlink to nothing
    K_OTHER,         // a device, socket or pipe
} Kind;

typedef struct TrashItem TrashItem;

// A file's first matching line, searched inside (F9 step 3): its number and the match's column
// (from 1, in characters), how many more lines match, and the line cut around the match (control
// characters as spaces) with the match at [at, at + len).
typedef struct {
    i64   line;
    i64   col;
    i64   more;
    Str   text;
    isize at, len;
    bool  cut;   // text starts after the line's start
} SearchHit;

typedef struct {
    Str   name;
    Kind  kind;
    i64   size;      // the target's, for a symlink; 0 for a folder
    i64   mtime;     // seconds since the epoch; in the trash view, when it was trashed
    u32   mode;      // st_mode, of the target when there is one
    u32   uid, gid;  // its owner and group, the target's as mode is (the list's Owner and Group)
    Str   target;    // a symlink's, as readlink gives it (relative or not); empty for the rest
    const TrashItem *trash;   // the trash view's (F6, trash.c): where the item is; NULL elsewhere
    Str   dir;       // the search view's (F9): its folder below the one searched ("" in that one)
    const SearchHit *hit;   // the search view's inside files (F9 step 3): where it matched; NULL elsewhere
} Entry;

typedef struct {
    Entry *e;
    isize  n;
    isize  hidden;   // dot-files in the folder (left out unless shown)
} Listing;

typedef enum { SORT_NAME, SORT_SIZE, SORT_MTIME, SORT_KEYS } SortKey;
static const char *const SORT_NAMES[SORT_KEYS] = { "name", "size", "modified" };   // as ofx.conf and the state file say them

typedef struct {
    SortKey key;
    bool    reverse;
} Sort;

static bool kind_is_dir(Kind k)
{
    return k == K_DIR || k == K_LINK_DIR;
}

// ---- natural order --------------------------------------------------------------------------

static int fold(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

// a against b: runs of digits compare as numbers (file2 before file10; leading zeros don't count,
// and a tie goes to the shorter run), other bytes by their lower case. A tie on that falls back to
// the plain bytes, so the order is total and the same every time.
static int natural_cmp(Str a, Str b)
{
    isize i = 0, j = 0;
    while (i < a.len && j < b.len) {
        if (is_digit(a.p[i]) && is_digit(b.p[j])) {
            isize i0 = i, j0 = j;
            while (i0 < a.len && a.p[i0] == '0') i0++;
            while (j0 < b.len && b.p[j0] == '0') j0++;
            isize i1 = i0, j1 = j0;
            while (i1 < a.len && is_digit(a.p[i1])) i1++;
            while (j1 < b.len && is_digit(b.p[j1])) j1++;
            if (i1 - i0 != j1 - j0) return i1 - i0 < j1 - j0 ? -1 : 1;
            int c = memcmp(a.p + i0, b.p + j0, (size_t)(i1 - i0));
            if (c) return c < 0 ? -1 : 1;
            if (i1 - i != j1 - j) return i1 - i < j1 - j ? -1 : 1;
            i = i1, j = j1;
            continue;
        }
        int x = fold((unsigned char)a.p[i]), y = fold((unsigned char)b.p[j]);
        if (x != y) return x < y ? -1 : 1;
        i++, j++;
    }
    if (i < a.len || j < b.len) return i < a.len ? 1 : -1;
    isize n = a.len < b.len ? a.len : b.len;
    int c = memcmp(a.p, b.p, (size_t)n);
    return c < 0 ? -1 : c > 0 ? 1 : a.len < b.len ? -1 : a.len > b.len ? 1 : 0;
}

// Folders first, whichever way; then the key, reversed if asked; ties by name, then by folder (the
// search view's), so the order is total and the same every time.
static int entry_cmp(const void *x, const void *y, void *ctx)
{
    const Entry *a = x, *b = y;
    const Sort *s = ctx;
    if (kind_is_dir(a->kind) != kind_is_dir(b->kind)) return kind_is_dir(a->kind) ? -1 : 1;
    int c = 0;
    if (s->key == SORT_SIZE && a->size != b->size) c = a->size < b->size ? -1 : 1;
    else if (s->key == SORT_MTIME && a->mtime != b->mtime) c = a->mtime < b->mtime ? -1 : 1;
    if (c == 0) c = natural_cmp(a->name, b->name);
    if (c == 0) c = natural_cmp(a->dir, b->dir);   // (the search view's names repeat, F9)
    return s->reverse ? -c : c;
}

static void fs_sort(Listing *l, Sort s)
{
    qsort_r(l->e, (size_t)l->n, sizeof *l->e, entry_cmp, &s);
}

// ---- reading --------------------------------------------------------------------------------

static Kind kind_of(int dirfd, const char *name, struct stat *st)
{
    if (fstatat(dirfd, name, st, AT_SYMLINK_NOFOLLOW) != 0) return *st = (struct stat){ 0 }, K_OTHER;   // (gone since readdir: nothing to show)
    if (S_ISLNK(st->st_mode)) {
        struct stat t;
        if (fstatat(dirfd, name, &t, 0) != 0) return K_LINK_BROKEN;
        *st = t;
        return S_ISDIR(t.st_mode) ? K_LINK_DIR : K_LINK_FILE;
    }
    return S_ISDIR(st->st_mode) ? K_DIR : S_ISREG(st->st_mode) ? K_FILE : K_OTHER;
}

// A symlink's target as it is written, into a; empty when it can't be read (or a is full).
static Str link_target(Arena *a, int dirfd, const char *name)
{
    char buf[PATH_MAX];
    ssize_t k = readlinkat(dirfd, name, buf, sizeof buf - 1);
    if (k <= 0) return STR_EMPTY;
    char *p = arena_try_push(a, k + 1, 1);
    if (!p) return STR_EMPTY;
    memcpy(p, buf, (size_t)k), p[k] = 0;
    return (Str){ p, k };
}

// The entry nm in the folder fd, its name and a link's target in a; false when a is full. (fs_list's,
// and the search view's walk's, F9.)
static bool entry_read(Arena *a, int fd, const char *nm, Entry *out)
{
    struct stat st;
    Kind k = kind_of(fd, nm, &st);
    isize len = (isize)strlen(nm);
    char *copy = arena_try_push(a, len + 1, 1);
    if (!copy) return false;
    memcpy(copy, nm, (size_t)len);
    bool link = k == K_LINK_FILE || k == K_LINK_DIR || k == K_LINK_BROKEN;
    *out = (Entry){
        .name = { copy, len },
        .kind = k,
        .size = kind_is_dir(k) || k == K_LINK_BROKEN ? 0 : (i64)st.st_size,
        .mtime = k == K_LINK_BROKEN ? 0 : (i64)st.st_mtim.tv_sec,
        .mode = k == K_LINK_BROKEN ? 0 : (u32)st.st_mode,
        .uid = (u32)st.st_uid, .gid = (u32)st.st_gid,
        .target = link ? link_target(a, fd, nm) : STR_EMPTY,
    };
    return true;
}

// The entries of dir, sorted; dot-files counted in hidden, and left out unless show_hidden. False,
// with the reason, when the folder can't be opened or the arena is full.
static bool fs_list(Arena *a, const char *dir, bool show_hidden, Sort sort, Listing *out, Err *err)
{
    *out = (Listing){ 0 };
    DIR *d = opendir(dir);
    if (!d) return err_set(err, "%s: %s", dir, strerror(errno)), false;
    int fd = dirfd(d);
    // The count is known only at the end: a full array is copied into one twice its size, and the
    // old one stays in the arena until its reset (the copies add up to less than the last one).
    isize cap = 256, n = 0;
    Entry *e = arena_try_new(a, Entry, cap);
    bool ok = e != NULL;
    for (struct dirent *de; ok && (de = readdir(d));) {
        const char *nm = de->d_name;
        if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) continue;
        if (nm[0] == '.') {
            out->hidden++;
            if (!show_hidden) continue;
        }
        if (n == cap) {
            Entry *grown = arena_try_new(a, Entry, cap * 2);
            if (!grown) {
                ok = false;
                break;
            }
            memcpy(grown, e, (size_t)n * sizeof *e);
            e = grown, cap *= 2;
        }
        if (!entry_read(a, fd, nm, &e[n])) {
            ok = false;
            break;
        }
        n++;
    }
    closedir(d);
    if (!ok) return err_set(err, "%s: too many entries for memory", dir), false;
    out->e = e, out->n = n;
    fs_sort(out, sort);
    return true;
}

// The index in l of the entry key names (entry_key), or -1.
static isize listing_find(const Listing *l, Str key)
{
    Str dir = path_dir(key), name = path_base(key);
    for (isize i = 0; i < l->n; i++)
        if (str_eq(l->e[i].name, name) && str_eq(l->e[i].dir, dir)) return i;
    return -1;
}

// What names e in its listing, into buf: its name; in the search view (F9) its folder, '/' and its
// name, as names repeat there (a name holds no '/'). Empty when it doesn't fit.
static Str entry_key(const Entry *e, char *buf, size_t cap)
{
    int n = e->dir.len ? snprintf(buf, cap, "%.*s/%.*s", STR_ARG(e->dir), STR_ARG(e->name)) : snprintf(buf, cap, "%.*s", STR_ARG(e->name));
    return n >= 0 && (size_t)n < cap ? (Str){ buf, n } : STR_EMPTY;
}

// How many entries folder path holds (dot-files only when hidden), for its Size cell; -1 when it
// can't be read (F4 "Item counts").
static isize count_entries(const char *path, bool hidden)
{
    DIR *d = opendir(path);
    if (!d) return -1;
    isize n = 0;
    for (struct dirent *de; (de = readdir(d));) {
        const char *nm = de->d_name;
        if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) continue;
        if (nm[0] == '.' && !hidden) continue;
        n++;
    }
    closedir(d);
    return n;
}
