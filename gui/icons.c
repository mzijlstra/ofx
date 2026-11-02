// The icons: the theme's PNGs and SVGs, cached by name and size, and the previews' textures. Included by main.c.

// ---- icons ----------------------------------------------------------------------------------

static int window_scale(void)
{
    float s = GetWindowScaleDPI().x;
    return s >= 1.5f ? (int)lroundf(s) : 1;
}

// Every texture dropped, and the icon theme read again: a theme change, or a new scale.
static void icons_reset(void)
{
    for (int i = 0; i < app.nslots; i++)
        if (app.slot[i].tex.id) UnloadTexture(app.slot[i].tex);
    app.nslots = 0;
    arena_reset(&icona);
    icon_load(&icona, &app.icons, str_from_c(app.theme.icons));
    strmap_init(&app.slot_of, &icona, ICONS_MAX);
    app.icon_scale = window_scale();
}

static Str entry_mime(isize i)
{
    if (!app.mime[i].p) app.mime[i] = mime_of(&scratch, &app.mime_db, &app.list.e[i]);
    return app.mime[i];
}

// An SVG drawn into an image `want` px on its longer side (app icons, F7.3; nanosvg, vendored):
// into a static buffer, which LoadTextureFromImage copies. data NULL when it won't parse, or is
// over 1 MiB: nanosvg takes seconds on the main thread for a 10 MB one, and real icons are a few
// KiB (F7.3.1 review).
static Image svg_image(const char *file, int want)
{
    enum { SVG_PX_MAX = 512, SVG_BYTES_MAX = 1 << 20 };
    static u8 pixels[SVG_PX_MAX * SVG_PX_MAX * 4];
    Image img = { 0 };
    struct stat st;
    if (stat(file, &st) != 0 || st.st_size > SVG_BYTES_MAX) return img;
    if (want > SVG_PX_MAX) want = SVG_PX_MAX;
    NSVGimage *svg = nsvgParseFromFile(file, "px", 96.0f);
    if (!svg) return img;
    float big = svg->width > svg->height ? svg->width : svg->height;
    NSVGrasterizer *r = big > 0 ? nsvgCreateRasterizer() : NULL;
    if (r) {
        float sc = (float)want / big;
        int w = (int)(svg->width * sc + 0.5f), h = (int)(svg->height * sc + 0.5f);
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        memset(pixels, 0, (size_t)w * (size_t)h * 4);
        nsvgRasterize(r, svg, 0, 0, sc, pixels, w, h, w * 4);
        img = (Image){ .data = pixels, .width = w, .height = h, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        nsvgDeleteRasterizer(r);
    }
    nsvgDelete(svg);
    return img;
}

// file into s's texture, px logical pixels on its longer side. A bigger file than drawn (F6.3:
// Yaru's 256 and 256@2x stand in for 64 to 256 px) is made the drawn size once here: the GPU's
// bilinear filter alone blurs a 4x shrink to grains, and holds four times the pixels, or sixteen.
static void icon_tex_load(IconSlot *s, const char *file, int px)
{
    snprintf(s->file, sizeof s->file, "%s", file);
    int want = px * app.icon_scale;
    if (str_ends_with(str_from_c(file), S(".svg"))) {
        Image img = svg_image(file, want);
        if (img.data) s->tex = LoadTextureFromImage(img);   // (the buffer is static: not unloaded)
    } else {
        Image img = LoadImage(file);
        int big = img.width > img.height ? img.width : img.height;
        if (img.data && big > want) ImageResize(&img, img.width * want / big, img.height * want / big);   // the longer side drawn
        if (img.data) s->tex = LoadTextureFromImage(img);
        UnloadImage(img);
    }
    if (s->tex.id) SetTextureFilter(s->tex, TEXTURE_FILTER_BILINEAR);
}

// The texture under key at px, from the first of names the theme has, loaded the first time it is
// needed; NULL when the cache is full. names NULL: they are the MIME type's (key). A size other
// than the list's is cached under "<px>:<key>".
static IconSlot *icon_for_px(Str key, const Str *given, isize ngiven, int px)
{
    int k;
    ArenaMark mark = arena_mark(&scratch);
    Str slot_key = px == ICON_PX ? key : str_from_c(arena_sprintf(&scratch, "%d:%.*s", px, STR_ARG(key)));
    if (strmap_get(&app.slot_of, slot_key, &k)) return arena_restore(&scratch, mark), &app.slot[k];
    if (app.nslots >= app.icons_max) return app.icons_missed = true, arena_restore(&scratch, mark), NULL;
    Str names[MIME_ICON_NAMES];
    isize n = given ? ngiven : mime_icon_names(&scratch, &app.mime_db, key, names);
    IconSlot *s = &app.slot[app.nslots];
    *s = (IconSlot){ .key = { arena_cstr(&icona, slot_key), slot_key.len } };
    const char *file = icon_find(&scratch, &app.icons, given ? given : names, n, px, app.icon_scale);
    if (file) icon_tex_load(s, file, px);
    arena_restore(&scratch, mark);
    strmap_put(&app.slot_of, s->key, app.nslots);
    return &app.slot[app.nslots++];
}

static IconSlot *icon_for(Str key, const Str *given, isize ngiven)
{
    return icon_for_px(key, given, ngiven, ICON_PX);
}

// The icon view's icon size and its cells' (F6.3): as wide as the icon and a little, a cell's width
// at least, so two lines of a name fit under a small one.
static int grid_px(void)
{
    return ui_zoom_px(&app.ui);
}

static int cell_w(void)
{
    int w = grid_px() + CELL_W_ROOM;
    return w > CELL_W_MIN ? w : CELL_W_MIN;
}

static int cell_h(void)
{
    return grid_px() + CELL_H_ROOM;
}

// The size the files' icons are drawn at: the list's, or the icon view's (F6.2).
static int entry_px(void)
{
    return app.ui.grid ? grid_px() : ICON_PX;
}

// The texture for entry i, at the view's size.
static IconSlot *entry_icon(isize i)
{
    const Entry *e = &app.list.e[i];
    int px = entry_px();
    if (!kind_is_dir(e->kind)) return icon_for_px(entry_mime(i), NULL, 0, px);
    Str names[2];
    isize n = 0;
    char path[PATH_MAX];
    Str special = !e->trash && entry_path(i, path, sizeof path) ? user_dir_icon(&app.user_dirs, path) : STR_EMPTY;
    if (special.len) names[n++] = special;
    names[n++] = S("folder");
    return icon_for_px(names[0], names, n, px);
}

// Entry i's preview in the icon view (F6.4), asked for when not known yet; NULL while there is none.
// draw: a cell on screen (else a page ahead).
static Texture2D *entry_thumb(isize i, bool draw)
{
    const Entry *e = &app.list.e[i];
    char path[PATH_MAX];
    if (kind_is_dir(e->kind) || e->kind == K_LINK_BROKEN || e->trash || !entry_path(i, path, sizeof path)) return NULL;
    return thumb_for(path, e->mtime, e->size, thumb_kind(entry_mime(i)), app.thumb_local, grid_px() * app.icon_scale, draw);
}

static IconSlot *place_icon(int i)
{
    Str names[4];
    isize n = place_icon_names(&app.places.p[i], &app.user_dirs, app.trash_full, names);
    return icon_for(names[0], names, n);
}
