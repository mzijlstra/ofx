// What ofx remembers by itself across a restart (docs/plan.md, F6 "Remembered"): the sidebar's
// mode and the files' view (list or icons, F6.2), in $XDG_STATE_HOME/ofx/state (~/.local/state by
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
    State s = { SIDE_PLACES, false };
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
        if (!str_eq(key, S("sidebar"))) continue;
        for (int k = 0; k < SIDE_MODES; k++)
            if (str_eq(val, str_from_c(SIDE_MODE_NAMES[k]))) s.side = (SideMode)k;
    }
    arena_restore(scratch, mark);
    return s;
}

// s into file path, its folder made first: written beside it and renamed over it, so a crash
// leaves the old file or the new, never half of one.
static bool state_save(const char *path, const State *s, Err *err)
{
    if (!ops_allowed(path, err)) return false;
    Str d = path_dir(str_from_c(path));
    char dir[PATH_MAX], tmp[PATH_MAX + 32];
    snprintf(dir, sizeof dir, "%.*s", STR_ARG(d.len ? d : S("/")));
    if (!mkdir_private(dir, err)) return false;
    snprintf(tmp, sizeof tmp, "%s.%d.tmp", path, (int)getpid());
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return err_set(err, "%s: %s", tmp, strerror(errno)), false;
    char body[128];
    int n = snprintf(body, sizeof body, "sidebar=%s\nview=%s\n", SIDE_MODE_NAMES[s->side < SIDE_MODES ? s->side : SIDE_PLACES],
                     s->icons ? "icons" : "list");
    bool ok = write(fd, body, (size_t)n) == n;
    ok &= close(fd) == 0;
    if (!ok || rename(tmp, path) != 0) {
        int e = errno;
        unlink(tmp);
        return err_set(err, "%s: %s", path, ok ? strerror(e) : "cannot write it"), false;
    }
    return true;
}
