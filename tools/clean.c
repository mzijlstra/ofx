// ofx-clean [--all] [--dry-run]: the thumbnail cache's cleanup (F6.5, fs/clean.c), run daily by
// ofx-clean.timer, by ofx.conf's clean options (F10; it reads the file, never writes it: without
// one, the built-in defaults). One summary line for the journal; --dry-run lists each file it would
// delete and deletes nothing. --all deletes every preview, the options and clean = off aside (View
// → Clear Previews…, F12, runs it and shows its line). No window: fs/ only. OFX_TEST_MOUNTINFO names
// another mount list (the tests').
#include "../base/base.c"
#include "../fs/fs.c"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static u8 mem[MiB(64)];

static void say(void *ctx, CleanRule r, const char *path, i64 size)
{
    (void)ctx;
    printf("%-6s %9lld %s\n", clean_rule_names[r], (long long)size, path);
}

static double mib(i64 b) { return (double)b / (1 << 20); }

int main(int argc, char **argv)
{
    bool dry = false, all = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dry-run") == 0 && !dry) dry = true;
        else if (strcmp(argv[i], "--all") == 0 && !all) all = true;
        else return fprintf(stderr, "usage: ofx-clean [--all] [--dry-run]\n"), 2;
    }
    Arena a;
    arena_init(&a, "clean", mem, sizeof mem);
    char root[PATH_MAX];
    if (!thumb_root(root, sizeof root)) return fprintf(stderr, "ofx-clean: no cache folder (HOME unset)\n"), 1;
    ops_guard(getenv("OFX_GUI_ROOT"));
    Conf c;
    char cp[PATH_MAX];
    if (!conf_path(cp, sizeof cp)) snprintf(cp, sizeof cp, "ofx.conf");
    conf_load(&a, &c);
    if (c.bad) fprintf(stderr, "ofx-clean: %s line %d skipped: %s\n", cp, c.bad, c.why);
    if (!c.clean && !all) return printf("ofx-clean: off (clean = off in %s)\n", cp), 0;
    const char *mi = getenv("OFX_TEST_MOUNTINFO");
    Str mountinfo = read_file(&a, mi && mi[0] ? mi : "/proc/self/mountinfo");
    CleanOpts o = { .now = (i64)time(NULL), .dry = dry, .say = dry ? say : NULL };
    clean_opts_conf(&c, &o);
    o.all = all;
    CleanStats s;
    clean_run(&a, root, mountinfo, &o, &s);
    if (all) {   // (the line ofx shows after "<root>: ")
        printf("ofx-clean --all%s: %s: %td %s, %.1f MiB %s%s%s\n", dry ? " --dry-run" : "", root, s.n[CR_ALL] + s.n[CR_TEMP],
               dry ? "would go" : "deleted", mib(s.bytes[CR_ALL] + s.bytes[CR_TEMP]), dry ? "in all" : "freed",
               s.failed ? ", deletes failed" : "", s.cut ? ", too many files: only some were listed" : "");
        return s.failed ? 1 : 0;
    }
    i64 gone = 0;
    for (int r = CR_TEMP; r < CR_N; r++) gone += s.bytes[r];
    printf("ofx-clean%s: %s: %td unused, %td orphans, %td stale, %td over the cap, %td temp: %.1f MiB %s, "
           "%td kept (%.1f MiB)%s%s\n",
           dry ? " --dry-run" : "", root, s.n[CR_UNUSED], s.n[CR_ORPHAN], s.n[CR_STALE], s.n[CR_CAP], s.n[CR_TEMP],
           mib(gone), dry ? "would go" : "freed", s.n[CR_KEEP], mib(s.bytes[CR_KEEP]),
           s.failed ? ", deletes failed" : "", s.cut ? ", too many files: only some were listed" : "");
    return s.failed ? 1 : 0;
}
