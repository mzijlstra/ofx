// The freedesktop icon theme lookup (Icon Theme Specification 0.13), PNG only: raylib loads PNG,
// and Yaru ships every size as PNG (docs/plan.md, "Icons").
//
// At start the theme, its Inherits chain (depth first, as the spec's FindIcon) and hicolor are
// read: each one's index.theme and, per subdirectory and base dir, the PNG names in it (of the
// contexts fx uses). A lookup
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
    if (v.len == 0 || v.len > 9) return dflt;
    int n = 0;
    for (isize i = 0; i < v.len; i++) {
        if (!is_digit(v.p[i])) return dflt;
        n = n * 10 + (v.p[i] - '0');
    }
    return n;
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

// The PNG names in dir into m (sized to them); false when dir isn't there.
static bool icon_scan(Arena *a, const char *dir, StrMap *m)
{
    DIR *d = opendir(dir);
    if (!d) return false;
    isize n = 0, cap = 64;
    Str *names = arena_new(a, Str, cap);
    for (struct dirent *de; (de = readdir(d));) {
        Str s = str_from_c(de->d_name);
        if (!str_ends_with(s, S(".png")) || s.len == 4) continue;
        if (n == cap) {
            Str *grown = arena_new(a, Str, cap * 2);
            memcpy(grown, names, (size_t)n * sizeof *names);
            names = grown, cap *= 2;
        }
        names[n++] = (Str){ arena_cstr(a, str_cut(s, 0, s.len - 4)), s.len - 4 };
    }
    closedir(d);
    strmap_init(m, a, n + 1);
    for (isize i = 0; i < n; i++) strmap_put(m, names[i], 1);
    return true;
}

// fx asks only for folder and file icons, which themes keep under Places and MimeTypes (and
// hicolor's 600-odd app directories would take most of the start-up). A directory that names no
// context is read too.
static bool icon_context_used(Str context)
{
    return context.len == 0 || str_eq(context, S("Places")) || str_eq(context, S("MimeTypes")) ||
           str_eq(context, S("Mimetypes"));
}

static bool icon_has_theme(const IconDb *db, Str name)
{
    for (isize i = 0; i < db->n; i++)
        if (str_eq(db->t[i].name, name)) return true;
    return false;
}

// Reads theme `name` (unless it is in already), then, depth first, every theme it inherits.
static void icon_add_theme(Arena *a, IconDb *db, const char **bases, isize nb, Str name, int depth)
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
            if (size <= 0 || !icon_context_used(ini_get(index, sub, S("Context")))) continue;
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
                if (icon_scan(a, d.path, &d.names)) t->dirs[t->ndirs++] = d;
            }
        }
    }
    Str inherits = ini_get(index, S("Icon Theme"), S("Inherits")), parent;
    for (isize at = 0; next_item(inherits, &at, &parent);) icon_add_theme(a, db, bases, nb, parent, depth + 1);
}

static void icon_load(Arena *a, IconDb *db, Str theme)
{
    *db = (IconDb){ 0 };
    const char *bases[ICON_BASES_MAX];
    isize nb = icon_bases(a, bases, ICON_BASES_MAX);
    if (theme.len) icon_add_theme(a, db, bases, nb, theme, 0);
    icon_add_theme(a, db, bases, nb, S("hicolor"), 0);
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

// The spec's LookupIcon in one theme: a directory that matches the size and scale, else the
// closest one. NULL when the theme has no such icon.
static const IconDir *icon_in_theme(const IconTheme *t, Str name, int size, int scale)
{
    int have;
    for (isize i = 0; i < t->ndirs; i++)
        if (icon_dir_matches(&t->dirs[i], size, scale) && strmap_get(&t->dirs[i].names, name, &have)) return &t->dirs[i];
    const IconDir *best = NULL;
    int best_d = 0;
    for (isize i = 0; i < t->ndirs; i++) {
        if (!strmap_get(&t->dirs[i].names, name, &have)) continue;
        int dist = icon_dir_distance(&t->dirs[i], size, scale);
        if (!best || dist < best_d) best = &t->dirs[i], best_d = dist;
    }
    return best;
}

// The file for the first of names any theme has, theme by theme (the spec's FindBestIcon), at size
// logical pixels on a scale-`scale` output; into a. NULL when there is none.
static const char *icon_find(Arena *a, const IconDb *db, const Str *names, isize nnames, int size, int scale)
{
    for (isize t = 0; t < db->n; t++)
        for (isize k = 0; k < nnames; k++) {
            const IconDir *d = icon_in_theme(&db->t[t], names[k], size, scale);
            if (d) return arena_sprintf(a, "%s/%.*s.png", d->path, STR_ARG(names[k]));
        }
    return NULL;
}
