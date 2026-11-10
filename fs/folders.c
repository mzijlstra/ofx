// What ofx remembers per folder (F11, docs/plan.md "F11 design"): its view, icon size, sort key and
// Reverse Order where they differ from ofx.conf's defaults, and the cursor's entry. Kept in the state
// file (state.c) as one line a folder, beside the global keys:
//
//   folder <path %-encoded> <last opened, s> <view> <zoom> <sort> <reverse> <cursor %-encoded>
//
// The path is a folder's, or the trash view's TRASH_URI.
// "-" for a field not set (the default applies). Each window is its own process, so a write reads
// the file again and puts in only the fields this window changed (folders_write), as state_put does
// for a key. Each write also keeps the file small: the most recently opened FOLDERS_MAX, and none
// for a folder that is gone, but a folder on a drive that isn't there now, or on a network mount, is
// kept (ofx-clean's rules: clean_real, clean_absent_drive).

#include "../base/base.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum { FOLDERS_MAX = 1000, FOLDERS_ROOM = FOLDERS_MAX + 64 };

// The fields, as bits of FolderPrefs.changed (this window's changes, not written yet).
enum { FP_VIEW = 1, FP_ZOOM = 2, FP_SORT = 4, FP_REV = 8, FP_SEL = 16, FP_AT = 32 };

typedef struct {
    const char *path;      // in the table's arena
    i64         at;        // last opened, in seconds
    int         view;      // -1: not set; 0 the list, 1 icons
    int         zoom;      // 0: not set
    int         sort;      // -1: not set; else a SortKey
    int         rev;       // -1: not set; 0 or 1
    const char *sel;       // the cursor's entry; NULL: none
    u8          changed;   // FP_ bits
} FolderPrefs;

typedef struct {
    FolderPrefs f[FOLDERS_ROOM];
    int         n;
} Folders;


// s copied into a, NUL-ended; NULL when a is full (a folder past it is left out, never a crash).
static const char *folder_str(Arena *a, Str s)
{
    char *p = arena_try_push(a, s.len + 1, 1);
    if (p) memcpy(p, s.p, (size_t)s.len), p[s.len] = 0;
    return p;
}

// The next word of line from *at.
static Str folder_word(Str line, isize *at)
{
    while (*at < line.len && line.p[*at] == ' ') (*at)++;
    isize b = *at;
    while (*at < line.len && line.p[*at] != ' ') (*at)++;
    return str_cut(line, b, *at);
}

// The state file's folder lines into t, their strings in a; other lines left alone.
static int folders_find(const Folders *t, const char *path)
{
    for (int i = 0; i < t->n; i++)
        if (strcmp(t->f[i].path, path) == 0) return i;
    return -1;
}

// Whether line is a folder line ofx reads (its seven words, its path a folder's or the trash
// view's), its words into w and its path into path; a line that isn't stays in the file as it is.
static bool folder_line_read(Str line, Str *w, char *path, size_t cap)
{
    if (!str_starts_with(line, S("folder "))) return false;
    isize k = 7;
    for (int i = 0; i < 7; i++) w[i] = folder_word(line, &k);
    return w[6].len && percent_decode(w[0], path, cap) && (path[0] == '/' || strcmp(path, TRASH_URI) == 0);
}

static void folders_parse(Arena *a, Str text, Folders *t)
{
    t->n = 0;
    isize at = 0;
    for (Str line; next_line(text, &at, &line);) {
        Str w[7];
        char path[PATH_MAX], sel[NAME_MAX * 3 + 1];
        if (!folder_line_read(line, w, path, sizeof path) || folders_find(t, path) >= 0) continue;   // (a folder said twice: its first line, F11 review)
        if (t->n == FOLDERS_ROOM) continue;   // (the rest left out: folders_tidy keeps FOLDERS_MAX)
        FolderPrefs *f = &t->f[t->n];
        i64 when = str_num(w[1], INT64_MAX / 2), zoom = str_num(w[3], 4096);
        *f = (FolderPrefs){ .at = when < 0 ? 0 : when, .view = -1, .zoom = (int)(zoom < 0 ? 0 : zoom), .sort = -1, .rev = -1 };
        if (str_eq(w[2], S("list"))) f->view = 0;
        if (str_eq(w[2], S("icons"))) f->view = 1;
        for (int s = 0; s < SORT_KEYS; s++)
            if (str_eq(w[4], str_from_c(SORT_NAMES[s]))) f->sort = s;
        if (str_eq(w[5], S("on"))) f->rev = 1;
        if (str_eq(w[5], S("off"))) f->rev = 0;
        if (!str_eq(w[6], S("-")) && percent_decode(w[6], sel, sizeof sel) && sel[0] && !strchr(sel, '/')) f->sel = folder_str(a, str_from_c(sel));
        if ((f->path = folder_str(a, str_from_c(path)))) t->n++;   // (else the arena's full: left out)
    }
}


// The entry for path, made (nothing set) when there is none; NULL when the table is full.
static FolderPrefs *folders_get(Arena *a, Folders *t, const char *path)
{
    int i = folders_find(t, path);
    if (i >= 0) return &t->f[i];
    const char *p = t->n < FOLDERS_ROOM ? folder_str(a, str_from_c(path)) : NULL;
    if (!p) return NULL;
    FolderPrefs *f = &t->f[t->n++];
    *f = (FolderPrefs){ .path = p, .view = -1, .sort = -1, .rev = -1 };
    return f;
}

// Whether an entry says anything: a field set, or a cursor.
static bool folder_says(const FolderPrefs *f)
{
    return f->view >= 0 || f->zoom > 0 || f->sort >= 0 || f->rev >= 0 || f->sel;
}

// f's line, with its newline, into out; its length, 0 when it doesn't fit.
static int folder_line(const FolderPrefs *f, char *out, size_t cap)
{
    char path[PATH_MAX * 3], sel[NAME_MAX * 3 + 1], zoom[8] = "-";
    if (!percent_encode(str_from_c(f->path), "/", path, sizeof path)) return 0;
    if (!f->sel) snprintf(sel, sizeof sel, "-");
    else if (!percent_encode(str_from_c(f->sel), "", sel, sizeof sel)) return 0;
    if (f->zoom > 0) snprintf(zoom, sizeof zoom, "%d", f->zoom);
    int n = snprintf(out, cap, "folder %s %lld %s %s %s %s %s\n", path, (long long)f->at, f->view < 0 ? "-" : f->view ? "icons" : "list", zoom,
                     f->sort < 0 ? "-" : SORT_NAMES[(int)f->sort], f->rev < 0 ? "-" : f->rev ? "on" : "off", sel);
    return n > 0 && (size_t)n < cap ? n : 0;
}

// Whether a folder is gone for good: not there (ENOENT, ENOTDIR), and not on a drive that is out now
// or a network or FUSE mount, where a look could hang (ofx-clean's rules: clean_real stops there).
static bool folder_gone(const Mounts *ms, const char *path)
{
    char real[PATH_MAX];
    struct stat st;
    if (path[0] != '/' || !clean_real(ms, path, real, sizeof real) || clean_absent_drive(ms, real)) return false;   // (the trash view's: never)
    return stat(path, &st) != 0 && (errno == ENOENT || errno == ENOTDIR);
}

static int folders_by_age(const void *x, const void *y)
{
    i64 a = ((const FolderPrefs *)x)->at, b = ((const FolderPrefs *)y)->at;
    return a < b ? 1 : a > b ? -1 : 0;   // the most recent first
}

// The housekeeping on t before a write: entries that say nothing, or whose folder is gone, out; then
// the most recently opened FOLDERS_MAX kept. The number dropped.
static int folders_tidy(Folders *t, const Mounts *ms)
{
    int k = 0, before = t->n;
    for (int i = 0; i < t->n; i++)
        if (folder_says(&t->f[i]) && !(ms && folder_gone(ms, t->f[i].path))) t->f[k++] = t->f[i];
    t->n = k;
    if (t->n > FOLDERS_MAX) {
        qsort(t->f, (size_t)t->n, sizeof t->f[0], folders_by_age);
        t->n = FOLDERS_MAX;
    }
    return before - t->n;
}

// folders_write's part in state_rewrite: the file's table, with this window's changes and the
// housekeeping, then its lines.
typedef struct {
    Folders           *mine;
    const Mounts *ms;
} FoldersWrite;

static Folders folders_new;   // (the table as written: the file's, with this window's changes)

static bool folders_write_prepare(void *ctx, Arena *a, Str text, isize *room, Err *err)
{
    (void)err;
    FoldersWrite *fw = ctx;
    Folders *t = &folders_new;
    folders_parse(a, text, t);
    for (int i = 0; i < fw->mine->n; i++) {
        const FolderPrefs *m = &fw->mine->f[i];
        if (!m->changed) continue;
        FolderPrefs *f = folders_get(a, t, m->path);
        if (!f) break;
        if (m->changed & FP_VIEW) f->view = m->view;
        if (m->changed & FP_ZOOM) f->zoom = m->zoom;
        if (m->changed & FP_SORT) f->sort = m->sort;
        if (m->changed & FP_REV) f->rev = m->rev;
        if (m->changed & FP_SEL) f->sel = m->sel ? folder_str(a, str_from_c(m->sel)) : NULL;
        if ((m->changed & FP_AT) && m->at > f->at) f->at = m->at;
    }
    folders_tidy(t, fw->ms);
    *room = 64;
    for (int i = 0; i < t->n; i++) *room += 3 * (isize)(strlen(t->f[i].path) + (t->f[i].sel ? strlen(t->f[i].sel) : 0)) + 80;
    return true;
}

static bool folders_write_keep(void *ctx, Str text, Str line, Str *out)
{
    (void)ctx, (void)text, (void)out;
    Str w[7];
    char path[PATH_MAX];
    return !folder_line_read(line, w, path, sizeof path);   // (the folder lines ofx reads: written again from the table)
}

static isize folders_write_tail(void *ctx, char *out, isize cap)
{
    (void)ctx;
    isize n = 0;
    for (int i = 0; i < folders_new.n; i++) {
        int k = folder_line(&folders_new.f[i], out + n, (size_t)(cap - n));
        if (!k) return -1;   // (a folder line is never empty: 0 is one that doesn't fit)
        n += k;
    }
    return n;
}

// mine's changes into state file path (state_rewrite): the file read again, each entry's changed
// fields put into its line (a new line when there is none), the housekeeping done, every other line
// kept. mine is then the file's folders as written, nothing changed (another window's changes seen
// too), its strings in a: a is not the arena mine's strings are in now (the caller swaps two). ms:
// the mount list, for the housekeeping (NULL: no folder judged gone).
static bool folders_write(Arena *a, const char *path, Folders *mine, const Mounts *ms, Err *err)
{
    FoldersWrite fw = { mine, ms };
    StateEdit e = { folders_write_prepare, folders_write_keep, folders_write_tail, &fw, false };
    if (!state_rewrite(a, path, &e, err)) return false;
    *mine = folders_new;
    return true;
}

// Every entry's fields in mask (FP_VIEW and so on) unset, marked changed: a default in ofx.conf
// changed, and the choices made against the old one go (Michael, 2026-10-30). How many entries had
// one.
static int folders_wipe(Folders *t, u8 mask)
{
    int n = 0;
    for (int i = 0; i < t->n; i++) {
        FolderPrefs *f = &t->f[i];
        bool had = ((mask & FP_VIEW) && f->view >= 0) || ((mask & FP_ZOOM) && f->zoom > 0) || ((mask & FP_SORT) && f->sort >= 0) ||
                   ((mask & FP_REV) && f->rev >= 0);
        if (!had) continue;
        if (mask & FP_VIEW) f->view = -1;
        if (mask & FP_ZOOM) f->zoom = 0;
        if (mask & FP_SORT) f->sort = -1;
        if (mask & FP_REV) f->rev = -1;
        f->changed |= mask, n++;
    }
    return n;
}
