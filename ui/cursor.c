// The lists' cursor and scroll (the files, the sidebar, a panel's), rows and cells (F6.2), the
// sidebar's headings and modes, the view and the icon size. Included by ui.c.

static isize ui_clamp(isize v, isize lo, isize hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// Entries a row: 1 in a list.
static isize ui_cols(const UiList *l)
{
    return l->cols > 1 ? l->cols : 1;
}

// The scroll as far down as it goes: the first entry of the row that puts the last row at the bottom.
static isize ui_max_top(const UiList *l)
{
    isize rows = l->rows > 0 ? l->rows : 1, c = ui_cols(l), all = (l->n + c - 1) / c;
    return all > rows ? (all - rows) * c : 0;
}

// The scroll at top, in range. Its callers keep it on the first entry of a row: ui_scroll moves
// whole rows, and the rest call ui_show_sel after it, which puts it there.
static isize ui_top_at(const UiList *l, isize top)
{
    return ui_clamp(top, 0, ui_max_top(l));
}

// The cursor on screen: the scroll moves as little as it must, a row at a time.
static void ui_show_sel(UiList *l)
{
    isize rows = l->rows > 0 ? l->rows : 1, c = ui_cols(l), row = l->sel / c, top = l->top / c;
    if (row < top) top = row;
    if (row >= top + rows) top = row - rows + 1;
    l->top = ui_top_at(l, top * c);
}

// A new list (a folder entered, or the same one reloaded) of n entries, the cursor on sel. The
// scroll keeps its place where it can, so a reload doesn't jump.
static void ui_set(UiList *l, isize n, isize sel)
{
    l->n = n;
    l->sel = n > 0 ? ui_clamp(sel, 0, n - 1) : 0;
    l->top = ui_top_at(l, l->top);
    ui_show_sel(l);
}

// A new window height: rows fit.
static void ui_resize(UiList *l, int rows)
{
    l->rows = rows;
    l->top = ui_top_at(l, l->top);
    ui_show_sel(l);
}

// A new number of columns (the icon view's width changed, or the view itself: 1 for the list). The
// scroll keeps the row its first entry was on, the cursor on screen.
static void ui_set_cols(UiList *l, int cols)
{
    l->cols = cols;
    l->top = ui_top_at(l, l->top);
    ui_show_sel(l);
}

// New columns and rows after a zoom (F6.3): the cursor's cell stays on its row on screen where the
// new rows allow (its last row when there are fewer), not wherever the least scroll leaves it.
static void ui_refit(UiList *l, int cols, int rows)
{
    isize c = ui_cols(l), on = l->sel / c - l->top / c;
    l->cols = cols, l->rows = rows;
    c = ui_cols(l);
    l->top = (l->sel / c - on) * c;   // out of range here: ui_show_sel brings it in, the cursor on screen
    ui_show_sel(l);
}

// The wheel: notches rows up (positive) or down, the cursor left where it is.
static void ui_scroll(UiList *l, int notches)
{
    l->top = ui_top_at(l, l->top - (isize)notches * ui_cols(l));
}

static void ui_move(UiList *l, isize by)
{
    if (l->n == 0) return;
    l->sel = ui_clamp(l->sel + by, 0, l->n - 1);
    ui_show_sel(l);
}

// The icon view's cursor `rows` rows down (up when negative), in its column: past the first row it
// stays in the first; past the last, in the last row's cell under it, or on the last entry when
// that row ends before its column (as Nemo does).
static void ui_move_rows(UiList *l, isize rows)
{
    if (l->n == 0) return;
    isize c = ui_cols(l), col = l->sel % c, to = l->sel + rows * c;
    if (to < 0) to = col;
    if (to >= l->n) to = (l->n - 1) / c * c + col;
    if (to >= l->n) to = l->n - 1;
    l->sel = to;
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
           in->backs > 0 || in->forwards > 0 || in->job_x >= 0 || in->side_mode >= 0 || in->side_twisty >= 0 || in->side_eject >= 0 ||
           in->zoom_wheel != 0 || in->zoom_to >= 0 || in->menu_title >= 0 || in->menu_item >= 0 || in->menu_away || in->ctx_row >= 0 || in->ctx_here || in->ctx_side >= 0 || in->ctx_crumb >= 0 || in->ctx_tab >= 0 || in->prop_box >= 0 ||
           in->ow_row >= 0 || in->ow_drow >= 0 || in->ow_field || in->toggle > 0 || in->find_x || in->find_step || in->field >= 0 || in->inside_btn ||
           in->bmp_row >= 0 || in->bmp_drow >= 0 || in->bmp_btn || in->ow_wheel != 0;   // (a panel's wheel: Open With, Keyboard Shortcuts)
}

// Whether a status message gives way to the status line on input that acts (ui_input_acts): a
// plain one at once; an error only from a second after it came, so one that lands while keys are
// being pressed (a job failing) is seen (F6 review).
enum { UI_ERROR_MIN_MS = 1000 };

static bool ui_status_clears(bool bad, i64 at_ms, i64 now_ms)
{
    return !bad || now_ms - at_ms >= UI_ERROR_MIN_MS;
}

// The sidebar in mode m: a hidden one gives the keys back to the list.
static UiAction ui_side_mode(Ui *u, SideMode m)
{
    u->side_mode = m;
    if (m == SIDE_HIDDEN) ui_unfocus(u, UI_FOCUS_SIDE);
    return (UiAction){ UA_SIDE_MODE, m, false };
}

// The files' cursor one step (Michael, 2026-10-25): past either end it goes round, one step at a
// time only (PgUp, PgDn, Home and End stop at the ends). In the icon view by is a cell (Left,
// Right) or, rows true, a row (Up, Down): a row's move keeps its column, onto the last row that
// has it.
static void ui_step_round(UiList *l, isize by, bool rows)
{
    if (l->n == 0) return;
    isize c = rows ? ui_cols(l) : 1, col = l->sel % ui_cols(l), last_row = (l->n - 1) / ui_cols(l) * ui_cols(l);
    if (!rows) {
        l->sel = by < 0 ? (l->sel == 0 ? l->n - 1 : l->sel - 1) : (l->sel == l->n - 1 ? 0 : l->sel + 1);
    } else if (by < 0 && l->sel < c) {   // the top row: up to the bottom row's cell in this column
        l->sel = last_row + col < l->n ? last_row + col : last_row + col - c;
    } else if (by > 0 && l->sel >= last_row) {   // the bottom row: down to the top
        l->sel = col;
    } else {
        ui_move_rows(l, by);
        return;
    }
    ui_show_sel(l);
}

// A move of the list's cursor or the sidebar's.
static void ui_step(Ui *u, UiList *m, isize by)
{
    ui_move(m, by);
    if (m == &u->side) ui_side_settle(u, by < 0 ? -1 : 1);
}

// The level of px in UI_ZOOM_PX: the nearest (a state file written by hand may say any size).
static int ui_zoom_level(int px)
{
    if (px <= 0) px = UI_ZOOM_DEFAULT;
    int best = 0;
    for (int k = 1; k < UI_ZOOMS; k++)
        if (abs(UI_ZOOM_PX[k] - px) < abs(UI_ZOOM_PX[best] - px)) best = k;
    return best;
}

// The icon view's icon size now.
static int ui_zoom_px(const Ui *u)
{
    return UI_ZOOM_PX[ui_zoom_level(u->zoom)];
}

// The icon view's icons at level k (clamped); nothing in the list, or at the size it has.
static UiAction ui_zoom(Ui *u, int k)
{
    k = (int)ui_clamp(k, 0, UI_ZOOMS - 1);
    if (!u->grid || UI_ZOOM_PX[k] == ui_zoom_px(u)) return (UiAction){ UA_NONE, 0, false };
    u->zoom = UI_ZOOM_PX[k];
    return (UiAction){ UA_ZOOM, u->zoom, false };
}

// The view: icons in a grid, or the list. gui/ sets the columns at its next layout.
static UiAction ui_view(Ui *u, bool grid)
{
    u->grid = grid;
    if (!grid) ui_set_cols(&u->list, 1);
    return (UiAction){ UA_VIEW, grid, false };
}
