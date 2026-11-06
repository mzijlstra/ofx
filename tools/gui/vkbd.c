// vkbd <chord>... : keys typed into the test sway (tools/gui/sway.sh) through a virtual keyboard,
// for the GUI scripts' `sh` lines after `user-keys` (2026-10-28). wtype sends its own keymap with
// keycodes it makes up, and GLFW names keys by keycode (its evdev table), so wtype's 'b' reached ofx
// as Escape. vkbd sends a plain US keymap and the real evdev keycodes, as a keyboard would.
//
//   vkbd ctrl+shift+f ctrl+return left text:a.b-c
//
// A chord is modifiers (ctrl, shift, alt, super) and one key, joined by '+'; text:<chars> types
// each character (ASCII letters, digits and . - _ / space). It runs only against the test sway: its
// WAYLAND_DISPLAY must be the one run-script.sh's sway wrote to $OFX_GUI_ROOT/sway.socket, from a
// process named sway (its pid in $OFX_GUI_ROOT/sway.pid). Anywhere else it would type into
// Michael's focused window.
#include <linux/input-event-codes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "testsway.h"

static struct wl_seat *seat;
static struct zwp_virtual_keyboard_manager_v1 *mgr;

static void on_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version)
{
    (void)data, (void)version;
    if (strcmp(iface, wl_seat_interface.name) == 0 && !seat) seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
    if (strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name) == 0 && !mgr)
        mgr = wl_registry_bind(r, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
}

static void on_global_remove(void *data, struct wl_registry *r, uint32_t name)
{
    (void)data, (void)r, (void)name;
}

static const struct wl_registry_listener registry_listener = { .global = on_global, .global_remove = on_global_remove };

static const struct { const char *name; int code; } KEYS[] = {
    { "return", KEY_ENTER }, { "kp_enter", KEY_KPENTER }, { "escape", KEY_ESC }, { "tab", KEY_TAB },
    { "backspace", KEY_BACKSPACE }, { "delete", KEY_DELETE }, { "space", KEY_SPACE },
    { "left", KEY_LEFT }, { "right", KEY_RIGHT }, { "up", KEY_UP }, { "down", KEY_DOWN },
    { "home", KEY_HOME }, { "end", KEY_END }, { "pageup", KEY_PAGEUP }, { "pagedown", KEY_PAGEDOWN },
    { "f1", KEY_F1 }, { "f2", KEY_F2 }, { "f9", KEY_F9 }, { "f10", KEY_F10 },
    { "minus", KEY_MINUS }, { "equal", KEY_EQUAL }, { "period", KEY_DOT }, { "slash", KEY_SLASH },
};

static const int LETTERS[26] = {
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
};
static const int DIGITS[10] = { KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };

// A key's evdev code by name: a letter, a digit, or one of KEYS; -1 if none.
static int key_code(const char *s)
{
    if (s[0] && !s[1] && s[0] >= 'a' && s[0] <= 'z') return LETTERS[s[0] - 'a'];
    if (s[0] && !s[1] && s[0] >= '0' && s[0] <= '9') return DIGITS[s[0] - '0'];
    for (size_t i = 0; i < sizeof KEYS / sizeof *KEYS; i++)
        if (strcmp(s, KEYS[i].name) == 0) return KEYS[i].code;
    return -1;
}

// A character's key on the US layout, and whether Shift goes with it; -1 if vkbd has none.
static int char_code(char c, bool *shift)
{
    *shift = false;
    if (c >= 'a' && c <= 'z') return LETTERS[c - 'a'];
    if (c >= 'A' && c <= 'Z') return *shift = true, LETTERS[c - 'A'];
    if (c >= '0' && c <= '9') return DIGITS[c - '0'];
    switch (c) {
    case '.': return KEY_DOT;
    case '-': return KEY_MINUS;
    case '_': return *shift = true, KEY_MINUS;
    case '/': return KEY_SLASH;
    case ' ': return KEY_SPACE;
    default: return -1;
    }
}

static struct zwp_virtual_keyboard_v1 *kb;
static struct xkb_state *xs;
static struct wl_display *dpy;
static uint32_t ms;

static void pause_ms(int n)
{
    nanosleep(&(struct timespec){ n / 1000, (long)(n % 1000) * 1000000L }, NULL);
}

// One key down or up, the modifiers as xkb counts them after it, sent and flushed.
static void key(int code, bool down)
{
    zwp_virtual_keyboard_v1_key(kb, ms += 10, (uint32_t)code, down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    xkb_state_update_key(xs, (xkb_keycode_t)code + 8, down ? XKB_KEY_DOWN : XKB_KEY_UP);
    zwp_virtual_keyboard_v1_modifiers(kb, xkb_state_serialize_mods(xs, XKB_STATE_MODS_DEPRESSED),
                                      xkb_state_serialize_mods(xs, XKB_STATE_MODS_LATCHED),
                                      xkb_state_serialize_mods(xs, XKB_STATE_MODS_LOCKED),
                                      xkb_state_serialize_layout(xs, XKB_STATE_LAYOUT_EFFECTIVE));
    wl_display_flush(dpy);
    pause_ms(10);
}

static void chord(const int *mods, int nmods, int code)
{
    for (int i = 0; i < nmods; i++) key(mods[i], true);
    key(code, true);
    key(code, false);
    for (int i = nmods - 1; i >= 0; i--) key(mods[i], false);
}

// A chord's key code, its modifiers' codes into mods (four at most) and their count into *n; -1 when
// it names a key or a modifier there isn't (said on stderr).
static int chord_parse(const char *arg, int mods[4], int *n)
{
    char buf[128];
    snprintf(buf, sizeof buf, "%s", arg);
    char *last = strrchr(buf, '+');
    int code = key_code(last ? last + 1 : buf);
    *n = 0;
    if (code < 0) return fprintf(stderr, "vkbd: no key '%s'\n", arg), -1;
    if (!last) return code;
    *last = 0;
    for (char *m = strtok(buf, "+"); m; m = strtok(NULL, "+")) {
        int k = !strcmp(m, "ctrl") ? KEY_LEFTCTRL : !strcmp(m, "shift") ? KEY_LEFTSHIFT : !strcmp(m, "alt") ? KEY_LEFTALT : !strcmp(m, "super") ? KEY_LEFTMETA : -1;
        if (*n == 4 || k < 0) return fprintf(stderr, "vkbd: no modifier '%s' (four at most)\n", m), -1;
        mods[(*n)++] = k;
    }
    return code;
}

int main(int argc, char **argv)
{
    if (argc < 2) return fprintf(stderr, "usage: vkbd <chord|text:chars>...\n"), 2;
    // Every argument checked before the first key goes out.
    for (int i = 1; i < argc; i++) {
        bool shift;
        if (strncmp(argv[i], "text:", 5) == 0) {
            for (const char *c = argv[i] + 5; *c; c++)
                if (char_code(*c, &shift) < 0) return fprintf(stderr, "vkbd: no key for '%c'\n", *c), 2;
            continue;
        }
        int mods[4], n;
        if (chord_parse(argv[i], mods, &n) < 0) return 2;
    }
    if (!(dpy = test_sway_connect("vkbd"))) return 1;
    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(dpy);
    if (!seat || !mgr) return fprintf(stderr, "vkbd: the compositor has no virtual keyboards\n"), 1;
    // The US layout named here, so the XKB_DEFAULT_* of whoever runs it don't count.
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_ENVIRONMENT_NAMES);
    struct xkb_rule_names names = { .rules = "evdev", .model = "pc105", .layout = "us" };
    struct xkb_keymap *km = ctx ? xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS) : NULL;
    if (!km) return fprintf(stderr, "vkbd: no US keymap\n"), 1;
    xs = xkb_state_new(km);
    char *text = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
    size_t size = strlen(text) + 1;
    int fd = memfd_create("vkbd-keymap", MFD_CLOEXEC);
    if (fd < 0 || write(fd, text, size) != (ssize_t)size) return fprintf(stderr, "vkbd: no keymap file\n"), 1;
    kb = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(mgr, seat);
    zwp_virtual_keyboard_v1_keymap(kb, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, (uint32_t)size);
    wl_display_roundtrip(dpy);
    // The window binds the seat's new keyboard and gets its focus first.
    pause_ms(150);
    for (int i = 1; i < argc; i++) {
        bool shift;
        int sh[1] = { KEY_LEFTSHIFT };
        if (strncmp(argv[i], "text:", 5) == 0) {
            for (const char *c = argv[i] + 5; *c; c++) {
                int code = char_code(*c, &shift);
                chord(sh, shift ? 1 : 0, code);
            }
            continue;
        }
        int mods[4], n, code = chord_parse(argv[i], mods, &n);
        chord(mods, n, code);
    }
    wl_display_roundtrip(dpy);
    zwp_virtual_keyboard_v1_destroy(kb);
    wl_display_roundtrip(dpy);
    free(text);
    close(fd);
    xkb_state_unref(xs);
    xkb_keymap_unref(km);
    xkb_context_unref(ctx);
    wl_display_disconnect(dpy);
    return 0;
}
