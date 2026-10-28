// ofx: a file explorer, drawn natively on Wayland. One folder at a time, as a list. Clay lays out
// the window, render.c draws it with raylib. docs/plan.md has the plan; ui/ decides what input
// does, fs/ reads the folder, the MIME types, the icon theme and the Omarchy theme.
//
// One Clay unit is one logical pixel.
#include "../base/base.c"
#include "../fs/fs.c"
#include "../ui/ui.c"

#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Vendored, and included with -isystem so our warning flags do not apply to them. Clay's and
// nanosvg's code is compiled here, except in the GUI mutants' build, which has it once in its own
// object (gui/vendor.c).
#ifndef OFX_VENDOR_APART
#define CLAY_IMPLEMENTATION
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#endif
#include "clay.h"
#include "nanosvg.h"
#include "nanosvgrast.h"
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

// ms from now, for join_by.
static struct timespec deadline_in(int ms)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_nsec += (long)ms * 1000000L;
    t.tv_sec += t.tv_nsec / 1000000000L, t.tv_nsec %= 1000000000L;
    return t;
}

// A worker told to stop, joined by until; one that isn't done by then (stuck in a hung mount, say)
// is left to finish on its own.
static void join_by(pthread_t t, const struct timespec *until)
{
    if (pthread_timedjoin_np(t, NULL, until) != 0) pthread_detach(t);
}

// A wake-up at a time to come: the main loop sleeps until an event, so a tooltip's delay needs a
// thread that sleeps until then and wakes it (Michael, 2026-10-24).
static struct {
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    i64             due;   // now_ms() time; 0 for none
    bool            quit;
    pthread_t       th;
} timer = { .mu = PTHREAD_MUTEX_INITIALIZER };

static void *timer_main(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&timer.mu);
    while (!timer.quit) {
        if (!timer.due) {
            pthread_cond_wait(&timer.cv, &timer.mu);
            continue;
        }
        if (now_ms() >= timer.due) {
            timer.due = 0;
            pthread_mutex_unlock(&timer.mu);
            wake_main();
            pthread_mutex_lock(&timer.mu);
            continue;
        }
        struct timespec ts = { (time_t)(timer.due / 1000), (long)(timer.due % 1000) * 1000000L };
        pthread_cond_timedwait(&timer.cv, &timer.mu, &ts);   // the cv waits on CLOCK_MONOTONIC, as now_ms
    }
    pthread_mutex_unlock(&timer.mu);
    return NULL;
}

static void timer_start(void)
{
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_cond_init(&timer.cv, &ca);
    pthread_condattr_destroy(&ca);
    pthread_create(&timer.th, NULL, timer_main, NULL);
}

// A wake-up at due (now_ms() time); 0 cancels it.
static void timer_at(i64 due)
{
    pthread_mutex_lock(&timer.mu);
    timer.due = due;
    pthread_cond_signal(&timer.cv);
    pthread_mutex_unlock(&timer.mu);
}

static void timer_stop(void)
{
    pthread_mutex_lock(&timer.mu);
    timer.quit = true;
    pthread_cond_signal(&timer.cv);
    pthread_mutex_unlock(&timer.mu);
    pthread_join(timer.th, NULL);
}

#include "watch.c"
#include "clipboard.c"
#include "counts.c"
#include "thumbs.c"
#include "space.c"
#include "walk.c"
#include "search.c"

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
static u8 tree_mem[MiB(4)];        // the sidebar tree's paths and names; reset per build
static u8 bm_mem[MiB(1)], bmw_mem[MiB(1)];   // the bookmarks file as the menu lists it; a change's own read (F7.5)

static Arena frame, lists[2], data, icona, scratch, undoa, owna, pastea, treea, bma, bmwa;
static Bookmarks bmarks;   // the bookmarks file's lines (F7.5), read with the places
static int   watch_delay_ms;   // OFX_TEST_WATCH_DELAY_MS, for one test

enum { BAR_BTN_W = 34, BAR_BTN_H = 30, BAR_GAP = 4, ARROW_W = 22, CRUMB_PAD = 10, CRUMB_ICON_PX = 16 };
enum { TREE_STEP = 10, TRASH_BAR_H = 38, SEARCH_BAR_H = 38 };
// The menu bar (F7.1): a row of titles over the top bar; a menu's entries, its lines between groups.
enum { MENU_H = 26, MENU_TITLE_PAD = 10, MENU_ROW_H = 26, MENU_SEP_H = 9, MENU_W_MIN = 280 };
enum { TAB_H = 30, TAB_X_W = 22, JOB_ROW_H = 40, JOB_WAIT_H = 22, MODE_BTN_W = 28, MODE_BTN_H = 22, MODE_GAP = 4, FIND_BAR_H = 36 };
enum { FONT_PX = 15, ROW_H = 28, ICON_PX = 24, HEADER_H = 40, COLS_H = 26, FOOTER_H = 28, PAD = 12, SIZE_W = 90,
       DATE_W = 100, OWNER_W = 100, OWNER_PAD = 20, GROUP_W = 80, PERM_W = 84, SIDE_W = 200, SIDE_ROW_H = 28, SIDE_ICON_PX = 20, GAP = 10, ICONS_MAX = 512 };
// The icon view (F6.2): a cell holds its icon (48 px at first, F6.3's zoom from 32 to 256) over two
// lines of its name; the cursor's name in full, up to CELL_LINES_FULL lines, over the cells below it.
enum { CELL_W_MIN = 112, CELL_W_ROOM = 64, CELL_H_ROOM = 56, CELL_PAD = 4, CELL_LINE_H = FONT_PX + 4, CELL_LINES_FULL = 8 };
// The footer's zoom slider (F6.3): a minus, a track with a notch a size, a plus.
enum { ZOOM_BTN_W = 22, ZOOM_TRACK_W = 84, ZOOM_KNOB_W = 10 };

// ---- state ----------------------------------------------------------------------------------

typedef struct {
    Str        key;       // the MIME type, or a folder's icon name
    Texture2D  tex;       // id 0: no file for it, or it would not load
    char       file[PATH_MAX];
} IconSlot;

static struct {
    char      cwd[PATH_MAX];
    // The search view (F9): the folder searched, what the walk left out so far and whether it ended,
    // when the field's last change runs (0: none waiting), and the entry to put the cursor on when
    // it comes (back to a search, the one left from).
    char        search_root[PATH_MAX];
    SearchSkips search_s;
    bool        search_done;
    i64         search_due;
    char        search_pick[PATH_MAX];
    UiMatcher   search_m;     // its text as the walk matches it: a bad expression's reason, for the bar
    int       cur;          // lists[cur] holds the listing
    Listing   list;
    Str      *mime;         // per entry, filled as rows are drawn; .p NULL until then
    isize    *count;        // per entry, a folder's item count (counts.c): COUNT_UNKNOWN until asked
    bool      have_list;
    char      error[sizeof(Err)];   // why the folder could not be read
    Ui        ui;           // the list's and sidebar's cursors, find, the sort
    UiPress   press;
    UiPress   ow_press;   // Open With's rows (F7.3)
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
    int       reloads_asked;   // Reload picked or Ctrl+R (F7.4): the tests count these apart from the watcher's
    int       lists;        // listings shown and sorts: the find bar's count is counted again

    // The edit line's entry: its name when the line opened, and whether it is a folder just made
    // or file (Esc keeps "New folder"; the undo removes it whatever it is called by then).
    char      edit_from[NAME_MAX + 1];
    char      edit_dir[PATH_MAX];   // the folder it is in: the one shown, or a search result's (F9)
    UndoKind  edit_new;     // UNDO_MKDIR, UNDO_MKFILE (F7.1) for one just made, else UNDO_NONE
    bool      clip_told;    // "the clipboard stays inside ofx" said once
    UiCrumbs  crumbs;       // the top bar's path buttons
    int       crumb_lo, crumb_hi;   // the ones shown in the last frame
    bool      crumb_arrows;
    int       modal_hover;  // the modal panel's button under the pointer (UI_MODAL_*), 0 for none
    int       bmp_btn_hover;   // Edit Bookmarks' button under it (1..4), 0 for none (F7.5)
    bool      findx_hover;  // the find bar's × under it
    int       findstep_hover;   // its chevron under it: -1 the previous, 1 the next, 0 none
    UiBar     bar_hover;    // the top bar button under the pointer
    int       crumb_hover;
    int       tab_hover, tab_x_hover;   // the tab, and the tab's ×, under the pointer; -1 for none
    int       copy_delay_ms;   // OFX_TEST_COPY_DELAY_MS
    int       jobx_hover;      // the job row whose × is under the pointer, -1 for none
    int       mode_hover;      // the sidebar mode button under the pointer (a SideMode), -1 for none
    int       eject_hover;     // the sidebar row whose eject button is under the pointer, -1 for none
    char      text_copied[PATH_MAX];   // the last text an edit line put on the clipboard (F5.2)
    char      hover_num[32];   // the number drawn on the drive row under the pointer (F6.1), for the tests
    // What the last frame's icon view drew, for the tests (F6.2): its cells, the cursor's name's
    // lines, and the lines of the name under the pointer and whether it was cut.
    int       grid_cells, grid_full_lines, grid_hover_lines;
    bool      grid_hover_cut;
    isize     grid_first, grid_last;   // the entries in the first and last cell drawn, -1 for none
    int       icons_max;    // the icon cache's slots: ICONS_MAX, or OFX_TEST_ICONS_MAX for a test
    int       icon_full_resets;   // a full cache emptied at a listing (reviewer-2, F6.2 review), or a zoom
    // A lookup found the cache full; after a zoom or a view change that may empty it once, before
    // the next frame (reviewer-2, F6.3 review: zooming in one folder filled it, icons went blank).
    bool      icons_missed, icons_reset_armed;
    UiTip     tip;          // the tooltip of the icon button under the pointer (Michael, 2026-10-24)
    bool      zoomed;       // the icon size changed: the next refit keeps the cursor's row (ui_refit)
    bool      zoom_drag;    // the zoom slider's knob held (F6.3)
    bool      zoom_hover;   // the pointer over its track
    bool      thumb_local;  // the folder's drive is one ofx makes previews on (F6.4: not a network one)
    int       menu_title_hover;   // the menu bar title under the pointer, -1 for none (F7.1)
    float     ctx_x, ctx_y;       // a right-click menu's top left (F8): where it opens, kept inside the window
    bool      ctx_placed;         // one is up and placed (ctx_place)
    char      ctx_path[PATH_MAX]; // the folder a folder menu is on (ctx_facts)
    char      exe[PATH_MAX];      // this ofx's binary, for New Window (F7.1); "ofx" when unknown
    char      windows[PATH_MAX];  // the window registry's folder (fs/windows.c); "" for none
} app = { .tip = { -1, 0, false, false }, .hover = -1, .side_hover = -1, .tab_hover = -1, .tab_x_hover = -1, .jobx_hover = -1, .mode_hover = -1, .eject_hover = -1, .menu_title_hover = -1 };

static UiHistory hist;
static Tree      tree;     // the sidebar's tree mode (F6)

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

// Whether the menu bar is drawn: unless it is hidden (View → Show Menu, 2026-10-27), and then while
// one of its menus is open by its keys (the window below moves down for it, as Firefox's does).
static bool bar_shown(void)
{
    return !app.ui.bar_hidden || (app.ui.menu_on && app.ui.menu < UI_MENUS);
}

static int bar_h(void)
{
    return bar_shown() ? MENU_H : 0;
}

// What is above the list: the menu bar, the top bar, and the tab bar over the files from two tabs on.
static int top_h(void)
{
    return bar_h() + HEADER_H + (tabbar.n >= 2 ? TAB_H : 0);
}

// The sidebar's width: none when it is hidden (F6).
static int side_w(void)
{
    return app.ui.side_mode == SIDE_HIDDEN ? 0 : SIDE_W;
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

// The cursor's entry as listing_find finds it: its name, in the search view its folder and name (F9).
static Str sel_name(void)
{
    static char key[PATH_MAX];
    return app.have_list && app.ui.list.n > 0 ? entry_key(&app.list.e[app.ui.list.sel], key, sizeof key) : STR_EMPTY;
}

// Entry i's file: in the folder shown, in the trash view where the trash keeps it, or in the search
// view below the folder searched (F9). False when the path doesn't fit.
static bool entry_path(isize i, char *out, size_t cap)
{
    const Entry *e = &app.list.e[i];
    if (e->trash) {
        int n = snprintf(out, cap, "%s", e->trash->file);
        return n >= 0 && (size_t)n < cap;
    }
    if (app.ui.in_search) {
        char dir[PATH_MAX];
        if (e->dir.len && !path_put(dir, sizeof dir, app.search_root, e->dir)) return false;
        return path_put(out, cap, e->dir.len ? dir : app.search_root, e->name);
    }
    return path_put(out, cap, app.cwd, e->name);
}

static void tree_refresh(bool follow, int fallback);
static void icons_reset(void);

static TrashDirs trashd;   // the trash folders the trash view shows (F6)

// The trash folders there are now: the home trash, and each drive's.
static void trash_scan(void)
{
    const char *mp[PLACES_MAX];
    int n = 0;
    for (int i = 0; i < app.places.n; i++)
        if (place_is_drive(app.places.p[i].kind)) mp[n++] = app.places.p[i].path;
    trash_dirs(&trashd, mp, n);
}

// The search view's listing (F9): the matches so far, sorted. A search starts first when another
// one runs or ran (entering the view always does: leaving it asked for none).
static bool search_list(Arena *a, const char *uri, Listing *l, Err *err)
{
    Str root, text;
    char r[PATH_MAX];
    if (!search_parse(uri, &root, &text) || root.len >= PATH_MAX) return err_set(err, "Not a search"), false;
    snprintf(r, sizeof r, "%.*s", STR_ARG(root));
    if (!search_is(r, text, app.ui.find_opts, app.show_hidden)) {
        search_ask(r, text, app.ui.find_opts, app.show_hidden);
        ui_matcher_set(&app.search_m, text, app.ui.find_opts);
    }
    if (!search_copy(a, l, &app.search_s, &app.search_done)) return err_set(err, "%s: too many results for memory", r), false;
    fs_sort(l, app.ui.sort);
    snprintf(app.search_root, sizeof app.search_root, "%s", r);
    return true;
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
    bool in_trash = strcmp(dir, TRASH_URI) == 0, in_search = is_search(dir), same = strcmp(dir, app.cwd) == 0;
    if (in_trash) trash_scan();
    if (in_trash    ? !trash_list(&lists[next], &trashd, app.show_hidden, app.ui.sort, &l, &err)
        : in_search ? !search_list(&lists[next], dir, &l, &err)
                    : !fs_list(&lists[next], dir, app.show_hidden, app.ui.sort, &l, &err)) {
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
    // The same folder again: the selection is kept by name.
    isize marked = 0;
    for (isize i = 0; same && app.have_list && app.ui.marked && i < app.list.n; i++) {
        if (!app.ui.mark[i]) continue;
        char key[PATH_MAX];
        isize k = listing_find(&l, entry_key(&app.list.e[i], key, sizeof key));
        if (k >= 0 && !mark[k]) mark[k] = 1, marked++;
    }
    // The folder left, with its cursor, for Back: its name is still in the old listing's arena.
    if (!same && app.have_list && !stepping) hist_leave(&hist, app.cwd, sel_name());
    app.cur = next, app.list = l, app.mime = mime, app.have_list = true, app.error[0] = 0, app.lists++;
    app.ui.in_trash = in_trash;
    if (app.ui.in_search && !in_search) search_ask("", STR_EMPTY, app.ui.find_opts, false), app.search_due = 0;   // (out of it: the walk stops)
    if (!in_search) ui_unfocus(&app.ui, UI_FOCUS_SEARCH);
    if (in_search && !same) {   // the field shows its text; empty, it has the keys
        Str root, text;
        search_parse(dir, &root, &text);
        app.ui.search_len = (int)(text.len < UI_FIND_MAX ? text.len : UI_FIND_MAX);
        memcpy(app.ui.search, text.p, (size_t)app.ui.search_len);
        app.ui.search_caret = app.ui.search_anchor = app.ui.search_len;
        if (text.len == 0) app.ui.focus = UI_FOCUS_SEARCH;
        else ui_unfocus(&app.ui, UI_FOCUS_SEARCH);
        app.ui.search_edited = false, app.search_due = 0;
        snprintf(app.search_pick, sizeof app.search_pick, "%.*s", STR_ARG(select));
    }
    app.ui.in_search = in_search;
    app.ui.can_write = !in_trash && !in_search && access(dir, W_OK) == 0;   // the File menu's Create entries (F7.1)
    for (isize i = 0; i < l.n; i++) count[i] = COUNT_UNKNOWN;
    app.count = count;
    count_new_listing();
    ArenaMark mi_mark = arena_mark(&scratch);
    app.thumb_local = thumb_local(read_file(&scratch, mountinfo_path()), in_search ? app.search_root : dir);
    arena_restore(&scratch, mi_mark);
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
        // The trash view watches the home trash's info/ (a drive's trash changes from ofx reload it).
        char tinfo[PATH_MAX + 8] = "";
        if (in_trash && trash_home(tinfo, sizeof tinfo - 8)) strcat(tinfo, "/info");
        watch_dir(in_trash ? tinfo : in_search ? NULL : app.cwd);   // (a search isn't watched: Ctrl+R runs it again)
        // A change between the listing and the watch made no event: list once more.
        atomic_store(&watch.list_ready, true);
        glfwPostEmptyEvent();
        char shown[PATH_MAX + 1];
        ui_tilde(shown, sizeof shown, app.search_root, getenv("HOME"));
        SetWindowTitle(in_trash ? "ofx: Trash" : in_search ? TextFormat("ofx: Search in %s", shown) : TextFormat("ofx: %s", app.cwd));
    }
    ui_set(&app.ui.list, l.n, sel);
    // The find bar stays open in another folder; Esc there goes back to where this one opened, not
    // to a place in the last one's list.
    if (!same) app.ui.find_from = app.ui.list.sel;
    // A full icon cache would leave every icon not in it blank until a theme change: emptied as
    // another folder is entered, where a frame holds none of its slots, it fills again with what is
    // on screen. (A reload of the same folder shows the same icons.)
    if (!same && app.nslots >= app.icons_max) icons_reset(), app.icon_full_resets++;
    tree_refresh(!same && !in_search, app.ui.side.sel);
    // The drives' space moves with what changes on them; asked again with every listing.
    if (!same) app.cwd_avail = -1;
    space_ask(&app.places, app.cwd);
    return true;
}

// The search view's listing again, as more matches come (F9): the cursor stays on its entry, or
// goes to the one to pick when that comes.
static void search_relist(void)
{
    char keep[PATH_MAX];
    snprintf(keep, sizeof keep, "%.*s", STR_ARG(sel_name()));
    go_to_in(app.cwd, str_from_c(app.search_pick[0] ? app.search_pick : keep), app.ui.list.sel, true);
    if (app.search_done || (app.search_pick[0] && listing_find(&app.list, str_from_c(app.search_pick)) >= 0)) app.search_pick[0] = 0;
}

// The field's text, 150 ms after its last change (F9): searched for in place, the cursor on the
// first match (not a move in the history).
enum { SEARCH_TYPE_MS = 150 };

static void search_retype(void)
{
    char uri[PATH_MAX];
    if (!search_uri(uri, sizeof uri, app.search_root, (Str){ app.ui.search, app.ui.search_len })) {
        set_status(true, "That search is too long");
        return;
    }
    snprintf(app.cwd, sizeof app.cwd, "%s", uri);
    app.search_pick[0] = 0;
    go_to_in(app.cwd, STR_EMPTY, 0, true);
}

// Ctrl+R in the search view: the same search again.
static void search_again(void)
{
    char keep[PATH_MAX];
    snprintf(keep, sizeof keep, "%.*s", STR_ARG(sel_name()));
    snprintf(app.search_pick, sizeof app.search_pick, "%s", keep);
    Str root, text;
    if (search_parse(app.cwd, &root, &text)) search_ask(app.search_root, text, app.ui.find_opts, app.show_hidden);
    search_relist();
}

// The places read again (at the start, a drive mounted or gone): the sidebar's cursor stays on its
// place, found by path and kind, else on the first row that isn't a heading.
static void places_reload(void)
{
    // A folder menu open on a place was resolved against these places (ctx_facts: its row, its
    // bookmark line); read again, they may be another's, so it closes (F8 review, 2026-10-27: Nemo
    // adding a bookmark above made Remove take the wrong line).
    if (app.ui.menu_on && app.ui.menu == UI_CTX_FOLDER && app.ui.ctx_crumb < 0) app.ui.menu_on = false;
    Place keep = { .kind = PL_HEAD };   // in the tree mode the cursor is a tree row's: none kept
    if (app.ui.side_mode == SIDE_PLACES && app.ui.side.sel < app.places.n) keep = app.places.p[app.ui.side.sel];
    char bm[PATH_MAX];
    places_load(&scratch, &app.places, bookmarks_path(bm, sizeof bm), mountinfo_path());
    // The Bookmarks menu's (F7.5). With Edit Bookmarks up its cursor stays on its line, found
    // again as a change finds it (bm_locate), wherever a change made elsewhere moved it; the lines
    // as they were are copied into bmwa, which no change holds by now (they reload after writing).
    static Bookmarks prev;
    int bm_keep = -1;
    if (app.ui.bmp && app.ui.bmp_list.sel < bmarks.n) {
        arena_reset(&bmwa);
        prev.n = bmarks.n, prev.full = bmarks.full;
        for (int i = 0; i < bmarks.n; i++) {
            prev.b[i] = bmarks.b[i];
            prev.b[i].uri = (Str){ arena_cstr(&bmwa, bmarks.b[i].uri), bmarks.b[i].uri.len };
            prev.b[i].label = (Str){ arena_cstr(&bmwa, bmarks.b[i].label), bmarks.b[i].label.len };
        }
        bm_keep = (int)app.ui.bmp_list.sel;
    }
    arena_reset(&bma);
    bm_parse(&bma, read_file(&bma, bm), &bmarks);
    if (app.ui.bmp) {
        int at = bm_keep >= 0 ? bm_locate(&bmarks, &prev, bm_keep) : -1;
        if (at < 0) at = (int)app.ui.bmp_list.sel;
        ui_set(&app.ui.bmp_list, bmarks.n, at >= bmarks.n ? bmarks.n - 1 : at);
    }
    space_ask(&app.places, app.cwd);
    isize sel = 0;
    for (int i = 0; i < app.places.n; i++) {
        app.place_head[i] = app.places.p[i].kind == PL_HEAD;
        if (keep.kind != PL_HEAD && app.places.p[i].kind == keep.kind && strcmp(app.places.p[i].path, keep.path) == 0) sel = i;
    }
    app.place_cur = places_current(&app.places, app.cwd);
    if (app.ui.side_mode != SIDE_PLACES) return;
    app.ui.side_head = app.place_head;
    isize top = app.ui.side.top;
    ui_set(&app.ui.side, app.places.n, sel);
    app.ui.side.top = ui_clamp(top, 0, ui_max_top(&app.ui.side));
    ui_side_settle(&app.ui, 1);
}

// The menu's bookmark k: the k-th of the file's local ones (F7.5); NULL past them.
static const Bookmark *bm_menu(int k)
{
    for (int i = 0; i < bmarks.n; i++)
        if (bmarks.b[i].path[0] && k-- == 0) return &bmarks.b[i];
    return NULL;
}

// A bookmark's name in the menu: its label, else its folder's.
static Str bm_label(const Bookmark *b)
{
    return b->label.len ? b->label : strcmp(b->path, "/") == 0 ? S("/") : path_base(str_from_c(b->path));
}

// Bookmarks → Add Bookmark, Ctrl+D (F7.5): the folder shown (or dir, a folder's right-click menu,
// F8) at the end of the file, read again first so a change Nemo made meanwhile stays; the places
// (and the menu) read again after.
static void bookmark_add(const char *dir)
{
    char file[PATH_MAX];
    bookmarks_path(file, sizeof file);
    static Bookmarks now;
    arena_reset(&bmwa);
    Err err = { 0 };
    bool ok = bm_read(&bmwa, file, &now, &err) && bm_add(&bmwa, &now, dir, &err);
    Str text = ok ? bm_format(&bmwa, &now) : STR_EMPTY;
    if (ok && !text.len) err_set(&err, "the bookmarks are too long to write"), ok = false;   // (n > 0 after an add)
    if (!ok || !bm_write(file, text, &err)) {
        set_status(true, "No bookmark: %s", err.msg);
        return;
    }
    watch_bookmarks();   // its folder may be new
    places_reload();
    set_status(false, "Bookmarked %.*s", STR_ARG(path_base(str_from_c(dir))));
}

// The tree built again (tree mode only): opened down to the folder shown when `follow`, with the
// cursor on its row; else the cursor stays on its folder, or on the row `fallback`.
static void tree_refresh(bool follow, int fallback)
{
    if (app.ui.side_mode != SIDE_TREE) return;
    char keep[PATH_MAX] = "";
    if (!follow && app.ui.side.sel < tree.n) snprintf(keep, sizeof keep, "%.*s", STR_ARG(tree.row[app.ui.side.sel].path));
    if (follow) tree_follow(&tree, app.cwd);
    arena_reset(&treea);
    tree_build(&treea, &tree, S("/"), app.show_hidden);
    int sel = tree_find(&tree, str_from_c(follow ? app.cwd : keep));
    if (sel < 0) sel = fallback < tree.n ? fallback : 0;
    app.ui.side_head = NULL;
    isize top = app.ui.side.top;
    ui_set(&app.ui.side, tree.n, sel);
    if (!follow) app.ui.side.top = ui_clamp(top, 0, ui_max_top(&app.ui.side)), ui_show_sel(&app.ui.side);
}

// The sidebar after a change of mode: the places' rows, or the tree's opened to the folder shown.
static void side_refresh(void)
{
    app.ui.side.top = 0;
    if (app.ui.side_mode == SIDE_PLACES) places_reload();
    else if (app.ui.side_mode == SIDE_TREE) tree_refresh(true, 0);
}

// The folder of sidebar row i: a place's, or a tree row's; NULL for a heading or none.
static const char *side_path(isize i, char *buf, size_t cap)
{
    if (app.ui.side_mode == SIDE_TREE) {
        if (i < 0 || i >= tree.n) return NULL;
        snprintf(buf, cap, "%.*s", STR_ARG(tree.row[i].path));
        return buf;
    }
    if (i < 0 || i >= app.places.n || app.places.p[i].kind == PL_HEAD) return NULL;
    return app.places.p[i].path;
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
    if (app.ui.in_search) search_prune(app.search_root);   // (what went since: trashed, deleted, moved, F9)
    char keep[PATH_MAX] = "";
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
    if (app.ui.in_search) {   // the folder searched (F9)
        go_to(app.search_root, STR_EMPTY, 0);
        return;
    }
    if (strcmp(app.cwd, "/") == 0) return;
    Str cwd = str_from_c(app.cwd), child = path_base(cwd), parent = path_dir(cwd);
    char child_buf[NAME_MAX + 1], up[PATH_MAX];
    snprintf(child_buf, sizeof child_buf, "%.*s", STR_ARG(child));
    snprintf(up, sizeof up, "%.*s", STR_ARG(parent.len ? parent : S("/")));
    go_to(up, str_from_c(child_buf), 0);
}

static Str entry_mime(isize i);
static void ow_open(void);   // Open With (gui/openwith.c, F7.3)
static void ow_launch(isize i, bool def);
static void open_marked(void);

static void open_entry(isize i)
{
    const Entry *e = &app.list.e[i];
    char path[PATH_MAX];
    if (!entry_path(i, path, sizeof path)) {
        set_status(true, "%.*s: the path is too long", STR_ARG(e->name));
        return;
    }
    if (kind_is_dir(e->kind) && e->trash) {   // the trash view: a file opens, a folder doesn't
        set_status(true, "%.*s is in the trash: restore it to open it", STR_ARG(e->name));
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
    // An empty file: gio types it application/x-zerosize, for which no app is set, and refuses it.
    // Its name's type's default app instead (F7.1.1); none, and gio open says why.
    char desktop[PATH_MAX];
    if (e->size == 0 && (e->kind == K_FILE || e->kind == K_LINK_FILE) && apps_default(&scratch, entry_mime(i), desktop, sizeof desktop)) {
        if (spawn_launch(desktop, path, &err)) set_status(false, "opened %.*s", STR_ARG(e->name));
        else set_status(true, "%s", err.msg);
        return;
    }
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
    char keep[PATH_MAX];
    snprintf(keep, sizeof keep, "%.*s", STR_ARG(sel_name()));
    fs_sort(&app.list, app.ui.sort);
    app.lists++;
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
        if (entry_path(i, p, sizeof p)) out[n++] = arena_cstr(a, str_from_c(p));
    }
    return n;
}

static const char *items(isize n)
{
    return n == 1 ? "item" : "items";
}

#include "jobs.c"
#include "eject.c"

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
    if (app.ui.in_trash && app.ui.marked > CLIP_PATHS_MAX / 2) {   // each goes with its .trashinfo
        app.ui.confirming = false;
        set_status(true, "%td selected: ofx deletes at most %d at a time from the trash", app.ui.marked, CLIP_PATHS_MAX / 2);
        return;
    }
    arena_reset(&doomeda);
    doomed.n = targets(&doomeda, doomed.path, app.ui.in_trash ? CLIP_PATHS_MAX / 2 : CLIP_PATHS_MAX);
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
        if (app.ui.in_trash && str_eq(path_base(files), S("files")))
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
    if (ok == n && n == 1) set_status(false, "Restored %s", name);
    else if (ok == n) set_status(false, "Restored %td items", n);
    else set_status(true, "Restored %td of %td: %s", ok, n, first.msg[0] ? first.msg : "a job is using it");
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

// Whether a and b are the same folder, resolved.
static bool same_dir(const char *a, const char *b)
{
    char x[PATH_MAX], y[PATH_MAX];
    return realpath(a, x) && realpath(b, y) && strcmp(x, y) == 0;
}

// c's files copied (or, cut, moved) into the folder dest, as one job: there already, a copy gets a
// free name ("a (copy).txt", "a (copy 2).txt"), and a cut one stays. Paste (into this folder, or
// into one of its folders, F8), and Duplicate (F7.4).
static void copy_here(const ClipFiles *c, const char *into)
{
    JobRec *r = job_new();
    if (!r) return;
    isize n = 0, here = 0, self = 0, gone = 0;
    for (isize i = 0; i < c->n && n < CLIP_PATHS_MAX; i++) {
        const char *src = c->path[i];
        Str base = path_base(str_from_c(src)), sd = path_dir(str_from_c(src));
        char sdir[PATH_MAX], name[NAME_MAX + 1], dst[PATH_MAX];
        snprintf(sdir, sizeof sdir, "%.*s", STR_ARG(sd.len ? sd : S("/")));
        const char *dest = into ? into : sdir;
        struct stat st;
        if (lstat(src, &st) != 0) {
            gone++;
            continue;
        }
        if (S_ISDIR(st.st_mode) && into_itself(src, dest)) {
            self++;
            continue;
        }
        if (same_dir(sdir, dest)) {
            if (c->cut) {
                here++;
                continue;
            }
            if (!name_free(dest, base, S_ISDIR(st.st_mode), true, true, name, sizeof name)) continue;
        } else {
            snprintf(name, sizeof name, "%.*s", STR_ARG(base));
        }
        if (!path_put(dst, sizeof dst, dest, str_from_c(name))) continue;
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
    for (isize i = 0; i < n; i++) {
        char name[NAME_MAX + 8], one[NAME_MAX + 1];
        Str base = path_base(str_from_c(paths[i]));
        if (snprintf(name, sizeof name, "Link to %.*s", STR_ARG(base)) >= (int)sizeof name) {
            err_set(&err, "the name is too long for a link to %.*s", STR_ARG(base));
            continue;
        }
        Str d = path_dir(str_from_c(paths[i]));   // (beside it: a search result's own folder, F9)
        char in[PATH_MAX];
        snprintf(in, sizeof in, "%.*s", STR_ARG(d.len ? d : S("/")));
        if (!op_symlink(in, paths[i], str_from_c(name), one, sizeof one, &err) || !path_put(p, sizeof p, in, str_from_c(one))) continue;
        if (done++ == 0) undo_gen++, undo_begin(&undo, UNDO_LINK);
        undo_add(&undo, paths[i], p, NULL);
        snprintf(made, sizeof made, "%s", one);
    }
    if (done < n) set_status(true, "%s", err.msg);
    else if (done == 1) set_status(false, "Made %s", made);
    else set_status(false, "Made %td links", done);
    if (done) ui_mark_clear(&app.ui), go_to(app.cwd, app.ui.in_search ? sel_name() : str_from_c(made), app.ui.list.sel);
}

// Follow Link (F8): to the folder of what the link at entry i leads to, with it selected (every
// link on the way followed, as --select's folder is).
static void follow_link(isize i)
{
    char path[PATH_MAX], real[PATH_MAX], dir[PATH_MAX];
    if (i >= app.list.n || !entry_path(i, path, sizeof path)) return;
    if (!realpath(path, real)) {
        set_status(true, "%.*s leads nowhere: %s", STR_ARG(app.list.e[i].name), strerror(errno));
        return;
    }
    Str d = path_dir(str_from_c(real)), base = path_base(str_from_c(real));
    snprintf(dir, sizeof dir, "%.*s", STR_ARG(d.len ? d : S("/")));
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
    const char *in = app.edit_dir;
    char was[PATH_MAX], will[PATH_MAX];
    if (path_put(was, sizeof was, in, from) && jobs_refuse(was)) return;
    if (path_put(will, sizeof will, in, to) && jobs_refuse(will)) return;
    Err err;
    if (!op_rename(in, from, to, &err)) { set_status(true, "%s", err.msg); return; }
    // The cursor stays on it: by its new name, in a search its folder's too, where the result takes
    // the new name in place (F9).
    char a[PATH_MAX], b[PATH_MAX], keep[PATH_MAX];
    Str key = sel_name(), rel = path_dir(key);
    if (app.ui.in_search) search_rename(key, to);
    if (rel.len) snprintf(keep, sizeof keep, "%.*s/%.*s", STR_ARG(rel), STR_ARG(to));
    else snprintf(keep, sizeof keep, "%.*s", STR_ARG(to));
    if (path_put(a, sizeof a, in, from) && path_put(b, sizeof b, in, to)) {
        // A folder just made stays a "new folder" for undo, whatever its name now.
        undo_gen++;
        undo_begin(&undo, app.edit_new ? app.edit_new : UNDO_RENAME);
        undo_add(&undo, app.edit_new ? NULL : a, b, NULL);
    }
    ui_edit_close(&app.ui);
    set_status(false, "Renamed %s to %.*s", app.edit_from, STR_ARG(to));
    if (app.ui.in_search) search_prune(app.search_root);   // (a folder renamed: the results below it are gone)
    go_to(app.cwd, str_from_c(keep), app.ui.list.sel);
}

static void new_folder(void)
{
    char made[NAME_MAX + 1], p[PATH_MAX];
    Err err;
    if (!op_mkdir(app.cwd, S("New folder"), made, sizeof made, &err)) { set_status(true, "%s", err.msg); return; }
    if (path_put(p, sizeof p, app.cwd, str_from_c(made))) undo_gen++, undo_begin(&undo, UNDO_MKDIR), undo_add(&undo, NULL, p, NULL);
    ui_mark_clear(&app.ui);
    go_to(app.cwd, str_from_c(made), app.ui.list.sel);
    ui_edit_open_all(&app.ui, str_from_c(made));
    snprintf(app.edit_from, sizeof app.edit_from, "%s", made), snprintf(app.edit_dir, sizeof app.edit_dir, "%s", app.cwd);
    app.edit_new = UNDO_MKDIR;
}

// An empty file "New file", named in the row as a new folder is (F7.1).
static void new_file(void)
{
    char made[NAME_MAX + 1], p[PATH_MAX];
    Err err;
    if (!op_mkfile(app.cwd, S("New file"), made, sizeof made, &err)) { set_status(true, "%s", err.msg); return; }
    if (path_put(p, sizeof p, app.cwd, str_from_c(made))) undo_gen++, undo_begin(&undo, UNDO_MKFILE), undo_add(&undo, NULL, p, NULL);
    ui_mark_clear(&app.ui);
    go_to(app.cwd, str_from_c(made), app.ui.list.sel);
    ui_edit_open_all(&app.ui, str_from_c(made));
    snprintf(app.edit_from, sizeof app.edit_from, "%s", made), snprintf(app.edit_dir, sizeof app.edit_dir, "%s", app.cwd);
    app.edit_new = UNDO_MKFILE;
}

static void close_others(bool quit);
static void props_open(const char *path);
static void folder_do(UiMenuItem it);
static void props_chmod(int k);

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
    if (strcmp(path, TRASH_URI) != 0 && !is_search(path) && (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))) {
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

// F2 opened the edit line: on the cursor's entry, as it is named now, in its folder (a search
// result's own, F9).
static void edit_opened(void)
{
    const Entry *e = app.have_list && app.ui.list.n > 0 ? &app.list.e[app.ui.list.sel] : NULL;
    char path[PATH_MAX];
    snprintf(app.edit_from, sizeof app.edit_from, "%.*s", e ? (int)e->name.len : 0, e ? e->name.p : "");
    app.edit_new = UNDO_NONE;
    Str d = e && entry_path(app.ui.list.sel, path, sizeof path) ? path_dir(str_from_c(path)) : str_from_c(app.cwd);
    snprintf(app.edit_dir, sizeof app.edit_dir, "%.*s", STR_ARG(d.len ? d : S("/")));
}

// The location line's Enter: there, if it is a folder; else the line stays open with the reason.
static void location_go(void)
{
    Str to;
    char path[PATH_MAX];
    if (!ui_location(&scratch, (Str){ app.ui.edit, app.ui.edit_len }, app.ui.in_search ? app.search_root : app.cwd, getenv("HOME"), &to) || to.len >= PATH_MAX) {
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

static void bmp_act(UiAction a);   // Edit Bookmarks (gui/bookmarks.c, F7.5)

static void act(UiAction a)
{
    Err err;
    switch (a.kind) {
    case UA_RESTORE: trash_restore_targets(); break;
    case UA_EJECT:
        if (a.index >= 0 && a.index < app.places.n && place_ejectable(&app.places.p[a.index])) eject_start(&app.places.p[a.index]);
        break;
    case UA_EMPTY_TRASH: trash_empty(); break;
    case UA_NOT_IN_TRASH: set_status(true, "That doesn't work in the trash: restore it first"); break;
    case UA_NOT_IN_SEARCH: set_status(true, "A search isn't a folder: that works in one"); break;
    case UA_SEARCH: {   // below the folder shown, or (all) a folder entry's, in a tab of its own (F9)
        char uri[PATH_MAX], in[PATH_MAX];
        const char *dir = a.all ? (a.index < app.list.n && entry_path(a.index, in, sizeof in) ? in : NULL) : app.cwd;
        if (dir && search_uri(uri, sizeof uri, dir, STR_EMPTY)) tab_open(uri, STR_EMPTY, true);
        break;
    }
    case UA_CONTAINING: {   // a result's folder, it selected (F9)
        char path[PATH_MAX], dir[PATH_MAX];
        if (a.index >= app.list.n || !entry_path(a.index, path, sizeof path)) break;
        Str d = path_dir(str_from_c(path));
        snprintf(dir, sizeof dir, "%.*s", STR_ARG(d.len ? d : S("/")));
        go_to(dir, app.list.e[a.index].name, 0);
        break;
    }
    case UA_SIDE_MODE: {
        side_refresh();
        char path[PATH_MAX];
        if (state_path(path, sizeof path) && !state_put(&scratch, path, "sidebar", SIDE_MODE_NAMES[app.ui.side_mode], &err)) set_status(true, "%s", err.msg);
        break;
    }
    case UA_COLORS: {   // remembered, the one key (as UA_VIEW)
        char path[PATH_MAX];
        if (state_path(path, sizeof path) && !state_put(&scratch, path, "colors", app.ui.colors ? "on" : "off", &err)) set_status(true, "%s", err.msg);
        break;
    }
    case UA_MENUBAR: {   // remembered, the one key (as UA_VIEW)
        char path[PATH_MAX];
        if (state_path(path, sizeof path) && !state_put(&scratch, path, "menubar", app.ui.bar_hidden ? "off" : "on", &err)) set_status(true, "%s", err.msg);
        break;
    }
    case UA_PERMS_COLS: {   // remembered, the one key (as UA_VIEW)
        char path[PATH_MAX];
        if (state_path(path, sizeof path) && !state_put(&scratch, path, "perms", app.ui.perms_cols ? "on" : "off", &err)) set_status(true, "%s", err.msg);
        break;
    }
    case UA_GROUP_COL: {
        char path[PATH_MAX];
        if (state_path(path, sizeof path) && !state_put(&scratch, path, "group", app.ui.group_col ? "on" : "off", &err)) set_status(true, "%s", err.msg);
        break;
    }
    case UA_ZOOM:     // F6.3: as the view, the cursor kept on its row at the refit (ui_refit)
    case UA_VIEW: {   // F6.2: remembered; the loop lays it out before the next frame
        if (a.kind == UA_ZOOM) app.zoomed = true;
        app.icons_reset_armed = true;   // the other size's icons may not fit beside these
        // Only the key that changed: another window may have changed the others (F7.1).
        char path[PATH_MAX], zoom[16];
        snprintf(zoom, sizeof zoom, "%d", app.ui.zoom > 0 ? app.ui.zoom : UI_ZOOM_DEFAULT);
        if (state_path(path, sizeof path) &&
            !(a.kind == UA_ZOOM ? state_put(&scratch, path, "zoom", zoom, &err) : state_put(&scratch, path, "view", app.ui.grid ? "icons" : "list", &err)))
            set_status(true, "%s", err.msg);
        break;
    }
    case UA_BACK: hist_go(-1); break;
    case UA_FORWARD: hist_go(1); break;
    case UA_HIDDEN: {
        app.show_hidden = !app.show_hidden;
        char keep[PATH_MAX];
        snprintf(keep, sizeof keep, "%.*s", STR_ARG(sel_name()));
        go_to(app.cwd, str_from_c(keep), 0);
        break;
    }
    case UA_SORT: resort(); break;
    case UA_PLACE: {
        char buf[PATH_MAX];
        const char *p = side_path(a.index, buf, sizeof buf);
        if (p) go_to(p, STR_EMPTY, 0);
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
        // The cursor stays on its folder; one hidden by the close goes to the closed folder.
        tree_refresh(false, (int)a.index);
        break;
    }
    case UA_OPEN: a.all ? open_marked() : open_entry(a.index); break;
    case UA_PARENT: go_parent(); break;
    case UA_TERMINAL: {   // all: in the folder entry index (its right-click menu, F8)
        char in[PATH_MAX];
        const char *dir = a.all ? (a.index < app.list.n && entry_path(a.index, in, sizeof in) ? in : NULL) : app.ui.in_search ? app.search_root : app.cwd;
        if (dir && spawn_terminal(dir, &err)) set_status(false, "terminal in %s", dir);
        else if (dir) set_status(true, "%s", err.msg);
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
        // One action holds CLIP_PATHS_MAX paths: more would quietly do part of the selection.
        if (app.ui.marked > CLIP_PATHS_MAX) {
            set_status(true, "%td selected: ofx takes at most %d at a time", app.ui.marked, CLIP_PATHS_MAX);
            break;
        }
        if (a.kind == UA_TRASH) trash_targets();
        else copy_targets(a.kind == UA_CUT, NULL);
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
    case UA_TAB_OTHERS:   // (F8) from the last, so each place stays right as the ones after it go
        for (int i = tabbar.n - 1; i >= 0; i--)
            if (i != tabbar.cur) tab_close(i);
        break;
    case UA_TAB_BG_ROW: {
        char path[PATH_MAX];
        if (a.index < app.list.n && !app.ui.in_trash && entry_path(a.index, path, sizeof path)) tab_open_bg(path);
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
    case UA_LOCATION: ui_location_open(&app.ui, str_from_c(app.ui.in_search ? app.search_root : app.cwd)); break;
    case UA_GO: location_go(); break;
    case UA_CRUMB: crumb_go((int)a.index); break;
    case UA_CRUMB_SHIFT: ui_crumbs_shift(&app.crumbs, app.crumb_lo, (int)a.index); break;
    case UA_NEWFOLDER: new_folder(); break;
    case UA_NEWFILE: new_file(); break;
    case UA_NEW_WINDOW: {   // all: on the folder entry index (F8)
        char on[PATH_MAX];
        const char *dir = a.all ? (a.index < app.list.n && entry_path(a.index, on, sizeof on) ? on : NULL) : app.ui.in_search ? app.search_root : app.cwd;
        if (dir && spawn_window(app.exe, dir, &err)) set_status(false, "New window on %s", dir);
        else if (dir) set_status(true, "%s", err.msg);
        break;
    }
    case UA_CLOSE_ALL: close_others(a.all); break;
    case UA_PROPS: props_open(a.index == 1 ? app.cwd : a.index == 2 ? app.ctx_path : NULL); break;   // (F8: 1 the folder shown, 2 the folder menu's)
    case UA_CHMOD: props_chmod((int)a.index); break;
    case UA_OPEN_WITH: ow_open(); break;
    case UA_LAUNCH: ow_launch(a.index, a.all); break;
    case UA_PASTE: {   // all: into the folder entry index (F8)
        char into[PATH_MAX];
        if (!a.all) paste(app.cwd);
        else if (a.index < app.list.n && entry_path(a.index, into, sizeof into)) paste(into);
        break;
    }
    case UA_MAKE_LINK:
        if (app.ui.marked > CLIP_PATHS_MAX) set_status(true, "%td selected: ofx takes at most %d at a time", app.ui.marked, CLIP_PATHS_MAX);
        else make_link();
        break;
    case UA_FOLLOW: follow_link(a.index); break;
    case UA_FOLDER_DO: folder_do((UiMenuItem)a.index); break;
    case UA_DUPLICATE:
        if (app.ui.marked > CLIP_PATHS_MAX) set_status(true, "%td selected: ofx takes at most %d at a time", app.ui.marked, CLIP_PATHS_MAX);
        else duplicate();
        break;
    case UA_UNDO: undo_last(); break;
    case UA_RELOAD: app.reloads_asked++, app.ui.in_search ? search_again() : reload(); break;
    case UA_TRASH_VIEW: go_to(TRASH_URI, STR_EMPTY, 0); break;
    case UA_BM_EDIT: case UA_BM_GO: case UA_BM_NAME: case UA_BM_REMOVE: case UA_BM_UP: case UA_BM_DOWN: case UA_BM_RENAME: bmp_act(a); break;
    case UA_ADD_BOOKMARK: {   // all: the folder entry index (F8)
        char bm[PATH_MAX];
        if (!a.all) bookmark_add(app.cwd);
        else if (a.index < app.list.n && entry_path(a.index, bm, sizeof bm)) bookmark_add(bm);
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

// An SVG drawn into an image `want` px on its longer side (app icons, F7.3; nanosvg, vendored):
// into a static buffer, which LoadTextureFromImage copies. data NULL when it won't parse, or is
// over 1 MiB: nanosvg takes seconds on the main thread for a 10 MB one, and real icons are a few
// KiB (reviewer-2, F7.3.1 review).
static Image svg_image(const char *file, int want)
{
    enum { SVG_PX_MAX = 512, SVG_BYTES_MAX = 1 << 20 };
    static u8 pixels[SVG_PX_MAX * SVG_PX_MAX * 4];
    Image img = { 0 };
    struct stat st;
    if (stat(file, &st) != 0 || st.st_size > SVG_BYTES_MAX) return img;
    if (want > SVG_PX_MAX) want = SVG_PX_MAX;
    NSVGimage *svg = nsvgParseFromFile(file, "px", 96.0f);
    if (!svg) return img;
    float big = svg->width > svg->height ? svg->width : svg->height;
    NSVGrasterizer *r = big > 0 ? nsvgCreateRasterizer() : NULL;
    if (r) {
        float sc = (float)want / big;
        int w = (int)(svg->width * sc + 0.5f), h = (int)(svg->height * sc + 0.5f);
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        memset(pixels, 0, (size_t)w * (size_t)h * 4);
        nsvgRasterize(r, svg, 0, 0, sc, pixels, w, h, w * 4);
        img = (Image){ .data = pixels, .width = w, .height = h, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        nsvgDeleteRasterizer(r);
    }
    nsvgDelete(svg);
    return img;
}

// file into s's texture, px logical pixels on its longer side. A bigger file than drawn (F6.3:
// Yaru's 256 and 256@2x stand in for 64 to 256 px) is made the drawn size once here: the GPU's
// bilinear filter alone blurs a 4x shrink to grains, and holds four times the pixels, or sixteen.
static void icon_tex_load(IconSlot *s, const char *file, int px)
{
    snprintf(s->file, sizeof s->file, "%s", file);
    int want = px * app.icon_scale;
    if (str_ends_with(str_from_c(file), S(".svg"))) {
        Image img = svg_image(file, want);
        if (img.data) s->tex = LoadTextureFromImage(img);   // (the buffer is static: not unloaded)
    } else {
        Image img = LoadImage(file);
        int big = img.width > img.height ? img.width : img.height;
        if (img.data && big > want) ImageResize(&img, img.width * want / big, img.height * want / big);   // the longer side drawn
        if (img.data) s->tex = LoadTextureFromImage(img);
        UnloadImage(img);
    }
    if (s->tex.id) SetTextureFilter(s->tex, TEXTURE_FILTER_BILINEAR);
}

// The texture under key at px, from the first of names the theme has, loaded the first time it is
// needed; NULL when the cache is full. names NULL: they are the MIME type's (key). A size other
// than the list's is cached under "<px>:<key>".
static IconSlot *icon_for_px(Str key, const Str *given, isize ngiven, int px)
{
    int k;
    ArenaMark mark = arena_mark(&scratch);
    Str slot_key = px == ICON_PX ? key : str_from_c(arena_sprintf(&scratch, "%d:%.*s", px, STR_ARG(key)));
    if (strmap_get(&app.slot_of, slot_key, &k)) return arena_restore(&scratch, mark), &app.slot[k];
    if (app.nslots >= app.icons_max) return app.icons_missed = true, arena_restore(&scratch, mark), NULL;
    Str names[MIME_ICON_NAMES];
    isize n = given ? ngiven : mime_icon_names(&scratch, &app.mime_db, key, names);
    IconSlot *s = &app.slot[app.nslots];
    *s = (IconSlot){ .key = { arena_cstr(&icona, slot_key), slot_key.len } };
    const char *file = icon_find(&scratch, &app.icons, given ? given : names, n, px, app.icon_scale);
    if (file) icon_tex_load(s, file, px);
    arena_restore(&scratch, mark);
    strmap_put(&app.slot_of, s->key, app.nslots);
    return &app.slot[app.nslots++];
}

static IconSlot *icon_for(Str key, const Str *given, isize ngiven)
{
    return icon_for_px(key, given, ngiven, ICON_PX);
}

// The icon view's icon size and its cells' (F6.3): as wide as the icon and a little, a cell's width
// at least, so two lines of a name fit under a small one.
static int grid_px(void)
{
    return ui_zoom_px(&app.ui);
}

static int cell_w(void)
{
    int w = grid_px() + CELL_W_ROOM;
    return w > CELL_W_MIN ? w : CELL_W_MIN;
}

static int cell_h(void)
{
    return grid_px() + CELL_H_ROOM;
}

// The size the files' icons are drawn at: the list's, or the icon view's (F6.2).
static int entry_px(void)
{
    return app.ui.grid ? grid_px() : ICON_PX;
}

// The texture for entry i, at the view's size.
static IconSlot *entry_icon(isize i)
{
    const Entry *e = &app.list.e[i];
    int px = entry_px();
    if (!kind_is_dir(e->kind)) return icon_for_px(entry_mime(i), NULL, 0, px);
    Str names[2];
    isize n = 0;
    char path[PATH_MAX];
    Str special = !e->trash && entry_path(i, path, sizeof path) ? user_dir_icon(&app.user_dirs, path) : STR_EMPTY;
    if (special.len) names[n++] = special;
    names[n++] = S("folder");
    return icon_for_px(names[0], names, n, px);
}

// Entry i's preview in the icon view (F6.4), asked for when not known yet; NULL while there is none.
// draw: a cell on screen (else a page ahead).
static Texture2D *entry_thumb(isize i, bool draw)
{
    const Entry *e = &app.list.e[i];
    char path[PATH_MAX];
    if (kind_is_dir(e->kind) || e->kind == K_LINK_BROKEN || e->trash || !entry_path(i, path, sizeof path)) return NULL;
    return thumb_for(path, e->mtime, e->size, thumb_kind(entry_mime(i)), app.thumb_local, grid_px() * app.icon_scale, draw);
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

// The list's rows that fit, or the icon view's rows of cells (no column headers there).
static int list_rows(int h)
{
    int room = h - top_h() - FOOTER_H - jobs_h() - (app.ui.in_trash ? TRASH_BAR_H : 0) - (app.ui.in_search ? SEARCH_BAR_H : 0) -
               (app.ui.finding ? FIND_BAR_H : 0);
    int r = app.ui.grid ? (room - PAD) / cell_h() : (room - COLS_H) / ROW_H;
    return r > 0 ? r : 0;
}

// The icon view's columns at window width w (the list: 1).
static int list_cols(int w)
{
    if (!app.ui.grid) return 1;
    int c = (w - side_w() - 2 * PAD) / cell_w();
    return c > 1 ? c : 1;
}

// The list's rows, or the icon view's cells, there are places for on screen.
static int list_cells(void)
{
    return app.ui.list.rows * (int)ui_cols(&app.ui.list);
}

// The list's rows and columns for the window as it is now.
static int  ow_rows(int h);   // Open With (gui/openwith.c, F7.3)
static int  bmp_rows(int h);  // Edit Bookmarks (gui/bookmarks.c, F7.5)
static int  keys_rows(int h); // Keyboard Shortcuts (gui/shortcuts.c, F7.5)

static void list_fit(void)
{
    ui_set_cols(&app.ui.list, list_cols(GetScreenWidth()));
    ui_resize(&app.ui.list, list_rows(GetScreenHeight()));
    ui_resize(&app.ui.ow_list, ow_rows(GetScreenHeight()));   // Open With's list (F7.3)
    ui_resize(&app.ui.bmp_list, bmp_rows(GetScreenHeight()));   // Edit Bookmarks' (F7.5)
    ui_resize(&app.ui.keys_list, keys_rows(GetScreenHeight()));   // Keyboard Shortcuts'
}

static int side_rows(int h)
{
    int r = (h - bar_h() - HEADER_H - FOOTER_H - PAD) / SIDE_ROW_H;   // the tab bar isn't over the sidebar
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

// How full a drive is, 0..1.
static float drive_used(const Place *p)
{
    float used = p->size > 0 ? 1.0f - (float)p->avail / (float)p->size : 0;
    return used < 0 ? 0 : used > 1 ? 1 : used;
}

// The bar's colours, which its numbers take too (F6.1): the used part in the accent (red from 90%
// full on), the free part dimmed.
static Clay_Color drive_used_color(const Place *p)
{
    return drive_used(p) >= 0.9f ? cc(app.theme.red) : cc(app.theme.accent);
}

static Clay_Color drive_free_color(void)
{
    return mix(app.theme.fg_dim, app.theme.bg_light, 0.75f);
}

// A drive's fullness: a thin bar under its name.
static void drive_bar(int r, const Place *p, float w)
{
    CLAY(CLAY_IDI("side-bar", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(3) } },
                                    .backgroundColor = drive_free_color() }) {
        CLAY(CLAY_IDI("side-used", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(w * drive_used(p)), CLAY_SIZING_GROW(0) } },
                                         .backgroundColor = drive_used_color(p) }) {}
    }
}

// Whether sidebar row i has a folder menu open on it (F8): lit as the cursor is, so the menu's
// folder shows.
static bool side_ctx(isize i)
{
    return app.ui.menu_on && app.ui.menu == UI_CTX_FOLDER && app.ui.ctx_crumb < 0 && app.ui.ctx_side == i;
}

// The tree mode (F6): a row a folder, indented by its depth, with its open (▾) or closed (▸)
// mark; the folder shown in the accent.
static void layout_tree(float h)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("side"), { .layout = { .sizing = { CLAY_SIZING_FIXED(SIDE_W), CLAY_SIZING_FIXED(h) },
                                        .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = { 0, 0, PAD / 2, 0 } },
                            .clip = { .horizontal = true }, .backgroundColor = cc(t->bg_light) }) {
        UiList *sl = &app.ui.side;
        Str cwd = str_from_c(app.cwd);
        for (int r = 0; r < sl->rows && sl->top + r < sl->n; r++) {
            int i = (int)(sl->top + r);
            const TreeRow *row = &tree.row[i];
            bool cursor = (app.ui.focus == UI_FOCUS_SIDE && i == sl->sel) || side_ctx(i), here = str_eq(row->path, cwd), hover = r == app.side_hover;
            Clay_Color bg = cursor ? cc(t->selection) : hover ? cc(t->bg) : cc(t->bg_light);
            int indent = row->depth * TREE_STEP;   // deep names are cut short with "…"
            CLAY(CLAY_IDI("side-row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(SIDE_ROW_H) },
                                                        .padding = { (uint16_t)(PAD / 2 + indent), PAD, 0, 0 }, .childGap = 4,
                                                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                            .backgroundColor = bg,
                                            .border = { .color = cc(t->accent), .width = { .left = cursor ? 3 : 0 } } }) {
                CLAY(CLAY_IDI("side-twisty", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(SIDE_ROW_H) },
                                                               .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
                    text(cz(row->open ? "\xE2\x96\xBE" : "\xE2\x96\xB8"), cc(t->fg_dim));   // ▾ ▸
                }
                Str names[2];
                isize n = 0;
                char path[PATH_MAX];
                snprintf(path, sizeof path, "%.*s", STR_ARG(row->path));
                Str special = row->depth == 0 ? S("drive-harddisk") : user_dir_icon(&app.user_dirs, path);
                if (special.len) names[n++] = special;
                names[n++] = S("folder");
                icon_box(CLAY_IDI("side-icon", r), icon_for(names[0], names, n), SIDE_ICON_PX);
                float room = SIDE_W - PAD / 2 - indent - 16 - 4 - SIDE_ICON_PX - 4 - PAD;
                if (room > 0) text(fit(row->name, room, false), here ? cc(t->accent) : cc(t->fg));
            }
        }
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
        app.hover_num[0] = 0;
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
            bool cursor = (app.ui.focus == UI_FOCUS_SIDE && i == sl->sel) || side_ctx(i), here = i == app.place_cur, hover = r == app.side_hover;
            bool drive = place_is_drive(p->kind) && p->size > 0;
            Clay_Color bg = cursor ? cc(t->selection) : hover ? cc(t->bg) : cc(t->bg_light);
            // The free space; with the pointer over it, the used space (F6.1), each in its bar colour.
            char avail[32] = "";
            if (drive) ui_size(avail, sizeof avail, hover ? p->size - p->avail : p->avail);
            if (hover) snprintf(app.hover_num, sizeof app.hover_num, "%s", avail);
            Str free_s = str_from_c(arena_sprintf(&frame, "%s", avail));
            float room = SIDE_W - 2 * PAD - SIDE_ICON_PX - 8 - (drive ? str_width(free_s) + 8 : 0);
            CLAY(CLAY_IDI("side-row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(SIDE_ROW_H) },
                                                        .padding = { PAD, PAD, 0, 0 }, .childGap = 8,
                                                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                            .backgroundColor = bg,
                                            .border = { .color = cc(t->accent), .width = { .left = cursor ? 3 : 0 } } }) {
                if (hover && place_ejectable(p)) {   // in the icon's place while the pointer is over it
                    bool on = app.eject_hover == r;
                    CLAY(CLAY_IDI("side-eject", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(SIDE_ICON_PX), CLAY_SIZING_FIXED(SIDE_ICON_PX) },
                                                                  .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                                                      .backgroundColor = on ? cc(t->bg_light) : (Clay_Color){ 0, 0, 0, 0 },
                                                      .cornerRadius = CLAY_CORNER_RADIUS(3),
                                                      .border = { .color = on ? cc(t->accent) : mix(t->fg_dim, t->bg, 0.5f), .width = CLAY_BORDER_OUTSIDE(1) } }) {
                        text(cz("\xE2\x8F\x8F"), on ? cc(t->accent) : cc(t->fg));   // ⏏
                    }
                } else {
                    icon_box(CLAY_IDI("side-icon", r), place_icon(i), SIDE_ICON_PX);
                }
                CLAY(CLAY_IDI("side-text", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                                             .childGap = 3 } }) {
                    CLAY(CLAY_IDI("side-line", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 8 } }) {
                        text(fit(str_from_c(p->label), room, false), here ? cc(t->accent) : cc(t->fg));
                        if (drive) {
                            CLAY(CLAY_IDI("side-gap", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } } }) {}
                            text(cs(free_s), hover ? drive_used_color(p) : drive_free_color());
                        }
                    }
                    if (drive) drive_bar(r, p, SIDE_W - 2 * PAD - SIDE_ICON_PX - 8);
                }
            }
        }
    }
}

// The trash view's bar over the columns (F6): what it is, Restore and Empty Trash.
static void trash_button(const char *id, UiBar b, const char *label, bool danger)
{
    Theme *t = &app.theme;
    bool hover = app.bar_hover == b;
    CLAY(Clay_GetElementId(cz(id)), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(26) }, .padding = { 12, 12, 0, 0 },
                                                  .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                      .backgroundColor = hover ? cc(t->bg) : cc(t->bg_light),
                                      .border = { .color = danger ? cc(t->red) : mix(t->fg_dim, t->bg_light, 0.45f), .width = CLAY_BORDER_OUTSIDE(1) } }) {
        text(cz(label), danger && hover ? cc(t->red) : cc(t->fg));
    }
}

static void layout_trash_bar(void)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("trash-bar"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(TRASH_BAR_H) }, .padding = { PAD, PAD, 0, 0 },
                                             .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                 .backgroundColor = cc(t->bg_light) }) {
        text(cz("Trash"), cc(t->fg));
        text(cz("Restore puts the marked items back where they came from"), cc(t->fg_dim));
        CLAY(CLAY_ID("trash-gap"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } } }) {}
        if (app.list.n > 0) trash_button("trash-restore", UB_RESTORE, "Restore", false);
        if (app.list.n > 0 || app.list.hidden > 0) trash_button("trash-empty", UB_EMPTY, "Empty Trash", true);
    }
}

// The width Owner, Group (View → Show Group) and Permissions take in a row, their gaps included.
static float perms_w(void)
{
    return app.ui.group_col ? 3 * GAP + OWNER_W + GROUP_W + PERM_W : 2 * GAP + OWNER_W + PERM_W;
}

// Whether the list shows Owner and Permissions (and Group): View → Show Permissions on, and Name
// keeps NAME_MIN_W beside them in a window w wide (Michael, 2026-10-27: they give way on a narrow
// one, and the status line says so).
enum { NAME_MIN_W = 150 };   // (200 until Michael found it hid them beside a second window)

static bool perms_shown(float w)
{
    float rest = w - side_w() - 2 * PAD - ICON_PX - 3 * GAP - SIZE_W - DATE_W;
    return app.ui.perms_cols && rest - perms_w() >= NAME_MIN_W;
}

// A user's or a group's name, cached (the list's Owner and Group, 2026-10-27): a getpwuid per row
// and frame would be a passwd read each; the number when it has no name.
static const char *id_name(bool group, u32 id)
{
    enum { IDS_MAX = 32 };
    static struct { bool used, group; u32 id; char name[64]; } cache[IDS_MAX];
    static int next;
    for (int k = 0; k < IDS_MAX; k++)
        if (cache[k].used && cache[k].group == group && cache[k].id == id) return cache[k].name;
    int k = next++ % IDS_MAX;
    cache[k].used = true, cache[k].group = group, cache[k].id = id;
    char buf[4096];
    struct passwd pw, *pwp = NULL;
    struct group gr, *grp = NULL;
    if (!group && getpwuid_r(id, &pw, buf, sizeof buf, &pwp) == 0 && pwp) snprintf(cache[k].name, sizeof cache[k].name, "%s", pw.pw_name);
    else if (group && getgrgid_r(id, &gr, buf, sizeof buf, &grp) == 0 && grp) snprintf(cache[k].name, sizeof cache[k].name, "%s", gr.gr_name);
    else snprintf(cache[k].name, sizeof cache[k].name, "%u", (unsigned)id);
    return cache[k].name;
}

// The column headers: Name, Size, Modified, the sorted one with an arrow; a click sorts. Then Owner,
// Group (when shown) and Permissions, which don't sort (Michael, 2026-10-27).
static void layout_cols(void)
{
    Theme *t = &app.theme;
    const char *const NAMES[SORT_KEYS] = { "Name", "Size", app.ui.in_trash ? "Deleted" : "Modified" };
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
        static const struct { const char *id, *name; int w; } MORE[] = { { "col-owner", "Owner", OWNER_W }, { "col-group", "Group", GROUP_W },
                                                                         { "col-perms", "Permissions", PERM_W } };
        for (int k = 0; perms_shown((float)GetScreenWidth()) && k < COUNT(MORE); k++) {
            if (k == 1 && !app.ui.group_col) continue;
            CLAY(Clay_GetElementId(cz(MORE[k].id)), { .layout = { .sizing = { CLAY_SIZING_FIXED(MORE[k].w), CLAY_SIZING_GROW(0) },
                                                                  .padding = { k == 0 ? OWNER_PAD : 0, 0, 0, 0 },   // (clear of Modified, right-aligned)
                                                                  .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                text(cz(MORE[k].name), cc(t->fg_dim));
            }
        }
    }
}

// A run of the edit line's text: at most `most` wide (cut with "…" from the end, or from its
// start with keep_end), on the accent half mixed in when selected (the row under a rename has the
// selection colour already).
static void edit_run(Clay_ElementId sel_id, Str s, bool sel, float most, bool keep_end, Clay_Color fg)
{
    Theme *t = &app.theme;
    if (!s.len || most < 1) return;
    Clay_String txt = fit(s, most, keep_end);
    if (!sel) { text(txt, fg); return; }
    CLAY(sel_id, { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(FONT_PX + 6) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                   .backgroundColor = mix(t->accent, t->bg, 0.45f) }) {
        text(txt, fg);
    }
}

// The edit line's text in `room`: the selection marked (F5.2) and the caret at its place, kept in
// view (the text after it gets half the room at most, the ends cut with "…").
static void edit_text(const char *id, float room, UiLine line, Clay_Color fg)
{
    Theme *t = &app.theme;
    Str ed = { line.b, *line.len };
    int lo, hi, c = *line.caret;
    ui_line_sel(line, &lo, &hi);
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
    edit_run(sel, l1, false, w1, true, fg);
    edit_run(sel, l2, true, w2, true, fg);
    CLAY(Clay_GetElementId(cz(arena_sprintf(&frame, "%s-caret", id))), { .layout = { .sizing = { CLAY_SIZING_FIXED(2), CLAY_SIZING_FIXED(FONT_PX + 2) } },
                                                                          .backgroundColor = cc(t->accent) }) {}
    edit_run(sel, r1, true, v1, false, fg);
    edit_run(sel, r2, false, v2, false, fg);
}

// The edit line in place of the name.
static void layout_edit(Theme *t, float room)
{
    CLAY(CLAY_ID("edit"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ROW_H - 6) }, .padding = { 4, 4, 0, 0 },
                                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                            .clip = { .horizontal = true },
                            .backgroundColor = cc(t->bg), .border = { .color = cc(t->accent), .width = CLAY_BORDER_ALL(1) } }) {
        edit_text("edit", room - 12, ui_edit_line(&app.ui), cc(t->fg));
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
    if (ask && app.count[i] == COUNT_UNKNOWN && entry_path(i, path, sizeof path) && count_ask(i, path, app.show_hidden))
        app.count[i] = COUNT_ASKED;
    if (app.count[i] >= 0) snprintf(size, cap, "%td item%s", app.count[i], app.count[i] == 1 ? "" : "s");
    else if (app.count[i] == -1) snprintf(size, cap, "?");
}

// ---- ls's colours, as eza gives them (a trial, Michael 2026-10-27: his `ls` is eza, with no theme
// of its own, so its built-in colours on the terminal's palette, which Omarchy sets from colors.toml)

// A name's colour by its kind, as eza's: folders blue, links cyan, executables green, build files
// (Makefile, README…) yellow; images and videos magenta, music cyan, archives red; the rest plain.
static Clay_Color ls_name_colour(const Entry *e, isize i)
{
    Theme *t = &app.theme;
    if (e->kind == K_LINK_BROKEN) return cc(t->red);
    if (!app.ui.colors) return cc(t->fg);   // (View → Show Colors off)
    if (kind_is_dir(e->kind)) return e->kind == K_LINK_DIR ? cc(t->cyan) : cc(t->blue);
    if (e->kind == K_LINK_FILE) return cc(t->cyan);
    static const char *const BUILD[] = { "Makefile", "makefile", "GNUmakefile", "CMakeLists.txt", "meson.build", "build.gradle", "Cargo.toml",
                                         "package.json", "Dockerfile", "Justfile", "justfile", "Rakefile", "BUILD", "configure" };
    for (int k = 0; k < COUNT(BUILD); k++)
        if (str_eq(e->name, str_from_c(BUILD[k]))) return cc(t->yellow);
    if (str_starts_with(e->name, S("README")) || str_starts_with(e->name, S("readme"))) return cc(t->yellow);
    if (e->mode & 0111) return cc(t->green);
    Str type = entry_mime(i);
    if (str_starts_with(type, S("image/")) || str_starts_with(type, S("video/"))) return cc(t->magenta);
    if (str_starts_with(type, S("audio/"))) return cc(t->cyan);
    static const char *const PACKED[] = { "application/zip", "application/x-tar", "application/gzip", "application/x-xz", "application/x-bzip2",
                                          "application/zstd", "application/x-7z-compressed", "application/vnd.rar", "application/x-compressed-tar" };
    for (int k = 0; k < COUNT(PACKED); k++)
        if (str_eq(type, str_from_c(PACKED[k]))) return cc(t->red);
    return cc(t->fg);
}

// A Permissions cell in eza's colours: r yellow, w red, x green, s/S/t/T magenta, - dim.
static void ls_perms_cell(Clay_ElementId id, u32 mode, int w)
{
    Theme *t = &app.theme;
    char p[16];
    ui_perms(p, sizeof p, mode);
    CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIT(0) } }, .clip = { .horizontal = true } }) {
        for (int k = 0; p[k]; k++) {
            char c = p[k];
            Clay_Color col = !app.ui.colors ? cc(t->fg_dim) : c == 'r' ? cc(t->yellow) : c == 'w' ? cc(t->red) : c == 'x' ? cc(t->green) : c == '-' ? mix(t->fg_dim, t->fg, 0.25f) : cc(t->magenta);
            text(cz(arena_sprintf(&frame, "%c", c)), col);
        }
    }
}

// One of a row's Owner, Group, Permissions cells, pad on its left: s (copied into the frame arena: Clay reads the
// text after layout_row has returned) cut to w.
static void info_cell(Clay_ElementId id, const char *s, int w, int pad, Clay_Color col)
{
    CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIT(0) }, .padding = { pad, 0, 0, 0 } }, .clip = { .horizontal = true } }) {
        text(fit(str_from_c(arena_sprintf(&frame, "%s", s)), (float)(w - pad), false), col);
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
    size_cell(i, size, sizeof size, true);
    if (e->mtime) ui_when(when, sizeof when, e->mtime, now);
    bool link = e->kind == K_LINK_DIR || e->kind == K_LINK_FILE || e->kind == K_LINK_BROKEN;
    bool perms = perms_shown(w);
    float name_w = w - side_w() - 2 * PAD - ICON_PX - 3 * GAP - SIZE_W - DATE_W - (perms ? perms_w() : 0) - (link ? 16 : 0);
    CLAY(CLAY_IDI("row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ROW_H) },
                                           .padding = { PAD, PAD, 0, 0 }, .childGap = GAP,
                                           .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                               .backgroundColor = bg,
                               .border = { .color = cc(t->accent), .width = { .left = sel && app.ui.focus != UI_FOCUS_SIDE ? 3 : 0 } } }) {
        icon_box(CLAY_IDI("icon", r), icon, ICON_PX);
        CLAY(CLAY_IDI("name", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 6 } }) {
            if (sel && app.ui.focus == UI_FOCUS_EDIT && !app.ui.edit_path) {
                layout_edit(t, name_w);
            } else {
                text(fit(e->name, name_w, false), e->kind == K_LINK_BROKEN ? cc(t->red) : marked ? cc(t->accent) : sel ? cc(t->fg) : ls_name_colour(e, i));   // (the cursor's row plain: blue on the selection is faint)
                if (link) text(cz("\xE2\x86\x92"), cc(t->fg_dim));   // →
                if (link && e->target.len) {   // where it leads, as written, home as ~ (2026-10-27)
                    char to[PATH_MAX];
                    snprintf(to, sizeof to, "%.*s", STR_ARG(e->target));
                    char shown[PATH_MAX + 1];
                    ui_tilde(shown, sizeof shown, to, getenv("HOME"));
                    float room = name_w - str_width(e->name) - 12;
                    if (room > 40) text(fit(str_from_c(arena_sprintf(&frame, "%s", shown)), room, true), cc(t->fg_dim));
                }
                if (e->trash) {   // where it came from, home as ~
                    Str from = path_dir(str_from_c(e->trash->from));
                    const char *home = getenv("HOME");
                    Str h = str_from_c(home ? home : "");
                    char shown[PATH_MAX];
                    if (h.len > 1 && str_starts_with(from, h) && (from.len == h.len || from.p[h.len] == '/'))
                        snprintf(shown, sizeof shown, "~%.*s", STR_ARG(str_cut(from, h.len, from.len)));
                    else snprintf(shown, sizeof shown, "%.*s", STR_ARG(from.len ? from : S("/")));
                    float room = name_w - str_width(e->name) - 12;
                    if (room > 40) text(fit(str_from_c(arena_sprintf(&frame, "%s", shown)), room, true), cc(t->fg_dim));
                }
                if (e->dir.len) {   // the search view's (F9): its folder below the one searched
                    float room = name_w - str_width(e->name) - 12;
                    if (room > 40) text(fit(e->dir, room, true), cc(t->fg_dim));
                }
            }
        }
        CLAY(CLAY_IDI("size", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(SIZE_W), CLAY_SIZING_FIT(0) },
                                                .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
            text(cz(arena_sprintf(&frame, "%s", size)), app.ui.colors ? cc(t->green) : cc(t->fg_dim));   // (eza's)
        }
        CLAY(CLAY_IDI("date", r), { .layout = { .sizing = { CLAY_SIZING_FIXED(DATE_W), CLAY_SIZING_FIT(0) },
                                                .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
            text(cz(arena_sprintf(&frame, "%s", when)), app.ui.colors ? cc(t->blue) : cc(t->fg_dim));   // (eza's)
        }
        // Owner, Group (View → Show Group), Permissions (2026-10-27): the target's for a link, as Size is.
        if (perms) {
            // (eza's: you and your group yellow, others plain)
            info_cell(CLAY_IDI("owner", r), id_name(false, e->uid), OWNER_W, OWNER_PAD, app.ui.colors && e->uid == (u32)geteuid() ? cc(t->yellow) : cc(t->fg_dim));
            if (app.ui.group_col) info_cell(CLAY_IDI("group", r), id_name(true, e->gid), GROUP_W, 0, app.ui.colors && e->gid == (u32)getegid() ? cc(t->yellow) : cc(t->fg_dim));
            ls_perms_cell(CLAY_IDI("perms", r), e->mode, PERM_W);
        }
    }
}

// Lines of a name under its icon, centred: the first `most` of them, the last cut with "…" when
// the name doesn't end there (ui_wrap). How many, and *cut_out whether it was cut.
static int name_lines(const char *id, int k, Str name, int most, float w, Clay_Color color, bool *cut_out)
{
    Str dots = S("\xE2\x80\xA6");
    isize from[CELL_LINES_FULL], to[CELL_LINES_FULL];
    bool cut;
    int n = ui_wrap(name, w, str_width(dots), advance, most, from, to, &cut);
    for (int j = 0; j < n; j++) {
        Str line = str_cut(name, from[j], to[j]);
        Clay_String txt = cut && j == n - 1 ? cz(arena_sprintf(&frame, "%.*s%.*s", STR_ARG(line), STR_ARG(dots))) : cs(line);
        CLAY(Clay_GetElementIdWithIndex(cz(id), (uint32_t)(k * CELL_LINES_FULL + j)),
             { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(CELL_LINE_H) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
            text(txt, color);
        }
    }
    *cut_out = cut;
    return n;
}

// Entry i in cell k of the icon view (F6.2): its icon over its name. The cursor's name shows in
// full on the selection colour, over the cells below (Nemo's way); a rename's edit line is there,
// wider than the cell.
static void layout_cell(int k, isize i)
{
    const Entry *e = &app.list.e[i];
    Theme *t = &app.theme;
    bool sel = i == app.ui.list.sel, hover = k == app.hover, marked = ui_marked(&app.ui, i);
    Clay_Color fg = e->kind == K_LINK_BROKEN ? cc(t->red) : marked ? cc(t->accent) : cc(t->fg);
    Clay_Color bg = marked ? mix(t->accent, t->bg, 0.28f) : hover ? cc(t->bg_light) : (Clay_Color){ 0, 0, 0, 0 };
    int cw = cell_w(), px = grid_px();
    float name_w = (float)(cw - 2 * CELL_PAD);
    CLAY(CLAY_IDI("row", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(cw), CLAY_SIZING_FIXED(cell_h()) }, .padding = { CELL_PAD, CELL_PAD, 6, CELL_PAD },
                                           .childGap = 4, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
                               .backgroundColor = sel ? mix(t->selection, t->bg, 0.5f) : bg, .cornerRadius = CLAY_CORNER_RADIUS(4) }) {
        Texture2D *th = entry_thumb(i, true);
        if (th) {
            // In the icon's square, its shape kept: a texel a device pixel.
            float s = (float)app.icon_scale;
            CLAY(CLAY_IDI("icon", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(px), CLAY_SIZING_FIXED(px) },
                                                    .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
                CLAY(CLAY_IDI("thumb", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(th->width / s), CLAY_SIZING_FIXED(th->height / s) } },
                                             .image = { .imageData = th } }) {}
            }
        } else {
            icon_box(CLAY_IDI("icon", k), entry_icon(i), px);
        }
        if (sel && app.ui.focus == UI_FOCUS_EDIT && !app.ui.edit_path) {
            // As wide as the name and a little, from a cell's width to two cells'.
            float ew = str_width((Str){ app.ui.edit, app.ui.edit_len }) + 24;
            ew = ew < cw ? cw : ew > 2 * cw ? 2 * cw : ew;
            CLAY(CLAY_ID("cell-edit"), { .layout = { .sizing = { CLAY_SIZING_FIXED(ew), CLAY_SIZING_FIT(0) } },
                                         .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 3, .offset = { 0, 6 + (float)px + 4 },
                                                       .attachPoints = { CLAY_ATTACH_POINT_CENTER_TOP, CLAY_ATTACH_POINT_CENTER_TOP },
                                                       .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT } }) {
                layout_edit(t, ew);
            }
        } else if (sel) {
            // Its two lines' room stays taken, so the cell keeps its size.
            CLAY(CLAY_IDI("cell-room", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(2 * CELL_LINE_H) } } }) {}
            CLAY(CLAY_ID("cell-full"), { .layout = { .sizing = { CLAY_SIZING_FIXED(cw), CLAY_SIZING_FIT(0) }, .padding = { CELL_PAD, CELL_PAD, 0, 2 },
                                                     .layoutDirection = CLAY_TOP_TO_BOTTOM },
                                         .backgroundColor = mix(t->selection, t->bg, 0.5f), .cornerRadius = CLAY_CORNER_RADIUS(4),
                                         .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 3, .offset = { 0, 6 + (float)px + 4 },
                                                       .attachPoints = { CLAY_ATTACH_POINT_CENTER_TOP, CLAY_ATTACH_POINT_CENTER_TOP },
                                                       .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT } }) {
                bool cut;
                app.grid_full_lines = name_lines("cell-full-line", 0, e->name, CELL_LINES_FULL, name_w, fg, &cut);
                if (hover) app.grid_hover_lines = app.grid_full_lines, app.grid_hover_cut = cut;
            }
        } else {
            bool cut;
            int n = name_lines("cell-line", k, e->name, 2, name_w, fg, &cut);
            if (hover) app.grid_hover_lines = n, app.grid_hover_cut = cut;
        }
    }
    if (k == 0) app.grid_first = i;
    app.grid_last = i, app.grid_cells++;
}

// The icon view: cells across, then down, from the scroll's row.
static void layout_grid(void)
{
    int cols = (int)ui_cols(&app.ui.list);
    for (int r = 0; r < app.ui.list.rows; r++) {
        isize first = app.ui.list.top + (isize)r * cols;
        if (first >= app.list.n) break;
        CLAY(CLAY_IDI("grid-row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(cell_h()) }, .padding = { PAD, PAD, 0, 0 } } }) {
            for (int c = 0; c < cols && first + c < app.list.n; c++) layout_cell(r * cols + c, first + c);
        }
    }
    // The previews of a page ahead are asked for after the screen's (F6.4).
    isize ahead = app.ui.list.top + (isize)app.ui.list.rows * cols;
    for (isize i = ahead; i < ahead + (isize)app.ui.list.rows * cols && i < app.list.n; i++) entry_thumb(i, false);
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
typedef enum { CHEV_LEFT, CHEV_RIGHT, CHEV_UP, CHEV_DOWN } ChevronDir;
typedef enum { CUSTOM_CHEVRON = 1, CUSTOM_LINE, CUSTOM_TOOL } CustomKind;   // the first field of each custom's data
typedef enum { TOOL_SEARCH, TOOL_PENCIL } ToolDir;   // the top bar's search and location pictures

typedef struct {
    CustomKind kind;
    ToolDir    which;
    Clay_Color color;
} ToolDraw;

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

// The search button's magnifier and the location button's pencil, in lines as the chevrons are, so
// they take the theme's colours as the drawn buttons beside them do (Michael, 2026-10-24).
static void draw_tool(Clay_BoundingBox b, const ToolDraw *d)
{
    float cx = b.x + b.width / 2, cy = b.y + b.height / 2;
    Color col = rl(d->color);
    if (d->which == TOOL_SEARCH) {
        Vector2 c = { cx - 1.5f, cy - 1.5f };
        float r = 4.5f;
        DrawRing(c, r - 0.8f, r + 0.8f, 0, 360, 36, col);
        DrawLineEx((Vector2){ c.x + r * 0.72f, c.y + r * 0.72f }, (Vector2){ cx + 6, cy + 6 }, 2.2f, col);
        return;
    }
    // A pencil from the lower left to the upper right, along u: its tip, then its body, 3.4 wide.
    Vector2 u = { 0.7071f, -0.7071f }, n = { 0.7071f, 0.7071f }, t = { cx - 6, cy + 6 };
    Vector2 a = { t.x + u.x * 4, t.y + u.y * 4 }, z = { t.x + u.x * 16, t.y + u.y * 16 };
    DrawLineEx(a, z, 3.4f, col);
    Vector2 l = { a.x + n.x * 1.7f, a.y + n.y * 1.7f }, r = { a.x - n.x * 1.7f, a.y - n.y * 1.7f };
    DrawTriangle(t, l, r, col);   // raylib draws one winding only: either way round, one of these does
    DrawTriangle(t, r, l, col);
}

static void draw_custom(Clay_BoundingBox b, void *arg)
{
    if (!arg) return;
    if (*(const CustomKind *)arg == CUSTOM_LINE) { draw_line(b, arg); return; }
    if (*(const CustomKind *)arg == CUSTOM_TOOL) { draw_tool(b, arg); return; }
    const Chevron *c = arg;
    float cx = b.x + b.width / 2, cy = b.y + b.height / 2, r = 5;
    Color col = { (u8)c->color.r, (u8)c->color.g, (u8)c->color.b, (u8)c->color.a };
    Vector2 p0, p1, p2;
    switch (c->dir) {
    case CHEV_LEFT: p0 = (Vector2){ cx + r / 2, cy - r }, p1 = (Vector2){ cx - r / 2, cy }, p2 = (Vector2){ cx + r / 2, cy + r }; break;
    case CHEV_RIGHT: p0 = (Vector2){ cx - r / 2, cy - r }, p1 = (Vector2){ cx + r / 2, cy }, p2 = (Vector2){ cx - r / 2, cy + r }; break;
    case CHEV_DOWN: p0 = (Vector2){ cx - r, cy - r / 2 }, p1 = (Vector2){ cx, cy + r / 2 }, p2 = (Vector2){ cx + r, cy - r / 2 }; break;
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
    if (app.ui.focus == UI_FOCUS_CRUMBS) ui_crumbs_show(c, wd, room, ARROW_W, app.ui.crumb_at);
    app.crumb_arrows = ui_crumbs_window(c, wd, room, ARROW_W, &lo, &hi);
    app.crumb_lo = lo, app.crumb_hi = hi;
    // One outlined group, a line between the buttons, so they read as one path (Michael, 2026-10-20).
    Clay_Color line = mix(t->fg_dim, t->bg_light, 0.45f);
    CLAY(CLAY_ID("crumb-group"), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(BAR_BTN_H) } },
                                   .border = { .color = line, .width = { 1, 1, 1, 1, 1 } } }) {
    if (app.crumb_arrows) bar_button(CLAY_ID("crumb-left"), UB_LEFT, CHEV_LEFT, NULL, NULL, lo > 0, ARROW_W);
    for (int i = lo; i < hi; i++) {
        bool cur = i == c->cur, hover = app.bar_hover == UB_CRUMB && app.crumb_hover == i;
        bool keyed = app.ui.focus == UI_FOCUS_CRUMBS && app.ui.crumb_at == i;   // Ctrl+K's mark: an accent outline
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
        edit_text("location", room - 24, ui_edit_line(&app.ui), cc(app.theme.fg));
    }
}

// The location and search buttons, drawn (draw_tool), marked in the accent while theirs is open.
static void tool_button(const char *id, UiBar which, ToolDir pic, bool on)
{
    Theme *t = &app.theme;
    bool hover = app.bar_hover == which;
    ToolDraw *d = arena_new(&frame, ToolDraw, 1);
    *d = (ToolDraw){ CUSTOM_TOOL, pic, on ? cc(t->accent) : hover ? cc(t->fg) : cc(t->fg_dim) };
    CLAY(Clay_GetElementId(cz(id)), { .layout = { .sizing = { CLAY_SIZING_FIXED(BAR_BTN_W), CLAY_SIZING_FIXED(BAR_BTN_H) },
                                                  .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                                      .backgroundColor = on ? mix(t->accent, t->bg_light, 0.25f) : hover ? cc(t->bg) : cc(t->bg_light) }) {
        CLAY(Clay_GetElementId(cz(arena_sprintf(&frame, "%s-pic", id))), { .layout = { .sizing = { CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(16) } },
                                                                             .custom = { .customData = d } }) {}
    }
}

// A view button's picture (F6.2), drawn in c: three lines for the list, four squares for the icons
// (the theme has its view icons as SVG only, which ofx doesn't draw).
static void view_picture(bool icons, Clay_Color c)
{
    const char *id = icons ? "view-pic-icons" : "view-pic-list";
    CLAY(Clay_GetElementId(cz(id)), { .layout = { .sizing = { CLAY_SIZING_FIXED(14), CLAY_SIZING_FIXED(12) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                                  .childGap = icons ? 2 : 3, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        for (int j = 0; j < (icons ? 2 : 3); j++) {
            if (!icons) {
                CLAY(CLAY_IDI("view-line", j), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(2) } }, .backgroundColor = c }) {}
                continue;
            }
            CLAY(CLAY_IDI("view-sq-row", j), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(5) }, .childGap = 2 } }) {
                for (int q = 0; q < 2; q++)
                    CLAY(CLAY_IDI("view-sq", j * 2 + q), { .layout = { .sizing = { CLAY_SIZING_FIXED(6), CLAY_SIZING_FIXED(5) } }, .backgroundColor = c }) {}
            }
        }
    }
}

// The list and icon buttons, the view shown marked in the accent.
static void view_button(const char *id, UiBar which, bool icons)
{
    Theme *t = &app.theme;
    bool on = app.ui.grid == icons, hover = app.bar_hover == which;
    CLAY(Clay_GetElementId(cz(id)), { .layout = { .sizing = { CLAY_SIZING_FIXED(BAR_BTN_W), CLAY_SIZING_FIXED(BAR_BTN_H) },
                                                  .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                                      .backgroundColor = on ? mix(t->accent, t->bg_light, 0.25f) : hover ? cc(t->bg) : cc(t->bg_light) }) {
        view_picture(icons, on ? cc(t->accent) : hover ? cc(t->fg) : cc(t->fg_dim));
    }
}

// ---- the zoom slider (F6.3) ------------------------------------------------------------------

// Its width at the footer's right end; 0 in the list, which doesn't zoom.
static float zoom_w(void)
{
    return app.ui.grid ? 2 * ZOOM_BTN_W + ZOOM_TRACK_W + 2 * 4 + PAD : 0;
}

// The knob's middle for level k, from the track's left.
static float zoom_x(int k)
{
    return ZOOM_KNOB_W / 2.0f + (float)k * (float)(ZOOM_TRACK_W - ZOOM_KNOB_W) / (UI_ZOOMS - 1);
}

// The level nearest x on the track as last drawn; -1 when it isn't there.
static int zoom_level_at(float x)
{
    Clay_ElementData d = Clay_GetElementData(CLAY_ID("zoom-track"));
    if (!d.found) return -1;
    float step = (float)(ZOOM_TRACK_W - ZOOM_KNOB_W) / (UI_ZOOMS - 1);
    int k = (int)lroundf((x - d.boundingBox.x - ZOOM_KNOB_W / 2.0f) / step);
    return k < 0 ? 0 : k >= UI_ZOOMS ? UI_ZOOMS - 1 : k;
}

// The minus or the plus, drawn; dim at its end of the sizes.
static void zoom_button(const char *id, UiBar which, bool plus, bool enabled)
{
    Theme *t = &app.theme;
    bool hover = enabled && app.bar_hover == which;
    Clay_Color c = !enabled ? dim_color() : hover ? cc(t->fg) : cc(t->fg_dim);
    CLAY(Clay_GetElementId(cz(id)), { .layout = { .sizing = { CLAY_SIZING_FIXED(ZOOM_BTN_W), CLAY_SIZING_FIXED(MODE_BTN_H) },
                                                  .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                                      .backgroundColor = hover ? cc(t->bg) : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(3) }) {
        CLAY(Clay_GetElementId(cz(arena_sprintf(&frame, "%s-bar", id))), { .layout = { .sizing = { CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(2) } },
                                                                             .backgroundColor = c }) {
            if (plus)
                CLAY(CLAY_ID("zoom-in-stem"), { .layout = { .sizing = { CLAY_SIZING_FIXED(2), CLAY_SIZING_FIXED(10) } }, .backgroundColor = c,
                                                .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 1,
                                                              .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER } } }) {}
        }
    }
}

// The slider: a line with a notch a size, the knob on the size shown, the minus and plus beside.
static void layout_zoom(void)
{
    Theme *t = &app.theme;
    int k = ui_zoom_level(app.ui.zoom);
    Clay_Color line = mix(t->fg_dim, t->bg_light, 0.45f);
    CLAY(CLAY_ID("zoom"), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_GROW(0) }, .padding = { PAD, 0, 0, 0 }, .childGap = 4,
                                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        zoom_button("zoom-out", UB_ZOOM_OUT, false, k > 0);
        CLAY(CLAY_ID("zoom-track"), { .layout = { .sizing = { CLAY_SIZING_FIXED(ZOOM_TRACK_W), CLAY_SIZING_FIXED(MODE_BTN_H) } } }) {
            CLAY(CLAY_ID("zoom-line"), { .layout = { .sizing = { CLAY_SIZING_FIXED(ZOOM_TRACK_W - ZOOM_KNOB_W), CLAY_SIZING_FIXED(2) } }, .backgroundColor = line,
                                         .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 1,
                                                       .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER } } }) {}
            for (int j = 0; j < UI_ZOOMS; j++)
                CLAY(CLAY_IDI("zoom-notch", j), { .layout = { .sizing = { CLAY_SIZING_FIXED(2), CLAY_SIZING_FIXED(8) } }, .backgroundColor = line,
                                                  .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 1, .offset = { zoom_x(j) - 1, 0 },
                                                                .attachPoints = { CLAY_ATTACH_POINT_LEFT_CENTER, CLAY_ATTACH_POINT_LEFT_CENTER } } }) {}
            bool lit = app.zoom_hover || app.zoom_drag;
            CLAY(CLAY_ID("zoom-knob"), { .layout = { .sizing = { CLAY_SIZING_FIXED(ZOOM_KNOB_W), CLAY_SIZING_FIXED(14) } },
                                         .backgroundColor = lit ? cc(t->fg) : cc(t->accent), .cornerRadius = CLAY_CORNER_RADIUS(3),
                                         .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 2, .offset = { zoom_x(k) - ZOOM_KNOB_W / 2.0f, 0 },
                                                       .attachPoints = { CLAY_ATTACH_POINT_LEFT_CENTER, CLAY_ATTACH_POINT_LEFT_CENTER } } }) {}
        }
        zoom_button("zoom-in", UB_ZOOM_IN, true, k < UI_ZOOMS - 1);
    }
}

static void layout_bar(float w)
{
    Theme *t = &app.theme;
    bool can_up = app.ui.in_search || strcmp(app.cwd, "/") != 0;
    CLAY(CLAY_ID("header"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(HEADER_H) },
                                          .padding = { PAD / 2, PAD / 2, 0, 0 }, .childGap = BAR_GAP,
                                          .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                              .backgroundColor = cc(t->bg_light) }) {
        bar_button(CLAY_ID("nav-back"), UB_BACK, CHEV_LEFT, NULL, NULL, hist.nback > 0, BAR_BTN_W);
        bar_button(CLAY_ID("nav-fwd"), UB_FORWARD, CHEV_RIGHT, NULL, NULL, hist.nfwd > 0, BAR_BTN_W);
        bar_button(CLAY_ID("nav-up"), UB_UP, CHEV_UP, NULL, NULL, can_up, BAR_BTN_W);
        float room = w - PAD - 7 * BAR_BTN_W - 9 * BAR_GAP - 2 * PAD;
        CLAY(CLAY_ID("crumbs"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(BAR_BTN_H) }, .padding = { PAD / 2, 0, 0, 0 },
                                              .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                  .clip = { .horizontal = true } }) {
            if (app.ui.focus == UI_FOCUS_EDIT && app.ui.edit_path) layout_location(room);
            else layout_crumbs(room);
        }
        tool_button("loc-toggle", UB_LOCATION, TOOL_PENCIL, app.ui.focus == UI_FOCUS_EDIT && app.ui.edit_path);
        tool_button("search", UB_SEARCH, TOOL_SEARCH, app.ui.finding);
        view_button("view-list", UB_VIEW_LIST, false);
        view_button("view-icons", UB_VIEW_ICONS, true);
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
            Str name = strcmp(dir, "/") == 0 ? S("/") : path_base(str_from_c(dir)), root, what;
            if (search_parse(dir, &root, &what))   // a search (F9): what it looks for, and where
                name = str_from_c(what.len ? arena_sprintf(&frame, "%.*s in %.*s", STR_ARG(what), STR_ARG(path_base(root)))
                                           : arena_sprintf(&frame, "Search in %.*s", STR_ARG(root.len == 1 ? root : path_base(root))));
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

// ---- the menu bar (F7.1) ----------------------------------------------------------------------

// Menu m's entries under its title: each label on the left and its key on the right, those that
// can't be picked now dimmed, the marked one in the selection colour; lines between the groups.
// A menu with checks or a group's dots keeps a column for them on the left (F7.4); an entry not
// shown now (Restore outside the trash view) is left out.
static void layout_menu(int m)
{
    Theme *t = &app.theme;
    Clay_Color off = mix(t->fg_dim, t->bg_light, 0.45f);
    // A bar menu hangs from its title; a right-click one floats where ctx_place put it (F8).
    Clay_FloatingElementConfig where = m < UI_MENUS ? (Clay_FloatingElementConfig){ .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 30, .offset = { 0, 2 },
                                                                                  .attachPoints = { CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_BOTTOM } }
                                                 : (Clay_FloatingElementConfig){ .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 30, .offset = { app.ctx_x, app.ctx_y } };
    CLAY(CLAY_ID("menu"), { .layout = { .sizing = { CLAY_SIZING_FIT(MENU_W_MIN, 0), CLAY_SIZING_FIT(0) }, .padding = { 4, 4, 4, 4 },
                                        .layoutDirection = CLAY_TOP_TO_BOTTOM },
                            .backgroundColor = cc(t->bg_light),
                            .border = { .color = mix(t->fg_dim, t->bg_light, 0.6f), .width = CLAY_BORDER_OUTSIDE(1) },
                            .floating = where }) {
        bool marks = false;
        for (int i = 0; i < UI_MENU[m].n; i++) marks |= UI_MENU_ITEM[UI_MENU[m].items[i]].mark != 0;
        for (int i = 0; i < UI_MENU[m].n; i++) {
            UiMenuItem it = UI_MENU[m].items[i];
            if (!ui_menu_shown(&app.ui, it) || (it == UM_SEP && !ui_menu_sep_shown(&app.ui, m, i))) continue;
            if (it == UM_SEP) {
                CLAY(CLAY_IDI("menu-sep", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(MENU_SEP_H) }, .padding = { 8, 8, 4, 4 } } }) {
                    CLAY(CLAY_IDI("menu-line", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } },
                                                     .backgroundColor = mix(t->fg_dim, t->bg_light, 0.3f) }) {}
                }
                continue;
            }
            bool on = ui_menu_enabled(&app.ui, it), at = on && app.ui.menu_at == i;
            CLAY(CLAY_IDI("menu-item", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(MENU_ROW_H) }, .padding = { 10, 10, 0, 0 },
                                                         .childGap = 32, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                             .backgroundColor = at ? cc(t->selection) : cc(t->bg_light), .cornerRadius = CLAY_CORNER_RADIUS(3) }) {
                CLAY(CLAY_IDI("menu-label", i), { .layout = { .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                    if (marks) {
                        int mk = ui_menu_mark(&app.ui, it);
                        CLAY(CLAY_IDI("menu-mark", i), { .layout = { .sizing = { CLAY_SIZING_FIXED(14), CLAY_SIZING_FIT(0) } } }) {
                            // ✓ for a check, • for the current one of a group
                            if (mk == 1) text(cz(UI_MENU_ITEM[it].mark == 'c' ? "\xE2\x9C\x93" : "\xE2\x80\xA2"), on ? cc(t->accent) : off);
                        }
                    }
                    const Bookmark *bk = it >= UM_BM_0 && it <= UM_BM_7 ? bm_menu(it - UM_BM_0) : NULL;
                    if (it == UM_BM_MORE) text(cz(arena_sprintf(&frame, "%d more in Edit Bookmarks\xE2\x80\xA6", app.ui.bm_more)), on ? cc(t->fg) : off);
                    else text(bk ? fit(bm_label(bk), 360, true) : cz(UI_MENU_ITEM[it].label), on ? cc(t->fg) : off);
                }
                CLAY(CLAY_IDI("menu-gap", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } } }) {}
                if (UI_MENU_ITEM[it].key[0]) text(cz(UI_MENU_ITEM[it].key), on ? cc(t->fg_dim) : off);
            }
        }
    }
}

// The menu bar over the top bar (always shown, Michael 2026-10-25): the menus' titles, the open
// one's menu hanging from it.
static void layout_menubar(void)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("menubar"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(MENU_H) }, .padding = { PAD / 2, PAD / 2, 0, 0 },
                                           .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                               .backgroundColor = cc(t->bg_light),
                               .border = { .color = mix(t->fg_dim, t->bg_light, 0.2f), .width = { .bottom = 1 } } }) {
        for (int m = 0; m < UI_MENUS; m++) {
            bool open = app.ui.menu_on && app.ui.menu == m, hover = app.menu_title_hover == m;
            CLAY(CLAY_IDI("menu-title", m), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(MENU_H - 4) },
                                                          .padding = { MENU_TITLE_PAD, MENU_TITLE_PAD, 0, 0 },
                                                          .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                              .backgroundColor = open ? cc(t->selection) : hover ? cc(t->bg) : cc(t->bg_light),
                                              .cornerRadius = CLAY_CORNER_RADIUS(3) }) {
                text(cz(UI_MENU[m].title), cc(t->fg));
                if (open) layout_menu(m);
            }
        }
    }
}

// ---- the modal panels (F5.1) --------------------------------------------------------------------

typedef enum { MODAL_NONE, MODAL_DELETE, MODAL_CLASH, MODAL_CANCEL, MODAL_QUIT, MODAL_FULL, MODAL_EMPTY, MODAL_PROPS, MODAL_OW, MODAL_BM, MODAL_KEYS, MODAL_ABOUT, MODAL_BAR } ModalKind;

// The panel on top, as ui_on_top has it.
static ModalKind modal_kind(void)
{
    switch (ui_on_top(&app.ui)) {   // (ui/'s order, which takes the keys)
    case UI_ON_FULL: return MODAL_FULL;
    case UI_ON_QUIT: return MODAL_QUIT;
    case UI_ON_CANCEL: return MODAL_CANCEL;
    case UI_ON_DELETE: return MODAL_DELETE;
    case UI_ON_EMPTY: return MODAL_EMPTY;
    case UI_ON_CLASH: return jobs.asking >= 0 ? MODAL_CLASH : MODAL_NONE;
    case UI_ON_PROPS: return MODAL_PROPS;
    case UI_ON_OW: return MODAL_OW;
    case UI_ON_BM: return MODAL_BM;
    case UI_ON_KEYS: return MODAL_KEYS;
    case UI_ON_ABOUT: return MODAL_ABOUT;
    case UI_ON_BAR: return MODAL_BAR;
    case UI_ON_NONE: break;
    }
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
                             .border = { .color = k == MODAL_DELETE || k == MODAL_CANCEL || k == MODAL_QUIT || k == MODAL_EMPTY ? cc(t->red) : cc(t->accent), .width = CLAY_BORDER_OUTSIDE(1) },
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
        } else if (k == MODAL_EMPTY) {
            isize all = app.list.n + (app.show_hidden ? 0 : app.list.hidden);
            text(cz("Empty the trash?"), cc(t->fg));
            text(cz(arena_sprintf(&frame, "All %td %s in it %s deleted for good.", all, items(all), all == 1 ? "is" : "are")), cc(t->fg_dim));
            text(cz("This can't be undone."), cc(t->red));
            text(cz("Enter or y empties it \xC2\xB7 n or Esc cancels"), cc(t->fg_dim));
            CLAY(CLAY_ID("modal-buttons"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 10,
                                                         .childAlignment = { .x = CLAY_ALIGN_X_RIGHT }, .padding = { 0, 0, 8, 0 } } }) {
                modal_button("modal-no", UI_MODAL_NO, "Cancel", false, false);
                modal_button("modal-yes", UI_MODAL_YES, "Empty Trash", true, true);
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

// ---- the Properties panel (F7.2) ---------------------------------------------------------------
//
// One entry's details and permissions, or several entries' counts (docs/plan.md, "F7.2"). What it
// shows is read when it opens (props_open), and again after a permission change; a folder's
// contents are counted on walk.c's thread.

static u8    props_mem[MiB(2)];
static Arena propsa;
static char *props_path[WALK_ROOTS_MAX];   // in propsa

typedef struct {
    isize       n;          // the entries shown
    bool        cut;        // more were selected than it counts (WALK_ROOTS_MAX)
    isize       idx;        // the one entry's place in the list, -1 for the folder itself
    char        name[NAME_MAX + 1];
    char        where[PATH_MAX];   // its folder (or theirs), "~" for home
    char        drive[256];
    char        link[PATH_MAX];    // a link's target, "" for none
    bool        have;       // st holds it (a link: its target's; a broken one: the link's)
    bool        broken, walking, local;
    struct stat st;
    i64         btime;      // its creation (statx), -1 when the file system doesn't say
    char        owner[64], group[64];
    char        type[128], type_name[128];   // one entry's MIME type, as gio has it, and its words
    bool        type_by_name;                // the type from its name only (gio didn't say)
    DuSum       sum;
    bool        done;
} Props;

static Props props;

// t as 2026-10-25 14:03:12, local time.
static void props_when(char *buf, size_t cap, i64 t)
{
    struct tm tm;
    time_t tt = (time_t)t;
    if (!localtime_r(&tt, &tm) || !strftime(buf, cap, "%Y-%m-%d %H:%M:%S", &tm)) snprintf(buf, cap, "?");
}

// n with its thousands apart: 2,345,678.
static void props_digits(char *buf, size_t cap, i64 n)
{
    char d[32];
    int len = snprintf(d, sizeof d, "%lld", (long long)(n < 0 ? 0 : n)), o = 0;
    for (int i = 0; i < len && o + 2 < (int)cap; i++) {
        if (i && (len - i) % 3 == 0) buf[o++] = ',';
        buf[o++] = d[i];
    }
    buf[o] = 0;
}

// The drive holding path: the drive place with the longest path above it.
static const char *props_drive(const char *path)
{
    int best = -1;
    size_t bn = 0;
    for (int i = 0; i < app.places.n; i++) {
        const Place *p = &app.places.p[i];
        size_t n = strlen(p->path);
        if (place_is_drive(p->kind) && n && path_within(path, p->path) && n >= bn) best = i, bn = n;
    }
    return best >= 0 ? app.places.p[best].label : "-";
}

// The one entry's details, read again.
static void props_read(void)
{
    const char *path = props_path[0];
    struct stat lst;
    props.have = lstat(path, &lst) == 0;
    props.link[0] = 0, props.broken = false, props.btime = -1;
    if (!props.have) return;
    props.st = lst;
    if (S_ISLNK(lst.st_mode)) {
        ssize_t k = readlink(path, props.link, sizeof props.link - 1);
        props.link[k > 0 ? k : 0] = 0;
        props.broken = stat(path, &props.st) != 0;
        if (props.broken) props.st = lst;
    }
    struct statx sx;
    if (statx(AT_FDCWD, path, props.broken ? AT_SYMLINK_NOFOLLOW : 0, STATX_BTIME, &sx) == 0 && (sx.stx_mask & STATX_BTIME))
        props.btime = (i64)sx.stx_btime.tv_sec;
    char buf[1024];
    struct passwd pw, *pwp = NULL;
    struct group gr, *grp = NULL;
    if (getpwuid_r(props.st.st_uid, &pw, buf, sizeof buf, &pwp) == 0 && pwp) snprintf(props.owner, sizeof props.owner, "%s", pw.pw_name);
    else snprintf(props.owner, sizeof props.owner, "%u", (unsigned)props.st.st_uid);
    if (getgrgid_r(props.st.st_gid, &gr, buf, sizeof buf, &grp) == 0 && grp) snprintf(props.group, sizeof props.group, "%s", gr.gr_name);
    else snprintf(props.group, sizeof props.group, "%u", (unsigned)props.st.st_gid);
    // Its permissions change only when it is Michael's, not in the trash, and there (one entry:
    // props_read is only for one).
    app.ui.prop_can = !app.ui.in_trash && !props.broken && props.st.st_uid == geteuid();
}

// The panel opened (ui_props_open): the marked entries, or the cursor's; the folder itself when it
// is empty; or path (a right-click menu's, F8: the folder shown, or a folder elsewhere). A folder
// among them is counted all the way down, on a local drive only.
static void props_open(const char *path)
{
    arena_reset(&propsa);
    props = (Props){ .idx = -1, .btime = -1 };
    bool here = path != NULL;
    isize n = !here && app.have_list && app.list.n > 0 ? targets(&propsa, props_path, WALK_ROOTS_MAX) : 0;
    if (here) props_path[0] = arena_cstr(&propsa, str_from_c(path)), n = 1;
    props.cut = !here && app.ui.marked > WALK_ROOTS_MAX;
    if (n == 0 && !app.ui.in_trash && !app.ui.in_search) props_path[0] = arena_cstr(&propsa, str_from_c(app.cwd)), n = 1;
    props.n = n;
    app.ui.prop_can = false;
    if (n == 0) {   // the trash view, empty
        snprintf(props.name, sizeof props.name, "Trash");
        snprintf(props.where, sizeof props.where, "-");
        return;
    }
    if (!here && app.have_list && app.list.n > 0 && n == 1)
        for (isize i = 0; i < app.list.n; i++)
            if (app.ui.marked ? app.ui.mark[i] : i == app.ui.list.sel) props.idx = i;
    Str base = path_base(str_from_c(props_path[0])), dir = path_dir(str_from_c(props_path[0]));
    char d[PATH_MAX];
    snprintf(d, sizeof d, "%.*s", STR_ARG(dir.len ? dir : S("/")));
    if (n == 1) {
        snprintf(props.name, sizeof props.name, "%.*s", STR_ARG(base.len ? base : S("/")));
        if (app.ui.in_trash && props.idx >= 0) snprintf(props.name, sizeof props.name, "%.*s", STR_ARG(app.list.e[props.idx].name));
        // The type gio opens it as, its content read (Michael, 2026-10-26); on a network drive,
        // whose reads can hang (F6.4), or when gio doesn't say, the name's.
        props.type_by_name = !app.thumb_local || !spawn_gio_type(props_path[0], props.type, sizeof props.type, 1000);
        if (props.type_by_name) {
            Str type = props.idx >= 0 ? entry_mime(props.idx) : S("inode/directory");
            snprintf(props.type, sizeof props.type, "%.*s", STR_ARG(type));
        }
        mime_comment(&scratch, str_from_c(props.type), props.type_name, sizeof props.type_name);
    }
    if (app.ui.in_trash) snprintf(props.where, sizeof props.where, "Trash");
    else ui_tilde(props.where, sizeof props.where, n == 1 ? d : app.ui.in_search ? app.search_root : app.cwd, getenv("HOME"));
    snprintf(props.drive, sizeof props.drive, "%s", props_drive(props_path[0]));
    if (n == 1) props_read();
    // Counted all the way down, but not on a network drive (its reads can hang, F6.4).
    props.local = app.thumb_local;
    bool any_dir = false;
    for (isize i = 0; i < n; i++) {
        struct stat st;
        any_dir |= lstat(props_path[i], &st) == 0 && S_ISDIR(st.st_mode);
    }
    props.walking = props.local;
    if (props.local) walk_ask(props_path, n);
    else if (!any_dir) {   // files on a network drive: the listing's sizes
        for (isize i = 0; i < app.list.n; i++)
            if (app.ui.marked ? app.ui.mark[i] : i == app.ui.list.sel) props.sum.files++, props.sum.bytes += app.list.e[i].size;
        props.done = true;
    }
}

// The grid's place k flipped (UA_CHMOD): applied at once, Ctrl+Z puts it back (F7.2).
static void props_chmod(int k)
{
    if (!app.ui.prop_can || props.n != 1 || k < 0 || k > 8) return;
    mode_t old, perms = (props.st.st_mode & 0777) ^ perm_bit(k);
    Err err;
    if (jobs_refuse(props_path[0])) return;
    if (!op_chmod(props_path[0], perms, &old, &err)) {
        set_status(true, "%s", err.msg);
        props_read();
        return;
    }
    char was[64];   // the old mode, and the file's device and inode (UNDO_CHMOD)
    snprintf(was, sizeof was, "%o %llu %llu", (unsigned)old, (unsigned long long)props.st.st_dev, (unsigned long long)props.st.st_ino);
    undo_gen++, undo_begin(&undo, UNDO_CHMOD), undo_add(&undo, NULL, props_path[0], was);
    props_read();
    set_status(false, "%s: %04o", props.name, (unsigned)(props.st.st_mode & 07777));
}

// A label and its value, one line of the panel.
static void props_line(const char *label, const char *value)
{
    Theme *t = &app.theme;
    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 12 } }) {
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(96), CLAY_SIZING_FIT(0) } } }) { text(cz(label), cc(t->fg_dim)); }
        text(fit(str_from_c(value), 360, false), cc(t->fg));
    }
}

// The sums as "12 folders, 340 files, 1.2 GiB", "counting…" while the walk runs.
static const char *props_counts(bool less_root)
{
    char sz[32], b[32];
    ui_size(sz, sizeof sz, props.sum.bytes);
    props_digits(b, sizeof b, props.sum.bytes);
    i64 dirs = props.sum.dirs - (less_root && props.sum.dirs > 0);
    if (!props.local && !props.done) return "not counted on a network drive";
    return arena_sprintf(&frame, "%lld %s, %lld %s, %s (%s bytes)%s%s", (long long)dirs, dirs == 1 ? "folder" : "folders",
                         (long long)props.sum.files, props.sum.files == 1 ? "file" : "files", sz, b,
                         props.done ? "" : " \xE2\x80\xA6 counting", props.sum.cut || props.cut ? " (some unreadable)" : "");
}

// The hard links among the sums, when there are any (Michael, 2026-10-26): "5 names of 2 files;
// 1.1 GiB (… bytes) counting each once". NULL when there are none.
static const char *props_links(void)
{
    if (props.sum.linked_names == 0) return NULL;
    char sz[32], b[32];
    ui_size(sz, sizeof sz, props.sum.once_bytes);
    props_digits(b, sizeof b, props.sum.once_bytes);
    return arena_sprintf(&frame, "%lld names of %lld %s; %s%s (%s bytes) counting each once", (long long)props.sum.linked_names,
                         (long long)props.sum.linked_files, props.sum.linked_files == 1 ? "file" : "files",
                         props.sum.once_over ? "at most " : "", sz, b);
}

static void layout_props_panel(void)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("props"), { .layout = { .sizing = { CLAY_SIZING_FIXED(560), CLAY_SIZING_FIT(0) }, .padding = { 24, 24, 20, 20 }, .childGap = 8,
                                         .layoutDirection = CLAY_TOP_TO_BOTTOM },
                             .backgroundColor = cc(t->bg_light), .border = { .color = cc(t->accent), .width = CLAY_BORDER_OUTSIDE(1) },
                             .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 20,
                                           .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER } } }) {
        // The icon (or preview) and the name, or the count of several.
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                                   .padding = { 0, 0, 0, 8 } } }) {
            Texture2D *th = props.idx >= 0 ? entry_thumb(props.idx, true) : NULL;
            if (th) {
                float sc = 48.0f / (float)(th->width > th->height ? th->width : th->height);
                CLAY(CLAY_ID("props-thumb"), { .layout = { .sizing = { CLAY_SIZING_FIXED(th->width * sc), CLAY_SIZING_FIXED(th->height * sc) } }, .image = { .imageData = th } }) {}
            } else {
                IconSlot *ic = props.idx >= 0 ? entry_icon(props.idx) : named_icon(app.ui.in_trash ? "user-trash" : "folder");
                icon_box(CLAY_ID("props-icon"), ic, 48);
            }
            const char *title = props.n == 1 ? props.name : arena_sprintf(&frame, "%td selected%s", props.n, props.cut ? " (the first counted)" : "");
            text(fit(str_from_c(title), 420, false), cc(t->fg));
        }
        if (props.n == 0) {
            props_line("Contents", "empty");
        } else if (props.n > 1) {
            props_line("Contents", props_counts(false));
            if (props_links()) props_line("Hard links", props_links());
            props_line("Location", props.where);
            props_line("Drive", props.drive);
        } else {
            char when[64], sz[32], b[32];
            props_line("MIME type", arena_sprintf(&frame, "%s%s%s%s%s", props.type, props.type_name[0] ? " (" : "", props.type_name,
                                                  props.type_name[0] ? ")" : "", props.type_by_name ? ", by its name" : ""));
            if (props.have && S_ISDIR(props.st.st_mode)) {
                props_line("Contents", props_counts(true));
                if (props_links()) props_line("Hard links", props_links());
            } else if (props.have) {
                ui_size(sz, sizeof sz, props.st.st_size), props_digits(b, sizeof b, props.st.st_size);
                props_line("Size", arena_sprintf(&frame, "%s bytes (%s)", b, sz));
                if (props.st.st_nlink > 1) props_line("Hard links", arena_sprintf(&frame, "%llu names", (unsigned long long)props.st.st_nlink));
            }
            props_line("Location", props.where);
            props_line("Drive", props.drive);
            if (props.link[0]) props_line("Link to", arena_sprintf(&frame, "%s%s", props.link, props.broken ? " (not there)" : ""));
            if (props.have) {
                props_when(when, sizeof when, props.st.st_mtim.tv_sec), props_line("Modified", arena_sprintf(&frame, "%s", when));
                props_when(when, sizeof when, props.st.st_atim.tv_sec), props_line("Accessed", arena_sprintf(&frame, "%s", when));
                if (props.btime >= 0) props_when(when, sizeof when, props.btime), props_line("Created", arena_sprintf(&frame, "%s", when));
                // Owner and group, each with its label (Michael, 2026-10-25: "group" read as a value).
                CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 12 } }) {
                    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(96), CLAY_SIZING_FIT(0) } } }) { text(cz("Owner"), cc(t->fg_dim)); }
                    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(150), CLAY_SIZING_FIT(0) } } }) { text(fit(str_from_c(props.owner), 150, false), cc(t->fg)); }
                    text(cz("Group"), cc(t->fg_dim));
                    text(fit(str_from_c(props.group), 150, false), cc(t->fg));
                }
                // The permissions: the octal number, then the grid; greyed when they can't change.
                bool can = app.ui.prop_can;
                Clay_Color off = mix(t->fg_dim, t->bg_light, 0.45f);
                CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 12, .padding = { 0, 0, 8, 0 } } }) {
                    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(96), CLAY_SIZING_FIT(0) } } }) { text(cz("Permissions"), cc(t->fg_dim)); }
                    text(cz(arena_sprintf(&frame, "%04o%s", (unsigned)(props.st.st_mode & 07777), can ? "" : app.ui.in_trash ? "  (in the trash)" : "  (not yours to change)")), cc(t->fg));
                }
                static const char *const WHO[] = { "Owner", "Group", "Others" }, *const WHAT[] = { "Read", "Write", "Execute" };
                CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = { 108, 0, 0, 0 } } }) {
                    for (int c = 0; c < 3; c++)
                        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(80), CLAY_SIZING_FIT(0) } } }) { text(cz(WHAT[c]), cc(t->fg_dim)); }
                }
                for (int r = 0; r < 3; r++) {
                    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(108), CLAY_SIZING_FIT(0) } } }) { text(cz(WHO[r]), can ? cc(t->fg) : off); }
                        for (int c = 0; c < 3; c++) {
                            int k = r * 3 + c;
                            bool on = (props.st.st_mode & perm_bit(k)) != 0, focus = can && app.ui.prop_at == k;
                            CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(80), CLAY_SIZING_FIT(0) } } }) {
                                // The focus ring around the box, then the box: filled when on.
                                CLAY_AUTO_ID({ .layout = { .padding = { 3, 3, 3, 3 } }, .cornerRadius = CLAY_CORNER_RADIUS(4),
                                               .border = { .color = focus ? cc(t->accent) : cc(t->bg_light), .width = CLAY_BORDER_OUTSIDE(2) } }) {
                                    CLAY(CLAY_IDI("prop-box", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(18), CLAY_SIZING_FIXED(18) },
                                                                               .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                                                   .backgroundColor = on ? (can ? cc(t->accent) : off) : cc(t->bg),
                                                                   .cornerRadius = CLAY_CORNER_RADIUS(3),
                                                                   .border = { .color = can ? cc(t->fg_dim) : off, .width = CLAY_BORDER_OUTSIDE(1) } }) {
                                        if (on) text(cz("\xE2\x9C\x93"), cc(t->bg));   // ✓
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
                props_line("", "can't be read now");
            }
        }
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = { 0, 0, 10, 0 },
                                   .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
            modal_button("modal-ok", UI_MODAL_OK, "Close", true, false);
        }
    }
}

#include "openwith.c"
#include "bookmarks.c"
#include "shortcuts.c"
#include "about.c"

// The panel over the window, the rest darkened but still readable (Michael's pick over a blur,
// 2026-10-21).
static void layout_modal(ModalKind k, float w, float h)
{
    CLAY(CLAY_ID("modal-backdrop"), { .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h) } },
                                      .backgroundColor = { 0, 0, 0, 150 },
                                      .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 10 } }) {}
    if (k == MODAL_PROPS) layout_props_panel();
    else if (k == MODAL_OW) layout_ow_panel();
    else if (k == MODAL_BM) layout_bmp_panel();
    else if (k == MODAL_KEYS) layout_keys_panel();
    else if (k == MODAL_ABOUT) layout_about_panel();
    else if (k == MODAL_BAR) layout_bar_panel();
    else layout_modal_panel(k);
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
        bool keyed = app.ui.focus == UI_FOCUS_JOBS && app.ui.job_at == i, hover = app.jobx_hover == i;
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

static Str  list_name(void *ctx, isize i);
static bool list_dir(void *ctx, isize i);

// The find bar's count and the cursor's place among the matches (ui_find_count), counted again only
// when the listing, the text, its toggles or the cursor changed: not on every hover's frame.
static isize find_count(isize *at)
{
    static struct { int lists; isize sel, n, at; UiMatchOpts o; int len, find_len; char text[UI_FIND_MAX + 1]; bool valid, finding; } c;
    const UiMatcher *m = &app.ui.find_m;
    if (!c.valid || c.finding != app.ui.finding || c.find_len != app.ui.find_len || c.lists != app.lists || c.sel != app.ui.list.sel || c.len != m->len || memcmp(c.text, m->text, (size_t)m->len) ||
        c.o.cased != m->o.cased || c.o.word != m->o.word || c.o.regex != m->o.regex) {
        c.n = ui_find_count(&app.ui, (UiNames){ list_name, NULL, list_dir }, &c.at);
        c.finding = app.ui.finding, c.find_len = app.ui.find_len, c.lists = app.lists, c.sel = app.ui.list.sel, c.len = m->len, c.o = m->o, c.valid = true;
        memcpy(c.text, m->text, (size_t)m->len);
    }
    *at = c.at;
    return c.n;
}

// The count's words: "2 of 5", or "5 matches" with the cursor off them.
static const char *find_label(isize n, isize at, char *buf, size_t cap)
{
    if (at > 0) snprintf(buf, cap, "%td of %td", at, n);
    else snprintf(buf, cap, n == 1 ? "%td match" : "%td matches", n);
    return buf;
}

// A find bar chevron: dimmed and inert under two matches.
static void find_chevron(Clay_ElementId id, ChevronDir dir, int step, bool on)
{
    Theme *t = &app.theme;
    bool hover = on && app.findstep_hover == step;
    CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(26), CLAY_SIZING_FIXED(26) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
               .backgroundColor = hover ? cc(t->bg) : (Clay_Color){ 0, 0, 0, 0 } }) {
        chevron(Clay_GetElementId(cz(arena_sprintf(&frame, "%.*s-icon", (int)id.stringId.length, id.stringId.chars))), dir, on ? cc(t->fg) : dim_color());
    }
}

// The find bar (Michael, 2026-10-26): over the job rows, as wide as the files. Its field (red text
// when nothing matches, a caret while it has the keys) with the three toggles, a bad expression's
// reason or the count ("2 of 5", "No match"), the chevrons to the previous and the next match, and
// a × that closes it.
static void layout_find_bar(void)
{
    Theme *t = &app.theme;
    Ui *u = &app.ui;
    isize at, n = find_count(&at);
    bool bad = u->find_m.bad && u->find_opts.regex && u->find_len;
    Clay_Color edge = bad ? cc(t->red) : u->focus == UI_FOCUS_FIND ? cc(t->accent) : mix(t->fg_dim, t->bg_light, 0.45f);
    CLAY(CLAY_ID("find-bar"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(FIND_BAR_H) }, .padding = { PAD, 8, 0, 0 }, .childGap = 10,
                                            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                .backgroundColor = cc(t->bg_light),
                                .border = { .color = mix(t->fg_dim, t->bg_light, 0.45f), .width = { .top = 1 } } }) {
        text(cz(u->find_select ? "Select" : "Find"), cc(t->fg_dim));   // Select Items Matching (F7.4)
        CLAY(CLAY_ID("find-field"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(26) }, .padding = { 8, 3, 0, 0 }, .childGap = 6,
                                                  .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                      .backgroundColor = cc(t->bg), .border = { .color = edge, .width = CLAY_BORDER_OUTSIDE(1) } }) {
            // As the edit line draws, its caret and selection, while it has the keys (2026-10-28); red
            // text when nothing matches.
            Clay_ElementData fd = Clay_GetElementData(CLAY_ID("find-field"));
            float room = fd.found && fd.boundingBox.width > 140 ? fd.boundingBox.width - 110 : 400;
            Clay_Color fg = u->find_miss ? cc(t->red) : cc(t->fg);
            CLAY(CLAY_ID("find-text"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                         .clip = { .horizontal = true } }) {
                if (u->focus == UI_FOCUS_FIND) edit_text("find", room, ui_find_line(u), fg);
                else text(fit((Str){ u->find, u->find_len }, room, true), fg);
            }
            layout_toggles(u->find_opts, 0);
        }
        // At least as wide as "88 of 88", so the chevrons don't move as the count grows.
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIT(str_width(S("88 of 88")), 0), CLAY_SIZING_FIT(0) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
            if (bad) text(fit(str_from_c(u->find_m.err), 240, false), cc(t->red));
            else if (u->find_miss) text(cz("No match"), cc(t->red));
            else if (n > 0) text(cz(find_label(n, at, arena_new(&frame, char, 48), 48)), cc(t->fg_dim));
        }
        CLAY_AUTO_ID({ .layout = { .childGap = 2, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
            find_chevron(CLAY_ID("find-prev"), CHEV_UP, -1, n >= 2);
            find_chevron(CLAY_ID("find-next"), CHEV_DOWN, 1, n >= 2);
        }
        bool hover = app.findx_hover;
        CLAY(CLAY_ID("find-x"), { .layout = { .sizing = { CLAY_SIZING_FIXED(26), CLAY_SIZING_FIXED(26) },
                                              .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                                  .backgroundColor = hover ? cc(t->bg) : (Clay_Color){ 0, 0, 0, 0 } }) {
            text(cz("\xC3\x97"), hover ? cc(t->red) : cc(t->fg_dim));   // ×
        }
    }
}

// The search view's bar (F9): where it searches, its field (a caret while it has the keys) with
// find's toggles, and a bad expression's reason.
static void layout_search_bar(void)
{
    Theme *t = &app.theme;
    Ui *u = &app.ui;
    bool bad = app.search_m.bad && u->find_opts.regex && u->search_len;
    Clay_Color edge = bad ? cc(t->red) : u->focus == UI_FOCUS_SEARCH ? cc(t->accent) : mix(t->fg_dim, t->bg_light, 0.45f);
    char where[PATH_MAX + 16];
    ui_tilde(where, sizeof where, app.search_root, getenv("HOME"));
    CLAY(CLAY_ID("search-bar"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(SEARCH_BAR_H) }, .padding = { PAD, PAD, 0, 0 }, .childGap = 10,
                                              .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                  .backgroundColor = cc(t->bg_light) }) {
        CLAY(CLAY_ID("search-where"), { .layout = { .sizing = { CLAY_SIZING_FIT(0, 300), CLAY_SIZING_FIT(0) } }, .clip = { .horizontal = true } }) {
            text(fit(str_from_c(arena_sprintf(&frame, "Search in %s", where)), 300, true), cc(t->fg_dim));
        }
        CLAY(CLAY_ID("search-field"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(26) }, .padding = { 8, 3, 0, 0 }, .childGap = 6,
                                                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                        .backgroundColor = cc(t->bg), .border = { .color = edge, .width = CLAY_BORDER_OUTSIDE(1) } }) {
            // As the edit line draws, its caret and selection, while it has the keys (2026-10-28).
            Clay_ElementData fd = Clay_GetElementData(CLAY_ID("search-field"));
            float room = fd.found && fd.boundingBox.width > 140 ? fd.boundingBox.width - 110 : 400;
            CLAY(CLAY_ID("search-text"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                           .clip = { .horizontal = true } }) {
                if (u->focus == UI_FOCUS_SEARCH) edit_text("search", room, ui_search_line(u), cc(t->fg));
                else text(fit((Str){ u->search, u->search_len }, room, true), cc(t->fg));
            }
            layout_toggles(u->find_opts, TOGGLE_SEARCH);
        }
        if (bad) text(fit(str_from_c(app.search_m.err), 240, false), cc(t->red));
    }
}

// The bottom bar's sidebar part (F6): as wide as the sidebar, or the buttons' own width when it
// is hidden.
static float modes_w(void)
{
    return side_w() ? (float)side_w() : (float)(2 * PAD + SIDE_MODES * MODE_BTN_W + (SIDE_MODES - 1) * MODE_GAP);
}

// A mode button's picture, drawn in the theme's colour: a window with a sidebar of places, the
// same with a tree's steps in it, and a window alone.
static void mode_picture(int k, Clay_Color c)
{
    CLAY(CLAY_IDI("mode-pic", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(12) }, .padding = { 2, 2, 2, 2 },
                                                .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2 },
                                    .border = { .color = c, .width = CLAY_BORDER_OUTSIDE(1) } }) {
        if (k == SIDE_PLACES) {
            CLAY(CLAY_IDI("mode-side", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(4), CLAY_SIZING_GROW(0) } }, .backgroundColor = c }) {}
        } else if (k == SIDE_TREE) {
            for (int j = 0; j < 3; j++)
                CLAY(CLAY_IDI("mode-step", j), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) }, .padding = { (uint16_t)(j * 3), 0, 0, 0 } } }) {
                    CLAY(CLAY_IDI("mode-line", j), { .layout = { .sizing = { CLAY_SIZING_FIXED(6), CLAY_SIZING_FIXED(1) } }, .backgroundColor = c }) {}
                }
        }
    }
}

// The three mode buttons: places, tree, hidden; the one on in the accent.
static void layout_modes(void)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("modes"), { .layout = { .sizing = { CLAY_SIZING_FIXED(modes_w()), CLAY_SIZING_GROW(0) }, .padding = { PAD, PAD, 0, 0 },
                                         .childGap = MODE_GAP, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        for (int k = 0; k < SIDE_MODES; k++) {
            bool on = app.ui.side_mode == (SideMode)k, hover = app.mode_hover == k;
            CLAY(CLAY_IDI("mode", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(MODE_BTN_W), CLAY_SIZING_FIXED(MODE_BTN_H) },
                                                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                                        .backgroundColor = on ? mix(t->accent, t->bg_light, 0.25f) : hover ? cc(t->bg) : (Clay_Color){ 0, 0, 0, 0 },
                                        .cornerRadius = CLAY_CORNER_RADIUS(3) }) {
                mode_picture(k, on ? cc(t->accent) : hover ? cc(t->fg) : cc(t->fg_dim));
            }
        }
    }
}

// The status line when no message is up (ui_status_info): the marked entry, or how many, or the
// folder's count and its drive's free space.
static void status_info(char *buf, size_t cap)
{
    UiInfo in = { .n = app.list.n, .hidden = app.show_hidden ? 0 : app.list.hidden, .marked = app.ui.marked,
                  .one_count = -1, .one_size = -1, .avail = app.cwd_avail,
                  .cramped = app.ui.perms_cols && !app.ui.grid && !perms_shown((float)GetScreenWidth()) };
    for (isize i = 0; app.ui.marked == 1 && i < app.list.n; i++) {
        if (!app.ui.mark[i]) continue;
        const Entry *e = &app.list.e[i];
        in.one = e->name, in.one_dir = kind_is_dir(e->kind);
        in.one_count = app.count[i] >= 0 ? app.count[i] : -1;
        if (!in.one_dir && e->kind != K_LINK_BROKEN && e->kind != K_OTHER) in.one_size = e->size;
        if (in.one_dir && app.count[i] == COUNT_UNKNOWN) {
            char path[PATH_MAX];
            if (entry_path(i, path, sizeof path) && count_ask(i, path, app.show_hidden)) app.count[i] = COUNT_ASKED;
        }
    }
    if (!app.have_list) in.n = 0, in.avail = -1;
    ui_status_info(buf, cap, &in);
}

// The search view's status line (F9) when nothing is marked: how many it found, while it searches or
// when it is done, and what it left out.
static void search_status(char *buf, size_t cap)
{
    char skipped[256];
    search_skips_text(&app.search_s, skipped, sizeof skipped);
    const char *sep = skipped[0] ? " \xC2\xB7 " : "";
    if (!app.ui.search_len) snprintf(buf, cap, "A name, or part of one; * and ? as wildcards (*.ts)");
    else if (!app.search_done) snprintf(buf, cap, "Searching\xE2\x80\xA6 %td found%s%s", app.list.n, sep, skipped);
    else if (app.search_s.full) snprintf(buf, cap, "%td found, the most a search shows%s%s", app.list.n, sep, skipped);
    else snprintf(buf, cap, "%td found%s%s", app.list.n, sep, skipped);
}

// The window, and the modal panel on top when one is up.
// ---- tooltips (Michael, 2026-10-24) ------------------------------------------------------------
//
// The icon-only buttons, numbered for UiTip: a top bar button 100 + its UiBar, a sidebar mode
// button 200 + its SideMode, a drive's eject button 300 + its row, a tab's x 400 + its place.

// The zoom slider's track is 500 (F6.3), the find bar's chevrons 600 and 601.
enum { TIP_BAR = 100, TIP_MODE = 200, TIP_EJECT = 300, TIP_TAB_X = 400, TIP_ZOOM = 500, TIP_FIND = 600 };

// Target k's text, and the element it hangs from; false for none (gone since).
static bool tip_of(int k, char *buf, size_t cap, Clay_ElementId *id)
{
    static const struct { UiBar b; const char *id, *text; } BAR[] = {
        { UB_BACK, "nav-back", "Back (Alt+Left)" }, { UB_FORWARD, "nav-fwd", "Forward (Alt+Right)" },
        { UB_UP, "nav-up", "Up to the parent (Backspace)" }, { UB_LOCATION, "loc-toggle", "Type a location (Ctrl+L)" },
        { UB_SEARCH, "search", "Search (Ctrl+Shift+F)" }, { UB_LEFT, "crumb-left", "Earlier folders in the path" },
        { UB_RIGHT, "crumb-right", "Later folders in the path" }, { UB_VIEW_LIST, "view-list", "List (Ctrl+1)" },
        { UB_VIEW_ICONS, "view-icons", "Icons (Ctrl+2)" }, { UB_ZOOM_OUT, "zoom-out", "Smaller icons (Ctrl+-)" },
        { UB_ZOOM_IN, "zoom-in", "Bigger icons (Ctrl+=)" },
    };
    static const char *const MODES[SIDE_MODES] = { "Places (F9)", "Tree (F9)", "No sidebar (F9)" };
    if (k >= TIP_FIND) {
        *id = k == TIP_FIND ? CLAY_ID("find-prev") : CLAY_ID("find-next");
        return app.ui.finding && snprintf(buf, cap, "%s", k == TIP_FIND ? "Previous match (Up)" : "Next match (Down)") > 0;
    }
    if (k == TIP_ZOOM) {
        *id = CLAY_ID("zoom-track");
        return app.ui.grid && snprintf(buf, cap, "Icons %d px (Ctrl+0: %d px)", ui_zoom_px(&app.ui), UI_ZOOM_DEFAULT) > 0;
    }
    if (k >= TIP_TAB_X) {
        *id = CLAY_IDI("tab-x", k - TIP_TAB_X);
        return snprintf(buf, cap, "Close the tab (Ctrl+W)") > 0;
    }
    if (k >= TIP_EJECT) {
        isize i = app.ui.side.top + (k - TIP_EJECT);
        if (app.ui.side_mode != SIDE_PLACES || i >= app.places.n) return false;
        *id = CLAY_IDI("side-eject", k - TIP_EJECT);
        return snprintf(buf, cap, "Eject %s", app.places.p[i].label) > 0;
    }
    if (k >= TIP_MODE) {
        *id = CLAY_IDI("mode", k - TIP_MODE);
        return snprintf(buf, cap, "%s", MODES[k - TIP_MODE]) > 0;
    }
    for (int j = 0; j < COUNT(BAR); j++)
        if (TIP_BAR + (int)BAR[j].b == k) return *id = Clay_GetElementId(cz(BAR[j].id)), snprintf(buf, cap, "%s", BAR[j].text) > 0;
    return false;
}

// The tooltip, under a top bar button or a tab's x, over the bottom bar's, beside the sidebar's;
// lined up with the button's edge on the side the window has room.
static void layout_tip(int w)
{
    char txt[NAME_MAX + 64];
    Clay_ElementId id;
    if (!app.tip.shown || !tip_of(app.tip.target, txt, sizeof txt, &id)) return;
    Clay_ElementData d = Clay_GetElementData(id);
    if (!d.found) return;
    bool above = (app.tip.target >= TIP_MODE && app.tip.target < TIP_EJECT) || app.tip.target == TIP_ZOOM || app.tip.target >= TIP_FIND ||
                 app.tip.target == TIP_BAR + UB_ZOOM_OUT || app.tip.target == TIP_BAR + UB_ZOOM_IN;
    bool right = d.boundingBox.x + d.boundingBox.width / 2 > w / 2.0f;
    Clay_FloatingAttachPoints at = above && right ? (Clay_FloatingAttachPoints){ CLAY_ATTACH_POINT_RIGHT_BOTTOM, CLAY_ATTACH_POINT_RIGHT_TOP }
                                 : above ? (Clay_FloatingAttachPoints){ CLAY_ATTACH_POINT_LEFT_BOTTOM, CLAY_ATTACH_POINT_LEFT_TOP }
                                 : right ? (Clay_FloatingAttachPoints){ CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_BOTTOM }
                                         : (Clay_FloatingAttachPoints){ CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_BOTTOM };
    Theme *t = &app.theme;
    CLAY(CLAY_ID("tooltip"), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) }, .padding = { 8, 8, 4, 4 } },
                               .backgroundColor = cc(t->bg_light), .cornerRadius = CLAY_CORNER_RADIUS(3),
                               .border = { .color = mix(t->fg_dim, t->bg_light, 0.6f), .width = CLAY_BORDER_OUTSIDE(1) },
                               .floating = { .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID, .parentId = id.id, .zIndex = 40,
                                             .offset = { 0, above ? -4.0f : 4.0f }, .attachPoints = at,
                                             .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
        text(cz(arena_sprintf(&frame, "%s", txt)), cc(t->fg));
    }
}

// What the Edit, View and Go menus grey or mark that only gui/ knows (F7.4): set at each layout,
// which follows any change that alters it, before the next input. Paste: the system clipboard can't be read without asking it, so it
// counts as holding files; without one, ofx's own copy does.
static void menu_facts(void)
{
    Ui *u = &app.ui;
    u->can_undo = undo.kind != UNDO_NONE && undo.n > 0;
    u->can_back = hist.nback > 0, u->can_forward = hist.nfwd > 0;
    u->can_up = !u->in_trash && (u->in_search || strcmp(app.cwd, "/") != 0);   // (from a search: the folder searched, F9)
    u->can_paste = clip.ok || own.n > 0;
    u->shows_hidden = app.show_hidden;
    u->bm_n = 0, u->bm_here = -1;
    for (int i = 0; i < bmarks.n && u->bm_n < UI_MENU_BOOKMARKS; i++) {
        if (!bmarks.b[i].path[0]) continue;
        if (strcmp(bmarks.b[i].path, app.cwd) == 0) u->bm_here = u->bm_n;
        u->bm_n++;
    }
    u->can_bookmark = !u->in_trash && !u->in_search && !bmarks.full && bm_find(&bmarks, app.cwd) < 0;
    // The cursor's entry (F8): a link that leads somewhere (Follow Link); a folder bookmarked already.
    const Entry *e = app.have_list && u->list.n > 0 && u->list.sel < app.list.n ? &app.list.e[u->list.sel] : NULL;
    char p[PATH_MAX];
    u->sel_link = e && (e->kind == K_LINK_FILE || e->kind == K_LINK_DIR);
    u->sel_bm = e && kind_is_dir(e->kind) && entry_path(u->list.sel, p, sizeof p) && bm_find(&bmarks, p) >= 0;
    u->tab_at = tabbar.cur, u->tab_n = tabbar.n;   // (the tab menu's moves, F8)
    u->bm_total = bmarks.n, u->bm_more = 0, u->bm_more_at = -1;
    for (int i = 0, local = 0; i < bmarks.n; i++) {
        if (!bmarks.b[i].path[0] || local++ < UI_MENU_BOOKMARKS) continue;
        if (u->bm_more++ == 0) u->bm_more_at = i;
    }
}


static Clay_RenderCommandArray layout(int w, int h)
{
    menu_facts();
    arena_reset(&frame);
    arena_reset(&scratch);
    app.grid_cells = app.grid_full_lines = app.grid_hover_lines = 0, app.grid_hover_cut = false;
    app.grid_first = app.grid_last = -1;
    Theme *t = &app.theme;
    Clay_SetLayoutDimensions((Clay_Dimensions){ (float)w, (float)h });
    Clay_BeginLayout();
    i64 now = (i64)time(NULL);
    CLAY(CLAY_ID("root"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                            .backgroundColor = cc(t->bg) }) {
        float foot_w = (float)w - modes_w() - zoom_w() - 2 * PAD;   // the status part of the bottom bar
        if (bar_shown()) layout_menubar();
        if (app.ui.menu_on && app.ui.menu >= UI_MENUS) layout_menu(app.ui.menu);   // a right-click menu (F8)
        layout_bar((float)w);
        float body_h = (float)(h - bar_h() - HEADER_H - FOOTER_H);
        CLAY(CLAY_ID("body"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(body_h) } } }) {
        if (app.ui.side_mode == SIDE_TREE) layout_tree(body_h);
        else if (side_w()) layout_side(body_h);
        CLAY(CLAY_ID("main"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
        // The tab bar over the files only, not the sidebar (Michael, 2026-10-21).
        if (tabbar.n >= 2) layout_tabs((float)(w - side_w()));
        if (app.ui.in_trash) layout_trash_bar();
        if (app.ui.in_search) layout_search_bar();
        if (!app.ui.grid) layout_cols();
        CLAY(CLAY_ID("list"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                            .padding = { 0, 0, app.ui.grid ? PAD : 0, 0 } },
                                .clip = { .vertical = true } }) {
            if (!app.have_list) {
                CLAY(CLAY_ID("error"), { .layout = { .padding = { PAD, PAD, PAD, PAD } } }) { text(cz(app.error), cc(t->red)); }
            } else if (app.list.n == 0) {
                CLAY(CLAY_ID("empty"), { .layout = { .padding = { PAD, PAD, PAD, PAD } } }) {
                    text(cz(app.ui.in_search ? (!app.ui.search_len ? "Type a name to search" : app.search_done ? "Nothing found" : "Searching\xE2\x80\xA6")
                            : app.list.hidden  ? "Only hidden files"
                                               : "Empty folder"),
                         cc(t->fg_dim));
                }
            }
            if (app.have_list && app.ui.grid) layout_grid();
            for (int r = 0; app.have_list && !app.ui.grid && r < app.ui.list.rows && app.ui.list.top + r < app.list.n; r++)
                layout_row(r, app.ui.list.top + r, (float)w, now);
        }
        if (app.ui.finding) layout_find_bar();
        layout_jobs();
        }
        }
        CLAY(CLAY_ID("footer"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(FOOTER_H) },
                                              .padding = { 0, PAD, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                  .backgroundColor = cc(t->bg_light),
                                  .border = { .color = mix(t->fg_dim, t->bg_light, 0.3f), .width = { .top = 1 } } }) {
            layout_modes();
            CLAY(CLAY_ID("footer-line"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                           .clip = { .horizontal = true } }) {
            if (modal_kind() == MODAL_PROPS) {
                text(fit(S("Properties: \xE2\x86\x90 \xE2\x86\x91 \xE2\x86\x93 \xE2\x86\x92 (h j k l) move \xC2\xB7 Space or Enter changes \xC2\xB7 Esc closes"), foot_w, false),
                     cc(t->fg_dim));
            } else if (modal_kind() == MODAL_BAR) {
                text(fit(S("Menu bar hidden: Enter or Esc closes this"), foot_w, false), cc(t->fg_dim));
            } else if (modal_kind() == MODAL_ABOUT) {
                text(fit(S("About: Esc or Enter closes"), foot_w, false), cc(t->fg_dim));
            } else if (modal_kind() == MODAL_KEYS) {
                text(fit(S("Keyboard Shortcuts: type to filter \xC2\xB7 \xE2\x86\x91 \xE2\x86\x93 PgUp PgDn scroll \xC2\xB7 Esc or F1 closes"), foot_w, false), cc(t->fg_dim));
            } else if (modal_kind() == MODAL_BM) {
                text(fit(str_from_c(app.ui.bmp_naming ? "Rename: Enter renames (empty: the folder's name) \xC2\xB7 Esc keeps the name"
                                             : "Bookmarks: \xE2\x86\x91 \xE2\x86\x93 (k j) move \xC2\xB7 Enter goes there \xC2\xB7 Delete removes \xC2\xB7 F2 renames \xC2\xB7 "
                                               "Alt+\xE2\x86\x91 Alt+\xE2\x86\x93 move it \xC2\xB7 Esc closes"), foot_w, false), cc(t->fg_dim));
            } else if (modal_kind() == MODAL_OW) {
                text(fit(S("Open With: type to filter \xC2\xB7 Tab or \xE2\x86\x93 the list \xC2\xB7 Enter opens \xC2\xB7 Esc closes \xC2\xB7 "
                           "Alt+C/W/R case, words, regex \xC2\xB7 Alt+D default"), foot_w, false), cc(t->fg_dim));
            } else if (modal_kind() != MODAL_NONE) {
                text(cz("Answer the question above"), cc(t->fg_dim));
            } else if (app.ui.menu_on && app.ui.menu >= UI_MENUS) {
                text(fit(S("Menu: \xE2\x86\x91 \xE2\x86\x93 (k j) move \xC2\xB7 Enter picks \xC2\xB7 Esc closes"), foot_w, false), cc(t->fg_dim));
            } else if (app.ui.menu_on) {
                text(fit(S("Menu: \xE2\x86\x91 \xE2\x86\x93 (k j) move \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 the next menu \xC2\xB7 Enter picks \xC2\xB7 Esc closes"), foot_w, false),
                     cc(t->fg_dim));
            } else if (app.ui.focus == UI_FOCUS_CRUMBS) {
                text(fit(S("Path: \xE2\x86\x90 \xE2\x86\x92 (h l) move \xC2\xB7 Enter goes there \xC2\xB7 Shift+Enter in a new tab \xC2\xB7 Esc back to the list"), foot_w, false),
                     cc(t->fg_dim));
            } else if (app.ui.focus == UI_FOCUS_EDIT && app.ui.edit_path) {
                text(cz("Go to: Enter goes there \xC2\xB7 Esc cancels \xC2\xB7 ~ is home, a relative path starts here"), cc(t->fg_dim));
            } else if (app.ui.focus == UI_FOCUS_EDIT) {
                text(cz(app.edit_new == UNDO_MKDIR  ? "New folder: Enter keeps the name \xC2\xB7 Esc keeps \"New folder\""
                        : app.edit_new == UNDO_MKFILE ? "New file: Enter keeps the name \xC2\xB7 Esc keeps \"New file\""
                                                      : "Rename: Enter renames \xC2\xB7 Esc cancels"),
                     cc(t->fg_dim));
            } else if (app.ui.focus == UI_FOCUS_JOBS) {
                text(fit(S("Jobs: \xE2\x86\x91 \xE2\x86\x93 (k j) move \xC2\xB7 Enter cancels the marked job \xC2\xB7 Esc back to the list"), foot_w, false),
                     cc(t->fg_dim));
            } else if (app.ui.finding && app.ui.focus == UI_FOCUS_FIND && app.ui.find_select) {
                text(fit(S("Select: Enter selects every match \xC2\xB7 Esc cancels \xC2\xB7 \xE2\x86\x91 \xE2\x86\x93 previous and next \xC2\xB7 "
                           "Alt+C/W/R case, words, regex"), foot_w, false), cc(t->fg_dim));
            } else if (app.ui.finding && app.ui.focus == UI_FOCUS_FIND) {
                text(fit(S("Find: \xE2\x86\x91 \xE2\x86\x93 previous and next \xC2\xB7 Enter keeps \xC2\xB7 Esc closes \xC2\xB7 Ctrl+J the jobs \xC2\xB7 "
                           "Alt+C/W/R case, words, regex"), foot_w, false), cc(t->fg_dim));
            } else if (app.status[0]) {
                text(fit(str_from_c(app.status), foot_w, false), app.status_bad ? cc(t->red) : cc(t->fg));
            } else if (app.ui.in_search && !app.ui.marked) {
                char info[512];
                search_status(info, sizeof info);
                text(fit(str_from_c(arena_sprintf(&frame, "%s", info)), foot_w, false), cc(t->fg_dim));
            } else {
                char info[NAME_MAX + 64];
                status_info(info, sizeof info);
                text(fit(str_from_c(arena_sprintf(&frame, "%s", info)), foot_w, false), cc(t->fg_dim));
            }
            }
            if (app.ui.grid) layout_zoom();
        }
        layout_tip(w);
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
    thumbs_frame();
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
    if (m == GLFW_MOD_ALT && key == GLFW_KEY_HOME) return UK_HOMEDIR;   // (F7.4, as Nemo)
    if (m == GLFW_MOD_ALT && key == GLFW_KEY_END) return UK_TRASH_VIEW;   // (Michael, 2026-10-27)
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_D) return UK_ADD_BOOKMARK;   // (F7.5, as Nemo)
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_B) return UK_EDIT_BOOKMARKS;
    if ((m == 0 || m == GLFW_MOD_CONTROL) && key == GLFW_KEY_F1) return UK_SHORTCUTS;   // (F7.5: F1 and Ctrl+F1, Nemo's two)
    if (m == GLFW_MOD_ALT && key == GLFW_KEY_DOWN) return UK_ALT_DOWN;   // (Edit Bookmarks' move down)
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_R) return UK_RELOAD;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_P) return UK_PERMS_COLS;   // (Michael, 2026-10-27)
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_G) return UK_GROUP_COL;
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_I) return UK_INVERT;
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_A) return UK_SELECT_MATCH;
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_F) return UK_SEARCH;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_T) return UK_TAB_NEW;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_N) return UK_NEW_WINDOW;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_I) return UK_PROPERTIES;
    if (m == GLFW_MOD_ALT && (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)) return UK_TERMINAL;
    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)) return UK_CONTAINING;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_W) return UK_TAB_CLOSE;
    if ((m == GLFW_MOD_CONTROL && key == GLFW_KEY_TAB) || (m == GLFW_MOD_CONTROL && key == GLFW_KEY_PAGE_DOWN)) return UK_TAB_NEXT;
    if ((m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_TAB) || (m == GLFW_MOD_CONTROL && key == GLFW_KEY_PAGE_UP)) return UK_TAB_PREV;
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_PAGE_UP) return UK_TAB_LEFT;
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_PAGE_DOWN) return UK_TAB_RIGHT;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_L) return UK_LOCATION;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_K) return UK_CRUMBS;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_J) return UK_JOBS;
    // Ctrl and an arrow: the keys to the part of the window that way (Michael, 2026-10-24); Ctrl+S
    // the sidebar's next mode, as F9.
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_LEFT) return UK_WORD_LEFT;   // (outside a text field: UK_SIDE)
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_LEFT) return UK_SEL_WORD_LEFT;
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_RIGHT) return UK_SEL_WORD_RIGHT;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_BACKSPACE) return UK_WORD_BACKSPACE;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_DELETE) return UK_WORD_DELETE;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_UP) return UK_CRUMBS;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_DOWN) return UK_JOBS;
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_RIGHT) return UK_WORD_RIGHT;   // (outside a text field: UK_LOCATION)
    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_S) return UK_F9;
    // The views in the top bar's order, list first (Michael, 2026-10-24; Nemo has them the other way).
    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_1 || key == GLFW_KEY_KP_1)) return UK_VIEW_LIST;
    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_2 || key == GLFW_KEY_KP_2)) return UK_VIEW_ICONS;
    // The icon view's sizes (F6.3), as Nemo's: Ctrl+= or Ctrl++ (Shift+= on most layouts), Ctrl+-,
    // Ctrl+0, and the keypad's.
    if ((m == GLFW_MOD_CONTROL || m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT)) && (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD)) return UK_ZOOM_IN;
    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT)) return UK_ZOOM_OUT;
    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_0 || key == GLFW_KEY_KP_0)) return UK_ZOOM_RESET;
    if (m == GLFW_MOD_SHIFT) {
        switch (key) {
        case GLFW_KEY_LEFT: return UK_SEL_LEFT;
        case GLFW_KEY_RIGHT: return UK_SEL_RIGHT;
        case GLFW_KEY_HOME: return UK_SEL_HOME;
        case GLFW_KEY_END: return UK_SEL_END;
        case GLFW_KEY_UP: return UK_SEL_UP;
        case GLFW_KEY_DOWN: return UK_SEL_DOWN;
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
        case GLFW_KEY_M: return UK_MAKE_LINK;   // (F8, as Nemo)
        default: break;
        }
    }
    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_N) return UK_NEWFOLDER;
    if (key == GLFW_KEY_DELETE || key == GLFW_KEY_KP_DECIMAL) {
        if (key == GLFW_KEY_KP_DECIMAL) return UK_NONE;   // the keypad's . types a dot
        return m == GLFW_MOD_SHIFT ? UK_SHIFT_DELETE : m == 0 ? UK_DELETE : UK_NONE;
    }
    if ((m == GLFW_MOD_SHIFT && key == GLFW_KEY_F10) || (m == 0 && key == GLFW_KEY_MENU)) return UK_CONTEXT;   // the right-click menu (F8, as GTK)
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
    case GLFW_KEY_TAB: return m == 0 || m == GLFW_MOD_SHIFT ? UK_TAB : UK_NONE;   // Shift+Tab: back, where there are two places (F7.3)
    case GLFW_KEY_F2: return m == 0 ? UK_F2 : UK_NONE;
    case GLFW_KEY_F9: return m == 0 ? UK_F9 : UK_NONE;
    case GLFW_KEY_F10: return m == 0 ? UK_F10 : UK_NONE;
    default: return UK_NONE;
    }
}

static void on_key(GLFWwindow *w, int key, int scancode, int action, int mods)
{
    if (debug_input) fprintf(stderr, "ofx: key %d action %d mods %#x\n", key, action, mods);
    // Ctrl+Q closes every ofx window (F7.1), each asking first when its jobs run or wait (F5.3);
    // a scripted window quits at once.
    if (action == GLFW_PRESS && key == GLFW_KEY_Q && (mods & GLFW_MOD_CONTROL)) {
        if (script_on) app.quit = true;
        else ev_push((UiEvent){ UK_QUIT_ALL, 0 });
    }
    UiKey k = action != GLFW_RELEASE ? ui_key_of(key, mods) : UK_NONE;
    if (k) ev_push((UiEvent){ k, 0 });
    // Alt+1 .. Alt+9: a tab by place (the digit rides in ch).
    int m = mods & (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT | GLFW_MOD_ALT | GLFW_MOD_SUPER);
    if (action == GLFW_PRESS && m == GLFW_MOD_ALT && key >= GLFW_KEY_1 && key <= GLFW_KEY_9) ev_push((UiEvent){ UK_TAB_JUMP, (u32)(key - GLFW_KEY_0) });
    // Alt and a letter: a menu (F7.1); ui/ knows which letters have one.
    if (action == GLFW_PRESS && m == GLFW_MOD_ALT && key >= GLFW_KEY_A && key <= GLFW_KEY_Z) ev_push((UiEvent){ UK_MENU, (u32)('a' + key - GLFW_KEY_A) });
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
    int   mode;         // a sidebar mode button (a SideMode), -1 for none
    int   twisty;       // a tree row's open/close mark, -1 for none
    int   eject;        // a drive's eject button, -1 for none
    bool  zoom;         // the zoom slider's track (F6.3)
    int   menu_title;   // a menu bar title, -1 for none (F7.1)
    int   menu_item;    // an entry of the open menu (its place), -1 for none
    int   prop_box;     // a checkbox of the Properties panel's grid, -1 for none (F7.2)
    int   ow_row;       // a row of Open With's list (on screen), -1 for none (F7.3)
    bool  find_x, find_field;   // the find bar's × and its field
    bool  ow_field;     // its field
    int   toggle;       // a toggle of its field or of the find line (1..3), 0 for none
    int   find_step;    // the find bar's chevrons: -1 the previous, 1 the next, 0 none
    int   bmp_row;      // a row of Edit Bookmarks' list (on screen), -1 for none (F7.5)
    int   bmp_btn;      // one of its buttons (1..4), 0 for none
    bool  search_field; // the search bar's field (F9)
} Hit;

static bool in_box(Clay_ElementId id, float x, float y)
{
    Clay_ElementData d = Clay_GetElementData(id);
    return d.found && x >= d.boundingBox.x && x < d.boundingBox.x + d.boundingBox.width && y >= d.boundingBox.y &&
           y < d.boundingBox.y + d.boundingBox.height;
}

static Hit hit_at(float x, float y)
{
    Hit h = { -1, -1, -1, UB_NONE, -1, -1, -1, 0, -1, -1, -1, -1, false, -1, -1, -1, -1, false, false, false, 0, 0, -1, 0, false };
    // The find bar's toggles, or Open With's (its panel's); ui_handle drops a find or search bar's
    // under a panel or a menu (a click on Properties' OK above them flipped one, 2026-10-26).
    for (int k = 1; k <= 4 && !h.toggle && (app.ui.ow || app.ui.finding); k++)
        if (in_box(CLAY_IDI("toggle", k), x, y)) h.toggle = k;
    for (int k = 1; k <= 3 && !h.toggle && app.ui.in_search; k++)   // the search bar's (F9)
        if (in_box(CLAY_IDI("toggle", TOGGLE_SEARCH + k), x, y)) h.toggle = k;
    if (modal_kind() == MODAL_NONE)
        for (int m = 0; m < UI_MENUS && h.menu_title < 0; m++)
            if (in_box(CLAY_IDI("menu-title", m), x, y)) h.menu_title = m;
    // A menu open: its titles and its entries are all there is to hit (F7.1).
    if (app.ui.menu_on && modal_kind() == MODAL_NONE) {
        for (int i = 0; i < UI_MENU[app.ui.menu].n && h.menu_item < 0; i++)
            if (UI_MENU[app.ui.menu].items[i] != UM_SEP && in_box(CLAY_IDI("menu-item", i), x, y)) h.menu_item = i;   // (one not shown has no box)
        return h;
    }
    if (modal_kind() != MODAL_NONE) {
        // A panel is up: its buttons are all there is to hit.
        static const struct { const char *id; int b; } MB[] = {
            { "modal-yes", UI_MODAL_YES }, { "modal-no", UI_MODAL_NO }, { "modal-skip", UI_MODAL_SKIP }, { "modal-replace", UI_MODAL_REPLACE },
            { "modal-keep", UI_MODAL_KEEP }, { "modal-cancel", UI_MODAL_CANCEL }, { "modal-all", UI_MODAL_ALL }, { "modal-ok", UI_MODAL_OK },
        };
        for (int k = 0; k < COUNT(MB) && !h.modal; k++)
            if (in_box(Clay_GetElementId(cz(MB[k].id)), x, y)) h.modal = MB[k].b;
        for (int k = 0; modal_kind() == MODAL_PROPS && k < 9 && h.prop_box < 0; k++)
            if (in_box(CLAY_IDI("prop-box", k), x, y)) h.prop_box = k;
        for (int r = 0; modal_kind() == MODAL_OW && r < app.ui.ow_list.rows && h.ow_row < 0; r++)
            if (in_box(CLAY_IDI("ow-row", r), x, y)) h.ow_row = r;
        h.ow_field = modal_kind() == MODAL_OW && !h.toggle && in_box(CLAY_ID("ow-field"), x, y);
        for (int r = 0; modal_kind() == MODAL_BM && r < app.ui.bmp_list.rows && h.bmp_row < 0; r++)
            if (in_box(CLAY_IDI("bmp-row", r), x, y)) h.bmp_row = r;
        for (int k = 1; modal_kind() == MODAL_BM && k <= 4 && !h.bmp_btn; k++)
            if (in_box(CLAY_IDI("bmp-btn", k), x, y)) h.bmp_btn = k;
        return h;
    }
    if (app.ui.finding) {
        h.find_x = in_box(CLAY_ID("find-x"), x, y);
        h.find_step = in_box(CLAY_ID("find-prev"), x, y) ? -1 : in_box(CLAY_ID("find-next"), x, y) ? 1 : 0;
        h.find_field = !h.toggle && !h.find_x && in_box(CLAY_ID("find-field"), x, y);
    }
    h.search_field = app.ui.in_search && !h.toggle && in_box(CLAY_ID("search-field"), x, y);
    for (int i = 0; tabbar.n >= 2 && i < tabbar.n && h.tab < 0; i++) {
        if (in_box(CLAY_IDI("tab-x", i), x, y)) h.tab_x = h.tab = i;
        else if (in_box(CLAY_IDI("tab", i), x, y)) h.tab = i;
    }
    static const struct { const char *id; UiBar b; } BAR[] = {
        { "nav-back", UB_BACK }, { "nav-fwd", UB_FORWARD }, { "nav-up", UB_UP }, { "loc-toggle", UB_LOCATION },
        { "search", UB_SEARCH }, { "crumb-left", UB_LEFT }, { "crumb-right", UB_RIGHT },
        { "trash-restore", UB_RESTORE }, { "trash-empty", UB_EMPTY }, { "view-list", UB_VIEW_LIST }, { "view-icons", UB_VIEW_ICONS },
        { "zoom-out", UB_ZOOM_OUT }, { "zoom-in", UB_ZOOM_IN },
    };
    for (int k = 0; k < COUNT(BAR) && h.bar == UB_NONE; k++)
        if (in_box(Clay_GetElementId(cz(BAR[k].id)), x, y)) h.bar = BAR[k].b;
    for (int i = app.crumb_lo; h.bar == UB_NONE && i < app.crumb_hi; i++)
        if (in_box(CLAY_IDI("crumb", i), x, y)) h.bar = UB_CRUMB, h.crumb = i;
    // The cursor's full name in the icon view lies over the cells below it; a press there is the
    // cell's underneath, so a double click never opens the cursor's entry from another's place
    // (reviewer-2, F6.2 review).
    for (int r = 0; r < list_cells() && h.row < 0; r++)
        if (in_box(CLAY_IDI("row", r), x, y)) h.row = r;
    for (int i = 0; i < jobs.nrun && h.job_x < 0; i++)
        if (in_box(CLAY_IDI("job-x", i), x, y)) h.job_x = i;
    for (int r = 0; side_w() && r < app.ui.side.rows && h.side < 0; r++)
        if (in_box(CLAY_IDI("side-row", r), x, y)) h.side = r;
    if (h.side >= 0 && app.ui.side_mode == SIDE_TREE && in_box(CLAY_IDI("side-twisty", h.side), x, y)) h.twisty = h.side;
    if (h.side >= 0 && app.ui.side_mode == SIDE_PLACES && in_box(CLAY_IDI("side-eject", h.side), x, y)) h.eject = h.side;
    for (int k = 0; k < SIDE_MODES && h.mode < 0; k++)
        if (in_box(CLAY_IDI("mode", k), x, y)) h.mode = k;
    h.zoom = in_box(CLAY_ID("zoom-track"), x, y);   // only drawn in the icon view
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
    // The menu bar (F7.1): with a menu open a press elsewhere only closes it.
    in->menu_title = h.menu_title, in->menu_item = h.menu_item;
    if (app.ui.menu_on) {
        in->menu_away = h.menu_title < 0 && h.menu_item < 0;
        return;
    }
    if (h.row >= 0) {
        if (!mods && ui_press(&app.press, h.row, now_ms()) == 2) in->dclick = h.row;
        else in->click = h.row, in->click_mods = mods;
    }
    in->side_click = h.twisty >= 0 || h.eject >= 0 ? -1 : h.side;   // the mark opens or closes; the rest of the row goes there
    in->side_eject = h.eject;
    in->header = h.header;
    in->bar = h.bar, in->crumb = h.crumb;
    in->modal = h.modal;
    in->prop_box = h.prop_box;
    if (h.ow_row >= 0) {
        if (ui_press(&app.ow_press, app.ui.ow_list.top + h.ow_row, now_ms()) == 2) in->ow_drow = h.ow_row;
        else in->ow_row = h.ow_row;
    }
    in->ow_field = h.ow_field;
    if (h.bmp_row >= 0) {
        if (ui_press(&app.ow_press, app.ui.bmp_list.top + h.bmp_row, now_ms()) == 2) in->bmp_drow = h.bmp_row;
        else in->bmp_row = h.bmp_row;
    }
    in->bmp_btn = h.bmp_btn;
    in->toggle = h.toggle;
    in->find_x = h.find_x, in->find_field = h.find_field, in->find_step = h.find_step, in->search_field = h.search_field;
    in->job_x = h.job_x;
    in->side_mode = h.mode;
    in->side_twisty = h.twisty;
    // The zoom track: the size nearest the press, and the knob held until the button is let go.
    if (h.zoom) in->zoom_to = zoom_level_at(x), app.zoom_drag = true;
    if (h.tab_x >= 0) in->tab_close = h.tab_x;
    else if (h.tab >= 0) in->tab_click = h.tab;
}

// A middle press at a point: a folder (a row, a place, a path button) in a background tab, or a
// tab closed.
static void middle_at(UiInput *in, float x, float y)
{
    Hit h = hit_at(x, y);   // a menu open: only its titles and entries, so nothing here (F7.1)
    if (h.tab >= 0) in->tab_close = h.tab;
    else if (h.row >= 0) in->mid_row = h.row;
    else if (h.side >= 0) in->mid_side = h.side;
    else if (h.bar == UB_CRUMB) in->mid_crumb = h.crumb;
}

// A right press at a point (F8): a row's menu, or the folder shown's on the files' empty space;
// with a menu open it only closes it, as a left press elsewhere does. Where it opens: here.
static void right_at(UiInput *in, float x, float y)
{
    Hit h = hit_at(x, y);
    if (app.ui.menu_on) {
        in->menu_away = true;
        return;
    }
    if (modal_kind() != MODAL_NONE) return;
    app.ctx_x = x, app.ctx_y = y;
    if (h.tab >= 0) in->ctx_tab = h.tab;
    else if (h.row >= 0) in->ctx_row = h.row;
    else if (h.side >= 0) in->ctx_side = h.side;
    else if (h.bar == UB_CRUMB) in->ctx_crumb = h.crumb;
    else if (in_box(CLAY_ID("list"), x, y)) in->ctx_here = true;
}

// A right-click menu's size as layout_menu draws it (F8), before it is drawn: its rows and lines,
// the widest label and key on one row (the gaps between them, the marks' column), its padding.
static Vector2 menu_size(int m)
{
    float row = 0, hgt = 8;
    bool marks = false;
    for (int i = 0; i < UI_MENU[m].n; i++) {
        UiMenuItem it = UI_MENU[m].items[i];
        if (it == UM_SEP ? !ui_menu_sep_shown(&app.ui, m, i) : !ui_menu_shown(&app.ui, it)) continue;   // (as layout_menu leaves them out)
        if (it == UM_SEP) {
            hgt += MENU_SEP_H;
            continue;
        }
        hgt += MENU_ROW_H;
        marks |= UI_MENU_ITEM[it].mark != 0;
        float lw = str_width(str_from_c(UI_MENU_ITEM[it].label)) + str_width(str_from_c(UI_MENU_ITEM[it].key));
        if (lw > row) row = lw;
    }
    // The menu's padding, an entry's, the marks' column and its gap, the widest label and key on one
    // row, two gaps of 32 round the 1-pixel spring between them: as layout_menu lays them out.
    float wid = 8 + 20 + (marks ? 14 + 6 : 0) + row + 2 * 32 + 1;
    return (Vector2){ wid > MENU_W_MIN ? wid : MENU_W_MIN, hgt + 2 };
}

// A right-click menu just opened (F8): its top left. By the pointer, at the press, flipped left or
// up where it would leave the window; by a key, under the cursor's entry (above it near the
// bottom), or the files' top left with none drawn. Kept inside the window either way.
static void ctx_place(void)
{
    Vector2 sz = menu_size(app.ui.menu);
    float w = (float)GetScreenWidth(), h = (float)GetScreenHeight(), x = app.ctx_x, y = app.ctx_y;
    if (app.ui.ctx_key) {
        Clay_ElementData r = Clay_GetElementData(CLAY_IDI("row", (int)(app.ui.list.sel - app.ui.list.top)));
        Clay_ElementData l = Clay_GetElementData(CLAY_ID("list"));
        Clay_ElementData f = app.ui.menu != UI_CTX_FOLDER ? (Clay_ElementData){ 0 }
                             : app.ui.ctx_crumb >= 0 ? Clay_GetElementData(CLAY_IDI("crumb", app.ui.ctx_crumb))
                                                     : Clay_GetElementData(CLAY_IDI("side-row", (int)(app.ui.ctx_side - app.ui.side.top)));
        if (f.found) {   // under the sidebar row or path button (F8)
            x = f.boundingBox.x + PAD, y = f.boundingBox.y + f.boundingBox.height;
            if (y + sz.y > h) y = f.boundingBox.y - sz.y;
        } else if (app.ui.menu == UI_CTX_ENTRY && r.found) {
            x = r.boundingBox.x + (app.ui.grid ? r.boundingBox.width / 2 : 2 * PAD + ICON_PX);
            y = r.boundingBox.y + r.boundingBox.height;
            if (y + sz.y > h) y = r.boundingBox.y - sz.y;
        } else if (l.found) {
            x = l.boundingBox.x + PAD, y = l.boundingBox.y + PAD;
        }
    } else {
        if (x + sz.x > w) x -= sz.x;
        if (y + sz.y > h) y -= sz.y;
    }
    if (x + sz.x > w) x = w - sz.x;
    if (y + sz.y > h) y = h - sz.y;
    app.ctx_x = x < 0 ? 0 : x, app.ctx_y = y < 0 ? 0 : y;
}

// Whether any trash folder holds something (the folder menu's Empty Trash, F8).
static bool trash_any(void)
{
    trash_scan();
    for (int i = 0; i < trashd.n; i++) {
        char files[PATH_MAX];
        if (snprintf(files, sizeof files, "%s/files", trashd.dir[i]) >= (int)sizeof files) continue;
        DIR *d = opendir(files);
        if (!d) continue;
        struct dirent *e;
        bool any = false;
        while (!any && (e = readdir(d))) any = strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0;
        closedir(d);
        if (any) return true;
    }
    return false;
}

// A folder menu just opened (F8): its folder, and what ui/ shows for it (Ui.ctx_*): Trash; a place
// (a bookmark's line in the file, a drive's eject); a bookmark of it already.
static void ctx_facts(void)
{
    Ui *u = &app.ui;
    char buf[PATH_MAX];
    const char *p = u->ctx_crumb >= 0 && u->ctx_crumb < app.crumbs.n
                        ? (snprintf(buf, sizeof buf, "%.*s", STR_ARG(ui_crumb_path(&app.crumbs, u->ctx_crumb))), buf)
                        : side_path(u->ctx_side, buf, sizeof buf);
    if (p && is_search(p)) p = app.search_root;   // the "Search" button's: the folder searched (F9), never its address
    snprintf(app.ctx_path, sizeof app.ctx_path, "%s", p ? p : "");
    u->ctx_w = app.ctx_path[0] && access(app.ctx_path, W_OK) == 0;
    u->ctx_trash = strcmp(app.ctx_path, TRASH_URI) == 0;
    u->ctx_full = u->ctx_trash && trash_any();
    u->ctx_bmed = bm_find(&bmarks, app.ctx_path) >= 0;
    u->ctx_bms = bmarks.n;
    if (u->ctx_crumb < 0 && u->side_mode == SIDE_PLACES && u->ctx_side >= 0 && u->ctx_side < app.places.n) {
        const Place *pl = &app.places.p[u->ctx_side];
        u->ctx_place = true;
        u->ctx_eject = place_ejectable(pl);
        if (pl->kind == PL_BOOKMARK) u->ctx_bmk = bm_find(&bmarks, pl->path);
    }
}

// The folder menu's entry it (UA_FOLDER_DO, F8), for its folder: as the files' entries do for one of
// theirs.
static void folder_do(UiMenuItem it)
{
    const char *dir = app.ctx_path;
    Err err = { 0 };
    if (!dir[0]) return;
    switch (it) {
    case UM_OPEN_IT: go_to(dir, STR_EMPTY, 0); break;
    case UM_OPEN_TAB: tab_open_bg(dir); break;
    case UM_SEARCH_IN: {   // below it, in a tab of its own (F9)
        char uri[PATH_MAX];
        if (search_uri(uri, sizeof uri, dir, STR_EMPTY)) tab_open(uri, STR_EMPTY, true);
        break;
    }
    case UM_OPEN_WINDOW:
        if (spawn_window(app.exe, dir, &err)) set_status(false, "New window on %s", dir);
        else set_status(true, "%s", err.msg);
        break;
    case UM_TERMINAL_IN:
        if (spawn_terminal(dir, &err)) set_status(false, "terminal in %s", dir);
        else set_status(true, "%s", err.msg);
        break;
    case UM_CUT_IT: case UM_COPY_IT: copy_targets(it == UM_CUT_IT, dir); break;
    case UM_PASTE_INTO: paste(dir); break;
    case UM_BOOKMARK_IT: bookmark_add(dir); break;
    case UM_EJECT_IT:
        if (app.ui.ctx_side >= 0 && app.ui.ctx_side < app.places.n && place_ejectable(&app.places.p[app.ui.ctx_side])) eject_start(&app.places.p[app.ui.ctx_side]);
        break;
    default: break;
    }
}

static float wheel_left;   // a touchpad's part of a notch, carried to the next frame

// The wheel's notches at x: the files scroll, or with Ctrl held their icons zoom (F6.3); the
// sidebar scrolls by itself. True when the sidebar moved.
static bool wheel_at(UiInput *in, float x, int notches, bool ctrl)
{
    if (app.ui.ow || app.ui.keys) return in->ow_wheel = notches, false;   // the panel's list (F7.3; Keyboard Shortcuts', F7.5)
    if (x >= side_w() && ctrl) in->zoom_wheel = notches;
    else if (x >= side_w()) in->wheel = notches;
    else if (notches) return app.ui.side.top = ui_clamp(app.ui.side.top - notches, 0, ui_max_top(&app.ui.side)), true;
    return false;
}

static int tip_target(Hit h)
{
    switch (h.bar) {
    case UB_BACK: case UB_FORWARD: case UB_UP: case UB_LOCATION: case UB_SEARCH: case UB_LEFT: case UB_RIGHT:
    case UB_VIEW_LIST: case UB_VIEW_ICONS: case UB_ZOOM_OUT: case UB_ZOOM_IN: return TIP_BAR + h.bar;
    default: break;
    }
    if (h.zoom) return TIP_ZOOM;
    if (h.find_step) return TIP_FIND + (h.find_step > 0);
    if (h.mode >= 0) return TIP_MODE + h.mode;
    if (h.eject >= 0) return TIP_EJECT + h.eject;
    if (h.tab_x >= 0) return TIP_TAB_X + h.tab_x;
    return -1;
}

// The zoom slider's knob held, the pointer at x (F6.3): the size nearest it; the button let go, the
// knob is dropped (true then: it is drawn unlit).
static bool zoom_drag_at(UiInput *in, float x, bool held)
{
    if (!app.zoom_drag) return false;
    if (!held) return app.zoom_drag = false, true;
    in->zoom_to = zoom_level_at(x);
    return false;
}

static bool hover_at(float x, float y);

// When the timer wakes the loop: a tooltip due, or the search field's text to search for (F9).
static i64 wake_due(void)
{
    i64 a = ui_tip_due(&app.tip), b = app.search_due;
    return !a ? b : !b ? a : a < b ? a : b;
}

// The hover highlights for the pointer where it is now, not where it last moved: none once it has
// left the window (GLFW keeps its last position), and again after a frame that moved things under
// a still pointer (a folder entered, the view or the size changed). Else a cell stayed lit that
// the pointer wasn't on (Michael, 2026-10-24). True when any changed.
static bool hover_refresh(bool on, float x, float y)
{
    return on ? hover_at(x, y) : hover_at(-1e6f, -1e6f);
}

// What is under the pointer at x, y, for the hover highlights: true when any changed.
static bool hover_at(float x, float y)
{
    Hit h = hit_at(x, y);
    bool changed = h.row != app.hover || h.side != app.side_hover || h.bar != app.bar_hover || h.crumb != app.crumb_hover ||
                   h.tab != app.tab_hover || h.tab_x != app.tab_x_hover || h.mode != app.mode_hover || h.eject != app.eject_hover ||
                   h.modal != app.modal_hover || h.job_x != app.jobx_hover || h.zoom != app.zoom_hover || h.menu_title != app.menu_title_hover ||
                   h.bmp_btn != app.bmp_btn_hover ||
                   h.find_x != app.findx_hover || h.find_step != app.findstep_hover;
    app.menu_title_hover = h.menu_title;
    changed |= ui_menu_hover(&app.ui, h.menu_title, h.menu_item);
    app.hover = h.row, app.side_hover = h.side, app.bar_hover = h.bar, app.crumb_hover = h.crumb;
    app.tab_hover = h.tab, app.tab_x_hover = h.tab_x, app.mode_hover = h.mode, app.eject_hover = h.eject;
    app.modal_hover = h.modal, app.jobx_hover = h.job_x, app.zoom_hover = h.zoom, app.findx_hover = h.find_x, app.findstep_hover = h.find_step;
    app.bmp_btn_hover = h.bmp_btn;
    // Onto another icon button (or off one): its tooltip waits UI_TIP_MS, the timer wakes the loop.
    if (ui_tip_hover(&app.tip, tip_target(h), now_ms())) {
        changed = true;
        timer_at(wake_due());
    }
    return changed;
}

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
    *seen |= wheel_at(&in, m.x, notches, IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
    if (!script_on) *seen |= hover_refresh(IsCursorOnScreen(), m.x, m.y);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int mods = (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ? UI_MOD_CTRL : 0) |
                   (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? UI_MOD_SHIFT : 0);
        press_at(&in, m.x, m.y, mods);
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) middle_at(&in, m.x, m.y);
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) right_at(&in, m.x, m.y);
    bool held = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    // Off the window the drag ends: Wayland's leave releases no button, and a compositor grab
    // (a keybind, a screenshot) can take the release (reviewer-2, F6.3 review).
    bool on = IsCursorOnScreen();
    if (d.x != 0 || !held || !on) *seen |= zoom_drag_at(&in, m.x, held && on);
    in.backs = mouse_backs, mouse_backs = 0;
    in.forwards = mouse_forwards, mouse_forwards = 0;
    // Anything the user did draws a frame: one list, ui_input_acts (unit-tested), so a new click
    // can't be left out. The mode buttons and the tree's marks were (F6, Michael 2026-10-23: a
    // mode click showed only when the pointer next moved).
    *seen |= in.wheel != 0 || ui_input_acts(&in);
    return in;
}

// For find: the name of entry i.
static Str list_name(void *ctx, isize i)
{
    (void)ctx;
    return app.list.e[i].name;
}

// For F2: whether entry i is a folder (its whole name selected).
static bool list_dir(void *ctx, isize i)
{
    (void)ctx;
    return kind_is_dir(app.list.e[i].kind);
}

// ---- signals --------------------------------------------------------------------------------
//
// SIGTERM and SIGINT end the loop like Ctrl+Q, so the process exits through main: CloseWindow
// runs, and so does LeakSanitizer, which a default SIGTERM death skips. SIGUSR1 is another
// window's Close All (F7.1): this one closes as when its own window closes, asking first when jobs
// run. All three are blocked in every thread, and this one takes them with sigwait, then wakes the
// event wait.

static atomic_bool quit_signal, close_signal;
static sigset_t    quit_signals;
static void *volatile planted;   // OFX_TEST_LEAK's block, dropped so nothing points at it

static void *signal_waiter(void *arg)
{
    (void)arg;
    for (;;) {
        int sig;
        if (sigwait(&quit_signals, &sig) != 0) continue;
        if (sig == SIGUSR1) {
            atomic_store(&close_signal, true);
            wake_main();
            continue;
        }
        atomic_store(&quit_signal, true);
        wake_main();
        return NULL;
    }
}

// Close All Windows (F7.1): the other ofx windows in the registry are asked to close (SIGUSR1),
// and this one quits (quit), or has asked first (ui_handle, with jobs). From the menu, it closes
// as when its window closes: at once, or after that question.
static void close_others(bool quit)
{
    if (app.windows[0]) win_signal_others(app.windows, getpid(), SIGUSR1);
    if (quit && jobs.nrun + jobs.nwait > 0) close_asked = true;
    else if (quit) app.quit = true;
}

static void theme_apply(void)
{
    app.theme = theme_load(&scratch);
    icons_reset();
}

#include "script.c"

static void usage(void)
{
    fprintf(stderr, "usage: ofx [--select] [PATH] | --version\n"
                    "  A file explorer: the folder (default: the current one) as a list; on a file, its folder\n"
                    "  with it selected; --select: PATH's folder with PATH selected, a folder too.\n"
                    "  Enter, Right, l open; Backspace, Left, h up; j k, arrows, PgUp PgDn, Home End, g G move;\n"
                    "  Alt+Left/Right back and forward; / or Ctrl+F find, n N again; . or Ctrl+H hidden files;\n"
                    "  Ctrl+P the owner and permissions columns, Ctrl+G the group column;\n"
                    "  s sort by the next column, S reverse; Tab the places; t terminal here; ~ home;\n"
                    "  Space select, Ctrl+A all; F2 rename; Ctrl+Shift+N new folder; Delete trash, Shift+Delete for good;\n"
                    "  Ctrl+C, Ctrl+X, Ctrl+V copy, cut, paste; Ctrl+Z undo; Ctrl+L type a location; Alt+Up up;\n"
                    "  Ctrl+T new tab, Ctrl+W close it, Ctrl+Tab / Ctrl+PgDn next, Ctrl+Shift+Tab / Ctrl+PgUp previous,\n"
                    "  Alt+1..9 a tab, Ctrl+Shift+PgUp/PgDn move it; middle click a folder for a tab behind;\n"
                    "  F10 or Alt+F the File menu; Ctrl+N a new window; Ctrl+Q close every ofx window.\n");
}

// --select PATH: PATH's folder (links in it resolved) and PATH's own name, not resolved, so a
// folder or a link is selected where it is (FileManager1's ShowItems, 2026-10-26). False, errno
// set, when the folder or the name isn't there. "/", "." and ".." are resolved whole first (the
// folder they name, then selected in its own parent; "/" alone as a plain start).
static bool select_path(const char *start, char *out, size_t cap)
{
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s", start);
    for (size_t n = strlen(p); n > 1 && p[n - 1] == '/'; n--) p[n - 1] = 0;
    char *slash = strrchr(p, '/');
    const char *base = slash ? slash + 1 : p;
    if (!*base || strcmp(base, ".") == 0 || strcmp(base, "..") == 0) return realpath(p, out) != NULL;
    char parent[PATH_MAX];
    if (!realpath(!slash ? "." : slash == p ? "/" : (*slash = 0, p), parent)) return false;
    int n = snprintf(out, cap, "%s/%s", strcmp(parent, "/") == 0 ? "" : parent, base);
    if (n < 0 || (size_t)n >= cap) return errno = ENAMETOOLONG, false;
    struct stat st;
    return lstat(out, &st) == 0;
}

int main(int argc, char **argv)
{
    const char *start = NULL;
    bool selecting = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) return usage(), 0;
        if (strcmp(argv[i], "--version") == 0) return printf("ofx %s\n", OFX_VERSION), 0;
        if (strcmp(argv[i], "--select") == 0 && !selecting) {
            selecting = true;
            continue;
        }
        if (argv[i][0] == '-' || start) return usage(), 2;
        start = argv[i];
    }
    char dir[PATH_MAX];
    selecting = selecting && start;
    if (selecting ? !select_path(start, dir, sizeof dir) : !realpath(start ? start : ".", dir)) {
        fprintf(stderr, "ofx: %s: %s\n", start ? start : ".", strerror(errno));
        return 1;
    }
    // Every path is absolute from here on: off the folder ofx was started in, so that folder's
    // drive isn't kept busy by ofx (or by what it starts) when it is to be ejected (reviewer-2, F6.1).
    if (chdir("/") != 0) fprintf(stderr, "ofx: chdir /: %s\n", strerror(errno));

    // Before any thread exists (GLFW and Mesa start some), so all of them inherit the mask.
    sigemptyset(&quit_signals);
    sigaddset(&quit_signals, SIGTERM);
    sigaddset(&quit_signals, SIGINT);
    sigaddset(&quit_signals, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &quit_signals, NULL);
    spawn_init();
    // OFX_TEST_LEAK=1 leaks one block on purpose: the control that shows LeakSanitizer reports
    // from this binary at all.
    const char *delay = getenv("OFX_TEST_WATCH_DELAY_MS");
    watch_delay_ms = delay ? atoi(delay) : 0;
    if (watch_delay_ms < 0 || watch_delay_ms > 1000) watch_delay_ms = 0;   // a window must never stall long
    // OFX_TEST_COPY_DELAY_MS: a sleep per copied block, so a test sees the progress (capped short:
    // a job never stalls the window, but a test must not run long).
    // OFX_TEST_ICONS_MAX: a smaller icon cache, so a test can fill it.
    const char *imax = getenv("OFX_TEST_ICONS_MAX");
    app.icons_max = imax ? atoi(imax) : ICONS_MAX;
    if (app.icons_max < 1 || app.icons_max > ICONS_MAX) app.icons_max = ICONS_MAX;
    const char *cdelay = getenv("OFX_TEST_COPY_DELAY_MS");
    app.copy_delay_ms = cdelay ? atoi(cdelay) : 0;
    if (app.copy_delay_ms < 0 || app.copy_delay_ms > 500) app.copy_delay_ms = 0;
    // OFX_TEST_CROSS=1: a move copies as across drives, so a test can see a move run (F5.2).
    if (getenv("OFX_TEST_CROSS")) job_test_cross = true;
    // The test guard: with OFX_GUI_ROOT set (the GUI tests), no write outside it.
    ops_guard(getenv("OFX_GUI_ROOT"));
    const char *media = getenv("OFX_TEST_MEDIA");   // tests: mounts under it are removable (places.c)
    if (media && media[0]) snprintf(places_test_media, sizeof places_test_media, "%s", media);
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
    arena_init(&treea, "tree", tree_mem, sizeof tree_mem);
    arena_init(&bma, "bookmarks", bm_mem, sizeof bm_mem);
    arena_init(&bmwa, "bookmarks-write", bmw_mem, sizeof bmw_mem);
    arena_init(&doomeda, "delete", doomed_mem, sizeof doomed_mem);
    arena_init(&propsa, "properties", props_mem, sizeof props_mem);
    arena_init(&owa, "open with", ow_mem, sizeof ow_mem);
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
    // while answers for the old one are on their way, or (60 s) stand in for a hung mount at quit.
    const char *kdelay = getenv("OFX_TEST_COUNT_DELAY_MS");
    counts.delay_ms = kdelay ? atoi(kdelay) : 0;
    if (counts.delay_ms < 0 || counts.delay_ms > 60000) counts.delay_ms = 0;
    count_start();
    walk_start();
    search_start();
    thumbs_start();
    space_start();
    timer_start();
    places_reload();
    // New Window starts this binary (F7.1); one replaced since it started reads "… (deleted)".
    ssize_t xn = readlink("/proc/self/exe", app.exe, sizeof app.exe - 1);
    app.exe[xn > 0 ? xn : 0] = 0;
    if (xn <= 0 || strstr(app.exe, " (deleted)")) snprintf(app.exe, sizeof app.exe, "ofx");
    // In the window registry, for another window's Close All (F7.1).
    Err werr;
    if (win_dir(app.windows, sizeof app.windows) && !win_register(app.windows, getpid(), &werr)) {
        fprintf(stderr, "ofx: %s\n", werr.msg);
        app.windows[0] = 0;
    }
    char state_file[PATH_MAX];
    app.ui.perms_cols = app.ui.colors = true;   // (on without a state file too)
    if (state_path(state_file, sizeof state_file)) {
        State st = state_load(&scratch, state_file);
        app.ui.side_mode = st.side, app.ui.grid = st.icons, app.ui.zoom = st.zoom, app.ui.perms_cols = !st.no_perms, app.ui.bar_hidden = st.no_bar,
        app.ui.colors = !st.no_colors, app.ui.group_col = st.group;
    }
    side_refresh();
    list_fit();
    ui_resize(&app.ui.side, side_rows(GetScreenHeight()));
    app.press.row = -1, app.ow_press.row = -1;
    // Started on a file (a desktop entry's %f, xdg-open on a file's folder), or with --select
    // (ShowItems): its folder, it selected.
    struct stat sst;
    Str pick = STR_EMPTY;
    if ((selecting && strcmp(dir, "/") != 0) || (stat(dir, &sst) == 0 && !S_ISDIR(sst.st_mode))) {
        char *slash = strrchr(dir, '/');
        static char base[NAME_MAX + 1];
        snprintf(base, sizeof base, "%s", slash + 1);
        pick = str_from_c(base);
        if (slash == dir) dir[1] = 0;
        else *slash = 0;
    }
    go_to(dir, pick, 0);
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
        // Rows and columns for the window as it is, before a frame shows them: a resize, the
        // sidebar's mode, the view (F6.2) and the jobs' rows all change them.
        int rows = list_rows(GetScreenHeight()), srows = side_rows(GetScreenHeight()), cols = list_cols(GetScreenWidth());
        if (app.zoomed) ui_refit(&app.ui.list, cols, rows), app.zoomed = false, dirty = true;
        // Once a zoom or view change: the cache emptied, it fills with this size's icons (a frame
        // that alone needs more than it holds would otherwise empty it every frame: f62c).
        if (app.icons_missed && app.icons_reset_armed) icons_reset(), app.icon_full_resets++, app.icons_reset_armed = false, dirty = true;
        app.icons_missed = false;
        if (cols != (int)ui_cols(&app.ui.list)) ui_set_cols(&app.ui.list, cols), dirty = true;
        if (rows != app.ui.list.rows) ui_resize(&app.ui.list, rows), dirty = true;
        if (srows != app.ui.side.rows) ui_resize(&app.ui.side, srows), dirty = true;
        if (ui_tip_tick(&app.tip, now_ms())) dirty = true;
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
            // The frame may have moved things under the pointer: lit again, at once.
            Vector2 m = GetMousePosition();
            if (scripted ? script.pointing && hover_refresh(true, script.px, script.py) : hover_refresh(IsCursorOnScreen(), m.x, m.y))
                dirty = true, glfwPostEmptyEvent();
            if (app.icons_missed && app.icons_reset_armed) dirty = true, glfwPostEmptyEvent();   // emptied and drawn again at once
        }
        PollInputEvents();   // waits for input, or for watch.c's wake-up
        if (atomic_exchange(&close_signal, false)) close_asked = true;
        if (atomic_exchange(&watch.theme_ready, false)) theme_apply(), app.themes++, dirty = true;
        if (atomic_exchange(&watch.list_ready, false)) reload(), dirty = true;
        if (atomic_exchange(&watch.mounts_ready, false)) places_reload(), dirty = true;
        dirty |= jobs_poll();
        if (app.have_list && count_take(app.count, app.list.n)) dirty = true;
        if (space_take(&app.places, app.cwd, &app.cwd_avail)) dirty = true;
        if (eject_take()) dirty = true;
        if (props.walking && walk_take(&props.sum, &props.done)) dirty = true;
        if (props.walking && !app.ui.props) walk_ask(NULL, 0), props.walking = false;   // the panel closed: the count stops
        if (thumbs_take()) dirty = true;
        if (app.ui.in_search && search_take()) search_relist(), dirty = true;
        if (app.search_due && now_ms() >= app.search_due) app.search_due = 0, search_retype(), dirty = true;
        bool seen;
        UiInput in = input_real(&seen);
        if (scripted) {
            UiInput real = in;
            in = ui_input_none();   // the user's input does not reach a scripted window (Ctrl+Q still quits),
            if (script.user_keys) {   // but after `user-keys` the keyboard's does (wtype in the test sway, 2026-10-28)
                memcpy(in.ev, real.ev, sizeof *real.ev * (size_t)real.nev);
                in.nev = real.nev;
            }
            if (!script_input(&in)) break;
            seen = true;
        }
        in.crumb_n = app.crumbs.n, in.crumb_last = app.crumb_hi - 1;
        in.job_n = jobs.nrun, in.jobs_all = jobs.nrun + jobs.nwait;
        for (int k = 0; k < jobs.nrun; k++) in.job_id[k] = job_recs[jobs.run[k]].id;
        if (close_asked && in.nev < UI_EVENTS_MAX) in.ev[in.nev++] = (UiEvent){ UK_QUIT, 0 }, close_asked = false, seen = true;
        // A Ctrl+V in a text field reads the clipboard first, and only then.
        bool pasting = false;
        for (int k = 0; k < in.nev; k++) pasting |= in.ev[k].key == UK_PASTE;
        if (pasting && ui_line_active(&app.ui, NULL)) in.paste = text_paste();   // (whichever text field has the keys)
        // A message stays until the next key or click; an error at least a second.
        if (ui_input_acts(&in) && ui_status_clears(app.status_bad, app.status_at, now_ms())) app.status[0] = 0;
        if (ui_input_acts(&in) && ui_tip_act(&app.tip)) seen = true;   // a click or key: the tooltip goes
        bool was_editing = app.ui.focus == UI_FOCUS_EDIT, was_confirming = app.ui.confirming;
        if (app.ui.ow) in.app_name = ow.names, in.app_n = ow.shown < UI_APPS_MAX ? ow.shown : UI_APPS_MAX;
        UiAction a = ui_handle(&app.ui, (UiNames){ list_name, NULL, list_dir }, &in);
        bool ctx_up = app.ui.menu_on && app.ui.menu >= UI_MENUS;   // a right-click menu opened: where (F8)
        if (ctx_up && !app.ctx_placed && app.ui.menu == UI_CTX_FOLDER) ctx_facts();
        if (ctx_up && !app.ctx_placed && app.ui.menu == UI_CTX_ENTRY) {   // (Paste Into Folder: the cursor's, writable)
            char e[PATH_MAX];
            app.ui.ctx_w = app.ui.ctx_dir && app.ui.list.sel < app.list.n && entry_path(app.ui.list.sel, e, sizeof e) && access(e, W_OK) == 0;
        }
        if (ctx_up && !app.ctx_placed) ctx_place();
        app.ctx_placed = ctx_up;
        if (!app.ui.ow && ow.icons_on) ow_drop();   // the panel closed: its icons go (F7.3)
        if (app.ui.copied) text_copy((Str){ app.ui.copy, app.ui.copy_len }), app.ui.copied = false;
        // Shift+Delete and its y can come in one frame's input: the question opened and was answered
        // within ui_handle, so the targets are taken here either way (reviewer-2, F5 confirm). A
        // new question always starts from no targets.
        if (!was_confirming && (app.ui.confirming || a.kind == UA_DELETE)) delete_ask();
        // F2 opened the edit line: on the cursor's entry, as it is named now.
        if (!was_editing && app.ui.focus == UI_FOCUS_EDIT && !app.ui.edit_path) edit_opened();
        if (a.kind != UA_NONE) act(a);
        // The search field changed (F9): it is searched for 150 ms after the last change.
        if (app.ui.search_edited) app.ui.search_edited = false, app.search_due = now_ms() + SEARCH_TYPE_MS, timer_at(wake_due());
        dirty |= seen;
    }
    if (scripted) script_end();
    if (app.windows[0]) win_unregister(app.windows, getpid());
    jobs_stop();
    clip_quit();
    count_stop();
    walk_stop();
    search_stop();
    ui_matcher_set(&app.ui.find_m, STR_EMPTY, app.ui.find_opts), ui_matcher_set(&app.ui.ow_m, STR_EMPTY, app.ui.ow_opts);   // (a compiled expression freed: LeakSanitizer)
    ui_matcher_set(&app.search_m, STR_EMPTY, app.ui.find_opts);
    thumbs_stop();
    space_stop();
    timer_stop();
    for (int i = 0; i < app.nslots; i++)
        if (app.slot[i].tex.id) UnloadTexture(app.slot[i].tex);
    pthread_mutex_lock(&wake_mu);
    wake_off = true;
    pthread_mutex_unlock(&wake_mu);
    CloseWindow();
    return scripted && script.failed ? 1 : 0;
}
