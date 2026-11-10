// The left button's drags of files (input.c's): inside the window (F13: the press that may become
// one, where the files would land, the spring-loaded tabs), out to other programs and in from them
// (F14, with gui/dnd.c), and the keys during one. Moved out of input.c (code-health review,
// 2026-11-06). Included by input.c, where it stood.

// ---- drag and drop inside ofx (F13 step 3) ----------------------------------------------------

// A press that may become a drag, on the entry or tree folder at path: what it would drag is taken
// now, by its path, not its place in a list a reload may change before the drag starts (code-health
// review, 2026-11-04); marked: the press was on a marked entry, so the marks go. icon: its key, a
// MIME type or (named) an icon's name.
static void drag_arm(float x, float y, Str path, bool marked, bool in_tree, Str icon, bool named)
{
    app.drag = DRAG_PRESS, app.drag_x0 = app.drag_px = x, app.drag_y0 = app.drag_py = y;
    app.drag_marked = marked, app.drag_tree = in_tree;
    drag_icon_set(icon, named);
    snprintf(app.drag_path, sizeof app.drag_path, "%.*s", STR_ARG(path));
    snprintf(app.drag_name, sizeof app.drag_name, "%.*s", STR_ARG(path_base(path)));
}

// The press on list entry i, armed.
static void drag_arm_entry(float x, float y, isize i)
{
    char p[PATH_MAX];
    const Entry *e = &app.list.e[i];
    bool dir = kind_is_dir(e->kind);
    if (entry_path(i, p, sizeof p)) drag_arm(x, y, str_from_c(p), ui_marked(&app.ui, i), false, dir ? S("folder") : entry_mime(i), dir);
}

// The folders dragged, resolved (dragged_real): a target reached through a link into one of them is
// refused as copy_here's into_itself would refuse it, so the outline and the job agree (step 3
// review).
static void drag_real(void)
{
    for (isize i = 0; i < dragged.n; i++) {
        struct stat st;
        char r[PATH_MAX];
        bool reaches;   // (one on a far drive isn't looked at: resolved by far_path, kept whatever it is, F16 step 3)
        bool ok = far_path(dragged.path[i], r, sizeof r, &reaches) && (reaches || (lstat(dragged.path[i], &st) == 0 && S_ISDIR(st.st_mode)));
        dragged_real[i] = ok ? arena_cstr(&draga, str_from_c(r)) : NULL;
    }
}

// The press moved far enough: what it drags, into dragged. On a marked entry every marked one, else
// the entry pressed alone (the marks stay); a tree row's folder. False when there is nothing (or
// more than a drop takes: said).
static bool drag_start(void)
{
    arena_reset(&draga);
    dragged.n = 0;
    if (app.drag_marked && too_many_marked()) return false;
    if (app.drag_marked) dragged.n = targets(&draga, dragged.path, CLIP_PATHS_MAX);
    else dragged.path[0] = arena_cstr(&draga, str_from_c(app.drag_path)), dragged.n = 1;
    drag_real();
    return dragged.n > 0;
}

// Whether folder dir takes the files dragged: there, writable, neither one of them nor inside one
// (as a paste refuses); *same_drive: on the first one's drive (Nemo's rule, ui_drop_op). A far one
// (F16 step 3) isn't looked at: far_writable, as the menus, and never "the same drive" (a copy, the
// safer guess).
static bool drop_takes(const char *dir, bool *same_drive)
{
    struct stat ds, fs;
    char real[PATH_MAX];
    bool reaches;
    if (!far_path(dir, real, sizeof real, &reaches)) return false;
    if (reaches) {
        if (!far_writable(dir)) return false;
        for (isize i = 0; i < dragged.n; i++)
            if (dragged_real[i] && ui_under(str_from_c(real), str_from_c(dragged_real[i]))) return false;
        return *same_drive = false, true;
    }
    if (stat(dir, &ds) != 0 || !S_ISDIR(ds.st_mode) || access(dir, W_OK) != 0) return false;
    for (isize i = 0; i < dragged.n; i++)
        if (dragged_real[i] && ui_under(str_from_c(real), str_from_c(dragged_real[i]))) return false;
    *same_drive = !far_dir(dragged.path[0]) && lstat(dragged.path[0], &fs) == 0 && fs.st_dev == ds.st_dev;
    return true;
}

// What the files dragged are over at x, y, into app.drop (the folder asked again only when it is
// another): a folder's row or cell, a sidebar row (the Trash's trashes; a heading, a drive not
// mounted yet, none), a tab (its folder), a path button, the files' empty space (the folder
// shown). Not a file's row, nor the trash view's or a search's rows and space (many folders).
static void drop_find(float x, float y)
{
    Drop d = { .kind = DROP_NONE, .at = -1 };
    Hit h = hit_at(x, y);
    const char *dir = NULL;
    char buf[PATH_MAX];
    bool shown = app.ui.view == UI_VIEW_FOLDER;
    if (h.row >= 0) {
        isize e = app.ui.list.top + h.row;
        d.kind = DROP_ROW, d.at = h.row;
        if (shown && app.have_list && e < app.list.n && kind_is_dir(app.list.e[e].kind) && entry_path(e, buf, sizeof buf)) dir = buf;
    } else if (h.side >= 0) {
        d.kind = DROP_SIDE, d.at = h.side;
        dir = side_path(app.ui.side.top + h.side, buf, sizeof buf);
        d.trash = dir && strcmp(dir, TRASH_URI) == 0;
    } else if (h.tab >= 0) {
        d.kind = DROP_TAB, d.at = h.tab;
        dir = h.tab == tabbar.cur ? (shown ? app.cwd : NULL) : tabs[tabbar.slot[h.tab]].dir;
        if (dir && (strcmp(dir, TRASH_URI) == 0 || is_search(dir))) dir = NULL;
    } else if (h.bar == UB_CRUMB && h.crumb >= 0) {
        d.kind = DROP_CRUMB, d.at = h.crumb;
        snprintf(buf, sizeof buf, "%.*s", STR_ARG(ui_crumb_path(&app.crumbs, h.crumb)));
        dir = buf;
    } else if (in_box(CLAY_ID("list"), x, y)) {
        d.kind = DROP_LIST;
        if (shown) dir = app.cwd;
    }
    if (dir) snprintf(d.dir, sizeof d.dir, "%s", dir);
    // Over another tab the wait for it to open starts (spring_tick); off it, it starts over.
    int spring = d.kind == DROP_TAB && d.at != tabbar.cur ? d.at : -1;
    if (spring != app.spring_tab) app.spring_tab = spring, app.spring_at = spring >= 0 ? now_ms() : 0, timer_at(wake_due());
    d.reloads = app.reloads;
    if (d.kind == app.drop.kind && d.at == app.drop.at && d.reloads == app.drop.reloads && strcmp(d.dir, app.drop.dir) == 0) return;   // (the same: not asked again)
    d.ok = dir && (d.trash || drop_takes(d.dir, &d.same_drive));
    app.drop = d;
}

// The drag's end: nothing held, nothing lit, no tab waited for.
// Where the files would land forgotten (at: DROP_AGAIN, found at the next point; -1, none), and the
// tab held under them with it: a drag starts, ends, or leaves the window (a drag out that came back
// onto the tab it left from found its old timer, and the tab opened at once: code-health review,
// 2026-11-06).
static void drop_reset(int at)
{
    app.drop = (Drop){ .kind = DROP_NONE, .at = at }, app.spring_tab = -1;
}

static void drag_end(void)
{
    app.drag = DRAG_NONE, drop_reset(-1);
}

// A drag of files ended without a drop: Esc, or the pointer off the window.
static void drag_cancel(void)
{
    if (app.drag == DRAG_OUT) dnd_cancel();
    if (drag_of_files() && app.drag != DRAG_IN) drag_end();   // (another program's is its own to cancel)
}

// ---- dragging out to other programs (F14 step 2) ---------------------------------------------

enum { DRAG_IMG_PX = 48, DRAG_IMG_ICON = 32, DRAG_IMG_MAX = DRAG_IMG_PX * 4 };
static u32 drag_img[DRAG_IMG_MAX * DRAG_IMG_MAX];   // the drag image's pixels, for the compositor's buffer

// The drag image, as the badge's first part (Michael, 2026-11-05): the first file's icon, and when
// more go a count in the accent colour at its corner; drawn at the window's scale into a texture and
// read back premultiplied (the compositor's ARGB8888). False when it can't be (no texture).
static bool drag_image(DndImage *im)
{
    int sc = window_scale(), px = DRAG_IMG_PX * sc;
    if (sc > 4) return false;
    RenderTexture2D rt = LoadRenderTexture(px, px);
    if (!rt.id) return false;
    Theme *t = &app.theme;
    BeginTextureMode(rt);
    ClearBackground(BLANK);
    // Every channel blended over the clear texture: the colours come out premultiplied, alpha whole
    // (BLEND_ALPHA would square an edge's alpha).
    rlSetBlendFactorsSeparate(0x0302, 0x0303, 1, 0x0303, 0x8006, 0x8006);   // (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, ..., GL_FUNC_ADD)
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    BeginMode2D((Camera2D){ .zoom = (float)sc });
    float ix = 4, iy = (float)(DRAG_IMG_PX - DRAG_IMG_ICON) - 4;
    IconSlot *icon = drag_icon_slot();
    if (icon && icon->tex.id) {
        Texture2D tx = icon->tex;
        DrawTexturePro(tx, (Rectangle){ 0, 0, (float)tx.width, (float)tx.height }, (Rectangle){ ix, iy, DRAG_IMG_ICON, DRAG_IMG_ICON }, (Vector2){ 0 }, 0, WHITE);
    }
    if (dragged.n > 1) {
        char num[24];
        snprintf(num, sizeof num, "%td", dragged.n);
        float w = 0, size = 12;
        for (const char *c = num; *c; c++) w += font_advance((u32)*c, size);
        float r = w / 2 + 5 < 9 ? 9 : w / 2 + 5, cx = DRAG_IMG_PX - r - 1, cy = r + 1;
        Color acc = { t->accent.r, t->accent.g, t->accent.b, 255 }, bg = { t->bg.r, t->bg.g, t->bg.b, 255 };
        DrawRectangleRounded((Rectangle){ cx - r, cy - 9, 2 * r, 18 }, 1, 12, acc);
        float x = cx - w / 2;
        for (const char *c = num; *c; c++) font_draw((u32)*c, x, cy - 8, size, bg), x += font_advance((u32)*c, size);
    }
    EndMode2D();
    EndBlendMode();
    EndTextureMode();
    Image img = LoadImageFromTexture(rt.texture);
    UnloadRenderTexture(rt);
    if (!img.data) return false;
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const u8 *p = img.data;
    for (int y = 0; y < px; y++)   // (a render texture is upside down)
        for (int x = 0; x < px; x++) {
            const u8 *q = p + ((size_t)(px - 1 - y) * (size_t)px + (size_t)x) * 4;
            drag_img[y * px + x] = (u32)q[3] << 24 | (u32)q[0] << 16 | (u32)q[1] << 8 | q[2];
        }
    UnloadImage(img);
    *im = (DndImage){ drag_img, px, px, sc, (int)(ix + DRAG_IMG_ICON / 2), (int)(iy + DRAG_IMG_ICON / 2) };
    return true;
}

// The files dragged past the window's edge, the button still held (the compositor keeps the
// pointer ours until it is let go): handed to the compositor as a drag to other programs, as a
// copy, and with Shift held a move too (Michael, 2026-11-05). raylib is told the button is up, as
// it will never see its release (the next press would be no press). False when it can't be (no
// compositor drags, no press it saw, too many files): the drag stays ofx's, and isn't handed off
// again until the next one (each try draws the image and builds the payloads).
static bool drag_out(void)
{
    if (app.out_failed) return false;
    DndImage im;
    bool have = dnd_can_start() && drag_image(&im);
    if (!dnd_start(&dragged, app.drag_mods & UI_MOD_SHIFT, have ? &im : NULL)) return app.out_failed = true, false;
    app.drag = DRAG_OUT, app.out_over = false;
    drop_reset(-1);
    if (raylib_button) raylib_button(glfwGetCurrentContext(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
    return true;
}

// Each frame of a drag out: back over the window it is ours again, lit and answered as a drag
// inside it is, and dropped there as one; ended (dropped elsewhere, or cancelled), the status says
// where it went. True when what is drawn changed.
static bool drag_out_poll(void)
{
    if (app.drag != DRAG_OUT) return false;
    float x, y;
    bool over = dnd_own_over(&x, &y), was = app.out_over;
    app.out_over = over;
    if (over) {
        app.drag_px = x, app.drag_py = y;
        drop_find(x, y);
        UiDropOp op = drop_op(&app.drop);
        dnd_own_answer(app.drop.ok, op != UI_DROP_COPY);
    } else if (was) {
        drop_reset(-1), dnd_own_answer(false, false);
    }
    // Dropped back on the window: as a drag inside it, by Nemo's rule (drop_op), not by the
    // compositor's action. Without Shift the source offers only copy, so the compositor settles on
    // copy while the badge said Move on the same drive; the badge and the drop agree, as inside the
    // window (Michael, 2026-11-06). Other programs' drops go by both (ui_dnd_merge).
    if (dnd_own_dropped(&x, &y)) {
        drop_find(x, y);
        if (app.drop.ok) drop_do(&app.drop);
        app.out_own = true;
    }
    if (dnd.out == DND_OUT_DRAGGING || dnd.out == DND_OUT_DROPPED) return over != was;   // (each motion asks for its frame itself: dnd_poll)
    if (dnd.out == DND_OUT_DONE && !app.out_own) {
        char name[NAME_MAX + 1];
        snprintf(name, sizeof name, "%.*s", STR_ARG(path_base(str_from_c(dragged.path[0]))));
        bool moved = dnd.out_action == WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
        if (dragged.n == 1) set_status(false, "%s %s to another program", moved ? "Moved" : "Copied", name);
        else set_status(false, "%s %td items to another program", moved ? "Moved" : "Copied", dragged.n);
    }
    app.out_own = false;
    drag_end();
    return true;
}

// A drag held over another tab UI_SPRING_MS (F13 step 4): that tab shown, so the drop can go into
// one of its folders, and what the files are over found again. True when it opened.
static bool spring_tick(void)
{
    if (!files_over() || app.spring_tab < 0 || now_ms() < app.spring_at + UI_SPRING_MS) return false;
    tab_show(app.spring_tab);
    app.spring_tab = -1, app.drop.at = DROP_AGAIN;   // (the same place, under another tab)
    drop_find(app.drag_px, app.drag_py);
    return true;
}

// ---- dropping in from other programs (F14 step 3) ---------------------------------------------

_Static_assert((int)UI_DND_COPY == (int)WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY && (int)UI_DND_MOVE == (int)WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE, "ui/'s actions are Wayland's");

// What a drop of another program's files on d does, as the compositor's action: Nemo's rule (a move
// on the same drive, a copy across; Ctrl, Shift) within what the source allows (ui_dnd_action). 0:
// refused.
static uint32_t drop_in_action(const Drop *d, uint32_t actions)
{
    return d->ok ? ui_dnd_action(drop_op(d), actions) : 0;
}

// Another program's files come over the window (offer id's): dragged as a drag inside it is (F13),
// from their own list: the badge, the targets lit, the tabs that open.
static void drag_in_start(const ClipFiles *f, u32 id)
{
    arena_reset(&draga);
    for (isize i = 0; i < f->n; i++) dragged.path[i] = arena_cstr(&draga, str_from_c(f->path[i]));
    dragged.n = f->n, dragged.cut = false;
    drag_real();
    Str name = path_base(str_from_c(f->path[0]));
    struct stat st;
    bool dir = stat(f->path[0], &st) == 0 && S_ISDIR(st.st_mode);
    app.drag = DRAG_IN, app.in_id = id;
    drop_reset(DROP_AGAIN);
    app.drag_marked = app.drag_tree = false;
    drag_icon_set(dir ? S("folder") : mime_of_name(&scratch, &app.mime_db, name), dir);
    snprintf(app.drag_path, sizeof app.drag_path, "%s", f->path[0]);
    snprintf(app.drag_name, sizeof app.drag_name, "%.*s", STR_ARG(name));
}

// The window's drag made offer id's, from its files f, when no drag goes or the one going is an
// earlier offer's (a leave and the next enter between two frames: its files aren't these); true when
// it started now.
static bool drag_in_take(const ClipFiles *f, u32 id)
{
    if (app.drag == DRAG_IN && app.in_id != id) drag_end();
    if (app.drag != DRAG_NONE) return false;
    drag_in_start(f, id);
    return true;
}

// Each frame of another program's drag: over the window, found and answered as F13's drag is; let
// go, dropped as one (copy, move, or the trash's move: ui_dnd_merge), and the source told; gone (a
// leave, or its files not read yet), the drag ends. True when what is drawn changed.
static bool drag_in_poll(void)
{
    float x, y;
    uint32_t actions = 0, act;
    u32 id;
    bool no = dnd_in_nofiles(&x, &y);
    if (no) app.drag_px = x, app.drag_py = y;
    bool said = no != app.in_nofiles;   // (the badge saying so comes and goes)
    app.in_nofiles = no;
    const ClipFiles *f = dnd_in_over(&x, &y, &actions, &id);
    bool started = f && drag_in_take(f, id);
    if (f && app.drag == DRAG_IN) {   // (each motion asks for its frame itself: dnd_poll)
        app.drag_px = x, app.drag_py = y;
        drop_find(x, y);
        uint32_t a = drop_in_action(&app.drop, actions);
        dnd_in_answer(a != 0, a);
        return started || said;
    }
    const ClipFiles *d = dnd_in_dropped(&x, &y, &actions, &act, &id);
    if (d) {
        bool done = false;
        if (app.drag == DRAG_IN && app.in_id == id) {   // (a drag of an earlier offer's files: not these)
            drop_find(x, y);
            UiDropOp op;
            if (app.drop.ok && ui_dnd_drop(app.drop.trash, ui_dnd_merge(drop_in_action(&app.drop, actions), act), &op)) drop_with(&app.drop, op), done = true;
        }
        dnd_in_finish(done);
        if (app.drag == DRAG_IN) drag_end();
        return true;
    }
    if (app.drag == DRAG_IN) return drag_end(), true;
    return said;
}

// The keys during a drag, or a press that may become one: Esc cancels it, and is the drag's alone
// (it would clear the marks); the rest go nowhere, as they could change what it carries or is over
// (Delete trashed the files dragged, Ctrl+W closed the tab under it: code-health review,
// 2026-11-04).
static void drag_keys(UiInput *in)
{
    if (!drag_of_files()) return;
    bool esc = false;
    for (int i = 0; i < in->nev; i++) esc |= in->ev[i].key == UK_ESC;
    in->nev = 0;
    if (esc) drag_cancel();
}
