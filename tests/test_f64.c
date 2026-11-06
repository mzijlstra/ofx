// F6.4's unit tests: the previews' cache names (as GLib and Nemo make them), the PNG's text, the
// decoders, and thumb_get's one-directory rule with its failures remembered by mtime. Every image
// is made here, in the run's root; the cache is a folder of the root. Included by test.c.

#include <webp/encode.h>

static u8 thumb_test_mem[MiB(48)];

static void test_md5(void)
{
    char h[33];
    md5_hex("", 0, h);
    CHECK_STR(str_from_c(h), "d41d8cd98f00b204e9800998ecf8427e");
    md5_hex("abc", 3, h);
    CHECK_STR(str_from_c(h), "900150983cd24fb0d6963f7d28e17f72");
    // 80 bytes: across a block, and the padding's own block.
    const char *d = "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
    md5_hex(d, strlen(d), h);
    CHECK_STR(str_from_c(h), "57edf4a22be3c955ac49da2e2107b67a");
    // 55 and 56 bytes: the length still fits the block, then it doesn't.
    md5_hex(d, 55, h);
    CHECK_STR(str_from_c(h), "c9ccf168914a1bcfc3229f1948e67da0");
    md5_hex(d, 56, h);
    CHECK_STR(str_from_c(h), "49f193adce178490e34d1b3a4ec0064c");
    md5_hex(d, 64, h);
    CHECK_STR(str_from_c(h), "eb6c4179c0a7c82cc2828c1e6338e165");
}

static void test_thumb_names(void)
{
    char uri[PATH_MAX * 3], file[PATH_MAX];
    // As `gio info` printed it for this name (2026-10-24).
    CHECK(file_uri("/a !\"#$%&'()*+,;<=>?@[\\]^`{|}~\xc3\xa9", uri, sizeof uri));
    CHECK_STR(str_from_c(uri), "file:///a%20!%22%23$%25&'()*+,%3B%3C=%3E%3F@%5B%5C%5D%5E%60%7B%7C%7D~%C3%A9");
    CHECK(file_uri("/x/A-z_0.9", uri, sizeof uri));
    CHECK_STR(str_from_c(uri), "file:///x/A-z_0.9");
    CHECK(file_uri("/\x01\x7f\xff", uri, sizeof uri));
    CHECK_STR(str_from_c(uri), "file:///%01%7F%FF");
    // Too small a buffer: refused, never cut (a cut URI names another file).
    CHECK(!file_uri("/abc", uri, 11) && file_uri("/abc", uri, 12));
    CHECK(!file_uri("/ab ", uri, 13) && file_uri("/ab ", uri, 14));
    CHECK(!file_uri("", uri, 7) && file_uri("", uri, 8));
    // A name Nemo wrote in Michael's large/ (seen 2026-10-24): the URI's MD5 is the file's name.
    CHECK(file_uri("/home/mzijlstra/Downloads/cpt_report_CS574.pdf", uri, sizeof uri));
    CHECK(thumb_file("/c/thumbnails", "large", uri, file, sizeof file));
    CHECK_STR(str_from_c(file), "/c/thumbnails/large/003931868899a9ba56c6e378b18d3bd4.png");
    CHECK(!thumb_file("/c/thumbnails", "large", uri, file, 40));
    // The directory for a drawn size: the smallest at least as big.
    CHECK(thumb_level(1) == 0 && thumb_level(128) == 0 && thumb_level(129) == 1 && thumb_level(256) == 1);
    CHECK(thumb_level(257) == 2 && thumb_level(512) == 2 && thumb_level(513) == 3 && thumb_level(4096) == 3);
    CHECK(strcmp(thumb_dirs[0], "normal") == 0 && strcmp(thumb_dirs[3], "xx-large") == 0);
    // The cache's root: XDG_CACHE_HOME when absolute, else ~/.cache.
    char r[PATH_MAX];
    env_set("XDG_CACHE_HOME", "/x/cache");
    env_set("HOME", "/h");
    CHECK(thumb_root(r, sizeof r));
    CHECK_STR(str_from_c(r), "/x/cache/thumbnails");
    CHECK(!thumb_root(r, 12));
    setenv("XDG_CACHE_HOME", "rel", 1);
    CHECK(thumb_root(r, sizeof r));
    CHECK_STR(str_from_c(r), "/h/.cache/thumbnails");
    unsetenv("XDG_CACHE_HOME");
    CHECK(thumb_root(r, sizeof r));
    CHECK_STR(str_from_c(r), "/h/.cache/thumbnails");
    setenv("HOME", "h", 1);
    CHECK(!thumb_root(r, sizeof r));
    env_restore();
    CHECK(thumb_kind(S("image/png")) == TK_PNG && thumb_kind(S("image/jpeg")) == TK_JPEG && thumb_kind(S("image/gif")) == TK_GIF);
    CHECK(thumb_kind(S("image/webp")) == TK_WEBP && thumb_kind(S("application/pdf")) == TK_NONE && thumb_kind(S("image/png2")) == TK_NONE);
}

static void test_thumb_local(void)
{
    Str mi = S("22 1 259:2 / / rw,relatime shared:1 - ext4 /dev/nvme0n1p2 rw\n"
               "40 22 179:1 / /mnt/sd rw shared:20 - exfat /dev/mmcblk0p1 rw\n"
               "41 22 0:50 / /mnt/nas rw - nfs4 server:/export rw\n"
               "42 22 0:51 / /mnt/box rw - fuse.sshfs me@box: rw\n"
               "43 22 8:17 / /mnt/win rw - fuseblk /dev/sdb1 rw\n"
               "44 22 0:52 / /mnt/my\\040share rw - cifs //srv/s rw\n"
               "45 41 259:3 / /mnt/nas/local rw - ext4 /dev/nvme0n1p3 rw\n"
               "46 22 0:53 / /run/user/1000/gvfs rw - fuse.gvfsd-fuse gvfsd-fuse rw\n"
               "47 22 0:54 / /mnt/fuser rw - fuserfs x rw\n");
    CHECK(thumb_local(mi, "/home/m/a.png") && thumb_local(mi, "/mnt/sd/x/y.jpg") && thumb_local(mi, "/mnt/win/a.png"));
    CHECK(!thumb_local(mi, "/mnt/nas/a.png") && !thumb_local(mi, "/mnt/nas") && !thumb_local(mi, "/mnt/box/p/a.png"));
    CHECK(!thumb_local(mi, "/mnt/my share/a.png") && !thumb_local(mi, "/run/user/1000/gvfs/sftp:host=x/a.png"));
    CHECK(thumb_local(mi, "/mnt/nas/local/a.png") && thumb_local(mi, "/mnt/nasty/a.png") && thumb_local(mi, "/mnt/sdx"));
    CHECK(thumb_local(mi, "/mnt/fuser/a.png") && thumb_local(STR_EMPTY, "/x"));
    // Mounted over: the later line on the same point.
    CHECK(!thumb_local(S("1 0 0:1 / / rw - ext4 a rw\n2 1 0:2 / /m rw - ext4 b rw\n3 1 0:3 / /m rw - nfs c rw\n"), "/m/a"));
    CHECK(thumb_local(S("1 0 0:1 / / rw - nfs a rw\n2 1 0:2 / /m rw - ext4 b rw\n"), "/m/a"));
    CHECK(!thumb_local(S("1 0 0:1 / / rw - nfs a rw\n"), "/m/a"));
}

// A w by h picture: the left half red, the right half blue, opaque.
static Img halves(Arena *a, int w, int h)
{
    Img im;
    img_new(a, w, h, &im);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            u8 *p = im.px + ((isize)y * w + x) * 4;
            p[0] = x < w / 2 ? 255 : 0, p[1] = 0, p[2] = x < w / 2 ? 0 : 255, p[3] = 255;
        }
    return im;
}

static bool reddish(const Img *im, int x, int y)
{
    const u8 *p = im->px + ((isize)y * im->w + x) * 4;
    return p[0] > 200 && p[2] < 60;
}

static bool blueish(const Img *im, int x, int y)
{
    const u8 *p = im->px + ((isize)y * im->w + x) * 4;
    return p[2] > 200 && p[0] < 60;
}

static void put_bytes(const char *path, const void *p, size_t n)
{
    mkdirs(path_dir(str_from_c(path)).len ? arena_cstr(&ta, path_dir(str_from_c(path))) : "/");
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (!f) return;
    fwrite(p, 1, n, f);
    fclose(f);
}

static i64 mtime_of(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (i64)st.st_mtim.tv_sec : -1;
}

static i64 size_of(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (i64)st.st_size : -1;
}

static bool jpeg_progressive;   // jpeg_of makes a progressive JPEG

// A JPEG of im (quality 95, no subsampling), with an EXIF orientation when o > 0, MM or II order.
static Str jpeg_of(Arena *a, Img im, int o, bool le)
{
    tjhandle tj = tj3Init(TJINIT_COMPRESS);
    tj3Set(tj, TJPARAM_PROGRESSIVE, jpeg_progressive);
    tj3Set(tj, TJPARAM_SUBSAMP, TJSAMP_444);
    tj3Set(tj, TJPARAM_QUALITY, 95);
    u8 *buf = NULL;
    size_t n = 0;
    CHECK(tj3Compress8(tj, im.px, im.w, 0, im.h, TJPF_RGBA, &buf, &n) == 0);
    tj3Destroy(tj);
    u8 exif[36] = { 0xFF, 0xE1, 0, 34, 'E', 'x', 'i', 'f', 0, 0 };
    u8 *t = exif + 10;
    if (le) memcpy(t, "II\x2a\0\x08\0\0\0\x01\0\x12\x01\x03\0\x01\0\0\0", 18), t[18] = (u8)o;
    else memcpy(t, "MM\0\x2a\0\0\0\x08\0\x01\x01\x12\0\x03\0\0\0\x01", 18), t[19] = (u8)o;
    isize extra = o > 0 ? 36 : 0;
    u8 *out = arena_push(a, (isize)n + extra, 1);
    memcpy(out, buf, 2);
    if (extra) memcpy(out + 2, exif, 36);
    memcpy(out + 2 + extra, buf + 2, n - 2);
    tj3Free(buf);
    return (Str){ (const char *)out, (isize)n + extra };
}

static void test_png_meta(void)
{
    Arena a;
    arena_init(&a, "thumb test", thumb_test_mem, sizeof thumb_test_mem);
    Img im = halves(&a, 6, 4);
    Str png;
    CHECK(png_encode(&a, im, "file:///p/q.png", 1700000000, 1234, &png));
    ThumbMeta m;
    CHECK(png_meta(png, &m) && m.has_uri && m.has_mtime && m.has_size);
    CHECK_STR(str_from_c(m.uri), "file:///p/q.png");
    CHECK(m.mtime == 1700000000 && m.size == 1234);
    CHECK(thumb_fresh(&m, "file:///p/q.png", 1700000000, 1234));
    CHECK(!thumb_fresh(&m, "file:///p/q.png", 1700000001, 1234) && !thumb_fresh(&m, "file:///p/q.png", 1700000000, 1235));
    CHECK(!thumb_fresh(&m, "file:///p/r.png", 1700000000, 1234));
    m.has_size = false;   // Nemo's have no Thumb::Size: the mtime alone then
    CHECK(thumb_fresh(&m, "file:///p/q.png", 1700000000, 99));
    m.has_mtime = false;
    CHECK(!thumb_fresh(&m, "file:///p/q.png", 1700000000, 1234));
    // It decodes, and is the picture.
    Img back;
    CHECK(png_decode(&a, png, 64, &back) && back.w == 6 && back.h == 4 && reddish(&back, 0, 0) && blueish(&back, 5, 3));
    // Cut short (no IEND), a wrong signature, no text: not a preview.
    CHECK(!png_meta((Str){ png.p, png.len - 1 }, &m));
    CHECK(!png_meta((Str){ png.p, 7 }, &m));
    char bad[64];
    memcpy(bad, png.p, sizeof bad);
    bad[1] = 'Q';
    CHECK(!png_meta((Str){ bad, sizeof bad }, &m));
    Str plain;
    CHECK(png_encode(&a, im, NULL, 0, 0, &plain) && png_meta(plain, &m) && !m.has_uri && !m.has_mtime && !m.has_size);
    // A chunk longer than the file: refused, not read past.
    char big[64];
    memcpy(big, png.p, 33);
    big[33] = 0x7f;
    CHECK(!png_meta((Str){ big, 40 }, &m));
    // A tEXt chunk's length past the file's end: refused, never read past (ASan has the copy's end).
    isize uc = -1;
    for (isize i = 0; uc < 0 && i + 16 <= png.len; i++)
        if (memcmp(png.p + i, "tEXtThumb::URI", 14) == 0) uc = i - 4;
    CHECK(uc > 0);
    u8 *cut = malloc((size_t)png.len);   // test-only: exactly the file, so a read past it shows
    memcpy(cut, png.p, (size_t)png.len);
    put32(cut + uc, (u32)(png.len - uc));   // its value would be copied from past the end
    CHECK(!png_meta((Str){ (const char *)cut, png.len }, &m));
    free(cut);
    // Numbers: digits only.
    i64 v;
    CHECK(parse_i64(S("0"), &v) && v == 0 && parse_i64(S("123456789012345678"), &v) && v == 123456789012345678LL);
    CHECK(!parse_i64(S(""), &v) && !parse_i64(S("-1"), &v) && !parse_i64(S("12a"), &v) && !parse_i64(S("1234567890123456789"), &v));
    CHECK(!parse_i64(S("0000000000000000001"), &v));   // (nineteen digits, zeros too: review, 2026-11-06)
}

static void test_thumb_decode(void)
{
    Arena a;
    arena_init(&a, "thumb test", thumb_test_mem, sizeof thumb_test_mem);
    Img im = halves(&a, 1000, 500), out;
    // A JPEG, at 1/4 then fitted: 128 by 64, the halves where they were.
    Str j = jpeg_of(&a, im, 0, false);
    CHECK(jpeg_orientation(j) == 1);
    CHECK(thumb_make(&a, TK_JPEG, j, 128, &out) && out.w == 128 && out.h == 64 && reddish(&out, 10, 32) && blueish(&out, 117, 32));
    // Turned by EXIF 6 (90 degrees clockwise): standing, the left half on top. Both byte orders.
    Str j6 = jpeg_of(&a, im, 6, false), j6le = jpeg_of(&a, im, 6, true);
    CHECK(jpeg_orientation(j6) == 6 && jpeg_orientation(j6le) == 6);
    CHECK(thumb_make(&a, TK_JPEG, j6, 128, &out) && out.w == 64 && out.h == 128 && reddish(&out, 32, 10) && blueish(&out, 32, 117));
    Str j8 = jpeg_of(&a, im, 8, true);
    CHECK(thumb_make(&a, TK_JPEG, j8, 128, &out) && out.w == 64 && out.h == 128 && blueish(&out, 32, 10) && reddish(&out, 32, 117));
    Str j3 = jpeg_of(&a, im, 3, false);
    CHECK(thumb_make(&a, TK_JPEG, j3, 128, &out) && out.w == 128 && out.h == 64 && blueish(&out, 10, 32) && reddish(&out, 117, 32));
    // Bad orientations, and bad segments, read as 1.
    CHECK(jpeg_orientation(jpeg_of(&a, im, 9, false)) == 1 && jpeg_orientation(jpeg_of(&a, im, 0, true)) == 1);
    CHECK(jpeg_orientation(S("\xFF\xD8\xFF\xE1\x00\x01")) == 1 && jpeg_orientation(S("\xFF\xD8\xFF\xE1\x00\x20" "Exif")) == 1);
    CHECK(jpeg_orientation(S("abcd")) == 1 && jpeg_orientation(S("\xFF\xD8\x00\x00\x00\x00")) == 1);
    // The JPEG smaller than asked: whole, never made bigger.
    Img small = halves(&a, 40, 20);
    CHECK(thumb_make(&a, TK_JPEG, jpeg_of(&a, small, 0, false), 128, &out) && out.w == 40 && out.h == 20);
    // Not a JPEG.
    CHECK(!thumb_make(&a, TK_JPEG, S("not a jpeg at all"), 128, &out));
    // libjpeg's own memory capped (F6.4 review): a progressive JPEG holds its
    // coefficients at full size, 1000 by 500 about 3 MiB, over a 1 MiB cap; a baseline one doesn't.
    jpeg_progressive = true;
    Str prog = jpeg_of(&a, im, 0, false);
    jpeg_progressive = false;
    CHECK(thumb_make(&a, TK_JPEG, prog, 128, &out) && out.w == 128);
    thumb_jpeg_max_mib = 1;
    CHECK(!thumb_make(&a, TK_JPEG, prog, 128, &out));
    CHECK(thumb_make(&a, TK_JPEG, j, 128, &out) && out.w == 128);
    thumb_jpeg_max_mib = 256;
    // Only warnings (a review): 3 junk bytes before a marker, and a cut file, still decode.
    isize seg = 4 + ((u8)j.p[4] << 8 | (u8)j.p[5]);   // past SOI and APP0
    u8 *junk = arena_push(&a, j.len + 3, 1);
    memcpy(junk, j.p, (size_t)seg), memcpy(junk + seg, "\0\0\0", 3), memcpy(junk + seg + 3, j.p + seg, (size_t)(j.len - seg));
    CHECK((u8)j.p[seg] == 0xFF && thumb_make(&a, TK_JPEG, (Str){ (const char *)junk, j.len + 3 }, 128, &out) && out.w == 128);
    CHECK(thumb_make(&a, TK_JPEG, (Str){ j.p, j.len * 6 / 10 }, 128, &out) && out.w == 128 && reddish(&out, 10, 5));
    // Fitted, each side rounded: 200 by 300 to 256 is 171 by 256 (170.67).
    CHECK(img_fit(&a, halves(&a, 200, 300), 256, &out) && out.w == 171 && out.h == 256);
    // PNG, fitted.
    Str png;
    CHECK(png_encode(&a, im, NULL, 0, 0, &png));
    CHECK(thumb_make(&a, TK_PNG, png, 256, &out) && out.w == 256 && out.h == 128 && reddish(&out, 5, 64) && blueish(&out, 250, 64));
    // A PNG of more pixels than THUMB_PIXELS_MAX (only its header is read): refused.
    u8 huge[33];
    memcpy(huge, png.p, 33);
    put32(huge + 16, 10000), put32(huge + 20, 5000);
    put32(huge + 29, (u32)crc32(0, huge + 12, 17));
    CHECK(!thumb_make(&a, TK_PNG, (Str){ (const char *)huge, 33 }, 256, &out));
    // A GIF: 2 by 1, red then blue, its first (only) frame.
    static const u8 gif[] = { 'G', 'I', 'F', '8', '9', 'a', 2, 0, 1, 0, 0x80, 0, 0, 255, 0, 0, 0, 0, 255,
                              ',', 0, 0, 0, 0, 2, 0, 1, 0, 0, 2, 2, 0x44, 0x0A, 0, ';' };
    CHECK(thumb_make(&a, TK_GIF, (Str){ (const char *)gif, sizeof gif }, 128, &out) && out.w == 2 && out.h == 1);
    CHECK(reddish(&out, 0, 0) && blueish(&out, 1, 0));
    // Its graphic control's transparent colour (1, blue) clear; a frame at (1,0) on a 3 by 1
    // screen, the rest clear too.
    static const u8 gif_t[] = { 'G', 'I', 'F', '8', '9', 'a', 2, 0, 1, 0, 0x80, 0, 0, 255, 0, 0, 0, 0, 255,
                                0x21, 0xF9, 4, 1, 0, 0, 1, 0, ',', 0, 0, 0, 0, 2, 0, 1, 0, 0, 2, 2, 0x44, 0x0A, 0, ';' };
    CHECK(thumb_make(&a, TK_GIF, (Str){ (const char *)gif_t, sizeof gif_t }, 128, &out) && out.w == 2 && out.h == 1);
    CHECK(reddish(&out, 0, 0) && out.px[3] == 255 && out.px[7] == 0);
    static const u8 gif_o[] = { 'G', 'I', 'F', '8', '9', 'a', 3, 0, 1, 0, 0x80, 0, 0, 255, 0, 0, 0, 0, 255,
                                ',', 1, 0, 0, 0, 2, 0, 1, 0, 0, 2, 2, 0x44, 0x0A, 0, ';' };
    CHECK(thumb_make(&a, TK_GIF, (Str){ (const char *)gif_o, sizeof gif_o }, 128, &out) && out.w == 3 && out.h == 1);
    CHECK(out.px[3] == 0 && reddish(&out, 1, 0) && blueish(&out, 2, 0));
    // Interlaced (ImageMagick's, 2 by 4, its rows red, lime, white, blue as `magick ... txt:` reads
    // them; stored red, white, lime, blue), its rows put back in order.
    static const u8 gif_i[] = { 0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x02, 0x00, 0x04, 0x00, 0xf1, 0x03, 0x00, 0xff, 0x00, 0x00, 0x00,
                                0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x21, 0xf9, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2c,
                                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04, 0x00, 0x40, 0x02, 0x05, 0x04, 0x36, 0x11, 0x22, 0x05, 0x00, 0x3b };
    CHECK(thumb_make(&a, TK_GIF, (Str){ (const char *)gif_i, sizeof gif_i }, 128, &out) && out.w == 2 && out.h == 4);
    CHECK(reddish(&out, 1, 0) && out.px[8] == 0 && out.px[9] == 255 && out.px[10] == 0 && out.px[16] == 255 && out.px[17] == 255 && blueish(&out, 1, 3));
    // A frame past its screen's edge, a screen of more than THUMB_PIXELS_MAX, a cut file: refused.
    u8 bad_gif[sizeof gif_o];
    memcpy(bad_gif, gif_o, sizeof gif_o);
    bad_gif[20] = 2;   // Left 2, Width 2, on a screen 3 wide
    CHECK(!thumb_make(&a, TK_GIF, (Str){ (const char *)bad_gif, sizeof bad_gif }, 128, &out));
    memcpy(bad_gif, gif_o, sizeof gif_o);
    bad_gif[6] = 0x10, bad_gif[7] = 0x27, bad_gif[8] = 0x88, bad_gif[9] = 0x13;   // 10000 by 5000
    CHECK(!thumb_make(&a, TK_GIF, (Str){ (const char *)bad_gif, sizeof bad_gif }, 128, &out));
    CHECK(!thumb_make(&a, TK_GIF, (Str){ (const char *)gif_o, 30 }, 128, &out));
    CHECK(!thumb_make(&a, TK_GIF, S("GIF89a"), 128, &out) && !thumb_make(&a, TK_PNG, S("\x89PNG\r\n\x1a\n"), 128, &out));
    // WebP, scaled as it decodes.
    u8 *wp = NULL;
    size_t wn = WebPEncodeLosslessRGBA(im.px, im.w, im.h, im.w * 4, &wp);
    CHECK(wn > 0);
    CHECK(thumb_make(&a, TK_WEBP, (Str){ (const char *)wp, (isize)wn }, 128, &out) && out.w == 128 && out.h == 64);
    CHECK(reddish(&out, 10, 32) && blueish(&out, 117, 32));
    CHECK(thumb_make(&a, TK_WEBP, (Str){ (const char *)wp, (isize)wn }, 2000, &out) && out.w == 1000 && out.h == 500);
    WebPFree(wp);
    CHECK(!thumb_make(&a, TK_WEBP, S("RIFF....WEBPVP8 nonsense"), 128, &out));
    CHECK(!thumb_make(&a, TK_NONE, png, 128, &out));
    // Every orientation of a 3 by 2 (0 1 2 over 3 4 5), every pixel where EXIF puts it.
    Img t;
    img_new(&a, 3, 2, &t);
    for (int i = 0; i < 6; i++) memset(t.px + i * 4, i, 4);
    static const u8 want[9][6] = { { 0 }, { 0, 1, 2, 3, 4, 5 }, { 2, 1, 0, 5, 4, 3 }, { 5, 4, 3, 2, 1, 0 }, { 3, 4, 5, 0, 1, 2 },
                                   { 0, 3, 1, 4, 2, 5 }, { 3, 0, 4, 1, 5, 2 }, { 5, 2, 4, 1, 3, 0 }, { 2, 5, 1, 4, 0, 3 } };
    for (int o = 1; o <= 8; o++) {
        Img r;
        bool same = img_orient(&a, t, o, &r) && r.w == (o >= 5 ? 2 : 3) && r.h == (o >= 5 ? 3 : 2);
        for (int i = 0; same && i < 6; i++) same = r.px[i * 4] == want[o][i] && r.px[i * 4 + 3] == want[o][i];
        CHECK(same);
    }
    // The reads' limit: a file over it isn't read (CONF_PREVIEW_MAX for a source).
    const char *big = at("f64/big.bin");
    put(big, "");
    CHECK(truncate(big, CONF_PREVIEW_MAX + 1) == 0);
    struct stat st;
    CHECK(file_read_at(&a, AT_FDCWD, big, CONF_PREVIEW_MAX, &st, 0).len == 0 && st.st_size == CONF_PREVIEW_MAX + 1);
    CHECK(truncate(big, 100) == 0);
    CHECK(file_read_at(&a, AT_FDCWD, big, 100, &st, 0).len == 100 && file_read_at(&a, AT_FDCWD, big, 99, &st, 0).len == 0);
}

// The ask for path at px, as gui/thumbs.c makes it.
static ThumbAsk ask(const char *path, ThumbKind k, int px, const char *cache)
{
    return (ThumbAsk){ .path = path, .mtime = mtime_of(path), .size = size_of(path), .kind = k, .make = true, .px = px, .root = cache, .src_max = CONF_PREVIEW_MAX };
}

static bool cached_fresh(Arena *a, const char *cache, const char *dir, const char *path, int *w, int *h)
{
    char uri[PATH_MAX * 3], file[PATH_MAX];
    file_uri(path, uri, sizeof uri);
    thumb_file(cache, dir, uri, file, sizeof file);
    struct stat st;
    ThumbMeta m;
    Str c = file_read_at(a, AT_FDCWD, file, THUMB_CACHED_MAX, &st, 0);
    bool ok = c.len >= 24 && png_meta(c, &m) && thumb_fresh(&m, uri, mtime_of(path), size_of(path));
    if (ok) *w = (int)be32((const u8 *)c.p + 16), *h = (int)be32((const u8 *)c.p + 20);   // IHDR
    return ok && (st.st_mode & 0777) == 0600;
}

static void test_thumb_get(void)
{
    Arena a;
    arena_init(&a, "thumb test", thumb_test_mem, sizeof thumb_test_mem);
    ops_guard(root);
    char cache[PATH_MAX];
    snprintf(cache, sizeof cache, "%s", at("f64/cache/thumbnails"));
    Img im = halves(&a, 300, 200), out;
    Str png;
    CHECK(png_encode(&a, im, NULL, 0, 0, &png));
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s", at("f64/pics/a photo #1.png"));   // at() reuses its buffers
    put_bytes(p, png.p, (size_t)png.len);
    set_mtime(p, 1700000000);
    int w, h;
    // Made at 48 px from normal (128), written there with the spec's text, mode 0600.
    ThumbAsk q = ask(p, TK_PNG, 48, cache);
    CHECK(thumb_get(&a, &q, &out) == TG_MADE && out.w == 48 && out.h == 32);
    CHECK(cached_fresh(&a, cache, "normal", p, &w, &h) && w == 128 && h == 85);
    struct stat st;
    CHECK(stat(at("f64/cache/thumbnails/normal"), &st) == 0 && (st.st_mode & 0777) == 0700);
    // Again: read, not made. At 96 and 128 the same file.
    CHECK(thumb_get(&a, &q, &out) == TG_CACHED && out.w == 48 && out.h == 32 && reddish(&out, 2, 16) && blueish(&out, 45, 16));
    q.px = 128;
    CHECK(thumb_get(&a, &q, &out) == TG_CACHED && out.w == 128);
    // 256: large, its own file; normal not touched.
    q.px = 256;
    CHECK(thumb_get(&a, &q, &out) == TG_MADE && out.w == 256 && out.h == 171);
    CHECK(cached_fresh(&a, cache, "large", p, &w, &h) && w == 256);
    CHECK(thumb_get(&a, &q, &out) == TG_CACHED);
    // The file changed: stale, made again, written over.
    set_mtime(p, 1700000100);
    q = ask(p, TK_PNG, 48, cache);
    CHECK(thumb_get(&a, &q, &out) == TG_MADE && cached_fresh(&a, cache, "normal", p, &w, &h));
    // The listing's mtime older than the file's: nothing made, the next listing asks again.
    q.mtime = 1700000000;
    CHECK(thumb_get(&a, &q, &out) == TG_CHANGED);
    // No temporary file left in the folder.
    char cmd[3 * PATH_MAX];
    snprintf(cmd, sizeof cmd, "test -z \"$(ls -A '%s/normal' | grep -v '\\.png$')\" && test $(ls -A '%s/normal' | wc -l) -eq 1", cache, cache);
    CHECK(system(cmd) == 0);

    // A file that won't decode: failed, written to fail/ofx/, then known until it changes.
    char bad[PATH_MAX];
    snprintf(bad, sizeof bad, "%s", at("f64/pics/broken.png"));
    put(bad, "this is not a png");
    set_mtime(bad, 1700000000);
    q = ask(bad, TK_PNG, 48, cache);
    CHECK(thumb_get(&a, &q, &out) == TG_FAILED);
    CHECK(cached_fresh(&a, cache, "fail/ofx", bad, &w, &h) && w == 1 && h == 1);
    CHECK(thumb_get(&a, &q, &out) == TG_FAIL_KNOWN);
    q.px = 256;
    CHECK(thumb_get(&a, &q, &out) == TG_FAIL_KNOWN);   // one fail entry for every size
    set_mtime(bad, 1700000200);
    q = ask(bad, TK_PNG, 48, cache);
    CHECK(thumb_get(&a, &q, &out) == TG_FAILED && thumb_get(&a, &q, &out) == TG_FAIL_KNOWN);
    // An empty file fails too (the empty photo.png of F6.2).
    char empty[PATH_MAX];
    snprintf(empty, sizeof empty, "%s", at("f64/pics/empty.jpg"));
    put(empty, "");
    q = ask(empty, TK_JPEG, 48, cache);
    CHECK(thumb_get(&a, &q, &out) == TG_FAILED && thumb_get(&a, &q, &out) == TG_FAIL_KNOWN);

    // Another program's preview of a type ofx doesn't make (a PDF): shown while fresh, never made.
    char pdf[PATH_MAX];
    snprintf(pdf, sizeof pdf, "%s", at("f64/pics/notes.pdf"));
    put(pdf, "%PDF-1.4");
    set_mtime(pdf, 1700000000);
    char uri[PATH_MAX * 3], file[PATH_MAX];
    file_uri(pdf, uri, sizeof uri);
    thumb_file(cache, "large", uri, file, sizeof file);
    Str nemo;
    CHECK(png_encode(&a, halves(&a, 181, 256), uri, 1700000000, 0, &nemo));
    put_bytes(file, nemo.p, (size_t)nemo.len);
    q = ask(pdf, TK_NONE, 256, cache);
    CHECK(thumb_get(&a, &q, &out) == TG_NONE);   // its Thumb::Size (0) isn't the file's
    CHECK(png_encode(&a, halves(&a, 181, 256), uri, 1700000000, size_of(pdf), &nemo));
    put_bytes(file, nemo.p, (size_t)nemo.len);
    CHECK(thumb_get(&a, &q, &out) == TG_CACHED && out.w == 181 && out.h == 256);
    q.px = 100;   // normal: Nemo's is in large only, and ofx doesn't look there for 100 px
    CHECK(thumb_get(&a, &q, &out) == TG_NONE);
    set_mtime(pdf, 1700000300);   // stale: the icon, and nothing written
    q = ask(pdf, TK_NONE, 256, cache);
    CHECK(thumb_get(&a, &q, &out) == TG_NONE && size_of(file) == nemo.len);
    char failed[PATH_MAX];
    thumb_file(cache, "fail/ofx", uri, failed, sizeof failed);
    CHECK(size_of(failed) == -1);

    // Not to be made (a network drive, a file over the limit): what's cached only.
    char far[PATH_MAX];
    snprintf(far, sizeof far, "%s", at("f64/pics/far.png"));
    put_bytes(far, png.p, (size_t)png.len);
    q = ask(far, TK_PNG, 48, cache);
    q.make = false;
    CHECK(thumb_get(&a, &q, &out) == TG_NONE && !cached_fresh(&a, cache, "normal", far, &w, &h));
    q.make = true;
    CHECK(thumb_get(&a, &q, &out) == TG_MADE);
    q.make = false;
    CHECK(thumb_get(&a, &q, &out) == TG_CACHED);
    // Gone, or unreadable: the icon, no fail entry.
    char gone[PATH_MAX];
    snprintf(gone, sizeof gone, "%s", at("f64/pics/gone.png"));
    q = (ThumbAsk){ .path = gone, .mtime = 5, .size = 5, .kind = TK_PNG, .make = true, .px = 48, .root = cache, .src_max = CONF_PREVIEW_MAX };
    CHECK(thumb_get(&a, &q, &out) == TG_NONE && !cached_fresh(&a, cache, "fail/ofx", gone, &w, &h));
    // The cache's own files get none.
    file_uri(p, uri, sizeof uri);
    thumb_file(cache, "normal", uri, file, sizeof file);
    q = ask(file, TK_PNG, 48, cache);
    CHECK(thumb_get(&a, &q, &out) == TG_NONE);

    // A cache outside the test guard: the preview is made and shown, the write refused.
    char away[PATH_MAX];
    snprintf(away, sizeof away, "%s/thumbnails", decoy);
    int refused = ops_refusals;
    q = ask(p, TK_PNG, 48, away);
    CHECK(thumb_get(&a, &q, &out) == TG_MADE && ops_refusals == refused + 1);
    snprintf(cmd, sizeof cmd, "test ! -e '%s'", away);
    CHECK(system(cmd) == 0);
    // A read-only cache: made, not written, and no temporary file left.
    const char *ro = at("f64/ro/thumbnails/normal");
    mkdirs(ro);
    chmod(ro, 0500);
    char roc[PATH_MAX];
    snprintf(roc, sizeof roc, "%s", at("f64/ro/thumbnails"));
    q = ask(p, TK_PNG, 48, roc);
    CHECK(thumb_get(&a, &q, &out) == TG_MADE && !cached_fresh(&a, roc, "normal", p, &w, &h));
    chmod(ro, 0700);
    snprintf(cmd, sizeof cmd, "test -z \"$(ls -A '%s')\"", ro);
    CHECK(system(cmd) == 0);
    ops_guard(NULL);
}
