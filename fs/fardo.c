// ofx-list --do <action> <args>: one action on a far drive (a network share, a FUSE mount), done in
// this child rather than on ofx's main thread (F16 step 3b; Michael, 2026-11-09: "child, bounded
// wait"). A dead share then holds this process, not the window: ofx waits for the answer a moment
// (gui/far.c, far_do), and goes on without it after that. Every write goes through ops_allowed, as
// in ofx (the guard's root comes with the environment).
//
// The answer, on stdout: "ok <n>\n" or "no <n>\n", then n fields, each "<length>\n<bytes>" (a path
// may hold a newline). "no"'s first field is the reason.
//
//   rename <dir> <from> <to>                 op_rename
//   mkdir|mkfile <dir> <name>                op_mkdir, op_mkfile: the name made
//   symlink <dir> <target> <name>            op_symlink: the name made
//   chmod <path> <octal>                     op_chmod: the mode it had (octal)
//   trash <path>                             trash_put: where it went (from, file, info)
//   free <dir> <name> <dir?> <copy?> <fresh?> name_free (0 or 1 each): the free name
//   stat <path>                              lstat, and stat when a link: both as bytes, the link's
//                                            text, the birth time (-1: none); "" for a broken link's
//                                            stat
//   undo <kind> (<from> <to> <info>)...      undo_do ("" for none): "done <n>", or "job" when it is
//                                            a move across drives (ofx runs that as a job)
// Included by fs.c after undo.c.

#include "../base/base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { FAR_DO_FIELDS = 8 };

typedef struct {
    bool ok;
    int  n;
    Str  f[FAR_DO_FIELDS];
} FarReply;

// The answer written: ok or not, n fields.
static void far_reply(FILE *out, bool ok, int n, const Str *f)
{
    fprintf(out, "%s %d\n", ok ? "ok" : "no", n);
    for (int i = 0; i < n; i++) {
        fprintf(out, "%td\n", f[i].len);
        fwrite(f[i].p, 1, (size_t)f[i].len, out);
    }
    fflush(out);
}

static void far_reply_err(FILE *out, const char *why)
{
    Str f = str_from_c(why);
    far_reply(out, false, 1, &f);
}

// A line of digits alone, at most hi, into *v.
static bool far_len(Str line, i64 hi, i64 *v)
{
    if (line.len < 1 || line.len > 18) return false;
    i64 x = 0;
    for (isize i = 0; i < line.len; i++) {
        if (line.p[i] < '0' || line.p[i] > '9') return false;
        x = x * 10 + (line.p[i] - '0');
    }
    return x <= hi && (*v = x, true);
}

// b read as an answer into *r (its fields point into b). False when it isn't a whole one.
static bool far_reply_parse(Str b, FarReply *r)
{
    *r = (FarReply){ 0 };
    isize at = 0;
    Str line;
    i64 v;
    if (!next_line(b, &at, &line) || line.len < 4) return false;
    bool ok = memcmp(line.p, "ok ", 3) == 0, no = memcmp(line.p, "no ", 3) == 0;
    if ((!ok && !no) || !far_len((Str){ line.p + 3, line.len - 3 }, FAR_DO_FIELDS, &v)) return false;
    if (no && v < 1) return false;   // (a refusal says why)
    r->ok = ok, r->n = (int)v;
    for (int i = 0; i < r->n; i++) {
        if (!next_line(b, &at, &line) || !far_len(line, b.len - at, &v)) return false;
        r->f[i] = (Str){ b.p + at, v };
        at += v;
    }
    return at == b.len;
}

// The bytes of a struct as a field.
static Str far_bytes(const void *p, size_t n)
{
    return (Str){ (char *)p, (isize)n };
}

// One action, argv from the action's name on; its answer on out. 0, or 2 for a call ofx never makes.
static int far_do_serve(FILE *out, int argc, char **argv)
{
    Err err = { "" };
    const char *op = argc > 0 ? argv[0] : "";
#define ARGS(n)                                                   \
    if (argc != (n) + 1) {                                        \
        far_reply_err(out, "ofx-list --do: wrong arguments");     \
        return 2;                                                 \
    }
    char made[NAME_MAX + 1];
    if (strcmp(op, "rename") == 0) {
        ARGS(3);
        if (!op_rename(argv[1], str_from_c(argv[2]), str_from_c(argv[3]), &err)) return far_reply_err(out, err.msg), 0;
        far_reply(out, true, 0, NULL);
    } else if (strcmp(op, "mkdir") == 0 || strcmp(op, "mkfile") == 0) {
        ARGS(2);
        bool ok = op[2] == 'd' ? op_mkdir(argv[1], str_from_c(argv[2]), made, sizeof made, &err) : op_mkfile(argv[1], str_from_c(argv[2]), made, sizeof made, &err);
        if (!ok) return far_reply_err(out, err.msg), 0;
        Str f = str_from_c(made);
        far_reply(out, true, 1, &f);
    } else if (strcmp(op, "symlink") == 0) {
        ARGS(3);
        if (!op_symlink(argv[1], argv[2], str_from_c(argv[3]), made, sizeof made, &err)) return far_reply_err(out, err.msg), 0;
        Str f = str_from_c(made);
        far_reply(out, true, 1, &f);
    } else if (strcmp(op, "chmod") == 0) {
        ARGS(2);
        mode_t old;
        if (!op_chmod(argv[1], (mode_t)strtol(argv[2], NULL, 8), &old, &err)) return far_reply_err(out, err.msg), 0;
        char was[16];
        snprintf(was, sizeof was, "%o", (unsigned)old);
        Str f = str_from_c(was);
        far_reply(out, true, 1, &f);
    } else if (strcmp(op, "trash") == 0) {
        ARGS(1);
        static Trashed t;
        if (!trash_put(argv[1], &t, &err)) return far_reply_err(out, err.msg), 0;
        Str f[3] = { str_from_c(t.from), str_from_c(t.file), str_from_c(t.info) };
        far_reply(out, true, 3, f);
    } else if (strcmp(op, "free") == 0) {
        ARGS(5);
        if (!name_free(argv[1], str_from_c(argv[2]), argv[3][0] == '1', argv[4][0] == '1', argv[5][0] == '1', made, sizeof made))
            return far_reply_err(out, "no free name"), 0;
        Str f = str_from_c(made);
        far_reply(out, true, 1, &f);
    } else if (strcmp(op, "stat") == 0) {
        ARGS(1);
        struct stat lst, st;
        if (lstat(argv[1], &lst) != 0) return far_reply_err(out, strerror(errno)), 0;
        char link[PATH_MAX] = "", bt[24];
        bool broken = false;
        st = lst;
        if (S_ISLNK(lst.st_mode)) {
            ssize_t k = readlink(argv[1], link, sizeof link - 1);
            link[k > 0 ? k : 0] = 0;
            broken = stat(argv[1], &st) != 0;
        }
        struct statx sx;
        i64 btime = statx(AT_FDCWD, argv[1], broken ? AT_SYMLINK_NOFOLLOW : 0, STATX_BTIME, &sx) == 0 && (sx.stx_mask & STATX_BTIME) ? (i64)sx.stx_btime.tv_sec : -1;
        snprintf(bt, sizeof bt, "%lld", (long long)btime);
        Str f[4] = { far_bytes(&lst, sizeof lst), broken ? STR_EMPTY : far_bytes(&st, sizeof st), str_from_c(link), str_from_c(bt) };
        far_reply(out, true, 4, f);
    } else if (strcmp(op, "undo") == 0) {
        if (argc < 2 || (argc - 2) % 3 != 0 || (argc - 2) / 3 > UNDO_ITEMS_MAX) return far_reply_err(out, "ofx-list --do: wrong arguments"), 2;
        static Undo u;
        static u8 umem[MiB(2)];
        static Arena ua;
        arena_init(&ua, "undo", umem, sizeof umem);
        u.arena = &ua;
        undo_begin(&u, (UndoKind)atoi(argv[1]));
        for (int i = 2; i + 2 < argc; i += 3)
            undo_add(&u, argv[i][0] ? argv[i] : NULL, argv[i + 1][0] ? argv[i + 1] : NULL, argv[i + 2][0] ? argv[i + 2] : NULL);
        if (undo_is_job(&u)) {
            if (!undo_check(&u, &err)) return far_reply_err(out, err.msg), 0;
            Str f = S("job");
            far_reply(out, true, 1, &f);
            return 0;
        }
        isize done;
        bool ok = undo_do(&u, &done, &err);
        char d[24];
        snprintf(d, sizeof d, "%td", done);
        Str f[2] = { str_from_c(ok ? "done" : err.msg), str_from_c(d) };
        far_reply(out, ok, 2, f);
    } else {
        far_reply_err(out, "ofx-list --do: no such action");
        return 2;
    }
#undef ARGS
    return 0;
}
