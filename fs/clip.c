// What Ctrl+C and Ctrl+X put on the system clipboard, and what Ctrl+V reads back from it
// (docs/plan.md, F3 "System clipboard"): the same paths in the forms Nemo, browsers and
// terminals each read. gui/clipboard.c moves the bytes; this is only their format.

#include "../base/base.h"

#include <limits.h>
#include <stdio.h>

enum { CLIP_PATHS_MAX = 4096 };

typedef enum { CLIP_FMT_GNOME, CLIP_FMT_URIS, CLIP_FMT_TEXT, CLIP_FMTS } ClipFormat;

// The MIME types offered, in that order; text has two names, utf-8 first.
static const char *const CLIP_MIME_GNOME = "x-special/gnome-copied-files";
static const char *const CLIP_MIME_URIS = "text/uri-list";
static const char *const CLIP_MIME_TEXT[2] = { "text/plain;charset=utf-8", "text/plain" };

typedef struct {
    bool  cut;                      // Ctrl+X: a paste moves them
    char *path[CLIP_PATHS_MAX];     // absolute
    isize n;
} ClipFiles;

// The payload for one format into the arena (NUL-terminated, the NUL not counted):
//   gnome: "copy" or "cut", then a file:// URI a line (no newline at the end, as Nautilus writes)
//   uris:  a file:// URI a line, each ending in CRLF (RFC 2483)
//   text:  the paths a line, as they are (a terminal pastes them)
// c's paths as text in a, one a line: each quoted as a shell would need it (shell_quote), as other
// programs are offered them (a name with a line break pasted into a terminal was a second command:
// Michael, 2026-11-04), or as they are (ofx's own text fields). Empty when they don't fit.
static Str clip_text(Arena *a, const ClipFiles *c, bool quoted)
{
    isize avail, n = 0;
    char *out = arena_tail(a, &avail);
    if (avail < 1) return arena_commit(a, 0), STR_EMPTY;
    for (isize i = 0; i < c->n && n >= 0; i++) {
        if (i && n + 1 < avail) out[n++] = '\n';
        isize k = quoted ? shell_quote(str_from_c(c->path[i]), out + n, avail - n) : snprintf(out + n, (size_t)(avail - n), "%s", c->path[i]);
        n = k < 0 || k >= avail - n ? -1 : n + k;
    }
    if (n < 0) n = 0;
    out[n] = 0;
    arena_commit(a, n + 1);
    return (Str){ out, n };
}

static Str clip_payload(Arena *a, const ClipFiles *c, ClipFormat f)
{
    if (f == CLIP_FMT_TEXT) return clip_text(a, c, true);
    isize avail;
    char *out = arena_tail(a, &avail);
    isize n = 0;
    bool ok = true;
#define PUT(...)                                                                                   \
    do {                                                                                           \
        int k_ = snprintf(out + n, (size_t)(avail - n), __VA_ARGS__);                              \
        if (k_ < 0 || k_ >= avail - n) ok = false;                                                 \
        else n += k_;                                                                              \
    } while (0)
    if (f == CLIP_FMT_GNOME) PUT("%s", c->cut ? "cut" : "copy");
    for (isize i = 0; ok && i < c->n; i++) {
        char enc[PATH_MAX * 3];
        if (!percent_encode(str_from_c(c->path[i]), "/", enc, sizeof enc)) ok = false;
        else if (f == CLIP_FMT_GNOME) PUT("\nfile://%s", enc);
        else PUT("file://%s\r\n", enc);
    }
#undef PUT
    if (!ok) n = 0;
    out[n] = 0;
    arena_commit(a, n + 1);
    return (Str){ out, n };
}

// The paths in a gnome-copied-files or uri-list payload into c (strings in the arena). Only
// local file:// URIs count (an empty host or "localhost"); "#" lines of a uri-list are comments.
// False when there is no file in it.
static bool clip_parse(Arena *a, Str text, bool gnome, ClipFiles *c)
{
    *c = (ClipFiles){ 0 };
    isize at = 0;
    Str line;
    bool first = true;
    while (next_line(text, &at, &line)) {
        line = str_trim(line);   // CRLF, and Nautilus's lack of one
        if (gnome && first) {
            first = false;
            if (str_eq(line, S("cut"))) {
                c->cut = true;
                continue;
            }
            if (str_eq(line, S("copy"))) continue;
            return false;
        }
        if (line.len == 0 || line.p[0] == '#') continue;
        char path[PATH_MAX];
        if (!file_uri_path(line, path, sizeof path) || c->n == CLIP_PATHS_MAX) continue;
        c->path[c->n++] = arena_cstr(a, str_from_c(path));
    }
    return c->n > 0;
}
