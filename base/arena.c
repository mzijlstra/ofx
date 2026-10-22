#include "base.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// Under AddressSanitizer the free tail of every arena is poisoned, so a read or write past the
// last allocation is reported on the spot, exactly as for a heap block. Without it, an overrun
// lands in the arena's own free space (or stale data from the previous refresh) and goes unseen.
#if defined(__SANITIZE_ADDRESS__)
#define ARENA_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ARENA_ASAN 1
#endif
#endif

#ifdef ARENA_ASAN
#include <sanitizer/asan_interface.h>
#define POISON(p, n)   __asan_poison_memory_region((p), (size_t)(n))
#define UNPOISON(p, n) __asan_unpoison_memory_region((p), (size_t)(n))
#else
#define POISON(p, n)   ((void)(p), (void)(n))
#define UNPOISON(p, n) ((void)(p), (void)(n))
#endif

void arena_init(Arena *a, const char *name, void *mem, isize cap)
{
    a->base = mem;
    a->cap  = cap;
    a->used = 0;
    a->name = name;
    POISON(a->base, a->cap);
}

// Only the used part needs poisoning again: the free tail is poisoned already, since every
// operation that opens it (push, tail) closes the rest again (push, commit). A reset of a large,
// mostly empty arena is then cheap, which the fuzzers do two or three times per input.
void arena_reset(Arena *a)
{
    POISON(a->base, a->used);
    a->used = 0;
}

// Arenas are sized for the worst case we expect. Running out means a bug or a project far larger
// than planned, and handing back NULL would only move the crash somewhere harder to read.
static void arena_full(const Arena *a, isize asked)
{
    fprintf(stderr, "ofx: arena '%s' is full (%td of %td bytes used, %td more asked)\n",
            a->name, a->used, a->cap, asked);
    abort();
}

void *arena_try_push(Arena *a, isize size, isize align)
{
    isize start = (a->used + align - 1) & ~(align - 1);
    if (size < 0 || start > a->cap - size) return NULL;
    a->used = start + size;
    void *p = a->base + start;
    UNPOISON(p, size);
    memset(p, 0, (size_t)size);
    return p;
}

void *arena_push(Arena *a, isize size, isize align)
{
    void *p = arena_try_push(a, size, align);
    if (!p) arena_full(a, size);
    return p;
}

char *arena_tail(Arena *a, isize *avail)
{
    *avail = a->cap - a->used;
    UNPOISON(a->base + a->used, *avail);
    return (char *)a->base + a->used;
}

void arena_commit(Arena *a, isize n)
{
    if (n < 0 || n > a->cap - a->used) arena_full(a, n);
    a->used += n;
    POISON(a->base + a->used, a->cap - a->used);
}

char *arena_sprintf(Arena *a, const char *fmt, ...)
{
    isize avail;
    char *dst = arena_tail(a, &avail);

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(dst, (size_t)avail, fmt, ap);
    va_end(ap);

    // vsnprintf fails only on an encoding error (a wide string it cannot convert), which no
    // format here can produce; treat it as the bug it would be.
    if (n < 0) {
        fprintf(stderr, "ofx: arena_sprintf: encoding error in \"%s\"\n", fmt);
        abort();
    }
    if ((isize)n + 1 > avail) arena_full(a, (isize)n + 1);
    arena_commit(a, (isize)n + 1);
    return dst;
}

char *arena_cstr(Arena *a, Str s)
{
    char *p = arena_push(a, s.len + 1, 1);
    if (s.len) memcpy(p, s.p, (size_t)s.len);
    return p;
}

ArenaMark arena_mark(const Arena *a)
{
    return (ArenaMark){ a->used };
}

void arena_restore(Arena *a, ArenaMark m)
{
    if (m.used < 0 || m.used > a->used) arena_full(a, m.used - a->used);   // not a mark of this arena's past
    POISON(a->base + m.used, a->used - m.used);
    a->used = m.used;
}
