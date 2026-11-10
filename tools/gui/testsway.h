// The test sway's display, and only that (vkbd, vptr, dndpeer: 2026-10-28, F14): each would type,
// move the pointer or drop into whatever display it reaches, so anywhere else Michael's Hyprland.
// The display must be the one $OFX_GUI_ROOT/sway.socket names (run-script.sh writes it), its pid
// file must name a running sway, and the socket's other end, as the kernel sees it, must be that
// sway. Included by each tool.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <wayland-client.h>

// A reason to refuse, or NULL with *pid the test sway's.
static const char *test_sway_refused(int *pid)
{
    const char *root = getenv("OFX_GUI_ROOT"), *wd = getenv("WAYLAND_DISPLAY");
    if (!root || !root[0] || !wd || !wd[0]) return "OFX_GUI_ROOT and WAYLAND_DISPLAY must be set";
    // libwayland connects to an inherited WAYLAND_SOCKET before it reads WAYLAND_DISPLAY.
    if (getenv("WAYLAND_SOCKET")) return "WAYLAND_SOCKET is set: the display checked here wouldn't be the one used";
    char path[4096], sock[256] = "", comm[64] = "";
    *pid = 0;
    snprintf(path, sizeof path, "%s/sway.socket", root);
    FILE *f = fopen(path, "r");
    if (f) fscanf(f, "%255s", sock), fclose(f);
    // (run-script names it by its absolute path, the window's XDG_RUNTIME_DIR being the run's own:
    // its last part is the name; the peer's pid below proves the socket is the test sway's)
    const char *base = strrchr(wd, '/');
    if (strcmp(sock, base ? base + 1 : wd) != 0) return "WAYLAND_DISPLAY is not the test sway's ($OFX_GUI_ROOT/sway.socket)";
    snprintf(path, sizeof path, "%s/sway.pid", root);
    if ((f = fopen(path, "r"))) fscanf(f, "%d", pid), fclose(f);
    snprintf(path, sizeof path, "/proc/%d/comm", *pid);
    if (*pid <= 0 || !(f = fopen(path, "r"))) return "no test sway running ($OFX_GUI_ROOT/sway.pid)";
    fscanf(f, "%63s", comm), fclose(f);
    if (strcmp(comm, "sway") != 0) return "$OFX_GUI_ROOT/sway.pid is not a sway";
    return NULL;
}

// Connected to the test sway, or NULL with the reason on stderr after "<who>: ".
static struct wl_display *test_sway_connect(const char *who)
{
    int pid;
    const char *why = test_sway_refused(&pid);
    if (why) return fprintf(stderr, "%s: refused: %s\n", who, why), NULL;
    struct wl_display *d = wl_display_connect(NULL);
    if (!d) return fprintf(stderr, "%s: no display\n", who), NULL;
    struct ucred cr;
    socklen_t crlen = sizeof cr;
    if (getsockopt(wl_display_get_fd(d), SOL_SOCKET, SO_PEERCRED, &cr, &crlen) != 0 || cr.pid != pid) {
        wl_display_disconnect(d);
        return fprintf(stderr, "%s: refused: the display isn't served by the test sway (pid %d)\n", who, pid), NULL;
    }
    return d;
}
