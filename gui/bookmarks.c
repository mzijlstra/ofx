// Edit Bookmarks (F7.5, docs/plan.md "F7.5 decided"): the panel over the window that lists the
// bookmarks file's lines, and the changes it makes to the file. ui/ has its keys (ui_bmp_event); here
// each change reads the file again, finds the line the panel showed (bm_locate: by its text, not its
// place, so a change Nemo made meanwhile is never undone), changes it, writes the file and reads the
// places again. Included by main.c after openwith.c.

enum { BMP_ROWS_MAX = 12, BMP_ROW_H = 30, BMP_W = 620 };

static int bmp_rows(int h)
{
    int r = (h - 240) / BMP_ROW_H;
    return r < 1 ? 1 : r > BMP_ROWS_MAX ? BMP_ROWS_MAX : r;
}

// The panel's list as the file is now (after a change, or opened): the cursor on line at.
static void bmp_refresh(int at)
{
    app.ui.bm_total = bmarks.n;
    ui_set(&app.ui.bmp_list, bmarks.n, at < 0 ? 0 : at >= bmarks.n ? bmarks.n - 1 : at);
}

// The file changed since the list was shown (its line is elsewhere or gone): the list as it is
// now, and said.
static void bm_changed(int i)
{
    places_reload(), bmp_refresh(i);
    set_status(true, "The bookmarks changed meanwhile: here they are as they are now");
}

// A change asked for line i of the list shown: the file read again, the line found there, op done
// (UA_BM_REMOVE, UP, DOWN, RENAME), written; the cursor follows the line.
static void bmp_change(UiActionKind op, int i)
{
    char file[PATH_MAX];
    bookmarks_path(file, sizeof file);
    static Bookmarks now;
    arena_reset(&bmwa);
    Err err = { 0 };
    if (!bm_read(&bmwa, file, &now, &err)) {
        set_status(true, "Bookmarks not changed: %s", err.msg);
        return;
    }
    if (now.full) {
        set_status(true, "The bookmarks file has more than %d lines: ofx leaves it as it is", BM_MAX);
        return;
    }
    int j = bm_locate(&now, &bmarks, i);
    if (j < 0) {
        bm_changed(i);
        return;
    }
    int k = bmarks.n == 0 ? 0 : i;   // the line the cursor stays on, in the list shown
    // A move swaps with the line shown next to it, found again too (so lines added meanwhile
    // elsewhere stay where they were).
    if (op == UA_BM_UP || op == UA_BM_DOWN) {
        int other = bm_locate(&now, &bmarks, op == UA_BM_UP ? i - 1 : i + 1);
        if (other < 0) {
            bm_changed(i);
            return;
        }
        bm_swap(&now, j, other);
        k = op == UA_BM_UP ? i - 1 : i + 1;
    } else if (op == UA_BM_REMOVE) {
        bm_remove(&now, j);
    } else if (op == UA_BM_RENAME) {
        bm_rename(&bmwa, &now, j, (Str){ app.ui.bmp_name.b, app.ui.bmp_name.len });
    }
    Str text = bm_format(&bmwa, &now);
    bool ok = now.n == 0 || text.len > 0;
    if (!ok) err_set(&err, "the bookmarks are too long to write");
    if (!ok || !bm_write(file, text, &err)) {
        set_status(true, "Bookmarks not changed: %s", err.msg);
        return;
    }
    places_reload();
    bmp_refresh(k);
}

// What gui/ does for the panel's actions.
static void bmp_act(UiAction a)
{
    switch (a.kind) {
    case UA_BM_EDIT: places_reload(), bmp_refresh(app.ui.bmp_list.sel); break;
    case UA_BM_GO:
        if (a.index < 0 || a.index >= bmarks.n) break;
        if (bmarks.b[a.index].path[0]) go_to(bmarks.b[a.index].path, STR_EMPTY, 0);
        else set_status(true, "ofx opens local folders only: %.*s", STR_ARG(bmarks.b[a.index].uri));
        break;
    case UA_BM_NAME: {
        // The field starts with its label, or the folder's name, all of it; the caret at its end.
        Str s = a.index < bmarks.n ? (bmarks.b[a.index].label.len || !bmarks.b[a.index].path[0] ? bmarks.b[a.index].label : bm_label(&bmarks.b[a.index]))
                                   : STR_EMPTY;
        ui_line_set(ui_bmp_line(&app.ui), s);
        break;
    }
    case UA_BM_REMOVE: case UA_BM_UP: case UA_BM_DOWN: case UA_BM_RENAME: bmp_change(a.kind, (int)a.index); break;
    default: break;
    }
}

// One of the panel's buttons (UI_BMP_*): lit under the pointer,
// dimmed when it can't act now.
static void bmp_button(int k, const char *label, bool on)
{
    Theme *t = &app.theme;
    bool hover = on && app.hov.bmp_btn == k;
    CLAY(CLAY_IDI("bmp-btn", k), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(32) }, .padding = { 14, 14, 0, 0 },
                                               .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                   .backgroundColor = hover ? cc(t->bg) : cc(t->bg_light),
                                   .border = { .color = rule_color(), .width = CLAY_BORDER_OUTSIDE(1) } }) {
        text(cz(label), on ? cc(t->fg) : rule_color());
    }
}

static void layout_bmp_panel(void)
{
    Theme *t = &app.theme;
    Ui *u = &app.ui;
    UiList *l = &u->bmp_list;
    CLAY(CLAY_ID("bmp"), { .layout = { .sizing = { CLAY_SIZING_FIXED(BMP_W), CLAY_SIZING_FIT(0) }, .padding = { 24, 24, 20, 20 }, .childGap = 10,
                                       .layoutDirection = CLAY_TOP_TO_BOTTOM },
                           .backgroundColor = cc(t->bg_light),
                           .border = { .color = cc(t->accent), .width = CLAY_BORDER_OUTSIDE(1) },
                           .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 20,
                                         .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER } } }) {
        text(cz("Bookmarks"), cc(t->fg));
        // A row a line of the file: its name, where it goes (dimmer); the cursor's lit; renaming, the
        // name is a field with its caret.
        CLAY(CLAY_ID("bmp-list"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(l->rows * BMP_ROW_H) }, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                                    .clip = { .vertical = true } }) {
            if (l->n == 0)
                CLAY_AUTO_ID({ .layout = { .padding = { 8, 0, 8, 0 } } }) {
                    const char *k = ui_cmd_key(UK_ADD_BOOKMARK, 0);   // (as bound, F15)
                    text(cz(k[0] ? arena_sprintf(&frame, "No bookmarks: %s bookmarks the folder shown", k) : "No bookmarks: Bookmarks \xE2\x86\x92 Add Bookmark"), cc(t->fg_dim));
                }
            for (int r = 0; r < l->rows; r++) {
                isize v = l->top + r;
                if (v >= l->n || v >= bmarks.n) break;
                const Bookmark *b = &bmarks.b[v];
                bool sel = v == l->sel;
                CLAY(CLAY_IDI("bmp-row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(BMP_ROW_H) }, .padding = { 8, 8, 0, 0 }, .childGap = 14,
                                                           .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                               .backgroundColor = sel ? mix(t->accent, t->bg_light, 0.45f) : cc(t->bg_light) }) {
                    if (sel && u->bmp_naming) {
                        CLAY(CLAY_ID("bmp-field"), { .layout = { .sizing = { CLAY_SIZING_FIXED(240), CLAY_SIZING_FIXED(24) }, .padding = { 6, 6, 0, 0 },
                                                                 .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                                     .backgroundColor = cc(t->bg), .border = { .color = cc(t->accent), .width = CLAY_BORDER_OUTSIDE(1) },
                                                     .clip = { .horizontal = true } }) {
                            edit_text(t, "bmp", 220, ui_bmp_line(u), cc(t->fg));   // (as the edit line draws, 2026-10-28)
                        }
                    } else {
                        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(240), CLAY_SIZING_FIT(0) } }, .clip = { .horizontal = true } }) {
                            text(fit(b->path[0] ? bm_label(b) : b->label.len ? b->label : b->uri, 240, false), cc(t->fg));
                        }
                    }
                    Str where = b->path[0] ? str_from_c(b->path) : b->uri;
                    text(fit(where, BMP_W - 330, true), cc(t->fg_dim));
                }
            }
        }
        bool any = l->n > 0 && !u->bmp_naming;
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = { 0, 0, 6, 0 }, .childGap = 8,
                                   .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
            bmp_button(UI_BMP_REMOVE, "Remove", any);
            bmp_button(UI_BMP_RENAME, "Rename", any);
            bmp_button(UI_BMP_UP, "Move Up", any && l->sel > 0);
            bmp_button(UI_BMP_DOWN, "Move Down", any && l->sel < l->n - 1);
            CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
            modal_button(UI_MODAL_NO, "Close", true, false);
        }
    }
}
