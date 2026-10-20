#!/bin/sh
# Runs one scripted fx window (FX_GUI_SCRIPT, gui/script.c) in a private headless weston
# (tools/gui/weston.sh), started for the run and stopped after it. Nothing reaches Michael's
# screen or his Hyprland: the window's WAYLAND_DISPLAY is weston's socket, checked before launch.
#
#   tools/gui/run-script.sh <scratch-dir> <script> <binary> <args...>
#
# A script of "-" runs the window unscripted, waiting for events as for a user; the caller ends it
# with kill (pid in <scratch-dir>/run.pid) and then `tools/gui/weston.sh stop <scratch-dir>`.
#
# Everything the window gets is set here:
# - fakes for uwsm-app, xdg-open and xdg-terminal-exec first on PATH, logging their arguments to
#   <scratch-dir>/spawn.log: a test never opens a real file or terminal on Michael's screen;
# - XDG_STATE_HOME=<scratch-dir>/state, with a copy of Michael's current theme files (colors.toml,
#   icons.theme) unless the caller made <scratch-dir>/state already: a theme test edits the copy;
# - XDG_CONFIG_HOME=<scratch-dir>/config: the sidebar's GTK bookmarks and the XDG user dirs come
#   from there (empty unless the caller wrote them), never from Michael's;
# - FX_GUI_SHOTS=<scratch-dir>/shots for the script's shots, FX_GUI_ROOT=<scratch-dir> for its
#   sh guard.
# Prints the script's output, stderr's first lines, and whether LeakSanitizer or ASan reported
# anything. Exit status: 1 when an expectation failed, an ASan report appeared, or the window had
# to be killed.
set -u
T=${1:?scratch dir} sc=${2:?script} bin=${3:?binary}
case $T in /?*) ;; *) echo "run-script: the scratch dir must be an absolute path: '$T'" >&2; exit 1 ;; esac
shift 3
mkdir -p "$T/shots" "$T/fakes" "$T/config"
rm -f "$T/run.pid" "$T/out" "$T/err" "$T"/asan.*
# The window starts in the compositor's directory, so a relative script path would not be found
# there: resolve it here, and refuse a missing one.
if [ "$sc" != - ]; then
    [ -f "$sc" ] || { echo "run-script: no script $sc" >&2; exit 1; }
    sc=$(cd "$(dirname "$sc")" && pwd)/$(basename "$sc")
fi
for f in uwsm-app xdg-open xdg-terminal-exec; do
    printf '#!/bin/sh\necho "%s $*" >> "%s/spawn.log"\n' "$f" "$T" > "$T/fakes/$f"
    chmod +x "$T/fakes/$f"
done
if [ ! -d "$T/state" ]; then
    mkdir -p "$T/state/omarchy/current/theme"
    real=${XDG_STATE_HOME:-$HOME/.local/state}/omarchy/current/theme
    cp "$real/colors.toml" "$real/icons.theme" "$T/state/omarchy/current/theme/" 2>/dev/null
fi
args=""
script_env="FX_GUI_SCRIPT=$sc"
[ "$sc" = - ] && script_env=""
for a in "$@"; do args="$args '$a'"; done
root=$(cd "$(dirname "$0")/../.." && pwd)
here=$(cd "$(dirname "$0")" && pwd)
sock=$("$here/weston.sh" start "$T") || exit 1
# Never Michael's display: the socket must be the one weston just made, and not his.
case $sock in fx-test-*) ;; *) echo "run-script: unexpected socket '$sock'" >&2; "$here/weston.sh" stop "$T"; exit 1 ;; esac
[ "$sock" != "${WAYLAND_DISPLAY:-}" ] || { echo "run-script: weston's socket is this session's display" >&2; "$here/weston.sh" stop "$T"; exit 1; }
cat > "$T/launch.sh" <<EOS
#!/bin/sh
echo \$\$ > "$T/run.pid"
exec env -u DISPLAY -u WAYLAND_SOCKET WAYLAND_DISPLAY="$sock" FX_TEST_SIZE=936x490 ${script_env} FX_GUI_ROOT="$T" FX_GUI_SHOTS="$T/shots" PATH="$T/fakes:\$PATH" XDG_STATE_HOME="$T/state" XDG_CONFIG_HOME="$T/config" \
  LSAN_OPTIONS=suppressions=$root/gui/lsan.supp ASAN_OPTIONS=log_path="$T/asan" ${FX_TEST_LEAK:+FX_TEST_LEAK=1} \
  ${FX_TEST_WATCH_DELAY_MS:+FX_TEST_WATCH_DELAY_MS=$FX_TEST_WATCH_DELAY_MS} \
  "$bin" $args >"$T/out" 2>"$T/err"
EOS
chmod +x "$T/launch.sh"
setsid "$T/launch.sh" </dev/null >/dev/null 2>&1 &
for i in $(seq 50); do [ -s "$T/run.pid" ] && break; sleep 0.2; done
pid=$(cat "$T/run.pid")
bad=0
limit=${RUN_SCRIPT_LIMIT:-120}
if [ "$sc" != - ]; then
    for i in $(seq $((limit * 5))); do kill -0 "$pid" 2>/dev/null || break; sleep 0.2; done
    if kill -0 "$pid" 2>/dev/null; then echo "still running after $limit s, killing $pid"; kill "$pid"; sleep 1; bad=1; fi
    "$here/weston.sh" stop "$T"
    grep -v "GLFW: Error: 65548" "$T/out" | grep -v "^$"
    grep -v "GLFW: Error: 65548" "$T/err" | head -20
    grep -q "expectations failed" "$T/out" && ! grep -q "script: 0 of" "$T/out" && bad=1
    grep -q "^script:[0-9]" "$T/out" && bad=1
    if ! grep -q "^script: " "$T/out"; then echo "run-script: the script never finished"; bad=1; fi
    if grep -l "ERROR\|leak of" "$T"/asan.* 2>/dev/null; then echo "ASAN REPORTS ABOVE"; bad=1; else echo "no asan reports"; fi
fi
exit $bad
