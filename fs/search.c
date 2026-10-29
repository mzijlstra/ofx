// Search below a folder (F9, docs/plan.md "F9 design"): the view's address, and the walk that finds
// the names, run on gui/search.c's thread. It goes depth first from the folder searched and stays
// on its drive; it enters no link, no hidden folder unless View → Show Hidden Files, and none of
// SEARCH_SKIP (Michael, 2026-10-27: a fixed list until ofx.conf). A folder it doesn't enter still
// matches by its own name. It counts what it left out, for the status line, and stops at
// SEARCH_MAX matches or when the generation moves (a new search), as du_walk.
//
// Inside files (step 3, Michael 2026-10-29): each regular file whose name matches is read, in
// SEARCH_CHUNK pieces, and kept when a line matches; folders and links aren't results then. A NUL
// in the first SEARCH_HEAD bytes means binary, files over SEARCH_FILE_MAX aren't read, links to
// files aren't followed (as grep -r and rg), and all three are counted.

#include "../base/base.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

// The view's address, a folder's path as far as tabs, history and go_to are concerned:
// "search:" and the folder searched's length, ':', the folder, then the text, so either may hold
// any byte. Searching inside files (step 3), the name's length follows the folder's after a ',',
// and the text looked for inside comes last: "search:<folder len>,<name len>:<folder><name><text>".
// A path always starts with '/', so it is never one.
#define SEARCH_URI "search:"

enum {
    SEARCH_MAX = 10000, SEARCH_DEPTH_MAX = 256, SEARCH_TICK = 512,
    // Inside files (step 3): read in pieces of SEARCH_CHUNK; a line longer than that is matched a
    // piece at a time, each starting with the last SEARCH_OVERLAP bytes of the one before, so a match
    // across two pieces is found while it is shorter than that (any plain text: the field holds
    // UI_FIND_MAX bytes; a regular expression can match more, and then only within a piece).
    SEARCH_FILE_MAX = 16 << 20, SEARCH_HEAD = 8192, SEARCH_CHUNK = 64 << 10, SEARCH_OVERLAP = 1024,
    SEARCH_BUF = SEARCH_CHUNK + SEARCH_OVERLAP,
    SEARCH_SNIP = 160, SEARCH_SNIP_BEFORE = 40,   // a hit's line: at most this, from this before the match
};

static const char *const SEARCH_SKIP[] = { ".git", "node_modules" };
enum { SEARCH_SKIPS = 2 };

static bool is_search(const char *dir)
{
    return strncmp(dir, SEARCH_URI, sizeof SEARCH_URI - 1) == 0;
}

// The address of a search for text below root, and inside files for inside (step 3; empty: by
// name only); false when it doesn't fit.
static bool search_uri(char *out, size_t cap, const char *root, Str text, Str inside)
{
    int n = inside.len ? snprintf(out, cap, SEARCH_URI "%zu,%td:%s%.*s%.*s", strlen(root), text.len, root, STR_ARG(text), STR_ARG(inside))
                       : snprintf(out, cap, SEARCH_URI "%zu:%s%.*s", strlen(root), root, STR_ARG(text));
    return n >= 0 && (size_t)n < cap;
}

// A length in an address: digits, at most PATH_MAX; false when there are none or too many.
static bool search_len(const char **p, isize *len)
{
    *len = 0;
    const char *at = *p;
    while (**p >= '0' && **p <= '9') {
        *len = *len * 10 + (*(*p)++ - '0');
        if (*len > PATH_MAX) return false;
    }
    return *p > at;
}

// The folder, the text and the text inside files an address holds (pointing into it; inside empty
// for a search by name); false when it isn't one.
static bool search_parse(const char *uri, Str *root, Str *text, Str *inside)
{
    if (!is_search(uri)) return false;
    const char *p = uri + sizeof SEARCH_URI - 1;
    isize len, tlen = -1;
    if (!search_len(&p, &len)) return false;
    if (*p == ',' && (p++, !search_len(&p, &tlen))) return false;
    if (*p++ != ':' || (isize)strnlen(p, (size_t)len) < len || len == 0 || p[0] != '/') return false;
    *root = (Str){ p, len };
    Str rest = str_from_c(p + len);
    if (tlen < 0) return *text = rest, *inside = STR_EMPTY, true;
    if (tlen > rest.len || tlen == rest.len) return false;   // (inside, when there, isn't empty)
    *text = (Str){ rest.p, tlen };
    *inside = (Str){ rest.p + tlen, rest.len - tlen };
    return true;
}

// What the walk left out (none of it searched inside), for the status line.
typedef struct {
    i64  skip[SEARCH_SKIPS];   // folders named in SEARCH_SKIP
    i64  hidden;               // dot-files and dot-folders (View → Show Hidden Files off)
    i64  drives;               // folders where another drive is mounted
    i64  links;                // links to folders
    i64  unread;               // folders that couldn't be read, or nested too deep
    i64  folders;              // folders searched, the one it started from included
    // Inside files (step 3): files read, and those left unread whose name matched.
    i64  files;
    i64  binary;               // a NUL in the first SEARCH_HEAD bytes
    i64  big;                  // over SEARCH_FILE_MAX
    i64  file_links;           // links to files
    i64  unread_files;         // files that couldn't be opened or read
    bool full;                 // SEARCH_MAX matches: it stopped there
} SearchSkips;

typedef struct {
    const atomic_uint *gen;    // the walk stops when this moves off mine
    u32                mine;
    bool               hidden; // dot-files searched and their folders entered
    bool             (*match)(void *ctx, Str name);
    // Inside files (step 3; NULL: names only): whether a line (or a piece of a long one) matches,
    // where in it; buf has SEARCH_BUF bytes to read into.
    bool             (*line)(void *ctx, Str line, isize *from, isize *to);
    char              *buf;
    // A match: name in the folder fd, which is dir below the folder searched ("" for that folder
    // itself), and where inside it matched (NULL by name; its text is the walk's until the next
    // file). False when there is no room for it: the walk stops, full.
    bool             (*found)(void *ctx, Str dir, Str name, int fd, const SearchHit *hit);
    void             (*tick)(void *ctx, const SearchSkips *s);   // every SEARCH_TICK entries
    void              *ctx;
    dev_t              dev;    // the drive it started on
    SearchSkips        s;
    isize              seen;
    char               rel[PATH_MAX];   // the folder being read, below the one searched
    isize              rel_len;
    char               snip[SEARCH_SNIP];   // a hit's line, cut
} SearchWalk;

static bool search_stopped(const SearchWalk *w)
{
    return w->gen && atomic_load(w->gen) != w->mine;
}

static int search_skip_of(const char *name)
{
    for (int k = 0; k < SEARCH_SKIPS; k++)
        if (strcmp(name, SEARCH_SKIP[k]) == 0) return k;
    return -1;
}

typedef enum { SR_NONE, SR_HIT, SR_BINARY, SR_BIG, SR_UNREAD, SR_STOPPED } SearchRead;

// Characters in s[0, n): the bytes that start one.
static i64 search_chars(const char *s, isize n)
{
    i64 c = 0;
    for (isize i = 0; i < n; i++) c += ((unsigned char)s[i] & 0xC0) != 0x80;
    return c;
}

// The reading of one file: the line it is on, and what it found so far.
typedef struct {
    i64  line;      // the line being read, from 1
    i64  before;    // characters of it already gone from the buffer (a long line's)
    bool lit;       // it matched already (counted once)
    i64  lines;     // lines that matched
} SearchScan;

// A line, or a piece of a long one, matched: the first match kept in *hit (its text in w->snip).
static void search_scan(SearchWalk *w, SearchScan *sc, Str piece, SearchHit *hit)
{
    if (piece.len && piece.p[piece.len - 1] == '\r') piece.len--;   // (CRLF)
    isize from, to;
    if (sc->lit || !w->line(w->ctx, piece, &from, &to)) return;
    sc->lit = true;
    if (sc->lines++) return;
    // The match with SEARCH_SNIP_BEFORE before it, at a character, its indent left out.
    isize a = from > SEARCH_SNIP_BEFORE ? from - SEARCH_SNIP_BEFORE : 0;
    while (a < from && ((unsigned char)piece.p[a] & 0xC0) == 0x80) a++;
    bool cut = a > 0;   // (a later piece of a long line has a match SEARCH_OVERLAP in, at least, less its length)
    while (a < from && (piece.p[a] == ' ' || piece.p[a] == '\t')) a++;
    isize b = a + SEARCH_SNIP < piece.len ? a + SEARCH_SNIP : piece.len;
    while (b > a && b < piece.len && ((unsigned char)piece.p[b] & 0xC0) == 0x80) b--;
    for (isize i = a; i < b; i++) {
        unsigned char c = (unsigned char)piece.p[i];
        w->snip[i - a] = c < 0x20 || c == 0x7F ? ' ' : (char)c;
    }
    *hit = (SearchHit){ .line = sc->line, .col = sc->before + search_chars(piece.p, from) + 1, .text = { w->snip, b - a },
                        .at = from - a, .len = (to < b ? to : b) - from, .cut = cut };
}

// The file name in the folder dfd read for a line that matches (step 3); *hit the first, its more
// the other lines that do. Opened without following a link (the walk leaves links out), without
// blocking (a FIFO), and without changing its access time where that is allowed (his own files).
static SearchRead search_read(SearchWalk *w, int dfd, const char *name, SearchHit *hit)
{
    int fl = O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;
    int fd = openat(dfd, name, fl | O_NOATIME);
    if (fd < 0 && errno == EPERM) fd = openat(dfd, name, fl);
    if (fd < 0) return SR_UNREAD;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) return close(fd), SR_UNREAD;
    if (st.st_size > SEARCH_FILE_MAX) return close(fd), SR_BIG;
    SearchScan sc = { .line = 1 };
    char *buf = w->buf;
    isize have = 0, start = 0;   // buf[0, have) read; the line being read starts at start
    bool head = false, end = false;
    SearchRead r = SR_NONE;
    while (!end) {
        if (search_stopped(w)) {
            r = SR_STOPPED;
            break;
        }
        ssize_t k = read(fd, buf + have, (size_t)(SEARCH_BUF - have));
        if (k < 0 && errno == EINTR) continue;
        if (k < 0) {
            r = SR_UNREAD;
            break;
        }
        have += k, end = k == 0;
        if (!head) {   // binary or not, from the first SEARCH_HEAD bytes (or all, when fewer)
            if (have < SEARCH_HEAD && !end) continue;
            if (memchr(buf, 0, (size_t)(have < SEARCH_HEAD ? have : SEARCH_HEAD))) {
                r = SR_BINARY;
                break;
            }
            head = true;
        }
        for (char *nl; (nl = memchr(buf + start, '\n', (size_t)(have - start)));) {
            search_scan(w, &sc, (Str){ buf + start, nl - (buf + start) }, hit);
            start = nl + 1 - buf;
            sc.line++, sc.before = 0, sc.lit = false;
        }
        if (end) {
            if (start < have) search_scan(w, &sc, (Str){ buf + start, have - start }, hit);
            break;
        }
        if (start > 0) {   // the line begun moves to the front
            memmove(buf, buf + start, (size_t)(have - start));
            have -= start, start = 0;
        } else if (have == SEARCH_BUF) {   // a line longer than the buffer: this piece, then its tail kept
            search_scan(w, &sc, (Str){ buf, have }, hit);
            isize keep = have - SEARCH_OVERLAP;
            while (keep < have && ((unsigned char)buf[keep] & 0xC0) == 0x80) keep++;
            sc.before += search_chars(buf, keep);
            memmove(buf, buf + keep, (size_t)(have - keep));
            have -= keep;
        }
    }
    close(fd);
    if (r == SR_NONE && sc.lines) hit->more = sc.lines - 1, r = SR_HIT;
    return r;
}

// Inside files (step 3): the file name in the folder d, whose name matched, read when it is a
// regular file; false when the walk stops (stopped, or full).
static bool search_inside(SearchWalk *w, DIR *d, const char *nm, unsigned char t)
{
    struct stat st;
    if (t == DT_LNK) {
        if (fstatat(dirfd(d), nm, &st, 0) == 0 && S_ISREG(st.st_mode)) w->s.file_links++;
        return true;
    }
    if (t != DT_REG) return true;
    SearchHit hit;
    switch (search_read(w, dirfd(d), nm, &hit)) {
    case SR_HIT: w->s.files++; break;
    case SR_NONE: w->s.files++; return true;
    case SR_BINARY: w->s.binary++; return true;
    case SR_BIG: w->s.big++; return true;
    case SR_UNREAD: w->s.unread_files++; return true;
    case SR_STOPPED: return false;
    }
    if (w->found(w->ctx, (Str){ w->rel, w->rel_len }, str_from_c(nm), dirfd(d), &hit)) return true;
    w->s.full = true;
    return false;
}

// The folder fd (closed here) and everything below it it may enter. False when stopped or full.
static bool search_dir(SearchWalk *w, int fd, int depth)
{
    if (depth > SEARCH_DEPTH_MAX) return close(fd), w->s.unread++, true;
    DIR *d = fdopendir(fd);
    if (!d) return close(fd), w->s.unread++, true;
    w->s.folders++;
    bool go = true;
    for (struct dirent *e; go && (e = readdir(d));) {
        const char *nm = e->d_name;
        if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) continue;
        if (search_stopped(w)) {
            go = false;
            break;
        }
        if (++w->seen % SEARCH_TICK == 0 && w->tick) w->tick(w->ctx, &w->s);
        if (nm[0] == '.' && !w->hidden) {
            w->s.hidden++;
            continue;
        }
        Str name = str_from_c(nm);
        // Its type: readdir's, else a stat (and one for a link, below, to tell a link to a folder
        // from one to a file).
        unsigned char t = e->d_type;
        struct stat st;
        if (t == DT_UNKNOWN) {
            if (fstatat(dirfd(d), nm, &st, AT_SYMLINK_NOFOLLOW) != 0) continue;
            t = S_ISDIR(st.st_mode) ? DT_DIR : S_ISLNK(st.st_mode) ? DT_LNK : S_ISREG(st.st_mode) ? DT_REG : DT_FIFO;
        }
        if (w->match(w->ctx, name)) {
            if (w->line) go = search_inside(w, d, nm, t);
            else if (!w->found(w->ctx, (Str){ w->rel, w->rel_len }, name, dirfd(d), NULL)) w->s.full = true, go = false;
            if (!go) break;
        }
        if (t == DT_LNK && fstatat(dirfd(d), nm, &st, 0) == 0 && S_ISDIR(st.st_mode)) w->s.links++;
        if (t != DT_DIR) continue;
        int k = search_skip_of(nm);
        if (k >= 0) {
            w->s.skip[k]++;
            continue;
        }
        if (fstatat(dirfd(d), nm, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(st.st_mode)) continue;
        if (st.st_dev != w->dev) {
            w->s.drives++;
            continue;
        }
        isize was = w->rel_len, len = name.len;
        if (was + (was ? 1 : 0) + len >= (isize)sizeof w->rel) {
            w->s.unread++;
            continue;
        }
        if (was) w->rel[w->rel_len++] = '/';
        memcpy(w->rel + w->rel_len, nm, (size_t)len), w->rel_len += len, w->rel[w->rel_len] = 0;
        int sub = openat(dirfd(d), nm, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (sub < 0) w->s.unread++;
        else go = search_dir(w, sub, depth + 1);
        w->rel_len = was, w->rel[was] = 0;
    }
    closedir(d);
    return go;
}

// Everything below root that matches, into found, w->s zeroed first. False when stopped (a new
// search) or full; w->s says which.
static bool search_walk(SearchWalk *w, const char *root)
{
    w->s = (SearchSkips){ 0 }, w->seen = 0, w->rel_len = 0, w->rel[0] = 0;
    struct stat st;
    int fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0 || fstat(fd, &st) != 0) {
        if (fd >= 0) close(fd);
        w->s.unread++;
        if (w->tick) w->tick(w->ctx, &w->s);
        return true;
    }
    w->dev = st.st_dev;
    bool whole = search_dir(w, fd, 1);
    if (w->tick) w->tick(w->ctx, &w->s);
    return whole;
}

// What the walk left out, for the status line ("skipped 3 .git, 1 link to a folder"); empty when
// nothing was.
static void search_skips_text(const SearchSkips *s, char *out, size_t cap)
{
    out[0] = 0;
    size_t at = 0;
    const char *sep = "skipped ";
#define SEARCH_PART(n, one, many)                                                                             \
    if ((n) > 0 && at < cap) {                                                                              \
        int k = snprintf(out + at, cap - at, "%s%lld %s", sep, (long long)(n), (n) == 1 ? (one) : (many));    \
        if (k > 0) at += (size_t)k;   /* (past cap: cut, and nothing more) */                                \
        sep = ", ";                                                                                         \
    }
    SEARCH_PART(s->skip[0], ".git", ".git")
    SEARCH_PART(s->skip[1], "node_modules", "node_modules")
    SEARCH_PART(s->hidden, "hidden", "hidden")
    SEARCH_PART(s->drives, "other drive", "other drives")
    SEARCH_PART(s->links, "link to a folder", "links to folders")
    SEARCH_PART(s->unread, "unreadable folder", "unreadable folders")
    SEARCH_PART(s->binary, "binary", "binary")
    SEARCH_PART(s->big, "over 16 MB", "over 16 MB")
    SEARCH_PART(s->file_links, "link to a file", "links to files")
    SEARCH_PART(s->unread_files, "unreadable file", "unreadable files")
#undef SEARCH_PART
}
