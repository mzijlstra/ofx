// The tabs across a restart (F11 steps 3 and 3b, docs/plan.md "F11 design"; ofx.conf's
// restore-tabs): each window keeps one line in the state file (state.c), beside the rest:
//
//   window <id> <time, s> <tab shown> <how> <folder %-encoded> <folder %-encoded> …
//
// The id is the window's process id; how is "closed", or "open:<boot>:<start>" while the window
// runs: its boot's id (/proc/sys/kernel/random/boot_id, its first 8 characters) and the process's
// start time (windows.c's win_started), so a line whose process is gone, or is another one now (a
// pid comes round again, a reboot), is a window that crashed or lost its power (step 3b, Michael,
// 2026-11-02). A line from before 3b, without how, is closed.
//
// A running window writes its line as its tabs change (at most every 2 s: gui/restore.c). Closed:
// the last window, Close All and a SIGTERM mark it closed; one of several closed while the others
// stay takes it out. A start with no other window running takes the newest line that isn't a running
// window's (session_take) and asks whether to bring back the rest; a line stays until it is taken or
// forgotten (No). At most SESSION_KEEP lines that aren't running windows' are kept.

#include "../base/base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SESSION_TABS = 32, SESSION_WINDOWS = 64, SESSION_KEEP = 8 };   // (WINDOWS: the lines a start hands back at most)

typedef struct {
    char boot[16];   // the boot's id, its first 8 characters
    i64  start;      // the process's start time (clock ticks since that boot)
} SessionLive;

typedef struct {
    long        id;
    i64         at;
    int         shown;                // the tab shown, an index into dir
    bool        open;                 // written by a running window (live), not closed
    SessionLive live;
    int         n;
    const char *dir[SESSION_TABS];    // in the arena given
} SessionWin;

typedef struct {
    long id;
    i64  at;
    bool crashed;   // its window's process ended without closing it
} SessionKey;

// This boot's id, its first 8 characters, into out; false without one (then no line is a running
// window's but this window's own).
static bool session_boot(char *out, size_t cap)
{
    char buf[64] = "";
    FILE *f = fopen("/proc/sys/kernel/random/boot_id", "re");
    bool ok = f && fgets(buf, sizeof buf, f) && strlen(buf) >= 8 && cap > 8;
    if (f) fclose(f);
    if (ok) memcpy(out, buf, 8), out[8] = 0;
    return ok;
}

static i64 win_started(pid_t pid);

// Whether w is a window running now: written open in this boot, by the process at its id that
// started when it did.
static bool session_alive(const SessionWin *w, const char *boot)
{
    return w->open && boot && strcmp(w->live.boot, boot) == 0 && win_started((pid_t)w->id) == w->live.start;
}

// Whether w is this window's own line (id, and me its live mark).
static bool session_mine(const SessionWin *w, long id, const SessionLive *me)
{
    return me && w->open && w->id == id && strcmp(w->live.boot, me->boot) == 0 && w->live.start == me->start;
}

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
    isize was = at;
    Str how = folder_word(line, &at);
    if (str_starts_with(how, S("open:"))) {   // open:<boot>:<start>
        isize c = str_find(str_cut(how, 5, how.len), ':');
        i64 start = c >= 0 ? folder_num(str_cut(how, 5 + c + 1, how.len), INT64_MAX / 2) : -1;
        if (c < 1 || c >= (isize)sizeof w->live.boot || start < 0) return false;
        snprintf(w->live.boot, sizeof w->live.boot, "%.*s", (int)c, how.p + 5);
        w->live.start = start, w->open = true;
    } else if (!str_eq(how, S("closed"))) {
        at = was;   // (a line from before 3b: its first folder)
    }
    for (Str d; w->n < SESSION_TABS && (d = folder_word(line, &at)).len;) {
        char p[PATH_MAX];
        if (!percent_decode(d, p, sizeof p) || (p[0] != '/' && !str_starts_with(str_from_c(p), S("trash:")) && !str_starts_with(str_from_c(p), S("search:")))) continue;
        const char *s = folder_str(a, str_from_c(p));
        if (s) w->dir[w->n++] = s;
    }
    if (w->shown >= w->n) w->shown = 0;
    return w->n > 0;
}

// The window line for id (its time, shown, live: NULL when closed, dirs) into out, with its newline;
// 0 when it doesn't fit.
static int session_line(long id, i64 at, int shown, const SessionLive *live, const char *const *dir, int n, char *out, size_t cap)
{
    int k = live ? snprintf(out, cap, "window %ld %lld %d open:%s:%lld", id, (long long)at, shown, live->boot, (long long)live->start)
                 : snprintf(out, cap, "window %ld %lld %d closed", id, (long long)at, shown);
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

// Whether line l of text (at its place self, written at `at`) is among the newest SESSION_KEEP of
// text's window lines that aren't running windows' (those are always kept), counting a new line
// (written at extra_at) as one more when extra.
static bool session_kept(Arena *a, Str text, i64 at, isize self, bool extra, i64 extra_at, const char *boot)
{
    int newer = extra && extra_at >= at;   // (the new line goes last: on a tie it is the newer)
    isize p = 0;
    for (Str l; next_line(text, &p, &l);) {
        SessionWin w;
        isize here = (isize)(l.p - text.p);
        if (here != self && session_parse_line(a, l, &w) && !session_alive(&w, boot) && (w.at > at || (w.at == at && here > self))) newer++;
    }
    return newer < SESSION_KEEP;
}

// The state file's window lines changed, under its lock: this window's own line (id, me) taken out
// and line (NULL: none) put at the end; or, me NULL, the line of window key forgotten. Then the
// lines that aren't running windows' past the newest SESSION_KEEP dropped (reviewer-2, 2026-11-01:
// a question left unanswered each day, then Close All, piled up 80). Every other line kept.
static bool session_edit(Arena *a, const char *path, long id, const SessionLive *me, const SessionKey *key, const char *line, Err *err)
{
    Str text;
    char boot[16] = "";
    session_boot(boot, sizeof boot);
    int lock = state_lock(path);
    if (!state_read(a, path, &text, err)) return state_unlock(lock), false;
    SessionWin mine;
    bool extra = line && session_parse_line(a, str_cut(str_from_c(line), 0, (isize)strlen(line) - 1), &mine) && !mine.open;   // (a running window's line isn't counted)
    isize cap = text.len + (line ? (isize)strlen(line) : 0) + 2;
    char *body = arena_try_push(a, cap, 1);
    if (!body) return state_unlock(lock), err_set(err, "%s: too big", path), false;
    isize n = 0, at = 0;
    for (Str l; next_line(text, &at, &l);) {
        SessionWin w;
        if (!l.len) continue;
        if (session_parse_line(a, l, &w) &&
            (session_mine(&w, id, me) || (key && w.id == key->id && w.at == key->at) ||
             (!session_alive(&w, boot) && !session_kept(a, text, w.at, (isize)(l.p - text.p), extra, extra ? mine.at : 0, boot))))
            continue;
        memcpy(body + n, l.p, (size_t)l.len), n += l.len, body[n++] = '\n';
    }
    if (line) memcpy(body + n, line, strlen(line)), n += (isize)strlen(line);
    bool ok = state_write(path, body, n, err);
    state_unlock(lock);
    return ok;
}

static bool session_put(Arena *a, const char *path, long id, const SessionLive *me, const char *line, Err *err)
{
    return session_edit(a, path, id, me, NULL, line, err);
}

static bool session_forget(Arena *a, const char *path, SessionKey key, Err *err)
{
    return session_edit(a, path, 0, NULL, &key, NULL, err);
}

// A window's line taken from the state file, under its lock: id's, or (id 0) the newest of all, but
// never a running window's. Into *w (its folders in a); the other lines that aren't running windows',
// in the file's order, into rest (*nrest, at most cap). False when there is none to take. Only the
// line taken goes, by its place (pids come round again).
static bool session_take(Arena *a, const char *path, long id, SessionWin *w, SessionKey *rest, int cap, int *nrest, Err *err)
{
    Str text;
    char boot[16] = "";
    session_boot(boot, sizeof boot);
    *nrest = 0;
    int lock = state_lock(path);
    if (!state_read(a, path, &text, err)) return state_unlock(lock), false;
    bool found = false;
    isize pick = -1, at = 0;
    for (Str l; next_line(text, &at, &l);) {
        SessionWin c;
        if (session_parse_line(a, l, &c) && !session_alive(&c, boot) && (id ? c.id == id && (!found || c.at > w->at) : !found || c.at > w->at))
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
        if (*nrest < cap && session_parse_line(a, l, &c) && !session_alive(&c, boot)) rest[(*nrest)++] = (SessionKey){ c.id, c.at, c.open };
        memcpy(body + m, l.p, (size_t)l.len), m += l.len, body[m++] = '\n';
    }
    bool ok = state_write(path, body, m, err);
    state_unlock(lock);
    return ok;
}
