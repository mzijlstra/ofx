// ofx-probe DIR [SIZE SCALE]: the listing as ofx sees it, one line per entry (kind, MIME type, icon
// file), from the real MIME database, icon theme and Omarchy theme. No window: fs/ only.
// ofx-probe --conf: the ofx.conf Preferences writes (F10), for the README's copy (ui/ for its keys).
#include "../base/base.c"
#include "../fs/fs.c"
#include "../ui/ui.c"   // (the key lines the file has, F15)

#include <stdio.h>
#include <stdlib.h>

static u8 mem[MiB(64)];

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--conf") == 0) {
        static char keys[8192], text[CONF_TEXT_MAX];
        ui_keys_conf_text(keys, sizeof keys);
        fwrite(text, 1, conf_default_text(text, sizeof text, keys), stdout);
        return 0;
    }
    if (argc != 2 && argc != 4) return fprintf(stderr, "usage: ofx-probe DIR [SIZE SCALE] | --conf\n"), 2;
    int size = argc == 4 ? atoi(argv[2]) : 24, scale = argc == 4 ? atoi(argv[3]) : 2;
    Arena a;
    arena_init(&a, "probe", mem, sizeof mem);
    i64 t0 = now_ms();
    Theme t = theme_load(&a);
    MimeDb db;
    mime_load(&a, &db);
    IconDb icons;
    icon_load(&a, &icons, str_from_c(t.icons));
    UserDirs u;
    user_dirs_load(&a, &u);
    i64 t1 = now_ms();
    Listing l;
    Err err;
    if (!fs_list(&a, argv[1], true, (Sort){ 0 }, &l, &err)) return fprintf(stderr, "%s\n", err.msg), 1;
    i64 t2 = now_ms();
    printf("theme %s, %d colours, accent #%02x%02x%02x; %td globs, %td icon themes (%td dirs in the first); load %lld ms, list %lld ms\n",
           t.icons, t.ncolors, t.accent.r, t.accent.g, t.accent.b, db.n, icons.n, icons.n ? icons.t[0].ndirs : 0,
           (long long)(t1 - t0), (long long)(t2 - t1));
    for (isize i = 0; i < l.n; i++) {
        const Entry *e = &l.e[i];
        Str type = mime_of(&a, &db, e), names[MIME_ICON_NAMES];
        isize n = 0;
        if (kind_is_dir(e->kind)) {
            Str special = user_dir_icon(&u, arena_sprintf(&a, "%s/%.*s", argv[1], STR_ARG(e->name)));
            if (special.len) names[n++] = special;
            names[n++] = S("folder");
        } else {
            n = mime_icon_names(&a, &db, type, names);
        }
        const char *f = icon_find(&a, &icons, names, n, size, scale);
        printf("%-28.*s %-34.*s %s\n", STR_ARG(e->name), STR_ARG(type), f ? f : "-");
    }
    return 0;
}
