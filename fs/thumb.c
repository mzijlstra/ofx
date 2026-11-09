// Previews (F6.4, docs/plan.md "F6.4 design"): the freedesktop thumbnail cache, read and written as
// its spec has it, so Nemo uses what ofx makes and ofx what Nemo made. A file's preview is
// `<root>/<size>/<md5 of its URI>.png`, fresh while its Thumb::MTime (and Thumb::Size, when there)
// match the file's. ofx makes them of PNG, JPEG, GIF and WebP; of another type with a thumbnailer
// installed it runs that (thumbers.c, F12; ofx.conf's preview-thumbnailers); of any other it shows
// one only when the cache has it fresh.
//
// The rule from Nemo's loop (docs/plan.md, 2026-10-20): one directory for one drawn size, checked
// and written there only; a failure is written to fail/ofx/ and not tried again until the file
// changes. gui/thumbs.c runs thumb_get on its workers, and remembers each answer by the file's
// mtime, so nothing here runs twice for one change.
//
// The decoders read files from anywhere (a download), so they are the system's, fuzzed on
// OSS-Fuzz and patched with its updates (Michael, 2026-10-24: stb_image makes no claim to handle
// hostile files): libjpeg-turbo (decodes straight at 1/2 to 1/8 size), libwebp (scales as it
// decodes), libpng (the previews in the cache too) and giflib. They allocate on their own; the
// images come from the caller's arena. stb_image_resize2 only resizes our own buffers.

#include "../base/base.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gif_lib.h>
#include <png.h>
#include <turbojpeg.h>
#include <webp/decode.h>
#include <zlib.h>

#ifdef __TINYC__   // the unit mutants' first build (tools/mutation/run.py): tcc has no SSE headers
#define STBIR_NO_SIMD
#endif
#define STB_IMAGE_RESIZE_STATIC
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "external/stb_image_resize2.h"
#undef STB_IMAGE_RESIZE_IMPLEMENTATION

// The largest file ofx makes a preview of is ofx.conf's preview-max-size (F10, its default
// CONF_PREVIEW_MAX), each ask carrying its own (ThumbAsk.src_max). THUMB_PIXELS_MAX: a PNG or GIF is decoded whole, four bytes a pixel, so a
// small file of a huge image is refused before it takes more than 160 MiB of the worker's arena. THUMB_CACHED_MAX: a cached
// preview bigger than this isn't one (an xx-large is 1024 px, about 3 MiB).
enum { THUMB_PIXELS_MAX = 40000000, THUMB_CACHED_MAX = MiB(8), THUMB_LEVELS = 4 };

typedef struct { u8 *px; int w, h; } Img;   // RGBA, rows packed

typedef enum { TK_NONE, TK_PNG, TK_JPEG, TK_GIF, TK_WEBP, TK_EXEC } ThumbKind;   // TK_EXEC: by a thumbnailer (thumbers.c)

// The types ofx makes previews of; TK_NONE for any other.
static ThumbKind thumb_kind(Str mime)
{
    if (str_eq(mime, S("image/png"))) return TK_PNG;
    if (str_eq(mime, S("image/jpeg"))) return TK_JPEG;
    if (str_eq(mime, S("image/gif"))) return TK_GIF;
    if (str_eq(mime, S("image/webp"))) return TK_WEBP;
    return TK_NONE;
}

// ---- MD5 (RFC 1321), for the cache's file names -------------------------------------------------

typedef struct { u32 s[4]; u64 n; u8 buf[64]; } Md5;

static u32 md5_rotl(u32 x, int c) { return (x << c) | (x >> (32 - c)); }

static void md5_block(Md5 *m, const u8 *p)
{
    static const u32 K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
    };
    static const int R[16] = { 7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21 };
    u32 w[16];
    for (int i = 0; i < 16; i++) w[i] = (u32)p[4 * i] | (u32)p[4 * i + 1] << 8 | (u32)p[4 * i + 2] << 16 | (u32)p[4 * i + 3] << 24;
    u32 a = m->s[0], b = m->s[1], c = m->s[2], d = m->s[3];
    for (int i = 0; i < 64; i++) {
        u32 f;
        int g;
        if (i < 16) f = (b & c) | (~b & d), g = i;
        else if (i < 32) f = (d & b) | (~d & c), g = (5 * i + 1) % 16;
        else if (i < 48) f = b ^ c ^ d, g = (3 * i + 5) % 16;
        else f = c ^ (b | ~d), g = (7 * i) % 16;
        u32 t = d;
        d = c, c = b;
        b = b + md5_rotl(a + f + K[i] + w[g], R[(i / 16) * 4 + i % 4]);
        a = t;
    }
    m->s[0] += a, m->s[1] += b, m->s[2] += c, m->s[3] += d;
}

static void md5_add(Md5 *m, const void *data, size_t n)
{
    const u8 *p = data;
    for (size_t i = 0; i < n; i++) {
        m->buf[m->n++ % 64] = p[i];
        if (m->n % 64 == 0) md5_block(m, m->buf);
    }
}

// The digest of data, as 32 lowercase hex digits.
static void md5_hex(const void *data, size_t n, char out[33])
{
    Md5 m = { .s = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 } };
    md5_add(&m, data, n);
    u64 bits = m.n * 8;
    md5_add(&m, "\x80", 1);
    while (m.n % 64 != 56) md5_add(&m, "", 1);
    u8 len[8];
    for (int i = 0; i < 8; i++) len[i] = (u8)(bits >> (8 * i));
    md5_add(&m, len, 8);
    for (int i = 0; i < 16; i++) snprintf(out + 2 * i, 3, "%02x", (m.s[i / 4] >> (8 * (i % 4))) & 0xff);
}

// ---- names --------------------------------------------------------------------------------------

// The cache's folders: a preview of up to 128 px is in normal, 256 large, 512 x-large, 1024 xx-large.
static const char *const thumb_dirs[THUMB_LEVELS] = { "normal", "large", "x-large", "xx-large" };

// The level for a preview drawn at px device pixels: the smallest that is at least as big.
static int thumb_level(int px)
{
    int l = 0;
    while (l < THUMB_LEVELS - 1 && (128 << l) < px) l++;
    return l;
}

// The cache: $XDG_CACHE_HOME/thumbnails, or ~/.cache/thumbnails (an XDG_CACHE_HOME that isn't
// absolute is ignored, as the spec says).
static bool thumb_root(char *out, size_t cap)
{
    return xdg_dir(out, cap, "XDG_CACHE_HOME", ".cache", "thumbnails");
}

// <root>/<dir>/<md5 of uri>.png
static bool thumb_file(const char *root, const char *dir, const char *uri, char *out, size_t cap)
{
    char md5[33];
    md5_hex(uri, strlen(uri), md5);
    int n = snprintf(out, cap, "%s/%s/%s.png", root, dir, md5);
    return n > 0 && (size_t)n < cap;
}

// ---- the PNG's text: Thumb::URI, Thumb::MTime, Thumb::Size ---------------------------------------

typedef struct {
    char uri[PATH_MAX * 3 + 8];
    i64  mtime, size;
    bool has_uri, has_mtime, has_size;
} ThumbMeta;

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

// Digits only, at most 18 of them (str_num).
static bool parse_i64(Str s, i64 *out)
{
    i64 v = s.len > 18 ? -1 : str_num(s, 999999999999999999LL);   // (leading zeros counted)
    return v >= 0 && (*out = v, true);
}

// The tEXt chunks of a whole PNG; false when it isn't one, or is cut short (no IEND).
static bool png_meta(Str png, ThumbMeta *m)
{
    *m = (ThumbMeta){ 0 };
    const u8 *p = (const u8 *)png.p;
    if (png.len < 8 || memcmp(p, "\x89PNG\r\n\x1a\n", 8) != 0) return false;
    for (isize i = 8; i + 12 <= png.len;) {
        u32 n = be32(p + i);
        if (n > (u64)(png.len - i - 12)) return false;
        const char *type = png.p + i + 4;
        Str d = { png.p + i + 8, n };
        if (memcmp(type, "IEND", 4) == 0) return true;
        isize z = memcmp(type, "tEXt", 4) == 0 ? str_find(d, 0) : -1;
        if (z > 0) {
            Str key = str_cut(d, 0, z), val = str_cut(d, z + 1, d.len);
            if (str_eq(key, S("Thumb::URI")) && val.len < (isize)sizeof m->uri)
                memcpy(m->uri, val.p, (size_t)val.len), m->uri[val.len] = 0, m->has_uri = true;
            else if (str_eq(key, S("Thumb::MTime"))) m->has_mtime = parse_i64(val, &m->mtime);
            else if (str_eq(key, S("Thumb::Size"))) m->has_size = parse_i64(val, &m->size);
        }
        i += 12 + (isize)n;
    }
    return false;
}

// Whether a cached preview is of this file as it is now.
static bool thumb_fresh(const ThumbMeta *m, const char *uri, i64 mtime, i64 size)
{
    return m->has_uri && strcmp(m->uri, uri) == 0 && m->has_mtime && m->mtime == mtime && (!m->has_size || m->size == size);
}

static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24), p[1] = (u8)(v >> 16), p[2] = (u8)(v >> 8), p[3] = (u8)v; }

// One chunk at *at in out: length, type, data, CRC over type and data.
static void thumb_chunk(u8 *out, isize *at, const char *type, const void *data, isize n)
{
    u8 *c = out + *at;
    put32(c, (u32)n);
    memcpy(c + 4, type, 4);
    if (n) memcpy(c + 8, data, (size_t)n);
    put32(c + 8 + n, (u32)crc32(0, c + 4, (uInt)(n + 4)));
    *at += 12 + n;
}

static void thumb_text(u8 *out, isize *at, const char *key, const char *val)
{
    char t[sizeof ((ThumbMeta *)0)->uri + 32];
    int n = snprintf(t, sizeof t, "%s%c%s", key, 0, val);
    thumb_chunk(out, at, "tEXt", t, n);
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

// im as a PNG (RGBA, 8 bits, Paeth-filtered) with the spec's text; uri NULL: no text.
static bool png_encode(Arena *a, Img im, const char *uri, i64 mtime, i64 size, Str *out)
{
    isize row = 1 + (isize)im.w * 4, raw_n = row * im.h;
    uLong zcap = compressBound((uLong)raw_n);
    u8 *raw = arena_try_push(a, raw_n, 1);
    u8 *png = raw ? arena_try_push(a, (isize)zcap + 16 * KiB(1) + 3 * (isize)sizeof ((ThumbMeta *)0)->uri, 1) : NULL;
    if (!png) return false;
    for (int y = 0; y < im.h; y++) {
        u8 *r = raw + y * row;
        const u8 *cur = im.px + (isize)y * im.w * 4, *up = y ? cur - (isize)im.w * 4 : NULL;
        r[0] = 4;
        for (int x = 0; x < im.w * 4; x++) {
            int left = x >= 4 ? cur[x - 4] : 0, above = up ? up[x] : 0, corner = up && x >= 4 ? up[x - 4] : 0;
            r[1 + x] = (u8)(cur[x] - paeth(left, above, corner));
        }
    }
    isize at = 8;
    memcpy(png, "\x89PNG\r\n\x1a\n", 8);
    u8 ihdr[13] = { 0, 0, 0, 0, 0, 0, 0, 0, 8, 6, 0, 0, 0 };
    put32(ihdr, (u32)im.w), put32(ihdr + 4, (u32)im.h);
    thumb_chunk(png, &at, "IHDR", ihdr, 13);
    if (uri) {
        char num[24];
        thumb_text(png, &at, "Thumb::URI", uri);
        snprintf(num, sizeof num, "%lld", (long long)mtime);
        thumb_text(png, &at, "Thumb::MTime", num);
        snprintf(num, sizeof num, "%lld", (long long)size);
        thumb_text(png, &at, "Thumb::Size", num);
        thumb_text(png, &at, "Software", "ofx");
    }
    // The IDAT's data is compressed in place after its length and type.
    uLongf zn = zcap;
    if (compress2(png + at + 8, &zn, raw, (uLong)raw_n, 6) != Z_OK) return false;
    put32(png + at, (u32)zn);
    memcpy(png + at + 4, "IDAT", 4);
    put32(png + at + 8 + zn, (u32)crc32(0, png + at + 4, (uInt)(zn + 4)));
    at += 12 + (isize)zn;
    thumb_chunk(png, &at, "IEND", NULL, 0);
    *out = (Str){ (const char *)png, at };
    return true;
}

// ---- files --------------------------------------------------------------------------------------

// png written to file through a temporary file in its folder, renamed over it, so another program
// never reads half of one. Every write asks the test guard first (guard.c).
static bool thumb_write(const char *file, Str png)
{
    static atomic_int seq;
    Err err;
    char tmp[PATH_MAX];
    Str dir = path_dir(str_from_c(file));
    int n = snprintf(tmp, sizeof tmp, "%.*s/.ofx-%d-%d.png", STR_ARG(dir), (int)getpid(), atomic_fetch_add(&seq, 1));
    if (n < 0 || (size_t)n >= sizeof tmp || !ops_allowed(file, &err) || !mkdir_p(file, 0700, false, &err)) return false;   // (0700: the spec's mode)
    return file_replace(file, tmp, png.p, png.len, 0600, false, &err);
}

// ---- decoding -----------------------------------------------------------------------------------

static bool img_new(Arena *a, int w, int h, Img *out)
{
    out->w = w, out->h = h;
    out->px = w > 0 && h > 0 ? arena_try_push(a, (isize)w * h * 4, 16) : NULL;
    return out->px != NULL;
}

// in with its longer side made side (never bigger than it was), into the arena.
static bool img_fit(Arena *a, Img in, int side, Img *out)
{
    int big = in.w > in.h ? in.w : in.h;
    if (big <= side) return img_new(a, in.w, in.h, out) && (memcpy(out->px, in.px, (size_t)in.w * in.h * 4), true);
    int w = (int)(((i64)in.w * side + big / 2) / big), h = (int)(((i64)in.h * side + big / 2) / big);
    if (!img_new(a, w > 0 ? w : 1, h > 0 ? h : 1, out)) return false;
    // Linear, as raylib's ImageResize: the sRGB path's table indexing trips clang's UBSan (vendored code).
    return stbir_resize_uint8_linear(in.px, in.w, in.h, 0, out->px, out->w, out->h, 0, STBIR_RGBA) != NULL;
}

static u32 tiff16(const u8 *p, bool le) { return le ? (u32)p[0] | (u32)p[1] << 8 : (u32)p[0] << 8 | p[1]; }
static u32 tiff32(const u8 *p, bool le) { return le ? tiff16(p, le) | tiff16(p + 2, le) << 16 : tiff16(p, le) << 16 | tiff16(p + 2, le); }

// A JPEG's EXIF orientation (1 to 8), 1 when it has none: a phone's photo is stored as the sensor
// saw it and turned by this tag (GNOME's previews are turned too).
static int jpeg_orientation(Str j)
{
    const u8 *p = (const u8 *)j.p;
    if (j.len < 4 || p[0] != 0xFF || p[1] != 0xD8) return 1;
    for (isize i = 2; i + 4 <= j.len;) {
        if (p[i] != 0xFF || p[i + 1] == 0xDA || p[i + 1] == 0xD9) return 1;   // the image data, or its end
        isize seg = (isize)tiff16(p + i + 2, false), end = i + 2 + seg;
        if (seg < 2) return 1;
        if (p[i + 1] == 0xE1 && seg >= 16 && end <= j.len && memcmp(p + i + 4, "Exif\0\0", 6) == 0) {
            const u8 *t = p + i + 10;
            isize n = end - (i + 10);
            bool le = t[0] == 'I' && t[1] == 'I';
            if (!le && !(t[0] == 'M' && t[1] == 'M')) return 1;
            u32 ifd = tiff32(t + 4, le);
            if (ifd > (u64)(n - 2)) return 1;
            u32 count = tiff16(t + ifd, le);
            for (u32 k = 0; k < count && ifd + 2 + 12 * (u64)(k + 1) <= (u64)n; k++) {
                const u8 *e = t + ifd + 2 + 12 * k;
                if (tiff16(e, le) == 0x0112) {
                    u32 v = tiff16(e + 8, le);
                    return v >= 1 && v <= 8 ? (int)v : 1;
                }
            }
            return 1;
        }
        i = end;
    }
    return 1;
}

// in turned or mirrored by EXIF orientation o, into the arena.
static bool img_orient(Arena *a, Img in, int o, Img *out)
{
    bool swap = o >= 5;
    if (!img_new(a, swap ? in.h : in.w, swap ? in.w : in.h, out)) return false;
    int w = in.w, h = in.h;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int dx, dy;
            switch (o) {
            case 2: dx = w - 1 - x, dy = y; break;
            case 3: dx = w - 1 - x, dy = h - 1 - y; break;
            case 4: dx = x, dy = h - 1 - y; break;
            case 5: dx = y, dy = x; break;
            case 6: dx = h - 1 - y, dy = x; break;
            case 7: dx = h - 1 - y, dy = w - 1 - x; break;
            case 8: dx = y, dy = w - 1 - x; break;
            default: dx = x, dy = y; break;
            }
            memcpy(out->px + ((isize)dy * out->w + dx) * 4, in.px + ((isize)y * w + x) * 4, 4);
        }
    return true;
}

// libjpeg-turbo's own memory for one JPEG, in MiB, and a progressive one's scans: it allocates a
// progressive file's coefficients at full size itself, outside the arena, and TurboJPEG 3.2's
// defaults are unlimited (F6.4 review: a 6.3 MB 40000 by 40000 file took 3.1 GiB).
// A 12 MP progressive photo needs about 70. A variable, so a test can lower it.
static int thumb_jpeg_max_mib = 256;
enum { THUMB_JPEG_SCANS_MAX = 500 };

// A TurboJPEG call that succeeded, or only warned: junk before a marker, a cut file (gdk-pixbuf,
// Nemo's, decodes both; F6.4 review).
static bool tj_ok(tjhandle tj, int r)
{
    return r == 0 || tj3GetErrorCode(tj) == TJERR_WARNING;
}

// A JPEG decoded at the smallest of libjpeg-turbo's scales that keeps its longer side at least
// side, then fitted and turned.
static bool jpeg_decode(Arena *a, Str src, int side, Img *out)
{
    tjhandle tj = tj3Init(TJINIT_DECOMPRESS);
    if (!tj) return false;
    Img full;
    bool ok = false;
    if (tj3Set(tj, TJPARAM_MAXMEMORY, thumb_jpeg_max_mib) == 0 && tj3Set(tj, TJPARAM_SCANLIMIT, THUMB_JPEG_SCANS_MAX) == 0 &&
        tj_ok(tj, tj3DecompressHeader(tj, (const u8 *)src.p, (size_t)src.len))) {
        int w = tj3Get(tj, TJPARAM_JPEGWIDTH), h = tj3Get(tj, TJPARAM_JPEGHEIGHT), big = w > h ? w : h, nsf;
        tjscalingfactor *sf = tj3GetScalingFactors(&nsf), best = { 1, 1 };
        for (int k = 0; sf && k < nsf; k++) {
            int s = TJSCALED(big, sf[k]);
            if (s >= side && s < TJSCALED(big, best)) best = sf[k];
        }
        ok = w > 0 && h > 0 && tj3SetScalingFactor(tj, best) == 0 && img_new(a, TJSCALED(w, best), TJSCALED(h, best), &full) &&
             tj_ok(tj, tj3Decompress8(tj, (const u8 *)src.p, (size_t)src.len, full.px, 0, TJPF_RGBA));
    }
    tj3Destroy(tj);
    Img fit;
    int o = jpeg_orientation(src);
    return ok && img_fit(a, full, side, &fit) && (o == 1 ? (*out = fit, true) : img_orient(a, fit, o, out));
}

// A WebP, scaled to side as it decodes. An animated one isn't decoded (WebPDecode refuses it).
static bool webp_decode(Arena *a, Str src, int side, Img *out)
{
    WebPDecoderConfig c;
    if (!WebPInitDecoderConfig(&c) || WebPGetFeatures((const u8 *)src.p, (size_t)src.len, &c.input) != VP8_STATUS_OK) return false;
    int w = c.input.width, h = c.input.height, big = w > h ? w : h;
    if (w <= 0 || h <= 0) return false;
    if (big > side) {
        c.options.use_scaling = 1;
        c.options.scaled_width = (int)(((i64)w * side + big / 2) / big), c.options.scaled_height = (int)(((i64)h * side + big / 2) / big);
        if (c.options.scaled_width < 1) c.options.scaled_width = 1;
        if (c.options.scaled_height < 1) c.options.scaled_height = 1;
        w = c.options.scaled_width, h = c.options.scaled_height;
    }
    if (!img_new(a, w, h, out)) return false;
    c.output.colorspace = MODE_RGBA;
    c.output.is_external_memory = 1;
    c.output.u.RGBA.rgba = out->px, c.output.u.RGBA.stride = w * 4, c.output.u.RGBA.size = (size_t)w * h * 4;
    bool ok = WebPDecode((const u8 *)src.p, (size_t)src.len, &c) == VP8_STATUS_OK;
    WebPFreeDecBuffer(&c.output);
    return ok;
}

// A PNG through libpng's simplified API, whole (as 8-bit sRGB RGBA), then fitted.
static bool png_decode(Arena *a, Str src, int side, Img *out)
{
    png_image im = { .version = PNG_IMAGE_VERSION };
    if (!png_image_begin_read_from_memory(&im, src.p, (size_t)src.len)) return false;
    Img full;
    im.format = PNG_FORMAT_RGBA;
    if (im.width == 0 || im.height == 0 || (i64)im.width * im.height > THUMB_PIXELS_MAX || !img_new(a, (int)im.width, (int)im.height, &full)) {
        png_image_free(&im);
        return false;
    }
    if (!png_image_finish_read(&im, NULL, full.px, 0, NULL)) return false;   // it frees im itself, either way
    return img_fit(a, full, side, out);
}

typedef struct { const u8 *p; isize n, at; } GifSrc;

static int gif_read(GifFileType *g, GifByteType *buf, int n)
{
    GifSrc *s = g->UserData;
    isize k = n < s->n - s->at ? n : s->n - s->at;
    memcpy(buf, s->p + s->at, (size_t)k);
    s->at += k;
    return (int)k;
}

// A GIF's first frame through giflib, on its screen (clear where the frame doesn't cover it, and
// where its graphic control says transparent), then fitted. Only the first frame is read: an
// animated GIF of many frames costs no more.
static bool gif_decode(Arena *a, Str src, int side, Img *out)
{
    GifSrc in = { (const u8 *)src.p, src.len, 0 };
    int err, transparent = -1;
    GifFileType *g = DGifOpen(&in, gif_read, &err);
    if (!g) return false;
    Img full;
    bool ok = false, done = false;
    if (g->SWidth > 0 && g->SHeight > 0 && (i64)g->SWidth * g->SHeight <= THUMB_PIXELS_MAX && img_new(a, g->SWidth, g->SHeight, &full)) {
        for (GifRecordType t; !done && DGifGetRecordType(g, &t) == GIF_OK;) {
            if (t == EXTENSION_RECORD_TYPE) {
                int code;
                GifByteType *ext;
                if (DGifGetExtension(g, &code, &ext) != GIF_OK) break;
                GraphicsControlBlock gcb;
                if (code == GRAPHICS_EXT_FUNC_CODE && ext && DGifExtensionToGCB(ext[0], ext + 1, &gcb) == GIF_OK) transparent = gcb.TransparentColor;
                while (ext && DGifGetExtensionNext(g, &ext) == GIF_OK && ext) {}
            } else if (t == IMAGE_DESC_RECORD_TYPE) {
                done = true;
                if (DGifGetImageDesc(g) != GIF_OK) break;
                GifImageDesc *d = &g->Image;
                ColorMapObject *map = d->ColorMap ? d->ColorMap : g->SColorMap;
                if (!map || d->Left < 0 || d->Top < 0 || d->Width <= 0 || d->Height <= 0 || d->Left + d->Width > g->SWidth || d->Top + d->Height > g->SHeight) break;
                GifPixelType *line = arena_try_push(a, d->Width, 1);
                if (!line) break;
                // An interlaced frame's rows come in four passes: every 8th from 0, every 8th from
                // 4, every 4th from 2, every 2nd from 1.
                static const int start[4] = { 0, 4, 2, 1 }, step[4] = { 8, 8, 4, 2 };
                ok = true;
                for (int pass = 0; ok && pass < (d->Interlace ? 4 : 1); pass++)
                    for (int y = d->Interlace ? start[pass] : 0; ok && y < d->Height; y += d->Interlace ? step[pass] : 1) {
                        ok = DGifGetLine(g, line, d->Width) == GIF_OK;
                        u8 *row = full.px + ((isize)(d->Top + y) * full.w + d->Left) * 4;
                        for (int x = 0; ok && x < d->Width; x++) {
                            int c = line[x];
                            if (c == transparent || c >= map->ColorCount) continue;
                            row[4 * x] = map->Colors[c].Red, row[4 * x + 1] = map->Colors[c].Green;
                            row[4 * x + 2] = map->Colors[c].Blue, row[4 * x + 3] = 255;
                        }
                    }
            } else if (t == TERMINATE_RECORD_TYPE) {
                break;
            }
        }
    }
    DGifCloseFile(g, &err);
    return ok && img_fit(a, full, side, out);
}

// The file's bytes as an image with its longer side at most side.
static bool thumb_make(Arena *a, ThumbKind k, Str src, int side, Img *out)
{
    switch (k) {
    case TK_JPEG: return jpeg_decode(a, src, side, out);
    case TK_WEBP: return webp_decode(a, src, side, out);
    case TK_PNG: return png_decode(a, src, side, out);
    case TK_GIF: return gif_decode(a, src, side, out);
    default: return false;
    }
}

// ---- which drives ------------------------------------------------------------------------------

// Whether ofx makes previews on the drive holding path, by mountinfo (the mount with the longest
// mount point above it): not on network file systems or FUSE ones (fuse.sshfs, fuse.gvfsd-fuse),
// whose reads can hang a worker; fuseblk, a local disk (NTFS), isn't "fuse" nor "fuse.*"; autofs, as
// a look under it mounts what it stands for (a NAS that is down blocks it; review, 2026-11-01).
// Unknown: local.
// Whether a mount of type t (as mountinfo writes it: "nfs4", "fuse.sshfs") is a network or user
// one, whose files may be far or slow.
static bool thumb_far_type(Str t)
{
    static const char *const far[] = { "nfs", "nfs4", "cifs", "smb3", "smbfs", "9p", "ceph", "afs", "glusterfs", "davfs", "lustre", "fuse", "autofs" };
    for (int i = 0; i < COUNT(far); i++) {
        isize n = (isize)strlen(far[i]);
        if (t.len >= n && memcmp(t.p, far[i], (size_t)n) == 0 && (t.len == n || t.p[n] == '.')) return true;
    }
    return false;
}

static bool thumb_local(Str mountinfo, const char *path)
{
    isize at = 0, best = -1;
    char type[64] = "";
    for (Str line; next_line(mountinfo, &at, &line);) {
        char mp[PATH_MAX];
        Str t, src;
        if (!mountinfo_line(line, mp, sizeof mp, &t, &src)) continue;
        size_t n = strlen(mp);
        if (!path_within(path, mp) || (isize)n < best || !t.len) continue;   // a later mount on the same point wins
        snprintf(type, sizeof type, "%.*s", STR_ARG(t));
        best = (isize)n;
    }
    return !thumb_far_type(str_from_c(type));
}

// ---- one preview --------------------------------------------------------------------------------

typedef struct {
    const char *path;   // absolute
    i64         mtime, size;   // as the listing read them
    ThumbKind   kind;   // TK_NONE: shown only when another program made it
    const char *exec;   // TK_EXEC: its thumbnailer's Exec line (thumbers_find)
    bool        make;   // on a local drive and within src_max: ofx may make it
    int         px;     // the device pixels its longer side is drawn at
    const char *root;   // the cache (thumb_root)
    i64         src_max;   // the largest file read to make one (ofx.conf's preview-max-size, F10)
} ThumbAsk;

typedef enum {
    TG_NONE,        // no preview: the icon stays
    TG_CACHED,      // read from the cache
    TG_MADE,        // made, and written to the cache
    TG_FAILED,      // the file wouldn't decode: written to fail/ofx/
    TG_FAIL_KNOWN,  // fail/ofx/ says so already for this mtime
    TG_CHANGED,     // the file changed since the listing: its next listing asks again
} ThumbGot;

// The preview of q's file by its thumbnailer, side px at most, into t: run in a folder of its own
// in the cache (<root>/.ofx-run-<pid>-<n>, emptied and gone after), its PNG read back. TG_MADE;
// TG_FAILED when it made none; TG_NONE when it was killed for its time, or didn't run (nothing
// written: it may make one next time).
static ThumbGot thumb_exec(Arena *a, const ThumbAsk *q, const char *uri, int side, Img *t)
{
    static atomic_int seq;
    Err err;
    char dir[PATH_MAX], out[PATH_MAX];
    if (snprintf(dir, sizeof dir, "%s/.ofx-run-%d-%d", q->root, (int)getpid(), atomic_fetch_add(&seq, 1)) >= (int)sizeof dir ||
        snprintf(out, sizeof out, "%s/out.png", dir) >= (int)sizeof out)
        return TG_NONE;
    if (!ops_allowed(dir, &err) || !mkdir_p(dir, 0700, false, &err) || mkdir(dir, 0700) != 0) return TG_NONE;
    const char *argv[THUMB_ARGS_MAX + 1];
    ThumbGot got = TG_FAILED;   // (an Exec line that won't fill in: a failure too)
    struct stat st;
    if (thumbers_argv(a, q->exec, q->path, uri, out, side, argv, COUNT(argv)) > 0) {
        BoxRun r = thumb_run(argv, q->path, dir);
        Str png = r == BOX_ENDED ? file_read_at(a, AT_FDCWD, out, THUMB_CACHED_MAX, &st, O_NOFOLLOW) : STR_EMPTY;
        got = r != BOX_ENDED ? TG_NONE : png.len && png_decode(a, png, side, t) ? TG_MADE : TG_FAILED;
        if (got == TG_FAILED && !png.len && access(dir, F_OK) != 0) got = TG_NONE;   // (its folder went meanwhile: Clear Previews, not the file's fault; F12 step 4 review)
    }
    isize gone;
    Err derr;
    delete_tree(dir, &gone, &derr);   // (whatever it left, its own folders too: code-health review, 2026-11-03)
    return got;
}

// The preview for q, drawn size, into the arena (out: when TG_CACHED or TG_MADE). Reads only the
// one directory q->px picks; writes there, or to fail/ofx/.
static ThumbGot thumb_get(Arena *a, const ThumbAsk *q, Img *out)
{
    char uri[sizeof ((ThumbMeta *)0)->uri], file[PATH_MAX], fail[PATH_MAX];
    if (path_below(q->path, q->root)) return TG_NONE;   // the cache's own files (the spec)
    int level = thumb_level(q->px), side = 128 << level;
    if (!file_uri(q->path, uri, sizeof uri) || !thumb_file(q->root, thumb_dirs[level], uri, file, sizeof file)) return TG_NONE;
    struct stat st;
    ThumbMeta m;
    Str c = file_read_at(a, AT_FDCWD, file, THUMB_CACHED_MAX, &st, 0);
    if (c.len && png_meta(c, &m) && thumb_fresh(&m, uri, q->mtime, q->size) && png_decode(a, c, q->px, out)) return TG_CACHED;
    if (q->kind == TK_NONE || !q->make || !thumb_file(q->root, "fail/ofx", uri, fail, sizeof fail)) return TG_NONE;
    Str f = file_read_at(a, AT_FDCWD, fail, THUMB_CACHED_MAX, &st, 0);
    if (f.len && png_meta(f, &m) && thumb_fresh(&m, uri, q->mtime, q->size)) return TG_FAIL_KNOWN;
    st = (struct stat){ 0 };
    Img t;
    bool made;
    if (q->kind == TK_EXEC) {   // (the thumbnailer reads the file itself, so no preview-max-size)
        if (stat(q->path, &st) != 0 || !S_ISREG(st.st_mode)) return TG_NONE;
        if (st.st_mtim.tv_sec != q->mtime || st.st_size != q->size) return TG_CHANGED;
        ThumbGot g = thumb_exec(a, q, uri, side, &t);
        if (g == TG_NONE) return TG_NONE;
        made = g == TG_MADE;
    } else {
        Str src = file_read_at(a, AT_FDCWD, q->path, q->src_max, &st, 0);
        bool empty = S_ISREG(st.st_mode) && st.st_size == 0;   // read, and nothing in it
        if (!src.len && !empty) return TG_NONE;   // gone, unreadable, or grown past the limit
        if (st.st_mtim.tv_sec != q->mtime || st.st_size != q->size) return TG_CHANGED;
        made = !empty && thumb_make(a, q->kind, src, side, &t);
    }
    Str png;
    if (!made) {
        u8 clear[4] = { 0 };
        if (png_encode(a, (Img){ clear, 1, 1 }, uri, q->mtime, q->size, &png)) thumb_write(fail, png);
        return TG_FAILED;
    }
    if (png_encode(a, t, uri, q->mtime, q->size, &png)) thumb_write(file, png);
    return img_fit(a, t, q->px, out) ? TG_MADE : TG_NONE;
}
