// The GTK bookmarks file (F7.5, docs/plan.md "F7.5 decided"): `$XDG_CONFIG_HOME/gtk-3.0/bookmarks`,
// shared with Nemo and GTK's file choosers, one `URI [label]` a line. places.c reads the sidebar's
// from it; here it is read for the Bookmarks menu and its panel, which change it: every line kept
// in its order, those ofx doesn't use (sftp://, smb://) too, and written back whole, a new file
// renamed into place. A change reads the file again first, so one Nemo made meanwhile is kept.

#include "../base/base.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { BM_MAX = 256 };   // lines kept; past that the rest are left out of the menu, never dropped from the file (bm_full)

typedef struct {
    Str  uri;               // as the file has it
    Str  label;             // empty: none (the folder's name shows)
    char path[PATH_MAX];    // a local folder's path, decoded, no trailing slash; "" for another scheme
} Bookmark;

typedef struct {
    Bookmark b[BM_MAX];
    int      n;
    bool     full;          // the file has more lines than BM_MAX: written back, the rest would go, so it isn't
} Bookmarks;

// The file's lines, in its order; blank ones dropped.
static void bm_parse(Arena *a, Str text, Bookmarks *out)
{
    out->n = 0, out->full = false;
    isize at = 0;
    for (Str line; next_line(text, &at, &line);) {
        line = str_trim(line);
        if (!line.len) continue;
        if (out->n == BM_MAX) {
            out->full = true;
            break;
        }
        Bookmark *b = &out->b[out->n++];
        Str uri, label;
        gtk_bookmark_line(line, &uri, &label, b->path, sizeof b->path);   // (path "" for another scheme's)
        b->uri = (Str){ arena_cstr(a, uri), uri.len };
        b->label = (Str){ arena_cstr(a, label), label.len };
    }
}

// The file read for a change: false, saying why, when it is there but not read whole (unreadable,
// too big for a), since a change written over it would drop its lines.
static bool bm_read(Arena *a, const char *file, Bookmarks *out, Err *err)
{
    struct stat st;
    Str text = read_file(a, file);
    if (!text.len && (stat(file, &st) == 0 ? st.st_size > 0 : errno != ENOENT)) return err_set(err, "%s can't be read", file), false;
    bm_parse(a, text, out);
    return true;
}

// The place of path's bookmark, -1 for none.
static int bm_find(const Bookmarks *bm, const char *path)
{
    for (int i = 0; i < bm->n; i++)
        if (bm->b[i].path[0] && strcmp(bm->b[i].path, path) == 0) return i;
    return -1;
}

// A bookmark of path at the end, as GLib writes the URI (Nemo's own way); false, saying why, when
// it is there already or there is no room.
static bool bm_add(Arena *a, Bookmarks *bm, const char *path, Err *err)
{
    if (bm_find(bm, path) >= 0) return err_set(err, "already a bookmark"), false;
    if (bm->n == BM_MAX) return err_set(err, "the bookmarks file is full (%d)", BM_MAX), false;   // (full: n is BM_MAX)
    char uri[PATH_MAX * 3 + 8];
    if (!file_uri(path, uri, sizeof uri)) return err_set(err, "the path is too long"), false;
    Bookmark *b = &bm->b[bm->n++];
    b->uri = (Str){ arena_cstr(a, str_from_c(uri)), (isize)strlen(uri) };
    b->label = STR_EMPTY;
    snprintf(b->path, sizeof b->path, "%s", path);
    return true;
}

// The place of the bookmark whose address is uri (a network one, F16 step 5), -1 for none.
static int bm_find_uri(const Bookmarks *bm, const char *uri)
{
    for (int i = 0; i < bm->n; i++)
        if (str_eq(bm->b[i].uri, str_from_c(uri))) return i;
    return -1;
}

// A bookmark of a network address at the end (a folder in a share, F16 step 5: as Nautilus writes
// one), with its label (empty: none); false, saying why, as bm_add.
static bool bm_add_uri(Arena *a, Bookmarks *bm, const char *uri, Str label, Err *err)
{
    if (bm_find_uri(bm, uri) >= 0) return err_set(err, "already a bookmark"), false;
    if (bm->n == BM_MAX) return err_set(err, "the bookmarks file is full (%d)", BM_MAX), false;
    Bookmark *b = &bm->b[bm->n++];
    b->uri = (Str){ arena_cstr(a, str_from_c(uri)), (isize)strlen(uri) };
    b->label = label.len ? (Str){ arena_cstr(a, label), label.len } : STR_EMPTY;
    b->path[0] = 0;
    return true;
}

// The file's text: a line a bookmark, the label after a space.
static Str bm_format(Arena *a, const Bookmarks *bm)
{
    isize avail;
    char *out = arena_tail(a, &avail);
    isize n = 0;
    for (int i = 0; i < bm->n; i++) {
        const Bookmark *b = &bm->b[i];
        int k = b->label.len ? snprintf(out + n, (size_t)(avail - n), "%.*s %.*s\n", STR_ARG(b->uri), STR_ARG(b->label))
                             : snprintf(out + n, (size_t)(avail - n), "%.*s\n", STR_ARG(b->uri));
        if (k < 0 || k >= avail - n) return STR_EMPTY;
        n += k;
    }
    arena_commit(a, n + 1);
    out[n] = 0;
    return (Str){ out, n };
}

// text as the bookmarks file `file`: a new file beside it (its mode kept, 0644 for a first one),
// synced, renamed over it; its folder made (0700, as GLib makes it) when there is none. As GLib
// does (a review): a symbolic link's target is written, the link kept (dotfiles kept elsewhere);
// a file with other names (st_nlink > 1) is written in place, so they see it too. Refused outside
// OFX_GUI_ROOT when that is set (ops_allowed), as every write of ofx's.
static bool bm_write(const char *file, Str text, Err *err)
{
    char real[PATH_MAX];
    struct stat lst, st;
    if (lstat(file, &lst) == 0 && S_ISLNK(lst.st_mode)) {
        if (!realpath(file, real)) return err_set(err, "%s: a link to nothing (%s)", file, strerror(errno)), false;
        file = real;
    }
    if (!ops_allowed(file, err)) return false;
    if (stat(file, &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink > 1) {
        int fd = open(file, O_WRONLY | O_TRUNC | O_CLOEXEC);
        if (fd < 0) return err_set(err, "%s: %s", file, strerror(errno)), false;
        bool ok = fd_write_all(fd, text.p, text.len) && fsync(fd) == 0;
        int e = errno;
        if (close(fd) != 0 || !ok) return err_set(err, "%s: %s", file, strerror(ok ? errno : e)), false;
        return true;
    }
    char dir[PATH_MAX], tmp[PATH_MAX];
    if (!path_parent(dir, sizeof dir, file)) return err_set(err, "the path is too long"), false;
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) return err_set(err, "%s: %s", dir, strerror(errno)), false;
    if (snprintf(tmp, sizeof tmp, "%s/.bookmarks.ofx-%d", dir, (int)getpid()) >= (int)sizeof tmp) return err_set(err, "the path is too long"), false;
    mode_t mode = stat(file, &st) == 0 ? st.st_mode & 0777 : 0644;
    return file_replace(file, tmp, text.p, text.len, mode, true, err);
}

// ---- Edit Bookmarks' changes (F7.5) -----------------------------------------------------------

static bool bm_same(const Bookmark *x, const Bookmark *y)
{
    return str_eq(x->uri, y->uri) && str_eq(x->label, y->label);
}

// The place in now (the file read again) of line i of was (what the panel showed): the same text,
// as many like it before it; -1 when it is gone or now has fewer of them, the file changed.
static int bm_locate(const Bookmarks *now, const Bookmarks *was, int i)
{
    if (i < 0 || i >= was->n) return -1;
    int before = 0;
    for (int k = 0; k < i; k++) before += bm_same(&was->b[k], &was->b[i]);
    for (int k = 0; k < now->n; k++)
        if (bm_same(&now->b[k], &was->b[i]) && before-- == 0) return k;
    return -1;
}

static void bm_remove(Bookmarks *bm, int i)
{
    if (i < 0 || i >= bm->n) return;
    memmove(&bm->b[i], &bm->b[i + 1], sizeof bm->b[0] * (size_t)(bm->n - i - 1));
    bm->n--;
}

// Lines i and j swapped (a move up or down).
static void bm_swap(Bookmarks *bm, int i, int j)
{
    if (i < 0 || j < 0 || i >= bm->n || j >= bm->n) return;
    Bookmark t = bm->b[i];
    bm->b[i] = bm->b[j], bm->b[j] = t;
}

// Line i's label, trimmed; empty for none (the folder's name shows). A newline can't be in one.
static void bm_rename(Arena *a, Bookmarks *bm, int i, Str label)
{
    if (i < 0 || i >= bm->n) return;
    for (isize k = 0; k < label.len; k++)
        if (label.p[k] == '\n' || label.p[k] == '\r') label.len = k;
    label = str_trim(label);
    bm->b[i].label = (Str){ arena_cstr(a, label), label.len };
}
