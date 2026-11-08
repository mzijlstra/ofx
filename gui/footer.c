// Under the files: the job rows (F5.3), the find bar, the search view's bar (F9), the sidebar's
// mode buttons and the status line. Included by main.c.

// The job rows under the files (F5.3): a row a running job, its throughput line across it, what it
// does and how far on the left, its speed and a × on the right; then how many wait.
static void layout_jobs(void)
{
    Theme *t = &app.theme;
    for (int i = 0; i < jobs.nrun; i++) {
        JobRec *r = &job_recs[jobs.run[i]];
        Job *j = &r->j;
        i64 bt = atomic_load(&j->bytes_total), ft = atomic_load(&j->files_total);
        float done = bt > 0 ? (float)atomic_load(&j->bytes_done) / (float)bt : ft > 0 ? (float)atomic_load(&j->files_done) / (float)ft : 0;
        if (done > 1) done = 1;
        LineDraw *d = arena_new(&frame, LineDraw, 1);
        *d = (LineDraw){ CUSTOM_LINE, bt > 0 && j->kind != JOB_DELETE ? &r->line : NULL, done, cc(t->accent), mix(t->accent, t->bg_light, 0.2f) };
        bool keyed = app.ui.focus == UI_FOCUS_JOBS && app.ui.job_at == i, hover = app.hov.job_x == i;
        char speed[32] = "";
        if (d->line) jobq_speed(speed, sizeof speed, r->line.now);
        const char *pct = atomic_load(&j->phase) == JP_SCAN ? "counting\xE2\x80\xA6" : arena_sprintf(&frame, "%d%%", (int)(done * 100));
        CLAY(CLAY_IDI("job-row", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(JOB_ROW_H) } },
                                       .backgroundColor = cc(t->bg_light),
                                       .border = { .color = rule_color(), .width = { .top = 1 } } }) {
            CLAY(CLAY_IDI("job-line", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { PAD, 8, 0, 0 }, .childGap = 12,
                                                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                            .custom = { .customData = d } }) {
                text(fit(str_from_c(r->label), 420, false), cc(t->fg));
                text(cz(pct), cc(t->fg_dim));
                CLAY(CLAY_IDI("job-gap", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } } }) {}
                if (speed[0]) text(cz(arena_sprintf(&frame, "%s", speed)), cc(t->fg));
                CLAY(CLAY_IDI("job-x", i), { .layout = { .sizing = { CLAY_SIZING_FIXED(26), CLAY_SIZING_FIXED(26) },
                                                         .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                                             .backgroundColor = hover ? cc(t->bg) : NO_COLOR,
                                             .border = { .color = cc(t->accent), .width = { keyed * 2, keyed * 2, keyed * 2, keyed * 2, 0 } } }) {
                    text(cz("\xC3\x97"), hover || keyed ? cc(t->red) : cc(t->fg_dim));   // ×
                }
            }
        }
    }
    if (jobs.nwait) {
        CLAY(CLAY_ID("jobs-waiting"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(JOB_WAIT_H) }, .padding = { PAD, PAD, 0, 0 },
                                                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                        .backgroundColor = cc(t->bg_light) }) {
            text(cz(arena_sprintf(&frame, "%d waiting", jobs.nwait)), cc(t->fg_dim));
        }
    }
}

static Str  list_name(void *ctx, isize i);
static bool list_dir(void *ctx, isize i);

// The find bar's count and the cursor's place among the matches (ui_find_count), counted again only
// when the listing, the text, its toggles or the cursor changed: not on every hover's frame.
static isize find_count(isize *at)
{
    static struct { int lists; isize sel, n, at; UiMatchOpts o; int len, find_len; char text[UI_FIND_MAX + 1]; bool valid, finding; } c;
    const UiMatcher *m = &app.ui.find_m;
    if (!c.valid || c.finding != app.ui.finding || c.find_len != app.ui.find.len || c.lists != app.lists || c.sel != app.ui.list.sel || c.len != m->len || memcmp(c.text, m->text, (size_t)m->len) ||
        c.o.cased != m->o.cased || c.o.word != m->o.word || c.o.regex != m->o.regex) {
        c.n = ui_find_count(&app.ui, (UiNames){ list_name, NULL, list_dir }, &c.at);
        c.finding = app.ui.finding, c.find_len = app.ui.find.len, c.lists = app.lists, c.sel = app.ui.list.sel, c.len = m->len, c.o = m->o, c.valid = true;
        memcpy(c.text, m->text, (size_t)m->len);
    }
    *at = c.at;
    return c.n;
}

// The count's words: "2 of 5", or "5 matches" with the cursor off them.
static const char *find_label(isize n, isize at, char *buf, size_t cap)
{
    if (at > 0) snprintf(buf, cap, "%td of %td", at, n);
    else snprintf(buf, cap, n == 1 ? "%td match" : "%td matches", n);
    return buf;
}

// A find bar chevron: dimmed and inert under two matches.
static void find_chevron(Clay_ElementId id, ChevronDir dir, int step, bool on)
{
    Theme *t = &app.theme;
    bool hover = on && app.hov.find_step == step;
    CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(26), CLAY_SIZING_FIXED(26) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
               .backgroundColor = hover ? cc(t->bg) : NO_COLOR }) {
        chevron(Clay_GetElementId(cz(arena_sprintf(&frame, "%.*s-icon", (int)id.stringId.length, id.stringId.chars))), dir, on ? cc(t->fg) : dim_color());
    }
}

// A bar's field, find's or the search view's (name: "find", "search", "inside" for Containing): its frame red when the
// expression is bad, the accent while it has the keys; its text drawn as the edit line draws (a
// caret and the selection) while it has them, cut to fit otherwise; the toggles at its end.
static void bar_field(const char *name, bool keys, bool bad, UiLine line, Str shown, Clay_Color fg, UiMatchOpts o, int toggles)
{
    Theme *t = &app.theme;
    Clay_Color edge = bad ? cc(t->red) : keys ? cc(t->accent) : rule_color();
    Clay_ElementId field = Clay_GetElementId(cz(arena_sprintf(&frame, "%s-field", name)));
    CLAY(field, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(26) }, .padding = { 8, 3, 0, 0 }, .childGap = 6,
                              .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                  .backgroundColor = cc(t->bg), .border = { .color = edge, .width = CLAY_BORDER_OUTSIDE(1) } }) {
        Clay_ElementData fd = Clay_GetElementData(field);
        float room = fd.found && fd.boundingBox.width > 140 ? fd.boundingBox.width - 110 : 400;
        CLAY(Clay_GetElementId(cz(arena_sprintf(&frame, "%s-text", name))),
             { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .clip = { .horizontal = true } }) {
            if (keys) edit_text(t, name, room, line, fg);
            else text(fit(shown, room, true), fg);
        }
        layout_toggles(o, toggles);
    }
}

// The find bar (Michael, 2026-10-26): over the job rows, as wide as the files. Its field (red text
// when nothing matches, a caret while it has the keys) with the three toggles, a bad expression's
// reason or the count ("2 of 5", "No match"), the chevrons to the previous and the next match, and
// a × that closes it.
static void layout_find_bar(void)
{
    Theme *t = &app.theme;
    Ui *u = &app.ui;
    isize at, n = find_count(&at);
    bool bad = u->find_m.bad && u->find_opts.regex && u->find.len;
    CLAY(CLAY_ID("find-bar"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(FIND_BAR_H) }, .padding = { PAD, 8, 0, 0 }, .childGap = 10,
                                            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                .backgroundColor = cc(t->bg_light),
                                .border = { .color = rule_color(), .width = { .top = 1 } } }) {
        text(cz(u->find_select ? "Select" : "Find"), cc(t->fg_dim));   // Select Items Matching (F7.4)
        bar_field("find", u->focus == UI_FOCUS_FIND, bad, ui_find_line(u), (Str){ u->find.b, u->find.len }, u->find_miss ? cc(t->red) : cc(t->fg), u->find_opts, 0);   // (red text: no match)
        // At least as wide as "88 of 88", so the chevrons don't move as the count grows.
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIT(str_width(S("88 of 88")), 0), CLAY_SIZING_FIT(0) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
            if (bad) text(fit(str_from_c(u->find_m.err), 240, false), cc(t->red));
            else if (u->find_miss) text(cz("No match"), cc(t->red));
            else if (n > 0) text(cz(find_label(n, at, arena_new(&frame, char, 48), 48)), cc(t->fg_dim));
        }
        CLAY_AUTO_ID({ .layout = { .childGap = 2, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
            find_chevron(CLAY_ID("find-prev"), CHEV_UP, -1, n >= 2);
            find_chevron(CLAY_ID("find-next"), CHEV_DOWN, 1, n >= 2);
        }
        bool hover = app.hov.find_x;
        CLAY(CLAY_ID("find-x"), { .layout = { .sizing = { CLAY_SIZING_FIXED(26), CLAY_SIZING_FIXED(26) },
                                              .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                                  .backgroundColor = hover ? cc(t->bg) : NO_COLOR }) {
            text(cz("\xC3\x97"), hover ? cc(t->red) : cc(t->fg_dim));   // ×
        }
    }
}

// The "In files" button (F9 step 3): lit while on.
static void search_inside_btn(void)
{
    Theme *t = &app.theme;
    bool on = app.ui.search_inside, hover = app.hov.inside_btn;
    CLAY(CLAY_ID("search-inside"), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(26) }, .padding = { 10, 10, 0, 0 },
                                                 .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                     .backgroundColor = on ? cc(t->bg) : hover ? mix(t->bg, t->bg_light, 0.5f) : NO_COLOR,
                                     .border = { .color = on ? cc(t->accent) : rule_color(), .width = CLAY_BORDER_OUTSIDE(1) } }) {
        text(cz("In files"), on ? cc(t->accent) : cc(t->fg_dim));
    }
}

// The search view's bar (F9): where it searches, its field (a caret while it has the keys) with
// find's toggles, a bad expression's reason, and "In files"; inside files (step 3) a second row,
// "Containing", with its field and toggles. The labels share a width, so the fields line up.
static void layout_search_bar(void)
{
    Theme *t = &app.theme;
    Ui *u = &app.ui;
    bool bad = app.search_m.bad && u->find_opts.regex && u->search.len;
    bool ibad = app.search_im.bad && u->inside_opts.regex && u->inside.len;
    char where[PATH_MAX + 16];
    ui_tilde(where, sizeof where, app.search_root, getenv("HOME"));
    Str all = str_from_c(arena_sprintf(&frame, "Search in %s", where));
    float lw = str_width(all) < 300 ? str_width(all) : 300;
    if (lw < str_width(S("Containing"))) lw = str_width(S("Containing"));
    CLAY(CLAY_ID("search-bar"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(view_bar_h()) }, .padding = { PAD, PAD, 6, 6 },
                                              .layoutDirection = CLAY_TOP_TO_BOTTOM, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                  .backgroundColor = cc(t->bg_light) }) {
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(26) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
            CLAY(CLAY_ID("search-where"), { .layout = { .sizing = { CLAY_SIZING_FIXED(lw), CLAY_SIZING_FIT(0) } }, .clip = { .horizontal = true } }) {
                text(fit(all, 300, true), cc(t->fg_dim));
            }
            bar_field("search", u->focus == UI_FOCUS_SEARCH, bad, ui_search_line(u), (Str){ u->search.b, u->search.len }, cc(t->fg), u->find_opts, TOGGLE_SEARCH);
            if (bad) text(fit(str_from_c(app.search_m.err), 240, false), cc(t->red));
            search_inside_btn();
        }
        if (u->search_inside) {
            CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(26) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                CLAY(CLAY_ID("inside-label"), { .layout = { .sizing = { CLAY_SIZING_FIXED(lw), CLAY_SIZING_FIT(0) } } }) { text(cz("Containing"), cc(t->fg_dim)); }
                bar_field("inside", u->focus == UI_FOCUS_INSIDE, ibad, ui_inside_line(u), (Str){ u->inside.b, u->inside.len }, cc(t->fg), u->inside_opts, TOGGLE_INSIDE);
                if (ibad) text(fit(str_from_c(app.search_im.err), 240, false), cc(t->red));
                // (as wide as "In files" above, as the last frame laid it out, so both fields end together)
                Clay_ElementData b = Clay_GetElementData(CLAY_ID("search-inside"));
                CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(b.found ? b.boundingBox.width : str_width(S("In files")) + 20), CLAY_SIZING_FIT(0) } } }) {}
            }
        }
    }
}

// The bottom bar's sidebar part (F6): as wide as the sidebar, or the buttons' own width when it
// is hidden.
static float modes_w(void)
{
    return side_w() ? (float)side_w() : (float)(2 * PAD + SIDE_MODES * MODE_BTN_W + (SIDE_MODES - 1) * MODE_GAP);
}

// A mode button's picture, drawn in the theme's colour: a window with a sidebar of places, the
// same with a tree's steps in it, and a window alone.
static void mode_picture(int k, Clay_Color c)
{
    CLAY(CLAY_IDI("mode-pic", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(12) }, .padding = { 2, 2, 2, 2 },
                                                .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2 },
                                    .border = { .color = c, .width = CLAY_BORDER_OUTSIDE(1) } }) {
        if (k == SIDE_PLACES) {
            CLAY(CLAY_IDI("mode-side", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(4), CLAY_SIZING_GROW(0) } }, .backgroundColor = c }) {}
        } else if (k == SIDE_TREE) {
            for (int j = 0; j < 3; j++)
                CLAY(CLAY_IDI("mode-step", j), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) }, .padding = { (uint16_t)(j * 3), 0, 0, 0 } } }) {
                    CLAY(CLAY_IDI("mode-line", j), { .layout = { .sizing = { CLAY_SIZING_FIXED(6), CLAY_SIZING_FIXED(1) } }, .backgroundColor = c }) {}
                }
        }
    }
}

// The three mode buttons: places, tree, hidden; the one on in the accent.
static void layout_modes(void)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("modes"), { .layout = { .sizing = { CLAY_SIZING_FIXED(modes_w()), CLAY_SIZING_GROW(0) }, .padding = { PAD, PAD, 0, 0 },
                                         .childGap = MODE_GAP, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        for (int k = 0; k < SIDE_MODES; k++) {
            bool on = app.ui.side_mode == (SideMode)k, hover = app.hov.mode == k;
            CLAY(CLAY_IDI("mode", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(MODE_BTN_W), CLAY_SIZING_FIXED(MODE_BTN_H) },
                                                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                                        .backgroundColor = on ? mix(t->accent, t->bg_light, 0.25f) : hover ? cc(t->bg) : NO_COLOR,
                                        .cornerRadius = CLAY_CORNER_RADIUS(3) }) {
                mode_picture(k, on ? cc(t->accent) : hover ? cc(t->fg) : cc(t->fg_dim));
            }
        }
    }
}

// The status line when no message is up (ui_status_info): the marked entry, or how many, or the
// folder's count and its drive's free space.
static void status_info(char *buf, size_t cap)
{
    UiInfo in = { .n = app.list.n, .hidden = app.show_hidden ? 0 : app.list.hidden, .marked = app.ui.marked,
                  .one_count = -1, .one_size = -1, .avail = app.cwd_avail,
                  .cramped = app.ui.perms_cols && !app.ui.grid && !perms_shown((float)GetScreenWidth()),
                  .changed = git_shown() ? git_shown()->g.changed : 0 };
    for (isize i = 0; app.ui.marked == 1 && i < app.list.n; i++) {
        if (!app.ui.mark[i]) continue;
        const Entry *e = &app.list.e[i];
        in.one = e->name, in.one_dir = kind_is_dir(e->kind);
        in.one_count = app.count[i] >= 0 ? app.count[i] : -1;
        if (!in.one_dir && e->kind != K_LINK_BROKEN && e->kind != K_OTHER) in.one_size = e->size;
        if (in.one_dir && app.count[i] == COUNT_UNKNOWN) {
            char path[PATH_MAX];
            if (entry_path(i, path, sizeof path) && count_ask(i, path, app.show_hidden)) app.count[i] = COUNT_ASKED;
        }
    }
    if (!app.have_list) in.n = 0, in.avail = -1;
    ui_status_info(buf, cap, &in);
}

// The search view's status line (F9) when nothing is marked: how many it found, while it searches or
// when it is done, and what it left out.
static void search_status(char *buf, size_t cap)
{
    char skipped[400];
    search_skips_text(&app.search_s, &app.skips_shown, skipped, sizeof skipped);
    const char *sep = skipped[0] ? " \xC2\xB7 " : "";
    // Inside files (step 3): how many files it read, as well.
    bool inside = app.ui.search_inside && app.ui.inside.len;
    char *read = inside ? arena_sprintf(&frame, " in %lld file%s", (long long)app.search_s.files, app.search_s.files == 1 ? "" : "s") : "";
    if (!inside && !app.ui.search.len)
        snprintf(buf, cap, app.ui.search_inside ? "Text to look for inside files; the name field narrows which (*.ts)" : "A name, or part of one; * and ? as wildcards (*.ts)");
    else if (!app.search_done) snprintf(buf, cap, "Searching\xE2\x80\xA6 %td found%s%s%s", app.list.n, read, sep, skipped);
    else if (app.search_s.full) snprintf(buf, cap, "%td found, the most a search shows%s%s%s", app.list.n, read, sep, skipped);
    else snprintf(buf, cap, "%td found%s%s%s", app.list.n, read, sep, skipped);
}
