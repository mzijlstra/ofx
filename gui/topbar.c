// The top bar (F4): back, forward, up, the path buttons, the location line, search and the view
// buttons; the zoom slider (F6.3); the menu bar (F7.1). Included by main.c.

// ---- the top bar (F4) -----------------------------------------------------------------------

static IconSlot *named_icon(const char *name)
{
    Str n = str_from_c(name);
    return icon_for(n, &n, 1);
}

// The top bar's chevrons, drawn as two lines each (Nemo's look; the theme has its arrows only as
// SVG, which ofx doesn't draw). Clay hands the data back to draw_custom at render time; it lives in
// the frame arena, which outlasts the frame's render.
typedef enum { CHEV_LEFT, CHEV_RIGHT, CHEV_UP, CHEV_DOWN } ChevronDir;
typedef enum { CUSTOM_CHEVRON = 1, CUSTOM_LINE, CUSTOM_TOOL } CustomKind;   // the first field of each custom's data
typedef enum { TOOL_SEARCH, TOOL_PENCIL } ToolDir;   // the top bar's search and location pictures

typedef struct {
    CustomKind kind;
    ToolDir    which;
    Clay_Color color;
} ToolDraw;

typedef struct {
    CustomKind kind;
    ChevronDir dir;
    Clay_Color color;
} Chevron;

// A job row's throughput line (F5.3): the speed at each point it reached, scaled to the fastest,
// 1 device pixel wide, the part done tinted under it; a job without bytes to count (a delete, a
// move within a drive) a plain bar by items.
typedef struct {
    CustomKind     kind;
    const JobLine *line;   // NULL: the plain bar
    float          done;   // 0..1
    Clay_Color     stroke, fill;
} LineDraw;

static Color rl(Clay_Color c)
{
    return (Color){ (u8)c.r, (u8)c.g, (u8)c.b, (u8)c.a };
}

static void draw_line(Clay_BoundingBox b, const LineDraw *d)
{
    float scale = d->line ? jobline_scale(d->line, now_ms()) : 0;
    if (scale <= 0) {
        DrawRectangleRec((Rectangle){ b.x, b.y, b.width * d->done, b.height }, rl(d->fill));
        return;
    }
    const JobLine *l = d->line;
    float step = b.width / JOBQ_POINTS, room = b.height - 8, px = 1.0f / GetWindowScaleDPI().x;   // the fastest 8 px under the top
    Vector2 prev = { 0 };
    for (int k = 0; k < l->reached; k++) {
        float h = l->speed[k] / scale;
        float y = b.y + b.height - room * (h < 1 ? h : 1) - (h > 1 ? 4 : 0);   // over the scale: clipped at the top
        Vector2 p = { b.x + step * ((float)k + 0.5f), y };
        DrawRectangleRec((Rectangle){ b.x + step * (float)k, y, step + 0.5f, b.y + b.height - y }, rl(d->fill));
        if (k > 0) DrawLineEx(prev, p, px, rl(d->stroke));
        prev = p;
    }
}

// The search button's magnifier and the location button's pencil, in lines as the chevrons are, so
// they take the theme's colours as the drawn buttons beside them do (Michael, 2026-10-24).
static void draw_tool(Clay_BoundingBox b, const ToolDraw *d)
{
    float cx = b.x + b.width / 2, cy = b.y + b.height / 2;
    Color col = rl(d->color);
    if (d->which == TOOL_SEARCH) {
        Vector2 c = { cx - 1.5f, cy - 1.5f };
        float r = 4.5f;
        DrawRing(c, r - 0.8f, r + 0.8f, 0, 360, 36, col);
        DrawLineEx((Vector2){ c.x + r * 0.72f, c.y + r * 0.72f }, (Vector2){ cx + 6, cy + 6 }, 2.2f, col);
        return;
    }
    // A pencil from the lower left to the upper right, along u: its tip, then its body, 3.4 wide.
    Vector2 u = { 0.7071f, -0.7071f }, n = { 0.7071f, 0.7071f }, t = { cx - 6, cy + 6 };
    Vector2 a = { t.x + u.x * 4, t.y + u.y * 4 }, z = { t.x + u.x * 16, t.y + u.y * 16 };
    DrawLineEx(a, z, 3.4f, col);
    Vector2 l = { a.x + n.x * 1.7f, a.y + n.y * 1.7f }, r = { a.x - n.x * 1.7f, a.y - n.y * 1.7f };
    DrawTriangle(t, l, r, col);   // raylib draws one winding only: either way round, one of these does
    DrawTriangle(t, r, l, col);
}

static void draw_custom(Clay_BoundingBox b, void *arg)
{
    if (!arg) return;
    if (*(const CustomKind *)arg == CUSTOM_LINE) { draw_line(b, arg); return; }
    if (*(const CustomKind *)arg == CUSTOM_TOOL) { draw_tool(b, arg); return; }
    const Chevron *c = arg;
    float cx = b.x + b.width / 2, cy = b.y + b.height / 2, r = 5;
    Color col = { (u8)c->color.r, (u8)c->color.g, (u8)c->color.b, (u8)c->color.a };
    Vector2 p0, p1, p2;
    switch (c->dir) {
    case CHEV_LEFT: p0 = (Vector2){ cx + r / 2, cy - r }, p1 = (Vector2){ cx - r / 2, cy }, p2 = (Vector2){ cx + r / 2, cy + r }; break;
    case CHEV_RIGHT: p0 = (Vector2){ cx - r / 2, cy - r }, p1 = (Vector2){ cx + r / 2, cy }, p2 = (Vector2){ cx - r / 2, cy + r }; break;
    case CHEV_DOWN: p0 = (Vector2){ cx - r, cy - r / 2 }, p1 = (Vector2){ cx, cy + r / 2 }, p2 = (Vector2){ cx + r, cy - r / 2 }; break;
    default: p0 = (Vector2){ cx - r, cy + r / 2 }, p1 = (Vector2){ cx, cy - r / 2 }, p2 = (Vector2){ cx + r, cy + r / 2 }; break;
    }
    DrawLineEx(p0, p1, 1.6f, col);
    DrawLineEx(p1, p2, 1.6f, col);
    DrawCircleV(p1, 0.8f, col);   // a clean tip where the lines meet
}

static void chevron(Clay_ElementId id, ChevronDir dir, Clay_Color color)
{
    Chevron *c = arena_new(&frame, Chevron, 1);
    *c = (Chevron){ CUSTOM_CHEVRON, dir, color };
    CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(16) } }, .custom = { .customData = c } }) {}
}

static Clay_Color dim_color(void)
{
    return mix(app.theme.fg_dim, app.theme.bg_light, 0.5f);
}

// A top bar button: a chevron, an icon from the theme, or a glyph from the UI font. Dimmed when it
// can't act.
static void bar_button(UiBar which, int chev, const char *glyph, IconSlot *icon, bool enabled, float w)
{
    Theme *t = &app.theme;
    bool hover = enabled && app.hov.bar == which;
    Clay_Color fg = enabled ? cc(t->fg) : dim_color();
    Clay_ElementId id = bar_id(which);
    CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(BAR_BTN_H) },
                           .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
               .backgroundColor = hover ? cc(t->bg) : cc(t->bg_light) }) {
        Clay_ElementId inner = Clay_GetElementId(cz(arena_sprintf(&frame, "%.*s-icon", (int)id.stringId.length, id.stringId.chars)));
        if (chev >= 0) chevron(inner, (ChevronDir)chev, fg);
        else if (icon && icon->tex.id) icon_box(inner, icon, CRUMB_ICON_PX);
        else text(cz(glyph), fg);
    }
}

static float crumb_width(int i)
{
    const UiCrumbs *c = &app.crumbs;
    return str_width(ui_crumb_label(c, i)) + 2 * CRUMB_PAD + (c->seg[i].home || c->seg[i].share ? CRUMB_ICON_PX + 6 : 0);
}

// The path buttons: the current folder marked, the deeper ones dimmer; ‹ and › when they don't fit.
static void layout_crumbs(float room)
{
    Theme *t = &app.theme;
    UiCrumbs *c = &app.crumbs;
    float wd[UI_CRUMBS_MAX];
    for (int i = 0; i < c->n; i++) wd[i] = crumb_width(i);
    int lo, hi;
    if (app.ui.focus == UI_FOCUS_CRUMBS) ui_crumbs_show(c, wd, room, ARROW_W, app.ui.crumb_at < c->n ? app.ui.crumb_at : c->n - 1);   // (crumb_at past them: the branch button)
    app.crumb_arrows = ui_crumbs_window(c, wd, room, ARROW_W, &lo, &hi);
    app.crumb_lo = lo, app.crumb_hi = hi;
    // One outlined group, a line between the buttons, so they read as one path (Michael, 2026-10-20).
    Clay_Color line = rule_color();
    CLAY(CLAY_ID("crumb-group"), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(BAR_BTN_H) } },
                                   .border = { .color = line, .width = { 1, 1, 1, 1, 1 } } }) {
    if (app.crumb_arrows) bar_button(UB_LEFT, CHEV_LEFT, NULL, NULL, lo > 0, ARROW_W);
    for (int i = lo; i < hi; i++) {
        bool cur = i == c->cur, hover = app.hov.bar == UB_CRUMB && app.hov.crumb == i;
        bool keyed = app.ui.focus == UI_FOCUS_CRUMBS && app.ui.crumb_at == i;   // Ctrl+K's mark: an accent outline
        CLAY(CLAY_IDI("crumb", i), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(BAR_BTN_H) }, .padding = { CRUMB_PAD, CRUMB_PAD, 0, 0 },
                                                 .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                     .backgroundColor = cur ? cc(t->selection) : hover ? cc(t->bg) : cc(t->bg_light),
                                     .border = { .color = cc(t->accent), .width = { keyed * 2, keyed * 2, keyed * 2, keyed * 2, 0 } } }) {
            if (c->seg[i].home || c->seg[i].share) icon_box(CLAY_IDI("crumb-icon", i), named_icon(c->seg[i].share ? "folder-remote" : "user-home"), CRUMB_ICON_PX);
            Str label = ui_crumb_label(c, i);
            float most = room - 2 * ARROW_W - 2 * CRUMB_PAD - (c->seg[i].home || c->seg[i].share ? CRUMB_ICON_PX + 6 : 0);
            text(fit(label, most > 20 ? most : 20, false), cur ? cc(t->accent) : i > c->cur ? cc(t->fg_dim) : cc(t->fg));
        }
    }
    if (app.crumb_arrows) bar_button(UB_RIGHT, CHEV_RIGHT, NULL, NULL, hi < c->n, ARROW_W);
    }
}

// The git branch button after the path buttons (F12 step 5b): its label (branch_label), outlined
// as the path is; marked while its menu (frame.c's, as a right-click one) is open.
static float bar_room(float w)   // (the path buttons' room in a bar w wide)
{
    return w - PAD - 7 * BAR_BTN_W - 9 * BAR_GAP - 2 * PAD;
}

// The branch button's width (0: none): its label's, at most a third of the room (a long name gives
// up its end first: the path keeps two thirds).
static float branch_btn_w(float w, const char *label)
{
    float bw = label ? str_width(str_from_c(label)) + 2 * CRUMB_PAD : 0, most = bar_room(w) / 3;
    return bw > most ? most : bw;
}

// Its label as drawn in a button bw wide: cut at its end to fit.
static Clay_String branch_text(const char *label, float bw)
{
    float most = bw - 2 * CRUMB_PAD;
    return fit(str_from_c(label), most > 20 ? most : 20, false);
}

static void branch_button(const char *label, float bw)
{
    Theme *t = &app.theme;
    bool open = app.ui.menu_on && app.ui.menu == UI_CTX_BRANCH, hover = app.hov.bar == UB_BRANCH;
    bool keyed = app.ui.focus == UI_FOCUS_CRUMBS && app.ui.crumb_at >= app.crumbs.n;   // Ctrl+K's mark, as a path button's (F12 step 5c)
    CLAY(Clay_GetElementId(cz(BAR_IDS[UB_BRANCH])), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(BAR_BTN_H) }, .padding = { CRUMB_PAD, CRUMB_PAD, 0, 0 },
                                                                  .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                                      .backgroundColor = open ? cc(t->selection) : hover ? cc(t->bg) : cc(t->bg_light),
                                                      .border = keyed ? (Clay_BorderElementConfig){ .color = cc(t->accent), .width = CLAY_BORDER_OUTSIDE(2) }
                                                                      : (Clay_BorderElementConfig){ .color = rule_color(), .width = CLAY_BORDER_OUTSIDE(1) } }) {
        text(branch_text(label, bw), cc(t->fg_dim));
    }
}

// The location line in the path buttons' place: the path, its selection and a caret (all of it
// selected as it opens).
static void layout_location(float room)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("location"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(BAR_BTN_H) }, .padding = { 6, 6, 0, 0 },
                                            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                .clip = { .horizontal = true },
                                .backgroundColor = cc(t->bg), .border = { .color = cc(t->accent), .width = CLAY_BORDER_ALL(1) } }) {
        edit_text(&app.theme, "location", room - 24, ui_edit_line(&app.ui), cc(app.theme.fg));
    }
}

// The location and search buttons, drawn (draw_tool), marked in the accent while theirs is open.
static void tool_button(UiBar which, ToolDir pic, bool on)
{
    const char *id = BAR_IDS[which];
    Theme *t = &app.theme;
    bool hover = app.hov.bar == which;
    ToolDraw *d = arena_new(&frame, ToolDraw, 1);
    *d = (ToolDraw){ CUSTOM_TOOL, pic, on ? cc(t->accent) : hover ? cc(t->fg) : cc(t->fg_dim) };
    CLAY(Clay_GetElementId(cz(id)), { .layout = { .sizing = { CLAY_SIZING_FIXED(BAR_BTN_W), CLAY_SIZING_FIXED(BAR_BTN_H) },
                                                  .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                                      .backgroundColor = on ? mix(t->accent, t->bg_light, 0.25f) : hover ? cc(t->bg) : cc(t->bg_light) }) {
        CLAY(Clay_GetElementId(cz(arena_sprintf(&frame, "%s-pic", id))), { .layout = { .sizing = { CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(16) } },
                                                                             .custom = { .customData = d } }) {}
    }
}

// A view button's picture (F6.2), drawn in c: three lines for the list, four squares for the icons
// (the theme has its view icons as SVG only, which ofx doesn't draw).
static void view_picture(bool icons, Clay_Color c)
{
    const char *id = icons ? "view-pic-icons" : "view-pic-list";
    CLAY(Clay_GetElementId(cz(id)), { .layout = { .sizing = { CLAY_SIZING_FIXED(14), CLAY_SIZING_FIXED(12) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                                  .childGap = icons ? 2 : 3, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        for (int j = 0; j < (icons ? 2 : 3); j++) {
            if (!icons) {
                CLAY(CLAY_IDI("view-line", j), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(2) } }, .backgroundColor = c }) {}
                continue;
            }
            CLAY(CLAY_IDI("view-sq-row", j), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(5) }, .childGap = 2 } }) {
                for (int q = 0; q < 2; q++)
                    CLAY(CLAY_IDI("view-sq", j * 2 + q), { .layout = { .sizing = { CLAY_SIZING_FIXED(6), CLAY_SIZING_FIXED(5) } }, .backgroundColor = c }) {}
            }
        }
    }
}

// The list and icon buttons, the view shown marked in the accent.
static void view_button(UiBar which, bool icons)
{
    const char *id = BAR_IDS[which];
    Theme *t = &app.theme;
    bool on = app.ui.grid == icons, hover = app.hov.bar == which;
    CLAY(Clay_GetElementId(cz(id)), { .layout = { .sizing = { CLAY_SIZING_FIXED(BAR_BTN_W), CLAY_SIZING_FIXED(BAR_BTN_H) },
                                                  .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                                      .backgroundColor = on ? mix(t->accent, t->bg_light, 0.25f) : hover ? cc(t->bg) : cc(t->bg_light) }) {
        view_picture(icons, on ? cc(t->accent) : hover ? cc(t->fg) : cc(t->fg_dim));
    }
}

// ---- the zoom slider (F6.3) ------------------------------------------------------------------

// Its width at the footer's right end; 0 in the list, which doesn't zoom.
static float zoom_w(void)
{
    return app.ui.grid ? 2 * ZOOM_BTN_W + ZOOM_TRACK_W + 2 * 4 + PAD : 0;
}

// The knob's middle for level k, from the track's left.
static float zoom_x(int k)
{
    return ZOOM_KNOB_W / 2.0f + (float)k * (float)(ZOOM_TRACK_W - ZOOM_KNOB_W) / (UI_ZOOMS - 1);
}

// The level nearest x on the track as last drawn; -1 when it isn't there.
static int zoom_level_at(float x)
{
    Clay_ElementData d = Clay_GetElementData(CLAY_ID("zoom-track"));
    if (!d.found) return -1;
    float step = (float)(ZOOM_TRACK_W - ZOOM_KNOB_W) / (UI_ZOOMS - 1);
    int k = (int)lroundf((x - d.boundingBox.x - ZOOM_KNOB_W / 2.0f) / step);
    return k < 0 ? 0 : k >= UI_ZOOMS ? UI_ZOOMS - 1 : k;
}

// The minus or the plus, drawn; dim at its end of the sizes.
static void zoom_button(UiBar which, bool plus, bool enabled)
{
    const char *id = BAR_IDS[which];
    Theme *t = &app.theme;
    bool hover = enabled && app.hov.bar == which;
    Clay_Color c = !enabled ? dim_color() : hover ? cc(t->fg) : cc(t->fg_dim);
    CLAY(Clay_GetElementId(cz(id)), { .layout = { .sizing = { CLAY_SIZING_FIXED(ZOOM_BTN_W), CLAY_SIZING_FIXED(MODE_BTN_H) },
                                                  .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                                      .backgroundColor = hover ? cc(t->bg) : NO_COLOR, .cornerRadius = CLAY_CORNER_RADIUS(3) }) {
        CLAY(Clay_GetElementId(cz(arena_sprintf(&frame, "%s-bar", id))), { .layout = { .sizing = { CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(2) } },
                                                                             .backgroundColor = c }) {
            if (plus)
                CLAY(CLAY_ID("zoom-in-stem"), { .layout = { .sizing = { CLAY_SIZING_FIXED(2), CLAY_SIZING_FIXED(10) } }, .backgroundColor = c,
                                                .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 1,
                                                              .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER } } }) {}
        }
    }
}

// The slider: a line with a notch a size, the knob on the size shown, the minus and plus beside.
static void layout_zoom(void)
{
    Theme *t = &app.theme;
    int k = ui_zoom_level(app.ui.zoom);
    Clay_Color line = rule_color();
    CLAY(CLAY_ID("zoom"), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_GROW(0) }, .padding = { PAD, 0, 0, 0 }, .childGap = 4,
                                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        zoom_button(UB_ZOOM_OUT, false, k > 0);
        CLAY(CLAY_ID("zoom-track"), { .layout = { .sizing = { CLAY_SIZING_FIXED(ZOOM_TRACK_W), CLAY_SIZING_FIXED(MODE_BTN_H) } } }) {
            CLAY(CLAY_ID("zoom-line"), { .layout = { .sizing = { CLAY_SIZING_FIXED(ZOOM_TRACK_W - ZOOM_KNOB_W), CLAY_SIZING_FIXED(2) } }, .backgroundColor = line,
                                         .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 1,
                                                       .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER } } }) {}
            for (int j = 0; j < UI_ZOOMS; j++)
                CLAY(CLAY_IDI("zoom-notch", j), { .layout = { .sizing = { CLAY_SIZING_FIXED(2), CLAY_SIZING_FIXED(8) } }, .backgroundColor = line,
                                                  .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 1, .offset = { zoom_x(j) - 1, 0 },
                                                                .attachPoints = { CLAY_ATTACH_POINT_LEFT_CENTER, CLAY_ATTACH_POINT_LEFT_CENTER } } }) {}
            bool lit = app.hov.zoom || app.drag == DRAG_ZOOM;
            CLAY(CLAY_ID("zoom-knob"), { .layout = { .sizing = { CLAY_SIZING_FIXED(ZOOM_KNOB_W), CLAY_SIZING_FIXED(14) } },
                                         .backgroundColor = lit ? cc(t->fg) : cc(t->accent), .cornerRadius = CLAY_CORNER_RADIUS(3),
                                         .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 2, .offset = { zoom_x(k) - ZOOM_KNOB_W / 2.0f, 0 },
                                                       .attachPoints = { CLAY_ATTACH_POINT_LEFT_CENTER, CLAY_ATTACH_POINT_LEFT_CENTER } } }) {}
        }
        zoom_button(UB_ZOOM_IN, true, k < UI_ZOOMS - 1);
    }
}

static void layout_bar(float w)
{
    Theme *t = &app.theme;
    bool can_up = view_can_up();
    CLAY(CLAY_ID("header"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(HEADER_H) },
                                          .padding = { PAD / 2, PAD / 2, 0, 0 }, .childGap = BAR_GAP,
                                          .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                              .backgroundColor = cc(t->bg_light) }) {
        bar_button(UB_BACK, CHEV_LEFT, NULL, NULL, hist.nback > 0, BAR_BTN_W);
        bar_button(UB_FORWARD, CHEV_RIGHT, NULL, NULL, hist.nfwd > 0, BAR_BTN_W);
        bar_button(UB_UP, CHEV_UP, NULL, NULL, can_up, BAR_BTN_W);
        float room = bar_room(w);
        bool locating = app.ui.focus == UI_FOCUS_EDIT && app.ui.edit_path;
        const char *branch = locating ? NULL : branch_label();   // (the location line takes the room)
        float bw = branch_btn_w(w, branch);
        CLAY(CLAY_ID("crumbs"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(BAR_BTN_H) }, .padding = { PAD / 2, 0, 0, 0 },
                                              .childGap = BAR_GAP, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                  .clip = { .horizontal = true } }) {
            if (locating) layout_location(room);
            else layout_crumbs(branch ? room - bw - BAR_GAP : room);
            if (branch) branch_button(branch, bw);
        }
        tool_button(UB_LOCATION, TOOL_PENCIL, app.ui.focus == UI_FOCUS_EDIT && app.ui.edit_path);
        tool_button(UB_SEARCH, TOOL_SEARCH, app.ui.finding);
        view_button(UB_VIEW_LIST, false);
        view_button(UB_VIEW_ICONS, true);
    }
}

// The tab bar: the tabs share the width, each its folder's name and a ×; the one on screen marked.
static void layout_tabs(float w)
{
    Theme *t = &app.theme;
    float each = (w - PAD) / (float)tabbar.n;
    CLAY(CLAY_ID("tabs"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(TAB_H) }, .padding = { PAD / 2, PAD / 2, 0, 0 } },
                            .backgroundColor = cc(t->bg_light),
                            .border = { .color = rule_color(), .width = { .bottom = 1 } } }) {
        for (int i = 0; i < tabbar.n; i++) {
            bool cur = i == tabbar.cur, hover = app.hov.tab == i;
            const char *dir = cur ? app.cwd : tabs[tabbar.slot[i]].dir;
            Str name = strcmp(dir, "/") == 0 ? S("/") : path_base(str_from_c(dir)), root, what, inside;
            if (search_parse(dir, &root, &what, &inside)) {   // a search (F9): what it looks for, and where
                if (inside.len) what = inside;   // (inside files, step 3: the text looked for there)
                name = str_from_c(what.len ? arena_sprintf(&frame, "%.*s in %.*s", STR_ARG(what), STR_ARG(path_base(root)))
                                           : arena_sprintf(&frame, "Search in %.*s", STR_ARG(root.len == 1 ? root : path_base(root))));
            }
            CLAY(CLAY_IDI("tab", i), { .layout = { .sizing = { CLAY_SIZING_FIXED(each), CLAY_SIZING_FIXED(TAB_H) }, .padding = { PAD, 4, 0, 0 },
                                                   .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                       .backgroundColor = cur ? cc(t->bg) : hover ? mix(t->bg, t->bg_light, 0.5f) : cc(t->bg_light),
                                       .border = { .color = cur ? cc(t->accent) : rule_color(),
                                                   .width = { .right = 1, .bottom = cur ? 2 : 0 } } }) {
                CLAY(CLAY_IDI("tab-name", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
                    text(fit(name, each - PAD - TAB_X_W - 12, false), cur ? cc(t->fg) : cc(t->fg_dim));
                }
                bool xhover = app.hov.tab_x == i;
                CLAY(CLAY_IDI("tab-x", i), { .layout = { .sizing = { CLAY_SIZING_FIXED(TAB_X_W), CLAY_SIZING_FIXED(TAB_X_W) },
                                                         .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                                             .backgroundColor = xhover ? cc(t->bg_light) : (cur ? cc(t->bg) : hover ? mix(t->bg, t->bg_light, 0.5f) : cc(t->bg_light)) }) {
                    text(cz("\xC3\x97"), xhover ? cc(t->fg) : cc(t->fg_dim));   // ×
                }
            }
        }
    }
}

// ---- the menu bar (F7.1) ----------------------------------------------------------------------

// The menu's padding, an entry's, its marks' column and the gap after it, and the gap between an
// entry's label, the spring and its key: layout_menu draws with them, menu_size measures with them.
enum { MENU_PAD = 4, MENU_ITEM_PAD = 10, MENU_MARK_W = 14, MENU_MARK_GAP = 6, MENU_GAP = 32, MENU_LABEL_MAX = 360 };

// Entry it's label as its menu draws it: a bookmark's, a branch's (cut to MENU_LABEL_MAX), Open at
// Line's with its editor, "N more", or the table's (the F15 code-health review: menu_size measured
// the branch's uncut).
static Clay_String menu_label(UiMenuItem it)
{
    const Bookmark *bk = ui_slot(it, UM_BM_0, UI_MENU_BOOKMARKS) >= 0 ? bm_menu(ui_slot(it, UM_BM_0, UI_MENU_BOOKMARKS)) : NULL;
    if (it == UM_BM_MORE) return cz(arena_sprintf(&frame, "%d more in Edit Bookmarks\xE2\x80\xA6", app.ui.bm_more));
    if (it == UM_OPEN_LINE) return cs(open_line_label());   // (the editor's name: F9 step 4)
    if (it >= UM_BR_NOTE && it <= UM_BR_ALL) return fit(branch_menu_label(it), MENU_LABEL_MAX, true);   // (F12 step 5b)
    return bk ? fit(bm_label(bk), MENU_LABEL_MAX, true) : cz(UI_MENU_ITEM[it].label);
}

// Whether menu m has a column for checks and a group's dots (any entry with a mark, shown or not).
static bool menu_marks(int m)
{
    bool marks = false;
    for (int i = 0; i < UI_MENU[m].n; i++) marks |= UI_MENU_ITEM[UI_MENU[m].items[i]].mark != 0;
    return marks;
}

// Menu m's entries under its title: each label on the left and its key on the right, those that
// can't be picked now dimmed, the marked one in the selection colour; lines between the groups.
// A menu with checks or a group's dots keeps a column for them on the left (F7.4); an entry not
// shown now (Restore outside the trash view) is left out.
static void layout_menu(int m)
{
    Theme *t = &app.theme;
    Clay_Color off = rule_color();
    // A bar menu hangs from its title; a right-click one floats where ctx_place put it (F8).
    Clay_FloatingElementConfig where = m < UI_MENUS ? (Clay_FloatingElementConfig){ .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 30, .offset = { 0, 2 },
                                                                                  .attachPoints = { CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_BOTTOM } }
                                                 : (Clay_FloatingElementConfig){ .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 30, .offset = { app.ctx_x, app.ctx_y } };
    CLAY(CLAY_ID("menu"), { .layout = { .sizing = { CLAY_SIZING_FIT(MENU_W_MIN, 0), CLAY_SIZING_FIT(0) }, .padding = { MENU_PAD, MENU_PAD, MENU_PAD, MENU_PAD },
                                        .layoutDirection = CLAY_TOP_TO_BOTTOM },
                            .backgroundColor = cc(t->bg_light),
                            .border = { .color = mix(t->fg_dim, t->bg_light, 0.6f), .width = CLAY_BORDER_OUTSIDE(1) },
                            .floating = where }) {
        bool marks = menu_marks(m);
        for (int i = 0; i < UI_MENU[m].n; i++) {
            UiMenuItem it = UI_MENU[m].items[i];
            if (!ui_menu_shown(&app.ui, it) || (it == UM_SEP && !ui_menu_sep_shown(&app.ui, m, i))) continue;
            if (it == UM_SEP) {
                CLAY(CLAY_IDI("menu-sep", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(MENU_SEP_H) }, .padding = { 8, 8, 4, 4 } } }) {
                    CLAY(CLAY_IDI("menu-line", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } },
                                                     .backgroundColor = mix(t->fg_dim, t->bg_light, 0.3f) }) {}
                }
                continue;
            }
            bool on = ui_menu_enabled(&app.ui, it), at = on && app.ui.menu_at == i;
            CLAY(CLAY_IDI("menu-item", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(MENU_ROW_H) }, .padding = { MENU_ITEM_PAD, MENU_ITEM_PAD, 0, 0 },
                                                         .childGap = MENU_GAP, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                             .backgroundColor = at ? cc(t->selection) : cc(t->bg_light), .cornerRadius = CLAY_CORNER_RADIUS(3) }) {
                CLAY(CLAY_IDI("menu-label", i), { .layout = { .childGap = MENU_MARK_GAP, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                    if (marks) {
                        int mk = ui_menu_mark(&app.ui, it);
                        CLAY(CLAY_IDI("menu-mark", i), { .layout = { .sizing = { CLAY_SIZING_FIXED(MENU_MARK_W), CLAY_SIZING_FIT(0) } } }) {
                            // ✓ for a check, • for the current one of a group
                            if (mk == 1) text(cz(UI_MENU_ITEM[it].mark == 'c' ? "\xE2\x9C\x93" : "\xE2\x80\xA2"), on ? cc(t->accent) : off);
                        }
                    }
                    text(menu_label(it), on ? cc(t->fg) : off);
                }
                CLAY(CLAY_IDI("menu-gap", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } } }) {}
                if (ui_menu_key(it)[0]) text(cz(ui_menu_key(it)), on ? cc(t->fg_dim) : off);
            }
        }
    }
}

// The menu bar over the top bar (always shown, Michael 2026-10-25): the menus' titles, the open
// one's menu hanging from it.
static void layout_menubar(void)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("menubar"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(MENU_H) }, .padding = { PAD / 2, PAD / 2, 0, 0 },
                                           .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                               .backgroundColor = cc(t->bg_light),
                               .border = { .color = mix(t->fg_dim, t->bg_light, 0.2f), .width = { .bottom = 1 } } }) {
        for (int m = 0; m < UI_MENUS; m++) {
            bool open = app.ui.menu_on && app.ui.menu == m, hover = app.hov.menu_title == m;
            CLAY(CLAY_IDI("menu-title", m), { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(MENU_H - 4) },
                                                          .padding = { MENU_TITLE_PAD, MENU_TITLE_PAD, 0, 0 },
                                                          .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                                              .backgroundColor = open ? cc(t->selection) : hover ? cc(t->bg) : cc(t->bg_light),
                                              .cornerRadius = CLAY_CORNER_RADIUS(3) }) {
                text(cz(UI_MENU[m].title), cc(t->fg));
                if (open) layout_menu(m);
            }
        }
    }
}
