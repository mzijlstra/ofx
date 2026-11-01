// ofx.conf (F10, docs/plan.md "F10 design"; fs/conf.c reads it): read at the start and whenever
// its folder's watch says it changed, and applied at once: the previews, the list's dates, the
// search's skip list (a search under way keeps the one it started with). Written at the start when
// it isn't there, so the file is what applies (Michael, 2026-11-01). Edit → Preferences… opens it
// in Omarchy's default editor, writing it first if it went since. Included by main.c.

// The file as the status line names it: ~/.config/ofx/ofx.conf.
static void conf_shown(char *out, size_t cap)
{
    char p[PATH_MAX];
    if (!conf_path(p, sizeof p)) snprintf(p, sizeof p, "~/.config/ofx/ofx.conf");
    ui_tilde(out, cap, p, getenv("HOME"));
}

// At the start: ofx.conf written when it isn't there, its defaults and their help in it; when it
// can't be, the status line says so, and the built-in defaults apply.
static void conf_start(void)
{
    char path[PATH_MAX];
    Err err;
    if (conf_path(path, sizeof path) && !conf_create(path, &err)) set_status(true, "Can't make ofx.conf, so the built-in options apply: %s", err.msg);
}

// ofx.conf read again and applied; a line it couldn't read said in the status line. With no file to
// read (it couldn't be made, or went since), the built-in defaults.
static void conf_apply(void)
{
    char path[PATH_MAX];
    struct stat st;
    ArenaMark mark = arena_mark(&scratch);
    Conf c;
    if (conf_path(path, sizeof path) && stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, R_OK) == 0)
        conf_parse(read_file(&scratch, path), &c);
    else conf_defaults(&c);
    arena_restore(&scratch, mark);
    static bool once;   // (the first read: nothing asked under the old values yet)
    bool previews = once && (c.previews != app.conf.previews || c.preview_max != app.conf.preview_max);
    once = true;
    app.conf = c;
    app.skips = (SearchSkipList){ .n = c.nskip };
    for (int k = 0; k < c.nskip; k++) memcpy(app.skips.name[k], c.skip[k], sizeof app.skips.name[k]);
    thumbs.off = !c.previews, thumbs.src_max = c.preview_max;
    if (previews) thumbs_reset();   // (asked again, under the new limit, or not at all)
    if (c.bad) {
        char shown[PATH_MAX + 1];
        conf_shown(shown, sizeof shown);
        set_status(true, "%s line %d: %s", shown, c.bad, c.why);
    }
}

// Edit → Preferences…: ofx.conf, written first when there is none, opened in the editor Open at
// Line uses (F9 step 4), at its first line; with no recipe for it, as any file opens.
static void prefs_open(void)
{
    char path[PATH_MAX], shown[PATH_MAX + 1];
    Err err;
    conf_shown(shown, sizeof shown);
    if (!conf_path(path, sizeof path)) {
        set_status(true, "No HOME or XDG_CONFIG_HOME: no place for ofx.conf");
        return;
    }
    if (!conf_create(path, &err)) {
        set_status(true, "Can't make %s: %s", shown, err.msg);
        return;
    }
    watch_conf();   // (its folder may be new)
    const Editor *e = editor_now(true);
    char buf[EDITOR_ARGS_MAX];
    const char *argv[EDITOR_WORDS + 2];
    int n = e ? editor_argv(e, path, 1, 1, buf, sizeof buf, argv, COUNT(argv)) : -1;
    bool ok = n >= 0 ? editor_open(e, argv, n, &err) : spawn_open(path, &err);
    if (!ok) set_status(true, "Can't open %s: %s", shown, err.msg);
}
