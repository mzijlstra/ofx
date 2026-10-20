#!/bin/sh
# A Hyprland headless output for GUI tests, made and removed safely (CLAUDE.md, "Testing the GUI").
#
#   tools/gui/output.sh create <NAME>    makes the output, then checks the workspace Hyprland gave it:
#                                        if any window is on it (a hidden workspace of Michael's;
#                                        2026-10-14 and 2026-10-20 it carried his Nemo), the output
#                                        goes again at once. Which workspace it gets can't be told
#                                        in advance: 6 (twice, holding Nemo), then a fresh 2.
#   tools/gui/output.sh remove <NAME>    refuses while a window is left on it (it would land on his
#                                        screen); then removes it and shows the dock script's log
set -u
cmd=${1:?create|remove} name=${2:?output name}
case $cmd in
create)
    hyprctl monitors -j | jq -e --arg n "$name" '.[] | select(.name == $n)' >/dev/null && { echo "output: $name exists already" >&2; exit 1; }
    hyprctl output create headless "$name" >/dev/null
    sleep 1
    # Whatever workspace it got, nothing may be on it: else it goes again at once.
    got=$(hyprctl monitors -j | jq -r --arg n "$name" '.[] | select(.name == $n) | .activeWorkspace.id')
    on=$(hyprctl clients -j | jq --argjson w "${got:-0}" '[.[] | select(.workspace.id == $w)] | length')
    if [ -z "$got" ] || [ "$on" != 0 ]; then
        hyprctl output remove "$name" >/dev/null
        echo "output: $name got workspace ${got:-?} with $on window(s) on it: removed again" >&2
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
