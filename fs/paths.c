// Paths as fs/ uses them, in one place (2026-10-28: each file had its own copy, a little
// different from the next): whether a path is inside a folder, a name joined to a folder, the XDG
// folders, mkdir -p, file:// URIs and %XX, a mountinfo line, a whole number (str_num). Included
// first by fs.c.
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

// n bytes of body written whole into fd (a short write goes on; EINTR again); false, errno set, when
// a write fails. The one writer of whole small files (the F15 code-health review: four did a single
// write() and took a short one for an error, two looped).
static bool fd_write_all(int fd, const void *body, isize n)
{
    for (isize at = 0; at < n;) {
        ssize_t k = write(fd, (const char *)body + at, (size_t)(n - at));
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return k == 0 ? (errno = EIO, false) : false;
        at += k;
    }
    return true;
}

// body as file path through tmp, a new file beside it (one a crash left there taken out first):
// mode, written whole, synced when sync, renamed over path, so a reader or a crash sees the old file
// or the new, never half of one; tmp removed when it fails. The caller asks the test guard for path
// and makes its folder (each its own way: the state's private, a thumbnail's 0700 by the spec).
static bool file_replace(const char *path, const char *tmp, const void *body, isize n, mode_t mode, bool sync, Err *err)
{
    unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
    if (fd < 0) return err_set(err, "%s: %s", tmp, strerror(errno)), false;
    bool ok = fchmod(fd, mode) == 0 && fd_write_all(fd, body, n) && (!sync || fsync(fd) == 0);
    int e = errno;
    if (close(fd) != 0 && ok) ok = false, e = errno;
    if (ok && rename(tmp, path) == 0) return true;
    if (ok) e = errno;
    unlink(tmp);
    return err_set(err, "%s: %s", path, strerror(e)), false;
}

// The regular file open at fd (st its fstat) whole into the arena, max bytes at most, with a NUL
// after it when nul: STR_EMPTY when it isn't one, is empty, too big, or reads shorter than its
// size (it changed meanwhile). file_read_at's and read_file's one loop.
static Str file_read_fd(Arena *a, int fd, const struct stat *st, isize max, bool nul)
{
    char *p;
    if (!S_ISREG(st->st_mode) || st->st_size <= 0 || st->st_size > max || !(p = arena_try_push(a, (isize)st->st_size + nul, 1))) return STR_EMPTY;
    isize n = 0;
    for (ssize_t got; n < st->st_size && (got = read(fd, p + n, (size_t)(st->st_size - n))) > 0;) n += got;
    if (n != st->st_size) return STR_EMPTY;
    if (nul) p[n] = 0;
    return (Str){ p, n };
}

// A regular file, name in folder dfd (AT_FDCWD: a path), of at most max bytes, whole, into the
// arena, and its stat; STR_EMPTY otherwise. O_NONBLOCK: a FIFO there must not block the open (then
// it isn't regular, and is refused). flags: more for open (O_NOFOLLOW: a link isn't read;
// O_NOATIME: the access time kept). One reader for every small file read whole (code-health
// review, 2026-11-03: written out six times).
static Str file_read_at(Arena *a, int dfd, const char *name, isize max, struct stat *st, int flags)
{
    int fd = openat(dfd, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | flags);
    if (fd < 0) return STR_EMPTY;
    Str s = fstat(fd, st) == 0 ? file_read_fd(a, fd, st, max, false) : STR_EMPTY;
    close(fd);
    return s;
}

// The whole file, NUL-terminated, into a; STR_EMPTY when it isn't there or won't fit. Sized by
// fstat first: opening the arena's whole free tail would cost a pass over it under ASan. A file
// that reads shorter than that size changed while read, and gives nothing. procfs files say 0
// (/proc/self/mountinfo): those are read to their end, up to READ_UNSIZED_MAX.
enum { READ_UNSIZED_MAX = MiB(1) };

static Str read_file(Arena *a, const char *path)
{
    // A FIFO or a device at one of these paths would block the open: opened without blocking, then
    // regular files only (file_read_fd; a stat before the open raced the file's change).
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return STR_EMPTY;
    struct stat st;
    Str s = STR_EMPTY;
    if (fstat(fd, &st) != 0) {
    } else if (S_ISREG(st.st_mode) && st.st_size == 0) {   // (procfs: read to its end)
        isize avail, n = 0;
        char *dst = arena_tail(a, &avail);
        isize cap = avail - 1 < READ_UNSIZED_MAX ? avail - 1 : READ_UNSIZED_MAX;
        ssize_t got = 1;
        while (avail > 1 && n < cap && (got = read(fd, dst + n, (size_t)(cap - n))) > 0) n += got;
        bool whole = avail > 1 && got == 0;
        if (!whole && n == READ_UNSIZED_MAX) fprintf(stderr, "ofx: WARN %s is over %d bytes: not read\n", path, READ_UNSIZED_MAX);
        if (whole) dst[n] = 0, s = (Str){ dst, n };
        arena_commit(a, whole ? n + 1 : 0);
    } else {
        s = file_read_fd(a, fd, &st, PTRDIFF_MAX - 1, true);
    }
    close(fd);
    return s;
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

// A `key = value` line split at its first '=', each side trimmed; false with no '='. ofx.conf, the
// state file and the theme's files read their lines so (the F15 code-health review: four copies).
static bool kv_split(Str line, Str *key, Str *val)
{
    isize eq = str_find(line, '=');
    if (eq < 0) return false;
    *key = str_trim(str_cut(line, 0, eq)), *val = str_trim(str_cut(line, eq + 1, line.len));
    return true;
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

// path's folder into out ("/" for one at the top); false when it doesn't fit.
static bool path_parent(char *out, size_t cap, const char *path)
{
    Str d = path_dir(str_from_c(path));
    int n = snprintf(out, cap, "%.*s", STR_ARG(d.len ? d : S("/")));
    return n >= 0 && (size_t)n < cap;
}

// w as a whole number, at most max: -1 when it isn't one (empty, not all digits, or past max; no
// i64 overflow, F11 review). The state file's and the window lines' numbers (folders.c, session.c,
// state.c: F13 code-health review, one parser for the three).
static i64 str_num(Str w, i64 max)
{
    i64 n = 0;
    if (!w.len) return -1;
    for (isize i = 0; i < w.len; i++) {   // (n checked before it grows)
        if (w.p[i] < '0' || w.p[i] > '9' || n > (max - (w.p[i] - '0')) / 10) return -1;
        n = n * 10 + (w.p[i] - '0');
    }
    return n;
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

// path's file:// URI as GLib makes it (g_filename_to_uri), which is what Nemo hashes for its
// previews: every byte but letters, digits and -._~!$&'()*+,=@:/ as %XX in upper case (seen with
// `gio info`, 2026-10-24). The previews', the bookmarks' and the clipboard's and drags' (one
// builder: code-health review, 2026-11-06). False when it would not fit.
static bool file_uri(const char *path, char *out, size_t cap)
{
    if (cap < 8) return false;
    memcpy(out, "file://", 7);
    return percent_encode(str_from_c(path), "!$&'()*+,=@:/", out + 7, cap - 7);
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
