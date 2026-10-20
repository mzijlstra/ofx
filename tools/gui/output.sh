#!/bin/sh
# A Hyprland headless output for GUI tests, made and removed safely (CLAUDE.md, "Testing the GUI").
#
#   tools/gui/output.sh create <NAME>    refuses while any workspace that no monitor shows holds a
#                                        window: a new output takes the next such workspace, and on
#                                        2026-10-14 and 2026-10-20 that carried Michael's window
#                                        (Nemo) onto the invisible test output
#                                        (OUTPUT_CHECK_ONLY=1: print the check, create nothing)
#   tools/gui/output.sh remove <NAME>    refuses while a window is left on it (it would land on his
#                                        screen); then removes it and shows the dock script's log
set -u
cmd=${1:?create|remove} name=${2:?output name}
case $cmd in
create)
    hyprctl monitors -j | jq -e --arg n "$name" '.[] | select(.name == $n)' >/dev/null && { echo "output: $name exists already" >&2; exit 1; }
    # Fail closed: if either query fails, nothing is created.
    shown=$(hyprctl monitors -j | jq -er '[.[].activeWorkspace.id | tostring] | join(" ")') || { echo "output: cannot read the monitors: not creating" >&2; exit 1; }
    hidden=$(hyprctl workspaces -j | jq -er --arg shown " $shown " \
        '[.[] | select(.windows > 0) | .id as $id | select(($shown | index(" \($id) ")) | not) | "\(.name) (\(.windows) window(s))"] | join("\n")') \
        || { echo "output: cannot read the workspaces: not creating" >&2; exit 1; }
    [ "${OUTPUT_CHECK_ONLY:-}" = 1 ] && { echo "check: hidden workspaces with windows: ${hidden:-none}"; exit 0; }
    if [ -n "$hidden" ]; then
        echo "output: not creating $name: these workspaces are off screen and hold windows, and the new output would take one:" >&2
        echo "$hidden" | sed 's/^/  workspace /' >&2
        exit 1
    fi
    hyprctl output create headless "$name" >/dev/null
    sleep 1
    hyprctl monitors -j | jq -r --arg n "$name" '.[] | select(.name == $n) | "output: \(.name) on workspace \(.activeWorkspace.name), scale \(.scale)"'
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
