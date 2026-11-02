// The tabs across a restart (F11 steps 3 and 3b, fs/session.c; ofx.conf's restore-tabs): this
// window's line kept as its tabs change (2 s after a change at most, so a crash or a power cut loses
// little), closed or taken out as the window closes, read as one starts, and the question whether
// the other windows come back, each started on its line's id (--restore). Included by main.c,
// after actions.c.

static bool closing_all;   // this window closes by Close All, its own Ctrl+Q or another's, or a SIGTERM (its tabs kept)

enum { RESTORE_WRITE_MS = 2000 };   // a change written this long after it at most (Michael, 2026-11-02)

static struct {
    SessionKey  keys[SESSION_WINDOWS];   // the lines left after this start took its own
    int         n;
    bool        crashed;                 // one of them a window's that ended without closing
    SessionLive me;                      // this window's live mark
    bool        have_me, wrote;
    u64         last;                    // the hash of the line last written past its time: the tabs, the one shown
    i64         due;                     // the write waiting, 0 none
} restore;

// This window's tabs (the one shown as app has it) as its line, live or closed, into out; its length,
// 0 when it doesn't fit.
static int restore_line(bool live, char *out, size_t cap)
{
    const char *dir[SESSION_TABS];
    int n = 0;
    for (int i = 0; i < tabbar.n && n < SESSION_TABS; i++) dir[n++] = i == tabbar.cur ? app.cwd : tabs[tabbar.slot[i]].dir;
    return session_line((long)getpid(), (i64)time(NULL), tabbar.cur, live && restore.have_me ? &restore.me : NULL, dir, n, out, cap);
}

// The room this window's line needs: its fields, each tab's folder %-encoded (3 bytes a byte at
// most) and a space (reviewer-2, 2026-11-02: SESSION_TABS × PATH_MAX × 3 on the scratch arena left
// a state file over 330 KiB no room to be read).
static size_t restore_cap(void)
{
    size_t cap = 160;
    for (int i = 0; i < tabbar.n && i < SESSION_TABS; i++) cap += 3 * strlen(i == tabbar.cur ? app.cwd : tabs[tabbar.slot[i]].dir) + 1;
    return cap;
}

// The hash (FNV-1a) of a line past "window <id> <time>": what changes with the tabs; 0 for none.
static u64 restore_tail(const char *line)
{
    const char *p = strchr(line + 7, ' ');
    p = p ? strchr(p + 1, ' ') : NULL;
    u64 h = 1469598103934665603ull;
    for (; p && *p; p++) h = (h ^ (u8)*p) * 1099511628211ull;
    return p ? h : 0;
}

// The live line written now, as the tabs are, when they differ from the last one written.
static void restore_write_in(void)
{
    char path[PATH_MAX];
    Err err;
    restore.due = 0;
    if (!app.conf.restore_tabs || !restore.have_me || !state_path(path, sizeof path)) return;
    size_t cap = restore_cap();
    char *line = arena_try_push(&scratch, (isize)cap, 1);
    if (!line || !restore_line(true, line, cap)) return;
    u64 tail = restore_tail(line);   // (restore_tick asks for a write only when it changed)
    if (!tail) return;
    if (!session_put(&scratch, path, (long)getpid(), &restore.me, line, &err)) {
        fprintf(stderr, "ofx: the tabs not kept: %s\n", err.msg);
        return;
    }
    restore.last = tail, restore.wrote = true;
}

static void restore_write(void)
{
    ArenaMark mark = arena_mark(&scratch);   // (what it read and wrote there given back: a state file of 420 KiB, twice, would fill it)
    restore_write_in();
    arena_restore(&scratch, mark);
}

// After each frame's input: a change of the tabs (or of the folder shown) since the last write is
// written RESTORE_WRITE_MS after it. The due time for the timer, 0 for none.
static i64 restore_tick(i64 now)
{
    if (!app.conf.restore_tabs || !restore.have_me) return 0;
    if (restore.due && now >= restore.due) restore_write();
    if (restore.due) return restore.due;
    static char line[SESSION_TABS * PATH_MAX * 3 + 96];   // (as restore_write's: the same tabs fit both)
    u64 tail = restore_line(true, line, sizeof line) ? restore_tail(line) : 0;
    if (tail && tail != restore.last) restore.due = now + RESTORE_WRITE_MS;
    return restore.due;
}

// As this window closes for good: its line closed (the last window, Close All, a SIGTERM), or taken
// out when other windows stay open (as a browser forgets a window closed then).
static void restore_save_in(void)
{
    char path[PATH_MAX];
    Err err;
    if (!state_path(path, sizeof path)) return;
    int others = app.windows[0] ? win_signal_others(app.windows, getpid(), 0) : 0;
    const SessionLive *me = restore.have_me ? &restore.me : NULL;
    if (!app.conf.restore_tabs || (others > 0 && !closing_all)) {
        if (restore.wrote && !session_put(&scratch, path, (long)getpid(), me, NULL, &err)) fprintf(stderr, "ofx: %s\n", err.msg);
        return;
    }
    size_t cap = restore_cap();   // (restore_line takes the tab shown from app)
    char *line = arena_try_push(&scratch, (isize)cap, 1);
    if (!line || !restore_line(false, line, cap) || !session_put(&scratch, path, (long)getpid(), me, line, &err))
        fprintf(stderr, "ofx: the tabs not kept: %s\n", line ? err.msg : "no memory");
}

static void restore_save(void)
{
    ArenaMark mark = arena_mark(&scratch);   // (what it read and wrote there given back: a state file of 420 KiB, twice, would fill it)
    restore_save_in();
    arena_restore(&scratch, mark);
}

// As ofx starts, after the first folder is shown: this window's live mark; then a window's tabs
// (id's, from --restore; else, with no other window running, the newest line's: closed, or one whose
// window crashed), the first in this tab and the others behind it, the one shown then as it was.
// plain: started on home (or nothing): the restored ones alone; else the folder asked stays shown, a
// tab more. With other lines left, the question. (restore_start then writes this window's own line.)
static void restore_start_in(long id, bool plain)
{
    char path[PATH_MAX];
    Err err;
    SessionWin w;
    i64 start = win_started(getpid());
    restore.have_me = session_boot(restore.me.boot, sizeof restore.me.boot) && start >= 0;
    restore.me.start = start;
    if (!app.conf.restore_tabs || !state_path(path, sizeof path)) return;
    bool others = !id && app.windows[0] && win_signal_others(app.windows, getpid(), 0) > 0;   // (an extra window: nothing to bring back)
    if (!others && session_take(&scratch, path, id, &w, restore.keys, SESSION_WINDOWS, &restore.n, &err)) {
        // What the start said (a bad line in ofx.conf, no inotify, the folders' defaults forgotten)
        // stays: opening the tabs would clear it (F11 review, 2026-11-02).
        char said[sizeof app.status];
        bool said_bad = app.status_bad;
        snprintf(said, sizeof said, "%s", app.status);
        if (plain) go_to_in(w.dir[0], STR_EMPTY, 0, true);   // (stepping: Back from it isn't the start folder)
        for (int i = w.n - 1; i >= (plain ? 1 : 0); i--) tab_open(w.dir[i], STR_EMPTY, false);   // (last first: each goes right after the tab shown, so they end in their order; F11 review)
        if (plain) tab_show(w.shown);
        snprintf(app.status, sizeof app.status, "%s", said), app.status_bad = said_bad;   // (not "Opened … in a new tab" for each)
        for (int i = 0; i < restore.n; i++) restore.crashed |= restore.keys[i].crashed;
        if (!id && restore.n > 0) app.ui.restore_n = restore.n;
    }
}

static void restore_start(long id, bool plain)
{
    ArenaMark mark = arena_mark(&scratch);   // (what it read and wrote there given back: a state file of 420 KiB, twice, would fill it)
    restore_start_in(id, plain);
    arena_restore(&scratch, mark);
    restore_write();   // (this window's own line at once, after what the take read is given back)
}

// restore-tabs turned off while this window runs: its line out at once, so a crash later doesn't
// leave one a start long after (the option on again) would bring back (reviewer-2, 2026-11-02).
static void restore_off_in(void)
{
    char path[PATH_MAX];
    Err err;
    if (restore.wrote && state_path(path, sizeof path) && session_put(&scratch, path, (long)getpid(), restore.have_me ? &restore.me : NULL, NULL, &err))
        restore.wrote = false, restore.last = 0, restore.due = 0;
}

static void restore_off(void)
{
    ArenaMark mark = arena_mark(&scratch);   // (what it read and wrote there given back: a state file of 420 KiB, twice, would fill it)
    restore_off_in();
    arena_restore(&scratch, mark);
}

// The question's answer: yes, each other window started on its line (this binary, --restore id,
// through uwsm-app as every launch); no, their lines forgotten.
static void restore_others_in(bool yes)
{
    char path[PATH_MAX];
    Err err;
    for (int i = 0; i < restore.n; i++) {
        char id[24];
        snprintf(id, sizeof id, "%ld", restore.keys[i].id);
        const char *cmd[] = { app.exe, "--restore", id };
        if (yes ? !spawn_app(cmd, COUNT(cmd), &err) : state_path(path, sizeof path) && !session_forget(&scratch, path, restore.keys[i], &err))
            set_status(true, "%s", err.msg);
    }
    restore.n = 0;
}

static void restore_others(bool yes)
{
    ArenaMark mark = arena_mark(&scratch);   // (what it read and wrote there given back: a state file of 420 KiB, twice, would fill it)
    restore_others_in(yes);
    arena_restore(&scratch, mark);
}
