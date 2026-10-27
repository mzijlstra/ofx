// Help → About (Michael, 2026-10-27): the name, the author, the build's version, the license and the
// vendored libraries; Close, Esc or Enter close it. And the note shown as the menu bar is hidden. OFX_VERSION is the last commit's date and hash,
// "+dirty" when the tree had changes (build/version.h, made by make). Included by main.c after
// shortcuts.c.

#include "version.h"

static void layout_about_panel(void)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("about"), { .layout = { .sizing = { CLAY_SIZING_FIXED(420), CLAY_SIZING_FIT(0) }, .padding = { 24, 24, 24, 20 }, .childGap = 8,
                                         .layoutDirection = CLAY_TOP_TO_BOTTOM, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
                             .backgroundColor = cc(t->bg_light),
                             .border = { .color = cc(t->accent), .width = CLAY_BORDER_OUTSIDE(1) },
                             .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 20,
                                           .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER } } }) {
        CLAY_TEXT(CLAY_STRING("OFX"), { .textColor = cc(t->fg), .fontSize = 36, .letterSpacing = 2, .wrapMode = CLAY_TEXT_WRAP_NONE });
        text(cz("Omarchy File Explorer"), cc(t->fg));
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(6) } } }) {}
        text(cz("By A.M.S. Zijlstra"), cc(t->fg_dim));
        text(cz(OFX_VERSION), cc(t->fg_dim));
        text(cz("MIT License"), cc(t->fg_dim));
        text(cz("Built with raylib, Clay and nanosvg"), cc(t->fg_dim));
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(8) } } }) {}
        modal_button("modal-no", UI_MODAL_NO, "Close", true, false);
    }
}

// View → Show Menu unchecked (Michael, 2026-10-27): how the menus still open. Not plain Alt: the
// edit and find lines take Alt with a letter (Alt+C, W, R) for themselves.
static void layout_bar_panel(void)
{
    Theme *t = &app.theme;
    CLAY(CLAY_ID("bar-note"), { .layout = { .sizing = { CLAY_SIZING_FIXED(460), CLAY_SIZING_FIT(0) }, .padding = { 24, 24, 20, 20 }, .childGap = 8,
                                            .layoutDirection = CLAY_TOP_TO_BOTTOM },
                                .backgroundColor = cc(t->bg_light),
                                .border = { .color = cc(t->accent), .width = CLAY_BORDER_OUTSIDE(1) },
                                .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 20,
                                              .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER } } }) {
        text(cz("The menu bar is hidden"), cc(t->fg));
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(4) } } }) {}
        text(cz("Its menus still open with F10, or with Alt and"), cc(t->fg_dim));
        text(cz("a menu's letter: Alt+F, E, V, G, B, H."), cc(t->fg_dim));
        text(cz("View \xE2\x86\x92 Show Menu brings it back."), cc(t->fg_dim));
        CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = { 0, 0, 8, 0 },
                                   .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
            modal_button("modal-ok", UI_MODAL_OK, "OK", true, false);
        }
    }
}
