#!/bin/sh
# Every GUI script, each on a fresh scratch tree, each in its own headless weston
# (tools/gui/run-script.sh):
#   tests/gui/run.sh <scratch-dir> [binary]
set -u
base=${1:?usage: run.sh <scratch-dir> [binary]}
bin=${2:-$(cd "$(dirname "$0")/../.." && pwd)/build/ofx}
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
echo "== browse"; "$here/../../tools/gui/run-script.sh" "$T" "$here/browse.script" "$bin" "$T/tree" || fail=1
T=$base/big; rm -rf "$T"; mkdir -p "$T/tree"; (cd "$T/tree" && seq -f 'f%g' 0 9999 | xargs touch)
echo "== big"; "$here/../../tools/gui/run-script.sh" "$T" "$here/big.script" "$bin" "$T/tree" || fail=1
T=$base/gone; tree "$T"
echo "== gone"; "$here/../../tools/gui/run-script.sh" "$T" "$here/gone.script" "$bin" "$T/tree" || fail=1
T=$base/watchgap; tree "$T"
echo "== watchgap"; OFX_TEST_WATCH_DELAY_MS=300 "$here/../../tools/gui/run-script.sh" "$T" "$here/watchgap.script" "$bin" "$T/tree" || fail=1
T=$base/f2; rm -rf "$T"; mkdir -p "$T/tree/adir" "$T/tree/bdir" "$T/config/gtk-3.0"
printf '%030d' 0 > "$T/tree/apple.txt"; printf '%010d' 0 > "$T/tree/Banana.txt"; printf '%020d' 0 > "$T/tree/cherry.txt"
printf '%05d' 0 > "$T/tree/.dot"; touch "$T/tree/adir/in-a.txt" "$T/tree/bdir/in-b.txt"
touch -d 2024-01-03 "$T/tree/apple.txt"; touch -d 2024-01-01 "$T/tree/Banana.txt"; touch -d 2024-01-02 "$T/tree/cherry.txt"
touch -d 2024-02-01 "$T/tree/adir"; touch -d 2024-01-15 "$T/tree/bdir"
printf 'file://%s/tree/bdir Bee\nfile://%s/tree Tree\n' "$T" "$T" > "$T/config/gtk-3.0/bookmarks"
echo "== f2"; "$here/../../tools/gui/run-script.sh" "$T" "$here/f2.script" "$bin" "$T/tree" || fail=1
T=$base/f3; rm -rf "$T"; mkdir -p "$T/tree/adir" "$T/tree/bdir" "$T/data"
touch "$T/tree/adir/inner.txt" "$T/tree/a.txt" "$T/tree/b.txt" "$T/tree/c.txt"
head -c 8388608 /dev/urandom > "$T/tree/big.bin"
echo "== f3"; OFX_TEST_COPY_DELAY_MS=150 "$here/../../tools/gui/run-script.sh" "$T" "$here/f3.script" "$bin" "$T/tree" || fail=1
T=$base/f4; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/adir" "$H/tree/bdir" "$T/data"
touch "$H/tree/adir/x" "$H/tree/adir/y" "$H/tree/adir/.h" "$H/tree/f.txt"
L="$H/tree"; for i in 1 2 3 4 5 6 7 8; do L="$L/zz-a-rather-long-folder-name-$i"; done; mkdir -p "$L"
echo "== f4"; OFX_TEST_COUNT_DELAY_MS=50 OFX_TEST_HOME="$H" "$here/../../tools/gui/run-script.sh" "$T" "$here/f4.script" "$bin" "$H/tree" || fail=1
T=$base/f5; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/adir" "$H/tree/bdir" "$H/tree/cdir" "$H/tree/gone" "$T/data"
touch "$H/tree/adir/a1" "$H/tree/adir/a2" "$H/tree/bdir/b1" "$H/tree/gone/g1"
for i in $(seq -w 1 40); do touch "$H/tree/f$i"; done
head -c 8388608 /dev/urandom > "$H/tree/big.bin"
echo "== f5"; OFX_TEST_COPY_DELAY_MS=150 OFX_TEST_HOME="$H" "$here/../../tools/gui/run-script.sh" "$T" "$here/f5.script" "$bin" "$H/tree" || fail=1
T=$base/f52; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/adir" "$H/tree/bdir" "$T/data"
touch "$H/tree/adir/a1" "$H/tree/notes.txt" "$H/tree/.dot"
head -c 4194304 /dev/urandom > "$H/tree/bdir/big.bin"
L="$H/tree"; for i in 1 2 3 4 5 6 7 8; do L="$L/zz-a-rather-long-folder-name-$i"; done; mkdir -p "$L"
echo "== f52"; OFX_TEST_COPY_DELAY_MS=150 OFX_TEST_CROSS=1 OFX_TEST_HOME="$H" "$here/../../tools/gui/run-script.sh" "$T" "$here/f52.script" "$bin" "$H/tree" || fail=1
T=$base/f53; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/src" "$H/tree/src2" "$H/tree/adir" "$H/tree/bdir" "$H/tree/cdir" "$H/tree/ddir" "$H/tree/many" "$T/data"
echo d > "$H/tree/ddir/d.txt"
head -c 8388608 /dev/urandom > "$H/tree/src/big1.bin"; head -c 8388608 /dev/urandom > "$H/tree/src2/big2.bin"
echo z > "$H/tree/src/z.txt"; echo z > "$H/tree/src2/z.txt"; echo old > "$H/tree/bdir/z.txt"; echo old > "$H/tree/cdir/z.txt"
for i in $(seq -w 1 10); do touch "$H/tree/many/f$i"; done
echo "== f53"; OFX_TEST_COPY_DELAY_MS=300 OFX_TEST_HOME="$H" "$here/../../tools/gui/run-script.sh" "$T" "$here/f53.script" "$bin" "$H/tree" || fail=1
# Quitting cancelled the running copy and dropped the waiting ones: no part file, no big1.bin.
if ls -A "$H/tree/adir" | grep -q ofx-part || [ -e "$H/tree/adir/big1.bin" ]; then echo "f53: quitting left a copy in adir"; ls -A "$H/tree/adir"; fail=1; fi
T=$base/f54; tree "$T"
echo "== f54"; "$here/../../tools/gui/run-script.sh" "$T" "$here/f54.script" "$bin" "$T/tree" || fail=1
# F6: a bookmark, a mount list of its own (OFX_TEST_MOUNTINFO), then f6b on the same tree: the
# sidebar's mode came back from the state file.
T=$base/f6; rm -rf "$T"; mkdir -p "$T/tree/adir" "$T/tree/bdir" "$T/config/gtk-3.0" "$T/data"
touch "$T/tree/adir/inner.txt" "$T/tree/b.txt" "$T/tree/c.txt"; printf abc > "$T/tree/a.txt"
printf 'file://%s/tree/bdir Bee\n' "$T" > "$T/config/gtk-3.0/bookmarks"
printf '33 1 0:31 / / rw - btrfs /dev/x rw\n' > "$T/mountinfo"
mkdir -p "$T/many/zz" "$T/media/OFXUSB"; for i in $(seq -w 0 69); do mkdir "$T/many/d$i"; done
head -c 8388608 /dev/zero > "$T/big.bin"
echo "== f6"; OFX_TEST_COPY_DELAY_MS=200 OFX_TEST_MEDIA="$T/media" OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f6.script" "$bin" "$T/tree" || fail=1
# f6b with the drives' space 10 s late (a hung drive): the window answers meanwhile and quits at once.
echo "== f6b"; start=$(date +%s)
OFX_TEST_SPACE_DELAY_MS=10000 OFX_TEST_MEDIA="$T/media" OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f6b.script" "$bin" "$T/tree" || fail=1
took=$(( $(date +%s) - start )); if [ "$took" -ge 9 ]; then echo "f6b: FAILED, took ${took}s (the space thread held the quit?)"; fail=1; else echo "f6b: quit in ${took}s with the space 10s late"; fi
T=$base/hang; rm -rf "$T"; mkdir -p "$T/tree/stuck"
echo "== hang"
t0=$(date +%s)
OFX_TEST_COUNT_DELAY_MS=10000 "$here/../../tools/gui/run-script.sh" "$T" "$here/hang.script" "$bin" "$T/tree" > "$T/run.out" 2>&1
took=$(( $(date +%s) - t0 ))
if grep -q "script: 0 of" "$T/run.out" && grep -q "no asan reports" "$T/run.out" && [ "$took" -lt 8 ]; then
    echo "hang: quit in ${took}s with a count stuck for 10s"
else
    cat "$T/run.out"; echo "hang: FAILED (took ${took}s)"; fail=1
fi
# ofx's own guard: started outside <T>, every change it tries there is refused, nothing changes.
T=$base/guard3; O=$base/guard3-outside; rm -rf "$T" "$O"; mkdir -p "$T" "$O"; echo keep > "$O/victim.txt"
echo "== guard3"
"$here/../../tools/gui/run-script.sh" "$T" "$here/guard3.script" "$bin" "$O" > "$T/run.out" 2>&1
if [ "$(ls -A "$O")" = victim.txt ] && [ "$(cat "$O/victim.txt")" = keep ] && grep -q "script: 0 of" "$T/run.out" &&
   [ "$(grep -c "ofx: guard: refused" "$T/err")" -ge 4 ] && grep -q "no asan reports" "$T/run.out"; then
    echo "guard3: every write outside the scratch tree refused"
else
    cat "$T/run.out" "$T/err"; ls -A "$O"; echo "guard3: FAILED"; fail=1
fi
rm -rf "$O"
# The guard: started in $base, outside <T>, every sh line is refused and nothing runs.
T=$base/guard; rm -rf "$T"; mkdir -p "$T"
echo "== guard"
"$here/../../tools/gui/run-script.sh" "$T" "$here/guard.script" "$bin" "$base" > "$T/run.out" 2>&1
if grep -q "sh refused: .* is not under OFX_GUI_ROOT" "$T/run.out" && [ ! -e "$T/outside-ran" ] && grep -q "no asan reports" "$T/run.out"; then
    echo "guard: sh refused outside the scratch tree"
else
    cat "$T/run.out"; echo "guard: FAILED"; fail=1
fi
exit $fail
