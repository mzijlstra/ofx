// The test guard (code-health review, 2026-11-06: defined after two files that used it, through
// forward declarations): included right after paths.c, so every file after it in fs/ has it. With
// OFX_GUI_ROOT set (the GUI tests) every write is refused outside that folder, and only a program
// inside it runs, so a bug in a test, or a mutant, can never reach Michael's files or run his
// udisksctl. The unit tests set it to their mkdtemp root. First, the PATH lookups the program
// check needs (and fs/spawn.c runs by).

#include "../base/base.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Whether path is an executable file (not a folder: access(X_OK) passes one).
static bool exec_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

// The first executable file named prog in a PATH directory, into out; false when there is none (an
// empty entry is skipped, not the working folder).
static bool path_find(const char *prog, char *out, size_t cap)
{
    const char *path = getenv("PATH");
    Str p = str_from_c(path ? path : "");
    for (isize at = 0; at <= p.len;) {
        isize e = at;
        while (e < p.len && p.p[e] != ':') e++;
        if (e > at && snprintf(out, cap, "%.*s/%s", (int)(e - at), p.p + at, prog) < (int)cap && exec_file(out)) return true;
        at = e + 1;
    }
    return false;
}

// prog as it will run, into out: as given with a /, else as PATH finds it.
static bool prog_resolve(const char *prog, char *out, size_t cap)
{
    return strchr(prog, '/') ? snprintf(out, cap, "%s", prog) < (int)cap && exec_file(out) : path_find(prog, out, cap);
}

// Whether prog is an executable file in a PATH directory.
static bool on_path(const char *prog)
{
    char full[4096];
    return path_find(prog, full, sizeof full);
}

// ---- the guard ----------------------------------------------------------------------------------

static char ops_root[PATH_MAX];   // resolved; empty: no guard
static int  ops_refusals;         // writes the guard refused, for the tests

static void ops_guard(const char *root)
{
    ops_root[0] = 0;
    if (root && root[0] && !realpath(root, ops_root)) {
        // A guard that can't be resolved refuses everything rather than nothing.
        snprintf(ops_root, sizeof ops_root, "/nonexistent/ofx-guard");
    }
}

// path resolved into out: its nearest folder that exists, through realpath, then the rest as it
// is (the parts that don't exist yet, and the last part, which may be a symlink that is the thing
// itself). False when nothing resolves, or the rest climbs with "..".
static bool ops_resolve(const char *path, char *out, size_t cap)
{
    char d[PATH_MAX], r[PATH_MAX];
    Str p = str_from_c(path), rest = path_base(p), dir = path_dir(p);
    for (;;) {
        snprintf(d, sizeof d, "%.*s", STR_ARG(dir.len ? dir : S("/")));
        if (realpath(d, r)) break;
        if (dir.len == 0) return false;
        rest = str_cut(p, dir.len - path_base(dir).len, p.len);
        dir = path_dir(dir);
    }
    for (isize i = 0; i < rest.len;) {
        isize e = i;
        while (e < rest.len && rest.p[e] != '/') e++;
        if (str_eq(str_cut(rest, i, e), S(".."))) return false;
        i = e + 1;
    }
    int n = snprintf(out, cap, "%s%s%.*s", r, strcmp(r, "/") == 0 ? "" : "/", STR_ARG(rest));
    return n >= 0 && (size_t)n < cap;
}

// Whether prog, as PATH finds it (or as given, with a /), may run (2026-10-30, a review): with the
// guard on, only one under its folder (a test's fake), so a test whose PATH lost its fakes never
// runs the real udisksctl on Michael's drives, nor any program ofx starts (spawn_detached). Without
// the guard, any.
static bool ops_program_allowed(const char *prog, Err *err)
{
    if (!ops_root[0]) return true;
    char full[PATH_MAX], r[PATH_MAX];
    if (prog_resolve(prog, full, sizeof full) && realpath(full, r) && path_below(r, ops_root)) return true;
    snprintf(err->msg, sizeof err->msg, "%.64s isn't a test's own (outside %.300s): not run", prog, ops_root);
    return false;
}

// Whether a write to path is allowed: always without a guard; with one, only when path is under it.
static bool ops_allowed(const char *path, Err *err)
{
    if (!ops_root[0]) return true;
    char r[PATH_MAX];
    if (ops_resolve(path, r, sizeof r) && path_below(r, ops_root)) return true;
    ops_refusals++;
    fprintf(stderr, "ofx: guard: refused a write to %s (outside %s)\n", path, ops_root);
    return err_set(err, "%s: outside the test folder, refused", path), false;
}
