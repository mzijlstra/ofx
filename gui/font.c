// Text for the GUI, with any Unicode a source file can hold.
//
// Copied from prdash (tools/prdash/gui/font.c), where it was proved; see its docs/gui-findings.md.
//
// raylib's own fonts cannot do this: a font is one atlas built at load time from a fixed list of
// code points, it opens only .ttf/.otf (never a .ttc collection, which is how every CJK font on
// this machine ships), and it always reads face 0. So the GUI keeps its own glyph cache:
//
// - fontconfig picks a face for each code point the primary font lacks (file and face index);
// - font files are mapped read-only, never copied;
// - a glyph's metrics are cached per code point and size the first time text is measured, and it
//   is rasterised into one shared atlas texture only the first time it is drawn: the layout
//   measures every element, offscreen ones too, but only visible ones are drawn;
// - glyphs are rasterised at the window's pixel scale, and drawn at the logical size, so text is
//   sharp on a HiDPI output.
//
// Rasterising (M9d, 2026-10-14): FreeType with light hinting, as GTK draws text on Michael's
// desktop (fontconfig: hintstyle slight, grey antialiasing). Michael compared it with stb_truetype
// (raylib's), snapped and unsnapped, and picked it. Light hinting fits outlines to the pixel grid
// vertically only, so each glyph's origin must sit on a whole device pixel: the pen and the
// baseline are rounded when a glyph is drawn, never when text is measured. Advances are the
// font's own unhinted widths, scaled exactly, so text measures as it did with stb_truetype and no
// layout moved. FreeType allocates for itself (as fontconfig does): our code still doesn't.
//
// A code point no installed face can draw shows as an empty box. Colour emoji fall in that group:
// Noto Color Emoji has only bitmaps (CBDT) at fixed sizes, and a face that can't scale is refused.

#include <fcntl.h>
#include <math.h>
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

enum { FACE_MAX = 16, GLYPH_MAX = 16384, GLYPH_SLOTS = 32768, ATLAS = 2048, GLYPH_PX_MAX = 256 };

typedef struct {
    char    path[512];
    int     index;                 // face within a .ttc collection
    FT_Face ft;
    float   scale;                 // font units to pixels at the current size
} Face;

typedef struct {
    u32       cp;
    int       px;                  // the pixel size its metrics are for
    bool      mono;                // from the monospace face (the source pane), when it has cp
    int       face;                // -1: no face has it, drawn as a box
    u32       index;               // its glyph in that face
    float     adv;                 // in pixels, unhinted
    // Known once it is placed: the hinted bitmap, from the pen on the baseline.
    int       w, h;                // 0 for a blank glyph such as a space
    int       left, top;           // FreeType's bitmap_left and bitmap_top
    bool      placed;              // rasterised, and in the atlas at src unless blank
    Rectangle src;
} Glyph;

static FT_Library ft_lib;
static Face      faces[FACE_MAX];
static int       nfaces;
static Glyph     glyphs[GLYPH_MAX];
static int       nglyphs;
static int       glyph_slot[GLYPH_SLOTS];   // open addressing into glyphs[], 0 = empty, else index + 1
static Texture2D atlas;
static u8        atlas_px[ATLAS * ATLAS * 2];   // gray + alpha, white text with coverage in alpha
static int       pack_x, pack_y, pack_row;
static int       mono_face = -1;                // fontconfig's "monospace", for source text
static float     font_dpi = 1;                  // physical pixels per logical pixel
static int       font_px;                       // the size the faces' scales are set for
static float     font_ascent;                   // the primary face's, at font_px
static float     font_em;                       // the primary face's em in pixels, at font_px
// Atlas overflow is handled between frames, never inside one: a glyph with no room is drawn as a
// box and marks the frame; the next frame starts from an empty atlas. A frame that overflows
// although it started empty shows more text than the atlas holds, so it keeps its boxes instead
// of asking for another redraw, which would only overflow again.
static bool      overflowed;                    // this frame found no room for a glyph
static bool      fresh;                         // this frame started from an empty atlas

// ---- faces ----------------------------------------------------------------------------------

// Opens a face, or finds it open. When need is not 0 and the face lacks it, the face is not kept:
// a fallback that cannot help must not take one of the FACE_MAX slots.
// Sets f's size to font_em pixels per em, for hinting and rasterising. Measuring uses f->scale.
static void face_size(Face *f)
{
    FT_Set_Char_Size(f->ft, 0, (FT_F26Dot6)lroundf(font_em * 64), 72, 72);
}

static int face_open(const char *path, int index, u32 need)
{
    for (int f = 0; f < nfaces; f++)
        if (faces[f].index == index && strcmp(faces[f].path, path) == 0) return f;
    if (nfaces == FACE_MAX) return -1;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    struct stat st;
    void *data = fstat(fd, &st) == 0 ? mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;
    close(fd);
    if (data == MAP_FAILED) return -1;

    Face *f = &faces[nfaces];
    if (FT_New_Memory_Face(ft_lib, data, (FT_Long)st.st_size, index, &f->ft) != 0) {
        munmap(data, (size_t)st.st_size);
        return -1;
    }
    if (!FT_IS_SCALABLE(f->ft) || (need && !FT_Get_Char_Index(f->ft, need))) {
        FT_Done_Face(f->ft);
        munmap(data, (size_t)st.st_size);
        return -1;
    }
    snprintf(f->path, sizeof f->path, "%s", path);
    f->index = index;
    f->scale = font_px ? font_em / (float)f->ft->units_per_EM : 0;
    if (font_px) face_size(f);
    return nfaces++;
}

// The face fontconfig would use for `pattern`, or for cp in that family when cp is not 0.
static int face_match(const char *pattern, u32 cp)
{
    FcPattern *pat = FcNameParse((const FcChar8 *)pattern);
    if (!pat) return -1;
    if (cp) {
        FcCharSet *cs = FcCharSetCreate();
        FcCharSetAddChar(cs, cp);
        FcPatternAddCharSet(pat, FC_CHARSET, cs);
        FcCharSetDestroy(cs);
    }
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult res;
    FcPattern *match = FcFontMatch(NULL, pat, &res);
    FcPatternDestroy(pat);
    if (!match) return -1;

    FcChar8 *file = NULL;
    int index = 0, f = -1;
    if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
        FcPatternGetInteger(match, FC_INDEX, 0, &index);
        f = face_open((const char *)file, index, cp);
    }
    FcPatternDestroy(match);
    return f;
}

// The first face that has an outline glyph for cp: the primary, one already open, or the one
// fontconfig picks for it.
static int face_for(u32 cp)
{
    for (int f = 0; f < nfaces; f++)
        if (FT_Get_Char_Index(faces[f].ft, cp)) return f;
    int f = face_match("sans-serif", cp);
    return f >= 0 && FT_Get_Char_Index(faces[f].ft, cp) ? f : -1;
}

// ---- the glyph cache ------------------------------------------------------------------------

// Forgets every placement: the next draw of each glyph rasterises it again. Metrics stay.
static void atlas_reset(void)
{
    for (int i = 0; i < nglyphs; i++) glyphs[i].placed = false;
    pack_x = pack_y = pack_row = 0;
    memset(atlas_px, 0, sizeof atlas_px);   // stale texels would bleed into new glyphs' edges
    UpdateTexture(atlas, atlas_px);
}

// The primary face fills px from ascent to descent: FreeType's are hhea's, as stb_truetype measured
// them until M9d. Every other face gets the same em size, not the same height: CJK faces have a
// taller design box, and fitting it into px made them smaller.
static void set_size(int px)
{
    if (px == font_px) return;
    font_px = px;
    FT_Face ft = faces[0].ft;
    float ascent = ft->ascender, descent = ft->descender;
    faces[0].scale = (float)px / (ascent - descent);
    font_em = faces[0].scale * (float)ft->units_per_EM;
    for (int f = 1; f < nfaces; f++) faces[f].scale = font_em / (float)faces[f].ft->units_per_EM;
    for (int f = 0; f < nfaces; f++) face_size(&faces[f]);
    font_ascent = ascent * faces[0].scale;
}

// Only the advance: what layout needs. The bitmap's box comes from hinting, when it is placed.
static void glyph_measure(Glyph *g)
{
    g->face = g->mono && mono_face >= 0 && FT_Get_Char_Index(faces[mono_face].ft, g->cp) ? mono_face : face_for(g->cp);
    if (g->face < 0) {   // drawn as a box
        g->adv = (float)g->px * 0.6f;
        return;
    }
    Face *f = &faces[g->face];
    g->index = FT_Get_Char_Index(f->ft, g->cp);
    FT_Fixed adv = 0;
    FT_Get_Advance(f->ft, g->index, FT_LOAD_NO_SCALE, &adv);   // font units: hmtx's width
    g->adv = (float)adv * f->scale;
}

// The glyph for cp at px, measured but not necessarily placed. When the table is full it is
// measured into a scratch entry every time: slower, never wrong.
static Glyph *glyph(u32 cp, int px, bool mono)
{
    set_size(px);
    u32 h = (cp * 2654435761u) ^ (u32)px ^ (mono ? 0x9e3779b9u : 0);
    for (u32 k = 0; k < GLYPH_SLOTS; k++) {
        int *slot = &glyph_slot[(h + k) % GLYPH_SLOTS];
        if (*slot != 0) {
            Glyph *g = &glyphs[*slot - 1];
            if (g->cp == cp && g->px == px && g->mono == mono) return g;
            continue;
        }
        static Glyph scratch;
        Glyph *g = nglyphs < GLYPH_MAX ? &glyphs[nglyphs] : &scratch;
        *g = (Glyph){ .cp = cp, .px = px, .mono = mono };
        glyph_measure(g);
        if (g != &scratch) *slot = ++nglyphs;
        return g;
    }
    return &glyphs[0];   // unreachable: there are twice as many slots as glyphs
}

// Rasterises g into the atlas if it is not there yet. False when there is no room. A glyph
// FreeType can't load or render as grey coverage, or an absurdly large one, loses its face and is
// drawn as a box from then on; a space renders to nothing and stays blank.
static bool place(Glyph *g)
{
    if (g->placed) return true;
    Face *f = &faces[g->face];
    FT_GlyphSlot slot = f->ft->glyph;
    // NO_BITMAP: a face's embedded strike would come back one bit deep, and outlines are hinted
    // the same at every size.
    if (FT_Load_Glyph(f->ft, g->index, FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP) != 0 ||
        FT_Render_Glyph(slot, FT_RENDER_MODE_LIGHT) != 0 || slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY ||
        slot->bitmap.width > GLYPH_PX_MAX || slot->bitmap.rows > GLYPH_PX_MAX) {
        g->face = -1;
        return true;
    }
    g->w = (int)slot->bitmap.width, g->h = (int)slot->bitmap.rows;
    g->left = slot->bitmap_left, g->top = slot->bitmap_top;
    if (g->w == 0 || g->h == 0) {
        g->w = g->h = 0;
        g->placed = true;
        return true;
    }
    if (pack_x + g->w + 1 > ATLAS) pack_x = 0, pack_y += pack_row + 1, pack_row = 0;
    if (pack_y + g->h + 1 > ATLAS) return false;
    int x = pack_x, y = pack_y;
    pack_x += g->w + 1;
    if (g->h > pack_row) pack_row = g->h;

    static u8 ga[GLYPH_PX_MAX * GLYPH_PX_MAX * 2];
    const FT_Bitmap *bm = &slot->bitmap;
    for (int r = 0; r < g->h; r++) {
        const u8 *row = bm->buffer + (isize)r * bm->pitch;   // pitch may be negative (bottom-up)
        for (int c = 0; c < g->w; c++) ga[2 * (r * g->w + c)] = 255, ga[2 * (r * g->w + c) + 1] = row[c];
    }
    for (int r = 0; r < g->h; r++)
        memcpy(&atlas_px[((y + r) * ATLAS + x) * 2], &ga[r * g->w * 2], (size_t)g->w * 2);
    UpdateTextureRec(atlas, (Rectangle){ (float)x, (float)y, (float)g->w, (float)g->h }, ga);
    g->src = (Rectangle){ (float)x, (float)y, (float)g->w, (float)g->h };
    g->placed = true;
    return true;
}

// ---- the interface render.c uses ------------------------------------------------------------

// Opens the primary face, enough to measure text without a window (the layout commands do).
// False when fontconfig finds no usable sans-serif face.
static bool font_open(void)
{
    return FT_Init_FreeType(&ft_lib) == 0 && FcInit() && face_match("sans-serif", 0) == 0;
}

// Opens the primary face, the monospace one (when there is one: else source text is drawn in the
// primary), and the atlas. False when fontconfig finds no usable sans-serif face.
static bool font_init(void)
{
    if (!font_open()) return false;
    mono_face = face_match("monospace", 0);
    Image img = { .data = atlas_px, .width = ATLAS, .height = ATLAS, .mipmaps = 1,
                  .format = PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA };
    atlas = LoadTextureFromImage(img);
    // Every glyph is drawn texel for device pixel (its origin is snapped), so filtering never
    // blends: bilinear still smooths a glyph drawn at a fractional scale (canvas_snapshot's).
    SetTextureFilter(atlas, TEXTURE_FILTER_BILINEAR);
    return IsTextureValid(atlas);
}

// The window's pixel scale. A change measures and rasterises every glyph again at the new size.
static void font_set_dpi(float dpi)
{
    if (dpi <= 0 || dpi == font_dpi) return;
    font_dpi = dpi;
    nglyphs = 0;
    memset(glyph_slot, 0, sizeof glyph_slot);
    atlas_reset();
}

// Around every frame. font_frame_end says whether to draw again: true only when this frame ran out
// of atlas and the next one may start from an empty atlas, which it will.
static void font_frame_begin(void)
{
    bool was_fresh = fresh;
    fresh = overflowed && !was_fresh;
    if (fresh) atlas_reset();
    overflowed = false;
}

static bool font_frame_end(void)
{
    return overflowed && !fresh;
}

static float font_advance_in(u32 cp, float size, bool mono)
{
    return glyph(cp, (int)lroundf(size * font_dpi), mono)->adv / font_dpi;
}

static float font_advance(u32 cp, float size)
{
    return font_advance_in(cp, size, false);
}

static void font_draw_in(u32 cp, float x, float y, float size, Color tint, bool mono)
{
    float d = font_dpi;
    Glyph *g = glyph(cp, (int)lroundf(size * font_dpi), mono);
    bool room = g->face < 0 || place(g);
    if (g->face < 0 || !room) {   // nothing can draw it, or no room: a box, not silently lost
        if (!room) overflowed = true;
        DrawRectangleLinesEx((Rectangle){ x + 1, y + (font_ascent - (float)g->px * 0.7f) / d,
                                          g->adv / d - 2, size * 0.7f }, 1, tint);
        return;
    }
    if (g->w == 0) return;
    // Hinting fitted the outline to a pen and a baseline on whole device pixels: put them there.
    float pen = roundf(x * d), base = roundf(y * d + font_ascent);
    Rectangle dst = { (pen + (float)g->left) / d, (base - (float)g->top) / d, g->src.width / d, g->src.height / d };
    DrawTexturePro(atlas, g->src, dst, (Vector2){ 0, 0 }, 0, tint);
}

static void font_draw(u32 cp, float x, float y, float size, Color tint)
{
    font_draw_in(cp, x, y, size, tint, false);
}
