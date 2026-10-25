// The default app for a MIME type, as the freedesktop MIME apps spec has it (F7.1.1, docs/plan.md):
// gio types an empty file by its content, application/x-zerosize, for which nothing is set, so
// `gio open` refuses it; ofx finds the app for the type its name gives and launches that
// (Michael, 2026-10-25). F7.3's Open With reads the same lists.
//
// The lists (read with icons.c's ini_get), most preferred first: in $XDG_CONFIG_HOME, each $XDG_CONFIG_DIRS, then each data dir's
// applications/ ($XDG_DATA_HOME first), `<desktop>-mimeapps.list` (each of $XDG_CURRENT_DESKTOP,
// lower case) before `mimeapps.list`. A type's default is the first installed app its
// [Default Applications] names, in the first list that names one; else the first installed app
// its [Added Associations] name, then each data dir's applications/mimeinfo.cache, leaving out
// what [Removed Associations] removes; a list may name the type by an alias; an app Hidden or without its TryExec isn't there. The type's
// parents come after it (subclasses, and every
// text/* under text/plain), as gio does.

#include "../base/base.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

enum { APPS_LISTS_MAX = 64, APPS_TYPES_MAX = 8 };

// The mimeapps.list files, most preferred first, into a.
static isize apps_lists(Arena *a, const char **out, isize max)
{
    const char *dirs[MIME_DIRS_MAX * 2 + 2];
    isize nd = 0;
    const char *home = getenv("HOME"), *ch = getenv("XDG_CONFIG_HOME"), *cd = getenv("XDG_CONFIG_DIRS");
    if (ch && ch[0] == '/') dirs[nd++] = arena_cstr(a, str_from_c(ch));
    else if (home && home[0]) dirs[nd++] = arena_sprintf(a, "%s/.config", home);
    Str cds = str_from_c(cd && cd[0] ? cd : "/etc/xdg");
    for (isize at = 0; at < cds.len && nd < MIME_DIRS_MAX;) {
        isize e = at;
        while (e < cds.len && cds.p[e] != ':') e++;
        if (e > at) dirs[nd++] = arena_cstr(a, str_cut(cds, at, e));
        at = e + 1;
    }
    const char *data[MIME_DIRS_MAX];
    isize ndata = data_dirs(a, data, MIME_DIRS_MAX);
    for (isize k = 0; k < ndata; k++) dirs[nd++] = arena_sprintf(a, "%s/applications", data[k]);
    const char *desk = getenv("XDG_CURRENT_DESKTOP");
    Str ds = str_from_c(desk ? desk : "");
    isize n = 0;
    for (isize k = 0; k < nd; k++) {
        for (isize at = 0; at < ds.len && n < max;) {
            isize e = at;
            while (e < ds.len && ds.p[e] != ':') e++;
            if (e > at) out[n++] = arena_sprintf(a, "%s/%.*s-mimeapps.list", dirs[k], STR_ARG(lower(a, str_cut(ds, at, e))));
            at = e + 1;
        }
        if (n < max) out[n++] = arena_sprintf(a, "%s/mimeapps.list", dirs[k]);
    }
    return n;
}

// Whether id is in a ';'-separated list.
static bool apps_listed(Str list, Str id)
{
    for (isize at = 0; at < list.len;) {
        isize e = at;
        while (e < list.len && list.p[e] != ';') e++;
        if (str_eq(str_trim(str_cut(list, at, e)), id)) return true;
        at = e + 1;
    }
    return false;
}

// Whether the desktop file text is an app that is there: not Hidden (a user's copy saying so
// deletes the app, as ~/.local/share/applications/java-java-openjdk.desktop does here), and its
// TryExec, when it has one, a program that is installed.
static bool apps_usable(Str text)
{
    if (str_eq(ini_get(text, S("Desktop Entry"), S("Hidden")), S("true"))) return false;
    Str t = ini_get(text, S("Desktop Entry"), S("TryExec"));
    if (t.len == 0) return true;
    char prog[PATH_MAX];
    snprintf(prog, sizeof prog, "%.*s", STR_ARG(t));
    return prog[0] == '/' ? access(prog, X_OK) == 0 : on_path(prog);
}

// The file of desktop id, into out: applications/<id> in each data dir, or with its first dashes
// as folders ("kde-foo.desktop" is kde/foo.desktop), as the desktop entry spec has it. The first
// found is the app; false when it isn't usable (apps_usable): a later one doesn't count.
static bool apps_find(Arena *a, Str id, char *out, size_t cap)
{
    if (id.len == 0 || str_find(id, '/') >= 0 || !str_ends_with(id, S(".desktop"))) return false;
    const char *data[MIME_DIRS_MAX];
    ArenaMark mark = arena_mark(a);
    isize nd = data_dirs(a, data, MIME_DIRS_MAX);
    bool ok = false;
    for (isize k = 0; k < nd && !ok; k++) {
        char rel[NAME_MAX + 1];
        snprintf(rel, sizeof rel, "%.*s", STR_ARG(id));
        for (char *dash = rel;; dash++) {
            if (snprintf(out, cap, "%s/applications/%s", data[k], rel) < (int)cap && access(out, R_OK) == 0) {
                ok = true;
                break;
            }
            dash = strchr(dash, '-');
            if (!dash) break;
            *dash = '/';
        }
    }
    if (ok) ok = apps_usable(read_file(a, out));
    arena_restore(a, mark);
    return ok;
}

// The first id of a ';'-separated list that is installed and isn't in removed, its file into out.
static bool apps_first(Arena *a, Str list, Str removed, char *out, size_t cap)
{
    for (isize at = 0; at < list.len;) {
        isize e = at;
        while (e < list.len && list.p[e] != ';') e++;
        Str id = str_trim(str_cut(list, at, e));
        if (id.len && !apps_listed(removed, id) && apps_find(a, id, out, cap)) return true;
        at = e + 1;
    }
    return false;
}

// type and its parents, most specific first, into out: subclasses from each data dir's
// mime/subclasses, and text/plain under every text/* (the shared MIME info spec).
static isize apps_types(Arena *a, Str type, Str *out, isize max)
{
    isize n = 0;
    out[n++] = type;
    const char *data[MIME_DIRS_MAX];
    isize nd = data_dirs(a, data, MIME_DIRS_MAX);
    Str sub[MIME_DIRS_MAX];
    for (isize k = 0; k < nd; k++) sub[k] = read_file(a, arena_sprintf(a, "%s/mime/subclasses", data[k]));
    for (isize i = 0; i < n && n < max; i++) {
        for (isize k = 0; k < nd && n < max; k++) {
            isize at = 0;
            for (Str line; n < max && next_line(sub[k], &at, &line);) {
                isize sp = str_find(line, ' ');
                if (sp < 0 || !str_eq(str_cut(line, 0, sp), out[i])) continue;
                Str parent = str_trim(str_cut(line, sp + 1, line.len));
                bool have = false;
                for (isize j = 0; j < n; j++) have |= str_eq(out[j], parent);
                if (!have && parent.len) out[n++] = parent;
            }
        }
    }
    bool plain = false;
    for (isize j = 0; j < n; j++) plain |= str_eq(out[j], S("text/plain"));
    if (!plain && n < max && str_starts_with(type, S("text/"))) out[n++] = S("text/plain");
    return n;
}

// The names a list may use for type: itself and its aliases (each data dir's mime/aliases, lines
// `alias canonical`), into out; a type that is itself an alias is first made canonical (*type).
static isize apps_names(Arena *a, Str *type, Str *out, isize max)
{
    const char *data[MIME_DIRS_MAX];
    isize nd = data_dirs(a, data, MIME_DIRS_MAX), n = 0;
    Str al[MIME_DIRS_MAX];
    for (isize k = 0; k < nd; k++) al[k] = read_file(a, arena_sprintf(a, "%s/mime/aliases", data[k]));
    for (int pass = 0; pass < 2; pass++)
        for (isize k = 0; k < nd; k++) {
            isize at = 0;
            for (Str line; next_line(al[k], &at, &line);) {
                isize sp = str_find(line, ' ');
                if (sp <= 0) continue;
                Str alias = str_cut(line, 0, sp), canon = str_trim(str_cut(line, sp + 1, line.len));
                if (pass == 0 && str_eq(alias, *type)) *type = canon;
                if (pass == 1 && str_eq(canon, *type) && n < max - 1) out[1 + n++] = alias;
            }
        }
    out[0] = *type;
    return n + 1;
}

// The first installed app any of names lists in [group] of text, less removed.
static bool apps_first_of(Arena *a, Str text, Str group, const Str *names, isize nn, Str removed, char *out, size_t cap)
{
    for (isize j = 0; j < nn; j++)
        if (apps_first(a, ini_get(text, group, names[j]), removed, out, cap)) return true;
    return false;
}

// What [Removed Associations] of text removes for any of names, after removed.
static Str apps_removed(Arena *a, Str text, const Str *names, isize nn, Str removed)
{
    for (isize j = 0; j < nn; j++) {
        Str r = ini_get(text, S("Removed Associations"), names[j]);
        if (r.len) removed = str_from_c(arena_sprintf(a, "%.*s;%.*s", STR_ARG(removed), STR_ARG(r)));
    }
    return removed;
}

// The default app for type: its desktop file into out; false when there is none.
static bool apps_default(Arena *a, Str type, char *out, size_t cap)
{
    ArenaMark mark = arena_mark(a);
    const char *lists[APPS_LISTS_MAX];
    isize nl = apps_lists(a, lists, APPS_LISTS_MAX);
    Str text[APPS_LISTS_MAX];
    for (isize k = 0; k < nl; k++) text[k] = read_file(a, lists[k]);
    Str types[APPS_TYPES_MAX], names[APPS_TYPES_MAX * 2];
    apps_names(a, &type, names, COUNT(names));   // canonical first
    isize nt = apps_types(a, type, types, APPS_TYPES_MAX);
    bool ok = false;
    // Each type in turn, its parents after it, as gio: its defaults, then its associations (added
    // ones, each list's, less what it and lists before it remove; then each data dir's cache, less
    // what any list removes).
    const char *data[MIME_DIRS_MAX];
    isize nd = data_dirs(a, data, MIME_DIRS_MAX);
    for (isize t = 0; t < nt && !ok; t++) {
        Str tt = types[t];
        isize nn = apps_names(a, &tt, names, COUNT(names));
        for (isize k = 0; k < nl && !ok; k++) ok = apps_first_of(a, text[k], S("Default Applications"), names, nn, STR_EMPTY, out, cap);
        Str removed = STR_EMPTY;
        for (isize k = 0; k < nl && !ok; k++) {
            removed = apps_removed(a, text[k], names, nn, removed);
            ok = apps_first_of(a, text[k], S("Added Associations"), names, nn, removed, out, cap);
        }
        for (isize k = 0; k < nd && !ok; k++) {
            Str cache = read_file(a, arena_sprintf(a, "%s/applications/mimeinfo.cache", data[k]));
            ok = apps_first_of(a, cache, S("MIME Cache"), names, nn, removed, out, cap);
        }
    }
    arena_restore(a, mark);
    return ok;
}
