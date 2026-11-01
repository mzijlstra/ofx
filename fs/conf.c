// ofx.conf (F10, docs/plan.md "F10 design"): the options Michael sets, in
// $XDG_CONFIG_HOME/ofx/ofx.conf. ofx writes it only when it isn't there, at the start (and on Edit →
// Preferences…), so from then on what applies is whatever the file says: a list is exactly its
// lines, none meaning an empty list (Michael, 2026-11-01). Only when there is no file to read (it
// couldn't be made) do the built-in defaults apply, conf_defaults. What ofx remembers by itself
// stays in the state file (state.c).
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

enum { CONF_SKIP_MAX = 32, CONF_NAME_MAX = 64, CONF_DATE_MAX = 64, CONF_WHY_MAX = 200, CONF_LINE_MAX = 512, CONF_DAYS_MAX = 36500 };

typedef enum { DATE_SHORT, DATE_LONG, DATE_ISO, DATE_PATTERN } DateFormat;

// The folders a search never enters, by name (search-skip, F9 and F10): each walk is given its own
// copy (fs/search.c), so a change to the file mid-search doesn't reach a walk under way.
typedef struct {
    char name[CONF_SKIP_MAX][CONF_NAME_MAX];
    int  n;
} SearchSkipList;

// The built-in defaults, each said once here; CONF_OPTS' text says the same (test_conf ties them).
static const SearchSkipList SEARCH_SKIP_DEFAULT = { { ".git", "node_modules" }, 2 };
#define CONF_PREVIEW_MAX ((i64)10 << 20)   // preview-max-size (Michael, 2026-10-24)
#define CLEAN_UNUSED_S   (90 * 86400LL)    // clean-unused-days, in seconds (F6.5, Michael, 2026-10-25)
#define CLEAN_CAP        ((i64)1 << 30)    // clean-cap

typedef struct {
    bool       previews;                          // previews in the icon view (F6.4)
    i64        preview_max;                       // the largest image previewed, in bytes
    DateFormat date;
    char       date_pattern[CONF_DATE_MAX];       // DATE_PATTERN's strftime pattern
    SearchSkipList skip;                          // search-skip
    char       editor[EDITORS_USER][CONF_LINE_MAX];  // Open at Line's recipes (F9 step 4), as written
    int        neditor;
    bool       clean, clean_orphans;              // ofx-clean (F6.5): at all; previews of files gone or changed
    i64        clean_unused_days;                 // 0: never for this
    i64        clean_cap;                         // bytes; 0: no cap
    int        bad;                               // the first line skipped (0: none), and why
    char       why[CONF_WHY_MAX];
} Conf;

typedef enum { CK_BOOL, CK_SIZE, CK_DATE, CK_NAMES, CK_EDITOR, CK_DAYS } ConfKind;

// Each option: its key, kind, its field in Conf, default as written in the file ("": none, the help
// has an example), and its help (lines, each made a comment). The help says what it does, what it
// takes, and an example where one helps.
typedef struct {
    const char *key;
    ConfKind    kind;
    size_t      at;     // offsetof(Conf, …): conf_set writes there by kind
    const char *def;
    const char *help;
} ConfOpt;

static const ConfOpt CONF_OPTS[] = {
    { "previews", CK_BOOL, offsetof(Conf, previews), "on",
      "Previews of images in the icon view, in place of their file type's icon.\n"
      "on or off. Off, ofx makes and reads no previews at all (the cache, shared\n"
      "with Nemo, is left as it is)." },
    { "preview-max-size", CK_SIZE, offsetof(Conf, preview_max), "10 MiB",
      "The largest image file ofx makes a preview of; a larger one shows its file\n"
      "type's icon. A size: B, KiB, MiB, GiB (or KB, MB, GB, in 1000s).\n"
      "Example: preview-max-size = 50 MiB" },
    { "date-format", CK_DATE, offsetof(Conf, date), "short",
      "How the list shows a file's date (Modified, or Deleted in the trash).\n"
      "  short   14:03 today, Oct 20 this year, 2025-10-20 before\n"
      "  long    2026-10-20 14:03, always\n"
      "  iso     2026-10-20T14:03:05, always\n"
      "or a strftime pattern with a %, as `date` takes: date-format = %d-%m-%Y %H:%M" },
    { "search-skip", CK_NAMES, offsetof(Conf, skip), ".git node_modules",
      "Folders a search (Ctrl+Shift+F) never goes into, by name, wherever they are;\n"
      "the status line counts each one it skipped. One name a line, and the list is\n"
      "exactly these lines: add one to skip a folder (search-skip = target), remove\n"
      "one to search inside it. With no search-skip line at all, a search goes into\n"
      "every folder." },
    { "editor", CK_EDITOR, offsetof(Conf, editor), "",
      "How Open at Line (Ctrl+Shift+Enter on a result found inside files) starts an\n"
      "editor at that line. It uses Omarchy's default editor (`omarchy default\n"
      "editor`, else $EDITOR), and ofx knows how to start nvim, vim, helix, hx,\n"
      "micro, nano, kak, emacs, code, codium, cursor, zeditor, zed, sublime_text and\n"
      "subl. A line here adds another, or changes one of those; none by default.\n"
      "One editor a line: editor = name terminal command...\n"
      "  name      the editor's name, as Omarchy's default (or $EDITOR's program)\n"
      "  terminal  yes: in a new terminal; no: a window of its own\n"
      "  command   its words, split at spaces; \"double quotes\" around a word with\n"
      "            a space; ~/ at a word's start is your home folder; {file},\n"
      "            {line} and {col} (characters, from 1) are filled in, and the file\n"
      "            is added at the end when no word has {file}\n"
      "Example: editor = nvim yes nvim +{line} {file}" },
    { "clean", CK_BOOL, offsetof(Conf, clean), "on",
      "Whether ofx-clean cleans the previews' cache (~/.cache/thumbnails, shared with\n"
      "Nemo) at all, by the three clean- options below. on or off. ofx-clean runs\n"
      "daily from a systemd timer, installed but not on until you turn it on:\n"
      "  systemctl --user enable --now ofx-clean.timer\n"
      "`ofx-clean --dry-run` lists what it would delete, and deletes nothing." },
    { "clean-orphans", CK_BOOL, offsetof(Conf, clean_orphans), "on",
      "ofx-clean deletes the preview of a file that is gone, or has changed since\n"
      "(the next look makes a new one). A file on a drive that isn't plugged in, or\n"
      "on a network mount, keeps its previews. on or off." },
    { "clean-unused-days", CK_DAYS, offsetof(Conf, clean_unused_days), "90",
      "ofx-clean deletes a preview no program has read for this many days (by its\n"
      "access time). A whole number of days; 0: never for this.\n"
      "Example: clean-unused-days = 30" },
    { "clean-cap", CK_SIZE, offsetof(Conf, clean_cap), "1 GiB",
      "The size ofx-clean keeps the cache under: past it, the least recently read\n"
      "previews go first. A size, as preview-max-size takes; 0: no cap." },
};

static const char CONF_HEAD[] =
    "ofx.conf: the options you set for ofx (the Omarchy file explorer).\n"
    "\n"
    "ofx reads this file when it starts and again whenever you save it: every open\n"
    "window applies a change at once (ofx-clean reads it each time it runs). ofx\n"
    "never changes it: it only writes it, as you see it here, when it isn't there\n"
    "(when ofx starts, or Edit > Preferences...).\n"
    "\n"
    "One option a line, `key = value`; # starts a comment. A line ofx can't read is\n"
    "skipped, the rest applies, and the status line says which line and why.\n"
    "Below, every option at its default, commented out: remove the # to change one.\n"
    "A list (search-skip) is written out instead, one line a name, since it is\n"
    "exactly the lines it has here: removing them all makes it empty.\n"
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
        if (str_eq(unit, str_from_c(UNITS[k].u))) return n <= INT64_MAX / UNITS[k].mul && (*out = n * UNITS[k].mul, true);   // (reviewer-2: 10^14 GiB overflowed)
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
    [CK_NAMES] = "a folder name, without a /", [CK_EDITOR] = "a name, yes or no, then the command",
    [CK_DAYS] = "a whole number of days",
};

// The built-in defaults: all of them with no file to read; under a file, what it doesn't set (but
// the list, which is the file's lines).
static void conf_defaults(Conf *c)
{
    *c = (Conf){ .previews = true, .preview_max = CONF_PREVIEW_MAX, .date = DATE_SHORT, .skip = SEARCH_SKIP_DEFAULT, .clean = true,
                 .clean_orphans = true, .clean_unused_days = CLEAN_UNUSED_S / 86400, .clean_cap = CLEAN_CAP };
}

// One value for opt k into c; false when it isn't one opt k takes.
static bool conf_set(Conf *c, int k, Str v)
{
    void *f = (char *)c + CONF_OPTS[k].at;
    switch (CONF_OPTS[k].kind) {
    case CK_BOOL: return conf_bool(v, f);
    case CK_SIZE: return conf_size(v, f);
    case CK_DAYS: {
        i64 n = 0;
        for (isize i = 0; i < v.len; i++)
            if (!is_digit(v.p[i]) || (n = n * 10 + (v.p[i] - '0')) > CONF_DAYS_MAX) return false;
        return v.len && (*(i64 *)f = n, true);
    }
    case CK_DATE:   // (date, and date_pattern for a pattern)
        if (str_eq(v, S("short"))) return c->date = DATE_SHORT, true;
        if (str_eq(v, S("long"))) return c->date = DATE_LONG, true;
        if (str_eq(v, S("iso"))) return c->date = DATE_ISO, true;
        if (str_find(v, '%') < 0 || v.len >= CONF_DATE_MAX) return false;
        snprintf(c->date_pattern, sizeof c->date_pattern, "%.*s", STR_ARG(v));
        return c->date = DATE_PATTERN, true;
    case CK_NAMES: {
        SearchSkipList *l = f;
        if (!v.len || v.len >= CONF_NAME_MAX || str_find(v, '/') >= 0 || str_eq(v, S(".")) || str_eq(v, S(".."))) return false;
        if (l->n == CONF_SKIP_MAX) return false;
        snprintf(l->name[l->n++], CONF_NAME_MAX, "%.*s", STR_ARG(v));
        return true;
    }
    case CK_EDITOR: {   // (editor and neditor; read as the editors file reads a recipe: one, no bad line)
        static u8 mem[KiB(16)];
        static Editors one;
        Arena a;
        arena_init(&a, "conf-editor", mem, sizeof mem);
        if (v.len >= CONF_LINE_MAX || c->neditor == EDITORS_USER) return false;
        editors_parse(&a, v, &one);
        if (one.n != 1) return false;
        snprintf(c->editor[c->neditor++], CONF_LINE_MAX, "%.*s", STR_ARG(v));
        return true;
    }
    }
    return false;
}

// The file's text into c: its options over the defaults, its search-skip lines alone the list (and
// its editor lines, which have no default).
static void conf_parse(Str text, Conf *c)
{
    conf_defaults(c);
    c->skip.n = 0;
    int no = 0;
    isize at = 0;
    for (Str line; next_line(text, &at, &line);) {
        line = str_trim(line), no++;
        if (line.len == 0 || line.p[0] == '#') continue;
        isize eq = str_find(line, '=');
        Str key = eq < 0 ? line : str_trim(str_cut(line, 0, eq)), val = eq < 0 ? STR_EMPTY : str_trim(str_cut(line, eq + 1, line.len));
        int k = -1;
        for (int i = 0; i < COUNT(CONF_OPTS) && k < 0; i++)
            if (str_eq(key, str_from_c(CONF_OPTS[i].key))) k = i;
        if (eq >= 0 && k >= 0 && conf_set(c, k, val)) continue;
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

// The file ofx writes when there is none: the head, then each option's help and its default
// commented out, but a list's default written out, a line per name (the file's lines are the
// list); an option with no default, its help alone. Its length; cut at cap.
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
            n = snprintf(out + at, cap - at, "%s%s = %.*s\n", o->kind == CK_NAMES ? "" : "#", o->key, (int)(e - i), def.p + i);
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

// ofx.conf as it is now into c: parsed when it is a file that can be read, else the built-in
// defaults (false: no file). The GUI and ofx-clean both read it this way; its text is in a only
// for the parse.
static bool conf_load(Arena *a, Conf *c)
{
    char path[PATH_MAX];
    struct stat st;
    bool there = conf_path(path, sizeof path) && stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, R_OK) == 0;
    ArenaMark mark = arena_mark(a);
    if (there) conf_parse(read_file(a, path), c);
    else conf_defaults(c);
    arena_restore(a, mark);
    return there;
}

static bool ops_allowed(const char *path, Err *err);

// The file ofx writes, at path, when there is none (at the start, and on Preferences): its folder
// made, never one replaced (an existing file, or a link, is left as it is: true, nothing written).
// Through the test guard.
static bool conf_create(const char *path, Err *err)
{
    struct stat st;
    if (lstat(path, &st) == 0) return true;
    if (!ops_allowed(path, err)) return false;
    char d[PATH_MAX];
    if (!path_parent(d, sizeof d, path) || !mkdir_p(d, 0700, true, err)) return false;
    static char text[16384];
    size_t n = conf_default_text(text, sizeof text);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0) return errno == EEXIST || (err_set(err, "%s: %s", path, strerror(errno)), false);
    bool ok = write(fd, text, n) == (ssize_t)n;
    int e = errno;
    if (close(fd) != 0 || !ok) {
        e = ok ? errno : e;
        unlink(path);   // (ours, O_EXCL: a cut file would never be written again)
        return err_set(err, "%s: %s", path, strerror(e)), false;
    }
    return true;
}
