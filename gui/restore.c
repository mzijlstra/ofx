// The tabs across a restart (F11 step 3, fs/session.c; ofx.conf's restore-tabs): written as a window
// closes for good, read as one starts, and after Close All the question whether the other windows
// come back, each started on its line's id (--restore). Included by main.c, after actions.c.

static bool closing_all;   // this window closes by Close All, its own Ctrl+Q or another's (its tabs kept)

static struct {
    long ids[SESSION_WINDOWS];   // the window lines left after this start took its own
    int  n;
} restore;

// As this window closes for good: its tabs, unless other windows stay open (then they are forgotten,
// as a browser does) and it isn't Close All.
static void restore_save(void)
{
    char path[PATH_MAX];
    Err err;
    if (!app.conf.restore_tabs || !state_path(path, sizeof path)) return;
    int others = app.windows[0] ? win_signal_others(app.windows, getpid(), 0) : 0;
    if (others > 0 && !closing_all) return;
    tab_save();
    const char *dir[SESSION_TABS];
    int n = 0;
    for (int i = 0; i < tabbar.n && n < SESSION_TABS; i++) dir[n++] = tabs[tabbar.slot[i]].dir;
    char *line = arena_try_push(&scratch, (isize)n * PATH_MAX * 3 + 64, 1);
    if (!line || !session_line((long)getpid(), (i64)time(NULL), tabbar.cur, dir, n, line, (size_t)n * PATH_MAX * 3 + 64) ||
        !session_put(&scratch, path, (long)getpid(), line, &err))
        fprintf(stderr, "ofx: the tabs not kept: %s\n", line ? err.msg : "no memory");
}

// As ofx starts, after the first folder is shown: a window's tabs (id's, from --restore; else the
// newest line's), the first in this tab and the others behind it, the one shown then as it was.
// plain: started on home (or nothing): the restored ones alone; else the folder asked stays shown,
// a tab more. With other lines left (Close All), the question.
static void restore_start(long id, bool plain)
{
    char path[PATH_MAX];
    Err err;
    SessionWin w;
    if (!app.conf.restore_tabs || !state_path(path, sizeof path)) return;
    if (!session_take(&scratch, path, id, &w, restore.ids, SESSION_WINDOWS, &restore.n, &err)) return;
    if (plain) go_to(w.dir[0], STR_EMPTY, 0);
    for (int i = plain ? 1 : 0; i < w.n; i++) tab_open(w.dir[i], STR_EMPTY, false);
    if (plain) tab_show(w.shown);
    app.status[0] = 0;   // (not "Opened … in a new tab" for each)
    if (!id && restore.n > 0) app.ui.restore_n = restore.n;
}

// The question's answer: yes, each other window started on its line (this binary, --restore id,
// through uwsm-app as every launch); no, their lines forgotten.
static void restore_others(bool yes)
{
    char path[PATH_MAX];
    Err err;
    for (int i = 0; i < restore.n; i++) {
        char id[24];
        snprintf(id, sizeof id, "%ld", restore.ids[i]);
        const char *cmd[] = { app.exe, "--restore", id };
        if (yes ? !spawn_app(cmd, COUNT(cmd), &err) : state_path(path, sizeof path) && !session_put(&scratch, path, restore.ids[i], NULL, &err))
            set_status(true, "%s", err.msg);
    }
    restore.n = 0;
}
