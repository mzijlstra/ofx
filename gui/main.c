// fx: a file explorer, drawn natively on Wayland. One folder at a time, as a list. Clay lays out
// the window, render.c draws it with raylib. docs/plan.md has the plan; ui/ decides what input
// does, fs/ reads the folder, the MIME types, the icon theme and the Omarchy theme.
//
// One Clay unit is one logical pixel.
#include "../base/base.c"
#include "../fs/fs.c"
#include "../ui/ui.c"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Vendored, and included with -isystem so our warning flags do not apply to them.
#define CLAY_IMPLEMENTATION
#include "clay.h"
#include "GLFW/glfw3.h"
#include "render.c"   // includes raylib and font.c
#include "watch.c"
#include "clipboard.c"

// ---- memory ---------------------------------------------------------------------------------

static u8 frame_mem[MiB(1)];       // strings for one frame; reset before each layout
static u8 clay_mem[MiB(8)];        // Clay's own arena, checked against Clay_MinMemorySize
static u8 list_mem[2][MiB(32)];    // the folder shown, and the one being read (a failed read keeps the old)
static u8 data_mem[MiB(4)];        // the MIME database and the user directories, read once
static u8 icon_mem[MiB(16)];       // the icon theme's index and the icon cache; reset on a theme change
static u8 scratch_mem[MiB(1)];
static u8 job_mem[MiB(4)];         // a job's paths
static u8 undo_mem[MiB(4)];        // the undo record's paths
static u8 own_mem[MiB(2)];         // what fx itself copied or cut
static u8 paste_mem[MiB(2)];       // another program's files, read for one paste
static u8 job_buf[JOB_BUF];

static Arena frame, lists[2], data, icona, scratch, joba, undoa, owna, pastea;
static int   watch_delay_ms;   // FX_TEST_WATCH_DELAY_MS, for one test

enum { FONT_PX = 15, ROW_H = 28, ICON_PX = 24, HEADER_H = 40, COLS_H = 26, FOOTER_H = 28, PAD = 12, SIZE_W = 90,
       DATE_W = 100, SIDE_W = 200, SIDE_ROW_H = 28, SIDE_ICON_PX = 20, GAP = 10, ICONS_MAX = 512 };

// ---- state ----------------------------------------------------------------------------------

typedef struct {
    Str        key;       // the MIME type, or a folder's icon name
    Texture2D  tex;       // id 0: no file for it, or it would not load
    char       file[PATH_MAX];
} IconSlot;

static struct {
    char      cwd[PATH_MAX];
    int       cur;          // lists[cur] holds the listing
    Listing   list;
    Str      *mime;         // per entry, filled as rows are drawn; .p NULL until then
    bool      have_list;
    char      error[sizeof(Err)];   // why the folder could not be read
    Ui        ui;           // the list's and sidebar's cursors, find, the sort
    UiPress   press;
    bool      show_hidden;
    Places    places;
    int       place_cur;    // the place holding cwd, -1 for none
    int       side_hover;   // the sidebar row under the pointer, -1 for none
    char      status[600];
    bool      status_bad;
    i64       status_at;
    bool      quit;

    Theme     theme;
    MimeDb    mime_db;
    UserDirs  user_dirs;
    IconDb    icons;
    IconSlot  slot[ICONS_MAX];
    int       nslots;
    StrMap    slot_of;
    int       icon_scale;   // the scale the cache holds icons for
    int       hover;        // the row under the pointer, -1 for none
    int       reloads, themes;

    // The edit line's entry: its name when the line opened, and whether it is a folder just made
    // (Esc keeps "New folder"; the undo removes it whatever it is called by then).
    char      edit_from[NAME_MAX + 1];
    bool      edit_new;
    bool      clip_told;    // "the clipboard stays inside fx" said once
    int       copy_delay_ms;   // FX_TEST_COPY_DELAY_MS
} app = { .hover = -1, .side_hover = -1 };

static UiHistory hist;
static Undo      undo;
static ClipFiles own;      // fx's own copy or cut (own.n 0: none)

static Clay_Color cc(Rgb c)
{
    return (Clay_Color){ c.r, c.g, c.b, 255 };
}

static Clay_String cs(Str s)
{
    return (Clay_String){ .length = (int32_t)s.len, .chars = s.p };
}

static Clay_String cz(const char *z)
{
    return cs(str_from_c(z));
}

static void set_status(bool bad, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void set_status(bool bad, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(app.status, sizeof app.status, fmt, ap);
    va_end(ap);
    app.status_bad = bad;
    app.status_at = now_ms();
}

static char clay_error[256];
static i64  frame_us_max;   // the slowest frame (draw and swap) since a script's last `state`

static i64 now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (i64)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static void on_clay_error(Clay_ErrorData e)
{
    snprintf(clay_error, sizeof clay_error, "clay: %.*s", e.errorText.length, e.errorText.chars);
}

// ---- Wayland app id -------------------------------------------------------------------------
//
// raylib calls glfwDefaultWindowHints() inside InitWindow, which clears any hint set before it,
// and it never sets an app id of its own. The link wraps that call (-Wl,--wrap): the real one
// runs, then the app id goes in, so compositor rules can match class "fx".

void __real_glfwDefaultWindowHints(void);
void __wrap_glfwDefaultWindowHints(void);
void __wrap_glfwDefaultWindowHints(void)
{
    __real_glfwDefaultWindowHints();
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "fx");
}

// ---- the folder -----------------------------------------------------------------------------

// dir/name into out; false when it would not fit (a cut path could name another file).
static bool join(char *out, size_t cap, const char *dir, Str name)
{
    int n = snprintf(out, cap, "%s%s%.*s", dir, strcmp(dir, "/") == 0 ? "" : "/", STR_ARG(name));
    return n >= 0 && (size_t)n < cap;
}

static Str sel_name(void)
{
    return app.have_list && app.ui.list.n > 0 ? app.list.e[app.ui.list.sel].name : STR_EMPTY;
}

// Shows dir, the cursor on the entry called `select` (else on row `fallback`). A move to another
// folder goes in the history, unless it is the history's own (`stepping`). False, with the reason
// in the status line, when dir can't be read: the old listing stays.
static bool go_to_in(const char *dir, Str select, isize fallback, bool stepping)
{
    int next = app.have_list ? 1 - app.cur : app.cur;
    arena_reset(&lists[next]);
    Listing l;
    Err err;
    if (!fs_list(&lists[next], dir, app.show_hidden, app.ui.sort, &l, &err)) {
        if (!app.have_list) snprintf(app.error, sizeof app.error, "%s", err.msg);
        set_status(true, "%s", err.msg);
        return false;
    }
    // The name to select lives in the old listing: find it before that arena is reused.
    isize sel = select.len ? listing_find(&l, select) : -1;
    if (sel < 0) sel = fallback;
    Str *mime = arena_try_new(&lists[next], Str, l.n + 1);
    u8 *mark = arena_try_new(&lists[next], u8, l.n + 1);
    if (!mime || !mark) {
        set_status(true, "%s: too many entries for memory", dir);
        return false;
    }
    bool same = strcmp(dir, app.cwd) == 0;
    // The same folder again: the selection is kept by name.
    isize marked = 0;
    for (isize i = 0; same && app.have_list && app.ui.marked && i < app.list.n; i++) {
        if (!app.ui.mark[i]) continue;
        isize k = listing_find(&l, app.list.e[i].name);
        if (k >= 0 && !mark[k]) mark[k] = 1, marked++;
    }
    // The folder left, with its cursor, for Back: its name is still in the old listing's arena.
    if (!same && app.have_list && !stepping) hist_leave(&hist, app.cwd, sel_name());
    app.cur = next, app.list = l, app.mime = mime, app.have_list = true, app.error[0] = 0;
    ui_mark_set(&app.ui, mark);
    app.ui.marked = marked;
    if (!same) ui_edit_close(&app.ui);
    if (!same) {
        snprintf(app.cwd, sizeof app.cwd, "%s", dir);
        app.status[0] = 0;   // what happened in the last folder
        app.ui.list.top = 0;
        app.place_cur = places_current(&app.places, app.cwd);
        // FX_TEST_WATCH_DELAY_MS widens the gap between the listing and the watch, so
        // tests/gui/watchgap.script can put a change in it.
        if (watch_delay_ms > 0) nanosleep(&(struct timespec){ watch_delay_ms / 1000, (watch_delay_ms % 1000) * 1000000L }, NULL);
        watch_dir(app.cwd);
        // A change between the listing and the watch made no event: list once more.
        atomic_store(&watch.list_ready, true);
        glfwPostEmptyEvent();
        SetWindowTitle(TextFormat("fx: %s", app.cwd));
    }
    ui_set(&app.ui.list, l.n, sel);
    return true;
}

static bool go_to(const char *dir, Str select, isize fallback)
{
    return go_to_in(dir, select, fallback, false);
}

// The folder again (a change on disk): the cursor stays on its name, or on its row when that name
// is gone. A folder that is gone itself gives way to the nearest one above that is there.
static void reload(void)
{
    app.reloads++;
    char keep[NAME_MAX + 1] = "";
    snprintf(keep, sizeof keep, "%.*s", STR_ARG(sel_name()));
    if (go_to(app.cwd, str_from_c(keep), app.ui.list.sel)) return;
    char gone[PATH_MAX], up[PATH_MAX];
    snprintf(gone, sizeof gone, "%s", app.cwd);
    snprintf(up, sizeof up, "%s", app.cwd);
    while (strcmp(up, "/") != 0) {
        // Cut in place: snprintf from up into up would copy a buffer onto itself.
        Str d = path_dir(str_from_c(up));
        if (d.len == 0) d = S("/");
        memmove(up, d.p, (size_t)d.len);
        up[d.len] = 0;
        if (go_to(up, STR_EMPTY, 0)) {
            set_status(true, "%s is gone", gone);
            return;
        }
    }
}

static void go_parent(void)
{
    if (strcmp(app.cwd, "/") == 0) return;
    Str cwd = str_from_c(app.cwd), child = path_base(cwd), parent = path_dir(cwd);
    char child_buf[NAME_MAX + 1], up[PATH_MAX];
    snprintf(child_buf, sizeof child_buf, "%.*s", STR_ARG(child));
    snprintf(up, sizeof up, "%.*s", STR_ARG(parent.len ? parent : S("/")));
    go_to(up, str_from_c(child_buf), 0);
}

static void open_entry(isize i)
{
    const Entry *e = &app.list.e[i];
    char path[PATH_MAX];
    if (!join(path, sizeof path, app.cwd, e->name)) {
        set_status(true, "%.*s: the path is too long", STR_ARG(e->name));
        return;
    }
    if (kind_is_dir(e->kind)) {
        go_to(path, STR_EMPTY, 0);
        return;
    }
    if (e->kind == K_LINK_BROKEN) {
        set_status(true, "%.*s is a broken link", STR_ARG(e->name));
        return;
    }
    Err err;
    if (spawn_open(path, &err)) set_status(false, "opened %.*s", STR_ARG(e->name));
    else set_status(true, "%s", err.msg);
}

// Back (-1) or forward (1) through the history. A folder there that is gone now is dropped and
// the next one tried, with a word in the status line.
static void hist_go(int dir)
{
    UiPlace to;
    char cur[PATH_MAX], cur_sel[NAME_MAX + 1];
    snprintf(cur, sizeof cur, "%s", app.cwd);
    snprintf(cur_sel, sizeof cur_sel, "%.*s", STR_ARG(sel_name()));
    while (hist_step(&hist, dir, cur, str_from_c(cur_sel), &to)) {
        if (go_to_in(to.dir, str_from_c(to.sel), 0, true)) return;
        hist_unstep(&hist, dir);
        set_status(true, "%s is gone: skipped", to.dir);
    }
}

// The listing in a new order, the cursor kept on its entry. The MIME types, cached by row, go.
static void resort(void)
{
    char keep[NAME_MAX + 1];
    snprintf(keep, sizeof keep, "%.*s", STR_ARG(sel_name()));
    fs_sort(&app.list, app.ui.sort);
    memset(app.mime, 0, sizeof *app.mime * (size_t)app.list.n);
    isize sel = listing_find(&app.list, str_from_c(keep));
    ui_set(&app.ui.list, app.list.n, sel >= 0 ? sel : 0);
}

// ---- changing files (F3) ---------------------------------------------------------------------

// The entries an action works on: the marked ones, or the cursor's when none is marked; their
// paths into a, in list order.
static isize targets(Arena *a, char **out, isize max)
{
    isize n = 0;
    for (isize i = 0; app.have_list && i < app.list.n && n < max; i++) {
        if (app.ui.marked ? !app.ui.mark[i] : i != app.ui.list.sel) continue;
        char p[PATH_MAX];
        if (join(p, sizeof p, app.cwd, app.list.e[i].name)) out[n++] = arena_cstr(a, str_from_c(p));
    }
    return n;
}

static const char *items(isize n)
{
    return n == 1 ? "item" : "items";
}

// The job on its worker thread: copy, move or delete for good. The worker asks about a clash
// through run.mu and run.cv; the main loop polls it (job_poll) and answers (job_answer).
static struct {
    Job             j;
    JobItem         item[CLIP_PATHS_MAX];
    pthread_t       thread;
    bool            running;
    bool            reverse;       // an undo's move back: it makes no new undo record
    bool            own_cut;       // a paste of fx's own cut: the clipboard empties after it
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    bool            waiting;       // the worker waits for an answer
    int             answer;        // ANS_*, 0 until given
    bool            all;
    char            clash[PATH_MAX];
    bool            clash_dirs;
    atomic_llong    last_tick;
} run = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER };

static Answer job_ask(Job *j, const char *dst, bool both_dirs, bool *rest)
{
    (void)j;
    pthread_mutex_lock(&run.mu);
    snprintf(run.clash, sizeof run.clash, "%s", dst);
    run.clash_dirs = both_dirs, run.answer = 0, run.waiting = true;
    glfwPostEmptyEvent();
    while (run.answer == 0) pthread_cond_wait(&run.cv, &run.mu);
    Answer a = (Answer)run.answer;
    *rest = run.all;
    run.waiting = false;
    pthread_mutex_unlock(&run.mu);
    return a;
}

// The worker's progress: the window wakes to draw it, ten times a second at most.
static void job_tick_gui(Job *j)
{
    i64 now = now_ms();
    if (atomic_load(&j->phase) == JP_DONE || now - atomic_load(&run.last_tick) >= 100) {
        atomic_store(&run.last_tick, now);
        glfwPostEmptyEvent();
    }
}

static void *job_thread(void *arg)
{
    (void)arg;
    job_run(&run.j);
    return NULL;
}

static void job_answer(int a, bool all)
{
    pthread_mutex_lock(&run.mu);
    if (run.waiting && run.answer == 0) run.answer = a, run.all = all, pthread_cond_signal(&run.cv);
    pthread_mutex_unlock(&run.mu);
}

static void job_cancel(void)
{
    if (!run.running) return;
    atomic_store(&run.j.cancel, true);
    job_answer(ANS_CANCEL, false);
}

// run.item[0..n) as a job of kind, on the worker.
static void job_start(JobKind kind, isize n)
{
    memset(&run.j, 0, sizeof run.j);
    run.j.kind = kind, run.j.item = run.item, run.j.n = n, run.j.buf = job_buf;
    run.j.ask = job_ask, run.j.tick = job_tick_gui, run.j.delay_ms = app.copy_delay_ms;
    run.waiting = false, run.answer = 0;
    if (pthread_create(&run.thread, NULL, job_thread, NULL) != 0) {
        set_status(true, "cannot start the job: %s", strerror(errno));
        return;
    }
    run.running = true;
    app.ui.busy = true;
}

static void job_finish(void)
{
    Job *j = &run.j;
    static const char *const DONE[] = { "Copied", "Moved", "Deleted" }, *const STOPPED[] = { "Copy", "Move", "Delete" };
    isize done = 0;
    for (isize i = 0; i < j->n; i++)
        if (j->item[i].out == OUT_NEW || j->item[i].out == OUT_REPLACED || j->item[i].out == OUT_MERGED) done++;
    char msg[600];
    int k;
    if (atomic_load(&j->cancel)) k = snprintf(msg, sizeof msg, "%s cancelled after %td of %td %s", STOPPED[j->kind], done, j->n, items(j->n));
    else k = snprintf(msg, sizeof msg, "%s %td %s", DONE[j->kind], done, items(done));
    if (k > 0 && j->skipped) k += snprintf(msg + k, sizeof msg - (size_t)k, ", %td skipped", j->skipped);
    if (k > 0 && j->errors) snprintf(msg + k, sizeof msg - (size_t)k, "; %td failed: %s", j->errors, j->err);
    else if (k > 0 && j->note[0] && (size_t)k < sizeof msg) snprintf(msg + k, sizeof msg - (size_t)k, " (%s)", j->note);
    set_status(j->errors > 0, "%s", msg);

    // The undo record: what the job made new (copies) or moved whole; the rest can't go back.
    if (run.reverse || j->kind == JOB_DELETE) {
        undo_clear(&undo);
    } else {
        undo_begin(&undo, j->kind == JOB_COPY ? UNDO_COPY : UNDO_MOVE);
        for (isize i = 0; i < j->n; i++) {
            JobItem *it = &j->item[i];
            if (it->out != OUT_NEW) continue;
            Str d = path_dir(str_from_c(it->dst));
            char dir[PATH_MAX], made[PATH_MAX];
            snprintf(dir, sizeof dir, "%.*s", STR_ARG(d.len ? d : S("/")));
            if (join(made, sizeof made, dir, str_from_c(it->final))) undo_add(&undo, j->kind == JOB_MOVE ? it->src : NULL, made, NULL);
        }
        if (undo.n == 0) undo_clear(&undo);
    }
    if (run.own_cut && !atomic_load(&j->cancel)) own.n = 0, clip_clear_own();
    if (j->kind == JOB_DELETE) ui_mark_clear(&app.ui);
    reload();
}

// Called every loop: true when something about the job changed on screen.
static bool job_poll(void)
{
    if (!run.running) return false;
    pthread_mutex_lock(&run.mu);
    bool ask = run.waiting && run.answer == 0;
    pthread_mutex_unlock(&run.mu);
    if (ask && !app.ui.asking) app.ui.asking = true;
    if (atomic_load(&run.j.phase) != JP_DONE) return true;
    pthread_join(run.thread, NULL);
    run.running = false;
    app.ui.busy = false, app.ui.asking = false;
    job_finish();
    return true;
}

static void trash_targets(void)
{
    char *paths[CLIP_PATHS_MAX];
    arena_reset(&pastea);
    isize n = targets(&pastea, paths, CLIP_PATHS_MAX), ok = 0;
    Err err, first = { "" };
    undo_begin(&undo, UNDO_TRASH);
    for (isize i = 0; i < n; i++) {
        Trashed t;
        if (trash_put(paths[i], &t, &err)) undo_add(&undo, t.from, t.file, t.info), ok++;
        else if (!first.msg[0]) first = err;
    }
    if (ok == 0) undo_clear(&undo);
    if (ok == n && n == 1) set_status(false, "Trashed %s", path_base(str_from_c(paths[0])).p);
    else if (ok == n) set_status(false, "Trashed %td items", n);
    else set_status(true, "Trashed %td of %td: %s", ok, n, first.msg);
    ui_mark_clear(&app.ui);
    reload();
}

static void delete_targets(void)
{
    if (run.running) { set_status(true, "a job is still running: wait, or Esc to cancel it"); return; }
    char *paths[CLIP_PATHS_MAX];
    arena_reset(&joba);
    isize n = targets(&joba, paths, CLIP_PATHS_MAX);
    for (isize i = 0; i < n; i++) run.item[i] = (JobItem){ .src = paths[i] };
    run.reverse = false, run.own_cut = false;
    if (n) job_start(JOB_DELETE, n);
}

static void copy_targets(bool cut)
{
    arena_reset(&owna);
    own.n = targets(&owna, own.path, CLIP_PATHS_MAX);
    own.cut = cut;
    if (own.n == 0) return;
    bool sys = clip_set(&own);
    char name[NAME_MAX + 1];
    snprintf(name, sizeof name, "%.*s", STR_ARG(path_base(str_from_c(own.path[0]))));
    const char *note = !sys && !app.clip_told ? " (no system clipboard here: it stays inside fx)" : "";
    if (!sys) app.clip_told = true;
    if (own.n == 1) set_status(false, "%s %s%s", cut ? "Cut" : "Copied", name, note);
    else set_status(false, "%s %td items%s", cut ? "Cut" : "Copied", own.n, note);
}

// Whether a and b are the same folder, resolved.
static bool same_dir(const char *a, const char *b)
{
    char x[PATH_MAX], y[PATH_MAX];
    return realpath(a, x) && realpath(b, y) && strcmp(x, y) == 0;
}

static void paste(void)
{
    if (run.running) { set_status(true, "a job is still running: wait, or Esc to cancel it"); return; }
    static ClipFiles theirs;
    arena_reset(&pastea);
    ClipFrom from = clip_get(&pastea, &theirs);
    ClipFiles *c = from == CLIP_THEIRS ? &theirs : from == CLIP_OURS || !clip.ok ? &own : NULL;
    if (!c || c->n == 0) { set_status(true, "The clipboard holds no files"); return; }
    arena_reset(&joba);
    isize n = 0, here = 0, self = 0, gone = 0;
    for (isize i = 0; i < c->n && n < CLIP_PATHS_MAX; i++) {
        const char *src = c->path[i];
        Str base = path_base(str_from_c(src)), sd = path_dir(str_from_c(src));
        char sdir[PATH_MAX], name[NAME_MAX + 1], dst[PATH_MAX];
        snprintf(sdir, sizeof sdir, "%.*s", STR_ARG(sd.len ? sd : S("/")));
        struct stat st;
        if (lstat(src, &st) != 0) {
            gone++;
            continue;
        }
        if (S_ISDIR(st.st_mode) && into_itself(src, app.cwd)) {
            self++;
            continue;
        }
        if (same_dir(sdir, app.cwd)) {
            if (c->cut) {
                here++;
                continue;
            }
            if (!name_free(app.cwd, base, S_ISDIR(st.st_mode), true, true, name, sizeof name)) continue;
        } else {
            snprintf(name, sizeof name, "%.*s", STR_ARG(base));
        }
        if (!join(dst, sizeof dst, app.cwd, str_from_c(name))) continue;
        run.item[n++] = (JobItem){ .src = arena_cstr(&joba, str_from_c(src)), .dst = arena_cstr(&joba, str_from_c(dst)) };
    }
    if (n == 0) {
        if (self) set_status(true, "Can't paste a folder into itself");
        else if (here) set_status(true, "Already here");
        else set_status(true, "Nothing to paste: %td %s gone", gone, gone == 1 ? "is" : "are");
        return;
    }
    run.reverse = false, run.own_cut = c == &own && c->cut;
    job_start(c->cut ? JOB_MOVE : JOB_COPY, n);
    if (self) set_status(true, "Skipped %td: a folder can't go into itself", self);
}

static void rename_commit(void)
{
    Str to = { app.ui.edit, app.ui.edit_len }, from = str_from_c(app.edit_from);
    if (str_eq(to, from)) { ui_edit_close(&app.ui); return; }
    Err err;
    if (!op_rename(app.cwd, from, to, &err)) { set_status(true, "%s", err.msg); return; }
    char a[PATH_MAX], b[PATH_MAX], keep[NAME_MAX + 1];
    snprintf(keep, sizeof keep, "%.*s", STR_ARG(to));
    if (join(a, sizeof a, app.cwd, from) && join(b, sizeof b, app.cwd, to)) {
        // A folder just made stays a "new folder" for undo, whatever its name now.
        undo_begin(&undo, app.edit_new ? UNDO_MKDIR : UNDO_RENAME);
        undo_add(&undo, app.edit_new ? NULL : a, b, NULL);
    }
    ui_edit_close(&app.ui);
    set_status(false, "Renamed %s to %s", app.edit_from, keep);
    go_to(app.cwd, str_from_c(keep), app.ui.list.sel);
}

static void new_folder(void)
{
    char made[NAME_MAX + 1], p[PATH_MAX];
    Err err;
    if (!op_mkdir(app.cwd, S("New folder"), made, sizeof made, &err)) { set_status(true, "%s", err.msg); return; }
    if (join(p, sizeof p, app.cwd, str_from_c(made))) undo_begin(&undo, UNDO_MKDIR), undo_add(&undo, NULL, p, NULL);
    ui_mark_clear(&app.ui);
    go_to(app.cwd, str_from_c(made), app.ui.list.sel);
    ui_edit_open(&app.ui, str_from_c(made));
    snprintf(app.edit_from, sizeof app.edit_from, "%s", made);
    app.edit_new = true;
}

static void undo_last(void)
{
    if (run.running) { set_status(true, "a job is still running: wait, or Esc to cancel it"); return; }
    char what[64];
    undo_what(&undo, what, sizeof what);
    Err err;
    if (undo_is_job(&undo)) {
        // A move across drives goes back as a move.
        if (!undo_check(&undo, &err)) { undo_clear(&undo), set_status(true, "%s", err.msg); return; }
        arena_reset(&joba);
        for (isize i = 0; i < undo.n; i++)
            run.item[i] = (JobItem){ .src = arena_cstr(&joba, str_from_c(undo.it[i].to)), .dst = arena_cstr(&joba, str_from_c(undo.it[i].from)) };
        isize n = undo.n;
        undo_clear(&undo);
        run.reverse = true, run.own_cut = false;
        job_start(JOB_MOVE, n);
        return;
    }
    // What comes back here gets the cursor: a renamed, trashed or moved entry.
    char back[NAME_MAX + 1] = "";
    if (undo.n && undo.it[0].from && (undo.kind == UNDO_RENAME || undo.kind == UNDO_TRASH || undo.kind == UNDO_MOVE)) {
        Str f = str_from_c(undo.it[0].from), d = path_dir(f);
        char dir[PATH_MAX];
        snprintf(dir, sizeof dir, "%.*s", STR_ARG(d.len ? d : S("/")));
        if (same_dir(dir, app.cwd)) snprintf(back, sizeof back, "%.*s", STR_ARG(path_base(f)));
    }
    isize done;
    if (undo_do(&undo, &done, &err)) set_status(false, "Undid the %s", what);
    else set_status(true, "%s", err.msg);
    if (back[0]) go_to(app.cwd, str_from_c(back), app.ui.list.sel);
    else reload();
}

static void act(UiAction a)
{
    Err err;
    switch (a.kind) {
    case UA_BACK: hist_go(-1); break;
    case UA_FORWARD: hist_go(1); break;
    case UA_HIDDEN: {
        app.show_hidden = !app.show_hidden;
        char keep[NAME_MAX + 1];
        snprintf(keep, sizeof keep, "%.*s", STR_ARG(sel_name()));
        go_to(app.cwd, str_from_c(keep), 0);
        break;
    }
    case UA_SORT: resort(); break;
    case UA_PLACE:
        if (a.index >= 0 && a.index < app.places.n) go_to(app.places.p[a.index].path, STR_EMPTY, 0);
        break;
    case UA_OPEN: open_entry(a.index); break;
    case UA_PARENT: go_parent(); break;
    case UA_TERMINAL:
        if (spawn_terminal(app.cwd, &err)) set_status(false, "terminal in %s", app.cwd);
        else set_status(true, "%s", err.msg);
        break;
    case UA_HOMEDIR: {
        const char *home = getenv("HOME");
        if (home && home[0]) go_to(home, STR_EMPTY, 0);
        break;
    }
    case UA_TRASH:
    case UA_DELETE:
    case UA_COPY:
    case UA_CUT:
        // One action holds CLIP_PATHS_MAX paths: more would quietly do part of the selection.
        if (app.ui.marked > CLIP_PATHS_MAX) {
            set_status(true, "%td selected: fx takes at most %d at a time", app.ui.marked, CLIP_PATHS_MAX);
            break;
        }
        if (a.kind == UA_TRASH) trash_targets();
        else if (a.kind == UA_DELETE) delete_targets();
        else copy_targets(a.kind == UA_CUT);
        break;
    case UA_RENAME: rename_commit(); break;
    case UA_NEWFOLDER: new_folder(); break;
    case UA_PASTE: paste(); break;
    case UA_UNDO: undo_last(); break;
    case UA_ANSWER: job_answer((int)a.index, a.all); break;
    case UA_CANCEL_JOB: job_cancel(); break;
    default: break;
    }
}

// ---- icons ----------------------------------------------------------------------------------

static int window_scale(void)
{
    float s = GetWindowScaleDPI().x;
    return s >= 1.5f ? (int)lroundf(s) : 1;
}

// Every texture dropped, and the icon theme read again: a theme change, or a new scale.
static void icons_reset(void)
{
    for (int i = 0; i < app.nslots; i++)
        if (app.slot[i].tex.id) UnloadTexture(app.slot[i].tex);
    app.nslots = 0;
    arena_reset(&icona);
    icon_load(&icona, &app.icons, str_from_c(app.theme.icons));
    strmap_init(&app.slot_of, &icona, ICONS_MAX);
    app.icon_scale = window_scale();
}

static Str entry_mime(isize i)
{
    if (!app.mime[i].p) app.mime[i] = mime_of(&scratch, &app.mime_db, &app.list.e[i]);
    return app.mime[i];
}

// The texture under key, from the first of names the theme has, loaded the first time it is
// needed; NULL when the cache is full. names NULL: they are the MIME type's (key).
static IconSlot *icon_for(Str key, const Str *given, isize ngiven)
{
    int k;
    if (strmap_get(&app.slot_of, key, &k)) return &app.slot[k];
    if (app.nslots == ICONS_MAX) return NULL;
    ArenaMark mark = arena_mark(&scratch);
    Str names[6];
    isize n = given ? ngiven : mime_icon_names(&scratch, &app.mime_db, key, names);
    IconSlot *s = &app.slot[app.nslots];
    *s = (IconSlot){ .key = { arena_cstr(&icona, key), key.len } };
    const char *file = icon_find(&scratch, &app.icons, given ? given : names, n, ICON_PX, app.icon_scale);
    if (file) {
        snprintf(s->file, sizeof s->file, "%s", file);
        s->tex = LoadTexture(file);
        if (s->tex.id) SetTextureFilter(s->tex, TEXTURE_FILTER_BILINEAR);
    }
    arena_restore(&scratch, mark);
    strmap_put(&app.slot_of, s->key, app.nslots);
    return &app.slot[app.nslots++];
}

// The texture for entry i.
static IconSlot *entry_icon(isize i)
{
    const Entry *e = &app.list.e[i];
    if (!kind_is_dir(e->kind)) return icon_for(entry_mime(i), NULL, 0);
    Str names[2];
    isize n = 0;
    char path[PATH_MAX];
    Str special = join(path, sizeof path, app.cwd, e->name) ? user_dir_icon(&app.user_dirs, path) : STR_EMPTY;
    if (special.len) names[n++] = special;
    names[n++] = S("folder");
    return icon_for(names[0], names, n);
}

static IconSlot *place_icon(int i)
{
    Str names[4];
    isize n = place_icon_names(&app.places.p[i], &app.user_dirs, names);
    return icon_for(names[0], names, n);
}

// ---- layout ---------------------------------------------------------------------------------

static void text(Clay_String s, Clay_Color color)
{
    CLAY_TEXT(s, { .textColor = color, .fontSize = FONT_PX, .wrapMode = CLAY_TEXT_WRAP_NONE });
}

static float advance(u32 cp);

static float str_width(Str s)
{
    return ui_width(s, advance);
}

static float advance(u32 cp)
{
    return font_advance(cp, FONT_PX);
}

// s cut to `width` (ui_fit), with "…" where it was cut; into the frame arena.
static Clay_String fit(Str s, float width, bool keep_end)
{
    Str dots = S("\xE2\x80\xA6");
    isize from, to;
    if (ui_fit(s, width, str_width(dots), advance, keep_end, &from, &to)) return cs(s);
    if (keep_end) return cs(str_from_c(arena_sprintf(&frame, "%.*s%.*s", STR_ARG(dots), (int)(to - from), s.p + from)));
    return cs(str_from_c(arena_sprintf(&frame, "%.*s%.*s", (int)(to - from), s.p + from, STR_ARG(dots))));
}

static int list_rows(int h)
{
    int r = (h - HEADER_H - COLS_H - FOOTER_H) / ROW_H;
    return r > 0 ? r : 0;
}

static int side_rows(int h)
{
    int r = (h - HEADER_H - FOOTER_H - PAD) / SIDE_ROW_H;
    return r > 0 ? r : 0;
}

static void icon_box(Clay_ElementId id, IconSlot *icon, int px)
{
    CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(px), CLAY_SIZING_FIXED(px) } },
               .image = { .imageData = icon && icon->tex.id ? &icon->tex : NULL } }) {}
}

static void layout_side(float h)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("side"), { .layout = { .sizing = { CLAY_SIZING_FIXED(SIDE_W), CLAY_SIZING_FIXED(h) },
                                        .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = { 0, 0, PAD / 2, 0 } },
                            .backgroundColor = cc(t->bg_light) }) {
        UiList *sl = &app.ui.side;
        for (int r = 0; r < sl->rows && sl->top + r < sl->n; r++) {
            int i = (int)(sl->top + r);
            bool cursor = app.ui.side_focus && i == sl->sel, here = i == app.place_cur, hover = r == app.side_hover;
            Clay_Color bg = cursor ? cc(t->selection) : hover ? cc(t->bg) : cc(t->bg_light);
            CLAY(CLAY_IDI("side-row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(SIDE_ROW_H) },
                                                        .padding = { PAD, PAD, 0, 0 }, .childGap = 8,
                                                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                            .backgroundColor = bg,
                                            .border = { .color = cc(t->accent), .width = { .left = cursor ? 3 : 0 } } }) {
                icon_box(CLAY_IDI("side-icon", r), place_icon(i), SIDE_ICON_PX);
                Str label = str_from_c(app.places.p[i].label);
                text(fit(label, SIDE_W - 2 * PAD - SIDE_ICON_PX - 8, false), here ? cc(t->accent) : cc(t->fg));
            }
        }
    }
}

// The column headers: Name, Size, Modified, the sorted one with an arrow; a click sorts.
static void layout_cols(void)
{
    Theme *t = &app.theme;
    static const char *const NAMES[SORT_KEYS] = { "Name", "Size", "Modified" };
    static const char *const IDS[SORT_KEYS] = { "col-name", "col-size", "col-date" };
    CLAY(CLAY_ID("cols"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(COLS_H) },
                                        .padding = { PAD + ICON_PX + GAP, PAD, 0, 0 }, .childGap = GAP,
                                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                            .border = { .color = cc(t->bg_light), .width = { .bottom = 1 } } }) {
        for (int k = 0; k < SORT_KEYS; k++) {
            bool on = app.ui.sort.key == (SortKey)k;
            Clay_String label = cz(arena_sprintf(&frame, "%s%s", NAMES[k], !on ? "" : app.ui.sort.reverse ? " \xE2\x96\xBC" : " \xE2\x96\xB2"));
            Clay_Sizing size = k == SORT_NAME ? (Clay_Sizing){ CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }
                             : (Clay_Sizing){ CLAY_SIZING_FIXED(k == SORT_SIZE ? SIZE_W : DATE_W), CLAY_SIZING_GROW(0) };
            CLAY(Clay_GetElementId(cz(IDS[k])), { .layout = { .sizing = size, .childAlignment = { .x = k == SORT_NAME ? CLAY_ALIGN_X_LEFT : CLAY_ALIGN_X_RIGHT,
                                                                                                    .y = CLAY_ALIGN_Y_CENTER } } }) {
                text(label, on ? cc(t->fg) : cc(t->fg_dim));
            }
        }
    }
}

// a mixed into b: t of a.
static Clay_Color mix(Rgb a, Rgb b, float t)
{
    return (Clay_Color){ a.r * t + b.r * (1 - t), a.g * t + b.g * (1 - t), a.b * t + b.b * (1 - t), 255 };
}

// The edit line in place of the name: the text, and a caret at its place.
static void layout_edit(Theme *t)
{
    Str ed = { app.ui.edit, app.ui.edit_len };
    CLAY(CLAY_ID("edit"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ROW_H - 6) }, .padding = { 4, 4, 0, 0 },
                                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                            .backgroundColor = cc(t->bg), .border = { .color = cc(t->accent), .width = CLAY_BORDER_ALL(1) } }) {
        if (app.ui.caret > 0) text(cs(str_cut(ed, 0, app.ui.caret)), cc(t->fg));
        CLAY(CLAY_ID("edit-caret"), { .layout = { .sizing = { CLAY_SIZING_FIXED(2), CLAY_SIZING_FIXED(FONT_PX + 2) } },
                                      .backgroundColor = cc(t->accent) }) {}
        if (app.ui.caret < app.ui.edit_len) text(cs(str_cut(ed, app.ui.caret, ed.len)), cc(t->fg));
    }
}

static void layout_row(int r, isize i, float w, i64 now)
{
    const Entry *e = &app.list.e[i];
    Theme *t = &app.theme;
    bool sel = i == app.ui.list.sel, hover = r == app.hover, marked = ui_marked(&app.ui, i);
    // Marked rows: the accent mixed into the background; the cursor keeps the selection colour.
    Clay_Color bg = sel ? cc(t->selection) : marked ? mix(t->accent, t->bg, 0.28f) : hover ? cc(t->bg_light) : cc(t->bg);
    IconSlot *icon = entry_icon(i);
    char size[32] = "", when[32] = "";
    if (!kind_is_dir(e->kind) && e->kind != K_LINK_BROKEN && e->kind != K_OTHER) ui_size(size, sizeof size, e->size);
    if (e->mtime) ui_when(when, sizeof when, e->mtime, now);
    bool link = e->kind == K_LINK_DIR || e->kind == K_LINK_FILE || e->kind == K_LINK_BROKEN;
    float name_w = w - SIDE_W - 2 * PAD - ICON_PX - 3 * GAP - SIZE_W - DATE_W - (link ? 16 : 0);
    CLAY(CLAY_IDI("row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ROW_H) },
                                           .padding = { PAD, PAD, 0, 0 }, .childGap = GAP,
                                           .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                               .backgroundColor = bg,
                               .border = { .color = cc(t->accent), .width = { .left = sel && !app.ui.side_focus ? 3 : 0 } } }) {
        icon_box(CLAY_IDI("icon", r), icon, ICON_PX);
        CLAY(CLAY_IDI("name", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 6 } }) {
            if (sel && app.ui.editing) {
                layout_edit(t);
            } else {
                text(fit(e->name, name_w, false), e->kind == K_LINK_BROKEN ? cc(t->red) : marked ? cc(t->accent) : cc(t->fg));
                if (link) text(cz("\xE2\x86\x92"), cc(t->fg_dim));   // →
            }
        }
        CLAY(CLAY_IDI("size", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(SIZE_W), CLAY_SIZING_FIT(0) },
                                                .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
            text(cz(arena_sprintf(&frame, "%s", size)), cc(t->fg_dim));
        }
        CLAY(CLAY_IDI("date", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(DATE_W), CLAY_SIZING_FIT(0) },
                                                .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
            text(cz(arena_sprintf(&frame, "%s", when)), cc(t->fg_dim));
        }
    }
}

static Clay_RenderCommandArray layout(int w, int h)
{
    arena_reset(&frame);
    arena_reset(&scratch);
    Theme *t = &app.theme;
    Clay_SetLayoutDimensions((Clay_Dimensions){ (float)w, (float)h });
    Clay_BeginLayout();
    i64 now = (i64)time(NULL);
    CLAY(CLAY_ID("root"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                            .backgroundColor = cc(t->bg) }) {
        char count[96];
        int k = app.list.hidden ? snprintf(count, sizeof count, "%td items, %td hidden", app.list.n, app.list.hidden)
                                : snprintf(count, sizeof count, "%td item%s", app.list.n, app.list.n == 1 ? "" : "s");
        if (app.ui.marked && k > 0) snprintf(count + k, sizeof count - (size_t)k, ", %td selected", app.ui.marked);
        Str cnt = str_from_c(arena_sprintf(&frame, "%s", count));
        CLAY(CLAY_ID("header"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(HEADER_H) },
                                              .padding = { PAD, PAD, 0, 0 }, .childGap = 16,
                                              .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                  .backgroundColor = cc(t->bg_light) }) {
            CLAY(CLAY_ID("path"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
                text(fit(str_from_c(app.cwd), (float)w - 2 * PAD - 16 - str_width(cnt), true), cc(t->accent));
            }
            text(cs(cnt), cc(t->fg_dim));
        }
        float body_h = (float)(h - HEADER_H - FOOTER_H);
        CLAY(CLAY_ID("body"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(body_h) } } }) {
        layout_side(body_h);
        CLAY(CLAY_ID("main"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
        layout_cols();
        CLAY(CLAY_ID("list"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                                .clip = { .vertical = true } }) {
            if (!app.have_list) {
                CLAY(CLAY_ID("error"), { .layout = { .padding = { PAD, PAD, PAD, PAD } } }) { text(cz(app.error), cc(t->red)); }
            } else if (app.list.n == 0) {
                CLAY(CLAY_ID("empty"), { .layout = { .padding = { PAD, PAD, PAD, PAD } } }) {
                    text(cz(app.list.hidden ? "Only hidden files" : "Empty folder"), cc(t->fg_dim));
                }
            }
            for (int r = 0; app.have_list && r < app.ui.list.rows && app.ui.list.top + r < app.list.n; r++)
                layout_row(r, app.ui.list.top + r, (float)w, now);
        }
        }
        }
        CLAY(CLAY_ID("footer"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(FOOTER_H) },
                                              .padding = { PAD, PAD, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                  .backgroundColor = cc(t->bg_light) }) {
            if (run.running && app.ui.asking) {
                Str name = path_base(str_from_c(run.clash));
                text(fit(str_from_c(arena_sprintf(&frame, "%.*s exists: s skip \xC2\xB7 r %s \xC2\xB7 k keep both \xC2\xB7 "
                                                          "capitals for all the rest \xC2\xB7 Esc cancel",
                                                  STR_ARG(name), run.clash_dirs ? "merge" : "replace")),
                         (float)w - 2 * PAD, false),
                     cc(t->accent));
            } else if (app.ui.confirming) {
                text(cz(arena_sprintf(&frame, "Delete %td %s for good? This can't be undone. Y/n", app.ui.confirm_n, items(app.ui.confirm_n))),
                     cc(t->red));
            } else if (app.ui.editing) {
                text(cz(app.edit_new ? "New folder: Enter keeps the name \xC2\xB7 Esc keeps \"New folder\""
                                     : "Rename: Enter renames \xC2\xB7 Esc cancels"),
                     cc(t->fg_dim));
            } else if (run.running) {
                Job *j = &run.j;
                static const char *const VERB[] = { "Copying", "Moving", "Deleting" };
                char done[32], total[32];
                ui_size(done, sizeof done, atomic_load(&j->bytes_done));
                ui_size(total, sizeof total, atomic_load(&j->bytes_total));
                long long fd = atomic_load(&j->files_done), ft = atomic_load(&j->files_total);
                const char *line = atomic_load(&j->phase) == JP_SCAN ? arena_sprintf(&frame, "%s: counting\xE2\x80\xA6 \xC2\xB7 Esc cancels", VERB[j->kind])
                                 : atomic_load(&j->bytes_total) > 0
                                     ? arena_sprintf(&frame, "%s %lld of %lld files, %s of %s \xC2\xB7 Esc cancels", VERB[j->kind], fd, ft, done, total)
                                     : arena_sprintf(&frame, "%s %lld of %lld \xC2\xB7 Esc cancels", VERB[j->kind], fd, ft);
                text(fit(str_from_c(line), (float)w - 2 * PAD, false), cc(t->fg));
            } else if (app.ui.finding) {
                // The find line: its text, red when it matches nothing, and a caret.
                Str f = { app.ui.find, app.ui.find_len };
                text(cz("Find: "), cc(t->fg_dim));
                text(fit(f, (float)w - 2 * PAD - 80, true), app.ui.find_miss ? cc(t->red) : cc(t->fg));
                CLAY(CLAY_ID("caret"), { .layout = { .sizing = { CLAY_SIZING_FIXED(2), CLAY_SIZING_FIXED(FONT_PX + 2) } },
                                         .backgroundColor = cc(t->accent) }) {}
            } else if (app.status[0]) {
                text(fit(str_from_c(app.status), (float)w - 2 * PAD, false), app.status_bad ? cc(t->red) : cc(t->fg));
            } else {
                text(fit(S("Enter open \xC2\xB7 Backspace up \xC2\xB7 Space select \xC2\xB7 F2 rename \xC2\xB7 Del trash \xC2\xB7 "
                           "Ctrl+C X V \xC2\xB7 Ctrl+Z undo \xC2\xB7 / find \xC2\xB7 . hidden \xC2\xB7 s sort \xC2\xB7 Tab places \xC2\xB7 "
                           "t terminal"),
                         (float)w - 2 * PAD, false),
                     cc(t->fg_dim));
            }
        }
    }
    return Clay_EndLayout(0);   // deltaTime drives transitions, which we do not use
}

static void draw(void)
{
    font_set_dpi(GetWindowScaleDPI().x);
    if (window_scale() != app.icon_scale) icons_reset();
    font_frame_begin();
    Clay_RenderCommandArray cmds = layout(GetScreenWidth(), GetScreenHeight());
    render_draw(cmds, cc(app.theme.bg));
}

// ---- input ----------------------------------------------------------------------------------

// Shortcuts and the keys without text are read in GLFW's key callback, chained in front of
// raylib's; letters come from the character callback, so the layout and Shift are the
// compositor's (`G`, `~`). The pointer's callbacks are chained the same way.
static GLFWkeyfun         raylib_key;
static GLFWcharfun        raylib_char;
static GLFWmousebuttonfun raylib_button;
static bool               debug_input;   // FX_GUI_DEBUG: log every key and button event

// Keys and characters in the order they came (find needs `/`, text, then Enter).
static struct {
    UiEvent ev[UI_EVENTS_MAX];
    int     n;
} evq;

static int mouse_backs, mouse_forwards;

static void ev_push(UiEvent e)
{
    if (evq.n < UI_EVENTS_MAX) evq.ev[evq.n++] = e;
}

// A key and its modifiers as ui/ names it; UK_NONE for the rest (letters come as characters).
static UiKey ui_key_of(int key, int mods)
{
    int m = mods & (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT | GLFW_MOD_ALT | GLFW_MOD_SUPER);
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_F) return UK_FIND;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_H) return UK_HIDDEN;
    if (m == GLFW_MOD_ALT && key == GLFW_KEY_LEFT) return UK_BACK;
    if (m == GLFW_MOD_ALT && key == GLFW_KEY_RIGHT) return UK_FORWARD;
    if (m == GLFW_MOD_CONTROL) {
        switch (key) {
        case GLFW_KEY_A: return UK_ALL;
        case GLFW_KEY_C: return UK_COPY;
        case GLFW_KEY_X: return UK_CUT;
        case GLFW_KEY_V: return UK_PASTE;
        case GLFW_KEY_Z: return UK_UNDO;
        default: break;
        }
    }
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_N) return UK_NEWFOLDER;
    if (key == GLFW_KEY_DELETE || key == GLFW_KEY_KP_DECIMAL) {
        if (key == GLFW_KEY_KP_DECIMAL) return UK_NONE;   // the keypad's . types a dot
        return m == GLFW_MOD_SHIFT ? UK_SHIFT_DELETE : m == 0 ? UK_DELETE : UK_NONE;
    }
    if (m & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER)) return UK_NONE;
    switch (key) {
    case GLFW_KEY_UP: return UK_UP;
    case GLFW_KEY_DOWN: return UK_DOWN;
    case GLFW_KEY_PAGE_UP: return UK_PAGEUP;
    case GLFW_KEY_PAGE_DOWN: return UK_PAGEDOWN;
    case GLFW_KEY_HOME: return UK_HOME;
    case GLFW_KEY_END: return UK_END;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER: return UK_ENTER;
    case GLFW_KEY_RIGHT: return UK_RIGHT;
    case GLFW_KEY_LEFT: return UK_LEFT;
    case GLFW_KEY_BACKSPACE: return UK_BACKSPACE;
    case GLFW_KEY_ESCAPE: return UK_ESC;
    case GLFW_KEY_TAB: return m == 0 ? UK_TAB : UK_NONE;
    case GLFW_KEY_F2: return m == 0 ? UK_F2 : UK_NONE;
    default: return UK_NONE;
    }
}

static void on_key(GLFWwindow *w, int key, int scancode, int action, int mods)
{
    if (debug_input) fprintf(stderr, "fx: key %d action %d mods %#x\n", key, action, mods);
    if (action == GLFW_PRESS && key == GLFW_KEY_Q && (mods & GLFW_MOD_CONTROL)) app.quit = true;
    UiKey k = action != GLFW_RELEASE ? ui_key_of(key, mods) : UK_NONE;
    if (k) ev_push((UiEvent){ k, 0 });
    if (raylib_key) raylib_key(w, key, scancode, action, mods);
}

static void on_char(GLFWwindow *w, unsigned int cp)
{
    if (debug_input) fprintf(stderr, "fx: char %#x\n", cp);
    ev_push((UiEvent){ UK_NONE, cp });
    if (raylib_char) raylib_char(w, cp);
}

// The mouse's back and forward (thumb) buttons walk the history. Linux reports them as BTN_SIDE
// and BTN_EXTRA (GLFW buttons 4 and 5) on most mice, BTN_BACK and BTN_FORWARD (7 and 6) on some
// (uml-viewer's finding).
static void on_button(GLFWwindow *w, int button, int action, int mods)
{
    if (debug_input) fprintf(stderr, "fx: mouse button %d action %d mods %#x\n", button + 1, action, mods);
    if (action == GLFW_PRESS && (button == GLFW_MOUSE_BUTTON_4 || button == GLFW_MOUSE_BUTTON_7)) mouse_backs++;
    if (action == GLFW_PRESS && (button == GLFW_MOUSE_BUTTON_5 || button == GLFW_MOUSE_BUTTON_6)) mouse_forwards++;
    if (raylib_button) raylib_button(w, button, action, mods);
}

// What is under a point of the last frame drawn: a list row, a sidebar row, or a column header
// (each -1 when not).
typedef struct {
    int row, side, header;
} Hit;

static bool in_box(Clay_ElementId id, float x, float y)
{
    Clay_ElementData d = Clay_GetElementData(id);
    return d.found && x >= d.boundingBox.x && x < d.boundingBox.x + d.boundingBox.width && y >= d.boundingBox.y &&
           y < d.boundingBox.y + d.boundingBox.height;
}

static Hit hit_at(float x, float y)
{
    Hit h = { -1, -1, -1 };
    for (int r = 0; r < app.ui.list.rows && h.row < 0; r++)
        if (in_box(CLAY_IDI("row", r), x, y)) h.row = r;
    for (int r = 0; r < app.ui.side.rows && h.side < 0; r++)
        if (in_box(CLAY_IDI("side-row", r), x, y)) h.side = r;
    static const char *const IDS[SORT_KEYS] = { "col-name", "col-size", "col-date" };
    for (int k = 0; k < SORT_KEYS && h.header < 0; k++)
        if (in_box(Clay_GetElementId(cz(IDS[k])), x, y)) h.header = k;
    return h;
}

// A left press at a point, into in: a list row's goes through the double-click timing (not with
// Ctrl or Shift held, which mark rows: UI_MOD_*).
static void press_at(UiInput *in, float x, float y, int mods)
{
    Hit h = hit_at(x, y);
    if (h.row >= 0) {
        if (!mods && ui_press(&app.press, h.row, now_ms()) == 2) in->dclick = h.row;
        else in->click = h.row, in->click_mods = mods;
    }
    in->side_click = h.side;
    in->header = h.header;
}

static float wheel_left;   // a touchpad's part of a notch, carried to the next frame

// The user's input since the last frame. *seen: anything that could change what is on screen.
static UiInput input_real(bool *seen)
{
    UiInput in = ui_input_none();
    memcpy(in.ev, evq.ev, sizeof *evq.ev * (size_t)evq.n);
    in.nev = evq.n;
    *seen = evq.n > 0 || IsWindowResized();
    evq.n = 0;
    while (GetKeyPressed() != 0) *seen = true;
    while (GetCharPressed() != 0) {}
    Vector2 m = GetMousePosition(), d = GetMouseDelta();
    wheel_left += GetMouseWheelMove();
    int notches = (int)wheel_left;
    wheel_left -= (float)notches;
    if (m.x >= SIDE_W) in.wheel = notches;
    else if (notches) app.ui.side.top = ui_clamp(app.ui.side.top - notches, 0, ui_max_top(&app.ui.side)), *seen = true;
    if (d.x != 0 || d.y != 0) {
        Hit h = hit_at(m.x, m.y);
        *seen |= h.row != app.hover || h.side != app.side_hover;
        app.hover = h.row, app.side_hover = h.side;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int mods = (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ? UI_MOD_CTRL : 0) |
                   (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? UI_MOD_SHIFT : 0);
        press_at(&in, m.x, m.y, mods);
    }
    in.backs = mouse_backs, mouse_backs = 0;
    in.forwards = mouse_forwards, mouse_forwards = 0;
    *seen |= in.wheel != 0 || in.click >= 0 || in.dclick >= 0 || in.side_click >= 0 || in.header >= 0 || in.backs > 0 ||
             in.forwards > 0;
    return in;
}

// For find: the name of entry i.
static Str list_name(void *ctx, isize i)
{
    (void)ctx;
    return app.list.e[i].name;
}

// ---- signals --------------------------------------------------------------------------------
//
// SIGTERM and SIGINT end the loop like Ctrl+Q, so the process exits through main: CloseWindow
// runs, and so does LeakSanitizer, which a default SIGTERM death skips. Both signals are blocked
// in every thread, and this one takes them with sigwait, then wakes the event wait.

static atomic_bool quit_signal;
static sigset_t    quit_signals;
static void *volatile planted;   // FX_TEST_LEAK's block, dropped so nothing points at it

static void *signal_waiter(void *arg)
{
    (void)arg;
    int sig;
    sigwait(&quit_signals, &sig);
    atomic_store(&quit_signal, true);
    glfwPostEmptyEvent();
    return NULL;
}

static void theme_apply(void)
{
    app.theme = theme_load(&scratch);
    icons_reset();
}

#include "script.c"

static void usage(void)
{
    fprintf(stderr, "usage: fx [FOLDER]\n"
                    "  A file explorer: the folder (default: the current one) as a list.\n"
                    "  Enter, Right, l open; Backspace, Left, h up; j k, arrows, PgUp PgDn, Home End, g G move;\n"
                    "  Alt+Left/Right back and forward; / or Ctrl+F find, n N again; . or Ctrl+H hidden files;\n"
                    "  s sort by the next column, S reverse; Tab the places; t terminal here; ~ home;\n"
                    "  Space select, Ctrl+A all; F2 rename; Ctrl+Shift+N new folder; Delete trash, Shift+Delete for good;\n"
                    "  Ctrl+C, Ctrl+X, Ctrl+V copy, cut, paste; Ctrl+Z undo; Ctrl+Q quit.\n");
}

int main(int argc, char **argv)
{
    const char *start = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) return usage(), 0;
        if (argv[i][0] == '-' || start) return usage(), 2;
        start = argv[i];
    }
    char dir[PATH_MAX];
    if (!realpath(start ? start : ".", dir)) {
        fprintf(stderr, "fx: %s: %s\n", start ? start : ".", strerror(errno));
        return 1;
    }

    // Before any thread exists (GLFW and Mesa start some), so all of them inherit the mask.
    sigemptyset(&quit_signals);
    sigaddset(&quit_signals, SIGTERM);
    sigaddset(&quit_signals, SIGINT);
    pthread_sigmask(SIG_BLOCK, &quit_signals, NULL);
    spawn_init();
    // FX_TEST_LEAK=1 leaks one block on purpose: the control that shows LeakSanitizer reports
    // from this binary at all.
    const char *delay = getenv("FX_TEST_WATCH_DELAY_MS");
    watch_delay_ms = delay ? atoi(delay) : 0;
    if (watch_delay_ms < 0 || watch_delay_ms > 1000) watch_delay_ms = 0;   // a window must never stall long
    // FX_TEST_COPY_DELAY_MS: a sleep per copied block, so a test sees the progress (capped short:
    // a job never stalls the window, but a test must not run long).
    const char *cdelay = getenv("FX_TEST_COPY_DELAY_MS");
    app.copy_delay_ms = cdelay ? atoi(cdelay) : 0;
    if (app.copy_delay_ms < 0 || app.copy_delay_ms > 500) app.copy_delay_ms = 0;
    // The test guard: with FX_GUI_ROOT set (the GUI tests), no write outside it.
    ops_guard(getenv("FX_GUI_ROOT"));
    const char *leak = getenv("FX_TEST_LEAK");
    if (leak && leak[0]) planted = malloc(4096), planted = NULL;

    arena_init(&frame, "frame", frame_mem, sizeof frame_mem);
    arena_init(&lists[0], "list", list_mem[0], sizeof list_mem[0]);
    arena_init(&lists[1], "list", list_mem[1], sizeof list_mem[1]);
    arena_init(&data, "data", data_mem, sizeof data_mem);
    arena_init(&icona, "icons", icon_mem, sizeof icon_mem);
    arena_init(&scratch, "scratch", scratch_mem, sizeof scratch_mem);
    arena_init(&joba, "job", job_mem, sizeof job_mem);
    arena_init(&undoa, "undo", undo_mem, sizeof undo_mem);
    arena_init(&owna, "clipboard", own_mem, sizeof own_mem);
    arena_init(&pastea, "paste", paste_mem, sizeof paste_mem);
    undo.arena = &undoa;
    uint32_t need = Clay_MinMemorySize();
    if ((size_t)need > sizeof clay_mem) {
        fprintf(stderr, "fx: Clay needs %llu bytes, clay_mem has %zu\n", (unsigned long long)need, sizeof clay_mem);
        return 1;
    }

    SetTraceLogLevel(LOG_WARNING);
    // GLFW would draw window decorations through libdecor, whose GTK plugin loads GTK 3, Pango
    // and fontconfig into the process. Hyprland tiles without a title bar, so go without it.
    glfwInitHint(GLFW_WAYLAND_LIBDECOR, GLFW_WAYLAND_DISABLE_LIBDECOR);
    // HIGHDPI: without it raylib turns GLFW's framebuffer scaling off on Wayland, so a scale-2
    // output gets a 1x buffer stretched by the compositor.
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    // FX_TEST_SIZE=WxH: the test compositor's kiosk shell rejects a first frame larger than its
    // output (tools/gui/weston.sh), so a test opens at the size it will get.
    int win_w = 1000, win_h = 700;
    const char *size = getenv("FX_TEST_SIZE");
    if (size && sscanf(size, "%dx%d", &win_w, &win_h) != 2) win_w = 1000, win_h = 700;
    InitWindow(win_w, win_h, TextFormat("fx: %s", dir));
    if (!IsWindowReady()) {
        fprintf(stderr, "fx: cannot open a window\n");
        return 1;
    }
    SetExitKey(KEY_NULL);   // Ctrl+Q quits; Esc is free for later
    const char *dbg = getenv("FX_GUI_DEBUG");
    debug_input = dbg && dbg[0];
    GLFWwindow *gw = glfwGetCurrentContext();
    raylib_key = glfwSetKeyCallback(gw, on_key);
    raylib_char = glfwSetCharCallback(gw, on_char);
    raylib_button = glfwSetMouseButtonCallback(gw, on_button);
    pthread_t waiter;
    pthread_create(&waiter, NULL, signal_waiter, NULL);
    pthread_detach(waiter);
    if (!font_init()) {
        fprintf(stderr, "fx: fontconfig finds no usable sans-serif font\n");
        CloseWindow();
        return 1;
    }
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(sizeof clay_mem, clay_mem);
    Clay_Initialize(arena, (Clay_Dimensions){ (float)GetScreenWidth(), (float)GetScreenHeight() },
                    (Clay_ErrorHandler){ .errorHandlerFunction = on_clay_error });
    Clay_SetMeasureTextFunction(render_measure_text, NULL);

    mime_load(&data, &app.mime_db);
    user_dirs_load(&data, &app.user_dirs);
    theme_apply();
    if (!watch_start()) set_status(true, "inotify is unavailable: no live refresh");
    clip_init();   // false: the clipboard stays inside fx
    char bm[PATH_MAX];
    places_load(&scratch, &app.places, bookmarks_path(bm, sizeof bm), "/proc/self/mountinfo");
    ui_set(&app.ui.side, app.places.n, 0);
    ui_resize(&app.ui.list, list_rows(GetScreenHeight()));
    ui_resize(&app.ui.side, side_rows(GetScreenHeight()));
    app.press.row = -1;
    go_to(dir, STR_EMPTY, 0);
    if (!app.have_list) snprintf(app.cwd, sizeof app.cwd, "%s", dir);

    const char *script_path = getenv("FX_GUI_SCRIPT");
    bool scripted = script_path && script_path[0];
    if (scripted && !script_open(script_path)) {
        CloseWindow();
        return 1;
    }

    // raylib is built with SUPPORT_CUSTOM_FRAME_CONTROL, so EndDrawing neither swaps nor polls nor
    // sleeps. The loop draws only when something changed: drawing every time never idles on
    // Wayland, because each swap earns a frame-done event that ends the wait at once. The wait
    // happens inside PollInputEvents (event waiting on). A script runs a frame per loop without
    // waiting, and draws every one.
    if (!scripted) EnableEventWaiting();
    bool dirty = true;
    while (!app.quit && !atomic_load(&quit_signal) && !WindowShouldClose()) {
        if (dirty || scripted) {
            i64 t0 = now_us();
            draw();
            i64 ta = now_us();
            if (scripted) script_after_draw();
            i64 tb = now_us();
            SwapScreenBuffer();
            i64 us = now_us() - t0 - (tb - ta);
            if (us > frame_us_max) frame_us_max = us;
            dirty = font_frame_end();   // out of atlas: the next frame starts from an empty one
        }
        PollInputEvents();   // waits for input, or for watch.c's wake-up
        if (atomic_exchange(&watch.theme_ready, false)) theme_apply(), app.themes++, dirty = true;
        if (atomic_exchange(&watch.list_ready, false)) reload(), dirty = true;
        dirty |= job_poll();
        int rows = list_rows(GetScreenHeight()), srows = side_rows(GetScreenHeight());
        if (rows != app.ui.list.rows) ui_resize(&app.ui.list, rows), dirty = true;
        if (srows != app.ui.side.rows) ui_resize(&app.ui.side, srows), dirty = true;
        bool seen;
        UiInput in = input_real(&seen);
        if (scripted) {
            in = ui_input_none();   // the user's input does not reach a scripted window (Ctrl+Q still quits)
            if (!script_input(&in)) break;
            seen = true;
        }
        bool was_editing = app.ui.editing;
        UiAction a = ui_handle(&app.ui, (UiNames){ list_name, NULL }, &in);
        // F2 opened the edit line: on the cursor's entry, as it is named now.
        if (!was_editing && app.ui.editing) snprintf(app.edit_from, sizeof app.edit_from, "%.*s", STR_ARG(sel_name())), app.edit_new = false;
        if (a.kind != UA_NONE) act(a);
        dirty |= seen;
    }
    if (run.running) {
        job_cancel();
        pthread_join(run.thread, NULL);
    }
    clip_quit();
    for (int i = 0; i < app.nslots; i++)
        if (app.slot[i].tex.id) UnloadTexture(app.slot[i].tex);
    CloseWindow();
    return scripted && script.failed ? 1 : 0;
}
