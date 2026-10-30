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
// The bookmarks file (F7.5): its folder is watched (writers rename a new file over it), and a change
// to `bookmarks` there reads the places again, as a mount does (mounts_ready), so a bookmark Nemo
// adds shows at once. A folder made after ofx started (its first bookmark) is watched then.
//
// The trash folders' files/ (2026-10-30): a change there (ofx's or another program's) raises
// trash_ready, and the main thread looks again whether any holds something (the Trash place's icon).
// The main thread arms them (watch_trashes) whenever it reads the trash folders again.
//
// The main thread points the folder watch at a new folder (watch_dir); the thread owns the theme
// watches. inotify's calls are safe from both threads. A batch ends 100 ms after its last event,
// and never more than a second after its first, so a file being written all the time (a download)
// still shows its size once a second.

#include <poll.h>
#include <stdatomic.h>
#include <sys/inotify.h>

enum { WATCH_QUIET_MS = 100, WATCH_MAX_MS = 1000 };

static struct {
    int         fd;
    atomic_int  dir_wd;        // the open folder's watch, -1 for none
    atomic_int  theme_wd, current_wd;   // the thread sets them; watch_dir reads them (2026-10-28: plain ints)
    int         mount_fd, mount_wd;  // the mountinfo file polled, or the test's watched; -1 for none
    atomic_int  bm_wd;         // the bookmarks file's folder, -1 for none (F7.5)
    char        current[PATH_MAX];   // $XDG_STATE_HOME/omarchy/current
    char        theme[PATH_MAX + 8]; // .../current/theme
    atomic_int  trash_wd[TRASH_DIRS_MAX];   // each trash folder's files/, 0 for none (inotify's start at 1)
    atomic_bool list_ready, theme_ready, mounts_ready, trash_ready;
    atomic_int  batches;       // how many batches it raised, for the tests' state line
} watch = { .fd = -1, .dir_wd = -1, .theme_wd = -1, .current_wd = -1, .mount_fd = -1, .mount_wd = -1, .bm_wd = -1 };

// The mount list ofx reads: /proc/self/mountinfo, or the tests' OFX_TEST_MOUNTINFO.
static const char *mountinfo_path(void)
{
    const char *t = getenv("OFX_TEST_MOUNTINFO");
    return t && t[0] ? t : "/proc/self/mountinfo";
}

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

// The folder to watch now, in place of the last one; NULL: none (the search view, F9).
static void watch_dir(const char *dir)
{
    if (watch.fd < 0) return;
    int old = atomic_exchange(&watch.dir_wd, -1);
    // The same directory may be watched for the theme or as a trash too (inotify gives one wd per
    // inode): removing it would end that watch as well.
    if (old >= 0 && old != atomic_load(&watch.theme_wd) && old != atomic_load(&watch.current_wd) && !watch_is_trash(old)) inotify_rm_watch(watch.fd, old);
    atomic_store(&watch.dir_wd, dir ? inotify_add_watch(watch.fd, dir, WATCH_DIR_MASK) : -1);
}

// The bookmarks file's folder, watched unless it is already (or there is none yet).
static void watch_bookmarks(void)
{
    if (watch.fd < 0 || atomic_load(&watch.bm_wd) >= 0) return;
    char file[PATH_MAX];
    Str dir = path_dir(str_from_c(bookmarks_path(file, sizeof file)));
    char d[PATH_MAX];
    snprintf(d, sizeof d, "%.*s", STR_ARG(dir));
    atomic_store(&watch.bm_wd, inotify_add_watch(watch.fd, d, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ONLYDIR));
}

// The trash folders' files/ to watch now, in place of the last ones: the new ones added first, so
// none goes unwatched in between, then the old ones no longer wanted removed. IN_MASK_ADD: a files/
// that is also the open folder has one wd, and a plain add would replace the folder's mask with
// this smaller one (reviewer-2, 2026-10-30: a file written there no longer showed its size).
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
    for (int i = 0; i < TRASH_DIRS_MAX; i++) {
        bool kept = false;
        for (int k = 0; k < TRASH_DIRS_MAX; k++) kept |= old[i] == now[k];
        if (old[i] > 0 && !kept && old[i] != atomic_load(&watch.dir_wd)) inotify_rm_watch(watch.fd, old[i]);
    }
}

static void watch_theme_dirs(void)
{
    atomic_store(&watch.current_wd, inotify_add_watch(watch.fd, watch.current, WATCH_THEME_MASK));
    atomic_store(&watch.theme_wd, inotify_add_watch(watch.fd, watch.theme, WATCH_THEME_MASK));
}

// Reads what is waiting: which flags it calls for.
static void watch_read(bool *list, bool *theme, bool *mounts, bool *trash)
{
    _Alignas(struct inotify_event) char buf[16384];
    ssize_t len = read(watch.fd, buf, sizeof buf);
    bool rewatch = false;
    for (ssize_t at = 0; len > 0 && at < len;) {
        const struct inotify_event *e = (const struct inotify_event *)(buf + at);
        at += (ssize_t)(sizeof *e + e->len);
        if (e->mask & IN_Q_OVERFLOW) {
            *list = *theme = *mounts = *trash = true;
            continue;
        }
        if (watch.mount_wd >= 0 && e->wd == watch.mount_wd) *mounts = true;
        if (e->wd == atomic_load(&watch.bm_wd) && e->len && strcmp(e->name, "bookmarks") == 0) *mounts = true;   // the places again (F7.5)
        if (e->wd == atomic_load(&watch.dir_wd)) *list = true;
        if (!(e->mask & IN_IGNORED) && watch_is_trash(e->wd)) *trash = true;
        if (e->wd == atomic_load(&watch.theme_wd) && !(e->mask & IN_IGNORED)) *theme = true;
        if (e->wd == atomic_load(&watch.current_wd)) *theme = true, rewatch = true;
    }
    // The theme directory replaced, or made after ofx started: watch the new one.
    if (rewatch) atomic_store(&watch.theme_wd, inotify_add_watch(watch.fd, watch.theme, WATCH_THEME_MASK));
}

// One poll of inotify and the mountinfo file, for at most ms (-1: no limit): the flags it raises.
static void watch_poll(int ms, bool *list, bool *theme, bool *mounts, bool *trash)
{
    struct pollfd p[2] = { { watch.fd, POLLIN, 0 }, { watch.mount_fd, POLLPRI, 0 } };
    if (poll(p, 2, ms) <= 0) return;
    if (p[0].revents & POLLIN) watch_read(list, theme, mounts, trash);
    if (p[1].revents & (POLLPRI | POLLERR)) *mounts = true;
}

static void *watch_thread(void *arg)
{
    (void)arg;
    // Its signal mask is the main thread's (SIGTERM and SIGINT blocked), so a quit still goes to
    // main.c's signal_waiter.
    watch_theme_dirs();
    for (;;) {
        bool list = false, theme = false, mounts = false, trash = false;
        watch_poll(-1, &list, &theme, &mounts, &trash);
        if (!list && !theme && !mounts && !trash) continue;
        i64 first = now_ms(), last = first;
        for (i64 now; (now = now_ms()) < ui_batch_due(first, last, WATCH_QUIET_MS, WATCH_MAX_MS);) {
            bool l2 = false, t2 = false, m2 = false, r2 = false;
            watch_poll((int)(ui_batch_due(first, last, WATCH_QUIET_MS, WATCH_MAX_MS) - now), &l2, &t2, &m2, &r2);
            if (l2 || t2 || m2 || r2) last = now_ms();
            list |= l2, theme |= t2, mounts |= m2, trash |= r2;
        }
        if (list) atomic_store(&watch.list_ready, true);
        if (theme) atomic_store(&watch.theme_ready, true);
        if (mounts) atomic_store(&watch.mounts_ready, true);
        if (trash) atomic_store(&watch.trash_ready, true);
        atomic_fetch_add(&watch.batches, 1);
        wake_main();
    }
    return NULL;
}

// False when inotify is unavailable: ofx then works without refreshing.
static bool watch_start(void)
{
    watch.fd = inotify_init1(IN_CLOEXEC);
    if (watch.fd < 0) return false;
    char dir[PATH_MAX];
    theme_dir(dir, sizeof dir);
    snprintf(watch.theme, sizeof watch.theme, "%s", dir);
    snprintf(watch.current, sizeof watch.current, "%.*s", STR_ARG(path_dir(str_from_c(dir))));
    const char *mi = mountinfo_path();
    if (strcmp(mi, "/proc/self/mountinfo") == 0) watch.mount_fd = open(mi, O_RDONLY | O_CLOEXEC);
    else watch.mount_wd = inotify_add_watch(watch.fd, mi, IN_CLOSE_WRITE | IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF);
    watch_bookmarks();
    pthread_t t;
    if (pthread_create(&t, NULL, watch_thread, NULL) != 0) return false;
    pthread_detach(t);
    return true;
}
