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
    FILE *f = fopen(g, "re");
    if (!f) return;
    bool ok = fgets(line, sizeof line, f) && strncmp(line, "gitdir: ", 8) == 0;
    fclose(f);
    if (!ok) return;
    line[strcspn(line, "\n")] = 0;
    const char *to = line + 8;
    snprintf(at, sizeof at, "%s%s%s", to[0] == '/' ? "" : top, to[0] == '/' ? "" : "/", to);
    if (realpath(at, r)) snprintf(out, cap, "%s", r);
}

// The repository folder a .git file leads into, up to its .git, into out ("" for a .git folder):
// bound too, as its objects are there.
static void git_common(const char *top, char *out, size_t cap)
{
    git_linked(top, out, cap);
    char *dot = strstr(out, "/.git/");
    if (dot) dot[5] = 0;   // (…/.git/worktrees/wt, …/.git/modules/sub: the .git above)
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

// j set up to run argv (git's) in folder dir of repository top, as below: in bubblewrap, those of
// g's paths bound, for git_run_ms.
static void git_job(BoxJob *j, GitBinds *g, const char *const *argv, const char *dir, const char *top)
{
    const char *home = getenv("HOME"), *xdg = getenv("XDG_CONFIG_HOME");
    git_common(top, g->common, sizeof g->common);
    snprintf(g->home_conf, sizeof g->home_conf, "%s/.gitconfig", home ? home : "/nonexistent");
    if (xdg && xdg[0]) snprintf(g->xdg_conf, sizeof g->xdg_conf, "%s/git", xdg);
    else snprintf(g->xdg_conf, sizeof g->xdg_conf, "%s/.config/git", home ? home : "/nonexistent");
    *j = (BoxJob){ .argv = argv, .ro = { top, g->home_conf, g->xdg_conf, "/etc/gitconfig" }, .cwd = dir, .ms = git_run_ms };
    if (g->common[0]) j->ro[4] = g->common;
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
    git_job(&j, &binds, argv, dir, top);
    isize len = 0;
    j.out = arena_try_push(a, GIT_OUT_MAX, 1), j.cap = GIT_OUT_MAX, j.len = &len;
    if (!j.out || box_run(&j) != BOX_ENDED || len == 0) return false;
    if (!git_parse(a, (Str){ j.out, len }, g)) return false;
    if (len == GIT_OUT_MAX) g->cut = true;
    return true;
}

// ---- the branches (F12 step 5b): the branch button's menu ----------------------------------------

enum { GIT_BRANCHES_MAX = 8, GIT_REFLOG_TAIL = KiB(64), GIT_REFS_OUT_MAX = KiB(256) };

typedef struct {
    int  n;
    int  cur;   // the one checked out (-1: detached, or not among them)
    char name[GIT_BRANCHES_MAX][GIT_HEAD_MAX];
} GitBranches;

// b holds name already (whole names only).
static bool git_branch_has(const GitBranches *b, Str name)
{
    for (int i = 0; i < b->n; i++)
        if (str_eq(str_from_c(b->name[i]), name)) return true;
    return false;
}

// name onto b when it is one of refs (the local branches, one per line) and not there yet; false
// once b is full.
static bool git_branch_add(GitBranches *b, Str name, Str refs)
{
    if (b->n >= GIT_BRANCHES_MAX) return false;
    if (!name.len || name.len >= GIT_HEAD_MAX || git_branch_has(b, name)) return true;
    for (isize at = 0; at < refs.len;) {
        isize e = at;
        while (e < refs.len && refs.p[e] != '\n') e++;
        if (str_eq(str_cut(refs, at, e), name)) {
            snprintf(b->name[b->n++], GIT_HEAD_MAX, "%.*s", STR_ARG(name));
            break;
        }
        at = e + 1;
    }
    return b->n < GIT_BRANCHES_MAX;
}

// The menu's branches into b: head (the one checked out, "" when detached) first, then those the
// reflog's checkouts name, newest first ("checkout: moving from A to B": B, then A), then the rest of
// refs, which come newest commit first; only local branches (refs' lines), each once, 8 at most.
static void git_branches_merge(GitBranches *b, const char *head, Str reflog, Str refs)
{
    *b = (GitBranches){ .cur = -1 };
    if (head[0]) git_branch_add(b, str_from_c(head), refs), b->cur = b->n ? 0 : -1;
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
        if (!git_branch_add(b, str_cut(m, to + 4, m.len), refs)) break;
        if (!git_branch_add(b, str_cut(m, 0, to), refs)) break;
    }
    for (isize at = 0; at < refs.len && b->n < GIT_BRANCHES_MAX;) {
        isize e = at;
        while (e < refs.len && refs.p[e] != '\n') e++;
        git_branch_add(b, str_cut(refs, at, e), refs);
        at = e + 1;
    }
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

// The branch menu's list for folder dir (resolved) in repository top, whose HEAD is on head ("":
// detached): the local branches from `git for-each-ref` (newest commit first; in bubblewrap, as
// git_status), merged with the reflog's checkouts. False when git didn't run or said nothing.
static bool git_branches(const char *git, const char *dir, const char *top, const char *head, GitBranches *b)
{
    static char out[GIT_REFS_OUT_MAX], tail[GIT_REFLOG_TAIL];   // (the worker's only)
    const char *argv[] = { git, "--no-optional-locks", "-c", "core.fsmonitor=false", "for-each-ref", "--sort=-committerdate", "--format=%(refname:lstrip=2)", "refs/heads/", NULL };
    char gitdir[PATH_MAX];
    GitBinds binds;
    BoxJob j;
    git_job(&j, &binds, argv, dir, top);
    git_gitdir(top, gitdir, sizeof gitdir);
    isize len = 0;
    j.out = out, j.cap = sizeof out, j.len = &len;
    if (box_run(&j) != BOX_ENDED || len == 0) return false;
    git_branches_merge(b, head, git_reflog_tail(gitdir, tail, sizeof tail), (Str){ out, len });
    return true;
}
