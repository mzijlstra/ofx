// A server's shares (F16 step 7a; Michael, 2026-11-10: a picker): put up once Connect to Server has
// read them from a server's address alone (smb://nas), as the branches panel is: a filter field, the
// list (sorted, gvfs's hidden ones left out: fs/connect.c gio_server_shares); Enter or a double
// click connects to the one under the cursor as if its address were typed (UA_SHARE). Included by
// panels.c after branches.c.

// What the panel shows: the server's address the shares were read from, its name, the names (gui/'s
// copy: the panel's list, and UA_SHARE's index into it), and whether the panel waits for another
// one to close (a panel opened while it connected stays). Main thread only.
static struct {
    char server[PATH_MAX], host[256];
    int  n;
    char name[SERVER_SHARES_MAX][SERVER_SHARE_NAME];
    bool pending;
} shp;
_Static_assert((int)SERVER_SHARES_MAX <= (int)UI_SHARES_MAX, "the panel holds every share fs/connect.c reads");

// The names ui/ filters, as shp has them.
static const Str *shp_names(void)
{
    static Str names[SERVER_SHARES_MAX];
    for (int i = 0; i < shp.n; i++) names[i] = str_from_c(shp.name[i]);
    return names;
}

// A server's shares as Connect to Server read them (connect_take, its lock held): kept for the panel.
static void shares_keep(const char *server, const char *host, const char (*names)[SERVER_SHARE_NAME], int n)
{
    snprintf(shp.server, sizeof shp.server, "%s", server), snprintf(shp.host, sizeof shp.host, "%s", host);
    shp.n = n;
    memcpy(shp.name, names, sizeof shp.name);
}

// The shares kept, to be shown once nothing else is up (shares_show): how many (0: none, not shown).
static int shares_offer(void)
{
    shp.pending = shp.n > 0;
    return shp.n;
}

// Another connection started: the shares not yet shown aren't.
static void shares_drop(void)
{
    shp.pending = false;
}

// A server's shares panel put up once nothing else is (a panel, a menu or the location line opened
// while it connected stays theirs). True when it opened.
static bool shares_show(void)
{
    Ui *u = &app.ui;
    if (!shp.pending || u->panel != UI_PANEL_NONE || u->menu_on || u->focus == UI_FOCUS_EDIT || ui_on_top(u) != UI_ON_NONE) return false;
    shp.pending = false;
    ui_shp_open(u);
    return true;
}

// A share picked in the panel (UA_SHARE): its address, the server's and its name, connected to as if
// typed.
static void share_pick(isize i)
{
    char uri[PATH_MAX];
    if (i < 0 || i >= shp.n) return;
    if (!server_share_uri(shp.server, shp.name[i], uri, sizeof uri)) return (void)set_status(true, "The address is too long");
    connect_start(str_from_c(uri));
}

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
    CLAY(CLAY_ID("shp"), panel_box(SHP_W, 10, app.theme.accent)) {
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
