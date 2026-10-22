// Live refresh (docs/plan.md, "Live refresh"): a thread blocks on inotify and, once a batch of
// changes has settled, raises a flag and wakes the main loop. Watched: the open folder (not
// recursive), and the Omarchy theme, both its directory and the `current/` directory around it (a
// theme switch may replace the theme directory itself, which ends its watch: an event in
// `current/` watches it again).
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
    int         theme_wd, current_wd;
    char        current[PATH_MAX];   // $XDG_STATE_HOME/omarchy/current
    char        theme[PATH_MAX + 8]; // .../current/theme
    atomic_bool list_ready, theme_ready;
    atomic_int  batches;       // how many batches it raised, for the tests' state line
} watch = { .fd = -1, .dir_wd = -1, .theme_wd = -1, .current_wd = -1 };

static const u32 WATCH_DIR_MASK = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_MODIFY |
                                  IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;
static const u32 WATCH_THEME_MASK = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ONLYDIR;

// The folder to watch now, in place of the last one.
static void watch_dir(const char *dir)
{
    if (watch.fd < 0) return;
    int old = atomic_exchange(&watch.dir_wd, -1);
    // The same directory may be watched for the theme too (inotify gives one wd per inode):
    // removing it would end that watch as well.
    if (old >= 0 && old != watch.theme_wd && old != watch.current_wd) inotify_rm_watch(watch.fd, old);
    atomic_store(&watch.dir_wd, inotify_add_watch(watch.fd, dir, WATCH_DIR_MASK));
}

static void watch_theme_dirs(void)
{
    watch.current_wd = inotify_add_watch(watch.fd, watch.current, WATCH_THEME_MASK);
    watch.theme_wd = inotify_add_watch(watch.fd, watch.theme, WATCH_THEME_MASK);
}

// Reads what is waiting: which flags it calls for.
static void watch_read(bool *list, bool *theme)
{
    _Alignas(struct inotify_event) char buf[16384];
    ssize_t len = read(watch.fd, buf, sizeof buf);
    bool rewatch = false;
    for (ssize_t at = 0; len > 0 && at < len;) {
        const struct inotify_event *e = (const struct inotify_event *)(buf + at);
        at += (ssize_t)(sizeof *e + e->len);
        if (e->mask & IN_Q_OVERFLOW) {
            *list = *theme = true;
            continue;
        }
        if (e->wd == atomic_load(&watch.dir_wd)) *list = true;
        if (e->wd == watch.theme_wd && !(e->mask & IN_IGNORED)) *theme = true;
        if (e->wd == watch.current_wd) *theme = true, rewatch = true;
    }
    // The theme directory replaced, or made after ofx started: watch the new one.
    if (rewatch) watch.theme_wd = inotify_add_watch(watch.fd, watch.theme, WATCH_THEME_MASK);
}

static void *watch_thread(void *arg)
{
    (void)arg;
    // Its signal mask is the main thread's (SIGTERM and SIGINT blocked), so a quit still goes to
    // main.c's signal_waiter.
    watch_theme_dirs();
    struct pollfd p = { watch.fd, POLLIN, 0 };
    for (;;) {
        bool list = false, theme = false;
        if (poll(&p, 1, -1) <= 0) continue;
        watch_read(&list, &theme);
        if (!list && !theme) continue;
        i64 first = now_ms(), last = first;
        for (i64 now; (now = now_ms()) < ui_batch_due(first, last, WATCH_QUIET_MS, WATCH_MAX_MS);) {
            if (poll(&p, 1, (int)(ui_batch_due(first, last, WATCH_QUIET_MS, WATCH_MAX_MS) - now)) <= 0) continue;
            bool l2 = false, t2 = false;
            watch_read(&l2, &t2);
            if (l2 || t2) last = now_ms();
            list |= l2, theme |= t2;
        }
        if (list) atomic_store(&watch.list_ready, true);
        if (theme) atomic_store(&watch.theme_ready, true);
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
    pthread_t t;
    if (pthread_create(&t, NULL, watch_thread, NULL) != 0) return false;
    pthread_detach(t);
    return true;
}
