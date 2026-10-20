// One folder's entries: read with readdir and fstatat, into an arena reset per folder, and sorted
// folders first, then by name in natural order, ignoring case (docs/plan.md, "The listing").

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

typedef struct {
    Str   name;
    Kind  kind;
    i64   size;      // the target's, for a symlink; 0 for a folder
    i64   mtime;     // seconds since the epoch
    u32   mode;      // st_mode, of the target when there is one
} Entry;

typedef struct {
    Entry *e;
    isize  n;
    isize  hidden;   // dot-files left out
} Listing;

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

static int entry_cmp(const void *x, const void *y)
{
    const Entry *a = x, *b = y;
    if (kind_is_dir(a->kind) != kind_is_dir(b->kind)) return kind_is_dir(a->kind) ? -1 : 1;
    return natural_cmp(a->name, b->name);
}

// ---- reading --------------------------------------------------------------------------------

static Kind kind_of(int dirfd, const char *name, struct stat *st)
{
    if (fstatat(dirfd, name, st, AT_SYMLINK_NOFOLLOW) != 0) return K_OTHER;
    if (S_ISLNK(st->st_mode)) {
        struct stat t;
        if (fstatat(dirfd, name, &t, 0) != 0) return K_LINK_BROKEN;
        *st = t;
        return S_ISDIR(t.st_mode) ? K_LINK_DIR : K_LINK_FILE;
    }
    return S_ISDIR(st->st_mode) ? K_DIR : S_ISREG(st->st_mode) ? K_FILE : K_OTHER;
}

// The entries of dir, sorted, dot-files left out (counted in hidden). False, with the reason, when
// the folder can't be opened or the arena is full.
static bool fs_list(Arena *a, const char *dir, Listing *out, Err *err)
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
        if (nm[0] == '.') {
            if (strcmp(nm, ".") != 0 && strcmp(nm, "..") != 0) out->hidden++;
            continue;
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
        struct stat st;
        Kind k = kind_of(fd, nm, &st);
        isize len = (isize)strlen(nm);
        char *copy = arena_try_push(a, len + 1, 1);
        if (!copy) {
            ok = false;
            break;
        }
        memcpy(copy, nm, (size_t)len);
        e[n++] = (Entry){
            .name = { copy, len },
            .kind = k,
            .size = kind_is_dir(k) || k == K_LINK_BROKEN ? 0 : (i64)st.st_size,
            .mtime = k == K_LINK_BROKEN ? 0 : (i64)st.st_mtim.tv_sec,
            .mode = k == K_LINK_BROKEN ? 0 : (u32)st.st_mode,
        };
    }
    closedir(d);
    if (!ok) return err_set(err, "%s: too many entries for memory", dir), false;
    qsort(e, (size_t)n, sizeof *e, entry_cmp);
    out->e = e, out->n = n;
    return true;
}

// The index of name in l, or -1.
static isize listing_find(const Listing *l, Str name)
{
    for (isize i = 0; i < l->n; i++)
        if (str_eq(l->e[i].name, name)) return i;
    return -1;
}
