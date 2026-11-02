#!/bin/sh
# ofx-clean as the timer runs it, reading ofx.conf's clean options (F10 step 2): a scratch cache,
# config and mount list, the guard (OFX_GUI_ROOT) on the scratch folder. Nothing of Michael's: HOME,
# XDG_CACHE_HOME and XDG_CONFIG_HOME all point inside it.
#   tests/clean/run.sh <ofx-clean>
set -u
bin=${1:?usage: run.sh <ofx-clean>}
T=$(mktemp -d "${TMPDIR:-/tmp}/ofx-clean-test-XXXXXX") || exit 1
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/cache/thumbnails/normal" "$T/config"
old="$T/cache/thumbnails/normal/0123456789abcdef0123456789abcdef.png"
printf 'x' > "$old"
touch -a -d '200 days ago' "$old"
printf '1 0 0:1 / / rw - ext4 /dev/x rw\n' > "$T/mountinfo"
fails=0
run() { HOME="$T" XDG_CACHE_HOME="$T/cache" XDG_CONFIG_HOME="$T/config" OFX_GUI_ROOT="$T" OFX_TEST_MOUNTINFO="$T/mountinfo" "$bin" "$@" > "$T/out" 2> "$T/err"; }
check() { if ! grep -q "$2" "$T/$1"; then echo "clean: $3: no '$2' in: $(cat "$T/$1")"; fails=$((fails + 1)); fi; }

# No ofx.conf: the built-in defaults (90 days: the preview is unused), and the file isn't written
run --dry-run
check out " 1 unused" "no file"
[ -e "$T/config/ofx/ofx.conf" ] && { echo "clean: ofx-clean wrote ofx.conf"; fails=$((fails + 1)); }
mkdir -p "$T/config/ofx"
# clean-unused-days = 0 and clean-orphans = off: kept; a bad line said on stderr, the rest applies
printf 'clean-unused-days = 0\nclean-orphans = off\nclean-cap = lots\n' > "$T/config/ofx/ofx.conf"
run --dry-run
check out " 0 unused" "days 0"
check err "ofx.conf line 3 skipped: clean-cap wants a size" "bad line"
# clean = off: nothing at all, said, exit 0, the preview there
printf 'clean = off\n' > "$T/config/ofx/ofx.conf"
if run; then check out "ofx-clean: off (clean = off in $T/config/ofx/ofx.conf)" "off"; else echo "clean: clean = off exited $?"; fails=$((fails + 1)); fi   # (its exit 0 checked too: F11 review)
[ -e "$old" ] || { echo "clean: clean = off deleted"; fails=$((fails + 1)); }
# clean-unused-days = 100: deleted for real
printf 'clean-unused-days = 100\n' > "$T/config/ofx/ofx.conf"
run
check out " 1 unused" "100 days"
[ -e "$old" ] && { echo "clean: not deleted at 100 days"; fails=$((fails + 1)); }
echo "clean: $fails failed"
[ "$fails" = 0 ]
