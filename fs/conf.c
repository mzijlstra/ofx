// ofx.conf (F10, docs/plan.md "F10 design"): the options Michael sets, in
// $XDG_CONFIG_HOME/ofx/ofx.conf, which ofx only reads (Edit → Preferences… writes it once, when it
// isn't there, on that click). What ofx remembers by itself stays in the state file (state.c).
//
// One `key = value` a line, `#` a comment, a key repeated for a list (as Ghostty's). A line ofx
// can't read, or a key it doesn't know, is skipped and the rest applies; the first such line is
// kept for the status line. Every option's help is in CONF_OPTS: the file Preferences writes and
// the README's option list are made from it (conf_default_text), so neither can drift from what
// conf_parse reads.

#include "../base/base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CONF_SKIP_MAX = 32, CONF_NAME_MAX = 64, CONF_DATE_MAX = 64, CONF_WHY_MAX = 200 };

typedef enum { DATE_SHORT, DATE_LONG, DATE_ISO, DATE_PATTERN } DateFormat;

typedef struct {
    bool       previews;                          // previews in the icon view (F6.4)
    i64        preview_max;                       // the largest image previewed, in bytes
    DateFormat date;
    char       date_pattern[CONF_DATE_MAX];       // DATE_PATTERN's strftime pattern
    char       skip[CONF_SKIP_MAX][CONF_NAME_MAX];   // folders a search never enters (F9)
    int        nskip;
    int        bad;                               // the first line skipped (0: none), and why
    char       why[CONF_WHY_MAX];
} Conf;

typedef enum { CK_BOOL, CK_SIZE, CK_DATE, CK_NAMES } ConfKind;

// Each option: its key, kind, default as written in the file, and its help (lines, each made a
// comment). The help says what it does, what it takes, and an example where one helps.
typedef struct {
    const char *key;
    ConfKind    kind;
    const char *def;
    const char *help;
} ConfOpt;

static const ConfOpt CONF_OPTS[] = {
    { "previews", CK_BOOL, "on",
      "Previews of images in the icon view, in place of their file type's icon.\n"
      "on or off. Off, ofx makes and reads no previews at all (the cache, shared\n"
      "with Nemo, is left as it is)." },
    { "preview-max-size", CK_SIZE, "10 MiB",
      "The largest image file ofx makes a preview of; a larger one shows its file\n"
      "type's icon. A size: B, KiB, MiB, GiB (or KB, MB, GB, in 1000s).\n"
      "Example: preview-max-size = 50 MiB" },
    { "date-format", CK_DATE, "short",
      "How the list shows a file's date (Modified, or Deleted in the trash).\n"
      "  short   14:03 today, Oct 20 this year, 2025-10-20 before\n"
      "  long    2026-10-20 14:03, always\n"
      "  iso     2026-10-20T14:03:05, always\n"
      "or a strftime pattern with a %, as `date` takes: date-format = %d-%m-%Y %H:%M" },
    { "search-skip", CK_NAMES, ".git node_modules",
      "Folders a search (Ctrl+Shift+F) never goes into, by name, wherever they are;\n"
      "the status line counts each one it skipped. One name a line; your lines\n"
      "replace this default list, so repeat .git and node_modules to keep them.\n"
      "Example:\n"
      "  search-skip = .git\n"
      "  search-skip = node_modules\n"
      "  search-skip = target" },
};

static const char CONF_HEAD[] =
    "ofx.conf: the options you set for ofx (the Omarchy file explorer).\n"
    "\n"
    "ofx reads this file when it starts and again whenever you save it: every open\n"
    "window applies a change at once. ofx never writes it, except to make it\n"
    "(Edit > Preferences..., when it isn't there).\n"
    "\n"
    "One option a line, `key = value`; # starts a comment. A line ofx can't read is\n"
    "skipped, the rest applies, and the status line says which line and why.\n"
    "Below, every option at its default, commented out: remove the # to change one.\n"
    "\n"
    "Not set here: what you toggle in the View menu (the sidebar's mode, list or\n"
    "icons, the icon size, the menu bar, the Owner, Group and Permissions columns)\n"
    "is remembered by ofx itself, in ~/.local/state/ofx/state. With the menu bar\n"
    "hidden, its menus still open with F10, or Alt and a menu's letter (Alt+V for\n"
    "View), and View > Show Menu brings it back.";

// A size ("10 MiB", "500KB", "0"), in bytes; false when it isn't one.
static bool conf_size(Str v, i64 *out)
{
    i64 n = 0;
    isize i = 0;
    while (i < v.len && is_digit(v.p[i]) && n < ((i64)1 << 50)) n = n * 10 + (v.p[i++] - '0');
    if (i == 0 || (i < v.len && is_digit(v.p[i]))) return false;
    Str unit = str_trim(str_cut(v, i, v.len));
    static const struct { const char *u; i64 mul; } UNITS[] = {
        { "", 1 }, { "B", 1 }, { "KiB", (i64)1 << 10 }, { "MiB", (i64)1 << 20 }, { "GiB", (i64)1 << 30 },
        { "KB", 1000 }, { "MB", 1000000 }, { "GB", 1000000000 },
    };
    for (int k = 0; k < COUNT(UNITS); k++)
        if (str_eq(unit, str_from_c(UNITS[k].u))) return *out = n * UNITS[k].mul, true;
    return false;
}

static bool conf_bool(Str v, bool *out)
{
    if (str_eq(v, S("on")) || str_eq(v, S("yes")) || str_eq(v, S("true"))) return *out = true, true;
    if (str_eq(v, S("off")) || str_eq(v, S("no")) || str_eq(v, S("false"))) return *out = false, true;
    return false;
}

// What each kind wants, for the status line.
static const char *const CONF_WANTS[] = {
    [CK_BOOL] = "on or off", [CK_SIZE] = "a size like 10 MiB", [CK_DATE] = "short, long, iso or a pattern with %",
    [CK_NAMES] = "a folder name, without a /",
};

static void conf_defaults(Conf *c)
{
    *c = (Conf){ .previews = true, .preview_max = (i64)10 << 20, .date = DATE_SHORT };
    snprintf(c->skip[0], CONF_NAME_MAX, ".git"), snprintf(c->skip[1], CONF_NAME_MAX, "node_modules");
    c->nskip = 2;
}

// One value for opt k into c; false when it isn't one opt k takes. *listed: this file's first
// search-skip line has emptied the default list.
static bool conf_set(Conf *c, int k, Str v, bool *listed)
{
    const char *key = CONF_OPTS[k].key;
    if (strcmp(key, "previews") == 0) return conf_bool(v, &c->previews);
    if (strcmp(key, "preview-max-size") == 0) return conf_size(v, &c->preview_max);
    if (strcmp(key, "date-format") == 0) {
        if (str_eq(v, S("short"))) return c->date = DATE_SHORT, true;
        if (str_eq(v, S("long"))) return c->date = DATE_LONG, true;
        if (str_eq(v, S("iso"))) return c->date = DATE_ISO, true;
        if (str_find(v, '%') < 0 || v.len >= CONF_DATE_MAX) return false;
        snprintf(c->date_pattern, sizeof c->date_pattern, "%.*s", STR_ARG(v));
        return c->date = DATE_PATTERN, true;
    }
    if (strcmp(key, "search-skip") == 0) {
        if (!v.len || v.len >= CONF_NAME_MAX || str_find(v, '/') >= 0 || str_eq(v, S(".")) || str_eq(v, S(".."))) return false;
        if (!*listed) c->nskip = 0, *listed = true;
        if (c->nskip == CONF_SKIP_MAX) return false;
        snprintf(c->skip[c->nskip++], CONF_NAME_MAX, "%.*s", STR_ARG(v));
        return true;
    }
    return false;
}

// text's options over the defaults into c.
static void conf_parse(Str text, Conf *c)
{
    conf_defaults(c);
    bool listed = false;
    int no = 0;
    for (isize at = 0; at < text.len;) {
        isize e = at;
        while (e < text.len && text.p[e] != '\n') e++;
        Str line = str_trim(str_cut(text, at, e));
        at = e + 1, no++;
        if (line.len == 0 || line.p[0] == '#') continue;
        isize eq = str_find(line, '=');
        Str key = eq < 0 ? line : str_trim(str_cut(line, 0, eq)), val = eq < 0 ? STR_EMPTY : str_trim(str_cut(line, eq + 1, line.len));
        int k = -1;
        for (int i = 0; i < COUNT(CONF_OPTS) && k < 0; i++)
            if (str_eq(key, str_from_c(CONF_OPTS[i].key))) k = i;
        if (eq >= 0 && k >= 0 && conf_set(c, k, val, &listed)) continue;
        if (c->bad) continue;
        c->bad = no;
        if (eq < 0) snprintf(c->why, sizeof c->why, "not a key = value line");
        else if (k < 0) snprintf(c->why, sizeof c->why, "no option %.*s", (int)(key.len < 60 ? key.len : 60), key.p);
        else snprintf(c->why, sizeof c->why, "%s wants %s", CONF_OPTS[k].key, CONF_WANTS[CONF_OPTS[k].kind]);
    }
}

// The comment lines of text, "# " before each ("#" alone for an empty one), into out at *at.
static void conf_comment(char *out, size_t cap, size_t *at, const char *text)
{
    for (const char *p = text; *p && *at < cap;) {
        const char *e = strchr(p, '\n');
        int n = e ? (int)(e - p) : (int)strlen(p);
        int k = snprintf(out + *at, cap - *at, n ? "# %.*s\n" : "#\n", n, p);
        if (k > 0) *at += (size_t)k;
        p = e ? e + 1 : p + n;
    }
}

// The file Preferences writes when there is none: the head, then each option's help and its default
// commented out (a list's default a line per name). Its length; cut at cap.
static size_t conf_default_text(char *out, size_t cap)
{
    size_t at = 0;
    out[0] = 0;
    conf_comment(out, cap, &at, CONF_HEAD);
    for (int k = 0; k < COUNT(CONF_OPTS) && at < cap; k++) {
        const ConfOpt *o = &CONF_OPTS[k];
        int n = snprintf(out + at, cap - at, "\n");
        if (n > 0) at += (size_t)n;
        conf_comment(out, cap, &at, o->help);
        Str def = str_from_c(o->def);
        for (isize i = 0; i < def.len && at < cap;) {   // (a list's names, one a line)
            isize e = o->kind == CK_NAMES ? i : def.len;
            while (o->kind == CK_NAMES && e < def.len && def.p[e] != ' ') e++;
            n = snprintf(out + at, cap - at, "#%s = %.*s\n", o->key, (int)(e - i), def.p + i);
            if (n > 0) at += (size_t)n;
            i = e + 1;
        }
    }
    return at < cap ? at : cap - 1;
}

// $XDG_CONFIG_HOME/ofx/ofx.conf, else ~/.config/ofx/ofx.conf; false when neither is set.
static bool conf_path(char *out, size_t cap)
{
    return xdg_dir(out, cap, "XDG_CONFIG_HOME", ".config", "ofx/ofx.conf");
}

static bool ops_allowed(const char *path, Err *err);

// The file Preferences writes, at path, when there is none: its folder made, never one replaced (an
// existing file, or a link, is left as it is: true, nothing written). Through the test guard.
static bool conf_create(const char *path, Err *err)
{
    struct stat st;
    if (lstat(path, &st) == 0) return true;
    if (!ops_allowed(path, err)) return false;
    Str dir = path_dir(str_from_c(path));
    char d[PATH_MAX];
    snprintf(d, sizeof d, "%.*s", STR_ARG(dir));
    if (!mkdir_p(d, 0700, true, err)) return false;
    static char text[16384];
    size_t n = conf_default_text(text, sizeof text);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0) return errno == EEXIST || (err_set(err, "%s: %s", path, strerror(errno)), false);
    bool ok = write(fd, text, n) == (ssize_t)n;
    int e = errno;
    if (close(fd) != 0 || !ok) return err_set(err, "%s: %s", path, strerror(ok ? errno : e)), false;
    return true;
}
