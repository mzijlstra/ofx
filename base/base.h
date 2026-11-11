// ofx base (copied from uml-viewer): types, strings, arenas, UTF-8 and time, shared by every module.
//
// Memory: our code never calls malloc. Every allocation comes from an arena backed by a static
// buffer and is released only by resetting the whole arena. Strings are Str slices into memory
// someone else owns, so they are never freed one by one.
#ifndef OFX_BASE_H
#define OFX_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t   u8;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int64_t   i64;
typedef ptrdiff_t isize;

#define COUNT(a) ((isize)(sizeof(a) / sizeof((a)[0])))
#define KiB(n)   ((isize)(n) << 10)
#define MiB(n)   ((isize)(n) << 20)

// ---- strings --------------------------------------------------------------------------------

// A pointer and a length. Never owned and not NUL-terminated; print with "%.*s", STR_ARG(s).
typedef struct { const char *p; isize len; } Str;

// S() takes a string literal only ("" lit won't compile with a pointer, where sizeof would be its size).
#define S(lit)     ((Str){ ("" lit), (isize)sizeof("" lit) - 1 })
#define STR_EMPTY  ((Str){ "", 0 })   /* absent values: .p is never NULL, so "%.*s" is defined */
#define STR_ARG(s) (int)(s).len, (s).p

bool str_eq(Str a, Str b);
Str  str_from_c(const char *c);
Str  str_trim(Str s);
bool str_starts_with(Str s, Str prefix);
bool str_ends_with(Str s, Str suffix);
Str  str_cut(Str s, isize from, isize to);   // s[from..to), clamped to s
isize str_find(Str s, char c);               // index of the first c, or -1
isize str_find_last(Str s, char c);          // index of the last c, or -1
isize str_find_str(Str s, Str w);            // index of the first w in s, or -1 (w empty: 0)

// The code point at s[*i], advancing *i past it. Reads only s[*i .. len-1]. Malformed or cut-off
// UTF-8 (overlongs, surrogates, past U+10FFFF, a lead without its continuations) yields U+FFFD and
// advances one byte, so a caller always makes progress.
u32  utf8_next(const char *s, isize len, isize *i);

// ---- errors ---------------------------------------------------------------------------------

typedef struct { char msg[512]; } Err;

void err_set(Err *e, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

// ---- arena ----------------------------------------------------------------------------------

typedef struct {
    u8         *base;
    isize       cap;
    isize       used;
    const char *name;   // for the out-of-memory message only
} Arena;

void  arena_init(Arena *a, const char *name, void *mem, isize cap);
void  arena_reset(Arena *a);
void *arena_push(Arena *a, isize size, isize align);   // zeroed; aborts when the arena is full
// The same, but NULL when the arena is full (or size is negative): for loading files, where a
// project larger than planned is an error to report, not a bug to crash on.
void *arena_try_push(Arena *a, isize size, isize align);
char *arena_sprintf(Arena *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
char *arena_cstr(Arena *a, Str s);

// For writers that cannot know their size up front (vsnprintf, read): arena_tail opens the whole
// free tail for writing, arena_commit keeps the first n bytes of it and closes the rest again.
// Every arena_tail must be followed by an arena_commit, with 0 to keep nothing.
char *arena_tail(Arena *a, isize *avail);
void  arena_commit(Arena *a, isize n);

// arena_mark / arena_restore: release everything pushed since the mark, poisoning it again. For
// scratch work, such as scanning one file of many.
typedef struct { isize used; } ArenaMark;
ArenaMark arena_mark(const Arena *a);
void      arena_restore(Arena *a, ArenaMark m);

#define arena_new(a, T, n) \
    ((T *)arena_push((a), (isize)sizeof(T) * (isize)(n), (isize)_Alignof(T)))
#define arena_try_new(a, T, n) \
    ((T *)arena_try_push((a), (isize)sizeof(T) * (isize)(n), (isize)_Alignof(T)))

// ---- paths --------------------------------------------------------------------------------
//
// Project paths are '/'-separated and relative to the project root, without a leading "./".

// dir joined with rel, then normalised: "." segments dropped, ".." taking back the segment before
// it, repeated slashes collapsed. A rel starting with '/' replaces dir. False when ".." would climb
// above the start, or when the arena is full; *out goes into the arena.
bool path_join(Arena *a, Str dir, Str rel, Str *out);
Str  path_dir(Str path);    // "a/b/c.ts" -> "a/b"; "c.ts" -> ""
Str  path_base(Str path);   // "a/b/c.ts" -> "c.ts"

// ---- string table ---------------------------------------------------------------------------
//
// Str to an int, by open addressing in an arena-backed slot array of fixed capacity (a power of two,
// at least twice the expected count). Keys are not copied: they must outlive the table.

typedef struct {
    Str  *keys;
    int  *vals;
    isize cap, n;
} StrMap;

bool strmap_init(StrMap *m, Arena *a, isize expected);   // false when the arena is full
// The slot for key: false when the table is full (n == cap / 2, kept so probes stay short).
bool strmap_put(StrMap *m, Str key, int val);
bool strmap_get(const StrMap *m, Str key, int *val);
u64  str_hash(Str s);

// ---- time -----------------------------------------------------------------------------------

i64 now_ms(void);   // monotonic milliseconds

#endif
