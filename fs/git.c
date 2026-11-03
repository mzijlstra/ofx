// Git status per file (F12 step 5, docs/plan.md "F12 design"): the repository a folder is in (one
// look for .git above it), and `git status --porcelain=v2 -z --ignored --branch -- .` run in that
// folder (gui/git.c runs it) read into the state of each entry the listing shows. Paths in the
// output are relative to the repository's top; a folder's state is the most urgent of what it
// holds (conflicted, modified, staged, untracked), and inside an ignored folder everything is
// ignored (git names the folder only).

#include "../base/base.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

// By urgency: a folder takes the highest of what it holds (an ignored file doesn't count there).
typedef enum { GS_NONE, GS_IGNORED, GS_UNTRACKED, GS_STAGED, GS_RENAMED, GS_MODIFIED, GS_CONFLICT, GS_N } GitState;

// The letter at a row's right (VS Code's), or 0 for none.
static char git_letter(GitState s)
{
    static const char L[GS_N] = { [GS_UNTRACKED] = 'U', [GS_STAGED] = 'A', [GS_RENAMED] = 'R', [GS_MODIFIED] = 'M', [GS_CONFLICT] = '!' };
    return s >= 0 && s < GS_N ? L[s] : 0;
}

enum { GIT_ENTRIES_MAX = 1 << 14, GIT_HEAD_MAX = 256 };   // (16,384 changes: their tables, 2.5 MiB a slot)

typedef struct {
    StrMap of;       // a path from the top (no trailing /) to its own GitState: a file, or a folder git names whole
    StrMap under;    // a folder's path to the most urgent state of what is under it
    isize  changed;  // entries not ignored (the footer's "N changed")
    isize  n_seen;   // entries read
    bool   cut;      // more than GIT_ENTRIES_MAX: the rest unknown
    char   head[GIT_HEAD_MAX];   // the branch, or "" when detached
    char   oid[41];              // the commit checked out ("(initial)" before the first)
    bool   has_ab;               // an upstream, with ahead and behind
    int    ahead, behind;
} GitStat;

// The repository's top for folder dir (resolved), into top: the nearest folder above it, or itself,
// holding a .git (a folder, or the file a worktree or a submodule has). False outside a repository,
// and inside a .git folder (status has no work tree there).
static bool git_top(const char *dir, char *top, size_t cap)
{
    char p[PATH_MAX];
    if (!realpath(dir, p) || strlen(p) >= cap) return false;
    for (Str s = str_from_c(p); s.len > 0;) {
        if (str_eq(path_base(s), S(".git"))) return false;
        char g[PATH_MAX + 8];
        struct stat st;
        snprintf(g, sizeof g, "%.*s/.git", STR_ARG(s));
        if (lstat(g, &st) == 0 && (S_ISDIR(st.st_mode) || S_ISREG(st.st_mode))) {
            snprintf(top, cap, "%.*s", STR_ARG(s.len ? s : S("/")));
            return true;
        }
        if (s.len == 1) break;   // (/ looked at)
        Str up = path_dir(s);
        s = up.len ? up : S("/");
    }
    return false;
}

// path's state is s; each folder above it within the repository takes s when more urgent (not an
// ignored one).
static void git_put(GitStat *g, Str path, GitState s)
{
    int was;
    if (path.len && path.p[path.len - 1] == '/') path.len--;   // (a folder git names whole: "newdir/")
    if (!path.len) return;
    if (!strmap_put(&g->of, path, s)) g->cut = true;
    if (s == GS_IGNORED) return;
    for (isize i = path.len - 1; i > 0; i--) {
        if (path.p[i] != '/') continue;
        Str d = { path.p, i };
        if (strmap_get(&g->under, d, &was) && was >= (int)s) continue;
        if (!strmap_put(&g->under, d, s)) g->cut = true;
    }
}

// One line of the output (a NUL-ended record, its text in a: kept, as the tables point into it).
// A rename's second record, its old path, is skipped by the caller (*skip).
static void git_line(GitStat *g, Str l, bool *skip)
{
    if (l.len < 2) return;
    if (l.p[0] == '#') {
        Str v;
        if (str_starts_with(l, S("# branch.head "))) {
            v = str_cut(l, 14, l.len);
            if (str_eq(v, S("(detached)"))) g->head[0] = 0;
            else snprintf(g->head, sizeof g->head, "%.*s", STR_ARG(v));
        } else if (str_starts_with(l, S("# branch.oid "))) {
            v = str_cut(l, 13, l.len);
            snprintf(g->oid, sizeof g->oid, "%.*s", STR_ARG(v));
        } else if (str_starts_with(l, S("# branch.ab "))) {
            g->has_ab = sscanf(l.p + 12, "+%d -%d", &g->ahead, &g->behind) == 2;
        }
        return;
    }
    // The path comes after the record's fields: "1 XY sub mH mI mW hH hI path" (8 spaces), "2 … Xscore
    // path" (9; then the old path), "u … h1 h2 h3 path" (10), "? path", "! path" (1).
    int fields = l.p[0] == '1' ? 8 : l.p[0] == '2' ? 9 : l.p[0] == 'u' ? 10 : l.p[0] == '?' || l.p[0] == '!' ? 1 : -1;
    if (fields < 0) return;
    isize at = 0;
    for (int f = 0; f < fields && at < l.len; at++)
        if (l.p[at] == ' ') f++;
    if (at >= l.len) return;
    Str path = str_cut(l, at, l.len);
    char x = l.len > 3 ? l.p[2] : 0;
    GitState s = l.p[0] == '?' ? GS_UNTRACKED : l.p[0] == '!' ? GS_IGNORED : l.p[0] == 'u' ? GS_CONFLICT : l.p[0] == '2' ? GS_RENAMED
               : x == 'A' ? GS_STAGED : GS_MODIFIED;
    if (l.p[0] == '2') *skip = true;
    if (g->n_seen++ >= GIT_ENTRIES_MAX) {
        g->cut = true;
        return;
    }
    if (s != GS_IGNORED) g->changed++;
    git_put(g, path, s);
}

// out (git's output, in an arena that outlives g) into g. False when the tables don't fit a.
static bool git_parse(Arena *a, Str out, GitStat *g)
{
    *g = (GitStat){ 0 };
    if (!strmap_init(&g->of, a, GIT_ENTRIES_MAX) || !strmap_init(&g->under, a, GIT_ENTRIES_MAX)) return false;
    bool skip = false;
    for (isize at = 0; at < out.len;) {
        isize e = at;
        while (e < out.len && out.p[e]) e++;
        if (skip) skip = false;
        else git_line(g, str_cut(out, at, e), &skip);
        at = e + 1;
    }
    return true;
}

// Whether folder rel (from the top, "" the top itself) is ignored, or inside a folder that is:
// everything listed there is.
static bool git_ignored_at(const GitStat *g, Str rel)
{
    int s;
    for (Str d = rel; d.len > 0; d = path_dir(d))
        if (strmap_get(&g->of, d, &s) && s == GS_IGNORED) return true;
    return false;
}

// The state of entry name in folder rel (from the top; "" the top), a folder when dir: its own, or
// for a folder the most urgent under it.
static GitState git_state(const GitStat *g, Str rel, Str name, bool dir)
{
    char p[PATH_MAX];
    int n = snprintf(p, sizeof p, "%.*s%s%.*s", STR_ARG(rel), rel.len ? "/" : "", STR_ARG(name)), own = GS_NONE, under = GS_NONE;
    if (n < 0 || (size_t)n >= sizeof p) return GS_NONE;
    Str k = { p, n };
    strmap_get(&g->of, k, &own);
    if (dir) strmap_get(&g->under, k, &under);
    return (GitState)(own > under ? own : under);
}

// ---- running it (in bubblewrap: sandbox.c) -------------------------------------------------------

enum { GIT_RUN_MS = 3000, GIT_OUT_MAX = MiB(16) };
static int git_run_ms = GIT_RUN_MS;   // (OFX_TEST_GIT_RUN_MS, gui/)

// The folder a worktree's or a submodule's .git file names ("gitdir: …"; its index and HEAD are
// there), resolved, into out; "" when top's .git is a folder, or the file can't be read.
static void git_linked(const char *top, char *out, size_t cap)
{
    out[0] = 0;
    char g[PATH_MAX + 8], line[PATH_MAX + 16], at[2 * PATH_MAX + 2], r[PATH_MAX];
    snprintf(g, sizeof g, "%s/.git", top);
    isize n = file_read_buf(g, line, sizeof line - 1);
    if (n <= 0) return;
    line[n] = 0;
    if (strncmp(line, "gitdir: ", 8) != 0) return;
    line[strcspn(line, "\n")] = 0;
    const char *to = line + 8;
    snprintf(at, sizeof at, "%s%s%s", to[0] == '/' ? "" : top, to[0] == '/' ? "" : "/", to);
    if (realpath(at, r)) snprintf(out, cap, "%s", r);
}

// The repository folder a .git file leads into, up to its .git, into out ("" for a .git folder):
// bound too, as its objects are there.
static void git_common(const char *top, char *out, size_t cap)
{
    // The gitdir's commondir names the repository's (a worktree's "../..", one of a bare repository's
    // too, which has no /.git/ in its path); without one, as a submodule's, the gitdir is it (code-
    // health review, 2026-11-03: the path was cut at its /.git/).
    char gitdir[PATH_MAX], p[PATH_MAX + 16], rel[PATH_MAX], at[2 * PATH_MAX + 2], r[PATH_MAX];
    git_linked(top, gitdir, sizeof gitdir);
    snprintf(out, cap, "%s", gitdir);
    if (!gitdir[0]) return;
    snprintf(p, sizeof p, "%s/commondir", gitdir);
    isize n = file_read_buf(p, rel, sizeof rel - 1);
    if (n <= 0) return;
    rel[n] = 0, rel[strcspn(rel, "\n")] = 0;
    snprintf(at, sizeof at, "%s%s%s", rel[0] == '/' ? "" : gitdir, rel[0] == '/' ? "" : "/", rel);
    if (rel[0] && realpath(at, r)) snprintf(out, cap, "%s", r);
}

// Where top's index and HEAD are, into out: its .git folder, or the one its .git file names. What
// gui/ watches, to ask again when they change (a commit, an add, a switch elsewhere).
static void git_gitdir(const char *top, char *out, size_t cap)
{
    git_linked(top, out, cap);
    if (!out[0]) snprintf(out, cap, "%s/.git", top);
}

// The paths git reads, bound read only in its sandbox: the repository, a worktree's or a
// submodule's real .git (git_common), and git's config files (HOME's, XDG's, /etc's).
typedef struct { char common[PATH_MAX], home_conf[PATH_MAX], xdg_conf[PATH_MAX]; } GitBinds;

// j set up to run argv (git's) in folder dir of repository top, as below: in bubblewrap, g's paths
// bound, for git_run_ms; the repository and its real .git read only, or writable when it writes (a
// switch), git's config files read only either way.
static void git_job(BoxJob *j, GitBinds *g, const char *const *argv, const char *dir, const char *top, bool writes)
{
    const char *home = getenv("HOME"), *xdg = getenv("XDG_CONFIG_HOME");
    git_common(top, g->common, sizeof g->common);
    snprintf(g->home_conf, sizeof g->home_conf, "%s/.gitconfig", home ? home : "/nonexistent");
    if (xdg && xdg[0]) snprintf(g->xdg_conf, sizeof g->xdg_conf, "%s/git", xdg);
    else snprintf(g->xdg_conf, sizeof g->xdg_conf, "%s/.config/git", home ? home : "/nonexistent");
    const char *common = g->common[0] ? g->common : NULL;
    *j = (BoxJob){ .argv = argv, .ro = { g->home_conf, g->xdg_conf, "/etc/gitconfig" }, .cwd = dir, .ms = git_run_ms };
    if (writes) j->rw[0] = top, j->rw[1] = common;
    else j->ro[3] = top, j->ro[4] = common;
}

// git status for folder dir (resolved) in repository top, read into g; git's output is put in a,
// which must outlive g. git is the program (a test's own under the guard). False when it didn't run,
// ran out of time, or said nothing (not a repository after all).
//   --no-optional-locks: it doesn't write the index (the sandbox lets it write nothing anyway).
//   core.fsmonitor=false: a repository's own config doesn't start a program each time.
static bool git_status(Arena *a, const char *git, const char *dir, const char *top, GitStat *g)
{
    const char *argv[] = { git, "--no-optional-locks", "-c", "core.fsmonitor=false", "status", "--porcelain=v2", "-z", "--ignored", "--branch", "--", ".", NULL };
    GitBinds binds;
    BoxJob j;
    git_job(&j, &binds, argv, dir, top, false);
    isize len = 0;
    j.out = arena_try_push(a, GIT_OUT_MAX, 1), j.cap = GIT_OUT_MAX, j.len = &len;
    if (!j.out || box_run(&j) != BOX_ENDED || len == 0) return false;
    if (!git_parse(a, (Str){ j.out, len }, g)) return false;
    if (len == GIT_OUT_MAX) g->cut = true;
    return true;
}

// ---- the branches (F12 step 5b): the branch button's menu, and its panel (step 6) -----------------

// The branches (step 5b; all of them, step 6): the menu shows the first 8, the panel all it holds
// (GIT_BRANCHES_MAX, of GIT_REFS_MAX read).
enum { GIT_BRANCHES_MAX = 1024, GIT_REFS_MAX = 8192, GIT_REFLOG_TAIL = KiB(64), GIT_REFS_OUT_MAX = KiB(256) };

typedef struct {
    int  n;
    int  total;   // the repository's local branches (n: at most GIT_BRANCHES_MAX of them, the latest)
    int  cur;     // the one checked out (-1: detached, or not among them)
    bool more;    // total a lower bound: refs past GIT_REFS_MAX, or for-each-ref's output past its room
    char name[GIT_BRANCHES_MAX][GIT_HEAD_MAX];
    i64  when[GIT_BRANCHES_MAX];   // its last commit's time (unix seconds)
} GitBranches;

// The local branches as for-each-ref gives them, one "<committer time> <name>" a line (newest commit
// first), read into names and times; and whether each is in the list yet. A name of GIT_HEAD_MAX or
// more isn't one (the list holds none, so total doesn't count it).
typedef struct {
    int  n;
    bool more;   // lines left past GIT_REFS_MAX
    Str  name[GIT_REFS_MAX];
    i64  when[GIT_REFS_MAX];
    bool taken[GIT_REFS_MAX];
} GitRefs;

static void git_refs_parse(GitRefs *r, Str refs)
{
    r->n = 0;
    isize at = 0;
    while (at < refs.len && r->n < GIT_REFS_MAX) {
        isize e = at;
        while (e < refs.len && refs.p[e] != '\n') e++;
        Str l = str_cut(refs, at, e);
        at = e + 1;
        isize sp = str_find(l, ' ');
        if (sp <= 0 || sp + 1 >= l.len || l.len - sp - 1 >= GIT_HEAD_MAX) continue;
        i64 t = 0;
        for (isize k = 0; k < sp && l.p[k] >= '0' && l.p[k] <= '9'; k++) t = t * 10 + (l.p[k] - '0');
        r->name[r->n] = str_cut(l, sp + 1, l.len), r->when[r->n] = t, r->taken[r->n] = false, r->n++;
    }
    r->more = at < refs.len;
}

// for-each-ref's output as far as its last whole line: past its room (cap) the rest is gone, and the
// line it cut isn't a branch (review, 2026-11-03).
static Str git_refs_whole(Str out, isize cap, bool *cut)
{
    *cut = out.len >= cap;
    if (!*cut) return out;
    isize e = out.len;
    while (e > 0 && out.p[e - 1] != '\n') e--;
    return str_cut(out, 0, e);
}

// The ref named name (whole names only), or -1.
static int git_refs_find(const GitRefs *r, Str name)
{
    for (int k = 0; k < r->n; k++)
        if (str_eq(r->name[k], name)) return k;
    return -1;
}

// Ref k onto b, unless it is there already or b is full (-1: none). False once b is full.
static bool git_branch_add(GitBranches *b, GitRefs *r, int k)
{
    if (b->n >= GIT_BRANCHES_MAX) return false;
    if (k < 0 || r->taken[k]) return true;
    snprintf(b->name[b->n], GIT_HEAD_MAX, "%.*s", STR_ARG(r->name[k]));
    b->when[b->n++] = r->when[k], r->taken[k] = true;
    return b->n < GIT_BRANCHES_MAX;
}

// The branches into b: head (the one checked out, "" when detached) first, then those the reflog's
// checkouts name, newest first ("checkout: moving from A to B": B, then A), then the rest of refs,
// which come newest commit first; only local branches (refs' lines), each once, GIT_BRANCHES_MAX at
// most. The menu's 8 are the first. cut: refs' end was cut off (git_refs_whole). With refs not all
// read, head is listed even when they don't name it: HEAD's own refs/heads name, with no time
// (review, 2026-11-03: a sort by shared commit dates had put it past the cut).
static void git_branches_merge(GitBranches *b, const char *head, Str reflog, Str refs, bool cut)
{
    static GitRefs r;   // (the worker's, or a test's)
    git_refs_parse(&r, refs);
    b->n = 0, b->cur = -1, b->total = r.n, b->more = r.more || cut;
    if (head[0]) {
        int k = git_refs_find(&r, str_from_c(head));
        git_branch_add(b, &r, k), b->cur = k >= 0 ? 0 : -1;
        if (k < 0 && b->more) snprintf(b->name[0], GIT_HEAD_MAX, "%s", head), b->when[0] = 0, b->n = 1, b->cur = 0, b->total++;
    }
    static const Str MOVE = S("checkout: moving from ");
    for (isize e = reflog.len; e > 0 && b->n < GIT_BRANCHES_MAX;) {
        isize s = e - 1;
        while (s > 0 && reflog.p[s - 1] != '\n') s--;
        Str l = str_cut(reflog, s, reflog.p[e - 1] == '\n' ? e - 1 : e);
        e = s;
        isize tab = str_find(l, '\t');
        if (tab < 0) continue;
        Str m = str_cut(l, tab + 1, l.len);
        if (!str_starts_with(m, MOVE)) continue;
        m = str_cut(m, MOVE.len, m.len);
        isize to = -1;   // (" to ": the last one, as a name has no spaces)
        for (isize i = m.len - 4; i >= 0 && to < 0; i--)
            if (str_eq(str_cut(m, i, i + 4), S(" to "))) to = i;
        if (to < 0) continue;
        if (!git_branch_add(b, &r, git_refs_find(&r, str_cut(m, to + 4, m.len)))) break;
        if (!git_branch_add(b, &r, git_refs_find(&r, str_cut(m, 0, to)))) break;
    }
    for (int k = 0; k < r.n && b->n < GIT_BRANCHES_MAX; k++) git_branch_add(b, &r, k);
}

// The last GIT_REFLOG_TAIL bytes of HEAD's reflog in gitdir (whole lines), into buf; empty without one.
static Str git_reflog_tail(const char *gitdir, char *buf, isize cap)
{
    char p[PATH_MAX + 16];
    snprintf(p, sizeof p, "%s/logs/HEAD", gitdir);
    int fd = open(p, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);   // (a FIFO there would block the open: F12 step 5b review)
    if (fd < 0) return STR_EMPTY;
    struct stat st;
    isize len = 0;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        off_t from = st.st_size > cap ? st.st_size - cap : 0;
        ssize_t k = pread(fd, buf, (size_t)(st.st_size - from < cap ? st.st_size - from : cap), from);
        len = k > 0 ? k : 0;
        if (from > 0) {   // (the first line is cut: dropped)
            isize nl = str_find((Str){ buf, len }, '\n');
            close(fd);
            return nl < 0 ? STR_EMPTY : str_cut((Str){ buf, len }, nl + 1, len);
        }
    }
    close(fd);
    return (Str){ buf, len };
}

// HEAD's branch in gitdir into name ("ref: refs/heads/<name>"); "" when detached, or it can't be
// read (not followed if a link, nor waited on if a FIFO).
static void git_head_name(const char *gitdir, char *name, size_t cap)
{
    char p[PATH_MAX + 8], line[GIT_HEAD_MAX + 32];
    name[0] = 0;
    snprintf(p, sizeof p, "%s/HEAD", gitdir);
    isize k = file_read_buf(p, line, sizeof line - 1);
    if (k <= 0) return;
    line[k] = 0;
    line[strcspn(line, "\n")] = 0;
    if (strncmp(line, "ref: refs/heads/", 16) == 0) snprintf(name, cap, "%s", line + 16);
}

// Whether HEAD in gitdir is on local branch name.
static bool git_head_is(const char *gitdir, const char *name)
{
    char now[GIT_HEAD_MAX];
    git_head_name(gitdir, now, sizeof now);
    return now[0] && strcmp(now, name) == 0;
}

// The branch menu's and panel's list for folder dir (resolved) in repository top, its HEAD's branch
// read now (git_head_name; "": detached): the local branches from `git for-each-ref`, each with its
// last commit's time (newest commit first; in bubblewrap, as git_status), merged with the reflog's
// checkouts; its output as far as its last whole line (git_refs_whole). False when git didn't run or
// said nothing.
static bool git_branches(const char *git, const char *dir, const char *top, GitBranches *b)
{
    static char out[GIT_REFS_OUT_MAX], tail[GIT_REFLOG_TAIL];   // (the worker's only)
    const char *argv[] = { git, "--no-optional-locks", "-c", "core.fsmonitor=false", "for-each-ref", "--sort=-committerdate", "--format=%(committerdate:unix) %(refname:lstrip=2)", "refs/heads/", NULL };
    char gitdir[PATH_MAX];
    GitBinds binds;
    BoxJob j;
    git_job(&j, &binds, argv, dir, top, false);
    git_gitdir(top, gitdir, sizeof gitdir);
    char head[GIT_HEAD_MAX];
    git_head_name(gitdir, head, sizeof head);
    isize len = 0;
    j.out = out, j.cap = sizeof out, j.len = &len;
    if (box_run(&j) != BOX_ENDED || len == 0) return false;
    bool cut;
    Str refs = git_refs_whole((Str){ out, len }, sizeof out, &cut);
    git_branches_merge(b, head, git_reflog_tail(gitdir, tail, sizeof tail), refs, cut);
    return true;
}

// ---- switching (F12 step 5c) ---------------------------------------------------------------------

enum { GIT_SWITCH_MS = 120000, GIT_SAID_MAX = KiB(16) };   // (a big checkout writes many files: 2 minutes before it is killed)
static int git_switch_ms = GIT_SWITCH_MS;   // (OFX_TEST_GIT_SWITCH_MS, gui/; a unit test's shorter)

// What git said, for the status line: its last "error:" or "fatal:" line, the word itself left out
// (else its last line); "" for nothing.
static void git_said(Str out, char *msg, size_t cap)
{
    Str last = STR_EMPTY, bad = STR_EMPTY;
    for (isize at = 0; at < out.len;) {
        isize e = at;
        while (e < out.len && out.p[e] != '\n') e++;
        Str l = str_trim(str_cut(out, at, e));
        if (l.len) last = l;
        if (str_starts_with(l, S("error: ")) || str_starts_with(l, S("fatal: "))) bad = str_trim(str_cut(l, 7, l.len));
        at = e + 1;
    }
    Str s = bad.len ? bad : last;
    snprintf(msg, cap, "%.*s", STR_ARG(s));
}

typedef enum { GSW_DONE, GSW_REFUSED, GSW_GOING } GitSwitch;

// `git switch <name>` in repository top (resolved): in bubblewrap as git_status, but the repository
// writable, and a worktree's or a submodule's real .git (Michael, 2026-11-03), so its hooks and
// filters run as in a terminal's switch yet reach nothing else (no network; git's config files
// read only). Only a local branch's name, never an option (a name can't start with "-"), and only
// where the guard lets ofx write. It runs to its end whatever happens (BoxJob.outlive).
//   GSW_DONE: HEAD is on name after; msg what a hook or a filter said ("" for nothing: -q).
//   GSW_REFUSED: git's refusal (or why it didn't run) in msg.
//   GSW_GOING: still running after git_switch_ms; it carries on (the HEAD watch sees its end).
static GitSwitch git_switch(const char *git, const char *top, const char *name, char *msg, size_t cap)
{
    static char out[GIT_SAID_MAX];   // (the worker's only)
    Err err;
    msg[0] = 0;
    if (!name[0] || name[0] == '-' || strlen(name) >= GIT_HEAD_MAX) return snprintf(msg, cap, "not a branch: %s", name), GSW_REFUSED;
    if (!ops_allowed(top, &err)) return snprintf(msg, cap, "%s", err.msg), GSW_REFUSED;
    const char *argv[] = { git, "-c", "core.fsmonitor=false", "switch", "-q", "--no-guess", name, NULL };   // (no guess: a branch gone since the menu opened isn't made from a remote's)
    char gitdir[PATH_MAX];
    GitBinds binds;
    BoxJob j;
    git_job(&j, &binds, argv, top, top, true);
    git_gitdir(top, gitdir, sizeof gitdir);
    // (common comes from the repository's own .git file: the guard asked for it too; review, 2026-11-03)
    if (binds.common[0] && !ops_allowed(binds.common, &err)) return snprintf(msg, cap, "%s", err.msg), GSW_REFUSED;
    j.ms = git_switch_ms;
    isize len = 0;
    Err why = { "" };
    j.out = out, j.cap = sizeof out, j.len = &len, j.err = true, j.outlive = true, j.why = &why;
    BoxRun r = box_run(&j);
    if (r == BOX_GOING) return snprintf(msg, cap, "still switching after %g s; it carries on", git_switch_ms / 1000.0), GSW_GOING;
    git_said((Str){ out, len }, msg, cap);
    if (git_head_is(gitdir, name)) return GSW_DONE;
    if (r == BOX_NOT_RUN) snprintf(msg, cap, "%s", why.msg[0] ? why.msg : "git didn't run");
    if (!msg[0]) snprintf(msg, cap, "git switch failed");
    return GSW_REFUSED;
}
