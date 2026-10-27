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
        isize sp = str_find(line, ' ');
        Str uri = sp < 0 ? line : str_cut(line, 0, sp), label = sp < 0 ? STR_EMPTY : str_trim(str_cut(line, sp + 1, line.len));
        b->uri = (Str){ arena_cstr(a, uri), uri.len };
        b->label = (Str){ arena_cstr(a, label), label.len };
        b->path[0] = 0;
        if (!str_starts_with(uri, S("file://")) || !percent_decode(str_cut(uri, 7, uri.len), b->path, sizeof b->path) || b->path[0] != '/')
            b->path[0] = 0;
        for (size_t n = strlen(b->path); n > 1 && b->path[n - 1] == '/';) b->path[--n] = 0;
    }
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
    if (!thumb_uri(path, uri, sizeof uri)) return err_set(err, "the path is too long"), false;
    Bookmark *b = &bm->b[bm->n++];
    b->uri = (Str){ arena_cstr(a, str_from_c(uri)), (isize)strlen(uri) };
    b->label = STR_EMPTY;
    snprintf(b->path, sizeof b->path, "%s", path);
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
// synced, renamed over it; its folder made (0700, as GLib makes it) when there is none. Refused
// outside OFX_GUI_ROOT when that is set (ops_allowed), as every write of ofx's.
static bool bm_write(const char *file, Str text, Err *err)
{
    if (!ops_allowed(file, err)) return false;
    char dir[PATH_MAX], tmp[PATH_MAX];
    Str d = path_dir(str_from_c(file));
    snprintf(dir, sizeof dir, "%.*s", STR_ARG(d));
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) return err_set(err, "%s: %s", dir, strerror(errno)), false;
    if (snprintf(tmp, sizeof tmp, "%s/.bookmarks.ofx-%d", dir, (int)getpid()) >= (int)sizeof tmp) return err_set(err, "the path is too long"), false;
    struct stat st;
    mode_t mode = stat(file, &st) == 0 ? st.st_mode & 0777 : 0644;
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, mode);
    if (fd < 0) return err_set(err, "%s: %s", tmp, strerror(errno)), false;
    bool ok = fchmod(fd, mode) == 0;
    for (isize at = 0; ok && at < text.len;) {
        ssize_t k = write(fd, text.p + at, (size_t)(text.len - at));
        if (k <= 0) ok = false;
        else at += k;
    }
    ok = ok && fsync(fd) == 0;
    int e = errno;
    if (close(fd) != 0) ok = false;
    if (ok && rename(tmp, file) == 0) return true;
    if (!ok) errno = e;
    err_set(err, "%s: %s", file, strerror(errno));
    unlink(tmp);
    return false;
}
