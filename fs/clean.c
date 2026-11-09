// The thumbnail cache's cleanup (F6.5, docs/plan.md "F6.5 design"): ofx-clean runs it daily from a
// systemd user timer. Nothing else cleans ~/.cache/thumbnails here (Nemo doesn't), and on
// 2026-10-25 258 MiB of its 362 were previews of files that are gone.
//
// ofx-clean --all (View → Clear Previews…, F12) deletes every file of those names, and the run
// folders, whatever their age: the rules aside.
//
// ofx's own leftovers go after a day: a temporary file (thumb_write's), a thumbnailer's run folder
// in the root (thumb_exec's).
//
// Three rules, in order (ofx.conf's clean options, F10, set or turn off each): a preview not read
// for clean-unused-days goes (its atime); a preview whose file is gone or has changed goes (its
// Thumb::URI and Thumb::MTime); then, while the cache is over clean-cap, the least recently read go. Only the cache's own names are looked at, in its size
// folders and fail/<program>/, never followed through a link, and each delete asks the test guard
// (guard.c) first.
//
// A file's path is judged where its links lead: a link onto a drive that is out keeps its previews.
//
// Every preview is opened with O_NOATIME: a plain read would mark it as just used, and the first
// rule would never fire for anything the second one checks.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define CLEAN_TMP_S    86400LL   // ofx's own temporary file, left by a crash, after a day

typedef enum { CR_KEEP, CR_TEMP, CR_UNUSED, CR_ORPHAN, CR_STALE, CR_CAP, CR_ALL, CR_N } CleanRule;

static const char *const clean_rule_names[CR_N] = { "kept", "temp", "unused", "orphan", "stale", "cap", "all" };

typedef struct {
    i64  now;       // seconds
    i64  unused_s;  // the first rule's age; 0: off
    bool orphans;   // the second rule
    i64  cap;       // bytes; 0: no cap
    bool dry;    // list, delete nothing
    bool all;    // every file of the cache's (ofx-clean --all, View → Clear Previews…, F12): the rules aside
    void (*say)(void *ctx, CleanRule r, const char *path, i64 size);   // each delete (or would-be one)
    void *ctx;
} CleanOpts;

// ofx.conf's clean options into o's rules (clean itself is ofx-clean's to check).
static void clean_opts_conf(const Conf *c, CleanOpts *o)
{
    o->unused_s = c->clean_unused_days * 86400, o->orphans = c->clean_orphans, o->cap = c->clean_cap;
}

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

// ofx's thumbnailer run folder (thumb_exec, F12): .ofx-run-<pid>-<n>.
static bool clean_run_name(const char *n)
{
    if (strncmp(n, ".ofx-run-", 9) != 0) return false;
    const char *p = n + 9;
    for (int part = 0; part < 2; part++) {
        const char *d = p;
        while (*p >= '0' && *p <= '9') p++;
        if (p == d || p - d > 10 || *p != (part ? 0 : '-')) return false;
        p++;
    }
    return true;
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

// The mount list, parsed once a run: clean_real looks up every prefix of every preview's path, and
// reading mountinfo's text for each (about 66,000 times on Michael's cache) made a run 4-5 times
// slower (review, 2026-10-25). far is thumb_local's verdict on the mount point itself: the same
// mount wins for any path below it, so it is the verdict for those paths too.
typedef struct {
    char  *mp;
    size_t n;
    bool   far;
} CleanMount;

typedef struct {
    CleanMount *m;
    isize       n;
    bool        unknown;   // the list didn't fit its arena: no path is judged (clean_real refuses)
} CleanMounts;

static CleanMounts clean_mounts(Arena *a, Str mountinfo)
{
    isize at = 0, lines = 0;
    for (Str line; next_line(mountinfo, &at, &line);) lines++;
    CleanMounts ms = { arena_try_new(a, CleanMount, lines + 1), 0, false };
    if (!ms.m) ms.unknown = true;
    at = 0;
    // (try, never abort: thousands of mounts give the caller's arena no room, and then none are known,
    // so clean_real keeps every file; F11 review)
    for (Str line; ms.m && next_line(mountinfo, &at, &line);) {
        char mp[PATH_MAX];
        Str type, src;
        if (!mountinfo_line(line, mp, sizeof mp, &type, &src)) continue;
        size_t n = strlen(mp);
        char *p = arena_try_push(a, (isize)n + 1, 1);
        if (!p) return (CleanMounts){ NULL, 0, true };
        memcpy(p, mp, n + 1);
        // (its own type: clean_local takes the last mount on a point, as thumb_local does; one with
        // no type, thumb_local's rule, which skips it. Once a mount, not once a mount per mount: the
        // F15 code-health review, about 4 million line reads for 2,000 mounts)
        ms.m[ms.n++] = (CleanMount){ p, n, type.len ? thumb_far_type(type) : !thumb_local(mountinfo, mp) };
    }
    return ms;
}

// thumb_local on the parsed list: the mount with the longest mount point above path, a later one
// on the same point winning.
static bool clean_local(const CleanMounts *ms, const char *path)
{
    isize best = -1;
    bool far = false;
    for (isize i = 0; i < ms->n; i++) {
        const CleanMount *m = &ms->m[i];
        if (path_within(path, m->mp) && (isize)m->n >= best) best = (isize)m->n, far = m->far;
    }
    return !far;
}

// Whether the mount list has dir as a mount point.
static bool clean_mounted(const CleanMounts *ms, const char *dir)
{
    for (isize i = 0; i < ms->n; i++)
        if (strcmp(ms->m[i].mp, dir) == 0) return true;
    return false;
}

// Whether path is on a drive that isn't mounted now: under /run/media/<user>/<name>, /media/<name>
// or /mnt/<name> when that folder isn't a mount point. Its previews wait for the drive.
static bool clean_absent_drive(const CleanMounts *ms, const char *path)
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
        return !clean_mounted(ms, top);
    }
    return false;
}

// path with its links followed as far as they go, into out: a link that leads nowhere (its drive
// away) still names where it leads. Michael's ~/Documents and the like are links onto /mnt/sd, a
// card that may be out (review, 2026-10-25), so the drive rules judge where a file really is.
// False past 40 links, past PATH_MAX, or on reaching a network or FUSE mount, where a readlink could
// hang (clean_local): the caller then keeps the preview.
static bool clean_real(const CleanMounts *ms, const char *path, char *out, size_t cap)
{
    if (ms->unknown) return false;   // (no mount list: never judged, kept)
    char rest[PATH_MAX], t[PATH_MAX];
    if (snprintf(rest, sizeof rest, "%s", path) >= (int)sizeof rest || cap < 2) return false;
    snprintf(out, cap, "/");
    for (int hops = 0; rest[0];) {
        char *p = rest;
        while (*p == '/') p++;
        size_t n = strcspn(p, "/");
        if (n == 0) break;
        char part[NAME_MAX + 1];
        if (n > NAME_MAX) return false;
        memcpy(part, p, n), part[n] = 0;
        memmove(rest, p + n, strlen(p + n) + 1);
        if (strcmp(part, ".") == 0) continue;
        if (strcmp(part, "..") == 0) {   // out is resolved already, so its parent is the real one
            char *s = strrchr(out, '/');
            s == out ? (void)(out[1] = 0) : (void)(*s = 0);
            continue;
        }
        size_t o = strlen(out);
        if (snprintf(out + o, cap - o, "%s%s", o > 1 ? "/" : "", part) >= (int)(cap - o)) return false;
        if (!clean_local(ms, out)) return false;
        ssize_t k = readlink(out, t, sizeof t - 1);
        if (k < 0) continue;   // not a link, or not there: kept as it is
        if (++hops > 40) return false;
        t[k] = 0;
        char *s = strrchr(out, '/');
        s == out ? (void)(out[1] = 0) : (void)(*s = 0);
        if (t[0] == '/') snprintf(out, cap, "/");
        if (snprintf(t + k, sizeof t - (size_t)k, "/%s", rest) >= (int)(sizeof t - (size_t)k)) return false;
        memcpy(rest, t, strlen(t) + 1);
    }
    return true;
}

// What the second rule makes of one preview, read from dfd/name: CR_ORPHAN, CR_STALE or CR_KEEP.
static CleanRule clean_judge(Arena *a, const CleanMounts *ms, int dfd, const char *name)
{
    ArenaMark mark = arena_mark(a);
    CleanRule r = CR_KEEP;
    struct stat st;
    ThumbMeta *m = arena_try_new(a, ThumbMeta, 1);
    // (O_NOATIME: one not ours to read without touching its atime isn't read: left to the others)
    Str png = m ? file_read_at(a, dfd, name, THUMB_CACHED_MAX, &st, O_NOATIME | O_NOFOLLOW) : STR_EMPTY;
    if (png.len) {
        char path[PATH_MAX], real[PATH_MAX];
        struct stat f;
        if (png_meta(png, m) && m->has_uri && file_uri_path(str_from_c(m->uri), path, sizeof path) &&
            clean_real(ms, path, real, sizeof real) && !clean_absent_drive(ms, real)) {
            // stat, not lstat: a link's preview carries its target's MTime and Size (kind_of follows
            // it), so the link itself would be stale on every run (review, 2026-10-25).
            if (stat(path, &f) != 0) r = errno == ENOENT || errno == ENOTDIR ? CR_ORPHAN : CR_KEEP;
            else if ((m->has_mtime && m->mtime != f.st_mtim.tv_sec) || (m->has_size && m->size != f.st_size)) r = CR_STALE;
        }
    }
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
    if (!path_put(path, sizeof path, dirs[f->dir], str_from_c(f->name))) {   // (cut, the guard would judge another path than the one deleted)
        s->failed++;
        return;
    }
    if (!o->dry && (!ops_allowed(path, &err) || (unlinkat(fds[f->dir], f->name, 0) != 0 && errno != ENOENT))) {   // (gone already: another clean, the timer's or a window's)
        s->failed++;
        return;
    }
    s->n[r]++, s->bytes[r] += f->size;
    if (o->say) o->say(o->ctx, r, path, f->size);
}

#define CLEAN_DIRS_MAX 64

// The thumbnailer run folders a crash left in the cache's root (rfd), a day after their last
// change: what is in them (no link followed; a folder in one stays, and so does that one), then the
// folder, each counted as a temporary file (F12 step 3 review).
static void clean_runs(int rfd, const char *root, const CleanOpts *o, CleanStats *s)
{
    int dfd = fcntl(rfd, F_DUPFD_CLOEXEC, 0);
    DIR *d = dfd >= 0 ? fdopendir(dfd) : NULL;
    if (!d) {
        if (dfd >= 0) close(dfd);
        return;
    }
    for (struct dirent *e; (e = readdir(d));) {
        struct stat st;
        if (!clean_run_name(e->d_name) || fstatat(rfd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(st.st_mode) ||
            (!o->all && o->now - st.st_mtim.tv_sec <= CLEAN_TMP_S))
            continue;
        char path[PATH_MAX];
        Err err;
        isize gone;
        if (!path_put(path, sizeof path, root, str_from_c(e->d_name))) {   // (cut: another path than the one deleted)
            s->failed++;
            continue;
        }
        // (all of it, its own folders too, the guard asked: delete_tree; gone already is no failure)
        if (!o->dry && !delete_tree(path, &gone, &err) && access(path, F_OK) == 0) {
            s->failed++;
            continue;
        }
        s->n[CR_TEMP]++;
        if (o->say) o->say(o->ctx, CR_TEMP, path, 0);
    }
    closedir(d);
}

// One run over the cache at root (thumb_root's). The arena holds the list of files; mountinfo is
// the mount list (/proc/self/mountinfo's text).
static void clean_run(Arena *a, const char *root, Str mountinfo, const CleanOpts *o, CleanStats *s)
{
    *s = (CleanStats){ 0 };
    CleanMounts ms = clean_mounts(a, mountinfo);
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
    clean_runs(rfd, root, o, s);
    close(rfd);

    // Every file of the cache's names, by lstat only.
    CleanFile *files = NULL;   // pushed one by one, nothing else between: one array
    isize nf = 0;
    for (int i = 0; i < nd; i++) {
        int dup_fd = fcntl(fds[i], F_DUPFD_CLOEXEC, 0);
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
        CleanRule r = o->all ? CR_ALL
                    : f->temp ? (o->now - f->mtime > CLEAN_TMP_S ? CR_TEMP : CR_KEEP)
                    : o->unused_s > 0 && o->now - f->atime > o->unused_s ? CR_UNUSED
                    : o->orphans ? clean_judge(a, &ms, fds[f->dir], f->name) : CR_KEEP;
        if (r != CR_KEEP) {
            clean_drop(o, s, dirs, fds, f, r);
            continue;
        }
        files[kept++] = *f, total += f->size;
    }

    // The cap: the least recently read first.
    if (o->cap > 0 && total > o->cap) {
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
