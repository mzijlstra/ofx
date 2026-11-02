// Previews in the icon view (F6.4, docs/plan.md "F6.4 design"): two workers run fs/thumb.c's
// thumb_get, so a folder of photos fills in while the window keeps its frames. Included by main.c.
//
// The main thread asks for the cells it draws, and a page ahead (thumb_for), each frame afresh:
// what the last frame asked and no worker took yet is dropped at the next (thumbs_frame), so the
// queue is only ever what is on screen now, in drawing order, whatever the folder, zoom or view.
// An answer is remembered under "<px>:<path>" with the file's mtime, the texture or "none": asked
// again only when the mtime changes (the rule from Nemo's loop), or when its texture was dropped
// to keep at most THUMB_HELD_MAX bytes of them (the least recently drawn first).

enum { THUMB_WORKERS = 2, THUMB_QUEUE = 64, THUMB_DONE = 8, THUMB_PX_MAX = 512, THUMB_ENTRIES = 8192,
       THUMB_HELD_MAX = MiB(64), THUMB_WORK_MEM = MiB(192) };

typedef enum { TS_UNKNOWN, TS_ASKED, TS_SHOWN, TS_NONE, TS_DROPPED } ThumbState;

typedef struct {
    Str        key;     // "<px>:<path>", in thumbs.arena
    i64        mtime;   // the file's, when answered
    ThumbState state;
    Texture2D  tex;     // TS_SHOWN
    i64        drawn;   // the frame it was last drawn in
} ThumbEntry;

typedef struct {
    u32       gen;
    int       entry;
    i64       mtime, size;
    ThumbKind kind;
    const char *exec;    // TK_EXEC: its thumbnailer's Exec line, in thumbers (loaded once, at the start)
    bool      make;
    int       px;
    i64       src_max;   // the largest file it may make one of (ofx.conf's preview-max-size, F10)
    char      path[PATH_MAX];
} ThumbReq;

typedef struct {
    u32      gen;
    int      entry;
    i64      mtime;
    ThumbGot got;
    int      w, h;
    u8       px[THUMB_PX_MAX * THUMB_PX_MAX * 4];
} ThumbDone;

static u8 thumb_entry_mem[MiB(4)];                       // the entries' keys
static Thumbers thumbers;                                 // the system's thumbnailers (F12; read at the start)
// Each worker's, reset per preview: a 40 MP PNG decodes whole into 160 MiB of it. Pages past the
// first THUMB_WORK_KEEP are handed back after a preview that used them, so a big one doesn't
// stay resident.
enum { THUMB_WORK_KEEP = MiB(16) };
static _Alignas(4096) u8 thumb_work_mem[THUMB_WORKERS][THUMB_WORK_MEM];

static struct {
    // The main thread's.
    ThumbEntry e[THUMB_ENTRIES];
    int        n;
    StrMap     of;
    Arena      arena;
    bool       full;        // the entries or their keys ran out: all dropped at the next frame
    isize      held, held_max;
    i64        frame;
    char       root[PATH_MAX];   // the cache; "" when there is no HOME to find it from
    int        got[TG_CHANGED + 1];   // answers taken, by kind, for the tests
    int        drawn_last, drawn_now;   // previews drawn in the last frame, and in this one
    int        resets, evicted;
    bool       off;        // ofx.conf's previews = off (F10): none made or read
    bool       exec_on;    // ofx.conf's preview-thumbnailers (F12)
    i64        src_max;    // ofx.conf's preview-max-size

    // Shared, under mu.
    pthread_t       th[THUMB_WORKERS];
    bool            started[THUMB_WORKERS], quit;
    pthread_mutex_t mu;
    pthread_cond_t  cv;      // a request came, or there is room for an answer
    u32             gen;     // bumped by a reset: older answers are dropped
    ThumbReq        req[THUMB_QUEUE];
    int             rhead, rn, busy;   // busy: requests a worker took and hasn't answered
    ThumbDone       done[THUMB_DONE];
    int             dn;
} thumbs = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER };

static void *thumb_loop(void *arg)
{
    int w = (int)(intptr_t)arg;
    Arena a;
    arena_init(&a, "thumb worker", thumb_work_mem[w], sizeof thumb_work_mem[w]);
    pthread_mutex_lock(&thumbs.mu);
    for (;;) {
        while (!thumbs.quit && thumbs.rn == 0) pthread_cond_wait(&thumbs.cv, &thumbs.mu);
        if (thumbs.quit) break;
        ThumbReq r = thumbs.req[thumbs.rhead];
        thumbs.rhead = (thumbs.rhead + 1) % THUMB_QUEUE, thumbs.rn--, thumbs.busy++;
        pthread_mutex_unlock(&thumbs.mu);
        arena_reset(&a);
        Img out = { 0 };
        ThumbAsk q = { .path = r.path, .mtime = r.mtime, .size = r.size, .kind = r.kind, .exec = r.exec, .make = r.make, .px = r.px, .root = thumbs.root,
                       .src_max = r.src_max };
        ThumbGot got = thumb_get(&a, &q, &out);
        isize used = a.used;
        pthread_mutex_lock(&thumbs.mu);
        while (!thumbs.quit && thumbs.dn == THUMB_DONE) pthread_cond_wait(&thumbs.cv, &thumbs.mu);
        thumbs.busy--;
        if (thumbs.quit) break;
        ThumbDone *d = &thumbs.done[thumbs.dn++];
        d->gen = r.gen, d->entry = r.entry, d->mtime = r.mtime, d->got = got, d->w = d->h = 0;
        if ((got == TG_CACHED || got == TG_MADE) && out.w <= THUMB_PX_MAX && out.h <= THUMB_PX_MAX)
            d->w = out.w, d->h = out.h, memcpy(d->px, out.px, (size_t)out.w * out.h * 4);
        wake_main();
        if (used > THUMB_WORK_KEEP) {
            pthread_mutex_unlock(&thumbs.mu);   // the pixels are copied out: the arena is free
            madvise(thumb_work_mem[w] + THUMB_WORK_KEEP, THUMB_WORK_MEM - THUMB_WORK_KEEP, MADV_DONTNEED);
            pthread_mutex_lock(&thumbs.mu);
        }
    }
    pthread_mutex_unlock(&thumbs.mu);
    return NULL;
}

static void thumbs_start(Arena *scratch)   // (scratch: for reading the thumbnailers)
{
    if (!thumb_root(thumbs.root, sizeof thumbs.root)) thumbs.root[0] = 0;
    arena_init(&thumbs.arena, "thumbs", thumb_entry_mem, sizeof thumb_entry_mem);
    strmap_init(&thumbs.of, &thumbs.arena, THUMB_ENTRIES * 2);
    // OFX_TEST_THUMB_HELD_MAX: fewer texture bytes, so a test sees previews dropped and asked again.
    const char *m = getenv("OFX_TEST_THUMB_HELD_MAX");
    thumbs.held_max = m && atoi(m) > 0 ? atoi(m) : THUMB_HELD_MAX;
    // OFX_TEST_THUMB_RUN_MS: a test's hung fake thumbnailer killed sooner; OFX_TEST_SANDBOX=off, under
    // the guard only: the run without bubblewrap.
    thumb_run_ms = env_int("OFX_TEST_THUMB_RUN_MS", THUMB_RUN_MS, 1, THUMB_RUN_MS);
    const char *sb = getenv("OFX_TEST_SANDBOX");
    thumb_sandbox = !(ops_root[0] && sb && strcmp(sb, "off") == 0);
    thumbers_load(scratch, &thumbers);
    for (int i = 0; i < THUMB_WORKERS; i++) thumbs.started[i] = pthread_create(&thumbs.th[i], NULL, thumb_loop, (void *)(intptr_t)i) == 0;
}

// At quit, as count_quit and count_join: the workers told, joined by until, and a stuck one (a hung
// drive) left to the process's end; then the textures go.
static void thumbs_quit(void)
{
    pthread_mutex_lock(&thumbs.mu);
    thumbs.quit = true;
    pthread_cond_broadcast(&thumbs.cv);
    pthread_mutex_unlock(&thumbs.mu);
}

static void thumbs_join(const struct timespec *until)
{
    for (int i = 0; i < THUMB_WORKERS; i++)
        if (thumbs.started[i]) join_by(thumbs.th[i], until);
    for (int i = 0; i < thumbs.n; i++)
        if (thumbs.e[i].state == TS_SHOWN) UnloadTexture(thumbs.e[i].tex), thumbs.e[i].state = TS_NONE;
}

// Every texture and entry dropped; answers on the way are dropped as they come.
static void thumbs_reset(void)
{
    for (int i = 0; i < thumbs.n; i++)
        if (thumbs.e[i].state == TS_SHOWN) UnloadTexture(thumbs.e[i].tex);
    thumbs.n = 0, thumbs.held = 0, thumbs.full = false, thumbs.resets++;
    arena_reset(&thumbs.arena);
    strmap_init(&thumbs.of, &thumbs.arena, THUMB_ENTRIES * 2);
    pthread_mutex_lock(&thumbs.mu);
    thumbs.gen++, thumbs.rn = 0, thumbs.dn = 0;
    pthread_cond_broadcast(&thumbs.cv);
    pthread_mutex_unlock(&thumbs.mu);
}

// Before a frame's layout: what no worker took yet is dropped (it is asked again if still drawn).
static void thumbs_frame(void)
{
    if (thumbs.full) thumbs_reset();
    thumbs.frame++;
    thumbs.drawn_last = thumbs.drawn_now, thumbs.drawn_now = 0;
    pthread_mutex_lock(&thumbs.mu);
    for (int k = 0; k < thumbs.rn; k++) {
        int i = thumbs.req[(thumbs.rhead + k) % THUMB_QUEUE].entry;
        if (thumbs.e[i].state == TS_ASKED) thumbs.e[i].state = TS_UNKNOWN;
    }
    thumbs.rn = 0;
    pthread_mutex_unlock(&thumbs.mu);
}

// The preview of the file at path (mtime, size, its MIME type) drawn at px device pixels: the
// texture when there is one, else NULL, and asked for when not known yet. local: its drive is one
// ofx makes previews on. draw: a cell on screen (else a page ahead: asked, not counted).
static Texture2D *thumb_for(const char *path, i64 mtime, i64 size, Str mime, bool local, int px, bool draw)
{
    if (!thumbs.root[0] || thumbs.full || thumbs.off) return NULL;
    if (px > THUMB_PX_MAX) px = THUMB_PX_MAX;
    char key[PATH_MAX + 16];
    int kn = snprintf(key, sizeof key, "%d:%s", px, path), k;
    if (kn < 0 || (size_t)kn >= sizeof key) return NULL;
    if (!strmap_get(&thumbs.of, (Str){ key, kn }, &k)) {
        char *copy = thumbs.n < THUMB_ENTRIES ? arena_try_push(&thumbs.arena, kn, 1) : NULL;
        if (!copy) return thumbs.full = true, NULL;
        memcpy(copy, key, (size_t)kn);
        k = thumbs.n++;
        thumbs.e[k] = (ThumbEntry){ .key = { copy, kn }, .mtime = mtime };
        strmap_put(&thumbs.of, thumbs.e[k].key, k);
    }
    ThumbEntry *e = &thumbs.e[k];
    if (e->mtime != mtime && e->state != TS_ASKED) {   // changed: asked again, once
        if (e->state == TS_SHOWN) UnloadTexture(e->tex), thumbs.held -= (isize)e->tex.width * e->tex.height * 4;
        e->state = TS_UNKNOWN, e->mtime = mtime;
    }
    // A dropped texture is asked again for a cell on screen only: a page ahead asked again would load
    // it, have it dropped, and ask again, a read every frame.
    // A page ahead asks only for the types ofx makes: another type's preview is only read, and
    // that is quick enough when its cell comes (F6.4 review).
    // Its kind only now, asking (a thumbnailer is looked up in a table of hundreds of types).
    ThumbKind kind = TK_NONE;
    const char *exec = NULL;
    if (e->state == TS_UNKNOWN || (draw && e->state == TS_DROPPED)) {
        kind = thumb_kind(mime);
        if (kind == TK_NONE && thumbs.exec_on && (exec = thumbers_find(&thumbers, mime))) kind = TK_EXEC;
    }
    if ((e->state == TS_UNKNOWN && (draw || kind != TK_NONE)) || (draw && e->state == TS_DROPPED)) {
        pthread_mutex_lock(&thumbs.mu);
        if (thumbs.rn < THUMB_QUEUE) {
            ThumbReq *r = &thumbs.req[(thumbs.rhead + thumbs.rn++) % THUMB_QUEUE];
            *r = (ThumbReq){ .gen = thumbs.gen, .entry = k, .mtime = mtime, .size = size, .kind = kind, .exec = exec,
                             .make = local && kind != TK_NONE && (kind == TK_EXEC || size <= thumbs.src_max), .px = px, .src_max = thumbs.src_max };
            snprintf(r->path, sizeof r->path, "%s", path);
            e->state = TS_ASKED;
            pthread_cond_signal(&thumbs.cv);
        }
        pthread_mutex_unlock(&thumbs.mu);
    }
    if (e->state != TS_SHOWN) return NULL;
    if (draw) e->drawn = thumbs.frame, thumbs.drawn_now++;
    return &e->tex;
}

// The answers so far made textures; true when any came. Then textures over the limit dropped,
// the least recently drawn first (a page ahead's are never drawn), never one drawn in the last
// frame, nor one just loaded: it has a frame to be drawn in.
static bool thumbs_take(void)
{
    pthread_mutex_lock(&thumbs.mu);
    bool any = thumbs.dn > 0;
    for (int i = 0; i < thumbs.dn; i++) {
        ThumbDone *d = &thumbs.done[i];
        if (d->gen != thumbs.gen || d->entry >= thumbs.n) continue;
        thumbs.got[d->got]++;
        ThumbEntry *e = &thumbs.e[d->entry];
        // Its mtime is the asked one: thumb_for leaves an asked entry's mtime alone, and asks
        // again after this answer when the file has changed meanwhile.
        if (e->state != TS_ASKED) continue;
        e->state = TS_NONE;
        if (d->w > 0) {
            Image img = { .data = d->px, .width = d->w, .height = d->h, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
            e->tex = LoadTextureFromImage(img);
            if (e->tex.id) {
                SetTextureFilter(e->tex, TEXTURE_FILTER_BILINEAR);
                e->state = TS_SHOWN, e->drawn = thumbs.frame, thumbs.held += (isize)d->w * d->h * 4;
            }
        }
        // A TG_CHANGED answer stays "none" until the next listing brings the new mtime.
    }
    thumbs.dn = 0;
    pthread_cond_broadcast(&thumbs.cv);
    pthread_mutex_unlock(&thumbs.mu);
    while (thumbs.held > thumbs.held_max) {
        int old = -1;
        for (int i = 0; i < thumbs.n; i++)
            if (thumbs.e[i].state == TS_SHOWN && thumbs.e[i].drawn < thumbs.frame && (old < 0 || thumbs.e[i].drawn < thumbs.e[old].drawn)) old = i;
        if (old < 0) break;
        ThumbEntry *e = &thumbs.e[old];
        UnloadTexture(e->tex);
        thumbs.held -= (isize)e->tex.width * e->tex.height * 4;
        e->state = TS_DROPPED, thumbs.evicted++;
    }
    return any;
}

// The texture for path at px, if one is held, without asking (the tests' state line).
static Texture2D *thumb_peek(const char *path, int px)
{
    char key[PATH_MAX + 16];
    int kn = snprintf(key, sizeof key, "%d:%s", px > THUMB_PX_MAX ? THUMB_PX_MAX : px, path), k;
    if (kn < 0 || (size_t)kn >= sizeof key || !strmap_get(&thumbs.of, (Str){ key, kn }, &k)) return NULL;
    return thumbs.e[k].state == TS_SHOWN ? &thumbs.e[k].tex : NULL;
}

// Whether a worker has something to do or is doing it (the tests wait for none).
static bool thumbs_busy(void)
{
    pthread_mutex_lock(&thumbs.mu);
    bool b = thumbs.rn > 0 || thumbs.busy > 0 || thumbs.dn > 0;
    pthread_mutex_unlock(&thumbs.mu);
    return b;
}
