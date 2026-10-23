// Eject (F6.1): a removable drive's eject button runs `udisksctl unmount`, then `power-off`
// (spawn.c's eject_argv), on a thread of its own: the unmount writes out what is cached, which
// takes seconds on a USB stick. One at a time; the main loop takes the result (eject_take) and
// says it in the status part. The drive leaves the sidebar through the mount watch. Included by
// main.c.

static struct {
    pthread_mutex_t mu;
    bool            busy, ready;
    char            dev[256], label[256];
    EjectResult     result;
    char            why[sizeof(Err)];
} ej = { .mu = PTHREAD_MUTEX_INITIALIZER };

static void *eject_thread(void *arg)
{
    (void)arg;
    const char *argv[6];
    eject_argv(ej.dev, argv);   // dev is not written while busy
    static char out[4096];
    Err err;
    char why[sizeof(Err)];
    EjectResult r = EJECT_FAILED;
    if (spawn_capture((char *const *)argv, out, sizeof out, &err)) r = eject_result(str_from_c(out), why, sizeof why);
    else snprintf(why, sizeof why, "%s", err.msg);
    pthread_mutex_lock(&ej.mu);
    ej.result = r, ej.ready = true;
    snprintf(ej.why, sizeof ej.why, "%s", why);
    pthread_mutex_unlock(&ej.mu);
    wake_main();
    return NULL;
}

// Starts ejecting p; false, with the reason in the status part, when it can't now.
static bool eject_start(const Place *p)
{
    pthread_mutex_lock(&ej.mu);
    bool busy = ej.busy;
    pthread_mutex_unlock(&ej.mu);
    if (busy) {
        set_status(true, "Still ejecting %s", ej.label);
        return false;
    }
    if (!p->dev[0]) {
        set_status(true, "%s: no device to eject", p->label);
        return false;
    }
    if (jobs_refuse(p->path)) return false;   // a job of ours writes there: it says so
    snprintf(ej.dev, sizeof ej.dev, "%s", p->dev);
    snprintf(ej.label, sizeof ej.label, "%s", p->label);
    ej.busy = true, ej.ready = false;
    pthread_t t;
    if (pthread_create(&t, NULL, eject_thread, NULL) != 0) {
        ej.busy = false;
        set_status(true, "cannot eject: %s", strerror(errno));
        return false;
    }
    pthread_detach(t);
    set_status(false, "Ejecting %s\xE2\x80\xA6", p->label);
    return true;
}

// The result, when one came: said in the status part. True when one did.
static bool eject_take(void)
{
    pthread_mutex_lock(&ej.mu);
    bool got = ej.ready;
    EjectResult r = ej.result;
    char why[sizeof(Err)];
    snprintf(why, sizeof why, "%s", ej.why);
    if (got) ej.ready = false, ej.busy = false;
    pthread_mutex_unlock(&ej.mu);
    if (!got) return false;
    if (r == EJECT_DONE) set_status(false, "%s can be removed now", ej.label);
    else if (r == EJECT_UNMOUNTED) set_status(true, "%s is unmounted, but didn't power off: %s", ej.label, why[0] ? why : "no reason given");
    else set_status(true, "Can't eject %s: %s", ej.label, why[0] ? why : "no reason given");
    return true;
}
