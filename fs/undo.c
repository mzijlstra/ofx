// One level of undo: the last rename, new folder, new file (F7.1), trash, copy or move (docs/plan.md, F3 "Undo").
// Before it does anything it checks that things are as the action left them; if not, it does
// nothing and says why. Delete for good can't be undone.

#include "../base/base.h"

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum { UNDO_NONE, UNDO_RENAME, UNDO_MKDIR, UNDO_TRASH, UNDO_COPY, UNDO_MOVE, UNDO_MKFILE, UNDO_CHMOD } UndoKind;

// rename, move: from was moved to to; new folder, new file, copy: to was made; a permission change
// (F7.2): to's mode was info's first word (octal), the file then its device and inode (the rest),
// so the undo never reaches another file put at that path since (reviewer-2, F7.2 review); trash: from went to file,
// with its info.
typedef struct {
    char *from, *to, *info;
} UndoItem;

enum { UNDO_ITEMS_MAX = 4096 };

typedef struct {
    UndoKind kind;
    UndoItem it[UNDO_ITEMS_MAX];
    isize    n;
    Arena   *arena;   // the paths; reset with each new record
} Undo;

static void undo_begin(Undo *u, UndoKind kind)
{
    arena_reset(u->arena);
    u->kind = kind, u->n = 0;
}

static void undo_clear(Undo *u)
{
    u->kind = UNDO_NONE, u->n = 0;
}

// One more item; dropped (and the undo cut short to what it holds) when it is full.
static void undo_add(Undo *u, const char *from, const char *to, const char *info)
{
    if (u->n == UNDO_ITEMS_MAX) return;
    Arena *a = u->arena;
    char *f = from ? arena_try_push(a, (isize)strlen(from) + 1, 1) : NULL;
    char *t = to ? arena_try_push(a, (isize)strlen(to) + 1, 1) : NULL;
    char *i = info ? arena_try_push(a, (isize)strlen(info) + 1, 1) : NULL;
    if ((from && !f) || (to && !t) || (info && !i)) return;
    if (f) strcpy(f, from);
    if (t) strcpy(t, to);
    if (i) strcpy(i, info);
    u->it[u->n++] = (UndoItem){ f, t, i };
}

static bool present(const char *path)
{
    struct stat st;
    return path && lstat(path, &st) == 0;
}

// What the undo would do, in a few words ("rename", "trash of 3 items").
static void undo_what(const Undo *u, char *out, size_t cap)
{
    static const char *const NAMES[] = { "nothing", "rename", "new folder", "trash", "copy", "move", "new file", "permission change" };
    if (u->n == 1 || u->kind == UNDO_RENAME || u->kind == UNDO_MKDIR || u->kind == UNDO_MKFILE || u->kind == UNDO_CHMOD) snprintf(out, cap, "%s", NAMES[u->kind]);
    else snprintf(out, cap, "%s of %td items", NAMES[u->kind], u->n);
}

// Whether things are as the action left them: false with the reason when not.
static bool undo_check(const Undo *u, Err *err)
{
    if (u->kind == UNDO_NONE || u->n == 0) return err_set(err, "nothing to undo"), false;
    for (isize i = 0; i < u->n; i++) {
        const UndoItem *it = &u->it[i];
        if (!present(it->to)) return err_set(err, "%s is gone: can't undo", it->to), false;
        if (u->kind == UNDO_MKDIR) {
            struct stat st;
            if (lstat(it->to, &st) != 0 || !S_ISDIR(st.st_mode)) return err_set(err, "%s isn't a folder now: can't undo", it->to), false;
        }
        // A permission change goes back only on the file it was made on (a link's target).
        if (u->kind == UNDO_CHMOD) {
            struct stat st;
            unsigned long long dev = 0, ino = 0;
            unsigned mode;
            if (!it->info || sscanf(it->info, "%o %llu %llu", &mode, &dev, &ino) != 3 || stat(it->to, &st) != 0 ||
                (unsigned long long)st.st_dev != dev || (unsigned long long)st.st_ino != ino)
                return err_set(err, "%s is another file now: can't undo", it->to), false;
        }
        // A new file goes only while it is still empty: what was written into it since is kept.
        if (u->kind == UNDO_MKFILE) {
            struct stat st;
            if (lstat(it->to, &st) != 0 || !S_ISREG(st.st_mode)) return err_set(err, "%s isn't a file now: can't undo", it->to), false;
            if (st.st_size != 0) return err_set(err, "%s isn't empty now: can't undo", it->to), false;
        }
        if ((u->kind == UNDO_RENAME || u->kind == UNDO_TRASH || u->kind == UNDO_MOVE) && present(it->from))
            return err_set(err, "%s is there again: can't undo", it->from), false;
    }
    return true;
}

// Whether undoing this move crosses drives, so gui/ must run it as a move job (reversed).
static bool undo_is_job(const Undo *u)
{
    if (u->kind != UNDO_MOVE) return false;
    if (job_test_cross) return true;
    for (isize i = 0; i < u->n; i++) {
        dev_t a, b;
        Str d = path_dir(str_from_c(u->it[i].from));
        char from_dir[PATH_MAX];
        snprintf(from_dir, sizeof from_dir, "%.*s", STR_ARG(d.len ? d : S("/")));
        if (dev_of(from_dir, &a) && dev_of(u->it[i].to, &b) && a != b) return true;
    }
    return false;
}

// Undoes it (not a cross-drive move: undo_is_job). *done counts what went back. False with the
// first error; the undo is used up either way.
static bool undo_do(Undo *u, isize *done, Err *err)
{
    *done = 0;
    if (!undo_check(u, err)) return false;
    bool ok = true;
    for (isize i = 0; i < u->n; i++) {
        const UndoItem *it = &u->it[i];
        Err e;
        bool one = false;
        switch (u->kind) {
        case UNDO_RENAME:
        case UNDO_MOVE: one = move_noreplace(it->to, it->from, &e); break;
        case UNDO_MKDIR:
            one = ops_allowed(it->to, &e) && (rmdir(it->to) == 0 || (err_set(&e, "%s isn't empty now: can't undo", it->to), false));
            break;
        case UNDO_CHMOD: {
            mode_t was;
            one = op_chmod(it->to, (mode_t)strtol(it->info ? it->info : "", NULL, 8), &was, &e);
            break;
        }
        case UNDO_MKFILE: one = ops_allowed(it->to, &e) && (unlink(it->to) == 0 || (err_set(&e, "%s: %s", it->to, strerror(errno)), false)); break;
        case UNDO_TRASH: {
            Trashed t = { 0 };
            snprintf(t.file, sizeof t.file, "%s", it->to);
            snprintf(t.info, sizeof t.info, "%s", it->info);
            snprintf(t.from, sizeof t.from, "%s", it->from);
            one = trash_restore(&t, &e);
            break;
        }
        case UNDO_COPY: {
            Trashed t;
            one = trash_put(it->to, &t, &e);   // copies go to the trash, never for good
            break;
        }
        default: break;
        }
        if (one) (*done)++;
        else if (ok) ok = false, *err = e;
    }
    undo_clear(u);
    return ok;
}
