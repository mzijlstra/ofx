// F10 step 1 (docs/plan.md "F10 design"): ofx.conf's parser (values, lists, bad lines), the file
// Preferences writes (every option's help in it, and read back as the defaults), and that every
// option has its help. Included by test.c after test_f9.c.

// Whether two parsed files say the same (field by field: padding isn't compared).
static bool conf_same(const Conf *a, const Conf *b)
{
    bool same = a->previews == b->previews && a->preview_max == b->preview_max && a->date == b->date &&
                strcmp(a->date_pattern, b->date_pattern) == 0 && a->nskip == b->nskip && a->bad == b->bad;
    for (int k = 0; same && k < a->nskip; k++) same = strcmp(a->skip[k], b->skip[k]) == 0;
    return same;
}

static void test_conf(void)
{
    Conf c;
    conf_parse(STR_EMPTY, &c);
    CHECK(c.previews && c.preview_max == (i64)10 << 20 && c.date == DATE_SHORT && !c.bad);
    CHECK(c.nskip == 2 && strcmp(c.skip[0], ".git") == 0 && strcmp(c.skip[1], "node_modules") == 0);
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
    const char *notsize[] = { "MiB", "10 mb", "1.5 GiB", "-1", "10 MiBs", "" };
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
    // The list: the file's first line replaces the default, later ones add; a bad name is skipped.
    conf_parse(S("search-skip = target\nsearch-skip = .git\nsearch-skip = a/b\nsearch-skip = ..\n"), &c);
    CHECK(c.nskip == 2 && strcmp(c.skip[0], "target") == 0 && strcmp(c.skip[1], ".git") == 0 && c.bad == 3);
    char many[CONF_SKIP_MAX * 24 + 64] = "";
    for (int k = 0; k <= CONF_SKIP_MAX; k++) snprintf(many + strlen(many), sizeof many - strlen(many), "search-skip = d%d\n", k);
    conf_parse(str_from_c(many), &c);
    CHECK(c.nskip == CONF_SKIP_MAX && c.bad == CONF_SKIP_MAX + 1);
    // Bad lines: the first is kept, with why; the rest still apply; the last of a key wins.
    conf_parse(S("previews\nnosuch = 1\npreviews = off\npreviews = on\n"), &c);
    CHECK(c.bad == 1 && strcmp(c.why, "not a key = value line") == 0 && c.previews);
    conf_parse(S("\nnosuch = 1\n"), &c);
    CHECK(c.bad == 2 && strcmp(c.why, "no option nosuch") == 0);
    conf_parse(S("previews = maybe"), &c);   // no newline at the end
    CHECK(c.bad == 1 && strcmp(c.why, "previews wants on or off") == 0 && c.previews);
    // The file Preferences writes: every option's help and default in it, all commented out, so
    // it reads back as the defaults with no bad line.
    static char text[16384];
    size_t n = conf_default_text(text, sizeof text);
    CHECK(n > 0 && n < sizeof text - 1 && strlen(text) == n);
    for (int k = 0; k < COUNT(CONF_OPTS); k++) {
        CHECK(CONF_OPTS[k].help[0] && CONF_OPTS[k].def[0]);
        char line[96];
        snprintf(line, sizeof line, "\n#%s = ", CONF_OPTS[k].key);
        CHECK(strstr(text, line) != NULL);
    }
    CHECK(strstr(text, "#search-skip = .git\n#search-skip = node_modules\n") && strstr(text, "View > Show Menu brings it back"));
    conf_parse(str_from_c(text), &c);
    Conf d;
    conf_defaults(&d);
    CHECK(!c.bad && conf_same(&c, &d));
    // Uncommented, every default line reads back as the default too.
    static char on[16384];
    size_t o = 0;
    for (const char *p = text; *p; p++)
        if (!(*p == '#' && (p == text || p[-1] == '\n') && p[1] && p[1] != ' ' && p[1] != '\n')) on[o++] = *p;
    on[o] = 0;
    conf_parse(str_from_c(on), &c);
    CHECK(!c.bad && conf_same(&c, &d));
    // Preferences' file: made with its folder, read back as the defaults; an existing one (or a
    // link) never replaced; outside the guard's root, refused.
    Err err;
    CHECK(conf_create(at("cf/ofx/ofx.conf"), &err));
    CHECK(strcmp(slurp(at("cf/ofx/ofx.conf")), text) == 0);
    put(at("cf/ofx/ofx.conf"), "previews = off\n");
    CHECK(conf_create(at("cf/ofx/ofx.conf"), &err) && strcmp(slurp(at("cf/ofx/ofx.conf")), "previews = off\n") == 0);
    symlink("nowhere", at("cf/ofx/link.conf"));
    CHECK(conf_create(at("cf/ofx/link.conf"), &err) && access(at("cf/nowhere"), F_OK) != 0);
    CHECK(!conf_create(out_at("cf/ofx.conf"), &err) && access(out_at("cf/ofx.conf"), F_OK) != 0);
    put(out_at("cf-there.conf"), "x");   // one there already, outside the root: opened, not refused
    CHECK(conf_create(out_at("cf-there.conf"), &err) && strcmp(slurp(out_at("cf-there.conf")), "x") == 0);
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
    CHECK(conf_default_text(small, sizeof small) == sizeof small - 1 && strlen(small) == sizeof small - 1);
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
    search_skips_text(&w.s, NULL, line, sizeof line);   // the default list's names
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
