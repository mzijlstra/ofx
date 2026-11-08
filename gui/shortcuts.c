// The Keyboard Shortcuts panel (F7.5, docs/plan.md "F7.5 decided"): ui/'s table (UI_KEYS) as its
// filter leaves it, a heading a section, a row an action with its key and a second one. Included by
// main.c after bookmarks.c.

enum { KEYS_ROW_H = 26, KEYS_ROWS_MAX = 18, KEYS_W = 860, KEYS_NAME_W = 140, KEYS_KEY_W = 150 };

static int keys_rows(int h)
{
    int r = (h - 200) / KEYS_ROW_H;
    return r < 1 ? 1 : r > KEYS_ROWS_MAX ? KEYS_ROWS_MAX : r;
}

// The first keycap the panel drew this frame, and whether it is one of yours (a script's keyscap
// and keyscapmine, F15: the keys come from ui/keys.c).
static const char *keys_cap_drawn;
static bool        keys_cap_mine;

// A key as a little keycap, cut to fit width; one of yours (ofx.conf's, F15 step 3) in the accent
// colour.
static void keycap(const char *k, bool mine, float width)
{
    Theme *t = &app.theme;
    if (!k[0]) return;
    if (!keys_cap_drawn) keys_cap_drawn = k, keys_cap_mine = mine;
    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(20) }, .padding = { 6, 6, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                   .backgroundColor = cc(t->bg), .cornerRadius = CLAY_CORNER_RADIUS(3),
                   .border = { .color = mine ? cc(t->accent) : rule_color(), .width = CLAY_BORDER_OUTSIDE(1) } }) {
        text(fit(str_from_c(k), width - 12, false), mine ? cc(t->accent) : cc(t->fg));
    }
}

static void layout_keys_panel(void)
{
    keys_cap_drawn = NULL;
    Theme *t = &app.theme;
    Ui *u = &app.ui;
    UiList *l = &u->keys_list;
    CLAY(CLAY_ID("keys"), { .layout = { .sizing = { CLAY_SIZING_FIXED(KEYS_W), CLAY_SIZING_FIT(0) }, .padding = { 24, 24, 20, 20 }, .childGap = 10,
                                        .layoutDirection = CLAY_TOP_TO_BOTTOM },
                            .backgroundColor = cc(t->bg_light),
                            .border = { .color = cc(t->accent), .width = CLAY_BORDER_OUTSIDE(1) },
                            .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 20,
                                          .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER } } }) {
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
            text(cz("Keyboard Shortcuts"), cc(t->fg));
            // The filter: what is typed, a caret; a hint when empty.
            CLAY(CLAY_ID("keys-field"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(28) }, .padding = { 8, 8, 0, 0 },
                                                      .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                          .backgroundColor = cc(t->bg), .border = { .color = cc(t->accent), .width = CLAY_BORDER_OUTSIDE(1) },
                                          .clip = { .horizontal = true } }) {
                if (!u->keys_filter.len) {
                    text(cz("Type to filter"), cc(t->fg_dim));
                    CLAY(CLAY_ID("keys-caret"), { .layout = { .sizing = { CLAY_SIZING_FIXED(2), CLAY_SIZING_FIXED(FONT_PX + 2) } }, .backgroundColor = cc(t->accent) }) {}
                } else {
                    edit_text(t, "keys", KEYS_W - 300, ui_keys_line(u), cc(t->fg));   // (as the edit line draws, 2026-10-28)
                }
            }
        }
        CLAY(CLAY_ID("keys-list"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(l->rows * KEYS_ROW_H) }, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                                     .clip = { .vertical = true } }) {
            if (l->n == 0)
                CLAY_AUTO_ID({ .layout = { .padding = { 8, 0, 8, 0 } } }) { text(cz("No shortcut matches"), cc(t->fg_dim)); }
            for (int r = 0; r < l->rows; r++) {
                isize v = l->top + r;
                if (v >= l->n) break;
                int line = u->keys_line[v];
                if (line < 0) {   // a section's heading
                    CLAY(CLAY_IDI("keys-row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(KEYS_ROW_H) }, .padding = { 4, 8, 0, 0 },
                                                                .childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM } },
                                                    .border = { .color = v ? mix(t->fg_dim, t->bg_light, 0.3f) : cc(t->bg_light), .width = { .top = v ? 1 : 0 } } }) {
                        text(cz(UI_KEY_SECTION[-1 - line]), cc(t->accent));
                    }
                    continue;
                }
                CLAY(CLAY_IDI("keys-row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(KEYS_ROW_H) }, .padding = { 16, 8, 0, 0 }, .childGap = 8,
                                                            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                    text(fit(str_from_c(UI_KEYS[line].action), KEYS_W - 380 - KEYS_NAME_W, false), cc(t->fg));
                    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
                    // (the command's name in ofx.conf, dimmed: what a key line names, F15 step 3)
                    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(KEYS_NAME_W), CLAY_SIZING_FIT(0) } } }) {
                        text(fit(str_from_c(ui_keys_name(line)), KEYS_NAME_W - 8, false), cc(t->fg_dim));
                    }
                    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(KEYS_KEY_W), CLAY_SIZING_FIT(0) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
                        keycap(ui_keys_key(line, 0), ui_keys_mine(line, 0), KEYS_KEY_W);
                    }
                    CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(150), CLAY_SIZING_FIT(0) }, .childAlignment = { .x = CLAY_ALIGN_X_LEFT } } }) {
                        keycap(ui_keys_key(line, 1), ui_keys_mine(line, 1), 150);
                    }
                }
            }
        }
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = { 0, 0, 4, 0 }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
            modal_button(UI_MODAL_NO, "Close", true, false);
        }
    }
}
