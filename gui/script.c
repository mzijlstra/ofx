// FX_GUI_SCRIPT=FILE: the input comes from a script instead of the user, one frame per loop, so
// the window can be tested and screenshotted on a headless output without touching anyone's
// keyboard or pointer (CLAUDE.md, "Testing the GUI"). Included by main.c after its state.
//
// One command per line; # starts a comment.
//   settle                  wait until the window's size has held for 20 frames (the compositor
//                           resizes a new window after its first frames)
//   wait <frames>
//   sleep <ms>              frames until that much time has passed
//   key <name>              up down pageup pagedown home end enter right left backspace
//   char <c>                a typed character: j k h l g G t ~
//   wheel <notches>         positive away from the user (up)
//   press <row>             a left press on screen row <row> (0 = the first), through the same
//                           double-click timing as the mouse: two presses on a row within 400 ms
//                           open it
//   back                    the mouse's back button
//   shot <file.png>         the next drawn frame; a relative path goes under FX_GUI_SHOTS
//   sh <command...>         runs the rest of the line with /bin/sh and waits for it (5 s at most),
//                           $FX_DIR set to the folder shown. Point it only at scratch folders.
//   state                   prints one line of key=value words about the window
//   expect <word>...        each word must be in that line (whole words: `sel=a.txt`); the
//                           script ends with "script: N of M expectations failed" when any did
//   wait-for <ms> <word>... frames until every word is in the state line (a reload comes in its
//                           own time); an expectation, failed when <ms> pass
//   quit

#include <spawn.h>
#include <sys/wait.h>

static struct {
    Str   text;
    isize at;
    int   line;
    int   wait;          // frames left of a `wait`
    i64   until;         // a `sleep`'s or `wait-for`'s end, in ms
    char  waiting_for[512];
    int   settle_w, settle_h, settle_n;
    bool  settling;
    char  shot[2100];
    int   expects, failed;
} script;

static u8 script_mem[KiB(256)];

static bool script_open(const char *path)
{
    static Arena a;
    arena_init(&a, "script", script_mem, sizeof script_mem);
    script.text = read_file(&a, path);
    if (!script.text.p || script.text.len == 0) {
        fprintf(stderr, "fx: script %s: cannot read it\n", path);
        return false;
    }
    return true;
}

static void script_state(char *out, size_t cap)
{
    const char *icon = "-";
    Str sel = S("-");
    if (app.have_list && app.ui.n > 0) {
        sel = app.list.e[app.ui.sel].name;
        IconSlot *s = entry_icon(app.ui.sel);
        if (s && s->file[0]) icon = s->file;
    }
    Theme *t = &app.theme;
    snprintf(out, cap,
             "dir=%s n=%td hidden=%td sel=%.*s idx=%td top=%td rows=%d hover=%d accent=#%02x%02x%02x bg=#%02x%02x%02x "
             "icons=%s scale=%d w=%d h=%d reloads=%d themes=%d batches=%d frame_ms_max=%.1f icon=%s status=%s",
             app.cwd, app.list.n, app.list.hidden, STR_ARG(sel), app.ui.sel, app.ui.top, app.ui.rows, app.hover,
             t->accent.r, t->accent.g, t->accent.b, t->bg.r, t->bg.g, t->bg.b, t->icons[0] ? t->icons : "-",
             app.icon_scale, GetScreenWidth(), GetScreenHeight(), app.reloads, app.themes, atomic_load(&watch.batches),
             (double)frame_us_max / 1000.0, icon, app.status[0] ? app.status : "-");
    frame_us_max = 0;
}

// Whether every word of `words` is a whole word of line.
static bool script_has_all(const char *line, Str words, const char **missing, char *buf, size_t cap)
{
    Str l = str_from_c(line);
    for (isize at = 0; at < words.len;) {
        while (at < words.len && words.p[at] == ' ') at++;
        isize e = at;
        while (e < words.len && words.p[e] != ' ') e++;
        if (e == at) break;
        Str w = str_cut(words, at, e);
        bool found = false;
        for (isize i = 0; !found && i + w.len <= l.len; i++)
            found = (i == 0 || l.p[i - 1] == ' ') && memcmp(l.p + i, w.p, (size_t)w.len) == 0 &&
                    (i + w.len == l.len || l.p[i + w.len] == ' ');
        if (!found) {
            snprintf(buf, cap, "%.*s", STR_ARG(w));
            *missing = buf;
            return false;
        }
        at = e;
    }
    return true;
}

static UiKey script_key(Str name)
{
    static const struct { const char *name; UiKey k; } KEYS[] = {
        { "up", UK_UP }, { "down", UK_DOWN }, { "pageup", UK_PAGEUP }, { "pagedown", UK_PAGEDOWN },
        { "home", UK_HOME }, { "end", UK_END }, { "enter", UK_OPEN }, { "right", UK_OPEN },
        { "left", UK_PARENT }, { "backspace", UK_PARENT },
    };
    for (int i = 0; i < COUNT(KEYS); i++)
        if (str_eq(name, str_from_c(KEYS[i].name))) return KEYS[i].k;
    return UK_NONE;
}

static void script_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void script_fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("script:%d: ", script.line);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    script.failed++;
}

static void script_sh(Str cmd)
{
    setenv("FX_DIR", app.cwd, 1);
    char *c = arena_cstr(&scratch, cmd);
    // Under `timeout`: the window answers no events while it waits, and a window that stops
    // answering for long pops Hyprland's "terminate?" dialog on Michael's screen.
    char *argv[] = { "timeout", "5", "/bin/sh", "-c", c, NULL };
    // The window's SIGCHLD has SA_NOCLDWAIT (spawn.c): restore it, or waitpid finds no child.
    struct sigaction dfl = { .sa_handler = SIG_DFL }, old;
    sigaction(SIGCHLD, &dfl, &old);
    pid_t pid;
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);
    if (posix_spawnp(&pid, "timeout", NULL, &attr, argv, environ) == 0) {
        int st;
        waitpid(pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) script_fail("sh failed: %.*s", STR_ARG(cmd));
    }
    posix_spawnattr_destroy(&attr);
    sigaction(SIGCHLD, &old, NULL);
}

// After a frame is drawn, before the swap: a pending shot.
static void script_after_draw(void)
{
    if (!script.shot[0]) return;
    Image img = LoadImageFromScreen();
    if (!ExportImage(img, script.shot)) script_fail("cannot write %s", script.shot);
    UnloadImage(img);
    script.shot[0] = 0;
}

// The next frame's input: false at the end of the script.
static bool script_input(UiInput *in)
{
    char state[16384], missing_buf[256];
    const char *missing;
    if (script.wait > 0) return script.wait--, true;
    if (script.settling) {
        int w = GetScreenWidth(), h = GetScreenHeight();
        if (w == script.settle_w && h == script.settle_h) script.settle_n++;
        else script.settle_w = w, script.settle_h = h, script.settle_n = 0;
        if (script.settle_n < 20) return true;
        script.settling = false;
    }
    if (script.until) {
        if (script.waiting_for[0]) {
            script_state(state, sizeof state);
            if (script_has_all(state, str_from_c(script.waiting_for), &missing, missing_buf, sizeof missing_buf)) {
                script.until = 0, script.waiting_for[0] = 0;
                return true;
            }
            if (now_ms() < script.until) return true;
            script_fail("wait-for: no %s in: %s", missing, state);
            script.until = 0, script.waiting_for[0] = 0;
            return true;
        }
        if (now_ms() < script.until) return true;
        script.until = 0;
    }
    Str line;
    while (next_line(script.text, &script.at, &line)) {
        script.line++;
        line = str_trim(line);
        if (line.len == 0 || line.p[0] == '#') continue;
        isize sp = str_find(line, ' ');
        Str cmd = sp < 0 ? line : str_cut(line, 0, sp), arg = sp < 0 ? STR_EMPTY : str_trim(str_cut(line, sp + 1, line.len));
        char a[1024];
        snprintf(a, sizeof a, "%.*s", STR_ARG(arg));
        if (str_eq(cmd, S("settle"))) {
            script.settling = true, script.settle_n = 0;
        } else if (str_eq(cmd, S("wait"))) {
            script.wait = atoi(a);
        } else if (str_eq(cmd, S("sleep"))) {
            script.until = now_ms() + atoi(a);
        } else if (str_eq(cmd, S("key"))) {
            UiKey k = script_key(arg);
            if (!k) script_fail("no key %s", a);
            else in->keys[in->nkeys++] = k;
        } else if (str_eq(cmd, S("char"))) {
            UiKey k = arg.len == 1 ? ui_char_of((u32)(unsigned char)arg.p[0]) : UK_NONE;
            if (!k) script_fail("no char %s", a);
            else in->keys[in->nkeys++] = k;
        } else if (str_eq(cmd, S("wheel"))) {
            in->wheel = atoi(a);
        } else if (str_eq(cmd, S("press"))) {
            int r = atoi(a);
            if (ui_press(&app.press, r, now_ms()) == 2) in->dclick = r;
            else in->click = r;
        } else if (str_eq(cmd, S("back"))) {
            in->backs = 1;
        } else if (str_eq(cmd, S("shot"))) {
            const char *dir = getenv("FX_GUI_SHOTS");
            if (a[0] != '/' && dir && dir[0]) snprintf(script.shot, sizeof script.shot, "%s/%s", dir, a);
            else snprintf(script.shot, sizeof script.shot, "%s", a);
            script.wait = 1;   // drawn on the next frame
        } else if (str_eq(cmd, S("sh"))) {
            script_sh(arg);
        } else if (str_eq(cmd, S("state"))) {
            script_state(state, sizeof state);
            printf("state: %s\n", state);
        } else if (str_eq(cmd, S("expect"))) {
            script.expects++;
            script_state(state, sizeof state);
            if (!script_has_all(state, arg, &missing, missing_buf, sizeof missing_buf)) script_fail("expect: no %s in: %s", missing, state);
        } else if (str_eq(cmd, S("wait-for"))) {
            isize s2 = str_find(arg, ' ');
            if (s2 < 0) {
                script_fail("wait-for <ms> <word>...");
                continue;
            }
            script.expects++;
            script.until = now_ms() + atoi(a);
            snprintf(script.waiting_for, sizeof script.waiting_for, "%.*s", STR_ARG(str_cut(arg, s2 + 1, arg.len)));
        } else if (str_eq(cmd, S("quit"))) {
            break;
        } else {
            script_fail("unknown command %.*s", STR_ARG(cmd));
        }
        fflush(stdout);
        return true;
    }
    printf("script: %d of %d expectations failed\n", script.failed, script.expects);
    fflush(stdout);
    return false;
}
