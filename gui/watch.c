// Live refresh (docs/plan.md, "Live refresh"): a thread blocks on inotify and, once a batch of
// changes has settled, raises a flag and wakes the main loop. Watched: the open folder (not
// recursive), and the Omarchy theme, both its directory and the `current/` directory around it (a
// theme switch may replace the theme directory itself, which ends its watch: an event in
// `current/` watches it again).
//
// The mounts (F6): /proc/self/mountinfo reports a mount or unmount to poll as POLLPRI (proc(5)),
// which raises mounts_ready at once. OFX_TEST_MOUNTINFO names a file to read instead, for the
// tests (a test can't mount anything): an inotify watch on it stands in for the poll.
//
// ofx.conf's folder (F10): a save there (in place, or a new file renamed over it) reads it again.
//
// udev's database (2026-10-30): a file there for each device it has set up, written as a drive is
// plugged in and removed as it goes; a block device's reads the places again, as a mount does.
//
// The bookmarks file (F7.5): its folder is watched (writers rename a new file over it), and a change
// to `bookmarks` there reads the places again, as a mount does (mounts_ready), so a bookmark Nemo
// adds shows at once. A folder made after ofx started (its first bookmark) is watched then.
//
// The trash folders' files/ (2026-10-30): a change there (ofx's or another program's) raises
// trash_ready, and the main thread looks again whether any holds something (the Trash place's icon).
// The main thread arms them (watch_trashes) whenever it reads the trash folders again.
//
// The shares gvfs has mounted (F16 step 5): gvfsd-fuse shows each as a folder of its FUSE mount, and
// no inotify event comes as one appears (measured: only the folder's link count moves). So the thread
// has those FUSE folders listed every WATCH_GVFS_MS, and at once when the main thread kicks it (ofx's
// own connect or unmount, watch_gvfs_kick); a change reads the places again. The listing is a child's
// (ofx-list --shares, watch_gvfs_scan), never this thread's own. While none is mounted the wait grows
// (2, 4, 8, 16, then 30 s: F16 step 7d, Michael 2026-11-10: a child every 2 s was ~43,000 a day on a
// desktop with no share); a change in the list, or a kick, puts it back to 2 s. In Airplane Mode no
// listing at all, until a kick.
//
// The state file's folder (F16 step 7d): a change there reads Airplane Mode again (state_ready), so
// one window turning it on or off turns every window's.
//
// The main thread points the folder watch at a new folder (watch_dir); the thread owns the theme
// watches. inotify's calls are safe from both threads. A batch ends 100 ms after its last event,
// and never more than a second after its first, so a file being written all the time (a download)
// still shows its size once a second.

#include <poll.h>
#include <stdatomic.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>

enum { WATCH_QUIET_MS = 100, WATCH_MAX_MS = 1000, WATCH_GVFS_MS = 2000, WATCH_GVFS_MAX_MS = 30000 };

// Airplane Mode (F16 step 7d): ofx starts no network traffic (the shares' listing here, discovery,
// a connect). The main thread sets it (airplane_apply); the threads read it. airplane_gen counts its
// turnings on, so discovery knows one happened while it waited (the step 7d review).
static atomic_bool airplane;
static atomic_uint airplane_gen;

// The shares' folders, one a line, as the thread last listed them (watch_shares copies them out).
static struct {
    pthread_mutex_t mu;
    char            text[32768];
    int             len;
    int             kick;   // an eventfd: watch_gvfs_kick lists them again at once (-1: none)
    int             held;   // a lister that didn't answer, killed, not yet seen gone (its pidfd; -1 none: the scripts' sharesheld=)
    atomic_bool     held_moved;   // held set or cleared, or every changed, since the main loop last looked (a frame for the scripts)
    atomic_int      scans;   // listings started so far (the scripts' sharesscans=)
    atomic_int      every;   // the wait before the next one, in ms (the scripts' sharesevery=)
    char            exe[PATH_MAX];   // this ofx's binary (ofx-list is beside it), read at watch_start
} gvfsw = { .mu = PTHREAD_MUTEX_INITIALIZER, .kick = -1, .held = -1 };

static struct {
    int         fd;
    atomic_int  dir_wd;        // the open folder's watch, -1 for none
    atomic_int  theme_wd, current_wd;   // the thread sets them; watch_dir reads them (2026-10-28: plain ints)
    int         mount_fd, mount_wd;  // the mountinfo file polled, or the test's watched; -1 for none
    atomic_int  bm_wd;         // the bookmarks file's folder, -1 for none (F7.5)
    atomic_int  conf_wd;       // ofx.conf's folder, -1 for none (F10)
    int         state_wd;      // the state file's folder: Airplane Mode (F16 step 7d), -1 for none
    atomic_int  git_wd;        // the shown repository's git folder: its index and HEAD (F12), -1 for none
    char        current[PATH_MAX];   // $XDG_STATE_HOME/omarchy/current
    char        theme[PATH_MAX + 8]; // .../current/theme
    atomic_int  trash_wd[TRASH_DIRS_MAX];   // each trash folder's files/, 0 for none (inotify's start at 1)
    int         udev_wd;       // udev's database folder: a drive plugged in or pulled (2026-10-30)
    atomic_bool list_ready, theme_ready, mounts_ready, trash_ready, conf_ready, git_ready, state_ready;
    // The thread's re-watch of the theme folder (an add, then its store) against main's release of a
    // wd it no longer wants: inotify gives a folder one wd, so between the two main could take the
    // theme's watch away as the old folder's (the F15 code-health review).
    pthread_mutex_t wd_mu;
} watch = { .wd_mu = PTHREAD_MUTEX_INITIALIZER, .fd = -1, .dir_wd = -1, .theme_wd = -1, .current_wd = -1, .mount_fd = -1, .mount_wd = -1, .bm_wd = -1, .udev_wd = -1, .conf_wd = -1, .git_wd = -1, .state_wd = -1 };

// The mount list ofx reads: /proc/self/mountinfo, or the tests' OFX_TEST_MOUNTINFO.
static const char *mountinfo_path(void)
{
    const char *t = getenv("OFX_TEST_MOUNTINFO");
    return t && t[0] ? t : "/proc/self/mountinfo";
}

// The block devices and udev's database (unmounted drives, 2026-10-30): /sys/class/block and
// /run/udev/data, or the tests' OFX_TEST_SYSBLOCK and OFX_TEST_UDEV. With the guard on
// (OFX_GUI_ROOT, every test run) and no test folder, none: a test never lists Michael's drives,
// so it can never mount one.
static const char *test_or_real(const char *var, const char *real)
{
    const char *t = getenv(var);
    return t && t[0] ? t : getenv("OFX_GUI_ROOT") ? NULL : real;
}

static const char *sysblock_path(void) { return test_or_real("OFX_TEST_SYSBLOCK", "/sys/class/block"); }
static const char *udev_path(void) { return test_or_real("OFX_TEST_UDEV", "/run/udev/data"); }

static const u32 WATCH_DIR_MASK = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_MODIFY |
                                  IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;
static const u32 WATCH_THEME_MASK = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ONLYDIR;

// Whether wd is a trash folder's watch.
static bool watch_is_trash(int wd)
{
    for (int i = 0; i < TRASH_DIRS_MAX; i++)
        if (wd > 0 && atomic_load(&watch.trash_wd[i]) == wd) return true;
    return false;
}

// Whether wd is one of the fixed watches (the theme, ofx.conf's and the bookmarks' folders, udev's,
// the mount list): inotify gives one wd per inode, so the open folder's or a trash's may be one of
// them, and removing it would end that one too (2026-11-01: leaving ~/.config/ofx stopped ofx.conf
// edits applying).
static bool watch_fixed(int wd)
{
    return wd == atomic_load(&watch.theme_wd) || wd == atomic_load(&watch.current_wd) || wd == atomic_load(&watch.conf_wd) ||
           wd == atomic_load(&watch.bm_wd) || wd == watch.udev_wd || wd == watch.state_wd || wd == watch.mount_wd || wd == atomic_load(&watch.git_wd);
}

// wd, one a watch no longer wants (its slot already holds its new one), removed unless another
// still has it: the folder shown's, a trash's files/, the git folder's or a fixed one may be the same
// inode, so the same wd (code-health review, 2026-11-03: three copies, each excluding a slightly
// different set; 2026-10-30 and 2026-11-01 found the cost of one missing).
static void watch_release(int wd)
{
    pthread_mutex_lock(&watch.wd_mu);
    if (wd > 0 && !watch_fixed(wd) && !watch_is_trash(wd) && wd != atomic_load(&watch.dir_wd)) inotify_rm_watch(watch.fd, wd);
    pthread_mutex_unlock(&watch.wd_mu);
}

// The folder to watch now, in place of the last one; NULL: none (the search view, F9). IN_MASK_ADD,
// as the others: a plain add on a folder one of them watches would replace its mask.
static void watch_dir(const char *dir)
{
    if (watch.fd < 0) return;
    int old = atomic_exchange(&watch.dir_wd, -1);
    watch_release(old);   // (dir_wd is -1 meanwhile)
    atomic_store(&watch.dir_wd, dir ? inotify_add_watch(watch.fd, dir, WATCH_DIR_MASK | IN_MASK_ADD) : -1);
}

// A file's folder into *wd, watched unless it is already (or there is none yet: -1, and its
// IN_IGNORED, watch_read, sets -1 again when it goes). For the bookmarks file and ofx.conf.
static void watch_file_dir(atomic_int *wd, const char *file)
{
    char d[PATH_MAX];
    if (watch.fd < 0 || atomic_load(wd) >= 0 || !file || !path_parent(d, sizeof d, file)) return;
    atomic_store(wd, inotify_add_watch(watch.fd, d, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ONLYDIR | IN_MASK_ADD));
}

// The bookmarks file's folder (F7.5).
static void watch_bookmarks(void)
{
    char file[PATH_MAX];
    watch_file_dir(&watch.bm_wd, bookmarks_path(file, sizeof file));
}

// The trash folders' files/ to watch now, in place of the last ones: the new ones added first, so
// none goes unwatched in between, then the old ones no longer wanted removed. IN_MASK_ADD: a files/
// that is also the open folder has one wd, and a plain add would replace the folder's mask with
// this smaller one (review, 2026-10-30: a file written there no longer showed its size).
static void watch_trashes(const TrashDirs *d)
{
    if (watch.fd < 0) return;
    int now[TRASH_DIRS_MAX] = { 0 }, old[TRASH_DIRS_MAX];
    for (int i = 0; i < d->n && i < TRASH_DIRS_MAX; i++) {
        char files[PATH_MAX + 8];
        snprintf(files, sizeof files, "%s/files", d->dir[i]);
        int wd = inotify_add_watch(watch.fd, files, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF |
                                                    IN_ONLYDIR | IN_MASK_ADD);
        now[i] = wd > 0 ? wd : 0;
    }
    for (int i = 0; i < TRASH_DIRS_MAX; i++) old[i] = atomic_exchange(&watch.trash_wd[i], now[i]);
    for (int i = 0; i < TRASH_DIRS_MAX; i++) watch_release(old[i]);   // (one still wanted is among the new ones: kept)
}

// ofx.conf's folder (F10; none when conf_start couldn't make it: Preferences tries again).
static void watch_conf(void)
{
    char file[PATH_MAX];
    watch_file_dir(&watch.conf_wd, conf_path(file, sizeof file) ? file : NULL);
}

// The shown repository's git folder (F12), in place of the last one; NULL none. Its index and HEAD
// are written by a rename into place (git's lock files), so IN_MOVED_TO; git status itself writes
// neither (--no-optional-locks), so no answer asks for another.
static void watch_git(const char *gitdir)
{
    if (watch.fd < 0) return;
    int old = atomic_exchange(&watch.git_wd, -1);
    int wd = gitdir ? inotify_add_watch(watch.fd, gitdir, IN_MOVED_TO | IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_ONLYDIR | IN_MASK_ADD) : -1;
    atomic_store(&watch.git_wd, wd);
    watch_release(old);   // (git_wd is the new one: the same, kept)
}

static void watch_theme_dirs(void)
{
    pthread_mutex_lock(&watch.wd_mu);
    atomic_store(&watch.current_wd, inotify_add_watch(watch.fd, watch.current, WATCH_THEME_MASK | IN_MASK_ADD));
    atomic_store(&watch.theme_wd, inotify_add_watch(watch.fd, watch.theme, WATCH_THEME_MASK | IN_MASK_ADD));
    pthread_mutex_unlock(&watch.wd_mu);
}

// Reads what is waiting: which flags it calls for.
static void watch_read(bool *list, bool *theme, bool *mounts, bool *trash, bool *conf)
{
    _Alignas(struct inotify_event) char buf[16384];
    ssize_t len = read(watch.fd, buf, sizeof buf);
    bool rewatch = false;
    for (ssize_t at = 0; len > 0 && at < len;) {
        const struct inotify_event *e = (const struct inotify_event *)(buf + at);
        at += (ssize_t)(sizeof *e + e->len);
        if (e->mask & IN_Q_OVERFLOW) {
            *list = *theme = *mounts = *trash = *conf = true;
            continue;
        }
        if (watch.mount_wd >= 0 && e->wd == watch.mount_wd) *mounts = true;
        if (e->wd == atomic_load(&watch.bm_wd) && e->len && strcmp(e->name, "bookmarks") == 0) *mounts = true;   // the places again (F7.5)
        if (e->wd == atomic_load(&watch.bm_wd) && (e->mask & IN_IGNORED)) atomic_store(&watch.bm_wd, -1);   // (its folder gone: watch_bookmarks arms the new one)
        if (e->wd == atomic_load(&watch.dir_wd)) *list = true;
        if (e->wd == atomic_load(&watch.git_wd) && e->len && (strcmp(e->name, "index") == 0 || strcmp(e->name, "HEAD") == 0))
            atomic_store(&watch.git_ready, true), wake_main();   // (F12: git_ask merges a burst of them)
        if (e->wd == watch.udev_wd && e->len && e->name[0] == 'b') *mounts = true;   // a block device's (b8:17), not the rest udev keeps
        if (!(e->mask & IN_IGNORED) && watch_is_trash(e->wd)) *trash = true;
        if (e->wd == atomic_load(&watch.conf_wd) && e->len && strcmp(e->name, "ofx.conf") == 0) *conf = true;   // (F10)
        if (e->wd == atomic_load(&watch.conf_wd) && (e->mask & IN_IGNORED)) atomic_store(&watch.conf_wd, -1);   // (its folder gone: Preferences watches the new one, review, 2026-11-01)
        if (e->wd == watch.state_wd && e->len && strcmp(e->name, "state") == 0) atomic_store(&watch.state_ready, true), wake_main();   // (F16 step 7d)
        if (e->wd == atomic_load(&watch.theme_wd) && !(e->mask & IN_IGNORED)) *theme = true;
        if (e->wd == atomic_load(&watch.current_wd)) *theme = true, rewatch = true;
    }
    // The theme directory replaced, or made after ofx started: watch the new one.
    if (rewatch) {
        pthread_mutex_lock(&watch.wd_mu);
        atomic_store(&watch.theme_wd, inotify_add_watch(watch.fd, watch.theme, WATCH_THEME_MASK | IN_MASK_ADD));
        pthread_mutex_unlock(&watch.wd_mu);
    }
}

static bool far_prog(const char *exe, Str name, char *prog, size_t cap, Err *err);   // (far.c)

enum { SHARES_MS = 1000 };   // ofx-list --shares answers in milliseconds; past this it is held

// The shares listed again, by `ofx-list --shares` (gvfs's FUSE folders are read in a child, as
// every far read is: step 5 review, measured: with nine or more lookups stuck on a dead share,
// gvfsd-fuse's workers are all taken and even its root's listing waits, which would stall this
// thread's folder, theme, bookmarks and trash watches). Its answer within SHARES_MS, else it is
// killed and held: no other runs until it has left the kernel, and the last list stands. True when
// the list changed.
static bool watch_gvfs_scan(void)
{
    if (gvfsw.held >= 0) {
        struct pollfd p = { gvfsw.held, POLLIN, 0 };
        if (poll(&p, 1, 0) <= 0) return false;   // (still in the kernel)
        close(gvfsw.held), gvfsw.held = -1;
        atomic_store(&gvfsw.held_moved, true), wake_main();   // (a frame: the scripts' sharesheld= is read at one)
    }
    // No gvfs FUSE mount in the mount list: no shares, and no child (read here: the mount list is the
    // kernel's, never a share's).
    static u8 mem[KiB(512)];
    static Arena a;
    arena_init(&a, "gvfs mounts", mem, sizeof mem);
    Str mi = read_file(&a, mountinfo_path());
    bool any = false;
    isize at = 0;
    for (Str line; !any && next_line(mi, &at, &line);) {
        char mp[PATH_MAX];
        Str type, src;
        any = mountinfo_line(line, mp, sizeof mp, &type, &src) && str_eq(type, S("fuse.gvfsd-fuse"));
    }
    if (!any) {
        if (mi.len == 0) return false;   // (a read that failed: the last list stands)
        pthread_mutex_lock(&gvfsw.mu);
        bool changed = gvfsw.len != 0;
        gvfsw.len = 0;
        pthread_mutex_unlock(&gvfsw.mu);
        return changed;
    }
    char prog[PATH_MAX];
    Err err;
    if (!far_prog(gvfsw.exe, S("A share"), prog, sizeof prog, &err)) return false;
    char *argv[] = { prog, "--shares", NULL };
    atomic_fetch_add(&gvfsw.scans, 1);
    static int answer_ms = -1;   // (OFX_TEST_SHARES_MS: longer for the stand-in tests, whose ASan lister starts slowly under load)
    if (answer_ms < 0) answer_ms = env_int("OFX_TEST_SHARES_MS", SHARES_MS, 100, 60000);
    Child c;
    if (!child_start(argv, false, false, &c, &err)) return false;
    static char now[sizeof gvfsw.text];
    isize got = 0;
    bool done = child_read(&c, now, sizeof now - 1, &got, now_ms() + answer_ms);   // (more than fit: what fits)
    int n = (int)got;
    close(c.out);
    if (!done) {   // no answer: killed, held until it leaves the kernel; the last list stands
        syscall(SYS_pidfd_send_signal, c.pidfd, SIGKILL, NULL, 0);
        gvfsw.held = c.pidfd;
        atomic_store(&gvfsw.held_moved, true), wake_main();
        return false;
    }
    close(c.pidfd);
    if (n < 2 || memcmp(now + n - 2, ".\n", 2) != 0 || (n > 2 && now[n - 3] != '\n')) return false;   // (not its whole answer: the last list stands)
    n -= 2;
    pthread_mutex_lock(&gvfsw.mu);
    bool changed = n != gvfsw.len || memcmp(now, gvfsw.text, (size_t)n) != 0;
    if (changed) memcpy(gvfsw.text, now, (size_t)n), gvfsw.len = n;
    pthread_mutex_unlock(&gvfsw.mu);
    return changed;
}

// The shares' folders as last listed, copied into a (the places read them).
static Str watch_shares(Arena *a)
{
    pthread_mutex_lock(&gvfsw.mu);
    char *b = arena_push(a, gvfsw.len + 1, 1);
    memcpy(b, gvfsw.text, (size_t)gvfsw.len);
    Str s = { b, gvfsw.len };
    pthread_mutex_unlock(&gvfsw.mu);
    return s;
}

// The shares listed again at once (a connection made, or a share unmounted, by ofx).
static void watch_gvfs_kick(void)
{
    if (gvfsw.kick >= 0) eventfd_write(gvfsw.kick, 1);
}

// One poll of inotify, the mountinfo file and the kick, for at most ms (-1: no limit): the flags it
// raises; *kicked when the shares are to be listed now.
static void watch_poll(int ms, bool *list, bool *theme, bool *mounts, bool *trash, bool *conf, bool *kicked)
{
    struct pollfd p[3] = { { watch.fd, POLLIN, 0 }, { watch.mount_fd, POLLPRI, 0 }, { gvfsw.kick, POLLIN, 0 } };
    if (poll(p, 3, ms) <= 0) return;
    if (p[0].revents & POLLIN) watch_read(list, theme, mounts, trash, conf);
    if (p[1].revents & (POLLPRI | POLLERR)) *mounts = true;
    eventfd_t v;
    if ((p[2].revents & POLLIN) && eventfd_read(gvfsw.kick, &v) == 0) *kicked = true;
}

static void *watch_thread(void *arg)
{
    (void)arg;
    // Its signal mask is the main thread's (SIGTERM and SIGINT blocked), so a quit still goes to
    // main.c's signal_waiter.
    watch_theme_dirs();
    // (OFX_TEST_GVFS_MS: longer, so a test sees ofx's own kicks alone, step 5; OFX_TEST_GVFS_MAX_MS
    // the back-off's longest, shorter for a test, step 7d)
    int base = env_int("OFX_TEST_GVFS_MS", WATCH_GVFS_MS, 100, 600000);
    int most = env_int("OFX_TEST_GVFS_MAX_MS", WATCH_GVFS_MAX_MS, base, 3600000);
    i64 gvfs_due = 0;   // (listed at once)
    for (;;) {
        bool list = false, theme = false, mounts = false, trash = false, conf = false, kicked = false;
        bool off = atomic_load(&airplane);
        i64 wait = gvfs_due - now_ms();
        if (off) watch_poll(-1, &list, &theme, &mounts, &trash, &conf, &kicked);   // (Airplane Mode: no listing until a kick)
        else if (wait > 0) watch_poll((int)wait, &list, &theme, &mounts, &trash, &conf, &kicked);
        if (!atomic_load(&airplane) && (kicked || now_ms() >= gvfs_due)) {
            bool changed = watch_gvfs_scan();
            if (changed) mounts = true;
            // The next wait: 2 s while a share is mounted, or after a change or a kick; else twice
            // the last, up to most (step 7d).
            int every = share_poll_next(atomic_load(&gvfsw.every), base, most, kicked || changed || gvfsw.len > 0);
            if (atomic_exchange(&gvfsw.every, every) != every) atomic_store(&gvfsw.held_moved, true), wake_main();   // (a frame: the scripts' sharesevery= is read at one; a few a minute at most)
            gvfs_due = now_ms() + every;
        }
        if (!list && !theme && !mounts && !trash && !conf) continue;
        i64 first = now_ms(), last = first;
        for (i64 now; (now = now_ms()) < ui_batch_due(first, last, WATCH_QUIET_MS, WATCH_MAX_MS);) {
            bool l2 = false, t2 = false, m2 = false, r2 = false, c2 = false, k2 = false;
            watch_poll((int)(ui_batch_due(first, last, WATCH_QUIET_MS, WATCH_MAX_MS) - now), &l2, &t2, &m2, &r2, &c2, &k2);
            if (k2 && !atomic_load(&airplane)) {   // (a kick: listed now, and the wait back to its shortest)
                if (watch_gvfs_scan()) m2 = true;
                if (atomic_exchange(&gvfsw.every, base) != base) atomic_store(&gvfsw.held_moved, true), wake_main();
                gvfs_due = now_ms() + base;
            }
            if (l2 || t2 || m2 || r2 || c2) last = now_ms();
            list |= l2, theme |= t2, mounts |= m2, trash |= r2, conf |= c2;
        }
        if (list) atomic_store(&watch.list_ready, true);
        if (theme) atomic_store(&watch.theme_ready, true);
        if (mounts) atomic_store(&watch.mounts_ready, true);
        if (trash) atomic_store(&watch.trash_ready, true);
        if (conf) atomic_store(&watch.conf_ready, true);
        wake_main();
    }
    return NULL;
}

// False when inotify is unavailable: ofx then works without refreshing.
static bool watch_start(void)
{
    watch.fd = inotify_init1(IN_CLOEXEC);
    if (watch.fd < 0) return false;
    gvfsw.kick = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    ssize_t xn = readlink("/proc/self/exe", gvfsw.exe, sizeof gvfsw.exe - 1);
    gvfsw.exe[xn > 0 ? xn : 0] = 0;
    char dir[PATH_MAX];
    theme_dir(dir, sizeof dir);
    snprintf(watch.theme, sizeof watch.theme, "%s", dir);
    snprintf(watch.current, sizeof watch.current, "%.*s", STR_ARG(path_dir(str_from_c(dir))));
    const char *mi = mountinfo_path();
    if (strcmp(mi, "/proc/self/mountinfo") == 0) watch.mount_fd = open(mi, O_RDONLY | O_CLOEXEC);
    else watch.mount_wd = inotify_add_watch(watch.fd, mi, IN_CLOSE_WRITE | IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF);
    watch_bookmarks();
    watch_conf();
    char state[PATH_MAX], sdir[PATH_MAX];   // (F16 step 7d; the folder made if it isn't there yet, as the state file's first write would)
    Err serr;
    if (state_path(state, sizeof state) && path_parent(sdir, sizeof sdir, state) && ops_allowed(sdir, &serr) && mkdir_private(sdir, &serr))
        watch.state_wd = inotify_add_watch(watch.fd, sdir, IN_MOVED_TO | IN_CLOSE_WRITE | IN_ONLYDIR | IN_MASK_ADD);
    const char *udev = udev_path();
    if (udev) watch.udev_wd = inotify_add_watch(watch.fd, udev, IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ONLYDIR | IN_MASK_ADD);
    pthread_t t;
    if (pthread_create(&t, NULL, watch_thread, NULL) != 0) return false;
    pthread_detach(t);
    return true;
}
