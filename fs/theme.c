// The Omarchy theme: colours from colors.toml and the icon theme's name from icons.theme, both in
// $XDG_STATE_HOME/omarchy/current/theme (~/.local/state by default). A missing file or key keeps
// Tokyo Night's value (docs/plan.md, "Theme"). Also the XDG user directories, for their icons.

#include "../base/base.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { u8 r, g, b; } Rgb;

typedef struct {
    Rgb  bg, bg_light, fg, fg_dim, accent, selection, red;
    Rgb  yellow, green, blue, cyan, magenta;   // the terminal's, as eza colours ls with them (a trial, 2026-10-27)
    Rgb  orange, bright_red, bright_blue, bright_cyan;   // read for theme_fit's picks only, where `have` says
    Rgb  drive_used, drive_warn, drive_free, drive_track;   // theme_fit's: a drive's bar, its free number, the bar's track
    u32  have;         // which of THEME_KEYS colors.toml gave, bit k for key k
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
    .yellow = { 0xe0, 0xaf, 0x68 },
    .green = { 0x9e, 0xce, 0x6a },
    .blue = { 0x7a, 0xa2, 0xf7 },
    .cyan = { 0x7d, 0xcf, 0xff },
    .magenta = { 0xbb, 0x9a, 0xf7 },
};

// The theme directory, into buf.
static const char *theme_dir(char *buf, size_t cap)
{
    if (!xdg_dir(buf, cap, "XDG_STATE_HOME", ".local/state", "omarchy/current/theme")) buf[0] = 0;
    return buf;
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

// colors.toml's keys ofx reads, and where each goes.
enum { TK_ORANGE = 12, TK_BRIGHT_RED, TK_BRIGHT_BLUE, TK_BRIGHT_CYAN };
static const struct { const char *key; size_t off; } THEME_KEYS[] = {
    { "background", offsetof(Theme, bg) },
    { "lighter_background", offsetof(Theme, bg_light) },
    { "foreground", offsetof(Theme, fg) },
    { "dark_foreground", offsetof(Theme, fg_dim) },
    { "accent", offsetof(Theme, accent) },
    { "selection", offsetof(Theme, selection) },
    { "red", offsetof(Theme, red) },
    { "yellow", offsetof(Theme, yellow) },
    { "green", offsetof(Theme, green) },
    { "blue", offsetof(Theme, blue) },
    { "cyan", offsetof(Theme, cyan) },
    { "magenta", offsetof(Theme, magenta) },
    { "orange", offsetof(Theme, orange) },   // TK_ORANGE
    { "bright_red", offsetof(Theme, bright_red) },
    { "bright_blue", offsetof(Theme, bright_blue) },
    { "bright_cyan", offsetof(Theme, bright_cyan) },
};

// Reads colors.toml's top-level `key = "#rrggbb"` lines (a [table] ends them; # starts a comment).
static void theme_parse_colors(Theme *t, Str text)
{
    isize at = 0;
    for (Str line; next_line(text, &at, &line);) {
        line = str_trim(line);
        if (line.len && line.p[0] == '[') break;
        // A comment line's key starts with '#', so it never names a colour.
        isize eq = str_find(line, '=');
        if (eq <= 0) continue;
        Str key = str_trim(str_cut(line, 0, eq)), val = str_trim(str_cut(line, eq + 1, line.len));
        // A comment after the value: the colour's own '#' is inside its quotes.
        if (val.len && (val.p[0] == '"' || val.p[0] == '\'')) {
            isize close = str_find(str_cut(val, 1, val.len), val.p[0]);
            if (close >= 0) val = str_cut(val, 0, close + 2);
        }
        for (int k = 0; k < COUNT(THEME_KEYS); k++) {
            Rgb c;
            if (str_eq(key, str_from_c(THEME_KEYS[k].key)) && parse_rgb(val, &c)) {
                *(Rgb *)((char *)t + THEME_KEYS[k].off) = c;
                t->have |= 1u << k;
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

// ---- the colours ofx draws in, fitted to the theme (2026-11-07) ------------------------------
// Omarchy's themes don't promise that their colours read on their own backgrounds: on 4.5,
// dark_foreground (ofx's dimmed text) reads under 3:1 in 16 of 23 themes, and is the selection's
// own colour in some, and the terminal colours under 3:1 in up to 11. No fixed colour reads on all
// of them (docs/plan.md, "Theme colours across Omarchy's themes"). So each colour ofx draws text in
// is pushed toward the foreground just until it reads on every background it sits on, and left as
// the theme gave it where it already does: dimmed text to 2:1 (muted, never illegible: Michael,
// 2026-11-07), coloured text to 3:1. Contrast is WCAG's ratio; likeness CIE76's dE in Lab.

#define THEME_DIM_MIN    2.0
#define THEME_TEXT_MIN   3.0
#define THEME_UNLIKE_MIN 20.0   // dE: ristretto's accent and red are 29 apart, and read apart (Michael, 2026-11-07)

static double rgb_lin(u8 c)
{
    double v = c / 255.0;
    return v <= 0.03928 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

static double rgb_lum(Rgb c)
{
    return 0.2126 * rgb_lin(c.r) + 0.7152 * rgb_lin(c.g) + 0.0722 * rgb_lin(c.b);
}

// WCAG's contrast ratio, 1 to 21.
static double rgb_contrast(Rgb a, Rgb b)
{
    double la = rgb_lum(a), lb = rgb_lum(b);
    return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

// a mixed into b: t of a, as the window's mix.
static Rgb rgb_mix(Rgb a, Rgb b, double t)
{
    return (Rgb){ (u8)lround(a.r * t + b.r * (1 - t)), (u8)lround(a.g * t + b.g * (1 - t)), (u8)lround(a.b * t + b.b * (1 - t)) };
}

static void rgb_lab(Rgb c, double lab[3])
{
    double r = rgb_lin(c.r), g = rgb_lin(c.g), b = rgb_lin(c.b);
    double xyz[3] = { (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047, 0.2126 * r + 0.7152 * g + 0.0722 * b,
                      (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883 };
    double f[3];
    for (int i = 0; i < 3; i++) f[i] = xyz[i] > 0.008856 ? cbrt(xyz[i]) : 7.787 * xyz[i] + 16.0 / 116;
    lab[0] = 116 * f[1] - 16, lab[1] = 500 * (f[0] - f[1]), lab[2] = 200 * (f[1] - f[2]);
}

// How unlike two colours look (CIE76): under THEME_UNLIKE_MIN a warning beside the other doesn't stand out.
static double rgb_delta(Rgb a, Rgb b)
{
    double x[3], y[3];
    rgb_lab(a, x), rgb_lab(b, y);
    return sqrt((x[0] - y[0]) * (x[0] - y[0]) + (x[1] - y[1]) * (x[1] - y[1]) + (x[2] - y[2]) * (x[2] - y[2]));
}

// How coloured a colour is (Lab chroma): a grey is about 0.
static double rgb_chroma(Rgb c)
{
    double l[3];
    rgb_lab(c, l);
    return sqrt(l[1] * l[1] + l[2] * l[2]);
}

// The worst contrast c has over n backgrounds.
static double rgb_worst(Rgb c, const Rgb *bg, int n)
{
    double w = 21;
    for (int i = 0; i < n; i++) w = fmin(w, rgb_contrast(c, bg[i]));
    return w;
}

// OKLab (Björn Ottosson, 2020): lightness, and two colour axes, perceptually even. OKLCH is its
// polar form: L, chroma C, hue h. Moving L alone keeps a colour's hue and as much of its chroma as
// the sRGB gamut allows, where a mix toward fg (white or black) washes it out (Michael, 2026-11-07:
// the mix made the files area's colours "same-same").
static void rgb_oklab(Rgb c, double o[3])
{
    double r = rgb_lin(c.r), g = rgb_lin(c.g), b = rgb_lin(c.b);
    double l = cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    double m = cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    double s = cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    o[0] = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    o[1] = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    o[2] = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
}

static double lin_to_srgb(double v)
{
    return v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1 / 2.4) - 0.055;
}

// OKLab to sRGB, false where the colour is outside sRGB.
static bool oklab_rgb(const double o[3], Rgb *out)
{
    double l = o[0] + 0.3963377774 * o[1] + 0.2158037573 * o[2];
    double m = o[0] - 0.1055613458 * o[1] - 0.0638541728 * o[2];
    double s = o[0] - 0.0894841775 * o[1] - 1.2914855480 * o[2];
    l = l * l * l, m = m * m * m, s = s * s * s;
    double rgb[3] = { 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s, -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                      -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s };
    u8 v[3];
    for (int i = 0; i < 3; i++) {
        double e = lin_to_srgb(rgb[i]);
        if (e < -0.0005 || e > 1.0005) return false;
        v[i] = (u8)lround(fmin(1, fmax(0, e)) * 255);
    }
    *out = (Rgb){ v[0], v[1], v[2] };
    return true;
}

// The colour at lightness L with c's hue and as much of its chroma as fits in sRGB.
static Rgb oklch_at(const double c[3], double L)
{
    double lo = 0, hi = 1, o[3];
    Rgb x;
    o[0] = L, o[1] = c[1], o[2] = c[2];
    if (oklab_rgb(o, &x)) return x;
    for (int i = 0; i < 20; i++) {   // the most chroma that fits, by halving
        double k = (lo + hi) / 2;
        o[1] = c[1] * k, o[2] = c[2] * k;
        if (oklab_rgb(o, &x)) lo = k;
        else hi = k;
    }
    o[1] = c[1] * lo, o[2] = c[2] * lo;
    oklab_rgb(o, &x);
    return x;
}

// c's lightness moved toward `to`'s in fortieths, hue kept, just until it reads `min` over every
// background; `to` itself if nothing short of it does. marked: the list's marked rows are a
// background too, the colour's own accent 28% into bg (gui/files.c), so the accent moves its own
// background as it moves.
static Rgb rgb_push(Rgb c, Rgb to, const Rgb *bg, int n, double min, const Rgb *marked_bg)
{
    double o[3], ot[3];
    rgb_oklab(c, o), rgb_oklab(to, ot);
    for (int k = 0; k <= 40; k++) {
        Rgb x = k == 0 ? c : oklch_at(o, o[0] + (ot[0] - o[0]) * k / 40.0);
        if (rgb_worst(x, bg, n) >= min && (!marked_bg || rgb_contrast(x, rgb_mix(x, *marked_bg, 0.28)) >= min)) return x;
    }
    return to;
}

static bool theme_has(const Theme *t, int k)
{
    return t->have >> k & 1;
}

// The theme's colours fitted for drawing, in place (above), and the drive bar's three:
// - the warning (a drive 90% full): the theme's red where it reads, else the best-reading of red,
//   bright_red and orange that is a colour (solitude's red is a grey), pushed to 3:1; in a theme
//   without a colour among them (vantablack's and white's are greys) the foreground, so it differs
//   by lightness and nothing foreign comes in;
// - the used part: the accent, or where that looks like the warning (dE under THEME_UNLIKE_MIN), the theme's
//   best-reading colour that doesn't, pushed to 3:1;
// - the free number beside the name: dimmer than dark_foreground, at 2:1;
// - the bar's track: that mix too, of the fitted dark_foreground, but not pushed. It isn't text,
//   and pushed toward fg it met the used part, flat in the grey themes (white's 2.80 to 1.51:
//   theme-fit review, 2026-11-07).
// The sidebar's backgrounds: a row, the hovered row, the cursor's.
static void theme_fit(Theme *t)
{
    const Rgb side[] = { t->bg_light, t->bg, t->selection };
    const Rgb *warm[3];
    int nw = 0;
    warm[nw++] = &t->red;
    if (theme_has(t, TK_BRIGHT_RED)) warm[nw++] = &t->bright_red;
    if (theme_has(t, TK_ORANGE)) warm[nw++] = &t->orange;
    const Rgb *best = NULL;   // the theme's red where it reads and is a colour, else the best-reading colour
    for (int i = 0; i < nw; i++)
        if (rgb_chroma(*warm[i]) >= 20 && (!best || rgb_worst(*warm[i], side, 3) > rgb_worst(*best, side, 3))) best = warm[i];
    if (rgb_chroma(t->red) >= 20 && rgb_worst(t->red, side, 3) >= THEME_TEXT_MIN) best = &t->red;
    t->drive_warn = !best ? t->fg : rgb_push(*best, t->fg, side, 3, THEME_TEXT_MIN, NULL);
    const Rgb *used = &t->accent;
    if (rgb_delta(t->accent, t->drive_warn) < THEME_UNLIKE_MIN) {
        const Rgb *other[] = { &t->blue, &t->cyan, &t->green, &t->magenta, &t->yellow,
                               theme_has(t, TK_BRIGHT_BLUE) ? &t->bright_blue : NULL, theme_has(t, TK_BRIGHT_CYAN) ? &t->bright_cyan : NULL };
        double top = -1;   // unlike the warning first, then the best-reading
        for (int i = 0; i < COUNT(other); i++) {
            double score = !other[i] ? -1 : (rgb_delta(*other[i], t->drive_warn) >= THEME_UNLIKE_MIN) * 100 + rgb_worst(*other[i], side, 3);
            if (score > top) top = score, used = other[i];
        }
    }
    t->drive_used = rgb_push(*used, t->fg, side, 3, THEME_TEXT_MIN, NULL);

    // The text colours, over the list's backgrounds: a row, the hovered row, the cursor's, a marked
    // row (the accent first, as the others are read on its marked rows).
    t->accent = rgb_push(t->accent, t->fg, (Rgb[]){ t->bg, t->bg_light, t->selection }, 3, THEME_TEXT_MIN, &t->bg);
    const Rgb list[] = { t->bg, t->bg_light, t->selection, rgb_mix(t->accent, t->bg, 0.28) };
    t->fg_dim = rgb_push(t->fg_dim, t->fg, list, 4, THEME_DIM_MIN, NULL);
    Rgb *text[] = { &t->red, &t->yellow, &t->green, &t->blue, &t->cyan, &t->magenta };
    for (int i = 0; i < COUNT(text); i++) *text[i] = rgb_push(*text[i], t->fg, list, 4, THEME_TEXT_MIN, NULL);
    t->drive_track = rgb_mix(t->fg_dim, t->bg_light, 0.75);
    t->drive_free = rgb_push(t->drive_track, t->fg, side, 3, THEME_DIM_MIN, NULL);
}

// The files area's colours (2026-11-07, a prototype): only rescued where they are near illegible,
// under THEME_RESCUE_MIN, so the theme's own colours stay as vibrant as its author made them there.
#define THEME_RESCUE_MIN 1.5
static void theme_fit_list(Theme *t)
{
    t->accent = rgb_push(t->accent, t->fg, (Rgb[]){ t->bg, t->bg_light, t->selection }, 3, THEME_RESCUE_MIN, &t->bg);
    const Rgb list[] = { t->bg, t->bg_light, t->selection, rgb_mix(t->accent, t->bg, 0.28) };
    Rgb *text[] = { &t->fg_dim, &t->red, &t->yellow, &t->green, &t->blue, &t->cyan, &t->magenta };
    for (int i = 0; i < COUNT(text); i++) *text[i] = rgb_push(*text[i], t->fg, list, 4, THEME_RESCUE_MIN, NULL);
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
    const char *home = getenv("HOME");
    if (!home || !home[0]) return;
    snprintf(u->d[0].path, sizeof u->d[0].path, "%s", home);
    u->d[0].icon = S("user-home");
    u->n = 1;
    ArenaMark mark = arena_mark(scratch);
    char dirs[PATH_MAX];
    Str text = xdg_dir(dirs, sizeof dirs, "XDG_CONFIG_HOME", ".config", "user-dirs.dirs") ? read_file(scratch, dirs) : STR_EMPTY;
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
