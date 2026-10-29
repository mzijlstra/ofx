// The sidebar's places (docs/plan.md, F2 "Sidebar", F6 "Places mode"), in two sections, each
// under a heading row: Places, with Home first, the GTK bookmarks (shared with Nemo) and Trash
// last; Drives, with `/` and what is mounted under /mnt, /media or /run/media, each with its size
// and free space. A path appears once in each section, at its first place in it.

#include "../base/base.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/statvfs.h>

enum { PLACES_MAX = 64 };

// The trash view's folder name: no real path, so nothing that writes into a folder can take it for
// one (gio and Nemo name it the same).
#define TRASH_URI "trash:///"

typedef enum { PL_HEAD, PL_HOME, PL_BOOKMARK, PL_TRASH, PL_ROOT, PL_MOUNT, PL_REMOVABLE } PlaceKind;

typedef struct {
    char      path[PATH_MAX];   // "" for a heading
    char      label[256];
    PlaceKind kind;
    i64       size, avail;      // a drive's, in bytes (gui/space.c reads them); size 0 when unknown
    char      dev[256];         // a drive's source in mountinfo (/dev/sdd1), "" when it has none
} Place;

typedef struct {
    Place p[PLACES_MAX];
    int   n;
} Places;

static bool place_is_drive(PlaceKind k)
{
    return k == PL_ROOT || k == PL_MOUNT || k == PL_REMOVABLE;
}

// Whether a place is a drive that can be ejected (F6.1): a removable one, with a device under
// /dev (not a FUSE or network source udisks knows nothing of).
static bool place_ejectable(const Place *p)
{
    return p->kind == PL_REMOVABLE && strncmp(p->dev, "/dev/", 5) == 0;
}

// Whether path is a place already in its section (a drive among the drives, else among the rest).
static bool places_has(const Places *pl, PlaceKind kind, const char *path)
{
    for (int i = 0; i < pl->n; i++)
        if (place_is_drive(pl->p[i].kind) == place_is_drive(kind) && strcmp(pl->p[i].path, path) == 0) return true;
    return false;
}

static void places_head(Places *pl, const char *label)
{
    if (pl->n == PLACES_MAX) return;
    Place *p = &pl->p[pl->n++];
    *p = (Place){ .kind = PL_HEAD };
    snprintf(p->label, sizeof p->label, "%s", label);
}

static void places_add(Places *pl, PlaceKind kind, const char *path, Str label)
{
    if (pl->n == PLACES_MAX || !path[0] || places_has(pl, kind, path)) return;
    Place *p = &pl->p[pl->n++];
    *p = (Place){ .kind = kind };
    snprintf(p->path, sizeof p->path, "%s", path);
    if (label.len == 0) label = strcmp(path, "/") == 0 ? S("/") : path_base(str_from_c(path));
    snprintf(p->label, sizeof p->label, "%.*s", STR_ARG(label));
}

// `file:///path Label` lines (the label optional); other schemes are skipped. A trailing slash is
// dropped, but not from "/" itself.
static void places_add_bookmarks(Places *pl, Str text)
{
    isize at = 0;
    for (Str line; next_line(text, &at, &line);) {
        line = str_trim(line);
        isize sp = str_find(line, ' ');
        Str uri = sp < 0 ? line : str_cut(line, 0, sp), label = sp < 0 ? STR_EMPTY : str_trim(str_cut(line, sp + 1, line.len));
        char path[PATH_MAX];
        if (!file_uri_path(uri, path, sizeof path)) continue;
        for (size_t n = strlen(path); n > 1 && path[n - 1] == '/';) path[--n] = 0;
        places_add(pl, PL_BOOKMARK, path, label);
    }
}

// For the tests: mounts under this folder count as removable drives, as under /run/media (a test
// can't mount a USB stick, nor write under /run/media). gui/ sets it from OFX_TEST_MEDIA.
static char places_test_media[PATH_MAX];


// mountinfo's fifth field is the mount point: `/`, and anything under /mnt, /media or /run/media.
static void places_add_mounts(Places *pl, Str text)
{
    isize at = 0;
    for (Str line; next_line(text, &at, &line);) {
        char mp[PATH_MAX];
        Str type, src;
        if (!mountinfo_line(line, mp, sizeof mp, &type, &src)) continue;
        int before = pl->n;
        if (strcmp(mp, "/") == 0) places_add(pl, PL_ROOT, mp, S("File System"));
        else if (path_below(mp, "/run/media") || path_below(mp, "/media") || (places_test_media[0] && path_below(mp, places_test_media)))
            places_add(pl, PL_REMOVABLE, mp, STR_EMPTY);
        else if (path_below(mp, "/mnt")) places_add(pl, PL_MOUNT, mp, STR_EMPTY);
        if (pl->n == before) continue;
        Place *p = &pl->p[pl->n - 1];
        if (!octal_decode(src, p->dev, sizeof p->dev)) p->dev[0] = 0;
    }
}

// The size and free space (what an unprivileged user may still write) of the drive holding path,
// by statvfs; both 0 when it can't be read. It can block on a hung mount: gui/ calls it off the
// main thread (space.c).
static void fs_space(const char *path, i64 *size, i64 *avail)
{
    struct statvfs v;
    if (statvfs(path, &v) != 0) {
        *size = *avail = 0;
        return;
    }
    *size = (i64)v.f_blocks * (i64)v.f_frsize;
    *avail = (i64)v.f_bavail * (i64)v.f_frsize;
}

// The Drives section (the mountinfo file) and the Places section (Home, the bookmarks file,
// Trash), each under its heading: the drives first, as there are few of them and bookmarks may be
// many (Michael, 2026-10-29: they were pushed down). scratch holds only the files' text. No
// drive's space yet.
static void places_load(Arena *scratch, Places *pl, const char *bookmarks, const char *mountinfo)
{
    pl->n = 0;
    ArenaMark mark = arena_mark(scratch);
    places_head(pl, "Drives");
    places_add_mounts(pl, read_file(scratch, mountinfo));
    places_head(pl, "Places");
    const char *home = getenv("HOME");
    if (home && home[0]) places_add(pl, PL_HOME, home, S("Home"));
    places_add_bookmarks(pl, read_file(scratch, bookmarks));
    places_add(pl, PL_TRASH, TRASH_URI, S("Trash"));
    arena_restore(scratch, mark);
}

// $XDG_CONFIG_HOME/gtk-3.0/bookmarks, ~/.config by default; into buf.
static const char *bookmarks_path(char *buf, size_t cap)
{
    if (!xdg_dir(buf, cap, "XDG_CONFIG_HOME", ".config", "gtk-3.0/bookmarks")) buf[0] = 0;
    return buf;
}

// The place holding path: the longest one that is path or a folder above it, of two the same the
// one that isn't a drive (a bookmark before its drive, though the drives are listed first; two of
// a kind at one folder are one place, places_has); Trash for the trash view; -1 for none.
static int places_current(const Places *pl, const char *path)
{
    int best = -1;
    size_t best_n = 0;
    for (int i = 0; i < pl->n; i++) {
        if (pl->p[i].kind == PL_TRASH && strcmp(path, TRASH_URI) == 0) return i;
        if (pl->p[i].kind == PL_HEAD || pl->p[i].kind == PL_TRASH) continue;
        const char *p = pl->p[i].path;
        size_t n = strlen(p);
        if (path_within(path, p) && (best < 0 || n > best_n || (n == best_n && !place_is_drive(pl->p[i].kind)))) best = i, best_n = n;
    }
    return best;
}

// The icon names for a place, most specific first.
static isize place_icon_names(const Place *p, const UserDirs *u, Str *names)
{
    isize n = 0;
    switch (p->kind) {
    case PL_ROOT:
    case PL_MOUNT: names[n++] = S("drive-harddisk"); break;
    case PL_REMOVABLE: names[n++] = S("drive-removable-media"), names[n++] = S("drive-harddisk"); break;
    case PL_TRASH: names[n++] = S("user-trash"); break;
    default: {
        Str special = user_dir_icon(u, p->path);
        if (special.len) names[n++] = special;
        break;
    }
    }
    names[n++] = S("folder");
    return n;
}
