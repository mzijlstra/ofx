// View → Clear Previews… (F12 step 4, docs/plan.md "F12 design"): the question counts the cache's
// files on walk.c's thread as it opens (Properties' count: the two never show at once). Its yes runs
// `ofx-clean --all`, fs/clean.c's guarded deletes, in its own process, with its output on a pipe a
// thread here reads. The pipe's end is ofx-clean's (the kernel reaps it: spawn_init), and then the
// previews ofx holds are dropped, so the ones drawn are made again, and the status line says
// ofx-clean's summary. Included by main.c.

static struct {
    bool            counting;   // the question's walk (main thread's)
    DuSum           sum;
    bool            counted;
    pthread_t       th;         // the reader of ofx-clean's output
    bool            running;
    atomic_bool     ended;
    char            line[512];  // its last line (the reader's until ended)
} clear;

static void *clear_read(void *arg)
{
    int fd = (int)(intptr_t)arg;
    char buf[4096];
    isize have = 0;
    for (ssize_t n; (n = read(fd, buf + have, sizeof buf - 1 - (size_t)have)) > 0 || (n < 0 && errno == EINTR);) {
        if (n < 0) continue;
        have += n;
        buf[have] = 0;
        char *nl;
        while ((nl = strchr(buf, '\n'))) {   // each whole line; the last is kept
            *nl = 0;
            if (nl > buf) snprintf(clear.line, sizeof clear.line, "%.*s", (int)sizeof clear.line - 1, buf);   // (cut to fit)
            have -= nl + 1 - buf, memmove(buf, nl + 1, (size_t)have + 1);
        }
        if (have == (isize)sizeof buf - 1) have = 0;   // (a line longer than that: dropped)
    }
    close(fd);
    atomic_store(&clear.ended, true);
    wake_main();
    return NULL;
}

// The yes: ofx-clean --all started, from beside this ofx (as make install puts them), else from
// PATH (the guard lets only a test's own run: its copy on PATH).
static void clear_start(void)
{
    if (clear.running) return;
    char prog[PATH_MAX];
    Err err;
    Str d = path_dir(str_from_c(app.exe));
    if (snprintf(prog, sizeof prog, "%.*s/ofx-clean", STR_ARG(d)) >= (int)sizeof prog || access(prog, X_OK) != 0 || !ops_program_allowed(prog, &err)) {
        if (!on_path("ofx-clean")) {
            set_status(true, "Clear Previews needs ofx-clean, which isn't installed");
            return;
        }
        snprintf(prog, sizeof prog, "ofx-clean");
    }
    char *argv[] = { prog, "--all", NULL };
    int fd;
    if (!spawn_piped(argv, &fd, &err)) {
        set_status(true, "Can't clear the previews: %s", err.msg);
        return;
    }
    clear.line[0] = 0, atomic_store(&clear.ended, false);
    if (pthread_create(&clear.th, NULL, clear_read, (void *)(intptr_t)fd) != 0) {
        close(fd);   // (ofx-clean runs on; its line is lost)
        set_status(false, "Clearing every preview");
        return;
    }
    clear.running = true;
    set_status(false, "Clearing every preview\xE2\x80\xA6");
}

// Each frame: the question's count asked as it opens, and stopped as it closes; ofx-clean's end
// taken. True when something to draw changed.
static bool clear_poll(void)
{
    bool dirty = false;
    if (app.ui.clear_asking && !clear.counting) {
        char *roots[] = { thumbs.root };
        clear.sum = (DuSum){ 0 }, clear.counted = false;
        clear.counting = true;
        if (thumbs.root[0]) walk_ask(roots, 1);
        else clear.counted = true;   // (no cache folder: none)
    }
    if (clear.counting && !app.ui.clear_asking) walk_ask(NULL, 0), clear.counting = false;
    if (clear.counting && walk_take(&clear.sum, &clear.counted)) dirty = true;
    if (clear.running && atomic_load(&clear.ended)) {
        pthread_join(clear.th, NULL);
        clear.running = false;
        thumbs_reset();   // (held ones dropped; those drawn are asked for again)
        const char *at = thumbs.root[0] ? strstr(clear.line, thumbs.root) : NULL;
        const char *say = at && strncmp(at + strlen(thumbs.root), ": ", 2) == 0 ? at + strlen(thumbs.root) + 2 : clear.line;   // (after "<root>: ")
        if (!say[0]) set_status(true, "Clear Previews: ofx-clean said nothing (did it start?)");
        else set_status(strstr(say, "failed") != NULL, "Previews: %s", say);
        dirty = true;
    }
    app.ui.clearing = clear.running;
    return dirty;
}

// At quit: a reader still waiting for ofx-clean's end is left (ofx-clean finishes on its own, in
// its own session).
static void clear_join(const struct timespec *until)
{
    if (clear.running) join_by(clear.th, until);
}
