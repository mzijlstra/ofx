// Item counts for the folders on screen (docs/plan.md, F4 "Item counts"), on a thread of their
// own: reading a folder can be slow on a big or slow drive, and the window must not wait for it.
// The main thread asks for the rows it draws (count_ask) and takes the answers each loop
// (count_take); a new listing starts a new generation, and older answers are dropped. Included by
// main.c.

enum { COUNT_QUEUE = 64, COUNT_UNKNOWN = -2, COUNT_ASKED = -3 };

typedef struct {
    u32   gen;
    isize idx, n;
} CountAnswer;

static struct {
    pthread_t       thread;
    bool            started, quit;
    pthread_mutex_t mu;
    pthread_cond_t  cv;     // a request came, or there is room for an answer
    u32             gen;    // the listing's; requests and answers carry theirs
    struct { u32 gen; isize idx; bool hidden; char path[PATH_MAX]; } req[COUNT_QUEUE];
    int             rhead, rn;
    CountAnswer     res[COUNT_QUEUE];
    int             sn;
    int             delay_ms;   // FX_TEST_COUNT_DELAY_MS
} counts = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER };

static void *count_loop(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&counts.mu);
    for (;;) {
        while (!counts.quit && (counts.rn == 0 || counts.sn == COUNT_QUEUE)) pthread_cond_wait(&counts.cv, &counts.mu);
        if (counts.quit) break;
        int k = counts.rhead;
        u32 gen = counts.req[k].gen;
        isize idx = counts.req[k].idx;
        bool hidden = counts.req[k].hidden;
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s", counts.req[k].path);
        counts.rhead = (counts.rhead + 1) % COUNT_QUEUE, counts.rn--;
        pthread_mutex_unlock(&counts.mu);
        if (counts.delay_ms > 0) nanosleep(&(struct timespec){ counts.delay_ms / 1000, (counts.delay_ms % 1000) * 1000000L }, NULL);
        isize n = count_entries(path, hidden);
        pthread_mutex_lock(&counts.mu);
        if (gen == counts.gen) counts.res[counts.sn++] = (CountAnswer){ gen, idx, n };
        // Not after quit: the window and GLFW may be gone by the time a stuck count comes back.
        if (!counts.quit) glfwPostEmptyEvent();
    }
    pthread_mutex_unlock(&counts.mu);
    return NULL;
}

static void count_start(void)
{
    counts.started = pthread_create(&counts.thread, NULL, count_loop, NULL) == 0;
}

// At quit: the thread is told to stop and given 200 ms. A count stuck in the kernel (a hung network
// mount) must not keep fx from exiting, so it is then left to the process's end, never joined
// (reviewer-2, F4 review, 2026-10-20).
static void count_stop(void)
{
    if (!counts.started) return;
    pthread_mutex_lock(&counts.mu);
    counts.quit = true;
    pthread_cond_broadcast(&counts.cv);
    pthread_mutex_unlock(&counts.mu);
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += 200 * 1000000L;
    if (until.tv_nsec >= 1000000000L) until.tv_sec++, until.tv_nsec -= 1000000000L;
    if (pthread_timedjoin_np(counts.thread, NULL, &until) != 0) pthread_detach(counts.thread);
}

// A new listing: what was asked for the old one is dropped, and its answers with it.
static void count_new_listing(void)
{
    pthread_mutex_lock(&counts.mu);
    counts.gen++, counts.rn = 0, counts.sn = 0;
    pthread_mutex_unlock(&counts.mu);
}

// Ask for the count of entry idx at path; false when the queue is full (asked again next frame).
static bool count_ask(isize idx, const char *path, bool hidden)
{
    pthread_mutex_lock(&counts.mu);
    bool ok = counts.started && counts.rn < COUNT_QUEUE;
    if (ok) {
        int k = (counts.rhead + counts.rn) % COUNT_QUEUE;
        counts.req[k].gen = counts.gen, counts.req[k].idx = idx, counts.req[k].hidden = hidden;
        snprintf(counts.req[k].path, sizeof counts.req[k].path, "%s", path);
        counts.rn++;
        pthread_cond_signal(&counts.cv);
    }
    pthread_mutex_unlock(&counts.mu);
    return ok;
}

// The answers so far into count[] (n entries): true when there were any.
static bool count_take(isize *count, isize n)
{
    pthread_mutex_lock(&counts.mu);
    bool any = counts.sn > 0;
    for (int i = 0; i < counts.sn; i++)
        if (counts.res[i].gen == counts.gen && counts.res[i].idx < n) count[counts.res[i].idx] = counts.res[i].n;
    counts.sn = 0;
    pthread_cond_signal(&counts.cv);
    pthread_mutex_unlock(&counts.mu);
    return any;
}
