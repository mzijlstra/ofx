// A far folder's lister (F16 step 2): ofx-list (tools/list.c) started for the folder shown when it is
// on a far drive (a network share, any FUSE mount: thumb_local's rule), its output read by a thread
// of its own, and its counts asked on its input. Neither the thread nor the main thread ever touch
// the folder: the thread reads a pipe and polls pidfds, nothing else, so a dead share can't hold
// ofx (fs/farlist.c says why a child). One lister at a time; one left killed but still in the
// kernel (D, waiting on the share) keeps another from starting for its folder until it is gone, so
// a dead share can't gather them (F16 step 1 review). Never wait() or waitpid(-1): under
// SA_NOCLDWAIT they would wait for those too. Included by main.c, before counts.c.

enum { FAR_KILLED_MAX = 16, FAR_READ = 65536 };

static u8 far_mem[MiB(16)];   // what the lister said, for one folder (touched only as it comes)

static struct {
    pthread_t       th;
    bool            started, quit;
    pthread_mutex_t mu;
    int             wake[2];   // a pipe: the main thread's word to the thread (a new lister, quit)
    // The lister (set by the main thread under mu):
    u32   seq;            // each lister's number; 0 none yet
    pid_t pid;
    int   pidfd, in;      // -1: none (main's: the pidfd to kill it by; its input's write end)
    int   out;            // its output's read end: the thread's from the word on
    char  dir[PATH_MAX];
    // What it said so far (the thread appends under mu; the main thread reads [0, len) only):
    isize len;
    bool  eof, full;
    // Listers killed while still running, each with its folder (the thread drops each as it goes):
    struct { int pidfd; char dir[PATH_MAX]; } killed[FAR_KILLED_MAX];
    int   nkilled;
    int   kills;          // killed in all (the state line's farkills=)
    int   asked;          // counts asked of listers (farasked=)
} far = { .mu = PTHREAD_MUTEX_INITIALIZER, .wake = { -1, -1 }, .pidfd = -1, .in = -1, .out = -1 };

static u8 far_mi_mem[KiB(512)];   // the mount list, read for far_dir (main thread only)
static Arena far_mia;

// The mount list, read again (valid until the next call). Main thread only.
static Str far_mounts(void)
{
    if (!far_mia.base) arena_init(&far_mia, "far mounts", far_mi_mem, sizeof far_mi_mem);
    arena_reset(&far_mia);
    return read_file(&far_mia, mountinfo_path());
}

// Whether path is on a far drive, by the mount list alone (no syscall on path itself).
static bool far_dir(const char *path)
{
    return path[0] == '/' && !thumb_local(far_mounts(), path);
}

static void far_word(void)
{
    char c = 1;
    ssize_t k = write(far.wake[1], &c, 1);
    (void)k;   // (a full pipe already holds a word)
}

static void *far_loop(void *arg)
{
    (void)arg;
    static char buf[FAR_READ];
    int out = -1;
    u32 seq = 0;
    for (;;) {
        struct pollfd p[2 + FAR_KILLED_MAX];
        int n = 0;
        p[n++] = (struct pollfd){ .fd = far.wake[0], .events = POLLIN };
        p[n++] = (struct pollfd){ .fd = out, .events = POLLIN };
        pthread_mutex_lock(&far.mu);
        int nk = far.nkilled;
        for (int i = 0; i < nk; i++) p[n++] = (struct pollfd){ .fd = far.killed[i].pidfd, .events = POLLIN };
        pthread_mutex_unlock(&far.mu);
        if (poll(p, (nfds_t)n, -1) < 0 && errno != EINTR) break;
        if (p[0].revents) {
            char drain[64];
            while (read(far.wake[0], drain, sizeof drain) > 0) {}
        }
        pthread_mutex_lock(&far.mu);
        if (far.quit) {
            pthread_mutex_unlock(&far.mu);
            break;
        }
        bool woke = false;
        // Killed listers that are gone now: dropped (their pidfds said so). From the end, so the
        // indexes polled still match the ones not yet looked at.
        for (int i = nk - 1; i >= 0; i--) {
            if (!p[2 + i].revents) continue;
            close(far.killed[i].pidfd);
            far.killed[i] = far.killed[--far.nkilled];
            woke = true;
        }
        if (seq != far.seq) {   // a new lister (or none): the last one's output closed, unread
            if (out >= 0) close(out);
            out = far.out, far.out = -1, seq = far.seq;
            p[1].revents = 0;
        }
        pthread_mutex_unlock(&far.mu);
        if (out >= 0 && p[1].revents) {
            ssize_t k = read(out, buf, sizeof buf);
            pthread_mutex_lock(&far.mu);
            if (seq == far.seq) {
                if (k > 0 && far.len + k <= (isize)sizeof far_mem) memcpy(far_mem + far.len, buf, (size_t)k), far.len += k;
                else if (k > 0) far.full = true;
                if (k == 0 || (k < 0 && errno != EINTR && errno != EAGAIN)) far.eof = true;
                woke = true;
            }
            pthread_mutex_unlock(&far.mu);
            if (k == 0 || (k < 0 && errno != EINTR && errno != EAGAIN)) close(out), out = -1;
        }
        if (woke) wake_main();
    }
    if (out >= 0) close(out);
    return NULL;
}

static void far_start_thread(void)
{
    if (pipe2(far.wake, O_CLOEXEC | O_NONBLOCK) != 0) return;
    far.started = pthread_create(&far.th, NULL, far_loop, NULL) == 0;
}

// The lister shown (if any) killed: its input closed, SIGKILL through its pidfd, and its pidfd
// kept until it is gone (one still in the kernel keeps another from starting for its folder).
static void far_drop(void)
{
    pthread_mutex_lock(&far.mu);
    if (far.pidfd >= 0) {
        syscall(SYS_pidfd_send_signal, far.pidfd, SIGKILL, NULL, 0);
        far.kills++;
        if (far.nkilled < FAR_KILLED_MAX) {
            far.killed[far.nkilled].pidfd = far.pidfd;
            snprintf(far.killed[far.nkilled].dir, PATH_MAX, "%s", far.dir);
            far.nkilled++;
        } else {
            close(far.pidfd);   // (sixteen already: this one isn't counted against its folder)
        }
    }
    if (far.in >= 0) close(far.in);
    if (far.out >= 0) close(far.out);   // (started but not yet handed over)
    far.pidfd = far.in = far.out = -1, far.pid = 0, far.dir[0] = 0;
    far.seq++, far.len = 0, far.eof = far.full = false;
    pthread_mutex_unlock(&far.mu);
    if (far.started) far_word();
}

// Whether a killed lister of dir is still in the kernel (a new one waits for it).
static bool far_busy(const char *dir)
{
    pthread_mutex_lock(&far.mu);
    bool busy = false;
    for (int i = 0; i < far.nkilled && !busy; i++) busy = strcmp(far.killed[i].dir, dir) == 0;
    pthread_mutex_unlock(&far.mu);
    return busy;
}

// ofx-list for dir started (the last one dropped first): from beside this ofx (as make install
// puts them), else from PATH (the guard lets only a test's own run: its copy among the fakes).
// False, with the reason, when it can't start.
static bool far_start(const char *exe, const char *dir, bool hidden, Err *err)
{
    far_drop();
    Str name = path_base(str_from_c(dir));   // (said by its name: the crumbs say where)
    if (!far.started) return err_set(err, "%.*s: the far folder reader didn't start", STR_ARG(name)), false;
    char prog[PATH_MAX];
    Err why;
    Str d = path_dir(str_from_c(exe));
    if (snprintf(prog, sizeof prog, "%.*s/ofx-list", STR_ARG(d)) >= (int)sizeof prog || access(prog, X_OK) != 0 || !ops_program_allowed(prog, &why)) {
        if (!on_path("ofx-list")) return err_set(err, "%.*s is on a network drive, and reading it needs ofx-list, which isn't installed", STR_ARG(name)), false;
        snprintf(prog, sizeof prog, "ofx-list");
    }
    char *argv[] = { prog, (char *)dir, hidden ? "--hidden" : NULL, NULL };
    int in[2], out[2];
    if (pipe2(in, O_CLOEXEC) != 0) return err_set(err, "a pipe: %s", strerror(errno)), false;
    if (pipe2(out, O_CLOEXEC) != 0) return close(in[0]), close(in[1]), err_set(err, "a pipe: %s", strerror(errno)), false;
    pid_t pid;
    bool ok = spawn_fds(NULL, argv, in[0], out[1], false, &pid, err);
    close(in[0]), close(out[1]);
    int pfd = ok ? (int)syscall(SYS_pidfd_open, pid, 0) : -1;
    if (!ok || pfd < 0) {   // (no pidfd: it can't be killed safely later; not used)
        if (ok) kill(pid, SIGKILL), err_set(err, "%.*s: the reader can't be watched: %s", STR_ARG(name), strerror(errno));
        close(in[1]), close(out[0]);
        return false;
    }
    fcntl(in[1], F_SETFL, O_NONBLOCK);   // (a count asked never blocks the window)
    pthread_mutex_lock(&far.mu);
    far.pid = pid, far.pidfd = pfd, far.in = in[1], far.out = out[0];
    snprintf(far.dir, sizeof far.dir, "%s", dir);
    far.seq++, far.len = 0, far.eof = far.full = false;
    pthread_mutex_unlock(&far.mu);
    far_word();
    return true;
}

// What the lister said so far: [0, *len) of far_mem, stable once read; *eof once it ended.
static void far_said(isize *len, bool *eof, bool *full)
{
    pthread_mutex_lock(&far.mu);
    *len = far.len, *eof = far.eof, *full = far.full;
    pthread_mutex_unlock(&far.mu);
}

// Entry idx's count asked of the lister (gen: the listing's order's). False when it can't be now
// (no lister, its input full): asked again later.
static bool far_count_send(u32 gen, isize idx, Str name)
{
    char buf[NAME_MAX + 64];
    isize n = far_count_ask(buf, sizeof buf, gen, idx, name);
    pthread_mutex_lock(&far.mu);
    bool ok = n > 0 && far.in >= 0 && write(far.in, buf, (size_t)n) == n;   // (a pipe takes up to 4096 bytes whole or not at all)
    if (ok) far.asked++;
    pthread_mutex_unlock(&far.mu);
    return ok;
}

// At quit: the lister killed, the thread told to stop (it never blocks on the share: joined).
static void far_quit(void)
{
    far_drop();
    if (!far.started) return;
    pthread_mutex_lock(&far.mu);
    far.quit = true;
    pthread_mutex_unlock(&far.mu);
    far_word();
}

static void far_join(const struct timespec *until)
{
    if (far.started) join_by(far.th, until);
}
