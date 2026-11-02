// Git status in the list (F12 step 5, fs/git.c): a thread runs git status in bubblewrap for the
// folder shown, and again when it changes, or the repository's index or HEAD does (a commit, an add,
// a switch from a terminal); the window draws from the last answer. Two slots: the window's answer
// stays in one while the next is made in the other. Outside a repository, or with ofx.conf's
// git-status off, there is none. Included by main.c.

static _Alignas(4096) u8 git_mem[2][GIT_OUT_MAX + MiB(8)];   // (the output, and its tables)

typedef struct {
    Arena   a;
    GitStat g;
    bool    repo;               // an answer from a repository (else: none, outside one or it failed)
    char    dir[PATH_MAX];      // the folder it is for, as asked
    char    rel[PATH_MAX];      // that folder from the top ("" the top)
    char    gitdir[PATH_MAX];   // where its index and HEAD are
    bool    ignored;            // the folder is ignored, or inside one that is: all of it
} GitSlot;

static struct {
    pthread_t       th;
    bool            started, quit;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    char            want[PATH_MAX];   // the folder asked for
    bool            asked;
    u32             gen;              // bumped by each ask: an older answer is dropped
    GitSlot         slot[2];
    int             shown;            // the window's (main thread's)
    int             ready;            // a fresh answer's slot, -1 none
    i64             last, due;        // the main thread's: when it last asked, and an ask put off to then (0: none)
} git = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER, .shown = 0, .ready = -1 };

static void *git_loop(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&git.mu);
    for (;;) {
        while (!git.quit && !git.asked) pthread_cond_wait(&git.cv, &git.mu);
        if (git.quit) break;
        // (ready is -1 here: the ask that set asked cleared it, and an answer is published only when
        // no ask came since its own; F12 step 5a review)
        u32 gen = git.gen;
        int s = 1 - git.shown;
        GitSlot *g = &git.slot[s];
        snprintf(g->dir, sizeof g->dir, "%s", git.want);
        git.asked = false;
        pthread_mutex_unlock(&git.mu);
        char top[PATH_MAX], real[PATH_MAX];
        arena_reset(&g->a);
        g->repo = git_top(g->dir, top, sizeof top) && realpath(g->dir, real) && git_status(&g->a, "git", real, top, &g->g);
        if (g->repo) {
            size_t tn = strlen(top);
            snprintf(g->rel, sizeof g->rel, "%s", real[tn] == '/' ? real + tn + 1 : real + tn);
            git_gitdir(top, g->gitdir, sizeof g->gitdir);
            g->ignored = git_ignored_at(&g->g, str_from_c(g->rel));
        }
        pthread_mutex_lock(&git.mu);
        if (gen == git.gen) git.ready = s, wake_main();
    }
    pthread_mutex_unlock(&git.mu);
    return NULL;
}

static void git_start(void)
{
    for (int i = 0; i < 2; i++) arena_init(&git.slot[i].a, "git", git_mem[i], sizeof git_mem[i]);
    git_run_ms = env_int("OFX_TEST_GIT_RUN_MS", GIT_RUN_MS, 1, GIT_RUN_MS);
    git.started = pthread_create(&git.th, NULL, git_loop, NULL) == 0;
}

// Git status for folder dir asked for (NULL, or git-status off: none, at once).
static void git_ask(const char *dir)
{
    git.last = now_ms(), git.due = 0;
    pthread_mutex_lock(&git.mu);
    git.gen++, git.ready = -1;
    if (dir && app.conf.git_status && git.started) snprintf(git.want, sizeof git.want, "%s", dir), git.asked = true, pthread_cond_signal(&git.cv);
    else git.slot[git.shown].repo = false;   // (nothing shown until an answer)
    pthread_mutex_unlock(&git.mu);
}

static i64 wake_due(void);

// The folder shown asked again after a change in it or its repository: at once when the last ask is
// a second old, else then (F12 step 5a review: a folder a build writes in asked a git status per
// batch; on a 30k-file repo each was 310-340 ms). Going to a folder asks at once (git_ask). Also at
// once outside a repository, or with git-status off: no git runs then, and a timer put off for it
// would wake the window before a tooltip's.
enum { GIT_ASK_GAP_MS = 1000 };
static void git_ask_soon(void)
{
    char top[PATH_MAX];
    bool none = !app.conf.git_status || !git_top(app.cwd, top, sizeof top);   // (a realpath and a few lstats)
    i64 now = now_ms();
    if (none || now - git.last >= GIT_ASK_GAP_MS) git_ask(app.cwd);
    else if (!git.due) git.due = git.last + GIT_ASK_GAP_MS, timer_at(wake_due());
}

// Each frame: an ask put off, made when due; then the timer set for what else waits (one timer: a
// tooltip's would be lost).
static void git_poll(void)
{
    if (git.due && now_ms() >= git.due) git_ask(app.cwd), timer_at(wake_due());
}

// The window's answer: NULL outside a repository (or none yet). Its folder from the top in *rel.
static const GitSlot *git_shown(void)
{
    const GitSlot *g = &git.slot[git.shown];
    return g->repo && strcmp(g->dir, app.cwd) == 0 ? g : NULL;   // (one for another folder: not this listing's)
}

// Each frame: a fresh answer taken; true when it came. Its repository's index and HEAD watched.
static bool git_take(void)
{
    pthread_mutex_lock(&git.mu);
    int s = git.ready;
    if (s >= 0) git.shown = s, git.ready = -1;
    pthread_mutex_unlock(&git.mu);
    if (s < 0) return false;
    watch_git(git.slot[s].repo ? git.slot[s].gitdir : NULL);
    return true;
}

// An entry's state in the answer shown (GS_NONE for none). A link is a file to git.
static GitState entry_git(isize i)
{
    const GitSlot *g = git_shown();
    if (!g) return GS_NONE;
    if (g->ignored) return GS_IGNORED;
    const Entry *e = &app.list.e[i];
    return git_state(&g->g, str_from_c(g->rel), e->name, e->kind == K_DIR);
}

// At quit, as count_quit and count_join: told, joined by until, else left to the process's end.
static void git_quit(void)
{
    if (!git.started) return;
    pthread_mutex_lock(&git.mu);
    git.quit = true;
    pthread_cond_broadcast(&git.cv);
    pthread_mutex_unlock(&git.mu);
}

static void git_join(const struct timespec *until)
{
    if (git.started) join_by(git.th, until);
}
