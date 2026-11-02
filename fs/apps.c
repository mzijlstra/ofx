// The default app for a MIME type, as the freedesktop MIME apps spec has it (F7.1.1, docs/plan.md):
// gio types an empty file by its content, application/x-zerosize, for which nothing is set, so
// `gio open` refuses it; ofx finds the app for the type its name gives and launches that
// (Michael, 2026-10-25). F7.3's Open With reads the same lists, and Set as default (F7.3.1, at the
// end) writes the user's.
//
// The lists (read with icons.c's ini_get), most preferred first: in $XDG_CONFIG_HOME, each $XDG_CONFIG_DIRS, then each data dir's
// applications/ ($XDG_DATA_HOME first), `<desktop>-mimeapps.list` (each of $XDG_CURRENT_DESKTOP,
// lower case) before `mimeapps.list`. A type's default is the first installed app its
// [Default Applications] names, in the first list that names one; else the first installed app
// its [Added Associations] name, then each data dir's applications/mimeinfo.cache, leaving out
// what [Removed Associations] removes; a list may name the type by an alias; an app that isn't
// Type=Application, is Hidden, or lacks its TryExec isn't there. The type's parents come after it
// (subclasses, as gio does; and every text/* under text/plain, which gio doesn't when the
// subclasses don't say so: text/x-awk and the like get the text editor here, none from gio,
// the review's comparison over 1,619 types, 2026-10-25).

#include "../base/base.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
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

// Whether the desktop file text is an app that is there: Type=Application, not Hidden (a user's copy saying so
// deletes the app, as ~/.local/share/applications/java-java-openjdk.desktop does here), and its
// TryExec, when it has one, a program that is installed.
static bool apps_usable(Str text)
{
    if (!str_eq(ini_get(text, S("Desktop Entry"), S("Type")), S("Application"))) return false;   // a Link isn't (gio skips it)
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

// ---- every app, for Open With (F7.3) ------------------------------------------------------------

enum { APPS_MAX = 1024, APPS_DEPTH_MAX = 3 };

typedef enum { AS_DEFAULT, AS_RECOMMENDED, AS_RELATED, AS_OTHER, AS_NONE } AppSection;

typedef struct {
    Str        id;          // its desktop id, "org.gnome.TextEditor.desktop" (a folder's dashes: kde-foo.desktop)
    Str        path;        // its file
    Str        name, icon;  // Name= and Icon= (an icon name, or a file's path)
    Str        mimes;       // MimeType=, ';'-separated
    bool       nodisplay;   // NoDisplay=true: a helper, shown only where it names the type
    AppSection section;     // for the type apps_rank was given
} App;

typedef struct {
    App  *a;
    isize n;
    bool  cut;   // more than APPS_MAX
} Apps;

// The apps in dir (applications/ of a data dir, or a folder in it, its id's prefix pre), an id
// that an earlier data dir has kept by that one (the desktop entry spec: the first is the app, even
// when it isn't usable, as a user's Hidden copy).
static void apps_scan(Arena *a, Apps *out, const char *dir, const char *pre, int depth, Str *seen, isize *nseen)
{
    DIR *d = opendir(dir);
    if (!d) return;
    for (struct dirent *e; (e = readdir(d));) {
        if (e->d_name[0] == '.') continue;
        char *path = arena_sprintf(a, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < APPS_DEPTH_MAX) apps_scan(a, out, path, arena_sprintf(a, "%s%s-", pre, e->d_name), depth + 1, seen, nseen);
            continue;
        }
        Str id = str_from_c(arena_sprintf(a, "%s%s", pre, e->d_name));
        if (!S_ISREG(st.st_mode) || !str_ends_with(id, S(".desktop"))) continue;
        bool dup = false;
        for (isize k = 0; k < *nseen && !dup; k++) dup = str_eq(seen[k], id);
        if (dup) continue;
        if (*nseen < APPS_MAX * 2) seen[(*nseen)++] = id;
        Str text = read_file(a, path);
        if (!apps_usable(text)) continue;
        if (out->n >= APPS_MAX) {
            out->cut = true;
            continue;
        }
        Str name = ini_get(text, S("Desktop Entry"), S("Name"));
        out->a[out->n++] = (App){
            .id = id, .path = str_from_c(path), .name = name.len ? name : id, .icon = ini_get(text, S("Desktop Entry"), S("Icon")),
            .mimes = ini_get(text, S("Desktop Entry"), S("MimeType")),
            .nodisplay = str_eq(ini_get(text, S("Desktop Entry"), S("NoDisplay")), S("true")), .section = AS_OTHER,
        };
    }
    closedir(d);
}

// Every installed app (the data dirs' applications/, the user's first), into out (its slots in a).
static void apps_all(Arena *a, Apps *out)
{
    *out = (Apps){ .a = arena_new(a, App, APPS_MAX) };
    Str *seen = arena_new(a, Str, APPS_MAX * 2);
    isize nseen = 0;
    const char *data[MIME_DIRS_MAX];
    isize nd = data_dirs(a, data, MIME_DIRS_MAX);
    for (isize k = 0; k < nd; k++) apps_scan(a, out, arena_sprintf(a, "%s/applications", data[k]), "", 0, seen, &nseen);
}

static int apps_order(const void *pa, const void *pb)
{
    const App *x = pa, *y = pb;
    if (x->section != y->section) return x->section < y->section ? -1 : 1;
    for (isize i = 0; i < x->name.len && i < y->name.len; i++) {
        int cx = fold((unsigned char)x->name.p[i]), cy = fold((unsigned char)y->name.p[i]);
        if (cx != cy) return cx < cy ? -1 : 1;
    }
    if (x->name.len != y->name.len) return x->name.len < y->name.len ? -1 : 1;
    isize n = x->id.len < y->id.len ? x->id.len : y->id.len;
    int c = memcmp(x->id.p, y->id.p, (size_t)n);
    return c ? c : x->id.len < y->id.len ? -1 : x->id.len > y->id.len;
}

// apps sorted for type: its default app first, then the recommended ones (that name it or an alias
// in MimeType=, or a list names for it; less what a list removes for it), then the related ones
// (the same for its parent types: GTK's chooser lists them apart, Michael and the review,
// 2026-10-26), then the rest by name; a NoDisplay app only when recommended (AS_NONE, at the end,
// otherwise). Returns how many are shown (all but AS_NONE).
static isize apps_rank(Arena *a, Apps *apps, Str type)
{
    ArenaMark mark = arena_mark(a);
    char def[PATH_MAX];
    bool have_def = apps_default(a, type, def, sizeof def);
    const char *lists[APPS_LISTS_MAX];
    isize nl = apps_lists(a, lists, APPS_LISTS_MAX);
    Str text[APPS_LISTS_MAX];
    for (isize k = 0; k < nl; k++) text[k] = read_file(a, lists[k]);
    Str types[APPS_TYPES_MAX], names[APPS_TYPES_MAX * 4];
    Str canon = type;
    apps_names(a, &canon, names, APPS_TYPES_MAX);
    isize nt = apps_types(a, canon, types, APPS_TYPES_MAX), nn = 0, nself = 0;
    for (isize t = 0; t < nt; t++) {
        Str tt = types[t], one[APPS_TYPES_MAX];
        isize k1 = apps_names(a, &tt, one, APPS_TYPES_MAX);
        for (isize j = 0; j < k1 && nn < COUNT(names); j++) names[nn++] = one[j];
        if (t == 0) nself = nn;   // the type and its aliases; the parents' after them
    }
    // For the type itself (scope 0, names[0, nself)) and with its parents (1, all of names): what
    // the lists add or set, and what they remove.
    Str removed[2] = { STR_EMPTY, STR_EMPTY }, listed[2] = { STR_EMPTY, STR_EMPTY };
    for (int sc = 0; sc < 2; sc++) {
        isize upto = sc ? nn : nself;
        for (isize k = 0; k < nl; k++) {
            removed[sc] = apps_removed(a, text[k], names, upto, removed[sc]);
            for (isize j = 0; j < upto; j++) {
                Str add = ini_get(text[k], S("Added Associations"), names[j]), dflt = ini_get(text[k], S("Default Applications"), names[j]);
                listed[sc] = str_from_c(arena_sprintf(a, "%.*s;%.*s;%.*s", STR_ARG(listed[sc]), STR_ARG(add), STR_ARG(dflt)));
            }
        }
    }
    isize shown = 0;
    for (isize i = 0; i < apps->n; i++) {
        App *p = &apps->a[i];
        bool own = apps_listed(listed[0], p->id), related = apps_listed(listed[1], p->id);
        for (isize j = 0; j < nn; j++)
            if (apps_listed(p->mimes, names[j])) own |= j < nself, related = true;
        own &= !apps_listed(removed[0], p->id), related &= !apps_listed(removed[1], p->id);
        p->section = have_def && str_eq(p->path, str_from_c(def)) ? AS_DEFAULT
                   : own                                          ? AS_RECOMMENDED
                   : related && !p->nodisplay                     ? AS_RELATED
                   : p->nodisplay                                 ? AS_NONE
                                                                  : AS_OTHER;
        shown += p->section != AS_NONE;
    }
    arena_restore(a, mark);
    qsort(apps->a, (size_t)apps->n, sizeof *apps->a, apps_order);
    return shown;
}

// ---- the default app for a type (F7.3.1) ------------------------------------------------------

// The user's own mimeapps.list, where a default is set: $XDG_CONFIG_HOME's, else ~/.config's.
static bool apps_user_list(char *out, size_t cap)
{
    return xdg_dir(out, cap, "XDG_CONFIG_HOME", ".config", "mimeapps.list");
}

// A list read before the user's own that sets a default for type (or an alias of it), whose
// setting a change in the user's list wouldn't override (Omarchy's hyprland-mimeapps.list): its
// path into out. False when none does.
static bool apps_shadowed(Arena *a, Str type, char *out, size_t cap)
{
    ArenaMark mark = arena_mark(a);
    char mine[PATH_MAX];
    const char *lists[APPS_LISTS_MAX];
    isize nl = apps_user_list(mine, sizeof mine) ? apps_lists(a, lists, APPS_LISTS_MAX) : 0;
    Str names[APPS_TYPES_MAX];
    isize nn = apps_names(a, &type, names, APPS_TYPES_MAX);
    bool found = false;
    for (isize k = 0; k < nl && !found && strcmp(lists[k], mine) != 0; k++) {
        Str text = read_file(a, lists[k]);
        for (isize j = 0; j < nn && !found; j++)
            if (ini_get(text, S("Default Applications"), names[j]).len) found = true, snprintf(out, cap, "%s", lists[k]);
    }
    arena_restore(a, mark);
    return found;
}

// The first app the user's list sets as type's default, into out ("" none).
static void apps_user_default(Arena *a, const char *list, Str type, char *out, size_t cap)
{
    ArenaMark mark = arena_mark(a);
    Str v = ini_get(read_file(a, list), S("Default Applications"), type);
    isize semi = str_find(v, ';');
    if (semi >= 0) v = str_cut(v, 0, semi);
    snprintf(out, cap, "%.*s", STR_ARG(str_trim(v)));
    arena_restore(a, mark);
}

// app (a desktop id) made type's default in the user's list, by `gio mime` (what Nemo's "Set as
// default" does: GLib writes [Default Applications] and adds it to [Added Associations]). No undo
// (Michael, 2026-10-26). False with the reason when the guard refuses it, gio doesn't run, or the
// list doesn't say so after.
static bool op_set_default(Arena *a, Str type, const char *app, Err *err)
{
    // The canonical name, not an alias gio info may give (.p12, .p7b): a list's canonical key would
    // win over the alias's, so setting the alias would do nothing while the check said done
    // (F7.3.1 review).
    Str names[APPS_TYPES_MAX];
    apps_names(a, &type, names, APPS_TYPES_MAX);
    char list[PATH_MAX];
    if (!apps_user_list(list, sizeof list)) return err_set(err, "no home folder to keep the default in"), false;
    if (!ops_allowed(list, err)) return false;
    char t[256], out[1024];
    snprintf(t, sizeof t, "%.*s", STR_ARG(type));
    char *argv[] = { "gio", "mime", "--", t, (char *)app, NULL };
    bool late = false;
    if (!spawn_capture(NULL, argv, out, sizeof out, 3000, &late, err)) return false;
    char now[NAME_MAX + 1];
    apps_user_default(a, list, type, now, sizeof now);
    if (late || strcmp(now, app) != 0) {
        out[strcspn(out, "\n")] = 0;
        return err_set(err, "%s", out[0] ? out : "gio didn't set it"), false;
    }
    return true;
}
