// The search view's walk (F9, docs/plan.md "F9 design"): fs/search.c's search_walk on a thread of
// its own, as Properties' du_walk (walk.c). One search at a time: a new one moves the generation,
// which the old walk sees at its next entry and stops. Its matches go into search_res, their names,
// folders and link targets into search_arena; the main thread copies out the ones published
// (search_copy), under the lock, which the walk takes only to publish more or to start again. Like
// walk.c's, the thread is never joined at quit if it is stuck. Included by main.c.

enum { SEARCH_PUBLISH_MS = 100 };   // the list grows at most this often while the walk runs

static struct {
    pthread_t       thread;
    bool            started, quit;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    atomic_uint     gen;      // the search wanted; a walk with another stops
    bool            asked;    // a walk for gen waits to start
    // The request, written by the main thread.
    char            root[PATH_MAX];
    char            text[UI_FIND_MAX + 1];
    int             text_len;
    UiMatchOpts     opts;
    bool            hidden;
    // What res_gen's walk has published: search_res[0, n), what it left out so far, whether it ended.
    u32             res_gen;
    isize           n;
    SearchSkips     s;
    bool            done, fresh;   // fresh: published since search_take
} search = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER };

static Entry search_res[SEARCH_MAX];
static u8    search_mem[MiB(8)];
static Arena search_arena;
// The thread's own: its copy of the request, its matcher, how many results it wrote, the last
// folder it copied (results come a folder at a time), when it last published.
static char      search_root[PATH_MAX];
static UiMatcher search_m;
static isize     search_wrote;
static Str       search_dir_last;
static i64       search_pub_at;

static void search_publish(SearchWalk *w, bool done)
{
    pthread_mutex_lock(&search.mu);
    if (w->mine == atomic_load(&search.gen)) {
        search.n = search_wrote, search.s = w->s, search.done = done, search.fresh = true;
        if (!search.quit) glfwPostEmptyEvent();
    }
    pthread_mutex_unlock(&search.mu);
    search_pub_at = now_ms();
}

static bool search_match_cb(void *ctx, Str name)
{
    (void)ctx;
    return ui_search_match(&search_m, name);
}

static bool search_found_cb(void *ctx, Str dir, Str name, int fd)
{
    SearchWalk *w = ctx;
    if (search_wrote == SEARCH_MAX) return false;
    if (search_wrote == 0 || !str_eq(dir, search_dir_last)) {
        char *p = arena_try_push(&search_arena, dir.len + 1, 1);
        if (!p) return false;
        memcpy(p, dir.p, (size_t)dir.len), p[dir.len] = 0;
        search_dir_last = (Str){ p, dir.len };
    }
    if (!entry_read(&search_arena, fd, name.p, &search_res[search_wrote])) return false;
    search_res[search_wrote++].dir = search_dir_last;
    if (now_ms() - search_pub_at >= SEARCH_PUBLISH_MS) search_publish(w, false);
    return true;
}

static void search_tick_cb(void *ctx, const SearchSkips *s)
{
    (void)s;
    if (now_ms() - search_pub_at >= SEARCH_PUBLISH_MS) search_publish(ctx, false);
}

static void *search_loop(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&search.mu);
    for (;;) {
        while (!search.quit && !search.asked) pthread_cond_wait(&search.cv, &search.mu);
        if (search.quit) break;
        u32 gen = atomic_load(&search.gen);
        memcpy(search_root, search.root, sizeof search_root);
        Str text = { search.text, search.text_len };
        ui_matcher_set(&search_m, text, search.opts);
        bool hidden = search.hidden;
        search.asked = false;
        search.res_gen = gen, search.n = 0, search.s = (SearchSkips){ 0 }, search.done = false;
        pthread_mutex_unlock(&search.mu);
        // Nothing of the last walk is read any more (n is 0 for this generation).
        arena_reset(&search_arena);
        search_wrote = 0, search_dir_last = STR_EMPTY, search_pub_at = now_ms();
        SearchWalk w = { .gen = &search.gen, .mine = gen, .hidden = hidden, .match = search_match_cb,
                         .found = search_found_cb, .tick = search_tick_cb };
        w.ctx = &w;
        search_walk(&w, search_root);
        search_publish(&w, true);   // (stopped: another generation, nothing published)
        pthread_mutex_lock(&search.mu);
    }
    pthread_mutex_unlock(&search.mu);
    ui_matcher_set(&search_m, STR_EMPTY, (UiMatchOpts){ 0 });   // (a compiled expression freed)
    return NULL;
}

static void search_start(void)
{
    arena_init(&search_arena, "search", search_mem, sizeof search_mem);
    (void)ui_utf8_locale();   // made here, before the thread's matcher shares it
    search.started = pthread_create(&search.thread, NULL, search_loop, NULL) == 0;
}

// Whether the search running (or done) is for this root, text, toggles and hidden files.
static bool search_is(const char *root, Str text, UiMatchOpts o, bool hidden)
{
    pthread_mutex_lock(&search.mu);
    bool same = strcmp(search.root, root) == 0 && str_eq((Str){ search.text, search.text_len }, text) &&
                search.opts.cased == o.cased && search.opts.word == o.word && search.opts.regex == o.regex && search.hidden == hidden;
    pthread_mutex_unlock(&search.mu);
    return same;
}

// A new search below root, the old one stopped; empty text only stops it (and finds nothing).
static void search_ask(const char *root, Str text, UiMatchOpts o, bool hidden)
{
    pthread_mutex_lock(&search.mu);
    u32 gen = atomic_fetch_add(&search.gen, 1) + 1;
    snprintf(search.root, sizeof search.root, "%s", root);
    search.text_len = (int)(text.len < UI_FIND_MAX ? text.len : UI_FIND_MAX);
    memcpy(search.text, text.p, (size_t)search.text_len), search.text[search.text_len] = 0;
    search.opts = o, search.hidden = hidden;
    search.res_gen = gen, search.n = 0, search.s = (SearchSkips){ 0 }, search.fresh = true;
    search.done = text.len == 0 || !search.started;
    search.asked = !search.done;
    pthread_cond_signal(&search.cv);
    pthread_mutex_unlock(&search.mu);
}

// s copied into a (empty stays empty); *ok false when a is full.
static Str search_str(Arena *a, Str s, bool *ok)
{
    if (!s.len) return STR_EMPTY;
    char *p = arena_try_push(a, s.len + 1, 1);
    if (!p) return *ok = false, STR_EMPTY;
    memcpy(p, s.p, (size_t)s.len), p[s.len] = 0;
    return (Str){ p, s.len };
}

// The matches so far into out, copied into a (unsorted); *s what the walk left out, *done whether it
// ended. False when a is full.
static bool search_copy(Arena *a, Listing *out, SearchSkips *s, bool *done)
{
    *out = (Listing){ 0 };
    pthread_mutex_lock(&search.mu);
    isize n = search.res_gen == atomic_load(&search.gen) ? search.n : 0;
    *s = search.s, *done = search.done;
    Entry *e = arena_try_new(a, Entry, n + 1);
    bool ok = e != NULL;
    for (isize i = 0; ok && i < n; i++) {
        const Entry *r = &search_res[i];
        e[i] = *r;
        e[i].name = search_str(a, r->name, &ok), e[i].dir = search_str(a, r->dir, &ok), e[i].target = search_str(a, r->target, &ok);
    }
    pthread_mutex_unlock(&search.mu);
    if (ok) out->e = e, out->n = n;
    return ok;
}

// Whether more was published since the last take.
static bool search_take(void)
{
    pthread_mutex_lock(&search.mu);
    bool came = search.fresh;
    search.fresh = false;
    pthread_mutex_unlock(&search.mu);
    return came;
}

// At quit, as walk_stop: told to stop, 200 ms, then left to the process's end if stuck.
static void search_stop(void)
{
    if (!search.started) return;
    pthread_mutex_lock(&search.mu);
    search.quit = true;
    atomic_fetch_add(&search.gen, 1);
    pthread_cond_broadcast(&search.cv);
    pthread_mutex_unlock(&search.mu);
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += 200 * 1000000L;
    if (until.tv_nsec >= 1000000000L) until.tv_sec++, until.tv_nsec -= 1000000000L;
    if (pthread_timedjoin_np(search.thread, NULL, &until) != 0) pthread_detach(search.thread);
}
