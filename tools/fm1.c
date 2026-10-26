// ofx-fm1: org.freedesktop.FileManager1 on the session bus (Michael, 2026-10-26), so a browser's
// "Show in folder" opens ofx, not Nemo. The bus starts it on the first call (its service file,
// tools/dbus/), it starts `uwsm-app -- ofx <path>` for each window the call asks for (fs/fm1.c),
// and it exits after FM1_IDLE_S without a call, or when the bus goes. No window, no raylib.
//
// OFX_FM1_OFX names the ofx to start (the tests' fake); else the ofx beside this binary.
#include "../base/base.c"
#include "../fs/fs.c"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

enum { FM1_IDLE_S = 30, FM1_URIS_MAX = 256 };

static char    ofx_exe[PATH_MAX];
static Fm1Plan plan;   // 16 paths: too big for the stack of a callback

// One call's URIs (an array of strings, then the startup id, unused: ofx's window takes the focus
// as any new window does) to windows.
static int fm1_call(sd_bus_message *m, Fm1Call c)
{
    const char *uris[FM1_URIS_MAX];
    isize n = 0, over = 0;
    int r = sd_bus_message_enter_container(m, 'a', "s");
    if (r < 0) return r;
    const char *u;
    while ((r = sd_bus_message_read(m, "s", &u)) > 0)
        if (n < FM1_URIS_MAX) uris[n++] = u;
        else over++;
    if (r < 0 || (r = sd_bus_message_exit_container(m)) < 0) return r;
    const char *startup;
    if ((r = sd_bus_message_read(m, "s", &startup)) < 0) return r;
    fm1_plan(c, uris, n, &plan);
    for (isize i = 0; i < plan.n; i++) {
        Err err = { 0 };
        bool ok = c == FM1_FOLDERS ? spawn_window(ofx_exe, plan.path[i], &err) : spawn_window_select(ofx_exe, plan.path[i], &err);
        if (!ok) fprintf(stderr, "ofx-fm1: %s\n", err.msg);
    }
    if (plan.skipped + over) fprintf(stderr, "ofx-fm1: %td of the URIs skipped (not a local file there, or too many)\n", plan.skipped + over);
    return sd_bus_reply_method_return(m, "");
}

static int show_folders(sd_bus_message *m, void *ud, sd_bus_error *e) { (void)ud, (void)e; return fm1_call(m, FM1_FOLDERS); }
static int show_items(sd_bus_message *m, void *ud, sd_bus_error *e) { (void)ud, (void)e; return fm1_call(m, FM1_ITEMS); }
static int show_props(sd_bus_message *m, void *ud, sd_bus_error *e) { (void)ud, (void)e; return fm1_call(m, FM1_PROPS); }

static const sd_bus_vtable fm1_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ShowFolders", "ass", "", show_folders, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ShowItems", "ass", "", show_items, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ShowItemProperties", "ass", "", show_props, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) return fprintf(stderr, "usage: ofx-fm1 (started by the session bus)\n"), 2;
    const char *exe = getenv("OFX_FM1_OFX");
    if (exe && exe[0]) snprintf(ofx_exe, sizeof ofx_exe, "%s", exe);
    else {
        ssize_t k = readlink("/proc/self/exe", ofx_exe, sizeof ofx_exe - 8);
        char *slash = k > 0 ? (ofx_exe[k] = 0, strrchr(ofx_exe, '/')) : NULL;
        if (!slash) return fprintf(stderr, "ofx-fm1: can't find its own folder\n"), 1;
        strcpy(slash + 1, "ofx");
    }
    if (chdir("/") != 0) fprintf(stderr, "ofx-fm1: chdir /: %s\n", strerror(errno));   // keep no drive busy
    spawn_init();
    sd_bus *bus = NULL;
    sd_bus_slot *slot = NULL;
    int r = sd_bus_open(&bus);   // the bus that started it (DBUS_STARTER_ADDRESS), else the session's
    if (r >= 0) r = sd_bus_add_object_vtable(bus, &slot, "/org/freedesktop/FileManager1", "org.freedesktop.FileManager1", fm1_vtable, NULL);
    if (r >= 0) r = sd_bus_request_name(bus, "org.freedesktop.FileManager1", 0);   // taken (Nemo has it): exit
    if (r < 0) {
        fprintf(stderr, "ofx-fm1: %s\n", strerror(-r));
        sd_bus_slot_unref(slot), sd_bus_flush_close_unref(bus);
        return 1;
    }
    for (;;) {
        r = sd_bus_process(bus, NULL);
        if (r < 0) break;   // the bus went
        if (r > 0) continue;
        r = sd_bus_wait(bus, (uint64_t)FM1_IDLE_S * 1000000);
        if (r < 0) break;   // the bus went
        if (r == 0) {
            // Idle: the name goes first, so a call from now on starts another ofx-fm1; then the
            // calls that came before it are answered.
            sd_bus_release_name(bus, "org.freedesktop.FileManager1");
            while (sd_bus_process(bus, NULL) > 0) {}
            break;
        }
    }
    sd_bus_slot_unref(slot);
    sd_bus_flush_close_unref(bus);
    return 0;
}
