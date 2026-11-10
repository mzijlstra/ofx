// The freedesktop icon theme lookup (Icon Theme Specification 0.13), PNG only: raylib loads PNG,
// and Yaru ships every size as PNG (docs/plan.md, "Icons").
//
// At start the theme, its Inherits chain (depth first, as the spec's FindIcon) and hicolor are
// read: each one's index.theme and, per subdirectory and base dir, the PNG names in it (of the
// contexts ofx uses). A lookup
// is then map probes, no file system calls. Base dirs: ~/.icons, then icons/ under every XDG data
// dir.

#include "../base/base.h"

#include <dirent.h>
#include <stdlib.h>

enum { ICON_THEMES_MAX = 16, ICON_DIRS_MAX = 1024, ICON_BASES_MAX = 16 };

typedef enum { IT_THRESHOLD, IT_FIXED, IT_SCALABLE } IconType;

typedef struct {
    const char *path;   // the directory, absolute
    int         size, scale, min, max, threshold;
    IconType    type;
    StrMap      names;  // the PNGs in it, without ".png"
} IconDir;

typedef struct {
    Str      name;
    IconDir *dirs;
    isize    ndirs;
} IconTheme;

typedef struct {
    IconTheme t[ICON_THEMES_MAX];   // in lookup order: the theme, what it inherits, hicolor
    isize     n;
} IconDb;

// ---- index.theme ----------------------------------------------------------------------------

// The value of key in [section] of an ini text, or STR_EMPTY.
static Str ini_get(Str text, Str section, Str key)
{
    isize at = 0;
    bool in = false;
    for (Str line; next_line(text, &at, &line);) {
        line = str_trim(line);
        if (line.len && line.p[0] == '[') {
            in = line.len >= 2 && line.p[line.len - 1] == ']' && str_eq(str_cut(line, 1, line.len - 1), section);
            continue;
        }
        if (!in) continue;
        isize eq = str_find(line, '=');
        if (eq > 0 && str_eq(str_trim(str_cut(line, 0, eq)), key)) return str_trim(str_cut(line, eq + 1, line.len));
    }
    return STR_EMPTY;
}

static int ini_int(Str text, Str section, Str key, int dflt)
{
    Str v = ini_get(text, section, key);
    i64 n = v.len > 9 ? -1 : str_num(v, 999999999);   // (nine digits at most, leading zeros counted)
    return n < 0 ? dflt : (int)n;
}

// The next comma-separated item of list from *at, trimmed.
static bool next_item(Str list, isize *at, Str *item)
{
    while (*at < list.len) {
        isize e = *at;
        while (e < list.len && list.p[e] != ',') e++;
        *item = str_trim(str_cut(list, *at, e));
        *at = e + 1;
        if (item->len) return true;
    }
    return false;
}

static isize icon_bases(Arena *a, const char **out, isize max)
{
    isize n = 0;
    const char *home = getenv("HOME");
    if (home && home[0]) out[n++] = arena_sprintf(a, "%s/.icons", home);
    const char *dirs[MIME_DIRS_MAX];
    isize nd = data_dirs(a, dirs, MIME_DIRS_MAX);
    for (isize k = 0; k < nd && n < max; k++) out[n++] = arena_sprintf(a, "%s/icons", dirs[k]);
    return n;
}

// The PNG names in dir into m (sized to them), and with svg the SVG ones too (a name with both:
// the PNG); the value is 1 for a PNG, 2 for an SVG. A symbolic SVG (name-symbolic.svg) is taken
// always: Yaru and Adwaita have Airplane Mode's icon only so (F16 step 7d; about 2,000 names in
// Yaru and hicolor, ~200 KiB, and only a name ending -symbolic, which only that row asks for, finds
// one). False when dir isn't there.
static bool icon_scan(Arena *a, const char *dir, StrMap *m, bool svg)
{
    DIR *d = opendir(dir);
    if (!d) return false;
    isize n = 0, cap = 64;
    Str *names = arena_new(a, Str, cap);
    u8 *kind = arena_new(a, u8, cap);
    for (struct dirent *de; (de = readdir(d));) {
        Str s = str_from_c(de->d_name);
        bool png = str_ends_with(s, S(".png")), is_svg = (svg || str_ends_with(s, S("-symbolic.svg"))) && str_ends_with(s, S(".svg"));
        if ((!png && !is_svg) || s.len == 4) continue;
        if (n == cap) {
            Str *grown = arena_new(a, Str, cap * 2);
            u8 *kgrown = arena_new(a, u8, cap * 2);
            memcpy(grown, names, (size_t)n * sizeof *names), memcpy(kgrown, kind, (size_t)n);
            names = grown, kind = kgrown, cap *= 2;
        }
        kind[n] = png ? 1 : 2;
        names[n++] = (Str){ arena_cstr(a, str_cut(s, 0, s.len - 4)), s.len - 4 };
    }
    closedir(d);
    strmap_init(m, a, n + 1);
    for (isize i = 0; i < n; i++) {
        int had;
        if (!strmap_get(m, names[i], &had) || kind[i] < had) strmap_put(m, names[i], kind[i]);
    }
    return true;
}

// ofx asks only for folder, drive and file icons, which themes keep under Places, Devices and
// MimeTypes, the top bar's search (edit-find, under Actions), and the full trash (user-trash-full,
// under Status as the spec has it: 54 folders more in Yaru, 3 ms and 99 KiB, 2026-10-30); hicolor's
// 600-odd app directories would take most of the start-up. A directory that names no context is read too.
// The apps' own (Apps) are read apart, when Open With opens (icon_load_apps, Michael 2026-10-26).
static bool icon_context_used(Str context, bool apps)
{
    if (apps) return str_eq(context, S("Apps")) || str_eq(context, S("Applications"));
    return context.len == 0 || str_eq(context, S("Places")) || str_eq(context, S("MimeTypes")) ||
           str_eq(context, S("Mimetypes")) || str_eq(context, S("Devices")) || str_eq(context, S("Actions")) || str_eq(context, S("Status"));
}

static bool icon_has_theme(const IconDb *db, Str name)
{
    for (isize i = 0; i < db->n; i++)
        if (str_eq(db->t[i].name, name)) return true;
    return false;
}

// Reads theme `name` (unless it is in already), then, depth first, every theme it inherits.
static void icon_add_theme(Arena *a, IconDb *db, const char **bases, isize nb, Str name, int depth, bool apps)
{
    if (depth > 8 || db->n == ICON_THEMES_MAX || icon_has_theme(db, name)) return;
    Str index = STR_EMPTY;
    for (isize b = 0; b < nb && index.len == 0; b++)
        index = read_file(a, arena_sprintf(a, "%s/%.*s/index.theme", bases[b], STR_ARG(name)));
    if (index.len == 0) return;
    IconTheme *t = &db->t[db->n++];
    *t = (IconTheme){ .name = name, .dirs = arena_new(a, IconDir, ICON_DIRS_MAX) };
    Str subs[2] = { ini_get(index, S("Icon Theme"), S("Directories")), ini_get(index, S("Icon Theme"), S("ScaledDirectories")) };
    for (int list = 0; list < 2; list++) {
        Str sub;
        for (isize at = 0; next_item(subs[list], &at, &sub);) {
            int size = ini_int(index, sub, S("Size"), 0);
            if (size <= 0 || !icon_context_used(ini_get(index, sub, S("Context")), apps)) continue;
            Str type = ini_get(index, sub, S("Type"));
            IconDir proto = {
                .size = size,
                .scale = ini_int(index, sub, S("Scale"), 1),
                .min = ini_int(index, sub, S("MinSize"), size),
                .max = ini_int(index, sub, S("MaxSize"), size),
                .threshold = ini_int(index, sub, S("Threshold"), 2),
                .type = str_eq(type, S("Fixed")) ? IT_FIXED : str_eq(type, S("Scalable")) ? IT_SCALABLE : IT_THRESHOLD,
            };
            for (isize b = 0; b < nb && t->ndirs < ICON_DIRS_MAX; b++) {
                IconDir d = proto;
                d.path = arena_sprintf(a, "%s/%.*s/%.*s", bases[b], STR_ARG(name), STR_ARG(sub));
                if (icon_scan(a, d.path, &d.names, apps)) t->dirs[t->ndirs++] = d;
            }
        }
    }
    Str inherits = ini_get(index, S("Icon Theme"), S("Inherits")), parent;
    for (isize at = 0; next_item(inherits, &at, &parent);) icon_add_theme(a, db, bases, nb, parent, depth + 1, apps);
}

// The theme named and what it inherits, then hicolor. A theme that isn't installed gives way to its
// name cut at the last '-', again until one is (Yaru-grey, Yaru-sage-dark: Yaru, Yaru-sage):
// Omarchy's vantablack and white themes name Yaru-gray and
// Yaru-grey, which no package ships (omacom/omarchy#14851), and hicolor alone has no file or
// folder icons. Then plain Yaru (Omarchy installs it), then Adwaita, GTK's own fallback, last:
// GTK draws its SVGs, ofx only PNGs, and Adwaita has a few dozen PNGs, most of them 16 px. No
// name (no icons.theme) takes the same way.
static void icon_load_ctx(Arena *a, IconDb *db, Str theme, bool apps)
{
    *db = (IconDb){ 0 };
    const char *bases[ICON_BASES_MAX];
    isize nb = icon_bases(a, bases, ICON_BASES_MAX);
    for (Str name = theme; name.len && db->n == 0;) {
        icon_add_theme(a, db, bases, nb, name, 0, apps);
        isize dash = name.len;
        while (dash > 0 && name.p[dash - 1] != '-') dash--;
        name.len = dash > 1 ? dash - 1 : 0;
    }
    if (db->n == 0) icon_add_theme(a, db, bases, nb, S("Yaru"), 0, apps);
    if (db->n == 0) icon_add_theme(a, db, bases, nb, S("Adwaita"), 0, apps);
    icon_add_theme(a, db, bases, nb, S("hicolor"), 0, apps);
}

static void icon_load(Arena *a, IconDb *db, Str theme)
{
    icon_load_ctx(a, db, theme, false);
}

// The apps' icons of the same chain, SVG ones too (Open With, F7.3): read when the panel opens,
// into its own arena, and dropped with it.
static void icon_load_apps(Arena *a, IconDb *db, Str theme)
{
    icon_load_ctx(a, db, theme, true);
}

// ---- lookup ---------------------------------------------------------------------------------

// The spec's DirectoryMatchesSize and DirectorySizeDistance.
static bool icon_dir_matches(const IconDir *d, int size, int scale)
{
    if (d->scale != scale) return false;
    switch (d->type) {
    case IT_FIXED: return d->size == size;
    case IT_SCALABLE: return d->min <= size && size <= d->max;
    default: return d->size - d->threshold <= size && size <= d->size + d->threshold;
    }
}

static int icon_dir_distance(const IconDir *d, int size, int scale)
{
    int want = size * scale, lo, hi;
    switch (d->type) {
    case IT_FIXED: lo = hi = d->size * d->scale; break;
    case IT_SCALABLE: lo = d->min * d->scale, hi = d->max * d->scale; break;
    default: lo = (d->size - d->threshold) * d->scale, hi = (d->size + d->threshold) * d->scale; break;
    }
    return want < lo ? lo - want : want > hi ? want - hi : 0;
}

// Whether d's icons are smaller than size at scale, in device pixels: drawn there they would be
// scaled up.
static bool icon_dir_short(const IconDir *d, int size, int scale)
{
    int hi = d->type == IT_FIXED ? d->size : d->type == IT_SCALABLE ? d->max : d->size + d->threshold;
    return hi * d->scale < size * scale;
}

// The spec's LookupIcon in one theme: a directory that matches the size and scale, else the
// closest one, but one at least as big before any smaller (F6.3: 96 px on a scale-2 output is
// Yaru's 256 scaled down, not its 48@2x scaled up and blurred). NULL when the theme has no such icon.
static const IconDir *icon_in_theme(const IconTheme *t, Str name, int size, int scale)
{
    int have;
    for (isize i = 0; i < t->ndirs; i++)
        if (icon_dir_matches(&t->dirs[i], size, scale) && strmap_get(&t->dirs[i].names, name, &have)) return &t->dirs[i];
    const IconDir *best = NULL;
    int best_d = 0;
    bool best_short = false;
    for (isize i = 0; i < t->ndirs; i++) {
        if (!strmap_get(&t->dirs[i].names, name, &have)) continue;
        int dist = icon_dir_distance(&t->dirs[i], size, scale);
        bool short_ = icon_dir_short(&t->dirs[i], size, scale);
        if (!best || (best_short && !short_) || (best_short == short_ && dist < best_d)) best = &t->dirs[i], best_d = dist, best_short = short_;
    }
    return best;
}

// The file for the first of names any theme in the chain has, at size logical pixels on a
// scale-`scale` output; into a. NULL when there is none. Name by name, not the spec's FindBestIcon
// (theme by theme): ofx's lists end in generic fallbacks (folder, text-x-generic), and Yaru-magenta
// has a folder but no drive, so theme-first drew the drives as folders (2026-10-20).
static const char *icon_find(Arena *a, const IconDb *db, const Str *names, isize nnames, int size, int scale)
{
    for (isize k = 0; k < nnames; k++)
        for (isize t = 0; t < db->n; t++) {
            const IconDir *d = icon_in_theme(&db->t[t], names[k], size, scale);
            int kind = 1;
            if (d) strmap_get(&d->names, names[k], &kind);
            if (d) return arena_sprintf(a, "%s/%.*s.%s", d->path, STR_ARG(names[k]), kind == 2 ? "svg" : "png");
        }
    return NULL;
}
