// ofx-clean [--dry-run]: the thumbnail cache's cleanup (F6.5, fs/clean.c), run daily by
// ofx-clean.timer. One summary line for the journal; --dry-run lists each file it would delete and
// deletes nothing. No window: fs/ only. OFX_TEST_MOUNTINFO names another mount list (the tests').
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
    bool dry = argc == 2 && strcmp(argv[1], "--dry-run") == 0;
    if (argc > 2 || (argc == 2 && !dry)) return fprintf(stderr, "usage: ofx-clean [--dry-run]\n"), 2;
    Arena a;
    arena_init(&a, "clean", mem, sizeof mem);
    char root[PATH_MAX];
    if (!thumb_root(root, sizeof root)) return fprintf(stderr, "ofx-clean: no cache folder (HOME unset)\n"), 1;
    ops_guard(getenv("OFX_GUI_ROOT"));
    const char *mi = getenv("OFX_TEST_MOUNTINFO");
    Str mountinfo = read_file(&a, mi && mi[0] ? mi : "/proc/self/mountinfo");
    CleanOpts o = { .now = (i64)time(NULL), .cap = CLEAN_CAP, .dry = dry, .say = dry ? say : NULL };
    CleanStats s;
    clean_run(&a, root, mountinfo, &o, &s);
    i64 gone = 0;
    for (int r = CR_TEMP; r < CR_N; r++) gone += s.bytes[r];
    printf("ofx-clean%s: %s: %td unused, %td orphans, %td stale, %td over the cap, %td temp: %.1f MiB %s, "
           "%td kept (%.1f MiB)%s%s\n",
           dry ? " --dry-run" : "", root, s.n[CR_UNUSED], s.n[CR_ORPHAN], s.n[CR_STALE], s.n[CR_CAP], s.n[CR_TEMP],
           mib(gone), dry ? "would go" : "freed", s.n[CR_KEEP], mib(s.bytes[CR_KEEP]),
           s.failed ? ", deletes failed" : "", s.cut ? ", too many files: only some were listed" : "");
    return s.failed ? 1 : 0;
}
