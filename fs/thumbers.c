// The system's thumbnailers (F12 step 3, docs/plan.md "F12 design"): with ofx.conf's
// preview-thumbnailers on, a type ofx doesn't decode itself (a video, a PDF, an office file, a
// HEIF or SVG image) gets its preview from the program other file managers run for it, as the
// .thumbnailer files in every XDG data dir's thumbnailers/ say (the user's own first; a type's
// first one wins). Its PNG goes into the cache as ofx's own (thumb.c), so Nemo and Nautilus read it.
//
// A video or a PDF from a download is parsed by ffmpeg or poppler, so each runs in bubblewrap
// (sandbox.c), as GNOME's do (Nemo's don't); without bubblewrap installed none runs (Michael,
// 2026-11-02). It has THUMB_RUN_MS; then it is killed and nothing is written: a big file under load
// may make it next time. One that ends without a PNG counts as failed, so it isn't tried again
// until the file changes (the rule from Nemo's loop).
//
// With the test guard on (OFX_GUI_ROOT), only OFX_TEST_THUMBNAILERS's files are read (none
// without it), and ops_program_allowed runs only a test's own program.

#include "../base/base.h"

#include <dirent.h>
#include <unistd.h>

enum { THUMBERS_MAX = 64, THUMBER_TYPES_MAX = 2048, THUMBER_EXEC_MAX = 512, THUMBER_TYPE_MAX = 128, THUMBER_FILES_MAX = 256,
       THUMB_RUN_MS = 10000, THUMB_ARGS_MAX = 48 };

typedef struct {
    int  n, ntypes;
    char exec[THUMBERS_MAX][THUMBER_EXEC_MAX];   // each one's Exec line, as written
    struct {
        char type[THUMBER_TYPE_MAX];
        int  at;   // its thumbnailer, in exec
    } types[THUMBER_TYPES_MAX];
} Thumbers;

static int thumb_run_ms = THUMB_RUN_MS;   // (OFX_TEST_THUMB_RUN_MS, gui/: a test's hung fake killed sooner)

static int thumbers_name_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

// One .thumbnailer file's text into t: its Exec and each of its types not taken yet. Skipped when
// it has no Exec or MimeType, or its program (TryExec, else Exec's first word) isn't installed.
static void thumbers_add(Arena *a, Thumbers *t, Str text)
{
    Str g = S("Thumbnailer Entry"), exec = ini_get(text, g, S("Exec")), types = ini_get(text, g, S("MimeType")), tryx = ini_get(text, g, S("TryExec"));
    if (!exec.len || exec.len >= THUMBER_EXEC_MAX || !types.len || t->n == THUMBERS_MAX) return;
    const char *w[THUMB_ARGS_MAX];
    char prog[PATH_MAX];
    if (tryx.len) snprintf(prog, sizeof prog, "%.*s", STR_ARG(tryx));
    else if (editor_words(a, exec, w, THUMB_ARGS_MAX) > 0) snprintf(prog, sizeof prog, "%s", w[0]);
    else return;
    if (prog[0] == '/' ? access(prog, X_OK) != 0 : !on_path(prog)) return;
    int at = t->n++;
    snprintf(t->exec[at], THUMBER_EXEC_MAX, "%.*s", STR_ARG(exec));
    for (isize i = 0; i < types.len;) {
        isize e = i;
        while (e < types.len && types.p[e] != ';') e++;
        Str ty = str_cut(types, i, e);
        i = e + 1;
        if (!ty.len || ty.len >= THUMBER_TYPE_MAX || t->ntypes == THUMBER_TYPES_MAX) continue;
        bool have = false;
        for (int k = 0; k < t->ntypes && !have; k++) have = str_eq(ty, str_from_c(t->types[k].type));
        if (have) continue;
        snprintf(t->types[t->ntypes].type, THUMBER_TYPE_MAX, "%.*s", STR_ARG(ty));
        t->types[t->ntypes++].at = at;
    }
}

// dir's .thumbnailer files, by name.
static void thumbers_dir(Arena *a, Thumbers *t, const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return;
    char *names[THUMBER_FILES_MAX];
    int n = 0;
    for (struct dirent *de; (de = readdir(d)) && n < THUMBER_FILES_MAX;)
        if (str_ends_with(str_from_c(de->d_name), S(".thumbnailer"))) names[n++] = arena_cstr(a, str_from_c(de->d_name));
    closedir(d);
    qsort(names, (size_t)n, sizeof names[0], thumbers_name_cmp);
    for (int i = 0; i < n; i++) {
        char path[PATH_MAX];
        if (snprintf(path, sizeof path, "%s/%s", dir, names[i]) >= (int)sizeof path) continue;
        thumbers_add(a, t, read_file(a, path));
    }
}

// Every thumbnailer installed, into t (a scratch arena for the reading).
static void thumbers_load(Arena *scratch, Thumbers *t)
{
    t->n = t->ntypes = 0;
    ArenaMark m = arena_mark(scratch);
    if (ops_root[0]) {
        const char *d = getenv("OFX_TEST_THUMBNAILERS");
        if (d) thumbers_dir(scratch, t, d);
    } else {
        const char *dirs[MIME_DIRS_MAX];
        isize nd = data_dirs(scratch, dirs, MIME_DIRS_MAX);
        for (isize k = 0; k < nd; k++) thumbers_dir(scratch, t, arena_sprintf(scratch, "%s/thumbnailers", dirs[k]));
    }
    arena_restore(scratch, m);
}

// The Exec line of type's thumbnailer, or NULL.
static const char *thumbers_find(const Thumbers *t, Str type)
{
    for (int k = 0; k < t->ntypes; k++)
        if (str_eq(type, str_from_c(t->types[k].type))) return t->exec[t->types[k].at];
    return NULL;
}

// What field c stands for (thumbers_argv), or NULL.
static const char *thumbers_field(char c, const char *path, const char *uri, const char *out, const char *size)
{
    switch (c) {
    case 'i': return path;
    case 'u': return uri;
    case 'o': return out;
    case 's': return size;
    case '%': return "%";
    default: return NULL;
    }
}

// exec's words with the spec's fields filled in, into argv (cap words and the NULL; the words in
// a): %i the file's path, %u its URI, %o the PNG to write, %s the size, %% a %. Its count, or -1
// for a field it doesn't know, a quote not closed, or too many words.
static int thumbers_argv(Arena *a, const char *exec, const char *path, const char *uri, const char *out, int size, const char **argv, int cap)
{
    const char *w[THUMB_ARGS_MAX];
    int n = editor_words(a, str_from_c(exec), w, THUMB_ARGS_MAX);
    if (n <= 0 || n >= cap) return -1;
    char num[16];
    snprintf(num, sizeof num, "%d", size);
    for (int k = 0; k < n; k++) {
        isize len = 0;   // (its length first, then the word)
        for (const char *p = w[k]; *p; p++) {
            if (*p != '%') { len++; continue; }
            const char *put = thumbers_field(*++p, path, uri, out, num);
            if (!put) return -1;
            len += (isize)strlen(put);
        }
        char *o = arena_try_push(a, len + 1, 1);
        if (!o) return -1;
        isize at = 0;
        for (const char *p = w[k]; *p; p++) {
            if (*p != '%') { o[at++] = *p; continue; }
            const char *put = thumbers_field(*++p, path, uri, out, num);
            size_t m = strlen(put);
            memcpy(o + at, put, m), at += (isize)m;
        }
        o[at] = 0;
        argv[k] = o;
    }
    argv[n] = NULL;
    return n;
}

// argv run on in, writing in dir only (sandbox.c), for thumb_run_ms. Whether it made a PNG is the
// caller's to see.
static BoxRun thumb_run(const char *const *argv, const char *in, const char *dir)
{
    BoxJob j = { .argv = argv, .ro = { in }, .rw = { dir }, .ms = thumb_run_ms };
    return box_run(&j);
}
