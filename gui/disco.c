// Servers on the network (F16 step 7b, Michael 2026-11-10): the SMB servers avahi finds, listed in
// the sidebar's Network section (PL_SERVER); a click lists a server's shares (step 7a). A thread of
// its own asks `avahi-browse` (fs/connect.c avahi_smb_servers): one query on the network as it
// starts (about 1 s), then avahi's cache alone every DISCO_MS (25 ms, no traffic), so ofx puts no
// query on the network on a timer. A change reads the places again (mounts_ready, as the shares'
// listing). Without avahi-browse nothing is found and the thread ends (with avahi-browse but no
// avahi-daemon, each run finds nothing). In Airplane Mode (step 7d) it runs none; turned off, it
// makes one query again (the cache may be another network's), then reads the cache. In the tests,
// run-script's avahi-browse stand-in answers (OFX_TEST_AVAHI), never the real one: it would query
// the desktop's network. Included by main.c after watch.c.

enum { DISCO_MS = 30000, DISCO_LIMIT_MS = 5000 };   // (the query ends after avahi's "all for now")

static struct {
    pthread_mutex_t mu;
    pthread_cond_t  cv;     // Airplane Mode turned on or off (disco_wake)
    int             n;
    NetServer       s[NET_SERVERS_MAX];
    atomic_int      asks;   // avahi-browse runs so far (the scripts' discoasks=)
} disco = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER };

// Airplane Mode changed (airplane_apply): the thread looks again now.
static void disco_wake(void)
{
    pthread_mutex_lock(&disco.mu);
    pthread_cond_broadcast(&disco.cv);
    pthread_mutex_unlock(&disco.mu);
}

// One run of avahi-browse (cache: avahi's cache alone): false when it can't be run at all.
static bool disco_ask(bool cache)
{
    static char out[65536];
    bool late;
    Err e;
    // (-k: the service's type as it is, "_smb._tcp"; without it avahi-browse gives its name from its
    // database, "Microsoft Windows Network", which a translation changes. Found by the first run on
    // the real network: the stand-in had printed what ofx expected.)
    char *full[] = { "avahi-browse", "-k", "-t", "-r", "-p", "_smb._tcp", NULL };
    char *cached[] = { "avahi-browse", "-k", "-t", "-c", "-r", "-p", "_smb._tcp", NULL };
    if (!spawn_capture(NULL, cache ? cached : full, out, sizeof out, DISCO_LIMIT_MS, &late, &e)) return false;
    atomic_fetch_add(&disco.asks, 1);
    if (late) return true;   // (killed past its limit: the last list stands)
    static NetServer now[NET_SERVERS_MAX];
    int n = avahi_smb_servers(str_from_c(out), now, NET_SERVERS_MAX);
    pthread_mutex_lock(&disco.mu);
    bool changed = n != disco.n || memcmp(now, disco.s, (size_t)n * sizeof *now) != 0;
    if (changed) memcpy(disco.s, now, (size_t)n * sizeof *now), disco.n = n;
    pthread_mutex_unlock(&disco.mu);
    if (changed) atomic_store(&watch.mounts_ready, true), wake_main();
    return true;
}

static void *disco_thread(void *arg)
{
    (void)arg;
    static int every = -1;   // (OFX_TEST_DISCO_MS: a test's cache reads come sooner)
    if (every < 0) every = env_int("OFX_TEST_DISCO_MS", DISCO_MS, 100, 600000);
    for (bool cache = false;; cache = true) {
        pthread_mutex_lock(&disco.mu);
        while (atomic_load(&airplane)) cache = false, pthread_cond_wait(&disco.cv, &disco.mu);   // (off again: a real query)
        pthread_mutex_unlock(&disco.mu);
        if (!disco_ask(cache)) break;
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        i64 ns = until.tv_nsec + (i64)(every % 1000) * 1000000L;
        until.tv_sec += every / 1000 + ns / 1000000000L, until.tv_nsec = ns % 1000000000L;
        pthread_mutex_lock(&disco.mu);
        int r = 0;
        while (!atomic_load(&airplane) && r != ETIMEDOUT) r = pthread_cond_timedwait(&disco.cv, &disco.mu, &until);   // (Airplane Mode on: stops waiting)
        pthread_mutex_unlock(&disco.mu);
    }
    return NULL;
}

// The servers found, copied into s (NET_SERVERS_MAX of them at most); their count.
static int disco_servers(NetServer *s)
{
    pthread_mutex_lock(&disco.mu);
    int n = disco.n;
    memcpy(s, disco.s, (size_t)n * sizeof *s);
    pthread_mutex_unlock(&disco.mu);
    return n;
}

static void disco_start(void)
{
    // (under the test guard only a run that gives a stand-in looks: the others' spawn logs and
    // counts stay as they were)
    if (getenv("OFX_GUI_ROOT") && !getenv("OFX_TEST_AVAHI")) return;
    pthread_t t;
    if (pthread_create(&t, NULL, disco_thread, NULL) == 0) pthread_detach(t);
}
