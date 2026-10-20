#!/bin/sh
# A Hyprland headless output for GUI tests, made and removed safely (CLAUDE.md, "Testing the GUI").
#
#   tools/gui/output.sh create <NAME>    makes the output, then checks the workspace Hyprland gave it:
#                                        if any window is on it (a hidden workspace of Michael's;
#                                        2026-10-14 and 2026-10-20 it carried his Nemo), the output
#                                        goes again at once. Which workspace it gets can't be told
#                                        in advance: 6 (twice, holding Nemo), then a fresh 2. Since
#                                        2026-10-20 a Hyprland rule (hyprland.lua) gives FXGUI 21,
#                                        UMLGUI 22, PRDASHGUI 23, REVIEWER2TEST 24; any workspace
#                                        of 10 or lower (his number keys) is refused too.
#   tools/gui/output.sh remove <NAME>    refuses while a window is left on it (it would land on his
#                                        screen); then removes it and shows the dock script's log
set -u
cmd=${1:?create|remove} name=${2:?output name}
case $cmd in
create)
    hyprctl monitors -j | jq -e --arg n "$name" '.[] | select(.name == $n)' >/dev/null && { echo "output: $name exists already" >&2; exit 1; }
    hyprctl output create headless "$name" >/dev/null
    # Whatever workspace it got, nothing may be on it: else it goes again at once. Polled until
    # the monitor shows a workspace (up to 2 s), rather than guessing a wait.
    got=""
    for i in $(seq 20); do
        got=$(hyprctl monitors -j | jq -r --arg n "$name" '.[] | select(.name == $n) | .activeWorkspace.id // empty')
        [ -n "$got" ] && [ "$got" != -1 ] && break
        sleep 0.1
    done
    on=$(hyprctl clients -j | jq --argjson w "${got:-0}" '[.[] | select(.workspace.id == $w)] | length')
    # Workspaces 1-10 are Michael's number keys: a test output there would catch his Super+N.
    # ~/.config/hypr/hyprland.lua gives FXGUI, UMLGUI, PRDASHGUI and REVIEWER2TEST 21-24.
    if [ -z "$got" ] || [ "$on" != 0 ] || [ "$got" -le 10 ]; then
        hyprctl output remove "$name" >/dev/null
        echo "output: $name got workspace ${got:-?} with $on window(s) on it: removed again (it needs one above 10, empty)" >&2
        exit 1
    fi
    echo "output: $name on workspace $got"
    ;;
remove)
    mon=$(hyprctl monitors -j | jq -r --arg n "$name" '.[] | select(.name == $n) | .id')
    [ -n "$mon" ] || { echo "output: no $name" >&2; exit 1; }
    left=$(hyprctl clients -j | jq "[.[] | select(.monitor == $mon)] | length")
    [ "$left" = 0 ] || { echo "output: $left window(s) still on $name: not removing" >&2; exit 1; }
    hyprctl output remove "$name" >/dev/null
    sleep 1
    tail -1 "$HOME/.local/state/omarchy/monitor-hook.log"
    ;;
*) echo "usage: output.sh create|remove <NAME>" >&2; exit 2 ;;
esac
