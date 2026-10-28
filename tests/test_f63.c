// F6.3's unit tests: the icon view's sizes, from the keys, the wheel and the slider; the cursor's
// row kept across a zoom; the size remembered. Included by test.c.

static UiAction zoom_by(Ui *u, UiInput in)
{
    return ui_handle(u, (UiNames){ name_at, NULL, NULL }, &in);
}

static void test_zoom_levels(void)
{
    // The sizes, and the nearest one for any other (a state file written by hand).
    CHECK(UI_ZOOMS == 6 && UI_ZOOM_PX[0] == 32 && UI_ZOOM_PX[UI_ZOOMS - 1] == 256 && UI_ZOOM_PX[1] == UI_ZOOM_DEFAULT);
    CHECK(ui_zoom_level(0) == 1 && ui_zoom_level(-5) == 1 && ui_zoom_level(48) == 1 && ui_zoom_level(32) == 0);
    CHECK(ui_zoom_level(1) == 0 && ui_zoom_level(39) == 0 && ui_zoom_level(41) == 1 && ui_zoom_level(9999) == 5);
    CHECK(ui_zoom_level(64) == 2 && ui_zoom_level(96) == 3 && ui_zoom_level(128) == 4 && ui_zoom_level(256) == 5);
    CHECK(ui_zoom_level(191) == 4 && ui_zoom_level(193) == 5 && ui_zoom_level(80) == 2 && ui_zoom_level(81) == 3);
    static Ui u;
    u = (Ui){ 0 };
    CHECK(ui_zoom_px(&u) == 48);
    u.zoom = 100;
    CHECK(ui_zoom_px(&u) == 96);
}

static void test_zoom_keys(void)
{
    static Ui u;
    grid(&u, 23, 5, 3, 7);
    // Ctrl+= up a size, Ctrl+- down, Ctrl+0 back to 48; each says the size.
    UiAction a = zoom_by(&u, keys(1, UK_ZOOM_IN));
    CHECK(a.kind == UA_ZOOM && a.index == 64 && u.zoom == 64);
    a = zoom_by(&u, keys(1, UK_ZOOM_IN));
    CHECK(a.kind == UA_ZOOM && u.zoom == 96);
    a = zoom_by(&u, keys(1, UK_ZOOM_OUT));
    CHECK(a.kind == UA_ZOOM && u.zoom == 64);
    a = zoom_by(&u, keys(1, UK_ZOOM_RESET));
    CHECK(a.kind == UA_ZOOM && a.index == 48 && u.zoom == 48);
    // At 48 already, Ctrl+0 does nothing; at the ends, nothing either.
    a = zoom_by(&u, keys(1, UK_ZOOM_RESET));
    CHECK(a.kind == UA_NONE && u.zoom == 48);
    a = zoom_by(&u, keys(1, UK_ZOOM_OUT));
    CHECK(a.kind == UA_ZOOM && u.zoom == 32);
    a = zoom_by(&u, keys(1, UK_ZOOM_OUT));
    CHECK(a.kind == UA_NONE && u.zoom == 32);
    u.zoom = 256;
    a = zoom_by(&u, keys(1, UK_ZOOM_IN));
    CHECK(a.kind == UA_NONE && u.zoom == 256);
    // Not 0 when unset: Ctrl+= from no size at all is 64.
    u.zoom = 0;
    a = zoom_by(&u, keys(1, UK_ZOOM_IN));
    CHECK(a.kind == UA_ZOOM && u.zoom == 64);
    // The cursor stays; so does the sidebar's keyboard.
    CHECK(u.list.sel == 7);
    a = zoom_by(&u, keys(2, UK_TAB, UK_ZOOM_IN));
    CHECK(a.kind == UA_ZOOM && u.zoom == 96 && (u.focus == UI_FOCUS_SIDE));
    ui_unfocus(&u, UI_FOCUS_SIDE);
    // The list doesn't zoom.
    u.grid = false, ui_set_cols(&u.list, 1);
    a = zoom_by(&u, keys(1, UK_ZOOM_IN));
    CHECK(a.kind == UA_NONE && u.zoom == 96);
    u.grid = true;
    // A question up takes the keys.
    u.confirming = true;
    a = zoom_by(&u, keys(1, UK_ZOOM_IN));
    CHECK(a.kind == UA_NONE && u.zoom == 96);
    u.confirming = false;
}

static void test_zoom_mouse(void)
{
    static Ui u;
    grid(&u, 23, 5, 3, 7);
    // The minus and plus.
    UiInput in = ui_input_none();
    in.bar = UB_ZOOM_IN;
    UiAction a = zoom_by(&u, in);
    CHECK(a.kind == UA_ZOOM && u.zoom == 64);
    in.bar = UB_ZOOM_OUT;
    a = zoom_by(&u, in);
    CHECK(a.kind == UA_ZOOM && u.zoom == 48);
    // Ctrl+wheel: a size a notch however many came, up bigger; the plain wheel still scrolls.
    in = ui_input_none();
    in.zoom_wheel = 3;
    a = zoom_by(&u, in);
    CHECK(a.kind == UA_ZOOM && u.zoom == 64);
    in.zoom_wheel = -1;
    a = zoom_by(&u, in);
    CHECK(a.kind == UA_ZOOM && u.zoom == 48);
    in.zoom_wheel = -2;
    a = zoom_by(&u, in);
    CHECK(a.kind == UA_ZOOM && u.zoom == 32);
    // The track: a level, clamped.
    in = ui_input_none();
    in.zoom_to = 5;
    a = zoom_by(&u, in);
    CHECK(a.kind == UA_ZOOM && u.zoom == 256);
    in.zoom_to = 9;
    a = zoom_by(&u, in);
    CHECK(a.kind == UA_NONE && u.zoom == 256);
    in.zoom_to = 0;
    a = zoom_by(&u, in);
    CHECK(a.kind == UA_ZOOM && u.zoom == 32);
    // A track press, a Ctrl+wheel and the buttons are input that acts (a frame is drawn); a
    // question up keeps them all from zooming.
    in = ui_input_none();
    CHECK(!ui_input_acts(&in));
    in.zoom_to = 2;
    CHECK(ui_input_acts(&in));
    in = ui_input_none();
    in.zoom_wheel = -1;
    CHECK(ui_input_acts(&in));
    u.empty_asking = true, u.view = UI_VIEW_TRASH;
    in = ui_input_none();
    in.zoom_to = 3;
    a = zoom_by(&u, in);
    CHECK(a.kind == UA_NONE && u.zoom == 32);
    in = ui_input_none();
    in.zoom_wheel = 1;
    a = zoom_by(&u, in);
    CHECK(a.kind == UA_NONE && u.zoom == 32);
    u.empty_asking = false, u.view = UI_VIEW_FOLDER;
}

static void test_zoom_refit(void)
{
    static Ui u;
    // 100 entries, 5 across, 4 rows; the scroll on row 4, the cursor on row 6: the third row on
    // screen.
    grid(&u, 100, 5, 4, 0);
    u.list.top = 20, u.list.sel = 32;
    // Bigger: 3 across, 2 rows; the cursor's row 10 now. On screen it can only be the second row.
    ui_refit(&u.list, 3, 2);
    CHECK(u.list.sel == 32 && u.list.top == 27 && ui_cols(&u.list) == 3 && u.list.rows == 2);
    // Smaller again: 5 across, 4 rows; it stays the second row (top row 5, the cursor's 6).
    ui_refit(&u.list, 5, 4);
    CHECK(u.list.sel == 32 && u.list.top == 25);
    // Smaller still: 8 across, 6 rows; 100 entries make 13 rows, so the scroll goes to row 3 at
    // the most, the cursor's row 4 the second on screen.
    ui_refit(&u.list, 8, 6);
    CHECK(u.list.top == 24 && u.list.sel / 8 - u.list.top / 8 == 1);
    // All of it on screen: no scroll.
    ui_refit(&u.list, 10, 12);
    CHECK(u.list.top == 0 && u.list.sel == 32);
    // The cursor on the top row stays there; an empty listing has nothing to keep.
    grid(&u, 100, 5, 4, 0);
    u.list.top = 40, u.list.sel = 43;
    ui_refit(&u.list, 2, 3);
    CHECK(u.list.top == 42 && u.list.sel == 43);
    grid(&u, 0, 5, 4, 0);
    ui_refit(&u.list, 2, 3);
    CHECK(u.list.top == 0 && u.list.sel == 0);
    // No rows yet (a window not laid out): the cursor on the first.
    grid(&u, 100, 5, 4, 0);
    u.list.top = 20, u.list.sel = 37;
    ui_refit(&u.list, 4, 0);
    CHECK(u.list.top == 36 && u.list.sel == 37);
}

static void test_state_zoom(void)
{
    arena_reset(&ta);
    ops_guard(root);
    char path[PATH_MAX];
    env_set("XDG_STATE_HOME", at("state63"));
    CHECK(state_path(path, sizeof path));
    // No file: no size said.
    CHECK(state_load(&ta, path).zoom == 0);
    Err err;
    CHECK(state_put(&ta, path, "sidebar", "places", &err) && state_put(&ta, path, "view", "icons", &err) && state_put(&ta, path, "zoom", "96", &err));
    State r = state_load(&ta, path);
    CHECK(r.icons && r.zoom == 96);
    CHECK(strcmp(read_file(&ta, path).p, "sidebar=places\nview=icons\nzoom=96\n") == 0);
    // Digits only, four at most, more than 0; the last one said wins.
    static const struct { const char *text; int zoom; } Z[] = {
        { "zoom=256\n", 256 }, { "zoom = 32", 32 }, { "zoom=64\nzoom=128\n", 128 }, { "zoom=\n", 0 }, { "zoom=0\n", 0 },
        { "zoom=-48\n", 0 }, { "zoom=4x\n", 0 }, { "zoom=12345\n", 0 }, { "zoom=9999\n", 9999 }, { "zoom=96\nzoom=x\n", 96 },
        { "zom=96\n", 0 }, { "zoom 96\n", 0 }, { "zoom=96\nzoom=0\n", 96 }, { "zoom=96\nzoom=\n", 96 }, { "zoom=x4\n", 0 },
    };
    for (int k = 0; k < COUNT(Z); k++) {
        put(path, Z[k].text);
        checks++;
        int got = state_load(&ta, path).zoom;
        if (got != Z[k].zoom) failures++, fprintf(stderr, "state zoom for '%s': %d\n", Z[k].text, got);
    }
    env_restore();
}
