// The drives' size and free space, and the folder shown's, on a thread of their own (review,
// F6 review): statvfs on a hung mount, or a USB disk spinning up, blocks in the kernel, and the
// window must not wait for it. The main thread asks with the paths it wants (space_ask, the latest
// ask replacing one not started) and takes the answer each loop (space_take); an answer is matched
// to the places by path, so places read again meanwhile keep what still applies. Included by
// main.c.

enum { SPACE_PATHS = PLACES_MAX + 1 };

static struct {
    pthread_t       thread;
    bool            started, quit;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    bool            asked;                       // a request not taken by the thread yet
    char            req[SPACE_PATHS][PATH_MAX];
    int             nreq;
    bool            ready;                       // an answer not taken by the main thread yet
    char            res[SPACE_PATHS][PATH_MAX];
    i64             size[SPACE_PATHS], avail[SPACE_PATHS];
    int             nres;
    int             delay_ms;   // OFX_TEST_SPACE_DELAY_MS: each answer takes that much longer (a hung drive)
} space = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER };

static void *space_loop(void *arg)
{
    (void)arg;
    static char path[SPACE_PATHS][PATH_MAX];
    static i64 size[SPACE_PATHS], avail[SPACE_PATHS];
    pthread_mutex_lock(&space.mu);
    for (;;) {
        while (!space.quit && !space.asked) pthread_cond_wait(&space.cv, &space.mu);
        if (space.quit) break;
        int n = space.nreq;
        memcpy(path, space.req, sizeof path[0] * (size_t)n);
        space.asked = false;
        pthread_mutex_unlock(&space.mu);
        if (space.delay_ms > 0) nanosleep(&(struct timespec){ space.delay_ms / 1000, (space.delay_ms % 1000) * 1000000L }, NULL);
        for (int i = 0; i < n; i++) fs_space(path[i], &size[i], &avail[i]);
        pthread_mutex_lock(&space.mu);
        memcpy(space.res, path, sizeof path[0] * (size_t)n);
        memcpy(space.size, size, sizeof size[0] * (size_t)n);
        memcpy(space.avail, avail, sizeof avail[0] * (size_t)n);
        space.nres = n, space.ready = true;
        wake_main();
    }
    pthread_mutex_unlock(&space.mu);
    return NULL;
}

static void space_start(void)
{
    space.delay_ms = env_int("OFX_TEST_SPACE_DELAY_MS", 0, 0, 30000);   // f6b: 30 s
    space.started = pthread_create(&space.thread, NULL, space_loop, NULL) == 0;
}

// At quit, as count_quit and count_join: told, joined by until, else left to the process's end (a
// statvfs stuck in the kernel).
static void space_quit(void)
{
    worker_quit(space.started, &space.mu, &space.cv, &space.quit, NULL);
}

static void space_join(const struct timespec *until)
{
    if (space.started) join_by(space.thread, until);
}

// The space of each drive in pl and of cwd, asked for.
static void space_ask(const Places *pl, const char *cwd)
{
    pthread_mutex_lock(&space.mu);
    int n = 0;
    for (int i = 0; i < pl->n && n < SPACE_PATHS - 1; i++)
        if (place_is_drive(pl->p[i].kind)) snprintf(space.req[n++], PATH_MAX, "%s", pl->p[i].path);
    if (cwd[0] == '/') snprintf(space.req[n++], PATH_MAX, "%s", cwd);
    space.nreq = n, space.asked = n > 0;
    if (space.asked) pthread_cond_signal(&space.cv);
    pthread_mutex_unlock(&space.mu);
}

// An answer, into the drives of pl it is about and *cwd_avail (when it is about cwd): true when
// one came.
static bool space_take(Places *pl, const char *cwd, i64 *cwd_avail)
{
    pthread_mutex_lock(&space.mu);
    bool got = space.ready;
    for (int k = 0; got && k < space.nres; k++) {
        for (int i = 0; i < pl->n; i++)
            if (place_is_drive(pl->p[i].kind) && strcmp(pl->p[i].path, space.res[k]) == 0)
                pl->p[i].size = space.size[k], pl->p[i].avail = space.avail[k];
        if (strcmp(cwd, space.res[k]) == 0) *cwd_avail = space.size[k] > 0 ? space.avail[k] : -1;
    }
    space.ready = false;
    pthread_mutex_unlock(&space.mu);
    return got;
}
