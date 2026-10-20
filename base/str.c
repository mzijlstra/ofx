#include "base.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

bool str_eq(Str a, Str b)
{
    return a.len == b.len && (a.len == 0 || memcmp(a.p, b.p, (size_t)a.len) == 0);
}

Str str_from_c(const char *c)
{
    return c ? (Str){ c, (isize)strlen(c) } : STR_EMPTY;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

Str str_trim(Str s)
{
    while (s.len && is_space(s.p[0])) s.p++, s.len--;
    while (s.len && is_space(s.p[s.len - 1])) s.len--;
    return s;
}

bool str_starts_with(Str s, Str prefix)
{
    return s.len >= prefix.len && memcmp(s.p, prefix.p, (size_t)prefix.len) == 0;
}

bool str_ends_with(Str s, Str suffix)
{
    return s.len >= suffix.len && memcmp(s.p + s.len - suffix.len, suffix.p, (size_t)suffix.len) == 0;
}

Str str_cut(Str s, isize from, isize to)
{
    if (to > s.len) to = s.len;
    if (from < 0) from = 0;
    if (from > to) from = to;
    return (Str){ s.p + from, to - from };
}

isize str_find(Str s, char c)
{
    for (isize i = 0; i < s.len; i++)
        if (s.p[i] == c) return i;
    return -1;
}

isize str_find_last(Str s, char c)
{
    for (isize i = s.len - 1; i >= 0; i--)
        if (s.p[i] == c) return i;
    return -1;
}

u32 utf8_next(const char *s, isize len, isize *i)
{
    const u8 *p = (const u8 *)s + *i;
    isize     n = len - *i;
    u8        c = p[0];
    if (c < 0x80) return *i += 1, c;

    int need;
    u32 cp;
    u8  lo = 0x80, hi = 0xBF;   // the second byte's range, narrowed against overlongs and surrogates
    if (c >= 0xC2 && c <= 0xDF) need = 1, cp = c & 0x1F;
    else if (c >= 0xE0 && c <= 0xEF) {
        need = 2, cp = c & 0x0F;
        if (c == 0xE0) lo = 0xA0;
        if (c == 0xED) hi = 0x9F;
    } else if (c >= 0xF0 && c <= 0xF4) {
        need = 3, cp = c & 0x07;
        if (c == 0xF0) lo = 0x90;
        if (c == 0xF4) hi = 0x8F;
    } else {
        return *i += 1, 0xFFFD;
    }
    if (n <= need || p[1] < lo || p[1] > hi) return *i += 1, 0xFFFD;
    for (int k = 1; k <= need; k++) {
        if ((p[k] & 0xC0) != 0x80) return *i += 1, 0xFFFD;
        cp = cp << 6 | (p[k] & 0x3F);
    }
    *i += 1 + need;
    return cp;
}

void err_set(Err *e, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->msg, sizeof e->msg, fmt, ap);
    va_end(ap);
}

i64 now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (i64)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

// ---- paths --------------------------------------------------------------------------------

Str path_dir(Str path)
{
    isize i = str_find_last(path, '/');
    return i < 0 ? STR_EMPTY : str_cut(path, 0, i);
}

Str path_base(Str path)
{
    return str_cut(path, str_find_last(path, '/') + 1, path.len);
}

bool path_join(Arena *a, Str dir, Str rel, Str *out)
{
    // A rel starting with '/' is absolute: dir does not apply.
    bool absolute = rel.len && rel.p[0] == '/';
    if (absolute) dir = STR_EMPTY;
    else if (!dir.len) absolute = false;
    else absolute = dir.p[0] == '/';
    isize cap = dir.len + rel.len + 2;
    char *buf = arena_try_push(a, cap, 1);
    if (!buf) return false;
    // Segment start offsets in buf, to step back on "..".
    isize o = 0;
    if (absolute) buf[o++] = '/';
    isize base = o;
    for (int part = 0; part < 2; part++) {
        Str s = part ? rel : dir;
        for (isize i = 0; i < s.len;) {
            while (i < s.len && s.p[i] == '/') i++;
            isize start = i;
            while (i < s.len && s.p[i] != '/') i++;
            Str seg = str_cut(s, start, i);
            if (!seg.len || str_eq(seg, S("."))) continue;
            if (str_eq(seg, S(".."))) {
                if (o == base) return false;   // above the start
                while (o > base && buf[o - 1] != '/') o--;
                if (o > base) o--;   // the slash before the dropped segment
                continue;
            }
            if (o > base) buf[o++] = '/';
            memcpy(buf + o, seg.p, (size_t)seg.len);
            o += seg.len;
        }
    }
    *out = (Str){ buf, o };
    return true;
}

// ---- string table ---------------------------------------------------------------------------

u64 str_hash(Str s)
{
    u64 h = 1469598103934665603ull;
    for (isize i = 0; i < s.len; i++) h = (h ^ (u8)s.p[i]) * 1099511628211ull;
    h ^= h >> 33, h *= 0xff51afd7ed558ccdull;
    return h ^ (h >> 33);
}

bool strmap_init(StrMap *m, Arena *a, isize expected)
{
    isize cap = 16;
    while (cap < 2 * expected) cap *= 2;
    *m = (StrMap){ .cap = cap };
    m->keys = arena_try_new(a, Str, cap);
    m->vals = arena_try_new(a, int, cap);
    return m->keys && m->vals;
}

// The slot holding key, or the empty one where it would go.
static isize strmap_slot(const StrMap *m, Str key)
{
    isize i = (isize)(str_hash(key) & (u64)(m->cap - 1));
    while (m->keys[i].p && !str_eq(m->keys[i], key)) i = (i + 1) & (m->cap - 1);
    return i;
}

bool strmap_put(StrMap *m, Str key, int val)
{
    isize i = strmap_slot(m, key);
    if (!m->keys[i].p) {
        if (m->n >= m->cap / 2) return false;
        m->keys[i] = key.p ? key : STR_EMPTY;
        m->n++;
    }
    m->vals[i] = val;
    return true;
}

bool strmap_get(const StrMap *m, Str key, int *val)
{
    isize i = strmap_slot(m, key);
    if (!m->keys[i].p) return false;
    *val = m->vals[i];
    return true;
}
