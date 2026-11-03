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
    // The branch menu's list (step 5b), asked as it opens: for the folder and HEAD's branch asked.
    char            br_want[PATH_MAX];
    bool            br_asked, br_ready;
    u32             br_gen;
    GitBranches     br_out;           // the worker's answer, taken into br
    GitBranches     br;               // the main thread's: the menu's
    bool            br_wait;          // asked, no answer yet ("Reading branches…")
    // A switch (step 5c), picked in the menu: to branch sw_name, for folder sw_dir; its answer.
    char            sw_dir[PATH_MAX], sw_name[GIT_HEAD_MAX], sw_msg[256];
    bool            sw_asked, sw_ready;
    GitSwitch       sw_got;
    bool            sw_busy;          // the main thread's: one asked, not answered yet
} git = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER, .shown = 0, .ready = -1 };

// The branches asked for, run with git.mu held as it is taken (and held again as it returns): none
// when git fails, or the folder is no repository's any more.
static void git_branches_run(void)
{
    static GitBranches b;
    char dir[PATH_MAX], top[PATH_MAX], real[PATH_MAX];
    u32 gen = git.br_gen;
    snprintf(dir, sizeof dir, "%s", git.br_want);
    git.br_asked = false;
    pthread_mutex_unlock(&git.mu);
    bool ok = git_top(dir, top, sizeof top) && realpath(dir, real) && git_branches("git", real, top, &b);
    if (!ok) b = (GitBranches){ .cur = -1 };
    pthread_mutex_lock(&git.mu);
    if (gen == git.br_gen) git.br_out = b, git.br_ready = true, wake_main();
}

// The switch asked for, run as git_branches_run is (git.mu held as it is taken and as it returns).
static void git_switch_run(void)
{
    char dir[PATH_MAX], name[GIT_HEAD_MAX], top[PATH_MAX], msg[sizeof git.sw_msg];
    snprintf(dir, sizeof dir, "%s", git.sw_dir), snprintf(name, sizeof name, "%s", git.sw_name);
    git.sw_asked = false;
    pthread_mutex_unlock(&git.mu);
    GitSwitch got = git_top(dir, top, sizeof top) ? git_switch("git", top, name, msg, sizeof msg) : (snprintf(msg, sizeof msg, "not in a repository any more"), GSW_REFUSED);
    pthread_mutex_lock(&git.mu);
    git.sw_got = got, snprintf(git.sw_msg, sizeof git.sw_msg, "%s", msg), git.sw_ready = true, wake_main();
}

static void *git_loop(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&git.mu);
    for (;;) {
        while (!git.quit && !git.asked && !git.br_asked && !git.sw_asked) pthread_cond_wait(&git.cv, &git.mu);
        if (git.quit) break;
        if (git.sw_asked) {   // a switch first: it was picked
            git_switch_run();
            continue;
        }
        if (!git.asked) {   // the branches (a status asked meanwhile goes first)
            git_branches_run();
            continue;
        }
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
    git_switch_ms = env_int("OFX_TEST_GIT_SWITCH_MS", GIT_SWITCH_MS, 1, GIT_SWITCH_MS);
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

static const GitSlot *git_shown(void);

// Branch name picked in the menu: switched to, in the folder shown's repository, on the worker
// (git_switch_take says how it went). One at a time.
static bool git_ask_switch(const char *name)
{
    if (git.sw_busy || !git.started) return false;
    pthread_mutex_lock(&git.mu);
    snprintf(git.sw_dir, sizeof git.sw_dir, "%s", app.cwd), snprintf(git.sw_name, sizeof git.sw_name, "%s", name);
    git.sw_asked = true, git.sw_ready = false, git.sw_busy = true;
    pthread_cond_signal(&git.cv);
    pthread_mutex_unlock(&git.mu);
    return true;
}

// Each frame: a switch's answer; true when it came, with how it went (*got), the branch and what
// git said.
static bool git_switch_take(GitSwitch *got, char *name, size_t ncap, char *msg, size_t mcap)
{
    pthread_mutex_lock(&git.mu);
    bool r = git.sw_ready;
    if (r) git.sw_ready = false, git.sw_busy = false, *got = git.sw_got, snprintf(name, ncap, "%s", git.sw_name), snprintf(msg, mcap, "%s", git.sw_msg);
    pthread_mutex_unlock(&git.mu);
    return r;
}

// The branch menu opened: its list asked for the folder shown, the one before dropped (none outside
// a repository).
static void git_ask_branches(void)
{
    const GitSlot *g = git_shown();
    pthread_mutex_lock(&git.mu);
    git.br_gen++, git.br_ready = false;
    git.br = (GitBranches){ .cur = -1 }, git.br_wait = g != NULL;
    if (g) snprintf(git.br_want, sizeof git.br_want, "%s", app.cwd), git.br_asked = true, pthread_cond_signal(&git.cv);   // (HEAD read then: a switch may come first)
    pthread_mutex_unlock(&git.mu);
}

// The window's answer: NULL outside a repository (or none yet). Its folder from the top in *rel.
static const GitSlot *git_shown(void)
{
    const GitSlot *g = &git.slot[git.shown];
    return g->repo && strcmp(g->dir, app.cwd) == 0 ? g : NULL;   // (one for another folder: not this listing's)
}

// Each frame: a fresh answer taken (a status, or the branch menu's list); true when one came. A
// status's repository's index and HEAD watched.
static bool git_take(void)
{
    pthread_mutex_lock(&git.mu);
    int s = git.ready;
    if (s >= 0) git.shown = s, git.ready = -1;
    bool br = git.br_ready;
    if (br) git.br = git.br_out, git.br_ready = false, git.br_wait = false;
    pthread_mutex_unlock(&git.mu);
    if (br && s < 0) return true;
    if (s < 0) return false;
    watch_git(git.slot[s].repo ? git.slot[s].gitdir : NULL);
    return true;
}

// The branch button's label: HEAD's branch, or its commit's short id when detached, then how far
// it is ahead of and behind its upstream ("main ↑2 ↓1", each when not 0). NULL: no button (outside
// a repository, or no answer yet; another view has none, its address being no folder of git's).
static const char *branch_label(void)
{
    const GitSlot *g = git_shown();
    if (!g) return NULL;
    static char buf[GIT_HEAD_MAX + 32];
    const GitStat *s = &g->g;
    int n = s->head[0] ? snprintf(buf, sizeof buf, "%s", s->head) : snprintf(buf, sizeof buf, "%.7s", s->oid);
    if (s->has_ab && s->ahead) n += snprintf(buf + n, sizeof buf - (size_t)n, " \xE2\x86\x91%d", s->ahead);   // ↑
    if (s->has_ab && s->behind) snprintf(buf + n, sizeof buf - (size_t)n, " \xE2\x86\x93%d", s->behind);    // ↓
    return buf;
}

// The branch menu's entry it's label: a branch's name, or the note in their place.
static Str branch_menu_label(UiMenuItem it)
{
    if (it == UM_BR_NOTE && git.sw_busy) return str_from_c(arena_sprintf(&frame, "Switching to %s\xE2\x80\xA6", git.sw_name));   // (step 5c: the list comes after)
    if (it == UM_BR_NOTE) return git.br_wait ? S("Reading branches\xE2\x80\xA6") : S("No local branches");
    int k = (int)(it - UM_BR_0);
    return k >= 0 && k < git.br.n ? str_from_c(git.br.name[k]) : STR_EMPTY;
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
