// The jobs (docs/plan.md, F5.3): up to JOBQ_WORKERS copies, moves and deletes at once, each on a
// worker thread of its own, and up to JOBQ_WAITING more waiting in the order they came. fs/jobq.c
// says which waiting job may start (none that touches files a running one uses) and keeps each
// row's throughput line. A worker asks about a clash through jobs.mu and its record's cv; the main
// loop polls (jobs_poll), shows one clash at a time, and answers (job_answer). Included by main.c.

enum { JOB_RECS = JOBQ_WORKERS + JOBQ_WAITING };

typedef struct {
    int     id;             // > 0 while the record holds a job
    bool    started;
    Job     j;
    JobItem item[CLIP_PATHS_MAX];
    Arena   arena;          // the items' paths and the uses
    JobUses uses;
    bool    reverse;        // an undo's move back: it makes no new undo record
    bool    own_cut;        // a paste of fx's own cut: the clipboard empties after it
    u32     own_gen;        // own_gen when it was asked for
    u32     undo_gen;       // undo_gen then: a newer action's undo record wins over this job's
    char    label[200];     // "Copying 12 items to bdir"
    int     worker;         // its buffer, while it runs
    pthread_t thread;
    JobLine line;
    // A clash, under jobs.mu.
    pthread_cond_t cv;
    bool    waiting;        // the worker waits for an answer
    int     answer;         // ANS_*, 0 until given
    bool    all;
    char    clash[PATH_MAX];
    bool    clash_dirs;
} JobRec;

// Bumped by every undo record the main thread writes (trash, rename, a new folder, an undo): a job
// that finishes after one of those leaves its record be (reviewer-2, F5.3 review).
static u32 undo_gen;

static JobRec job_recs[JOB_RECS];
static u8     job_rec_mem[JOB_RECS][MiB(4)];
static u8     job_bufs[JOBQ_WORKERS][JOB_BUF];

static struct {
    pthread_mutex_t mu;
    int    run[JOBQ_WORKERS];    // records running, in the order they started (the rows' order)
    int    nrun;
    int    wait[JOBQ_WAITING];   // records waiting, in the order they came
    int    nwait;
    bool   busy_buf[JOBQ_WORKERS];
    int    next_id;
    int    asking;               // the record whose clash the panel shows, -1 for none
    atomic_llong last_tick;
    i64    last_draw;            // when jobs_poll last asked for a frame
    // The ticker: wakes the main loop 4 times a second while jobs run, so a row's speed and
    // percentage move on while a drive stalls (no worker ticks then).
    pthread_mutex_t tick_mu;
    pthread_cond_t  tick_cv;
    bool   tick_on, tick_started;
} jobs = { .mu = PTHREAD_MUTEX_INITIALIZER, .asking = -1, .tick_mu = PTHREAD_MUTEX_INITIALIZER };

enum { JOBS_TICK_MS = 250 };

static void *job_ticker(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&jobs.tick_mu);
    for (;;) {
        while (!jobs.tick_on) pthread_cond_wait(&jobs.tick_cv, &jobs.tick_mu);
        struct timespec until;
        clock_gettime(CLOCK_MONOTONIC, &until);   // tick_cv waits on it: a clock step can't freeze it
        until.tv_nsec += JOBS_TICK_MS * 1000000L;
        if (until.tv_nsec >= 1000000000L) until.tv_sec++, until.tv_nsec -= 1000000000L;
        pthread_cond_timedwait(&jobs.tick_cv, &jobs.tick_mu, &until);
        if (!jobs.tick_on) continue;
        pthread_mutex_unlock(&jobs.tick_mu);
        wake_main();
        pthread_mutex_lock(&jobs.tick_mu);
    }
    return NULL;
}

static void jobs_tick_set(bool on)
{
    pthread_mutex_lock(&jobs.tick_mu);
    if (on && !jobs.tick_started) {
        pthread_condattr_t ca;
        pthread_condattr_init(&ca);
        pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
        pthread_cond_init(&jobs.tick_cv, &ca);
        pthread_condattr_destroy(&ca);
        pthread_t t;
        if (pthread_create(&t, NULL, job_ticker, NULL) == 0) pthread_detach(t), jobs.tick_started = true;
    }
    if (jobs.tick_on != on) jobs.tick_on = on, pthread_cond_signal(&jobs.tick_cv);
    pthread_mutex_unlock(&jobs.tick_mu);
}

static Answer job_ask(Job *j, const char *dst, bool both_dirs, bool *rest)
{
    JobRec *r = j->ctx;
    pthread_mutex_lock(&jobs.mu);
    snprintf(r->clash, sizeof r->clash, "%s", dst);
    r->clash_dirs = both_dirs, r->answer = 0, r->waiting = true;
    wake_main();
    while (r->answer == 0) pthread_cond_wait(&r->cv, &jobs.mu);
    Answer a = (Answer)r->answer;
    *rest = r->all;
    r->waiting = false;
    pthread_mutex_unlock(&jobs.mu);
    return a;
}

// A worker's progress: the window wakes to draw it, ten times a second at most.
static void job_tick_gui(Job *j)
{
    i64 now = now_ms();
    if (atomic_load(&j->phase) == JP_DONE || now - atomic_load(&jobs.last_tick) >= 100) {
        atomic_store(&jobs.last_tick, now);
        wake_main();
    }
}

static void *job_thread(void *arg)
{
    JobRec *r = arg;
    job_run(&r->j);
    return NULL;
}

// A record for a new job, its arena empty; NULL when JOBQ_WAITING wait already (the panel says so).
static JobRec *job_new(void)
{
    if (jobs.nwait >= JOBQ_WAITING) {
        app.ui.full_told = true;
        return NULL;
    }
    for (int i = 0; i < JOB_RECS; i++) {
        JobRec *r = &job_recs[i];
        if (r->id) continue;
        arena_init(&r->arena, "job", job_rec_mem[i], sizeof job_rec_mem[i]);
        r->reverse = false, r->own_cut = false, r->own_gen = 0;
        return r;
    }
    app.ui.full_told = true;
    return NULL;
}

static int job_index(const JobRec *r)
{
    return (int)(r - job_recs);
}

// What a row says about the job: "Copying 12 items to bdir", "Deleting a.txt".
static void job_label(JobRec *r, JobKind kind, isize n)
{
    static const char *const VERB[] = { "Copying", "Moving", "Deleting" };
    char what[NAME_MAX + 32];
    if (n == 1) snprintf(what, sizeof what, "%.*s", STR_ARG(path_base(str_from_c(r->item[0].src))));
    else snprintf(what, sizeof what, "%td items", n);
    int k = snprintf(r->label, sizeof r->label, "%s %s", VERB[kind], what);
    if (r->reverse && k > 0) snprintf(r->label + k, sizeof r->label - (size_t)k, " back");
    else if (r->item[0].dst && k > 0) {
        Str d = path_dir(str_from_c(r->item[0].dst));
        Str to = d.len ? path_base(d) : S("/");
        snprintf(r->label + k, sizeof r->label - (size_t)k, " to %.*s", STR_ARG(to.len ? to : S("/")));
    }
}

// The paths the job uses, into its arena: each source, and each target's folder (once when
// they repeat, as a paste's do).
static void job_uses(JobRec *r, isize n)
{
    const char **p = arena_new(&r->arena, const char *, 2 * n);
    isize k = 0;
    const char *last_dir = NULL;
    for (isize i = 0; i < n; i++) {
        p[k++] = r->item[i].src;
        if (!r->item[i].dst) continue;
        Str d = path_dir(str_from_c(r->item[i].dst));
        if (last_dir && str_eq(str_from_c(last_dir), d)) continue;
        last_dir = arena_cstr(&r->arena, d.len ? d : S("/"));
        p[k++] = last_dir;
    }
    r->uses = (JobUses){ p, k };
}

static void job_launch(JobRec *r)
{
    int w = 0;
    while (w < JOBQ_WORKERS && jobs.busy_buf[w]) w++;
    r->worker = w, jobs.busy_buf[w] = true;
    r->j.buf = job_bufs[w];
    r->started = true;
    memset(&r->line, 0, sizeof r->line);
    if (pthread_create(&r->thread, NULL, job_thread, r) != 0) {
        set_status(true, "cannot start the job: %s", strerror(errno));
        jobs.busy_buf[w] = false, r->id = 0;
        return;
    }
    jobs.run[jobs.nrun++] = job_index(r);
    jobs_tick_set(true);
}

// Every waiting job that may start now, in order.
static void jobs_schedule(void)
{
    for (;;) {
        const JobUses *w[JOBQ_WAITING], *ru[JOBQ_WORKERS];
        for (int i = 0; i < jobs.nwait; i++) w[i] = &job_recs[jobs.wait[i]].uses;
        for (int i = 0; i < jobs.nrun; i++) ru[i] = &job_recs[jobs.run[i]].uses;
        int k = jobq_next(w, jobs.nwait, ru, jobs.nrun);
        if (k < 0) return;
        JobRec *r = &job_recs[jobs.wait[k]];
        memmove(jobs.wait + k, jobs.wait + k + 1, sizeof *jobs.wait * (size_t)(jobs.nwait - k - 1));
        jobs.nwait--;
        job_launch(r);
    }
}

// r->item[0..n) as a job of kind: queued, and started at once when nothing it uses is busy.
static void job_submit(JobRec *r, JobKind kind, isize n)
{
    if (n <= 0) return;
    memset(&r->j, 0, sizeof r->j);
    r->j.kind = kind, r->j.item = r->item, r->j.n = n, r->j.ctx = r;
    r->j.ask = job_ask, r->j.tick = job_tick_gui, r->j.delay_ms = app.copy_delay_ms;
    r->waiting = false, r->answer = 0, r->all = false, r->started = false;
    job_label(r, kind, n);
    job_uses(r, n);
    pthread_cond_init(&r->cv, NULL);
    r->undo_gen = undo_gen;
    r->id = ++jobs.next_id;
    jobs.wait[jobs.nwait++] = job_index(r);
    jobs_schedule();
}

static void job_answer(int a, bool all)
{
    if (jobs.asking < 0) return;
    JobRec *r = &job_recs[jobs.asking];
    pthread_mutex_lock(&jobs.mu);
    if (r->waiting && r->answer == 0) r->answer = a, r->all = all, pthread_cond_signal(&r->cv);
    pthread_mutex_unlock(&jobs.mu);
    jobs.asking = -1;
}

// The running job with that id stops (its part file removed by the worker); a clash it asks
// about goes with it.
static void job_cancel(int id)
{
    for (int i = 0; i < jobs.nrun; i++) {
        JobRec *r = &job_recs[jobs.run[i]];
        if (r->id != id) continue;
        atomic_store(&r->j.cancel, true);
        pthread_mutex_lock(&jobs.mu);
        if (r->waiting && r->answer == 0) r->answer = ANS_CANCEL, pthread_cond_signal(&r->cv);
        pthread_mutex_unlock(&jobs.mu);
        if (jobs.asking == jobs.run[i]) jobs.asking = -1, app.ui.asking = false;
    }
}

static bool job_running_id(int id)
{
    for (int i = 0; i < jobs.nrun; i++)
        if (job_recs[jobs.run[i]].id == id) return true;
    return false;
}

static void job_finish(JobRec *r)
{
    Job *j = &r->j;
    static const char *const DONE[] = { "Copied", "Moved", "Deleted" }, *const STOPPED[] = { "Copy", "Move", "Delete" };
    isize done = 0;
    for (isize i = 0; i < j->n; i++)
        if (j->item[i].out == OUT_NEW || j->item[i].out == OUT_REPLACED || j->item[i].out == OUT_MERGED) done++;
    char msg[600];
    int k;
    if (atomic_load(&j->cancel)) k = snprintf(msg, sizeof msg, "%s cancelled after %td of %td %s", STOPPED[j->kind], done, j->n, items(j->n));
    else k = snprintf(msg, sizeof msg, "%s %td %s", DONE[j->kind], done, items(done));
    if (k > 0 && j->skipped) k += snprintf(msg + k, sizeof msg - (size_t)k, ", %td skipped", j->skipped);
    if (k > 0 && j->errors) snprintf(msg + k, sizeof msg - (size_t)k, "; %td failed: %s", j->errors, j->err);
    else if (k > 0 && j->note[0] && (size_t)k < sizeof msg) snprintf(msg + k, sizeof msg - (size_t)k, " (%s)", j->note);
    set_status(j->errors > 0, "%s", msg);

    // The undo record: what the job made new (copies) or moved whole; the rest can't go back. The
    // job that finished last is the one Ctrl+Z undoes, unless a trash, rename, new folder or undo
    // came since the job was asked for.
    if (undo_gen != r->undo_gen) {
        // a newer action's record stays
    } else if (r->reverse || j->kind == JOB_DELETE) {
        undo_clear(&undo);
    } else {
        undo_begin(&undo, j->kind == JOB_COPY ? UNDO_COPY : UNDO_MOVE);
        for (isize i = 0; i < j->n; i++) {
            JobItem *it = &j->item[i];
            if (it->out != OUT_NEW) continue;
            Str d = path_dir(str_from_c(it->dst));
            char dir[PATH_MAX], made[PATH_MAX];
            snprintf(dir, sizeof dir, "%.*s", STR_ARG(d.len ? d : S("/")));
            if (join(made, sizeof made, dir, str_from_c(it->final))) undo_add(&undo, j->kind == JOB_MOVE ? it->src : NULL, made, NULL);
        }
        if (undo.n == 0) undo_clear(&undo);
    }
    if (r->own_cut && !atomic_load(&j->cancel) && own_gen == r->own_gen) own.n = 0, clip_clear_own();
    reload();
}

// Called every loop: true when the window should draw a frame for the jobs: when one ended or
// asks something, else at most every JOBQ_FRAME_MS (jobq_draw_due). Drawing on every loop pass while a job ran
// spun at the display's rate (each frame wakes the next pass): 67% of a core for the main thread
// and 41% for Mesa's on Michael's USB copy, the copy itself 0.2% (2026-10-22).
static bool jobs_poll(void)
{
    if (jobs.nrun == 0) return false;
    bool asked = jobs.asking >= 0;
    i64 now = now_ms();
    bool finished = false;
    for (int i = 0; i < jobs.nrun;) {
        JobRec *r = &job_recs[jobs.run[i]];
        jobline_sample(&r->line, now, atomic_load(&r->j.bytes_done), atomic_load(&r->j.bytes_total));
        if (atomic_load(&r->j.phase) != JP_DONE) { i++; continue; }
        pthread_join(r->thread, NULL);
        pthread_cond_destroy(&r->cv);
        jobs.busy_buf[r->worker] = false;
        memmove(jobs.run + i, jobs.run + i + 1, sizeof *jobs.run * (size_t)(jobs.nrun - i - 1));
        jobs.nrun--;
        job_finish(r);
        r->id = 0;
        finished = true;
    }
    if (finished) jobs_schedule();
    // One clash at a time, the earliest job's first, and only when no other panel is up.
    bool other = app.ui.confirming || app.ui.cancel_asking || app.ui.quit_asking || app.ui.full_told;
    if (!app.ui.asking && !other) {
        pthread_mutex_lock(&jobs.mu);
        for (int i = 0; i < jobs.nrun && jobs.asking < 0; i++) {
            JobRec *r = &job_recs[jobs.run[i]];
            if (r->waiting && r->answer == 0) jobs.asking = jobs.run[i];
        }
        pthread_mutex_unlock(&jobs.mu);
        if (jobs.asking >= 0) app.ui.asking = true, app.ui.ask_all = false;
    }
    bool gone = app.ui.cancel_asking && !job_running_id(app.ui.cancel_id);
    if (gone) app.ui.cancel_asking = false;   // it ended meanwhile
    if (jobs.nrun == 0) jobs_tick_set(false);
    return jobq_draw_due(finished || gone || (jobs.asking >= 0) != asked, now, &jobs.last_draw);
}

// Quitting: the running jobs cancelled and waited for half a second in all, the waiting ones
// dropped. A worker stuck in a hung mount is left behind (detached), as count_stop does; its part
// file stays.
static void jobs_stop(void)
{
    jobs.nwait = 0;
    for (int i = 0; i < jobs.nrun; i++) job_cancel(job_recs[jobs.run[i]].id);
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += 500 * 1000000L;
    if (until.tv_nsec >= 1000000000L) until.tv_sec++, until.tv_nsec -= 1000000000L;
    for (int i = 0; i < jobs.nrun; i++)
        if (pthread_timedjoin_np(job_recs[jobs.run[i]].thread, NULL, &until) != 0) pthread_detach(job_recs[jobs.run[i]].thread);
    jobs.nrun = 0;
}

// Whether a job running or waiting reads or writes path, or something under or above it: a
// trash, rename, new folder or undo then waits (reviewer-2, F5.3 review).
static bool jobs_touch(const char *path)
{
    Str p = str_from_c(path);
    for (int i = 0; i < JOB_RECS; i++) {
        JobRec *r = &job_recs[i];
        if (!r->id) continue;
        for (isize k = 0; k < r->j.n; k++)
            if (jobq_nested(p, str_from_c(r->item[k].src)) || (r->item[k].dst && jobq_nested(p, str_from_c(r->item[k].dst)))) return true;
    }
    return false;
}

// The status says so when path is a job's.
static bool jobs_refuse(const char *path)
{
    if (!jobs_touch(path)) return false;
    set_status(true, "%.*s is in use by a job: wait until it ends", STR_ARG(path_base(str_from_c(path))));
    return true;
}

static JobRec *job_oldest(void)
{
    return jobs.nrun ? &job_recs[jobs.run[0]] : NULL;
}
