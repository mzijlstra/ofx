// What Ctrl+C and Ctrl+X put on the system clipboard, and what Ctrl+V reads back from it
// (docs/plan.md, F3 "System clipboard"): the same paths in the forms Nemo, browsers and
// terminals each read, the text quoted as a shell reads it back (shell_quote, F13). gui/clipboard.c
// moves the bytes; this is only their format.

#include "../base/base.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

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

// fmt at out + *n, cap bytes in all, *n moved past it: false, *n as it was, when it doesn't fit
// (the clipboard's forms and shell_quote: F13 code-health review).
static bool buf_put(char *out, isize cap, isize *n, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static bool buf_put(char *out, isize cap, isize *n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(out + *n, (size_t)(cap - *n), fmt, ap);
    va_end(ap);
    if (k < 0 || k >= cap - *n) return false;
    *n += k;
    return true;
}

// ---- text a shell reads back (Copy Path, and Ctrl+C's text: F13) ---------------------------

// The length of the valid UTF-8 sequence at byte i of s (1 for ASCII), its code point in *cp; 0 for
// a byte that starts none (base's utf8_next: no overlong form, no surrogate, nothing past
// U+10FFFF; it gives U+FFFD for one such byte, a real U+FFFD being three).
static int utf8_at(Str s, isize i, u32 *cp)
{
    isize j = i;
    *cp = utf8_next(s.p, s.len, &j);
    return *cp == 0xfffd && j - i == 1 ? 0 : (int)(j - i);
}

// How many bytes from i a paste must spell as \xHH, 0 for none: a control character (C0, DEL, or
// C1 in UTF-8, U+0080..U+009F, which a terminal may take as a command: U+009B is CSI), an
// invisible one (below), or a byte that starts no valid UTF-8 (F13 step 1 re-review, 2026-11-04:
// so the paste is valid UTF-8, and no 8-bit control reaches a Latin-1 terminal).
static int shell_ctrl(Str s, isize i)
{
    u32 cp;
    int n = utf8_at(s, i, &cp);
    if (n == 0) return 1;
    // The invisible ones too (Michael, 2026-11-04): what turns text's direction (U+061C, U+200E-200F,
    // U+202A-202E, U+2066-2069), joins or breaks it unseen (U+180E, U+200B-200D, U+2060-2065,
    // U+FEFF), and the line and paragraph separators (U+2028-2029), so a path pastes as it runs.
    bool hidden = cp == 0x061c || cp == 0x180e || (cp >= 0x200b && cp <= 0x200f) || (cp >= 0x2028 && cp <= 0x202e) ||
                  (cp >= 0x2060 && cp <= 0x2069) || cp == 0xfeff;
    return cp < 0x20 || cp == 0x7f || (cp >= 0x80 && cp <= 0x9f) || hidden ? n : 0;
}

// s as a shell reads it back (ls's shell-escape style, Copy Path; Michael, 2026-11-04): as it is
// when every byte is safe there (letters, digits, / . _ - + , : @ % =, and UTF-8), else in single
// quotes (' as '\''), or in $'…' when it holds a control character or a byte of no valid UTF-8
// (\n, \t, \r, \\, \', the rest \xHH), so a name can't paste as a second command line. The length
// written into out (NUL ended), -1 when it doesn't fit.
static isize shell_quote(Str s, char *out, isize cap)
{
    bool plain = s.len > 0, ctrl = false;
    for (isize i = 0; i < s.len;) {   // (a sequence at a time: a UTF-8 character's later bytes start none)
        u8 c = (u8)s.p[i];
        int bad = shell_ctrl(s, i);
        bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("/._-+,:@%=", c) || c >= 0x80;
        if (bad) ctrl = true;
        if (!safe || bad) plain = false;
        u32 cp;
        i += bad ? bad : utf8_at(s, i, &cp);
    }
    isize n = 0;
#define PUT(...)                                                                                   \
    do {                                                                                           \
        if (!buf_put(out, cap, &n, __VA_ARGS__)) return -1;                                        \
    } while (0)
    if (cap <= 0) return -1;
    out[0] = 0;
    if (plain) PUT("%.*s", STR_ARG(s));
    else if (!ctrl) {
        PUT("'");
        for (isize i = 0; i < s.len; i++)
            if (s.p[i] == '\'') PUT("'\\''");
            else PUT("%c", s.p[i]);
        PUT("'");
    } else {
        PUT("$'");
        for (isize i = 0; i < s.len;) {
            u8 c = (u8)s.p[i];
            u32 cp;
            int bad = shell_ctrl(s, i), len = bad ? bad : utf8_at(s, i, &cp);
            if (c == '\n') PUT("\\n");
            else if (c == '\t') PUT("\\t");
            else if (c == '\r') PUT("\\r");
            else if (c == '\\' || c == '\'') PUT("\\%c", c);
            else if (bad)
                for (int k = 0; k < len; k++) PUT("\\x%02x", (u8)s.p[i + k]);
            else PUT("%.*s", len, s.p + i);
            i += len;
        }
        PUT("'");
    }
#undef PUT
    return n;
}

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

// The payload for one format into the arena (NUL-terminated, the NUL not counted), for the clipboard's
// offers (gui/clipboard.c) and a drag out's (gui/dnd.c); empty when it doesn't fit:
//   gnome: "copy" or "cut", then a file:// URI a line (no newline at the end, as Nautilus writes)
//   uris:  a file:// URI a line, each ending in CRLF (RFC 2483)
//   text:  the paths a line, each quoted as a shell reads it back (clip_text)
static Str clip_payload(Arena *a, const ClipFiles *c, ClipFormat f)
{
    if (f == CLIP_FMT_TEXT) return clip_text(a, c, true);
    isize avail;
    char *out = arena_tail(a, &avail);
    if (avail < 1) return arena_commit(a, 0), STR_EMPTY;   // (no room for the 0 either, as clip_text)
    isize n = 0;
    bool ok = true;
#define PUT(...) (ok = ok && buf_put(out, avail, &n, __VA_ARGS__))
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

// The format a MIME type asks for (CLIP_MIME_*), -1 for none of ours.
static int clip_fmt_of(const char *mime)
{
    if (strcmp(mime, CLIP_MIME_GNOME) == 0) return CLIP_FMT_GNOME;
    if (strcmp(mime, CLIP_MIME_URIS) == 0) return CLIP_FMT_URIS;
    if (strcmp(mime, CLIP_MIME_TEXT[0]) == 0 || strcmp(mime, CLIP_MIME_TEXT[1]) == 0) return CLIP_FMT_TEXT;
    return -1;
}

// Another program's bytes from fd into buf, cap - 1 at most and a NUL after them, wait_ms at most
// (it writes in its own time): the count, *whole when it read to the end. For the clipboard's paste
// and a drop's read (gui/clipboard.c, gui/dnd.c: their two copies had drifted, one giving up at an
// EINTR: code-health review, 2026-11-06).
static isize fd_read_timed(int fd, char *buf, isize cap, int wait_ms, bool *whole)
{
    isize n = 0;
    *whole = false;
    i64 until = now_ms() + wait_ms;
    while (n < cap - 1 && now_ms() < until) {
        if (poll(&(struct pollfd){ .fd = fd, .events = POLLIN }, 1, 50) <= 0) continue;
        ssize_t k = read(fd, buf + n, (size_t)(cap - 1 - n));
        if (k < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (k <= 0) {
            *whole = k == 0;
            break;
        }
        n += k;
    }
    buf[n] = 0;
    return n;
}

// p to another program's fd, made nonblocking, wait_ms at most (it reads in its own time; one that
// leaves early gets EPIPE, as SIGPIPE is blocked): the bytes written.
static isize fd_write_timed(int fd, Str p, int wait_ms)
{
    fcntl(fd, F_SETFL, O_NONBLOCK);
    i64 until = now_ms() + wait_ms;
    isize at = 0;
    while (at < p.len && now_ms() < until) {
        ssize_t k = write(fd, p.p + at, (size_t)(p.len - at));
        if (k > 0) at += k;
        else if (k < 0 && errno != EAGAIN && errno != EINTR) break;
        else poll(&(struct pollfd){ .fd = fd, .events = POLLOUT }, 1, 50);
    }
    return at;
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
