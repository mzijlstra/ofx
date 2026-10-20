// The Omarchy theme: colours from colors.toml and the icon theme's name from icons.theme, both in
// $XDG_STATE_HOME/omarchy/current/theme (~/.local/state by default). A missing file or key keeps
// Tokyo Night's value (docs/plan.md, "Theme"). Also the XDG user directories, for their icons.

#include "../base/base.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct { u8 r, g, b; } Rgb;

typedef struct {
    Rgb  bg, bg_light, fg, fg_dim, accent, selection, red;
    char icons[128];   // the icon theme's name, "" without one
    int  ncolors;      // how many keys colors.toml gave: 0 means all fallbacks
} Theme;

static const Theme THEME_DEFAULT = {
    .bg = { 0x1a, 0x1b, 0x26 },
    .bg_light = { 0x24, 0x28, 0x3b },
    .fg = { 0xa9, 0xb1, 0xd6 },
    .fg_dim = { 0x56, 0x5f, 0x89 },
    .accent = { 0x7a, 0xa2, 0xf7 },
    .selection = { 0x29, 0x2e, 0x42 },
    .red = { 0xf7, 0x76, 0x8e },
};

// The theme directory, into buf.
static const char *theme_dir(char *buf, size_t cap)
{
    const char *state = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
    if (state && state[0]) snprintf(buf, cap, "%s/omarchy/current/theme", state);
    else snprintf(buf, cap, "%s/.local/state/omarchy/current/theme", home ? home : "");
    return buf;
}

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// "#rrggbb", quoted or not.
static bool parse_rgb(Str v, Rgb *out)
{
    if (v.len >= 2 && (v.p[0] == '"' || v.p[0] == '\'') && v.p[v.len - 1] == v.p[0]) v = str_cut(v, 1, v.len - 1);
    if (v.len != 7 || v.p[0] != '#') return false;
    int d[6];
    for (int i = 0; i < 6; i++)
        if ((d[i] = hex_digit(v.p[1 + i])) < 0) return false;
    *out = (Rgb){ (u8)(d[0] * 16 + d[1]), (u8)(d[2] * 16 + d[3]), (u8)(d[4] * 16 + d[5]) };
    return true;
}

// Reads colors.toml's top-level `key = "#rrggbb"` lines (a [table] ends them; # starts a comment).
static void theme_parse_colors(Theme *t, Str text)
{
    static const struct { const char *key; size_t off; } KEYS[] = {
        { "background", offsetof(Theme, bg) },
        { "lighter_background", offsetof(Theme, bg_light) },
        { "foreground", offsetof(Theme, fg) },
        { "dark_foreground", offsetof(Theme, fg_dim) },
        { "accent", offsetof(Theme, accent) },
        { "selection", offsetof(Theme, selection) },
        { "red", offsetof(Theme, red) },
    };
    isize at = 0;
    for (Str line; next_line(text, &at, &line);) {
        line = str_trim(line);
        if (line.len && line.p[0] == '[') break;
        isize hash = str_find(line, '#'), eq = str_find(line, '=');
        if (eq <= 0 || (hash >= 0 && hash < eq)) continue;
        Str key = str_trim(str_cut(line, 0, eq)), val = str_trim(str_cut(line, eq + 1, line.len));
        // A comment after the value: the colour's own '#' is inside its quotes.
        if (val.len && (val.p[0] == '"' || val.p[0] == '\'')) {
            isize close = str_find(str_cut(val, 1, val.len), val.p[0]);
            if (close >= 0) val = str_cut(val, 0, close + 2);
        }
        for (int k = 0; k < COUNT(KEYS); k++) {
            Rgb c;
            if (str_eq(key, str_from_c(KEYS[k].key)) && parse_rgb(val, &c)) {
                *(Rgb *)((char *)t + KEYS[k].off) = c;
                t->ncolors++;
            }
        }
    }
}

// The current theme; scratch holds only the files' text.
static Theme theme_load(Arena *scratch)
{
    Theme t = THEME_DEFAULT;
    char dir[4096];
    theme_dir(dir, sizeof dir);
    ArenaMark mark = arena_mark(scratch);
    theme_parse_colors(&t, read_file(scratch, arena_sprintf(scratch, "%s/colors.toml", dir)));
    Str icons = str_trim(read_file(scratch, arena_sprintf(scratch, "%s/icons.theme", dir)));
    if (icons.len < (isize)sizeof t.icons && str_find(icons, '/') < 0 && str_find(icons, '\n') < 0)
        snprintf(t.icons, sizeof t.icons, "%.*s", STR_ARG(icons));
    arena_restore(scratch, mark);
    return t;
}

// ---- XDG user directories -------------------------------------------------------------------

typedef struct {
    char path[4096];
    Str  icon;
} UserDir;

typedef struct {
    UserDir d[12];
    int     n;
} UserDirs;

// $HOME and the folders in ~/.config/user-dirs.dirs (XDG_DOWNLOAD_DIR="$HOME/Downloads/"), with
// the icon each gets. A key pointing at $HOME itself (Michael's TEMPLATES, PUBLICSHARE) is skipped,
// so home keeps user-home.
static void user_dirs_load(Arena *scratch, UserDirs *u)
{
    static const struct { const char *key; const char *icon; } KEYS[] = {
        { "XDG_DESKTOP_DIR", "user-desktop" },       { "XDG_DOWNLOAD_DIR", "folder-download" },
        { "XDG_TEMPLATES_DIR", "folder-templates" }, { "XDG_PUBLICSHARE_DIR", "folder-publicshare" },
        { "XDG_DOCUMENTS_DIR", "folder-documents" }, { "XDG_MUSIC_DIR", "folder-music" },
        { "XDG_PICTURES_DIR", "folder-pictures" },   { "XDG_VIDEOS_DIR", "folder-videos" },
    };
    *u = (UserDirs){ 0 };
    const char *home = getenv("HOME"), *cfg = getenv("XDG_CONFIG_HOME");
    if (!home || !home[0]) return;
    snprintf(u->d[0].path, sizeof u->d[0].path, "%s", home);
    u->d[0].icon = S("user-home");
    u->n = 1;
    ArenaMark mark = arena_mark(scratch);
    Str text = read_file(scratch, cfg && cfg[0] ? arena_sprintf(scratch, "%s/user-dirs.dirs", cfg)
                                                : arena_sprintf(scratch, "%s/.config/user-dirs.dirs", home));
    isize at = 0;
    for (Str line; next_line(text, &at, &line);) {
        isize eq = str_find(line, '=');
        if (eq <= 0 || line.p[0] == '#') continue;
        Str key = str_trim(str_cut(line, 0, eq)), val = str_trim(str_cut(line, eq + 1, line.len));
        if (val.len >= 2 && val.p[0] == '"' && val.p[val.len - 1] == '"') val = str_cut(val, 1, val.len - 1);
        while (val.len > 1 && val.p[val.len - 1] == '/') val.len--;
        char path[4096];
        if (str_starts_with(val, S("$HOME"))) snprintf(path, sizeof path, "%s%.*s", home, STR_ARG(str_cut(val, 5, val.len)));
        else if (val.len && val.p[0] == '/') snprintf(path, sizeof path, "%.*s", STR_ARG(val));
        else continue;
        if (strcmp(path, home) == 0 || u->n == COUNT(u->d)) continue;
        for (int k = 0; k < COUNT(KEYS); k++)
            if (str_eq(key, str_from_c(KEYS[k].key))) {
                snprintf(u->d[u->n].path, sizeof u->d[u->n].path, "%s", path);
                u->d[u->n++].icon = str_from_c(KEYS[k].icon);
            }
    }
    arena_restore(scratch, mark);
}

// The icon a folder at path gets, STR_EMPTY for a plain one.
static Str user_dir_icon(const UserDirs *u, const char *path)
{
    for (int i = 0; i < u->n; i++)
        if (strcmp(u->d[i].path, path) == 0) return u->d[i].icon;
    return STR_EMPTY;
}
