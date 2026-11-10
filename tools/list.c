// ofx-list <folder> [--hidden]: a folder on a far drive (a network share, a FUSE mount) read for
// ofx, which never reads one itself (F16 step 2; fs/farlist.c says why, and what it writes). Its
// entries, then the item counts ofx asks for on its input, until that ends. No window: fs/ only.
// ofx-list --do <action> <args>: one action there (step 3b, fs/fardo.c).
// ofx-list --shares: the shares gvfs has mounted, as gvfs's FUSE folders show them (step 5,
// fs/connect.c's gvfs_shares): ofx never reads those folders itself.
#include "../base/base.c"
#include "../fs/fs.c"

#include <stdio.h>
#include <string.h>

static u8 mem[MiB(1)];   // one entry's name and link target at a time

int main(int argc, char **argv)
{
    ops_guard(getenv("OFX_GUI_ROOT"));   // (a test's: --do writes only inside its folder, as ofx; the cleanup's step A)
    if (getenv("OFX_TEST_CROSS")) job_test_cross = true;   // (a test's moves as across drives, as ofx's: an undo's job)
    if (argc >= 2 && strcmp(argv[1], "--do") == 0) return far_do_serve(stdout, argc - 2, argv + 2);
    if (argc == 2 && strcmp(argv[1], "--shares") == 0) {
        Arena a;
        arena_init(&a, "ofx-list", mem, sizeof mem);
        const char *mi = getenv("OFX_TEST_MOUNTINFO");   // (the tests' mount list, as ofx's)
        return gvfs_shares(stdout, &a, read_file(&a, mi && mi[0] ? mi : "/proc/self/mountinfo")) ? 0 : 1;
    }
    bool hidden = argc == 3 && strcmp(argv[2], "--hidden") == 0;
    if (argc < 2 || argc > 3 || (argc == 3 && !hidden) || argv[1][0] != '/') return fprintf(stderr, "usage: ofx-list /folder [--hidden]\n"), 2;
    Arena a;
    arena_init(&a, "ofx-list", mem, sizeof mem);
    return far_serve(stdin, stdout, &a, argv[1], hidden);
}
