// F10 step 1 (docs/plan.md "F10 design"): ofx.conf's parser (values, lists, bad lines), the file
// ofx writes (every option's help in it, and read back as the defaults), and that every option has
// its help. Included by test.c after test_f9.c.

// Whether two parsed files say the same (field by field: padding isn't compared).
static bool conf_same(const Conf *a, const Conf *b)
{
    bool same = a->previews == b->previews && a->preview_max == b->preview_max && a->date == b->date &&
                strcmp(a->date_pattern, b->date_pattern) == 0 && a->skip.n == b->skip.n && a->bad == b->bad;   // (key lines: test_f15.c)
    for (int k = 0; same && k < a->skip.n; k++) same = strcmp(a->skip.name[k], b->skip.name[k]) == 0;
    return same;
}

static void test_conf(void)
{
    Conf c;
    // No file: the built-in defaults. An empty file: the same, but no list (it is the file's lines).
    conf_defaults(&c);
    CHECK(c.previews && c.preview_max == (i64)10 << 20 && c.date == DATE_SHORT && !c.bad);
    CHECK(c.skip.n == 2 && strcmp(c.skip.name[0], ".git") == 0 && strcmp(c.skip.name[1], "node_modules") == 0);
    conf_parse(STR_EMPTY, &c);
    CHECK(c.previews && c.preview_max == (i64)10 << 20 && c.date == DATE_SHORT && !c.bad && c.skip.n == 0);
    // Values: on/off words, sizes with and without a space and unit, the dates.
    conf_parse(S("# a comment\n\n  previews = no\npreview-max-size=50 MiB\ndate-format = long\n"), &c);
    CHECK(!c.previews && c.preview_max == (i64)50 << 20 && c.date == DATE_LONG && !c.bad);
    const struct { const char *v; i64 n; } sizes[] = { { "0", 0 }, { "512", 512 }, { "3B", 3 }, { "2 KiB", 2048 }, { "1 GiB", (i64)1 << 30 },
                                                        { "5 KB", 5000 }, { "7MB", 7000000 }, { "2 GB", 2000000000 } };
    for (int k = 0; k < COUNT(sizes); k++) {
        char line[64];
        snprintf(line, sizeof line, "preview-max-size = %s", sizes[k].v);
        conf_parse(str_from_c(line), &c);
        if (c.preview_max != sizes[k].n || c.bad) fprintf(stderr, "size %s: %lld\n", sizes[k].v, (long long)c.preview_max);
        CHECK(c.preview_max == sizes[k].n && !c.bad);
    }
    const char *notsize[] = { "MiB", "10 mb", "1.5 GiB", "-1", "10 MiBs", "", "100000000000000 GiB", "9000000000 GiB" };
    for (int k = 0; k < COUNT(notsize); k++) {
        char line[64];
        snprintf(line, sizeof line, "preview-max-size = %s", notsize[k]);
        conf_parse(str_from_c(line), &c);
        CHECK(c.bad == 1 && c.preview_max == (i64)10 << 20 && strstr(c.why, "preview-max-size wants a size"));
    }
    conf_parse(S("previews = off\npreviews = true\n"), &c);
    CHECK(c.previews);
    conf_parse(S("previews = false\n"), &c);
    CHECK(!c.previews);
    conf_parse(S("date-format = iso\n"), &c);
    CHECK(c.date == DATE_ISO);
    conf_parse(S("date-format = %d-%m-%Y %H:%M\n"), &c);
    CHECK(c.date == DATE_PATTERN && strcmp(c.date_pattern, "%d-%m-%Y %H:%M") == 0);
    conf_parse(S("date-format = weekly\n"), &c);
    CHECK(c.date == DATE_SHORT && c.bad == 1 && strstr(c.why, "date-format wants short, long, iso"));
    // The list: the file's lines, in order; a bad name is skipped.
    conf_parse(S("search-skip = target\nsearch-skip = .git\nsearch-skip = a/b\nsearch-skip = ..\n"), &c);
    CHECK(c.skip.n == 2 && strcmp(c.skip.name[0], "target") == 0 && strcmp(c.skip.name[1], ".git") == 0 && c.bad == 3);
    char many[CONF_SKIP_MAX * 24 + 64] = "";
    for (int k = 0; k <= CONF_SKIP_MAX; k++) snprintf(many + strlen(many), sizeof many - strlen(many), "search-skip = d%d\n", k);
    conf_parse(str_from_c(many), &c);
    CHECK(c.skip.n == CONF_SKIP_MAX && c.bad == CONF_SKIP_MAX + 1);
    // Bad lines: the first is kept, with why; the rest still apply; the last of a key wins.
    conf_parse(S("previews\nnosuch = 1\npreviews = off\npreviews = on\n"), &c);
    CHECK(c.bad == 1 && strcmp(c.why, "not a key = value line") == 0 && c.previews);
    conf_parse(S("\nnosuch = 1\n"), &c);
    CHECK(c.bad == 2 && strcmp(c.why, "no option nosuch") == 0);
    conf_parse(S("previews = maybe"), &c);   // no newline at the end
    CHECK(c.bad == 1 && strcmp(c.why, "previews wants on or off") == 0 && c.previews);
    // The file ofx writes: every option's help and default in it, commented out but the list's
    // lines, so it reads back as the defaults with no bad line.
    static char keys[CONF_KEYS_TEXT_MAX], text[CONF_TEXT_MAX];
    ui_keys_conf_text(keys, sizeof keys);
    size_t n = conf_default_text(text, sizeof text, keys);
    CHECK(n > 0 && n < sizeof text - 1 && strlen(text) == n);
    for (int k = 0; k < COUNT(CONF_OPTS); k++) {
        CHECK(CONF_OPTS[k].help[0]);
        if (!CONF_OPTS[k].def[0]) continue;   // (editor: no default, its help has an example)
        char line[96];
        snprintf(line, sizeof line, "\n%s%s = ", CONF_OPTS[k].kind == CK_NAMES ? "" : "#", CONF_OPTS[k].key);
        CHECK(strstr(text, line) != NULL);
    }
    CHECK(strstr(text, "\nsearch-skip = .git\nsearch-skip = node_modules\n") && strstr(text, "View > Show Menu brings it back"));
    CHECK(strstr(text, "With no search-skip line at all, a search goes into\n# every folder."));
    CHECK(!strstr(text, "\neditor =") && !strstr(text, "\n#editor =") && strstr(text, "\n# Example: editor = nvim yes nvim +{line} {file}\n"));
    conf_parse(str_from_c(text), &c);
    Conf d;
    conf_defaults(&d);
    CHECK(!c.bad && conf_same(&c, &d));
    // Uncommented, every default line reads back as the default too.
    static char on[CONF_TEXT_MAX];
    size_t o = 0;
    for (const char *p = text; *p; p++)
        if (!(*p == '#' && (p == text || p[-1] == '\n') && p[1] && p[1] != ' ' && p[1] != '\n')) on[o++] = *p;
    on[o] = 0;
    conf_parse(str_from_c(on), &c);
    CHECK(!c.bad && conf_same(&c, &d));
    // Its search-skip lines removed (Michael, 2026-11-01): nothing skipped; one of them, the other.
    static char cut[CONF_TEXT_MAX];
    snprintf(cut, sizeof cut, "%s", text);
    char *g = strstr(cut, "\nsearch-skip = .git\n");
    memmove(g + 1, g + 20, strlen(g + 20) + 1);
    conf_parse(str_from_c(cut), &c);
    CHECK(!c.bad && c.skip.n == 1 && strcmp(c.skip.name[0], "node_modules") == 0);
    g = strstr(cut, "\nsearch-skip = node_modules\n");
    memmove(g + 1, g + 28, strlen(g + 28) + 1);
    conf_parse(str_from_c(cut), &c);
    CHECK(!c.bad && c.skip.n == 0 && c.previews && c.date == DATE_SHORT);
    // ofx's file: made with its folder, read back as the defaults; an existing one (or a
    // link) never replaced; outside the guard's root, refused.
    Err err;
    CHECK(conf_create(at("cf/ofx/ofx.conf"), keys, &err));
    Str made = read_file(&ta, at("cf/ofx/ofx.conf"));   // (past slurp's 8 KiB since F15's key lines)
    CHECK((size_t)made.len == n && memcmp(made.p, text, n) == 0);
    put(at("cf/ofx/ofx.conf"), "previews = off\n");
    CHECK(conf_create(at("cf/ofx/ofx.conf"), keys, &err) && strcmp(slurp(at("cf/ofx/ofx.conf")), "previews = off\n") == 0);
    symlink("nowhere", at("cf/ofx/link.conf"));
    CHECK(conf_create(at("cf/ofx/link.conf"), keys, &err) && access(at("cf/nowhere"), F_OK) != 0);
    CHECK(!conf_create(out_at("cf/ofx.conf"), keys, &err) && access(out_at("cf/ofx.conf"), F_OK) != 0);
    put(out_at("cf-there.conf"), "x");   // one there already, outside the root: opened, not refused
    CHECK(conf_create(out_at("cf-there.conf"), keys, &err) && strcmp(slurp(out_at("cf-there.conf")), "x") == 0);
    // Text past CONF_TEXT_MAX is refused, not written cut (a cut file is never written again).
    static char big[CONF_TEXT_MAX + 16];
    memset(big, '#', sizeof big - 1), big[sizeof big - 1] = 0;
    CHECK(!conf_create(at("cf/ofx/big.conf"), big, &err) && strstr(err.msg, "past") && access(at("cf/ofx/big.conf"), F_OK) != 0);
    // Where it is: $XDG_CONFIG_HOME/ofx/ofx.conf, else ~/.config/ofx/ofx.conf.
    char cp[PATH_MAX];
    env_set("XDG_CONFIG_HOME", "/x/cfg");
    CHECK(conf_path(cp, sizeof cp) && strcmp(cp, "/x/cfg/ofx/ofx.conf") == 0);
    env_set("XDG_CONFIG_HOME", NULL), env_set("HOME", "/home/m");
    CHECK(conf_path(cp, sizeof cp) && strcmp(cp, "/home/m/.config/ofx/ofx.conf") == 0);
    env_restore(), env_restore(), env_restore();
    // The README's copy is this file, byte for byte (`build/ofx-probe --conf` writes it again).
    Str readme = read_file(&ta, "README.md");
    const char *from = "<!-- ofx.conf: written by `build/ofx-probe --conf`; make test checks this copy -->\n```ini\n";
    const char *b = readme.len ? strstr(readme.p, from) : NULL, *e = b ? strstr(b, "```\n<!-- ofx.conf end -->") : NULL;
    CHECK(b && e);
    if (b && e) {
        b += strlen(from);
        if ((size_t)(e - b) != n || memcmp(b, text, n) != 0) fprintf(stderr, "README.md's ofx.conf is stale: run build/ofx-probe --conf\n");
        CHECK((size_t)(e - b) == n && memcmp(b, text, n) == 0);
    }
    // Cut short: never past cap, NUL-ended.
    char small[64];
    CHECK(conf_default_text(small, sizeof small, keys) == sizeof small - 1 && strlen(small) == sizeof small - 1);
}

// The search's skip list from ofx.conf (F10): the walk skips its names, counts each, and the status
// line names them; the default when none is given.
static void test_conf_skips(void)
{
    put(at("f10s/target/a.ts"), "");
    put(at("f10s/node_modules/b.ts"), "");
    put(at("f10s/.git/c.ts"), "");
    put(at("f10s/d.ts"), "");
    SearchSkipList l = { { "target", ".git" }, 2 };
    SearchWalk w = sw_walk("*.ts", (UiMatchOpts){ 0 }, true);
    w.skips = &l;
    CHECK(search_walk(&w, at("f10s")));
    CHECK(sw.n == 2 && sw_has("|d.ts") && sw_has("node_modules|b.ts") && w.s.skip[0] == 1 && w.s.skip[1] == 1);
    char line[200];
    search_skips_text(&w.s, &l, line, sizeof line);
    CHECK(strcmp(line, "skipped 1 target, 1 .git") == 0);
    search_skips_text(&w.s, &SEARCH_SKIP_DEFAULT, line, sizeof line);   // the default list's names
    CHECK(strcmp(line, "skipped 1 .git, 1 node_modules") == 0);
    // Edit → Preferences… goes to gui/ (UA_PREFERENCES).
    Ui u = menu_ui(3);
    UiNames names = { name_at, NULL, NULL };
    u.menu_on = true, u.menu = 1;
    UiInput in = ui_input_none();
    in.menu_item = menu_place(1, UM_PREFERENCES);
    CHECK(ui_handle(&u, names, &in).kind == UA_PREFERENCES);
}

// The largest image previewed (ofx.conf's preview-max-size, F10): over it, none made.
static void test_conf_preview_max(void)
{
    static u8 mem[MiB(4)];
    Arena a;
    arena_init(&a, "conf-thumb", mem, sizeof mem);
    char cache[PATH_MAX], p[PATH_MAX];
    snprintf(cache, sizeof cache, "%s", at("f10t/cache/thumbnails"));
    Img im = halves(&a, 30, 20), out;
    Str png;
    CHECK(png_encode(&a, im, NULL, 0, 0, &png));
    snprintf(p, sizeof p, "%s", at("f10t/x.png"));
    put_bytes(p, png.p, (size_t)png.len);
    ThumbAsk q = ask(p, TK_PNG, 48, cache);
    q.src_max = png.len - 1;
    CHECK(thumb_get(&a, &q, &out) == TG_NONE);
    q.src_max = png.len;
    CHECK(thumb_get(&a, &q, &out) == TG_MADE);
}

// F10 step 2: ofx-clean's options (into its rules) and the editor lines (as recipes, before the
// editors file's), and conf_load (the file, else the built-in defaults).
static void test_conf_step2(void)
{
    static Conf c;
    conf_defaults(&c);
    CHECK(c.clean && c.clean_orphans && c.clean_unused_days == 90 && c.clean_cap == (i64)1 << 30 && c.neditor == 0);
    CleanOpts o = { 0 };
    clean_opts_conf(&c, &o);
    CHECK(o.unused_s == CLEAN_UNUSED_S && o.unused_s == 90 * 86400LL && o.orphans && o.cap == CLEAN_CAP);
    conf_parse(S("clean = off\nclean-orphans = no\nclean-unused-days = 0\nclean-cap = 0\n"), &c);
    CHECK(!c.clean && !c.clean_orphans && c.clean_unused_days == 0 && c.clean_cap == 0 && !c.bad);
    clean_opts_conf(&c, &o);
    CHECK(o.unused_s == 0 && !o.orphans && o.cap == 0);
    conf_parse(S("clean-unused-days = 36500\nclean-cap = 500 MiB\n"), &c);
    CHECK(c.clean_unused_days == 36500 && c.clean_cap == (i64)500 << 20 && !c.bad);
    const char *notdays[] = { "-1", "1.5", "36501", "", "30 days", "99999999999999999999" };
    for (int k = 0; k < COUNT(notdays); k++) {
        char line[64];
        snprintf(line, sizeof line, "clean-unused-days = %s", notdays[k]);
        conf_parse(str_from_c(line), &c);
        CHECK(c.bad == 1 && c.clean_unused_days == 90 && strcmp(c.why, "clean-unused-days wants a whole number of days") == 0);
    }
    // Editor lines: kept as written, each a recipe the editors file would take; a bad one skipped.
    conf_parse(S("editor = mine no ~/bin/ed \"--at {line}\" {file}\neditor = x maybe y\neditor = short yes\neditor = q no \"open\neditor = nvim yes nvim +{line}\n"), &c);
    CHECK(c.neditor == 2 && strcmp(c.editor[0], "mine no ~/bin/ed \"--at {line}\" {file}") == 0 && strcmp(c.editor[1], "nvim yes nvim +{line}") == 0);
    CHECK(c.bad == 2 && strcmp(c.why, "editor wants a name, yes or no, then the command") == 0);
    char many[EDITORS_USER * 24 + 64] = "", longline[CONF_LINE_MAX + 32];
    for (int k = 0; k <= EDITORS_USER; k++) snprintf(many + strlen(many), sizeof many - strlen(many), "editor = e%d no e\n", k);
    conf_parse(str_from_c(many), &c);
    CHECK(c.neditor == EDITORS_USER && c.bad == EDITORS_USER + 1);
    snprintf(longline, sizeof longline, "editor = l no %0*d", CONF_LINE_MAX - 5, 0);   // a value of CONF_LINE_MAX
    conf_parse(str_from_c(longline), &c);
    CHECK(c.neditor == 0 && c.bad == 1);
    longline[strlen(longline) - 1] = 0;   // one shorter: kept
    conf_parse(str_from_c(longline), &c);
    CHECK(c.neditor == 1 && !c.bad);
    // 32 lines of 511 bytes, 17 words each, fit the GUI's 64 KiB arena (editor_now's): each word
    // takes its own bytes, not the rest of its line (review, 2026-11-01).
    {
        static char big[EDITORS_USER * (CONF_LINE_MAX + 16)];
        size_t at = 0;
        for (int k = 0; k < EDITORS_USER; k++) {
            char line[CONF_LINE_MAX];
            int m = snprintf(line, sizeof line, "e%02d no", k);
            for (int w = 0; w < 14; w++) m += snprintf(line + m, sizeof line - (size_t)m, " %030d", 0);
            snprintf(line + m, sizeof line - (size_t)m, " %0*d", CONF_LINE_MAX - 2 - m, 0);   // the 15th: to 511 bytes
            at += (size_t)snprintf(big + at, sizeof big - at, "editor = %s\n", line);
        }
        conf_parse(str_from_c(big), &c);
        CHECK(c.neditor == EDITORS_USER && !c.bad && strlen(c.editor[0]) == CONF_LINE_MAX - 1);
        static u8 mem[KiB(64)];
        Arena ga;
        arena_init(&ga, "editors-64k", mem, sizeof mem);
        Editors g = { 0 };
        for (int k = 0; k < c.neditor; k++) editors_add(&ga, str_from_c(c.editor[k]), &g);
        CHECK(g.n == EDITORS_USER && !g.bad && g.e[31].argv[14] && !g.e[31].argv[15]);
    }
    // As Open at Line reads them: ofx.conf's lines first, then the editors file's; the first wins.
    conf_parse(S("editor = nvim no mynvim {file}\n"), &c);
    Editors u = { 0 };
    for (int k = 0; k < c.neditor; k++) editors_add(&ta, str_from_c(c.editor[k]), &u);
    editors_add(&ta, S("nvim yes filenvim {file}\nother no oth {file}\nbad\n"), &u);
    const Editor *e = editor_find(&u, "nvim");
    CHECK(u.n == 3 && u.bad == 3 && e && !e->term && strcmp(e->argv[0], "mynvim") == 0 && editor_find(&u, "other"));
    // conf_load: no file, the built-in defaults (the list too); a file, parsed; a folder there, none.
    env_set("XDG_CONFIG_HOME", at("f10l"));
    CHECK(!conf_load(&ta, &c) && c.skip.n == 2 && c.clean_cap == (i64)1 << 30);
    put(at("f10l/ofx/ofx.conf"), "clean = off\n");
    CHECK(conf_load(&ta, &c) && !c.clean && c.skip.n == 0);
    unlink(at("f10l/ofx/ofx.conf"));
    CHECK(mkdir(at("f10l/ofx/ofx.conf"), 0700) == 0);
    CHECK(!conf_load(&ta, &c) && c.clean && c.skip.n == 2);
    env_restore();
}
