// The GUI scripts' pointer in the test sway (F14, 2026-11-05), as vkbd is their keyboard: a
// virtual pointer (zwlr_virtual_pointer_v1), so the seat has one at all (the headless sway has no
// devices), moved by its arguments:
//   vptr move <x> <y> | press | release | wait <ms> ...
// x and y are the output's logical pixels (936x516), 100 ms between steps. Refused unless
// WAYLAND_DISPLAY is the test sway's, served by it (testsway.h): anywhere else it would move
// Michael's pointer.

#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"
#include "testsway.h"

static struct wl_seat *seat;
static struct zwlr_virtual_pointer_manager_v1 *mgr;

static void on_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version)
{
    (void)data, (void)version;
    if (strcmp(iface, wl_seat_interface.name) == 0 && !seat) seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
    if (strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name) == 0 && !mgr)
        mgr = wl_registry_bind(r, name, &zwlr_virtual_pointer_manager_v1_interface, 1);
}
static void on_global_remove(void *data, struct wl_registry *r, uint32_t name) { (void)data, (void)r, (void)name; }
static const struct wl_registry_listener registry_listener = { on_global, on_global_remove };

static void pause_ms(int n)
{
    struct timespec ts = { n / 1000, (long)(n % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static uint32_t now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

int main(int argc, char **argv)
{
    struct wl_display *dpy = test_sway_connect("vptr");
    if (!dpy) return 1;
    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(dpy);
    if (!seat || !mgr) return fprintf(stderr, "vptr: no seat or virtual pointer manager\n"), 1;
    struct zwlr_virtual_pointer_v1 *p = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(mgr, seat);
    wl_display_roundtrip(dpy);
    pause_ms(300);   // (the seat's new pointer reaches the clients first)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "move") == 0 && i + 2 < argc) {
            zwlr_virtual_pointer_v1_motion_absolute(p, now(), (uint32_t)atoi(argv[i + 1]), (uint32_t)atoi(argv[i + 2]), 936, 516);
            i += 2;
        } else if (strcmp(argv[i], "press") == 0 || strcmp(argv[i], "release") == 0) {
            zwlr_virtual_pointer_v1_button(p, now(), BTN_LEFT, strcmp(argv[i], "press") == 0 ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
        } else if (strcmp(argv[i], "wait") == 0 && i + 1 < argc) {
            pause_ms(atoi(argv[++i]));
            continue;
        } else {
            return fprintf(stderr, "vptr: what is %s?\n", argv[i]), 2;
        }
        zwlr_virtual_pointer_v1_frame(p);
        wl_display_roundtrip(dpy);
        pause_ms(100);
    }
    pause_ms(500);
    zwlr_virtual_pointer_v1_destroy(p);
    wl_display_roundtrip(dpy);
    return 0;
}
