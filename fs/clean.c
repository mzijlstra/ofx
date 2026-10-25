// The thumbnail cache's cleanup (F6.5, docs/plan.md "F6.5 design"): ofx-clean runs it daily from a
// systemd user timer. Nothing else cleans ~/.cache/thumbnails here (Nemo doesn't), and on
// 2026-10-25 258 MiB of its 362 were previews of files that are gone.
//
// Three rules, in order: a preview not read for CLEAN_UNUSED_S goes (its atime); a preview whose
// file is gone or has changed goes (its Thumb::URI and Thumb::MTime); then, while the cache is over
// CLEAN_CAP, the least recently read go. Only the cache's own names are looked at, in its size
// folders and fail/<program>/, never followed through a link, and each delete asks the test guard
// (ops.c) first.
//
// Every preview is opened with O_NOATIME: a plain read would mark it as just used, and the first
// rule would never fire for anything the second one checks.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

// Fixed until the settings page (Michael, 2026-10-25).
#define CLEAN_UNUSED_S (90 * 86400LL)
#define CLEAN_CAP      ((i64)1 << 30)
#define CLEAN_TMP_S    86400LL   // ofx's own temporary file, left by a crash, after a day

typedef enum { CR_KEEP, CR_TEMP, CR_UNUSED, CR_ORPHAN, CR_STALE, CR_CAP, CR_N } CleanRule;

static const char *const clean_rule_names[CR_N] = { "kept", "temp", "unused", "orphan", "stale", "cap" };

typedef struct {
    i64  now;    // seconds
    i64  cap;    // bytes; CLEAN_CAP but in the tests
    bool dry;    // list, delete nothing
    void (*say)(void *ctx, CleanRule r, const char *path, i64 size);   // each delete (or would-be one)
    void *ctx;
} CleanOpts;

typedef struct {
    isize n[CR_N];
    i64   bytes[CR_N];
    isize failed;   // deletes refused or failed
    bool  cut;      // the arena filled before every file was listed: the cap judged the listed ones
} CleanStats;

typedef struct {
    int  dir;        // index into the run's folders
    char name[48];
    i64  size, atime, mtime;
    bool temp;
} CleanFile;

// A preview's name: 32 lowercase hex digits and ".png".
static bool clean_name(const char *n)
{
    for (int i = 0; i < 32; i++)
        if (!((n[i] >= '0' && n[i] <= '9') || (n[i] >= 'a' && n[i] <= 'f'))) return false;
    return strcmp(n + 32, ".png") == 0;
}

// ofx's temporary name (thumb_write): .ofx-<pid>-<n>.png.
static bool clean_temp_name(const char *n)
{
    if (strncmp(n, ".ofx-", 5) != 0) return false;
    const char *p = n + 5;
    for (int part = 0; part < 2; part++) {
        const char *d = p;
        while (*p >= '0' && *p <= '9') p++;
        if (p == d || p - d > 10 || *p != (part ? '.' : '-')) return false;
        p++;
    }
    return strcmp(p, "png") == 0;
}

// A file:// URI back to its path: "file:///" then %XX escapes; false for another scheme, a host,
// a NUL, or a bad escape.
static bool clean_uri_path(const char *uri, char *out, size_t cap)
{
    if (strncmp(uri, "file:///", 8) != 0) return false;
    size_t o = 0;
    for (const char *p = uri + 7; *p; p++) {
        int c = (unsigned char)*p;
        if (c == '%') {
            int v = 0;
            for (int k = 1; k <= 2; k++) {
                int h = p[k];
                if (!h) return false;   // cut short: p[2] would be past the end
                v = v * 16 + (h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -256);
            }
            if (v <= 0) return false;
            c = v, p += 2;
        }
        if (o + 1 >= cap) return false;
        out[o++] = (char)c;
    }
    out[o] = 0;
    return true;
}

// Whether mountinfo lists dir as a mount point.
static bool clean_mounted(Str mountinfo, const char *dir)
{
    isize at = 0;
    for (Str line; next_line(mountinfo, &at, &line);) {
        Str f = line;
        for (int k = 0; k < 4; k++) {
            isize sp = str_find(f, ' ');
            f = sp < 0 ? STR_EMPTY : str_cut(f, sp + 1, f.len);
        }
        isize sp = str_find(f, ' ');
        char mp[PATH_MAX];
        if (sp > 0 && octal_decode(str_cut(f, 0, sp), mp, sizeof mp) && strcmp(mp, dir) == 0) return true;
    }
    return false;
}

// Whether path is on a drive that isn't mounted now: under /run/media/<user>/<name>, /media/<name>
// or /mnt/<name> when that folder isn't a mount point. Its previews wait for the drive.
static bool clean_absent_drive(Str mountinfo, const char *path)
{
    static const struct { const char *prefix; int depth; } roots[] = { { "/run/media/", 2 }, { "/media/", 1 }, { "/mnt/", 1 } };
    for (int i = 0; i < COUNT(roots); i++) {
        size_t n = strlen(roots[i].prefix);
        if (strncmp(path, roots[i].prefix, n) != 0) continue;
        const char *p = path + n;
        for (int d = 0; d < roots[i].depth; d++) {
            const char *s = strchr(p, '/');
            if (!s) return false;   // the folder itself, or a file beside the drives: not on one
            p = s + 1;
        }
        char top[PATH_MAX];
        snprintf(top, sizeof top, "%.*s", (int)(p - 1 - path), path);
        return !clean_mounted(mountinfo, top);
    }
    return false;
}

// What the second rule makes of one preview, read from dfd/name: CR_ORPHAN, CR_STALE or CR_KEEP.
static CleanRule clean_judge(Arena *a, Str mountinfo, int dfd, const char *name)
{
    int fd = openat(dfd, name, O_RDONLY | O_NOATIME | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return CR_KEEP;   // not ours to read without touching its atime: left to the others
    ArenaMark mark = arena_mark(a);
    CleanRule r = CR_KEEP;
    struct stat st;
    char *p;
    ThumbMeta *m = arena_try_new(a, ThumbMeta, 1);
    if (m && fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= THUMB_CACHED_MAX &&
        (p = arena_try_push(a, (isize)st.st_size, 1))) {
        isize n = 0;
        for (ssize_t got; n < st.st_size && (got = read(fd, p + n, (size_t)(st.st_size - n))) > 0;) n += got;
        char path[PATH_MAX];
        struct stat f;
        if (n == st.st_size && png_meta((Str){ p, n }, m) && m->has_uri && clean_uri_path(m->uri, path, sizeof path) &&
            thumb_local(mountinfo, path) && !clean_absent_drive(mountinfo, path)) {
            if (lstat(path, &f) != 0) r = errno == ENOENT || errno == ENOTDIR ? CR_ORPHAN : CR_KEEP;
            else if ((m->has_mtime && m->mtime != f.st_mtim.tv_sec) || (m->has_size && m->size != f.st_size)) r = CR_STALE;
        }
    }
    close(fd);
    arena_restore(a, mark);
    return r;
}

static int clean_by_atime(const void *x, const void *y)
{
    const CleanFile *a = x, *b = y;
    return a->atime < b->atime ? -1 : a->atime > b->atime;
}

// Removes dirs[f->dir]/f->name, or only says so on a dry run.
static void clean_drop(const CleanOpts *o, CleanStats *s, const char *const *dirs, const int *fds, const CleanFile *f, CleanRule r)
{
    char path[PATH_MAX];
    Err err;
    snprintf(path, sizeof path, "%s/%s", dirs[f->dir], f->name);
    if (!o->dry && (!ops_allowed(path, &err) || unlinkat(fds[f->dir], f->name, 0) != 0)) {
        s->failed++;
        return;
    }
    s->n[r]++, s->bytes[r] += f->size;
    if (o->say) o->say(o->ctx, r, path, f->size);
}

#define CLEAN_DIRS_MAX 64

// One run over the cache at root (thumb_root's). The arena holds the list of files; mountinfo is
// the mount list (/proc/self/mountinfo's text).
static void clean_run(Arena *a, const char *root, Str mountinfo, const CleanOpts *o, CleanStats *s)
{
    *s = (CleanStats){ 0 };
    const char *dirs[CLEAN_DIRS_MAX];
    int fds[CLEAN_DIRS_MAX], nd = 0;
    int rfd = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (rfd < 0) return;
    for (int i = 0; i < THUMB_LEVELS; i++) {
        int fd = openat(rfd, thumb_dirs[i], O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd >= 0) dirs[nd] = arena_sprintf(a, "%s/%s", root, thumb_dirs[i]), fds[nd++] = fd;
    }
    int ffd = openat(rfd, "fail", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    DIR *fd_dir = ffd >= 0 ? fdopendir(ffd) : NULL;
    if (!fd_dir && ffd >= 0) close(ffd);
    for (struct dirent *d; fd_dir && nd < CLEAN_DIRS_MAX && (d = readdir(fd_dir));) {
        if (d->d_name[0] == '.') continue;
        int fd = openat(dirfd(fd_dir), d->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd >= 0) dirs[nd] = arena_sprintf(a, "%s/fail/%s", root, d->d_name), fds[nd++] = fd;
    }
    if (fd_dir) closedir(fd_dir);
    close(rfd);

    // Every file of the cache's names, by lstat only.
    CleanFile *files = NULL;   // pushed one by one, nothing else between: one array
    isize nf = 0;
    for (int i = 0; i < nd; i++) {
        int dup_fd = dup(fds[i]);
        DIR *dd = dup_fd >= 0 ? fdopendir(dup_fd) : NULL;
        if (!dd) {
            if (dup_fd >= 0) close(dup_fd);
            continue;
        }
        for (struct dirent *d; (d = readdir(dd));) {
            bool temp = clean_temp_name(d->d_name);
            struct stat st;
            if ((!temp && !clean_name(d->d_name)) || fstatat(fds[i], d->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(st.st_mode)) continue;
            CleanFile *f = arena_try_new(a, CleanFile, 1);
            if (!f) {
                s->cut = true;
                break;
            }
            if (!files) files = f;
            *f = (CleanFile){ .dir = i, .size = st.st_size, .atime = st.st_atim.tv_sec, .mtime = st.st_mtim.tv_sec, .temp = temp };
            memcpy(f->name, d->d_name, strlen(d->d_name) + 1);   // both name checks allow at most 36 bytes
            nf++;
        }
        closedir(dd);
    }

    // The first two rules; what stays is packed at the front, for the cap.
    isize kept = 0;
    i64 total = 0;
    for (isize i = 0; i < nf; i++) {
        CleanFile *f = &files[i];
        CleanRule r = f->temp ? (o->now - f->mtime > CLEAN_TMP_S ? CR_TEMP : CR_KEEP)
                    : o->now - f->atime > CLEAN_UNUSED_S ? CR_UNUSED
                    : clean_judge(a, mountinfo, fds[f->dir], f->name);
        if (r != CR_KEEP) {
            clean_drop(o, s, dirs, fds, f, r);
            continue;
        }
        files[kept++] = *f, total += f->size;
    }

    // The cap: the least recently read first.
    if (total > o->cap) {
        qsort(files, (size_t)kept, sizeof *files, clean_by_atime);
        for (isize i = 0; i < kept && total > o->cap; i++) {
            isize before = s->n[CR_CAP];
            clean_drop(o, s, dirs, fds, &files[i], CR_CAP);
            if (s->n[CR_CAP] > before) total -= files[i].size;
        }
    }
    s->n[CR_KEEP] = kept - s->n[CR_CAP], s->bytes[CR_KEEP] = total;
    for (int i = 0; i < nd; i++) close(fds[i]);
}
