// The marks, and the text fields' one editor (UiLine): the edit line's, find's, the search field's
// and the panels' fields. Included by ui.c.

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

// Shift and a move in the files (Michael, 2026-10-26): every entry the cursor passed over, from
// where it was (from) to where it is, joins the selection, as a Shift+click does. -1: none
// pending. Returns -1, for the next event.
static isize ui_extend(Ui *u, isize from)
{
    if (from >= 0) ui_mark_range(u, from, u->list.sel);   // (ui_mark keeps to the list)
    return -1;
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
    ui_find_leave(u);
    u->focus = UI_FOCUS_EDIT;   // (the search field's keys too: they stayed before, 2026-10-28)
}

// The edit line opened on a name just made (a new folder or file), all of it selected, so typing
// replaces it (Michael, 2026-10-25).
static void ui_edit_open_all(Ui *u, Str text)
{
    ui_edit_start(u, text, false);
    u->anchor = 0;
}

// Where a file name's extension starts, as F2 selects the rest (Michael, 2026-10-25): at its last
// '.', or at ".tar" before it ("a.tar.gz" selects "a"); the length when there is none, or when the
// name only starts with a dot (".bashrc").
static int ui_name_stem(Str name)
{
    isize dot = name.len;
    while (dot > 0 && name.p[dot - 1] != '.') dot--;
    if (dot <= 1) return (int)name.len;
    dot--;
    if (dot > 4 && memcmp(name.p + dot - 4, ".tar", 4) == 0) dot -= 4;
    return (int)dot;
}

// F2's edit line: a file's name selected up to its extension, a folder's whole (as Nemo does).
static void ui_edit_rename(Ui *u, Str name, bool dir)
{
    ui_edit_start(u, name, false);
    u->anchor = 0;
    if (!dir) u->caret = ui_name_stem((Str){ u->edit, u->edit_len });
}

// The location line opened on a path, all of it selected, as browsers and Nemo do: typing a new one
// replaces it, a move of the caret ends the selection.
static void ui_location_open(Ui *u, Str path)
{
    ui_edit_start(u, path, true);
}

static void ui_edit_close(Ui *u)
{
    ui_unfocus(u, UI_FOCUS_EDIT);
}

// A line of text being edited (F5.2): the rename and location lines' (u->edit) and, since F9, the
// search field's. Its bytes and at most how many, the caret and the selection's other end (bytes),
// and whether a '/' may go in (the location line's paths).
typedef struct {
    char *b;
    int  *len, *caret, *anchor;
    int   max;
    bool  slash;
} UiLine;

static UiLine ui_edit_line(Ui *u)
{
    return (UiLine){ u->edit, &u->edit_len, &u->caret, &u->anchor, ui_edit_max(u), u->edit_path };
}

static UiLine ui_ow_line(Ui *u)
{
    return (UiLine){ u->ow_text, &u->ow_len, &u->ow_caret, &u->ow_anchor, UI_FIND_MAX, true };
}

static UiLine ui_bmp_line(Ui *u)
{
    return (UiLine){ u->bmp_text, &u->bmp_len, &u->bmp_caret, &u->bmp_anchor, (int)sizeof u->bmp_text - 1, true };
}

static UiLine ui_keys_line(Ui *u)
{
    return (UiLine){ u->keys_text, &u->keys_len, &u->keys_caret, &u->keys_anchor, (int)sizeof u->keys_text - 1, true };
}

static UiLine ui_find_line(Ui *u)
{
    return (UiLine){ u->find, &u->find_len, &u->find_caret, &u->find_anchor, UI_FIND_MAX, false };
}

static UiLine ui_search_line(Ui *u)
{
    return (UiLine){ u->search, &u->search_len, &u->search_caret, &u->search_anchor, UI_FIND_MAX, false };
}

// The selection, [*lo, *hi): empty when the anchor is at the caret.
static void ui_line_sel(UiLine l, int *lo, int *hi)
{
    *lo = *l.anchor < *l.caret ? *l.anchor : *l.caret;
    *hi = *l.anchor < *l.caret ? *l.caret : *l.anchor;
}

static void ui_edit_sel(const Ui *u, int *lo, int *hi)
{
    ui_line_sel(ui_edit_line((Ui *)u), lo, hi);
}

static int ui_line_back(UiLine l, int at)
{
    if (at <= 0) return 0;
    at--;
    while (at > 0 && ((unsigned char)l.b[at] & 0xC0) == 0x80) at--;
    return at;
}

static int ui_line_fwd(UiLine l, int at)
{
    if (at >= *l.len) return *l.len;
    at++;
    while (at < *l.len && ((unsigned char)l.b[at] & 0xC0) == 0x80) at++;
    return at;
}

// Bytes [from, to) removed, the caret where they were.
static void ui_line_cut(UiLine l, int from, int to)
{
    memmove(l.b + from, l.b + to, (size_t)(*l.len - to));
    *l.len -= to - from;
    *l.caret = *l.anchor = from;
}

// Bytes b[0..n) in place of the selection, the caret after them; false (nothing changed) when they
// don't fit.
static bool ui_line_put(UiLine l, const char *b, int n)
{
    int lo, hi;
    ui_line_sel(l, &lo, &hi);
    if (*l.len - (hi - lo) + n > l.max) return false;
    ui_line_cut(l, lo, hi);
    memmove(l.b + *l.caret + n, l.b + *l.caret, (size_t)(*l.len - *l.caret));
    memcpy(l.b + *l.caret, b, (size_t)n);
    *l.len += n, *l.caret += n, *l.anchor = *l.caret;
    return true;
}

// The selection onto u->copy for gui/'s clipboard; nothing selected: nothing.
static bool ui_line_copy(Ui *u, UiLine l)
{
    int lo, hi;
    ui_line_sel(l, &lo, &hi);
    if (lo == hi) return false;
    memcpy(u->copy, l.b + lo, (size_t)(hi - lo));
    u->copy_len = hi - lo, u->copied = true;
    return true;
}

// Ctrl+V: the clipboard's first line over the selection; without the characters a name can't
// hold ('/') unless the line takes them, and never a control character. What doesn't fit is left
// off, whole characters only.
static void ui_line_paste(UiLine l, Str text)
{
    char b[PATH_MAX];
    int n = 0, lo, hi;
    ui_line_sel(l, &lo, &hi);
    int room = l.max - (*l.len - (hi - lo));
    for (isize i = 0; i < text.len && text.p[i] != '\n' && text.p[i] != '\r';) {
        unsigned char c = (unsigned char)text.p[i];
        isize e = i + 1;
        while (e < text.len && ((unsigned char)text.p[e] & 0xC0) == 0x80) e++;
        bool drop = c < 0x20 || c == 0x7F || (c == '/' && !l.slash);
        if (n + (e - i) > room) break;   // a dropped one is a byte: none fits after it either
        if (!drop) memcpy(b + n, text.p + i, (size_t)(e - i)), n += (int)(e - i);
        i = e;
    }
    if (n > 0) ui_line_put(l, b, n);
}

// A word's edges for Ctrl and the arrows (2026-10-28): from at, back over what isn't a word (spaces,
// dots, dashes) and then over the word to its start (dir -1); or on over what isn't one and then the
// word to its end (1). A word is ui_word_byte's: letters, digits, '_', anything past ASCII.
static int ui_line_word(UiLine l, int at, int dir)
{
    if (dir < 0) {
        while (at > 0 && !ui_word_byte((unsigned char)l.b[at - 1])) at--;
        while (at > 0 && ui_word_byte((unsigned char)l.b[at - 1])) at--;
        return at;
    }
    while (at < *l.len && !ui_word_byte((unsigned char)l.b[at])) at++;
    while (at < *l.len && ui_word_byte((unsigned char)l.b[at])) at++;
    return at;
}

static bool ui_line_do(Ui *u, UiLine l, UiEvent e, Str paste)
{
    if (e.key == UK_NONE) {
        char b[4];
        int n = ui_utf8(e.ch, b);
        if (n > 0 && (e.ch != '/' || l.slash)) ui_line_put(l, b, n);
        return true;
    }
    int lo, hi;
    ui_line_sel(l, &lo, &hi);
    switch (e.key) {
    // A move ends the selection; Left and Right from one land at its ends.
    case UK_LEFT: *l.caret = *l.anchor = lo < hi ? lo : ui_line_back(l, *l.caret); return true;
    case UK_RIGHT: *l.caret = *l.anchor = lo < hi ? hi : ui_line_fwd(l, *l.caret); return true;
    case UK_WORD_LEFT: *l.caret = *l.anchor = ui_line_word(l, *l.caret, -1); return true;
    case UK_WORD_RIGHT: *l.caret = *l.anchor = ui_line_word(l, *l.caret, 1); return true;
    case UK_HOME: *l.caret = *l.anchor = 0; return true;
    case UK_END: *l.caret = *l.anchor = *l.len; return true;
    // With Shift the caret moves and the anchor stays.
    case UK_SEL_LEFT: *l.caret = ui_line_back(l, *l.caret); return true;
    case UK_SEL_RIGHT: *l.caret = ui_line_fwd(l, *l.caret); return true;
    case UK_SEL_WORD_LEFT: *l.caret = ui_line_word(l, *l.caret, -1); return true;
    case UK_SEL_WORD_RIGHT: *l.caret = ui_line_word(l, *l.caret, 1); return true;
    case UK_SEL_HOME: *l.caret = 0; return true;
    case UK_SEL_END: *l.caret = *l.len; return true;
    case UK_ALL: *l.anchor = 0, *l.caret = *l.len; return true;
    // Backspace and Delete take the selection, else a character (Ctrl: a word) that way.
    case UK_BACKSPACE: ui_line_cut(l, lo < hi ? lo : ui_line_back(l, *l.caret), lo < hi ? hi : *l.caret); return true;
    case UK_DELETE: ui_line_cut(l, lo < hi ? lo : *l.caret, lo < hi ? hi : ui_line_fwd(l, *l.caret)); return true;
    case UK_WORD_BACKSPACE: ui_line_cut(l, lo < hi ? lo : ui_line_word(l, *l.caret, -1), lo < hi ? hi : *l.caret); return true;
    case UK_WORD_DELETE: ui_line_cut(l, lo < hi ? lo : *l.caret, lo < hi ? hi : ui_line_word(l, *l.caret, 1)); return true;
    case UK_COPY: ui_line_copy(u, l); return true;
    case UK_CUT:
        if (ui_line_copy(u, l)) ui_line_cut(l, lo, hi);
        return true;
    case UK_PASTE: ui_line_paste(l, paste); return true;
    default: return false;
    }
}

// What a key did to a line: nothing (not its key), moved the caret or the selection, or changed the
// text (its owner filters or searches again on that).
typedef enum { UI_LINE_NOT, UI_LINE_MOVED, UI_LINE_CHANGED } UiLineHit;

// The keys that edit a line: text, the moves (with Shift the selection grows; with Ctrl a word at a
// time), Ctrl+A, Backspace, Delete, Ctrl+C, Ctrl+X, Ctrl+V. The caret and the anchor are put back
// inside the text first: its owner may have set a shorter one (a search's address, a bookmark's
// label).
static UiLineHit ui_line_key(Ui *u, UiLine l, UiEvent e, Str paste)
{
    *l.caret = (int)ui_clamp(*l.caret, 0, *l.len), *l.anchor = (int)ui_clamp(*l.anchor, 0, *l.len);
    char was[PATH_MAX];
    int was_len = *l.len;
    memcpy(was, l.b, (size_t)was_len);
    if (!ui_line_do(u, l, e, paste)) return UI_LINE_NOT;
    return *l.len != was_len || memcmp(was, l.b, (size_t)was_len) != 0 ? UI_LINE_CHANGED : UI_LINE_MOVED;
}

// The line that has the keys now, in ui_handle's order (the panel on top's field; none under a
// question or a panel without one): into *out when given; false when none has. gui/ reads the clipboard for a Ctrl+V only then.
static bool ui_line_active(Ui *u, UiLine *out)
{
    UiLine l;
    UiOnTop on = ui_on_top(u);
    if (on == UI_ON_OW && !u->ow_in_list) l = ui_ow_line(u);
    else if (on == UI_ON_BM && u->bmp_naming) l = ui_bmp_line(u);
    else if (on == UI_ON_KEYS) l = ui_keys_line(u);
    else if (on != UI_ON_NONE) return false;   // (a question, or a panel without a field)
    else if (u->focus == UI_FOCUS_EDIT) l = ui_edit_line(u);
    else if (u->focus == UI_FOCUS_SEARCH && u->in_search) l = ui_search_line(u);
    else if (u->focus == UI_FOCUS_FIND && u->finding) l = ui_find_line(u);
    else return false;
    if (out) *out = l;
    return true;
}

// A key or character in the edit line: an action for gui/ (UA_RENAME or UA_GO on Enter) or UA_NONE.
static UiAction ui_edit_event(Ui *u, UiEvent e, Str paste)
{
    if (ui_line_key(u, ui_edit_line(u), e, paste)) return (UiAction){ UA_NONE, 0, false };
    switch (e.key) {
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
