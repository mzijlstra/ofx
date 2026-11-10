// The Properties panel's counts (F7.2), and Clear Previews' (F12): the files, folders and bytes
// under what it shows, counted all the way down (fs/props.c, du_walk) on a thread of its own, the
// sums showing as they grow.
// One walk at a time: a new one (or the panel closing) moves the generation, which the old walk
// sees at its next entry and stops. Like counts.c's, the thread is never joined at quit if it
// is stuck. Included by main.c.

enum { WALK_ROOTS_MAX = 512 };

static struct {
    pthread_t       thread;
    bool            started, quit;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    atomic_uint     gen;      // the walk wanted; a walk with another stops
    bool            asked;    // a walk for gen waits to start
    char            req[WALK_ROOTS_MAX][PATH_MAX];   // its roots, written by the main thread
    isize           nreq;
    DuSum           sum;      // the sums for sum_gen so far
    u32             sum_gen;
    bool            done, fresh;   // the walk for sum_gen ended; sums came since walk_take
} walk = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER };

static char    walk_roots[WALK_ROOTS_MAX][PATH_MAX];   // the thread's copy of the roots
static DuInode walk_inodes[DU_INODES];                 // the linked files the walk has seen

// The sums so far, every DU_TICK entries; the window woken for them at most every
// WALK_WAKE_MS, as search.c and jobs.c do (each wake draws a frame; 2026-10-28).
enum { WALK_WAKE_MS = 100 };

static void walk_tick(void *ctx, const DuSum *s)
{
    static i64 woke;   // (the walk thread's own)
    u32 gen = (u32)(uintptr_t)ctx;
    pthread_mutex_lock(&walk.mu);
    if (gen == atomic_load(&walk.gen)) walk.sum = *s, walk.sum_gen = gen, walk.fresh = true;
    i64 now = now_ms();
    if (now - woke >= WALK_WAKE_MS) wake_main(), woke = now;
    pthread_mutex_unlock(&walk.mu);
}

static void *walk_loop(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&walk.mu);
    for (;;) {
        while (!walk.quit && !walk.asked) pthread_cond_wait(&walk.cv, &walk.mu);
        if (walk.quit) break;
        u32 gen = atomic_load(&walk.gen);
        isize n = walk.nreq;
        for (isize i = 0; i < n; i++) memcpy(walk_roots[i], walk.req[i], sizeof walk_roots[i]);
        walk.asked = false;
        pthread_mutex_unlock(&walk.mu);
        const char *roots[WALK_ROOTS_MAX];
        for (isize i = 0; i < n; i++) roots[i] = walk_roots[i];
        static DuWalk w;   // (its mount points and path: ~140 KiB, not on the thread's stack)
        static u8 mi_mem[KiB(512)];
        static Arena mia;
        arena_init(&mia, "walk mounts", mi_mem, sizeof mi_mem);
        w = (DuWalk){ .gen = &walk.gen, .mine = gen, .tick = walk_tick, .ctx = (void *)(uintptr_t)gen, .inodes = walk_inodes,
                      .mounts = read_file(&mia, mountinfo_path()) };   // (the mount points under the roots: never stat'ed)
        DuSum s;
        bool whole = du_walk(&w, roots, n, &s);
        pthread_mutex_lock(&walk.mu);
        if (whole && gen == atomic_load(&walk.gen)) walk.sum = s, walk.sum_gen = gen, walk.done = walk.fresh = true;
        wake_main();
    }
    pthread_mutex_unlock(&walk.mu);
    return NULL;
}

static void walk_start(void)
{
    walk.started = pthread_create(&walk.thread, NULL, walk_loop, NULL) == 0;
}

// A new walk of roots (n of them, at most WALK_ROOTS_MAX), the old one stopped; n 0 only stops.
static void walk_ask(char *const *roots, isize n)
{
    pthread_mutex_lock(&walk.mu);
    atomic_fetch_add(&walk.gen, 1);
    walk.sum = (DuSum){ 0 }, walk.done = false, walk.fresh = true;
    walk.nreq = n < WALK_ROOTS_MAX ? n : WALK_ROOTS_MAX;
    for (isize i = 0; i < walk.nreq; i++) snprintf(walk.req[i], sizeof walk.req[i], "%s", roots[i]);
    walk.asked = n > 0 && walk.started;
    pthread_cond_signal(&walk.cv);
    pthread_mutex_unlock(&walk.mu);
}

// The sums so far and whether the walk ended; true when they changed since the last take.
static bool walk_take(DuSum *s, bool *done)
{
    pthread_mutex_lock(&walk.mu);
    bool came = walk.fresh;
    *s = walk.sum, *done = walk.done && walk.sum_gen == atomic_load(&walk.gen);
    walk.fresh = false;
    pthread_mutex_unlock(&walk.mu);
    return came;
}

// At quit, as count_quit and count_join: told to stop, joined by until, else left to the
// process's end.
static void walk_quit(void)
{
    worker_quit(walk.started, &walk.mu, &walk.cv, &walk.quit, &walk.gen);
}

static void walk_join(const struct timespec *until)
{
    if (walk.started) join_by(walk.thread, until);
}
