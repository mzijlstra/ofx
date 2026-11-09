// The sidebar's tree (docs/plan.md, F6 "Tree mode"): the folders from / down, a row each, open
// along the way to the current folder and wherever the user opened one. Each open folder's
// subfolders (symlinks to folders too) under it, in natural order; dot-folders only when hidden
// files are shown. Built again whenever something changes; only the open folders are read.

#include "../base/base.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

enum { TREE_ROWS_MAX = 4096, TREE_OPEN_MAX = 128 };

typedef struct {
    Str  path;    // in the build's arena
    Str  name;    // the path's last part, "/" for the root
    int  depth;   // 0 for /
    bool open;    // its subfolders are the rows after it
} TreeRow;

typedef struct {
    TreeRow row[TREE_ROWS_MAX];
    int     n;
    bool    cut;                            // more folders than TREE_ROWS_MAX: the rest left out
    char    open[TREE_OPEN_MAX][PATH_MAX];  // the folders open, oldest first
    int     nopen;
    Str     mounts;                         // the mount list (mountinfo): a far drive's folders aren't read (F16)
} Tree;


static int tree_open_at(const Tree *t, Str path)
{
    for (int i = 0; i < t->nopen; i++)
        if (str_eq(str_from_c(t->open[i]), path)) return i;
    return -1;
}

static bool tree_is_open(const Tree *t, Str path)
{
    return tree_open_at(t, path) >= 0;
}

// path open; when the list is full the oldest open folder closes for it.
static void tree_open(Tree *t, Str path)
{
    if (path.len >= PATH_MAX || tree_is_open(t, path)) return;
    if (t->nopen == TREE_OPEN_MAX) memmove(t->open[0], t->open[1], sizeof t->open[0] * (TREE_OPEN_MAX - 1)), t->nopen--;
    snprintf(t->open[t->nopen++], PATH_MAX, "%.*s", STR_ARG(path));
}

static void tree_close(Tree *t, Str path)
{
    int i = tree_open_at(t, path);
    if (i < 0) return;
    memmove(t->open[i], t->open[i + 1], sizeof t->open[0] * (size_t)(t->nopen - i - 1));
    t->nopen--;
}

// The folders open, as the state file's tree= keeps them (F11): each %-encoded, a space between, into
// out; false when they don't fit (cap).
static bool tree_open_text(const Tree *t, char *out, size_t cap)
{
    size_t n = 0;
    out[0] = 0;
    for (int i = 0; i < t->nopen; i++) {
        if (i && n + 1 < cap) out[n++] = ' ', out[n] = 0;
        if (!percent_encode(str_from_c(t->open[i]), "/", out + n, cap - n)) return false;
        n += strlen(out + n);
    }
    return true;
}

// The room tree_open_text needs: each path at most 3 bytes a byte (%XX), a space after, the NUL.
static size_t tree_open_cap(const Tree *t)
{
    size_t cap = 1;
    for (int i = 0; i < t->nopen; i++) cap += 3 * strlen(t->open[i]) + 1;
    return cap;
}

// tree='s folders opened in t (an absolute path each; others left out).
static void tree_open_load(Tree *t, Str text)
{
    isize at = 0;
    while (at < text.len) {
        while (at < text.len && text.p[at] == ' ') at++;
        isize b = at;
        while (at < text.len && text.p[at] != ' ') at++;
        char p[PATH_MAX];
        if (at > b && percent_decode(str_cut(text, b, at), p, sizeof p) && p[0] == '/') tree_open(t, str_from_c(p));
    }
}

// The folders above dir (an absolute path) open, / first, so dir is a row of the next build.
static void tree_follow(Tree *t, const char *dir)
{
    Str d = str_from_c(dir);
    if (d.len == 0 || d.p[0] != '/') return;
    tree_open(t, S("/"));
    for (isize i = 1; i < d.len; i++)
        if (d.p[i] == '/') tree_open(t, str_cut(d, 0, i));
}

static int tree_name_cmp(const void *x, const void *y)
{
    return natural_cmp(*(const Str *)x, *(const Str *)y);
}

// Whether entry de of the folder dirfd (dir) is a folder, or a symlink to one: as the list takes it
// (kind_of_far), so a link into a far drive is one, never followed (F16).
static bool tree_is_dir(int dirfd, const char *dir, Str mounts, const struct dirent *de)
{
    if (de->d_type == DT_DIR) return true;
    if (de->d_type != DT_LNK && de->d_type != DT_UNKNOWN) return false;
    struct stat st;
    bool far;
    return kind_is_dir(kind_of_far(dirfd, dir, de->d_name, mounts, &st, &far));
}

// Whether path is a link whose target, by its text, is on a far drive (F16): its folder isn't read.
static bool tree_far_link(Str mounts, const char *path)
{
    char t[PATH_MAX], dir[PATH_MAX], real[PATH_MAX], abs[PATH_MAX];
    ssize_t n = mounts.len ? readlink(path, t, sizeof t - 1) : -1;
    return n > 0 && path_parent(dir, sizeof dir, path) && realpath(dir, real) && link_abs(real, (Str){ t, n }, abs, sizeof abs) && !thumb_local(mounts, abs);   // (its folder's real path: as fs_list_far)
}

static void tree_add(Arena *a, Tree *t, Str path, int depth, bool hidden);

// The subfolders of path, sorted, each added at depth (with its own when open).
static void tree_children(Arena *a, Tree *t, Str path, int depth, bool hidden)
{
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%.*s", STR_ARG(path));
    if (t->mounts.len && (!thumb_local(t->mounts, dir) || tree_far_link(t->mounts, dir))) return;   // (open, but shown without its subfolders: a dead share would hold the window, F16)
    DIR *d = opendir(dir);
    if (!d) return;
    char real[PATH_MAX];   // (the far checks from its real path, as fs_list_far's)
    const char *at = t->mounts.len && realpath(dir, real) ? real : dir;
    isize cap = 64, n = 0;
    Str *names = arena_try_new(a, Str, cap);
    for (struct dirent *de; names && (de = readdir(d));) {
        const char *nm = de->d_name;
        if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0 || (nm[0] == '.' && !hidden) || !tree_is_dir(dirfd(d), at, t->mounts, de)) continue;
        if (n == cap) {
            Str *grown = arena_try_new(a, Str, cap * 2);
            if (!grown) break;
            memcpy(grown, names, sizeof *names * (size_t)n);
            names = grown, cap *= 2;
        }
        char *copy = arena_try_push(a, (isize)strlen(nm) + 1, 1);
        if (!copy) break;
        strcpy(copy, nm);
        names[n++] = str_from_c(copy);
    }
    closedir(d);
    if (!names) return;
    qsort(names, (size_t)n, sizeof *names, tree_name_cmp);
    for (isize i = 0; i < n; i++) {
        bool root = path.len == 1;
        char *p = arena_try_push(a, path.len + names[i].len + 2, 1);
        if (!p) return;
        int len = snprintf(p, (size_t)(path.len + names[i].len + 2), "%.*s%s%.*s", STR_ARG(path), root ? "" : "/", STR_ARG(names[i]));
        tree_add(a, t, (Str){ p, len }, depth, hidden);
    }
}

static void tree_add(Arena *a, Tree *t, Str path, int depth, bool hidden)
{
    if (t->n == TREE_ROWS_MAX) {
        t->cut = true;
        return;
    }
    TreeRow *r = &t->row[t->n++];
    *r = (TreeRow){ .path = path, .name = path.len == 1 ? S("/") : path_base(path), .depth = depth, .open = tree_is_open(t, path) };
    if (r->open) tree_children(a, t, path, depth + 1, hidden);
}

// The rows, from root (/ but in the tests), into the arena (reset by the caller before each build).
static void tree_build(Arena *a, Tree *t, Str root, bool hidden)
{
    t->n = 0, t->cut = false;
    tree_add(a, t, root, 0, hidden);
}

// The row of path, -1 when it isn't one.
static int tree_find(const Tree *t, Str path)
{
    for (int i = 0; i < t->n; i++)
        if (str_eq(t->row[i].path, path)) return i;
    return -1;
}

// The row of path, or of the nearest folder above it that has one (a folder inside one closed, a
// hidden one with hidden files off: Tab's cursor, Michael 2026-10-30); -1 when none has.
static int tree_find_near(const Tree *t, Str path)
{
    for (;;) {
        int i = tree_find(t, path);
        if (i >= 0 || path.len <= 1) return i;
        Str up = path_dir(path);
        path = up.len ? up : S("/");
    }
}

// The row above row i at one depth less (its folder's), -1 for the root.
static int tree_parent(const Tree *t, int i)
{
    for (int k = i - 1; k >= 0; k--)
        if (t->row[k].depth < t->row[i].depth) return k;
    return -1;
}

// Left on row i (h): an open folder closes (*toggle), a closed one's cursor goes to its folder's
// row. The row the cursor goes to.
static int tree_left(const Tree *t, int i, bool *toggle)
{
    *toggle = i >= 0 && i < t->n && t->row[i].open;
    if (*toggle || i < 0 || i >= t->n) return i;
    int p = tree_parent(t, i);
    return p >= 0 ? p : i;
}

// Right on row i (l): a closed folder opens (*toggle), an open one's cursor goes to its first
// subfolder, if it has one.
static int tree_right(const Tree *t, int i, bool *toggle)
{
    *toggle = i >= 0 && i < t->n && !t->row[i].open;
    if (*toggle || i < 0 || i >= t->n) return i;
    return i + 1 < t->n && t->row[i + 1].depth > t->row[i].depth ? i + 1 : i;
}
