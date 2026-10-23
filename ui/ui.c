// What the keys, characters, wheel and clicks do: the list's cursor and scroll, the sidebar's,
// find, sort, the history, the selection, the edit line and the prompts, and the one action a
// frame may ask of gui/ (open, go somewhere, toggle hidden files, sort again, change files).
// raylib-free and unit-tested; gui/main.c feeds it (docs/plan.md, F1 "Keys", F2 and F3).

#include "../base/base.h"

#include <limits.h>
#include <stdio.h>
#include <time.h>

enum { UI_EVENTS_MAX = 64, UI_FIND_MAX = 256, UI_HISTORY_MAX = 64, UI_JOB_ROWS = 4 };

// The keys without text, as gui/ reads them (a chord already named). Characters come apart.
typedef enum {
    UK_NONE,
    UK_UP, UK_DOWN, UK_PAGEUP, UK_PAGEDOWN, UK_HOME, UK_END,
    UK_ENTER, UK_RIGHT, UK_LEFT, UK_BACKSPACE, UK_ESC, UK_TAB,
    UK_BACK,       // Alt+Left
    UK_FORWARD,    // Alt+Right
    UK_FIND,       // Ctrl+F
    UK_HIDDEN,     // Ctrl+H
    UK_F2,         // rename
    UK_DELETE,     // to the trash; in the edit line, the character after the caret
    UK_SHIFT_DELETE,   // for good
    UK_ALL,        // Ctrl+A
    UK_COPY,       // Ctrl+C
    UK_CUT,        // Ctrl+X
    UK_PASTE,      // Ctrl+V
    UK_UNDO,       // Ctrl+Z
    UK_NEWFOLDER,  // Ctrl+Shift+N
    UK_LOCATION,   // Ctrl+L
    UK_PARENT,     // Alt+Up
    UK_TAB_NEW,    // Ctrl+T
    UK_TAB_CLOSE,  // Ctrl+W
    UK_TAB_NEXT,   // Ctrl+Tab, Ctrl+PgDn
    UK_TAB_PREV,   // Ctrl+Shift+Tab, Ctrl+PgUp
    UK_TAB_LEFT,   // Ctrl+Shift+PgUp: the tab moves
    UK_TAB_RIGHT,  // Ctrl+Shift+PgDn
    UK_TAB_JUMP,   // Alt+1 .. Alt+9: the event's ch is the digit
    UK_SEL_LEFT,   // Shift+Left: in an edit line the selection grows; elsewhere as Left
    UK_SEL_RIGHT,  // Shift+Right
    UK_SEL_HOME,   // Shift+Home
    UK_SEL_END,    // Shift+End
    UK_SHIFT_ENTER,   // on a path button: a background tab; elsewhere as Enter
    UK_CRUMBS,     // Ctrl+K: the keyboard on the path buttons (F5.2)
    UK_JOBS,       // Ctrl+J: the keyboard on the job rows' × (F5.3)
    UK_QUIT,       // Ctrl+Q, or the window closed
    UK_F9,         // the sidebar's next mode (F6)
} UiKey;

// The top bar's buttons (F4).
typedef enum { UB_NONE, UB_BACK, UB_FORWARD, UB_UP, UB_LOCATION, UB_SEARCH, UB_LEFT, UB_RIGHT, UB_CRUMB } UiBar;

enum { UI_MOD_CTRL = 1, UI_MOD_SHIFT = 2 };

// One key or one typed character, in the order they came: a find needs `/`, then text, then Enter.
typedef struct {
    UiKey key;   // UK_NONE: the character ch
    u32   ch;
} UiEvent;

typedef struct {
    UiEvent ev[UI_EVENTS_MAX];
    int     nev;
    int     wheel;       // notches over the list, positive away from the user (up)
    int     click;       // the list row pressed (0 = the first row on screen), -1 for none
    int     click_mods;  // UI_MOD_CTRL, UI_MOD_SHIFT held for that press
    int     dclick;      // a list row double-clicked, -1 for none
    int     side_click;  // a sidebar row pressed, -1 for none
    int     header;      // a column header clicked (a SortKey), -1 for none
    UiBar   bar;         // a top bar button clicked (UB_CRUMB: path button `crumb`)
    int     crumb;
    int     tab_click;   // a tab clicked (its place in the bar), -1 for none
    int     tab_close;   // a tab's × clicked, or the tab middle-clicked, -1 for none
    int     mid_row, mid_side, mid_crumb;   // a middle click on a list row, a place, a path button
    int     modal;       // a modal panel's button (F5.1): UI_MODAL_*, 0 for none
    int     backs, forwards;   // presses of the mouse's back and forward buttons
    Str     paste;       // the clipboard's text for a Ctrl+V in an edit line (gui/ reads it first)
    int     crumb_n;     // the path buttons there are, and the rightmost one shown (F5.2)
    int     crumb_last;
    // The job rows (F5.3): each running job's id, top row first; a row's × clicked (its place), -1
    // for none; and how many jobs there are in all, running and waiting.
    int     job_n;
    int     job_id[UI_JOB_ROWS];
    int     job_x;
    int     jobs_all;
    int     side_mode;   // a sidebar mode button clicked (a SideMode, F6), -1 for none
} UiInput;

typedef enum {
    UA_NONE, UA_OPEN, UA_PARENT, UA_TERMINAL, UA_HOMEDIR, UA_BACK, UA_FORWARD, UA_HIDDEN, UA_SORT, UA_PLACE,
    UA_RENAME,      // the edit line's Enter: its text is the new name (the line stays open until
                    // gui/ closes it, so a refused name can be fixed)
    UA_NEWFOLDER, UA_TRASH, UA_DELETE, UA_COPY, UA_CUT, UA_PASTE, UA_UNDO,
    UA_ANSWER,      // a clash answered: index is the Answer, all "the rest the same"
    UA_CANCEL_JOB,  // index: the job's id, asked and answered
    UA_QUIT,        // asked first when jobs run or wait
    UA_LOCATION,    // the location line, open or closed (gui/ fills it with the folder)
    UA_GO,          // the location line's Enter: its text is where to go (open until gui/ closes it)
    UA_CRUMB,       // a path button: index
    UA_CRUMB_SHIFT, // the path buttons' arrows: index -1 or 1
    UA_TAB_NEW,     // Ctrl+T
    UA_TAB_CLOSE,   // index: the tab's place, -1 the current one
    UA_TAB_SHOW,    // index: the tab's place
    UA_TAB_STEP,    // index: -1 or 1, wrapping
    UA_TAB_MOVE,    // index: -1 or 1, the current tab moves
    UA_TAB_JUMP,    // index: 1..9 (9 the last)
    UA_TAB_BG_ROW,  // a middle click: entry index in a new tab in the background
    UA_TAB_BG_PLACE,
    UA_TAB_BG_CRUMB,
    UA_SIDE_MODE,   // the sidebar's mode changed (Ui.side_mode): gui/ remembers it
} UiActionKind;

typedef struct {
    UiActionKind kind;
    isize        index;   // UA_OPEN: the entry; UA_PLACE: the place; UA_ANSWER: the answer
    bool         all;     // UA_ANSWER: for all the rest
} UiAction;

// The clash answers, as fs/job.c numbers them (Answer); ui/ doesn't include it.
enum { UI_ANS_SKIP = 1, UI_ANS_REPLACE = 2, UI_ANS_KEEP = 3, UI_ANS_CANCEL = 4 };

// The modal panels' buttons (F5.1): the delete question's, then the clash question's (the answers
// above, and the "same for the rest" box).
// The cancel, quit and queue-full panels (F5.3) use YES and NO, and OK.
enum { UI_MODAL_YES = 1, UI_MODAL_NO, UI_MODAL_SKIP, UI_MODAL_REPLACE, UI_MODAL_KEEP, UI_MODAL_CANCEL, UI_MODAL_ALL, UI_MODAL_OK };

typedef struct {
    isize n;      // rows in the list
    isize sel;    // the cursor, 0..n-1; 0 for an empty list
    isize top;    // the first row on screen
    int   rows;   // how many rows fit
} UiList;

// The entries' names, for find: gui/ hands its listing in through this.
typedef struct {
    Str (*at)(void *ctx, isize i);
    void *ctx;
} UiNames;

typedef struct {
    UiList list, side;
    SideMode side_mode;      // places, tree or hidden (F6)
    bool   side_focus;       // the keys move the sidebar's cursor
    const u8 *side_head;     // a byte per sidebar row, 1 for a heading (F6), never the cursor's; NULL: none
    bool   finding;          // the find line is open
    bool   find_miss;        // its text matches nothing
    char   find[UI_FIND_MAX];
    int    find_len;
    isize  find_from;        // the cursor when the find line opened
    char   last[UI_FIND_MAX];   // the last text found, for n and N
    int    last_len;
    Sort   sort;

    // The selection: gui/ gives one byte per entry of the listing (in the listing's arena), and
    // keeps it by name across a reload.
    u8    *mark;
    isize  marked;           // how many are set
    // The edit line: a rename (a new folder is made at once and renamed with it), or the top bar's
    // location line (F4), which holds a path.
    bool   editing;
    bool   edit_path;        // the location line
    char   edit[PATH_MAX];
    int    edit_len, caret;  // bytes
    int    anchor;           // the selection's other end (F5.2): selected between it and the caret
    // A Ctrl+C or Ctrl+X in the edit line: the text gui/ puts on the clipboard, then clears copied.
    bool   copied;
    char   copy[PATH_MAX];
    int    copy_len;
    // The keyboard on the path buttons (Ctrl+K, F5.2): the one marked.
    bool   crumb_focus;
    int    crumb_at;
    // The prompts and the job: gui/ says when one runs, and when it asks about a clash.
    bool   confirming;       // "Delete … for good? Y/n"
    isize  confirm_n;
    bool   asking;           // a job waits for a clash answer
    bool   ask_all;          // its "same for the rest" box (the capitals too)
    // The job rows (F5.3): the keyboard on their × (Ctrl+J), the row marked; "Cancel this job?"
    // about job cancel_id; "Quit and cancel them?"; "No more jobs can be queued".
    bool   jobs_focus;
    int    job_at;
    bool   cancel_asking;
    int    cancel_id;
    bool   quit_asking;
    bool   full_told;
} Ui;

static UiInput ui_input_none(void)
{
    return (UiInput){ .click = -1, .dclick = -1, .side_click = -1, .header = -1, .click_mods = 0, .bar = UB_NONE, .crumb = -1,
                      .tab_click = -1, .tab_close = -1, .mid_row = -1, .mid_side = -1, .mid_crumb = -1, .modal = 0, .job_x = -1, .side_mode = -1 };
}

static isize ui_clamp(isize v, isize lo, isize hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static isize ui_max_top(const UiList *l)
{
    isize rows = l->rows > 0 ? l->rows : 1;
    return l->n > rows ? l->n - rows : 0;
}

// The cursor on screen: the scroll moves as little as it must.
static void ui_show_sel(UiList *l)
{
    isize rows = l->rows > 0 ? l->rows : 1;
    if (l->sel < l->top) l->top = l->sel;
    if (l->sel >= l->top + rows) l->top = l->sel - rows + 1;
    l->top = ui_clamp(l->top, 0, ui_max_top(l));
}

// A new list (a folder entered, or the same one reloaded) of n entries, the cursor on sel. The
// scroll keeps its place where it can, so a reload doesn't jump.
static void ui_set(UiList *l, isize n, isize sel)
{
    l->n = n;
    l->sel = n > 0 ? ui_clamp(sel, 0, n - 1) : 0;
    l->top = ui_clamp(l->top, 0, ui_max_top(l));
    ui_show_sel(l);
}

// A new window height: rows fit.
static void ui_resize(UiList *l, int rows)
{
    l->rows = rows;
    l->top = ui_clamp(l->top, 0, ui_max_top(l));
    ui_show_sel(l);
}

static void ui_move(UiList *l, isize by)
{
    if (l->n == 0) return;
    l->sel = ui_clamp(l->sel + by, 0, l->n - 1);
    ui_show_sel(l);
}

static bool ui_side_head(const Ui *u, isize i)
{
    return u->side_head && i >= 0 && i < u->side.n && u->side_head[i];
}

// The sidebar's cursor off a heading: on the way it moved (dir 1 down, -1 up), else back the
// other way; a heading right above it kept in view.
static void ui_side_settle(Ui *u, int dir)
{
    UiList *s = &u->side;
    isize i = s->sel;
    while (ui_side_head(u, i)) i += dir;
    if (i < 0 || i >= s->n)
        for (i = s->sel; ui_side_head(u, i);) i -= dir;
    if (i >= 0 && i < s->n) s->sel = i;
    ui_show_sel(s);
    if (s->top == s->sel && ui_side_head(u, s->sel - 1)) s->top--;
}

// Whether in holds something the user did: a key, a character, a press of a button (not the
// wheel, nor the pointer moving). A status message gives way to the status line then (F6).
static bool ui_input_acts(const UiInput *in)
{
    return in->nev > 0 || in->click >= 0 || in->dclick >= 0 || in->side_click >= 0 || in->header >= 0 || in->bar != UB_NONE ||
           in->tab_click >= 0 || in->tab_close >= 0 || in->mid_row >= 0 || in->mid_side >= 0 || in->mid_crumb >= 0 || in->modal > 0 ||
           in->backs > 0 || in->forwards > 0 || in->job_x >= 0 || in->side_mode >= 0;
}

// The sidebar in mode m: a hidden one gives the keys back to the list.
static UiAction ui_side_mode(Ui *u, SideMode m)
{
    u->side_mode = m;
    if (m == SIDE_HIDDEN) u->side_focus = false;
    return (UiAction){ UA_SIDE_MODE, m, false };
}

// A move of the list's cursor or the sidebar's.
static void ui_step(Ui *u, UiList *m, isize by)
{
    ui_move(m, by);
    if (m == &u->side) ui_side_settle(u, by < 0 ? -1 : 1);
}

// ---- find -------------------------------------------------------------------------------------

// Whether needle is in hay, ASCII letters folded.
static bool ui_contains(Str hay, Str needle)
{
    for (isize i = 0; i + needle.len <= hay.len; i++) {
        isize k = 0;
        while (k < needle.len && fold((unsigned char)hay.p[i + k]) == fold((unsigned char)needle.p[k])) k++;
        if (k == needle.len) return true;
    }
    return false;
}

// The first entry from `from` on (dir 1) or back (dir -1), wrapping, whose name holds text; -1.
static isize ui_find_at(const UiList *l, UiNames names, Str text, isize from, int dir)
{
    if (l->n == 0 || text.len == 0) return -1;
    for (isize k = 0; k < l->n; k++) {
        isize i = ((from + dir * k) % l->n + l->n) % l->n;
        if (ui_contains(names.at(names.ctx, i), text)) return i;
    }
    return -1;
}

// The find line's text changed: the cursor goes to its first match from where the find began,
// or back there when the text is empty.
static void ui_find_update(Ui *u, UiNames names)
{
    Str text = { u->find, u->find_len };
    isize i = ui_find_at(&u->list, names, text, u->find_from, 1);
    u->find_miss = text.len > 0 && i < 0;
    u->list.sel = i >= 0 ? i : u->find_from;
    ui_show_sel(&u->list);
}

static void ui_find_close(Ui *u)
{
    u->finding = false, u->find_miss = false;
    if (u->find_len) memcpy(u->last, u->find, (size_t)u->find_len), u->last_len = u->find_len;
}

static void ui_find_open(Ui *u)
{
    u->finding = true, u->find_miss = false, u->find_len = 0, u->find_from = u->list.sel, u->side_focus = false;
}

// cp as UTF-8 into b: its length, or 0 for a control character or no code point at all.
static int ui_utf8(u32 cp, char b[4])
{
    if (cp < 0x20 || cp == 0x7F || cp > 0x10FFFF) return 0;
    return cp < 0x80 ? (b[0] = (char)cp, 1)
         : cp < 0x800 ? (b[0] = (char)(0xC0 | cp >> 6), b[1] = (char)(0x80 | (cp & 0x3F)), 2)
         : cp < 0x10000 ? (b[0] = (char)(0xE0 | cp >> 12), b[1] = (char)(0x80 | (cp >> 6 & 0x3F)), b[2] = (char)(0x80 | (cp & 0x3F)), 3)
         : (b[0] = (char)(0xF0 | cp >> 18), b[1] = (char)(0x80 | (cp >> 12 & 0x3F)), b[2] = (char)(0x80 | (cp >> 6 & 0x3F)), b[3] = (char)(0x80 | (cp & 0x3F)), 4);
}

// A character typed into the find line, as UTF-8; it is dropped when the line is full.
static void ui_find_char(Ui *u, u32 cp)
{
    char b[4];
    int n = ui_utf8(cp, b);
    if (n == 0 || u->find_len + n > UI_FIND_MAX) return;
    memcpy(u->find + u->find_len, b, (size_t)n);
    u->find_len += n;
}

// Backspace in the find line: its last code point goes.
static void ui_find_erase(Ui *u)
{
    if (u->find_len == 0) return;
    u->find_len--;
    while (u->find_len > 0 && ((unsigned char)u->find[u->find_len] & 0xC0) == 0x80) u->find_len--;
}

// n and N: the next or previous match of the last text, from the cursor.
static void ui_find_again(Ui *u, UiNames names, int dir)
{
    isize i = ui_find_at(&u->list, names, (Str){ u->last, u->last_len }, u->list.sel + dir, dir);
    if (i >= 0) u->list.sel = i, ui_show_sel(&u->list);
}

// ---- the selection ----------------------------------------------------------------------------

static bool ui_marked(const Ui *u, isize i)
{
    return u->mark && i >= 0 && i < u->list.n && u->mark[i];
}

static void ui_mark(Ui *u, isize i, bool on)
{
    if (!u->mark || i < 0 || i >= u->list.n || (bool)u->mark[i] == on) return;
    u->mark[i] = on;
    u->marked += on ? 1 : -1;
}

static void ui_mark_clear(Ui *u)
{
    for (isize i = 0; u->mark && i < u->list.n; i++) u->mark[i] = 0;
    u->marked = 0;
}

// A new listing's selection: mark (n bytes, all clear) is the array, marked recounted by gui/
// as it sets the names it kept.
static void ui_mark_set(Ui *u, u8 *mark)
{
    u->mark = mark, u->marked = 0;
}

// Rows from..to marked (either way round).
static void ui_mark_range(Ui *u, isize from, isize to)
{
    if (from > to) {
        isize t = from;
        from = to, to = t;
    }
    for (isize i = from; i <= to; i++) ui_mark(u, i, true);
}

// ---- the edit line -----------------------------------------------------------------------------

// How many bytes the edit line holds: a name, or a path.
static int ui_edit_max(const Ui *u)
{
    return u->edit_path ? PATH_MAX - 1 : NAME_MAX;
}

static void ui_edit_start(Ui *u, Str text, bool path)
{
    u->edit_path = path;
    isize max = ui_edit_max(u), n = text.len < max ? text.len : max;
    memcpy(u->edit, text.p, (size_t)n);
    u->edit_len = (int)n, u->caret = (int)n, u->anchor = path ? 0 : (int)n;
    u->editing = true, u->finding = false, u->side_focus = false, u->crumb_focus = false, u->jobs_focus = false;
}

// The edit line opened on a name, the caret at its end.
static void ui_edit_open(Ui *u, Str text)
{
    ui_edit_start(u, text, false);
}

// The location line opened on a path, all of it selected, as browsers and Nemo do: typing a new one
// replaces it, a move of the caret ends the selection.
static void ui_location_open(Ui *u, Str path)
{
    ui_edit_start(u, path, true);
}

static void ui_edit_close(Ui *u)
{
    u->editing = false;
}

// The selection, [*lo, *hi): empty when the anchor is at the caret.
static void ui_edit_sel(const Ui *u, int *lo, int *hi)
{
    *lo = u->anchor < u->caret ? u->anchor : u->caret;
    *hi = u->anchor < u->caret ? u->caret : u->anchor;
}

static int ui_edit_back(const Ui *u, int at)
{
    if (at <= 0) return 0;
    at--;
    while (at > 0 && ((unsigned char)u->edit[at] & 0xC0) == 0x80) at--;
    return at;
}

static int ui_edit_fwd(const Ui *u, int at)
{
    if (at >= u->edit_len) return u->edit_len;
    at++;
    while (at < u->edit_len && ((unsigned char)u->edit[at] & 0xC0) == 0x80) at++;
    return at;
}

// Bytes [from, to) of the edit line removed, the caret where they were.
static void ui_edit_cut(Ui *u, int from, int to)
{
    memmove(u->edit + from, u->edit + to, (size_t)(u->edit_len - to));
    u->edit_len -= to - from;
    u->caret = u->anchor = from;
}

// Bytes b[0..n) in place of the selection, the caret after them; false (nothing changed) when they
// don't fit.
static bool ui_edit_put(Ui *u, const char *b, int n)
{
    int lo, hi;
    ui_edit_sel(u, &lo, &hi);
    if (u->edit_len - (hi - lo) + n > ui_edit_max(u)) return false;
    ui_edit_cut(u, lo, hi);
    memmove(u->edit + u->caret + n, u->edit + u->caret, (size_t)(u->edit_len - u->caret));
    memcpy(u->edit + u->caret, b, (size_t)n);
    u->edit_len += n, u->caret += n, u->anchor = u->caret;
    return true;
}

// The selection onto u->copy for gui/'s clipboard; nothing selected: nothing.
static bool ui_edit_copy(Ui *u)
{
    int lo, hi;
    ui_edit_sel(u, &lo, &hi);
    if (lo == hi) return false;
    memcpy(u->copy, u->edit + lo, (size_t)(hi - lo));
    u->copy_len = hi - lo, u->copied = true;
    return true;
}

// Ctrl+V: the clipboard's first line over the selection; in a name line without the characters a
// name can't hold ('/'), and never a control character. What doesn't fit is left off, whole
// characters only.
static void ui_edit_paste(Ui *u, Str text)
{
    char b[PATH_MAX];
    int n = 0, lo, hi;
    ui_edit_sel(u, &lo, &hi);
    int room = ui_edit_max(u) - (u->edit_len - (hi - lo));
    for (isize i = 0; i < text.len && text.p[i] != '\n' && text.p[i] != '\r';) {
        unsigned char c = (unsigned char)text.p[i];
        isize e = i + 1;
        while (e < text.len && ((unsigned char)text.p[e] & 0xC0) == 0x80) e++;
        bool drop = c < 0x20 || c == 0x7F || (c == '/' && !u->edit_path);
        if (n + (e - i) > room) break;   // a dropped one is a byte: none fits after it either
        if (!drop) memcpy(b + n, text.p + i, (size_t)(e - i)), n += (int)(e - i);
        i = e;
    }
    if (n > 0) ui_edit_put(u, b, n);
}

// A key or character in the edit line: an action for gui/ (UA_RENAME or UA_GO on Enter) or UA_NONE.
static UiAction ui_edit_event(Ui *u, UiEvent e, Str paste)
{
    if (e.key == UK_NONE) {
        char b[4];
        int n = ui_utf8(e.ch, b);
        if (n > 0 && (e.ch != '/' || u->edit_path)) ui_edit_put(u, b, n);
        return (UiAction){ UA_NONE, 0, false };
    }
    int lo, hi;
    ui_edit_sel(u, &lo, &hi);
    switch (e.key) {
    // A move ends the selection; Left and Right from one land at its ends.
    case UK_LEFT: u->caret = u->anchor = lo < hi ? lo : ui_edit_back(u, u->caret); break;
    case UK_RIGHT: u->caret = u->anchor = lo < hi ? hi : ui_edit_fwd(u, u->caret); break;
    case UK_HOME: u->caret = u->anchor = 0; break;
    case UK_END: u->caret = u->anchor = u->edit_len; break;
    // With Shift the caret moves and the anchor stays.
    case UK_SEL_LEFT: u->caret = ui_edit_back(u, u->caret); break;
    case UK_SEL_RIGHT: u->caret = ui_edit_fwd(u, u->caret); break;
    case UK_SEL_HOME: u->caret = 0; break;
    case UK_SEL_END: u->caret = u->edit_len; break;
    case UK_ALL: u->anchor = 0, u->caret = u->edit_len; break;
    case UK_BACKSPACE: ui_edit_cut(u, lo < hi ? lo : ui_edit_back(u, u->caret), lo < hi ? hi : u->caret); break;
    case UK_DELETE: ui_edit_cut(u, lo < hi ? lo : u->caret, lo < hi ? hi : ui_edit_fwd(u, u->caret)); break;
    case UK_COPY: ui_edit_copy(u); break;
    case UK_CUT:
        if (ui_edit_copy(u)) ui_edit_cut(u, lo, hi);
        break;
    case UK_PASTE: ui_edit_paste(u, paste); break;
    case UK_ENTER:
    case UK_SHIFT_ENTER: return (UiAction){ u->edit_path ? UA_GO : UA_RENAME, 0, false };
    case UK_ESC: ui_edit_close(u); break;
    case UK_LOCATION:   // Ctrl+L again: the location line closes; a rename doesn't take it
        if (u->edit_path) ui_edit_close(u);
        break;
    default: break;
    }
    return (UiAction){ UA_NONE, 0, false };
}

// ---- the path buttons by keyboard (F5.2) -------------------------------------------------------

// Ctrl+K: the keyboard on the path buttons, the rightmost shown marked (none there: nothing).
static void ui_crumbs_focus(Ui *u, const UiInput *in)
{
    if (in->crumb_n <= 0) return;
    u->crumb_focus = true, u->side_focus = false;   // the job rows' keyboard went with the key
    u->crumb_at = in->crumb_last < 0 ? 0 : in->crumb_last >= in->crumb_n ? in->crumb_n - 1 : in->crumb_last;
}

// A key while the path buttons have the keyboard (h and l as Left and Right): true when it was
// theirs (*a: what gui/ does).
// Any other key gives the keyboard back to the list and is the list's.
static bool ui_crumbs_event(Ui *u, const UiInput *in, UiEvent e, UiAction *a)
{
    *a = (UiAction){ UA_NONE, 0, false };
    if (e.key == UK_NONE && e.ch == 'h') e.key = UK_LEFT;    // as in the list (Michael, 2026-10-21)
    if (e.key == UK_NONE && e.ch == 'l') e.key = UK_RIGHT;
    switch (e.key) {
    case UK_LEFT: case UK_SEL_LEFT: if (u->crumb_at > 0) u->crumb_at--; return true;
    case UK_RIGHT: case UK_SEL_RIGHT: if (u->crumb_at < in->crumb_n - 1) u->crumb_at++; return true;
    case UK_HOME: case UK_SEL_HOME: u->crumb_at = 0; return true;
    case UK_END: case UK_SEL_END: u->crumb_at = in->crumb_n - 1; return true;
    case UK_ENTER: u->crumb_focus = false, *a = (UiAction){ UA_CRUMB, u->crumb_at, false }; return true;
    case UK_SHIFT_ENTER: *a = (UiAction){ UA_TAB_BG_CRUMB, u->crumb_at, false }; return true;
    case UK_CRUMBS: return true;
    case UK_ESC: case UK_TAB: u->crumb_focus = false; return true;
    default: u->crumb_focus = false; return false;
    }
}

// ---- the prompts -------------------------------------------------------------------------------

// A yes-or-no question (delete for good, cancel a job, quit with jobs): Enter and y say yes, n and Esc no; *answered
// when either.
static bool ui_yes_no(UiEvent e, bool *answered)
{
    bool yes = e.key == UK_ENTER || (e.key == UK_NONE && (e.ch == 'y' || e.ch == 'Y'));
    bool no = e.key == UK_ESC || (e.key == UK_NONE && (e.ch == 'n' || e.ch == 'N'));
    *answered = yes || no;
    return yes;
}

// A key or character while "Delete … for good? Y/n" is up: Enter and y go ahead, n and Esc don't,
// the rest is ignored.
static UiAction ui_confirm_event(Ui *u, UiEvent e)
{
    bool answered, yes = ui_yes_no(e, &answered);
    if (answered) u->confirming = false;
    return (UiAction){ yes ? UA_DELETE : UA_NONE, 0, false };
}

// A key or character while a clash waits: s skip, r replace (a folder: merge), k keep both, the
// capitals for all the rest; Esc cancels the job.
static UiAction ui_ask_event(Ui *u, UiEvent e)
{
    if (e.key == UK_ESC) return u->asking = false, (UiAction){ UA_ANSWER, UI_ANS_CANCEL, false };
    if (e.key != UK_NONE) return (UiAction){ UA_NONE, 0, false };
    int a = 0;
    switch (e.ch) {
    case 's': case 'S': a = UI_ANS_SKIP; break;
    case 'r': case 'R': a = UI_ANS_REPLACE; break;
    case 'k': case 'K': a = UI_ANS_KEEP; break;
    default: return (UiAction){ UA_NONE, 0, false };
    }
    u->asking = false;
    return (UiAction){ UA_ANSWER, a, (e.ch >= 'A' && e.ch <= 'Z') || u->ask_all };
}

// The job rows by keyboard: true when the key was theirs (*a: what gui/ does). Up, Down, k and j
// move the mark, Enter asks to cancel the marked job; any other key gives the keyboard back to the
// list and is the list's.
static bool ui_jobs_event(Ui *u, const UiInput *in, UiEvent e)
{
    if (e.key == UK_NONE && e.ch == 'k') e.key = UK_UP;
    if (e.key == UK_NONE && e.ch == 'j') e.key = UK_DOWN;
    switch (e.key) {
    case UK_UP: if (u->job_at > 0) u->job_at--; return true;
    case UK_DOWN: if (u->job_at < in->job_n - 1) u->job_at++; return true;
    case UK_ENTER: u->cancel_asking = true, u->cancel_id = in->job_id[u->job_at]; return true;
    case UK_JOBS: return true;
    case UK_ESC: case UK_TAB: u->jobs_focus = false; return true;
    default: u->jobs_focus = false; return false;
    }
}

// ---- input ------------------------------------------------------------------------------------

// The next sort for a header click (or `s`'s next column): the same column again reverses.
static Sort ui_sort_click(Sort s, SortKey k)
{
    return s.key == k ? (Sort){ k, !s.reverse } : (Sort){ k, false };
}

// A key or character while the find line is open: true when it was the line's (the rest of the
// event is then done).
static bool ui_find_event(Ui *u, UiNames names, UiEvent e)
{
    if (e.key == UK_NONE) {
        if (e.ch == '/') return true;   // can't be in a name
        ui_find_char(u, e.ch);
        ui_find_update(u, names);
        return true;
    }
    switch (e.key) {
    case UK_BACKSPACE: ui_find_erase(u), ui_find_update(u, names); return true;
    case UK_ENTER: ui_find_close(u); return true;
    case UK_ESC:
        ui_find_close(u);
        u->list.sel = u->find_from, ui_show_sel(&u->list);
        return true;
    case UK_FIND: return true;
    default: ui_find_close(u); return false;   // a move or a command: the line closes, the cursor stays
    }
}

// One frame's input. Clicks first, then the events in order up to the first that asks gui/ for
// something (after it, the list may be another folder's). The wheel scrolls the list without
// moving its cursor, one row a notch (Michael's choice for prdash-tui). A prompt (a clash, the
// delete confirm) takes every key until it is answered; the edit line takes the keys that edit.
static UiAction ui_handle(Ui *u, UiNames names, const UiInput *in)
{
    UiList *l = &u->list;
    // The panels F5.3 adds: the queue full (any key or OK), quit, cancel a job.
    if (u->full_told && (in->modal == UI_MODAL_OK || in->nev > 0)) {
        u->full_told = false;
        return (UiAction){ UA_NONE, 0, false };
    }
    if (u->quit_asking && (in->modal == UI_MODAL_YES || in->modal == UI_MODAL_NO)) {
        u->quit_asking = false;
        return (UiAction){ in->modal == UI_MODAL_YES ? UA_QUIT : UA_NONE, 0, false };
    }
    if (u->cancel_asking && (in->modal == UI_MODAL_YES || in->modal == UI_MODAL_NO)) {
        u->cancel_asking = false;
        return (UiAction){ in->modal == UI_MODAL_YES ? UA_CANCEL_JOB : UA_NONE, u->cancel_id, false };
    }
    bool prompt = u->asking || u->confirming || u->cancel_asking || u->quit_asking || u->full_told;
    // The job rows: the mark kept on a row there is; none left, no keyboard there.
    if (u->job_at >= in->job_n) u->job_at = in->job_n - 1;
    if (u->job_at < 0) u->job_at = 0;
    if (in->job_n <= 0) u->jobs_focus = false;
    if (!prompt && in->job_x >= 0 && in->job_x < in->job_n) {
        u->cancel_asking = true, u->cancel_id = in->job_id[in->job_x];
        return (UiAction){ UA_NONE, 0, false };
    }
    // A click anywhere gives the path buttons' keyboard back; so does their going.
    bool clicked = in->click >= 0 || in->dclick >= 0 || in->side_click >= 0 || in->header >= 0 || in->bar != UB_NONE ||
                   in->tab_click >= 0 || in->tab_close >= 0 || in->mid_row >= 0 || in->mid_side >= 0 || in->mid_crumb >= 0;
    if (clicked || in->crumb_n <= 0) u->crumb_focus = false;
    if (clicked) u->jobs_focus = false;
    if (u->crumb_focus && u->crumb_at >= in->crumb_n) u->crumb_at = in->crumb_n - 1;
    // A modal panel's buttons: as the keys they stand for.
    if (u->confirming && (in->modal == UI_MODAL_YES || in->modal == UI_MODAL_NO)) {
        u->confirming = false;
        return (UiAction){ in->modal == UI_MODAL_YES ? UA_DELETE : UA_NONE, 0, false };
    }
    if (u->asking && in->modal == UI_MODAL_ALL) u->ask_all = !u->ask_all;
    if (u->asking && in->modal >= UI_MODAL_SKIP && in->modal <= UI_MODAL_CANCEL) {
        static const int ANSWER[] = { UI_ANS_SKIP, UI_ANS_REPLACE, UI_ANS_KEEP, UI_ANS_CANCEL };
        int a = ANSWER[in->modal - UI_MODAL_SKIP];
        u->asking = false;
        return (UiAction){ UA_ANSWER, a, a != UI_ANS_CANCEL && u->ask_all };
    }
    if (in->wheel) l->top = ui_clamp(l->top - in->wheel, 0, ui_max_top(l));
    if (!prompt && in->header >= 0 && in->header < SORT_KEYS) {
        u->sort = ui_sort_click(u->sort, (SortKey)in->header);
        return (UiAction){ UA_SORT, 0, false };
    }
    if (!prompt && in->side_mode >= 0 && in->side_mode < SIDE_MODES) return ui_side_mode(u, (SideMode)in->side_mode);
    if (!prompt && in->side_click >= 0 && ui_side_head(u, u->side.top + in->side_click)) return (UiAction){ UA_NONE, 0, false };
    if (!prompt && in->side_click >= 0 && u->side.top + in->side_click < u->side.n) {
        u->side.sel = u->side.top + in->side_click;
        ui_edit_close(u);
        return (UiAction){ UA_PLACE, u->side.sel, false };
    }
    if (!prompt && in->tab_close >= 0) return ui_edit_close(u), (UiAction){ UA_TAB_CLOSE, in->tab_close, false };
    if (!prompt && in->tab_click >= 0) return ui_edit_close(u), (UiAction){ UA_TAB_SHOW, in->tab_click, false };
    if (!prompt && in->mid_row >= 0 && l->top + in->mid_row < l->n) return (UiAction){ UA_TAB_BG_ROW, l->top + in->mid_row, false };
    if (!prompt && in->mid_side >= 0 && u->side.top + in->mid_side < u->side.n && !ui_side_head(u, u->side.top + in->mid_side)) return (UiAction){ UA_TAB_BG_PLACE, u->side.top + in->mid_side, false };
    if (!prompt && in->mid_crumb >= 0) return (UiAction){ UA_TAB_BG_CRUMB, in->mid_crumb, false };
    if (!prompt && in->bar != UB_NONE) {
        switch (in->bar) {
        case UB_BACK: ui_edit_close(u); return (UiAction){ UA_BACK, 0, false };
        case UB_FORWARD: ui_edit_close(u); return (UiAction){ UA_FORWARD, 0, false };
        case UB_UP: ui_edit_close(u); return (UiAction){ UA_PARENT, 0, false };
        case UB_LOCATION:
            if (u->editing && u->edit_path) return ui_edit_close(u), (UiAction){ UA_NONE, 0, false };
            return (UiAction){ UA_LOCATION, 0, false };
        case UB_SEARCH: ui_edit_close(u), ui_find_open(u); return (UiAction){ UA_NONE, 0, false };
        case UB_LEFT: return (UiAction){ UA_CRUMB_SHIFT, -1, false };
        case UB_RIGHT: return (UiAction){ UA_CRUMB_SHIFT, 1, false };
        case UB_CRUMB: ui_edit_close(u); return (UiAction){ UA_CRUMB, in->crumb, false };
        default: break;
        }
    }
    if (!prompt && in->click >= 0 && l->top + in->click < l->n) {
        isize row = l->top + in->click;
        ui_edit_close(u);
        if (in->click_mods & UI_MOD_SHIFT) ui_mark_range(u, l->sel, row);
        else if (in->click_mods & UI_MOD_CTRL) ui_mark(u, row, !ui_marked(u, row));
        l->sel = row, u->side_focus = false;
        ui_show_sel(l);
    }
    if (!prompt && in->dclick >= 0 && l->top + in->dclick < l->n) {
        l->sel = l->top + in->dclick, u->side_focus = false;
        ui_edit_close(u);
        return (UiAction){ UA_OPEN, l->sel, false };
    }
    if (!prompt && in->backs > 0) return (UiAction){ UA_BACK, 0, false };
    if (!prompt && in->forwards > 0) return (UiAction){ UA_FORWARD, 0, false };
    for (int k = 0; k < in->nev; k++) {
        UiEvent e = in->ev[k];
        // Ctrl+Q: at once, or asked first when jobs run or wait; under any panel but these.
        if (e.key == UK_QUIT && !u->quit_asking) {   // (a full queue's note took the frame's keys)
            if (in->jobs_all <= 0) return (UiAction){ UA_QUIT, 0, false };
            u->quit_asking = true, u->cancel_asking = false;
            continue;
        }
        if (u->quit_asking || u->cancel_asking) {
            bool answered, yes = ui_yes_no(e, &answered);
            if (!answered) continue;
            bool quit = u->quit_asking;
            u->quit_asking = u->cancel_asking = false;
            if (yes) return (UiAction){ quit ? UA_QUIT : UA_CANCEL_JOB, quit ? 0 : u->cancel_id, false };
            continue;
        }
        if (u->editing && u->edit_path && !prompt && e.key == UK_CRUMBS) {   // from the location line: it closes
            ui_edit_close(u), ui_crumbs_focus(u, in);
            continue;
        }
        if (u->editing && !prompt) {
            UiAction a = ui_edit_event(u, e, in->paste);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        if (u->crumb_focus && !prompt) {
            UiAction a;
            if (ui_crumbs_event(u, in, e, &a)) {
                if (a.kind != UA_NONE) return a;
                continue;
            }
        }
        if (u->jobs_focus && !prompt && ui_jobs_event(u, in, e)) continue;   // before a clash's keys
        // Outside an edit line and the path buttons, Shift doesn't change a move or Enter.
        switch (e.key) {
        case UK_SEL_LEFT: e.key = UK_LEFT; break;
        case UK_SEL_RIGHT: e.key = UK_RIGHT; break;
        case UK_SEL_HOME: e.key = UK_HOME; break;
        case UK_SEL_END: e.key = UK_END; break;
        case UK_SHIFT_ENTER: e.key = UK_ENTER; break;
        default: break;
        }
        if (u->asking) {
            UiAction a = ui_ask_event(u, e);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        if (u->confirming) {
            UiAction a = ui_confirm_event(u, e);
            if (a.kind != UA_NONE) return a;
            continue;
        }
        if (u->finding && ui_find_event(u, names, e)) continue;
        // The keys and letters that move a cursor: the sidebar's when it has the keyboard.
        UiList *m = u->side_focus ? &u->side : l;
        isize page = m->rows > 1 ? m->rows - 1 : 1;
        UiKey key = e.key;
        if (key == UK_NONE) {
            switch (e.ch) {
            case 'j': key = UK_DOWN; break;
            case 'k': key = UK_UP; break;
            case 'l': key = UK_RIGHT; break;
            case 'h': key = UK_LEFT; break;
            case 'g': key = UK_HOME; break;
            case 'G': key = UK_END; break;
            case '/': key = UK_FIND; break;
            case '.': key = UK_HIDDEN; break;
            case 't': return (UiAction){ UA_TERMINAL, 0, false };
            case '~': return (UiAction){ UA_HOMEDIR, 0, false };
            case 's': u->sort = (Sort){ (SortKey)((u->sort.key + 1) % SORT_KEYS), false }; return (UiAction){ UA_SORT, 0, false };
            case 'S': u->sort.reverse = !u->sort.reverse; return (UiAction){ UA_SORT, 0, false };
            case 'n': ui_find_again(u, names, 1); continue;
            case 'N': ui_find_again(u, names, -1); continue;
            case ' ':
                // Space marks the entry under the cursor, or unmarks it, and moves on.
                if (!u->side_focus && l->n > 0) ui_mark(u, l->sel, !ui_marked(u, l->sel)), ui_move(l, 1);
                continue;
            default: continue;
            }
        }
        switch (key) {
        case UK_UP: ui_step(u, m, -1); break;
        case UK_DOWN: ui_step(u, m, 1); break;
        case UK_PAGEUP: ui_step(u, m, -page); break;
        case UK_PAGEDOWN: ui_step(u, m, page); break;
        case UK_HOME: ui_step(u, m, -m->n); break;
        case UK_END: ui_step(u, m, m->n); break;
        case UK_ENTER:
        case UK_RIGHT:
            if (u->side_focus && u->side.n > 0) return (UiAction){ UA_PLACE, u->side.sel, false };
            if (!u->side_focus && l->n > 0) return (UiAction){ UA_OPEN, l->sel, false };
            break;
        case UK_LEFT:
        case UK_BACKSPACE: return (UiAction){ UA_PARENT, 0, false };
        case UK_F9: return ui_side_mode(u, (SideMode)((u->side_mode + 1) % SIDE_MODES));
        case UK_TAB:
            u->side_focus = !u->side_focus && u->side.n > 0 && u->side_mode != SIDE_HIDDEN;
            if (u->side_focus) ui_side_settle(u, 1);
            break;
        case UK_ESC:
            // The selection, then the sidebar's keyboard (a job is cancelled from its row, F5.3).
            if (u->marked) ui_mark_clear(u);
            else u->side_focus = false;
            break;
        case UK_BACK: return (UiAction){ UA_BACK, 0, false };
        case UK_FORWARD: return (UiAction){ UA_FORWARD, 0, false };
        case UK_FIND: ui_find_open(u); break;
        case UK_HIDDEN: return (UiAction){ UA_HIDDEN, 0, false };
        case UK_ALL:
            for (isize i = 0; i < l->n; i++) ui_mark(u, i, true);
            break;
        case UK_F2:
            if (l->n > 0 && !u->side_focus) ui_edit_open(u, names.at(names.ctx, l->sel));
            break;
        case UK_DELETE: if (l->n > 0 && !u->side_focus) return (UiAction){ UA_TRASH, 0, false }; break;
        case UK_SHIFT_DELETE:
            if (l->n > 0 && !u->side_focus) u->confirming = true, u->confirm_n = u->marked ? u->marked : 1;
            break;
        case UK_COPY: if (l->n > 0 && !u->side_focus) return (UiAction){ UA_COPY, 0, false }; break;
        case UK_CUT: if (l->n > 0 && !u->side_focus) return (UiAction){ UA_CUT, 0, false }; break;
        case UK_PASTE: return (UiAction){ UA_PASTE, 0, false };
        case UK_UNDO: return (UiAction){ UA_UNDO, 0, false };
        case UK_NEWFOLDER: return (UiAction){ UA_NEWFOLDER, 0, false };
        case UK_LOCATION: return (UiAction){ UA_LOCATION, 0, false };
        case UK_CRUMBS: ui_crumbs_focus(u, in); break;
        case UK_JOBS:
            if (in->job_n > 0) u->jobs_focus = true, u->job_at = 0, u->crumb_focus = false, u->side_focus = false;
            break;
        case UK_PARENT: return (UiAction){ UA_PARENT, 0, false };
        case UK_TAB_NEW: return (UiAction){ UA_TAB_NEW, 0, false };
        case UK_TAB_CLOSE: return (UiAction){ UA_TAB_CLOSE, -1, false };
        case UK_TAB_NEXT: return (UiAction){ UA_TAB_STEP, 1, false };
        case UK_TAB_PREV: return (UiAction){ UA_TAB_STEP, -1, false };
        case UK_TAB_LEFT: return (UiAction){ UA_TAB_MOVE, -1, false };
        case UK_TAB_RIGHT: return (UiAction){ UA_TAB_MOVE, 1, false };
        case UK_TAB_JUMP: return (UiAction){ UA_TAB_JUMP, (isize)e.ch, false };
        default: break;
        }
    }
    return (UiAction){ UA_NONE, 0, false };
}

// ---- the path buttons (F4) ---------------------------------------------------------------------
//
// The top bar's path as buttons, one per folder. The bar keeps the deepest path it showed: going
// up keeps the deeper buttons (Nemo does), going anywhere off that path starts it anew. Under the
// home folder the first button is home, and the folders above it are left out.

enum { UI_CRUMBS_MAX = 128 };

typedef struct {
    isize end;    // the button's folder is deep[0..end)
    bool  home;   // the home folder's button
} UiCrumb;

typedef struct {
    char    deep[PATH_MAX];
    UiCrumb seg[UI_CRUMBS_MAX];
    int     n;
    int     cur;  // the button of the current folder
    int     lo;   // the first button shown when they don't all fit; -1: chosen to show cur
} UiCrumbs;

// p is dir or below it.
static bool ui_under(Str p, Str dir)
{
    if (str_eq(dir, S("/"))) return p.len > 0 && p.p[0] == '/';
    return str_starts_with(p, dir) && (p.len == dir.len || p.p[dir.len] == '/');
}

// The current folder is cwd (absolute); home is $HOME (NULL or empty: none).
static void ui_crumbs_set(UiCrumbs *c, const char *cwd, const char *home)
{
    Str cw = str_from_c(cwd), h = str_from_c(home);
    while (h.len > 1 && h.p[h.len - 1] == '/') h.len--;
    if (!c->deep[0] || !ui_under(str_from_c(c->deep), cw)) snprintf(c->deep, sizeof c->deep, "%s", cwd);
    c->lo = -1;
    Str d = str_from_c(c->deep);
    c->n = 0;
    isize at;
    if (h.len > 1 && h.p[0] == '/' && ui_under(cw, h)) c->seg[c->n++] = (UiCrumb){ h.len, true }, at = h.len;
    else c->seg[c->n++] = (UiCrumb){ 1, false }, at = 1;
    while (at < d.len && c->n < UI_CRUMBS_MAX) {
        while (at < d.len && d.p[at] == '/') at++;
        isize e = at;
        while (e < d.len && d.p[e] != '/') e++;
        if (e > at) c->seg[c->n++] = (UiCrumb){ e, false };
        at = e;
    }
    // Past UI_CRUMBS_MAX buttons the current one may not be among them: the deepest shown stands
    // for it, never the root.
    c->cur = -1;
    for (int i = 0; i < c->n; i++)
        if (c->seg[i].end == cw.len) c->cur = i;
    if (c->cur < 0) c->cur = c->n - 1;
}

// Button i's folder.
static Str ui_crumb_path(const UiCrumbs *c, int i)
{
    return (Str){ c->deep, c->seg[i].end };
}

// Button i's label: "/" for the root, else its folder's own name (home's too).
static Str ui_crumb_label(const UiCrumbs *c, int i)
{
    if (!c->seg[i].home && c->seg[i].end == 1) return S("/");
    return path_base(ui_crumb_path(c, i));
}

// From lo on, how many buttons of widths w fit in room beside the two arrows: hi (at least lo + 1).
static int ui_crumbs_fit(const float *w, int n, float room, int lo)
{
    float used = 0;
    int hi = lo;
    while (hi < n && (hi == lo || used + w[hi] <= room)) used += w[hi++];
    return hi;
}

// The buttons shown, [*lo, *hi), and whether the arrows are (when not all fit). Without a scroll of
// its own (c->lo < 0) the window shows as many folders above the current one as it can, then the
// deeper ones, never leaving the current one out.
static bool ui_crumbs_window(const UiCrumbs *c, const float *w, float room, float arrow, int *lo, int *hi)
{
    float all = 0;
    for (int i = 0; i < c->n; i++) all += w[i];
    if (all <= room) return *lo = 0, *hi = c->n, false;
    room -= 2 * arrow;
    if (c->lo >= 0) {
        *lo = c->lo < c->n ? c->lo : c->n - 1;
    } else {
        *lo = 0;
        while (*lo < c->cur && ui_crumbs_fit(w, c->n, room, *lo) <= c->cur) (*lo)++;
    }
    *hi = ui_crumbs_fit(w, c->n, room, *lo);
    return true;
}

// The ‹ (by -1) and › (by 1) arrows: the window moves a button, from where it is now (lo).
static void ui_crumbs_shift(UiCrumbs *c, int lo, int by)
{
    int to = lo + by;
    c->lo = to < 0 ? 0 : to > c->n - 1 ? c->n - 1 : to;
}

// The window moved, if it must, so button at is shown (the one the keyboard marks, F5.2).
static void ui_crumbs_show(UiCrumbs *c, const float *w, float room, float arrow, int at)
{
    int lo, hi;
    ui_crumbs_window(c, w, room, arrow, &lo, &hi);   // all shown: at is among them
    if (at < lo) c->lo = at;
    while (at >= hi && lo < at) hi = ui_crumbs_fit(w, c->n, room - 2 * arrow, ++lo), c->lo = lo;
}

// ---- the location line (F4) -------------------------------------------------------------------

// What the location line says as a folder: "~" and "~/…" from home, a relative path from cwd, then
// "." and ".." taken out. False for an empty line, or one that climbs above "/".
static bool ui_location(Arena *a, Str text, const char *cwd, const char *home, Str *out)
{
    text = str_trim(text);
    if (text.len == 0) return false;
    Str base = str_from_c(cwd);
    if (text.p[0] == '~' && (text.len == 1 || text.p[1] == '/')) {
        if (!home || home[0] != '/') return false;
        base = str_from_c(home), text = str_cut(text, 1, text.len);
        while (text.len && text.p[0] == '/') text = str_cut(text, 1, text.len);
    }
    if (!path_join(a, base, text, out)) return false;
    if (out->len == 0) *out = S("/");
    return true;
}

// ---- tabs (F5) ---------------------------------------------------------------------------------
//
// The tab bar's order: place -> slot, where gui/ keeps each tab's folder, cursor and history.

enum { UI_TABS_MAX = 32 };

typedef struct {
    int slot[UI_TABS_MAX];
    int n;
    int cur;   // the place of the tab on screen
} UiTabs;

// A tab for slot, placed after the current one; shown when `show`. False when the bar is full.
static bool ui_tabs_open(UiTabs *t, int slot, bool show)
{
    if (t->n == UI_TABS_MAX) return false;
    int at = t->n == 0 ? 0 : t->cur + 1;
    memmove(t->slot + at + 1, t->slot + at, sizeof *t->slot * (size_t)(t->n - at));
    t->slot[at] = slot, t->n++;
    if (show) t->cur = at;   // the first tab is at 0, where cur already is
    return true;
}

// The tab at place `at` closed: its slot (now free), or -1 for no such tab. Closing the current
// one shows the tab to its right, or to its left at the end.
static int ui_tabs_close(UiTabs *t, int at)
{
    if (at < 0 || at >= t->n) return -1;
    int slot = t->slot[at];
    memmove(t->slot + at, t->slot + at + 1, sizeof *t->slot * (size_t)(t->n - at - 1));
    t->n--;
    if (at < t->cur || (at == t->cur && t->cur == t->n)) t->cur--;
    if (t->cur < 0) t->cur = 0;
    return slot;
}

// The next (1) or previous (-1) tab, wrapping.
static void ui_tabs_step(UiTabs *t, int by)
{
    if (t->n > 0) t->cur = ((t->cur + by) % t->n + t->n) % t->n;
}

// Alt+k: the k-th tab (1-based), Alt+9 the last; a tab that isn't there changes nothing.
static void ui_tabs_jump(UiTabs *t, int k)
{
    if (k == 9 && t->n > 0) t->cur = t->n - 1;
    else if (k >= 1 && k <= t->n) t->cur = k - 1;
}

// The current tab one place left (-1) or right (1), not past the ends.
static void ui_tabs_move(UiTabs *t, int by)
{
    int to = t->cur + by;
    if (to < 0 || to >= t->n) return;
    int s = t->slot[to];
    t->slot[to] = t->slot[t->cur], t->slot[t->cur] = s, t->cur = to;
}

// ---- history ----------------------------------------------------------------------------------

typedef struct {
    char dir[PATH_MAX];
    char sel[NAME_MAX + 1];   // the entry the cursor was on
} UiPlace;

typedef struct {
    UiPlace back[UI_HISTORY_MAX], fwd[UI_HISTORY_MAX];
    int     nback, nfwd;
} UiHistory;

static void hist_put(UiPlace *stack, int *n, const char *dir, Str sel)
{
    if (*n == UI_HISTORY_MAX) memmove(stack, stack + 1, sizeof *stack * (UI_HISTORY_MAX - 1)), (*n)--;   // the oldest goes
    snprintf(stack[*n].dir, sizeof stack[*n].dir, "%s", dir);
    snprintf(stack[*n].sel, sizeof stack[*n].sel, "%.*s", STR_ARG(sel));
    (*n)++;
}

// Leaving dir (the cursor on sel) for another folder: it goes on the back stack, and forward is
// cleared.
static void hist_leave(UiHistory *h, const char *dir, Str sel)
{
    hist_put(h->back, &h->nback, dir, sel);
    h->nfwd = 0;
}

// Back (dir -1) or forward (1) from dir: false when there is nowhere to go. *to is where to go;
// the folder left goes on the other stack.
static bool hist_step(UiHistory *h, int dir, const char *cur, Str sel, UiPlace *to)
{
    UiPlace *from = dir < 0 ? h->back : h->fwd, *other = dir < 0 ? h->fwd : h->back;
    int *nf = dir < 0 ? &h->nback : &h->nfwd, *no = dir < 0 ? &h->nfwd : &h->nback;
    if (*nf == 0) return false;
    *to = from[--*nf];
    hist_put(other, no, cur, sel);
    return true;
}

// A step that led nowhere (its folder is gone): the folder it left is taken back off the other
// stack, and the dead entry stays dropped.
static void hist_unstep(UiHistory *h, int dir)
{
    int *no = dir < 0 ? &h->nfwd : &h->nback;
    if (*no > 0) (*no)--;
}

// ---- timing -------------------------------------------------------------------------------

enum { UI_DCLICK_MS = 400 };

typedef struct {
    int row;      // the row of the last press, -1 for none
    i64 at;       // when
} UiPress;

// A left press on screen row `row` at `now` (ms): 2 for the second press on the same row within
// UI_DCLICK_MS (a double click, which then starts over), else 1. raylib reports presses, not
// double clicks, as terminals do for prdash-tui.
static int ui_press(UiPress *p, int row, i64 now)
{
    if (p->row == row && row >= 0 && now - p->at <= UI_DCLICK_MS) {
        p->row = -1;
        return 2;
    }
    p->row = row, p->at = now;
    return 1;
}

// When a batch of file system events ends: `quiet` ms after its last event, but never more than
// `most` after its first.
static i64 ui_batch_due(i64 first, i64 last, int quiet, int most)
{
    i64 q = last + quiet, m = first + most;
    return q < m ? q : m;
}

// ---- text that fits ------------------------------------------------------------------------

static float ui_width(Str s, float (*adv)(u32))
{
    float w = 0;
    for (isize i = 0; i < s.len;) w += adv(utf8_next(s.p, s.len, &i));
    return w;
}

// Whether s fits in width. When it doesn't, [*from, *to) is the part to keep beside an ellipsis
// of width `dots`: whole code points from the start (a name), or from the end when keep_end (a
// path, whose end is the part that matters). Nothing is kept when not even one fits.
static bool ui_fit(Str s, float width, float dots, float (*adv)(u32), bool keep_end, isize *from, isize *to)
{
    *from = 0, *to = s.len;
    if (ui_width(s, adv) <= width) return true;
    float room = width - dots, w = 0;
    if (keep_end) {
        *from = s.len;
        while (*from > 0) {
            isize j = *from - 1;
            while (j > 0 && ((unsigned char)s.p[j] & 0xC0) == 0x80) j--;
            isize k = j;
            float a = adv(utf8_next(s.p, s.len, &k));
            if (w + a > room) break;
            w += a, *from = j;
        }
        return false;
    }
    *to = 0;
    for (isize i = 0; i < s.len;) {
        float a = adv(utf8_next(s.p, s.len, &i));
        if (w + a > room) break;
        w += a, *to = i;
    }
    return false;
}

// ---- display --------------------------------------------------------------------------------

// A size as ls -h prints it, in binary units: "512 B", "4.0 KiB", "13 MiB", one decimal below 10.
static void ui_size(char *buf, size_t cap, i64 size)
{
    static const char *const UNITS[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
    if (size < 1024) {
        snprintf(buf, cap, "%lld B", (long long)size);
        return;
    }
    double v = (double)size;
    int u = 0;
    while (v >= 1024 && u < COUNT(UNITS) - 1) v /= 1024, u++;
    // As printed: 1023.7 KiB would show as "1024 KiB", so it goes up a unit ("1.0 MiB").
    if (v + 0.5 >= 1024 && u < COUNT(UNITS) - 1) v /= 1024, u++;
    if (v < 9.95) snprintf(buf, cap, "%.1f %s", v, UNITS[u]);
    else snprintf(buf, cap, "%.0f %s", v, UNITS[u]);
}

// A modification time: "14:03" today, "Oct 20" this year, "2025-10-20" before, as `now` sees it
// (local time).
// What the status line says when no message is up (F6): about the one entry marked, how many are
// marked, or else the folder's count and its drive's free space.
typedef struct {
    isize n, hidden;     // the listing's entries, and its dot-files (left out or not)
    isize marked;
    Str   one;           // marked == 1: its name,
    bool  one_dir;       //   whether it is a folder,
    isize one_count;     //   a folder's item count (< 0 unknown),
    i64   one_size;      //   a file's size (< 0 none: a broken link, a device)
    i64   avail;         // the drive's free space, < 0 unknown
} UiInfo;

static void ui_status_info(char *buf, size_t cap, const UiInfo *in)
{
    char size[32];
    if (in->marked == 1) {
        int k = snprintf(buf, cap, "\"%.*s\" selected", STR_ARG(in->one));
        if (k < 0 || (size_t)k >= cap) return;
        if (in->one_dir && in->one_count >= 0) snprintf(buf + k, cap - (size_t)k, " (%td item%s)", in->one_count, in->one_count == 1 ? "" : "s");
        else if (!in->one_dir && in->one_size >= 0) ui_size(size, sizeof size, in->one_size), snprintf(buf + k, cap - (size_t)k, " (%s)", size);
        return;
    }
    if (in->marked > 1) {
        snprintf(buf, cap, "%td items selected", in->marked);
        return;
    }
    int k = snprintf(buf, cap, "%td item%s", in->n, in->n == 1 ? "" : "s");
    if (k > 0 && in->hidden && (size_t)k < cap) k += snprintf(buf + k, cap - (size_t)k, " (%td hidden)", in->hidden);
    if (k > 0 && in->avail >= 0 && (size_t)k < cap) ui_size(size, sizeof size, in->avail), snprintf(buf + k, cap - (size_t)k, ", %s free", size);
}

static void ui_when(char *buf, size_t cap, i64 mtime, i64 now)
{
    time_t t = (time_t)mtime, n = (time_t)now;
    struct tm tm, tn;
    localtime_r(&t, &tm);
    localtime_r(&n, &tn);
    if (tm.tm_year == tn.tm_year && tm.tm_yday == tn.tm_yday) strftime(buf, cap, "%H:%M", &tm);
    else if (tm.tm_year == tn.tm_year) strftime(buf, cap, "%b %e", &tm);
    else strftime(buf, cap, "%Y-%m-%d", &tm);
}
