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

// ---- the trash view (F6) ----------------------------------------------------------------------
//
// The home trash and each drive's (its .Trash/$uid and .Trash-$uid), read into one listing: an
// entry a trashed item, by its first name, with the time it was trashed as its time, and where
// it is (Entry.trash). An item without its .trashinfo, or a .trashinfo without its item, is left
// out, as gio does.

enum { TRASH_DIRS_MAX = 2 * PLACES_MAX + 1 };

typedef struct {
    char dir[TRASH_DIRS_MAX][PATH_MAX];   // trash folders (each with files/ and info/)
    char top[TRASH_DIRS_MAX][PATH_MAX];   // the top dir its relative Paths are under; "" for the home trash
    int  n;
} TrashDirs;

// A drive's trash folder that ofx lists only as the spec lets it write there (trash_for): its own
// folder, no symlink; .Trash/$uid only in a sticky .Trash that isn't a symlink (reviewer-2, F6).
enum { TRASH_HOME, TRASH_ADMIN, TRASH_USER };

static bool trash_dir_ours(const char *dir, int kind)
{
    struct stat st;
    if (kind == TRASH_HOME) return true;
    if (lstat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) return false;
    if (kind == TRASH_USER) return true;
    Str admin = path_dir(str_from_c(dir));
    char a[PATH_MAX];
    snprintf(a, sizeof a, "%.*s", STR_ARG(admin));
    return lstat(a, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & S_ISVTX);
}

static void trash_dirs_add(TrashDirs *d, const char *dir, const char *top, int kind)
{
    if (d->n == TRASH_DIRS_MAX || !trash_dir_ours(dir, kind)) return;
    for (int i = 0; i < d->n; i++)
        if (strcmp(d->dir[i], dir) == 0) return;
    char info[PATH_MAX + 8];
    snprintf(info, sizeof info, "%s/info", dir);
    struct stat st;
    if (lstat(info, &st) != 0 || !S_ISDIR(st.st_mode)) return;
    snprintf(d->dir[d->n], PATH_MAX, "%s", dir);
    snprintf(d->top[d->n++], PATH_MAX, "%s", top);
}

// The trash folders there are: the home trash, then each mount point's (the drives in pl, or
// the tests' own list), the ones that hold an info/ folder.
static void trash_dirs(TrashDirs *d, const char *const *mounts, int nmounts)
{
    d->n = 0;
    char home[PATH_MAX];
    if (trash_home(home, sizeof home)) trash_dirs_add(d, home, "", TRASH_HOME);
    unsigned uid = (unsigned)getuid();
    for (int i = 0; i < nmounts; i++) {
        const char *mp = mounts[i];
        const char *sep = strcmp(mp, "/") == 0 ? "" : "/";
        char dir[PATH_MAX + 32];
        snprintf(dir, sizeof dir, "%s%s.Trash/%u", mp, sep, uid);
        trash_dirs_add(d, dir, mp, TRASH_ADMIN);
        snprintf(dir, sizeof dir, "%s%s.Trash-%u", mp, sep, uid);
        trash_dirs_add(d, dir, mp, TRASH_USER);
    }
}

// Where a trashed item is, kept with its entry.
typedef struct TrashItem {
    const char *file, *info, *from;
    const char *top;   // the drive's top dir for a drive's trash, "" for the home trash
} TrashItem;

// Whether path has a ".." part: a .trashinfo on a drive anyone could have written (a USB stick)
// must not send a restore out of its drive (reviewer-2, F6 review).
static bool trash_path_climbs(const char *path)
{
    Str p = str_from_c(path);
    for (isize i = 0; i < p.len;) {
        isize e = i;
        while (e < p.len && p.p[e] != '/') e++;
        if (e - i == 2 && p.p[i] == '.' && p.p[i + 1] == '.') return true;
        i = e + 1;
    }
    return false;
}

// Whether path is top or under it ("/" holds everything).
static bool trash_under(const char *path, const char *top)
{
    size_t n = strlen(top);
    return strcmp(top, "/") == 0 || (strncmp(path, top, n) == 0 && (path[n] == '/' || path[n] == 0));
}

// A .trashinfo's Path (decoded, made absolute against top) and DeletionDate (local time, 0 when
// missing or unreadable); false when it has no usable Path. In a drive's trash (top set) the Path
// must stay on that drive: no ".." part, and an absolute one under top.
static bool trash_info_parse(Str text, const char *top, char *from, size_t cap, i64 *when)
{
    *when = 0;
    bool in_group = false, have = false;
    isize at = 0;
    for (Str line; next_line(text, &at, &line);) {
        line = str_trim(line);
        if (line.len && line.p[0] == '[') {
            in_group = str_eq(line, S("[Trash Info]"));
            continue;
        }
        if (!in_group) continue;
        if (str_starts_with(line, S("Path="))) {
            char dec[PATH_MAX];
            if (!percent_decode(str_cut(line, 5, line.len), dec, sizeof dec) || !dec[0] || trash_path_climbs(dec)) continue;
            int n;
            if (dec[0] == '/' && top[0] && !trash_under(dec, top)) continue;
            if (dec[0] == '/') n = snprintf(from, cap, "%s", dec);
            else if (top[0]) n = snprintf(from, cap, "%s%s%s", top, strcmp(top, "/") == 0 ? "" : "/", dec);
            else continue;   // relative in the home trash: nowhere to put it
            have = n > 0 && (size_t)n < cap;
        } else if (str_starts_with(line, S("DeletionDate="))) {
            char d[32];
            snprintf(d, sizeof d, "%.*s", STR_ARG(str_cut(line, 13, line.len)));
            struct tm tm = { 0 };
            char *end = strptime(d, "%Y-%m-%dT%H:%M:%S", &tm);
            if (end && *end == 0) tm.tm_isdst = -1, *when = (i64)mktime(&tm);
        }
    }
    return have;
}

// Every trashed item of the folders in d, sorted as a folder's listing is; dot-named ones counted
// in hidden and left out unless show_hidden. False when the arena is full.
static bool trash_list(Arena *a, const TrashDirs *d, bool show_hidden, Sort sort, Listing *out, Err *err)
{
    *out = (Listing){ 0 };
    isize cap = 64, n = 0;
    Entry *e = arena_try_new(a, Entry, cap);
    if (!e) return err_set(err, "the trash: too many items for memory"), false;
    for (int k = 0; k < d->n; k++) {
        char info[PATH_MAX + 8], files[PATH_MAX + 8];
        snprintf(info, sizeof info, "%s/info", d->dir[k]);
        snprintf(files, sizeof files, "%s/files", d->dir[k]);
        DIR *dir = opendir(info);
        if (!dir) continue;
        int ffd = open(files, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        for (struct dirent *de; ffd >= 0 && (de = readdir(dir));) {
            Str nm = str_from_c(de->d_name);
            if (!str_ends_with(nm, S(".trashinfo")) || nm.len == 10) continue;
            Str base = str_cut(nm, 0, nm.len - 10);
            char item[NAME_MAX + 1];
            snprintf(item, sizeof item, "%.*s", STR_ARG(base));
            struct stat st;
            if (fstatat(ffd, item, &st, AT_SYMLINK_NOFOLLOW) != 0) continue;   // no item: an orphan
            ArenaMark mark = arena_mark(a);
            char ipath[PATH_MAX + 8 + NAME_MAX + 16], from[PATH_MAX];
            snprintf(ipath, sizeof ipath, "%s/%s", info, de->d_name);
            i64 when;
            bool ok = trash_info_parse(read_file(a, ipath), d->top[k], from, sizeof from, &when);
            arena_restore(a, mark);
            if (!ok) continue;
            Str first = path_base(str_from_c(from));
            if (first.len && first.p[0] == '.') {
                out->hidden++;
                if (!show_hidden) continue;
            }
            if (n == cap) {
                Entry *grown = arena_try_new(a, Entry, cap * 2);
                if (!grown) {
                    closedir(dir), close(ffd);
                    return err_set(err, "the trash: too many items for memory"), false;
                }
                memcpy(grown, e, sizeof *e * (size_t)n);
                e = grown, cap *= 2;
            }
            TrashItem *t = arena_try_new(a, TrashItem, 1);
            char fpath[PATH_MAX + 8 + NAME_MAX + 2];
            snprintf(fpath, sizeof fpath, "%s/%s", files, item);
            if (!t) {
                closedir(dir), close(ffd);
                return err_set(err, "the trash: too many items for memory"), false;
            }
            *t = (TrashItem){ arena_cstr(a, str_from_c(fpath)), arena_cstr(a, str_from_c(ipath)), arena_cstr(a, str_from_c(from)),
                              arena_cstr(a, str_from_c(d->top[k])) };
            Kind kd = kind_of(ffd, item, &st);
            Str shown = path_base(str_from_c(t->from));
            e[n++] = (Entry){
                .name = shown,
                .kind = kd,
                .size = kind_is_dir(kd) || kd == K_LINK_BROKEN ? 0 : (i64)st.st_size,
                .mtime = when,
                .mode = kd == K_LINK_BROKEN ? 0 : (u32)st.st_mode,
                .uid = (u32)st.st_uid, .gid = (u32)st.st_gid,
                .target = kd == K_LINK_FILE || kd == K_LINK_DIR || kd == K_LINK_BROKEN ? link_target(a, ffd, item) : STR_EMPTY,
                .trash = t,
            };
        }
        if (ffd >= 0) close(ffd);
        closedir(dir);
    }
    out->e = e, out->n = n;
    fs_sort(out, sort);
    return true;
}

// A trashed item back where it came from (its folder made again when it is gone, as Nemo does);
// never over something there now.
static bool trash_restore_item(const TrashItem *t, Err *err)
{
    // From a drive's trash: the place it goes, its symlinks followed, still on that drive (a link
    // on a stick could point the restore at the home folder).
    if (t->top && t->top[0]) {
        char res[PATH_MAX], top[PATH_MAX];
        if (!realpath(t->top, top) || !ops_resolve(t->from, res, sizeof res) || !trash_under(res, top))
            return err_set(err, "%s: its trash says it came from outside its drive: not restored", t->from), false;
    }
    Trashed tr;
    snprintf(tr.file, sizeof tr.file, "%s", t->file);
    snprintf(tr.info, sizeof tr.info, "%s", t->info);
    snprintf(tr.from, sizeof tr.from, "%s", t->from);
    Str dir = path_dir(str_from_c(t->from));
    char d[PATH_MAX];
    snprintf(d, sizeof d, "%.*s", STR_ARG(dir.len ? dir : S("/")));
    struct stat st;
    if (stat(d, &st) != 0) {
        if (!ops_allowed(d, err)) return false;
        for (char *s = d + 1;; s++) {
            if (*s != '/' && *s != 0) continue;
            char c = *s;
            *s = 0;
            if (mkdir(d, 0755) != 0 && errno != EEXIST) return err_set(err, "%s: %s", d, strerror(errno)), false;
            *s = c;
            if (!c) break;
        }
    }
    return trash_restore(&tr, err);
}

// What Empty Trash deletes, the folders themselves kept (a trash watcher, gvfs's, watches info/;
// reviewer-2, F6 review): each item in files/ and after it its .trashinfo (deleted only when the
// item went), the items without one, the .trashinfo files without an item, and the home trash's
// directorysizes cache. Into out, at most max; false when that was too few.
typedef struct {
    const char *path;
    bool        after_ok;
} TrashDoomed;

// How many of d[at..n) one job of at most max items takes: never an item without its .trashinfo
// after it (which then waits for the next job).
static isize trash_empty_chunk(const TrashDoomed *d, isize n, isize at, isize max)
{
    isize k = n - at < max ? n - at : max;
    if (at + k < n && d[at + k].after_ok) k--;
    return k;
}

static bool trash_empty_items(Arena *a, const TrashDirs *d, TrashDoomed *out, isize max, isize *n)
{
    *n = 0;
    for (int k = 0; k < d->n; k++) {
        static const char *const SUB[] = { "files", "info" };
        for (int s = 0; s < 2; s++) {
            char sub[PATH_MAX + 8];
            snprintf(sub, sizeof sub, "%s/%s", d->dir[k], SUB[s]);
            DIR *dir = opendir(sub);
            if (!dir) continue;
            for (struct dirent *de; (de = readdir(dir));) {
                const char *nm = de->d_name;
                if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) continue;
                Str name = str_from_c(nm);
                char other[PATH_MAX + NAME_MAX + 32];
                struct stat st;
                if (s == 0) {   // an item, and its .trashinfo right after it
                    if (*n + 2 > max) return closedir(dir), false;
                    out[(*n)++] = (TrashDoomed){ arena_sprintf(a, "%s/%s", sub, nm), false };
                    snprintf(other, sizeof other, "%s/info/%s.trashinfo", d->dir[k], nm);
                    if (lstat(other, &st) == 0) out[(*n)++] = (TrashDoomed){ arena_cstr(a, str_from_c(other)), true };
                } else {        // a .trashinfo whose item isn't there (the others went with theirs)
                    if (str_ends_with(name, S(".trashinfo"))) {
                        snprintf(other, sizeof other, "%s/files/%.*s", d->dir[k], (int)(name.len - 10), nm);
                        if (lstat(other, &st) == 0) continue;
                    }
                    if (*n + 1 > max) return closedir(dir), false;
                    out[(*n)++] = (TrashDoomed){ arena_sprintf(a, "%s/%s", sub, nm), false };
                }
            }
            closedir(dir);
        }
        char cache[PATH_MAX + 32];
        struct stat st;
        snprintf(cache, sizeof cache, "%s/directorysizes", d->dir[k]);
        if (d->top[k][0] == 0 && lstat(cache, &st) == 0) {
            if (*n + 1 > max) return false;
            out[(*n)++] = (TrashDoomed){ arena_cstr(a, str_from_c(cache)), false };
        }
    }
    return true;
}
