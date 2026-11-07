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
#include "rlgl.h"     // (rlSetBlendFactorsSeparate: the drag image, F14)

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

// A test knob: name's value when it is a number from lo to hi, else none (def).
static int env_int(const char *name, int def, int lo, int hi)
{
    const char *v = getenv(name);
    int n = v && v[0] ? atoi(v) : def;
    return n < lo || n > hi ? def : n;
}

enum { QUIT_WAIT_MS = 200, QUIT_JOBS_MS = 500 };   // (quit_workers)

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

// A worker thread told to quit, as each one's X_quit does (code-health review, 2026-11-03: six
// copies): quit set and its threads woken under its lock, and gen (when there is one) moved, so a
// long job of its own sees it. Nothing when it never started.
static void worker_quit(bool started, pthread_mutex_t *mu, pthread_cond_t *cv, bool *quit, atomic_uint *gen)
{
    if (!started) return;
    pthread_mutex_lock(mu);
    *quit = true;
    if (gen) atomic_fetch_add(gen, 1);
    pthread_cond_broadcast(cv);
    pthread_mutex_unlock(mu);
}

// A wake-up at a time to come: the main loop sleeps until an event, so a tooltip's delay needs a
// thread that sleeps until then and wakes it (Michael, 2026-10-24).
static struct {
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    i64             due;   // now_ms() time; 0 for none
    bool            quit;
    pthread_t       th;
    bool            started;
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
    timer.started = pthread_create(&timer.th, NULL, timer_main, NULL) == 0;   // (without it, no tooltip waits for its delay)
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
    if (timer.started) pthread_join(timer.th, NULL);
}

#include "watch.c"
#include "clipboard.c"
#include "dnd.c"
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
static u8 drag_mem[MiB(2)];        // the files a drag carries (F13), apart from the clipboard
static u8 paste_mem[MiB(2)];       // another program's files, read for one paste
static u8 tree_mem[MiB(4)];        // the sidebar tree's paths and names; reset per build
static u8 bm_mem[MiB(1)], bmw_mem[MiB(1)];   // the bookmarks file as the menu lists it; a change's own read (F7.5)

static Arena frame, lists[2], data, icona, scratch, undoa, owna, pastea, treea, bma, bmwa, draga;
static Bookmarks bmarks;   // the bookmarks file's lines (F7.5), read with the places
static int   watch_delay_ms;   // OFX_TEST_WATCH_DELAY_MS, for one test

enum { BAR_BTN_W = 34, BAR_BTN_H = 30, BAR_GAP = 4, ARROW_W = 22, CRUMB_PAD = 10, CRUMB_ICON_PX = 16 };
enum { TREE_STEP = 10, TRASH_BAR_H = 38, SEARCH_BAR_H = 38, SEARCH_ROW_H = 32 };   // (SEARCH_ROW_H: "Containing", F9 step 3)
// The menu bar (F7.1): a row of titles over the top bar; a menu's entries, its lines between groups.
enum { MENU_H = 26, MENU_TITLE_PAD = 10, MENU_ROW_H = 26, MENU_SEP_H = 9, MENU_W_MIN = 280 };
enum { TAB_H = 30, TAB_X_W = 22, JOB_ROW_H = 40, JOB_WAIT_H = 22, MODE_BTN_W = 28, MODE_BTN_H = 22, MODE_GAP = 4, FIND_BAR_H = 36 };
enum { FONT_PX = 15, ROW_H = 28, ICON_PX = 24, HEADER_H = 40, COLS_H = 26, FOOTER_H = 28, PAD = 12, SIZE_W = 90,
       DATE_W = 100, OWNER_W = 100, OWNER_PAD = 20, GROUP_W = 80, PERM_W = 84, SIDE_EDGE = 4, SIDE_ROW_H = 28, SIDE_ICON_PX = 20, GAP = 10, ICONS_MAX = 512 };
// The icon view (F6.2): a cell holds its icon (48 px at first, F6.3's zoom from 32 to 256) over two
// lines of its name; the cursor's name in full, up to CELL_LINES_FULL lines, over the cells below it.
enum { CELL_W_MIN = 112, CELL_W_ROOM = 64, CELL_H_ROOM = 56, CELL_PAD = 4, CELL_LINE_H = FONT_PX + 4, CELL_LINES_FULL = 8 };
// The footer's zoom slider (F6.3): a minus, a track with a notch a size, a plus.
enum { ZOOM_BTN_W = 22, ZOOM_TRACK_W = 84, ZOOM_KNOB_W = 10 };
enum { GIT_W = 16 };   // the git letter's column, after the name, in a repository (F12)

// ---- state ----------------------------------------------------------------------------------

typedef struct {
    Str        key;       // the MIME type, or a folder's icon name
    Texture2D  tex;       // id 0: no file for it, or it would not load
    char       file[PATH_MAX];
} IconSlot;

// What the left button holds, from its press until it is let go (input.c's drag_at): the zoom
// slider's knob, the sidebar's edge; a press on files that may become a drag of them (DRAG_PRESS,
// until it moves UI_DRAG_PX), and that drag (DRAG_FILES, F13 step 3); then F14's, which the
// compositor carries and ends: ours handed to it past the window's edge (DRAG_OUT), another
// program's files over the window (DRAG_IN, no button of ours held).
enum { DRAG_NONE, DRAG_ZOOM, DRAG_SIDE, DRAG_PRESS, DRAG_FILES, DRAG_OUT, DRAG_IN };

// Where a drag of files would land (F13 step 3), and what is drawn lit for it: a folder row (at:
// its place on screen), a sidebar row (the Trash's among them), a tab, a path button, the files'
// empty space. DROP_AGAIN: an at that is no place, so drop_find asks again.
typedef enum { DROP_NONE, DROP_ROW, DROP_SIDE, DROP_TAB, DROP_CRUMB, DROP_LIST } DropKind;
enum { DROP_AGAIN = -2 };
typedef struct {
    DropKind kind;            // what is under the pointer
    int      at;
    bool     ok;              // and it takes the files (drop_takes): lit, and a release drops there
    bool     trash;           // the Trash place: the files trashed, not moved
    bool     same_drive;      // the folder on the first file's drive
    char     dir[PATH_MAX];   // the folder they go into
    int      reloads;         // app.reloads when it was found: a listing since moves the rows under it
} Drop;

// What a point of the window is over (hit_at): for a press, and for the hover highlights.
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
    bool  side_edge;    // the sidebar's right edge, its handle (F13)
    int   menu_title;   // a menu bar title, -1 for none (F7.1)
    int   menu_item;    // an entry of the open menu (its place), -1 for none
    int   prop_box;     // a checkbox of the Properties panel's grid, -1 for none (F7.2)
    int   pick_row;     // a row of the picker panel's list (Open With's, the branches'; on screen), -1 for none
    bool  find_x;       // the find bar's ×
    bool  pick_field;   // its field
    int   toggle;       // a toggle of its field (1..4: the 4th "Always open … with this app"), 0 for none
    int   find_step;    // the find bar's chevrons: -1 the previous, 1 the next, 0 none
    int   bmp_row;      // a row of Edit Bookmarks' list (on screen), -1 for none (F7.5)
    int   bmp_btn;      // one of its buttons (UI_BMP_*), 0 for none
    int   field;        // a text bar's field (UiField), -1 for none; with field_toggle one of its toggles (1..3)
    int   field_toggle;
    bool  inside_btn;   // the search bar's "In files" (F9 step 3)
} Hit;
// Whether two hits are over the same things: hover_at draws again when not. Every field, lit or not
// (one that isn't drawn lit costs a frame more; one left out was a highlight that stayed where the
// pointer left: the F11 review found one, and no script can see it). A field added changes the
// size below: add it here too (F13 code-health review, 2026-11-04).
static bool hit_same(Hit a, Hit b)
{
    return a.row == b.row && a.side == b.side && a.header == b.header && a.bar == b.bar && a.crumb == b.crumb && a.tab == b.tab &&
           a.tab_x == b.tab_x && a.modal == b.modal && a.job_x == b.job_x && a.mode == b.mode && a.twisty == b.twisty && a.eject == b.eject &&
           a.zoom == b.zoom && a.side_edge == b.side_edge && a.menu_title == b.menu_title && a.menu_item == b.menu_item && a.prop_box == b.prop_box && a.pick_row == b.pick_row &&
           a.find_x == b.find_x && a.pick_field == b.pick_field && a.toggle == b.toggle && a.find_step == b.find_step && a.bmp_row == b.bmp_row && a.bmp_btn == b.bmp_btn &&
           a.field == b.field && a.field_toggle == b.field_toggle && a.inside_btn == b.inside_btn;
}
_Static_assert(sizeof(Hit) == 100, "a new Hit field: hit_same compares it");

// Under nothing: the start of hit_at, and the hover before the first frame (F11 review: two copies
// had drifted, the hover's without .field).
#define HIT_NONE                                                                                                   \
    { .row = -1, .side = -1, .header = -1, .bar = UB_NONE, .crumb = -1, .tab = -1, .tab_x = -1, .job_x = -1, .mode = -1, \
      .twisty = -1, .eject = -1, .menu_title = -1, .menu_item = -1, .prop_box = -1, .pick_row = -1, .bmp_row = -1, .field = -1 }

static struct {
    char      cwd[PATH_MAX];
    // The search view (F9): the folder searched, what the walk left out so far and whether it ended,
    // when the field's last change runs (0: none waiting), and the entry to put the cursor on when
    // it comes (back to a search, the one left from).
    char        search_root[PATH_MAX];
    SearchSkips search_s;
    bool        search_done;
    i64         search_due;
    isize       empty_n;      // the trash's items as the Empty Trash question opened (-1: not counted)
    char        search_pick[PATH_MAX];
    UiMatcher   search_m;     // its text as the walk matches it: a bad expression's reason, for the bar
    UiMatcher   search_im;    // likewise the text inside files (step 3)
    int       cur;          // lists[cur] holds the listing
    Listing   list;
    Str      *mime;         // per entry, filled as rows are drawn; .p NULL until then
    isize    *count;        // per entry, a folder's item count (counts.c): COUNT_UNKNOWN until asked
    bool      have_list;
    char      error[sizeof(Err)];   // why the folder could not be read
    Ui        ui;           // the list's and sidebar's cursors, find, the sort
    UiPress   press;
    UiPress   panel_press;   // a panel list's rows: Open With's (F7.3) or Edit Bookmarks' (F7.5), never both up
    bool      show_hidden;
    Places    places;
    u8        place_head[PLACES_MAX];   // 1 for a heading row (app.ui.side_head)
    int       place_cur;    // the place holding cwd, -1 for none
    bool      trash_full;   // any trash folder holds something (trash_state): the Trash place's icon
    bool      drive_gone;   // the drive the folder shown was on left the mount list (places_reload): Home
    Conf      conf;         // ofx.conf as last read (F10, prefs.c)
    SearchSkipList skips_shown;   // the list the search shown walks with (search_copy)
    i64       cwd_avail;    // the free space on cwd's drive, -1 unknown
    char      status[600];
    bool      status_bad;
    i64       status_at;
    bool      quit;

    Theme     theme;
    Theme     theme_list;   // the files area's: the theme as given (theme is fitted for the sidebar and the bars, 2026-11-07)
    MimeDb    mime_db;
    UserDirs  user_dirs;
    IconDb    icons;
    IconSlot  slot[ICONS_MAX];
    int       nslots;
    StrMap    slot_of;
    int       icon_scale;   // the scale the cache holds icons for
    Hit       hov;          // what the pointer is over (hover_at), for the highlights; -1 or 0 for nothing
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
    int       copy_delay_ms;   // OFX_TEST_COPY_DELAY_MS
    char      text_copied[PATH_MAX];   // the last text an edit line put on the clipboard (F5.2)
    char      hover_num[32];   // the number drawn on the drive row under the pointer (F6.1), for the tests
    // What the last frame's icon view drew, for the tests (F6.2): its cells, the cursor's name's
    // lines, and the lines of the name under the pointer and whether it was cut.
    int       grid_cells, grid_full_lines, grid_hover_lines;
    bool      grid_hover_cut;
    isize     grid_first, grid_last;   // the entries in the first and last cell drawn, -1 for none
    int       icons_max;    // the icon cache's slots: ICONS_MAX, or OFX_TEST_ICONS_MAX for a test
    int       icon_full_resets;   // a full cache emptied at a listing (F6.2 review), or a zoom
    // A lookup found the cache full; after a zoom or a view change that may empty it once, before
    // the next frame (F6.3 review: zooming in one folder filled it, icons went blank).
    bool      icons_missed, icons_reset_armed;
    UiTip     tip;          // the tooltip of the icon button under the pointer (Michael, 2026-10-24)
    bool      zoomed;       // the icon size changed: the next refit keeps the cursor's row (ui_refit)
    int       drag;         // what the left button holds, or the compositor's drag (DRAG_*, above)
    float     drag_x0;      // where the press was
    int       drag_w0;      // the sidebar's width then
    bool      drag_moved;   // (DRAG_SIDE) it moved: let go, the width is kept
    float     drag_y0, drag_px, drag_py;   // (the press's y; the files' drags: the pointer now, the badge's place)
    char      drag_path[PATH_MAX];   // what the press was on (an entry, a tree row's folder)
    bool      drag_marked;  // a marked entry: the marks are dragged
    bool      drag_tree;    // a tree row: let go without a drag, it opens
    int       drag_side;    // its row on screen: a press let go there without a drag opens it
    char      drag_name[NAME_MAX + 1];   // the badge's: the entry pressed, or the tree row's folder
    char      drag_icon[128];   // its icon's key (a MIME type, or "folder" by that name): looked up when
                                // drawn, as an icon cache reset mid-drag gives its slot to another (drag_icon_slot)
    bool      drag_icon_named;  // the key is the icon's name, not a MIME type
    bool      out_failed;   // (DRAG_FILES) the hand-off to the compositor failed: not tried again this drag
    int       drag_mods;    // Ctrl, Shift held (UI_MOD_*): a copy, a move
    Drop      drop;         // where it would land now
    bool      out_over;     // (DRAG_OUT) back over the window: lit and answered as DRAG_FILES is
    bool      out_own;      // (DRAG_OUT) dropped back on the window: its own drop said what it did
    bool      in_nofiles;   // another program's drag over the window holds no files: the badge says so
    u32       in_id;        // (DRAG_IN) the offer it is of (dnd_in_over): another offer's files are another drag
    int       spring_tab;   // the tab it is held over (not the one shown), -1 none (F13 step 4)
    i64       spring_at;    // since when
    int       side_px;      // the sidebar's width dragged to, UI_SIDE_W at first (fitted to the window: ui_side_fit)
    UiPress   edge_press;   // the sidebar edge's presses, for its double click
    bool      thumb_local;  // the folder's drive is one ofx makes previews on (F6.4: not a network one)
    float     ctx_x, ctx_y;       // a right-click menu's top left (F8): where it opens, kept inside the window
    bool      ctx_placed;         // one is up and placed (ctx_place)
    char      ctx_path[PATH_MAX]; // the folder a folder menu is on (ctx_facts)
    char      exe[PATH_MAX];      // this ofx's binary, for New Window (F7.1); "ofx" when unknown
    char      windows[PATH_MAX];  // the window registry's folder (fs/windows.c); "" for none
} app = { .tip = { -1, 0, false, false },
        .hov = HIT_NONE };

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

// The sidebar's width: none when it is hidden (F6); else the one dragged to, fitted to the window
// (F13).
static int side_w(void)
{
    return app.ui.side_mode == SIDE_HIDDEN ? 0 : ui_side_fit(app.side_px, GetScreenWidth());
}
static Undo      undo;
static ClipFiles own;      // ofx's own copy or cut (own.n 0: none)
static ClipFiles dragged;  // what a drag of files carries (F13 step 3), its own: the clipboard stays as it was
static char     *dragged_real[CLIP_PATHS_MAX];   // each folder of it resolved (realpath), NULL for the rest: drop_takes

// Files dragged over the window, lit and answered: a drag inside it (F13), ours out to other programs
// brought back over it, or another program's (F14).
static bool files_over(void)
{
    return app.drag == DRAG_FILES || app.drag == DRAG_IN || (app.drag == DRAG_OUT && app.out_over);
}

// A drag of files, or a press that may become one: the keys are its own (drag_keys).
static bool drag_of_files(void)
{
    return app.drag == DRAG_PRESS || app.drag == DRAG_FILES || app.drag == DRAG_OUT || app.drag == DRAG_IN;
}

// A drag with another program: the compositor's, ended by it (drag_out_poll, drag_in_poll), never by
// the window's own button or pointer.
static bool drag_by_compositor(void)
{
    return app.drag == DRAG_OUT || app.drag == DRAG_IN;
}
// Bumped by every copy of ours, files or text: a cut's paste empties the clipboard after it only
// if nothing was copied meanwhile (F5.2 review).
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

#include "git.c"       // (before folder.c, which asks it)
#include "remember.c"
#include "folder.c"
#include "clear.c"     // (before actions.c, which runs it)
#include "actions.c"
#include "restore.c"
#include "prefs.c"
#include "icons.c"
#include "files.c"
#include "topbar.c"
#include "panels.c"
#include "footer.c"
#include "frame.c"
#include "input.c"

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
    closing_all = true;
    if (quit && jobs.nrun + jobs.nwait > 0) close_asked = true;
    else if (quit) app.quit = true;
}

static void theme_apply(void)
{
    app.theme = theme_load(&scratch);
    app.theme_list = app.theme;
    theme_fit(&app.theme);   // (2026-11-07: the sidebar's and the bars' colours read on their backgrounds)
    icons_reset();
}

#include "script.c"

// Quitting: every worker told to stop first, so they wind down together, then joined by deadlines
// from that one moment: 200 ms, and half a second for the jobs (a cancelled copy removes its part
// file). A hung mount under all of them costs the longest once; before, each stop told and waited
// in turn, 500 + 5 × 200 ms (F11 review). The clipboard's thread is told with them and joined last
// (clip_close).
static void quit_workers(void)
{
    jobs_quit(), clip_quit(), count_quit(), walk_quit(), search_quit(), thumbs_quit(), space_quit(), git_quit();
    struct timespec until = deadline_in(QUIT_WAIT_MS), jobs_until = deadline_in(QUIT_JOBS_MS);
    count_join(&until), walk_join(&until), search_join(&until), thumbs_join(&until), space_join(&until), git_join(&until);
    jobs_join(&jobs_until), clear_join(&until);
    clip_close();
    dnd_close();
}

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
                    "  F10 or Alt+F the File menu; Ctrl+N a new window; Ctrl+Q close every ofx window.\n"
                    "  The rest (search, Ctrl+Shift+T reopen a closed tab, ...): F1 in a window.\n");
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

// The window closed for good: no other thread wakes the loop after this (wake_main), as GLFW goes.
// Every way out once it opened (review, 2026-11-03: a bad script's return skipped wake_off).
static void wake_close(void)
{
    pthread_mutex_lock(&wake_mu);
    wake_off = true;
    pthread_mutex_unlock(&wake_mu);
    CloseWindow();
}

// The tests' knobs and the guard (OFX_GUI_ROOT), read once at the start.
static void test_knobs(void)
{
    watch_delay_ms = env_int("OFX_TEST_WATCH_DELAY_MS", 0, 0, 1000);   // a window must never stall long
    // OFX_TEST_ICONS_MAX: a smaller icon cache, so a test can fill it.
    app.icons_max = env_int("OFX_TEST_ICONS_MAX", ICONS_MAX, 1, ICONS_MAX);
    // OFX_TEST_COPY_DELAY_MS: a sleep per copied block, so a test sees the progress (capped short:
    // a job never stalls the window, but a test must not run long).
    app.copy_delay_ms = env_int("OFX_TEST_COPY_DELAY_MS", 0, 0, 500);
    // OFX_TEST_CROSS=1: a move copies as across drives, so a test can see a move run (F5.2).
    if (getenv("OFX_TEST_CROSS")) job_test_cross = true;
    // The test guard: with OFX_GUI_ROOT set (the GUI tests), no write outside it.
    ops_guard(getenv("OFX_GUI_ROOT"));
    // OFX_TEST_SANDBOX=off, under the guard only: bubblewrap's runs as they are (fs/sandbox.c), set
    // before any thread that runs one starts (code-health review, 2026-11-06: thumbs_start set it,
    // after git's worker could already have read it).
    const char *sb = getenv("OFX_TEST_SANDBOX");
    box_sandbox = !(ops_root[0] && sb && strcmp(sb, "off") == 0);
    const char *media = getenv("OFX_TEST_MEDIA");   // tests: mounts under it are removable (places.c)
    if (media && media[0]) snprintf(places_test_media, sizeof places_test_media, "%s", media);
    // OFX_TEST_LEAK=1 leaks one block on purpose: the control that shows LeakSanitizer reports
    // from this binary at all.
    const char *leak = getenv("OFX_TEST_LEAK");
    if (leak && leak[0]) planted = malloc(4096), planted = NULL;
}

static void arenas_init(void)
{
    arena_init(&frame, "frame", frame_mem, sizeof frame_mem);
    arena_init(&lists[0], "list", list_mem[0], sizeof list_mem[0]);
    arena_init(&lists[1], "list", list_mem[1], sizeof list_mem[1]);
    arena_init(&data, "data", data_mem, sizeof data_mem);
    arena_init(&icona, "icons", icon_mem, sizeof icon_mem);
    arena_init(&scratch, "scratch", scratch_mem, sizeof scratch_mem);
    arena_init(&undoa, "undo", undo_mem, sizeof undo_mem);
    arena_init(&owna, "clipboard", own_mem, sizeof own_mem);
    arena_init(&draga, "drag", drag_mem, sizeof drag_mem);
    arena_init(&pastea, "paste", paste_mem, sizeof paste_mem);
    arena_init(&treea, "tree", tree_mem, sizeof tree_mem);
    arena_init(&bma, "bookmarks", bm_mem, sizeof bm_mem);
    arena_init(&bmwa, "bookmarks-write", bmw_mem, sizeof bmw_mem);
    arena_init(&doomeda, "delete", doomed_mem, sizeof doomed_mem);
    arena_init(&propsa, "properties", props_mem, sizeof props_mem);
    arena_init(&owa, "open with", ow_mem, sizeof ow_mem);
    arena_init(&owia, "open with icons", ow_idb_mem, sizeof ow_idb_mem);
    undo.arena = &undoa;
}

// The window opened on dir, its input callbacks and the signal waiter set, the font and Clay ready.
// False (said) when it can't.
static bool window_open(const char *dir)
{
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
        return false;
    }
    SetExitKey(KEY_NULL);   // Ctrl+Q quits; Esc is ours (it closes a bar, a panel, a menu), not raylib's exit
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
        wake_close();
        return false;
    }
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(sizeof clay_mem, clay_mem);
    Clay_Initialize(arena, (Clay_Dimensions){ (float)GetScreenWidth(), (float)GetScreenHeight() },
                    (Clay_ErrorHandler){ .errorHandlerFunction = on_clay_error });
    Clay_SetMeasureTextFunction(render_measure_text, NULL);
    render_custom = draw_custom;
    return true;
}

// What the window shows before its first folder: the MIME types, the theme, ofx.conf, the watches,
// the clipboard, the worker threads, the places and the trash, the window registry, the state file's
// settings and the tree, the sidebar.
static void app_start(void)
{
    mime_load(&data, &app.mime_db);
    user_dirs_load(&data, &app.user_dirs);
    theme_apply();
    conf_start();   // (F10: before watch_start, which watches the file's folder)
    if (!watch_start()) set_status(true, "inotify is unavailable: no live refresh");
    clip_init();   // false: the clipboard stays inside ofx
    dnd_init();    // (F14: drags to and from other programs)
    // OFX_TEST_COUNT_DELAY_MS: each count takes that much longer, so a test can change the listing
    // while answers for the old one are on their way, or (60 s) stand in for a hung mount at quit.
    counts.delay_ms = env_int("OFX_TEST_COUNT_DELAY_MS", 0, 0, 60000);
    count_start();
    walk_start();
    search_start();
    thumbs_start(&scratch);
    git_start();
    space_start();
    timer_start();
    places_reload();
    trash_state();
    conf_apply();   // (F10: after thumbs_start, which its preview settings reach)
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
    app.ui.perms_cols = app.ui.colors = true, app.side_px = UI_SIDE_W;   // (on without a state file too)
    if (state_path(state_file, sizeof state_file)) {
        State st = state_load(&scratch, state_file);
        app.ui.side_mode = st.side, app.ui.perms_cols = !st.no_perms, app.ui.bar_hidden = st.no_bar,
        app.ui.colors = !st.no_colors, app.ui.group_col = st.group, app.show_hidden = st.hidden;
        if (st.side_px) app.side_px = st.side_px;
    }
    remember_defaults();   // (F11: the folders' table, after ofx.conf, which has their defaults)
    if (state_path(state_file, sizeof state_file)) {   // (F11: the tree's open folders)
        ArenaMark mark = arena_mark(&scratch);
        tree_open_load(&tree, state_value(&scratch, state_file, "tree"));
        arena_restore(&scratch, mark);
    }
    side_refresh();
    list_fit();
    ui_resize(&app.ui.side, side_rows(GetScreenHeight()));
    app.press.row = -1, app.panel_press.row = -1, app.edge_press.row = -1, app.spring_tab = -1;
}

// Each frame: what the watches and the worker threads brought, taken. True when the window must
// draw again.
static bool poll_workers(void)
{
    bool dirty = false;
    if (atomic_exchange(&watch.theme_ready, false)) theme_apply(), app.themes++, dirty = true;
    // The mounts before the folder: a drive unmounted and its folder removed come in one batch,
    // and the folder's going would move ofx to the folder above before the drive's going sends it
    // Home (2026-10-30).
    if (atomic_exchange(&watch.mounts_ready, false)) places_reload(), trash_state(), dirty = true;
    if (app.drive_gone) {
        const char *home = getenv("HOME");
        app.drive_gone = false;
        if (home && home[0]) go_to(home, STR_EMPTY, 0), dirty = true;
    }
    if (atomic_exchange(&watch.list_ready, false)) reload(), dirty = true;
    if (atomic_exchange(&watch.conf_ready, false)) conf_apply(), reload(), dirty = true;   // (the dates, a search's skips)
    if (atomic_exchange(&watch.trash_ready, false) && trash_state()) dirty = true;
    dirty |= jobs_poll();
    if (app.have_list && count_take(app.count, app.list.n)) dirty = true;
    if (space_take(&app.places, app.cwd, &app.cwd_avail)) dirty = true;
    if (eject_take()) dirty = true;
    if (props.walking && walk_take(&props.sum, &props.done)) dirty = true;
    if (props.walking && app.ui.panel != UI_PANEL_PROPS) walk_ask(NULL, 0), props.walking = false;   // the panel closed: the count stops
    if (clear_poll()) dirty = true;
    if (atomic_exchange(&watch.git_ready, false) && app.ui.view == UI_VIEW_FOLDER) git_ask_soon();   // (its index or HEAD changed: F12)
    git_poll();
    if (git_take()) dirty = true;
    if (branch_switched()) dirty = true;
    if (thumbs_take()) dirty = true;
    if (dnd_poll()) dirty = true;
    if (drag_out_poll()) dirty = true;   // (F14: our drag out, back over the window or ended)
    if (drag_in_poll()) dirty = true;    // (F14: another program's files over the window, or dropped)
    if (app.ui.view == UI_VIEW_SEARCH && search_take()) search_relist(), dirty = true;
    if (app.search_due && now_ms() >= app.search_due) app.search_due = 0, search_retype(), dirty = true;
    i64 was_due = restore.due, was_rem = rem.due;   // (F11: the tabs, a folder's view, written 2 s after a change)
    if ((restore_tick(now_ms()) != was_due && restore.due) | (remember_tick(now_ms()) != was_rem && rem.due)) timer_at(wake_due());
    return dirty;
}

// A right-click menu just opened (F8): what only gui/ knows of its target, then its place.
static void ctx_opened(void)
{
    bool ctx_up = app.ui.menu_on && app.ui.menu >= UI_MENUS;   // a right-click menu opened: where (F8)
    if (ctx_up && !app.ctx_placed && app.ui.menu == UI_CTX_FOLDER) ctx_facts();
    if (ctx_up && !app.ctx_placed && app.ui.menu == UI_CTX_ENTRY) {   // (Paste Into Folder: the cursor's, writable)
        char e[PATH_MAX];
        app.ui.ctx_w = app.ui.ctx_dir && app.ui.list.sel < app.list.n && entry_path(app.ui.list.sel, e, sizeof e) && access(e, W_OK) == 0;
    }
    if (ctx_up && !app.ctx_placed && app.ui.menu == UI_CTX_BRANCH) git_ask_branches(), menu_facts();   // (its list read as it opens: F12 step 5b)
    if (ctx_up && !app.ctx_placed) ctx_place();
    app.ctx_placed = ctx_up;
}

// The window's end: its tabs and the folder's view written, its registry entry gone, the threads
// told to quit and joined, and the window closed (wake_close).
static void app_end(void)
{
    if (atomic_load(&quit_signal)) closing_all = true;   // (SIGTERM, SIGINT: a logout or shutdown ends every window at once, so each keeps its tabs as Close All does; Michael, 2026-11-02)
    restore_save();   // (F11 step 3: before the cursor's write below, which tab_save's listing doesn't touch)
    if (fold_view_of(app.ui.view) && app.have_list) remember_cursor(app.cwd, sel_name());   // (F11)
    if (rem.dirty) remember_write();
    if (app.windows[0]) win_unregister(app.windows, getpid());
    quit_workers();
    ui_matcher_set(&app.ui.find_m, STR_EMPTY, app.ui.find_opts), ui_matcher_set(&app.ui.ow.m, STR_EMPTY, app.ui.ow_opts);   // (a compiled expression freed: LeakSanitizer)
    ui_matcher_set(&app.search_m, STR_EMPTY, app.ui.find_opts);
    ui_matcher_set(&app.search_im, STR_EMPTY, app.ui.inside_opts);
    timer_stop();
    for (int i = 0; i < app.nslots; i++)
        if (app.slot[i].tex.id) UnloadTexture(app.slot[i].tex);
    wake_close();
}

int main(int argc, char **argv)
{
    const char *start = NULL;
    bool selecting = false;
    long restore_id = 0;   // --restore <id>: another window's tabs after Close All (F11 step 3; ofx starts these itself)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) return usage(), 0;
        if (strcmp(argv[i], "--restore") == 0 && i + 1 < argc && !restore_id) {
            restore_id = strtol(argv[++i], NULL, 10);
            if (restore_id <= 0) return usage(), 2;
            continue;
        }
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
    // drive isn't kept busy by ofx (or by what it starts) when it is to be ejected (F6.1 review).
    if (chdir("/") != 0) fprintf(stderr, "ofx: chdir /: %s\n", strerror(errno));

    // Before any thread exists (GLFW and Mesa start some), so all of them inherit the mask.
    sigemptyset(&quit_signals);
    sigaddset(&quit_signals, SIGTERM);
    sigaddset(&quit_signals, SIGINT);
    sigaddset(&quit_signals, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &quit_signals, NULL);
    // SIGPIPE too, which nobody waits for: a program reading our clipboard or our drag that leaves
    // early would end the window at the next write; blocked, the write gets EPIPE (F14). Children
    // start with no signal blocked (fs/spawn.c, fs/sandbox.c, script.c's sh).
    sigset_t pipe_sig;
    sigemptyset(&pipe_sig);
    sigaddset(&pipe_sig, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &pipe_sig, NULL);
    spawn_init();
    test_knobs();

    arenas_init();
    uint32_t need = Clay_MinMemorySize();
    if ((size_t)need > sizeof clay_mem) {
        fprintf(stderr, "ofx: Clay needs %llu bytes, clay_mem has %zu\n", (unsigned long long)need, sizeof clay_mem);
        return 1;
    }

    if (!window_open(dir)) return 1;

    app_start();
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
    // F11 step 3: the tabs from the last time, the folder asked a tab more unless it is home.
    char home_real[PATH_MAX];
    const char *h = getenv("HOME");
    restore_start(restore_id, !selecting && !pick.len && (!start || (h && realpath(h, home_real) && strcmp(home_real, dir) == 0)));   // (a file asked: not plain)

    const char *script_path = getenv("OFX_GUI_SCRIPT");
    bool scripted = script_path && script_path[0];
    script_on = scripted;
    if (scripted && !script_open(script_path)) {
        quit_workers();
        wake_close();
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
        if (spring_tick() || (files_over() && app.spring_tab >= 0)) dirty = true;   // (its bar fills as it waits)
        if (files_over() && app.drop.reloads != app.reloads) drop_find(app.drag_px, app.drag_py), dirty = true;   // (the rows under a still pointer changed)
        if (dirty || scripted) {
            bool asked = dirty;   // (a frame an unscripted window draws too: what the script's checks read)
            i64 t0 = now_us();
            draw();
            i64 ta = now_us();
            if (scripted) script_after_draw();
            if (scripted && asked) script_drawn();
            i64 tb = now_us();
            SwapScreenBuffer();
            i64 us = now_us() - t0 - (tb - ta);
            if (us > frame_us_max) frame_us_max = us;
            dirty = font_frame_end();   // out of atlas: the next frame starts from an empty one
            // The frame may have moved things under the pointer: lit again, at once.
            Vector2 m = GetMousePosition();
            if (scripted && !script.user_pointer ? script.pointing && hover_refresh(true, script.px, script.py) : hover_refresh(IsCursorOnScreen(), m.x, m.y))
                dirty = true, glfwPostEmptyEvent();
            if (app.icons_missed && app.icons_reset_armed) dirty = true, glfwPostEmptyEvent();   // emptied and drawn again at once
            if (files_over() && app.spring_tab >= 0) glfwPostEmptyEvent();   // (the spring's bar: the next frame at once)
        }
        PollInputEvents();   // waits for input, or for watch.c's wake-up
        if (atomic_exchange(&close_signal, false)) close_asked = true, closing_all = true;
        dirty |= poll_workers();
        bool seen;
        UiInput in = input_real(&seen);
        if (scripted) {
            UiInput real = in;
            bool real_seen = seen;
            // The user's input does not reach a scripted window (Ctrl+Q still quits), but after
            // `user-keys` the keyboard's does (vkbd in the test sway, 2026-10-28), and after
            // `user-pointer` all of it (vptr: code-health review, 2026-11-06).
            if (!script.user_pointer) in = ui_input_none();
            if (script.user_keys && !script.user_pointer) {
                memcpy(in.ev, real.ev, sizeof *real.ev * (size_t)real.nev);
                in.nev = real.nev;
            }
            script.pending = dirty;
            if (!script_input(&in)) break;
            seen = script.gave || IsWindowResized() || (script.user_keys && real.nev > 0) || (script.user_pointer && real_seen);   // (a frame the script gave no input to is one an unscripted window sleeps through)
        }
        drag_keys(&in);   // (Esc during a drag of files: the drag's)
        cursor_shape();
        in.crumb_n = app.crumbs.n, in.crumb_last = app.crumb_hi - 1;
        _Static_assert((int)UI_JOB_ROWS >= (int)JOBQ_WORKERS, "a job row each for the jobs running");
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
        if (app.ui.panel == UI_PANEL_OW) in.app_name = ow.names, in.app_n = ow.shown < UI_APPS_MAX ? ow.shown : UI_APPS_MAX;
        if (app.ui.panel == UI_PANEL_BRANCHES || (app.ui.menu_on && app.ui.menu == UI_CTX_BRANCH)) in.br_name = brp_names(), in.br_names = git.br.n;   // (the menu's too: its All branches opens the panel this frame)
        UiAction a = ui_handle(&app.ui, (UiNames){ list_name, NULL, list_dir }, &in);
        ctx_opened();
        if (app.ui.panel != UI_PANEL_OW && ow.icons_on) ow_drop();   // the panel closed: its icons go (F7.3)
        if (app.ui.copied) text_copy((Str){ app.ui.copy, app.ui.copy_len }), app.ui.copied = false;
        // Shift+Delete and its y can come in one frame's input: the question opened and was answered
        // within ui_handle, so the targets are taken here either way (F5 confirm). A
        // new question always starts from no targets.
        if (!was_confirming && (app.ui.confirming || a.kind == UA_DELETE)) delete_ask();
        // F2 opened the edit line: on the cursor's entry, as it is named now.
        if (!was_editing && app.ui.focus == UI_FOCUS_EDIT && !app.ui.edit_path) edit_opened();
        if (a.kind != UA_NONE) act(a);
        // A Close All answered Stay (its question, with jobs) is no Close All any more: this window
        // closing later as one of several forgets its tabs (F11 review, 2026-11-02).
        if (closing_all && !close_asked && !app.ui.quit_asking && !app.quit) closing_all = false;
        if (!app.ui.empty_asking) app.empty_n = -1;
        else if (app.empty_n < 0) app.empty_n = trash_count();   // (once, as the question opens)
        // The search field changed (F9): it is searched for 150 ms after the last change.
        if (app.ui.search_edited) app.ui.search_edited = false, app.search_due = now_ms() + SEARCH_TYPE_MS, timer_at(wake_due());
        dirty |= seen;
    }
    if (scripted) script_end();
    app_end();
    return scripted && script.failed ? 1 : 0;
}
