// Search below a folder (F9, docs/plan.md "F9 design"): the view's address, and the walk that finds
// the names, run on gui/search.c's thread. It goes depth first from the folder searched and stays
// on its drive; it enters no link, no hidden folder unless View → Show Hidden Files, and none of
// SEARCH_SKIP (Michael, 2026-10-27: a fixed list until ofx.conf). A folder it doesn't enter still
// matches by its own name. It counts what it left out, for the status line, and stops at
// SEARCH_MAX matches or when the generation moves (a new search), as du_walk.

#include "../base/base.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

// The view's address, a folder's path as far as tabs, history and go_to are concerned:
// "search:" and the folder searched's length, ':', the folder, then the text, so either may hold
// any byte. A path always starts with '/', so it is never one.
#define SEARCH_URI "search:"

enum { SEARCH_MAX = 10000, SEARCH_DEPTH_MAX = 256, SEARCH_TICK = 512 };

static const char *const SEARCH_SKIP[] = { ".git", "node_modules" };
enum { SEARCH_SKIPS = 2 };

static bool is_search(const char *dir)
{
    return strncmp(dir, SEARCH_URI, sizeof SEARCH_URI - 1) == 0;
}

// The address of a search for text below root; false when it doesn't fit.
static bool search_uri(char *out, size_t cap, const char *root, Str text)
{
    int n = snprintf(out, cap, SEARCH_URI "%zu:%s%.*s", strlen(root), root, STR_ARG(text));
    return n >= 0 && (size_t)n < cap;
}

// The folder and the text an address holds (pointing into it); false when it isn't one.
static bool search_parse(const char *uri, Str *root, Str *text)
{
    if (!is_search(uri)) return false;
    const char *p = uri + sizeof SEARCH_URI - 1;
    isize len = 0;
    while (*p >= '0' && *p <= '9') {
        len = len * 10 + (*p++ - '0');
        if (len > PATH_MAX) return false;
    }
    if (*p++ != ':' || (isize)strnlen(p, (size_t)len) < len || len == 0 || p[0] != '/') return false;
    *root = (Str){ p, len };
    *text = str_from_c(p + len);
    return true;
}

// What the walk left out (none of it searched inside), for the status line.
typedef struct {
    i64  skip[SEARCH_SKIPS];   // folders named in SEARCH_SKIP
    i64  hidden;               // dot-files and dot-folders (View → Show Hidden Files off)
    i64  drives;               // folders where another drive is mounted
    i64  links;                // links to folders
    i64  unread;               // folders that couldn't be read, or nested too deep
    i64  folders;              // folders searched, the one it started from included
    bool full;                 // SEARCH_MAX matches: it stopped there
} SearchSkips;

typedef struct {
    const atomic_uint *gen;    // the walk stops when this moves off mine
    u32                mine;
    bool               hidden; // dot-files searched and their folders entered
    bool             (*match)(void *ctx, Str name);
    // A match: name in the folder fd, which is dir below the folder searched ("" for that folder
    // itself). False when there is no room for it: the walk stops, full.
    bool             (*found)(void *ctx, Str dir, Str name, int fd);
    void             (*tick)(void *ctx, const SearchSkips *s);   // every SEARCH_TICK entries
    void              *ctx;
    dev_t              dev;    // the drive it started on
    SearchSkips        s;
    isize              seen;
    char               rel[PATH_MAX];   // the folder being read, below the one searched
    isize              rel_len;
} SearchWalk;

static bool search_stopped(const SearchWalk *w)
{
    return w->gen && atomic_load(w->gen) != w->mine;
}

static int search_skip_of(const char *name)
{
    for (int k = 0; k < SEARCH_SKIPS; k++)
        if (strcmp(name, SEARCH_SKIP[k]) == 0) return k;
    return -1;
}

// The folder fd (closed here) and everything below it it may enter. False when stopped or full.
static bool search_dir(SearchWalk *w, int fd, int depth)
{
    if (depth > SEARCH_DEPTH_MAX) return close(fd), w->s.unread++, true;
    DIR *d = fdopendir(fd);
    if (!d) return close(fd), w->s.unread++, true;
    w->s.folders++;
    bool go = true;
    for (struct dirent *e; go && (e = readdir(d));) {
        const char *nm = e->d_name;
        if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) continue;
        if (search_stopped(w)) {
            go = false;
            break;
        }
        if (++w->seen % SEARCH_TICK == 0 && w->tick) w->tick(w->ctx, &w->s);
        if (nm[0] == '.' && !w->hidden) {
            w->s.hidden++;
            continue;
        }
        Str name = str_from_c(nm);
        if (w->match(w->ctx, name) && !w->found(w->ctx, (Str){ w->rel, w->rel_len }, name, dirfd(d))) {
            w->s.full = true, go = false;
            break;
        }
        // Into a folder: the type readdir gives, else a stat (and one for a link, to tell a link to
        // a folder from one to a file).
        unsigned char t = e->d_type;
        struct stat st;
        if (t == DT_UNKNOWN) {
            if (fstatat(dirfd(d), nm, &st, AT_SYMLINK_NOFOLLOW) != 0) continue;
            t = S_ISDIR(st.st_mode) ? DT_DIR : S_ISLNK(st.st_mode) ? DT_LNK : DT_REG;
        }
        if (t == DT_LNK && fstatat(dirfd(d), nm, &st, 0) == 0 && S_ISDIR(st.st_mode)) w->s.links++;
        if (t != DT_DIR) continue;
        int k = search_skip_of(nm);
        if (k >= 0) {
            w->s.skip[k]++;
            continue;
        }
        if (fstatat(dirfd(d), nm, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(st.st_mode)) continue;
        if (st.st_dev != w->dev) {
            w->s.drives++;
            continue;
        }
        isize was = w->rel_len, len = name.len;
        if (was + (was ? 1 : 0) + len >= (isize)sizeof w->rel) {
            w->s.unread++;
            continue;
        }
        if (was) w->rel[w->rel_len++] = '/';
        memcpy(w->rel + w->rel_len, nm, (size_t)len), w->rel_len += len, w->rel[w->rel_len] = 0;
        int sub = openat(dirfd(d), nm, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (sub < 0) w->s.unread++;
        else go = search_dir(w, sub, depth + 1);
        w->rel_len = was, w->rel[was] = 0;
    }
    closedir(d);
    return go;
}

// Everything below root that matches, into found, w->s zeroed first. False when stopped (a new
// search) or full; w->s says which.
static bool search_walk(SearchWalk *w, const char *root)
{
    w->s = (SearchSkips){ 0 }, w->seen = 0, w->rel_len = 0, w->rel[0] = 0;
    struct stat st;
    int fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0 || fstat(fd, &st) != 0) {
        if (fd >= 0) close(fd);
        w->s.unread++;
        if (w->tick) w->tick(w->ctx, &w->s);
        return true;
    }
    w->dev = st.st_dev;
    bool whole = search_dir(w, fd, 1);
    if (w->tick) w->tick(w->ctx, &w->s);
    return whole;
}

// What the walk left out, for the status line ("skipped 3 .git, 1 link to a folder"); empty when
// nothing was.
static void search_skips_text(const SearchSkips *s, char *out, size_t cap)
{
    out[0] = 0;
    size_t at = 0;
    const char *sep = "skipped ";
#define SEARCH_PART(n, one, many)                                                                             \
    if ((n) > 0 && at < cap) {                                                                              \
        int k = snprintf(out + at, cap - at, "%s%lld %s", sep, (long long)(n), (n) == 1 ? (one) : (many));    \
        if (k > 0) at += (size_t)k;   /* (past cap: cut, and nothing more) */                                \
        sep = ", ";                                                                                         \
    }
    SEARCH_PART(s->skip[0], ".git", ".git")
    SEARCH_PART(s->skip[1], "node_modules", "node_modules")
    SEARCH_PART(s->hidden, "hidden", "hidden")
    SEARCH_PART(s->drives, "other drive", "other drives")
    SEARCH_PART(s->links, "link to a folder", "links to folders")
    SEARCH_PART(s->unread, "unreadable folder", "unreadable folders")
#undef SEARCH_PART
}
