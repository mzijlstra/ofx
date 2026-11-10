// A folder on a far drive (F16 step 2: a network share, any FUSE mount) is read by a child,
// ofx-list (tools/list.c), never by ofx itself. A read there can block for as long as the server
// takes to be given up on (gvfs: about 90 s), and a reader killed meanwhile stays in D until gvfsd
// answers (about 20 s, measured in tests/net). A thread of ofx's own would hold the whole process,
// its window's Wayland connection with it; a child ofx doesn't wait for holds nothing
// (spawn_init's SA_NOCLDWAIT: the kernel reaps it whenever it ends).
//
// What ofx-list says, on its output, each message a line; an entry's name and link target follow
// its line, their bytes counted (a name can hold a newline):
//   W <0|1>                                            the folder can be written (Can Write)
//   E <kind> <size> <mtime> <mode> <uid> <gid> <nlen> <tlen>\n<name><target>
//   H <hidden>                                         dot-files in the folder, then the end:
//   L
//   X <errno> <reason>                                 it can't be read (then ofx-list ends); the errno
//                                                      for ofx to act on (ENOENT, ENOTDIR), the reason to say
//   C <gen> <idx> <n>                                  a count asked for (n -1: unreadable)
// What ofx asks on its input, for the rows on screen: "C <gen> <idx> <nlen>\n<name>". Its input's
// end is ofx-list's.

enum { FAR_LINE_MAX = 512 };

typedef struct {
    char type;          // 'W', 'E', 'H', 'L', 'X' or 'C'
    i64  v[6];          // W: v[0]; E: kind, size, mtime, mode, uid, gid; H: v[0]; C: gen, idx, n
    Str  name, target;  // E: in the buffer read
    Str  text;          // X: the reason, in the buffer read (its errno in v[0])
} FarMsg;

// The numbers of line (after its type letter and a space) into v, n of them, each within
// [lo, hi]; false when one isn't there, isn't a number, is out of range, or more follow.
static bool far_nums(Str line, i64 *v, int n, i64 lo, i64 hi)
{
    char buf[FAR_LINE_MAX];
    if (line.len < 2 || line.len >= FAR_LINE_MAX || line.p[1] != ' ') return false;
    memcpy(buf, line.p + 2, (size_t)line.len - 2), buf[line.len - 2] = 0;
    char *p = buf;
    for (int i = 0; i < n; i++) {
        if (*p != '-' && (*p < '0' || *p > '9')) return false;
        char *end;
        errno = 0;
        long long x = strtoll(p, &end, 10);
        if (errno || end == p || x < lo || x > hi) return false;
        v[i] = x, p = end;
        if (i < n - 1 && *p++ != ' ') return false;
    }
    return *p == 0;
}

// The message at *at in b: 1 with *at past it; 0 when it hasn't all come yet (*at kept); -1 when
// it is malformed (what follows can't be trusted: the reader stops there).
static int far_next(Str b, isize *at, FarMsg *m)
{
    isize s = *at, nl = s;
    while (nl < b.len && b.p[nl] != '\n') nl++;
    if (nl == b.len) return nl - s >= FAR_LINE_MAX ? -1 : 0;
    Str line = { b.p + s, nl - s };
    *m = (FarMsg){ .type = line.len ? line.p[0] : 0 };
    isize next = nl + 1;
    switch (m->type) {
    case 'W':
        if (!far_nums(line, m->v, 1, 0, 1)) return -1;
        break;
    case 'H':
        if (!far_nums(line, m->v, 1, 0, INT64_MAX)) return -1;
        break;
    case 'L':
        if (line.len != 1) return -1;
        break;
    case 'X': {
        isize sp = 2;   // (the errno, 1 to 4095, then the reason, not empty)
        while (sp < line.len && line.p[sp] != ' ') sp++;
        if (sp >= line.len - 1 || !far_nums((Str){ line.p, sp }, m->v, 1, 1, 4095)) return -1;
        m->text = (Str){ line.p + sp + 1, line.len - sp - 1 };
        break;
    }
    case 'C':
        if (!far_nums(line, m->v, 3, -1, INT64_MAX) || m->v[0] < 0 || m->v[1] < 0) return -1;
        break;
    case 'E': {
        i64 v[8];
        // (the mtime, v[2], may be before 1970: a file's date as an archive kept it; the rest can't
        // be negative. The F16 code-health review: one such file made the whole folder malformed.)
        bool ok = far_nums(line, v, 8, INT64_MIN, INT64_MAX);
        for (int k = 0; ok && k < 8; k++) ok = k == 2 || v[k] >= 0;
        if (!ok || v[0] > K_OTHER || v[3] > UINT32_MAX || v[4] > UINT32_MAX || v[5] > UINT32_MAX ||
            v[6] < 1 || v[6] > NAME_MAX || v[7] >= PATH_MAX)
            return -1;
        if (b.len - next < v[6] + v[7]) return 0;
        memcpy(m->v, v, sizeof m->v);
        m->name = (Str){ b.p + next, v[6] };
        m->target = (Str){ b.p + next + v[6], v[7] };
        if (memchr(m->name.p, '/', (size_t)m->name.len) || memchr(m->name.p, 0, (size_t)m->name.len)) return -1;
        next += v[6] + v[7];
        break;
    }
    default:
        return -1;
    }
    *at = next;
    return 1;
}

// The listing in b, from its start to its L, into out (names and targets copied into a; unsorted),
// with Can Write; *after past the L, where the counts come. False, with the reason, when ofx-list
// said it can't read the folder (X), said something malformed, or a is full; *ended false when the
// L hasn't come yet (then nothing else is set).
static bool far_listing(Arena *a, Str b, const char *dir, Listing *out, bool *can_write, isize *after, bool *ended, Err *err)
{
    *out = (Listing){ 0 }, *can_write = false, *ended = false;
    isize at = 0, cap = 256, n = 0;
    FarMsg m;
    for (int r; (r = far_next(b, &at, &m)) != 0;) {   // (first: has it all come?)
        if (r < 0) break;
        if (m.type == 'L' || m.type == 'X') {
            *ended = true;
            break;
        }
    }
    if (!*ended) {
        if (at < b.len && far_next(b, &at, &m) < 0) return *ended = true, err_set(err, "%s: the lister said something malformed", dir), false;
        return false;
    }
    if (m.type == 'X') return err_set(err, "%s: %.*s", dir, STR_ARG(m.text)), false;
    Entry *e = arena_try_new(a, Entry, cap);
    if (!e) return err_set(err, "%s: too many entries for memory", dir), false;
    at = 0;
    for (; far_next(b, &at, &m) == 1 && m.type != 'L';) {
        if (m.type == 'W') *can_write = m.v[0] == 1;
        if (m.type == 'H') out->hidden = m.v[0];
        if (m.type != 'E') continue;
        if (n == cap) {
            Entry *grown = arena_try_new(a, Entry, cap * 2);
            if (!grown) return err_set(err, "%s: too many entries for memory", dir), false;
            memcpy(grown, e, (size_t)n * sizeof *e);
            e = grown, cap *= 2;
        }
        char *name = arena_cstr(a, m.name);   // (aborts when full: as fs_list's names, checked below)
        char *target = m.target.len ? arena_cstr(a, m.target) : NULL;
        e[n++] = (Entry){
            .name = { name, m.name.len }, .kind = (Kind)m.v[0], .size = m.v[1], .mtime = m.v[2],
            .mode = (u32)m.v[3], .uid = (u32)m.v[4], .gid = (u32)m.v[5],
            .target = target ? (Str){ target, m.target.len } : STR_EMPTY,
        };
    }
    out->e = e, out->n = n, *after = at;
    return true;
}

// An entry as E says it.
static void far_put_entry(FILE *f, const Entry *e)
{
    fprintf(f, "E %d %lld %lld %u %u %u %td %td\n", (int)e->kind, (long long)e->size, (long long)e->mtime, e->mode, e->uid, e->gid,
            e->name.len, e->target.len);
    fwrite(e->name.p, 1, (size_t)e->name.len, f);
    fwrite(e->target.p, 1, (size_t)e->target.len, f);
}

// ofx-list's work: dir listed onto out (dot-files only when hidden), then the counts asked on in
// answered until its end. a holds one entry at a time. 0, or 1 when dir can't be read.
static int far_serve(FILE *in, FILE *out, Arena *a, const char *dir, bool hidden)
{
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(out, "X %d %s\n", errno, strerror(errno)), fflush(out);
        return 1;
    }
    fprintf(out, "W %d\n", access(dir, W_OK) == 0);
    isize dot = 0;
    for (struct dirent *de; (de = readdir(d));) {
        const char *nm = de->d_name;
        if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) continue;
        if (nm[0] == '.') {
            dot++;
            if (!hidden) continue;
        }
        ArenaMark mk = arena_mark(a);
        Entry e;
        if (entry_read(a, dirfd(d), nm, &e)) far_put_entry(out, &e);
        arena_restore(a, mk);
    }
    closedir(d);
    fprintf(out, "H %td\nL\n", dot), fflush(out);
    char line[FAR_LINE_MAX], name[NAME_MAX + 1], path[PATH_MAX];
    while (fgets(line, sizeof line, in)) {
        long long gen, idx, len;
        int used = 0;
        if (sscanf(line, "C %lld %lld %lld%n", &gen, &idx, &len, &used) != 3 || line[used] != '\n' || len < 1 || len > NAME_MAX) break;
        if (fread(name, 1, (size_t)len, in) != (size_t)len) break;
        name[len] = 0;
        isize n = strlen(name) == (size_t)len && !strchr(name, '/') && snprintf(path, sizeof path, "%s/%s", dir, name) < (int)sizeof path
                      ? count_entries(path, hidden)
                      : -1;
        fprintf(out, "C %lld %lld %td\n", gen, idx, n), fflush(out);
    }
    return 0;
}

// A count's question, as ofx-list reads it, into buf; its length, or 0 when it doesn't fit.
static isize far_count_ask(char *buf, size_t cap, u32 gen, isize idx, Str name)
{
    int n = snprintf(buf, cap, "C %u %td %td\n", gen, idx, name.len);
    if (n < 0 || (size_t)n + (size_t)name.len > cap || name.len < 1 || name.len > NAME_MAX) return 0;
    memcpy(buf + n, name.p, (size_t)name.len);
    return n + name.len;
}
