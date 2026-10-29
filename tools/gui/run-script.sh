#!/bin/sh
# Runs one scripted ofx window (OFX_GUI_SCRIPT, gui/script.c) in a private headless weston
# (tools/gui/weston.sh, or tools/gui/sway.sh with OFX_COMPOSITOR=sway), started for the run and
# stopped after it. Nothing reaches Michael's
# screen or his Hyprland: the window's WAYLAND_DISPLAY is weston's socket, checked before launch.
#
#   tools/gui/run-script.sh <scratch-dir> <script> <binary> <args...>
#
# A script of "-" runs the window unscripted, waiting for events as for a user; the caller ends it
# with kill (pid in <scratch-dir>/run.pid) and then `tools/gui/weston.sh stop <scratch-dir>` (or sway.sh).
#
# Everything the window gets is set here:
# - fakes for uwsm-app, gio, xdg-open, xdg-terminal-exec and udisksctl first on PATH, logging their
#   arguments to <scratch-dir>/spawn.log: a test never opens a real file or terminal on Michael's
#   screen, nor unmounts one of his drives;
# - the window OFX_SCRIPT_SIZE big (WxH; 936x516 unless the caller says);
# - XDG_STATE_HOME=<scratch-dir>/state, with a copy of Michael's current theme files (colors.toml,
#   icons.theme) unless the caller made <scratch-dir>/state already: a theme test edits the copy;
# - XDG_CONFIG_HOME=<scratch-dir>/config: the sidebar's GTK bookmarks and the XDG user dirs come
#   from there (empty unless the caller wrote them), never from Michael's;
# - XDG_DATA_HOME=<scratch-dir>/data: the home trash is <scratch-dir>/data/Trash, never his;
# - XDG_CACHE_HOME=<scratch-dir>/cache: the previews' thumbnail cache (F6.4), never his
#   ~/.cache/thumbnails, which Nemo shares;
# - OFX_TEST_THUMB_HELD_MAX, when set, is passed on: the previews' texture bytes held (eviction);
# - OFX_TEST_SPACE_DELAY_MS, when set, is passed on: each drive space answer that much later;
# - OFX_TEST_MEDIA, when set, is passed on: mounts under it count as removable drives;
# - OFX_TEST_ICONS_MAX, when set, is passed on: an icon cache of that many slots (a full cache);
# - OFX_TEST_MOUNTINFO, when set, is passed on: the mount list read instead of /proc/self/mountinfo;
# - OFX_TEST_HOME, when set, becomes the window's HOME (a scratch home for the path buttons' test);
# - OFX_GUI_SHOTS=<scratch-dir>/shots for the script's shots, OFX_GUI_ROOT=<scratch-dir> for its
#   sh guard and ofx's own (fs/ops.c: no write outside it).
# Prints the script's output, stderr's first lines, and whether LeakSanitizer or ASan reported
# anything. Exit status: 1 when an expectation failed, an ASan report appeared, or the window had
# to be killed.
set -u
T=${1:?scratch dir} sc=${2:?script} bin=${3:?binary}
case $T in /?*) ;; *) echo "run-script: the scratch dir must be an absolute path: '$T'" >&2; exit 1 ;; esac
shift 3
mkdir -p "$T/shots" "$T/fakes" "$T/config" "$T/data" "$T/cache"
rm -f "$T/run.pid" "$T/out" "$T/err" "$T"/asan.*
# The window starts in the compositor's directory, so a relative script path would not be found
# there: resolve it here, and refuse a missing one.
if [ "$sc" != - ]; then
    [ -f "$sc" ] || { echo "run-script: no script $sc" >&2; exit 1; }
    sc=$(cd "$(dirname "$sc")" && pwd)/$(basename "$sc")
fi
# Each also writes its words one by one, in brackets, to spawn-args.log: spawn.log joins them with
# spaces, so a word with a space in it can't be told from two (reviewer-2, 2026-10-29).
for f in uwsm-app gio xdg-open xdg-terminal-exec; do   # xdg-open: ofx's opener before gio
    cat > "$T/fakes/$f" <<EOS
#!/bin/sh
echo "$f \$*" >> "$T/spawn.log"
{ printf '%s' "$f"; for a do printf ' [%s]' "\$a"; done; echo; } >> "$T/spawn-args.log"
EOS
    chmod +x "$T/fakes/$f"
done
# gio info (the Properties panel's MIME type) only reads: the real gio answers it, unlogged.
real_gio=$(command -v gio)
# gio mime (Set as default, F7.3.1) logs and runs the real one too: XDG_CONFIG_HOME is <scratch>/config,
# so it writes the scratch mimeapps.list only.
printf '#!/bin/sh\nif [ "$1" = info ]; then exec %s "$@"; fi\necho "gio $*" >> "%s/spawn.log"\nif [ "$1" = mime ]; then exec %s "$@"; fi\n' "$real_gio" "$T" "$real_gio" > "$T/fakes/gio"
# udisksctl (eject, F6.1): logs too, and fails the step named in <scratch-dir>/udisks-fail.
cat > "$T/fakes/udisksctl" <<EOS
#!/bin/sh
echo "udisksctl \$*" >> "$T/spawn.log"
if [ "\$1" = "\$(cat "$T/udisks-fail" 2>/dev/null)" ]; then echo "Error \$1: target is busy" >&2; exit 1; fi
EOS
chmod +x "$T/fakes/udisksctl"
# vkbd (tools/gui/vkbd.c), the scripts' keyboard in the test sway: `make build/vkbd` first.
root=$(cd "$(dirname "$0")/../.." && pwd)
if [ "${OFX_COMPOSITOR:-weston}" = sway ]; then
    [ -x "$root/build/vkbd" ] || { echo "run-script: no $root/build/vkbd (make build/vkbd)" >&2; exit 1; }
    ln -sf "$root/build/vkbd" "$T/fakes/vkbd"
fi
if [ ! -d "$T/state" ]; then
    mkdir -p "$T/state/omarchy/current/theme"
    real=${XDG_STATE_HOME:-$HOME/.local/state}/omarchy/current/theme
    # (a missing theme said up front: the window then has the default colours and no icons, and
    # scripts that expect the theme's fail far from the cause; F8 review, 2026-10-27)
    cp "$real/colors.toml" "$real/icons.theme" "$T/state/omarchy/current/theme/" 2>/dev/null ||
        echo "run-script: no theme in $real (colors.toml, icons.theme): the defaults instead" >&2
fi
args=""
script_env="OFX_GUI_SCRIPT=$sc"
[ "$sc" = - ] && script_env=""
for a in "$@"; do args="$args '$a'"; done
here=$(cd "$(dirname "$0")" && pwd)
# Another window size: weston's output that much bigger, as 944x544 gives 936x516.
if [ -n "${OFX_SCRIPT_SIZE:-}" ]; then
    case $OFX_SCRIPT_SIZE in
    *x*x* | x* | *x | *[!0-9x]*) echo "run-script: OFX_SCRIPT_SIZE is WxH, not '$OFX_SCRIPT_SIZE'" >&2; exit 2 ;;
    *x*) ;;
    *) echo "run-script: OFX_SCRIPT_SIZE is WxH, not '$OFX_SCRIPT_SIZE'" >&2; exit 2 ;;
    esac
    sw=${OFX_SCRIPT_SIZE%x*}; sh_=${OFX_SCRIPT_SIZE#*x}
    export OFX_WESTON_SIZE="$((sw + 8))x$((sh_ + 28))" OFX_SWAY_SIZE="$OFX_SCRIPT_SIZE"
fi
# OFX_COMPOSITOR=sway: the test sway (tools/gui/sway.sh) instead, for the system clipboard and
# typed input (2026-10-28); weston otherwise.
comp=${OFX_COMPOSITOR:-weston}
case $comp in weston | sway) ;; *) echo "run-script: OFX_COMPOSITOR is weston or sway, not '$comp'" >&2; exit 2 ;; esac
sock=$("$here/$comp.sh" start "$T") || exit 1
# Never Michael's display: the socket must be the one the compositor just made, and not his.
case $comp:$sock in weston:ofx-test-* | sway:wayland-[0-9]*) ;; *) echo "run-script: unexpected socket '$sock'" >&2; "$here/$comp.sh" stop "$T"; exit 1 ;; esac
[ "$sock" != "${WAYLAND_DISPLAY:-}" ] || { echo "run-script: $comp's socket is this session's display" >&2; "$here/$comp.sh" stop "$T"; exit 1; }
cat > "$T/launch.sh" <<EOS
#!/bin/sh
echo \$\$ > "$T/run.pid"
exec env -u DISPLAY -u WAYLAND_SOCKET WAYLAND_DISPLAY="$sock" OFX_TEST_SIZE=${OFX_SCRIPT_SIZE:-936x516} OFX_TEST_WINDOWS="$T/windows" ${script_env} OFX_GUI_ROOT="$T" OFX_GUI_SHOTS="$T/shots" PATH="$T/fakes:\$PATH" XDG_STATE_HOME="$T/state" XDG_CONFIG_HOME="$T/config" XDG_DATA_HOME="$T/data" XDG_CACHE_HOME="$T/cache" \
  LSAN_OPTIONS=suppressions=$root/gui/lsan.supp ASAN_OPTIONS=log_path="$T/asan" ${OFX_TEST_LEAK:+OFX_TEST_LEAK=1} ${OFX_GUI_FAIL_FAST:+OFX_GUI_FAIL_FAST=1} ${OFX_GUI_DEBUG:+OFX_GUI_DEBUG=1} \
  ${OFX_TEST_WATCH_DELAY_MS:+OFX_TEST_WATCH_DELAY_MS=$OFX_TEST_WATCH_DELAY_MS} ${OFX_TEST_COPY_DELAY_MS:+OFX_TEST_COPY_DELAY_MS=$OFX_TEST_COPY_DELAY_MS} ${OFX_TEST_CROSS:+OFX_TEST_CROSS=1} ${OFX_TEST_HOME:+HOME=$OFX_TEST_HOME} ${OFX_TEST_COUNT_DELAY_MS:+OFX_TEST_COUNT_DELAY_MS=$OFX_TEST_COUNT_DELAY_MS} ${OFX_TEST_MOUNTINFO:+OFX_TEST_MOUNTINFO=$OFX_TEST_MOUNTINFO} ${OFX_TEST_MEDIA:+OFX_TEST_MEDIA=$OFX_TEST_MEDIA} ${OFX_TEST_SPACE_DELAY_MS:+OFX_TEST_SPACE_DELAY_MS=$OFX_TEST_SPACE_DELAY_MS} ${OFX_TEST_ICONS_MAX:+OFX_TEST_ICONS_MAX=$OFX_TEST_ICONS_MAX} ${OFX_TEST_THUMB_HELD_MAX:+OFX_TEST_THUMB_HELD_MAX=$OFX_TEST_THUMB_HELD_MAX} \
  "$bin" $args >"$T/out" 2>"$T/err"
EOS
chmod +x "$T/launch.sh"
t_launch=$(date +%s.%N)
echo "run-script: the window launched"   # (tools/mutation/gui.py: a failure before this line tested nothing)
setsid "$T/launch.sh" </dev/null >/dev/null 2>&1 &
for i in $(seq 50); do [ -s "$T/run.pid" ] && break; sleep 0.2; done
pid=$(cat "$T/run.pid")
bad=0
limit=${RUN_SCRIPT_LIMIT:-120}
if [ "$sc" != - ]; then
    for i in $(seq $((limit * 5))); do kill -0 "$pid" 2>/dev/null || break; sleep 0.2; done
    if kill -0 "$pid" 2>/dev/null; then echo "still running after $limit s, killing $pid"; kill "$pid"; sleep 1; bad=1; fi
    # A window stuck on a lock never runs its SIGTERM exit: it outlived the run (clip.script on the
    # unfixed clip_get, 2026-10-28).
    if kill -0 "$pid" 2>/dev/null; then echo "$pid ignored SIGTERM, SIGKILL"; kill -9 "$pid"; sleep 0.2; fi
    t_gone=$(date +%s.%N)
    "$here/$comp.sh" stop "$T"
    # Where a slow run's time went (2026-10-26, f6b's quit late under load): seconds from the
    # launch to the script's end (its out file last written) and to the window's process gone, and
    # weston's stop after it. run.sh's timed groups print it when they fail.
    awk -v l="$t_launch" -v e="$(stat -c %.9Y "$T/out" 2>/dev/null || echo 0)" -v g="$t_gone" -v s="$(date +%s.%N)" \
        'BEGIN { printf "times: script ended at %.1f s, window gone at %.1f s, weston stopped at %.1f s\n", e - l, g - l, s - l }' > "$T/times"
    grep -v "GLFW: Error: 65548" "$T/out" | grep -v "^$"
    grep -v "GLFW: Error: 65548" "$T/err" | head -20
    grep -q "expectations failed" "$T/out" && ! grep -q "script: 0 of" "$T/out" && bad=1
    grep -q "^script:[0-9]" "$T/out" && bad=1
    if ! grep -q "^script: " "$T/out"; then echo "run-script: the script never finished"; bad=1; fi
    if grep -l "ERROR\|leak of" "$T"/asan.* 2>/dev/null; then echo "ASAN REPORTS ABOVE"; bad=1; else echo "no asan reports"; fi
fi
exit $bad
