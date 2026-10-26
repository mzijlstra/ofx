// org.freedesktop.FileManager1 (Michael, 2026-10-26): what its calls ask of ofx. A browser's
// "Show in folder" calls ShowItems with the file's URI; xdg-open and others call ShowFolders.
// tools/fm1.c answers on the session bus; here the URIs become the paths ofx is started on, one
// window each: a folder as it is (ShowFolders), an item with --select, so ofx opens its folder
// with it selected, a folder too (ShowItems, and ShowItemProperties for now), one window a folder however many of
// its items are asked for. Only local file:// URIs, and only paths that are there; FM1_MAX
// windows a call at most.

#include "../base/base.h"

#include <limits.h>
#include <string.h>
#include <sys/stat.h>

typedef enum { FM1_FOLDERS, FM1_ITEMS, FM1_PROPS } Fm1Call;

enum { FM1_MAX = 16 };   // windows one call opens; the rest are skipped

typedef struct {
    isize n;
    char  path[FM1_MAX][PATH_MAX];
    isize skipped;   // a URI not local, not there, or past FM1_MAX
} Fm1Plan;

// p's folder: up to its last '/', "/" for one at the top.
static Str fm1_parent(const char *p)
{
    const char *s = strrchr(p, '/');
    return (Str){ p, s && s > p ? s - p : 1 };
}

static void fm1_plan(Fm1Call c, const char *const *uris, isize n, Fm1Plan *out)
{
    out->n = 0, out->skipped = 0;
    for (isize i = 0; i < n; i++) {
        char path[PATH_MAX];
        struct stat st;
        if (!clean_uri_path(uris[i], path, sizeof path) || stat(path, &st) != 0) {
            out->skipped++;
            continue;
        }
        for (size_t k = strlen(path); k > 1 && path[k - 1] == '/'; k--) path[k - 1] = 0;   // a/b/ is a/b
        // Already one window for it: the same folder, or (an item) the same folder above.
        Str key = c == FM1_FOLDERS ? str_from_c(path) : fm1_parent(path);
        bool seen = false;
        for (isize k = 0; k < out->n && !seen; k++)
            seen = str_eq(c == FM1_FOLDERS ? str_from_c(out->path[k]) : fm1_parent(out->path[k]), key);
        if (seen) continue;
        if (out->n == FM1_MAX) {
            out->skipped++;
            continue;
        }
        memcpy(out->path[out->n++], path, strlen(path) + 1);
    }
}
