#!/bin/sh
# Every GUI script, each on a fresh scratch tree, on a headless output the caller made:
#   tests/gui/run.sh <output> <scratch-dir> [binary]
set -u
out=${1:?usage: run.sh <output> <scratch-dir> [binary]} base=${2:?usage: run.sh <output> <scratch-dir> [binary]}
bin=${3:-$(cd "$(dirname "$0")/../.." && pwd)/build/fx}
# Each run removes $base/<name>: an empty or relative base would aim that at / or the cwd.
case $base in /?*) ;; *) echo "run.sh: the scratch dir must be an absolute path: '$base'" >&2; exit 1 ;; esac
here=$(cd "$(dirname "$0")" && pwd)
fail=0
tree() {   # a fresh <T>/tree
    rm -rf "$1" && mkdir -p "$1/tree/adir" "$1/tree/bdir"
    touch "$1/tree/adir/inner.txt" "$1/tree/a.txt" "$1/tree/b2.txt" "$1/tree/.hidden"
    printf '0123456789' > "$1/tree/b10.txt"
}
T=$base/browse; tree "$T"
echo "== browse"; "$here/../../tools/gui/run-script.sh" "$out" "$T" "$here/browse.script" "$bin" "$T/tree" || fail=1
T=$base/big; rm -rf "$T"; mkdir -p "$T/tree"; (cd "$T/tree" && seq -f 'f%g' 0 9999 | xargs touch)
echo "== big"; "$here/../../tools/gui/run-script.sh" "$out" "$T" "$here/big.script" "$bin" "$T/tree" || fail=1
T=$base/gone; tree "$T"
echo "== gone"; "$here/../../tools/gui/run-script.sh" "$out" "$T" "$here/gone.script" "$bin" "$T/tree" || fail=1
T=$base/watchgap; tree "$T"
echo "== watchgap"; FX_TEST_WATCH_DELAY_MS=300 "$here/../../tools/gui/run-script.sh" "$out" "$T" "$here/watchgap.script" "$bin" "$T/tree" || fail=1
# The guard: started in $base, outside <T>, every sh line is refused and nothing runs.
T=$base/guard; rm -rf "$T"; mkdir -p "$T"
echo "== guard"
"$here/../../tools/gui/run-script.sh" "$out" "$T" "$here/guard.script" "$bin" "$base" > "$T/run.out" 2>&1
if grep -q "sh refused: .* is not under FX_GUI_ROOT" "$T/run.out" && [ ! -e "$T/outside-ran" ] && grep -q "no asan reports" "$T/run.out"; then
    echo "guard: sh refused outside the scratch tree"
else
    cat "$T/run.out"; echo "guard: FAILED"; fail=1
fi
exit $fail
