// Paths as fs/ uses them, in one place (2026-10-28: each file had its own copy, a little
// different from the next): whether a path is inside a folder, a name joined to a folder, the XDG
// folders, mkdir -p, file:// URIs and %XX, and a mountinfo line. Included first by fs.c.
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Whether path is dir or inside it; "/" holds everything. Both absolute, no '/' at the end.
static bool path_within(const char *path, const char *dir)
{
    size_t n = strlen(dir);
    return strcmp(dir, "/") == 0 || (strncmp(path, dir, n) == 0 && (path[n] == 0 || path[n] == '/'));
}

// Whether path is strictly inside dir: something after the '/' (never with dir "/": the test
// guard's root is never that, and refuses everything if it were).
static bool path_below(const char *path, const char *dir)
{
    size_t n = strlen(dir);
    return strncmp(path, dir, n) == 0 && path[n] == '/' && path[n + 1];
}

// dir/name into out; false when it would not fit (a cut path could name another file).
static bool path_put(char *out, size_t cap, const char *dir, Str name);

// A regular file, name in folder dfd (AT_FDCWD: a path), of at most max bytes, whole, into the
// arena, and its stat; STR_EMPTY otherwise. O_NONBLOCK: a FIFO there must not block the open (then
// it isn't regular, and is refused). flags: more for open (O_NOFOLLOW: a link isn't read;
// O_NOATIME: the access time kept). One reader for every small file read whole (code-health
// review, 2026-11-03: written out six times).
static Str file_read_at(Arena *a, int dfd, const char *name, isize max, struct stat *st, int flags)
{
    int fd = openat(dfd, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | flags);
    if (fd < 0) return STR_EMPTY;
    Str s = STR_EMPTY;
    char *p;
    if (fstat(fd, st) == 0 && S_ISREG(st->st_mode) && st->st_size > 0 && st->st_size <= max &&
        (p = arena_try_push(a, (isize)st->st_size, 1))) {
        isize n = 0;
        for (ssize_t got; n < st->st_size && (got = read(fd, p + n, (size_t)(st->st_size - n))) > 0;) n += got;
        if (n == st->st_size) s = (Str){ p, n };
    }
    close(fd);
    return s;
}

// The start of the regular file at path, up to cap bytes, into buf: its length, or -1 when it isn't
// one (or can't be read). Not followed if a link, nor waited on if a FIFO.
static isize file_read_buf(const char *path, char *buf, isize cap)
{
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    struct stat st;
    ssize_t n = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) ? read(fd, buf, (size_t)cap) : -1;
    close(fd);
    return n;
}

static bool path_put(char *out, size_t cap, const char *dir, Str name)
{
    int n = snprintf(out, cap, "%s%s%.*s", dir, strcmp(dir, "/") == 0 ? "" : "/", STR_ARG(name));
    return n >= 0 && (size_t)n < cap;
}

// Whether byte i of s starts a control character: C0, DEL, or C1 in UTF-8 (U+0080..U+009F, which
// a terminal may take as a command: U+009B is CSI).
static bool shell_ctrl(Str s, isize i)
{
    u8 c = (u8)s.p[i];
    return c < 0x20 || c == 0x7f || (c == 0xc2 && i + 1 < s.len && (u8)s.p[i + 1] >= 0x80 && (u8)s.p[i + 1] <= 0x9f);
}

// s as a shell reads it back (ls's shell-escape style, Copy Path; Michael, 2026-11-04): as it is
// when every byte is safe there (letters, digits, / . _ - + , : @ % =, and UTF-8), else in single
// quotes (' as '\''), or in $'…' when it holds a control character (\n, \t, \r, \\, \', the rest
// \xHH), so a name can't paste as a second command line. The length written into out (NUL
// ended), -1 when it doesn't fit.
static isize shell_quote(Str s, char *out, isize cap)
{
    bool plain = s.len > 0, ctrl = false;
    for (isize i = 0; i < s.len; i++) {
        u8 c = (u8)s.p[i];
        if (shell_ctrl(s, i)) ctrl = true;
        bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("/._-+,:@%=", c) || c >= 0x80;
        if (!safe || shell_ctrl(s, i)) plain = false;
    }
    isize n = 0;
#define PUT(...)                                                                                   \
    do {                                                                                           \
        int k_ = snprintf(out + n, (size_t)(cap - n), __VA_ARGS__);                                \
        if (k_ < 0 || k_ >= cap - n) return -1;                                                    \
        n += k_;                                                                                   \
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
        for (isize i = 0; i < s.len; i++) {
            u8 c = (u8)s.p[i];
            if (c == '\n') PUT("\\n");
            else if (c == '\t') PUT("\\t");
            else if (c == '\r') PUT("\\r");
            else if (c == '\\' || c == '\'') PUT("\\%c", c);
            else if (shell_ctrl(s, i) && c == 0xc2) {
                PUT("\\x%02x\\x%02x", c, (u8)s.p[i + 1]);
                i++;
            }
            else if (shell_ctrl(s, i)) PUT("\\x%02x", c);
            else PUT("%c", c);
        }
        PUT("'");
    }
#undef PUT
    return n;
}

// path's folder into out ("/" for one at the top); false when it doesn't fit.
static bool path_parent(char *out, size_t cap, const char *path)
{
    Str d = path_dir(str_from_c(path));
    int n = snprintf(out, cap, "%.*s", STR_ARG(d.len ? d : S("/")));
    return n >= 0 && (size_t)n < cap;
}

// An XDG base folder's sub: $var/sub when var is absolute (a relative one is ignored, as the spec
// says), else $HOME/fallback/sub (".local/share" and the like); false without either, or when it
// would not fit.
static bool xdg_dir(char *out, size_t cap, const char *var, const char *fallback, const char *sub)
{
    const char *x = getenv(var), *home = getenv("HOME");
    int n;
    if (x && x[0] == '/') n = snprintf(out, cap, "%s/%s", x, sub);
    else if (home && home[0] == '/') n = snprintf(out, cap, "%s/%s/%s", home, fallback, sub);
    else return false;
    return n > 0 && (size_t)n < cap;
}

// mkdir -p: every folder of path up to its last '/', and path itself when whole, with mode where
// missing. The caller asks the test guard first.
static bool mkdir_p(const char *path, mode_t mode, bool whole, Err *err)
{
    char p[PATH_MAX];
    if (snprintf(p, sizeof p, "%s", path) >= (int)sizeof p) return err_set(err, "%s: the path is too long", path), false;
    for (char *s = p + 1; *s; s++) {
        if (*s != '/') continue;
        *s = 0;
        if (mkdir(p, mode) != 0 && errno != EEXIST) return err_set(err, "%s: %s", p, strerror(errno)), false;
        *s = '/';
    }
    if (whole && mkdir(p, mode) != 0 && errno != EEXIST) return err_set(err, "%s: %s", p, strerror(errno)), false;
    return true;
}

// mkdir -p for a folder of ofx's own (the trash's, the state's, the window registry), 0700, and
// a folder at the end (not a link to one).
static bool mkdir_private(const char *path, Err *err)
{
    if (!mkdir_p(path, 0700, true, err)) return false;
    struct stat st;
    if (lstat(path, &st) != 0 || !S_ISDIR(st.st_mode)) return err_set(err, "%s isn't a folder", path), false;
    return true;
}

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// s with %XX decoded into out; false when it doesn't fit, a % isn't followed by two hex digits, or
// one is %00 (a NUL would end the path early).
static bool percent_decode(Str s, char *out, size_t cap)
{
    size_t o = 0;
    for (isize i = 0; i < s.len; i++) {
        char c = s.p[i];
        if (c == '%') {
            int h = i + 2 < s.len ? hex_digit(s.p[i + 1]) : -1, l = i + 2 < s.len ? hex_digit(s.p[i + 2]) : -1;
            if (h < 0 || l < 0 || h * 16 + l == 0) return false;
            c = (char)(h * 16 + l), i += 2;
        }
        if (o + 1 >= cap) return false;
        out[o++] = c;
    }
    out[o] = 0;
    return true;
}

// s %XX-encoded: letters, digits, -._~ and the bytes in keep as they are, the rest as %XX in upper
// case. False when it would not fit.
static bool percent_encode(Str s, const char *keep, char *out, size_t cap)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t n = 0;
    for (isize i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.p[i];
        bool as_is = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c && strchr("-._~", c)) ||
                     (c && strchr(keep, c));
        if (n + (as_is ? 1 : 3) >= cap) return false;
        if (as_is) out[n++] = (char)c;
        else out[n++] = '%', out[n++] = HEX[c >> 4], out[n++] = HEX[c & 15];
    }
    out[n] = 0;
    return true;
}

// A file:// URI's path: "file:///" or "file://localhost/", then %XX escapes; false for another
// scheme or host, a bad escape, or one that doesn't fit.
static bool file_uri_path(Str uri, char *out, size_t cap)
{
    Str rest;
    if (str_starts_with(uri, S("file:///"))) rest = str_cut(uri, 7, uri.len);
    else if (str_starts_with(uri, S("file://localhost/"))) rest = str_cut(uri, 16, uri.len);
    else return false;
    return percent_decode(rest, out, cap);
}

// mountinfo writes a space, tab, newline and backslash in a path as \040, \011, \012 and \134.
static bool octal_decode(Str s, char *out, size_t cap)
{
    size_t o = 0;
    for (isize i = 0; i < s.len; i++) {
        char c = s.p[i];
        if (c == '\\' && i + 3 < s.len && s.p[i + 1] >= '0' && s.p[i + 1] <= '3' && s.p[i + 2] >= '0' &&
            s.p[i + 2] <= '7' && s.p[i + 3] >= '0' && s.p[i + 3] <= '7') {
            c = (char)((s.p[i + 1] - '0') * 64 + (s.p[i + 2] - '0') * 8 + (s.p[i + 3] - '0'));
            i += 3;
        }
        if (o + 1 >= cap) return false;
        out[o++] = c;
    }
    out[o] = 0;
    return true;
}

// One mountinfo line: its mount point (the fifth field, decoded) into mp, and after the " - " that
// ends the optional fields, its file system type and source (empty when the " - " is missing).
// False when it has no mount point.
static bool mountinfo_line(Str line, char *mp, size_t cap, Str *type, Str *source)
{
    Str f = line;
    for (int k = 0; k < 4; k++) {
        isize sp = str_find(f, ' ');
        f = sp < 0 ? STR_EMPTY : str_cut(f, sp + 1, f.len);
    }
    isize sp = str_find(f, ' ');
    if (sp <= 0 || !octal_decode(str_cut(f, 0, sp), mp, cap)) return false;
    *type = *source = STR_EMPTY;
    isize dash = -1;
    for (isize i = 0; dash < 0 && i + 3 <= line.len; i++)
        if (memcmp(line.p + i, " - ", 3) == 0) dash = i;
    if (dash < 0) return true;
    Str rest = str_cut(line, dash + 3, line.len);
    isize s1 = str_find(rest, ' ');
    *type = s1 < 0 ? rest : str_cut(rest, 0, s1);
    if (s1 >= 0) {
        Str src = str_cut(rest, s1 + 1, rest.len);
        isize s2 = str_find(src, ' ');
        *source = s2 < 0 ? src : str_cut(src, 0, s2);
    }
    return true;
}
