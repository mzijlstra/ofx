// What the keys, characters, wheel and clicks do: the list's cursor and scroll, the sidebar's,
// find, sort and the history, and the one action a frame may ask of gui/ (open, go somewhere,
// toggle hidden files, sort again). raylib-free and unit-tested; gui/main.c feeds it
// (docs/plan.md, F1 "Keys" and F2).

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
} UiKey;

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
    int     dclick;      // a list row double-clicked, -1 for none
    int     side_click;  // a sidebar row pressed, -1 for none
    int     header;      // a column header clicked (a SortKey), -1 for none
    int     backs, forwards;   // presses of the mouse's back and forward buttons
} UiInput;

typedef enum {
    UA_NONE, UA_OPEN, UA_PARENT, UA_TERMINAL, UA_HOMEDIR, UA_BACK, UA_FORWARD, UA_HIDDEN, UA_SORT, UA_PLACE,
} UiActionKind;

typedef struct {
    UiActionKind kind;
    isize        index;   // UA_OPEN: the entry; UA_PLACE: the place
} UiAction;

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
} Ui;

static UiInput ui_input_none(void)
{
    return (UiInput){ .click = -1, .dclick = -1, .side_click = -1, .header = -1 };
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

// A character typed into the find line, as UTF-8; it is dropped when the line is full.
static void ui_find_char(Ui *u, u32 cp)
{
    char b[4];
    int n = cp < 0x80 ? (b[0] = (char)cp, 1)
          : cp < 0x800 ? (b[0] = (char)(0xC0 | cp >> 6), b[1] = (char)(0x80 | (cp & 0x3F)), 2)
          : cp < 0x10000 ? (b[0] = (char)(0xE0 | cp >> 12), b[1] = (char)(0x80 | (cp >> 6 & 0x3F)), b[2] = (char)(0x80 | (cp & 0x3F)), 3)
          : (b[0] = (char)(0xF0 | cp >> 18), b[1] = (char)(0x80 | (cp >> 12 & 0x3F)), b[2] = (char)(0x80 | (cp >> 6 & 0x3F)), b[3] = (char)(0x80 | (cp & 0x3F)), 4);
    if (cp < 0x20 || cp == 0x7F || cp > 0x10FFFF || u->find_len + n > UI_FIND_MAX) return;
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
// moving its cursor, one row a notch (Michael's choice for prdash-tui).
static UiAction ui_handle(Ui *u, UiNames names, const UiInput *in)
{
    UiList *l = &u->list;
    if (in->wheel) l->top = ui_clamp(l->top - in->wheel, 0, ui_max_top(l));
    if (in->header >= 0 && in->header < SORT_KEYS) {
        u->sort = ui_sort_click(u->sort, (SortKey)in->header);
        return (UiAction){ UA_SORT, 0 };
    }
    if (in->side_click >= 0 && u->side.top + in->side_click < u->side.n) {
        u->side.sel = u->side.top + in->side_click;
        return (UiAction){ UA_PLACE, u->side.sel };
    }
    if (in->click >= 0 && l->top + in->click < l->n) l->sel = l->top + in->click, u->side_focus = false;
    if (in->dclick >= 0 && l->top + in->dclick < l->n) {
        l->sel = l->top + in->dclick, u->side_focus = false;
        return (UiAction){ UA_OPEN, l->sel };
    }
    if (in->backs > 0) return (UiAction){ UA_BACK, 0 };
    if (in->forwards > 0) return (UiAction){ UA_FORWARD, 0 };
    for (int k = 0; k < in->nev; k++) {
        UiEvent e = in->ev[k];
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
            case 't': return (UiAction){ UA_TERMINAL, 0 };
            case '~': return (UiAction){ UA_HOMEDIR, 0 };
            case 's': u->sort = (Sort){ (SortKey)((u->sort.key + 1) % SORT_KEYS), false }; return (UiAction){ UA_SORT, 0 };
            case 'S': u->sort.reverse = !u->sort.reverse; return (UiAction){ UA_SORT, 0 };
            case 'n': ui_find_again(u, names, 1); continue;
            case 'N': ui_find_again(u, names, -1); continue;
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
            if (u->side_focus && u->side.n > 0) return (UiAction){ UA_PLACE, u->side.sel };
            if (!u->side_focus && l->n > 0) return (UiAction){ UA_OPEN, l->sel };
            break;
        case UK_LEFT:
        case UK_BACKSPACE: return (UiAction){ UA_PARENT, 0 };
        case UK_TAB: u->side_focus = !u->side_focus && u->side.n > 0; break;
        case UK_ESC: u->side_focus = false; break;
        case UK_BACK: return (UiAction){ UA_BACK, 0 };
        case UK_FORWARD: return (UiAction){ UA_FORWARD, 0 };
        case UK_FIND: ui_find_open(u); break;
        case UK_HIDDEN: return (UiAction){ UA_HIDDEN, 0 };
        default: break;
        }
    }
    return (UiAction){ UA_NONE, 0 };
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
