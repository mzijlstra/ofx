#!/bin/sh
# A private headless Wayland compositor for GUI tests: weston's headless backend, on its own
# socket. Nothing it shows reaches Michael's screen, and nothing in it touches his Hyprland
# (workspaces, monitors, the dock script). Since 2026-10-20 it replaces Hyprland headless outputs
# for ofx: those took a workspace Hyprland picked, three times one holding his windows.
#
#   tools/gui/weston.sh start <scratch-dir>   prints the socket name; pid in <scratch-dir>/weston.pid
#   tools/gui/weston.sh stop <scratch-dir>
set -u
cmd=${1:?start|stop} T=${2:?scratch dir}
case $T in /?*) ;; *) echo "weston.sh: the scratch dir must be an absolute path" >&2; exit 1 ;; esac
mkdir -p "$T"
case $cmd in
start)
    sock="ofx-test-$$"
    [ -e "$XDG_RUNTIME_DIR/$sock" ] && { echo "weston.sh: $sock exists" >&2; exit 1; }
    # --fake-seat: GLFW 3.4 reads the seat's version even when there is none, and crashes without
    # one. --no-config: not his weston.ini (if any). The kiosk shell fills the output with the window:
    # 936x516 logical at scale 2: 936x490 as a Hyprland headless output tiled it before, and the menu
    # bar's 26 (F7.1), so everything under it has the room the scripts were written for.
    # OFX_WESTON_SIZE=WxH another output (run-script's OFX_SCRIPT_SIZE, the window 8 and 28 smaller).
    size=${OFX_WESTON_SIZE:-944x544}
    env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET -u DISPLAY weston --backend=headless --fake-seat --shell=kiosk --renderer=gl --width="${size%x*}" --height="${size#*x}" --scale=2 \
        --socket="$sock" --idle-time=0 --no-config --log="$T/weston.log" >/dev/null 2>&1 &
    echo $! > "$T/weston.pid"
    for i in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/$sock" ] && break; sleep 0.1; done
    [ -S "$XDG_RUNTIME_DIR/$sock" ] || { echo "weston.sh: no socket after 5 s; see $T/weston.log" >&2; kill "$(cat "$T/weston.pid")" 2>/dev/null; exit 1; }
    echo "$sock" > "$T/weston.socket"
    echo "$sock"
    ;;
stop)
    # Only that pid if it is still a weston: a stale file could name some other process by now.
    p=$(cat "$T/weston.pid" 2>/dev/null)
    [ -n "$p" ] && [ "$(cat "/proc/$p/comm" 2>/dev/null)" = weston ] && kill "$p" 2>/dev/null
    rm -f "$T/weston.pid" "$T/weston.socket"
    ;;
*) echo "usage: weston.sh start|stop <scratch-dir>" >&2; exit 2 ;;
esac
