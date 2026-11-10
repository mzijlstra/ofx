// F6.5's unit tests: the thumbnail cache's cleanup (fs/clean.c). Every preview is made here with
// png_encode, in a cache under the run's root; the mount list is a string. Included by test.c.

static u8 clean_test_mem[MiB(16)];

static void set_times(const char *path, i64 atime, i64 mtime)
{
    struct timespec ts[2] = { { (time_t)atime, 0 }, { (time_t)mtime, 0 } };
    CHECK(utimensat(AT_FDCWD, path, ts, AT_SYMLINK_NOFOLLOW) == 0);
}

static i64 atime_ns(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0 ? (i64)st.st_atim.tv_sec * 1000000000 + st.st_atim.tv_nsec : -1;
}

// A preview in cache/dir of uri (with mtime and size when >= 0), read last at atime; its path in
// out.
static void preview(Arena *a, const char *cache, const char *dir, const char *uri, i64 mtime, i64 size, i64 atime, char *out)
{
    u8 px[4] = { 1, 2, 3, 255 };
    Str png;
    CHECK(png_encode(a, (Img){ px, 1, 1 }, uri, mtime < 0 ? 0 : mtime, size < 0 ? 0 : size, &png));
    CHECK(thumb_file(cache, dir, uri ? uri : "file:///no-uri", out, PATH_MAX));   // NULL: a PNG without the text
    put_bytes(out, png.p, (size_t)png.len);
    set_times(out, atime, atime);
}

// A preview of the file at path, as it is now.
static void preview_of(Arena *a, const char *cache, const char *dir, const char *path, i64 atime, char *out)
{
    char uri[PATH_MAX * 3];
    struct stat st;
    CHECK(file_uri(path, uri, sizeof uri) && stat(path, &st) == 0);
    preview(a, cache, dir, uri, st.st_mtim.tv_sec, st.st_size, atime, out);
}

typedef struct { int n[CR_N]; char last[PATH_MAX]; } Said;

static void said(void *ctx, CleanRule r, const char *path, i64 size)
{
    (void)size;
    Said *s = ctx;
    s->n[r]++;
    snprintf(s->last, sizeof s->last, "%s", path);
}

static void test_clean_names(void)
{
    CHECK(clean_name("0123456789abcdef0123456789abcdef.png"));
    CHECK(!clean_name("0123456789ABCDEF0123456789abcdef.png") && !clean_name("0123456789abcdef0123456789abcde.png"));
    CHECK(!clean_name("0123456789abcdef0123456789abcdefa.png") && !clean_name("0123456789abcdef0123456789abcdef.pn"));
    CHECK(!clean_name("0123456789abcdef0123456789abcdeg.png") && !clean_name("0123456789abcdef0123456789abcdef.png~"));
    CHECK(!clean_name("/123456789abcdef0123456789abcdef.png") && !clean_name("0123456789abcdef0123456789abcde:.png"));
    CHECK(clean_temp_name(".ofx-123-0.png") && clean_temp_name(".ofx-1234567890-1234567890.png"));
    CHECK(!clean_temp_name(".ofx--0.png") && !clean_temp_name(".ofx-1-.png") && !clean_temp_name(".ofx-1-2.pngx"));
    CHECK(!clean_temp_name(".ofx-12345678901-0.png") && !clean_temp_name(".ofx-1-2-3.png") && !clean_temp_name("ofx-1-2.png"));
    CHECK(!clean_temp_name(".ofx-1.2.png") && !clean_temp_name(".ofx-a-2.png") && !clean_temp_name(".ofx-1-2.jpg"));
    CHECK(!clean_temp_name(".ofxx1-2.png") && !clean_temp_name(".ofx1-2.png"));

    char p[PATH_MAX];
    CHECK(file_uri_path(S("file:///a%20b/%C3%A9%2f%7E"), p, sizeof p));
    CHECK_STR(str_from_c(p), "/a b/\xc3\xa9/~");
    CHECK(file_uri_path(S("file:///x/A-z_0.9"), p, sizeof p));
    CHECK_STR(str_from_c(p), "/x/A-z_0.9");
    CHECK(!file_uri_path(S("trash:///a"), p, sizeof p) && !file_uri_path(S("file://host/a"), p, sizeof p));
    CHECK(!file_uri_path(S("file:///a%00b"), p, sizeof p) && !file_uri_path(S("file:///a%2"), p, sizeof p));
    CHECK(!file_uri_path(S("file:///a%g0"), p, sizeof p) && !file_uri_path(S("file:///a%0g"), p, sizeof p));
    CHECK(!file_uri_path(S("file:///a%1g"), p, sizeof p) && !file_uri_path(S("file:///a%1G"), p, sizeof p));
    CHECK(!file_uri_path(S("file:///a%"), p, sizeof p) && !file_uri_path(S("http:///a"), p, sizeof p));
    // Too small: refused, never cut.
    CHECK(!file_uri_path(S("file:///abc"), p, 4) && file_uri_path(S("file:///abc"), p, 5));
    // Its round trip with file_uri, for every byte but NUL.
    char path[300], uri[PATH_MAX * 3];
    path[0] = '/';
    for (int i = 1; i < 256; i++) path[i] = (char)i;
    path[256] = 0;
    CHECK(file_uri(path, uri, sizeof uri) && file_uri_path(str_from_c(uri), p, sizeof p) && strcmp(p, path) == 0);

    Str mi = S("1 0 0:1 / / rw - ext4 a rw\n"
               "2 1 0:2 / /mnt/sd rw - ext4 b rw\n"
               "3 1 0:3 / /run/media/m/USB\\040KEY rw - vfat c rw\n"
               "4 1 0:4 / /media/cd rw - iso9660 d rw\n");
    Arena a;
    arena_init(&a, "clean test", clean_test_mem, sizeof clean_test_mem);
    Mounts m = mounts_parse(&a, mi), none = mounts_parse(&a, STR_EMPTY);
    Mounts *ms = &m;
    CHECK(m.n == 4 && none.n == 0);
    CHECK(clean_mounted(ms, "/mnt/sd") && clean_mounted(ms, "/") && clean_mounted(ms, "/run/media/m/USB KEY"));
    CHECK(!clean_mounted(ms, "/mnt") && !clean_mounted(ms, "/mnt/s") && !clean_mounted(&none, "/"));
    // On a drive that isn't mounted: kept for it.
    CHECK(clean_absent_drive(ms, "/run/media/m/OTHER/a.png") && clean_absent_drive(ms, "/media/dvd/a.png"));
    CHECK(clean_absent_drive(ms, "/mnt/usb/x/a.png") && clean_absent_drive(ms, "/run/media/n/USB KEY/a"));
    // Mounted, or not under a drive's folder: judged.
    CHECK(!clean_absent_drive(ms, "/mnt/sd/a.png") && !clean_absent_drive(ms, "/run/media/m/USB KEY/a.png"));
    CHECK(!clean_absent_drive(ms, "/media/cd/x/a.png") && !clean_absent_drive(ms, "/home/m/a.png"));
    CHECK(!clean_absent_drive(ms, "/mnt/a.png") && !clean_absent_drive(ms, "/media/a.png") && !clean_absent_drive(ms, "/run/media/m/a.png"));
    CHECK(!clean_absent_drive(ms, "/mnt") && !clean_absent_drive(ms, "/run/media") && !clean_absent_drive(ms, "/mntx/y/a.png"));

    // clean_local, the parsed list's thumb_local: the same verdict on test_thumb_local's list.
    Str tl = S("22 1 259:2 / / rw,relatime shared:1 - ext4 /dev/nvme0n1p2 rw\n"
               "40 22 179:1 / /mnt/sd rw shared:20 - exfat /dev/mmcblk0p1 rw\n"
               "41 22 0:50 / /mnt/nas rw - nfs4 server:/export rw\n"
               "42 22 0:51 / /mnt/box rw - fuse.sshfs me@box: rw\n"
               "43 22 8:17 / /mnt/win rw - fuseblk /dev/sdb1 rw\n"
               "44 22 0:52 / /mnt/my\\040share rw - cifs //srv/s rw\n"
               "45 41 259:3 / /mnt/nas/local rw - ext4 /dev/nvme0n1p3 rw\n"
               "46 22 0:53 / /run/user/1000/gvfs rw - fuse.gvfsd-fuse gvfsd-fuse rw\n"
               "47 22 0:54 / /mnt/fuser rw - fuserfs x rw\n"
               "48 22 0:55 / /mnt/over rw - ext4 e rw\n"
               "49 22 0:56 / /mnt/over rw - nfs f rw\n"
               "50 22 0:57 / /mnt/back rw - nfs g rw\n"
               "51 22 0:58 / /mnt/back rw - ext4 h rw\n"
               "52 22 0:59 / /mnt/nodash rw ext4 i rw\n"
               "53 22 0:60 / /mnt/deep/in rw - ext4 j rw\n"   // listed before the mount above it
               "54 22 0:61 / /mnt/deep rw - nfs k rw\n");
    Mounts t = mounts_parse(&a, tl);
    const char *paths[] = { "/", "/home/m/a.png", "/mnt/sd/x/y.jpg", "/mnt/win/a.png", "/mnt/nas/a.png", "/mnt/nas",
                            "/mnt/box/p/a.png", "/mnt/my share/a.png", "/run/user/1000/gvfs/sftp:host=x/a.png",
                            "/mnt/nas/local/a.png", "/mnt/nasty/a.png", "/mnt/sdx", "/mnt/fuser/a.png", "/mnt/over/a",
                            "/mnt/back/a", "/mnt/nodash/a", "/mnt/nas/localx/a", "/mnt/deep/in/a", "/mnt/deep/a" };
    for (int i = 0; i < COUNT(paths); i++) {
        CHECK(mounts_local(&t, paths[i]) == mounts_local(mt(tl), paths[i]));
        if (mounts_local(&t, paths[i]) != mounts_local(mt(tl), paths[i])) fprintf(stderr, "  clean_local %s\n", paths[i]);
    }
    CHECK(!mounts_local(&t, "/mnt/nas/a.png") && !mounts_local(&t, "/mnt/over/a") && mounts_local(&t, "/mnt/back/a"));
    CHECK(mounts_local(&t, "/mnt/nas/local/a.png") && mounts_local(&t, "/mnt/deep/in/a") && !mounts_local(&t, "/mnt/deep/a"));
    CHECK(mounts_local(&none, "/x"));
    Mounts rootnfs = mounts_parse(&a, S("1 0 0:1 / / rw - nfs a rw\n"));
    CHECK(!mounts_local(&rootnfs, "/m/a") && !mounts_local(&rootnfs, "/"));
}

static void test_clean_real(void)
{
    char base[1200], r[PATH_MAX], want[PATH_MAX], nas[1300], mi[3 * PATH_MAX];
    snprintf(base, sizeof base, "%s", at("f65r"));
    snprintf(nas, sizeof nas, "%s/nas", base);
    snprintf(mi, sizeof mi, "1 0 0:1 / / rw - ext4 a rw\n2 1 0:2 / %s rw - nfs4 b rw\n", nas);
    Arena a;
    arena_init(&a, "clean test", clean_test_mem, sizeof clean_test_mem);
    Mounts mounts = mounts_parse(&a, str_from_c(mi)), *m = &mounts;
    mkdirs(at("f65r/real/deep")), mkdirs(nas);
    CHECK(symlink("real", at("f65r/rel")) == 0);                         // relative
    CHECK(symlink(at("f65r/real"), at("f65r/abs")) == 0);                // absolute
    CHECK(symlink("rel", at("f65r/chain")) == 0);                        // a link to a link
    CHECK(symlink("/mnt/ofx-test-nodrive/D", at("f65r/away")) == 0);     // leads nowhere
    CHECK(symlink("real/deep", at("f65r/down")) == 0);
    CHECK(symlink("loop2", at("f65r/loop1")) == 0 && symlink("loop1", at("f65r/loop2")) == 0);
    CHECK(symlink("nas", at("f65r/tonas")) == 0);
    struct { const char *in, *out; } cases[] = {
        { "real/a.png", "real/a.png" }, { "rel/a.png", "real/a.png" }, { "abs/a.png", "real/a.png" },
        { "chain/x/a.png", "real/x/a.png" }, { "rel/./deep/../a.png", "real/a.png" },
        { "down/../a.png", "real/a.png" },   // .. of where the link leads, as the kernel has it
        { "missing/rel/a.png", "missing/rel/a.png" },
    };
    for (int i = 0; i < COUNT(cases); i++) {
        snprintf(want, sizeof want, "%s/%s", base, cases[i].out);
        CHECK(clean_real(m, at("f65r/%s", cases[i].in), r, sizeof r) && strcmp(r, want) == 0);
        if (strcmp(r, want) != 0) fprintf(stderr, "  %s -> %s\n", cases[i].in, r);
    }
    CHECK(clean_real(m, at("f65r/away/x/a.png"), r, sizeof r));
    CHECK_STR(str_from_c(r), "/mnt/ofx-test-nodrive/D/x/a.png");
    CHECK(clean_real(m, "/", r, sizeof r));
    CHECK_STR(str_from_c(r), "/");
    CHECK(clean_real(m, "/..//a/", r, sizeof r));
    CHECK_STR(str_from_c(r), "/a");
    // 40 links followed, not 41: c0 -> c1 -> ... -> c40 -> real.
    char cn[64], tn[64];
    for (int i = 0; i <= 40; i++) {
        snprintf(cn, sizeof cn, "f65r/c%d", i);
        snprintf(tn, sizeof tn, i == 40 ? "real" : "c%d", i + 1);
        CHECK(symlink(tn, at("%s", cn)) == 0);
    }
    CHECK(clean_real(m, at("f65r/c1/a.png"), r, sizeof r) && !clean_real(m, at("f65r/c0/a.png"), r, sizeof r));
    // Refused: a loop, a network mount (by the path, or through a link), too small an out.
    CHECK(!clean_real(m, at("f65r/loop1/a.png"), r, sizeof r));
    CHECK(!clean_real(m, at("f65r/nas/a.png"), r, sizeof r) && !clean_real(m, at("f65r/tonas/a.png"), r, sizeof r));
    CHECK(!clean_real(m, at("f65r/real/a.png"), r, 1) && !clean_real(m, at("f65r/real/a.png"), r, strlen(base) + 5));
    CHECK(clean_real(m, at("f65r/real"), r, strlen(base) + 6));
}

static void test_clean_run(void)
{
    Arena a;
    arena_init(&a, "clean test", clean_test_mem, sizeof clean_test_mem);
    ops_guard(root);
    i64 now = (i64)time(NULL), day = 86400, recent = now - 10 * day;   // a read 10 days ago: relatime would update it
    char cache[1200], pics[1200], nas[1200];   // at()'s size, so a name added still fits PATH_MAX
    snprintf(cache, sizeof cache, "%s", at("f65/cache/thumbnails"));
    snprintf(pics, sizeof pics, "%s", at("f65/pics"));
    snprintf(nas, sizeof nas, "%s", at("f65/nas"));
    char mi[3 * PATH_MAX];
    snprintf(mi, sizeof mi, "1 0 0:1 / / rw - ext4 a rw\n2 1 0:2 / %s rw - nfs4 b rw\n3 1 0:3 / /mnt/ofx-test-drive rw - ext4 c rw\n", nas);

    // The files.
    char keep[PATH_MAX], gone[PATH_MAX], gonedir[PATH_MAX], changed[PATH_MAX], grown[PATH_MAX], old[PATH_MAX];
    snprintf(keep, sizeof keep, "%s/keep.png", pics);
    snprintf(gone, sizeof gone, "%s/gone.png", pics);
    snprintf(gonedir, sizeof gonedir, "%s/sub/gone.png", pics);
    snprintf(changed, sizeof changed, "%s/changed.png", pics);
    snprintf(grown, sizeof grown, "%s/grown.png", pics);
    snprintf(old, sizeof old, "%s/old.png", pics);
    put(keep, "k"), put(gone, "g"), put(gonedir, "g"), put(changed, "c"), put(grown, "g"), put(old, "o");

    enum { P_KEEP, P_GONE, P_GONEDIR, P_CHANGED, P_GROWN, P_OLD, P_DAYS89, P_NAS, P_NODRIVE, P_DRIVE, P_TRASH, P_JUNK,
           P_FAIL_GONE, P_FAIL_OLD, P_TMP_OLD, P_TMP_NEW, P_LINK, P_DIR, P_OTHER, P_NOURI, P_NOTDIR, P_LOCKED, P_BIG,
           P_VIA_AWAY, P_VIA_MOUNTED, P_VIA_NAS, P_LINKED_FILE, P_N };
    char p[P_N][PATH_MAX];
    preview_of(&a, cache, "normal", keep, recent, p[P_KEEP]);
    preview_of(&a, cache, "large", gone, recent, p[P_GONE]);
    preview_of(&a, cache, "x-large", gonedir, recent, p[P_GONEDIR]);
    preview_of(&a, cache, "xx-large", changed, recent, p[P_CHANGED]);
    preview_of(&a, cache, "normal", grown, recent, p[P_GROWN]);
    preview_of(&a, cache, "large", old, now - 91 * day, p[P_OLD]);
    set_times(p[P_OLD], now - 91 * day, now - day);   // the atime decides, not the mtime
    char uri[PATH_MAX * 3];
    preview_of(&a, cache, "x-large", keep, now - 89 * day, p[P_DAYS89]);
    set_times(p[P_DAYS89], now - 89 * day, now - 200 * day);
    snprintf(uri, sizeof uri, "file://%s/missing.png", nas);
    preview(&a, cache, "normal", uri, 1, 1, recent, p[P_NAS]);
    preview(&a, cache, "normal", "file:///mnt/ofx-test-nodrive/a.png", 1, 1, recent, p[P_NODRIVE]);
    preview(&a, cache, "normal", "file:///mnt/ofx-test-drive/a.png", 1, 1, recent, p[P_DRIVE]);
    preview(&a, cache, "normal", "trash:///a.png", 1, 1, recent, p[P_TRASH]);
    snprintf(p[P_JUNK], PATH_MAX, "%s/normal/%s", cache, "00000000000000000000000000000000.png");
    put(p[P_JUNK], "not a png"), set_times(p[P_JUNK], recent, recent);
    preview_of(&a, cache, "fail/gnome-thumbnail-factory", gone, recent, p[P_FAIL_GONE]);
    preview_of(&a, cache, "fail/ofx", keep, now - 200 * day, p[P_FAIL_OLD]);
    snprintf(p[P_TMP_OLD], PATH_MAX, "%s/large/.ofx-12-3.png", cache);
    put(p[P_TMP_OLD], "t"), set_times(p[P_TMP_OLD], now, now - 2 * day);
    snprintf(p[P_TMP_NEW], PATH_MAX, "%s/large/.ofx-12-4.png", cache);
    put(p[P_TMP_NEW], "t"), set_times(p[P_TMP_NEW], now - 300 * day, now - 3600);
    // A link and a folder with a preview's name, and a file without one: never touched, even old.
    char target[PATH_MAX];
    snprintf(target, sizeof target, "%s", at("f65/target.png"));
    put(target, "t");
    snprintf(p[P_LINK], PATH_MAX, "%s/normal/11111111111111111111111111111111.png", cache);
    CHECK(symlink(target, p[P_LINK]) == 0);
    set_times(p[P_LINK], now - 300 * day, now - 300 * day);
    snprintf(p[P_DIR], PATH_MAX, "%s/normal/22222222222222222222222222222222.png", cache);
    mkdirs(p[P_DIR]);
    set_times(p[P_DIR], now - 300 * day, now - 300 * day);
    snprintf(p[P_OTHER], PATH_MAX, "%s/normal/other.png", cache);
    put(p[P_OTHER], "o"), set_times(p[P_OTHER], now - 300 * day, now - 300 * day);
    preview(&a, cache, "x-large", NULL, -1, -1, recent, p[P_NOURI]);
    // A file below a file (ENOTDIR): gone. One in a folder it can't search (EACCES): kept.
    snprintf(uri, sizeof uri, "file://%s/keep.png/a.png", pics);
    preview(&a, cache, "x-large", uri, 1, 1, recent, p[P_NOTDIR]);
    char locked[PATH_MAX];
    snprintf(locked, sizeof locked, "%s/locked/a.png", pics);
    put(locked, "l");
    preview_of(&a, cache, "x-large", locked, recent, p[P_LOCKED]);
    CHECK(chmod(path_dir(str_from_c(locked)).len ? arena_cstr(&a, path_dir(str_from_c(locked))) : "", 0) == 0);
    // Through links (Michael's ~/Documents is a link onto /mnt/sd): judged where they lead. Onto a
    // drive that is out: kept. Onto one mounted: gone. Onto a network mount: kept.
    CHECK(symlink("/mnt/ofx-test-nodrive/D", at("f65/pics/Away")) == 0);
    snprintf(uri, sizeof uri, "file://%s/Away/a.png", pics);
    preview(&a, cache, "large", uri, 1, 1, recent, p[P_VIA_AWAY]);
    CHECK(symlink("/mnt/ofx-test-drive/D", at("f65/pics/Mounted")) == 0);
    snprintf(uri, sizeof uri, "file://%s/Mounted/a.png", pics);
    preview(&a, cache, "large", uri, 1, 1, recent, p[P_VIA_MOUNTED]);
    mkdirs(nas);
    CHECK(symlink(nas, at("f65/pics/Nas")) == 0);
    snprintf(uri, sizeof uri, "file://%s/Nas/missing.png", pics);
    preview(&a, cache, "large", uri, 1, 1, recent, p[P_VIA_NAS]);
    // A link to a file: its preview has the target's mtime and size, as ofx writes it. Fresh.
    {
        char lf[PATH_MAX], lu[PATH_MAX * 3];
        struct stat ks;
        snprintf(lf, sizeof lf, "%s/link.png", pics);
        CHECK(symlink("keep.png", lf) == 0 && stat(keep, &ks) == 0 && file_uri(lf, lu, sizeof lu));
        preview(&a, cache, "large", lu, ks.st_mtim.tv_sec, ks.st_size, recent, p[P_LINKED_FILE]);
    }
    // Bigger than a cached preview can be: not read, so kept although its file is gone.
    {
        u8 px[4] = { 0 };
        Str png;
        CHECK(png_encode(&a, (Img){ px, 1, 1 }, "file:///ofx-test-none/big.png", 1, 1, &png));
        CHECK(thumb_file(cache, "x-large", "file:///ofx-test-none/big.png", p[P_BIG], PATH_MAX));
        FILE *bf = fopen(p[P_BIG], "wb");
        CHECK(bf != NULL);
        if (bf) {
            fwrite(png.p, 1, (size_t)png.len, bf);
            CHECK(fseek(bf, THUMB_CACHED_MAX, SEEK_CUR) == 0 && fputc(0, bf) == 0);
            fclose(bf);
        }
        set_times(p[P_BIG], recent, recent);
    }
    // Not looked at: a size folder that is a link, and a file in the cache's root.
    char away[PATH_MAX], awayf[PATH_MAX], rootf[PATH_MAX];
    snprintf(away, sizeof away, "%s", at("f65/away"));
    preview_of(&a, away, "normal", gone, now - 300 * day, awayf);
    CHECK(symlink(at("f65/away/normal"), at("f65/cache/thumbnails/fail/linked")) == 0);
    snprintf(rootf, sizeof rootf, "%s/33333333333333333333333333333333.png", cache);
    put(rootf, "r"), set_times(rootf, now - 300 * day, now - 300 * day);

    // The files change after their previews: one deleted, one in a deleted folder, one touched, one grown.
    unlink(gone), unlink(gonedir), rmdir(path_dir(str_from_c(gonedir)).len ? arena_cstr(&a, path_dir(str_from_c(gonedir))) : "");
    set_mtime(changed, mtime_of(changed) + 5);
    put(grown, "grown");
    set_mtime(grown, mtime_of(keep));
    (void)size_of;

    static const CleanRule want[P_N] = {
        [P_GONE] = CR_ORPHAN, [P_GONEDIR] = CR_ORPHAN, [P_CHANGED] = CR_STALE, [P_GROWN] = CR_STALE, [P_OLD] = CR_UNUSED,
        [P_DRIVE] = CR_ORPHAN, [P_FAIL_GONE] = CR_ORPHAN, [P_FAIL_OLD] = CR_UNUSED, [P_TMP_OLD] = CR_TEMP, [P_NOTDIR] = CR_ORPHAN, [P_VIA_MOUNTED] = CR_ORPHAN,
    };
    i64 before[P_N];
    for (int i = 0; i < P_N; i++) before[i] = atime_ns(p[i]);

    // A dry run: each said, nothing deleted, no atime touched.
    Said s = { 0 };
    CleanStats st;
    // ofx.conf's clean-unused-days = 0 and clean-orphans = off (F10): only the temp file would go.
    CleanOpts o = { .now = now, .unused_s = 0, .orphans = false, .cap = CLEAN_CAP, .dry = true, .say = said, .ctx = &s };
    clean_run(&a, cache, str_from_c(mi), &o, &st);
    CHECK(st.n[CR_ORPHAN] + st.n[CR_STALE] + st.n[CR_UNUSED] == 0 && st.n[CR_TEMP] == 1 && st.n[CR_KEEP] == 23);
    memset(&s, 0, sizeof s);
    o.unused_s = CLEAN_UNUSED_S, o.orphans = true;
    clean_run(&a, cache, str_from_c(mi), &o, &st);
    CHECK(st.n[CR_ORPHAN] == 6 && st.n[CR_STALE] == 2 && st.n[CR_UNUSED] == 2 && st.n[CR_TEMP] == 1 && st.n[CR_CAP] == 0);
    CHECK(s.n[CR_ORPHAN] == 6 && s.n[CR_STALE] == 2 && s.n[CR_UNUSED] == 2 && s.n[CR_TEMP] == 1 && s.n[CR_KEEP] == 0);
    CHECK(st.n[CR_KEEP] == 13 && st.failed == 0 && !st.cut);   // keep, 89 days, nas, nodrive, trash, junk, tmp_new, nouri, locked, big, via away, via nas, linked file
    for (int i = 0; i < P_N; i++) CHECK(there(p[i]) && atime_ns(p[i]) == before[i]);

    // The run: the same files go, the others stay with their atimes.
    memset(&s, 0, sizeof s);
    o.dry = false;
    clean_run(&a, cache, str_from_c(mi), &o, &st);
    CHECK(st.n[CR_ORPHAN] == 6 && st.n[CR_STALE] == 2 && st.n[CR_UNUSED] == 2 && st.n[CR_TEMP] == 1 && st.failed == 0);
    CHECK(s.n[CR_ORPHAN] == 6);
    for (int i = 0; i < P_N; i++) {
        bool stays = want[i] == CR_KEEP;
        CHECK(there(p[i]) == stays);
        if (stays && there(p[i]) != stays) fprintf(stderr, "  preview %d\n", i);
        if (stays) CHECK(atime_ns(p[i]) == before[i]);
    }
    CHECK(there(target) && there(awayf) && there(rootf) && there(keep));
    // A preview reached through a link is never read (O_NOFOLLOW): a link to an orphan's preview.
    {
        char orph[PATH_MAX], lnk[PATH_MAX];
        preview(&a, at("f65/linked"), "normal", "file:///ofx-test-none/l.png", 1, 1, recent, orph);
        snprintf(lnk, sizeof lnk, "%s", at("f65/linked/normal/link.png"));
        CHECK(symlink(orph, lnk) == 0);
        int dfd = open(at("f65/linked/normal"), O_RDONLY | O_DIRECTORY);
        Mounts ms = mounts_parse(&a, str_from_c(mi));
        CHECK(clean_judge(&a, &ms, dfd, path_base(str_from_c(orph)).p) == CR_ORPHAN);
        CHECK(clean_judge(&a, &ms, dfd, "link.png") == CR_KEEP);
        close(dfd);
    }
    // Again: nothing more.
    clean_run(&a, cache, str_from_c(mi), &o, &st);
    CHECK(st.n[CR_ORPHAN] + st.n[CR_STALE] + st.n[CR_UNUSED] + st.n[CR_TEMP] + st.n[CR_CAP] == 0 && st.n[CR_KEEP] == 13);

    // The cap: the least recently read go first, until the rest fit.
    char capc[PATH_MAX], c[4][PATH_MAX];
    snprintf(capc, sizeof capc, "%s", at("f65/capcache/thumbnails"));
    for (int i = 0; i < 4; i++) {
        snprintf(uri, sizeof uri, "trash:///cap%d", i);
        preview(&a, capc, "large", uri, -1, -1, now - (10 - i) * day, c[i]);   // c[0] read longest ago
    }
    i64 one = size_of(c[0]);
    CHECK(one > 0 && size_of(c[3]) == one);
    o.cap = 2 * one;
    clean_run(&a, capc, str_from_c(mi), &o, &st);
    CHECK(st.n[CR_CAP] == 2 && st.bytes[CR_CAP] == 2 * one && st.n[CR_KEEP] == 2 && st.bytes[CR_KEEP] == 2 * one);
    CHECK(!there(c[0]) && !there(c[1]) && there(c[2]) && there(c[3]));
    o.cap = 0;   // clean-cap = 0: no cap
    clean_run(&a, capc, str_from_c(mi), &o, &st);
    CHECK(st.n[CR_CAP] == 0 && there(c[2]) && there(c[3]));
    o.cap = 2 * one;
    clean_run(&a, capc, str_from_c(mi), &o, &st);   // exactly at the cap: nothing goes
    CHECK(st.n[CR_CAP] == 0 && there(c[2]) && there(c[3]));
    o.cap = 2 * one - 1;   // one byte over: one more goes
    clean_run(&a, capc, str_from_c(mi), &o, &st);
    CHECK(st.n[CR_CAP] == 1 && !there(c[2]) && there(c[3]));
    // A delete that fails frees nothing: the next ones still go until the rest fit.
    char capd[1200], d[4][PATH_MAX];
    snprintf(capd, sizeof capd, "%s", at("f65/capfail/thumbnails"));
    for (int i = 0; i < 4; i++) {
        snprintf(uri, sizeof uri, "trash:///capfail%d", i);
        preview(&a, capd, i ? "large" : "normal", uri, -1, -1, now - (10 - i) * day, d[i]);
    }
    char ro[PATH_MAX];
    snprintf(ro, sizeof ro, "%s/normal", capd);
    CHECK(chmod(ro, 0555) == 0);
    o.cap = 2 * size_of(d[1]);
    clean_run(&a, capd, str_from_c(mi), &o, &st);
    CHECK(st.failed == 1 && st.n[CR_CAP] == 2 && there(d[0]) && !there(d[1]) && !there(d[2]) && there(d[3]));
    chmod(ro, 0755);
    o.cap = CLEAN_CAP;

    // The guard: a cache outside the root deletes nothing.
    char far[PATH_MAX], farf[PATH_MAX];
    snprintf(far, sizeof far, "%s/thumbnails", decoy);
    preview(&a, far, "normal", "file:///nowhere/a.png", 1, 1, recent, farf);
    clean_run(&a, far, str_from_c(mi), &o, &st);
    CHECK(st.failed == 1 && st.n[CR_ORPHAN] == 0 && there(farf));
    // A cache that is a link: not followed.
    CHECK(symlink(capc, at("f65/linkcache")) == 0);
    clean_run(&a, at("f65/linkcache"), str_from_c(mi), &o, &st);
    CHECK(st.n[CR_KEEP] == 0 && there(c[3]));
    ops_guard(NULL);
}
