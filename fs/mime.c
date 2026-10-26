// A file's MIME type from its name, by shared-mime-info's glob tables, and the icon names for it
// (docs/plan.md, "MIME type"). Read once, from every XDG data dir's mime/: globs2, generic-icons
// and icons.
//
// globs2 lines are `weight:type:glob[:flags]`; the flag `cs` makes a glob case-sensitive. A name is
// matched in three tiers, the first that matches wins, as xdgmime does: literal names
// ("Makefile"), then `*.ext` suffixes, longest first ("*.tar.gz" before "*.gz"), then any other
// pattern with fnmatch. Within a tier the highest weight wins, then the longest glob, and a
// case-sensitive match goes before a folded one. Data dirs earlier in the search order (the user's
// own first) win ties.

#include "../base/base.h"

#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

enum { MIME_GLOBS_MAX = 8192, MIME_DIRS_MAX = 16 };

typedef struct {
    Str  type;
    Str  glob;
    int  weight;
    bool cs;
    int  order;     // the line's place over all files: lower wins a tie
} MimeGlob;

typedef struct {
    MimeGlob *g;
    isize     n;
    StrMap    literal_cs, literal_ci, suffix_cs, suffix_ci;   // key: the literal or the suffix after "*"
    isize    *other;   // the globs fnmatch must try
    isize     nother;
    StrMap    generic;   // type -> its generic icon, in `icon_of`
    StrMap    icon;      // type -> its own icon name
    Str      *icon_of;   // the values, by the maps' int
    isize     nicons;
} MimeDb;

static Str lower(Arena *a, Str s)
{
    char *p = arena_push(a, s.len + 1, 1);
    for (isize i = 0; i < s.len; i++) p[i] = (char)fold((unsigned char)s.p[i]);
    return (Str){ p, s.len };
}

// The whole file, NUL-terminated, into a; STR_EMPTY when it isn't there or won't fit. Sized by
// fstat first: opening the arena's whole free tail would cost a pass over it under ASan. A file
// that reads shorter than that size changed while read, and gives nothing. procfs files say 0
// (/proc/self/mountinfo): those are read to their end, up to READ_UNSIZED_MAX.
enum { READ_UNSIZED_MAX = MiB(1) };

static Str read_file(Arena *a, const char *path)
{
    // A FIFO or a device at one of these paths would block fopen: regular files only.
    struct stat pre;
    if (stat(path, &pre) != 0 || !S_ISREG(pre.st_mode)) return STR_EMPTY;
    FILE *f = fopen(path, "rb");
    if (!f) return STR_EMPTY;
    struct stat st;
    char *dst = NULL;
    size_t n = 0;
    bool whole = false;
    if (fstat(fileno(f), &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 0) {
        isize avail;
        dst = arena_tail(a, &avail);
        size_t cap = avail - 1 < READ_UNSIZED_MAX ? (size_t)(avail - 1) : READ_UNSIZED_MAX;
        for (size_t got; avail > 1 && n < cap && (got = fread(dst + n, 1, cap - n, f)) > 0;) n += got;
        whole = feof(f) && !ferror(f);
        if (!whole && n == READ_UNSIZED_MAX) fprintf(stderr, "ofx: WARN %s is over %d bytes: not read\n", path, READ_UNSIZED_MAX);
        arena_commit(a, whole ? (isize)n + 1 : 0);
    } else if (S_ISREG(st.st_mode) && (dst = arena_try_push(a, (isize)st.st_size + 1, 1))) {
        n = fread(dst, 1, (size_t)st.st_size, f);
        whole = n == (size_t)st.st_size;
    }
    fclose(f);
    if (!whole) return STR_EMPTY;
    dst[n] = 0;
    return (Str){ dst, (isize)n };
}

// The next line of s from *at, without its newline.
static bool next_line(Str s, isize *at, Str *line)
{
    if (*at >= s.len) return false;
    isize e = *at;
    while (e < s.len && s.p[e] != '\n') e++;
    *line = str_cut(s, *at, e);
    *at = e + 1;
    return true;
}

static bool glob_is_literal(Str g)
{
    for (isize i = 0; i < g.len; i++)
        if (g.p[i] == '*' || g.p[i] == '?' || g.p[i] == '[') return false;
    return true;
}

// "*.ext" with no other wildcard: its suffix ".ext".
static bool glob_suffix(Str g, Str *suffix)
{
    if (g.len < 3 || g.p[0] != '*' || g.p[1] != '.') return false;
    Str rest = str_cut(g, 1, g.len);
    if (!glob_is_literal(rest)) return false;
    *suffix = rest;
    return true;
}

// Whether glob i beats glob j (both matched in the same tier).
static bool glob_better(const MimeDb *db, isize i, isize j, bool i_folded, bool j_folded)
{
    const MimeGlob *a = &db->g[i], *b = &db->g[j];
    if (a->weight != b->weight) return a->weight > b->weight;
    if (a->glob.len != b->glob.len) return a->glob.len > b->glob.len;
    if (i_folded != j_folded) return !i_folded;
    return a->order < b->order;
}

// Puts glob i under key in m, unless a better one holds it.
static void map_best(MimeDb *db, StrMap *m, Str key, isize i)
{
    int have;
    if (strmap_get(m, key, &have)) {
        if (!glob_better(db, i, have, false, false)) return;
        // StrMap keeps one value per key: overwrite it in place.
        for (isize s = 0; s < m->cap; s++)
            if (m->keys[s].p && str_eq(m->keys[s], key)) m->vals[s] = (int)i;
        return;
    }
    strmap_put(m, key, (int)i);
}

static void mime_add_globs(Arena *a, MimeDb *db, Str text)
{
    isize at = 0;
    for (Str line; next_line(text, &at, &line);) {
        if (line.len == 0 || line.p[0] == '#' || db->n == MIME_GLOBS_MAX) continue;
        isize c1 = str_find(line, ':');
        if (c1 < 0) continue;
        Str rest = str_cut(line, c1 + 1, line.len);
        isize c2 = str_find(rest, ':');
        if (c2 < 0) continue;
        Str type = str_cut(rest, 0, c2), glob = str_cut(rest, c2 + 1, rest.len), flags = STR_EMPTY;
        isize c3 = str_find(glob, ':');
        if (c3 >= 0) flags = str_cut(glob, c3 + 1, glob.len), glob = str_cut(glob, 0, c3);
        if (type.len == 0 || glob.len == 0) continue;
        bool cs = false;
        for (isize fat = 0; fat < flags.len;) {
            isize e = fat;
            while (e < flags.len && flags.p[e] != ',') e++;
            cs |= str_eq(str_cut(flags, fat, e), S("cs"));
            fat = e + 1;
        }
        isize i = db->n++;
        db->g[i] = (MimeGlob){ type, glob, atoi(arena_cstr(a, str_cut(line, 0, c1))), cs, (int)i };
        Str suffix;
        if (glob_is_literal(glob)) map_best(db, cs ? &db->literal_cs : &db->literal_ci, cs ? glob : lower(a, glob), i);
        else if (glob_suffix(glob, &suffix)) map_best(db, cs ? &db->suffix_cs : &db->suffix_ci, cs ? suffix : lower(a, suffix), i);
        else db->other[db->nother++] = i;
    }
}

// `type:icon` lines into m; the first data dir's line for a type wins.
static void mime_add_icons(Arena *a, MimeDb *db, StrMap *m, Str text)
{
    (void)a;
    isize at = 0;
    for (Str line; next_line(text, &at, &line);) {
        isize c = str_find(line, ':');
        if (c <= 0 || c == line.len - 1 || db->nicons == MIME_GLOBS_MAX) continue;
        Str type = str_cut(line, 0, c);
        int have;
        if (strmap_get(m, type, &have)) continue;
        db->icon_of[db->nicons] = str_cut(line, c + 1, line.len);
        strmap_put(m, type, (int)db->nicons++);
    }
}

// The XDG data dirs, the user's first: $XDG_DATA_HOME (~/.local/share), then $XDG_DATA_DIRS
// (/usr/local/share:/usr/share). Each into a as a C string.
static isize data_dirs(Arena *a, const char **out, isize max)
{
    isize n = 0;
    const char *home = getenv("HOME"), *dh = getenv("XDG_DATA_HOME"), *dd = getenv("XDG_DATA_DIRS");
    if (dh && dh[0]) out[n++] = arena_cstr(a, str_from_c(dh));
    else if (home && home[0]) out[n++] = arena_sprintf(a, "%s/.local/share", home);
    Str dirs = str_from_c(dd && dd[0] ? dd : "/usr/local/share:/usr/share");
    for (isize at = 0; at < dirs.len && n < max;) {
        isize e = at;
        while (e < dirs.len && dirs.p[e] != ':') e++;
        if (e > at) out[n++] = arena_cstr(a, str_cut(dirs, at, e));
        at = e + 1;
    }
    return n;
}

static void mime_load(Arena *a, MimeDb *db)
{
    *db = (MimeDb){ 0 };
    db->g = arena_new(a, MimeGlob, MIME_GLOBS_MAX);
    db->other = arena_new(a, isize, MIME_GLOBS_MAX);
    db->icon_of = arena_new(a, Str, MIME_GLOBS_MAX);
    strmap_init(&db->literal_cs, a, MIME_GLOBS_MAX);
    strmap_init(&db->literal_ci, a, MIME_GLOBS_MAX);
    strmap_init(&db->suffix_cs, a, MIME_GLOBS_MAX);
    strmap_init(&db->suffix_ci, a, MIME_GLOBS_MAX);
    strmap_init(&db->generic, a, MIME_GLOBS_MAX);
    strmap_init(&db->icon, a, MIME_GLOBS_MAX);
    const char *dirs[MIME_DIRS_MAX];
    isize nd = data_dirs(a, dirs, MIME_DIRS_MAX);
    for (isize k = 0; k < nd; k++) {
        mime_add_globs(a, db, read_file(a, arena_sprintf(a, "%s/mime/globs2", dirs[k])));
        mime_add_icons(a, db, &db->generic, read_file(a, arena_sprintf(a, "%s/mime/generic-icons", dirs[k])));
        mime_add_icons(a, db, &db->icon, read_file(a, arena_sprintf(a, "%s/mime/icons", dirs[k])));
    }
}

// ---- matching -------------------------------------------------------------------------------

// The best glob in a map for key, or -1.
static isize map_get(const StrMap *m, Str key)
{
    int i;
    return strmap_get(m, key, &i) ? i : -1;
}

// The type for a file name, or STR_EMPTY when no glob matches. scratch holds only temporaries.
static Str mime_of_name(Arena *scratch, const MimeDb *db, Str name)
{
    ArenaMark mark = arena_mark(scratch);
    Str low = lower(scratch, name);
    isize best = map_get(&db->literal_cs, name);
    if (best < 0) best = map_get(&db->literal_ci, low);
    // Suffixes, longest first: every '.' from the first on starts one.
    for (isize i = 0; best < 0 && i < name.len; i++) {
        if (name.p[i] != '.') continue;
        isize cs = map_get(&db->suffix_cs, str_cut(name, i, name.len));
        isize ci = map_get(&db->suffix_ci, str_cut(low, i, low.len));
        if (cs >= 0 && ci >= 0) best = glob_better(db, cs, ci, false, true) ? cs : ci;
        else best = cs >= 0 ? cs : ci;
    }
    if (best < 0) {
        char *cname = arena_cstr(scratch, name), *clow = arena_cstr(scratch, low);
        bool best_folded = false;
        for (isize k = 0; k < db->nother; k++) {
            isize i = db->other[k];
            const MimeGlob *g = &db->g[i];
            char *pat = arena_cstr(scratch, g->cs ? g->glob : lower(scratch, g->glob));
            if (fnmatch(pat, g->cs ? cname : clow, 0) != 0) continue;
            bool folded = !g->cs;
            if (best < 0 || glob_better(db, i, best, folded, best_folded)) best = i, best_folded = folded;
        }
    }
    arena_restore(scratch, mark);
    return best >= 0 ? db->g[best].type : STR_EMPTY;
}

// The MIME type of an entry: inode/directory for a folder, the glob's type for a file, else
// application/x-executable with an execute bit, else application/octet-stream.
static Str mime_of(Arena *scratch, const MimeDb *db, const Entry *e)
{
    if (kind_is_dir(e->kind)) return S("inode/directory");
    if (e->kind == K_LINK_BROKEN) return S("inode/symlink");
    if (e->kind == K_OTHER) return S("inode/x-other");
    Str t = mime_of_name(scratch, db, e->name);
    if (t.len) return t;
    return (e->mode & 0111) ? S("application/x-executable") : S("application/octet-stream");
}

// The icon names to try for a MIME type, most specific first, into names (at most 5); each one
// into a. The theme lookup (icons.c) takes the first it has. Without a generic icon in
// generic-icons, the spec's: the media type and "-x-generic" (image-x-generic: generic-icons names
// none for image/png, and Yaru has no application-x-generic, so a PNG without a preview had no
// icon at all, 2026-10-24).
static isize mime_icon_names(Arena *a, const MimeDb *db, Str type, Str *names)
{
    isize n = 0;
    int i;
    if (strmap_get(&db->icon, type, &i)) names[n++] = db->icon_of[i];
    char *dashed = arena_cstr(a, type);
    for (char *p = dashed; *p; p++)
        if (*p == '/') *p = '-';
    names[n++] = str_from_c(dashed);
    if (strmap_get(&db->generic, type, &i)) names[n++] = db->icon_of[i];
    isize slash = str_find(type, '/');
    Str media = slash > 0 ? str_cut(type, 0, slash) : type;
    names[n++] = str_from_c(arena_sprintf(a, "%.*s-x-generic", STR_ARG(media)));
    if (!str_eq(media, S("application"))) names[n++] = S("application-x-generic");
    return n;
}

// ---- the type's name ------------------------------------------------------------------------

// The type's name in words ("Plain text document" for text/plain) into out: the first <comment>
// without xml:lang in mime/<type>.xml of the first data dir that has it (the user's own first), its
// entities read. False, out empty, when none says (F7.2's Type line, Michael, 2026-10-26).
static bool mime_comment(Arena *scratch, Str type, char *out, size_t cap)
{
    out[0] = 0;
    isize slash = str_find(type, '/');
    if (slash <= 0 || str_find(str_cut(type, slash + 1, type.len), '/') >= 0 || type.p[0] == '.')
        return false;   // one media/subtype: no "..", so nothing outside mime/ is read
    ArenaMark mark = arena_mark(scratch);
    const char *dirs[MIME_DIRS_MAX];
    isize nd = data_dirs(scratch, dirs, MIME_DIRS_MAX);
    for (isize k = 0; k < nd && !out[0]; k++) {
        Str x = read_file(scratch, arena_sprintf(scratch, "%s/mime/%.*s.xml", dirs[k], STR_ARG(type)));
        char *at = x.len ? strstr(arena_cstr(scratch, x), "<comment>") : NULL;
        if (!at) continue;
        at += strlen("<comment>");
        char *end = strstr(at, "</comment>");
        if (!end) continue;
        size_t n = 0;
        static const struct { const char *e; char c; } ents[] = { { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' } };
        while (at < end && n + 1 < cap) {
            int which = -1;
            for (int e = 0; *at == '&' && e < (int)COUNT(ents); e++)
                if (strncmp(at, ents[e].e, strlen(ents[e].e)) == 0) which = e;
            if (which >= 0) out[n++] = ents[which].c, at += strlen(ents[which].e);
            else out[n++] = *at++;
        }
        out[n] = 0;
    }
    arena_restore(scratch, mark);
    return out[0] != 0;
}
