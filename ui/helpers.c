// What gui/ asks of ui/ without a Ui: the path buttons (F4), the location line, tabs, history,
// timing, text that fits, and the status line's words. Included by ui.c.

// ---- the path buttons (F4) ---------------------------------------------------------------------
//
// The top bar's path as buttons, one per folder. The bar keeps the deepest path it showed: going
// up keeps the deeper buttons (Nemo does), going anywhere off that path starts it anew. Under the
// home folder the first button is home, and the folders above it are left out; in a share gvfs
// mounted (F16 step 5), the share, named as gvfs names it ("pub on nas"), as Nautilus and Nemo do.

enum { UI_CRUMBS_MAX = 128 };

typedef struct {
    isize end;    // the button's folder is deep[0..end)
    bool  home;   // the home folder's button
    bool  share;  // a share's (its label: UiCrumbs.share_label)
} UiCrumb;

typedef struct {
    char    deep[PATH_MAX];
    char    share_label[256];
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

// The current folder is cwd (absolute); home is $HOME (NULL or empty: none); share the folder of the
// share cwd is in and label its name (NULL: none).
static void ui_crumbs_set_share(UiCrumbs *c, const char *cwd, const char *home, const char *share, const char *label)
{
    Str cw = str_from_c(cwd), h = str_from_c(home), sh = str_from_c(share);
    if (str_eq(cw, S(TRASH_URI)) || str_eq(cw, S(NETWORK_URI)) || is_search(cwd)) {   // the trash view (F6): one button, "Trash"; a search's (F9), "Search"; Browse Network's (F16 step 7e)
        snprintf(c->deep, sizeof c->deep, "%s", cwd);
        c->seg[0] = (UiCrumb){ cw.len, false, false };
        c->n = 1, c->cur = 0, c->lo = -1;
        return;
    }
    if (str_starts_with(cw, S(NETWORK_URI))) {   // a server's shares (F16 step 7e): "Browse Network", then the server
        snprintf(c->deep, sizeof c->deep, "%s", cwd);
        c->seg[0] = (UiCrumb){ (isize)strlen(NETWORK_URI), false, false }, c->seg[1] = (UiCrumb){ cw.len, false, false };
        c->n = 2, c->cur = 1, c->lo = -1;
        return;
    }
    while (h.len > 1 && h.p[h.len - 1] == '/') h.len--;
    if (!c->deep[0] || !ui_under(str_from_c(c->deep), cw)) snprintf(c->deep, sizeof c->deep, "%s", cwd);
    c->lo = -1;
    Str d = str_from_c(c->deep);
    c->n = 0;
    isize at;
    if (sh.len > 1 && sh.p[0] == '/' && ui_under(cw, sh) && label) {
        c->seg[c->n++] = (UiCrumb){ sh.len, false, true }, at = sh.len;
        snprintf(c->share_label, sizeof c->share_label, "%s", label);
    } else if (h.len > 1 && h.p[0] == '/' && ui_under(cw, h)) c->seg[c->n++] = (UiCrumb){ h.len, true, false }, at = h.len;
    else c->seg[c->n++] = (UiCrumb){ 1, false, false }, at = 1;
    while (at < d.len && c->n < UI_CRUMBS_MAX) {
        while (at < d.len && d.p[at] == '/') at++;
        isize e = at;
        while (e < d.len && d.p[e] != '/') e++;
        if (e > at) c->seg[c->n++] = (UiCrumb){ e, false, false };
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

// Button i's label: "/" for the root, a share's name, else its folder's own name (home's too).
static Str ui_crumb_label(const UiCrumbs *c, int i)
{
    if (c->seg[i].share) return str_from_c(c->share_label);
    if (!c->seg[i].home && c->seg[i].end == 1) return S("/");
    if (str_eq(ui_crumb_path(c, i), S(TRASH_URI))) return S("Trash");
    if (str_eq(ui_crumb_path(c, i), S(NETWORK_URI))) return S("Browse Network");
    if (str_starts_with(ui_crumb_path(c, i), S(NETWORK_URI))) {   // (a server's shares: its host, "yoga.local" of smb://yoga.local)
        Str p = ui_crumb_path(c, i);
        isize sep = str_find_str(p, S("://"));
        Str host = sep < 0 ? p : str_cut(p, sep + 3, p.len);
        while (host.len > 1 && host.p[host.len - 1] == '/') host.len--;
        return host;
    }
    if (str_starts_with(ui_crumb_path(c, i), S(SEARCH_URI))) return S("Search");
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

// path with home as "~" ("~/x" under it), into buf: the Properties panel's location (F7.2).
static void ui_tilde(char *buf, size_t cap, const char *path, const char *home)
{
    size_t n = home ? strlen(home) : 0;
    if (n > 1 && strncmp(path, home, n) == 0 && (path[n] == '/' || path[n] == 0)) snprintf(buf, cap, "~%s", path + n);
    else snprintf(buf, cap, "%s", path);
}

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
    // (a search's folder/name too long for it: none, not a cut one naming another entry, F9)
    snprintf(stack[*n].sel, sizeof stack[*n].sel, "%.*s", sel.len < (isize)sizeof stack[*n].sel ? (int)sel.len : 0, sel.p ? sel.p : "");
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

// A step found to lead nowhere only later (a far folder's lister said it is gone, F16 step 3): the
// folder it left taken back off the other stack into *was. False when there is none.
static bool hist_take(UiHistory *h, int dir, UiPlace *was)
{
    UiPlace *other = dir < 0 ? h->fwd : h->back;
    int *no = dir < 0 ? &h->nfwd : &h->nback;
    if (*no == 0) return false;
    *was = other[--*no];
    return true;
}

// ---- timing -------------------------------------------------------------------------------

// A tooltip (Michael, 2026-10-24): shown once the pointer has rested UI_TIP_MS on one button (gui/
// numbers them, -1 for none); gone when the pointer moves to another, and after a click or a key
// until the pointer leaves that button.
enum { UI_TIP_MS = 600 };

typedef struct {
    int  target;   // the button under the pointer, -1 for none
    i64  since;    // when the pointer came onto it (ms)
    bool shown;
    bool off;      // a click or key on it: no tooltip until the pointer leaves
} UiTip;

// The pointer over target at now: true when that is another button than before.
static bool ui_tip_hover(UiTip *t, int target, i64 now)
{
    if (target == t->target) return false;
    *t = (UiTip){ target, now, false, false };
    return true;
}

// The soonest of n due times (now_ms times, 0 for none): the window's one timer is set to it. 0:
// none due.
static i64 ui_due_min(const i64 *d, int n)
{
    i64 due = 0;
    for (int k = 0; k < n; k++)
        if (d[k] && (!due || d[k] < due)) due = d[k];
    return due;
}

// How long ago then was, at now (unix seconds), as the branches panel says it (F12 step 6): "now",
// "5 min", "3 h", "2 days", "3 weeks", "4 months", "2 years" (whole units, rounded down; a time to come,
// as a clock's skew, is "now").
static void ui_age(char *out, size_t cap, i64 now, i64 then)
{
    i64 d = now - then;
    if (d < 60) snprintf(out, cap, "now");
    else if (d < 3600) snprintf(out, cap, "%lld min", (long long)(d / 60));
    else if (d < 86400) snprintf(out, cap, "%lld h", (long long)(d / 3600));
    else if (d < 14 * 86400) snprintf(out, cap, "%lld %s", (long long)(d / 86400), d / 86400 == 1 ? "day" : "days");
    else if (d < 60 * 86400) snprintf(out, cap, "%lld weeks", (long long)(d / (7 * 86400)));
    else if (d < 365 * 86400) snprintf(out, cap, "%lld months", (long long)(d / (30 * 86400)));
    else snprintf(out, cap, "%lld %s", (long long)(d / (365 * 86400)), d / (365 * 86400) == 1 ? "year" : "years");
}

// When the tooltip is due (ms), 0 when none is waiting.
static i64 ui_tip_due(const UiTip *t)
{
    return t->target >= 0 && !t->shown && !t->off ? t->since + UI_TIP_MS : 0;
}

// At now: true when the tooltip appears just now.
static bool ui_tip_tick(UiTip *t, i64 now)
{
    i64 due = ui_tip_due(t);
    if (!due || now < due) return false;
    return t->shown = true;
}

// A click or key: the tooltip goes, and stays away while the pointer stays. True when one was shown.
static bool ui_tip_act(UiTip *t)
{
    bool was = t->shown;
    t->shown = false, t->off = t->target >= 0;
    return was;
}

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

// Whether s breaks after byte i: after a space, '-', '_' or '.', where a name reads as words.
static bool ui_breaks_after(Str s, isize i)
{
    char c = s.p[i];
    return c == ' ' || c == '-' || c == '_' || c == '.';
}

// s in lines of `width` at most, for a name under its icon (F6.2): each line ends after the last
// break (ui_breaks_after) that fits, or between code points when none does, and holds one code
// point at least. At most `most` lines: the last holds the rest, cut to fit with an ellipsis of
// width `dots` when it doesn't (*cut true). Line k is [from[k], to[k]); returns how many.
static int ui_wrap(Str s, float width, float dots, float (*adv)(u32), int most, isize *from, isize *to, bool *cut)
{
    *cut = false;
    int n = 0;
    isize at = 0;
    while (n < most && (at < s.len || n == 0)) {
        Str rest = str_cut(s, at, s.len);
        isize f, t;
        if (n == most - 1 || ui_width(rest, adv) <= width) {
            *cut = !ui_fit(rest, width, dots, adv, false, &f, &t);
            from[n] = at, to[n] = at + t, n++;
            break;
        }
        // The longest run of whole code points that fits, then back to its last break.
        float w = 0;
        isize end = at, brk = -1;
        for (isize i = at; i < s.len;) {
            isize j = i;
            float a = adv(utf8_next(s.p, s.len, &j));
            if (w + a > width && end > at) break;
            w += a, end = j;
            if (ui_breaks_after(s, i)) brk = j;
            i = j;
        }
        isize e = brk > at ? brk : end;   // end < s.len: the rest fitting was the case above
        from[n] = at, to[n] = e, n++;
        at = e;
    }
    return n;
}

// ---- the sidebar's width (F13) -------------------------------------------------------------

enum { UI_SIDE_W = 200, UI_SIDE_MIN = 140, UI_SIDE_MAX = 480 };

// The sidebar's width in a window w wide: the one asked for, at least UI_SIDE_MIN, at most half the
// window or UI_SIDE_MAX (Michael, 2026-11-04); a window too narrow for both keeps the least.
// (UI_SIDE_W, the first, is gui/'s when the state file has none: a drag past the window's edge,
// 0 or less, is the least, not the first: F13 code-health review.)
static int ui_side_fit(int want, int w)
{
    int hi = w / 2 < UI_SIDE_MAX ? w / 2 : UI_SIDE_MAX;
    if (want > hi) want = hi;
    return want < UI_SIDE_MIN ? UI_SIDE_MIN : want;
}

// ---- drag and drop (F13 step 3) -------------------------------------------------------------

// A press held and moved this far (px) is a drag; less is still a click. A drag held this long
// (ms) over another tab shows it (spring-loaded tabs, F13 step 4).
enum { UI_DRAG_PX = 6, UI_SPRING_MS = 800 };

static bool ui_drag_past(float dx, float dy)
{
    return dx * dx + dy * dy >= (float)(UI_DRAG_PX * UI_DRAG_PX);
}

// What a drop does: Nemo's and Nautilus's rule (Michael, 2026-11-04): Ctrl held copies, Shift held
// moves; else a move on the same drive, a copy to another. (The Trash's is UI_DROP_TRASH, gui/'s.)
typedef enum { UI_DROP_MOVE, UI_DROP_COPY, UI_DROP_TRASH } UiDropOp;

static UiDropOp ui_drop_op(bool same_drive, int mods)
{
    if (mods & UI_MOD_CTRL) return UI_DROP_COPY;
    if (mods & UI_MOD_SHIFT) return UI_DROP_MOVE;
    return same_drive ? UI_DROP_MOVE : UI_DROP_COPY;
}

// ---- drops from other programs (F14 step 3) -------------------------------------------------------

// The compositor's actions, as wl_data_device_manager numbers them (gui/dnd.c asserts they match).
enum { UI_DND_COPY = 1, UI_DND_MOVE = 2 };

// What op asks of another program's source, which allows actions: a move it doesn't allow is a
// copy, the Trash needs a move (Michael, 2026-11-05). 0: refused.
static u32 ui_dnd_action(UiDropOp op, u32 actions)
{
    u32 want = op == UI_DROP_COPY ? UI_DND_COPY : UI_DND_MOVE;
    if (actions & want) return want;
    return op == UI_DROP_MOVE && (actions & UI_DND_COPY) ? UI_DND_COPY : 0;
}

// The action a drop takes: a move only when the window's (a) and the compositor's (c) both say so,
// else a copy (a copy loses nothing); either alone when the other has none. Hyprland didn't send
// the action again after the window's answer (Michael's log, 2026-11-05: Ctrl's copy in Nemo dropped
// as the move it had sent before the enter), and a compositor may change it for keys the window
// doesn't see (review, 2026-11-06). (A drop the window refused, which Hyprland sends with c 0, the
// source told it was cancelled, never comes here: gui/dnd.c gives it back.)
static u32 ui_dnd_merge(u32 a, u32 c)
{
    return !a ? c : !c || a == c ? a : UI_DND_COPY;
}

// What a drop with action act does to the files (*op): on the Trash only a move trashes them,
// elsewhere a move or a copy as it says. False: nothing (no action, or a copy onto the Trash).
static bool ui_dnd_drop(bool trash, u32 act, UiDropOp *op)
{
    if (!act || (trash && act != UI_DND_MOVE)) return false;
    *op = trash ? UI_DROP_TRASH : act == UI_DND_MOVE ? UI_DROP_MOVE : UI_DROP_COPY;
    return true;
}

// ---- display --------------------------------------------------------------------------------

// A mode's permissions as ls prints them, without the type: "rwxr-xr-x"; s/S for set-user and
// set-group IDs, t/T for the sticky bit (upper case: the x under it is off). "-" for mode 0 (a
// broken link: nothing to read). The list's Permissions column (2026-10-27).
static void ui_perms(char *buf, size_t cap, u32 mode)
{
    if (!mode) {
        snprintf(buf, cap, "-");
        return;
    }
    char p[10];
    static const char RWX[] = "rwx";
    for (int k = 0; k < 9; k++) p[k] = mode & (0400u >> k) ? RWX[k % 3] : '-';
    if (mode & 04000) p[2] = p[2] == 'x' ? 's' : 'S';
    if (mode & 02000) p[5] = p[5] == 'x' ? 's' : 'S';
    if (mode & 01000) p[8] = p[8] == 'x' ? 't' : 'T';
    p[9] = 0;
    snprintf(buf, cap, "%s", p);
}

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
    bool  cramped;       // Show Permissions on, but the window has no room for them: said (2026-10-27)
    bool  opening;       // a far folder's listing still to come (F16), or Browse Network's query (step 7e): said instead of its count
    isize changed;       // in a git repository, the changes under the folder (F12 step 5); 0: none said
    const char *noun;    // what the rows are when not files: "server", "share" (Browse Network's, F16 step 7e); NULL: items
} UiInfo;

static void ui_status_base(char *buf, size_t cap, const UiInfo *in);

// That, and when the permissions' columns gave way for lack of room, so it says (Michael, 2026-10-27:
// an option set and nothing showing would confuse).
static void ui_status_info(char *buf, size_t cap, const UiInfo *in)
{
    ui_status_base(buf, cap, in);
    size_t k = strlen(buf);
    if (in->cramped && k < cap) snprintf(buf + k, cap - k, " \xC2\xB7 Permissions hidden: the window is too narrow");
}

static void ui_status_base(char *buf, size_t cap, const UiInfo *in)
{
    char size[32];
    if (in->marked == 1) {
        int k = snprintf(buf, cap, "\"%.*s\" selected", STR_ARG(in->one));
        if (k < 0 || (size_t)k >= cap) return;
        if (in->one_dir && in->one_count >= 0) snprintf(buf + k, cap - (size_t)k, " (%td item%s)", in->one_count, in->one_count == 1 ? "" : "s");
        else if (!in->one_dir && in->one_size >= 0) ui_size(size, sizeof size, in->one_size), snprintf(buf + k, cap - (size_t)k, " (%s)", size);
        return;
    }
    const char *noun = in->noun ? in->noun : "item";
    if (in->marked > 1) {
        snprintf(buf, cap, "%td %ss selected", in->marked, noun);
        return;
    }
    if (in->opening) {
        snprintf(buf, cap, in->noun && strcmp(in->noun, "server") == 0 ? "Searching the network\xE2\x80\xA6" : "Opening\xE2\x80\xA6");   // (Browse Network's: the last rows shown meanwhile)
        return;
    }
    int k = snprintf(buf, cap, "%td %s%s", in->n, noun, in->n == 1 ? "" : "s");
    if (k > 0 && in->hidden && (size_t)k < cap) k += snprintf(buf + k, cap - (size_t)k, " (%td hidden)", in->hidden);
    if (k > 0 && in->avail >= 0 && (size_t)k < cap) ui_size(size, sizeof size, in->avail), k += snprintf(buf + k, cap - (size_t)k, ", %s free", size);
    if (k > 0 && in->changed > 0 && (size_t)k < cap) snprintf(buf + k, cap - (size_t)k, " \xC2\xB7 %td changed", in->changed);
}

// A file's date as the list shows it, in ofx.conf's date-format (F10): short (the time today, the
// day this year, the date before), long, iso, or its own strftime pattern. "?" when it doesn't fit.
static void ui_when(char *buf, size_t cap, i64 mtime, i64 now, DateFormat f, const char *pattern)
{
    time_t t = (time_t)mtime, n = (time_t)now;
    struct tm tm, tn;
    localtime_r(&t, &tm);
    localtime_r(&n, &tn);
    if (f != DATE_SHORT) {
        const char *p = f == DATE_LONG ? "%Y-%m-%d %H:%M" : f == DATE_ISO ? "%Y-%m-%dT%H:%M:%S" : pattern;
        if (!strftime(buf, cap, p, &tm)) snprintf(buf, cap, "?");
        return;
    }
    if (tm.tm_year == tn.tm_year && tm.tm_yday == tn.tm_yday) strftime(buf, cap, "%H:%M", &tm);
    else if (tm.tm_year == tn.tm_year) strftime(buf, cap, "%b %e", &tm);
    else strftime(buf, cap, "%Y-%m-%d", &tm);
}
