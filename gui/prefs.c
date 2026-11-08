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

// The key lines the written file has (ui/keys.c's, every key as ofx has it).
static const char *conf_keys(void)
{
    static char text[8192];
    if (!text[0]) ui_keys_conf_text(text, sizeof text);
    return text;
}

// What ofx.conf's key lines did when last read (a script's keysbad and keyclashes).
static UiKeysRead conf_keys_read;

// At the start: ofx.conf written when it isn't there, its defaults and their help in it; when it
// can't be, the status line says so, and the built-in defaults apply.
static void conf_start(void)
{
    char path[PATH_MAX];
    Err err;
    if (conf_path(path, sizeof path) && !conf_create(path, conf_keys(), &err)) set_status(true, "Can't make ofx.conf, so the built-in options apply: %s", err.msg);
}

// ofx.conf read again and applied; the first line it couldn't read said in the status line, else,
// after a save, a key a line takes from another command now that it didn't before (F15: said once). With no file to read
// (it couldn't be made, or went since), the built-in defaults. Open at Line reads its editor lines
// from app.conf (editor_now).
static void conf_apply(void)
{
    static Conf c;   // (34 KiB: the editor and key lines)
    conf_load(&scratch, &c);
    UiKeysRead keys = conf_keys_read = ui_binds_set(c.key, c.key_no, c.nkey);
    static bool once;   // (the first read: nothing asked under the old values yet)
    bool was_once = once;
    bool previews = once && (c.previews != app.conf.previews || c.preview_max != app.conf.preview_max || c.thumbnailers != app.conf.thumbnailers);
    bool off = once && app.conf.restore_tabs && !c.restore_tabs;   // (F11 step 3b: this window's line out)
    bool views = once && (c.icons != app.conf.icons || c.zoom != app.conf.zoom || c.sort != app.conf.sort || c.sort_rev != app.conf.sort_rev);
    once = true;
    app.conf = c;
    if (off) restore_off();
    if (views) {   // (F11: the folders' choices against the old defaults forgotten; the folder shown again)
        remember_defaults();
        if (fold_view_of(app.ui.view)) remember_apply(app.cwd);
    }
    thumbs.off = !c.previews, thumbs.src_max = c.preview_max, thumbs.exec_on = c.thumbnailers;
    app.ui.shift_add = c.shift_add;
    if (previews) thumbs_reset();   // (asked again, under the new limit, or not at all)
    char shown[PATH_MAX + 1];
    conf_shown(shown, sizeof shown);
    if (c.bad && (!keys.bad || c.bad < keys.bad)) set_status(true, "%s line %d: %s", shown, c.bad, c.why);
    else if (keys.bad) set_status(true, "%s line %d: %s", shown, keys.bad, keys.why);
    else if (was_once && keys.fresh)
        set_status(false, "ofx.conf: %s%s", keys.clash, keys.fresh > 1 ? arena_sprintf(&scratch, " (and %d more)", keys.fresh - 1) : "");
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
    if (!conf_create(path, conf_keys(), &err)) {
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
