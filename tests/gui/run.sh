#!/bin/sh
# Every GUI script, each on a fresh scratch tree, on a headless output the caller made:
#   tests/gui/run.sh <output> <scratch-dir> [binary]
set -u
out=$1 base=$2 bin=${3:-$(cd "$(dirname "$0")/../.." && pwd)/build/fx}
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
exit $fail
