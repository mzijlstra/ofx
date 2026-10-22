// The trash, by the freedesktop Trash spec 1.0 (docs/plan.md, F3 "Trash"): a file on the home
// drive goes to $XDG_DATA_HOME/Trash, one on another drive to that drive's own trash, never across
// drives. Nemo's trash view and `gio trash --list` show what ofx trashed.

#include "../base/base.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// For the tests: a top dir to use instead of the one a file's drive has, as if the file were on
// another drive (a unit test can't mount one).
static const char *trash_test_topdir;

// s percent-encoded as a URI path: unreserved bytes and '/' kept, the rest as %XX.
static bool percent_encode(Str s, char *out, size_t cap)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t n = 0;
    for (isize i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.p[i];
        bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                    c == '_' || c == '~' || c == '/';
        if (n + (keep ? 1 : 3) >= cap) return false;
        if (keep) out[n++] = (char)c;
        else out[n++] = '%', out[n++] = HEX[c >> 4], out[n++] = HEX[c & 15];
    }
    out[n] = 0;
    return true;
}

// The home trash's folder: $XDG_DATA_HOME/Trash, else ~/.local/share/Trash.
static bool trash_home(char *out, size_t cap)
{
    const char *x = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    int n;
    if (x && x[0] == '/') n = snprintf(out, cap, "%s/Trash", x);
    else if (home && home[0]) n = snprintf(out, cap, "%s/.local/share/Trash", home);
    else return false;
    return n >= 0 && (size_t)n < cap;
}

// The device of path, or of the nearest folder above it that exists.
static bool dev_of(const char *path, dev_t *dev)
{
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s", path);
    for (;;) {
        struct stat st;
        if (stat(p, &st) == 0) return *dev = st.st_dev, true;
        char *slash = strrchr(p, '/');
        if (!slash || slash == p) return stat("/", &st) == 0 ? (*dev = st.st_dev, true) : false;
        *slash = 0;
    }
}

// The top of the drive that holds folder dir (resolved): the highest folder above it on the same
// device.
static void topdir_of(const char *dir, dev_t dev, char *out, size_t cap)
{
    snprintf(out, cap, "%s", dir);
    while (strcmp(out, "/") != 0) {
        char up[PATH_MAX];
        Str d = path_dir(str_from_c(out));
        snprintf(up, sizeof up, "%.*s", STR_ARG(d.len ? d : S("/")));
        struct stat st;
        if (stat(up, &st) != 0 || st.st_dev != dev) return;
        snprintf(out, cap, "%s", up);
    }
}

// mkdir -p for the trash's own folders, 0700.
static bool mkdir_private(const char *path, Err *err)
{
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1;; s++) {
        if (*s == '/' || *s == 0) {
            char c = *s;
            *s = 0;
            if (mkdir(p, 0700) != 0 && errno != EEXIST) return err_set(err, "%s: %s", p, strerror(errno)), false;
            *s = c;
            if (!c) break;
        }
    }
    struct stat st;
    if (lstat(path, &st) != 0 || !S_ISDIR(st.st_mode)) return err_set(err, "%s isn't a folder", path), false;
    return true;
}

// Where a trashed file is: its file, its .trashinfo, and the place it came from.
typedef struct {
    char file[PATH_MAX];
    char info[PATH_MAX];
    char from[PATH_MAX];
} Trashed;

// The trash folder for a file in folder dir (resolved), into out; *top gets the top dir whose
// trash it is ("" for the home trash), which info Paths are relative to.
static bool trash_for(const char *dir, char *out, size_t cap, char *top, size_t tcap, Err *err)
{
    char home[PATH_MAX];
    dev_t hd = 0, fd = 0;
    top[0] = 0;
    if (!trash_test_topdir && trash_home(home, sizeof home) && dev_of(home, &hd) && dev_of(dir, &fd) && hd == fd) {
        snprintf(out, cap, "%s", home);
        return true;
    }
    if (trash_test_topdir) snprintf(top, tcap, "%s", trash_test_topdir);
    else topdir_of(dir, fd, top, tcap);
    uid_t uid = getuid();
    char admin[PATH_MAX];
    struct stat st;
    snprintf(admin, sizeof admin, "%s%s.Trash", top, strcmp(top, "/") == 0 ? "" : "/");
    // $topdir/.Trash/$uid only in a real, sticky .Trash folder (spec: else it could be a trap).
    if (lstat(admin, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & S_ISVTX)) {
        snprintf(out, cap, "%s/%u", admin, (unsigned)uid);
        if (!ops_allowed(out, err)) return false;
        if (mkdir(out, 0700) == 0 || errno == EEXIST) {
            if (lstat(out, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == uid) return true;
        }
    }
    snprintf(out, cap, "%s%s.Trash-%u", top, strcmp(top, "/") == 0 ? "" : "/", (unsigned)uid);
    if (!ops_allowed(out, err)) return false;
    if (mkdir(out, 0700) != 0 && errno != EEXIST) return err_set(err, "%s: %s", out, strerror(errno)), false;
    if (lstat(out, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != uid)
        return err_set(err, "%s isn't a trash folder of yours", out), false;
    return true;
}

// path (a file, folder or link; not followed) moved to the trash; *t says where, for undo.
static bool trash_put(const char *path, Trashed *t, Err *err)
{
    char res[PATH_MAX], trash[PATH_MAX], top[PATH_MAX], files[PATH_MAX], info[PATH_MAX];
    if (!ops_allowed(path, err)) return false;
    if (!ops_resolve(path, res, sizeof res)) return err_set(err, "%s: %s", path, strerror(errno)), false;
    struct stat st;
    if (lstat(res, &st) != 0) return err_set(err, "%s: %s", path, strerror(errno)), false;
    Str dir = path_dir(str_from_c(res)), base = path_base(str_from_c(res));
    char d[PATH_MAX];
    snprintf(d, sizeof d, "%.*s", STR_ARG(dir.len ? dir : S("/")));
    if (!trash_for(d, trash, sizeof trash, top, sizeof top, err)) return false;
    if (snprintf(files, sizeof files, "%s/files", trash) >= (int)sizeof files || snprintf(info, sizeof info, "%s/info", trash) >= (int)sizeof info)
        return err_set(err, "%s: the path is too long", trash), false;
    if (!ops_allowed(files, err) || !mkdir_private(files, err) || !mkdir_private(info, err)) return false;

    // Path=: absolute in the home trash, relative to the top dir in a drive's own.
    const char *shown = res;
    size_t tl = strlen(top);
    if (top[0] && strncmp(res, top, tl) == 0 && (res[tl] == '/' || strcmp(top, "/") == 0)) shown = res + (strcmp(top, "/") == 0 ? 1 : tl + 1);
    char enc[PATH_MAX * 3];
    if (!percent_encode(str_from_c(shown), enc, sizeof enc)) return err_set(err, "%s: the path is too long", path), false;
    char date[32];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", &tm);

    // The .trashinfo first, with O_EXCL: that picks the free name (spec), "name", "name.2", ….
    for (int k = 1; k < 10000; k++) {
        char nm[NAME_MAX + 16];
        if (k == 1) snprintf(nm, sizeof nm, "%.*s", STR_ARG(base));
        else snprintf(nm, sizeof nm, "%.*s.%d", STR_ARG(base), k);
        if (snprintf(t->info, sizeof t->info, "%s/%s.trashinfo", info, nm) >= (int)sizeof t->info ||
            snprintf(t->file, sizeof t->file, "%s/%s", files, nm) >= (int)sizeof t->file)
            return err_set(err, "%s: the path is too long", path), false;
        struct stat ex;
        if (lstat(t->file, &ex) == 0) continue;
        int fd = open(t->info, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            return err_set(err, "%s: %s", t->info, strerror(errno)), false;
        }
        char body[PATH_MAX * 3 + 128];
        int n = snprintf(body, sizeof body, "[Trash Info]\nPath=%s\nDeletionDate=%s\n", enc, date);
        bool wrote = n > 0 && write(fd, body, (size_t)n) == n;
        close(fd);
        if (!wrote) {
            unlink(t->info);
            return err_set(err, "%s: cannot write it", t->info), false;
        }
        if (renameat2(AT_FDCWD, res, AT_FDCWD, t->file, RENAME_NOREPLACE) != 0) {
            int e = errno;
            unlink(t->info);
            if (e == EXDEV) return err_set(err, "%s: its trash is on another drive", path), false;
            return err_set(err, "%s: %s", path, strerror(e)), false;
        }
        snprintf(t->from, sizeof t->from, "%s", res);
        return true;
    }
    return err_set(err, "%s: no free name in the trash", path), false;
}

// A trashed file back where it came from, its .trashinfo gone; never over something there now.
static bool trash_restore(const Trashed *t, Err *err)
{
    if (!ops_allowed(t->from, err)) return false;
    if (renameat2(AT_FDCWD, t->file, AT_FDCWD, t->from, RENAME_NOREPLACE) != 0) {
        if (errno == EEXIST) return err_set(err, "%s is there again", t->from), false;
        return err_set(err, "%s: %s", t->file, strerror(errno)), false;
    }
    unlink(t->info);
    return true;
}
