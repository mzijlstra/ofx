// Open at Line (F9 step 4, docs/plan.md "Steps 3 and 4 decided"): which editor opens a search
// result at its line, and how. Omarchy's default editor (as `omarchy default editor` sets it), else
// $EDITOR's program, else nvim; its recipe from ofx.conf's editor lines (F10) or the older
// $XDG_CONFIG_HOME/ofx/editors when they name it, else the built-in ones. A recipe is a command, word by word, with {file}, {line} and {col}
// (characters, from 1) filled in within each word, never through a shell, and whether it runs in a
// terminal. No loadable code: a recipe's program may be a script (Michael, 2026-10-29).

#include "../base/base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { EDITOR_WORDS = 16, EDITORS_USER = 32, EDITOR_NAME_MAX = 64, EDITOR_ARGS_MAX = 8192 };

typedef struct {
    const char *name;                // Omarchy's name for it (its state file), or $EDITOR's program
    const char *label;               // in the menu: "Open at Line in Neovim"
    bool        term;                // run in a terminal
    const char *argv[EDITOR_WORDS];  // the command, NULL-ended
} Editor;

// Each one's line and column as its own manual has them (2026-10-29). nvim and vim take
// setcursorcharpos, which counts characters as ofx does (cursor() counts bytes: after "éé" it
// landed two short, tried in nvim 0.12). Omarchy's editor list (code, cursor, zeditor,
// sublime_text, helix, vim, emacs, nvim) and the usual others.
static const Editor EDITORS[] = {
    { "nvim", "Neovim", true, { "nvim", "+call setcursorcharpos({line},{col})", "{file}" } },
    { "vim", "Vim", true, { "vim", "+call setcursorcharpos({line},{col})", "{file}" } },
    { "helix", "Helix", true, { "helix", "{file}:{line}:{col}" } },
    { "hx", "Helix", true, { "hx", "{file}:{line}:{col}" } },
    { "micro", "micro", true, { "micro", "+{line}:{col}", "{file}" } },
    { "nano", "nano", true, { "nano", "+{line},{col}", "{file}" } },
    { "kak", "Kakoune", true, { "kak", "+{line}:{col}", "{file}" } },
    { "emacs", "Emacs", false, { "emacs", "+{line}:{col}", "{file}" } },
    { "code", "VS Code", false, { "code", "--goto", "{file}:{line}:{col}" } },
    { "codium", "VSCodium", false, { "codium", "--goto", "{file}:{line}:{col}" } },
    { "cursor", "Cursor", false, { "cursor", "--goto", "{file}:{line}:{col}" } },
    { "zeditor", "Zed", false, { "zeditor", "{file}:{line}:{col}" } },
    { "zed", "Zed", false, { "zed", "{file}:{line}:{col}" } },
    { "sublime_text", "Sublime Text", false, { "sublime_text", "{file}:{line}:{col}" } },
    { "subl", "Sublime Text", false, { "subl", "{file}:{line}:{col}" } },
};

// The user's recipes, read from the editors file: one a line, `name terminal command…`, terminal
// `yes` or `no`, the command's words split at spaces and tabs, "double quotes" around a word with
// spaces in it, `~/` at a word's start the home folder, # a comment line. *bad: the first line
// that isn't one (0: none). Their strings are in a.
typedef struct {
    Editor e[EDITORS_USER];
    int    n;
    int    bad;
} Editors;

// The words of line, into w (at most cap; quotes kept out), each NUL-terminated in a; their count,
// -1 when a quote isn't closed or there are too many.
static int editor_words(Arena *a, Str line, const char **w, int cap)
{
    int n = 0;
    for (isize i = 0; i < line.len;) {
        while (i < line.len && (line.p[i] == ' ' || line.p[i] == '\t')) i++;
        if (i == line.len) break;
        if (n == cap) return -1;
        isize len = 0;   // (the word's bytes first, quotes out: only those pushed, review, 2026-11-01)
        bool quoted = false;
        for (isize j = i; j < line.len && (quoted || (line.p[j] != ' ' && line.p[j] != '\t')); j++) {
            if (line.p[j] == '"') quoted = !quoted;
            else len++;
        }
        char *out = arena_try_push(a, len + 1, 1);
        if (!out) return -1;
        isize k = 0;
        quoted = false;
        for (; i < line.len && (quoted || (line.p[i] != ' ' && line.p[i] != '\t')); i++) {
            if (line.p[i] == '"') quoted = !quoted;
            else out[k++] = line.p[i];
        }
        if (quoted) return -1;
        out[k] = 0;
        w[n++] = out;
    }
    return n;
}

// text's recipes added to out; out->bad, when it has none yet, the first line of text that isn't one.
static void editors_add(Arena *a, Str text, Editors *out)
{
    int no = 0;
    isize at = 0;
    for (Str line; next_line(text, &at, &line);) {
        line = str_trim(line), no++;
        if (line.len == 0 || line.p[0] == '#') continue;
        const char *w[EDITOR_WORDS + 2];
        int n = editor_words(a, line, w, COUNT(w) - 1);
        bool term = n >= 3 && strcmp(w[1], "yes") == 0;
        if (n < 3 || (!term && strcmp(w[1], "no") != 0) || strlen(w[0]) >= EDITOR_NAME_MAX || out->n == EDITORS_USER) {
            if (!out->bad) out->bad = no;
            continue;
        }
        Editor *ed = &out->e[out->n++];
        *ed = (Editor){ .name = w[0], .label = w[0], .term = term };
        for (int k = 2; k < n; k++) ed->argv[k - 2] = w[k];
    }
}

// text's recipes into out (none and no bad line when it is empty).
static void editors_parse(Arena *a, Str text, Editors *out)
{
    *out = (Editors){ 0 };
    editors_add(a, text, out);
}

// The editors file: $XDG_CONFIG_HOME/ofx/editors, ~/.config/ofx/editors without it. Still read
// after ofx.conf's editor lines, which replace it (F10).
static bool editors_path(char *out, size_t cap)
{
    return xdg_dir(out, cap, "XDG_CONFIG_HOME", ".config", "ofx/editors");
}

// The editor to open at a line with, by name, into out: Omarchy's default (its state file,
// $XDG_STATE_HOME/omarchy/defaults/editor, as ofx reads Omarchy's theme from there), else
// $EDITOR's program (its first word, without its folder), else nvim.
static void editor_default(Arena *a, char *out, size_t cap)
{
    char p[PATH_MAX];
    Str name = STR_EMPTY;
    if (xdg_dir(p, sizeof p, "XDG_STATE_HOME", ".local/state", "omarchy/defaults/editor")) {
        Str f = read_file(a, p);
        isize e = 0;
        while (e < f.len && f.p[e] != '\n') e++;
        name = str_trim(str_cut(f, 0, e));
    }
    const char *env = getenv("EDITOR");
    if (name.len == 0 && env) {
        Str s = str_trim(str_from_c(env));
        isize e = 0;
        while (e < s.len && s.p[e] != ' ' && s.p[e] != '\t') e++;
        name = str_cut(s, 0, e);
    }
    name = path_base(name);
    if (name.len == 0 || name.len >= (isize)cap) name = S("nvim");
    snprintf(out, cap, "%.*s", STR_ARG(name));
}

// The recipe for name: the user's first, then the built-in ones; NULL when neither has it.
static const Editor *editor_find(const Editors *user, const char *name)
{
    for (int i = 0; user && i < user->n; i++)
        if (strcmp(user->e[i].name, name) == 0) return &user->e[i];
    for (isize i = 0; i < COUNT(EDITORS); i++)
        if (strcmp(EDITORS[i].name, name) == 0) return &EDITORS[i];
    return NULL;
}

// e's command for file at line and col, into argv (cap words and the NULL; the words themselves in
// buf): each word with {file}, {line} and {col} filled in, `~/` at its start the home folder; the
// file as a last word of its own when no word has {file}. Its count, or -1 when it doesn't fit.
static int editor_argv(const Editor *e, const char *file, i64 line, i64 col, char *buf, size_t cap, const char **argv, int words)
{
    size_t at = 0;
    int n = 0;
    bool has_file = false;
    const char *home = getenv("HOME");
    for (int k = 0; k < EDITOR_WORDS && e->argv[k]; k++) {
        if (n >= words - 2) return -1;   // (room for the file, and the NULL)
        char *out = buf + at;
        const char *w = e->argv[k];
        size_t m = 0;
        if (w[0] == '~' && w[1] == '/' && home && home[0] == '/') {
            int r = snprintf(out, cap - at, "%s", home);
            if (r < 0 || (size_t)r >= cap - at) return -1;
            m = (size_t)r, w++;
        }
        for (const char *p = w; *p;) {
            char num[24];
            const char *put = NULL;
            size_t skip = 1;
            if (strncmp(p, "{file}", 6) == 0) put = file, skip = 6, has_file = true;
            else if (strncmp(p, "{line}", 6) == 0) snprintf(num, sizeof num, "%lld", (long long)line), put = num, skip = 6;
            else if (strncmp(p, "{col}", 5) == 0) snprintf(num, sizeof num, "%lld", (long long)col), put = num, skip = 5;
            size_t len = put ? strlen(put) : 1;
            if (at + m + len + 1 > cap) return -1;
            memcpy(out + m, put ? put : p, len);
            m += len, p += skip;
        }
        out[m] = 0;
        argv[n++] = out;
        at += m + 1;
    }
    if (!has_file) {
        size_t len = strlen(file);
        if (at + len + 1 > cap) return -1;
        memcpy(buf + at, file, len + 1);
        argv[n++] = buf + at;
    }
    argv[n] = NULL;
    return n;
}

// Opens with e: argv (editor_argv's, n words) through uwsm-app as every launch (spawn_app); a
// terminal editor in a terminal, as Omarchy's omarchy-launch-tui runs one (`xdg-terminal-exec
// --app-id=org.omarchy.<program> -e …`). False, with the reason, when its program isn't there.
// Each word stays one word as far as the terminal: uwsm-app and xdg-terminal-exec pass them on as
// they are, and foot (Omarchy's) runs -e's without a shell. A terminal whose -e joins them into a
// shell line would split a path with a space (review, 2026-10-29).
static bool editor_open(const Editor *e, const char *const *argv, int n, Err *err)
{
    const char *prog = argv[0];
    if (strchr(prog, '/') ? access(prog, X_OK) != 0 : !on_path(prog)) return err_set(err, "%s isn't installed (%s)", e->label, prog), false;
    if (!e->term) return spawn_app(argv, n, err);
    char id[PATH_MAX + 32];
    snprintf(id, sizeof id, "--app-id=org.omarchy.%s", strrchr(prog, '/') ? strrchr(prog, '/') + 1 : prog);
    const char *cmd[EDITOR_WORDS + 4] = { "xdg-terminal-exec", id, "-e" };
    int k = 3;
    for (int i = 0; i < n && k < COUNT(cmd); i++) cmd[k++] = argv[i];
    return spawn_app(cmd, k, err);
}
