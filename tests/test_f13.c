// F13 (docs/plan.md "F13 design"): Copy Path. Included by test.c after test_f12.c.

// Ctrl+Shift+C: the files' paths as text, in a folder (one that can't be written too: nothing is
// written), an empty one (its own path, Michael 2026-11-04) and a search; not in the trash view,
// nor from the sidebar's keys. Edit's entry agrees, and the entry and folder menus have one.
static void test_copy_path(void)
{
    static const struct { isize n; UiView v; bool w; UiActionKind want; } C[] = {
        { 3, UI_VIEW_FOLDER, true, UA_COPY_PATH }, { 3, UI_VIEW_FOLDER, false, UA_COPY_PATH }, { 0, UI_VIEW_FOLDER, true, UA_COPY_PATH },
        { 3, UI_VIEW_SEARCH, false, UA_COPY_PATH }, { 3, UI_VIEW_TRASH, false, UA_NOT_IN_TRASH }, { 0, UI_VIEW_TRASH, false, UA_NOT_IN_TRASH },
    };
    for (int c = 0; c < COUNT(C); c++) {
        Ui u = menu_ui(C[c].n);
        u.view = C[c].v, u.can_write = C[c].w;
        CHECK(menu_keys(&u, 1, UK_COPY_PATH).kind == C[c].want);
        CHECK(ui_menu_enabled(&u, UM_COPY_PATH) == (C[c].want == UA_COPY_PATH));
    }
    Ui u = menu_ui(3);
    u.focus = UI_FOCUS_SIDE;
    CHECK(menu_keys(&u, 1, UK_COPY_PATH).kind == UA_NONE && !ui_menu_enabled(&u, UM_COPY_PATH));
    // The menus: Edit and an entry's after Copy, a folder's after its Copy; picked, Edit's runs the
    // key, the folder's goes to gui/ for its folder.
    CHECK(menu_place(1, UM_COPY_PATH) == menu_place(1, UM_COPY) + 1);
    CHECK(menu_place(UI_CTX_ENTRY, UM_COPY_PATH) == menu_place(UI_CTX_ENTRY, UM_COPY) + 1);
    CHECK(menu_place(UI_CTX_FOLDER, UM_COPY_PATH_IT) == menu_place(UI_CTX_FOLDER, UM_COPY_IT) + 1);
    CHECK(menu_place(UI_CTX_HERE, UM_COPY_PATH) < 0 && menu_place(UI_CTX_FOLDER, UM_COPY_PATH) < 0);
    u = menu_ui(0);
    u.menu_on = true, u.menu = 1;
    UiInput in = ui_input_none();
    in.menu_item = menu_place(1, UM_COPY_PATH);
    CHECK(ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in).kind == UA_COPY_PATH && !u.menu_on);
    // A folder's menu: a place's too (a bookmark, a drive: they have no Cut or Copy), not the
    // trash's, nor a drive not mounted yet.
    u = menu_ui(3);
    u.menu_on = true, u.menu = UI_CTX_FOLDER, u.ctx_bmk = -1;
    CHECK(ui_menu_shown(&u, UM_COPY_PATH_IT) && ui_menu_enabled(&u, UM_COPY_PATH_IT) && !ui_menu_shown(&u, UM_COPY_PATH));
    u.ctx_place = true;
    CHECK(ui_menu_shown(&u, UM_COPY_PATH_IT) && !ui_menu_shown(&u, UM_COPY_IT));
    in = ui_input_none(), in.menu_item = menu_place(UI_CTX_FOLDER, UM_COPY_PATH_IT);
    UiAction a = ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(a.kind == UA_FOLDER_DO && a.index == UM_COPY_PATH_IT);
    u.menu_on = true, u.ctx_trash = true;
    CHECK(!ui_menu_shown(&u, UM_COPY_PATH_IT));
    u.ctx_trash = false, u.ctx_unmounted = true;
    CHECK(!ui_menu_shown(&u, UM_COPY_PATH_IT));
}
