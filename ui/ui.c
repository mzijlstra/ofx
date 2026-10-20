// What the keys, characters, wheel and clicks do: the list's cursor and scroll, the sidebar's,
// find, sort, the history, the selection, the edit line and the prompts, and the one action a
// frame may ask of gui/ (open, go somewhere, toggle hidden files, sort again, change files).
// raylib-free and unit-tested; gui/main.c feeds it (docs/plan.md, F1 "Keys", F2 and F3).

#include "../base/base.h"

#include <limits.h>
#include <stdio.h>
#include <time.h>

enum { UI_EVENTS_MAX = 64, UI_FIND_MAX = 256, UI_HISTORY_MAX = 64 };

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
    int     backs, forwards;   // presses of the mouse's back and forward buttons
} UiInput;

typedef enum {
    UA_NONE, UA_OPEN, UA_PARENT, UA_TERMINAL, UA_HOMEDIR, UA_BACK, UA_FORWARD, UA_HIDDEN, UA_SORT, UA_PLACE,
    UA_RENAME,      // the edit line's Enter: its text is the new name (the line stays open until
                    // gui/ closes it, so a refused name can be fixed)
    UA_NEWFOLDER, UA_TRASH, UA_DELETE, UA_COPY, UA_CUT, UA_PASTE, UA_UNDO,
    UA_ANSWER,      // a clash answered: index is the Answer, all "the rest the same"
    UA_CANCEL_JOB,
    UA_LOCATION,    // the location line, open or closed (gui/ fills it with the folder)
    UA_GO,          // the location line's Enter: its text is where to go (open until gui/ closes it)
    UA_CRUMB,       // a path button: index
    UA_CRUMB_SHIFT, // the path buttons' arrows: index -1 or 1
} UiActionKind;

typedef struct {
    UiActionKind kind;
    isize        index;   // UA_OPEN: the entry; UA_PLACE: the place; UA_ANSWER: the answer
    bool         all;     // UA_ANSWER: for all the rest
} UiAction;

// The clash answers, as fs/job.c numbers them (Answer); ui/ doesn't include it.
enum { UI_ANS_SKIP = 1, UI_ANS_REPLACE = 2, UI_ANS_KEEP = 3, UI_ANS_CANCEL = 4 };

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
    bool   side_focus;       // the keys move the sidebar's cursor
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
    bool   edit_all;         // all of it selected (the location line as it opens): typing replaces it
    char   edit[PATH_MAX];
    int    edit_len, caret;  // bytes
    // The prompts and the job: gui/ says when one runs, and when it asks about a clash.
    bool   confirming;       // "Delete … for good? Y/n"
    isize  confirm_n;
    bool   busy;             // a job runs: Esc cancels it
    bool   asking;           // the job waits for a clash answer
} Ui;

static UiInput ui_input_none(void)
{
    return (UiInput){ .click = -1, .dclick = -1, .side_click = -1, .header = -1, .click_mods = 0, .bar = UB_NONE, .crumb = -1 };
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
    u->edit_len = (int)n, u->caret = (int)n, u->edit_all = path;
    u->editing = true, u->finding = false, u->side_focus = false;
}

// The edit line opened on a name, the caret at its end.
static void ui_edit_open(Ui *u, Str text)
{
    ui_edit_start(u, text, false);
}

// The location line opened on a path, all of it selected, as browsers and Nemo do: typing a new one
// replaces it, a move of the caret keeps it.
static void ui_location_open(Ui *u, Str path)
{
    ui_edit_start(u, path, true);
}

static void ui_edit_close(Ui *u)
{
    u->editing = false;
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

// Bytes [from, to) of the edit line removed.
static void ui_edit_cut(Ui *u, int from, int to)
{
    memmove(u->edit + from, u->edit + to, (size_t)(u->edit_len - to));
    u->edit_len -= to - from;
    u->caret = from;
}

// A key or character in the edit line: an action for gui/ (UA_RENAME on Enter) or UA_NONE.
static UiAction ui_edit_event(Ui *u, UiEvent e)
{
    if (u->edit_all) {
        u->edit_all = false;
        bool replace = e.key == UK_NONE || e.key == UK_BACKSPACE || e.key == UK_DELETE;
        if (replace) u->edit_len = 0, u->caret = 0;   // Backspace and Delete then find nothing to take
    }
    if (e.key == UK_NONE) {
        char b[4];
        int n = ui_utf8(e.ch, b);
        if (n == 0 || (e.ch == '/' && !u->edit_path) || u->edit_len + n > ui_edit_max(u)) return (UiAction){ UA_NONE, 0, false };
        memmove(u->edit + u->caret + n, u->edit + u->caret, (size_t)(u->edit_len - u->caret));
        memcpy(u->edit + u->caret, b, (size_t)n);
        u->edit_len += n, u->caret += n;
        return (UiAction){ UA_NONE, 0, false };
    }
    switch (e.key) {
    case UK_LEFT: u->caret = ui_edit_back(u, u->caret); break;
    case UK_RIGHT: u->caret = ui_edit_fwd(u, u->caret); break;
    case UK_HOME: u->caret = 0; break;
    case UK_END: u->caret = u->edit_len; break;
    case UK_BACKSPACE: ui_edit_cut(u, ui_edit_back(u, u->caret), u->caret); break;
    case UK_DELETE: ui_edit_cut(u, u->caret, ui_edit_fwd(u, u->caret)); break;
    case UK_ENTER: return (UiAction){ u->edit_path ? UA_GO : UA_RENAME, 0, false };
    case UK_ESC: ui_edit_close(u); break;
    case UK_LOCATION:   // Ctrl+L again: the location line closes; a rename doesn't take it
        if (u->edit_path) ui_edit_close(u);
        break;
    default: break;
    }
    return (UiAction){ UA_NONE, 0, false };
}

// ---- the prompts -------------------------------------------------------------------------------

// A key or character while "Delete … for good? Y/n" is up: Enter and y go ahead, n and Esc don't,
// the rest is ignored.
static UiAction ui_confirm_event(Ui *u, UiEvent e)
{
    bool yes = e.key == UK_ENTER || (e.key == UK_NONE && (e.ch == 'y' || e.ch == 'Y'));
    bool no = e.key == UK_ESC || (e.key == UK_NONE && (e.ch == 'n' || e.ch == 'N'));
    if (yes || no) u->confirming = false;
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
    return (UiAction){ UA_ANSWER, a, e.ch >= 'A' && e.ch <= 'Z' };
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
    bool prompt = u->asking || u->confirming;
    if (in->wheel) l->top = ui_clamp(l->top - in->wheel, 0, ui_max_top(l));
    if (!prompt && in->header >= 0 && in->header < SORT_KEYS) {
        u->sort = ui_sort_click(u->sort, (SortKey)in->header);
        return (UiAction){ UA_SORT, 0, false };
    }
    if (!prompt && in->side_click >= 0 && u->side.top + in->side_click < u->side.n) {
        u->side.sel = u->side.top + in->side_click;
        ui_edit_close(u);
        return (UiAction){ UA_PLACE, u->side.sel, false };
    }
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
        if (u->editing) {
            UiAction a = ui_edit_event(u, e);
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
        case UK_UP: ui_move(m, -1); break;
        case UK_DOWN: ui_move(m, 1); break;
        case UK_PAGEUP: ui_move(m, -page); break;
        case UK_PAGEDOWN: ui_move(m, page); break;
        case UK_HOME: ui_move(m, -m->n); break;
        case UK_END: ui_move(m, m->n); break;
        case UK_ENTER:
        case UK_RIGHT:
            if (u->side_focus && u->side.n > 0) return (UiAction){ UA_PLACE, u->side.sel, false };
            if (!u->side_focus && l->n > 0) return (UiAction){ UA_OPEN, l->sel, false };
            break;
        case UK_LEFT:
        case UK_BACKSPACE: return (UiAction){ UA_PARENT, 0, false };
        case UK_TAB: u->side_focus = !u->side_focus && u->side.n > 0; break;
        case UK_ESC:
            // A job first, then the selection, then the sidebar's keyboard.
            if (u->busy) return (UiAction){ UA_CANCEL_JOB, 0, false };
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
        case UK_PARENT: return (UiAction){ UA_PARENT, 0, false };
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
    c->cur = 0;
    for (int i = 0; i < c->n; i++)
        if (c->seg[i].end == cw.len) c->cur = i;
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
