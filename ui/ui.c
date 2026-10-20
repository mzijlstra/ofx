// What the keys, wheel and clicks do to the list: the cursor, the scroll, and the one action a
// frame may ask of gui/ (enter a folder, go up, open a file, a terminal, home). raylib-free and
// unit-tested; gui/main.c feeds it (docs/plan.md, "Keys").

#include "../base/base.h"

#include <stdio.h>
#include <time.h>

enum { UI_KEYS_MAX = 64 };

typedef enum {
    UK_NONE,
    UK_UP, UK_DOWN, UK_PAGEUP, UK_PAGEDOWN, UK_HOME, UK_END,
    UK_OPEN,       // Enter, Right, l: enter the folder, or open the file
    UK_PARENT,     // Backspace, Left, h
    UK_TERMINAL,   // t
    UK_HOMEDIR,    // ~
} UiKey;

typedef struct {
    UiKey keys[UI_KEYS_MAX];
    int   nkeys;
    int   wheel;      // notches, positive away from the user (up)
    int   click;      // the row pressed (0 = the first row on screen), -1 for none
    int   dclick;     // a row double-clicked, -1 for none
    int   backs;      // presses of the mouse's back button
} UiInput;

typedef enum { UA_NONE, UA_OPEN, UA_PARENT, UA_TERMINAL, UA_HOMEDIR } UiActionKind;

typedef struct {
    UiActionKind kind;
    isize        index;   // UA_OPEN: the entry
} UiAction;

typedef struct {
    isize n;      // entries in the list
    isize sel;    // the cursor, 0..n-1; 0 for an empty list
    isize top;    // the first row on screen
    int   rows;   // how many rows fit
} UiList;

static UiInput ui_input_none(void)
{
    return (UiInput){ .click = -1, .dclick = -1 };
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

// One frame's input. Keys are taken in order up to the first that asks gui/ for something (after
// it, the list may be another folder's); the wheel scrolls without moving the cursor, one row a
// notch (Michael's choice for prdash-tui), and a click puts the cursor on its row.
static UiAction ui_handle(UiList *l, const UiInput *in)
{
    isize page = l->rows > 1 ? l->rows - 1 : 1;
    if (in->wheel) l->top = ui_clamp(l->top - in->wheel, 0, ui_max_top(l));
    if (in->click >= 0 && l->top + in->click < l->n) l->sel = l->top + in->click;
    if (in->dclick >= 0 && l->top + in->dclick < l->n) {
        l->sel = l->top + in->dclick;
        return (UiAction){ UA_OPEN, l->sel };
    }
    if (in->backs > 0) return (UiAction){ UA_PARENT, 0 };
    for (int k = 0; k < in->nkeys; k++) {
        switch (in->keys[k]) {
        case UK_UP: ui_move(l, -1); break;
        case UK_DOWN: ui_move(l, 1); break;
        case UK_PAGEUP: ui_move(l, -page); break;
        case UK_PAGEDOWN: ui_move(l, page); break;
        case UK_HOME: ui_move(l, -l->n); break;
        case UK_END: ui_move(l, l->n); break;
        case UK_OPEN:
            if (l->n > 0) return (UiAction){ UA_OPEN, l->sel };
            break;
        case UK_PARENT: return (UiAction){ UA_PARENT, 0 };
        case UK_TERMINAL: return (UiAction){ UA_TERMINAL, 0 };
        case UK_HOMEDIR: return (UiAction){ UA_HOMEDIR, 0 };
        default: break;
        }
    }
    return (UiAction){ UA_NONE, 0 };
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
