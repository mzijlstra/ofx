// What ofx remembers by itself across a restart (docs/plan.md, F6 "Remembered", F11), in
// $XDG_STATE_HOME/ofx/state (~/.local/state by default), which ofx writes: the sidebar's mode, the
// View menu's toggles, Show Hidden Files, and per folder its view, icon size, sort and cursor
// (fs/folders.c's "folder …" lines). Options Michael sets go in $XDG_CONFIG_HOME/ofx/ofx.conf
// (fs/conf.c), which ofx only writes when it isn't there.
//
// One `key=value` a line; a key ofx doesn't know, or a value it doesn't, is left out.

#include "../base/base.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
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

// body (n bytes) as file path, its folder made first: written beside it and renamed over it, so a
// crash leaves the old file or the new, never half of one.
static bool state_write(const char *path, const char *body, isize n, Err *err)
{
    if (!ops_allowed(path, err)) return false;
    Str d = path_dir(str_from_c(path));
    char dir[PATH_MAX], tmp[PATH_MAX + 32];
    snprintf(dir, sizeof dir, "%.*s", STR_ARG(d.len ? d : S("/")));
    if (!mkdir_private(dir, err)) return false;
    snprintf(tmp, sizeof tmp, "%s.%d.tmp", path, (int)getpid());
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return err_set(err, "%s: %s", tmp, strerror(errno)), false;
    bool ok = write(fd, body, (size_t)n) == n;
    ok &= close(fd) == 0;
    if (!ok || rename(tmp, path) != 0) {
        int e = errno;
        unlink(tmp);
        return err_set(err, "%s: %s", path, ok ? strerror(e) : "cannot write it"), false;
    }
    return true;
}

// One key's value into file path, the rest of it as it is now (F7.1): each window is its own
// process (New Window), so a save of the whole State would put back what another window changed
// since this one read the file. The file is read again here, the key's line replaced (or added),
// every other line kept, a key ofx doesn't know too.
static bool state_put(Arena *scratch, const char *path, const char *key, const char *value, Err *err)
{
    ArenaMark mark = arena_mark(scratch);
    Str text = read_file(scratch, path), k = str_from_c(key);
    isize cap = text.len + (isize)strlen(key) + (isize)strlen(value) + 4;
    char *body = arena_try_push(scratch, cap, 1);
    if (!body) return arena_restore(scratch, mark), err_set(err, "%s: too big", path), false;
    isize n = 0, at = 0;
    bool put = false;
    for (Str line; next_line(text, &at, &line);) {
        isize eq = str_find(line, '=');
        bool mine = eq >= 0 && str_eq(str_trim(str_cut(line, 0, eq)), k);
        if (mine && put) continue;   // a key said twice: once
        if (mine) n += snprintf(body + n, (size_t)(cap - n), "%s=%s\n", key, value), put = true;
        else if (line.len) memcpy(body + n, line.p, (size_t)line.len), n += line.len, body[n++] = '\n';
    }
    if (!put) n += snprintf(body + n, (size_t)(cap - n), "%s=%s\n", key, value);
    bool ok = state_write(path, body, n, err);
    arena_restore(scratch, mark);
    return ok;
}
