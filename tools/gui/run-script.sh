#!/bin/sh
# Runs one scripted fx window (FX_GUI_SCRIPT, gui/script.c) on a Hyprland headless output, the flow
# CLAUDE.md allows: the caller has made the output (hyprctl output create headless <NAME>) and
# removes it afterwards. The pid comes from the launch script, never from a class lookup.
#
#   tools/gui/run-script.sh <output> <scratch-dir> <script> <binary> <args...>
#
# A script of "-" runs the window unscripted, waiting for events as for a user; the caller ends it
# with kill (pid in <scratch-dir>/run.pid).
#
# The window starts from Hyprland's environment, not this shell's, so everything it gets is set
# here:
# - fakes for uwsm-app, xdg-open and xdg-terminal-exec first on PATH, logging their arguments to
#   <scratch-dir>/spawn.log: a test never opens a real file or terminal on Michael's screen;
# - XDG_STATE_HOME=<scratch-dir>/state, with a copy of Michael's current theme files (colors.toml,
#   icons.theme) unless the caller made <scratch-dir>/state already: a theme test edits the copy;
# - FX_GUI_SHOTS=<scratch-dir>/shots for the script's shots.
# Prints the script's output, stderr's first lines, whether LeakSanitizer or ASan reported
# anything, and how many windows are left on the output. Exit status: 1 when an expectation
# failed, an ASan report appeared, or the window had to be killed.
set -u
out=$1 T=$2 sc=$3 bin=$4
case $T in /?*) ;; *) echo "run-script: the scratch dir must be an absolute path: '$T'" >&2; exit 1 ;; esac
shift 4
mkdir -p "$T/shots" "$T/fakes"
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
cat > "$T/launch.sh" <<EOS
#!/bin/sh
echo \$\$ > "$T/run.pid"
exec env ${script_env} FX_GUI_ROOT="$T" FX_GUI_SHOTS="$T/shots" PATH="$T/fakes:\$PATH" XDG_STATE_HOME="$T/state" \
  LSAN_OPTIONS=suppressions=$root/gui/lsan.supp ASAN_OPTIONS=log_path="$T/asan" ${FX_TEST_LEAK:+FX_TEST_LEAK=1} \
  "$bin" $args >"$T/out" 2>"$T/err"
EOS
chmod +x "$T/launch.sh"
ws=$(hyprctl monitors -j | jq -r ".[] | select(.name==\"$out\") | .activeWorkspace.name")
[ -n "$ws" ] || { echo "no output $out"; exit 1; }
# The output's workspace must hold nothing of Michael's: Hyprland gives a new output the next free
# workspace, which may be one of his with windows on it (2026-10-14, uml-viewer). Refuse.
# Every window counts, fx included: the run's own doesn't exist yet, and an fx window there is
# Michael's.
theirs=$(hyprctl clients -j | jq -r "[.[] | select(.workspace.name==\"$ws\")] | length")
if [ "$theirs" != 0 ]; then
    echo "run-script: workspace $ws on $out holds $theirs window(s): not running" >&2
    exit 1
fi
hyprctl dispatch "hl.dsp.exec_cmd(\"$T/launch.sh\", { workspace = \"$ws silent\" })" >/dev/null
for i in $(seq 50); do [ -s "$T/run.pid" ] && break; sleep 0.2; done
pid=$(cat "$T/run.pid")
bad=0
limit=${RUN_SCRIPT_LIMIT:-120}
if [ "$sc" != - ]; then
    for i in $(seq $((limit * 5))); do kill -0 "$pid" 2>/dev/null || break; sleep 0.2; done
    if kill -0 "$pid" 2>/dev/null; then echo "still running after $limit s, killing $pid"; kill "$pid"; sleep 1; bad=1; fi
    mon=$(hyprctl monitors -j | jq -r ".[] | select(.name==\"$out\") | .id")
    echo "windows left on $out: $(hyprctl clients -j | jq "[.[] | select(.monitor==$mon)] | length")"
    grep -v "GLFW: Error: 65548" "$T/out" | grep -v "^$"
    grep -v "GLFW: Error: 65548" "$T/err" | head -20
    grep -q "expectations failed" "$T/out" && ! grep -q "script: 0 of" "$T/out" && bad=1
    grep -q "^script:[0-9]" "$T/out" && bad=1
    if ! grep -q "^script: " "$T/out"; then echo "run-script: the script never finished"; bad=1; fi
    if grep -l "ERROR\|leak of" "$T"/asan.* 2>/dev/null; then echo "ASAN REPORTS ABOVE"; bad=1; else echo "no asan reports"; fi
fi
exit $bad
