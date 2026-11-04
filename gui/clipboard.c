// The system clipboard for files (docs/plan.md, F3 "System clipboard"). GLFW offers only
// text/plain, which Nemo can't paste as files, so ofx opens a second Wayland connection of its own
// and binds ext-data-control-v1, which needs no input serial: Hyprland has it, weston (the tests)
// doesn't, and without it the clipboard stays inside ofx. fs/clip.c makes the payloads.
//
// A thread dispatches the connection: it answers other programs' requests for our payloads and
// keeps track of what the selection offers. The main thread sets the selection and reads others'.
// Included by main.c.

#include "ext-data-control-v1-client-protocol.h"
#include "ext-data-control-v1-client-protocol-code.h"

#include <fcntl.h>
#include <poll.h>
#include <stdatomic.h>

enum { CLIP_OFFERS = 8, CLIP_READ_MAX = MiB(1), CLIP_WAIT_MS = 1000 };
enum { OFFER_GNOME = 1, OFFER_URIS = 2, OFFER_TEXT = 4, OFFER_UTF8 = 8 };

static u8 clip_mem[MiB(2)];   // our payloads, under clip.mu

static struct {
    struct wl_display                  *dpy;
    struct wl_seat                     *seat;
    struct ext_data_control_manager_v1 *mgr;
    struct ext_data_control_device_v1  *dev;
    atomic_bool ok;        // the protocol is there: the system clipboard works (the thread clears it)
    pthread_t thread;
    bool      started;     // thread is running (an unstarted pthread_t is no value to test)
    int       wake[2];     // a pipe that ends the thread
    pthread_mutex_t mu;

    struct ext_data_control_source_v1 *src;   // ours, while we own the selection
    bool      text;        // it is an edit line's text, not files (F5.2)
    Arena     arena;
    Str       payload[CLIP_FMTS];

    // Offers: one slot each, the types they offer as bits.
    struct { struct ext_data_control_offer_v1 *offer; unsigned types; } offers[CLIP_OFFERS];
    int       sel;         // the slot of the selection's offer, -1 for none
} clip = { .sel = -1, .wake = { -1, -1 } };

static int offer_slot(struct ext_data_control_offer_v1 *o)
{
    for (int i = 0; i < CLIP_OFFERS; i++)
        if (clip.offers[i].offer == o) return i;
    return -1;
}

static void offer_drop(int i)
{
    if (i < 0) return;
    ext_data_control_offer_v1_destroy(clip.offers[i].offer);
    clip.offers[i].offer = NULL;
    if (clip.sel == i) clip.sel = -1;
}

static void on_offer_type(void *data, struct ext_data_control_offer_v1 *o, const char *mime)
{
    (void)data;
    pthread_mutex_lock(&clip.mu);
    int i = offer_slot(o);
    if (i >= 0 && strcmp(mime, CLIP_MIME_GNOME) == 0) clip.offers[i].types |= OFFER_GNOME;
    if (i >= 0 && strcmp(mime, CLIP_MIME_URIS) == 0) clip.offers[i].types |= OFFER_URIS;
    if (i >= 0 && strcmp(mime, CLIP_MIME_TEXT[0]) == 0) clip.offers[i].types |= OFFER_UTF8;
    if (i >= 0 && strcmp(mime, CLIP_MIME_TEXT[1]) == 0) clip.offers[i].types |= OFFER_TEXT;
    pthread_mutex_unlock(&clip.mu);
}

static const struct ext_data_control_offer_v1_listener offer_listener = { .offer = on_offer_type };

static void on_data_offer(void *data, struct ext_data_control_device_v1 *d, struct ext_data_control_offer_v1 *o)
{
    (void)data, (void)d;
    pthread_mutex_lock(&clip.mu);
    int free = -1;
    for (int i = 0; i < CLIP_OFFERS && free < 0; i++)
        if (!clip.offers[i].offer) free = i;
    if (free < 0) {
        // Full (offers that never became a selection): the oldest that isn't the selection goes.
        for (int i = 0; i < CLIP_OFFERS && free < 0; i++)
            if (i != clip.sel) offer_drop(i), free = i;
    }
    clip.offers[free].offer = o, clip.offers[free].types = 0;
    pthread_mutex_unlock(&clip.mu);
    ext_data_control_offer_v1_add_listener(o, &offer_listener, NULL);
}

static void on_selection(void *data, struct ext_data_control_device_v1 *d, struct ext_data_control_offer_v1 *o)
{
    (void)data, (void)d;
    pthread_mutex_lock(&clip.mu);
    int old = clip.sel;
    clip.sel = o ? offer_slot(o) : -1;
    if (old >= 0 && old != clip.sel) offer_drop(old);
    pthread_mutex_unlock(&clip.mu);
}

static void on_primary(void *data, struct ext_data_control_device_v1 *d, struct ext_data_control_offer_v1 *o)
{
    (void)data, (void)d;
    if (!o) return;
    pthread_mutex_lock(&clip.mu);
    offer_drop(offer_slot(o));   // ofx doesn't use the primary selection
    pthread_mutex_unlock(&clip.mu);
}

static void on_finished(void *data, struct ext_data_control_device_v1 *d)
{
    (void)data, (void)d;
    clip.ok = false;   // the compositor withdrew the device
}

static const struct ext_data_control_device_v1_listener device_listener = {
    .data_offer = on_data_offer, .selection = on_selection, .finished = on_finished, .primary_selection = on_primary,
};

// Another program wants our payload: written to fd, a second at most, then closed. clip.mu is held
// meanwhile (clip_set resets the payloads' arena), so a slow reader can stall a Ctrl+C or a paste
// by that second at most.
static void on_send(void *data, struct ext_data_control_source_v1 *s, const char *mime, int32_t fd)
{
    (void)data, (void)s;
    pthread_mutex_lock(&clip.mu);
    Str p = STR_EMPTY;
    if (strcmp(mime, CLIP_MIME_GNOME) == 0) p = clip.payload[CLIP_FMT_GNOME];
    else if (strcmp(mime, CLIP_MIME_URIS) == 0) p = clip.payload[CLIP_FMT_URIS];
    else if (strcmp(mime, CLIP_MIME_TEXT[0]) == 0 || strcmp(mime, CLIP_MIME_TEXT[1]) == 0) p = clip.payload[CLIP_FMT_TEXT];
    fcntl(fd, F_SETFL, O_NONBLOCK);
    i64 until = now_ms() + CLIP_WAIT_MS;
    for (isize at = 0; at < p.len && now_ms() < until;) {
        ssize_t k = write(fd, p.p + at, (size_t)(p.len - at));
        if (k > 0) at += k;
        else if (k < 0 && errno != EAGAIN && errno != EINTR) break;
        else poll(&(struct pollfd){ .fd = fd, .events = POLLOUT }, 1, 50);
    }
    pthread_mutex_unlock(&clip.mu);
    close(fd);
}

// Someone else owns the selection now. Whoever takes a source out of clip.src destroys it: when
// clip_set or clip_clear_own already did (a Ctrl+C racing another program's copy), s is gone, and
// destroying it here too would free it twice (F3 review, 2026-10-20).
static void on_cancelled(void *data, struct ext_data_control_source_v1 *s)
{
    (void)data;
    pthread_mutex_lock(&clip.mu);
    bool mine = clip.src == s;
    if (mine) clip.src = NULL;
    pthread_mutex_unlock(&clip.mu);
    if (mine) ext_data_control_source_v1_destroy(s);
}

static const struct ext_data_control_source_v1_listener source_listener = { .send = on_send, .cancelled = on_cancelled };

static void on_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version)
{
    (void)data, (void)version;
    if (strcmp(iface, ext_data_control_manager_v1_interface.name) == 0 && !clip.mgr)
        clip.mgr = wl_registry_bind(r, name, &ext_data_control_manager_v1_interface, 1);
    else if (strcmp(iface, wl_seat_interface.name) == 0 && !clip.seat)
        clip.seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
}

static void on_global_remove(void *data, struct wl_registry *r, uint32_t name)
{
    (void)data, (void)r, (void)name;
}

static const struct wl_registry_listener registry_listener = { .global = on_global, .global_remove = on_global_remove };

static void *clip_loop(void *arg)
{
    (void)arg;
    int fd = wl_display_get_fd(clip.dpy);
    for (;;) {
        while (wl_display_prepare_read(clip.dpy) != 0) wl_display_dispatch_pending(clip.dpy);
        wl_display_flush(clip.dpy);
        struct pollfd p[2] = { { .fd = fd, .events = POLLIN }, { .fd = clip.wake[0], .events = POLLIN } };
        if (poll(p, 2, -1) < 0 && errno != EINTR) {
            wl_display_cancel_read(clip.dpy);
            break;
        }
        if (p[1].revents) {
            wl_display_cancel_read(clip.dpy);
            break;
        }
        if (p[0].revents & POLLIN) {
            if (wl_display_read_events(clip.dpy) != 0) break;
        } else {
            wl_display_cancel_read(clip.dpy);
            if (p[0].revents & (POLLERR | POLLHUP)) break;
        }
        wl_display_dispatch_pending(clip.dpy);
    }
    clip.ok = false;   // (the connection gone: a copy falls back to GLFW's, not lost; F11 review)
    return NULL;
}

// Connects and starts the thread: false (and the clipboard stays inside ofx) without the protocol.
static bool clip_init(void)
{
    arena_init(&clip.arena, "clipboard", clip_mem, sizeof clip_mem);
    pthread_mutex_init(&clip.mu, NULL);
    clip.dpy = wl_display_connect(NULL);
    if (!clip.dpy) return false;
    struct wl_registry *reg = wl_display_get_registry(clip.dpy);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(clip.dpy);
    if (!clip.mgr || !clip.seat) {
        if (clip.seat) wl_seat_destroy(clip.seat), clip.seat = NULL;
        if (clip.mgr) ext_data_control_manager_v1_destroy(clip.mgr), clip.mgr = NULL;
        wl_registry_destroy(reg);
        wl_display_disconnect(clip.dpy);
        clip.dpy = NULL;
        return false;
    }
    wl_registry_destroy(reg);
    clip.dev = ext_data_control_manager_v1_get_data_device(clip.mgr, clip.seat);
    ext_data_control_device_v1_add_listener(clip.dev, &device_listener, NULL);
    wl_display_roundtrip(clip.dpy);   // the current selection
    if (pipe2(clip.wake, O_CLOEXEC) != 0) return false;
    clip.started = pthread_create(&clip.thread, NULL, clip_loop, NULL) == 0;
    clip.ok = clip.started;
    return clip.started;
}

// At quit: the thread told to stop, with the other workers (quit_workers).
static void clip_quit(void)
{
    if (clip.dpy && clip.wake[1] >= 0) write(clip.wake[1], "q", 1);
}

// Then joined, with no deadline: its longest wait is a reader's second (on_send), and the objects
// it dispatches for go after it.
static void clip_close(void)
{
    if (!clip.dpy) return;
    if (clip.wake[1] >= 0 && clip.started) pthread_join(clip.thread, NULL);
    if (clip.src) ext_data_control_source_v1_destroy(clip.src);
    for (int i = 0; i < CLIP_OFFERS; i++)
        if (clip.offers[i].offer) ext_data_control_offer_v1_destroy(clip.offers[i].offer);
    if (clip.dev) ext_data_control_device_v1_destroy(clip.dev);
    if (clip.mgr) ext_data_control_manager_v1_destroy(clip.mgr);
    if (clip.seat) wl_seat_destroy(clip.seat);
    wl_display_flush(clip.dpy);
    wl_display_disconnect(clip.dpy);
    clip.dpy = NULL;
    close(clip.wake[0]), close(clip.wake[1]);
}

// A new source of ours offered as the selection, the payloads made already; under clip.mu.
static void clip_offer(bool files)
{
    struct ext_data_control_source_v1 *old = clip.src;
    clip.src = ext_data_control_manager_v1_create_data_source(clip.mgr);
    ext_data_control_source_v1_add_listener(clip.src, &source_listener, NULL);
    if (files) ext_data_control_source_v1_offer(clip.src, CLIP_MIME_GNOME);
    if (files) ext_data_control_source_v1_offer(clip.src, CLIP_MIME_URIS);
    ext_data_control_source_v1_offer(clip.src, CLIP_MIME_TEXT[0]);
    ext_data_control_source_v1_offer(clip.src, CLIP_MIME_TEXT[1]);
    ext_data_control_device_v1_set_selection(clip.dev, clip.src);
    clip.text = !files;
    if (old) ext_data_control_source_v1_destroy(old);
}

// The selection becomes files: every payload made, then offered.
static bool clip_set(const ClipFiles *c)
{
    if (!clip.ok) return false;
    pthread_mutex_lock(&clip.mu);
    arena_reset(&clip.arena);
    for (int f = 0; f < CLIP_FMTS; f++) clip.payload[f] = clip_payload(&clip.arena, c, (ClipFormat)f);
    clip_offer(true);
    pthread_mutex_unlock(&clip.mu);
    wl_display_flush(clip.dpy);
    return true;
}

// The selection becomes text (an edit line's Ctrl+C, F5.2), offered as text/plain only, as any
// program's copy: our files leave the clipboard. Through our own connection rather than GLFW's,
// whose reads have no time limit, and whose source the main thread couldn't serve while
// clip_get_text waits on it.
static bool clip_set_text(Str text)
{
    if (!clip.ok) return false;
    pthread_mutex_lock(&clip.mu);
    arena_reset(&clip.arena);
    for (int f = 0; f < CLIP_FMTS; f++) clip.payload[f] = STR_EMPTY;
    clip.payload[CLIP_FMT_TEXT] = str_from_c(arena_cstr(&clip.arena, text));
    clip_offer(false);
    pthread_mutex_unlock(&clip.mu);
    wl_display_flush(clip.dpy);
    return true;
}

// The selection emptied, if it is still ours (a cut that was pasted).
static void clip_clear_own(void)
{
    if (!clip.ok) return;
    pthread_mutex_lock(&clip.mu);
    if (clip.src) {
        ext_data_control_device_v1_set_selection(clip.dev, NULL);
        ext_data_control_source_v1_destroy(clip.src);
        clip.src = NULL;
    }
    pthread_mutex_unlock(&clip.mu);
    wl_display_flush(clip.dpy);
}


typedef enum { CLIP_NONE, CLIP_OURS, CLIP_THEIRS } ClipFrom;

// Offer i's `mime` read into a, NUL-ended: the other program writes it, a second at most, so the
// window keeps answering. Called with clip.mu held; returns with it released.
static bool clip_receive(Arena *a, int i, const char *mime, Str *out)
{
    int fds[2] = { -1, -1 };
    if (pipe2(fds, O_CLOEXEC) != 0) {
        pthread_mutex_unlock(&clip.mu);
        return false;
    }
    ext_data_control_offer_v1_receive(clip.offers[i].offer, mime, fds[1]);
    pthread_mutex_unlock(&clip.mu);
    wl_display_flush(clip.dpy);
    close(fds[1]);
    isize avail;
    char *buf = arena_tail(a, &avail);
    if (avail > CLIP_READ_MAX) avail = CLIP_READ_MAX;
    isize n = 0;
    i64 until = now_ms() + CLIP_WAIT_MS;
    while (n < avail - 1 && now_ms() < until) {
        if (poll(&(struct pollfd){ .fd = fds[0], .events = POLLIN }, 1, 50) <= 0) continue;
        ssize_t k = read(fds[0], buf + n, (size_t)(avail - 1 - n));
        if (k <= 0) break;
        n += k;
    }
    close(fds[0]);
    buf[n] = 0;
    arena_commit(a, n + 1);
    *out = (Str){ buf, n };
    return true;
}

// What a paste would take: our own files (the caller has them), another program's files (into c,
// strings in a), or none (text, an image, or nothing).
static ClipFrom clip_get(Arena *a, ClipFiles *c)
{
    if (!clip.ok) return CLIP_NONE;
    pthread_mutex_lock(&clip.mu);
    if (clip.src) {
        bool text = clip.text;
        pthread_mutex_unlock(&clip.mu);
        return text ? CLIP_NONE : CLIP_OURS;
    }
    int i = clip.sel;
    unsigned types = i >= 0 ? clip.offers[i].types & (OFFER_GNOME | OFFER_URIS) : 0;
    bool gnome = types & OFFER_GNOME;
    Str got;
    if (!types) {   // (left held, the next paste or clipboard change hung the window: 2026-10-28)
        pthread_mutex_unlock(&clip.mu);
        return CLIP_NONE;
    }
    if (!clip_receive(a, i, gnome ? CLIP_MIME_GNOME : CLIP_MIME_URIS, &got)) return CLIP_NONE;   // (it released the lock)
    return clip_parse(a, got, gnome, c) ? CLIP_THEIRS : CLIP_NONE;
}

// Whether the selection is ours (we offered it last, and nobody has taken it since).
static bool clip_ours(void)
{
    pthread_mutex_lock(&clip.mu);
    bool ours = clip.src != NULL;
    pthread_mutex_unlock(&clip.mu);
    return ours;
}

// The selection as text for an edit line's Ctrl+V (into a): ours as we offer it, another
// program's text/plain. False without the protocol (the caller asks GLFW); true and empty when the
// selection holds no text.
static bool clip_get_text(Arena *a, Str *out)
{
    *out = STR_EMPTY;
    if (!clip.ok) return false;
    pthread_mutex_lock(&clip.mu);
    if (clip.src) {
        *out = str_from_c(arena_cstr(a, clip.payload[CLIP_FMT_TEXT]));
        pthread_mutex_unlock(&clip.mu);
        return true;
    }
    int i = clip.sel;
    unsigned types = i >= 0 ? clip.offers[i].types : 0;
    if (!(types & (OFFER_UTF8 | OFFER_TEXT))) {
        pthread_mutex_unlock(&clip.mu);
        return true;
    }
    clip_receive(a, i, types & OFFER_UTF8 ? CLIP_MIME_TEXT[0] : CLIP_MIME_TEXT[1], out);
    return true;
}
