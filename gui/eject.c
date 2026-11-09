// Eject (F6.1): a removable drive's eject button runs `udisksctl unmount`, then `power-off`
// (spawn.c's eject_argv), on a thread of its own: the unmount writes out what is cached, which
// takes seconds on a USB stick. One at a time; the main loop takes the result (eject_take) and
// says it in the status part. The drive leaves the sidebar through the mount watch. Mount and
// Unmount (2026-10-30) run on the same thread, one of the three at a time: `udisksctl mount`, then
// the folder it names opens; `udisksctl unmount`, the drive left there to mount again. Included by
// main.c.

enum { EJECT_TIMEOUT_MS = 60000 };   // a USB stick's cache written out takes seconds, not a minute
// A mount may wait on polkit's password prompt, and a timeout doesn't cancel it (udisksd mounts when
// the answer comes, with no folder opened): five minutes (review, 2026-10-30).
enum { MOUNT_TIMEOUT_MS = 300000 };

// DRIVE_SHARE (F16 step 5): a share gvfs mounted, unmounted with `gio mount -u <its folder>`
// (measured: gio takes the FUSE folder for its share). gvfs unmounts it whatever is open in it; on a
// server that doesn't answer it takes gvfs's 20 s.
typedef enum { DRIVE_EJECT, DRIVE_MOUNT, DRIVE_UNMOUNT, DRIVE_SHARE } DriveOp;
static const char *const DRIVE_VERB[] = { "eject", "mount", "unmount", "unmount" }, *const DRIVE_DOING[] = { "Ejecting", "Mounting", "Unmounting", "Unmounting" };

static struct {
    pthread_mutex_t mu;
    bool            busy, ready;
    DriveOp         op;
    char            dev[PATH_MAX], label[256];   // (dev: a share's folder for DRIVE_SHARE)
    EjectResult     result;
    bool            mounted;              // a mount's result, with where
    char            at[PATH_MAX];
    char            why[sizeof(Err)];
} ej = { .mu = PTHREAD_MUTEX_INITIALIZER };

static void *eject_thread(void *arg)
{
    (void)arg;
    const char *argv[7];
    char udisks[PATH_MAX] = "udisksctl";   // (eject's and unmount's sh -c run it as resolved)
    if (!prog_resolve("udisksctl", udisks, sizeof udisks)) snprintf(udisks, sizeof udisks, "udisksctl");
    if (ej.op == DRIVE_SHARE) argv[0] = "gio", argv[1] = "mount", argv[2] = "-u", argv[3] = ej.dev, argv[4] = NULL;   // (dev: the share's folder)
    else if (ej.op == DRIVE_MOUNT) mount_argv(ej.dev, argv);
    else if (ej.op == DRIVE_UNMOUNT) unmount_argv(ej.dev, udisks, argv);
    else eject_argv(ej.dev, udisks, argv);   // dev and op are not written while busy
    static char out[4096];
    static char at[PATH_MAX];
    Err err;
    char why[sizeof(Err)];
    EjectResult r = EJECT_FAILED;
    bool late, mounted = false;
    at[0] = 0;
    int limit = ej.op == DRIVE_MOUNT ? MOUNT_TIMEOUT_MS : EJECT_TIMEOUT_MS;
    if (!spawn_capture(ej.op == DRIVE_SHARE ? NULL : udisks, (char *const *)argv, out, sizeof out, limit, &late, &err)) snprintf(why, sizeof why, "%s", err.msg);
    else if (ej.op == DRIVE_SHARE) r = gio_why(str_from_c(out), why, sizeof why) ? EJECT_FAILED : EJECT_DONE;
    else if (ej.op == DRIVE_MOUNT) mounted = mount_result(str_from_c(out), at, sizeof at, why, sizeof why);
    else r = eject_result(str_from_c(out), why, sizeof why);
    if (late) snprintf(why, sizeof why, "%s gave no answer in %d s", ej.op == DRIVE_SHARE ? "gio" : "udisksctl", limit / 1000), mounted = false, r = EJECT_FAILED;
    pthread_mutex_lock(&ej.mu);
    ej.result = r, ej.mounted = mounted, ej.ready = true;
    snprintf(ej.at, sizeof ej.at, "%s", at);
    snprintf(ej.why, sizeof ej.why, "%s", why);
    pthread_mutex_unlock(&ej.mu);
    wake_main();
    return NULL;
}

// Starts op on p; false, with the reason in the status part, when it can't now.
static bool drive_start(const Place *p, DriveOp op)
{
    pthread_mutex_lock(&ej.mu);
    bool busy = ej.busy;
    DriveOp was = ej.op;
    pthread_mutex_unlock(&ej.mu);
    if (busy) {
        set_status(true, "Still %sing %s", DRIVE_VERB[was], ej.label);
        return false;
    }
    if (!p->dev[0] && op != DRIVE_SHARE) {
        set_status(true, "%s: no device to %s", p->label, DRIVE_VERB[op]);
        return false;
    }
    if (op != DRIVE_MOUNT && jobs_refuse(p->path)) return false;   // a job of ours writes there: it says so
    Err gerr;
    if (!ops_program_allowed(op == DRIVE_SHARE ? "gio" : "udisksctl", &gerr)) return set_status(true, "%s", gerr.msg), false;
    snprintf(ej.dev, sizeof ej.dev, "%s", op == DRIVE_SHARE ? p->path : p->dev);
    snprintf(ej.label, sizeof ej.label, "%s", p->label);
    ej.busy = true, ej.ready = false, ej.op = op;
    pthread_t t;
    if (pthread_create(&t, NULL, eject_thread, NULL) != 0) {
        ej.busy = false;
        set_status(true, "cannot %s: %s", DRIVE_VERB[op], strerror(errno));
        return false;
    }
    pthread_detach(t);
    set_status(false, "%s %s\xE2\x80\xA6", DRIVE_DOING[op], p->label);
    return true;
}

static bool eject_start(const Place *p)
{
    return drive_start(p, DRIVE_EJECT);
}

static bool mount_start(const Place *p)
{
    return drive_start(p, DRIVE_MOUNT);
}

static bool unmount_start(const Place *p)
{
    return drive_start(p, DRIVE_UNMOUNT);
}

static bool share_unmount_start(const Place *p)
{
    return drive_start(p, DRIVE_SHARE);
}

// The result, when one came: said in the status part. True when one did.
static bool eject_take(void)
{
    pthread_mutex_lock(&ej.mu);
    bool got = ej.ready;
    EjectResult r = ej.result;
    DriveOp op = ej.op;
    bool mounted = ej.mounted;
    char why[sizeof(Err)], at[PATH_MAX];
    snprintf(why, sizeof why, "%s", ej.why);
    snprintf(at, sizeof at, "%s", ej.at);
    if (got) ej.ready = false, ej.busy = false;
    pthread_mutex_unlock(&ej.mu);
    if (!got) return false;
    // (A drive unmounted or ejected while its folder is shown has sent ofx Home already, as it left
    // the mount list: places_reload's drive_gone. So the message comes after, and stays.)
    if (op == DRIVE_SHARE) watch_gvfs_kick();   // (gone from the sidebar, and its folders Home, at once)
    if (op == DRIVE_UNMOUNT || op == DRIVE_SHARE) {
        if (r == EJECT_FAILED) set_status(true, "Can't unmount %s: %s", ej.label, why[0] ? why : "no reason given");
        else set_status(false, "%s is unmounted", ej.label);
        return true;
    }
    if (op == DRIVE_MOUNT) {   // mounted: its folder opens, as in Nemo
        if (!mounted) set_status(true, "Can't mount %s: %s", ej.label, why[0] ? why : "no reason given");
        else if (go_to(at, STR_EMPTY, 0)) set_status(false, "Mounted %s", ej.label);
        return true;
    }
    if (r == EJECT_DONE) set_status(false, "%s can be removed now", ej.label);
    else if (r == EJECT_UNMOUNTED) set_status(true, "%s is unmounted, but didn't power off: %s", ej.label, why[0] ? why : "no reason given");
    else set_status(true, "Can't eject %s: %s", ej.label, why[0] ? why : "no reason given");
    return true;
}
