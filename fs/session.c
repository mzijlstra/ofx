// The tabs across a restart (F11 step 3, docs/plan.md "F11 design"; ofx.conf's restore-tabs): each
// window ofx closes for good leaves one line in the state file (state.c), beside the rest:
//
//   window <id> <closed, s> <tab shown> <folder %-encoded> <folder %-encoded> …
//
// The id is the window's process id. The last window closed leaves its line; Close All (Ctrl+Q)
// leaves one line a window (a window closed while others stay leaves none). The next start takes the
// newest line for itself (session_take) and asks whether to bring back the rest, each in a window of
// its own started on its id; a line stays until it is taken or forgotten (No), so one a window was
// just started on isn't lost to the first window closing before that one has read it.

#include "../base/base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SESSION_TABS = 32, SESSION_WINDOWS = 64 };   // (WINDOWS: the ids a start hands back at most)

typedef struct {
    long        id;
    i64         at;
    int         shown;                // the tab shown, an index into dir
    int         n;
    const char *dir[SESSION_TABS];    // in the arena given
} SessionWin;

// line's window into w, its folders in a; false when it isn't a window line ofx wrote.
static bool session_parse_line(Arena *a, Str line, SessionWin *w)
{
    if (!str_starts_with(line, S("window "))) return false;
    isize at = 7;
    Str f[3];
    for (int i = 0; i < 3; i++) f[i] = folder_word(line, &at);
    i64 id = folder_num(f[0], INT32_MAX), when = folder_num(f[1], INT64_MAX / 2), shown = folder_num(f[2], SESSION_TABS - 1);
    if (id <= 0 || when < 0 || shown < 0) return false;
    *w = (SessionWin){ .id = (long)id, .at = when, .shown = (int)shown };
    for (Str d; w->n < SESSION_TABS && (d = folder_word(line, &at)).len;) {
        char p[PATH_MAX];
        if (!percent_decode(d, p, sizeof p) || (p[0] != '/' && !str_starts_with(str_from_c(p), S("trash:")) && !str_starts_with(str_from_c(p), S("search:")))) continue;
        const char *s = folder_str(a, str_from_c(p));
        if (s) w->dir[w->n++] = s;
    }
    if (w->shown >= w->n) w->shown = 0;
    return w->n > 0;
}

// The window line for id (closed at, shown, dirs) into out, with its newline; 0 when it doesn't fit.
static int session_line(long id, i64 at, int shown, const char *const *dir, int n, char *out, size_t cap)
{
    int k = snprintf(out, cap, "window %ld %lld %d", id, (long long)at, shown);
    for (int i = 0; i < n && k > 0 && (size_t)k < cap; i++) {
        if ((size_t)k + 1 >= cap) return 0;
        out[k++] = ' ';
        if (!percent_encode(str_from_c(dir[i]), "/:", out + k, cap - (size_t)k)) return 0;
        k += (int)strlen(out + k);
    }
    if (k <= 0 || (size_t)k + 1 >= cap) return 0;
    out[k++] = '\n', out[k] = 0;
    return k;
}

// Window lines in a file: at most SESSION_KEEP, the newest by their closing time (reviewer-2,
// 2026-11-01: a question left unanswered each day, then Close All, piled up 80 of them).
enum { SESSION_KEEP = 8 };

// Whether window line l (closed at `at`) is among the newest SESSION_KEEP of text's window lines,
// counting extra (closed at extra_at) as one more when it is set.
static bool session_kept(Arena *a, Str text, i64 at, isize self, bool extra, i64 extra_at)
{
    int newer = extra && extra_at >= at;   // (the new line goes last: on a tie it is the newer)
    isize p = 0;
    for (Str l; next_line(text, &p, &l);) {
        SessionWin w;
        isize here = (isize)(l.p - text.p);
        if (here != self && session_parse_line(a, l, &w) && (w.at > at || (w.at == at && here > self))) newer++;
    }
    return newer < SESSION_KEEP;
}

// The state file's window lines changed, under its lock: line put at the end (a window writes its
// own once, as it closes), or (NULL) id's lines forgotten; the window lines past the newest
// SESSION_KEEP dropped; every other line kept.
static bool session_put(Arena *a, const char *path, long id, const char *line, Err *err)
{
    Str text;
    int lock = state_lock(path);
    if (!state_read(a, path, &text, err)) return state_unlock(lock), false;
    SessionWin mine;
    bool extra = line && session_parse_line(a, str_cut(str_from_c(line), 0, (isize)strlen(line) - 1), &mine);
    isize cap = text.len + (line ? (isize)strlen(line) : 0) + 2;
    char *body = arena_try_push(a, cap, 1);
    if (!body) return state_unlock(lock), err_set(err, "%s: too big", path), false;
    isize n = 0, at = 0;
    for (Str l; next_line(text, &at, &l);) {
        SessionWin w;
        if (!l.len) continue;
        if (session_parse_line(a, l, &w) && ((!line && w.id == id) || !session_kept(a, text, w.at, (isize)(l.p - text.p), extra, extra ? mine.at : 0))) continue;
        memcpy(body + n, l.p, (size_t)l.len), n += l.len, body[n++] = '\n';
    }
    if (line) memcpy(body + n, line, strlen(line)), n += (isize)strlen(line);
    bool ok = state_write(path, body, n, err);
    state_unlock(lock);
    return ok;
}

// A window's line taken from the state file, under its lock: id's, or (id 0) the newest of all. Into
// *w (its folders in a); the ids of the window lines left, in the file's order, into rest (*nrest, at
// most cap). False when there is none to take. Only the line taken goes (by its id and time: pids
// come round again after a reboot).
static bool session_take(Arena *a, const char *path, long id, SessionWin *w, long *rest, int cap, int *nrest, Err *err)
{
    Str text;
    *nrest = 0;
    int lock = state_lock(path);
    if (!state_read(a, path, &text, err)) return state_unlock(lock), false;
    bool found = false;
    isize pick = -1, at = 0;
    for (Str l; next_line(text, &at, &l);) {
        SessionWin c;
        if (session_parse_line(a, l, &c) && (id ? c.id == id && (!found || c.at > w->at) : !found || c.at > w->at))
            *w = c, found = true, pick = (isize)(l.p - text.p);
    }
    if (!found) return state_unlock(lock), false;
    char *body = arena_try_push(a, text.len + 1, 1);
    if (!body) return state_unlock(lock), err_set(err, "%s: too big", path), false;
    isize m = 0;
    at = 0;
    for (Str l; next_line(text, &at, &l);) {
        SessionWin c;
        if (!l.len || (isize)(l.p - text.p) == pick) continue;
        if (*nrest < cap && session_parse_line(a, l, &c)) rest[(*nrest)++] = c.id;
        memcpy(body + m, l.p, (size_t)l.len), m += l.len, body[m++] = '\n';
    }
    bool ok = state_write(path, body, m, err);
    state_unlock(lock);
    return ok;
}
