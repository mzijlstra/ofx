// Matching a name against find's text: plain, whole word, case, a regular expression (F7.3). Included by ui.c.

// ---- matching (F7.3) ---------------------------------------------------------------------------

// Regular expressions run in C.UTF-8 (glibc's own, whatever the user's locale): `.` is a character,
// not a byte, and case folds past ASCII (é and É, Michael 2026-10-26). Only around regcomp and
// regexec, on this thread: the rest of ofx keeps the C locale. (locale_t)0 when there is none:
// then bytes, as before.
static locale_t ui_utf8_locale(void)
{
    static locale_t loc;
    static bool tried;
    if (!tried) tried = true, loc = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0);
    return loc;
}

static locale_t ui_utf8_on(void)
{
    return ui_utf8_locale() ? uselocale(ui_utf8_locale()) : (locale_t)0;
}

static void ui_utf8_off(locale_t was)
{
    if (was) uselocale(was);
}

// m to match text with o: a regular expression compiled (the one before freed); one that doesn't
// compile leaves m bad, with regerror's words.
static void ui_matcher_set(UiMatcher *m, Str text, UiMatchOpts o)
{
    if (m->re_on) regfree(&m->re);
    m->re_on = false, m->bad = false, m->err[0] = 0, m->o = o;
    m->len = (int)(text.len < UI_FIND_MAX ? text.len : UI_FIND_MAX);
    memcpy(m->text, text.p, (size_t)m->len);
    m->text[m->len] = 0;
    if (!o.regex || m->len == 0) return;
    locale_t was = ui_utf8_on();
    int rc = regcomp(&m->re, m->text, REG_EXTENDED | (o.cased ? 0 : REG_ICASE));
    ui_utf8_off(was);
    if (rc == 0) m->re_on = true;
    else m->bad = true, regerror(rc, NULL, m->err, sizeof m->err);
}

// A byte of a word, for whole words: a letter, a digit, '_', or part of a character past ASCII.
static bool ui_word_byte(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c >= 0x80;
}

// Whether hay[from, to) stands as a whole word: no word byte just before or just after.
static bool ui_whole(Str hay, isize from, isize to)
{
    return (from == 0 || !ui_word_byte((unsigned char)hay.p[from - 1])) && (to >= hay.len || !ui_word_byte((unsigned char)hay.p[to]));
}

// Whether hay matches m's expression (compiled, in C.UTF-8). With whole words: some span from a
// boundary to a boundary that it matches in full. The longest match from a start may run past a
// boundary a shorter one stops at (`-*-` in "--_ ", F7.3 review), so each span is
// tried: the leftmost-longest match within it is all of it exactly when the span matches. ^ and $
// keep meaning the name's start and end (NOTBOL, NOTEOL inside it). *at, *end: where it matched.
static bool ui_match_re(const UiMatcher *m, Str hay, isize *at, isize *end)
{
    regmatch_t all = { .rm_so = 0, .rm_eo = (regoff_t)hay.len };
    if (regexec(&m->re, hay.p ? hay.p : "", 1, &all, REG_STARTEND) != 0) return false;
    if (!m->o.word) return *at = all.rm_so, *end = all.rm_eo, true;
    for (isize from = 0; from <= hay.len; from++) {
        if (from > 0 && ui_word_byte((unsigned char)hay.p[from - 1])) continue;
        for (isize to = hay.len; to >= from; to--) {
            if (to < hay.len && ui_word_byte((unsigned char)hay.p[to])) continue;
            regmatch_t pm = { .rm_so = 0, .rm_eo = (regoff_t)(to - from) };
            int flags = REG_STARTEND | (from > 0 ? REG_NOTBOL : 0) | (to < hay.len ? REG_NOTEOL : 0);
            if (regexec(&m->re, hay.p ? hay.p + from : "", 1, &pm, flags) == 0 && pm.rm_so == 0 && pm.rm_eo == to - from) return *at = from, *end = to, true;
        }
    }
    return false;
}

// cp with its case folded as C.UTF-8 has it (towlower), so plain text folds é and É as a regular
// expression does (F7.3.1 review); ASCII as before.
static u32 ui_fold_cp(u32 cp)
{
    if (cp < 0x80) return (u32)fold((unsigned char)cp);
    locale_t l = ui_utf8_locale();
    return l ? (u32)towlower_l((wint_t)cp, l) : cp;
}

// Whether m's text, case folded, is at hay[i]: *end where it ends there (a folded letter can be
// another length in bytes).
static bool ui_folded_at(const UiMatcher *m, Str hay, isize i, isize *end)
{
    isize a = i, b = 0;
    while (b < m->len) {
        if (a >= hay.len) return false;
        u32 x = utf8_next(hay.p, hay.len, &a), y = utf8_next(m->text, m->len, &b);
        if (ui_fold_cp(x) != ui_fold_cp(y)) return false;
    }
    *end = a;
    return true;
}

// Whether hay matches m, the first match at [*from, *to) (a line searched inside files, F9 step 3,
// shows it). An empty text matches everything, there at 0; a bad expression nothing.
static bool ui_match_span(const UiMatcher *m, Str hay, isize *from, isize *to)
{
    if (m->len == 0) return *from = *to = 0, true;
    if (m->o.regex) {
        if (!m->re_on) return false;
        locale_t was = ui_utf8_on();
        bool hit = ui_match_re(m, hay, from, to);
        ui_utf8_off(was);
        return hit;
    }
    // Inside files (F9 step 3) this runs over every line read: a start is looked for first, by
    // memmem with case, and without by its first byte when that is ASCII (a byte past ASCII may still
    // fold to it, as K's Kelvin sign, so those go through the whole comparison).
    unsigned char first = (unsigned char)m->text[0];
    bool quick = first < 0x80;
    for (isize i = 0; i < hay.len; i++) {
        isize end = i;
        if (m->o.cased) {
            const char *p = memmem(hay.p + i, (size_t)(hay.len - i), m->text, (size_t)m->len);
            if (!p) break;
            i = p - hay.p, end = i + m->len;   // (at a character: m->text starts with one)
        } else {
            unsigned char c = (unsigned char)hay.p[i];
            if ((c & 0xC0) == 0x80) continue;   // a match starts at a character
            if (quick && c < 0x80 && fold(c) != fold(first)) continue;
            if (!ui_folded_at(m, hay, i, &end)) continue;
        }
        if (!m->o.word || ui_whole(hay, i, end)) return *from = i, *to = end, true;
    }
    return false;
}

static bool ui_match(const UiMatcher *m, Str hay)
{
    isize at, end;
    return ui_match_span(m, hay, &at, &end);
}

// Whether a search's text (F9) is a wildcard: a * or ? in it, the regular expression toggle off.
static bool ui_wild(const UiMatcher *m)
{
    return !m->o.regex && (memchr(m->text, '*', (size_t)m->len) || memchr(m->text, '?', (size_t)m->len));
}

// Whether a search (F9) matches name: a wildcard over the whole name, as a shell's (`*.ts`, case
// as the toggle says; whole words don't apply; a dot-file as any other, Show Hidden Files decides);
// any other text as find matches it.
static bool ui_search_match(const UiMatcher *m, Str name)
{
    if (!ui_wild(m)) return ui_match(m, name);
    char buf[NAME_MAX + 1];
    if (name.len > NAME_MAX) return false;
    memcpy(buf, name.p, (size_t)name.len), buf[name.len] = 0;
    locale_t was = ui_utf8_on();
    bool hit = fnmatch(m->text, buf, (m->o.cased ? 0 : FNM_CASEFOLD)) == 0;
    ui_utf8_off(was);
    return hit;
}

// A toggle flipped (UI_TOGGLE_*; 0 none).
static UiMatchOpts ui_toggle(UiMatchOpts o, int which)
{
    if (which == UI_TOGGLE_CASE) o.cased = !o.cased;
    if (which == UI_TOGGLE_WORD) o.word = !o.word;
    if (which == UI_TOGGLE_REGEX) o.regex = !o.regex;
    return o;
}

// Alt and the toggle's letter (c, w, r) as its number, 0 for another key.
static int ui_toggle_key(UiEvent e)
{
    if (e.key != UK_MENU) return 0;
    return e.ch == 'c' ? UI_TOGGLE_CASE : e.ch == 'w' ? UI_TOGGLE_WORD : e.ch == 'r' ? UI_TOGGLE_REGEX : 0;
}
