// Clay render commands drawn with raylib: rectangles (rounded or not), borders, text and clipping.
// Copied from prdash's gui/render.c.
// One Clay unit is one logical pixel.
//
// Text is measured and drawn glyph by glyph from the same metrics (font.c), so what Clay lays out
// is exactly what lands on screen. Strings are Clay slices, not C strings, so they go through the
// core's bounded utf8_next, never raylib's NUL-terminated helpers.

#include "raylib.h"
#include "font.c"

static float render_fade;            // 0..1: every colour mixed this far towards render_fade_to
static Clay_Color render_fade_to;

static Color render_color(Clay_Color c)
{
    if (render_fade > 0) {
        c.r += (render_fade_to.r - c.r) * render_fade;
        c.g += (render_fade_to.g - c.g) * render_fade;
        c.b += (render_fade_to.b - c.b) * render_fade;
    }
    return (Color){ (unsigned char)c.r, (unsigned char)c.g, (unsigned char)c.b, (unsigned char)c.a };
}

static Clay_Dimensions render_measure_text(Clay_StringSlice text, Clay_TextElementConfig *config, void *user)
{
    (void)user;
    float size = config->fontSize, w = 0;
    for (isize i = 0; i < text.length;) {
        u32 cp = utf8_next(text.chars, text.length, &i);
        w += font_advance(cp, size) + config->letterSpacing;
    }
    return (Clay_Dimensions){ w, size };
}

static void render_text(Clay_BoundingBox b, Clay_TextRenderData *t)
{
    Color fg = render_color(t->textColor);
    float size = t->fontSize, x = b.x;
    Clay_StringSlice s = t->stringContents;
    for (isize i = 0; i < s.length;) {
        u32 cp = utf8_next(s.chars, s.length, &i);
        font_draw(cp, x, b.y, size, fg);
        x += font_advance(cp, size) + t->letterSpacing;
    }
}

static void render_rect(Clay_BoundingBox b, Clay_RectangleRenderData *r)
{
    Color c = render_color(r->backgroundColor);
    Rectangle rec = { b.x, b.y, b.width, b.height };
    float radius = r->cornerRadius.topLeft;   // we only ever round all four corners alike
    float side   = b.width < b.height ? b.width : b.height;
    if (radius > 0 && side > 0) DrawRectangleRounded(rec, radius * 2 / side, 8, c);
    else DrawRectangleRec(rec, c);
}

static void render_border(Clay_BoundingBox b, Clay_BorderRenderData *r)
{
    Color c = render_color(r->color);
    if (r->width.top)    DrawRectangleRec((Rectangle){ b.x, b.y, b.width, r->width.top }, c);
    if (r->width.bottom) DrawRectangleRec((Rectangle){ b.x, b.y + b.height - r->width.bottom, b.width, r->width.bottom }, c);
    if (r->width.left)   DrawRectangleRec((Rectangle){ b.x, b.y, r->width.left, b.height }, c);
    if (r->width.right)  DrawRectangleRec((Rectangle){ b.x + b.width - r->width.right, b.y, r->width.right, b.height }, c);
}

// Clips nest (a clipped row inside a clipped card), so they are a stack: each one is cut to the one
// around it, and ending one restores the one around it rather than switching clipping off. Custom
// elements clip through the same stack.
static Rectangle render_clips[32];
static int       render_nclips;

static void render_apply_clip(void)
{
    if (render_nclips == 0) {
        EndScissorMode();
        return;
    }
    Rectangle r = render_clips[render_nclips - 1];
    BeginScissorMode((int)floorf(r.x), (int)floorf(r.y), (int)ceilf(r.width), (int)ceilf(r.height));
}

static void render_clip_push(Clay_BoundingBox b)
{
    Rectangle r = { b.x, b.y, b.width, b.height };
    if (render_nclips > 0) {
        Rectangle o = render_clips[render_nclips - 1];
        float x0 = fmaxf(r.x, o.x), y0 = fmaxf(r.y, o.y);
        float x1 = fminf(r.x + r.width, o.x + o.width), y1 = fminf(r.y + r.height, o.y + o.height);
        r = (Rectangle){ x0, y0, fmaxf(0, x1 - x0), fmaxf(0, y1 - y0) };
    }
    if (render_nclips < (int)(sizeof render_clips / sizeof *render_clips)) render_clips[render_nclips++] = r;
    render_apply_clip();
}

static void render_clip_pop(void)
{
    if (render_nclips > 0) render_nclips--;
    render_apply_clip();
}

// Draws a custom element (the diagram canvas); set by the program.
static void (*render_custom)(Clay_BoundingBox b, void *data);

static void render_draw(Clay_RenderCommandArray cmds, Clay_Color clear)
{
    BeginDrawing();
    render_nclips = 0;
    ClearBackground(render_color(clear));
    for (int32_t i = 0; i < cmds.length; i++) {
        Clay_RenderCommand *cmd = Clay_RenderCommandArray_Get(&cmds, i);
        Clay_BoundingBox b = cmd->boundingBox;
        switch (cmd->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: render_rect(b, &cmd->renderData.rectangle); break;
        case CLAY_RENDER_COMMAND_TYPE_BORDER:    render_border(b, &cmd->renderData.border); break;
        case CLAY_RENDER_COMMAND_TYPE_TEXT:      render_text(b, &cmd->renderData.text); break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: render_clip_push(b); break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: render_clip_pop(); break;
        case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
            if (render_custom) render_custom(b, cmd->renderData.custom.customData);
            break;
        default: break;   // no images
        }
    }
    EndDrawing();
}
