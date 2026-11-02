// What ofx remembers by itself across a restart (docs/plan.md, F6 "Remembered", F11), in
// $XDG_STATE_HOME/ofx/state (~/.local/state by default), which ofx writes: the sidebar's mode, the
// View menu's toggles, Show Hidden Files, the tree's open folders (tree=), per folder its view, icon
// size, sort and cursor (fs/folders.c's "folder …" lines), and each window's tabs (fs/session.c's
// "window …" lines). Options Michael sets go in $XDG_CONFIG_HOME/ofx/ofx.conf (fs/conf.c), which
// ofx only writes when it isn't there.
//
// A line each: `key=value`, or a `folder` or `window` line. A line a reader doesn't know, or can't
// read, is passed over when read and kept as it is when written (state_rewrite).

#include "../base/base.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum { SIDE_PLACES, SIDE_TREE, SIDE_HIDDEN, SIDE_MODES } SideMode;

static const char *const SIDE_MODE_NAMES[SIDE_MODES] = { "places", "tree", "hidden" };

typedef struct {
    SideMode side;
    bool     hidden;     // Show Hidden Files (F11): "hidden=on"
    char     folder_defaults[64];   // ofx.conf's view defaults the folders' choices were made against (F11)
    bool     no_bar;     // the menu bar hidden (2026-10-27): "menubar=off"
    bool     no_perms;   // the list's Owner and Permissions columns left out (2026-10-27): "perms=off"
    bool     group;      // and Group shown between them (2026-10-27, off by default as eza's): "group=on"
    bool     no_colors;  // the list's colours off (2026-10-27): "colors=off"
} State;

// $XDG_STATE_HOME/ofx/state, else ~/.local/state/ofx/state; false when neither is set.
static bool state_path(char *out, size_t cap)
{
    return xdg_dir(out, cap, "XDG_STATE_HOME", ".local/state", "ofx/state");
}

// The state in file path; the defaults for what it doesn't say (or no file at all).
static State state_load(Arena *scratch, const char *path)
{
    State s = { .side = SIDE_PLACES };
    ArenaMark mark = arena_mark(scratch);
    Str text = read_file(scratch, path);
    isize at = 0;
    for (Str line; next_line(text, &at, &line);) {
        line = str_trim(line);
        isize eq = str_find(line, '=');
        if (eq < 0) continue;
        Str key = str_trim(str_cut(line, 0, eq)), val = str_trim(str_cut(line, eq + 1, line.len));
        if (str_eq(key, S("hidden"))) s.hidden = str_eq(val, S("on"));
        if (str_eq(key, S("folderdefaults")) && val.len < (isize)sizeof s.folder_defaults) snprintf(s.folder_defaults, sizeof s.folder_defaults, "%.*s", STR_ARG(val));
        if (str_eq(key, S("perms"))) s.no_perms = str_eq(val, S("off"));
        if (str_eq(key, S("menubar"))) s.no_bar = str_eq(val, S("off"));
        if (str_eq(key, S("colors"))) s.no_colors = str_eq(val, S("off"));
        if (str_eq(key, S("group"))) s.group = str_eq(val, S("on"));
        if (!str_eq(key, S("sidebar"))) continue;
        for (int k = 0; k < SIDE_MODES; k++)
            if (str_eq(val, str_from_c(SIDE_MODE_NAMES[k]))) s.side = (SideMode)k;
    }
    arena_restore(scratch, mark);
    return s;
}

// key's value in file path, in a; STR_EMPTY when it isn't there (F11: tree=, too long for State).
static Str state_value(Arena *a, const char *path, const char *key)
{
    Str text = read_file(a, path), k = str_from_c(key), v = STR_EMPTY;
    isize at = 0;
    for (Str line; next_line(text, &at, &line);) {
        isize eq = str_find(line, '=');
        if (eq >= 0 && str_eq(str_trim(str_cut(line, 0, eq)), k)) v = str_trim(str_cut(line, eq + 1, line.len));
    }
    return v;
}

// body (n bytes) as file path, its folder made first: written beside it and renamed over it, so a
// crash leaves the old file or the new, never half of one.
static bool state_write(const char *path, const char *body, isize n, bool sync, Err *err)
{
    if (!ops_allowed(path, err)) return false;
    char dir[PATH_MAX], tmp[PATH_MAX + 32];
    if (!path_parent(dir, sizeof dir, path) || !mkdir_private(dir, err)) return false;
    snprintf(tmp, sizeof tmp, "%s.%d.tmp", path, (int)getpid());
    // The temp file made new, never one there followed (a link at it would have been truncated past
    // the guard; it is beside path, which the guard allowed); one a crash of this pid left is taken
    // out first. Synced before the rename (sync: all but a window's 2 s live writes, whose sync could
    // stall the window behind a big write on btrfs; a review), so after a power cut the file is the
    // old or the new, whole (F11 review, 2026-11-02).
    unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return err_set(err, "%s: %s", tmp, strerror(errno)), false;
    bool ok = write(fd, body, (size_t)n) == n;
    if (sync) ok &= fsync(fd) == 0;
    ok &= close(fd) == 0;
    if (!ok || rename(tmp, path) != 0) {
        int e = errno;
        unlink(tmp);
        return err_set(err, "%s: %s", path, ok ? strerror(e) : "cannot write it"), false;
    }
    return true;
}

// The state file's text into *text, in a: empty when there is no file. False, said, when there is one
// but it couldn't be read whole (too big for a): a write from what was read would lose the rest
// (review, 2026-11-01).
static bool state_read(Arena *a, const char *path, Str *text, Err *err)
{
    struct stat st;
    *text = read_file(a, path);
    if (text->len == 0 && stat(path, &st) == 0 && st.st_size > 0) return err_set(err, "%s: couldn't read it whole, so not written", path), false;
    return true;
}

// The state file's lock, held from the read to the rename by each writer (state_rewrite's callers:
// state_put, folders_write, session_edit, session_take):
// with Ctrl+Q every window writes at once, and a write from an old read loses another's change
// (review, 2026-11-01: 3 at once lost 112 of 600). A file of its own, <state>.lock: the rename
// replaces the state file's inode. The fd to close, -1 when it can't be had (then written unlocked).
static int state_lock(const char *path)
{
    char lock[PATH_MAX + 8], dir[PATH_MAX];
    Err err;
    if (snprintf(lock, sizeof lock, "%s.lock", path) >= (int)sizeof lock || !ops_allowed(lock, &err) || !path_parent(dir, sizeof dir, path) ||
        !mkdir_private(dir, &err))
        return -1;
    int fd = open(lock, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd >= 0 && flock(fd, LOCK_EX) != 0) close(fd), fd = -1;
    return fd;
}

static void state_unlock(int fd)
{
    if (fd >= 0) close(fd);
}

// A rewrite of the state file, the one way every writer changes it (state_put, folders_write,
// session_edit, session_take; F11 review, 2026-11-02: four had their own copy, each with its own
// rule for a line it couldn't read): under its lock, read whole (refused when it can't be), then
// prepare (what this writer reads from it, and the room its new lines need), each line kept as it is,
// replaced, or dropped by keep (NULL: kept), and tail's lines at the end. One rule for them all: a
// line a writer doesn't own, or can't read, stays as it is. Its strings stay in a for the caller.
typedef struct {
    bool  (*prepare)(void *ctx, Arena *a, Str text, isize *room, Err *err);   // false: no write (err said, or nothing to do)
    bool  (*keep)(void *ctx, Str text, Str line, Str *out);                   // false: dropped; *out (line) may be replaced
    isize (*tail)(void *ctx, char *out, isize cap);                           // what goes at the end: its length, -1 when it doesn't fit
    void  *ctx;
    bool   nosync;   // written without the sync (a window's live tab line, every 2 s: session_edit)
} StateEdit;

static bool state_rewrite(Arena *a, const char *path, const StateEdit *e, Err *err)
{
    Str text;
    isize room = 0;
    int lock = state_lock(path);
    bool ok = state_read(a, path, &text, err) && (!e->prepare || e->prepare(e->ctx, a, text, &room, err));
    char *body = ok ? arena_try_push(a, text.len + room + 2, 1) : NULL;
    if (ok && !body) err_set(err, "%s: too big", path), ok = false;
    isize n = 0, at = 0;
    for (Str line; ok && next_line(text, &at, &line);) {
        Str out = line;
        if (!line.len || (e->keep && !e->keep(e->ctx, text, line, &out))) continue;
        if (n + out.len + 1 > text.len + room + 2) {   // (a replacement past its room: no write, never a file cut short)
            err_set(err, "%s: too big", path), ok = false;
            break;
        }
        memcpy(body + n, out.p, (size_t)out.len), n += out.len, body[n++] = '\n';
    }
    isize t = ok && e->tail ? e->tail(e->ctx, body + n, text.len + room + 2 - n) : 0;
    if (t < 0) err_set(err, "%s: too big", path), ok = false;   // (the new lines past their room: the same)
    ok = ok && state_write(path, body, n + t, !e->nosync, err);
    state_unlock(lock);
    return ok;
}

// One key's value into file path, the rest of it as it is now (F7.1): each window is its own
// process (New Window), so a save of the whole State would put back what another window changed
// since this one read the file. The key's line replaced where it is (or added at the end), said
// twice once, every other line kept, a key ofx doesn't know too.
typedef struct {
    Str  key;
    Str  line;   // "key=value"
    bool put;
} StatePut;

static bool state_put_prepare(void *ctx, Arena *a, Str text, isize *room, Err *err)
{
    (void)a, (void)text, (void)err;
    *room = ((StatePut *)ctx)->line.len + 1;
    return true;
}

static bool state_put_keep(void *ctx, Str text, Str line, Str *out)
{
    (void)text;
    StatePut *p = ctx;
    isize eq = str_find(line, '=');
    if (eq < 0 || !str_eq(str_trim(str_cut(line, 0, eq)), p->key)) return true;
    if (p->put) return false;   // (a key said twice: once)
    *out = p->line, p->put = true;
    return true;
}

static isize state_put_tail(void *ctx, char *out, isize cap)
{
    StatePut *p = ctx;
    if (p->put) return 0;
    if (p->line.len + 1 > cap) return -1;
    memcpy(out, p->line.p, (size_t)p->line.len), out[p->line.len] = '\n';
    return p->line.len + 1;
}

static bool state_put(Arena *scratch, const char *path, const char *key, const char *value, Err *err)
{
    ArenaMark mark = arena_mark(scratch);
    char *line = arena_try_push(scratch, (isize)(strlen(key) + strlen(value) + 2), 1);
    if (!line) return err_set(err, "%s: too big", path), false;
    sprintf(line, "%s=%s", key, value);
    StatePut p = { str_from_c(key), str_from_c(line), false };
    StateEdit e = { state_put_prepare, state_put_keep, state_put_tail, &p, false };
    bool ok = state_rewrite(scratch, path, &e, err);
    arena_restore(scratch, mark);
    return ok;
}
