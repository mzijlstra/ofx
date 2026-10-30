// Eject (F6.1): a removable drive's eject button runs `udisksctl unmount`, then `power-off`
// (spawn.c's eject_argv), on a thread of its own: the unmount writes out what is cached, which
// takes seconds on a USB stick. One at a time; the main loop takes the result (eject_take) and
// says it in the status part. The drive leaves the sidebar through the mount watch. Mount
// (2026-10-30) runs on the same thread, one of the two at a time: `udisksctl mount`, then the folder
// it names opens. Included by main.c.

enum { EJECT_TIMEOUT_MS = 60000 };   // a USB stick's cache written out takes seconds, not a minute
// A mount may wait on polkit's password prompt, and a timeout doesn't cancel it (udisksd mounts when
// the answer comes, with no folder opened): five minutes (reviewer-2, 2026-10-30).
enum { MOUNT_TIMEOUT_MS = 300000 };

static struct {
    pthread_mutex_t mu;
    bool            busy, ready, mount;   // mount: this one mounts (the rest is eject's)
    char            dev[256], label[256];
    EjectResult     result;
    bool            mounted;              // a mount's result, with where
    char            at[PATH_MAX];
    char            why[sizeof(Err)];
} ej = { .mu = PTHREAD_MUTEX_INITIALIZER };

static void *eject_thread(void *arg)
{
    (void)arg;
    const char *argv[6];
    if (ej.mount) mount_argv(ej.dev, argv);
    else eject_argv(ej.dev, argv);   // dev and mount are not written while busy
    static char out[4096];
    static char at[PATH_MAX];
    Err err;
    char why[sizeof(Err)];
    EjectResult r = EJECT_FAILED;
    bool late, mounted = false;
    at[0] = 0;
    int limit = ej.mount ? MOUNT_TIMEOUT_MS : EJECT_TIMEOUT_MS;
    if (!spawn_capture((char *const *)argv, out, sizeof out, limit, &late, &err)) snprintf(why, sizeof why, "%s", err.msg);
    else if (ej.mount) mounted = mount_result(str_from_c(out), at, sizeof at, why, sizeof why);
    else r = eject_result(str_from_c(out), why, sizeof why);
    if (late) snprintf(why, sizeof why, "udisksctl gave no answer in %d s", limit / 1000), mounted = false;
    pthread_mutex_lock(&ej.mu);
    ej.result = r, ej.mounted = mounted, ej.ready = true;
    snprintf(ej.at, sizeof ej.at, "%s", at);
    snprintf(ej.why, sizeof ej.why, "%s", why);
    pthread_mutex_unlock(&ej.mu);
    wake_main();
    return NULL;
}

static bool drive_start(const Place *p, bool mount);

// Starts ejecting p; false, with the reason in the status part, when it can't now.
static bool eject_start(const Place *p)
{
    return drive_start(p, false);
}

// Starts ejecting or (mount) mounting p; false, with the reason in the status part, when it can't.
static bool drive_start(const Place *p, bool mount)
{
    pthread_mutex_lock(&ej.mu);
    bool busy = ej.busy, was = ej.mount;
    pthread_mutex_unlock(&ej.mu);
    if (busy) {
        set_status(true, "Still %s %s", was ? "mounting" : "ejecting", ej.label);
        return false;
    }
    if (!p->dev[0]) {
        set_status(true, "%s: no device to %s", p->label, mount ? "mount" : "eject");
        return false;
    }
    if (!mount && jobs_refuse(p->path)) return false;   // a job of ours writes there: it says so
    Err gerr;
    if (!ops_program_allowed("udisksctl", &gerr)) return set_status(true, "%s", gerr.msg), false;
    snprintf(ej.dev, sizeof ej.dev, "%s", p->dev);
    snprintf(ej.label, sizeof ej.label, "%s", p->label);
    ej.busy = true, ej.ready = false, ej.mount = mount;
    pthread_t t;
    if (pthread_create(&t, NULL, eject_thread, NULL) != 0) {
        ej.busy = false;
        set_status(true, "cannot %s: %s", mount ? "mount" : "eject", strerror(errno));
        return false;
    }
    pthread_detach(t);
    set_status(false, "%s %s\xE2\x80\xA6", mount ? "Mounting" : "Ejecting", p->label);
    return true;
}

static bool mount_start(const Place *p)
{
    return drive_start(p, true);
}

// The result, when one came: said in the status part. True when one did.
static bool eject_take(void)
{
    pthread_mutex_lock(&ej.mu);
    bool got = ej.ready;
    EjectResult r = ej.result;
    bool mount = ej.mount, mounted = ej.mounted;
    char why[sizeof(Err)], at[PATH_MAX];
    snprintf(why, sizeof why, "%s", ej.why);
    snprintf(at, sizeof at, "%s", ej.at);
    if (got) ej.ready = false, ej.busy = false;
    pthread_mutex_unlock(&ej.mu);
    if (!got) return false;
    if (mount) {   // mounted: its folder opens, as in Nemo
        if (!mounted) set_status(true, "Can't mount %s: %s", ej.label, why[0] ? why : "no reason given");
        else if (go_to(at, STR_EMPTY, 0)) set_status(false, "Mounted %s", ej.label);
        return true;
    }
    if (r == EJECT_DONE) set_status(false, "%s can be removed now", ej.label);
    else if (r == EJECT_UNMOUNTED) set_status(true, "%s is unmounted, but didn't power off: %s", ej.label, why[0] ? why : "no reason given");
    else set_status(true, "Can't eject %s: %s", ej.label, why[0] ? why : "no reason given");
    return true;
}
