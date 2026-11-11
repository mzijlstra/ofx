// Changing files (F3) and every other action ui/ asks for (act): trash, delete, copy and paste,
// rename, links, undo, tabs, the folder menus' (folder_do is input.c's). Included by main.c.

// ---- changing files (F3) ---------------------------------------------------------------------

// The entries an action works on: the marked ones, or the cursor's when none is marked; their
// paths into a, in list order.
static isize targets(Arena *a, char **out, isize max)
{
    isize n = 0;
    for (isize i = 0; app.have_list && i < app.list.n && n < max; i++) {
        if (app.ui.marked ? !app.ui.mark[i] : i != app.ui.list.sel) continue;
        char p[PATH_MAX];
        if (entry_path(i, p, sizeof p)) out[n++] = arena_cstr(a, str_from_c(p));
    }
    return n;
}

static const char *items(isize n)
{
    return n == 1 ? "item" : "items";
}

#include "jobs.c"

// A new undo record of kind k: a job still running from before no longer writes its own over it.
static void undo_start(UndoKind k)
{
    undo_gen++;
    undo_begin(&undo, k);
}
#include "eject.c"
#include "connect.c"   // (F16 step 4: Connect to Server)

// ---- a sidebar place's own actions (the code-health review's step F: each written two or three
// times by its kind) -----------------------------------------------------------------------------

// What the folder menu is on a place (UiCtxKind).
static UiCtxKind place_ctx_kind(const Place *p)
{
    if (place_brings(p->kind)) return UI_CTX_BRINGS;
    if (p->kind == PL_BROWSE) return UI_CTX_NETWORK;   // (F16 step 7e)
    if (place_ejectable(p)) return UI_CTX_DRIVE;
    if (p->kind == PL_SHARE) return UI_CTX_SHARE;   // (F16 step 5)
    return UI_CTX_PLACE;
}

// A place without a folder yet brought: a drive mounted, then opened (2026-10-30); an address
// connected to (a network bookmark: then opened; a server found: its shares listed; F16 steps 5,
// 7b). False when it isn't one.
static bool place_bring(const Place *p)
{
    if (p->kind == PL_UNMOUNTED) return mount_start(p), true;
    if (place_connects(p->kind)) return connect_start(str_from_c(p->path)), true;
    return false;
}

// A place's eject button or menu entry: a removable drive ejected (eject) or unmounted, a share
// unmounted (gio mount -u, F16 step 5; it has no Eject).
static void place_unmount(const Place *p, bool eject)
{
    if (place_ejectable(p)) (eject ? eject_start : unmount_start)(p);
    else if (p->kind == PL_SHARE) share_unmount_start(p);
}

// ---- on a far drive (F16 step 3b) ------------------------------------------------------------

static FarReply far_r;   // the last far action's answer (its fields in far.c's far_do_mem)

// An action on the far drive under dir, run by ofx-list --do (far_do: ofx waits for it a moment at
// most), args from the action's name on, NULL ended; until: a batch's deadline (far_batch), 0 for
// one action's own. False with the reason, its own or "isn't answering"; its answer in far_r.
static bool far_act_by(const char *dir, const char *const *args, i64 until, Err *err)
{
    if (!far_do(app.exe, dir, args, until, &far_r, err)) return false;
    if (!far_r.ok) return err_set(err, "%.*s", STR_ARG(far_r.f[0])), false;
    return true;
}

static bool far_act(const char *dir, const char *const *args, Err *err)
{
    return far_act_by(dir, args, 0, err);
}

// A batch's deadline on a far drive (step 3 review: the wait is bounded for the batch, not each of
// its actions), set by its first far action: *until 0 before.
static i64 far_batch(i64 *until)
{
    if (!*until) *until = now_ms() + far_do_ms();
    return *until;
}

// Whether the far drive didn't answer in time, or the batch's time is up (the action's err): the
// rest of a batch isn't tried.
static bool far_silent(const Err *err)
{
    return strstr(err->msg, " isn't answering: ") != NULL || strstr(err->msg, " is slow: ") != NULL;
}

// A batch on a far drive cut off after some of it was done: the share answers, slowly (step 3
// confirm), said so rather than that it isn't answering.
static void far_slow_said(Err *why, isize done, const char *path)
{
    if (done > 0) err_set(why, "%.*s is slow: the rest weren't tried", STR_ARG(far_who(path)));
}

// ---- one action, made where its folder is: by ofx-list on a far drive (F16 step 3b), else by
// fs/ops.c. (The code-health review's step D: each caller chose for itself, with its own checks.)

// from renamed to `to` in folder in.
static bool act_rename(const char *in, Str from, Str to, Err *err)
{
    if (!far_dir(in)) return op_rename(in, from, to, err);
    char fc[NAME_MAX + 1], tc[NAME_MAX + 1];
    if (from.len >= (isize)sizeof fc || to.len >= (isize)sizeof tc) return err_set(err, "the name is too long"), false;
    snprintf(fc, sizeof fc, "%.*s", STR_ARG(from)), snprintf(tc, sizeof tc, "%.*s", STR_ARG(to));
    return far_act(in, (const char *const[]){ "rename", in, fc, tc, NULL }, err);
}

// An empty file "New file" or (!file) a folder "New folder" in folder in, a number added when the
// name is taken: the name made into made.
static bool act_make(const char *in, bool file, char *made, size_t cap, Err *err)
{
    const char *name = file ? "New file" : "New folder";
    if (!far_dir(in)) return file ? op_mkfile(in, str_from_c(name), made, cap, err) : op_mkdir(in, str_from_c(name), made, cap, err);
    if (!far_act(in, (const char *const[]){ file ? "mkfile" : "mkdir", in, name, NULL }, err)) return false;
    return snprintf(made, cap, "%.*s", STR_ARG(far_r.f[0])), true;
}

// A link named name to target in folder in, a number added when the name is taken: the name made
// into made. until: the batch's deadline on a far drive (far_batch).
static bool act_symlink(const char *in, const char *target, const char *name, char *made, size_t cap, i64 *until, Err *err)
{
    if (!far_dir(in)) return op_symlink(in, target, str_from_c(name), made, cap, err);
    if (!far_act_by(in, (const char *const[]){ "symlink", in, target, name, NULL }, far_batch(until), err)) return false;
    return snprintf(made, cap, "%.*s", STR_ARG(far_r.f[0])), true;
}

// path's permissions set to perms, the ones it had into *old.
static bool act_chmod(const char *path, mode_t perms, mode_t *old, Err *err)
{
    if (!far_dir(path)) return op_chmod(path, perms, old, err);
    char octal[8];
    snprintf(octal, sizeof octal, "%o", (unsigned)perms);
    if (!far_act(path, (const char *const[]){ "chmod", path, octal, NULL }, err)) return false;
    char said[16];
    snprintf(said, sizeof said, "%.*s", STR_ARG(far_r.f[0]));
    return *old = (mode_t)strtol(said, NULL, 8), true;
}

// "Trashed a.txt", "Trashed 3 items" or "Trashed 2 of 3: why" (verb, ok of n done, one the name
// when it is the only one).
static void say_done(const char *verb, isize ok, isize n, const char *one, const char *why)
{
    if (ok == n && n == 1) set_status(false, "%s %s", verb, one);
    else if (ok == n) set_status(false, "%s %td items", verb, n);
    else set_status(true, "%s %td of %td: %s", verb, ok, n, why);
}

static void trash_paths(char *const *paths, isize n, bool marks);

static void trash_targets(void)
{
    char *paths[CLIP_PATHS_MAX];
    arena_reset(&pastea);
    isize n = targets(&pastea, paths, CLIP_PATHS_MAX);
    trash_paths(paths, n, true);
}

// paths to the trash, as one undo (Delete's, and a drop on the Trash: F13 step 3); marks: they are
// the marks, which clear (a drag of an unmarked entry leaves them: code-health review, 2026-11-04).
static void trash_paths(char *const *paths, isize n, bool marks)
{
    isize ok = 0;
    for (isize i = 0; i < n; i++)
        if (jobs_refuse(paths[i])) return;
    Err err, first = { "" };
    // gvfs gives a share no trash (step 3 review: its trash would be at gvfs's own root, which
    // refuses it): said at once, as Nautilus does, the files left.
    for (isize i = 0; i < n; i++)
        if (far_no_trash(paths[i])) {
            set_status(true, "A network share has no trash: Shift+Delete deletes for good");
            return;
        }
    i64 until = 0;
    undo_start(UNDO_TRASH);
    for (isize i = 0; i < n; i++) {
        static Trashed t;
        bool one;
        if (!far_dir(paths[i])) one = trash_put(paths[i], &t, &err);
        else if ((one = far_act_by(paths[i], (const char *const[]){ "trash", paths[i], NULL }, far_batch(&until), &err)))   // (F16 step 3b)
            snprintf(t.from, sizeof t.from, "%.*s", STR_ARG(far_r.f[0])), snprintf(t.file, sizeof t.file, "%.*s", STR_ARG(far_r.f[1])),
                snprintf(t.info, sizeof t.info, "%.*s", STR_ARG(far_r.f[2]));
        if (one) undo_add(&undo, t.from, t.file, t.info), ok++;
        else if (!first.msg[0]) first = err;
        if (!one && far_silent(&err)) {
            first = err;
            far_slow_said(&first, ok, paths[i]);
            break;
        }
    }
    if (ok == 0) undo_clear(&undo);
    if (n > 0) say_done("Trashed", ok, n, path_base(str_from_c(paths[0])).p, first.msg);
    if (marks) ui_mark_clear(&app.ui);
    trash_state();
    reload();
}

// What the delete question asks about, taken when it opens: a selection or a cursor that changes
// while it is up (a delete job finishing in another tab, the cursor's file removed from outside)
// must not change what Yes deletes (F5 review, 2026-10-21).
static struct {
    char *path[CLIP_PATHS_MAX];
    isize n;
} doomed;

static u8    doomed_mem[MiB(2)];
static Arena doomeda;

// More marked than one action holds (CLIP_PATHS_MAX): said, and true; it would quietly do part.
static bool too_many_marked(void)
{
    if (app.ui.marked <= CLIP_PATHS_MAX) return false;
    set_status(true, "%td selected: ofx takes at most %d at a time", app.ui.marked, CLIP_PATHS_MAX);
    return true;
}

// The question just opened (and maybe was answered in the same frame): its targets taken now, and
// its count made theirs; none when it may delete nothing (then Yes deletes nothing).
static void delete_ask(void)
{
    doomed.n = 0;
    if (too_many_marked()) {
        app.ui.confirming = false;
        return;
    }
    if (app.ui.view == UI_VIEW_TRASH && app.ui.marked > CLIP_PATHS_MAX / 2) {   // each goes with its .trashinfo
        app.ui.confirming = false;
        set_status(true, "%td selected: ofx deletes at most %d at a time from the trash", app.ui.marked, CLIP_PATHS_MAX / 2);
        return;
    }
    arena_reset(&doomeda);
    doomed.n = targets(&doomeda, doomed.path, app.ui.view == UI_VIEW_TRASH ? CLIP_PATHS_MAX / 2 : CLIP_PATHS_MAX);
    app.ui.confirm_n = doomed.n;
    if (doomed.n == 0) app.ui.confirming = false;
}

static void delete_targets(void)
{
    isize n = doomed.n;
    doomed.n = 0;
    JobRec *r = n ? job_new() : NULL;
    if (!r) return;
    isize k = 0;
    for (isize i = 0; i < n; i++) {
        r->item[k++] = (JobItem){ .src = arena_cstr(&r->arena, str_from_c(doomed.path[i])) };
        // In the trash view, each item's .trashinfo after it: <trash>/files/NAME has <trash>/info/NAME.trashinfo.
        Str f = str_from_c(doomed.path[i]), files = path_dir(f), top = path_dir(files);
        if (app.ui.view == UI_VIEW_TRASH && str_eq(path_base(files), S("files")))
            r->item[k++] = (JobItem){ .src = arena_sprintf(&r->arena, "%.*s/info/%.*s.trashinfo", STR_ARG(top), STR_ARG(path_base(f))), .after_ok = true };
    }
    bool pairs = k > n;
    job_submit(r, JOB_DELETE, k);
    if (pairs) {
        r->trash = JOB_TRASH_ITEMS;
        if (n == 1) snprintf(r->label, sizeof r->label, "Deleting %.*s from the trash", STR_ARG(path_base(str_from_c(doomed.path[0]))));
        else snprintf(r->label, sizeof r->label, "Deleting %td items from the trash", n);
    }
}

// Empty Trash, answered (F6): every trash folder's files/ and info/ deleted for good as one job
// (trash_put makes them again).
static void trash_empty(void)
{
    enum { EMPTY_MAX = 8 * CLIP_PATHS_MAX };
    static TrashDoomed doomed_all[EMPTY_MAX];
    static u8 empty_mem[MiB(4)];
    static Arena emptya;
    arena_init(&emptya, "empty", empty_mem, sizeof empty_mem);
    trash_scan();
    isize n;
    bool all = trash_empty_items(&emptya, &trashd, doomed_all, EMPTY_MAX, &n);
    if (n == 0) {
        set_status(false, "The trash is empty");
        return;
    }
    // A job per CLIP_PATHS_MAX items, never between an item and its .trashinfo.
    for (isize at = 0; at < n;) {
        isize k = trash_empty_chunk(doomed_all, n, at, CLIP_PATHS_MAX);
        JobRec *r = job_new();
        if (!r) return;   // the queue is full: its panel says so; Empty Trash again for the rest
        for (isize i = 0; i < k; i++)
            r->item[i] = (JobItem){ .src = arena_cstr(&r->arena, str_from_c(doomed_all[at + i].path)), .after_ok = doomed_all[at + i].after_ok };
        job_submit(r, JOB_DELETE, k);
        r->trash = JOB_TRASH_EMPTY;
        snprintf(r->label, sizeof r->label, "Emptying the trash");
        at += k;
    }
    if (!all) set_status(true, "More in the trash than ofx empties at once: Empty Trash again for the rest");
}

// Restore (F6): the marked items, or the cursor's, back where they came from.
static void trash_restore_targets(void)
{
    isize n = 0, ok = 0;
    Err first = { 0 };
    char name[NAME_MAX + 1] = "";
    for (isize i = 0; app.have_list && i < app.list.n; i++) {
        if (app.ui.marked ? !app.ui.mark[i] : i != app.ui.list.sel) continue;
        const Entry *e = &app.list.e[i];
        if (!e->trash) continue;
        n++;
        Err err;
        if (jobs_refuse(e->trash->from)) continue;
        if (trash_restore_item(e->trash, &err)) ok++, snprintf(name, sizeof name, "%.*s", STR_ARG(e->name));
        else if (!first.msg[0]) first = err;
    }
    if (n == 0) return;
    say_done("Restored", ok, n, name, first.msg[0] ? first.msg : "a job is using it");
    reload();
}

static void copy_targets(bool cut, const char *only)
{
    own_gen++;
    arena_reset(&owna);
    if (only) own.path[0] = arena_cstr(&owna, str_from_c(only)), own.n = 1;   // a folder's right-click menu's (F8)
    else own.n = targets(&owna, own.path, CLIP_PATHS_MAX);
    own.cut = cut;
    if (own.n == 0) return;
    bool sys = clip_set(&own);
    char name[NAME_MAX + 1];
    snprintf(name, sizeof name, "%.*s", STR_ARG(path_base(str_from_c(own.path[0]))));
    const char *note = !sys && !app.clip_told ? " (no system clipboard here: it stays inside ofx)" : "";
    if (!sys) app.clip_told = true;
    if (own.n == 1) set_status(false, "%s %s%s", cut ? "Cut" : "Copied", name, note);
    else set_status(false, "%s %td items%s", cut ? "Cut" : "Copied", own.n, note);
}

// Whether folder src would go into itself in dst_dir (fs/job.c's into_itself), resolved by far_path:
// never past a far drive (F16 step 3b).
static bool into_itself_far(const char *src, const char *dst_dir)
{
    char s[PATH_MAX], d[PATH_MAX];
    bool fs, fd;
    return far_path(src, s, sizeof s, &fs) && far_path(dst_dir, d, sizeof d, &fd) && path_within(d, s);
}

// Whether a and b are the same folder, resolved (never past a far drive: far_path, F16 step 3).
static bool same_dir(const char *a, const char *b)
{
    char x[PATH_MAX], y[PATH_MAX];
    bool fx, fy;
    return far_path(a, x, sizeof x, &fx) && far_path(b, y, sizeof y, &fy) && strcmp(x, y) == 0;
}

// c's files copied (or, cut, moved) into the folder dest, as one job: there already, a copy gets a
// free name ("a (copy).txt", "a (copy 2).txt"), and a cut one stays. Paste (into this folder, or
// into one of its folders, F8), and Duplicate (F7.4).
static void copy_here(const ClipFiles *c, const char *into)
{
    JobRec *r = job_new();
    if (!r) return;
    // What each is (d a folder, o anything else, - gone; 0 not yet) and the name it gets: a far
    // one's by ofx-list, all of them in one child, and the free names in one more, under one
    // deadline (step 3 review: one child per item held the window for the whole batch).
    static char kind[CLIP_PATHS_MAX], names[CLIP_PATHS_MAX][NAME_MAX + 1];
    static const char *args[3 * CLIP_PATHS_MAX + 4];
    static isize wait[CLIP_PATHS_MAX];
    isize m = c->n < CLIP_PATHS_MAX ? c->n : CLIP_PATHS_MAX, k = 0, nw = 0;
    i64 until = 0;
    Err err;
    const char *far_at = NULL;
    args[k++] = "kinds";
    for (isize i = 0; i < m; i++) {
        struct stat st;
        if (far_dir(c->path[i])) args[k++] = c->path[i], kind[i] = 0, far_at = far_at ? far_at : c->path[i];
        else kind[i] = lstat(c->path[i], &st) != 0 ? '-' : S_ISDIR(st.st_mode) ? 'd' : 'o';
    }
    args[k] = NULL;
    if (far_at && !far_act_by(far_at, args, far_batch(&until), &err)) {   // (r unused: still free)
        set_status(true, "%s", err.msg);
        return;
    }
    for (isize i = 0, f = 0; far_at && i < m; i++)
        if (!kind[i]) kind[i] = f < far_r.n && far_r.f[f].len == 1 ? far_r.f[f].p[0] : '-', f++;
    isize here = 0, self = 0, gone = 0;
    k = 0, far_at = NULL;
    args[k++] = "frees";
    for (isize i = 0; i < m; i++) {
        const char *src = c->path[i];
        Str base = path_base(str_from_c(src));
        char sdir[PATH_MAX];
        path_parent(sdir, sizeof sdir, src);
        const char *dest = into ? into : sdir;
        names[i][0] = 0;
        if (kind[i] == '-') {
            gone++;
            continue;
        }
        if (kind[i] == 'd' && into_itself_far(src, dest)) {
            self++;
            continue;
        }
        if (!same_dir(sdir, dest)) {
            snprintf(names[i], sizeof names[i], "%.*s", STR_ARG(base));
        } else if (c->cut) {
            here++;
        } else if (!far_dir(dest)) {
            name_free(dest, base, kind[i] == 'd', true, true, names[i], sizeof names[i]);
        } else {   // (a far one: asked of ofx-list with the rest, below)
            args[k++] = arena_cstr(&r->arena, str_from_c(dest)), args[k++] = kind[i] == 'd' ? "1" : "0", args[k++] = arena_cstr(&r->arena, base);
            wait[nw++] = i, far_at = far_at ? far_at : src;
        }
    }
    args[k] = NULL;
    if (far_at && !far_act_by(far_at, args, far_batch(&until), &err)) {
        set_status(true, "%s", err.msg);
        return;
    }
    for (isize w = 0; far_at && w < nw && w < far_r.n; w++) snprintf(names[wait[w]], sizeof names[0], "%.*s", STR_ARG(far_r.f[w]));
    isize n = 0;
    for (isize i = 0; i < m; i++) {
        char sdir[PATH_MAX], dst[PATH_MAX];
        path_parent(sdir, sizeof sdir, c->path[i]);
        if (!names[i][0] || !path_put(dst, sizeof dst, into ? into : sdir, str_from_c(names[i]))) continue;
        r->item[n++] = (JobItem){ .src = arena_cstr(&r->arena, str_from_c(c->path[i])), .dst = arena_cstr(&r->arena, str_from_c(dst)) };
    }
    if (n == 0) {
        if (self) set_status(true, "Can't paste a folder into itself");
        else if (here) set_status(true, "Already here");
        else set_status(true, "Nothing to paste: %td %s gone", gone, gone == 1 ? "is" : "are");
        return;
    }
    r->own_cut = c == &own && c->cut, r->own_gen = own_gen;
    job_submit(r, c->cut ? JOB_MOVE : JOB_COPY, n);
    if (self) set_status(true, "Skipped %td: a folder can't go into itself", self);
}

// What a drop on d does with the files dragged (F13 step 3): the Trash trashes them; else
// Nemo's rule (ui_drop_op), with the keys held now.
static UiDropOp drop_op(const Drop *d)
{
    return d->trash ? UI_DROP_TRASH : ui_drop_op(d->same_drive, app.drag_mods);
}

// The files dragged, dropped on d: trashed, or copied or moved there as a paste is (its job, its
// clash questions, its undo), from their own list, so the clipboard stays as it was.
static void drop_with(const Drop *d, UiDropOp op)
{
    if (op == UI_DROP_TRASH) {
        trash_paths(dragged.path, dragged.n, app.drag_marked);
        return;
    }
    dragged.cut = op == UI_DROP_MOVE;
    copy_here(&dragged, d->dir);
}

static void drop_do(const Drop *d)
{
    drop_with(d, drop_op(d));
}

static void paste(const char *dest)
{
    static ClipFiles theirs;
    arena_reset(&pastea);
    ClipFrom from = clip_get(&pastea, &theirs);
    ClipFiles *c = from == CLIP_THEIRS ? &theirs : from == CLIP_OURS || !clip.ok ? &own : NULL;
    if (!c || c->n == 0) { set_status(true, "The clipboard holds no files"); return; }
    copy_here(c, dest);
}

// Edit → Duplicate (F7.4): the selected entries (or the cursor's) copied beside themselves as
// "name (copy)", as a paste of them here makes; the clipboard isn't touched, Ctrl+Z removes them.
static void duplicate(void)
{
    static ClipFiles dup;
    arena_reset(&pastea);
    dup.cut = false, dup.n = targets(&pastea, dup.path, CLIP_PATHS_MAX);
    if (dup.n > 0) copy_here(&dup, NULL);   // (each beside itself: a search's are in many folders, F9)
}

// Make Link, Ctrl+M (F8, as Nemo): beside each marked entry (or the cursor's) a symbolic link to its
// full path, "Link to <name>" ("Link to a (2).txt" when taken); one undo removes them all, the
// cursor on the last made.
static void make_link(void)
{
    char *paths[CLIP_PATHS_MAX], made[NAME_MAX + 1] = "", p[PATH_MAX];
    arena_reset(&pastea);
    isize n = targets(&pastea, paths, CLIP_PATHS_MAX), done = 0;
    Err err = { 0 };
    i64 until = 0;   // (on a far drive: one deadline for all of them, step 3 review)
    for (isize i = 0; i < n; i++) {
        char name[NAME_MAX + 8], one[NAME_MAX + 1];
        Str base = path_base(str_from_c(paths[i]));
        if (snprintf(name, sizeof name, "Link to %.*s", STR_ARG(base)) >= (int)sizeof name) {
            err_set(&err, "the name is too long for a link to %.*s", STR_ARG(base));
            continue;
        }
        char in[PATH_MAX];
        path_parent(in, sizeof in, paths[i]);   // (beside it: a search result's own folder, F9)
        bool ok = act_symlink(in, paths[i], name, one, sizeof one, &until, &err);
        if (!ok && far_silent(&err)) {
            far_slow_said(&err, done, paths[i]);
            break;
        }
        if (!ok || !path_put(p, sizeof p, in, str_from_c(one))) continue;
        if (done++ == 0) undo_start(UNDO_LINK);
        undo_add(&undo, paths[i], p, NULL);
        snprintf(made, sizeof made, "%s", one);
    }
    if (done < n) set_status(true, "%s", err.msg);
    else if (done == 1) set_status(false, "Made %s", made);
    else set_status(false, "Made %td links", done);
    if (done) ui_mark_clear(&app.ui), go_to(app.cwd, app.ui.view == UI_VIEW_SEARCH ? sel_name() : str_from_c(made), app.ui.list.sel);
}

// Follow Link (F8): to the folder of what the link at entry i leads to, with it selected (every
// link on the way followed, as --select's folder is).
static void follow_link(isize i)
{
    char path[PATH_MAX], real[PATH_MAX], dir[PATH_MAX];
    bool reaches;   // (a link into a far drive: its target by the text from there, never looked at, F16 step 3)
    if (i >= app.list.n || !entry_path(i, path, sizeof path)) return;
    if (!far_path(path, real, sizeof real, &reaches)) {
        set_status(true, "%.*s leads nowhere: %s", STR_ARG(app.list.e[i].name), strerror(errno));
        return;
    }
    Str base = path_base(str_from_c(real));
    path_parent(dir, sizeof dir, real);
    go_to(dir, strcmp(real, "/") == 0 ? STR_EMPTY : base, 0);
}

// An edit line's Ctrl+C or Ctrl+X (F5.2): its selection onto the clipboard as text, in place of
// whatever was there, our files too. Without data-control (weston) GLFW's clipboard holds it.
static void text_copy(Str t)
{
    own_gen++;
    snprintf(app.text_copied, sizeof app.text_copied, "%.*s", STR_ARG(t));
    if (!clip_set_text(t)) SetClipboardText(app.text_copied);
    own.n = 0;
}

// Copy Path (F13): the marked entries' paths (or the cursor's) as text, one a line, or only's (a
// folder's menu); in an empty folder its own (Michael, 2026-11-04). Each quoted as a shell needs it
// (shell_quote: a name with a line break would paste as a command; step 1 review).
static void path_copy(const char *only)
{
    static ClipFiles c;
    arena_reset(&pastea);
    if (only) c.path[0] = arena_cstr(&pastea, str_from_c(only)), c.n = 1;
    else c.n = targets(&pastea, c.path, CLIP_PATHS_MAX);
    if (c.n == 0 && app.list.n == 0) c.path[0] = arena_cstr(&pastea, str_from_c(here_dir())), c.n = 1;
    if (c.n == 0) return;
    Str t = clip_text(&pastea, &c, true);
    if (t.len == 0) {
        set_status(true, "The paths are too long to copy together");
        return;
    }
    text_copy(t);
    if (c.n == 1) set_status(false, "Copied the path");
    else set_status(false, "Copied %td paths", c.n);
}

// The clipboard's text for an edit line's Ctrl+V, into pastea: our own files' paths as they are,
// not quoted as other programs get them (F13 code-health review, 2026-11-04), while the clipboard
// is still ours; else through data-control where there is one, or GLFW's text.
static Str text_paste(void)
{
    arena_reset(&pastea);
    Str t;
    if (own.n > 0 && (!clip.ok || clip_ours())) return clip_text(&pastea, &own, false);
    if (clip_get_text(&pastea, &t)) return t;
    const char *g = GetClipboardText();
    return g ? str_from_c(arena_cstr(&pastea, str_from_c(g))) : STR_EMPTY;
}

static void rename_commit(void)
{
    Str to = { app.ui.edit.b, app.ui.edit.len }, from = str_from_c(app.edit_from);
    if (str_eq(to, from)) { ui_edit_close(&app.ui); return; }
    const char *in = app.edit_dir;
    char was[PATH_MAX], will[PATH_MAX];
    if (path_put(was, sizeof was, in, from) && jobs_refuse(was)) return;
    if (path_put(will, sizeof will, in, to) && jobs_refuse(will)) return;
    Err err;
    if (!act_rename(in, from, to, &err)) { set_status(true, "%s", err.msg); return; }
    // The cursor stays on it: by its new name, in a search its folder's too, where the result takes
    // the new name in place (F9).
    char a[PATH_MAX], b[PATH_MAX], keep[PATH_MAX];
    Str key = sel_name(), rel = path_dir(key);
    if (app.ui.view == UI_VIEW_SEARCH) search_rename(key, to);
    if (rel.len) snprintf(keep, sizeof keep, "%.*s/%.*s", STR_ARG(rel), STR_ARG(to));
    else snprintf(keep, sizeof keep, "%.*s", STR_ARG(to));
    if (path_put(a, sizeof a, in, from) && path_put(b, sizeof b, in, to)) {
        // A folder just made stays a "new folder" for undo, whatever its name now.
        undo_start(app.edit_new ? app.edit_new : UNDO_RENAME);
        undo_add(&undo, app.edit_new ? NULL : a, b, NULL);
    }
    ui_edit_close(&app.ui);
    set_status(false, "Renamed %s to %.*s", app.edit_from, STR_ARG(to));
    view_changed();   // (a folder renamed: the results below it are gone)
    go_to(app.cwd, str_from_c(keep), app.ui.list.sel);
}

// A new folder "New folder", or (file) an empty file "New file" (F7.1): made, its name in the row
// to edit at once, all of it selected; a name taken gets a number. Ctrl+Z removes it.
static void new_made(bool file)
{
    char made[NAME_MAX + 1], p[PATH_MAX];
    Err err;
    UndoKind kind = file ? UNDO_MKFILE : UNDO_MKDIR;
    if (!act_make(app.cwd, file, made, sizeof made, &err)) {
        set_status(true, "%s", err.msg);
        return;
    }
    if (path_put(p, sizeof p, app.cwd, str_from_c(made))) undo_start(kind), undo_add(&undo, NULL, p, NULL);
    ui_mark_clear(&app.ui);
    go_to(app.cwd, str_from_c(made), app.ui.list.sel);
    if (app.far_on) {   // (its row comes with the listing: not edited before it is there, F16 step 3b)
        set_status(false, "Made %s: F2 renames it", made);
        return;
    }
    ui_edit_open_all(&app.ui, str_from_c(made));
    snprintf(app.edit_from, sizeof app.edit_from, "%s", made), snprintf(app.edit_dir, sizeof app.edit_dir, "%s", app.cwd);
    app.edit_new = kind;
}

static void close_others(bool quit);
static void props_open(const char *path);
static void folder_do(UiMenuItem it);
static void props_chmod(int k);

// Whether any of the undo's paths is on a far drive: then all of it is done by ofx-list (F16 step 3b).
static bool undo_far(void)
{
    for (isize i = 0; i < undo.n; i++)
        if ((undo.it[i].to && far_dir(undo.it[i].to)) || (undo.it[i].from && far_dir(undo.it[i].from))) return true;
    return false;
}

enum { UNDO_FAR_MAX = 256 };   // (each item three arguments to ofx-list: FAR_DO_ARGS)

// The undo by ofx-list --do undo: true with *job when it is a move across drives, checked there,
// for a job to undo; false with the reason (the undo used up).
static bool undo_by_far(bool *job, isize *done, Err *err)
{
    static const char *args[3 + 3 * UNDO_FAR_MAX];
    char kind[8];
    *job = false, *done = 0;
    if (undo.n > UNDO_FAR_MAX) return err_set(err, "%td items on a network drive is more than ofx undoes at once", undo.n), false;
    snprintf(kind, sizeof kind, "%d", (int)undo.kind);
    int k = 0;
    args[k++] = "undo", args[k++] = kind;
    for (isize i = 0; i < undo.n; i++) {
        const UndoItem *it = &undo.it[i];
        args[k++] = it->from ? it->from : "", args[k++] = it->to ? it->to : "", args[k++] = it->info ? it->info : "";
    }
    args[k] = NULL;
    bool ok = far_act(undo.it[0].to ? undo.it[0].to : app.cwd, args, err);
    if (ok && str_eq(far_r.f[0], S("job"))) *job = true;
    else if (far_r.n > 1) *done = (isize)strtol(arena_cstr(&scratch, far_r.f[1]), NULL, 10);
    return ok;
}

static void undo_last(void)
{
    char what[64];
    undo_what(&undo, what, sizeof what);
    for (isize i = 0; i < undo.n; i++)
        if ((undo.it[i].from && jobs_refuse(undo.it[i].from)) || (undo.it[i].to && jobs_refuse(undo.it[i].to))) return;
    undo_gen++;
    Err err;
    bool on_far = undo_far(), job = false, far_ok = true;
    isize done;
    if (on_far) far_ok = undo_by_far(&job, &done, &err);
    if (on_far && !far_ok) {   // (its reason; the undo used up, as undo_do's)
        undo_clear(&undo), set_status(true, "%s", err.msg);
        reload();
        return;
    }
    if (on_far ? job : undo_is_job(&undo)) {
        // A move across drives goes back as a move.
        if (!on_far && !undo_check(&undo, &err)) { undo_clear(&undo), set_status(true, "%s", err.msg); return; }
        JobRec *r = job_new();
        if (!r) return;
        for (isize i = 0; i < undo.n; i++)
            r->item[i] = (JobItem){ .src = arena_cstr(&r->arena, str_from_c(undo.it[i].to)), .dst = arena_cstr(&r->arena, str_from_c(undo.it[i].from)) };
        isize n = undo.n;
        undo_clear(&undo);
        r->reverse = true;
        job_submit(r, JOB_MOVE, n);
        return;
    }
    // What comes back here gets the cursor: a renamed, trashed or moved entry.
    char back[NAME_MAX + 1] = "";
    if (undo.n && undo.it[0].from && (undo.kind == UNDO_RENAME || undo.kind == UNDO_TRASH || undo.kind == UNDO_MOVE)) {
        Str f = str_from_c(undo.it[0].from);
        char dir[PATH_MAX];
        path_parent(dir, sizeof dir, undo.it[0].from);
        if (same_dir(dir, app.cwd)) snprintf(back, sizeof back, "%.*s", STR_ARG(path_base(f)));
    }
    if (on_far) undo_clear(&undo), set_status(false, "Undid the %s", what);   // (done by ofx-list above)
    else if (undo_do(&undo, &done, &err)) set_status(false, "Undid the %s", what);
    else set_status(true, "%s", err.msg);
    if (back[0]) go_to(app.cwd, str_from_c(back), app.ui.list.sel);
    else reload();
}

static int tab_slot_free(void)
{
    for (int i = 0; i < UI_TABS_MAX; i++)
        if (!tabs[i].used) return i;
    return -1;
}

// The tab on screen into its slot.
static void tab_save(void)
{
    Tab *t = &tabs[tabbar.slot[tabbar.cur]];
    t->used = true;
    snprintf(t->dir, sizeof t->dir, "%s", app.cwd);
    Str key = sel_name();   // (a search's key too long for it: none, not a cut one naming another entry)
    snprintf(t->sel, sizeof t->sel, "%.*s", key.len < (isize)sizeof t->sel ? (int)key.len : 0, key.p ? key.p : "");
    t->row = app.ui.list.sel, t->top = app.ui.list.top;
    t->hist = hist, t->crumbs = app.crumbs;
    memcpy(t->last, app.ui.last, sizeof t->last), t->last_len = app.ui.last_len;
    t->marks_len = 0, t->marks_cut = false;
    for (isize i = 0; app.have_list && app.ui.marked && i < app.list.n; i++) {
        if (!app.ui.mark[i]) continue;
        Str nm = app.list.e[i].name;
        if (t->marks_len + nm.len + 1 > (isize)sizeof t->marks) {
            t->marks_cut = true;
            break;
        }
        memcpy(t->marks + t->marks_len, nm.p, (size_t)nm.len);
        t->marks_len += nm.len, t->marks[t->marks_len++] = 0;
    }
}

// The tab at the current place onto the screen: its folder listed again, its cursor, scroll,
// selection and history back. A folder that is gone gives way to the nearest one above.
static void tab_load(void)
{
    Tab *t = &tabs[tabbar.slot[tabbar.cur]];
    hist = t->hist, app.crumbs = t->crumbs;
    memcpy(app.ui.last, t->last, sizeof app.ui.last), app.ui.last_len = t->last_len;
    ui_edit_close(&app.ui);
    ui_find_close(&app.ui);   // (a tab's own find isn't kept)
    ui_mark_clear(&app.ui);   // the same folder in two tabs: this tab's marks, not the other's
    if (!go_to_in(t->dir, str_from_c(t->sel), t->row, (GoHow){ .stepping = true, .then = FAR_THEN_UP })) {   // (a far one: if its lister says it is gone, F16 step 3)
        // Gone: the nearest folder above. There but unreadable: the same, with its error kept.
        char why[sizeof app.status];
        snprintf(why, sizeof why, "%s", app.status);
        struct stat st;
        bool there = !far_dir(t->dir) && stat(t->dir, &st) == 0;   // (not a far one: F16)
        go_up_from(t->dir);
        if (there) set_status(true, "%s", why);
        return;
    }
    app.ui.list.top = t->top;
    ui_set(&app.ui.list, app.list.n, app.ui.list.sel);
    for (isize at = 0; at < t->marks_len;) {
        Str nm = str_from_c(t->marks + at);
        isize k = listing_find(&app.list, nm);
        if (k >= 0) ui_mark(&app.ui, k, true);
        at += nm.len + 1;
    }
    if (t->marks_cut) set_status(true, "This tab's selection was too long to keep");
}

static void tab_show(int at)
{
    if (at < 0 || at >= tabbar.n || at == tabbar.cur) return;
    tab_save();
    tabbar.cur = at;
    tab_load();
}

// A new tab on dir: shown (Ctrl+T, on the current folder) or in the background (a middle click).
static void tab_open(const char *dir, Str sel, bool show)
{
    int slot = tab_slot_free();
    if (slot < 0 || tabbar.n == UI_TABS_MAX) {
        set_status(true, "%d tabs is the most ofx keeps open", UI_TABS_MAX);
        return;
    }
    tab_save();
    Tab *t = &tabs[slot];
    *t = (Tab){ .used = true };
    snprintf(t->dir, sizeof t->dir, "%s", dir);
    snprintf(t->sel, sizeof t->sel, "%.*s", STR_ARG(sel));
    crumbs_for(&t->crumbs, t->dir);
    ui_tabs_open(&tabbar, slot, show);
    if (show) tab_load();
    else set_status(false, "Opened %s in a new tab", path_base(str_from_c(t->dir)).p);
}

// A middle click's folder, in a background tab; a file isn't one.
static void tab_open_bg(const char *path)
{
    struct stat st;   // (a far folder isn't looked at here: its lister says, F16)
    if (strcmp(path, TRASH_URI) != 0 && strcmp(path, NETWORK_URI) != 0 && !is_search(path) && !far_dir(path) && (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))) {
        set_status(true, "%s isn't a folder", path_base(str_from_c(path)).p);
        return;
    }
    tab_open(path, STR_EMPTY, false);
}

// The tabs closed in this session, the last at the end, for Reopen Closed Tab (F11, Ctrl+Shift+T):
// each one's folder (its cursor is the folder's, remembered: remember.c).
enum { CLOSED_TABS_MAX = 16 };
static struct {
    char dir[CLOSED_TABS_MAX][PATH_MAX];
    int  n;
} closed_tabs;

// The tab at place `at` (-1: the one on screen) closed; the last one closes ofx.
static void tab_close(int at)
{
    if (at < 0) at = tabbar.cur;
    if (at < 0 || at >= tabbar.n) return;
    if (tabbar.n == 1) {
        app.quit = true;
        return;
    }
    bool shown = at == tabbar.cur;
    if (closed_tabs.n == CLOSED_TABS_MAX)   // (the oldest goes)
        memmove(closed_tabs.dir[0], closed_tabs.dir[1], sizeof closed_tabs.dir[0] * (CLOSED_TABS_MAX - 1)), closed_tabs.n--;
    snprintf(closed_tabs.dir[closed_tabs.n++], PATH_MAX, "%s", shown ? app.cwd : tabs[tabbar.slot[at]].dir);   // (the tab shown lives in app)
    int slot = ui_tabs_close(&tabbar, at);
    if (slot >= 0) tabs[slot].used = false;
    if (shown) tab_load();
}

// F2 opened the edit line: on the cursor's entry, as it is named now, in its folder (a search
// result's own, F9).
static void edit_opened(void)
{
    const Entry *e = app.have_list && app.ui.list.n > 0 ? &app.list.e[app.ui.list.sel] : NULL;
    char path[PATH_MAX];
    snprintf(app.edit_from, sizeof app.edit_from, "%.*s", e ? (int)e->name.len : 0, e ? e->name.p : "");
    app.edit_new = UNDO_NONE;
    if (e && entry_path(app.ui.list.sel, path, sizeof path)) path_parent(app.edit_dir, sizeof app.edit_dir, path);
    else snprintf(app.edit_dir, sizeof app.edit_dir, "%s", app.cwd);
}

// The location line's Enter: there, if it is a folder; else the line stays open with the reason.
static void location_go(void)
{
    Str to;
    char path[PATH_MAX];
    if (connect_start((Str){ app.ui.edit.b, app.ui.edit.len })) return;   // smb://… (F16 step 4)
    if (!ui_location(&scratch, (Str){ app.ui.edit.b, app.ui.edit.len }, here_dir(), getenv("HOME"), &to) || to.len >= PATH_MAX) {
        set_status(true, "Not a folder: %.*s", app.ui.edit.len, app.ui.edit.b);
        return;
    }
    snprintf(path, sizeof path, "%.*s", STR_ARG(to));
    // A far one (F16) is gone to as a folder, never looked at here: its lister says what it is.
    // A file there: its folder, it selected, when the lister says so (far_refused, step 3).
    if (far_dir(path)) {
        if (!go_to_then(path, STR_EMPTY, 0, FAR_THEN_SELECT)) return;
        ui_edit_close(&app.ui);
        return;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        set_status(true, "%s: %s", path, strerror(errno));
        return;
    }
    // A file: its folder, the cursor on it, as Nemo does (Michael, 2026-10-21).
    Str select = STR_EMPTY;
    char dir[PATH_MAX];
    if (!S_ISDIR(st.st_mode)) {
        select = path_base(str_from_c(path));
        path_parent(dir, sizeof dir, path);
    } else {
        snprintf(dir, sizeof dir, "%s", path);
    }
    char name[NAME_MAX + 1];
    snprintf(name, sizeof name, "%.*s", STR_ARG(select));
    if (!go_to(dir, str_from_c(name), 0)) return;
    ui_edit_close(&app.ui);
    if (name[0] == '.' && !app.show_hidden) set_status(false, "%s is hidden: . shows hidden files", name);
}

// A path button: its folder, the cursor on the next folder down the bar (where you came from).
static void crumb_go(int i)
{
    UiCrumbs *c = &app.crumbs;
    if (i < 0 || i >= c->n) return;
    char path[PATH_MAX], below[NAME_MAX + 1] = "";
    snprintf(path, sizeof path, "%.*s", STR_ARG(ui_crumb_path(c, i)));
    if (i + 1 < c->n) snprintf(below, sizeof below, "%.*s", STR_ARG(ui_crumb_label(c, i + 1)));
    if (strcmp(path, app.cwd) != 0) go_to(path, str_from_c(below), 0);
}

static void bmp_act(UiAction a);   // Edit Bookmarks (gui/bookmarks.c, F7.5)

// What the actions and a folder's menu (folder_do) both do on a folder: a search below it in a tab
// of its own (F9), a terminal there, another window on it; the status says how it went. NULL: none.
static void search_in(const char *dir)
{
    char uri[PATH_MAX];
    if (dir && search_uri(uri, sizeof uri, dir, STR_EMPTY, STR_EMPTY)) tab_open(uri, STR_EMPTY, true);
}

static void terminal_in(const char *dir)
{
    Err err = { 0 };
    if (!dir) return;
    if (spawn_terminal(dir, &err)) set_status(false, "terminal in %s", dir);
    else set_status(true, "%s", err.msg);
}

static void window_on(const char *dir)
{
    Err err = { 0 };
    if (!dir) return;
    if (spawn_window(app.exe, dir, &err)) set_status(false, "New window on %s", dir);
    else set_status(true, "%s", err.msg);
}

static void conf_shown(char *out, size_t cap);   // (prefs.c, after)

// ---- Open at Line (F9 step 4) -----------------------------------------------------------------

// The editor Open at Line uses (fs/editors.c): the default's name, its recipe (NULL: none), the
// recipes of ofx.conf's editor lines then of the older editors file, and that file's first bad line
// and how many it still has. Read again at most once a second (the result's menu asks each frame
// it is open), and always as it opens.
static struct {
    i64           at;
    Editors       user;
    int           old;     // recipes from the editors file
    bool          told;    // that they can move to ofx.conf, said once a run
    char          name[EDITOR_NAME_MAX];
    const Editor *e;
    u8            mem[KiB(64)];
    Arena         a;
} editors;

static const Editor *editor_now(bool again)
{
    i64 now = now_ms();
    if (!again && editors.at && now - editors.at < 1000) return editors.e;
    editors.at = now;
    arena_init(&editors.a, "editors", editors.mem, sizeof editors.mem);
    char path[PATH_MAX];
    editors.user = (Editors){ 0 };
    for (int k = 0; k < app.conf.neditor; k++) editors_add(&editors.a, str_from_c(app.conf.editor[k]), &editors.user);
    int mine = editors.user.n;
    editors_add(&editors.a, editors_path(path, sizeof path) ? read_file(&editors.a, path) : STR_EMPTY, &editors.user);
    editors.old = editors.user.n - mine;
    editor_default(&editors.a, editors.name, sizeof editors.name);
    return editors.e = editor_find(&editors.user, editors.name);
}

// The result's menu entry: "Open at Line in Neovim", the editor's name when it has no recipe.
static Str open_line_label(void)
{
    const Editor *e = editor_now(false);
    return str_from_c(arena_sprintf(&frame, "Open at Line in %s", e ? e->label : editors.name));
}

// Result i in the editor at the line it matched on (F9 step 4); the status line says why not.
static void open_at_line(isize i)
{
    const Entry *en = i >= 0 && i < app.list.n ? &app.list.e[i] : NULL;
    if (!en || !en->hit) {
        set_status(true, "Only a result found inside files has a line: In files (Alt+I)");
        return;
    }
    char path[PATH_MAX], ep[PATH_MAX], shown[PATH_MAX + 1], conf[PATH_MAX + 1];
    if (!entry_path(i, path, sizeof path)) return;
    const Editor *e = editor_now(true);
    if (!editors_path(ep, sizeof ep)) snprintf(ep, sizeof ep, "~/.config/ofx/editors");
    ui_tilde(shown, sizeof shown, ep, getenv("HOME"));
    conf_shown(conf, sizeof conf);
    if (!e) {
        set_status(true, "No Open at Line recipe for %s: add an editor line to %s (Edit > Preferences...)", editors.name, conf);
        return;
    }
    char buf[EDITOR_ARGS_MAX];
    const char *argv[EDITOR_WORDS + 2];
    Err err;
    int n = editor_argv(e, path, en->hit->line, en->hit->col, buf, sizeof buf, argv, COUNT(argv));
    if (n < 0) set_status(true, "%s's command is too long for that file", e->label);
    else if (!editor_open(e, argv, n, &err)) set_status(true, "%s", err.msg);
    else if (editors.user.bad) set_status(true, "%s: line %d isn't a recipe (a name, yes or no, then the command)", shown, editors.user.bad);
    else if (editors.old && !editors.told) editors.told = true, set_status(false, "%s still works; its lines can move to %s, each as editor = <line>", shown, conf);
}

// One key of the state file put (its value; another window's keys left alone: state_put), a failure
// said in the status line (false then).
static bool state_key(const char *key, const char *value)
{
    char path[PATH_MAX];
    Err err;
    if (state_path(path, sizeof path) && !state_put(&scratch, path, key, value, &err)) return set_status(true, "%s", err.msg), false;
    return true;
}

// The sidebar's width w, dragged to or given back (F13): kept, and remembered for every window as
// its mode is (sidewidth=).
static void side_keep(int w)
{
    char v[16];
    app.side_px = w;
    snprintf(v, sizeof v, "%d", w);
    state_key("sidewidth", v);
}

static void prefs_open(void);   // (prefs.c, after: it uses editor_now)
static void restore_others(bool yes);   // (restore.c, after: it uses the tabs)

// A switch's answer (F12 step 5c): the folder listed again (the branch may not have it: then the
// nearest above that is there), and the status line says where it is now, and what a hook said;
// or git's refusal; or that it carries on past its time.
static bool branch_switched(void)
{
    GitSwitch got;
    char name[GIT_HEAD_MAX], msg[256], was[PATH_MAX], gone[PATH_MAX + 32] = "", asked[PATH_MAX];
    if (!git_switch_take(&got, name, sizeof name, asked, sizeof asked, msg, sizeof msg)) return false;
    if (got == GSW_REFUSED) return set_status(true, "git switch %s: %s", name, msg), true;
    if (got == GSW_GOING) return set_status(false, "git switch %s: %s", name, msg), true;
    snprintf(was, sizeof was, "%s", app.cwd);
    if (app.ui.view == UI_VIEW_FOLDER && strcmp(app.cwd, asked) == 0) reload();   // (gone elsewhere meanwhile: that folder is its own; review, 2026-11-03)
    if (strcmp(was, app.cwd) != 0) snprintf(gone, sizeof gone, ": %.*s isn't on it", STR_ARG(path_base(str_from_c(was))));
    set_status(false, "Switched to %s%s%s%s%s", name, gone, msg[0] ? " (" : "", msg, msg[0] ? ")" : "");   // (a hook's or a filter's words)
    return true;
}

static void act(UiAction a)
{
    switch (a.kind) {
    case UA_RESTORE: trash_restore_targets(); break;
    case UA_EJECT:
        if (a.index >= 0 && a.index < app.places.n) place_unmount(&app.places.p[a.index], true);
        break;
    case UA_EMPTY_TRASH: trash_empty(); break;
    case UA_CLEAR_PREVIEWS: clear_start(); break;
    case UA_NOT_IN_TRASH: set_status(true, "That doesn't work in the trash: restore it first"); break;
    case UA_READ_ONLY: set_status(true, "%.*s can't be written to", STR_ARG(path_base(str_from_c(app.cwd)))); break;
    case UA_NOT_IN_SEARCH: set_status(true, "A search isn't a folder: that works in one"); break;
    case UA_NOT_IN_NETWORK: set_status(true, "Browse Network lists servers: Enter connects to one"); break;   // (F16 step 7e)
    case UA_SEARCH: {   // below the folder shown, or (all) a folder entry's (F9)
        char in[PATH_MAX];
        search_in(act_dir(a, in, sizeof in));
        break;
    }
    case UA_OPEN_LINE: open_at_line(a.index); break;
    case UA_PREFERENCES: prefs_open(); break;
    case UA_CONTAINING: {   // a result's folder, it selected (F9)
        char path[PATH_MAX], dir[PATH_MAX];
        if (a.index >= app.list.n || !entry_path(a.index, path, sizeof path)) break;
        path_parent(dir, sizeof dir, path);
        go_to(dir, app.list.e[a.index].name, 0);
        break;
    }
    case UA_SIDE_MODE: {
        side_refresh();
        state_key("sidebar", SIDE_MODE_NAMES[app.ui.side_mode]);
        break;
    }
    // View's toggles: each remembered, its one key (as UA_VIEW).
    case UA_COLORS: state_key("colors", app.ui.colors ? "on" : "off"); break;
    case UA_MENUBAR: state_key("menubar", app.ui.bar_hidden ? "off" : "on"); break;
    case UA_PERMS_COLS: state_key("perms", app.ui.perms_cols ? "on" : "off"); break;
    case UA_GROUP_COL: state_key("group", app.ui.group_col ? "on" : "off"); break;
    case UA_ZOOM:     // F6.3: as the view, the cursor kept on its row at the refit (ui_refit)
    case UA_VIEW: {   // F6.2: remembered; the loop lays it out before the next frame
        if (a.kind == UA_ZOOM) app.zoomed = true;
        app.icons_reset_armed = true;   // the other size's icons may not fit beside these
        remember_view(a.kind == UA_ZOOM ? FP_ZOOM : FP_VIEW);   // (the folder shown's own, F11)
        break;
    }
    case UA_BACK: hist_go(-1); break;
    case UA_FORWARD: hist_go(1); break;
    case UA_HIDDEN: {
        app.show_hidden = !app.show_hidden;
        state_key("hidden", app.show_hidden ? "on" : "off");   // (F11)
        char keep[PATH_MAX];
        snprintf(keep, sizeof keep, "%.*s", STR_ARG(sel_name()));
        go_to(app.cwd, str_from_c(keep), 0);
        break;
    }
    case UA_SORT: resort(), remember_view(FP_SORT | FP_REV); break;   // (F11)
    case UA_PLACE: {
        char buf[PATH_MAX];
        const char *p = side_path(a.index, buf, sizeof buf);
        const Place *pl = app.ui.side_mode == SIDE_PLACES && a.index >= 0 && a.index < app.places.n ? &app.places.p[a.index] : NULL;
        if (p) go_to(p, STR_EMPTY, 0);
        else if (pl && place_bring(pl)) {}   // (no folder yet: mounted or connected first)
        break;
    }
    case UA_TREE_LEFT:
    case UA_TREE_RIGHT:
    case UA_TREE_TOGGLE: {
        if (a.index < 0 || a.index >= tree.n) break;
        bool toggle = a.kind == UA_TREE_TOGGLE;
        int to = a.kind == UA_TREE_LEFT ? tree_left(&tree, (int)a.index, &toggle) : a.kind == UA_TREE_RIGHT ? tree_right(&tree, (int)a.index, &toggle) : app.ui.side.sel;
        if (!toggle) {
            app.ui.side.sel = to, ui_show_sel(&app.ui.side);
            break;
        }
        TreeRow *r = &tree.row[a.index];
        if (r->open) tree_close(&tree, r->path);
        else tree_open(&tree, r->path);
        tree_remember();   // (F11)
        // The cursor stays on its folder; one hidden by the close goes to the closed folder.
        tree_refresh(false, (int)a.index);
        break;
    }
    case UA_OPEN: a.all ? open_marked() : open_entry(a.index); break;
    case UA_PARENT: go_parent(); break;
    case UA_TERMINAL: {   // all: in the folder entry index (its right-click menu, F8)
        char in[PATH_MAX];
        terminal_in(act_dir(a, in, sizeof in));
        break;
    }
    case UA_HOMEDIR: {
        const char *home = getenv("HOME");
        if (home && home[0]) go_to(home, STR_EMPTY, 0);
        break;
    }
    case UA_DELETE: delete_targets(); break;   // its targets were taken when the question opened
    case UA_TRASH:
    case UA_COPY:
    case UA_CUT:
        if (too_many_marked()) break;
        if (a.kind == UA_TRASH) trash_targets();
        else copy_targets(a.kind == UA_CUT, NULL);
        break;
    case UA_COPY_PATH: {   // all: a sidebar row's folder, or the folder shown's
        char buf[PATH_MAX];
        const char *dir = !a.all ? NULL : a.index == UI_COPY_SHOWN ? here_dir() : side_path(a.index, buf, sizeof buf);
        if (!a.all && !too_many_marked()) path_copy(NULL);
        else if (dir && strcmp(dir, TRASH_URI) == 0) set_status(true, "The Trash has no path to copy");   // (said, as its menu hides the entry: F13 code-health review)
        else if (dir) path_copy(dir);
        break;
    }
    case UA_RENAME: rename_commit(); break;
    case UA_TAB_NEW: tab_open(app.cwd, sel_name(), true); break;
    case UA_TAB_CLOSE: tab_close((int)a.index); break;
    case UA_TAB_SHOW: tab_show((int)a.index); break;
    case UA_TAB_STEP: {
        UiTabs t = tabbar;
        ui_tabs_step(&t, (int)a.index);
        tab_show(t.cur);
        break;
    }
    case UA_TAB_JUMP: {
        UiTabs t = tabbar;
        ui_tabs_jump(&t, (int)a.index);
        tab_show(t.cur);
        break;
    }
    case UA_TAB_MOVE: ui_tabs_move(&tabbar, (int)a.index); break;
    case UA_RESTORE_WINDOWS: restore_others(a.all); break;   // (F11 step 3)
    case UA_TAB_REOPEN:   // (F11) open again, shown
        if (closed_tabs.n == 0) set_status(false, "No closed tab to open again");
        else if (tabbar.n == UI_TABS_MAX) set_status(true, "%d tabs is the most ofx keeps open", UI_TABS_MAX);   // (kept for when one closes)
        else closed_tabs.n--, tab_open(closed_tabs.dir[closed_tabs.n], STR_EMPTY, true);
        break;
    case UA_SHARE: share_pick(a.index); break;   // a server's share picked (F16 step 7a)
    case UA_BRANCH:   // (F12 step 5c) the one checked out: nothing
        // The list is for the folder the menu opened in: one another folder took over meanwhile
        // (the one shown removed, the view gone up into an outer repository) isn't switched by it
        // (review, 2026-11-03).
        if (strcmp(git.br_want, app.cwd) != 0) set_status(true, "The branch list is for another folder: open it again");
        else if (a.index >= 0 && a.index < git.br.n && a.index != git.br.cur && git_ask_switch(git.br.name[a.index]))
            set_status(false, "Switching to %s\xE2\x80\xA6", git.br.name[a.index]);
        break;
    case UA_TAB_OTHERS:   // (F8) from the last, so each place stays right as the ones after it go
        for (int i = tabbar.n - 1; i >= 0; i--)
            if (i != tabbar.cur) tab_close(i);
        break;
    case UA_TAB_BG_ROW: {
        char path[PATH_MAX];
        if (a.index < app.list.n && app.ui.view != UI_VIEW_TRASH && entry_path(a.index, path, sizeof path)) tab_open_bg(path);
        break;
    }
    case UA_TAB_BG_PLACE: {
        char buf[PATH_MAX];
        const char *p = side_path(a.index, buf, sizeof buf);
        if (p) tab_open_bg(p);
        break;
    }
    case UA_TAB_BG_CRUMB:
        if (a.index >= 0 && a.index < app.crumbs.n) {
            char path[PATH_MAX];
            snprintf(path, sizeof path, "%.*s", STR_ARG(ui_crumb_path(&app.crumbs, (int)a.index)));
            tab_open_bg(path);
        }
        break;
    case UA_LOCATION: ui_location_open(&app.ui, str_from_c(here_dir())); break;
    case UA_GO: location_go(); break;
    case UA_LOGIN: case UA_LOGIN_GUEST: case UA_LOGIN_CANCEL: connect_answer(a.kind); break;
    case UA_CRUMB: crumb_go((int)a.index); break;
    case UA_CRUMB_SHIFT: ui_crumbs_shift(&app.crumbs, app.crumb_lo, (int)a.index); break;
    case UA_NEWFOLDER: new_made(false); break;
    case UA_NEWFILE: new_made(true); break;
    case UA_NEW_WINDOW: {   // all: on the folder entry index (F8)
        char on[PATH_MAX];
        window_on(act_dir(a, on, sizeof on));
        break;
    }
    case UA_CLOSE_ALL: close_others(a.all); break;
    case UA_PROPS: props_open(a.index == UI_PROPS_HERE ? app.cwd : a.index == UI_PROPS_CTX ? app.ctx_path : NULL); break;   // (F8)
    case UA_CHMOD: props_chmod((int)a.index); break;
    case UA_OPEN_WITH: ow_open(); break;
    case UA_LAUNCH: ow_launch(a.index, a.all); break;
    case UA_PASTE: {   // all: into the folder entry index (F8)
        char into[PATH_MAX];
        const char *dir = act_dir(a, into, sizeof into);
        if (dir) paste(dir);
        break;
    }
    case UA_MAKE_LINK:
        if (!too_many_marked()) make_link();
        break;
    case UA_FOLLOW: follow_link(a.index); break;
    case UA_FOLDER_DO: folder_do((UiMenuItem)a.index); break;
    case UA_DUPLICATE:
        if (!too_many_marked()) duplicate();
        break;
    case UA_UNDO: undo_last(); break;
    case UA_RELOAD:   // (a search isn't watched; Browse Network asks the network again, F16 step 7e)
        app.reloads_asked++;
        if (app.ui.view == UI_VIEW_SEARCH) search_again();
        else {
            if (app.ui.view == UI_VIEW_NETWORK) browse_start();
            reload();
        }
        break;
    case UA_TRASH_VIEW: go_to(TRASH_URI, STR_EMPTY, 0); break;
    case UA_BROWSE: go_to(NETWORK_URI, STR_EMPTY, 0); break;   // (F16 step 7e)
    case UA_BM_EDIT: case UA_BM_GO: case UA_BM_NAME: case UA_BM_REMOVE: case UA_BM_UP: case UA_BM_DOWN: case UA_BM_RENAME: bmp_act(a); break;
    case UA_ADD_BOOKMARK: {   // all: the folder entry index (F8)
        char bm[PATH_MAX];
        const char *dir = act_dir(a, bm, sizeof bm);
        if (dir) bookmark_add(dir);
        break;
    }
    case UA_BOOKMARK: {
        const Bookmark *b = bm_menu((int)a.index);
        if (b) go_to(b->path, STR_EMPTY, 0);
        break;
    }
    case UA_ANSWER: job_answer((int)a.index, a.all); break;
    case UA_CANCEL_JOB: job_cancel((int)a.index); break;
    case UA_QUIT: app.quit = true; break;
    default: break;
    }
}
