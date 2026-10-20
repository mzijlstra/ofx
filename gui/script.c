// FX_GUI_SCRIPT=FILE: the input comes from a script instead of the user, one frame per loop, so
// the window can be tested and screenshotted on a headless output without touching anyone's
// keyboard or pointer (CLAUDE.md, "Testing the GUI"). Included by main.c after its state.
//
// One command per line; # starts a comment.
//   settle                  wait until the window's size has held for 20 frames (the compositor
//                           resizes a new window after its first frames)
//   wait <frames>
//   sleep <ms>              frames until that much time has passed
//   key <name>              up down pageup pagedown home end enter right left backspace esc tab
//                           f2 delete shift-delete space, and the chords alt-left alt-right ctrl-f
//                           ctrl-h ctrl-a ctrl-c ctrl-x ctrl-v ctrl-z ctrl-shift-n ctrl-l alt-up
//   char <c>                one typed character
//   type <text>             the rest of the line, after one space, as typed characters
//   wheel <notches>         over the list; positive away from the user (up)
//   press <row> [ctrl|shift] a left press on list row <row> (0 = the first on screen), through the
//                           same double-click timing as the mouse: two presses on a row within
//                           400 ms open it; with ctrl or shift held it marks rows instead
//   press-side <row>        a left press on sidebar row <row>
//   header name|size|mtime  a click on that column's header
//   click-at <x> <y>        a left press at a point, hit-tested against the last frame as the
//                           mouse's is (rows, sidebar rows, headers)
//   point-ui <id> [k]       the point at the middle of a Clay element (k: its CLAY_IDI index),
//                           printed as "point: x y" and kept for click-here
//   click-here              a left press at the last point-ui
//   back, forward           the mouse's back and forward buttons
//   raw-button <n>          GLFW's button n (1-8) pressed and released through main.c's callback
//   raw-key <key> [mods]    GLFW key code <key> pressed through main.c's key callback, with GLFW's
//                           mod bits (1 Shift, 2 Ctrl, 4 Alt): what ui_key_of makes of it
//   shot <file.png>         the next drawn frame; a relative path goes under FX_GUI_SHOTS
//   sh <command...>         runs the rest of the line with /bin/sh and waits for it (5 s at most),
//                           $FX_DIR set to the folder shown, $FX_ROOT to the run's scratch tree.
//                           Refused unless the folder is under FX_GUI_ROOT and nothing failed yet.
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
    float px, py;        // the last point-ui
    Str   typing;        // what is left of a `type` longer than a frame's events
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
    if (app.have_list && app.ui.list.n > 0) {
        sel = app.list.e[app.ui.list.sel].name;
        IconSlot *s = entry_icon(app.ui.list.sel);
        if (s && s->file[0]) icon = s->file;
    }
    Theme *t = &app.theme;
    // The edit line's text, its spaces as '_' (the state line is words).
    static const char *const JOB_PHASES[] = { "scan", "run", "ask", "done" };
    static const char *const UNDO_KINDS[] = { "-", "rename", "mkdir", "trash", "copy", "move" };
    char edit[NAME_MAX + 1];
    snprintf(edit, sizeof edit, "%.*s", app.ui.edit_len ? app.ui.edit_len : 1, app.ui.edit_len ? app.ui.edit : "-");
    for (char *c = edit; *c; c++)
        if (*c == ' ') *c = '_';
    // The path buttons' labels, '|' between; the cursor's folder count (- unknown, ? unreadable).
    char crumbs[2048] = "", cnt[32] = "-";
    for (int i = 0; i < app.crumbs.n; i++) {
        size_t k = strlen(crumbs);
        snprintf(crumbs + k, sizeof crumbs - k, "%s%.*s", i ? "|" : "", STR_ARG(ui_crumb_label(&app.crumbs, i)));
    }
    if (app.have_list && app.ui.list.n > 0 && kind_is_dir(app.list.e[app.ui.list.sel].kind)) {
        isize c = app.count[app.ui.list.sel];
        if (c >= 0) snprintf(cnt, sizeof cnt, "%td", c);
        else if (c == -1) snprintf(cnt, sizeof cnt, "?");
    }
    snprintf(out, cap,
             "dir=%s n=%td hidden=%td sel=%.*s idx=%td top=%td rows=%d hover=%d accent=#%02x%02x%02x bg=#%02x%02x%02x "
             "icons=%s scale=%d w=%d h=%d reloads=%d themes=%d batches=%d frame_ms_max=%.1f icon=%s "
             "shown=%d sort=%s rev=%d finding=%d miss=%d find=%.*s last=%.*s side=%td sidefocus=%d place=%s here=%s "
             "back=%d fwd=%d marked=%td editing=%d edit=%s confirming=%d asking=%d job=%s done=%lld/%lld undo=%s clip=%td cut=%d "
             "sysclip=%d crumbs=%s crumbhome=%d crumb=%d crumbshown=%d-%d arrows=%d locating=%d cnt=%s status=%s",
             app.cwd, app.list.n, app.list.hidden, STR_ARG(sel), app.ui.list.sel, app.ui.list.top, app.ui.list.rows, app.hover,
             t->accent.r, t->accent.g, t->accent.b, t->bg.r, t->bg.g, t->bg.b, t->icons[0] ? t->icons : "-",
             app.icon_scale, GetScreenWidth(), GetScreenHeight(), app.reloads, app.themes, atomic_load(&watch.batches),
             (double)frame_us_max / 1000.0, icon, app.show_hidden,
             app.ui.sort.key == SORT_NAME ? "name" : app.ui.sort.key == SORT_SIZE ? "size" : "mtime", app.ui.sort.reverse,
             app.ui.finding, app.ui.find_miss, app.ui.find_len ? app.ui.find_len : 1, app.ui.find_len ? app.ui.find : "-",
             app.ui.last_len ? app.ui.last_len : 1, app.ui.last_len ? app.ui.last : "-", app.ui.side.sel, app.ui.side_focus,
             app.places.n ? app.places.p[app.ui.side.sel].label : "-", app.place_cur >= 0 ? app.places.p[app.place_cur].label : "-",
             hist.nback, hist.nfwd, app.ui.marked, app.ui.editing, edit, app.ui.confirming, app.ui.asking,
             !run.running ? "-" : JOB_PHASES[atomic_load(&run.j.phase)], atomic_load(&run.j.files_done), atomic_load(&run.j.files_total),
             UNDO_KINDS[undo.kind], own.n, own.cut, clip.ok, crumbs, app.crumbs.seg[0].home ? 0 : -1, app.crumbs.cur, app.crumb_lo, app.crumb_hi, app.crumb_arrows,
             app.ui.editing && app.ui.edit_path, cnt, app.status[0] ? app.status : "-");
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
        { "home", UK_HOME }, { "end", UK_END }, { "enter", UK_ENTER }, { "right", UK_RIGHT },
        { "left", UK_LEFT }, { "backspace", UK_BACKSPACE }, { "esc", UK_ESC }, { "tab", UK_TAB },
        { "alt-left", UK_BACK }, { "alt-right", UK_FORWARD }, { "ctrl-f", UK_FIND }, { "ctrl-h", UK_HIDDEN },
        { "f2", UK_F2 }, { "delete", UK_DELETE }, { "shift-delete", UK_SHIFT_DELETE }, { "ctrl-a", UK_ALL },
        { "ctrl-c", UK_COPY }, { "ctrl-x", UK_CUT }, { "ctrl-v", UK_PASTE }, { "ctrl-z", UK_UNDO },
        { "ctrl-shift-n", UK_NEWFOLDER }, { "ctrl-l", UK_LOCATION }, { "alt-up", UK_PARENT },
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

// Whether path is root or under it, both resolved.
static bool script_under(const char *path, const char *root)
{
    char p[PATH_MAX], r[PATH_MAX];
    if (!root || !root[0] || !realpath(path, p) || !realpath(root, r)) return false;
    size_t n = strlen(r);
    return strncmp(p, r, n) == 0 && (p[n] == 0 || p[n] == '/');
}

// An `sh` line runs only while the script is on track: fx's folder inside the run's scratch tree
// (FX_GUI_ROOT, set by run-script.sh), and no expectation failed yet. The lines delete and edit
// files relative to $FX_DIR, so a navigation bug (or a GUI mutant that recreates one) would
// otherwise point them at wherever fx landed: / once, Michael's home another time.
static void script_sh(Str cmd)
{
    const char *root = getenv("FX_GUI_ROOT");
    if (script.failed) {
        script_fail("sh skipped after a failure: %.*s", STR_ARG(cmd));
        return;
    }
    if (!script_under(app.cwd, root)) {
        script_fail("sh refused: %s is not under FX_GUI_ROOT (%s)", app.cwd, root ? root : "unset");
        return;
    }
    setenv("FX_DIR", app.cwd, 1);
    setenv("FX_ROOT", root, 1);
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
    if (script.typing.len) {
        // A long `type` goes on over several frames, UI_EVENTS_MAX characters a frame.
        isize i = 0;
        while (i < script.typing.len && in->nev < UI_EVENTS_MAX) in->ev[in->nev++] = (UiEvent){ UK_NONE, utf8_next(script.typing.p, script.typing.len, &i) };
        script.typing = str_cut(script.typing, i, script.typing.len);
        return true;
    }
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
            if (str_eq(arg, S("space"))) in->ev[in->nev++] = (UiEvent){ UK_NONE, ' ' };   // a typed space
            else if (!k) script_fail("no key %s", a);
            else in->ev[in->nev++] = (UiEvent){ k, 0 };
        } else if (str_eq(cmd, S("char")) || str_eq(cmd, S("type"))) {
            // The raw line for type: its text may start or end with spaces.
            Str text = str_eq(cmd, S("type")) ? str_cut(line, 5, line.len) : arg;
            if (str_eq(cmd, S("char")) && text.len != 1) script_fail("char takes one character");
            isize i = 0;
            while (i < text.len && in->nev < UI_EVENTS_MAX) in->ev[in->nev++] = (UiEvent){ UK_NONE, utf8_next(text.p, text.len, &i) };
            script.typing = str_cut(text, i, text.len);   // the rest on the next frames
        } else if (str_eq(cmd, S("wheel"))) {
            in->wheel = atoi(a);
        } else if (str_eq(cmd, S("press"))) {
            int r = atoi(a);
            int mods = strstr(a, "ctrl") ? UI_MOD_CTRL : strstr(a, "shift") ? UI_MOD_SHIFT : 0;
            if (!mods && ui_press(&app.press, r, now_ms()) == 2) in->dclick = r;
            else in->click = r, in->click_mods = mods;
        } else if (str_eq(cmd, S("press-side"))) {
            in->side_click = atoi(a);
        } else if (str_eq(cmd, S("header"))) {
            in->header = str_eq(arg, S("name")) ? SORT_NAME : str_eq(arg, S("size")) ? SORT_SIZE : str_eq(arg, S("mtime")) ? SORT_MTIME : -1;
            if (in->header < 0) script_fail("no header %s", a);
        } else if (str_eq(cmd, S("click-at"))) {
            float x = 0, y = 0;
            if (sscanf(a, "%f %f", &x, &y) != 2) script_fail("click-at <x> <y>");
            else press_at(in, x, y, 0);
        } else if (str_eq(cmd, S("point-ui"))) {
            char id[200];
            int k = -1;
            Clay_ElementData d = { 0 };
            if (sscanf(a, "%199s %d", id, &k) >= 1)
                d = Clay_GetElementData(k >= 0 ? Clay_GetElementIdWithIndex(cz(id), (uint32_t)k) : Clay_GetElementId(cz(id)));
            if (!d.found) script_fail("point-ui: no element %s", a);
            script.px = d.boundingBox.x + d.boundingBox.width / 2, script.py = d.boundingBox.y + d.boundingBox.height / 2;
            printf("point: %.0f %.0f\n", script.px, script.py);
        } else if (str_eq(cmd, S("click-here"))) {
            press_at(in, script.px, script.py, 0);
        } else if (str_eq(cmd, S("back"))) {
            in->backs = 1;
        } else if (str_eq(cmd, S("forward"))) {
            in->forwards = 1;
        } else if (str_eq(cmd, S("raw-button"))) {
            int b = atoi(a);
            if (b < 1 || b > 8) script_fail("raw-button 1-8");
            else on_button(glfwGetCurrentContext(), b - 1, GLFW_PRESS, 0), on_button(glfwGetCurrentContext(), b - 1, GLFW_RELEASE, 0);
            // The callback counted it; this frame's input reads it as the mouse's would.
            in->backs = mouse_backs, in->forwards = mouse_forwards, mouse_backs = mouse_forwards = 0;
        } else if (str_eq(cmd, S("raw-key"))) {
            int k = 0, mods = 0;
            if (sscanf(a, "%d %d", &k, &mods) < 1) script_fail("raw-key <key> [mods]");
            else on_key(glfwGetCurrentContext(), k, 0, GLFW_PRESS, mods), on_key(glfwGetCurrentContext(), k, 0, GLFW_RELEASE, mods);
            // The callback queued it; this frame's input takes the queue as the user's would.
            for (int i = 0; i < evq.n && in->nev < UI_EVENTS_MAX; i++) in->ev[in->nev++] = evq.ev[i];
            evq.n = 0;
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
