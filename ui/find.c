// The find bar: opening, stepping through the matches, leaving and closing it. Included by ui.c.

// ---- find -------------------------------------------------------------------------------------

// The first entry from `from` on (dir 1) or back (dir -1), wrapping, whose name matches m; -1.
static isize ui_find_at(const UiList *l, UiNames names, const UiMatcher *m, isize from, int dir)
{
    if (l->n == 0 || m->len == 0) return -1;
    for (isize k = 0; k < l->n; k++) {
        isize i = ((from + dir * k) % l->n + l->n) % l->n;
        if (ui_match(m, names.at(names.ctx, i))) return i;
    }
    return -1;
}

// Where the find began, inside the list: the folder may have been listed again with fewer entries
// since (find bar review).
static isize ui_find_back(const Ui *u)
{
    return ui_clamp(u->find_from, 0, u->list.n > 0 ? u->list.n - 1 : 0);
}

// The find line's text changed: the cursor goes to its first match from where the find began,
// or back there when the text is empty.
static void ui_find_update(Ui *u, UiNames names)
{
    Str text = { u->find.b, u->find.len };
    ui_matcher_set(&u->find_m, text, u->find_opts);
    isize i = ui_find_at(&u->list, names, &u->find_m, u->find_from, 1);
    u->find_miss = text.len > 0 && i < 0;
    u->list.sel = i >= 0 ? i : ui_find_back(u);
    ui_show_sel(&u->list);
}

// The find bar's next match after the cursor (dir 1) or before it (-1), round the list: Up and
// Down, and the chevrons.
static void ui_find_step(Ui *u, UiNames names, int dir)
{
    isize i = ui_find_at(&u->list, names, &u->find_m, u->list.sel + dir, dir);
    if (i >= 0) u->list.sel = i, ui_show_sel(&u->list);
}

// How many names the find bar's text matches, and *at the cursor's place among them from 1, 0 when
// it isn't on one: "2 of 5" (Michael, 2026-10-26). 0 without text.
static isize ui_find_count(const Ui *u, UiNames names, isize *at)
{
    *at = 0;
    if (!u->finding || u->find.len == 0 || u->find_m.len == 0) return 0;
    isize n = 0;
    for (isize i = 0; i < u->list.n; i++) {
        if (!ui_match(&u->find_m, names.at(names.ctx, i))) continue;
        n++;
        if (i == u->list.sel) *at = n;
    }
    return n;
}

// The keys off the bar, its text kept for n and N.
static void ui_find_keep(Ui *u)
{
    ui_unfocus(u, UI_FOCUS_FIND);
    if (u->find.len) memcpy(u->last, u->find.b, (size_t)u->find.len), u->last_len = u->find.len;
}

static void ui_find_close(Ui *u)
{
    u->finding = false, u->find_miss = false, u->find_select = false;
    ui_find_keep(u);
}

// The keys leave the find bar (Enter, a command, the menu, the edit line); it stays open, its text
// kept for n and N. The select mode's bar closes: it is there for one Enter.
static void ui_find_leave(Ui *u)
{
    if (u->find_select) {
        if (u->finding) ui_find_close(u);
        return;
    }
    ui_find_keep(u);
}

static void ui_mark(Ui *u, isize i, bool on);
static void ui_mark_clear(Ui *u);

// Select Items Matching's Enter (F7.4, as Nemo's): the selection becomes every name the text
// matches, and the bar closes; with no text it only closes. A regular expression that doesn't
// compile matches nothing: the selection stays and so does the bar, its reason in red (a review).
static void ui_find_select(Ui *u, UiNames names)
{
    if (u->find_m.bad && u->find.len > 0) return;
    if (u->find.len > 0) {
        ui_mark_clear(u);
        for (isize i = 0; i < u->list.n; i++)
            if (ui_match(&u->find_m, names.at(names.ctx, i))) ui_mark(u, i, true);
    }
    ui_find_close(u);
}

static void ui_find_open(Ui *u)
{
    u->finding = true, u->find_miss = false, u->find_select = false, UI_TEXT_CLEAR(u->find), u->find_from = u->list.sel;
    u->focus = UI_FOCUS_FIND;
    ui_matcher_set(&u->find_m, STR_EMPTY, u->find_opts);   // Up and Down in an empty bar find nothing (a review)
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

// n and N: the next or previous match of the last text, from the cursor.
static void ui_find_again(Ui *u, UiNames names, int dir)
{
    ui_matcher_set(&u->find_m, (Str){ u->last, u->last_len }, u->find_opts);
    ui_find_step(u, names, dir);
}
