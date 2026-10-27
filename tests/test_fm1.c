// FileManager1 (2026-10-26): the URIs a call names to the paths ofx starts on (fs/fm1.c).

static void test_fm1(void)
{
    char d[512], u[8][700];
    snprintf(d, sizeof d, "%s/fm1/sub dir", root);
    mkdirs(d);
    char f[600];
    snprintf(f, sizeof f, "%s/a b.txt", d), put(f, "");
    snprintf(f, sizeof f, "%s/c.txt", d), put(f, "");
    snprintf(f, sizeof f, "%s/fm1/top.txt", root), put(f, "");
    snprintf(u[0], sizeof u[0], "file://%s/fm1/sub%%20dir/a%%20b.txt", root);
    snprintf(u[1], sizeof u[1], "file://%s/fm1/sub%%20dir/c.txt", root);
    snprintf(u[2], sizeof u[2], "file://%s/fm1/top.txt", root);
    snprintf(u[3], sizeof u[3], "file://%s/fm1/gone.txt", root);
    snprintf(u[4], sizeof u[4], "sftp://host%s/fm1/top.txt", root);
    snprintf(u[5], sizeof u[5], "file://%s/fm1/sub%%20dir", root);
    snprintf(u[6], sizeof u[6], "file://%s/fm1", root);
    snprintf(u[7], sizeof u[7], "file://%s/fm1/sub%%2", root);   // a bad escape
    static Fm1Plan p;
    char want[700];
    // Items: decoded; one window a folder, the first item asked for; the rest skipped.
    const char *items[] = { u[0], u[1], u[2], u[3], u[4], u[7] };
    fm1_plan(FM1_ITEMS, items, COUNT(items), &p);
    CHECK(p.n == 2 && p.skipped == 3);
    snprintf(want, sizeof want, "%s/fm1/sub dir/a b.txt", root);
    CHECK(strcmp(p.path[0], want) == 0);
    snprintf(want, sizeof want, "%s/fm1/top.txt", root);
    CHECK(p.n == 2 && strcmp(p.path[1], want) == 0);
    // Folders: each once, as it is; a folder and the one above are two windows.
    const char *dirs[] = { u[5], u[6], u[5] };
    fm1_plan(FM1_FOLDERS, dirs, COUNT(dirs), &p);
    snprintf(want, sizeof want, "%s/fm1", root);
    CHECK(p.n == 2 && p.skipped == 0 && strcmp(p.path[1], want) == 0);
    char slashed[710];
    snprintf(slashed, sizeof slashed, "%s/", u[5]);
    const char *items2[] = { slashed, u[2] };   // sub dir/ and top.txt: both in fm1
    fm1_plan(FM1_ITEMS, items2, COUNT(items2), &p);
    snprintf(want, sizeof want, "%s/fm1/sub dir", root);
    CHECK(p.n == 1 && strcmp(p.path[0], want) == 0);
    snprintf(want, sizeof want, "%s/fm1", root);
    const char *twice[] = { u[5], u[5] };
    fm1_plan(FM1_FOLDERS, twice, COUNT(twice), &p);
    CHECK(p.n == 1);
    // As an item a folder is shown in the folder above it: with a file in it, two windows; with
    // a file beside it, one.
    const char *mixed[] = { u[6], u[2] };
    fm1_plan(FM1_PROPS, mixed, COUNT(mixed), &p);
    CHECK(p.n == 2 && strcmp(p.path[0], want) == 0);
    const char *in_it[] = { u[5], u[1] };
    fm1_plan(FM1_ITEMS, in_it, COUNT(in_it), &p);
    CHECK(p.n == 2);
    const char *beside[] = { u[5], u[2] };
    fm1_plan(FM1_ITEMS, beside, COUNT(beside), &p);
    CHECK(p.n == 1);
    // At most FM1_MAX windows a call.
    static char many[FM1_MAX + 3][700];
    const char *mp[FM1_MAX + 3];
    for (int i = 0; i < FM1_MAX + 3; i++) {
        snprintf(f, sizeof f, "%s/fm1/m%d", root, i);
        mkdirs(f);
        snprintf(many[i], sizeof many[i], "file://%s", f), mp[i] = many[i];
    }
    fm1_plan(FM1_FOLDERS, mp, FM1_MAX + 3, &p);
    CHECK(p.n == FM1_MAX && p.skipped == 3);
    fm1_plan(FM1_ITEMS, mp, 0, &p);
    CHECK(p.n == 0 && p.skipped == 0);
    // The folder of one at the top is /.
    CHECK(str_eq(fm1_parent("/x"), S("/")) && str_eq(fm1_parent("/a/b"), S("/a")));
}
