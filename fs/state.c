// What ofx remembers by itself across a restart (docs/plan.md, F6 "Remembered"): the sidebar's
// mode, the files' view (list or icons, F6.2) and the icon view's size (F6.3), in $XDG_STATE_HOME/ofx/state (~/.local/state by
// default), which ofx writes. Options Michael sets go in $XDG_CONFIG_HOME/ofx/ofx.conf, which ofx
// only reads (none yet).
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
    bool     icons;   // the files as icons, not the list (F6.2)
    int      zoom;    // the icon view's icon size in px (F6.3); 0: not said, the default
    bool     no_perms;   // the list's Owner, Group and Permissions columns left out (2026-10-27): "perms=off"
} State;

// $XDG_STATE_HOME/ofx/state, else ~/.local/state/ofx/state; false when neither is set.
static bool state_path(char *out, size_t cap)
{
    const char *x = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
    int n;
    if (x && x[0] == '/') n = snprintf(out, cap, "%s/ofx/state", x);
    else if (home && home[0]) n = snprintf(out, cap, "%s/.local/state/ofx/state", home);
    else return false;
    return n >= 0 && (size_t)n < cap;
}

// The state in file path; the defaults for what it doesn't say (or no file at all).
static State state_load(Arena *scratch, const char *path)
{
    State s = { SIDE_PLACES, false, 0, false };
    ArenaMark mark = arena_mark(scratch);
    Str text = read_file(scratch, path);
    isize at = 0;
    for (Str line; next_line(text, &at, &line);) {
        line = str_trim(line);
        isize eq = str_find(line, '=');
        if (eq < 0) continue;
        Str key = str_trim(str_cut(line, 0, eq)), val = str_trim(str_cut(line, eq + 1, line.len));
        if (str_eq(key, S("view"))) {
            if (str_eq(val, S("icons"))) s.icons = true;
            else if (str_eq(val, S("list"))) s.icons = false;
        }
        if (str_eq(key, S("zoom")) && val.len <= 4) {
            int px = 0;
            isize i = 0;
            while (i < val.len && is_digit(val.p[i])) px = px * 10 + (val.p[i++] - '0');
            if (i == val.len && px > 0) s.zoom = px;
        }
        if (str_eq(key, S("perms"))) s.no_perms = str_eq(val, S("off"));
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

// All of s into file path.
static bool state_save(const char *path, const State *s, Err *err)
{
    char body[128];
    int n = snprintf(body, sizeof body, "sidebar=%s\nview=%s\n", SIDE_MODE_NAMES[s->side < SIDE_MODES ? s->side : SIDE_PLACES],
                     s->icons ? "icons" : "list");
    if (s->zoom > 0) n += snprintf(body + n, sizeof body - (size_t)n, "zoom=%d\n", s->zoom);
    if (s->no_perms) n += snprintf(body + n, sizeof body - (size_t)n, "perms=off\n");
    return state_write(path, body, n, err);
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
