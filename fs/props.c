// What the Properties panel (F7.2, docs/plan.md) needs from the file system: the files, folders and
// bytes under some paths, counted all the way down, and a permission change.

#include "../base/base.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

enum { DU_DEPTH_MAX = 256, DU_TICK = 512, DU_INODES = 1 << 16, DU_MOUNTS_MAX = 32 };

typedef struct {
    i64  files, dirs, bytes;   // a link counts as a file, its own size; a folder's own size isn't added
    bool cut;                  // something couldn't be read, or was nested too deep: the numbers are less
    // Hard links (Michael, 2026-10-26: both totals shown when there are any): the names of files
    // with more than one, the files they are, and the bytes counting each file once (as du).
    i64  linked_names, linked_files, once_bytes;
    bool once_over;            // more linked files than DU_INODES: some counted twice, once_bytes more
} DuSum;

typedef struct { u64 dev, ino; } DuInode;   // a slot of the linked files seen, dev+1 so 0 is free

typedef struct {
    const atomic_uint *gen;    // the walk stops when this moves off mine
    u32                mine;
    void             (*tick)(void *ctx, const DuSum *s);   // the sums so far, every DU_TICK entries
    void              *ctx;
    DuInode           *inodes; // DU_INODES slots, cleared by du_walk; NULL: links aren't told apart
    dev_t              dev;    // the drive the walk started on: it doesn't go onto others
    isize              seen;
    Str                mounts; // the mount list (mountinfo's text; empty: none known)
    // The mount points below the roots, from it (du_walk), and the folder being read (du_dir): one
    // of them is counted as a folder without a stat, which on a dead network share would hang the
    // walk (the F16 code-health review: the stat came before the drive check).
    char               below[DU_MOUNTS_MAX][PATH_MAX];
    isize              nbelow;
    char               path[PATH_MAX];
} DuWalk;

// Whether w->path + "/" + name is a mount point below the roots.
static bool du_mount_point(const DuWalk *w, const char *name)
{
    size_t n = strlen(w->path);
    for (isize i = 0; i < w->nbelow; i++) {
        const char *b = w->below[i];
        size_t base = n == 1 ? 0 : n;   // (the root "/": "/" + name)
        if (strncmp(b, w->path, base) == 0 && b[base] == '/' && strcmp(b + base + 1, name) == 0) return true;
    }
    return false;
}

static bool du_stopped(const DuWalk *w)
{
    return w->gen && atomic_load(w->gen) != w->mine;
}

// A file (not a folder) into the sums: its size, and once only when another name of it came before.
static void du_file(DuWalk *w, const struct stat *st, DuSum *s)
{
    s->files++, s->bytes += st->st_size, s->once_bytes += st->st_size;
    if (st->st_nlink < 2 || !w->inodes) return;
    s->linked_names++;
    u64 dev = (u64)st->st_dev + 1, ino = (u64)st->st_ino;
    u64 h = (dev * 0x9E3779B97F4A7C15ull) ^ (ino * 0xC2B2AE3D27D4EB4Full);
    for (isize i = 0; i < DU_INODES; i++) {
        DuInode *slot = &w->inodes[(h + (u64)i) & (DU_INODES - 1)];
        if (slot->dev == dev && slot->ino == ino) {
            s->once_bytes -= st->st_size;
            return;
        }
        if (slot->dev == 0) {
            slot->dev = dev, slot->ino = ino, s->linked_files++;
            return;
        }
    }
    s->linked_files++, s->once_over = true;
}

// Everything in the folder fd (closed here), depth first, never following a link nor leaving the
// drive. False when stopped.
static bool du_dir(DuWalk *w, int fd, DuSum *s, int depth)
{
    if (depth > DU_DEPTH_MAX) return close(fd), s->cut = true, true;
    DIR *d = fdopendir(fd);
    if (!d) return close(fd), s->cut = true, true;
    bool go = true;
    for (struct dirent *e; go && (e = readdir(d));) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (du_stopped(w)) {
            go = false;
            break;
        }
        if (w->nbelow && du_mount_point(w, e->d_name)) {   // another drive mounted here: counted, not looked at
            s->dirs++;
            continue;
        }
        struct stat st;
        if (fstatat(dirfd(d), e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            s->cut = true;
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            s->dirs++;
            if (st.st_dev != w->dev) continue;   // another drive mounted here: counted, not entered
            int sub = openat(dirfd(d), e->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            size_t n = strlen(w->path);
            if (sub < 0) s->cut = true;
            else if (w->nbelow && snprintf(w->path + n, sizeof w->path - n, "%s%s", n == 1 ? "" : "/", e->d_name) >= (int)(sizeof w->path - n))
                close(sub), w->path[n] = 0, s->cut = true;   // (too deep a path to compare: not entered)
            else go = du_dir(w, sub, s, depth + 1), w->path[n] = 0;
        } else {
            du_file(w, &st, s);
        }
        if (++w->seen % DU_TICK == 0 && w->tick) w->tick(w->ctx, s);
    }
    closedir(d);
    return go;
}

// The roots and everything under them into s (zeroed first): a root folder counts as one folder,
// a root file or link as one file. False when stopped (s holds what was counted).
static bool du_walk(DuWalk *w, const char *const *roots, isize n, DuSum *s)
{
    *s = (DuSum){ 0 };
    if (w->inodes) memset(w->inodes, 0, DU_INODES * sizeof *w->inodes);
    for (isize i = 0; i < n; i++) {
        if (du_stopped(w)) return false;
        struct stat st;
        if (lstat(roots[i], &st) != 0) {
            s->cut = true;
            continue;
        }
        if (!S_ISDIR(st.st_mode)) {
            du_file(w, &st, s);
            continue;
        }
        s->dirs++;
        w->dev = st.st_dev;
        w->nbelow = 0;
        isize at = 0;
        for (Str line; w->nbelow < DU_MOUNTS_MAX && next_line(w->mounts, &at, &line);) {
            char mp[PATH_MAX];
            Str type, src;
            if (mountinfo_line(line, mp, sizeof mp, &type, &src) && path_within(mp, roots[i]) && strcmp(mp, roots[i]) != 0)
                snprintf(w->below[w->nbelow++], PATH_MAX, "%s", mp);
        }
        snprintf(w->path, sizeof w->path, "%s", roots[i]);
        int fd = open(roots[i], O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) s->cut = true;
        else if (!du_dir(w, fd, s, 1)) return false;
    }
    if (w->tick) w->tick(w->ctx, s);
    return true;
}

// The permission bit of the grid's place k (0..8: owner read, write, execute; group's; others').
static mode_t perm_bit(int k)
{
    return (mode_t)(0400 >> k);
}

// path's permissions (a link's target's, as chmod has it) to perms (the 0777 part), its special
// bits kept; *old gets what they were (07777). The guard checks the real path: a link inside the
// test folder must not reach a file outside it.
static bool op_chmod(const char *path, mode_t perms, mode_t *old, Err *err)
{
    char real[PATH_MAX];
    if (!realpath(path, real)) return err_set(err, "%s: %s", path, strerror(errno)), false;
    if (!ops_allowed(real, err)) return false;
    struct stat st;
    if (stat(real, &st) != 0) return err_set(err, "%s: %s", path, strerror(errno)), false;
    *old = st.st_mode & 07777;
    if (chmod(real, (st.st_mode & 07000) | (perms & 0777)) != 0) return err_set(err, "%s: %s", path, strerror(errno)), false;
    return true;
}
