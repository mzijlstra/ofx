// theme_fit (2026-11-07): every colour ofx draws text in reads on its backgrounds, under every
// Omarchy theme, and one that already read is left as the theme gave it.

// Hard cases from Omarchy 4.5's themes (their colours only, as key=rrggbb): ristretto (
// only dark_foreground short), white and vantablack (greyscale: even their red is grey), solitude
// (red a grey, bright_red the colour), miasma (red 1.69:1), last-horizon (background ==
// lighter_background, dark_foreground == selection), gruvbox and nord (red under 3:1, orange reads:
// the warm pick), catppuccin-latte and rose-pine (light). Omarchy's themes, MIT, (c) David Heinemeier
// Hansson and the themes' authors.
static const struct { const char *name, *colors; } FIT_THEMES[] = {
    { "ristretto", "background=2c2525 lighter_background=3d2f2a foreground=e6d9db dark_foreground=72696a accent=f38d70 selection=403e41 red=fd6883 yellow=f9cc6c green=adda78 blue=f38d70 cyan=85dacc magenta=a8a9eb orange=fb9a77 bright_red=ff8297 bright_blue=f8a788 bright_cyan=9bf1e1" },
    { "white", "background=ffffff lighter_background=c0c0c0 foreground=000000 dark_foreground=c0c0c0 accent=6e6e6e selection=c0c0c0 red=2a2a2a yellow=4a4a4a green=3a3a3a blue=1a1a1a cyan=3e3e3e magenta=2e2e2e bright_red=2a2a2a bright_blue=1a1a1a bright_cyan=3e3e3e" },
    { "vantablack", "background=000000 lighter_background=1a1a1a foreground=ffffff dark_foreground=505050 accent=8d8d8d selection=1a1a1a red=a4a4a4 yellow=cecece green=b6b6b6 blue=8d8d8d cyan=b0b0b0 magenta=9b9b9b orange=b9b9b9 bright_red=a4a4a4 bright_blue=8d8d8d bright_cyan=b0b0b0" },
    { "solitude", "background=101315 lighter_background=101315 foreground=cacccc dark_foreground=4b4e55 accent=798186 selection=343d41 red=565d60 yellow=d9dbdc green=9fa5a9 blue=798186 cyan=707070 magenta=aeaeae bright_red=de6145 bright_blue=5d6367 bright_cyan=707070" },
    { "miasma", "background=222222 lighter_background=2c2c2c foreground=c2c2b0 dark_foreground=555555 accent=78824b selection=383838 red=685742 yellow=b36d43 green=5f875f blue=78824b cyan=c9a554 magenta=bb7744 orange=8d6242 bright_red=685742 bright_blue=78824b bright_cyan=c9a554" },
    { "last-horizon", "background=0c0b0c lighter_background=0c0b0c foreground=fafcfb dark_foreground=584e51 accent=b59790 selection=584e51 red=c38b7b yellow=6b5e73 green=87a9b0 blue=b59790 cyan=a5a0b6 magenta=c4d8e2 bright_red=c38b7b bright_blue=b59790 bright_cyan=a5a0b6" },
    { "catppuccin-latte", "background=eff1f5 lighter_background=dce0e8 foreground=4c4f69 dark_foreground=9ca0b0 accent=1e66f5 selection=ccd0da red=d20f39 yellow=df8e1d green=40a02b blue=1e66f5 cyan=179299 magenta=ea76cb orange=d84e2b bright_red=d20f39 bright_blue=1e66f5 bright_cyan=179299" },
    { "gruvbox", "background=282828 lighter_background=3c3836 foreground=d4be98 dark_foreground=7c6f64 accent=7daea3 selection=504945 red=ea6962 yellow=d8a657 green=a9b665 blue=7daea3 cyan=89b482 magenta=d3869b orange=e1875c bright_red=ea6962 bright_blue=7daea3 bright_cyan=89b482" },
    { "nord", "background=2e3440 lighter_background=3b4252 foreground=d8dee9 dark_foreground=667080 accent=81a1c1 selection=434c5e red=bf616a yellow=ebcb8b green=a3be8c blue=81a1c1 cyan=88c0d0 magenta=b48ead orange=d5967a bright_red=bf616a bright_blue=81a1c1 bright_cyan=8fbcbb" },
    { "rose-pine", "background=faf4ed lighter_background=f2e9e1 foreground=575279 dark_foreground=9893a5 accent=56949f selection=dfdad9 red=b4637a yellow=ea9d34 green=286983 blue=56949f cyan=d7827e magenta=907aa9 orange=cf8057 bright_red=b4637a bright_blue=56949f bright_cyan=d7827e" },
};

// key=rrggbb words as colors.toml's lines.
static Theme fit_theme_of(const char *words)
{
    Theme t = THEME_DEFAULT;
    char text[2048];
    int n = 0;
    for (const char *p = words; *p;) {
        const char *eq = strchr(p, '='), *end = strchr(p, ' ');
        if (!end) end = p + strlen(p);
        n += snprintf(text + n, sizeof text - (size_t)n, "%.*s = \"#%.*s\"\n", (int)(eq - p), p, (int)(end - eq - 1), eq + 1);
        p = *end ? end + 1 : end;
    }
    theme_parse_colors(&t, (Str){ text, n });
    return t;
}

static bool rgb_same(Rgb a, Rgb b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

// A colour reads min over n backgrounds, or is fg and fg itself reads (nothing short of it does).
static bool fit_reads(Rgb c, Rgb fg, const Rgb *bg, int n, double min)
{
    return rgb_worst(c, bg, n) >= min || (rgb_same(c, fg) && rgb_worst(fg, bg, n) >= min);
}

// Every property theme_fit promises, for one theme; a failure names the theme and the colour.
static int fit_checks_failed;
static void fit_check(const char *name, const char *what, bool ok)
{
    if (!ok) fprintf(stderr, "  theme_fit: %s: %s\n", name, what), fit_checks_failed++;
}

static void fit_check_theme(const char *name, Theme raw)
{
    Theme t = raw;
    theme_fit(&t);
    Rgb side[] = { t.bg_light, t.bg, t.selection };
    Rgb list[] = { t.bg, t.bg_light, t.selection, rgb_mix(t.accent, t.bg, 0.28) };
    Rgb raw_list[] = { raw.bg, raw.bg_light, raw.selection, rgb_mix(raw.accent, raw.bg, 0.28) };
    // Each reads, or is the foreground itself (nothing short of it reads).
    fit_check(name, "accent reads on the list and its marked rows",
              fit_reads(t.accent, t.fg, list, 3, THEME_TEXT_MIN) && fit_reads(t.accent, t.fg, list + 3, 1, THEME_TEXT_MIN));
    fit_check(name, "dark_foreground at 2:1", fit_reads(t.fg_dim, t.fg, list, 4, THEME_DIM_MIN));
    const struct { const char *what; Rgb now, was; } text[] = {
        { "red", t.red, raw.red }, { "yellow", t.yellow, raw.yellow }, { "green", t.green, raw.green },
        { "blue", t.blue, raw.blue }, { "cyan", t.cyan, raw.cyan }, { "magenta", t.magenta, raw.magenta },
    };
    for (int i = 0; i < COUNT(text); i++) {
        fit_check(name, text[i].what, fit_reads(text[i].now, t.fg, list, 4, THEME_TEXT_MIN));
        // Left alone where it read: against the backgrounds as they are after the fit (the accent's
        // marked rows may have moved).
        if (rgb_worst(text[i].was, list, 4) >= THEME_TEXT_MIN) fit_check(name, text[i].what, rgb_same(text[i].now, text[i].was));
    }
    if (rgb_worst(raw.fg_dim, raw_list, 4) >= THEME_DIM_MIN && rgb_worst(raw.fg_dim, list, 4) >= THEME_DIM_MIN)
        fit_check(name, "dark_foreground left alone", rgb_same(t.fg_dim, raw.fg_dim));
    if (rgb_worst(raw.accent, raw_list, 3) >= THEME_TEXT_MIN && rgb_contrast(raw.accent, raw_list[3]) >= THEME_TEXT_MIN)
        fit_check(name, "accent left alone", rgb_same(t.accent, raw.accent));
    // The drive's bar.
    fit_check(name, "drive used reads", rgb_worst(t.drive_used, side, 3) >= THEME_TEXT_MIN);
    fit_check(name, "drive warning reads", rgb_worst(t.drive_warn, side, 3) >= THEME_TEXT_MIN);
    fit_check(name, "drive free at 2:1", rgb_worst(t.drive_free, side, 3) >= THEME_DIM_MIN);
    fit_check(name, "the warning unlike the used part", rgb_delta(t.drive_used, t.drive_warn) >= THEME_UNLIKE_MIN);
    // The track shows on its row, and the used part on the track (the review's 1.6, 2026-11-07).
    fit_check(name, "drive track shows", rgb_contrast(t.drive_track, t.bg_light) >= 1.3);
    fit_check(name, "drive used on its track", rgb_contrast(t.drive_used, t.drive_track) >= 1.6);
}

static void test_theme_fit(void)
{
    // The measures.
    Rgb black = { 0, 0, 0 }, white = { 255, 255, 255 }, grey = { 0x77, 0x77, 0x77 };
    CHECK(fabs(rgb_contrast(black, white) - 21) < 1e-9 && fabs(rgb_contrast(white, black) - 21) < 1e-9);
    CHECK(fabs(rgb_contrast(grey, grey) - 1) < 1e-9);
    CHECK(fabs(rgb_contrast(grey, white) - 4.48) < 0.01);   // #777 on white, WCAG's well-known 4.48
    CHECK(rgb_same(rgb_mix(white, black, 0.25), (Rgb){ 64, 64, 64 }));
    CHECK(rgb_delta(black, white) > 99 && rgb_delta(black, white) < 101 && rgb_delta(grey, grey) == 0);
    CHECK(rgb_chroma(grey) < 1 && rgb_chroma((Rgb){ 255, 0, 0 }) > 100);
    Rgb bgs[] = { black, (Rgb){ 0x20, 0x20, 0x20 } };
    CHECK(fabs(rgb_worst(white, bgs, 2) - rgb_contrast(white, bgs[1])) < 1e-9);
    // A push leaves what reads alone, stops at the first twentieth that reads, and gives `to` when
    // nothing short of it does.
    CHECK(rgb_same(rgb_push(white, grey, bgs, 2, 3, NULL), white));
    Rgb dark = { 0x30, 0x30, 0x30 }, p = rgb_push(dark, white, bgs, 2, 3, NULL);
    CHECK(rgb_worst(p, bgs, 2) >= 3);
    int k = 0;
    while (k <= 20 && !rgb_same(rgb_mix(white, dark, k / 20.0), p)) k++;
    CHECK(k > 0 && k <= 20 && rgb_worst(rgb_mix(white, dark, (k - 1) / 20.0), bgs, 2) < 3);
    CHECK(rgb_same(rgb_push(dark, grey, bgs, 2, 20, NULL), grey));
    // The marked row moves with the colour: a colour that reads on bg alone may not on its marked row.
    Rgb mid = { 0x5c, 0x5c, 0x5c };
    CHECK(rgb_worst(mid, bgs, 1) >= 3 && rgb_contrast(mid, rgb_mix(mid, black, 0.28)) < 3);
    CHECK(!rgb_same(rgb_push(mid, white, bgs, 1, 3, &black), mid));

    // The keys: the extra four are read, and `have` says which came.
    CHECK(strcmp(THEME_KEYS[TK_ORANGE].key, "orange") == 0 && strcmp(THEME_KEYS[TK_BRIGHT_RED].key, "bright_red") == 0);
    CHECK(strcmp(THEME_KEYS[TK_BRIGHT_BLUE].key, "bright_blue") == 0 && strcmp(THEME_KEYS[TK_BRIGHT_CYAN].key, "bright_cyan") == 0);
    Theme w = fit_theme_of(FIT_THEMES[1].colors);   // white: no orange
    CHECK(theme_has(&w, TK_BRIGHT_RED) && !theme_has(&w, TK_ORANGE) && w.ncolors == 15);
    Theme r = fit_theme_of(FIT_THEMES[0].colors);
    CHECK(theme_has(&r, TK_ORANGE) && rgb_same(r.orange, (Rgb){ 0xfb, 0x9a, 0x77 }) && r.ncolors == 16);

    // Every property, on each hard case.
    fit_checks_failed = 0;
    for (int i = 0; i < COUNT(FIT_THEMES); i++) fit_check_theme(FIT_THEMES[i].name, fit_theme_of(FIT_THEMES[i].colors));
    CHECK(fit_checks_failed == 0);

    // The drive's four as they are for each embedded theme, pinned (the theme-fit review, 2026-11-07:
    // the picks' order and the track's mix pass every property above, wrong or right).
    static const struct { const char *name; Rgb warn, used, free_, track; } PINS[] = {
    { "ristretto", { 0xfd, 0x68, 0x83 }, { 0xf3, 0x8d, 0x70 }, { 0x78, 0x6d, 0x6d }, { 0x72, 0x67, 0x67 } },
    { "white", { 0x00, 0x00, 0x00 }, { 0x69, 0x69, 0x69 }, { 0x86, 0x86, 0x86 }, { 0x95, 0x95, 0x95 } },
    { "vantablack", { 0xff, 0xff, 0xff }, { 0x8d, 0x8d, 0x8d }, { 0x52, 0x52, 0x52 }, { 0x49, 0x49, 0x49 } },
    { "solitude", { 0xde, 0x61, 0x45 }, { 0x81, 0x89, 0x8d }, { 0x66, 0x69, 0x6d }, { 0x54, 0x57, 0x5c } },
    { "miasma", { 0x9d, 0x7f, 0x63 }, { 0x7f, 0x88, 0x55 }, { 0x69, 0x69, 0x64 }, { 0x5f, 0x5f, 0x5c } },
    { "last-horizon", { 0xc9, 0x96, 0x88 }, { 0xc4, 0xd8, 0xe2 }, { 0x87, 0x82, 0x84 }, { 0x6a, 0x64, 0x66 } },
    { "catppuccin-latte", { 0xd2, 0x0f, 0x39 }, { 0x1e, 0x66, 0xf5 }, { 0x8d, 0x91, 0xa3 }, { 0x9d, 0xa1, 0xb1 } },
    { "gruvbox", { 0xe1, 0x87, 0x5c }, { 0x7d, 0xae, 0xa3 }, { 0x86, 0x78, 0x68 }, { 0x73, 0x67, 0x5c } },
    { "nord", { 0xd5, 0x96, 0x7a }, { 0x81, 0xa1, 0xc1 }, { 0x78, 0x80, 0x8e }, { 0x6d, 0x75, 0x84 } },
    { "rose-pine", { 0xb4, 0x63, 0x7a }, { 0x56, 0x80, 0x94 }, { 0x9b, 0x95, 0xa7 }, { 0xac, 0xa6, 0xb3 } },
    };
    _Static_assert(COUNT(PINS) == COUNT(FIT_THEMES), "a pin per embedded theme");
    for (int i = 0; i < COUNT(PINS); i++) {
        Theme x = fit_theme_of(FIT_THEMES[i].colors);
        theme_fit(&x);
        bool same = strcmp(PINS[i].name, FIT_THEMES[i].name) == 0 && rgb_same(x.drive_warn, PINS[i].warn) && rgb_same(x.drive_used, PINS[i].used)
                    && rgb_same(x.drive_free, PINS[i].free_) && rgb_same(x.drive_track, PINS[i].track);
        if (!same) fprintf(stderr, "  theme_fit: %s: the drive's colours moved\n", FIT_THEMES[i].name);
        CHECK(same);
    }

    // What each case shows. ristretto: only dark_foreground moves; the drive keeps its accent and red.
    Theme t = r;
    theme_fit(&t);
    CHECK(!rgb_same(t.fg_dim, r.fg_dim) && rgb_same(t.accent, r.accent) && rgb_same(t.red, r.red) && rgb_same(t.blue, r.blue));
    CHECK(rgb_same(t.drive_used, r.accent) && rgb_same(t.drive_warn, r.red));
    // Just past the floor, not beyond: muted, as Michael picked (2:1, 2026-11-07).
    Rgb rl[] = { t.bg, t.bg_light, t.selection, rgb_mix(t.accent, t.bg, 0.28) };
    CHECK(rgb_worst(t.fg_dim, rl, 4) >= THEME_DIM_MIN && rgb_worst(t.fg_dim, rl, 4) < THEME_DIM_MIN + 0.25);
    // The greyscale ones: the warning is the foreground, no colour brought in.
    for (int i = 1; i <= 2; i++) {
        Theme g = fit_theme_of(FIT_THEMES[i].colors);
        theme_fit(&g);
        CHECK(rgb_same(g.drive_warn, g.fg) && rgb_chroma(g.drive_used) < 5);
    }
    // solitude: red is a grey, so the warning is bright_red's colour, and the used part the accent.
    Theme s = fit_theme_of(FIT_THEMES[3].colors);
    theme_fit(&s);
    CHECK(rgb_chroma(s.drive_warn) >= 20 && rgb_delta(s.drive_warn, (Rgb){ 0xde, 0x61, 0x45 }) < 20);
    // miasma: its red (1.69:1) and bright_red (the same) read worse than its orange, so the warning
    // is orange, pushed.
    Theme o = fit_theme_of(FIT_THEMES[4].colors), o0 = o;
    theme_fit(&o);
    Rgb oside[] = { o0.bg_light, o0.bg, o0.selection };
    CHECK(rgb_same(o.drive_warn, rgb_push(o0.orange, o0.fg, oside, 3, THEME_TEXT_MIN, NULL)) && !rgb_same(o.drive_warn, o0.orange));
    // last-horizon: the accent is too like the warning (its red), so the used part is another colour.
    Theme m = fit_theme_of(FIT_THEMES[5].colors), m0 = m;
    theme_fit(&m);
    CHECK(rgb_delta(m0.accent, m.drive_warn) < THEME_UNLIKE_MIN && rgb_delta(m.drive_used, m.accent) >= 10);
    // white: dark_foreground was the selection's own grey.
    CHECK(rgb_contrast(w.fg_dim, w.selection) < 1.01);
    theme_fit(&w);
    CHECK(rgb_worst(w.fg_dim, (Rgb[]){ w.bg, w.bg_light, w.selection }, 3) >= THEME_DIM_MIN);

    // Every theme installed here, when there are any (Omarchy's own folder, read only).
    DIR *d = opendir("/usr/share/omarchy/themes");
    int seen = 0;
    for (struct dirent *e; d && (e = readdir(d));) {
        if (e->d_name[0] == '.') continue;
        char path[PATH_MAX];
        snprintf(path, sizeof path, "/usr/share/omarchy/themes/%s/colors.toml", e->d_name);
        arena_reset(&ta);
        Str text = read_file(&ta, path);
        if (!text.len) continue;   // (4.5 left osaka-jade without one)
        Theme x = THEME_DEFAULT;
        theme_parse_colors(&x, text);
        fit_check_theme(e->d_name, x);
        seen++;
    }
    if (d) closedir(d);
    CHECK(fit_checks_failed == 0);
    if (seen) printf("  theme_fit: %d installed themes checked\n", seen);
}
