// F7.3's unit tests: the matcher (case, whole word, regular expression) and find's toggles; Open
// With's panel. Included by test.c.

static bool m_has(Str hay, const char *text, UiMatchOpts o)
{
    static UiMatcher m;
    ui_matcher_set(&m, str_from_c(text), o);
    return ui_match(&m, hay);
}

static void test_match(void)
{
    UiMatchOpts plain = { 0 }, cased = { .cased = true }, word = { .word = true }, re = { .regex = true };
    // Any part, case folded; as typed with the case toggle.
    CHECK(m_has(S("Visual Studio Code"), "studio", plain) && m_has(S("abc"), "BC", plain) && !m_has(S("ab"), "abc", plain));
    CHECK(!m_has(S("abd"), "abc", plain) && m_has(S("x"), "", plain) && m_has(STR_EMPTY, "", plain) && !m_has(STR_EMPTY, "a", plain));
    CHECK(m_has(S("Code"), "Co", cased) && !m_has(S("Code"), "co", cased) && !m_has(S("ab"), "B", cased));
    // Whole words: not inside another word, the next match tried; digits, '_' and UTF-8 are word bytes.
    CHECK(m_has(S("Text Editor"), "text", word) && m_has(S("my-text"), "text", word) && !m_has(S("Texts"), "text", word));
    CHECK(m_has(S("context text"), "text", word) && !m_has(S("a_text"), "text", word) && !m_has(S("text2"), "text", word));
    CHECK(!m_has(S("\xC3\xA9text"), "text", word) && m_has(S("(text)"), "text", word) && m_has(S("text"), "text", word));
    CHECK(!m_has(S("Texts Text"), "text", (UiMatchOpts){ .cased = true, .word = true }) && m_has(S("Texts text"), "text", (UiMatchOpts){ .cased = true, .word = true }));
    // Regular expressions: extended, folded unless cased; matched anywhere; whole words tried at
    // each match until one stands alone; ^ only at the start.
    CHECK(m_has(S("Firefox"), "^fire", re) && !m_has(S("a Firefox"), "^fire", re) && m_has(S("gimp"), "g(i|o)mp", re));
    CHECK(m_has(S("ab"), "B", re) && !m_has(S("ab"), "B", (UiMatchOpts){ .regex = true, .cased = true }));
    CHECK(m_has(S("textual text"), "te.t", (UiMatchOpts){ .regex = true, .word = true }));
    CHECK(!m_has(S("textual texts"), "te.t", (UiMatchOpts){ .regex = true, .word = true }));
    CHECK(!m_has(S("xxtext"), "^text", (UiMatchOpts){ .regex = true }));
    CHECK(!m_has(S("atext"), "text", (UiMatchOpts){ .regex = true, .word = true }));
    // The next try after a match that isn't a whole word starts one byte on (a whole match can begin
    // inside it), and ^ there is not the start.
    CHECK(m_has(S("a-b"), "a-|b", (UiMatchOpts){ .regex = true, .word = true }));
    CHECK(!m_has(S("-ab"), "^(-a|ab)", (UiMatchOpts){ .regex = true, .word = true }));
    // A shorter match from the same start that is whole, where the longest isn't (reviewer-2).
    CHECK(m_has(S("--_ "), "-*-", (UiMatchOpts){ .regex = true, .word = true }));
    CHECK(m_has(S("ab-cd"), "ab|ab-c", (UiMatchOpts){ .regex = true, .word = true }));
    CHECK(!m_has(S("abcd"), "ab|ab-c", (UiMatchOpts){ .regex = true, .word = true }));
    CHECK(m_has(S("x AB"), "ab", (UiMatchOpts){ .regex = true, .word = true }) && !m_has(S("x AB"), "ab", (UiMatchOpts){ .regex = true, .word = true, .cased = true }));
    CHECK(m_has(S("ab"), "", (UiMatchOpts){ .regex = true, .word = true }));
    // ^ and $ are the name's ends, not a word's.
    UiMatchOpts rw = { .regex = true, .word = true };
    CHECK(!m_has(S("text more"), "text$", rw) && m_has(S("more text"), "text$", rw) && !m_has(S("a text"), "^text", rw));
    CHECK(m_has(S("text a"), "^text", rw) && m_has(S("a-b c"), "b c$", rw) && !m_has(S("a-b cd"), "b c$", rw));
    CHECK(!m_has(S("text atext"), "text$", rw));   // $ matches at the end only, where "text" isn't whole
    CHECK(m_has(S("-"), "x*", rw) && !m_has(S("ab"), "x*", rw));   // an empty span between two boundaries
    // A match only past the end of what was given (the name is not NUL-terminated there).
    CHECK(!m_has((Str){ "abcdef", 3 }, "def", re) && !m_has((Str){ "abcdef", 3 }, "c$x", re) && m_has((Str){ "abcdef", 3 }, "c$", re));
    // Past ASCII (C.UTF-8): `.` is one character, case folds é and É; a name that isn't UTF-8
    // goes through without harm (Michael, 2026-10-26).
    CHECK(m_has(S("caf\xC3\xA9"), "^caf.$", re) && m_has(S("\xC3\xA9"), "^.$", re) && !m_has(S("\xC3\xA9"), "^..$", re));
    CHECK(m_has(S("\xC3\x89T\xC3\x89"), "\xC3\xA9t\xC3\xA9", re) && !m_has(S("\xC3\x89T\xC3\x89"), "\xC3\xA9t\xC3\xA9", (UiMatchOpts){ .regex = true, .cased = true }));
    CHECK(m_has(S("x \xC3\xA9t\xC3\xA9"), ".t.", (UiMatchOpts){ .regex = true, .word = true }));
    CHECK(m_has(S("a\xFF\xFEz"), "z$", re) && !m_has(S("\xFF"), "^a", re));
    m_has(S("a\xFF" "b"), "a.b", re), m_has(S("\xC3"), ".", (UiMatchOpts){ .regex = true, .word = true });   // (no crash either way)
    // Plain text folds past ASCII too (reviewer-2): é and É; not with the case toggle; whole words;
    // never from inside a character; a byte that isn't UTF-8 still matches itself.
    CHECK(m_has(S("CAF\xC3\x89"), "caf\xC3\xA9", plain) && !m_has(S("CAF\xC3\x89"), "caf\xC3\xA9", cased));
    CHECK(m_has(S("x \xC3\x89t\xC3\xA9 y"), "\xC3\xA9T\xC3\x89", word) && !m_has(S("\xC3\x89t\xC3\xA9s"), "\xC3\xA9t\xC3\xA9", word));
    CHECK(!m_has(S("\xC3\xA9"), "\xA9", plain));   // never from inside a character
    CHECK(m_has(S("a\xFF" "b"), "\xFF" "B", plain) && m_has(S("ab"), "b", plain) && !m_has(S("a"), "ab", plain));
    CHECK(!m_has((Str){ "abcdef", 3 }, "cd", plain));   // not past the name's end
    // A letter that folds to fewer bytes (K, the Kelvin sign, 3 bytes, is k): the word ends after it.
    CHECK(m_has(S("\xE2\x84\xAA-"), "k", word) && m_has(S("\xE2\x84\xAA"), "K", plain));
    // A bad expression: nothing matches, and the reason; the next good one is compiled again.
    static UiMatcher m;
    ui_matcher_set(&m, S("a(b"), re);
    CHECK(m.bad && m.err[0] && !m.re_on && !ui_match(&m, S("a(b")));
    ui_matcher_set(&m, S("a(b"), plain);
    CHECK(!m.bad && m.err[0] == 0 && ui_match(&m, S("xa(b")));
    ui_matcher_set(&m, S("a.c"), re);
    CHECK(m.re_on && ui_match(&m, S("abc")));
    ui_matcher_set(&m, STR_EMPTY, re);
    CHECK(!m.re_on && ui_match(&m, S("anything")));
    // A text longer than the line holds is cut there.
    char longer[UI_FIND_MAX + 20];
    memset(longer, 'a', sizeof longer);
    ui_matcher_set(&m, (Str){ longer, (isize)sizeof longer }, plain);
    CHECK(m.len == UI_FIND_MAX && m.text[UI_FIND_MAX] == 0);
    // The toggles and their keys.
    UiMatchOpts t = ui_toggle(plain, 1);
    CHECK(t.cased && !t.word && !t.regex);
    t = ui_toggle(ui_toggle(t, 2), 3);
    CHECK(t.cased && t.word && t.regex && !ui_toggle(t, 1).cased && !ui_toggle(t, 2).word && !ui_toggle(t, 3).regex);
    CHECK(ui_toggle(plain, 0).cased == false && ui_toggle(plain, 4).regex == false);
    CHECK(ui_toggle_key((UiEvent){ UK_MENU, 'c' }) == 1 && ui_toggle_key((UiEvent){ UK_MENU, 'w' }) == 2 && ui_toggle_key((UiEvent){ UK_MENU, 'r' }) == 3);
    CHECK(ui_toggle_key((UiEvent){ UK_MENU, 'f' }) == 0 && ui_toggle_key((UiEvent){ UK_NONE, 'c' }) == 0);
}

// Find with the toggles: Alt+C, Alt+W, Alt+R in the line, kept for n and for the next find.
static void test_find_toggles(void)
{
    Ui u = { 0 };
    UiNames names = { name_at, NULL, NULL };
    ui_resize(&u.list, 10);
    ui_set(&u.list, COUNT(find_names), 0);   // alpha Beta gamma.txt delta BETAMAX omega café
    UiInput in = chars("/beta");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1);
    in = ui_input_none();
    in.ev[in.nev++] = (UiEvent){ UK_MENU, 'c' };   // case: "beta" is in no name now
    ui_handle(&u, names, &in);
    CHECK(u.finding && u.find_opts.cased && u.find_miss && u.list.sel == 0 && !u.menu_on);
    in = ui_input_none();
    in.ev[in.nev++] = (UiEvent){ UK_MENU, 'c' };
    in.ev[in.nev++] = (UiEvent){ UK_MENU, 'w' };   // whole word: Beta, not BETAMAX
    ui_handle(&u, names, &in);
    CHECK(!u.find_opts.cased && u.find_opts.word && !u.find_miss && u.list.sel == 1);
    in = keys(1, UK_ENTER);
    ui_handle(&u, names, &in);
    in = chars("n");   // whole words for n too: Beta again, never BETAMAX
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1);
    // A regular expression, kept from the last find; a bad one matches nothing and says why.
    in = chars("/");
    ui_handle(&u, names, &in);
    in = ui_input_none();
    in.ev[in.nev++] = (UiEvent){ UK_MENU, 'w' };
    in.ev[in.nev++] = (UiEvent){ UK_MENU, 'r' };
    ui_handle(&u, names, &in);
    CHECK(u.find_opts.regex && !u.find_opts.word && u.finding);
    in = chars("^g.*txt$");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 2 && !u.find_miss);
    in = keys(1, UK_ESC);
    ui_handle(&u, names, &in);
    in = chars("/(");
    ui_handle(&u, names, &in);
    CHECK(u.find_miss && u.find_m.bad && u.find_m.err[0]);
    in = keys(1, UK_ESC);
    ui_handle(&u, names, &in);
    CHECK(!u.finding);
    // Alt+F still opens the File menu from the find line (it closes the line).
    in = chars("/a");
    ui_handle(&u, names, &in);
    in = ui_input_none();
    in.ev[in.nev++] = (UiEvent){ UK_MENU, 'f' };
    ui_handle(&u, names, &in);
    CHECK(u.finding && u.focus != UI_FOCUS_FIND && u.menu_on);
    // n after a find closed empty: the last text found, not the empty one.
    ui_matcher_set(&u.find_m, STR_EMPTY, (UiMatchOpts){ 0 });   // (regfree)
    u = (Ui){ 0 };
    ui_resize(&u.list, 10);
    ui_set(&u.list, COUNT(find_names), 0);
    in = chars("/gam");
    ui_handle(&u, names, &in);
    in = keys(1, UK_ENTER);
    ui_handle(&u, names, &in);
    u.list.sel = 0;
    in = chars("/x");
    ui_handle(&u, names, &in);
    in = keys(2, UK_BACKSPACE, UK_ESC);
    ui_handle(&u, names, &in);
    in = chars("n");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 2);
    // Enter with entries marked: all of them (F7.3); without, the cursor's only.
    u.mark = (u8[COUNT(find_names)]){ 0 };
    in = keys(1, UK_ENTER);
    CHECK(!ui_handle(&u, names, &in).all);
    ui_mark(&u, 1, true);
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_OPEN && a.all && a.index == 2);
    ui_matcher_set(&u.find_m, STR_EMPTY, (UiMatchOpts){ 0 });   // (regfree)
}

// The section and place of the app with id in apps, "-" when it isn't listed.
static const char *rank_of(const Apps *apps, const char *id, isize *place)
{
    static const char *const SEC[] = { "default", "recommended", "related", "other", "none" };
    for (isize i = 0; i < apps->n; i++)
        if (str_eq(apps->a[i].id, str_from_c(id))) return *place = i, SEC[apps->a[i].section];
    return *place = -1, "-";
}

static void app73(const char *rel, const char *text)
{
    put(at("f73apps/%s", rel), text);   // (put makes the folders)
}

static void test_apps_rank(void)
{
    arena_reset(&ta);
    env_set("XDG_CONFIG_HOME", at("f73apps/cfg"));
    env_set("XDG_CONFIG_DIRS", at("f73apps/etc"));
    env_set("XDG_DATA_HOME", at("f73apps/home"));
    env_set("XDG_DATA_DIRS", at("f73apps/sys"));
    env_set("XDG_CURRENT_DESKTOP", "");
    app73("sys/applications/edit.desktop", "[Desktop Entry]\nType=Application\nName=Editor\nIcon=system-one\nMimeType=text/plain;\nExec=e %F\n");
    app73("sys/applications/code.desktop", "[Desktop Entry]\nType=Application\nName=code\nMimeType=text/plain;inode/directory;\nExec=c %F\n");
    app73("sys/applications/md.desktop", "[Desktop Entry]\nType=Application\nName=Markdown\nMimeType=text/markdown;\nExec=m %f\n");
    app73("sys/applications/helper.desktop", "[Desktop Entry]\nType=Application\nName=Helper\nNoDisplay=true\nMimeType=text/markdown;\nExec=h\n");
    app73("sys/applications/hidden.desktop", "[Desktop Entry]\nType=Application\nName=Hidden helper\nNoDisplay=true\nExec=h\n");
    app73("sys/applications/added.desktop", "[Desktop Entry]\nType=Application\nName=added\nExec=a\n");
    app73("sys/applications/gimp.desktop", "[Desktop Entry]\nType=Application\nMimeType=image/png;\nExec=g\n");   // no Name: its id
    app73("sys/applications/zed.desktop", "[Desktop Entry]\nType=Application\nName=Zed\nMimeType=text/x-old;\nExec=z\n");
    app73("sys/applications/nope.desktop", "[Desktop Entry]\nType=Application\nName=Nope\nMimeType=text/markdown;\nExec=n\n");
    app73("sys/applications/kde/foo.desktop", "[Desktop Entry]\nType=Application\nName=Foo\nExec=f\n");
    app73("sys/applications/link.desktop", "[Desktop Entry]\nType=Link\nName=Link\n");
    app73("sys/applications/notes.txt", "[Desktop Entry]\nType=Application\nName=Not an app\n");
    app73("sys/applications/.hid.desktop", "[Desktop Entry]\nType=Application\nName=Dot\n");
    app73("sys/applications/a/b/c/d/deep.desktop", "[Desktop Entry]\nType=Application\nName=Deep\n");   // past the depth
    app73("home/applications/edit.desktop", "[Desktop Entry]\nType=Application\nName=My editor\nIcon=accessories-text-editor\nMimeType=text/plain;text/markdown;\nExec=e %F\n");
    app73("home/applications/added.desktop", "[Desktop Entry]\nType=Application\nName=added\nHidden=true\n");   // deleted: the system's isn't there
    app73("sys/applications/plus.desktop", "[Desktop Entry]\nType=Application\nName=Plus\nExec=p\n");
    app73("sys/mime/subclasses", "text/markdown text/plain\ntext/x-kid text/markdown\n");
    app73("sys/mime/aliases", "text/x-old text/markdown\n");
    app73("cfg/mimeapps.list", "[Default Applications]\ntext/markdown=md.desktop\n[Added Associations]\ntext/markdown=plus.desktop;\n"
                                  "[Removed Associations]\ntext/markdown=nope.desktop;\n");
    Apps apps;
    apps_all(&ta, &apps);
    isize at_k;
    CHECK(apps.n == 10 && !apps.cut);   // edit (the user's), code, md, helper, hidden, gimp, zed, nope, kde-foo, plus
    CHECK(strcmp(rank_of(&apps, "added.desktop", &at_k), "-") == 0 && strcmp(rank_of(&apps, "link.desktop", &at_k), "-") == 0);
    CHECK(strcmp(rank_of(&apps, ".hid.desktop", &at_k), "-") == 0 && strcmp(rank_of(&apps, "a-b-c-d-deep.desktop", &at_k), "-") == 0);
    CHECK(strcmp(rank_of(&apps, "kde-foo.desktop", &at_k), "-") != 0 && strcmp(rank_of(&apps, "a-b-c-d-deep.desktop", &at_k), "-") == 0);
    isize shown = apps_rank(&ta, &apps, S("text/markdown"));
    CHECK(shown == 9);   // all but the hidden helper
    struct { const char *id, *sec; isize place; } want[] = {
        { "md.desktop", "default", 0 },          // the list's default
        { "helper.desktop", "recommended", 1 },  // NoDisplay, but it names the type; names folded: Helper < My editor
        { "edit.desktop", "recommended", 2 },    // the user's copy (My editor), not the system's
        { "plus.desktop", "recommended", 3 },    // added by a list
        { "zed.desktop", "recommended", 4 },     // by the type's alias
        { "code.desktop", "related", 5 },        // text/plain, a parent's (GTK's "Related")
        { "kde-foo.desktop", "other", 6 },       // Foo
        { "gimp.desktop", "other", 7 },          // no Name: by its id
        { "nope.desktop", "other", 8 },          // removed by a list
        { "hidden.desktop", "none", 9 },        // NoDisplay and not for it: not shown, last
    };
    for (int k = 0; k < COUNT(want); k++) {
        const char *sec = rank_of(&apps, want[k].id, &at_k);
        CHECK(strcmp(sec, want[k].sec) == 0 && at_k == want[k].place);
        if (strcmp(sec, want[k].sec) != 0 || at_k != want[k].place) fprintf(stderr, "  %s: %s at %td\n", want[k].id, sec, at_k);
    }
    CHECK(str_eq(apps.a[2].name, S("My editor")) && str_eq(apps.a[2].icon, S("accessories-text-editor")) && str_eq(apps.a[7].name, S("gimp.desktop")));
    // A type under text/markdown: the parent's default is its default; the apps for its parents are
    // related, a NoDisplay one only for a parent not shown at all.
    shown = apps_rank(&ta, &apps, S("text/x-kid"));
    CHECK(strcmp(rank_of(&apps, "md.desktop", &at_k), "default") == 0 && strcmp(rank_of(&apps, "edit.desktop", &at_k), "related") == 0);
    CHECK(strcmp(rank_of(&apps, "code.desktop", &at_k), "related") == 0 && strcmp(rank_of(&apps, "plus.desktop", &at_k), "related") == 0);
    CHECK(strcmp(rank_of(&apps, "helper.desktop", &at_k), "none") == 0 && strcmp(rank_of(&apps, "nope.desktop", &at_k), "other") == 0);
    CHECK(shown == 8);
    // A folder: who names inode/directory; no default.
    apps_rank(&ta, &apps, S("inode/directory"));
    CHECK(strcmp(rank_of(&apps, "code.desktop", &at_k), "recommended") == 0 && at_k == 0);
    CHECK(strcmp(rank_of(&apps, "md.desktop", &at_k), "other") == 0 && strcmp(rank_of(&apps, "helper.desktop", &at_k), "none") == 0);
    env_restore();
}

// ---- Open With's panel ------------------------------------------------------------------------

static const Str OW_APPS[] = { S("Neovim"), S("LibreOffice Writer"), S("Omawrite"), S("Chromium"), S("GNU Image Manipulation Program"),
                               S("imv"), S("Visual Studio Code"), S("Text Editor"), S("caf\xC3\xA9 app") };

// One frame of events (as menu_keys: 0x100 + c a character, -c Alt and the letter) with the apps.
static UiAction ow_keys(Ui *u, int n, ...)
{
    UiNames names = { name_at, NULL, NULL };
    UiInput in = ui_input_none();
    in.app_name = OW_APPS, in.app_n = COUNT(OW_APPS);
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        int k = va_arg(ap, int);
        in.ev[in.nev++] = k >= 0x100 ? (UiEvent){ UK_NONE, (u32)(k - 0x100) } : k < 0 ? (UiEvent){ UK_MENU, (u32)-k } : (UiEvent){ (UiKey)k, 0 };
    }
    va_end(ap);
    return ui_handle(u, names, &in);
}

static UiAction ow_type(Ui *u, const char *s)
{
    UiAction a = { 0 };
    for (; *s; s++) a = ow_keys(u, 1, 0x100 + (unsigned char)*s);
    return a;
}

static void test_ow_keys(void)
{
    // Opened from the menu: gui/ lists the apps; the next frame filters them, the field has the keys.
    Ui u = menu_ui(3);
    u.ow_list.rows = 4;
    ow_keys(&u, 1, UK_F10);
    u.menu_at = file_at(UM_OPEN_WITH);
    CHECK(ow_keys(&u, 1, UK_ENTER).kind == UA_OPEN_WITH && u.ow && !u.ow_in_list && u.ow_fresh && !u.menu_on);
    ow_keys(&u, 0);
    CHECK(!u.ow_fresh && u.ow_list.n == COUNT(OW_APPS) && u.ow_list.sel == 0 && u.ow_list.rows == 4);
    // Typing filters (any part, folded); j and k are text in the field; Enter opens the first.
    ow_type(&u, "o");
    CHECK(u.ow_filter.len == 1 && u.ow_list.n == 7);   // Neovim, LibreOffice, Omawrite, Chromium, Program, Studio Code, Editor
    ow_type(&u, "ff");
    CHECK(u.ow_list.n == 1 && u.ow_idx[0] == 1);
    ow_keys(&u, 2, UK_BACKSPACE, UK_BACKSPACE);
    ow_type(&u, "jk");
    CHECK(u.ow_filter.len == 3 && u.ow_list.n == 0 && !u.ow_in_list);
    CHECK(ow_keys(&u, 2, UK_DOWN, UK_ENTER).kind == UA_NONE && u.ow && !u.ow_in_list);   // nothing to go to, nothing to open
    ow_keys(&u, 3, UK_BACKSPACE, UK_BACKSPACE, UK_BACKSPACE);
    CHECK(u.ow_filter.len == 0 && u.ow_list.n == COUNT(OW_APPS));
    // The caret: Left, Right, Home, End by code point; Delete; a character goes in at the caret.
    ow_type(&u, "caf");
    ow_keys(&u, 1, 0x100 + 0xE9);   // é, 2 bytes
    CHECK(u.ow_filter.len == 5 && u.ow_filter.caret == 5 && u.ow_list.n == 1 && u.ow_idx[0] == 8);
    ow_keys(&u, 1, UK_LEFT);
    CHECK(u.ow_filter.caret == 3);
    ow_keys(&u, 1, UK_RIGHT);
    CHECK(u.ow_filter.caret == 5);
    ow_keys(&u, 2, UK_HOME, UK_DELETE);
    CHECK(u.ow_filter.len == 4 && u.ow_filter.caret == 0 && memcmp(u.ow_filter.b, "af\xC3\xA9", 4) == 0);
    ow_keys(&u, 1, 0x100 + 'C');
    CHECK(u.ow_filter.len == 5 && u.ow_filter.caret == 1 && u.ow_filter.b[0] == 'C' && u.ow_list.n == 1);
    ow_keys(&u, 3, UK_END, UK_BACKSPACE, UK_LEFT);
    CHECK(u.ow_filter.len == 3 && u.ow_filter.caret == 2);
    ow_keys(&u, 2, UK_DELETE, UK_DELETE);   // the last; then nothing after the caret
    CHECK(u.ow_filter.len == 2 && u.ow_filter.caret == 2);
    ow_keys(&u, 3, UK_HOME, UK_LEFT, UK_BACKSPACE);   // nothing before the caret
    CHECK(u.ow_filter.len == 2 && u.ow_filter.caret == 0);
    ow_keys(&u, 2, UK_END, UK_RIGHT);
    CHECK(u.ow_filter.caret == 2);
    ow_keys(&u, 2, UK_BACKSPACE, UK_BACKSPACE);
    // The toggles: Alt+C, Alt+W, Alt+R; a bad expression shows nothing.
    ow_type(&u, "code");
    CHECK(u.ow_list.n == 1);
    ow_keys(&u, 1, -'c');
    CHECK(u.ow_opts.cased && u.ow_list.n == 0 && u.ow && !u.menu_on);
    ow_keys(&u, 2, -'c', -'w');
    CHECK(u.ow_opts.word && u.ow_list.n == 1);
    ow_keys(&u, 4, UK_BACKSPACE, UK_BACKSPACE, UK_BACKSPACE, UK_BACKSPACE);
    ow_keys(&u, 2, -'w', -'r');
    ow_type(&u, "^(neo|imv)");
    CHECK(u.ow_opts.regex && u.ow_list.n == 2 && u.ow_idx[1] == 5);
    ow_type(&u, "(");
    CHECK(u.ow_list.n == 0 && u.ow_m.bad);
    ow_keys(&u, 1, UK_BACKSPACE);
    CHECK(u.ow_m.re_on);
    // Closed with an expression compiled: freed; opened again as it was, the toggle kept.
    ui_ow_close(&u);
    CHECK(!u.ow_m.re_on && !u.ow);
    ui_ow_open(&u);
    ow_keys(&u, 0);
    ow_type(&u, "^(neo|imv)(");
    CHECK(u.ow_opts.regex && u.ow_m.bad);
    for (int k = 0; k < 11; k++) ow_keys(&u, 1, UK_BACKSPACE);
    ow_keys(&u, 1, -'r');
    CHECK(u.ow_filter.len == 0 && !u.ow_opts.regex && u.ow_list.n == COUNT(OW_APPS));
    // The list: Down from the field, j, k, Up on the first row back to the field; Tab both ways.
    ow_keys(&u, 1, UK_DOWN);
    CHECK(u.ow_in_list && u.ow_list.sel == 0);
    ow_keys(&u, 2, 0x100 + 'j', UK_DOWN);
    CHECK(u.ow_list.sel == 2 && u.ow_filter.len == 0);
    ow_keys(&u, 1, 0x100 + 'k');
    CHECK(u.ow_list.sel == 1);
    ow_keys(&u, 2, UK_UP, UK_UP);
    CHECK(!u.ow_in_list && u.ow_list.sel == 0);
    ow_keys(&u, 1, UK_TAB);
    CHECK(u.ow_in_list);
    ow_keys(&u, 1, UK_END);
    CHECK(u.ow_list.sel == COUNT(OW_APPS) - 1 && u.ow_list.top == COUNT(OW_APPS) - 4);
    ow_keys(&u, 1, UK_DOWN);
    CHECK(u.ow_list.sel == COUNT(OW_APPS) - 1);   // stops at the end
    ow_keys(&u, 1, UK_PAGEUP);
    CHECK(u.ow_list.sel == COUNT(OW_APPS) - 4);
    ow_keys(&u, 1, UK_HOME);
    CHECK(u.ow_list.sel == 0 && u.ow_list.top == 0);
    ow_keys(&u, 1, UK_PAGEDOWN);
    CHECK(u.ow_list.sel == 3);
    ow_keys(&u, 2, UK_LEFT, UK_TAB);   // Left does nothing in the list
    CHECK(!u.ow_in_list && u.ow_list.sel == 3);
    // Another letter in the list goes to the field, typed at its end; Backspace too.
    ow_keys(&u, 2, UK_TAB, 0x100 + 'g');
    CHECK(!u.ow_in_list && u.ow_filter.len == 1 && u.ow_list.n == 1 && u.ow_idx[0] == 4);
    ow_keys(&u, 3, UK_HOME, UK_TAB, 0x100 + 'n');
    CHECK(u.ow_filter.len == 2 && u.ow_filter.caret == 2 && memcmp(u.ow_filter.b, "gn", 2) == 0 && u.ow_list.n == 1);
    ow_keys(&u, 2, UK_TAB, UK_BACKSPACE);
    CHECK(!u.ow_in_list && u.ow_filter.len == 1 && u.ow_list.n == 1);
    ow_keys(&u, 2, UK_TAB, 0x100 + 0x1F);   // a control character: nothing
    CHECK(u.ow_in_list && u.ow_filter.len == 1);
    ow_keys(&u, 2, UK_BACKSPACE, UK_TAB);
    CHECK(u.ow_filter.len == 0 && u.ow_in_list);
    ow_keys(&u, 1, UK_BACKSPACE);   // empty: nothing to erase, to the field
    CHECK(!u.ow_in_list && u.ow_filter.len == 0);
    // Enter: the app under the cursor, the panel closed; from the list or the field.
    ow_keys(&u, 3, UK_DOWN, UK_DOWN, UK_DOWN);
    UiAction a = ow_keys(&u, 1, UK_ENTER);
    CHECK(a.kind == UA_LAUNCH && a.index == 2 && !u.ow);
    // Other keys are the panel's: t starts no terminal, Ctrl+I opens no Properties; Esc closes.
    ui_ow_open(&u);
    ow_keys(&u, 0);
    CHECK(ow_keys(&u, 1, UK_TERMINAL).kind == UA_NONE && ow_keys(&u, 1, UK_PROPERTIES).kind == UA_NONE && u.ow && !u.props);
    CHECK(ow_keys(&u, 1, 0x100 + 't').kind == UA_NONE && u.ow_filter.len == 1);
    CHECK(ow_keys(&u, 1, UK_ESC).kind == UA_NONE && !u.ow);
    // Opened again: an empty field, the toggles kept.
    u.ow_opts.word = true;
    ui_ow_open(&u);
    CHECK(u.ow_filter.len == 0 && u.ow_filter.caret == 0 && u.ow_opts.word && u.ow_list.n == 0 && u.ow_fresh);
    u.ow_opts.word = false;
    ui_ow_close(&u);
    // A full field drops what doesn't fit.
    ui_ow_open(&u);
    ow_keys(&u, 0);
    for (int k = 0; k < UI_FIND_MAX + 3; k++) ui_line_key(&u, ui_ow_line(&u), (UiEvent){ UK_NONE, 'x' }, STR_EMPTY);
    CHECK(u.ow_filter.len == UI_FIND_MAX);
    ui_line_key(&u, ui_ow_line(&u), (UiEvent){ UK_NONE, 0xE9 }, STR_EMPTY);
    CHECK(u.ow_filter.len == UI_FIND_MAX);
    ui_ow_close(&u);
}

static void test_ow_mouse(void)
{
    UiNames names = { name_at, NULL, NULL };
    Ui u = menu_ui(3);
    u.ow_list.rows = 4;
    ui_ow_open(&u);
    ow_keys(&u, 0);
    // A row takes the cursor and the keys; one past the list nothing; a double click opens.
    UiInput in = ui_input_none();
    in.app_name = OW_APPS, in.app_n = COUNT(OW_APPS);
    in.ow_row = 2;
    CHECK(ui_input_acts(&in) && ui_handle(&u, names, &in).kind == UA_NONE && u.ow_list.sel == 2 && u.ow_in_list);
    in.ow_row = 40;
    ui_handle(&u, names, &in);
    CHECK(u.ow_list.sel == 2);
    // The wheel over it scrolls it, the cursor kept; the field takes the keys.
    in = ui_input_none(), in.app_name = OW_APPS, in.app_n = COUNT(OW_APPS);
    in.ow_wheel = -2, in.ow_field = true;
    CHECK(ui_input_acts(&in));
    ui_handle(&u, names, &in);
    CHECK(u.ow_list.top == 2 && u.ow_list.sel == 2 && !u.ow_in_list && u.list.top == 0);
    in = ui_input_none(), in.app_name = OW_APPS, in.app_n = COUNT(OW_APPS);
    in.ow_drow = 1;   // the row on screen: the list's 3
    CHECK(ui_input_acts(&in));
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_LAUNCH && a.index == 3 && !u.ow);
    // A toggle; Open; Cancel.
    ui_ow_open(&u);
    ow_keys(&u, 0);
    in = ui_input_none(), in.app_name = OW_APPS, in.app_n = COUNT(OW_APPS);
    in.toggle = 2;
    CHECK(ui_input_acts(&in));
    ui_handle(&u, names, &in);
    CHECK(u.ow_opts.word && u.ow);
    in.toggle = 2;
    ui_handle(&u, names, &in);
    in.toggle = 9;   // none such
    ui_handle(&u, names, &in);
    CHECK(!u.ow_opts.word && !u.ow_opts.cased && !u.ow_opts.regex);
    in = ui_input_none(), in.app_name = OW_APPS, in.app_n = COUNT(OW_APPS);
    in.modal = UI_MODAL_YES;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_LAUNCH && a.index == 0 && !u.ow);
    ui_ow_open(&u);
    in.modal = UI_MODAL_NO;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.ow);
    // Find's toggles by the pointer, while the find line is open; none while it isn't.
    UiInput f = chars("/beta");
    ui_set(&u.list, COUNT(find_names), 0);
    ui_handle(&u, names, &f);
    f = ui_input_none(), f.field = UI_FIELD_FIND, f.field_toggle = 1;
    ui_handle(&u, names, &f);
    CHECK(u.find_opts.cased && u.find_miss && u.finding);
    f = ui_input_none(), f.field = UI_FIELD_FIND, f.field_toggle = 1;
    ui_handle(&u, names, &f);
    CHECK(!u.find_opts.cased && !u.find_miss);
    ui_find_close(&u);
    ui_handle(&u, names, &f);
    CHECK(!u.find_opts.cased);
    // Under a question the panel's pointer does nothing; nor does the menu bar over the panel.
    ui_ow_open(&u);
    u.quit_asking = true;
    in = ui_input_none(), in.app_name = OW_APPS, in.app_n = COUNT(OW_APPS), in.modal = UI_MODAL_NO;
    ui_handle(&u, names, &in);
    CHECK(u.ow);
    u.quit_asking = true;   // (No answered it)
    in = ui_input_none(), in.app_name = OW_APPS, in.app_n = COUNT(OW_APPS), in.toggle = 1;
    ui_handle(&u, names, &in);
    CHECK(!u.ow_opts.cased);
    u.quit_asking = false;
    u.confirming = true;
    ui_handle(&u, names, &in);
    CHECK(!u.ow_opts.cased);
    u.confirming = false;
    in = ui_input_none(), in.app_name = OW_APPS, in.app_n = COUNT(OW_APPS), in.menu_title = 0;
    ui_handle(&u, names, &in);
    CHECK(!u.menu_on && u.ow);
    ui_ow_close(&u);
    ui_matcher_set(&u.find_m, STR_EMPTY, (UiMatchOpts){ 0 });
}

static void test_spawn_launch_all(void)
{
    char out[2048];
    Err err;
    mkdirs(at("bin73"));
    fake(at("bin73"), "uwsm-app", at("spawn73.log"));
    env_set("PATH", at("bin73"));
    char *paths[] = { "/a b/one.png", "/two.png", "/three.png" };
    CHECK(spawn_launch_all("/x/imv.desktop", paths, 3, &err));
    CHECK(wait_file(at("spawn73.log"), out, sizeof out) &&
          strncmp(out, "uwsm-app -- gio launch /x/imv.desktop /a b/one.png /two.png /three.png sid=", 75) == 0);
    env_restore();
}

// ---- Set as default (F7.3.1) ------------------------------------------------------------------

static void test_set_default(void)
{
    arena_reset(&ta);
    env_set("XDG_CONFIG_HOME", at("f731/cfg"));
    env_set("XDG_CONFIG_DIRS", at("f731/etc"));
    env_set("XDG_DATA_HOME", at("f731/home"));
    env_set("XDG_DATA_DIRS", at("f731/sys"));
    env_set("XDG_CURRENT_DESKTOP", "Hyprland");
    mkdirs(at("f731/cfg"));
    mkdirs(at("f731/etc"));
    mkdirs(at("f731/sys/mime"));
    put(at("f731/sys/mime/aliases"), "image/x-old image/png\n");
    char list[PATH_MAX], why[PATH_MAX];
    CHECK(apps_user_list(list, sizeof list) && strcmp(list, at("f731/cfg/mimeapps.list")) == 0);
    // The desktop's list in the config home is read first: what it sets can't be changed in ours;
    // an alias counts; a list read after ours (the system's) doesn't.
    put(at("f731/cfg/hyprland-mimeapps.list"), "[Default Applications]\nimage/png=v.desktop\n[Added Associations]\ntext/x-added=e.desktop\n");
    put(at("f731/cfg/mimeapps.list"), "[Default Applications]\ntext/plain=e.desktop\n");
    put(at("f731/etc/mimeapps.list"), "[Default Applications]\ntext/html=w.desktop\n");
    CHECK(apps_shadowed(&ta, S("image/png"), why, sizeof why) && strcmp(why, at("f731/cfg/hyprland-mimeapps.list")) == 0);
    CHECK(apps_shadowed(&ta, S("image/x-old"), why, sizeof why));
    put(at("f731/cfg/hyprland-mimeapps.list"), "[Default Applications]\nimage/x-old=v.desktop\n");   // set by the alias, asked by the type
    CHECK(apps_shadowed(&ta, S("image/png"), why, sizeof why));
    put(at("f731/cfg/hyprland-mimeapps.list"), "[Default Applications]\nimage/png=v.desktop\n[Added Associations]\ntext/x-added=e.desktop\n");
    CHECK(!apps_shadowed(&ta, S("text/plain"), why, sizeof why) && !apps_shadowed(&ta, S("text/html"), why, sizeof why));
    CHECK(!apps_shadowed(&ta, S("text/x-added"), why, sizeof why));   // only a default shadows
    // Our list first in the order (no desktop): nothing before it.
    env_set("XDG_CURRENT_DESKTOP", "");
    CHECK(!apps_shadowed(&ta, S("image/png"), why, sizeof why));
    env_set("XDG_CURRENT_DESKTOP", "Hyprland");
    char now[64];
    apps_user_default(&ta, list, S("text/plain"), now, sizeof now);
    CHECK(strcmp(now, "e.desktop") == 0);
    put(list, "[Default Applications]\ntext/plain= e.desktop ;f.desktop\n");
    apps_user_default(&ta, list, S("text/plain"), now, sizeof now);
    CHECK(strcmp(now, "e.desktop") == 0);
    apps_user_default(&ta, list, S("text/none"), now, sizeof now);
    CHECK(now[0] == 0);
    // The setting goes through `gio mime -- <type> <app>`: a fake that writes the line as GLib
    // does; one that says no; none at all.
    mkdirs(at("bin731"));
    put(at("bin731/gio"), "#!/bin/sh\necho \"$*\" > \"${0%/*}/args\"\nprintf '[Default Applications]\\n%s=%s\\n' \"$3\" \"$4\" > \"$XDG_CONFIG_HOME/mimeapps.list\"\necho \"Set $4 as the default for $3\"\n");
    chmod(at("bin731/gio"), 0755);
    mkdirs(at("bin731no"));
    put(at("bin731no/gio"), "#!/bin/sh\necho 'Failed to set: no such app'\n");
    chmod(at("bin731no/gio"), 0755);
    Err err;
    env_set("PATH", at("bin731"));
    CHECK(op_set_default(&ta, S("text/x-tex"), "nvim.desktop", &err));
    char args[256];
    CHECK(wait_file(at("bin731/args"), args, sizeof args) && strcmp(args, "mime -- text/x-tex nvim.desktop") == 0);
    // An alias is set (and checked) by its canonical name (reviewer-2).
    unlink(at("bin731/args"));
    CHECK(op_set_default(&ta, S("image/x-old"), "imv.desktop", &err));
    CHECK(wait_file(at("bin731/args"), args, sizeof args) && strcmp(args, "mime -- image/png imv.desktop") == 0);
    env_set("PATH", at("bin731no"));
    CHECK(!op_set_default(&ta, S("text/x-other"), "nvim.desktop", &err) && strcmp(err.msg, "Failed to set: no such app") == 0);
    put(at("bin731no/gio"), "#!/bin/sh\n");
    CHECK(!op_set_default(&ta, S("text/x-other"), "nvim.desktop", &err) && strcmp(err.msg, "gio didn't set it") == 0);
    env_set("PATH", at("bin731-none"));
    CHECK(!op_set_default(&ta, S("text/x-other"), "nvim.desktop", &err));
    // The guard: a config home outside the test root is refused before gio runs.
    env_set("PATH", at("bin731"));
    char out_cfg[PATH_MAX + 8];
    snprintf(out_cfg, sizeof out_cfg, "%s/cfg", decoy);
    env_set("XDG_CONFIG_HOME", out_cfg);
    unlink(at("bin731/args"));
    CHECK(!op_set_default(&ta, S("text/x-tex"), "nvim.desktop", &err) && access(at("bin731/args"), F_OK) != 0);
    // No home at all.
    env_set("XDG_CONFIG_HOME", "relative");
    env_set("HOME", "");
    CHECK(!apps_user_list(list, sizeof list) && !op_set_default(&ta, S("text/x-tex"), "nvim.desktop", &err));
    env_restore();
}

static void test_ow_default(void)
{
    UiNames names = { name_at, NULL, NULL };
    Ui u = menu_ui(3);
    u.ow_list.rows = 4;
    ui_ow_open(&u);
    ow_keys(&u, 0);
    // Not allowed (gui/ says why): Alt+D and the box do nothing; Enter opens without it.
    u.ow_def_can = false;
    ow_keys(&u, 1, -'d');
    UiInput in = ui_input_none();
    in.app_name = OW_APPS, in.app_n = COUNT(OW_APPS), in.toggle = 4;
    ui_handle(&u, names, &in);
    CHECK(!u.ow_def && u.ow);
    u.ow_def = true;   // (even ticked before gui/ said no)
    UiAction a = ow_keys(&u, 1, UK_ENTER);
    CHECK(a.kind == UA_LAUNCH && !a.all);
    // Allowed: Alt+D and the box flip it, the field keeps its text; Enter carries it; Open too.
    ui_ow_open(&u);
    ow_keys(&u, 0);
    CHECK(!u.ow_def);   // clear at each opening
    u.ow_def_can = true;
    ow_type(&u, "neo");
    ow_keys(&u, 1, -'d');
    CHECK(u.ow_def && u.ow_filter.len == 3);
    ow_keys(&u, 1, -'d');
    CHECK(!u.ow_def);
    ui_handle(&u, names, &in);
    CHECK(u.ow_def && ui_input_acts(&in));
    ow_keys(&u, 2, UK_TAB, -'d');   // from the list as well
    CHECK(!u.ow_def && u.ow_in_list);
    ow_keys(&u, 1, -'d');
    a = ow_keys(&u, 1, UK_ENTER);
    CHECK(a.kind == UA_LAUNCH && a.all && a.index == 0 && !u.ow);
    ui_ow_open(&u);
    ow_keys(&u, 0);
    u.ow_def_can = true;
    ui_handle(&u, names, &in);
    in = ui_input_none(), in.app_name = OW_APPS, in.app_n = COUNT(OW_APPS), in.modal = UI_MODAL_YES;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_LAUNCH && a.all);
}

// ---- the find bar (Michael, 2026-10-26) ------------------------------------------------------

// The find bar's count and chevrons (Michael, 2026-10-26): "2 of 5"; a chevron steps round the
// list with two matches or more and leaves the keys where they are.
static void test_find_count(void)
{
    Ui u = { 0 };
    UiNames names = { name_at, NULL, NULL };
    ui_resize(&u.list, 10);
    ui_set(&u.list, COUNT(find_names), 0);   // alpha Beta gamma.txt delta BETAMAX omega café
    isize at = -1;
    CHECK(ui_find_count(&u, names, &at) == 0 && at == 0);   // no bar
    UiInput in = chars("/");
    ui_handle(&u, names, &in);
    CHECK(ui_find_count(&u, names, &at) == 0 && at == 0);   // no text
    in = chars("ta");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && ui_find_count(&u, names, &at) == 3 && at == 1);
    in = ui_input_none(), in.find_step = 1;
    CHECK(ui_input_acts(&in));
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 3 && (u.focus == UI_FOCUS_FIND) && ui_find_count(&u, names, &at) == 3 && at == 2);
    ui_handle(&u, names, &in);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1);   // round
    in.find_step = -1;
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 4 && ui_find_count(&u, names, &at) == 3 && at == 3);
    // The keys in the list: a chevron leaves them there; the cursor off the matches: at 0.
    in = keys(1, UK_ENTER);
    ui_handle(&u, names, &in);
    u.list.sel = 0;
    CHECK(ui_find_count(&u, names, &at) == 3 && at == 0);
    in = ui_input_none(), in.find_step = 1;
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && u.focus != UI_FOCUS_FIND && u.finding);
    // One match: the chevrons do nothing, the cursor stays off it.
    in = chars("/gam");
    ui_handle(&u, names, &in);
    u.list.sel = 5;
    CHECK(ui_find_count(&u, names, &at) == 1 && at == 0);
    in = ui_input_none(), in.find_step = -1;
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 5);
    in.find_step = 1;
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 5);
    // The toggles count too: case kept, "Beta" alone; whole words, none of "ta"'s.
    in = keys(3, UK_BACKSPACE, UK_BACKSPACE, UK_BACKSPACE);
    ui_handle(&u, names, &in);
    in = chars("Bet");
    ui_handle(&u, names, &in);
    CHECK(ui_find_count(&u, names, &at) == 2);
    u.find_opts.cased = true;
    in = keys(1, UK_BACKSPACE);
    ui_handle(&u, names, &in);
    in = chars("t");
    ui_handle(&u, names, &in);
    CHECK(ui_find_count(&u, names, &at) == 1 && at == 1 && u.list.sel == 1);
    u.find_opts.cased = false;
    // Closed: no count, and a chevron press does nothing.
    in = keys(1, UK_ESC);
    ui_handle(&u, names, &in);
    isize was = u.list.sel;
    CHECK(ui_find_count(&u, names, &at) == 0 && at == 0);
    in = ui_input_none(), in.find_step = 1;
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == was);
    // Opened again, no text yet: no count, and Up and Down find nothing (reviewer-2: they stepped
    // through the last find's matches).
    in = chars("/");
    ui_handle(&u, names, &in);
    was = u.list.sel;
    CHECK(u.finding && u.find_m.len == 0 && ui_find_count(&u, names, &at) == 0 && at == 0);
    in = keys(2, UK_DOWN, UK_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == was);
    // Enter, then n: the last text found again, the bar still empty and counting nothing.
    in = keys(1, UK_ENTER);
    ui_handle(&u, names, &in);
    in = chars("n");
    ui_handle(&u, names, &in);
    CHECK(u.finding && u.find.len == 0 && u.find_m.len > 0 && ui_find_count(&u, names, &at) == 0);
    in = keys(1, UK_ESC);
    ui_handle(&u, names, &in);
}

static void test_find_bar(void)
{
    Ui u = { 0 };
    UiNames names = { name_at, NULL, NULL };
    ui_resize(&u.list, 10);
    ui_set(&u.list, COUNT(find_names), 0);   // alpha Beta gamma.txt delta BETAMAX omega café
    // Up and Down in the bar: the previous and next match, round the ends; the bar keeps the keys.
    UiInput in = chars("/ta");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && (u.focus == UI_FOCUS_FIND));
    in = keys(1, UK_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 3 && (u.focus == UI_FOCUS_FIND) && u.finding);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 4);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1);   // round
    in = keys(1, UK_UP);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 4 && (u.focus == UI_FOCUS_FIND));
    in = chars("zz");   // no match: Up and Down stay
    ui_handle(&u, names, &in);
    in = keys(2, UK_DOWN, UK_UP);
    ui_handle(&u, names, &in);
    CHECK(u.find_miss && u.list.sel == 0 && (u.focus == UI_FOCUS_FIND));
    in = keys(2, UK_BACKSPACE, UK_BACKSPACE);
    ui_handle(&u, names, &in);
    // Enter: the keys back to the list, the bar open; a letter is the list's then (j moves).
    in = keys(1, UK_ENTER);
    ui_handle(&u, names, &in);
    CHECK(u.finding && u.focus != UI_FOCUS_FIND && u.list.sel == 1);
    in = chars("j");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 2 && u.find.len == 2);
    // Ctrl+J: list, bar, job rows, list; without jobs the bar's Ctrl+J goes back to the list.
    in = keys(1, UK_JOBS), in.job_n = 2, in.job_id[0] = 7, in.job_id[1] = 8;
    ui_handle(&u, names, &in);
    CHECK((u.focus == UI_FOCUS_FIND) && u.focus != UI_FOCUS_JOBS);
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_FIND && (u.focus == UI_FOCUS_JOBS) && u.job_at == 0 && u.finding);
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_FIND && u.focus != UI_FOCUS_JOBS);
    in = keys(2, UK_JOBS, UK_JOBS);   // no jobs
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_FIND && u.focus != UI_FOCUS_JOBS && u.finding);
    // The pointer: the field takes the keys (from the sidebar too), × closes, keeping the cursor.
    u.focus = UI_FOCUS_SIDE;
    in = ui_input_none(), in.field = UI_FIELD_FIND;
    CHECK(ui_input_acts(&in));
    ui_handle(&u, names, &in);
    CHECK((u.focus == UI_FOCUS_FIND) && u.focus != UI_FOCUS_SIDE);
    in = keys(1, UK_ENTER);
    ui_handle(&u, names, &in);
    u.list.sel = 5;
    in = ui_input_none(), in.find_x = true;
    CHECK(ui_input_acts(&in));
    ui_handle(&u, names, &in);
    CHECK(!u.finding && u.focus != UI_FOCUS_FIND && u.list.sel == 5);
    ui_handle(&u, names, &in);   // closed: nothing
    CHECK(!u.finding);
    // The magnifier is the search's since F9 (2026-10-28), not the find bar's.
    in = ui_input_none(), in.bar = UB_SEARCH, in.job_n = 1;
    CHECK(ui_handle(&u, names, &in).kind == UA_SEARCH && !u.finding);
    // / with the bar open but not its keys: a new find, from the cursor.
    in = chars("/gam");
    ui_handle(&u, names, &in);
    in = keys(1, UK_ENTER);
    ui_handle(&u, names, &in);
    u.list.sel = 6;
    in = chars("/al");
    ui_handle(&u, names, &in);
    CHECK((u.focus == UI_FOCUS_FIND) && u.find.len == 2 && u.find_from == 6 && u.list.sel == 0);
    // Esc in the bar: closed, the cursor where the find began.
    in = keys(1, UK_ESC);
    ui_handle(&u, names, &in);
    CHECK(!u.finding && u.focus != UI_FOCUS_FIND && u.list.sel == 6);
    // Listed again with fewer entries meanwhile: Esc goes to the last one there is.
    in = chars("/a");
    ui_handle(&u, names, &in);
    ui_set(&u.list, 3, 1);
    in = keys(1, UK_ESC);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 2);
    // The same with no match: the cursor where the find began, inside the list (reviewer-2: it was
    // past the end, and Enter opened entry 6 of 3).
    ui_set(&u.list, COUNT(find_names), 6);
    in = chars("/");
    ui_handle(&u, names, &in);
    ui_set(&u.list, 3, 1);
    in = chars("qq");
    ui_handle(&u, names, &in);
    CHECK(u.find_miss && u.list.sel == 2);
    in = keys(2, UK_ENTER, UK_ENTER);
    UiAction act = ui_handle(&u, names, &in);
    CHECK(act.kind == UA_OPEN && act.index == 2);
    in = chars("/");
    ui_handle(&u, names, &in);
    in = keys(1, UK_ESC);
    ui_handle(&u, names, &in);
    ui_set(&u.list, COUNT(find_names), 6);
    // A click in the field with the rename line open: the line closes, the keys go to the bar
    // (reviewer-2: the line took them, and Enter renamed).
    in = chars("/a");
    ui_handle(&u, names, &in);
    in = keys(2, UK_ENTER, UK_F2);
    ui_handle(&u, names, &in);
    CHECK((u.focus == UI_FOCUS_EDIT) && u.finding && u.focus != UI_FOCUS_FIND);
    in = ui_input_none(), in.field = UI_FIELD_FIND;
    ui_handle(&u, names, &in);
    CHECK(u.focus != UI_FOCUS_EDIT && (u.focus == UI_FOCUS_FIND));
    in = chars("l");
    ui_handle(&u, names, &in);
    CHECK(u.find.len == 2 && u.focus != UI_FOCUS_EDIT);
    in = keys(1, UK_ESC);
    ui_handle(&u, names, &in);
    // Under a question its pointer does nothing.
    in = chars("/a");
    ui_handle(&u, names, &in);
    u.confirming = true;
    in = ui_input_none(), in.find_x = true;
    ui_handle(&u, names, &in);
    CHECK(u.finding);
    u.confirming = false;
    ui_matcher_set(&u.find_m, STR_EMPTY, (UiMatchOpts){ 0 });
}

// ---- Shift and a move select (Michael, 2026-10-26) ---------------------------------------------

static void test_shift_select(void)
{
    UiNames names = { name_at, NULL, NULL };
    static u8 mark[20];
    Ui u = { 0 };
    ui_resize(&u.list, 10);
    ui_set(&u.list, 20, 2);
    u.mark = mark;
    // Shift+Down twice: the start and the two passed over; Shift+Up back marks nothing more and
    // unmarks nothing (as a Shift+click, the selection only grows).
    UiInput in = keys(2, UK_SEL_DOWN, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 4 && u.marked == 3 && mark[2] && mark[3] && mark[4] && !mark[5]);
    in = keys(1, UK_SEL_UP);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 3 && u.marked == 3);
    // A plain move marks nothing; J and K as Shift+Down and Shift+Up, from where the cursor is.
    in = keys(3, UK_DOWN, UK_DOWN, UK_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 6 && u.marked == 3);
    in = chars("JJ");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 8 && u.marked == 6 && mark[6] && mark[7] && mark[8]);
    in = chars("K");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 7 && u.marked == 6);
    // Shift+End and Shift+Home: all the way.
    in = keys(1, UK_SEL_END);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 19 && u.marked == 17 && mark[19] && !mark[1]);
    in = keys(1, UK_SEL_HOME);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 0 && u.marked == 20);
    // A Shift move doesn't wrap round (reviewer-2: Shift+Up on the first entry marked them all).
    ui_mark_clear(&u);
    in = keys(1, UK_SEL_UP);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 0 && u.marked == 1);
    ui_mark_clear(&u);
    in = keys(1, UK_END);
    ui_handle(&u, names, &in);
    in = chars("J");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 19 && u.marked == 1);
    // In the same frame as Enter: marked before Enter opens them all.
    ui_mark_clear(&u);
    ui_set(&u.list, 20, 0);
    in = keys(2, UK_SEL_DOWN, UK_ENTER);
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_OPEN && a.all && u.marked == 2);
    // The icon view: Shift+Right a cell, Shift+Down a row, everything between in the list's order;
    // H and L left and right.
    ui_mark_clear(&u);
    u.grid = true;
    ui_set_cols(&u.list, 4);
    ui_set(&u.list, 20, 1);
    in = keys(1, UK_SEL_RIGHT);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 2 && u.marked == 2 && mark[1] && mark[2]);
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 6 && u.marked == 6 && mark[6] && !mark[7]);
    in = chars("LH");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 6 && u.marked == 7 && mark[7]);
    ui_mark_clear(&u);
    in = keys(1, UK_END);
    ui_handle(&u, names, &in);
    in = keys(2, UK_SEL_RIGHT, UK_SEL_DOWN);   // the last cell: neither wraps
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 19 && u.marked == 1);
    in = keys(1, UK_HOME);
    ui_handle(&u, names, &in);
    ui_mark_clear(&u);
    in = keys(2, UK_SEL_LEFT, UK_SEL_UP);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 0 && u.marked == 1);
    u.grid = false;
    ui_set_cols(&u.list, 1);
    // The sidebar with the keys: its cursor moves, nothing is marked.
    ui_mark_clear(&u);
    u.side.rows = 5;
    ui_set(&u.side, 4, 0);
    u.focus = UI_FOCUS_SIDE;
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.side.sel == 1 && u.marked == 0);
    ui_unfocus(&u, UI_FOCUS_SIDE);
    // In the find bar: Shift+Down is Down (the next match), uppercase letters are text.
    ui_set(&u.list, COUNT(find_names), 0);
    in = chars("/a");
    ui_handle(&u, names, &in);
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.marked == 0 && (u.focus == UI_FOCUS_FIND) && u.list.sel == 1);
    in = chars("J");
    ui_handle(&u, names, &in);
    CHECK(u.find.len == 2 && u.marked == 0);
    ui_find_close(&u);
    ui_matcher_set(&u.find_m, STR_EMPTY, (UiMatchOpts){ 0 });
    // An empty list: nothing to mark, no crash.
    ui_set(&u.list, 0, 0);
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.marked == 0);
}
