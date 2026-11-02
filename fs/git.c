// Git status per file (F12 step 5, docs/plan.md "F12 design"): the repository a folder is in (one
// look for .git above it), and `git status --porcelain=v2 -z --ignored --branch -- .` run in that
// folder (gui/git.c runs it) read into the state of each entry the listing shows. Paths in the
// output are relative to the repository's top; a folder's state is the most urgent of what it
// holds (conflicted, modified, staged, untracked), and inside an ignored folder everything is
// ignored (git names the folder only).

#include "../base/base.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

// By urgency: a folder takes the highest of what it holds (an ignored file doesn't count there).
typedef enum { GS_NONE, GS_IGNORED, GS_UNTRACKED, GS_STAGED, GS_RENAMED, GS_MODIFIED, GS_CONFLICT, GS_N } GitState;

// The letter at a row's right (VS Code's), or 0 for none.
static char git_letter(GitState s)
{
    static const char L[GS_N] = { [GS_UNTRACKED] = 'U', [GS_STAGED] = 'A', [GS_RENAMED] = 'R', [GS_MODIFIED] = 'M', [GS_CONFLICT] = '!' };
    return s >= 0 && s < GS_N ? L[s] : 0;
}

enum { GIT_ENTRIES_MAX = 1 << 16, GIT_HEAD_MAX = 256 };

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
