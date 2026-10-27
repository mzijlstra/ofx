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

// Whether a write to path is allowed: always without a guard; with one, only when path is under it.
static bool ops_allowed(const char *path, Err *err)
{
    if (!ops_root[0]) return true;
    char r[PATH_MAX];
    size_t n = strlen(ops_root);
    if (ops_resolve(path, r, sizeof r) && strncmp(r, ops_root, n) == 0 && r[n] == '/') return true;
    ops_refusals++;
    fprintf(stderr, "ofx: guard: refused a write to %s (outside %s)\n", path, ops_root);
    return err_set(err, "%s: outside the test folder, refused", path), false;
}

// ---- names ------------------------------------------------------------------------------------

// dir/name into out; false when it would not fit (a cut path could name another file).
static bool path_put(char *out, size_t cap, const char *dir, Str name)
{
    int n = snprintf(out, cap, "%s%s%.*s", dir, strcmp(dir, "/") == 0 ? "" : "/", STR_ARG(name));
    return n >= 0 && (size_t)n < cap;
}

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

// A new folder in dir called name, or name (2) and so on when it is taken; *made gets the name.
static bool op_mkdir(const char *dir, Str name, char *made, size_t cap, Err *err)
{
    if (!name_valid(name, err)) return false;
    for (int tries = 0; tries < 5; tries++) {   // another program may take the name in between
        char p[PATH_MAX];
        if (!name_free(dir, name, true, false, false, made, cap) || !path_put(p, sizeof p, dir, str_from_c(made)))
            return err_set(err, "no free name for %.*s", STR_ARG(name)), false;
        if (!ops_allowed(p, err)) return false;
        if (mkdir(p, 0777) == 0) return true;
        if (errno != EEXIST) return err_set(err, "%s: %s", p, strerror(errno)), false;
    }
    return err_set(err, "no free name for %.*s", STR_ARG(name)), false;
}

// An empty file named name in dir (F7.1), as op_mkdir makes a folder: "New file (2)" when taken.
static bool op_mkfile(const char *dir, Str name, char *made, size_t cap, Err *err)
{
    if (!name_valid(name, err)) return false;
    for (int tries = 0; tries < 5; tries++) {
        char p[PATH_MAX];
        if (!name_free(dir, name, false, false, false, made, cap) || !path_put(p, sizeof p, dir, str_from_c(made)))
            return err_set(err, "no free name for %.*s", STR_ARG(name)), false;
        if (!ops_allowed(p, err)) return false;
        int fd = open(p, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0666);
        if (fd >= 0) return close(fd), true;
        if (errno != EEXIST) return err_set(err, "%s: %s", p, strerror(errno)), false;
    }
    return err_set(err, "no free name for %.*s", STR_ARG(name)), false;
}

// A symbolic link in dir to target (an absolute path), named name or name (2) and so on (before
// its extension) when it is taken (Make Link, F8); *made gets the name.
static bool op_symlink(const char *dir, const char *target, Str name, char *made, size_t cap, Err *err)
{
    if (!name_valid(name, err)) return false;
    for (int tries = 0; tries < 5; tries++) {
        char p[PATH_MAX];
        if (!name_free(dir, name, false, false, false, made, cap) || !path_put(p, sizeof p, dir, str_from_c(made)))
            return err_set(err, "no free name for %.*s", STR_ARG(name)), false;
        if (!ops_allowed(p, err)) return false;
        if (symlink(target, p) == 0) return true;
        if (errno != EEXIST) return err_set(err, "%s: %s", p, strerror(errno)), false;
    }
    return err_set(err, "no free name for %.*s", STR_ARG(name)), false;
}

// ---- delete for good --------------------------------------------------------------------------

enum { TREE_DEPTH_MAX = 256 };

// Everything in the folder dirfd, depth first, never following a symlink (a link is removed, not
// what it points at). *count gets one per entry removed. False with the first error.
static bool rm_children(int dirfd, const char *shown, int depth, isize *count, Err *err)
{
    if (depth > TREE_DEPTH_MAX) return err_set(err, "%s: folders nested too deep", shown), false;
    int fd = dup(dirfd);
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
                int sub = openat(dirfd, nm, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
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
        int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
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

// ---- the default app for a type (F7.3.1) ------------------------------------------------------

// The user's own mimeapps.list, where a default is set: $XDG_CONFIG_HOME's, else ~/.config's.
static bool apps_user_list(char *out, size_t cap)
{
    const char *ch = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    int n = ch && ch[0] == '/' ? snprintf(out, cap, "%s/mimeapps.list", ch)
          : home && home[0]   ? snprintf(out, cap, "%s/.config/mimeapps.list", home)
                              : -1;
    return n > 0 && (size_t)n < cap;
}

// A list read before the user's own that sets a default for type (or an alias of it), whose
// setting a change in the user's list wouldn't override (Omarchy's hyprland-mimeapps.list): its
// path into out. False when none does.
static bool apps_shadowed(Arena *a, Str type, char *out, size_t cap)
{
    ArenaMark mark = arena_mark(a);
    char mine[PATH_MAX];
    const char *lists[APPS_LISTS_MAX];
    isize nl = apps_user_list(mine, sizeof mine) ? apps_lists(a, lists, APPS_LISTS_MAX) : 0;
    Str names[APPS_TYPES_MAX];
    isize nn = apps_names(a, &type, names, APPS_TYPES_MAX);
    bool found = false;
    for (isize k = 0; k < nl && !found && strcmp(lists[k], mine) != 0; k++) {
        Str text = read_file(a, lists[k]);
        for (isize j = 0; j < nn && !found; j++)
            if (ini_get(text, S("Default Applications"), names[j]).len) found = true, snprintf(out, cap, "%s", lists[k]);
    }
    arena_restore(a, mark);
    return found;
}

// The first app the user's list sets as type's default, into out ("" none).
static void apps_user_default(Arena *a, const char *list, Str type, char *out, size_t cap)
{
    ArenaMark mark = arena_mark(a);
    Str v = ini_get(read_file(a, list), S("Default Applications"), type);
    isize semi = str_find(v, ';');
    if (semi >= 0) v = str_cut(v, 0, semi);
    snprintf(out, cap, "%.*s", STR_ARG(str_trim(v)));
    arena_restore(a, mark);
}

// app (a desktop id) made type's default in the user's list, by `gio mime` (what Nemo's "Set as
// default" does: GLib writes [Default Applications] and adds it to [Added Associations]). No undo
// (Michael, 2026-10-26). False with the reason when the guard refuses it, gio doesn't run, or the
// list doesn't say so after.
static bool op_set_default(Arena *a, Str type, const char *app, Err *err)
{
    // The canonical name, not an alias gio info may give (.p12, .p7b): a list's canonical key would
    // win over the alias's, so setting the alias would do nothing while the check said done
    // (reviewer-2, F7.3.1 review).
    Str names[APPS_TYPES_MAX];
    apps_names(a, &type, names, APPS_TYPES_MAX);
    char list[PATH_MAX];
    if (!apps_user_list(list, sizeof list)) return err_set(err, "no home folder to keep the default in"), false;
    if (!ops_allowed(list, err)) return false;
    char t[256], out[1024];
    snprintf(t, sizeof t, "%.*s", STR_ARG(type));
    char *argv[] = { "gio", "mime", "--", t, (char *)app, NULL };
    bool late = false;
    if (!spawn_capture(argv, out, sizeof out, 3000, &late, err)) return false;
    char now[NAME_MAX + 1];
    apps_user_default(a, list, type, now, sizeof now);
    if (late || strcmp(now, app) != 0) {
        out[strcspn(out, "\n")] = 0;
        return err_set(err, "%s", out[0] ? out : "gio didn't set it"), false;
    }
    return true;
}
