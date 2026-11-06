// Changing files, the small ones: the test guard, names (valid, free, " (2)", " (copy)"), rename,
// a new folder, and deleting a tree for good (docs/plan.md, F3). Every write in fs/ asks
// ops_allowed first.

#include "../base/base.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

// ---- the test guard ---------------------------------------------------------------------------
//
// With OFX_GUI_ROOT set (the GUI tests) every write is refused outside that folder, so a bug in a
// test, or a mutant, can never reach Michael's files. The unit tests set it to their mkdtemp root.

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

// ---- names ------------------------------------------------------------------------------------

// A name a file can have: not empty, no '/', not "." or "..", at most NAME_MAX bytes.
static bool name_valid(Str name, Err *err)
{
    if (name.len == 0) return err_set(err, "the name is empty"), false;
    if (str_find(name, '/') >= 0) return err_set(err, "a name can't hold a /"), false;
    if (str_eq(name, S(".")) || str_eq(name, S(".."))) return err_set(err, "%.*s isn't a name", STR_ARG(name)), false;
    if (name.len > NAME_MAX) return err_set(err, "the name is too long"), false;
    return true;
}

static bool exists_at(const char *dir, Str name)
{
    char p[PATH_MAX];
    struct stat st;
    return !path_put(p, sizeof p, dir, name) || lstat(p, &st) == 0;   // too long counts as taken
}

// name with a tag before its extension: "a.txt" + " (2)" = "a (2).txt". Folders, and names whose
// only dot is the first, take it at the end.
static void name_tagged(char *out, size_t cap, Str name, bool dir, const char *tag)
{
    isize dot = dir ? -1 : str_find_last(name, '.');
    if (dot <= 0) snprintf(out, cap, "%.*s%s", STR_ARG(name), tag);
    else snprintf(out, cap, "%.*s%s%.*s", (int)dot, name.p, tag, (int)(name.len - dot), name.p + dot);
}

// The first name free in dir: name itself unless `fresh`, then name with " (2)", " (3)", …, or with
// copy: " (copy)", " (copy 2)", …. False when none is free below 10000.
static bool name_free(const char *dir, Str name, bool is_dir, bool copy, bool fresh, char *out, size_t cap)
{
    if (!fresh && !exists_at(dir, name)) return snprintf(out, cap, "%.*s", STR_ARG(name)), true;
    for (int k = copy ? 1 : 2; k < 10000; k++) {
        char tag[32];
        if (copy) snprintf(tag, sizeof tag, k == 1 ? " (copy)" : " (copy %d)", k);
        else snprintf(tag, sizeof tag, " (%d)", k);
        name_tagged(out, cap, name, is_dir, tag);
        if (!exists_at(dir, str_from_c(out))) return true;
    }
    return false;
}

// ---- rename, new folder -----------------------------------------------------------------------

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif

// from to to, never over something that is there.
static bool move_noreplace(const char *from, const char *to, Err *err)
{
    if (!ops_allowed(from, err) || !ops_allowed(to, err)) return false;
    if (renameat2(AT_FDCWD, from, AT_FDCWD, to, RENAME_NOREPLACE) == 0) return true;
    if (errno == EEXIST) return err_set(err, "%.*s exists", STR_ARG(path_base(str_from_c(to)))), false;
    return err_set(err, "%s: %s", from, strerror(errno)), false;
}

// dir/from renamed to dir/to.
static bool op_rename(const char *dir, Str from, Str to, Err *err)
{
    if (!name_valid(to, err)) return false;
    char a[PATH_MAX], b[PATH_MAX];
    if (!path_put(a, sizeof a, dir, from) || !path_put(b, sizeof b, dir, to)) return err_set(err, "the path is too long"), false;
    return move_noreplace(a, b, err);
}

typedef enum { MAKE_DIR, MAKE_FILE, MAKE_LINK } MakeKind;

// A new folder, empty file or link (to target) in dir called name, or name (2) and so on when it
// is taken (a file's and a link's before the extension); *made gets the name. Tried five times, as
// another program may take the name in between (code-health review, 2026-11-06: three copies).
static bool op_make(MakeKind kind, const char *dir, const char *target, Str name, char *made, size_t cap, Err *err)
{
    if (!name_valid(name, err)) return false;
    for (int tries = 0; tries < 5; tries++) {
        char p[PATH_MAX];
        if (!name_free(dir, name, kind == MAKE_DIR, false, false, made, cap) || !path_put(p, sizeof p, dir, str_from_c(made)))
            return err_set(err, "no free name for %.*s", STR_ARG(name)), false;
        if (!ops_allowed(p, err)) return false;
        int fd = -1;
        bool ok = kind == MAKE_DIR ? mkdir(p, 0777) == 0
                : kind == MAKE_LINK ? symlink(target, p) == 0
                                    : (fd = open(p, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0666)) >= 0;
        if (fd >= 0) close(fd);
        if (ok) return true;
        if (errno != EEXIST) return err_set(err, "%s: %s", p, strerror(errno)), false;
    }
    return err_set(err, "no free name for %.*s", STR_ARG(name)), false;
}

// A new folder (F3), an empty file (F7.1), a link to target, an absolute path (Make Link, F8).
static bool op_mkdir(const char *dir, Str name, char *made, size_t cap, Err *err)
{
    return op_make(MAKE_DIR, dir, NULL, name, made, cap, err);
}

static bool op_mkfile(const char *dir, Str name, char *made, size_t cap, Err *err)
{
    return op_make(MAKE_FILE, dir, NULL, name, made, cap, err);
}

static bool op_symlink(const char *dir, const char *target, Str name, char *made, size_t cap, Err *err)
{
    return op_make(MAKE_LINK, dir, target, name, made, cap, err);
}

// ---- delete for good --------------------------------------------------------------------------

enum { TREE_DEPTH_MAX = 256 };

// Everything in the folder dirfd, depth first, never following a symlink (a link is removed, not
// what it points at). *count gets one per entry removed. False with the first error.
static bool rm_children(int dirfd, const char *shown, int depth, isize *count, Err *err)
{
    if (depth > TREE_DEPTH_MAX) return err_set(err, "%s: folders nested too deep", shown), false;
    int fd = fcntl(dirfd, F_DUPFD_CLOEXEC, 0);
    DIR *d = fd >= 0 ? fdopendir(fd) : NULL;
    if (!d) {
        if (fd >= 0) close(fd);
        return err_set(err, "%s: %s", shown, strerror(errno)), false;
    }
    // Entries removed while readdir runs may make it skip others, so passes go on until one finds
    // nothing left (the second, almost always).
    bool ok = true;
    for (isize removed = 1; ok && removed > 0;) {
        removed = 0;
        rewinddir(d);
        for (struct dirent *de; ok && (de = readdir(d));) {
            const char *nm = de->d_name;
            if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) continue;
            struct stat st;
            if (fstatat(dirfd, nm, &st, AT_SYMLINK_NOFOLLOW) != 0) {
                ok = (err_set(err, "%s/%s: %s", shown, nm, strerror(errno)), false);
                break;
            }
            if (S_ISDIR(st.st_mode)) {
                int sub = openat(dirfd, nm, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
                if (sub < 0) {
                    ok = (err_set(err, "%s/%s: %s", shown, nm, strerror(errno)), false);
                    break;
                }
                ok = rm_children(sub, nm, depth + 1, count, err);
                close(sub);
                if (ok && unlinkat(dirfd, nm, AT_REMOVEDIR) != 0) ok = (err_set(err, "%s/%s: %s", shown, nm, strerror(errno)), false);
            } else if (unlinkat(dirfd, nm, 0) != 0) {
                ok = (err_set(err, "%s/%s: %s", shown, nm, strerror(errno)), false);
            }
            if (ok) (*count)++, removed++;
        }
    }
    closedir(d);
    return ok;
}

// path deleted for good, a folder with everything in it; a symlink is removed itself.
static bool delete_tree(const char *path, isize *count, Err *err)
{
    if (!ops_allowed(path, err)) return false;
    struct stat st;
    if (lstat(path, &st) != 0) return err_set(err, "%s: %s", path, strerror(errno)), false;
    if (S_ISDIR(st.st_mode)) {
        int fd = open(path, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
        if (fd < 0) return err_set(err, "%s: %s", path, strerror(errno)), false;
        bool ok = rm_children(fd, path, 0, count, err);
        close(fd);
        if (!ok) return false;
        if (rmdir(path) != 0) return err_set(err, "%s: %s", path, strerror(errno)), false;
    } else if (unlink(path) != 0) {
        return err_set(err, "%s: %s", path, strerror(errno)), false;
    }
    (*count)++;
    return true;
}
