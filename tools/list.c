// ofx-list <folder> [--hidden]: a folder on a far drive (a network share, a FUSE mount) read for
// ofx, which never reads one itself (F16 step 2; fs/farlist.c says why, and what it writes). Its
// entries, then the item counts ofx asks for on its input, until that ends. No window: fs/ only.
#include "../base/base.c"
#include "../fs/fs.c"

#include <stdio.h>
#include <string.h>

static u8 mem[MiB(1)];   // one entry's name and link target at a time

int main(int argc, char **argv)
{
    bool hidden = argc == 3 && strcmp(argv[2], "--hidden") == 0;
    if (argc < 2 || argc > 3 || (argc == 3 && !hidden) || argv[1][0] != '/') return fprintf(stderr, "usage: ofx-list /folder [--hidden]\n"), 2;
    Arena a;
    arena_init(&a, "ofx-list", mem, sizeof mem);
    return far_serve(stdin, stdout, &a, argv[1], hidden);
}
