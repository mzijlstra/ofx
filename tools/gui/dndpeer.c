// The other program in F14's drag and drop tests (2026-11-05), in the test sway only (testsway.h):
//   dndpeer target <log>              a window that takes text/uri-list as a copy, logging what it is
//                                     offered and given
//   dndpeer target-move <log>         the same, a move when the source offers one
//   dndpeer target-hangup <log>       the same, but it closes the pipe unread (the source's write
//                                     then gets EPIPE, or SIGPIPE where it isn't blocked)
//   dndpeer source <uri> <log>        a window whose left press drags <uri> (text/uri-list), copy
//                                     or move
//   dndpeer source-text <text> <log>  the same with text/plain only (no files)
//   dndpeer source-stall <uri> <log>  the same as source, but it never writes the data asked for
// The pointer is vptr's. The log is a line per event.

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"
#include "xdg-shell-client-protocol-code.h"
#include "testsway.h"

static struct wl_compositor *comp;
static struct wl_shm *shm;
static struct xdg_wm_base *wm;
static struct wl_seat *seat;
static struct wl_data_device_manager *mgr;
static struct wl_surface *surf;
static struct wl_data_device *dev;
static struct wl_data_offer *offer;
static struct wl_data_source *src;
static bool source, uris, done, configured, text_only, stall, hangup, prefer_move;
static const char *uri;
static int stalled[8], nstalled;   // (source-stall: the fds asked for, held open, never written)
static FILE *peer_log;
static int width = 200, height = 200;

#define LOG(...) (fprintf(peer_log, __VA_ARGS__), fputc('\n', peer_log), fflush(peer_log))

static void on_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t v)
{
    (void)data;
    if (!strcmp(iface, wl_compositor_interface.name)) comp = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_shm_interface.name)) shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, xdg_wm_base_interface.name)) wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name) && !seat) seat = wl_registry_bind(r, name, &wl_seat_interface, v < 5 ? v : 5);
    else if (!strcmp(iface, wl_data_device_manager_interface.name)) mgr = wl_registry_bind(r, name, &wl_data_device_manager_interface, v < 3 ? v : 3);
}
static void on_global_remove(void *data, struct wl_registry *r, uint32_t name) { (void)data, (void)r, (void)name; }
static const struct wl_registry_listener reg_l = { on_global, on_global_remove };

static void wm_ping(void *data, struct xdg_wm_base *b, uint32_t serial) { (void)data; xdg_wm_base_pong(b, serial); }
static const struct xdg_wm_base_listener wm_l = { wm_ping };

// A plain grey buffer of the size asked for.
static void draw(void)
{
    int stride = width * 4, size = stride * height;
    int fd = memfd_create("dndpeer", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, size) != 0) exit(1);
    void *px = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    memset(px, source ? 0x60 : 0xa0, (size_t)size);
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    munmap(px, (size_t)size), close(fd);
    wl_surface_attach(surf, buf, 0, 0);
    wl_surface_damage(surf, 0, 0, width, height);
    wl_surface_commit(surf);
}
static void xs_configure(void *data, struct xdg_surface *xs, uint32_t serial) { (void)data; xdg_surface_ack_configure(xs, serial); configured = true; draw(); }
static const struct xdg_surface_listener xs_l = { xs_configure };
static void top_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *s)
{
    (void)data, (void)t, (void)s;
    if (w > 0 && h > 0) width = w, height = h;
}
static void top_close(void *data, struct xdg_toplevel *t) { (void)data, (void)t; done = true; }
static const struct xdg_toplevel_listener top_l = { .configure = top_configure, .close = top_close };

static void of_offer(void *data, struct wl_data_offer *o, const char *mime) { (void)data, (void)o; LOG("offer type %s", mime); if (!strcmp(mime, "text/uri-list")) uris = true; }
static void of_actions(void *data, struct wl_data_offer *o, uint32_t a) { (void)data, (void)o; LOG("source actions %u", a); }
static void of_action(void *data, struct wl_data_offer *o, uint32_t a) { (void)data, (void)o; LOG("action %u", a); }
static const struct wl_data_offer_listener of_l = { of_offer, of_actions, of_action };

static void dv_data_offer(void *data, struct wl_data_device *d, struct wl_data_offer *o) { (void)data, (void)d; uris = false; wl_data_offer_add_listener(o, &of_l, NULL); }
static void dv_enter(void *data, struct wl_data_device *d, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *o)
{
    (void)data, (void)d, (void)s;
    LOG("enter at %.1f,%.1f", wl_fixed_to_double(x), wl_fixed_to_double(y));
    offer = o;
    if (!o) return;
    wl_data_offer_accept(o, serial, uris ? "text/uri-list" : NULL);
    wl_data_offer_set_actions(o, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE,
                              prefer_move ? WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE : WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
}
static void dv_leave(void *data, struct wl_data_device *d) { (void)data, (void)d; LOG("leave"); offer = NULL; }
static void dv_motion(void *data, struct wl_data_device *d, uint32_t t, wl_fixed_t x, wl_fixed_t y) { (void)data, (void)d, (void)t, (void)x, (void)y; }
static void dv_drop(void *data, struct wl_data_device *d)
{
    (void)data, (void)d;
    LOG("drop");
    if (!offer || !uris) return;
    int p[2];
    if (pipe(p) != 0) return;
    wl_data_offer_receive(offer, "text/uri-list", p[1]);
    close(p[1]);
    if (hangup) {
        close(p[0]);
        wl_display_roundtrip(wl_proxy_get_display((struct wl_proxy *)offer));
        LOG("hung up");
        wl_data_offer_finish(offer);
        return;
    }
    wl_display_flush(wl_proxy_get_display((struct wl_proxy *)offer));   // (another process answers: a blocking read is safe here)
    char buf[4096];
    ssize_t n = 0, r;
    while (n < (ssize_t)sizeof buf - 1 && (r = read(p[0], buf + n, sizeof buf - 1 - (size_t)n)) > 0) n += r;
    close(p[0]);
    buf[n > 0 ? n : 0] = 0;
    LOG("received %zd bytes: %s", n, buf);
    wl_data_offer_finish(offer);
    LOG("finished");
}
static void dv_selection(void *data, struct wl_data_device *d, struct wl_data_offer *o) { (void)data, (void)d, (void)o; }
static const struct wl_data_device_listener dv_l = { dv_data_offer, dv_enter, dv_leave, dv_motion, dv_drop, dv_selection };

static void sr_target(void *data, struct wl_data_source *s, const char *m) { (void)data, (void)s; LOG("source target %s", m ? m : "(none)"); }
static void sr_send(void *data, struct wl_data_source *s, const char *m, int fd)
{
    (void)data, (void)s;
    LOG("source send %s", m);
    if (stall && nstalled < 8) {
        stalled[nstalled++] = fd;
        return;
    }
    dprintf(fd, text_only ? "%s" : "%s\r\n", uri);
    close(fd);
}
static void sr_cancelled(void *data, struct wl_data_source *s) { (void)data, (void)s; LOG("source cancelled"); }
static void sr_performed(void *data, struct wl_data_source *s) { (void)data, (void)s; LOG("source dnd_drop_performed"); }
static void sr_finished(void *data, struct wl_data_source *s) { (void)data, (void)s; LOG("source dnd_finished"); }
static void sr_action(void *data, struct wl_data_source *s, uint32_t a) { (void)data, (void)s; LOG("source action %u", a); }
static const struct wl_data_source_listener sr_l = { sr_target, sr_send, sr_cancelled, sr_performed, sr_finished, sr_action };

static void pt_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y) { (void)data, (void)p, (void)serial, (void)s, (void)x, (void)y; LOG("pointer enter"); }
static void pt_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s) { (void)data, (void)p, (void)serial, (void)s; }
static void pt_motion(void *data, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y) { (void)data, (void)p, (void)t, (void)x, (void)y; }
static void pt_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state)
{
    (void)data, (void)p, (void)t;
    LOG("pointer button %u state %u", button, state);
    if (!source || state != WL_POINTER_BUTTON_STATE_PRESSED || src) return;
    src = wl_data_device_manager_create_data_source(mgr);
    wl_data_source_add_listener(src, &sr_l, NULL);
    wl_data_source_offer(src, text_only ? "text/plain" : "text/uri-list");
    wl_data_source_set_actions(src, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE);
    wl_data_device_start_drag(dev, src, surf, NULL, serial);
    LOG("start_drag");
}
static void pt_axis(void *data, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) { (void)data, (void)p, (void)t, (void)a, (void)v; }
static void pt_frame(void *data, struct wl_pointer *p) { (void)data, (void)p; }
static void pt_axis_source(void *data, struct wl_pointer *p, uint32_t s) { (void)data, (void)p, (void)s; }
static void pt_axis_stop(void *data, struct wl_pointer *p, uint32_t t, uint32_t a) { (void)data, (void)p, (void)t, (void)a; }
static void pt_axis_discrete(void *data, struct wl_pointer *p, uint32_t a, int32_t d) { (void)data, (void)p, (void)a, (void)d; }
static const struct wl_pointer_listener pt_l = { .enter = pt_enter, .leave = pt_leave, .motion = pt_motion, .button = pt_button, .axis = pt_axis, .frame = pt_frame,
                                                 .axis_source = pt_axis_source, .axis_stop = pt_axis_stop, .axis_discrete = pt_axis_discrete };

static struct wl_pointer *ptr;
static void seat_caps(void *data, struct wl_seat *s, uint32_t caps)
{
    (void)data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !ptr) ptr = wl_seat_get_pointer(s), wl_pointer_add_listener(ptr, &pt_l, NULL);
    else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && ptr) wl_pointer_release(ptr), ptr = NULL;
}
static void seat_name(void *data, struct wl_seat *s, const char *name) { (void)data, (void)s, (void)name; }
static const struct wl_seat_listener seat_l = { seat_caps, seat_name };

int main(int argc, char **argv)
{
    static const char *const modes[] = { "target", "target-move", "target-hangup", "source", "source-text", "source-stall" };
    int m = -1;
    for (int i = 0; argc > 1 && i < 6; i++)
        if (!strcmp(argv[1], modes[i])) m = i;
    if (m < 0 || argc != (m >= 3 ? 4 : 3)) {
        fprintf(stderr, "dndpeer target|target-move|target-hangup <log> | dndpeer source|source-text|source-stall <data> <log>\n");
        return 2;
    }
    prefer_move = m == 1, hangup = m == 2, text_only = m == 4, stall = m == 5;
    source = m >= 3;
    uri = source ? argv[2] : NULL;
    peer_log = fopen(argv[source ? 3 : 2], "a");
    if (!peer_log) return 1;
    struct wl_display *d = test_sway_connect("dndpeer");
    if (!d) return 1;
    struct wl_registry *r = wl_display_get_registry(d);
    wl_registry_add_listener(r, &reg_l, NULL);
    wl_display_roundtrip(d);
    if (!comp || !shm || !wm || !seat || !mgr) return LOG("missing globals"), 1;
    xdg_wm_base_add_listener(wm, &wm_l, NULL);
    surf = wl_compositor_create_surface(comp);
    struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(wm, surf);
    xdg_surface_add_listener(xs, &xs_l, NULL);
    struct xdg_toplevel *top = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(top, &top_l, NULL);
    xdg_toplevel_set_title(top, source ? "dndpeer source" : "dndpeer target");
    wl_surface_commit(surf);
    dev = wl_data_device_manager_get_data_device(mgr, seat);
    wl_data_device_add_listener(dev, &dv_l, NULL);
    wl_seat_add_listener(seat, &seat_l, NULL);
    LOG("ready (%s)", argv[1]);
    while (!done && wl_display_dispatch(d) != -1) {}
    return 0;
}
