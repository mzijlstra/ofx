// The sidebar's places (docs/plan.md, F2 "Sidebar", F6 "Places mode"), in two sections, each
// under a heading row: Places, with Home first, the GTK bookmarks (shared with Nemo) and Trash
// last; Drives, with `/` and what is mounted under /mnt, /media or /run/media, each with its size
// and free space. A path appears once in each section, at its first place in it. Also the XDG user
// directories, for their icons, and a GTK bookmarks line's parts (Edit Bookmarks' too).

#include "../base/base.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/statvfs.h>

enum { PLACES_MAX = 64 };

// The trash view's folder name: no real path, so nothing that writes into a folder can take it for
// one (gio and Nemo name it the same).
#define TRASH_URI "trash:///"

// PL_UNMOUNTED: a removable drive not mounted yet (2026-10-30), its path and dev the device's
// (/dev/sdb1); a click mounts it. Not a drive for place_is_drive: it has no folder yet.
// The Network section (F16 step 5): PL_SHARE, a share gvfs has mounted, its path the share's folder
// under gvfs's FUSE mount; PL_NETBM, an smb:// or nfs:// bookmark (step 6), its path the address (a click connects);
// PL_SERVER, an SMB server avahi found on the network (step 7b), its path its address, smb://host
// (a click lists its shares).
// Neither is a drive: their space isn't asked (statvfs would wait on a share that doesn't answer).
typedef enum { PL_HEAD, PL_HOME, PL_BOOKMARK, PL_TRASH, PL_ROOT, PL_MOUNT, PL_REMOVABLE, PL_UNMOUNTED, PL_SHARE, PL_NETBM, PL_SERVER } PlaceKind;

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

// Whether a place has an unmount button: a removable drive (udisksctl), or a share (gio mount -u).
static bool place_unmountable(const Place *p)
{
    return place_ejectable(p) || p->kind == PL_SHARE;
}

static bool place_is_net(PlaceKind k)
{
    return k == PL_SHARE || k == PL_NETBM || k == PL_SERVER;
}

// Whether a click on a place connects to its address (a network bookmark, a server found): it has
// no folder until then.
static bool place_connects(PlaceKind k)
{
    return k == PL_NETBM || k == PL_SERVER;
}

// Whether path is a place already in its section (a drive among the drives, a share or network
// bookmark among those, else among the rest).
static bool places_has(const Places *pl, PlaceKind kind, const char *path)
{
    for (int i = 0; i < pl->n; i++)
        if (place_is_drive(pl->p[i].kind) == place_is_drive(kind) && place_is_net(pl->p[i].kind) == place_is_net(kind) && strcmp(pl->p[i].path, path) == 0) return true;
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

// A GTK bookmarks line, `<uri> [label]` (trimmed): its URI and label, and the URI's path when it is
// a local file:// one (false otherwise), a trailing slash dropped but not from "/" itself. The
// sidebar's places and Edit Bookmarks read each line so (the F15 code-health review: written twice).
static bool gtk_bookmark_line(Str line, Str *uri, Str *label, char *path, size_t cap)
{
    isize sp = str_find(line, ' ');
    *uri = sp < 0 ? line : str_cut(line, 0, sp), *label = sp < 0 ? STR_EMPTY : str_trim(str_cut(line, sp + 1, line.len));
    if (!file_uri_path(*uri, path, cap)) return path[0] = 0, false;
    for (size_t n = strlen(path); n > 1 && path[n - 1] == '/';) path[--n] = 0;
    return true;
}

// `file:///path Label` lines (the label optional); other schemes are skipped.
static void places_add_bookmarks(Places *pl, Str text)
{
    isize at = 0;
    for (Str line; next_line(text, &at, &line);) {
        Str uri, label;
        char path[PATH_MAX];
        if (gtk_bookmark_line(str_trim(line), &uri, &label, path, sizeof path)) places_add(pl, PL_BOOKMARK, path, label);
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
        Str key, val;
        if (!line.len || line.p[0] == '#' || !kv_split(line, &key, &val) || !key.len) continue;
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

// ---- unmounted drives (2026-10-30) ----------------------------------------------------------
// The kernel lists the block devices in /sys/class/block (a partition's folder inside its disk's,
// so `<part>/../removable` is the disk's); udev's database, /run/udev/data/b<major>:<minor>, has
// each one's file system (ID_FS_USAGE, ID_FS_TYPE, ID_FS_LABEL_ENC), its bus, and UDISKS_IGNORE.
// No program runs. gui/ passes test folders for both (OFX_TEST_SYSBLOCK, OFX_TEST_UDEV).

// A udev database line's value, "E:KEY=value"; STR_EMPTY when the key isn't there.
static Str udev_get(Str data, const char *key)
{
    isize at = 0, kn = (isize)strlen(key);
    for (Str line; next_line(data, &at, &line);)
        if (line.len > kn + 3 && memcmp(line.p, "E:", 2) == 0 && memcmp(line.p + 2, key, (size_t)kn) == 0 && line.p[2 + kn] == '=')
            return str_cut(line, kn + 3, line.len);
    return STR_EMPTY;
}

// udev's \xNN escapes (a label's spaces, its slashes), into out; false when it doesn't fit.
static bool udev_unescape(Str s, char *out, size_t cap)
{
    size_t o = 0;
    for (isize i = 0; i < s.len; i++) {
        char c = s.p[i];
        int hi, lo;
        if (c == '\\' && i + 3 < s.len && s.p[i + 1] == 'x' && (hi = hex_digit(s.p[i + 2])) >= 0 && (lo = hex_digit(s.p[i + 3])) >= 0)
            c = (char)(hi * 16 + lo), i += 3;
        if (o + 1 >= cap) return false;
        out[o++] = c;
    }
    out[o] = 0;
    return true;
}

// A small sysfs file's first line, trimmed; STR_EMPTY when it can't be read. Read here, not by
// read_file: sysfs says every file is 4096 bytes long, and read_file takes a shorter read for a
// file that changed, and gives nothing (2026-10-30: every drive's dev and size came back empty on
// Michael's machine; the tests' own files have their true sizes).
static Str sys_line(Arena *a, const char *dir, const char *name, const char *file)
{
    char path[PATH_MAX], buf[256];
    if (snprintf(path, sizeof path, "%s/%s/%s", dir, name, file) >= (int)sizeof path) return STR_EMPTY;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return STR_EMPTY;
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return STR_EMPTY;
    isize at = 0;
    Str line = STR_EMPTY;
    next_line((Str){ buf, n }, &at, &line);
    line = str_trim(line);
    return (Str){ arena_cstr(a, line), line.len };
}

// Whether a folder has nothing in it but . and .. (or isn't there).
static bool dir_empty(const char *path)
{
    DIR *d = opendir(path);
    if (!d) return true;
    struct dirent *e;
    bool empty = true;
    while (empty && (e = readdir(d))) empty = strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0;
    closedir(d);
    return empty;
}

// Whether dev is the source of any mount in the mountinfo text (all of them, shown or not: /boot).
static bool mounted_dev(Str mountinfo, const char *dev)
{
    isize at = 0;
    for (Str line; next_line(mountinfo, &at, &line);) {
        char mp[PATH_MAX], src[256];
        Str type, source;
        if (mountinfo_line(line, mp, sizeof mp, &type, &source) && octal_decode(source, src, sizeof src) && strcmp(src, dev) == 0)
            return true;
    }
    return false;
}

// The drives a click could mount: a file system on it (not an encrypted one: unlocking asks a
// passphrase, later), not empty, not mounted, nothing on top of it (a device mapper's), not one
// udisks is told to ignore, and removable or on USB (no unmounted internal partitions). Named by
// their label, else their size.
static void places_add_unmounted(Arena *scratch, Places *pl, const char *sysblock, const char *udev, Str mountinfo)
{
    DIR *d = opendir(sysblock);
    if (!d) return;
    enum { NAMES_MAX = 256 };   // (loop, zram and dm devices too; only the names are kept here)
    static char names[NAMES_MAX][64];
    int nn = 0;
    // Never a drive to mount, and there may be hundreds (loop devices): left out before the count,
    // so they can't crowd out the drives (review, 2026-10-30: 300 loops hid all four).
    static const char *const NOT[] = { "loop", "ram", "zram", "dm-", "md", "nbd" };
    for (struct dirent *e; nn < NAMES_MAX && (e = readdir(d));) {
        size_t len = strlen(e->d_name);
        bool skip = e->d_name[0] == '.' || len >= sizeof names[0];
        for (int k = 0; k < COUNT(NOT) && !skip; k++) skip = strncmp(e->d_name, NOT[k], strlen(NOT[k])) == 0;
        if (!skip) memcpy(names[nn++], e->d_name, len + 1);
    }
    closedir(d);
    qsort(names, (size_t)nn, sizeof names[0], (int (*)(const void *, const void *))strcmp);   // sda1 before sdb1
    for (int i = 0; i < nn && pl->n < PLACES_MAX; i++) {
        ArenaMark mark = arena_mark(scratch);
        const char *name = names[i];
        Str devno = sys_line(scratch, sysblock, name, "dev"), sectors = sys_line(scratch, sysblock, name, "size");
        char path[PATH_MAX], dev[256];
        snprintf(dev, sizeof dev, "/dev/%s", name);
        i64 size = sectors.len ? strtoll(arena_cstr(scratch, sectors), NULL, 10) * 512 : 0;
        Str data = STR_EMPTY;
        if (devno.len && snprintf(path, sizeof path, "%s/b%.*s", udev, STR_ARG(devno)) < (int)sizeof path) data = read_file(scratch, path);
        snprintf(path, sizeof path, "%s/%s/holders", sysblock, name);
        bool removable = str_eq(sys_line(scratch, sysblock, name, "removable"), S("1")) || str_eq(sys_line(scratch, sysblock, name, "../removable"), S("1")) ||
                         str_eq(udev_get(data, "ID_BUS"), S("usb"));
        bool wanted = size > 0 && str_eq(udev_get(data, "ID_FS_USAGE"), S("filesystem")) && !str_eq(udev_get(data, "UDISKS_IGNORE"), S("1")) &&
                      removable && dir_empty(path) && !mounted_dev(mountinfo, dev);
        char label[256] = "";
        if (wanted && !udev_unescape(udev_get(data, "ID_FS_LABEL_ENC"), label, sizeof label)) label[0] = 0;
        arena_restore(scratch, mark);
        if (!wanted) continue;
        if (!label[0]) snprintf(label, sizeof label, size >= 10000000000 ? "%.0f GB Drive" : "%.1f GB Drive", (double)size / 1e9);
        places_add(pl, PL_UNMOUNTED, dev, str_from_c(label));
        Place *p = &pl->p[pl->n - 1];
        if (p->kind == PL_UNMOUNTED && strcmp(p->path, dev) == 0) snprintf(p->dev, sizeof p->dev, "%s", dev), p->size = size;
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

// The Places section (Home, the bookmarks file, Trash) and the Drives section (the mountinfo
// file), each under its heading; scratch holds only the files' text. No drive's space yet. The
// places first, as in Nemo and the GTK file chooser: the drives went on top for a day and came
// back down (Michael, 2026-10-30: the places are where he goes most, and drives that come and go
// change the rows below his bookmarks, not above them).
// The drives a click could mount come last (sysblock and udev: NULL for none).
static void places_load(Arena *scratch, Places *pl, const char *bookmarks, const char *mountinfo, const char *sysblock, const char *udev)
{
    pl->n = 0;
    ArenaMark mark = arena_mark(scratch);
    places_head(pl, "Places");
    const char *home = getenv("HOME");
    if (home && home[0]) places_add(pl, PL_HOME, home, S("Home"));
    places_add_bookmarks(pl, read_file(scratch, bookmarks));
    places_add(pl, PL_TRASH, TRASH_URI, S("Trash"));
    places_head(pl, "Drives");
    Str mi = read_file(scratch, mountinfo);
    places_add_mounts(pl, mi);
    if (sysblock && udev) places_add_unmounted(scratch, pl, sysblock, udev, mi);
    arena_restore(scratch, mark);
}

// The Network section (F16 step 5), after the rest, under its heading when it has a row: the shares
// gvfs has mounted (shares: their folders, one a line, as the watch thread lists gvfs's FUSE
// folders), then the smb:// and nfs:// lines of the bookmarks file, with their labels (else the share's name
// as gvfs gives it), then the SMB servers avahi found (servers, n of them; step 7b), but for one a
// bookmark has (by its address: the bookmark's label stays). A bookmark of a mounted share stays a
// bookmark: a click on it connects, which for a share mounted already goes straight there.
static void places_load_net(Arena *scratch, Places *pl, const char *bookmarks, Str shares, const NetServer *servers, int n)
{
    int head = pl->n;
    places_head(pl, "Network");
    isize at = 0;
    for (Str line; next_line(shares, &at, &line);) {
        if (line.len == 0 || line.len >= PATH_MAX || line.p[0] != '/') continue;
        char path[PATH_MAX], label[256];
        snprintf(path, sizeof path, "%.*s", STR_ARG(line));
        gvfs_share_label(path_base(line), label, sizeof label);
        places_add(pl, PL_SHARE, path, str_from_c(label));
    }
    ArenaMark mark = arena_mark(scratch);
    Str text = read_file(scratch, bookmarks);
    at = 0;
    for (Str line; next_line(text, &at, &line);) {
        Str uri, label;
        char path[PATH_MAX], u[PATH_MAX], host[256], share[256], why[256], name[520];
        gtk_bookmark_line(str_trim(line), &uri, &label, path, sizeof path);   // (a local one isn't an address: Places has it)
        if (connect_uri(uri, u, sizeof u, host, sizeof host, share, sizeof share, why, sizeof why) != CONNECT_OK) continue;
        if (label.len == 0) snprintf(name, sizeof name, share[0] ? "%s on %s" : "%s%s", share, host), label = str_from_c(name);   // (a server's own address, step 7a: its name)
        places_add(pl, PL_NETBM, u, label);
    }
    arena_restore(scratch, mark);
    for (int i = 0; i < n; i++) places_add(pl, PL_SERVER, servers[i].addr, str_from_c(servers[i].name));
    if (pl->n == head + 1) pl->n = head;   // (nothing in it: no heading)
}

// $XDG_CONFIG_HOME/gtk-3.0/bookmarks, ~/.config by default; into buf.
static const char *bookmarks_path(char *buf, size_t cap)
{
    if (!xdg_dir(buf, cap, "XDG_CONFIG_HOME", ".config", "gtk-3.0/bookmarks")) buf[0] = 0;
    return buf;
}

// The place holding path: the longest one that is path or a folder above it, the first of two the
// same (a bookmark before its drive: the places are listed first; two of a kind at one folder are
// one place, places_has); Trash for the trash view; -1 for none.
static int places_current(const Places *pl, const char *path)
{
    int best = -1;
    size_t best_n = 0;
    for (int i = 0; i < pl->n; i++) {
        if (pl->p[i].kind == PL_TRASH && strcmp(path, TRASH_URI) == 0) return i;
        if (pl->p[i].kind == PL_HEAD || pl->p[i].kind == PL_TRASH || pl->p[i].kind == PL_UNMOUNTED || place_connects(pl->p[i].kind)) continue;
        const char *p = pl->p[i].path;
        size_t n = strlen(p);
        if (path_within(path, p) && (best < 0 || n > best_n)) best = i, best_n = n;
    }
    return best;
}

// The icon names for a place, most specific first (at most 3).
static isize place_icon_names(const Place *p, const UserDirs *u, bool trash_full, Str *names)
{
    isize n = 0;
    switch (p->kind) {
    case PL_ROOT:
    case PL_MOUNT: names[n++] = S("drive-harddisk"); break;
    case PL_REMOVABLE:
    case PL_UNMOUNTED: names[n++] = S("drive-removable-media"), names[n++] = S("drive-harddisk"); break;
    case PL_SHARE:
    case PL_NETBM: names[n++] = S("folder-remote"); break;
    case PL_SERVER: names[n++] = S("network-server"), names[n++] = S("folder-remote"); break;
    case PL_TRASH:   // full: when any trash folder holds something (2026-10-30)
        if (trash_full) names[n++] = S("user-trash-full");
        names[n++] = S("user-trash");
        break;
    default: {
        Str special = user_dir_icon(u, p->path);
        if (special.len) names[n++] = special;
        break;
    }
    }
    names[n++] = S("folder");
    return n;
}
