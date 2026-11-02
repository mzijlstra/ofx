#!/bin/sh
# A second private headless compositor for GUI tests: sway on wlroots' headless backend. weston
# (tools/gui/weston.sh) stays the main one; sway is here for what weston lacks: ext-data-control
# (the system clipboard, gui/clipboard.c) and virtual keyboards (vkbd, tools/gui/vkbd.c: input through GLFW).
# Added 2026-10-28. Nothing it shows reaches Michael's screen, and nothing in it touches his
# Hyprland: its own Wayland socket and IPC socket, and -c a config written here, so ~/.config/sway
# is never read.
#
#   tools/gui/sway.sh start <scratch-dir>   prints the socket name; pid in <scratch-dir>/sway.pid
#   tools/gui/sway.sh stop <scratch-dir>
#
# The IPC socket (SWAYSOCK, for swaymsg) is in a folder of its own under /tmp, named in
# <scratch-dir>/sway.ipcdir: a socket's path has at most 107 bytes, and in a long scratch dir sway
# couldn't bind it and crashed (2026-10-29: the GUI mutants' clip and keys runs). The output is 936x516 logical
# at scale 2, the window's size in weston (sway draws no decorations, so the tiled window is the
# output), or OFX_SWAY_SIZE=WxH.
set -u
cmd=${1:?start|stop} T=${2:?scratch dir}
case $T in /?*) ;; *) echo "sway.sh: the scratch dir must be an absolute path" >&2; exit 1 ;; esac
mkdir -p "$T"
case $cmd in
start)
    rm -f "$T/sway.socket"
    ipc=$(mktemp -d /tmp/ofx-sway-XXXXXX) || exit 1
    echo "$ipc" > "$T/sway.ipcdir"
    size=${OFX_SWAY_SIZE:-936x516}
    # sway picks its own socket name (wayland-N, the first free): the config's exec, a child of
    # this sway, writes it down. No bar, no borders, no background or nag helpers, no XWayland; the
    # window fills the output as in weston's kiosk shell.
    cat > "$T/sway.config" <<EOS
output HEADLESS-1 resolution $size scale 2
default_border none
default_floating_border none
swaybg_command -
swaynag_command -
xwayland disable
focus_follows_mouse no
exec sh -c 'printf %s "\$WAYLAND_DISPLAY" > "$T/sway.socket.tmp" && mv "$T/sway.socket.tmp" "$T/sway.socket"'
EOS
    before=$(cd "$XDG_RUNTIME_DIR" && ls -d wayland-[0-9]* 2>/dev/null | grep -v '\.lock$')
    env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET -u DISPLAY -u SWAYSOCK -u I3SOCK -u HYPRLAND_INSTANCE_SIGNATURE \
        WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 SWAYSOCK="$ipc/ipc" XDG_CONFIG_HOME="$T/sway-xdg" \
        sway -c "$T/sway.config" >"$T/sway.log" 2>&1 &
    echo $! > "$T/sway.pid"
    for i in $(seq 100); do [ -s "$T/sway.socket" ] && break; sleep 0.1; done
    sock=$(cat "$T/sway.socket" 2>/dev/null)
    if [ -z "$sock" ] || [ ! -S "$XDG_RUNTIME_DIR/$sock" ]; then
        echo "sway.sh: no socket after 10 s; see $T/sway.log, which ends:" >&2
        tail -n 5 "$T/sway.log" >&2
        p=$(cat "$T/sway.pid")
        kill "$p" 2>/dev/null
        for i in $(seq 20); do kill -0 "$p" 2>/dev/null || break; sleep 0.1; done
        # A sway stopped while starting may have made its socket without writing its name down:
        # one that wasn't there before and whose lock no one holds is its (2026-10-28).
        for s in $(cd "$XDG_RUNTIME_DIR" && ls -d wayland-[0-9]* 2>/dev/null | grep -v '\.lock$'); do
            case " $(echo $before) " in *" $s "*) continue ;; esac
            [ "$s" != "${WAYLAND_DISPLAY:-}" ] && [ -e "$XDG_RUNTIME_DIR/$s.lock" ] && flock -n "$XDG_RUNTIME_DIR/$s.lock" true &&
                rm -f "$XDG_RUNTIME_DIR/$s" "$XDG_RUNTIME_DIR/$s.lock"
        done
        rm -rf "$ipc"; rm -f "$T/sway.pid" "$T/sway.ipcdir"; exit 1
    fi
    echo "$sock"
    ;;
stop)
    # Only that pid if it is still a sway: a stale file could name some other process by now.
    p=$(cat "$T/sway.pid" 2>/dev/null) sock=$(cat "$T/sway.socket" 2>/dev/null)
    [ -n "$p" ] && [ "$(cat "/proc/$p/comm" 2>/dev/null)" = sway ] && kill "$p" 2>/dev/null
    # A sway killed hard (a run stopped by SIGKILL) leaves its socket and lock behind: once it is
    # gone, they go too (2026-10-28: three were left; a review). Only a wayland-N it wrote down,
    # and never this session's display.
    for i in $(seq 20); do [ -n "$p" ] && kill -0 "$p" 2>/dev/null || break; sleep 0.1; done
    case $sock in
    # Stale only when no compositor holds its lock, libwayland's own rule: a live one (Michael's
    # Hyprland included) always does, whatever sway.socket or WAYLAND_DISPLAY say.
    wayland-[0-9]*) l="$XDG_RUNTIME_DIR/$sock.lock"
                    if [ "$sock" != "${WAYLAND_DISPLAY:-}" ] && [ -e "$l" ] && flock -n "$l" true; then
                        rm -f "$XDG_RUNTIME_DIR/$sock" "$l"
                    fi ;;
    esac
    # Its IPC folder: only one of ours by name (a stale sway.ipcdir could name anything).
    d=$(cat "$T/sway.ipcdir" 2>/dev/null)
    case $d in /tmp/ofx-sway-[A-Za-z0-9][A-Za-z0-9][A-Za-z0-9][A-Za-z0-9][A-Za-z0-9][A-Za-z0-9]) rm -rf "$d" ;; esac   # (mktemp's names; ? would let a / through)
    rm -f "$T/sway.pid" "$T/sway.socket" "$T/sway.ipcdir"
    ;;
*) echo "usage: sway.sh start|stop <scratch-dir>" >&2; exit 2 ;;
esac
