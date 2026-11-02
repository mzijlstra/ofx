// Copy, move and delete for good, as a job: gui/ runs job_run on a worker thread and reads the
// progress; the tests run it in place (docs/plan.md, F3 "Copy and move", "Name clashes").
//
// A file is copied into ".ofx-part-<name>" beside its target and renamed into place at the end, a
// new folder the same way as a whole, so a cancel or a crash never leaves a half copy under the
// real name. Modes and times are kept; a symlink is copied as a link, never followed.

#include "../base/base.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef enum { JOB_COPY, JOB_MOVE, JOB_DELETE } JobKind;
typedef enum { JP_SCAN, JP_RUN, JP_ASK, JP_DONE } JobPhase;
typedef enum { ANS_NONE, ANS_SKIP, ANS_REPLACE, ANS_KEEP, ANS_CANCEL } Answer;

// What became of one item, for the status line and for undo.
typedef enum { OUT_NONE, OUT_NEW, OUT_MERGED, OUT_REPLACED, OUT_SKIPPED, OUT_FAILED } Outcome;

typedef struct {
    const char *src;     // absolute
    const char *dst;     // absolute, the target's own path (not its folder); NULL for a delete
    Outcome     out;
    char        final[NAME_MAX + 1];   // the name it got (another after "keep both")
    bool        after_ok;   // a delete: only when the item before it went (a .trashinfo after its item)
} JobItem;

typedef struct Job Job;
struct Job {
    JobKind  kind;
    JobItem *item;
    isize    n;

    // Written by the worker, read by anyone.
    atomic_llong bytes_done, bytes_total;
    atomic_llong files_done, files_total;
    atomic_int   phase;
    atomic_bool  cancel;

    // A clash: ask says what to do with dst (both_dirs: a folder onto a folder, where replace
    // merges); *rest set means the same for every clash of that kind after it. No ask: skip.
    Answer (*ask)(Job *j, const char *dst, bool both_dirs, bool *rest);
    void (*tick)(Job *j);   // the progress moved (often: throttle it)
    void  *ctx;
    Answer rest_file, rest_dir;

    int delay_ms;   // OFX_TEST_COPY_DELAY_MS: a sleep per block (and per item deleted), so a test can see the progress

    // The result.
    isize errors, skipped;
    char  err[512];     // the first error
    char  note[512];    // the first skip that wasn't asked for (a socket, a type mismatch)

    char    where[PATH_MAX];   // the target being written, for messages and clashes
    Outcome top;               // what the item's own entry (depth 0) became, and its name
    char    top_name[NAME_MAX + 1];
    u8   *buf;               // JOB_BUF bytes, for read/write when copy_file_range can't
};

enum { JOB_BUF = MiB(1) };

// For the tests: every move crosses drives, as a unit test can't mount one.
static bool job_test_cross;
// For the tests: read/write instead of copy_file_range.
static bool job_test_plain;

static void job_tick(Job *j)
{
    if (j->tick) j->tick(j);
}

static void job_error(Job *j, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void job_error(Job *j, const char *fmt, ...)
{
    j->errors++;
    if (j->err[0]) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(j->err, sizeof j->err, fmt, ap);
    va_end(ap);
}

static void job_note(Job *j, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void job_note(Job *j, const char *fmt, ...)
{
    j->skipped++;
    if (j->note[0]) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(j->note, sizeof j->note, fmt, ap);
    va_end(ap);
}

static bool job_cancelled(const Job *j)
{
    return atomic_load(&j->cancel);
}

// where += "/name"; the length before, for job_pop.
static size_t job_push(Job *j, const char *name)
{
    size_t n = strlen(j->where);
    snprintf(j->where + n, sizeof j->where - n, "/%s", name);
    return n;
}

static void job_pop(Job *j, size_t n)
{
    j->where[n] = 0;
}

static Answer job_clash(Job *j, bool src_dir, bool dst_dir)
{
    bool both = src_dir && dst_dir;
    Answer *rest = both ? &j->rest_dir : &j->rest_file;
    if (*rest) return *rest;
    if (!j->ask) return ANS_SKIP;
    bool all = false;
    atomic_store(&j->phase, JP_ASK);
    Answer a = j->ask(j, j->where, both, &all);
    atomic_store(&j->phase, JP_RUN);
    if (a == ANS_CANCEL) atomic_store(&j->cancel, true);
    else if (all) *rest = a;
    return a;
}

// The part name for name: ".ofx-part-<name>", or a numbered one when that would be too long.
static void part_name(char *out, size_t cap, const char *name)
{
    static atomic_int seq;
    if (strlen(name) + 9 <= NAME_MAX) snprintf(out, cap, ".ofx-part-%s", name);
    else snprintf(out, cap, ".ofx-part-%d-%d", (int)getpid(), atomic_fetch_add(&seq, 1));
}

// ---- counting -----------------------------------------------------------------------------------

// Files (anything not a folder) and their bytes under dirfd/name, into the totals.
static void job_count(Job *j, int dirfd, const char *name, int depth)
{
    struct stat st;
    if (job_cancelled(j) || fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) return;
    if (!S_ISDIR(st.st_mode)) {
        atomic_fetch_add(&j->files_total, 1);
        if (S_ISREG(st.st_mode)) atomic_fetch_add(&j->bytes_total, (long long)st.st_size);
        return;
    }
    if (depth > TREE_DEPTH_MAX) return;
    int fd = openat(dirfd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    DIR *d = fd >= 0 ? fdopendir(fd) : NULL;
    if (!d) {
        if (fd >= 0) close(fd);
        return;
    }
    for (struct dirent *de; (de = readdir(d));)
        if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) job_count(j, fd, de->d_name, depth + 1);
    closedir(d);
}

// ---- copying --------------------------------------------------------------------------------------

// The bytes of sfd into dfd: copy_file_range in blocks, read/write when the kernel can't.
static bool copy_bytes(Job *j, int sfd, int dfd)
{
    bool plain = job_test_plain;
    for (;;) {
        if (job_cancelled(j)) return false;
        ssize_t k;
        if (!plain) {
            k = copy_file_range(sfd, NULL, dfd, NULL, (size_t)JOB_BUF, 0);
            if (k < 0 && (errno == EXDEV || errno == EINVAL || errno == ENOSYS || errno == EOPNOTSUPP || errno == EPERM)) {
                plain = true;
                continue;
            }
        } else {
            k = read(sfd, j->buf, (size_t)JOB_BUF);
            for (ssize_t w = 0; k > 0 && w < k;) {
                ssize_t m = write(dfd, j->buf + w, (size_t)(k - w));
                if (m < 0) {
                    if (errno == EINTR) continue;
                    k = -1;
                    break;
                }
                w += m;
            }
        }
        if (k < 0) {
            if (errno == EINTR) continue;
            job_error(j, "%s: %s", j->where, strerror(errno));
            return false;
        }
        if (k == 0) return true;
        atomic_fetch_add(&j->bytes_done, (long long)k);
        if (j->delay_ms > 0) nanosleep(&(struct timespec){ j->delay_ms / 1000, (j->delay_ms % 1000) * 1000000L }, NULL);
        job_tick(j);
    }
}

// Removes ddir/name, a part file or a part folder, after a cancel or a failure.
static void part_remove(int ddir, const char *name)
{
    struct stat st;
    if (fstatat(ddir, name, &st, AT_SYMLINK_NOFOLLOW) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        int fd = openat(ddir, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (fd >= 0) {
            isize n = 0;
            Err e;
            rm_children(fd, name, 0, &n, &e);
            close(fd);
        }
        unlinkat(ddir, name, AT_REMOVEDIR);
    } else {
        unlinkat(ddir, name, 0);
    }
}

static bool copy_into(Job *j, int sdir, const char *sname, int ddir, const char *dname, bool fresh, int depth);

// The children of folder sfd into folder dfd. fresh: dfd is new (nothing there can clash).
static bool copy_children(Job *j, int sfd, int dfd, bool fresh, int depth)
{
    int fd = dup(sfd);
    DIR *d = fd >= 0 ? fdopendir(fd) : NULL;
    if (!d) {
        if (fd >= 0) close(fd);
        job_error(j, "%s: %s", j->where, strerror(errno));
        return false;
    }
    bool ok = true;
    for (struct dirent *de; ok && !job_cancelled(j) && (de = readdir(d));) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        size_t back = job_push(j, de->d_name);
        ok = copy_into(j, sfd, de->d_name, dfd, de->d_name, fresh, depth + 1);
        job_pop(j, back);
    }
    closedir(d);
    return ok && !job_cancelled(j);
}

// One entry, sdir/sname to ddir/dname, the target new (`fresh`, written directly: it is inside a
// part folder) or possibly there (a clash is asked). False on an error or a cancel.
static bool copy_into(Job *j, int sdir, const char *sname, int ddir, const char *dname, bool fresh, int depth)
{
    if (job_cancelled(j)) return false;
    if (depth > TREE_DEPTH_MAX) return job_error(j, "%s: folders nested too deep", j->where), false;
    struct stat st, dt;
    if (fstatat(sdir, sname, &st, AT_SYMLINK_NOFOLLOW) != 0) return job_error(j, "%s: %s", sname, strerror(errno)), false;
    bool replace = false, merge = false;
    char keep[NAME_MAX + 1];
    if (!fresh && fstatat(ddir, dname, &dt, AT_SYMLINK_NOFOLLOW) == 0) {
        Answer a = job_clash(j, S_ISDIR(st.st_mode), S_ISDIR(dt.st_mode));
        if (a == ANS_CANCEL) return false;
        if (a == ANS_SKIP) {
            job_note(j, "%s: skipped", j->where);
            if (!S_ISDIR(st.st_mode)) atomic_fetch_add(&j->files_done, 1);
            if (depth == 0) j->top = OUT_SKIPPED;
            return true;
        }
        if (a == ANS_KEEP) {
            // The folder of where, for name_free.
            char pd[PATH_MAX];
            if (!path_parent(pd, sizeof pd, j->where) || !name_free(pd, str_from_c(dname), S_ISDIR(st.st_mode), false, true, keep, sizeof keep))
                return job_error(j, "%s: no free name", j->where), false;
            dname = keep;
        } else if (S_ISDIR(st.st_mode) != S_ISDIR(dt.st_mode)) {
            job_note(j, "%s: can't replace a %s with a %s", j->where, S_ISDIR(dt.st_mode) ? "folder" : "file",
                     S_ISDIR(st.st_mode) ? "folder" : "file");
            if (depth == 0) j->top = OUT_SKIPPED;
            return true;
        } else if (S_ISDIR(st.st_mode)) {
            merge = true;
        } else {
            replace = true;
        }
    }
    if (depth == 0) j->top = merge ? OUT_MERGED : replace ? OUT_REPLACED : OUT_NEW, snprintf(j->top_name, sizeof j->top_name, "%s", dname);

    if (S_ISDIR(st.st_mode)) {
        int sfd = openat(sdir, sname, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (sfd < 0) return job_error(j, "%s: %s", j->where, strerror(errno)), false;
        if (merge) {
            int dfd = openat(ddir, dname, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
            if (dfd < 0) {
                close(sfd);
                return job_error(j, "%s: %s", j->where, strerror(errno)), false;
            }
            bool ok = copy_children(j, sfd, dfd, false, depth);
            close(dfd), close(sfd);
            return ok;
        }
        char part[NAME_MAX + 1];
        if (fresh) snprintf(part, sizeof part, "%s", dname);
        else part_name(part, sizeof part, dname);
        if (mkdirat(ddir, part, 0700) != 0) {
            close(sfd);
            return job_error(j, "%s: %s", j->where, strerror(errno)), false;
        }
        int dfd = openat(ddir, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        bool ok = dfd >= 0 && copy_children(j, sfd, dfd, true, depth);
        if (dfd < 0) job_error(j, "%s: %s", j->where, strerror(errno));
        if (ok) {
            fchmod(dfd, st.st_mode & 07777);
            futimens(dfd, (struct timespec[2]){ st.st_atim, st.st_mtim });
        }
        if (dfd >= 0) close(dfd);
        close(sfd);
        if (ok && !fresh && renameat2(ddir, part, ddir, dname, RENAME_NOREPLACE) != 0) {
            job_error(j, "%s: %s", j->where, strerror(errno));
            ok = false;
        }
        if (!ok && !fresh) part_remove(ddir, part);
        return ok;
    }

    char part[NAME_MAX + 1];
    if (fresh) snprintf(part, sizeof part, "%s", dname);
    else part_name(part, sizeof part, dname);
    bool ok = true;
    if (S_ISLNK(st.st_mode)) {
        char target[PATH_MAX];
        ssize_t k = readlinkat(sdir, sname, target, sizeof target - 1);
        if (k < 0 || symlinkat((target[k < 0 ? 0 : k] = 0, target), ddir, part) != 0) ok = false;
        if (ok) utimensat(ddir, part, (struct timespec[2]){ st.st_atim, st.st_mtim }, AT_SYMLINK_NOFOLLOW);
        if (!ok) job_error(j, "%s: %s", j->where, strerror(errno));
    } else if (S_ISREG(st.st_mode)) {
        int sfd = openat(sdir, sname, O_RDONLY | O_NOFOLLOW);
        int dfd = sfd < 0 ? -1 : openat(ddir, part, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (sfd < 0 || dfd < 0) {
            job_error(j, "%s: %s", j->where, strerror(errno));
            ok = false;
        } else {
            ok = copy_bytes(j, sfd, dfd);
            if (ok) {
                fchmod(dfd, st.st_mode & 07777);
                futimens(dfd, (struct timespec[2]){ st.st_atim, st.st_mtim });
            }
        }
        if (sfd >= 0) close(sfd);
        if (dfd >= 0 && close(dfd) != 0 && ok) {
            job_error(j, "%s: %s", j->where, strerror(errno));
            ok = false;
        }
        if (!ok && dfd >= 0) unlinkat(ddir, part, 0);
    } else {
        job_note(j, "%s: not a file, folder or link: skipped", j->where);
        atomic_fetch_add(&j->files_done, 1);
        return true;
    }
    if (ok && !fresh) {
        // Over the old one only when asked to replace it; it survives until the new one is whole.
        int r = replace ? renameat(ddir, part, ddir, dname) : renameat2(ddir, part, ddir, dname, RENAME_NOREPLACE);
        if (r != 0) {
            job_error(j, "%s: %s", j->where, strerror(errno));
            unlinkat(ddir, part, 0);
            ok = false;
        }
    }
    if (ok) atomic_fetch_add(&j->files_done, 1);
    job_tick(j);
    return ok;
}

// ---- moving -----------------------------------------------------------------------------------

static bool move_into(Job *j, int sdir, const char *sname, int ddir, const char *dname, int depth);

// A folder onto a folder on the same drive: each child moved in, then the source removed if empty.
static bool move_merge(Job *j, int sdir, const char *sname, int ddir, const char *dname, int depth)
{
    int sfd = openat(sdir, sname, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    int dfd = sfd < 0 ? -1 : openat(ddir, dname, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (sfd < 0 || dfd < 0) {
        job_error(j, "%s: %s", j->where, strerror(errno));
        if (sfd >= 0) close(sfd);
        return false;
    }
    int fd = dup(sfd);
    DIR *d = fd >= 0 ? fdopendir(fd) : NULL;
    bool ok = d != NULL;
    // Moving entries out while readdir runs: passes until one moves nothing.
    for (isize moved = 1; ok && moved > 0 && !job_cancelled(j);) {
        moved = 0;
        rewinddir(d);
        for (struct dirent *de; ok && !job_cancelled(j) && (de = readdir(d));) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            struct stat before;
            if (fstatat(sfd, de->d_name, &before, AT_SYMLINK_NOFOLLOW) != 0) continue;
            char nm[NAME_MAX + 1];
            snprintf(nm, sizeof nm, "%s", de->d_name);
            size_t back = job_push(j, nm);
            isize skipped = j->skipped;
            ok = move_into(j, sfd, nm, dfd, nm, depth + 1);
            job_pop(j, back);
            if (ok && j->skipped == skipped) moved++;
        }
    }
    if (d) closedir(d);
    else if (fd >= 0) close(fd);
    close(sfd), close(dfd);
    if (ok) unlinkat(sdir, sname, AT_REMOVEDIR);   // fails, harmlessly, when something was skipped
    return ok && !job_cancelled(j);
}

// One entry moved on the same drive: a rename, unless a clash says otherwise.
static bool move_into(Job *j, int sdir, const char *sname, int ddir, const char *dname, int depth)
{
    if (job_cancelled(j)) return false;
    if (renameat2(sdir, sname, ddir, dname, RENAME_NOREPLACE) == 0) {
        atomic_fetch_add(&j->files_done, 1);
        if (depth == 0) j->top = OUT_NEW, snprintf(j->top_name, sizeof j->top_name, "%s", dname);
        job_tick(j);
        return true;
    }
    if (errno != EEXIST) return job_error(j, "%s: %s", j->where, strerror(errno)), false;
    struct stat st, dt;
    if (fstatat(sdir, sname, &st, AT_SYMLINK_NOFOLLOW) != 0 || fstatat(ddir, dname, &dt, AT_SYMLINK_NOFOLLOW) != 0)
        return job_error(j, "%s: %s", j->where, strerror(errno)), false;
    Answer a = job_clash(j, S_ISDIR(st.st_mode), S_ISDIR(dt.st_mode));
    if (a == ANS_CANCEL) return false;
    if (a == ANS_SKIP) {
        if (depth == 0) j->top = OUT_SKIPPED;
        return job_note(j, "%s: skipped", j->where), true;
    }
    if (a == ANS_KEEP) {
        char pd[PATH_MAX], keep[NAME_MAX + 1];
        if (!path_parent(pd, sizeof pd, j->where) || !name_free(pd, str_from_c(dname), S_ISDIR(st.st_mode), false, true, keep, sizeof keep))
            return job_error(j, "%s: no free name", j->where), false;
        if (renameat2(sdir, sname, ddir, keep, RENAME_NOREPLACE) != 0) return job_error(j, "%s: %s", j->where, strerror(errno)), false;
        atomic_fetch_add(&j->files_done, 1);
        if (depth == 0) j->top = OUT_NEW, snprintf(j->top_name, sizeof j->top_name, "%s", keep);
        return true;
    }
    if (S_ISDIR(st.st_mode) != S_ISDIR(dt.st_mode)) {
        job_note(j, "%s: can't replace a %s with a %s", j->where, S_ISDIR(dt.st_mode) ? "folder" : "file", S_ISDIR(st.st_mode) ? "folder" : "file");
        if (depth == 0) j->top = OUT_SKIPPED;
        return true;
    }
    if (depth == 0) j->top = S_ISDIR(st.st_mode) ? OUT_MERGED : OUT_REPLACED, snprintf(j->top_name, sizeof j->top_name, "%s", dname);
    if (S_ISDIR(st.st_mode)) return move_merge(j, sdir, sname, ddir, dname, depth);
    if (renameat(sdir, sname, ddir, dname) != 0) return job_error(j, "%s: %s", j->where, strerror(errno)), false;
    atomic_fetch_add(&j->files_done, 1);
    return true;
}

// ---- the job ----------------------------------------------------------------------------------

// A folder pasted into itself, or below itself: src is dst's folder or above it.
static bool into_itself(const char *src, const char *dst_dir)
{
    char s[PATH_MAX], d[PATH_MAX];
    if (!realpath(src, s) || !realpath(dst_dir, d)) return false;
    return path_within(d, s);
}

// The parent folder of path opened, and its last part into name.
static int open_parent(const char *path, char *name, size_t cap)
{
    char d[PATH_MAX];
    if (!path_parent(d, sizeof d, path)) return errno = ENAMETOOLONG, -1;
    snprintf(name, cap, "%.*s", STR_ARG(path_base(str_from_c(path))));
    return open(d, O_RDONLY | O_DIRECTORY);
}

// One item of a copy or a move.
static void job_item(Job *j, JobItem *it)
{
    Err err;
    if (!ops_allowed(it->dst, &err) || (j->kind == JOB_MOVE && !ops_allowed(it->src, &err))) {
        job_error(j, "%s", err.msg);
        it->out = OUT_FAILED;
        return;
    }
    char sname[NAME_MAX + 1], dname[NAME_MAX + 1];
    int sdir = open_parent(it->src, sname, sizeof sname), ddir = open_parent(it->dst, dname, sizeof dname);
    if (sdir < 0 || ddir < 0) {
        job_error(j, "%s: %s", sdir < 0 ? it->src : it->dst, strerror(errno));
        it->out = OUT_FAILED;
        if (sdir >= 0) close(sdir);
        if (ddir >= 0) close(ddir);
        return;
    }
    snprintf(j->where, sizeof j->where, "%s", it->dst);
    struct stat st, ds;
    bool same_drive = !job_test_cross && fstat(sdir, &st) == 0 && fstat(ddir, &ds) == 0 && st.st_dev == ds.st_dev;
    isize errors = j->errors, skipped = j->skipped;
    j->top = OUT_NONE, j->top_name[0] = 0;
    bool ok;
    if (j->kind == JOB_MOVE && same_drive) ok = move_into(j, sdir, sname, ddir, dname, 0);
    else ok = copy_into(j, sdir, sname, ddir, dname, false, 0);
    bool whole = ok && j->errors == errors && j->skipped == skipped && !job_cancelled(j);
    it->out = !ok ? (job_cancelled(j) ? OUT_NONE : OUT_FAILED) : j->top;
    snprintf(it->final, sizeof it->final, "%s", j->top_name);
    // A move that left part of itself behind (a skip inside, an error) can't simply go back.
    if (j->kind == JOB_MOVE && it->out == OUT_NEW && !whole) it->out = OUT_MERGED;
    // A cross-drive move: the source goes once all of it arrived.
    if (whole && j->kind == JOB_MOVE && !same_drive) {
        isize n = 0;
        if (!delete_tree(it->src, &n, &err)) job_error(j, "%s", err.msg), it->out = OUT_MERGED;
    }
    close(sdir), close(ddir);
}

// Runs the whole job, here; the caller's thread, or gui/'s worker.
static void job_run(Job *j)
{
    atomic_store(&j->phase, JP_SCAN);
    bool scan = j->kind != JOB_MOVE;
    for (isize i = 0; i < j->n; i++) {
        char name[NAME_MAX + 1];
        int pd = open_parent(j->item[i].src, name, sizeof name);
        if (pd < 0) continue;
        struct stat a, b;
        bool cross = false;
        if (j->kind == JOB_MOVE && j->item[i].dst) {
            char dn[NAME_MAX + 1];
            int dd = open_parent(j->item[i].dst, dn, sizeof dn);
            cross = dd >= 0 && (job_test_cross || (fstat(pd, &a) == 0 && fstat(dd, &b) == 0 && a.st_dev != b.st_dev));
            if (dd >= 0) close(dd);
        }
        if (scan || cross) job_count(j, pd, name, 0);
        else atomic_fetch_add(&j->files_total, 1);
        close(pd);
    }
    atomic_store(&j->phase, JP_RUN);
    job_tick(j);
    for (isize i = 0; i < j->n && !job_cancelled(j); i++) {
        JobItem *it = &j->item[i];
        if (j->kind == JOB_DELETE) {
            Err err;
            isize n = 0;
            // The item before it is still there: so is this (its .trashinfo, which the trash view
            // lists it by; reviewer-2, F6 review).
            if (it->after_ok && (i == 0 || j->item[i - 1].out != OUT_NEW)) {
                it->out = OUT_SKIPPED;
                continue;
            }
            if (j->delay_ms > 0) nanosleep(&(struct timespec){ j->delay_ms / 1000, (j->delay_ms % 1000) * 1000000L }, NULL);
            if (delete_tree(it->src, &n, &err)) it->out = OUT_NEW;
            else job_error(j, "%s", err.msg), it->out = OUT_FAILED;
            atomic_fetch_add(&j->files_done, n);
            job_tick(j);
            continue;
        }
        job_item(j, it);
    }
    atomic_store(&j->phase, JP_DONE);
    job_tick(j);
}
