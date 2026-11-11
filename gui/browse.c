// Browse Network (F16 step 7e, Michael 2026-11-11): the SMB and SFTP servers avahi finds, listed in
// the files area as a view of its own (NETWORK_URI), asked of the network only when the view is
// visited or reloaded (Ctrl+R): nothing asks on its own (7b's discovery as each window started, and
// 7d's Airplane Mode to stop it, are gone). A query is one `avahi-browse -k -t -r -p <type>` a type
// (fs/connect.c NET_TYPES), both started at once, read by a thread of its own for BROWSE_LIMIT_MS at
// most (avahi answers in about a second, then "all for now" ends it). The answer replaces the last;
// the view lists it when it comes (browse_take). In the tests, run-script's avahi-browse stand-in
// answers (OFX_TEST_AVAHI), never the real one: it would ask the desktop's network. Under the
// guard without one, no query runs (the answer is none). Included by main.c after watch.c.

enum { BROWSE_LIMIT_MS = 5000 };

static struct {
    pthread_mutex_t mu;
    bool            running;   // a query on its way (one at a time; a reload meanwhile waits for it)
    bool            again;     // asked again while one ran: another when it ends
    bool            answered;  // a query has answered since the window started
    int             n;
    NetServer       s[NET_SERVERS_MAX];
    atomic_bool     ready;     // an answer the main loop hasn't taken (browse_take)
    atomic_int      asks;      // queries so far (the scripts' browseasks=)
} browse = { .mu = PTHREAD_MUTEX_INITIALIZER };

// One query: both types asked at once, each read until it ends or the limit; what came, parsed.
static int browse_query(NetServer *s)
{
    Child c[COUNT(NET_TYPES)];
    bool on[COUNT(NET_TYPES)] = { false };
    static char out[COUNT(NET_TYPES)][32768];
    isize len[COUNT(NET_TYPES)] = { 0 };
    Err err;
    for (int t = 0; t < COUNT(NET_TYPES); t++) {
        // (-k: the service's type as it is, "_smb._tcp"; without it avahi-browse gives its name from
        // its database, which a translation changes: found by 7b's first run on the real network)
        char *argv[] = { "avahi-browse", "-k", "-t", "-r", "-p", (char *)NET_TYPES[t].type, NULL };
        on[t] = child_start(argv, false, false, &c[t], &err);
    }
    i64 until = now_ms() + BROWSE_LIMIT_MS;
    int n = 0;
    for (int t = 0; t < COUNT(NET_TYPES); t++) {
        if (!on[t]) continue;
        bool ended = child_read(&c[t], out[t], sizeof out[t] - 1, &len[t], until);
        out[t][len[t]] = 0;
        close(c[t].out);
        if (!ended) syscall(SYS_pidfd_send_signal, c[t].pidfd, SIGKILL, NULL, 0);   // (past the limit: what it said so far counts)
        close(c[t].pidfd);
        avahi_servers((Str){ out[t], len[t] }, s, &n, NET_SERVERS_MAX);
    }
    return n;
}

static void *browse_thread(void *arg)
{
    (void)arg;
    static NetServer now[NET_SERVERS_MAX];
    for (;;) {
        int n = browse_query(now);
        atomic_fetch_add(&browse.asks, 1);
        pthread_mutex_lock(&browse.mu);
        memcpy(browse.s, now, (size_t)n * sizeof *now), browse.n = n, browse.answered = true;
        bool again = browse.again;
        browse.again = false, browse.running = again;
        pthread_mutex_unlock(&browse.mu);
        atomic_store(&browse.ready, true), wake_main();
        if (!again) return NULL;
    }
}

// A query started (the view visited, or reloaded): one at a time, another after it when asked
// meanwhile.
static void browse_start(void)
{
    pthread_mutex_lock(&browse.mu);
    // (under the test guard only a run that gives a stand-in asks: the others' spawn logs stay as
    // they were, and their view answers at once with none)
    if (getenv("OFX_GUI_ROOT") && !getenv("OFX_TEST_AVAHI")) {
        browse.n = 0, browse.answered = true;
        pthread_mutex_unlock(&browse.mu);
        atomic_store(&browse.ready, true);
        return;
    }
    if (browse.running) browse.again = true;
    else {
        pthread_t t;
        browse.running = pthread_create(&t, NULL, browse_thread, NULL) == 0;
        if (browse.running) pthread_detach(t);
    }
    pthread_mutex_unlock(&browse.mu);
}

// Whether a query is on its way (the view says "Searching…" until it answers).
static bool browse_busy(void)
{
    pthread_mutex_lock(&browse.mu);
    bool busy = browse.running;
    pthread_mutex_unlock(&browse.mu);
    return busy;
}

// The servers found by the last query, copied into s; their count.
static int browse_servers(NetServer *s)
{
    pthread_mutex_lock(&browse.mu);
    int n = browse.n;
    memcpy(s, browse.s, (size_t)n * sizeof *s);
    pthread_mutex_unlock(&browse.mu);
    return n;
}

// Browse Network's listing: the last query's servers as rows into a (their name, a server's icon,
// Kind in the size column's place and Address in the date's: Entry.net), none while the first
// query is on its way. Sorting by those columns sorts by kind and by address (size and mtime carry
// them: the kind's number, an IPv4 address as a number, IPv6 after).
static bool browse_list(Arena *a, Sort sort, Listing *l, Err *err)
{
    static NetServer s[NET_SERVERS_MAX];
    int n = browse_servers(s);
    Entry *e = arena_try_new(a, Entry, n + 1);
    NetServer *keep = arena_try_new(a, NetServer, n + 1);
    if (!e || !keep) return err_set(err, "Browse Network: too many servers for memory"), false;
    memcpy(keep, s, (size_t)n * sizeof *s);
    for (int i = 0; i < n; i++) {
        unsigned b[4];
        i64 ip = sscanf(keep[i].ip, "%u.%u.%u.%u", &b[0], &b[1], &b[2], &b[3]) == 4 ? (i64)b[0] << 24 | (i64)b[1] << 16 | (i64)b[2] << 8 | b[3] : (i64)1 << 40;
        e[i] = (Entry){ .name = str_from_c(keep[i].name), .kind = K_DIR, .size = keep[i].kind, .mtime = ip, .net = &keep[i] };
    }
    *l = (Listing){ e, n, 0 };
    fs_sort(l, sort);
    return true;
}
