// A server's shares (F16 step 7a; Michael, 2026-11-10: a picker): put up once Connect to Server has
// read them from a server's address alone (smb://nas), as the branches panel is: a filter field, the
// list (sorted, gvfs's hidden ones left out: fs/connect.c gio_server_shares); Enter or a double
// click connects to the one under the cursor as if its address were typed (UA_SHARE). Included by
// panels.c after branches.c.

enum { SHP_W = 480, SHP_ROW_H = 26, SHP_ROWS_MAX = 12 };

static int shp_rows(int h)
{
    int r = (h - 260) / SHP_ROW_H;
    return r < 1 ? 1 : r > SHP_ROWS_MAX ? SHP_ROWS_MAX : r;
}

static void layout_shp_panel(void)
{
    Theme *t = &app.theme;
    Ui *u = &app.ui;
    CLAY(CLAY_ID("shp"), { .layout = { .sizing = { CLAY_SIZING_FIXED(SHP_W), CLAY_SIZING_FIT(0) }, .padding = { 24, 24, 20, 20 }, .childGap = 10,
                                       .layoutDirection = CLAY_TOP_TO_BOTTOM },
                           .backgroundColor = cc(t->bg_light),
                           .border = { .color = cc(t->accent), .width = CLAY_BORDER_OUTSIDE(1) },
                           .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 20,
                                         .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER } } }) {
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 8 } }) {
            text(fit(str_from_c(arena_sprintf(&frame, "Shares on %s", shp.host)), SHP_W - 110, true), cc(t->fg));
            CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
            text(cz(arena_sprintf(&frame, "%d", shp.n)), cc(t->fg_dim));
        }
        layout_pick_field(CLAY_ID("shp-field"), "shp", &u->shp, SHP_W - 80, NULL);
        CLAY(CLAY_ID("shp-list"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(u->shp.list.rows * SHP_ROW_H) },
                                                .layoutDirection = CLAY_TOP_TO_BOTTOM },
                                    .clip = { .vertical = true } }) {
            if (u->shp.list.n == 0)
                CLAY_AUTO_ID({ .layout = { .padding = { 8, 0, 8, 0 } } }) { text(cz("No share matches"), cc(t->fg_dim)); }
            for (int r = 0; r < u->shp.list.rows; r++) {
                isize v = u->shp.list.top + r;
                if (v >= u->shp.list.n) break;
                isize i = u->shp_idx[v];
                if (i >= shp.n) continue;
                CLAY(CLAY_IDI("shp-row", r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(SHP_ROW_H) }, .padding = { 8, 8, 0, 0 },
                                                           .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                               .backgroundColor = pick_row_bg(&u->shp, v == u->shp.list.sel) }) {
                    text(fit(str_from_c(shp.name[i]), SHP_W - 80, true), cc(t->fg));
                }
            }
        }
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = { 0, 0, 6, 0 }, .childGap = 10,
                                   .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
            modal_button(UI_MODAL_NO, "Cancel", false, false);
            modal_button(UI_MODAL_YES, "Connect", true, false);
        }
    }
}
