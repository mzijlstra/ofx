#!/bin/sh
# Every GUI script, each on a fresh scratch tree, each in its own headless weston
# (tools/gui/run-script.sh):
#   tests/gui/run.sh [-j N] <scratch-dir> [binary] [group...]
# -j N: N groups at once (F11 review, 2026-11-02), each this script on that one group, its output
# printed whole, in order, once all are done; clip and keys share one (each starts a sway, which
# takes the first free wayland-N).
# The groups (one or more scripts on one tree): browse big gone watchgap f2 f3 f4 f5 f52 f53 f54
# f6 (f6, f6b) usb f62 (f62, f62b, f62c) f63 (f63, f63b, f63c) f64 (f64, f64b, f64c, f64d) f7 (f7, f7b) f72 f73 (f73, f73b, f73c, f73d) f74 f75 (f75, f75b) f8 (f8, f8b, f8c, f8d, f8e) f9 (f9, f9b) f10 (f10, f10b, f10c, f10d) f11 (f11, f11b, f11c, f11d, f11e, f11f, f11g, f11h, f11i, f11j, f11k, f11l, f11m, f11n, f11o, f11p, f11q, f11r, f11s) f12 clip keys (both in sway) hang guard3 guard. Named ones only, or all of them; a group
# that fails says "== <script>: FAILED" or "<group>: FAILED" (tools/mutation/gui.py reads it).
set -u
par=1
case ${1:-} in -j) par=${2:?-j N}; shift 2 ;; -j*) par=${1#-j}; shift ;; esac
case $par in *[!0-9]* | 0 | "") echo "run.sh: -j takes a number of groups at once: '$par'" >&2; exit 1 ;; esac
base=${1:?usage: run.sh [-j N] <scratch-dir> [binary]}
bin=${2:-$(cd "$(dirname "$0")/../.." && pwd)/build/ofx}
shift 2 2>/dev/null || shift $#
only=" $* "
want() { [ "$only" = "  " ] || case $only in *" $1 "*) true ;; *) false ;; esac; }
# Each run removes $base/<name>: an empty or relative base would aim that at / or the cwd.
case $base in /?*) ;; *) echo "run.sh: the scratch dir must be an absolute path: '$base'" >&2; exit 1 ;; esac
here=$(cd "$(dirname "$0")" && pwd)
fail=0
if [ "$par" -gt 1 ]; then
    jobs=""
    for g in $(sed -n 's/^if want \([a-z0-9]*\); then$/\1/p' "$0"); do
        case $only in "  " | *" $g "*) ;; *) continue ;; esac
        case $g in keys) case $jobs in *clip) jobs="$jobs+keys"; continue ;; esac ;; esac
        jobs="$jobs $g"
    done
    mkdir -p "$base/.par"
    # f11 starts first (the longest, 19 scripts: last, it set the time); the output keeps run.sh's order.
    { printf '%s\n' $jobs | grep -x f11; printf '%s\n' $jobs | grep -vx f11; } | xargs -P "$par" -I{} sh -c 'j=$1; "$2" "$3" "$4" $(echo "$j" | tr + " ") > "$3/.par/$j.out" 2>&1; echo $? > "$3/.par/$j.rc"' sh {} "$0" "$base" "$bin"
    for j in $jobs; do
        cat "$base/.par/$j.out"
        [ "$(cat "$base/.par/$j.rc" 2>/dev/null)" = 0 ] || { echo "$j: FAILED (exit $(cat "$base/.par/$j.rc" 2>/dev/null))"; fail=1; }
    done
    rm -rf "$base/.par"
    exit $fail
fi
# A run that fails says so by name: a script that can't even start (its tree missing) prints no
# "script: N of M" line of its own (fe01f7c ran f6 that way unseen).
# OFX_GUI_FAIL_FAST=1 (the GUI mutants, tools/mutation/gui.py): the first failed script ends the run,
# and each script ends at its first failure; one is all a mutant needs to count as killed.
failed() { echo "== $1: FAILED"; fail=1; [ -z "${OFX_GUI_FAIL_FAST:-}" ] || exit 1; }
# rs <script> [VAR=value ...] -- [ofx args]: one script on $T's tree, in its own compositor
# (run-script.sh), the variables its alone (a subshell), a failure said by name.
rs() {
    n=$1; shift; echo "== $n"
    ( while [ $# -gt 0 ] && [ "$1" != -- ]; do export "$1"; shift; done; [ $# -eq 0 ] || shift
      exec "$here/../../tools/gui/run-script.sh" "$T" "$here/$n.script" "$bin" "$@" ) || failed "$n"
}
mi() {   # <T>/mountinfo with / alone on it: ext4, or btrfs (the drive rules differ)
    case $1 in ext4) printf '1 0 0:1 / / rw - ext4 /dev/x rw\n' ;; *) printf '33 1 0:31 / / rw - btrfs /dev/x rw\n' ;; esac > "$T/mountinfo"
}
tree() {   # a fresh <T>/tree
    rm -rf "$1" && mkdir -p "$1/tree/adir" "$1/tree/bdir"
    touch "$1/tree/adir/inner.txt" "$1/tree/a.txt" "$1/tree/b2.txt" "$1/tree/.hidden"
    printf '0123456789' > "$1/tree/b10.txt"
}
if want browse; then
T=$base/browse; tree "$T"
rs browse -- "$T/tree"
fi
if want big; then
T=$base/big; rm -rf "$T"; mkdir -p "$T/tree"; (cd "$T/tree" && seq -f 'f%g' 0 9999 | xargs touch)
rs big -- "$T/tree"
fi
if want gone; then
T=$base/gone; tree "$T"
rs gone -- "$T/tree"
fi
if want watchgap; then
T=$base/watchgap; tree "$T"
rs watchgap OFX_TEST_WATCH_DELAY_MS=300 -- "$T/tree"
fi
if want f2; then
T=$base/f2; rm -rf "$T"; mkdir -p "$T/tree/adir" "$T/tree/bdir" "$T/config/gtk-3.0"
printf '%030d' 0 > "$T/tree/apple.txt"; printf '%010d' 0 > "$T/tree/Banana.txt"; printf '%020d' 0 > "$T/tree/cherry.txt"
printf '%05d' 0 > "$T/tree/.dot"; touch "$T/tree/adir/in-a.txt" "$T/tree/bdir/in-b.txt"
touch -d 2024-01-03 "$T/tree/apple.txt"; touch -d 2024-01-01 "$T/tree/Banana.txt"; touch -d 2024-01-02 "$T/tree/cherry.txt"
touch -d 2024-02-01 "$T/tree/adir"; touch -d 2024-01-15 "$T/tree/bdir"
printf 'file://%s/tree/bdir Bee\nfile://%s/tree Tree\n' "$T" "$T" > "$T/config/gtk-3.0/bookmarks"
rs f2 -- "$T/tree"
fi
if want f3; then
T=$base/f3; rm -rf "$T"; mkdir -p "$T/tree/adir" "$T/tree/bdir" "$T/data"
touch "$T/tree/adir/inner.txt" "$T/tree/a.txt" "$T/tree/b.txt" "$T/tree/c.txt"
head -c 8388608 /dev/urandom > "$T/tree/big.bin"
rs f3 OFX_TEST_COPY_DELAY_MS=150 -- "$T/tree"
fi
if want f4; then
T=$base/f4; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/adir" "$H/tree/bdir" "$T/data"
touch "$H/tree/adir/x" "$H/tree/adir/y" "$H/tree/adir/.h" "$H/tree/f.txt"
L="$H/tree"; for i in 1 2 3 4 5 6 7 8; do L="$L/zz-a-rather-long-folder-name-$i"; done; mkdir -p "$L"
rs f4 OFX_TEST_COUNT_DELAY_MS=50 OFX_TEST_HOME="$H" -- "$H/tree"
fi
if want f5; then
T=$base/f5; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/adir" "$H/tree/bdir" "$H/tree/cdir" "$H/tree/gone" "$T/data"
touch "$H/tree/adir/a1" "$H/tree/adir/a2" "$H/tree/bdir/b1" "$H/tree/gone/g1"
for i in $(seq -w 1 40); do touch "$H/tree/f$i"; done
head -c 8388608 /dev/urandom > "$H/tree/big.bin"
rs f5 OFX_TEST_COPY_DELAY_MS=150 OFX_TEST_HOME="$H" -- "$H/tree"
fi
if want f52; then
T=$base/f52; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/adir" "$H/tree/bdir" "$T/data"
touch "$H/tree/adir/a1" "$H/tree/notes.txt" "$H/tree/.dot"
head -c 4194304 /dev/urandom > "$H/tree/bdir/big.bin"
L="$H/tree"; for i in 1 2 3 4 5 6 7 8; do L="$L/zz-a-rather-long-folder-name-$i"; done; mkdir -p "$L"
rs f52 OFX_TEST_COPY_DELAY_MS=150 OFX_TEST_CROSS=1 OFX_TEST_HOME="$H" -- "$H/tree"
fi
if want f53; then
T=$base/f53; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/src" "$H/tree/src2" "$H/tree/adir" "$H/tree/bdir" "$H/tree/cdir" "$H/tree/ddir" "$H/tree/many" "$T/data"
echo d > "$H/tree/ddir/d.txt"
head -c 8388608 /dev/urandom > "$H/tree/src/big1.bin"; head -c 8388608 /dev/urandom > "$H/tree/src2/big2.bin"
echo z > "$H/tree/src/z.txt"; echo z > "$H/tree/src2/z.txt"; echo old > "$H/tree/bdir/z.txt"; echo old > "$H/tree/cdir/z.txt"
for i in $(seq -w 1 10); do touch "$H/tree/many/f$i"; done
rs f53 OFX_TEST_COPY_DELAY_MS=300 OFX_TEST_HOME="$H" -- "$H/tree"
# Quitting cancelled the running copy and dropped the waiting ones: no part file, no big1.bin.
if ls -A "$H/tree/adir" | grep -q ofx-part || [ -e "$H/tree/adir/big1.bin" ]; then echo "f53: FAILED: quitting left a copy in adir"; ls -A "$H/tree/adir"; fail=1; fi
fi
if want f54; then
T=$base/f54; tree "$T"
rs f54 -- "$T/tree"
fi
if want f6; then
# F6: a bookmark, a mount list of its own (OFX_TEST_MOUNTINFO), then f6b on the same tree: the
# sidebar's mode came back from the state file.
T=$base/f6; [ -d "$T" ] && chmod -R u+rwx "$T"; rm -rf "$T"   # a failed run can leave f6's locked folder
mkdir -p "$T/tree/adir" "$T/tree/bdir" "$T/config/gtk-3.0" "$T/data"
touch "$T/tree/adir/inner.txt" "$T/tree/b.txt" "$T/tree/c.txt"; printf abc > "$T/tree/a.txt"
printf 'file://%s/tree/bdir Bee\n' "$T" > "$T/config/gtk-3.0/bookmarks"
mi btrfs
mkdir -p "$T/many/zz" "$T/media/OFXUSB"; for i in $(seq -w 0 69); do mkdir "$T/many/d$i"; done
head -c 8388608 /dev/zero > "$T/big.bin"
rs f6 OFX_TEST_COPY_DELAY_MS=200 OFX_TEST_MEDIA="$T/media" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
# f6b with the drives' space 30 s late (a hung drive): the window answers meanwhile and quits at once
# (under 25 s: with 10 and 9 s it failed with six runs at once, 2026-10-26).
echo "== f6b"; start=$(date +%s)
OFX_TEST_SPACE_DELAY_MS=30000 OFX_TEST_MEDIA="$T/media" OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f6b.script" "$bin" "$T/tree" || failed f6b
took=$(( $(date +%s) - start )); if [ "$took" -ge 25 ]; then cat "$T/times"; echo "f6b: FAILED, took ${took}s (the space thread held the quit?)"; fail=1; else echo "f6b: quit in ${took}s with the space 30s late"; fi
fi
if want usb; then
# Unmounted drives (2026-10-30): a block device folder and a udev database of the run's own (the
# script adds and removes drives in them), mounted by run-script's fake udisksctl under media/.
T=$base/usb; rm -rf "$T"; mkdir -p "$T/tree/adir" "$T/sys" "$T/udevdb" "$T/media" "$T/home"
touch "$T/tree/a.txt"
mi btrfs
rs usb OFX_TEST_HOME="$T/home" OFX_TEST_MEDIA="$T/media" OFX_TEST_MOUNTINFO="$T/mountinfo" OFX_TEST_SYSBLOCK="$T/sys" OFX_TEST_UDEV="$T/udevdb" -- "$T/tree"
fi
if want f11; then
# F11: what ofx remembers per folder, in the run's own state and config folders (run-script's).
T=$base/f11; rm -rf "$T"; mkdir -p "$T/tree/sub1" "$T/tree/sub2"
touch "$T/tree/sub1/x.txt" "$T/tree/sub1/y.txt" "$T/tree/sub2/a.txt" "$T/tree/sub2/b.txt" "$T/tree/sub2/c.txt" "$T/tree/.hid"
echo top > "$T/tree/top.txt"
touch -d 2019-01-01 "$T/tree/.hid"; touch -d 2020-01-01 "$T/tree/sub1"; touch -d 2021-01-01 "$T/tree/sub2"; touch -d 2022-01-01 "$T/tree/top.txt"   # (the modified order: sub1, sub2; .hid, top.txt)
mi ext4
mkdir -p "$T/state/ofx" && printf 'sidebar=places\nfolder %s 1 icons - - - -\n' "$T" > "$T/state/ofx/state"   # (as before F11: no folderdefaults=)
rs f11 OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
rs f11b OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
rs f11c OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
rs f11d OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
# step 3: restore-tabs on, HOME the tree (a plain start)
printf 'restore-tabs = on\n' >> "$T/config/ofx/ofx.conf"
rs f11e OFX_TEST_HOME="$T/tree" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
rs f11f OFX_TEST_HOME="$T/tree" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
printf 'window 4242 1 0 %s\n' "$T/tree/sub2" >> "$T/state/ofx/state"
rs f11g OFX_TEST_HOME="$T/tree" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
rs f11h OFX_TEST_HOME="$T/tree" OFX_TEST_MOUNTINFO="$T/mountinfo" -- --restore 4242 "$T/tree"
printf 'window 5151 1 0 %s\nwindow 5252 2 0 %s\n' "$T/tree" "$T/tree" >> "$T/state/ofx/state"
rs f11i OFX_TEST_HOME="$T/tree" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree/sub1"
# (j: Close All with another window open keeps its tabs; k: closed as one of two, none; m: a SIGTERM keeps them)
for x in j k l m; do
  echo "== f11$x"; OFX_TEST_HOME="$T/tree" OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f11$x.script" "$bin" "$T/tree" || failed "f11$x"
  case $x in
  j) grep -q "^window $(cat "$T/f11j.pid") " "$T/state/ofx/state" || { echo "f11j: its tabs not kept"; failed f11j; } ;;
  k) ! grep -q "^window $(cat "$T/f11k.pid") " "$T/state/ofx/state" || { echo "f11k: its tabs kept"; failed f11k; } ;;
  m) grep -q "^window $(cat "$T/f11m.pid") " "$T/state/ofx/state" || { echo "f11m: its tabs not kept"; failed f11m; } ;;
  esac
done
[ -f "$T/other.pid" ] && kill "$(cat "$T/other.pid")" 2>/dev/null   # (the stand-in, when f11l didn't run)
# step 3b: a crash (SIGKILL) keeps the tabs written as they changed; the next start brings them back;
# a running window's line (a stand-in's) is left alone
sed -i '/^window /d' "$T/state/ofx/state"
awk 'BEGIN { for (i = 0; i < 4200; i++) printf "pad%d=%090d\n", i, 0 }' >> "$T/state/ofx/state"   # (420 KiB: the live writes still fit the scratch arena; review, 2026-11-02)
echo "== f11n"; OFX_TEST_WRITE_MS=2000 OFX_TEST_HOME="$T/tree" OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f11n.script" "$bin" "$T/tree"   # (killed: it "fails", no end line)
grep -q "^window $(cat "$T/f11n.pid" 2>/dev/null) [0-9]* 1 open:" "$T/state/ofx/state" || { echo "f11n: no open line after the kill"; failed f11n; }
rs f11o OFX_TEST_HOME="$T/tree" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
( trap '' USR1; exec sleep 20 ) >/dev/null 2>&1 & echo $! > "$T/other.pid"
printf 'window %s 1 0 open:%s:%s %s\n' "$(cat "$T/other.pid")" "$(cut -c1-8 /proc/sys/kernel/random/boot_id)" "$(sed 's/.*) //' /proc/$(cat "$T/other.pid")/stat | cut -d' ' -f20)" "$T/tree/sub1" >> "$T/state/ofx/state"
rs f11p OFX_TEST_HOME="$T/tree" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
kill "$(cat "$T/other.pid")" 2>/dev/null
( trap '' USR1; exec sleep 20 ) >/dev/null 2>&1 & echo $! > "$T/other.pid"
sed 's/.*) //' /proc/$(cat "$T/other.pid")/stat | cut -d' ' -f20 > "$T/windows/$(cat "$T/other.pid")"
rs f11q OFX_TEST_HOME="$T/tree" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
kill "$(cat "$T/other.pid")" 2>/dev/null
printf 'restore-tabs = on\nbogus\n' >> "$T/config/ofx/ofx.conf"
rs f11r OFX_TEST_HOME="$T/tree" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree/top.txt"
printf 'window 6161 9999999999 0 closed %s\n' "$T/tree/sub1" >> "$T/state/ofx/state"
rs f11s OFX_TEST_HOME="$T/tree" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
fi
if want f10; then
# F10 step 1: ofx.conf, made at the start in the run's own config folder (run-script's).
T=$base/f10; rm -rf "$T"; mkdir -p "$T/tree/target" "$T/tree/node_modules"
touch -d '2025-01-02 03:04:05' "$T/tree/a.txt"; touch "$T/tree/target/x.txt" "$T/tree/node_modules/x.txt"
cp "$here/f64/wide.png" "$T/tree/z.png"
mi ext4
rs f10 OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
rs f10b OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
rm -rf "$T/config/ofx" && echo x > "$T/config/ofx"
rs f10c OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
ln -s "$T/config/ofx" "$T/tree/cfg"
rs f10d OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
fi
if want f62; then
# F6.2: the icon view, then f62b on the same tree: the view came back from the state file.
T=$base/f62; rm -rf "$T"; mkdir -p "$T/tree/adir" "$T/tree/bdir"
touch "$T/tree/adir/inner.txt" "$T/tree/bdir/bin.txt" "$T/tree/a rather long file name that wraps over lines.txt" \
      "$T/tree/verylongnamewithoutanybreaksatallhere.bin"
for i in $(seq -w 1 20); do touch "$T/tree/f$i"; done
mi btrfs
rs f62 OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
rs f62b OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
rs f62c OFX_TEST_ICONS_MAX=2 OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
fi
if want f63; then
# F6.3: the icon view's sizes, then f63b on the same tree: the size came back from the state file.
T=$base/f63; rm -rf "$T"; mkdir -p "$T/tree"
for i in $(seq -w 1 60); do touch "$T/tree/f$i"; done
mi btrfs
rs f63 OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
rs f63b OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
# A full icon cache at a zoom (F6.3 review): 24 slots, 12 MIME types.
T=$base/f63c; rm -rf "$T"; mkdir -p "$T/tree"
for f in a.txt b.png c.jpg d.pdf e.c f.py g.sh h.html i.md j.zip k.mp3 l.mp4; do touch "$T/tree/$f"; done
rs f63c OFX_TEST_ICONS_MAX=24 OFX_TEST_MOUNTINFO="$base/f63/mountinfo" -- "$T/tree"
fi
if want f64; then
# F6.4: previews in the icon view (tests/gui/f64/ has the images), then f64b on the same tree and
# cache: a new window reads them, and knows the failures, making none. f64c: textures dropped.
T=$base/f64; rm -rf "$T"; mkdir -p "$T/tree/sub" "$T/tree/far" "$T/cache/thumbnails/normal"
cp "$here/f64/wide.png" "$here/f64/photo.jpg" "$here/f64/anim.gif" "$here/f64/pic.webp" "$here/f64/small.png" "$T/tree/"
cp "$here/f64/wide.png" "$T/tree/far/x.png"
printf 'not a png' > "$T/tree/broken.png"; : > "$T/tree/empty.jpg"; echo text > "$T/tree/readme.txt"
printf '%%PDF-1.4\n' > "$T/tree/notes.pdf"; touch -d @1700000000 "$T/tree/notes.pdf"
# Nemo's preview of the PDF, in normal (a type ofx doesn't make): 90 by 128, the spec's text.
python3 -I - "$T/tree/notes.pdf" "$T/cache/thumbnails/normal" <<'PY'
import hashlib, os, struct, sys, zlib
path, out = sys.argv[1], sys.argv[2]
uri = 'file://' + path
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
raw = b''.join(b'\0' + b'\0\x80\0\xff' * 90 for _ in range(128))
png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 90, 128, 8, 6, 0, 0, 0)) +
       chunk(b'tEXt', b'Thumb::URI\0' + uri.encode()) + chunk(b'tEXt', b'Thumb::MTime\0%d' % int(os.stat(path).st_mtime)) +
       chunk(b'tEXt', b'Software\0GNOME::ThumbnailFactory') + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
open(os.path.join(out, hashlib.md5(uri.encode()).hexdigest() + '.png'), 'wb').write(png)
PY
printf '33 1 0:31 / / rw - btrfs /dev/x rw\n34 33 0:40 / %s rw - nfs4 srv:/x rw\n' "$T/tree/far" > "$T/mountinfo"
rs f64 OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
rs f64b OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
# 40 previews of 96 by 64 (24 KiB each): f64d where 10 may be held (fewer than a screen's 18), f64c
# where 20 may: scrolling away drops the first ones.
T=$base/f64c; rm -rf "$T"; mkdir -p "$T/tree"
for i in $(seq -w 1 40); do cp "$here/f64/wide.png" "$T/tree/w$i.png"; done
rs f64c OFX_TEST_THUMB_HELD_MAX=491520 OFX_TEST_MOUNTINFO="$base/f64/mountinfo" -- "$T/tree"
rs f64d OFX_TEST_THUMB_HELD_MAX=245760 OFX_TEST_MOUNTINFO="$base/f64/mountinfo" -- "$T/tree"
fi
if want f7; then
# F7.1: the menu bar. f7: home/tree with a.txt, b.txt, sub.d/, ro/ (read only); f7b: Close All Windows, a copy running.
# HOME and the mount list are scratch: the trash view shows the scratch trash only, never a drive's.
T=$base/f7; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/sub.d" "$H/tree/ro"; echo a > "$H/tree/a.txt"; echo b > "$H/tree/b.txt"
echo x > "$H/tree/ro/x.txt"; chmod 555 "$H/tree/ro"
mi ext4
# F7.1.1: text/plain's default app in the scratch config, its desktop file in the scratch data home.
mkdir -p "$T/config" "$T/data/applications"
printf '[Default Applications]\ntext/plain=ofx-test-editor.desktop\n' > "$T/config/mimeapps.list"
printf '[Desktop Entry]\nType=Application\nName=Test editor\nExec=true %%f\n' > "$T/data/applications/ofx-test-editor.desktop"
rs f7 OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$H/tree"
chmod 755 "$H/tree/ro"
T=$base/f7b; rm -rf "$T"; mkdir -p "$T/tree/src" "$T/tree/dst"; head -c 8388608 /dev/urandom > "$T/tree/src/big.bin"
rs f7b OFX_TEST_COPY_DELAY_MS=300 OFX_TEST_MOUNTINFO="$base/f7/mountinfo" -- "$T/tree"
# Its own entry gone with it, the live one's taken away at the menu's pass; the sleeps ended.
for k in live reused; do [ -f "$T/$k.pid" ] && kill "$(cat "$T/$k.pid")" 2>/dev/null; done
if [ -n "$(ls -A "$T/windows" 2>/dev/null)" ]; then echo "f7b: FAILED: left in the registry: $(ls -A "$T/windows")"; fail=1; fi
if [ -e "$T/tree/dst/big.bin" ] || ls -A "$T/tree/dst" | grep -q ofx-part; then echo "f7b: FAILED: the copy wasn't cancelled"; fail=1; fi
fi
if want f72; then
# F7.2: the Properties panel. HOME=<T>/home; home/tree: notes.txt (11 bytes, 0644), run.sh (0755),
# dir/ (a 10 bytes; sub/b 20 bytes), empty/, link -> notes.txt, broken -> none, root -> /etc/hostname
# (root's), out -> a file outside <T> (the guard: never changed).
T=$base/f72; O=$base/f72-outside; rm -rf "$T" "$O"; H=$T/home; mkdir -p "$H/tree/dir/sub" "$H/tree/empty" "$O"
printf 'hello world' > "$H/tree/notes.txt"; printf '#!/bin/sh\n' > "$H/tree/run.sh"
printf '0123456789' > "$H/tree/dir/a"; printf '01234567890123456789' > "$H/tree/dir/sub/b"
chmod 644 "$H/tree/notes.txt"; chmod 755 "$H/tree/run.sh"
ln -s notes.txt "$H/tree/link"; ln -s none "$H/tree/broken"; ln -s /etc/hostname "$H/tree/root"
echo keep > "$O/victim"; chmod 644 "$O/victim"; ln -s "$O/victim" "$H/tree/out"
mi ext4
rs f72 OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$H/tree"
if [ "$(stat -c %a "$O/victim")" != 644 ]; then echo "f72: FAILED: a file outside the scratch tree changed its mode"; fail=1; fi
rm -rf "$O"
fi
if want f73; then
# F7.3: Open With. HOME=<T>/home; home/tree: b.txt, notes.txt, e.txt (empty), pic.png, sub/. The
# test's apps in the scratch data home (the system's are listed too: the script filters on
# "ofx t"); their defaults in the scratch config. Launches reach the fake uwsm-app only.
T=$base/f73; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/sub" "$T/config" "$T/data/applications"
echo b > "$H/tree/b.txt"; echo notes > "$H/tree/notes.txt"; : > "$H/tree/e.txt"; cp "$here/f64/wide.png" "$H/tree/pic.png"
printf '[Default Applications]\ntext/plain=ofx-t-edit.desktop\nimage/png=ofx-t-view.desktop\n' > "$T/config/mimeapps.list"
app() { printf '[Desktop Entry]\nType=Application\nName=%s\nExec=true %%F\n%b' "$2" "$3" > "$T/data/applications/$1.desktop"; }
# Icons: a PNG in the data home's pixmaps/, a PNG's own path, a theme's app icon by name.
mkdir -p "$T/data/pixmaps"; cp "$here/f64/wide.png" "$T/data/pixmaps/ofx-t-pix.png"; cp "$here/f64/wide.png" "$T/abs-icon.png"
printf '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="16" height="16" fill="#3c8"/></svg>\n' > "$T/data/pixmaps/ofx-t-vec.svg"
app ofx-t-edit "Ofx T Edit" 'Icon=ofx-t-pix\nMimeType=text/plain;\n'
app ofx-t-view "Ofx T View" "Icon=$T/abs-icon.png\\nMimeType=image/png;\\n"
app ofx-t-code "Ofx T Code" 'Icon=utilities-terminal\nMimeType=text/plain;inode/directory;\n'
app ofx-t-helper "Ofx T Helper" 'Icon=ofx-t-vec\nNoDisplay=true\nMimeType=text/plain;\n'
app ofx-t-hidden "Ofx T Hidden" 'NoDisplay=true\n'
app ofx-t-other "Ofx T Other" 'Icon=ofx-t-no-such-icon\n'
# An SVG over 1 MiB isn't drawn (F7.3.1 review): the generic icon.
{ printf '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><!-- '; head -c 1100000 /dev/zero | tr '\0' x; printf ' --><rect width="16" height="16"/></svg>\n'; } > "$T/data/pixmaps/zz-big.svg"
app zz-big "Zz Big Icon" 'Icon=zz-big\n'
# Set as default (F7.3.1): the desktop's list, read first, sets image/png (Omarchy's sets http).
printf '[Default Applications]\nimage/png=ofx-t-view.desktop\n' > "$T/config/hyprland-mimeapps.list"
mi ext4
rs f73 XDG_CURRENT_DESKTOP=Hyprland OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$H/tree"
rs f73b OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$H/tree/notes.txt"
# --select (FileManager1's ShowItems): a folder selected in its own folder, and a link where it is
rs f73c OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" -- --select "$H/tree/sub/"
ln -s sub "$H/tree/zlink"
rs f73d OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" -- --select "$H/tree/zlink"
fi
if want f74; then
# F7.4: the Edit, View and Go menus. HOME=<T>/home; home/tree: sub/, a.txt (3 bytes), b.txt (10),
# c.txt (1), .hid; the mount list scratch, so the trash view shows the scratch trash only.
T=$base/f74; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/sub"
printf aaa > "$H/tree/a.txt"; printf 0123456789 > "$H/tree/b.txt"; printf c > "$H/tree/c.txt"; : > "$H/tree/.hid"
mi ext4
rs f74 OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$H/tree"
fi
if want f75; then
# F7.5: the Bookmarks menu. HOME=<T>/home; home/tree: a/, b/; the bookmarks file in the scratch
# config (run-script points XDG_CONFIG_HOME there): home/tree/a labelled "Alpha", an sftp line ofx
# keeps but doesn't list, home itself.
T=$base/f75; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/a" "$H/tree/b" "$T/config/gtk-3.0"
printf 'file://%s/tree/a Alpha\nsftp://host/x Server\nfile://%s\n' "$H" "$H" > "$T/config/gtk-3.0/bookmarks"
mi ext4
rs f75 OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$H/tree"
rs f75b OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$H/tree"
fi
if want f8; then
# F8: the right-click menus. The tree as f74's: home/tree: sub/, a.txt, b.txt, c.txt, .hid; the
# scratch mount list (the trash view shows the scratch trash only).
T=$base/f8; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/sub"
printf aaa > "$H/tree/a.txt"; printf 0123456789 > "$H/tree/b.txt"; printf c > "$H/tree/c.txt"; : > "$H/tree/.hid"
mi ext4
rs f8 OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$H/tree"
# F8 step 2: a folder's own entries, Make Link, Follow Link. home/tree: sub/inner.txt, a.txt, zlink ->
# sub/inner.txt; the scratch config's gtk-3.0 for the bookmarks file.
T=$base/f8b; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/sub" "$T/config/gtk-3.0"
printf aaa > "$H/tree/a.txt"; : > "$H/tree/sub/inner.txt"; ln -s sub/inner.txt "$H/tree/zlink"
mi ext4
rs f8b OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$H/tree"
mkdir -p "$T/state/ofx" && printf 'perms=off\nmenubar=off\ncolors=off\ngroup=on\n' > "$T/state/ofx/state"   # (View → Show Permissions, Show Menu, Show Colors off, Show Group on, as a start finds them)
# An 860 px window (OFX_SCRIPT_SIZE): home/tree with two files, its own scratch.
E=$base/f8e; rm -rf "$E"; mkdir -p "$E/home/tree"; printf a > "$E/home/tree/a.txt"; printf b > "$E/home/tree/b.txt"; cp "$T/mountinfo" "$E/"
echo "== f8e"; OFX_SCRIPT_SIZE=860x516 OFX_TEST_HOME="$E/home" OFX_TEST_MOUNTINFO="$E/mountinfo" "$here/../../tools/gui/run-script.sh" "$E" "$here/f8e.script" "$bin" "$E/home/tree" || failed f8e
rs f8d OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$H/tree"
# F8 step 3: a folder elsewhere's menu. home/tree: sub/, sub2/; the bookmarks file: home/tree/sub
# labelled "Sub", then home/tree/sub2; the scratch trash there and empty.
T=$base/f8c; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/sub" "$H/tree/sub2" "$T/config/gtk-3.0" "$T/data/Trash/files" "$T/data/Trash/info"
printf 'file://%s/tree/sub Sub\nfile://%s/tree/sub2\n' "$H" "$H" > "$T/config/gtk-3.0/bookmarks"
mi ext4
rs f8c OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$H/tree"
fi
if want f9; then
# F9 step 1: the search view. home/tree: names at three depths, two index.ts, a hidden folder and
# file, .git and node_modules (left out), a link to a folder (not followed), a link to a file,
# a file 262 bytes below, and zz/ with 10,001 files (the limit).
T=$base/f9; rm -rf "$T"; H=$T/home; mkdir -p "$T/config/gtk-3.0" "$H/tree/src/deep" "$H/tree/.hid" "$H/tree/.git" "$H/tree/node_modules" "$H/tree/docs" "$H/tree/zz"
touch "$H/tree/a.ts" "$H/tree/b.txt" "$H/tree/src/c.ts" "$H/tree/src/index.ts" "$H/tree/src/deep/index.ts" "$H/tree/src/deep/d.TS" \
      "$H/tree/.hid/e.ts" "$H/tree/.f.ts" "$H/tree/.git/g.ts" "$H/tree/node_modules/h.ts" "$H/tree/docs/notes.txt"
printf x > "$H/tree/src/index.ts"; printf x > "$H/tree/src/deep/index.ts"   # (an empty file opens through its type's app)
ln -s src "$H/tree/link"; ln -s ../src/c.ts "$H/tree/docs/clink"
# A result whose folder and name, as the cursor keeps it, are longer than a name (review, 2026-10-28)
L="$H/tree/long/$(printf 'x%.0s' $(seq 250))"; mkdir -p "$L"; printf x > "$L/m2.txt"
(cd "$H/tree/zz" && seq -f 'zz%05g' 0 10000 | xargs touch)
mi ext4
rs f9 OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$H/tree"
# F9 step 3: inside files. "my tree" (a space in every path, for Open at Line's words): two .ts files and a .js with useState, a binary with it, a 17 MB
# file, a link to a file, and a file without it.
T=$base/f9b; rm -rf "$T"; H=$T/home; mkdir -p "$T/config/gtk-3.0" "$H/my tree/src" "$H/my tree/docs"
printf 'import x\nconst [a, b] = useState(0)\n  useState again\n' > "$H/my tree/src/app.ts"
printf 'useState here\n' > "$H/my tree/src/other.js"; printf 'const USESTATE = 1\n' > "$H/my tree/src/Upper.ts"
printf 'useState\0' > "$H/my tree/bin.dat"; truncate -s 17M "$H/my tree/big.txt"; printf 'nothing\n' > "$H/my tree/docs/readme.md"
ln -s src/app.ts "$H/my tree/link.ts"
mi ext4
rs f9b OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$H/my tree"
fi
if want clip; then
# The system clipboard, in the test sway (tools/gui/sway.sh: weston has no data-control, 2026-10-28).
# Due in 30 s, not 120: a window stuck on the clipboard's lock runs out the limit.
T=$base/clip; tree "$T"; rm -f "$T/tree/.hidden" "$T/tree/b2.txt" "$T/tree/b10.txt"; touch "$T/tree/b.txt"
rs clip RUN_SCRIPT_LIMIT=30 OFX_COMPOSITOR=sway -- "$T/tree"
fi
if want keys; then
# Keys through the compositor (tools/gui/vkbd.c in the test sway, 2026-10-28): GLFW's keycodes and
# ui_key_of, which the other scripts' ui/ keys skip.
T=$base/keys; rm -rf "$T"; mkdir -p "$T/tree/adir" "$T/tree/bdir"; touch "$T/tree/a.txt" "$T/tree/my-file name.txt" "$T/tree/adir/inner.txt"
# vkbd's guard first: it types only into the sway OFX_GUI_ROOT names, as the kernel sees the socket's
# other end (review, 2026-10-28). Refused: a display with no test sway's socket file; an
# inherited WAYLAND_SOCKET; a real test sway's socket while sway.pid names another process called
# sway (a copied sleep). The real pid then types, so the refusals aren't for some other reason.
G=$T/guard; mkdir -p "$G/bin"; vk=$here/../../build/vkbd
if WAYLAND_DISPLAY=ofx-no-such OFX_GUI_ROOT="$G" "$vk" b 2>/dev/null; then echo "keys: vkbd typed with no test sway"; failed keys; fi
if gs=$("$here/../../tools/gui/sway.sh" start "$G"); then
    real=$(cat "$G/sway.pid") bad=""   # (failed only after the stop: under OFX_GUI_FAIL_FAST it exits)
    if WAYLAND_SOCKET=99 WAYLAND_DISPLAY="$gs" OFX_GUI_ROOT="$G" "$vk" b 2>/dev/null; then bad="took WAYLAND_SOCKET"; fi
    cp /usr/bin/sleep "$G/bin/sway"; "$G/bin/sway" 30 & fake=$!
    echo "$fake" > "$G/sway.pid"
    if WAYLAND_DISPLAY="$gs" OFX_GUI_ROOT="$G" "$vk" b 2>/dev/null; then bad="typed into a sway sway.pid doesn't name"; fi
    kill "$fake"; echo "$real" > "$G/sway.pid"
    WAYLAND_DISPLAY="$gs" OFX_GUI_ROOT="$G" "$vk" b || bad="refused the test sway itself"
    "$here/../../tools/gui/sway.sh" stop "$G"
    [ -z "$bad" ] || { echo "keys: vkbd $bad"; failed keys; }
else
    failed keys
fi
rs keys RUN_SCRIPT_LIMIT=60 OFX_COMPOSITOR=sway -- "$T/tree"
fi
if want f12; then
# F12 step 3: previews by thumbnailers, the run's own fakes (run-script.sh: the window reads no
# other .thumbnailer files): copy makes a video's (they are PNGs, talk.mp4 with 11 MiB after it:
# past preview-max-size, which a thumbnailer doesn't go by), hang never finishes (killed after
# OFX_TEST_THUMB_RUN_MS), each in bubblewrap as the real ones would be.
T=$base/f12; rm -rf "$T"; mkdir -p "$T/tree" "$T/thumbnailers" "$T/tbin"
cp "$here/f64/wide.png" "$T/tree/clip.mp4"; cp "$here/f64/wide.png" "$T/tree/doc.pdf"
cp "$here/f64/wide.png" "$T/tree/talk.mp4"; head -c 11534336 /dev/zero >> "$T/tree/talk.mp4"
echo hi > "$T/tree/readme.txt"
printf '#!/bin/sh\nhead -c %d "$1" > "$2"\n' "$(stat -c %s "$here/f64/wide.png")" > "$T/tbin/copy"; z="19.$$"; printf '#!/bin/sh\nsleep %s\n' "$z" > "$T/tbin/hang"; chmod +x "$T/tbin/copy" "$T/tbin/hang"   # (z: this run's own, as gui.py runs f12 in several at once)
printf '[Thumbnailer Entry]\nExec=%s %%i %%o\nMimeType=video/mp4;\n' "$T/tbin/copy" > "$T/thumbnailers/copy.thumbnailer"
printf '[Thumbnailer Entry]\nExec=%s %%i %%o\nMimeType=application/pdf;\n' "$T/tbin/hang" > "$T/thumbnailers/hang.thumbnailer"
rs f12 OFX_TEST_THUMB_RUN_MS=500 OFX_TEST_THUMBNAILERS="$T/thumbnailers" -- "$T/tree"
if pgrep -x -f "sleep $z" > /dev/null; then echo "f12: FAILED: a hung thumbnailer outlived the window"; pkill -x -f "sleep $z"; fail=1; fi
# f12b, the same tree and cache (step 4): Clear Previews with ofx-clean itself, a copy among the
# fakes (the guard runs no program outside the run's folder; its own guard keeps it in there too),
# two more files in the cache than f12's three previews.
mkdir -p "$T/fakes" "$T/cache/thumbnails/large" "$T/cache/thumbnails/fail/ofx"
cp -L "$here/../../build/ofx-clean" "$T/fakes/ofx-clean.real"
printf '#!/bin/sh\nsleep 1\nexec "%s" "$@"\n' "$T/fakes/ofx-clean.real" > "$T/fakes/ofx-clean"; chmod +x "$T/fakes/ofx-clean"   # (a second's run: f12b sees the entry greyed meanwhile)
printf 'x' > "$T/cache/thumbnails/large/0123456789abcdef0123456789abcdef.png"
printf 'y' > "$T/cache/thumbnails/fail/ofx/0123456789abcdef0123456789abcdee.png"
rs f12b OFX_TEST_THUMB_RUN_MS=500 OFX_TEST_THUMBNAILERS="$T/thumbnailers" -- "$T/tree"
fi
if want f12g; then
# F12 step 5: git status, on a scratch repository made here (git's own config files out of it), the
# real git behind a wrapper among the fakes (the guard runs no program outside the run's folder), in
# bubblewrap as always. HOME is the run's: no config of Michael's comes in.
T=$base/f12g; rm -rf "$T"; mkdir -p "$T/tree/repo/src" "$T/fakes"
printf '#!/bin/sh\nexec /usr/bin/git "$@"\n' > "$T/fakes/git"; chmod +x "$T/fakes/git"
( cd "$T/tree/repo" && export GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null &&
  git init -q -b main . && printf 'a\n' > a.txt && printf 'k\n' > kept.txt && printf '*.log\nbuild/\n' > .gitignore && printf 'b\n' > src/b.txt &&
  git add -A && git -c user.name=t -c user.email=t@t commit -qm one &&
  printf 'a2\n' >> a.txt && printf 'n\n' > src/new.txt && git add src/new.txt && mkdir newdir build && printf 'u\n' > newdir/u && printf 'l\n' > x.log && printf 'o\n' > build/out.o ) || failed f12g
rs f12g -- "$T/tree/repo"
fi
if want hang; then
T=$base/hang; rm -rf "$T"; mkdir -p "$T/tree/stuck"
echo "== hang"
t0=$(date +%s)
# The count stuck 60 s and the run due within 45 s: a window that waits for the count takes over 60 s,
# and one that doesn't is far under 45 even with six runs at once (with 20 and 15 s, start and quit
# alone took 16-19 s under that load, 2026-10-26).
OFX_TEST_COUNT_DELAY_MS=60000 "$here/../../tools/gui/run-script.sh" "$T" "$here/hang.script" "$bin" "$T/tree" > "$T/run.out" 2>&1
took=$(( $(date +%s) - t0 ))
if grep -q "script: 0 of" "$T/run.out" && grep -q "no asan reports" "$T/run.out" && [ "$took" -lt 45 ]; then
    echo "hang: quit in ${took}s with a count stuck for 60s"
else
    cat "$T/run.out" "$T/times"; echo "hang: FAILED (took ${took}s)"; fail=1
fi
fi
if want guard3; then
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
fi
if want guard; then
# The guard: started in $base, outside <T>, every sh line is refused and nothing runs.
T=$base/guard; rm -rf "$T"; mkdir -p "$T"
echo "== guard"
"$here/../../tools/gui/run-script.sh" "$T" "$here/guard.script" "$bin" "$base" > "$T/run.out" 2>&1
if grep -q "sh refused: .* is not under OFX_GUI_ROOT" "$T/run.out" && [ ! -e "$T/outside-ran" ] && grep -q "no asan reports" "$T/run.out"; then
    echo "guard: sh refused outside the scratch tree"
else
    cat "$T/run.out"; echo "guard: FAILED"; fail=1
fi
fi
exit $fail
