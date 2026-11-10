// The branches panel (F12 step 6, docs/plan.md "F12 design"): every local branch of the folder's
// repository, from the branch menu's "All branches… (N)", as Open With's panel: a filter field, the
// list in the menu's order (checked out last first, then by commit date), each row with its last
// commit's age; Enter or a double click switches as the menu does (UA_BRANCH). The list is the
// menu's (git.br, read as the menu opened). Included by panels.c after openwith.c.

enum { BRP_W = 560, BRP_ROW_H = 26, BRP_ROWS_MAX = 16 };

static int brp_rows(int h)
{
    int r = (h - 260) / BRP_ROW_H;
    return r < 1 ? 1 : r > BRP_ROWS_MAX ? BRP_ROWS_MAX : r;
}

// The names ui/ filters, as git.br has them (each frame the branch menu or the panel is up; git.br
// changes only when the menu opens: git_ask_branches).
static const Str *brp_names(void)
{
    static Str names[GIT_BRANCHES_MAX];
    for (int i = 0; i < git.br.n; i++) names[i] = str_from_c(git.br.name[i]);
    return names;
}

static void layout_brp_panel(void)
{
    Theme *t = &app.theme;
    Ui *u = &app.ui;
    i64 now = (i64)time(NULL);
    CLAY(CLAY_ID("brp"), panel_box(BRP_W, 10, app.theme.accent)) {
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 8 } }) {
            text(cz("Branches"), cc(t->fg));
            CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
            // (more than it holds: the latest GIT_BRANCHES_MAX of them)
            text(cz(git.br.more ? arena_sprintf(&frame, "the latest %d of more than %d", git.br.n, git.br.total) : git.br.total > git.br.n ? arena_sprintf(&frame, "the latest %d of %d", git.br.n, git.br.total) : arena_sprintf(&frame, "%d", git.br.n)),
                 cc(t->fg_dim));
        }
        layout_pick_field(CLAY_ID("brp-field"), "brp", &u->brp, BRP_W - 80, NULL);
        // The list: a row a branch, the one checked out with the dot, its last commit's age at the right.
        CLAY(CLAY_ID("brp-list"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(u->brp.list.rows * BRP_ROW_H) },
                                                .layoutDirection = CLAY_TOP_TO_BOTTOM },
                                    .clip = { .vertical = true } }) {
            if (u->brp.list.n == 0)
                CLAY_AUTO_ID({ .layout = { .padding = { 8, 0, 8, 0 } } }) { text(cz(git.br.n ? "No branch matches" : "No branches"), cc(t->fg_dim)); }
            for (int r = 0; r < u->brp.list.rows; r++) {
                isize v = u->brp.list.top + r;
                if (v >= u->brp.list.n) break;
                isize i = u->brp_idx[v];
                if (i >= git.br.n) continue;
                bool sel = v == u->brp.list.sel, cur = i == git.br.cur;
                Clay_Color bg = pick_row_bg(&u->brp, sel);
                char age[32];
                if (git.br.when[i] > 0) ui_age(age, sizeof age, now, git.br.when[i]);
                else age[0] = 0;   // (HEAD's branch past a cut list: no time read)
                CLAY(CLAY_IDI("brp-row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(BRP_ROW_H) }, .padding = { 8, 8, 0, 0 },
                                                           .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                               .backgroundColor = bg }) {
                    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(10), CLAY_SIZING_FIT(0) } } }) {
                        if (cur) text(cz("\xE2\x80\xA2"), cc(t->accent));   // • (as the menu marks it)
                    }
                    text(fit(str_from_c(git.br.name[i]), BRP_W - 190, true), cur ? cc(t->accent) : cc(t->fg));
                    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
                    text(cz(arena_sprintf(&frame, "%s", age)), cc(t->fg_dim));
                }
            }
        }
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = { 0, 0, 6, 0 }, .childGap = 10,
                                   .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
            modal_button(UI_MODAL_NO, "Cancel", false, false);
            modal_button(UI_MODAL_YES, "Switch", true, false);
        }
    }
}
