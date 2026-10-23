// ofx: a file explorer, drawn natively on Wayland. One folder at a time, as a list. Clay lays out
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
#include <sys/statvfs.h>
#include <time.h>

// Vendored, and included with -isystem so our warning flags do not apply to them.
#define CLAY_IMPLEMENTATION
#include "clay.h"
#include "GLFW/glfw3.h"
#include "render.c"   // includes raylib and font.c

// Wakes the main loop's event wait from another thread; never once the window is closing, when
// GLFW is being torn down (a cancelled job's part file removed at quit woke the watcher then).
static pthread_mutex_t wake_mu = PTHREAD_MUTEX_INITIALIZER;
static bool            wake_off;

static void wake_main(void)
{
    pthread_mutex_lock(&wake_mu);
    if (!wake_off) glfwPostEmptyEvent();
    pthread_mutex_unlock(&wake_mu);
}

#include "watch.c"
#include "clipboard.c"
#include "counts.c"

// ---- memory ---------------------------------------------------------------------------------

static u8 frame_mem[MiB(1)];       // strings for one frame; reset before each layout
static u8 clay_mem[MiB(8)];        // Clay's own arena, checked against Clay_MinMemorySize
static u8 list_mem[2][MiB(32)];    // the folder shown, and the one being read (a failed read keeps the old)
static u8 data_mem[MiB(4)];        // the MIME database and the user directories, read once
static u8 icon_mem[MiB(16)];       // the icon theme's index and the icon cache; reset on a theme change
static u8 scratch_mem[MiB(1)];
static u8 undo_mem[MiB(4)];        // the undo record's paths
static u8 own_mem[MiB(2)];         // what ofx itself copied or cut
static u8 paste_mem[MiB(2)];       // another program's files, read for one paste

static Arena frame, lists[2], data, icona, scratch, undoa, owna, pastea;
static int   watch_delay_ms;   // OFX_TEST_WATCH_DELAY_MS, for one test

enum { BAR_BTN_W = 34, BAR_BTN_H = 30, BAR_GAP = 4, ARROW_W = 22, CRUMB_PAD = 10, CRUMB_ICON_PX = 16 };
enum { TAB_H = 30, TAB_X_W = 22, JOB_ROW_H = 40, JOB_WAIT_H = 22 };
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
    isize    *count;        // per entry, a folder's item count (counts.c): COUNT_UNKNOWN until asked
    bool      have_list;
    char      error[sizeof(Err)];   // why the folder could not be read
    Ui        ui;           // the list's and sidebar's cursors, find, the sort
    UiPress   press;
    bool      show_hidden;
    Places    places;
    u8        place_head[PLACES_MAX];   // 1 for a heading row (app.ui.side_head)
    int       place_cur;    // the place holding cwd, -1 for none
    i64       cwd_avail;    // the free space on cwd's drive, -1 unknown
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
    bool      clip_told;    // "the clipboard stays inside ofx" said once
    UiCrumbs  crumbs;       // the top bar's path buttons
    int       crumb_lo, crumb_hi;   // the ones shown in the last frame
    bool      crumb_arrows;
    int       modal_hover;  // the modal panel's button under the pointer (UI_MODAL_*), 0 for none
    UiBar     bar_hover;    // the top bar button under the pointer
    int       crumb_hover;
    int       tab_hover, tab_x_hover;   // the tab, and the tab's ×, under the pointer; -1 for none
    int       copy_delay_ms;   // OFX_TEST_COPY_DELAY_MS
    int       jobx_hover;      // the job row whose × is under the pointer, -1 for none
    char      text_copied[PATH_MAX];   // the last text an edit line put on the clipboard (F5.2)
} app = { .hover = -1, .side_hover = -1, .tab_hover = -1, .tab_x_hover = -1, .jobx_hover = -1 };

static UiHistory hist;

// ---- tabs (F5) --------------------------------------------------------------------------------
//
// The tab on screen lives in app, hist and app.crumbs; a tab left is kept in its slot by name, and
// listed again when it is shown (docs/plan.md, "F5 design").

typedef struct {
    bool      used;
    char      dir[PATH_MAX];
    char      sel[NAME_MAX + 1];   // the cursor's entry
    isize     row, top;            // its row (when the name is gone) and the scroll
    UiHistory hist;
    UiCrumbs  crumbs;
    char      last[UI_FIND_MAX];   // the find line's last text, for n and N
    int       last_len;
    char      marks[KiB(32)];      // the selection: names, each ending in a NUL
    isize     marks_len;
    bool      marks_cut;           // more than fitted: the selection was left out
} Tab;

static Tab    tabs[UI_TABS_MAX];
static UiTabs tabbar;

// What is above the list: the top bar, and the tab bar over the files from two tabs on.
static int top_h(void)
{
    return HEADER_H + (tabbar.n >= 2 ? TAB_H : 0);
}
static Undo      undo;
static ClipFiles own;      // ofx's own copy or cut (own.n 0: none)
// Bumped by every copy of ours, files or text: a cut's paste empties the clipboard after it only
// if nothing was copied meanwhile (reviewer-2, F5.2 review).
static u32       own_gen;

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
// runs, then the app id goes in, so compositor rules can match class "ofx".

void __real_glfwDefaultWindowHints(void);
void __wrap_glfwDefaultWindowHints(void);
void __wrap_glfwDefaultWindowHints(void)
{
    __real_glfwDefaultWindowHints();
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "ofx");
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
    isize *count = arena_try_new(&lists[next], isize, l.n + 1);
    if (!mime || !mark || !count) {
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
    for (isize i = 0; i < l.n; i++) count[i] = COUNT_UNKNOWN;
    app.count = count;
    count_new_listing();
    ui_mark_set(&app.ui, mark);
    app.ui.marked = marked;
    if (!same) ui_edit_close(&app.ui);
    if (!same) {
        snprintf(app.cwd, sizeof app.cwd, "%s", dir);
        ui_crumbs_set(&app.crumbs, app.cwd, getenv("HOME"));
        app.status[0] = 0;   // what happened in the last folder
        app.ui.list.top = 0;
        app.place_cur = places_current(&app.places, app.cwd);
        // OFX_TEST_WATCH_DELAY_MS widens the gap between the listing and the watch, so
        // tests/gui/watchgap.script can put a change in it.
        if (watch_delay_ms > 0) nanosleep(&(struct timespec){ watch_delay_ms / 1000, (watch_delay_ms % 1000) * 1000000L }, NULL);
        watch_dir(app.cwd);
        // A change between the listing and the watch made no event: list once more.
        atomic_store(&watch.list_ready, true);
        glfwPostEmptyEvent();
        SetWindowTitle(TextFormat("ofx: %s", app.cwd));
    }
    ui_set(&app.ui.list, l.n, sel);
    // The drives' space moves with what changes on them; read it with every listing.
    struct statvfs v;
    app.cwd_avail = statvfs(app.cwd, &v) == 0 ? (i64)v.f_bavail * (i64)v.f_frsize : -1;
    places_space(&app.places);
    return true;
}

// The places read again (at the start, a drive mounted or gone): the sidebar's cursor stays on its
// place, found by path and kind, else on the first row that isn't a heading.
static void places_reload(void)
{
    Place keep = { 0 };
    if (app.places.n) keep = app.places.p[app.ui.side.sel];
    char bm[PATH_MAX];
    places_load(&scratch, &app.places, bookmarks_path(bm, sizeof bm), mountinfo_path());
    places_space(&app.places);
    isize sel = 0;
    for (int i = 0; i < app.places.n; i++) {
        app.place_head[i] = app.places.p[i].kind == PL_HEAD;
        if (keep.kind != PL_HEAD && app.places.p[i].kind == keep.kind && strcmp(app.places.p[i].path, keep.path) == 0) sel = i;
    }
    app.ui.side_head = app.place_head;
    isize top = app.ui.side.top;
    ui_set(&app.ui.side, app.places.n, sel);
    app.ui.side.top = ui_clamp(top, 0, ui_max_top(&app.ui.side));
    ui_side_settle(&app.ui, 1);
    app.place_cur = places_current(&app.places, app.cwd);
}

static bool go_to(const char *dir, Str select, isize fallback)
{
    return go_to_in(dir, select, fallback, false);
}

static void go_up_from(const char *from);

// The folder again (a change on disk): the cursor stays on its name, or on its row when that name
// is gone. A folder that is gone itself gives way to the nearest one above that is there.
static void reload(void)
{
    app.reloads++;
    char keep[NAME_MAX + 1] = "";
    snprintf(keep, sizeof keep, "%.*s", STR_ARG(sel_name()));
    if (go_to(app.cwd, str_from_c(keep), app.ui.list.sel)) return;
    go_up_from(app.cwd);
}

// Folder `from` is gone: the nearest folder above it that is there, with a word in the status line.
static void go_up_from(const char *from)
{
    char gone[PATH_MAX], up[PATH_MAX];
    snprintf(gone, sizeof gone, "%s", from);
    snprintf(up, sizeof up, "%s", from);
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
    // The counts are by row too: asked again in the new order.
    for (isize i = 0; i < app.list.n; i++) app.count[i] = COUNT_UNKNOWN;
    count_new_listing();
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

#include "jobs.c"

static void trash_targets(void)
{
    char *paths[CLIP_PATHS_MAX];
    arena_reset(&pastea);
    isize n = targets(&pastea, paths, CLIP_PATHS_MAX), ok = 0;
    for (isize i = 0; i < n; i++)
        if (jobs_refuse(paths[i])) return;
    Err err, first = { "" };
    undo_gen++;
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

// What the delete question asks about, taken when it opens: a selection or a cursor that changes
// while it is up (a delete job finishing in another tab, the cursor's file removed from outside)
// must not change what Yes deletes (reviewer-2, F5 review, 2026-10-21).
static struct {
    char *path[CLIP_PATHS_MAX];
    isize n;
} doomed;

static u8    doomed_mem[MiB(2)];
static Arena doomeda;

// The question just opened (and maybe was answered in the same frame): its targets taken now, and
// its count made theirs; none when it may delete nothing (then Yes deletes nothing).
static void delete_ask(void)
{
    doomed.n = 0;
    if (app.ui.marked > CLIP_PATHS_MAX) {
        app.ui.confirming = false;
        set_status(true, "%td selected: ofx takes at most %d at a time", app.ui.marked, CLIP_PATHS_MAX);
        return;
    }
    arena_reset(&doomeda);
    doomed.n = targets(&doomeda, doomed.path, CLIP_PATHS_MAX);
    app.ui.confirm_n = doomed.n;
    if (doomed.n == 0) app.ui.confirming = false;
}

static void delete_targets(void)
{
    isize n = doomed.n;
    doomed.n = 0;
    JobRec *r = n ? job_new() : NULL;
    if (!r) return;
    for (isize i = 0; i < n; i++) r->item[i] = (JobItem){ .src = arena_cstr(&r->arena, str_from_c(doomed.path[i])) };
    job_submit(r, JOB_DELETE, n);
}

static void copy_targets(bool cut)
{
    own_gen++;
    arena_reset(&owna);
    own.n = targets(&owna, own.path, CLIP_PATHS_MAX);
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

// Whether a and b are the same folder, resolved.
static bool same_dir(const char *a, const char *b)
{
    char x[PATH_MAX], y[PATH_MAX];
    return realpath(a, x) && realpath(b, y) && strcmp(x, y) == 0;
}

static void paste(void)
{
    static ClipFiles theirs;
    arena_reset(&pastea);
    ClipFrom from = clip_get(&pastea, &theirs);
    ClipFiles *c = from == CLIP_THEIRS ? &theirs : from == CLIP_OURS || !clip.ok ? &own : NULL;
    if (!c || c->n == 0) { set_status(true, "The clipboard holds no files"); return; }
    JobRec *r = job_new();
    if (!r) return;
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
        r->item[n++] = (JobItem){ .src = arena_cstr(&r->arena, str_from_c(src)), .dst = arena_cstr(&r->arena, str_from_c(dst)) };
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

// An edit line's Ctrl+C or Ctrl+X (F5.2): its selection onto the clipboard as text, in place of
// whatever was there, our files too. Without data-control (weston) GLFW's clipboard holds it.
static void text_copy(Str t)
{
    own_gen++;
    snprintf(app.text_copied, sizeof app.text_copied, "%.*s", STR_ARG(t));
    if (!clip_set_text(t)) SetClipboardText(app.text_copied);
    own.n = 0;
}

// The clipboard's text for an edit line's Ctrl+V, into pastea: through data-control where there
// is one; else our own files' paths as text when we hold some, or GLFW's text.
static Str text_paste(void)
{
    arena_reset(&pastea);
    Str t;
    if (clip_get_text(&pastea, &t)) return t;
    if (own.n > 0) return clip_payload(&pastea, &own, CLIP_FMT_TEXT);
    const char *g = GetClipboardText();
    return g ? str_from_c(arena_cstr(&pastea, str_from_c(g))) : STR_EMPTY;
}

static void rename_commit(void)
{
    Str to = { app.ui.edit, app.ui.edit_len }, from = str_from_c(app.edit_from);
    if (str_eq(to, from)) { ui_edit_close(&app.ui); return; }
    char was[PATH_MAX], will[PATH_MAX];
    if (join(was, sizeof was, app.cwd, from) && jobs_refuse(was)) return;
    if (join(will, sizeof will, app.cwd, to) && jobs_refuse(will)) return;
    Err err;
    if (!op_rename(app.cwd, from, to, &err)) { set_status(true, "%s", err.msg); return; }
    char a[PATH_MAX], b[PATH_MAX], keep[NAME_MAX + 1];
    snprintf(keep, sizeof keep, "%.*s", STR_ARG(to));
    if (join(a, sizeof a, app.cwd, from) && join(b, sizeof b, app.cwd, to)) {
        // A folder just made stays a "new folder" for undo, whatever its name now.
        undo_gen++;
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
    if (join(p, sizeof p, app.cwd, str_from_c(made))) undo_gen++, undo_begin(&undo, UNDO_MKDIR), undo_add(&undo, NULL, p, NULL);
    ui_mark_clear(&app.ui);
    go_to(app.cwd, str_from_c(made), app.ui.list.sel);
    ui_edit_open(&app.ui, str_from_c(made));
    snprintf(app.edit_from, sizeof app.edit_from, "%s", made);
    app.edit_new = true;
}

static void undo_last(void)
{
    char what[64];
    undo_what(&undo, what, sizeof what);
    for (isize i = 0; i < undo.n; i++)
        if ((undo.it[i].from && jobs_refuse(undo.it[i].from)) || (undo.it[i].to && jobs_refuse(undo.it[i].to))) return;
    undo_gen++;
    Err err;
    if (undo_is_job(&undo)) {
        // A move across drives goes back as a move.
        if (!undo_check(&undo, &err)) { undo_clear(&undo), set_status(true, "%s", err.msg); return; }
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
    snprintf(t->sel, sizeof t->sel, "%.*s", STR_ARG(sel_name()));
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
    app.ui.finding = false;
    ui_mark_clear(&app.ui);   // the same folder in two tabs: this tab's marks, not the other's
    if (!go_to_in(t->dir, str_from_c(t->sel), t->row, true)) {
        // Gone: the nearest folder above. There but unreadable: the same, with its error kept.
        char why[sizeof app.status];
        snprintf(why, sizeof why, "%s", app.status);
        struct stat st;
        bool there = stat(t->dir, &st) == 0;
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
    ui_crumbs_set(&t->crumbs, t->dir, getenv("HOME"));
    ui_tabs_open(&tabbar, slot, show);
    if (show) tab_load();
    else set_status(false, "Opened %s in a new tab", path_base(str_from_c(t->dir)).p);
}

// A middle click's folder, in a background tab; a file isn't one.
static void tab_open_bg(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        set_status(true, "%s isn't a folder", path_base(str_from_c(path)).p);
        return;
    }
    tab_open(path, STR_EMPTY, false);
}

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
    int slot = ui_tabs_close(&tabbar, at);
    if (slot >= 0) tabs[slot].used = false;
    if (shown) tab_load();
}

// The location line's Enter: there, if it is a folder; else the line stays open with the reason.
static void location_go(void)
{
    Str to;
    char path[PATH_MAX];
    if (!ui_location(&scratch, (Str){ app.ui.edit, app.ui.edit_len }, app.cwd, getenv("HOME"), &to) || to.len >= PATH_MAX) {
        set_status(true, "Not a folder: %.*s", app.ui.edit_len, app.ui.edit);
        return;
    }
    snprintf(path, sizeof path, "%.*s", STR_ARG(to));
    struct stat st;
    if (stat(path, &st) != 0) {
        set_status(true, "%s: %s", path, strerror(errno));
        return;
    }
    // A file: its folder, the cursor on it, as Nemo does (Michael, 2026-10-21).
    Str select = STR_EMPTY;
    char dir[PATH_MAX];
    if (!S_ISDIR(st.st_mode)) {
        Str d = path_dir(str_from_c(path));
        select = path_base(str_from_c(path));
        snprintf(dir, sizeof dir, "%.*s", STR_ARG(d.len ? d : S("/")));
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
    case UA_DELETE: delete_targets(); break;   // its targets were taken when the question opened
    case UA_TRASH:
    case UA_COPY:
    case UA_CUT:
        // One action holds CLIP_PATHS_MAX paths: more would quietly do part of the selection.
        if (app.ui.marked > CLIP_PATHS_MAX) {
            set_status(true, "%td selected: ofx takes at most %d at a time", app.ui.marked, CLIP_PATHS_MAX);
            break;
        }
        if (a.kind == UA_TRASH) trash_targets();
        else copy_targets(a.kind == UA_CUT);
        break;
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
    case UA_TAB_BG_ROW: {
        char path[PATH_MAX];
        if (a.index < app.list.n && join(path, sizeof path, app.cwd, app.list.e[a.index].name)) tab_open_bg(path);
        break;
    }
    case UA_TAB_BG_PLACE:
        if (a.index >= 0 && a.index < app.places.n) tab_open_bg(app.places.p[a.index].path);
        break;
    case UA_TAB_BG_CRUMB:
        if (a.index >= 0 && a.index < app.crumbs.n) {
            char path[PATH_MAX];
            snprintf(path, sizeof path, "%.*s", STR_ARG(ui_crumb_path(&app.crumbs, (int)a.index)));
            tab_open_bg(path);
        }
        break;
    case UA_LOCATION: ui_location_open(&app.ui, str_from_c(app.cwd)); break;
    case UA_GO: location_go(); break;
    case UA_CRUMB: crumb_go((int)a.index); break;
    case UA_CRUMB_SHIFT: ui_crumbs_shift(&app.crumbs, app.crumb_lo, (int)a.index); break;
    case UA_NEWFOLDER: new_folder(); break;
    case UA_PASTE: paste(); break;
    case UA_UNDO: undo_last(); break;
    case UA_ANSWER: job_answer((int)a.index, a.all); break;
    case UA_CANCEL_JOB: job_cancel((int)a.index); break;
    case UA_QUIT: app.quit = true; break;
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

// The job rows' strip under the files: a row per running job, a line for those waiting.
static int jobs_h(void)
{
    return jobs.nrun * JOB_ROW_H + (jobs.nwait ? JOB_WAIT_H : 0);
}

static int list_rows(int h)
{
    int r = (h - top_h() - COLS_H - FOOTER_H - jobs_h()) / ROW_H;
    return r > 0 ? r : 0;
}

static int side_rows(int h)
{
    int r = (h - HEADER_H - FOOTER_H - PAD) / SIDE_ROW_H;   // the tab bar isn't over the sidebar
    return r > 0 ? r : 0;
}

static void icon_box(Clay_ElementId id, IconSlot *icon, int px)
{
    CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(px), CLAY_SIZING_FIXED(px) } },
               .image = { .imageData = icon && icon->tex.id ? &icon->tex : NULL } }) {}
}

// a mixed into b: t of a.
static Clay_Color mix(Rgb a, Rgb b, float t)
{
    return (Clay_Color){ a.r * t + b.r * (1 - t), a.g * t + b.g * (1 - t), a.b * t + b.b * (1 - t), 255 };
}

// A drive's fullness: a thin bar under its name, the used part in the accent (red from 90% on).
static void drive_bar(int r, const Place *p, float w)
{
    Theme *t = &app.theme;
    float used = p->size > 0 ? 1.0f - (float)p->avail / (float)p->size : 0;
    if (used < 0) used = 0;
    if (used > 1) used = 1;
    CLAY(CLAY_IDI("side-bar", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(3) } },
                                    .backgroundColor = mix(t->fg_dim, t->bg_light, 0.3f) }) {
        CLAY(CLAY_IDI("side-used", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(w * used), CLAY_SIZING_GROW(0) } },
                                         .backgroundColor = used >= 0.9f ? cc(t->red) : cc(t->accent) }) {}
    }
}

// The places mode (F6): the two headings, the places under each; a drive with its free space on
// the right and its fullness under its name.
static void layout_side(float h)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("side"), { .layout = { .sizing = { CLAY_SIZING_FIXED(SIDE_W), CLAY_SIZING_FIXED(h) },
                                        .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = { 0, 0, PAD / 2, 0 } },
                            .backgroundColor = cc(t->bg_light) }) {
        UiList *sl = &app.ui.side;
        for (int r = 0; r < sl->rows && sl->top + r < sl->n; r++) {
            int i = (int)(sl->top + r);
            const Place *p = &app.places.p[i];
            if (p->kind == PL_HEAD) {
                CLAY(CLAY_IDI("side-row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(SIDE_ROW_H) },
                                                            .padding = { PAD, PAD, 6, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM } } }) {
                    text(cz(p->label), cc(t->fg_dim));
                }
                continue;
            }
            bool cursor = app.ui.side_focus && i == sl->sel, here = i == app.place_cur, hover = r == app.side_hover;
            bool drive = place_is_drive(p->kind) && p->size > 0;
            Clay_Color bg = cursor ? cc(t->selection) : hover ? cc(t->bg) : cc(t->bg_light);
            char avail[32] = "";
            if (drive) ui_size(avail, sizeof avail, p->avail);
            Str free_s = str_from_c(arena_sprintf(&frame, "%s", avail));
            float room = SIDE_W - 2 * PAD - SIDE_ICON_PX - 8 - (drive ? str_width(free_s) + 8 : 0);
            CLAY(CLAY_IDI("side-row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(SIDE_ROW_H) },
                                                        .padding = { PAD, PAD, 0, 0 }, .childGap = 8,
                                                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                            .backgroundColor = bg,
                                            .border = { .color = cc(t->accent), .width = { .left = cursor ? 3 : 0 } } }) {
                icon_box(CLAY_IDI("side-icon", r), place_icon(i), SIDE_ICON_PX);
                CLAY(CLAY_IDI("side-text", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                                             .childGap = 3 } }) {
                    CLAY(CLAY_IDI("side-line", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 8 } }) {
                        text(fit(str_from_c(p->label), room, false), here ? cc(t->accent) : cc(t->fg));
                        if (drive) {
                            CLAY(CLAY_IDI("side-gap", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } } }) {}
                            text(cs(free_s), cc(t->fg_dim));
                        }
                    }
                    if (drive) drive_bar(r, p, SIDE_W - 2 * PAD - SIDE_ICON_PX - 8);
                }
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

// A run of the edit line's text: at most `most` wide (cut with "…" from the end, or from its
// start with keep_end), on the accent half mixed in when selected (the row under a rename has the
// selection colour already).
static void edit_run(Clay_ElementId sel_id, Str s, bool sel, float most, bool keep_end)
{
    Theme *t = &app.theme;
    if (!s.len || most < 1) return;
    Clay_String txt = fit(s, most, keep_end);
    if (!sel) { text(txt, cc(t->fg)); return; }
    CLAY(sel_id, { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(FONT_PX + 6) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                   .backgroundColor = mix(t->accent, t->bg, 0.45f) }) {
        text(txt, cc(t->fg));
    }
}

// The edit line's text in `room`: the selection marked (F5.2) and the caret at its place, kept in
// view (the text after it gets half the room at most, the ends cut with "…").
static void edit_text(const char *id, float room)
{
    Theme *t = &app.theme;
    Str ed = { app.ui.edit, app.ui.edit_len };
    int lo, hi, c = app.ui.caret;
    ui_edit_sel(&app.ui, &lo, &hi);
    // Left of the caret the text before the selection and, when the caret is its right end, the
    // selection; right of it the rest.
    Str l1 = str_cut(ed, 0, c == hi ? lo : c), l2 = c == hi ? str_cut(ed, lo, hi) : STR_EMPTY;
    Str r1 = c == lo ? str_cut(ed, lo, hi) : STR_EMPTY, r2 = str_cut(ed, c == lo ? hi : c, ed.len);
    float rw = str_width(r1) + str_width(r2), lw = str_width(l1) + str_width(l2);
    float left = room - (rw < room / 2 ? rw : room / 2);
    float w2 = str_width(l2) < left ? str_width(l2) : left, w1 = left - w2;
    float right = room - (lw < left ? lw : left);
    float v1 = str_width(r1) < right ? str_width(r1) : right, v2 = right - v1;
    Clay_ElementId sel = Clay_GetElementId(cz(arena_sprintf(&frame, "%s-sel", id)));
    edit_run(sel, l1, false, w1, true);
    edit_run(sel, l2, true, w2, true);
    CLAY(Clay_GetElementId(cz(arena_sprintf(&frame, "%s-caret", id))), { .layout = { .sizing = { CLAY_SIZING_FIXED(2), CLAY_SIZING_FIXED(FONT_PX + 2) } },
                                                                          .backgroundColor = cc(t->accent) }) {}
    edit_run(sel, r1, true, v1, false);
    edit_run(sel, r2, false, v2, false);
}

// The edit line in place of the name.
static void layout_edit(Theme *t, float room)
{
    CLAY(CLAY_ID("edit"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ROW_H - 6) }, .padding = { 4, 4, 0, 0 },
                                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                            .clip = { .horizontal = true },
                            .backgroundColor = cc(t->bg), .border = { .color = cc(t->accent), .width = CLAY_BORDER_ALL(1) } }) {
        edit_text("edit", room - 12);
    }
}

// Entry i's Size cell: a file's size, a folder's item count ("" until known). ask: ask counts.c for
// a count not asked yet (the row is being drawn).
static void size_cell(isize i, char *size, size_t cap, bool ask)
{
    const Entry *e = &app.list.e[i];
    size[0] = 0;
    if (!kind_is_dir(e->kind) && e->kind != K_LINK_BROKEN && e->kind != K_OTHER) ui_size(size, cap, e->size);
    if (!kind_is_dir(e->kind)) return;
    char path[PATH_MAX];
    if (ask && app.count[i] == COUNT_UNKNOWN && join(path, sizeof path, app.cwd, e->name) && count_ask(i, path, app.show_hidden))
        app.count[i] = COUNT_ASKED;
    if (app.count[i] >= 0) snprintf(size, cap, "%td item%s", app.count[i], app.count[i] == 1 ? "" : "s");
    else if (app.count[i] == -1) snprintf(size, cap, "?");
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
    size_cell(i, size, sizeof size, true);
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
            if (sel && app.ui.editing && !app.ui.edit_path) {
                layout_edit(t, name_w);
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

// ---- the top bar (F4) -----------------------------------------------------------------------

static IconSlot *named_icon(const char *name)
{
    Str n = str_from_c(name);
    return icon_for(n, &n, 1);
}

// The top bar's chevrons, drawn as two lines each (Nemo's look; the theme has its arrows only as
// SVG, which ofx doesn't draw). Clay hands the data back to draw_custom at render time; it lives in
// the frame arena, which outlasts the frame's render.
typedef enum { CHEV_LEFT, CHEV_RIGHT, CHEV_UP } ChevronDir;
typedef enum { CUSTOM_CHEVRON = 1, CUSTOM_LINE } CustomKind;   // the first field of each custom's data

typedef struct {
    CustomKind kind;
    ChevronDir dir;
    Clay_Color color;
} Chevron;

// A job row's throughput line (F5.3): the speed at each point it reached, scaled to the fastest,
// 1 device pixel wide, the part done tinted under it; a job without bytes to count (a delete, a
// move within a drive) a plain bar by items.
typedef struct {
    CustomKind     kind;
    const JobLine *line;   // NULL: the plain bar
    float          done;   // 0..1
    Clay_Color     stroke, fill;
} LineDraw;

static Color rl(Clay_Color c)
{
    return (Color){ (u8)c.r, (u8)c.g, (u8)c.b, (u8)c.a };
}

static void draw_line(Clay_BoundingBox b, const LineDraw *d)
{
    float scale = d->line ? jobline_scale(d->line, now_ms()) : 0;
    if (scale <= 0) {
        DrawRectangleRec((Rectangle){ b.x, b.y, b.width * d->done, b.height }, rl(d->fill));
        return;
    }
    const JobLine *l = d->line;
    float step = b.width / JOBQ_POINTS, room = b.height - 8, px = 1.0f / GetWindowScaleDPI().x;   // the fastest 8 px under the top
    Vector2 prev = { 0 };
    for (int k = 0; k < l->reached; k++) {
        float h = l->speed[k] / scale;
        float y = b.y + b.height - room * (h < 1 ? h : 1) - (h > 1 ? 4 : 0);   // over the scale: clipped at the top
        Vector2 p = { b.x + step * ((float)k + 0.5f), y };
        DrawRectangleRec((Rectangle){ b.x + step * (float)k, y, step + 0.5f, b.y + b.height - y }, rl(d->fill));
        if (k > 0) DrawLineEx(prev, p, px, rl(d->stroke));
        prev = p;
    }
}

static void draw_custom(Clay_BoundingBox b, void *arg)
{
    if (!arg) return;
    if (*(const CustomKind *)arg == CUSTOM_LINE) { draw_line(b, arg); return; }
    const Chevron *c = arg;
    float cx = b.x + b.width / 2, cy = b.y + b.height / 2, r = 5;
    Color col = { (u8)c->color.r, (u8)c->color.g, (u8)c->color.b, (u8)c->color.a };
    Vector2 p0, p1, p2;
    switch (c->dir) {
    case CHEV_LEFT: p0 = (Vector2){ cx + r / 2, cy - r }, p1 = (Vector2){ cx - r / 2, cy }, p2 = (Vector2){ cx + r / 2, cy + r }; break;
    case CHEV_RIGHT: p0 = (Vector2){ cx - r / 2, cy - r }, p1 = (Vector2){ cx + r / 2, cy }, p2 = (Vector2){ cx - r / 2, cy + r }; break;
    default: p0 = (Vector2){ cx - r, cy + r / 2 }, p1 = (Vector2){ cx, cy - r / 2 }, p2 = (Vector2){ cx + r, cy + r / 2 }; break;
    }
    DrawLineEx(p0, p1, 1.6f, col);
    DrawLineEx(p1, p2, 1.6f, col);
    DrawCircleV(p1, 0.8f, col);   // a clean tip where the lines meet
}

static void chevron(Clay_ElementId id, ChevronDir dir, Clay_Color color)
{
    Chevron *c = arena_new(&frame, Chevron, 1);
    *c = (Chevron){ CUSTOM_CHEVRON, dir, color };
    CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(16) } }, .custom = { .customData = c } }) {}
}

static Clay_Color dim_color(void)
{
    return mix(app.theme.fg_dim, app.theme.bg_light, 0.5f);
}

// A top bar button: a chevron, an icon from the theme, or a glyph from the UI font. Dimmed when it
// can't act.
static void bar_button(Clay_ElementId id, UiBar which, int chev, const char *glyph, IconSlot *icon, bool enabled, float w)
{
    Theme *t = &app.theme;
    bool hover = enabled && app.bar_hover == which;
    Clay_Color fg = enabled ? cc(t->fg) : dim_color();
    CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(BAR_BTN_H) },
                           .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
               .backgroundColor = hover ? cc(t->bg) : cc(t->bg_light) }) {
        Clay_ElementId inner = Clay_GetElementId(cz(arena_sprintf(&frame, "%.*s-icon", (int)id.stringId.length, id.stringId.chars)));
        if (chev >= 0) chevron(inner, (ChevronDir)chev, fg);
        else if (icon && icon->tex.id) icon_box(inner, icon, CRUMB_ICON_PX);
        else text(cz(glyph), fg);
    }
}

static float crumb_width(int i)
{
    const UiCrumbs *c = &app.crumbs;
    return str_width(ui_crumb_label(c, i)) + 2 * CRUMB_PAD + (c->seg[i].home ? CRUMB_ICON_PX + 6 : 0);
}

// The path buttons: the current folder marked, the deeper ones dimmer; ‹ and › when they don't fit.
static void layout_crumbs(float room)
{
    Theme *t = &app.theme;
    UiCrumbs *c = &app.crumbs;
    float wd[UI_CRUMBS_MAX];
    for (int i = 0; i < c->n; i++) wd[i] = crumb_width(i);
    int lo, hi;
    if (app.ui.crumb_focus) ui_crumbs_show(c, wd, room, ARROW_W, app.ui.crumb_at);
    app.crumb_arrows = ui_crumbs_window(c, wd, room, ARROW_W, &lo, &hi);
    app.crumb_lo = lo, app.crumb_hi = hi;
    // One outlined group, a line between the buttons, so they read as one path (Michael, 2026-10-20).
    Clay_Color line = mix(t->fg_dim, t->bg_light, 0.45f);
    CLAY(CLAY_ID("crumb-group"), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(BAR_BTN_H) } },
                                   .border = { .color = line, .width = { 1, 1, 1, 1, 1 } } }) {
    if (app.crumb_arrows) bar_button(CLAY_ID("crumb-left"), UB_LEFT, CHEV_LEFT, NULL, NULL, lo > 0, ARROW_W);
    for (int i = lo; i < hi; i++) {
        bool cur = i == c->cur, hover = app.bar_hover == UB_CRUMB && app.crumb_hover == i;
        bool keyed = app.ui.crumb_focus && app.ui.crumb_at == i;   // Ctrl+K's mark: an accent outline
        CLAY(CLAY_IDI("crumb", i), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(BAR_BTN_H) }, .padding = { CRUMB_PAD, CRUMB_PAD, 0, 0 },
                                                 .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                     .backgroundColor = cur ? cc(t->selection) : hover ? cc(t->bg) : cc(t->bg_light),
                                     .border = { .color = cc(t->accent), .width = { keyed * 2, keyed * 2, keyed * 2, keyed * 2, 0 } } }) {
            if (c->seg[i].home) icon_box(CLAY_IDI("crumb-icon", i), named_icon("user-home"), CRUMB_ICON_PX);
            Str label = ui_crumb_label(c, i);
            float most = room - 2 * ARROW_W - 2 * CRUMB_PAD - (c->seg[i].home ? CRUMB_ICON_PX + 6 : 0);
            text(fit(label, most > 20 ? most : 20, false), cur ? cc(t->accent) : i > c->cur ? cc(t->fg_dim) : cc(t->fg));
        }
    }
    if (app.crumb_arrows) bar_button(CLAY_ID("crumb-right"), UB_RIGHT, CHEV_RIGHT, NULL, NULL, hi < c->n, ARROW_W);
    }
}

// The location line in the path buttons' place: the path, its selection and a caret (all of it
// selected as it opens).
static void layout_location(float room)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("location"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(BAR_BTN_H) }, .padding = { 6, 6, 0, 0 },
                                            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                .clip = { .horizontal = true },
                                .backgroundColor = cc(t->bg), .border = { .color = cc(t->accent), .width = CLAY_BORDER_ALL(1) } }) {
        edit_text("location", room - 24);
    }
}

static void layout_bar(float w)
{
    Theme *t = &app.theme;
    bool can_up = strcmp(app.cwd, "/") != 0;
    CLAY(CLAY_ID("header"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(HEADER_H) },
                                          .padding = { PAD / 2, PAD / 2, 0, 0 }, .childGap = BAR_GAP,
                                          .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                              .backgroundColor = cc(t->bg_light) }) {
        bar_button(CLAY_ID("nav-back"), UB_BACK, CHEV_LEFT, NULL, NULL, hist.nback > 0, BAR_BTN_W);
        bar_button(CLAY_ID("nav-fwd"), UB_FORWARD, CHEV_RIGHT, NULL, NULL, hist.nfwd > 0, BAR_BTN_W);
        bar_button(CLAY_ID("nav-up"), UB_UP, CHEV_UP, NULL, NULL, can_up, BAR_BTN_W);
        float room = w - PAD - 5 * BAR_BTN_W - 7 * BAR_GAP - 2 * PAD;
        CLAY(CLAY_ID("crumbs"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(BAR_BTN_H) }, .padding = { PAD / 2, 0, 0, 0 },
                                              .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                  .clip = { .horizontal = true } }) {
            if (app.ui.editing && app.ui.edit_path) layout_location(room);
            else layout_crumbs(room);
        }
        bar_button(CLAY_ID("loc-toggle"), UB_LOCATION, -1, "\xE2\x9C\x8E", NULL, true, BAR_BTN_W);          // ✎
        bar_button(CLAY_ID("search"), UB_SEARCH, -1, "/", named_icon("edit-find"), true, BAR_BTN_W);
    }
}

// The tab bar: the tabs share the width, each its folder's name and a ×; the one on screen marked.
static void layout_tabs(float w)
{
    Theme *t = &app.theme;
    float each = (w - PAD) / (float)tabbar.n;
    CLAY(CLAY_ID("tabs"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(TAB_H) }, .padding = { PAD / 2, PAD / 2, 0, 0 } },
                            .backgroundColor = cc(t->bg_light),
                            .border = { .color = mix(t->fg_dim, t->bg_light, 0.45f), .width = { .bottom = 1 } } }) {
        for (int i = 0; i < tabbar.n; i++) {
            bool cur = i == tabbar.cur, hover = app.tab_hover == i;
            const char *dir = cur ? app.cwd : tabs[tabbar.slot[i]].dir;
            Str name = strcmp(dir, "/") == 0 ? S("/") : path_base(str_from_c(dir));
            CLAY(CLAY_IDI("tab", i), { .layout = { .sizing = { CLAY_SIZING_FIXED(each), CLAY_SIZING_FIXED(TAB_H) }, .padding = { PAD, 4, 0, 0 },
                                                   .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                       .backgroundColor = cur ? cc(t->bg) : hover ? mix(t->bg, t->bg_light, 0.5f) : cc(t->bg_light),
                                       .border = { .color = cur ? cc(t->accent) : mix(t->fg_dim, t->bg_light, 0.45f),
                                                   .width = { .right = 1, .bottom = cur ? 2 : 0 } } }) {
                CLAY(CLAY_IDI("tab-name", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
                    text(fit(name, each - PAD - TAB_X_W - 12, false), cur ? cc(t->fg) : cc(t->fg_dim));
                }
                bool xhover = app.tab_x_hover == i;
                CLAY(CLAY_IDI("tab-x", i), { .layout = { .sizing = { CLAY_SIZING_FIXED(TAB_X_W), CLAY_SIZING_FIXED(TAB_X_W) },
                                                         .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                                             .backgroundColor = xhover ? cc(t->bg_light) : (cur ? cc(t->bg) : hover ? mix(t->bg, t->bg_light, 0.5f) : cc(t->bg_light)) }) {
                    text(cz("\xC3\x97"), xhover ? cc(t->fg) : cc(t->fg_dim));   // ×
                }
            }
        }
    }
}

// ---- the modal panels (F5.1) --------------------------------------------------------------------

typedef enum { MODAL_NONE, MODAL_DELETE, MODAL_CLASH, MODAL_CANCEL, MODAL_QUIT, MODAL_FULL } ModalKind;

// The panel on top, in the order ui_handle takes their keys.
static ModalKind modal_kind(void)
{
    if (app.ui.full_told) return MODAL_FULL;
    if (app.ui.quit_asking) return MODAL_QUIT;
    if (app.ui.cancel_asking) return MODAL_CANCEL;
    if (app.ui.confirming) return MODAL_DELETE;
    if (jobs.asking >= 0 && app.ui.asking) return MODAL_CLASH;
    return MODAL_NONE;
}

static JobRec *job_by_id(int id)
{
    for (int i = 0; i < jobs.nrun; i++)
        if (job_recs[jobs.run[i]].id == id) return &job_recs[jobs.run[i]];
    return NULL;
}

// A panel's button; the main one (Enter's) filled, in red when it destroys something.
static void modal_button(const char *id, int which, const char *label, bool main_one, bool danger)
{
    Theme *t = &app.theme;
    bool hover = app.modal_hover == which;
    Rgb fill = danger ? t->red : t->accent;
    Clay_Color bg = main_one ? (hover ? mix(fill, t->bg_light, 0.85f) : cc(fill)) : hover ? cc(t->bg) : cc(t->bg_light);
    CLAY(Clay_GetElementId(cz(id)), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(32) }, .padding = { 16, 16, 0, 0 },
                                                  .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                      .backgroundColor = bg,
                                      .border = { .color = main_one ? cc(fill) : mix(t->fg_dim, t->bg_light, 0.45f), .width = CLAY_BORDER_OUTSIDE(1) } }) {
        text(cz(label), main_one ? cc(t->bg) : cc(t->fg));
    }
}

// The panel itself, in the window's middle over the rest.
static void layout_modal_panel(ModalKind k)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("modal"), { .layout = { .sizing = { CLAY_SIZING_FIXED(480), CLAY_SIZING_FIT(0) }, .padding = { 24, 24, 20, 20 }, .childGap = 10,
                                         .layoutDirection = CLAY_TOP_TO_BOTTOM },
                             .backgroundColor = cc(t->bg_light),
                             .border = { .color = k == MODAL_DELETE || k == MODAL_CANCEL || k == MODAL_QUIT ? cc(t->red) : cc(t->accent), .width = CLAY_BORDER_OUTSIDE(1) },
                             .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 20,
                                           .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER } } }) {
        if (k == MODAL_FULL || k == MODAL_QUIT || k == MODAL_CANCEL) {
            const char *line, *hint;
            if (k == MODAL_FULL) {
                line = "No more jobs can be queued right now. Wait until one finishes.";
                hint = "Enter or Esc closes this";
            } else if (k == MODAL_QUIT) {
                line = arena_sprintf(&frame, "%d %s running and %d waiting. Quit and cancel them?", jobs.nrun, jobs.nrun == 1 ? "job is" : "jobs are", jobs.nwait);
                if (jobs.nwait == 0) line = arena_sprintf(&frame, "%d %s running. Quit and cancel %s?", jobs.nrun, jobs.nrun == 1 ? "job is" : "jobs are", jobs.nrun == 1 ? "it" : "them");
                hint = "Enter or y quits \xC2\xB7 n or Esc stays";
            } else {
                JobRec *r = job_by_id(app.ui.cancel_id);
                Str label = r ? str_from_c(r->label) : S("this job");
                // "Copying 12 items to bdir" asked as "Cancel copying 12 items to bdir?"
                char lower[sizeof r->label];
                snprintf(lower, sizeof lower, "%.*s", STR_ARG(label));
                if (lower[0] >= 'A' && lower[0] <= 'Z') lower[0] = (char)(lower[0] - 'A' + 'a');
                line = arena_sprintf(&frame, "Cancel %s?", lower);
                hint = "Enter or y cancels it \xC2\xB7 n or Esc keeps it going";
            }
            text(fit(str_from_c(line), 430, false), cc(t->fg));
            text(cz(hint), cc(t->fg_dim));
            CLAY(CLAY_ID("modal-buttons"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 10,
                                                         .childAlignment = { .x = CLAY_ALIGN_X_RIGHT }, .padding = { 0, 0, 8, 0 } } }) {
                if (k == MODAL_FULL) {
                    modal_button("modal-ok", UI_MODAL_OK, "OK", true, false);
                } else {
                    modal_button("modal-no", UI_MODAL_NO, k == MODAL_QUIT ? "Stay" : "Keep going", false, false);
                    modal_button("modal-yes", UI_MODAL_YES, k == MODAL_QUIT ? "Quit" : "Cancel job", true, true);
                }
            }
        } else if (k == MODAL_DELETE) {
            text(cz(arena_sprintf(&frame, "Delete %td %s for good?", app.ui.confirm_n, items(app.ui.confirm_n))), cc(t->fg));
            CLAY(CLAY_ID("modal-names"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2, .padding = { 12, 0, 4, 4 } } }) {
                isize shown = doomed.n > 8 ? 7 : doomed.n;
                for (isize i = 0; i < shown; i++) text(fit(path_base(str_from_c(doomed.path[i])), 420, false), cc(t->fg_dim));
                if (doomed.n > shown) text(cz(arena_sprintf(&frame, "and %td more", doomed.n - shown)), cc(t->fg_dim));
            }
            text(cz("This can't be undone."), cc(t->red));
            text(cz("Enter or y deletes \xC2\xB7 n or Esc cancels"), cc(t->fg_dim));
            CLAY(CLAY_ID("modal-buttons"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 10,
                                                         .childAlignment = { .x = CLAY_ALIGN_X_RIGHT }, .padding = { 0, 0, 8, 0 } } }) {
                modal_button("modal-no", UI_MODAL_NO, "Cancel", false, false);
                modal_button("modal-yes", UI_MODAL_YES, "Delete", true, true);
            }
        } else {
            // r->clash is safe to read here without jobs.mu: asking is set only after jobs_poll saw
            // the worker waiting (under the lock), and cleared before job_answer lets it go on.
            JobRec *r = &job_recs[jobs.asking];
            text(fit(str_from_c(r->label), 430, false), cc(t->fg_dim));   // which job asks (two may)
            Str name = path_base(str_from_c(r->clash)), dir = path_dir(str_from_c(r->clash));
            Str in_dir = path_base(dir.len ? dir : S("/"));
            text(fit(str_from_c(arena_sprintf(&frame, "%.*s already exists in %.*s", STR_ARG(name), STR_ARG(in_dir))), 430, false), cc(t->fg));
            text(cz(r->clash_dirs ? "Merge the folders, keep both, or skip it?" : "Replace it, keep both, or skip it?"), cc(t->fg_dim));
            CLAY(CLAY_ID("modal-all"), { .layout = { .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }, .padding = { 0, 0, 4, 4 } } }) {
                CLAY(CLAY_ID("modal-all-box"), { .layout = { .sizing = { CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(16) } },
                                                 .backgroundColor = app.ui.ask_all ? cc(t->accent) : cc(t->bg),
                                                 .border = { .color = cc(t->fg_dim), .width = CLAY_BORDER_OUTSIDE(1) } }) {}
                text(cz("Do the same for the rest"), app.modal_hover == UI_MODAL_ALL ? cc(t->fg) : cc(t->fg_dim));
            }
            text(cz(r->clash_dirs ? "s skip \xC2\xB7 r merge \xC2\xB7 k keep both \xC2\xB7 Shift: for the rest \xC2\xB7 Esc cancels"
                                    : "s skip \xC2\xB7 r replace \xC2\xB7 k keep both \xC2\xB7 Shift: for the rest \xC2\xB7 Esc cancels"),
                 cc(t->fg_dim));
            CLAY(CLAY_ID("modal-buttons"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 10,
                                                         .childAlignment = { .x = CLAY_ALIGN_X_RIGHT }, .padding = { 0, 0, 8, 0 } } }) {
                modal_button("modal-cancel", UI_MODAL_CANCEL, "Cancel", false, false);
                modal_button("modal-skip", UI_MODAL_SKIP, "Skip", false, false);
                modal_button("modal-keep", UI_MODAL_KEEP, "Keep both", false, false);
                modal_button("modal-replace", UI_MODAL_REPLACE, r->clash_dirs ? "Merge" : "Replace", false, false);   // no default: Enter doesn't answer
            }
        }
    }
}

// The panel over the window, the rest darkened but still readable (Michael's pick over a blur,
// 2026-10-21).
static void layout_modal(ModalKind k, float w, float h)
{
    CLAY(CLAY_ID("modal-backdrop"), { .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h) } },
                                      .backgroundColor = { 0, 0, 0, 150 },
                                      .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 10 } }) {}
    layout_modal_panel(k);
}

// The job rows under the files (F5.3): a row a running job, its throughput line across it, what it
// does and how far on the left, its speed and a × on the right; then how many wait.
static void layout_jobs(void)
{
    Theme *t = &app.theme;
    for (int i = 0; i < jobs.nrun; i++) {
        JobRec *r = &job_recs[jobs.run[i]];
        Job *j = &r->j;
        i64 bt = atomic_load(&j->bytes_total), ft = atomic_load(&j->files_total);
        float done = bt > 0 ? (float)atomic_load(&j->bytes_done) / (float)bt : ft > 0 ? (float)atomic_load(&j->files_done) / (float)ft : 0;
        if (done > 1) done = 1;
        LineDraw *d = arena_new(&frame, LineDraw, 1);
        *d = (LineDraw){ CUSTOM_LINE, bt > 0 && j->kind != JOB_DELETE ? &r->line : NULL, done, cc(t->accent), mix(t->accent, t->bg_light, 0.2f) };
        bool keyed = app.ui.jobs_focus && app.ui.job_at == i, hover = app.jobx_hover == i;
        char speed[32] = "";
        if (d->line) jobq_speed(speed, sizeof speed, r->line.now);
        const char *pct = atomic_load(&j->phase) == JP_SCAN ? "counting\xE2\x80\xA6" : arena_sprintf(&frame, "%d%%", (int)(done * 100));
        CLAY(CLAY_IDI("job-row", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(JOB_ROW_H) } },
                                       .backgroundColor = cc(t->bg_light),
                                       .border = { .color = mix(t->fg_dim, t->bg_light, 0.45f), .width = { .top = 1 } } }) {
            CLAY(CLAY_IDI("job-line", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { PAD, 8, 0, 0 }, .childGap = 12,
                                                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                            .custom = { .customData = d } }) {
                text(fit(str_from_c(r->label), 420, false), cc(t->fg));
                text(cz(pct), cc(t->fg_dim));
                CLAY(CLAY_IDI("job-gap", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } } }) {}
                if (speed[0]) text(cz(arena_sprintf(&frame, "%s", speed)), cc(t->fg));
                CLAY(CLAY_IDI("job-x", i), { .layout = { .sizing = { CLAY_SIZING_FIXED(26), CLAY_SIZING_FIXED(26) },
                                                         .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                                             .backgroundColor = hover ? cc(t->bg) : (Clay_Color){ 0, 0, 0, 0 },
                                             .border = { .color = cc(t->accent), .width = { keyed * 2, keyed * 2, keyed * 2, keyed * 2, 0 } } }) {
                    text(cz("\xC3\x97"), hover || keyed ? cc(t->red) : cc(t->fg_dim));   // ×
                }
            }
        }
    }
    if (jobs.nwait) {
        CLAY(CLAY_ID("jobs-waiting"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(JOB_WAIT_H) }, .padding = { PAD, PAD, 0, 0 },
                                                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                        .backgroundColor = cc(t->bg_light) }) {
            text(cz(arena_sprintf(&frame, "%d waiting", jobs.nwait)), cc(t->fg_dim));
        }
    }
}

// The window, and the modal panel on top when one is up.
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
        float foot_w = (float)w - 2 * PAD - 24 - str_width(cnt);   // the footer's line beside the count
        layout_bar((float)w);
        float body_h = (float)(h - HEADER_H - FOOTER_H);
        CLAY(CLAY_ID("body"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(body_h) } } }) {
        layout_side(body_h);
        CLAY(CLAY_ID("main"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
        // The tab bar over the files only, not the sidebar (Michael, 2026-10-21).
        if (tabbar.n >= 2) layout_tabs((float)w - SIDE_W);
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
        layout_jobs();
        }
        }
        CLAY(CLAY_ID("footer"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(FOOTER_H) },
                                              .padding = { PAD, PAD, 0, 0 }, .childGap = 24, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                  .backgroundColor = cc(t->bg_light) }) {
            CLAY(CLAY_ID("footer-line"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                           .clip = { .horizontal = true } }) {
            if (modal_kind() != MODAL_NONE) {
                text(cz("Answer the question above"), cc(t->fg_dim));
            } else if (app.ui.crumb_focus) {
                text(fit(S("Path: \xE2\x86\x90 \xE2\x86\x92 (h l) move \xC2\xB7 Enter goes there \xC2\xB7 Shift+Enter in a new tab \xC2\xB7 Esc back to the list"), foot_w, false),
                     cc(t->fg_dim));
            } else if (app.ui.editing && app.ui.edit_path) {
                text(cz("Go to: Enter goes there \xC2\xB7 Esc cancels \xC2\xB7 ~ is home, a relative path starts here"), cc(t->fg_dim));
            } else if (app.ui.editing) {
                text(cz(app.edit_new ? "New folder: Enter keeps the name \xC2\xB7 Esc keeps \"New folder\""
                                     : "Rename: Enter renames \xC2\xB7 Esc cancels"),
                     cc(t->fg_dim));
            } else if (app.ui.jobs_focus) {
                text(fit(S("Jobs: \xE2\x86\x91 \xE2\x86\x93 (k j) move \xC2\xB7 Enter cancels the marked job \xC2\xB7 Esc back to the list"), foot_w, false),
                     cc(t->fg_dim));
            } else if (app.ui.finding) {
                // The find line: its text, red when it matches nothing, and a caret.
                Str f = { app.ui.find, app.ui.find_len };
                text(cz("Find: "), cc(t->fg_dim));
                text(fit(f, foot_w - 80, true), app.ui.find_miss ? cc(t->red) : cc(t->fg));
                CLAY(CLAY_ID("caret"), { .layout = { .sizing = { CLAY_SIZING_FIXED(2), CLAY_SIZING_FIXED(FONT_PX + 2) } },
                                         .backgroundColor = cc(t->accent) }) {}
            } else if (app.status[0]) {
                text(fit(str_from_c(app.status), foot_w, false), app.status_bad ? cc(t->red) : cc(t->fg));
            } else {
                text(fit(S("Enter open \xC2\xB7 Backspace up \xC2\xB7 Space select \xC2\xB7 F2 rename \xC2\xB7 Del trash \xC2\xB7 "
                           "Ctrl+C X V \xC2\xB7 Ctrl+Z undo \xC2\xB7 / find \xC2\xB7 Ctrl+L location \xC2\xB7 . hidden \xC2\xB7 s sort \xC2\xB7 "
                           "Tab places \xC2\xB7 t terminal"),
                         foot_w, false),
                     cc(t->fg_dim));
            }
            }
            text(cs(cnt), cc(t->fg_dim));
        }
        ModalKind mk = modal_kind();
        if (mk != MODAL_NONE) layout_modal(mk, (float)w, (float)h);
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
static bool               script_on;     // a script drives the window: Ctrl+Q quits at once
static bool               close_asked;   // the compositor asked the window to close
static GLFWcharfun        raylib_char;
static GLFWmousebuttonfun raylib_button;
static bool               debug_input;   // OFX_GUI_DEBUG: log every key and button event

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
    if (m == GLFW_MOD_ALT && key == GLFW_KEY_UP) return UK_PARENT;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_T) return UK_TAB_NEW;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_W) return UK_TAB_CLOSE;
    if ((m == GLFW_MOD_CONTROL && key == GLFW_KEY_TAB) || (m == GLFW_MOD_CONTROL && key == GLFW_KEY_PAGE_DOWN)) return UK_TAB_NEXT;
    if ((m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_TAB) || (m == GLFW_MOD_CONTROL && key == GLFW_KEY_PAGE_UP)) return UK_TAB_PREV;
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_PAGE_UP) return UK_TAB_LEFT;
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_PAGE_DOWN) return UK_TAB_RIGHT;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_L) return UK_LOCATION;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_K) return UK_CRUMBS;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_J) return UK_JOBS;
    if (m == GLFW_MOD_SHIFT) {
        switch (key) {
        case GLFW_KEY_LEFT: return UK_SEL_LEFT;
        case GLFW_KEY_RIGHT: return UK_SEL_RIGHT;
        case GLFW_KEY_HOME: return UK_SEL_HOME;
        case GLFW_KEY_END: return UK_SEL_END;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER: return UK_SHIFT_ENTER;
        default: break;
        }
    }
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
    if (debug_input) fprintf(stderr, "ofx: key %d action %d mods %#x\n", key, action, mods);
    // Ctrl+Q asks first when jobs run or wait (F5.3); a scripted window quits at once.
    if (action == GLFW_PRESS && key == GLFW_KEY_Q && (mods & GLFW_MOD_CONTROL)) {
        if (script_on) app.quit = true;
        else ev_push((UiEvent){ UK_QUIT, 0 });
    }
    UiKey k = action != GLFW_RELEASE ? ui_key_of(key, mods) : UK_NONE;
    if (k) ev_push((UiEvent){ k, 0 });
    // Alt+1 .. Alt+9: a tab by place (the digit rides in ch).
    int m = mods & (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT | GLFW_MOD_ALT | GLFW_MOD_SUPER);
    if (action == GLFW_PRESS && m == GLFW_MOD_ALT && key >= GLFW_KEY_1 && key <= GLFW_KEY_9) ev_push((UiEvent){ UK_TAB_JUMP, (u32)(key - GLFW_KEY_0) });
    if (raylib_key) raylib_key(w, key, scancode, action, mods);
}

// The window's close button or the compositor's close: as Ctrl+Q (asked first with jobs), so
// raylib's own callback, which would end the loop, isn't called.
static void on_close(GLFWwindow *w)
{
    glfwSetWindowShouldClose(w, GLFW_FALSE);
    close_asked = true;
    glfwPostEmptyEvent();
}

static void on_char(GLFWwindow *w, unsigned int cp)
{
    if (debug_input) fprintf(stderr, "ofx: char %#x\n", cp);
    // GLFW sends no character with Alt (Mod1) held, so Alt+1 is a tab only; AltGr (Mod5) still
    // types (reviewer-2, F5 review).
    ev_push((UiEvent){ UK_NONE, cp });
    if (raylib_char) raylib_char(w, cp);
}

// The mouse's back and forward (thumb) buttons walk the history. Linux reports them as BTN_SIDE
// and BTN_EXTRA (GLFW buttons 4 and 5) on most mice, BTN_BACK and BTN_FORWARD (7 and 6) on some
// (uml-viewer's finding).
static void on_button(GLFWwindow *w, int button, int action, int mods)
{
    if (debug_input) fprintf(stderr, "ofx: mouse button %d action %d mods %#x\n", button + 1, action, mods);
    if (action == GLFW_PRESS && (button == GLFW_MOUSE_BUTTON_4 || button == GLFW_MOUSE_BUTTON_7)) mouse_backs++;
    if (action == GLFW_PRESS && (button == GLFW_MOUSE_BUTTON_5 || button == GLFW_MOUSE_BUTTON_6)) mouse_forwards++;
    if (raylib_button) raylib_button(w, button, action, mods);
}

// What is under a point of the last frame drawn: a list row, a sidebar row, or a column header
// (each -1 when not).
typedef struct {
    int   row, side, header;
    UiBar bar;     // a top bar button
    int   crumb;   // UB_CRUMB: which path button
    int   tab, tab_x;   // a tab, or its ×
    int   modal;        // a modal panel's button (UI_MODAL_*), 0 for none
    int   job_x;        // a job row's ×, -1 for none
} Hit;

static bool in_box(Clay_ElementId id, float x, float y)
{
    Clay_ElementData d = Clay_GetElementData(id);
    return d.found && x >= d.boundingBox.x && x < d.boundingBox.x + d.boundingBox.width && y >= d.boundingBox.y &&
           y < d.boundingBox.y + d.boundingBox.height;
}

static Hit hit_at(float x, float y)
{
    Hit h = { -1, -1, -1, UB_NONE, -1, -1, -1, 0, -1 };
    if (modal_kind() != MODAL_NONE) {
        // A panel is up: its buttons are all there is to hit.
        static const struct { const char *id; int b; } MB[] = {
            { "modal-yes", UI_MODAL_YES }, { "modal-no", UI_MODAL_NO }, { "modal-skip", UI_MODAL_SKIP }, { "modal-replace", UI_MODAL_REPLACE },
            { "modal-keep", UI_MODAL_KEEP }, { "modal-cancel", UI_MODAL_CANCEL }, { "modal-all", UI_MODAL_ALL }, { "modal-ok", UI_MODAL_OK },
        };
        for (int k = 0; k < COUNT(MB) && !h.modal; k++)
            if (in_box(Clay_GetElementId(cz(MB[k].id)), x, y)) h.modal = MB[k].b;
        return h;
    }
    for (int i = 0; tabbar.n >= 2 && i < tabbar.n && h.tab < 0; i++) {
        if (in_box(CLAY_IDI("tab-x", i), x, y)) h.tab_x = h.tab = i;
        else if (in_box(CLAY_IDI("tab", i), x, y)) h.tab = i;
    }
    static const struct { const char *id; UiBar b; } BAR[] = {
        { "nav-back", UB_BACK }, { "nav-fwd", UB_FORWARD }, { "nav-up", UB_UP }, { "loc-toggle", UB_LOCATION },
        { "search", UB_SEARCH }, { "crumb-left", UB_LEFT }, { "crumb-right", UB_RIGHT },
    };
    for (int k = 0; k < COUNT(BAR) && h.bar == UB_NONE; k++)
        if (in_box(Clay_GetElementId(cz(BAR[k].id)), x, y)) h.bar = BAR[k].b;
    for (int i = app.crumb_lo; h.bar == UB_NONE && i < app.crumb_hi; i++)
        if (in_box(CLAY_IDI("crumb", i), x, y)) h.bar = UB_CRUMB, h.crumb = i;
    for (int r = 0; r < app.ui.list.rows && h.row < 0; r++)
        if (in_box(CLAY_IDI("row", r), x, y)) h.row = r;
    for (int i = 0; i < jobs.nrun && h.job_x < 0; i++)
        if (in_box(CLAY_IDI("job-x", i), x, y)) h.job_x = i;
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
    in->bar = h.bar, in->crumb = h.crumb;
    in->modal = h.modal;
    in->job_x = h.job_x;
    if (h.tab_x >= 0) in->tab_close = h.tab_x;
    else if (h.tab >= 0) in->tab_click = h.tab;
}

// A middle press at a point: a folder (a row, a place, a path button) in a background tab, or a
// tab closed.
static void middle_at(UiInput *in, float x, float y)
{
    Hit h = hit_at(x, y);
    if (h.tab >= 0) in->tab_close = h.tab;
    else if (h.row >= 0) in->mid_row = h.row;
    else if (h.side >= 0) in->mid_side = h.side;
    else if (h.bar == UB_CRUMB) in->mid_crumb = h.crumb;
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
        *seen |= h.row != app.hover || h.side != app.side_hover || h.bar != app.bar_hover || h.crumb != app.crumb_hover ||
                 h.tab != app.tab_hover || h.tab_x != app.tab_x_hover;
        app.hover = h.row, app.side_hover = h.side, app.bar_hover = h.bar, app.crumb_hover = h.crumb;
        app.tab_hover = h.tab, app.tab_x_hover = h.tab_x;
        *seen |= h.modal != app.modal_hover;
        app.modal_hover = h.modal;
        *seen |= h.job_x != app.jobx_hover;
        app.jobx_hover = h.job_x;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int mods = (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ? UI_MOD_CTRL : 0) |
                   (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? UI_MOD_SHIFT : 0);
        press_at(&in, m.x, m.y, mods);
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) middle_at(&in, m.x, m.y);
    in.backs = mouse_backs, mouse_backs = 0;
    in.forwards = mouse_forwards, mouse_forwards = 0;
    *seen |= in.wheel != 0 || in.click >= 0 || in.dclick >= 0 || in.side_click >= 0 || in.header >= 0 || in.backs > 0 ||
             in.forwards > 0 || in.bar != UB_NONE || in.tab_click >= 0 || in.tab_close >= 0 || in.mid_row >= 0 || in.mid_side >= 0 ||
             in.mid_crumb >= 0 || in.modal || in.job_x >= 0;
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
static void *volatile planted;   // OFX_TEST_LEAK's block, dropped so nothing points at it

static void *signal_waiter(void *arg)
{
    (void)arg;
    int sig;
    sigwait(&quit_signals, &sig);
    atomic_store(&quit_signal, true);
    wake_main();
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
    fprintf(stderr, "usage: ofx [FOLDER]\n"
                    "  A file explorer: the folder (default: the current one) as a list.\n"
                    "  Enter, Right, l open; Backspace, Left, h up; j k, arrows, PgUp PgDn, Home End, g G move;\n"
                    "  Alt+Left/Right back and forward; / or Ctrl+F find, n N again; . or Ctrl+H hidden files;\n"
                    "  s sort by the next column, S reverse; Tab the places; t terminal here; ~ home;\n"
                    "  Space select, Ctrl+A all; F2 rename; Ctrl+Shift+N new folder; Delete trash, Shift+Delete for good;\n"
                    "  Ctrl+C, Ctrl+X, Ctrl+V copy, cut, paste; Ctrl+Z undo; Ctrl+L type a location; Alt+Up up;\n"
                    "  Ctrl+T new tab, Ctrl+W close it, Ctrl+Tab / Ctrl+PgDn next, Ctrl+Shift+Tab / Ctrl+PgUp previous,\n"
                    "  Alt+1..9 a tab, Ctrl+Shift+PgUp/PgDn move it; middle click a folder for a tab behind; Ctrl+Q quit.\n");
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
        fprintf(stderr, "ofx: %s: %s\n", start ? start : ".", strerror(errno));
        return 1;
    }

    // Before any thread exists (GLFW and Mesa start some), so all of them inherit the mask.
    sigemptyset(&quit_signals);
    sigaddset(&quit_signals, SIGTERM);
    sigaddset(&quit_signals, SIGINT);
    pthread_sigmask(SIG_BLOCK, &quit_signals, NULL);
    spawn_init();
    // OFX_TEST_LEAK=1 leaks one block on purpose: the control that shows LeakSanitizer reports
    // from this binary at all.
    const char *delay = getenv("OFX_TEST_WATCH_DELAY_MS");
    watch_delay_ms = delay ? atoi(delay) : 0;
    if (watch_delay_ms < 0 || watch_delay_ms > 1000) watch_delay_ms = 0;   // a window must never stall long
    // OFX_TEST_COPY_DELAY_MS: a sleep per copied block, so a test sees the progress (capped short:
    // a job never stalls the window, but a test must not run long).
    const char *cdelay = getenv("OFX_TEST_COPY_DELAY_MS");
    app.copy_delay_ms = cdelay ? atoi(cdelay) : 0;
    if (app.copy_delay_ms < 0 || app.copy_delay_ms > 500) app.copy_delay_ms = 0;
    // OFX_TEST_CROSS=1: a move copies as across drives, so a test can see a move run (F5.2).
    if (getenv("OFX_TEST_CROSS")) job_test_cross = true;
    // The test guard: with OFX_GUI_ROOT set (the GUI tests), no write outside it.
    ops_guard(getenv("OFX_GUI_ROOT"));
    const char *leak = getenv("OFX_TEST_LEAK");
    if (leak && leak[0]) planted = malloc(4096), planted = NULL;

    arena_init(&frame, "frame", frame_mem, sizeof frame_mem);
    arena_init(&lists[0], "list", list_mem[0], sizeof list_mem[0]);
    arena_init(&lists[1], "list", list_mem[1], sizeof list_mem[1]);
    arena_init(&data, "data", data_mem, sizeof data_mem);
    arena_init(&icona, "icons", icon_mem, sizeof icon_mem);
    arena_init(&scratch, "scratch", scratch_mem, sizeof scratch_mem);
    arena_init(&undoa, "undo", undo_mem, sizeof undo_mem);
    arena_init(&owna, "clipboard", own_mem, sizeof own_mem);
    arena_init(&pastea, "paste", paste_mem, sizeof paste_mem);
    arena_init(&doomeda, "delete", doomed_mem, sizeof doomed_mem);
    undo.arena = &undoa;
    uint32_t need = Clay_MinMemorySize();
    if ((size_t)need > sizeof clay_mem) {
        fprintf(stderr, "ofx: Clay needs %llu bytes, clay_mem has %zu\n", (unsigned long long)need, sizeof clay_mem);
        return 1;
    }

    SetTraceLogLevel(LOG_WARNING);
    // GLFW would draw window decorations through libdecor, whose GTK plugin loads GTK 3, Pango
    // and fontconfig into the process. Hyprland tiles without a title bar, so go without it.
    glfwInitHint(GLFW_WAYLAND_LIBDECOR, GLFW_WAYLAND_DISABLE_LIBDECOR);
    // HIGHDPI: without it raylib turns GLFW's framebuffer scaling off on Wayland, so a scale-2
    // output gets a 1x buffer stretched by the compositor.
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    // OFX_TEST_SIZE=WxH: the test compositor's kiosk shell rejects a first frame larger than its
    // output (tools/gui/weston.sh), so a test opens at the size it will get.
    int win_w = 1000, win_h = 700;
    const char *size = getenv("OFX_TEST_SIZE");
    if (size && sscanf(size, "%dx%d", &win_w, &win_h) != 2) win_w = 1000, win_h = 700;
    InitWindow(win_w, win_h, TextFormat("ofx: %s", dir));
    if (!IsWindowReady()) {
        fprintf(stderr, "ofx: cannot open a window\n");
        return 1;
    }
    SetExitKey(KEY_NULL);   // Ctrl+Q quits; Esc is free for later
    const char *dbg = getenv("OFX_GUI_DEBUG");
    debug_input = dbg && dbg[0];
    GLFWwindow *gw = glfwGetCurrentContext();
    raylib_key = glfwSetKeyCallback(gw, on_key);
    glfwSetWindowCloseCallback(gw, on_close);
    raylib_char = glfwSetCharCallback(gw, on_char);
    raylib_button = glfwSetMouseButtonCallback(gw, on_button);
    pthread_t waiter;
    pthread_create(&waiter, NULL, signal_waiter, NULL);
    pthread_detach(waiter);
    if (!font_init()) {
        fprintf(stderr, "ofx: fontconfig finds no usable sans-serif font\n");
        CloseWindow();
        return 1;
    }
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(sizeof clay_mem, clay_mem);
    Clay_Initialize(arena, (Clay_Dimensions){ (float)GetScreenWidth(), (float)GetScreenHeight() },
                    (Clay_ErrorHandler){ .errorHandlerFunction = on_clay_error });
    Clay_SetMeasureTextFunction(render_measure_text, NULL);
    render_custom = draw_custom;

    mime_load(&data, &app.mime_db);
    user_dirs_load(&data, &app.user_dirs);
    theme_apply();
    if (!watch_start()) set_status(true, "inotify is unavailable: no live refresh");
    clip_init();   // false: the clipboard stays inside ofx
    // OFX_TEST_COUNT_DELAY_MS: each count takes that much longer, so a test can change the listing
    // while answers for the old one are on their way, or (10 s) stand in for a hung mount at quit.
    const char *kdelay = getenv("OFX_TEST_COUNT_DELAY_MS");
    counts.delay_ms = kdelay ? atoi(kdelay) : 0;
    if (counts.delay_ms < 0 || counts.delay_ms > 10000) counts.delay_ms = 0;
    count_start();
    places_reload();
    ui_resize(&app.ui.list, list_rows(GetScreenHeight()));
    ui_resize(&app.ui.side, side_rows(GetScreenHeight()));
    app.press.row = -1;
    go_to(dir, STR_EMPTY, 0);
    if (!app.have_list) snprintf(app.cwd, sizeof app.cwd, "%s", dir);
    tabs[0].used = true;
    ui_tabs_open(&tabbar, 0, true);

    const char *script_path = getenv("OFX_GUI_SCRIPT");
    bool scripted = script_path && script_path[0];
    script_on = scripted;
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
        if (atomic_exchange(&watch.mounts_ready, false)) places_reload(), dirty = true;
        dirty |= jobs_poll();
        if (app.have_list && count_take(app.count, app.list.n)) dirty = true;
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
        in.crumb_n = app.crumbs.n, in.crumb_last = app.crumb_hi - 1;
        in.job_n = jobs.nrun, in.jobs_all = jobs.nrun + jobs.nwait;
        for (int k = 0; k < jobs.nrun; k++) in.job_id[k] = job_recs[jobs.run[k]].id;
        if (close_asked && in.nev < UI_EVENTS_MAX) in.ev[in.nev++] = (UiEvent){ UK_QUIT, 0 }, close_asked = false, seen = true;
        // A Ctrl+V in an edit line reads the clipboard first, and only then.
        bool pasting = false;
        for (int k = 0; k < in.nev; k++) pasting |= in.ev[k].key == UK_PASTE;
        if (pasting && app.ui.editing) in.paste = text_paste();
        bool was_editing = app.ui.editing, was_confirming = app.ui.confirming;
        UiAction a = ui_handle(&app.ui, (UiNames){ list_name, NULL }, &in);
        if (app.ui.copied) text_copy((Str){ app.ui.copy, app.ui.copy_len }), app.ui.copied = false;
        // Shift+Delete and its y can come in one frame's input: the question opened and was answered
        // within ui_handle, so the targets are taken here either way (reviewer-2, F5 confirm). A
        // new question always starts from no targets.
        if (!was_confirming && (app.ui.confirming || a.kind == UA_DELETE)) delete_ask();
        // F2 opened the edit line: on the cursor's entry, as it is named now.
        if (!was_editing && app.ui.editing && !app.ui.edit_path) snprintf(app.edit_from, sizeof app.edit_from, "%.*s", STR_ARG(sel_name())), app.edit_new = false;
        if (a.kind != UA_NONE) act(a);
        dirty |= seen;
    }
    if (scripted) script_end();
    jobs_stop();
    clip_quit();
    count_stop();
    for (int i = 0; i < app.nslots; i++)
        if (app.slot[i].tex.id) UnloadTexture(app.slot[i].tex);
    pthread_mutex_lock(&wake_mu);
    wake_off = true;
    pthread_mutex_unlock(&wake_mu);
    CloseWindow();
    return scripted && script.failed ? 1 : 0;
}
