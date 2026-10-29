// The search view's walk (F9, docs/plan.md "F9 design"): fs/search.c's search_walk on a thread of
// its own, as Properties' du_walk (walk.c). One search at a time: a new one moves the generation,
// which the old walk sees at its next entry and stops. Its matches go into search_res, their names,
// folders and link targets into search_arena; the main thread copies out the ones published
// (search_copy), under the lock, which the walk takes only to publish more or to start again. Like
// walk.c's, the thread is never joined at quit if it is stuck. Included by main.c.

enum { SEARCH_PUBLISH_MS = 100 };   // the list grows at most this often while the walk runs

// What a search looks for: below root, names matching text with o, and inside files (step 3; empty:
// names only) lines matching inside with io; hidden files or not.
typedef struct {
    const char *root;
    Str         text, inside;
    UiMatchOpts o, io;
    bool        hidden;
} SearchQuery;

static struct {
    pthread_t       thread;
    bool            started, quit;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    atomic_uint     gen;      // the search wanted; a walk with another stops
    bool            asked;    // a walk for gen waits to start
    // The request, written by the main thread.
    char            root[PATH_MAX];
    char            text[UI_FIND_MAX + 1], inside[UI_FIND_MAX + 1];
    int             text_len, inside_len;
    UiMatchOpts     opts, inside_opts;
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
// The main thread's changes to what was published (step 2): results whose file went (trashed,
// deleted, moved: search_prune), and new names (search_rename), in an arena of their own. Only the
// main thread touches them, and only entries below n, which the walk is done with.
static u8    search_gone[SEARCH_MAX];
static u8    search_edit_mem[KiB(256)];
static Arena search_edits;
// The thread's own: its copy of the request, its matchers (names, lines inside files), the buffer it
// reads files into, how many results it wrote, the last folder it copied (results come a folder at
// a time), when it last published.
static char      search_root[PATH_MAX];
static UiMatcher search_m, search_im;
static char      search_buf[SEARCH_BUF];
static isize     search_wrote;
static Str       search_dir_last;
static i64       search_pub_at;

static void search_publish(SearchWalk *w, bool done)
{
    pthread_mutex_lock(&search.mu);
    if (w->mine == atomic_load(&search.gen)) {
        search.n = search_wrote, search.s = w->s, search.done = done, search.fresh = true;
        if (!search.quit) wake_main();
    }
    pthread_mutex_unlock(&search.mu);
    search_pub_at = now_ms();
}

static bool search_match_cb(void *ctx, Str name)
{
    (void)ctx;
    return ui_search_match(&search_m, name);
}

static bool search_line_cb(void *ctx, Str line, isize *from, isize *to)
{
    (void)ctx;
    return ui_match_span(&search_im, line, from, to);
}

// A hit copied into the arena, its text with it; NULL when there is no room.
static const SearchHit *search_hit_copy(Arena *a, const SearchHit *hit)
{
    SearchHit *h = arena_try_new(a, SearchHit, 1);
    char *p = h ? arena_try_push(a, hit->text.len + 1, 1) : NULL;
    if (!p) return NULL;
    memcpy(p, hit->text.p, (size_t)hit->text.len), p[hit->text.len] = 0;
    *h = *hit, h->text = (Str){ p, hit->text.len };
    return h;
}

static bool search_found_cb(void *ctx, Str dir, Str name, int fd, const SearchHit *hit)
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
    if (hit && !(search_res[search_wrote].hit = search_hit_copy(&search_arena, hit))) return false;
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
        Str text = { search.text, search.text_len }, inside = { search.inside, search.inside_len };
        ui_matcher_set(&search_m, text, search.opts);
        ui_matcher_set(&search_im, inside, search.inside_opts);
        bool hidden = search.hidden;
        search.asked = false;
        search.res_gen = gen, search.n = 0, search.s = (SearchSkips){ 0 }, search.done = false;
        pthread_mutex_unlock(&search.mu);
        // Nothing of the last walk is read any more (n is 0 for this generation).
        arena_reset(&search_arena);
        search_wrote = 0, search_dir_last = STR_EMPTY, search_pub_at = now_ms();
        SearchWalk w = { .gen = &search.gen, .mine = gen, .hidden = hidden, .match = search_match_cb,
                         .found = search_found_cb, .tick = search_tick_cb };
        if (inside.len) w.line = search_line_cb, w.buf = search_buf;
        w.ctx = &w;
        search_walk(&w, search_root);
        search_publish(&w, true);   // (stopped: another generation, nothing published)
        pthread_mutex_lock(&search.mu);
    }
    pthread_mutex_unlock(&search.mu);
    ui_matcher_set(&search_m, STR_EMPTY, (UiMatchOpts){ 0 });   // (a compiled expression freed)
    ui_matcher_set(&search_im, STR_EMPTY, (UiMatchOpts){ 0 });
    return NULL;
}

static void search_start(void)
{
    arena_init(&search_arena, "search", search_mem, sizeof search_mem);
    arena_init(&search_edits, "search edits", search_edit_mem, sizeof search_edit_mem);
    (void)ui_utf8_locale();   // made here, before the thread's matcher shares it
    search.started = pthread_create(&search.thread, NULL, search_loop, NULL) == 0;
}

static bool search_opts_eq(UiMatchOpts a, UiMatchOpts b)
{
    return a.cased == b.cased && a.word == b.word && a.regex == b.regex;
}

// Whether the search running (or done) is q (the toggles inside files only count with text there).
static bool search_is(SearchQuery q)
{
    pthread_mutex_lock(&search.mu);
    bool same = strcmp(search.root, q.root) == 0 && str_eq((Str){ search.text, search.text_len }, q.text) &&
                search_opts_eq(search.opts, q.o) && search.hidden == q.hidden && str_eq((Str){ search.inside, search.inside_len }, q.inside) &&
                (!q.inside.len || search_opts_eq(search.inside_opts, q.io));
    pthread_mutex_unlock(&search.mu);
    return same;
}

// t, at most UI_FIND_MAX bytes, into buf; its length.
static int search_text_put(char *buf, Str t)
{
    int n = (int)(t.len < UI_FIND_MAX ? t.len : UI_FIND_MAX);
    if (n) memcpy(buf, t.p, (size_t)n);   // (STR_EMPTY's p is NULL)
    buf[n] = 0;
    return n;
}

// A new search, the old one stopped; with neither text nor text inside files it only stops it (and
// finds nothing).
static void search_ask(SearchQuery q)
{
    pthread_mutex_lock(&search.mu);
    u32 gen = atomic_fetch_add(&search.gen, 1) + 1;
    snprintf(search.root, sizeof search.root, "%s", q.root);
    search.text_len = search_text_put(search.text, q.text);
    search.inside_len = search_text_put(search.inside, q.inside);
    search.opts = q.o, search.inside_opts = q.io, search.hidden = q.hidden;
    search.res_gen = gen, search.n = 0, search.s = (SearchSkips){ 0 }, search.fresh = true;
    memset(search_gone, 0, sizeof search_gone), arena_reset(&search_edits);
    search.done = (q.text.len == 0 && q.inside.len == 0) || !search.started;
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
    isize k = 0;
    for (isize i = 0; ok && i < n; i++) {
        if (search_gone[i]) continue;
        const Entry *r = &search_res[i];
        e[k] = *r;
        e[k].name = search_str(a, r->name, &ok), e[k].dir = search_str(a, r->dir, &ok), e[k].target = search_str(a, r->target, &ok);
        if (ok && r->hit) ok = (e[k].hit = search_hit_copy(a, r->hit)) != NULL;
        k++;
    }
    pthread_mutex_unlock(&search.mu);
    if (ok) out->e = e, out->n = k;
    return ok;
}

// Result i's file below root, into out; false when it doesn't fit.
static bool search_res_path(isize i, const char *root, char *out, size_t cap)
{
    const Entry *r = &search_res[i];
    int n = r->dir.len ? snprintf(out, cap, "%s%s%.*s/%.*s", root, strcmp(root, "/") == 0 ? "" : "/", STR_ARG(r->dir), STR_ARG(r->name))
                       : snprintf(out, cap, "%s%s%.*s", root, strcmp(root, "/") == 0 ? "" : "/", STR_ARG(r->name));
    return n >= 0 && (size_t)n < cap;
}

// After a change (step 2, from reload): the results whose file is gone left out, those back again
// (an undo) shown again; no watching, so this is when they go.
static void search_prune(const char *root)
{
    pthread_mutex_lock(&search.mu);
    isize n = search.res_gen == atomic_load(&search.gen) ? search.n : 0;
    pthread_mutex_unlock(&search.mu);
    for (isize i = 0; i < n; i++) {   // (below n the walk is done: no lock needed, reviewer-2)
        char p[PATH_MAX];
        struct stat st;
        search_gone[i] = search_res_path(i, root, p, sizeof p) && lstat(p, &st) != 0;
    }
}

// The result key names (entry_key: folder/name) renamed to name (step 2: the list shows the new
// name, its row kept); false when it isn't one or there is no room.
static bool search_rename(Str key, Str name)
{
    Str dir = path_dir(key), was = path_base(key);
    bool ok = false;
    pthread_mutex_lock(&search.mu);
    isize n = search.res_gen == atomic_load(&search.gen) ? search.n : 0;
    for (isize i = 0; i < n && !ok; i++) {
        Entry *r = &search_res[i];
        if (search_gone[i] || !str_eq(r->name, was) || !str_eq(r->dir, dir)) continue;
        char *p = arena_try_push(&search_edits, name.len + 1, 1);
        if (!p) break;
        memcpy(p, name.p, (size_t)name.len), p[name.len] = 0;
        r->name = (Str){ p, name.len }, ok = true;
    }
    pthread_mutex_unlock(&search.mu);
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
    struct timespec until = deadline_in(200);
    join_by(search.thread, &until);
}
