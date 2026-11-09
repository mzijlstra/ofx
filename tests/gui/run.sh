#!/bin/sh
# Every GUI script, each on a fresh scratch tree, each in its own headless weston
# (tools/gui/run-script.sh):
#   tests/gui/run.sh [-j N] <scratch-dir> [binary] [group...]
# -j N: N groups at once (F11 review, 2026-11-02), each this script on that one group, its output
# printed whole, in order, once all are done; clip, keys and dnd share one (each starts a sway,
# which takes the first free wayland-N).
# The groups (one or more scripts on one tree): browse big gone watchgap f2 f3 f4 f5 f52 f53 f54
# f6 (f6, f6b) usb f62 (f62, f62b, f62c) f63 (f63, f63b, f63c, f63d) f64 (f64, f64b, f64c, f64d) f7 (f7, f7b) f72 f73 (f73, f73b, f73c, f73d) f74 f75 (f75, f75b, f75c) f15 (f15, f15b) f16 (f16, f16b, f16c, f16d) net f8 (f8, f8b, f8c, f8d, f8e) f9 (f9, f9b) f10 (f10, f10b, f10c, f10d) f11 (f11, f11b, f11c, f11d, f11e, f11f, f11g, f11h, f11i, f11j, f11k, f11l, f11m, f11n, f11o, f11p, f11q, f11r, f11s, f11t) f12 (f12, f12b) f12g f12h f12i f13 (f13, f13b, f13c, f13d, f13e, f13f, f13g, f13h) clip (clip, copypath) keys dnd (dnd, dndb, dndc, dndd, dnde, dndf, dndg, dndh, dndi, dndj, dndk) (the three in sway) hang guard3 guard harness (harness, harnessb, harnessc, harnessd). Named ones only, or all of them; a group
# that fails says "== <script>: FAILED" or "<group>: FAILED" (tools/mutation/gui.py reads it).
set -u
par=1
case ${1:-} in -j) par=${2:?-j N}; shift 2 ;; -j*) par=${1#-j}; shift ;; esac
case $par in *[!0-9]* | 0 | "") echo "run.sh: -j takes a number of groups at once: '$par'" >&2; exit 1 ;; esac
base=${1:?usage: run.sh [-j N] <scratch-dir> [binary]}
bin=${2:-$(cd "$(dirname "$0")/../.." && pwd)/build/ofx}
shift 2 2>/dev/null || shift $#
only=" $* "
want() { RS_HOME=; [ "$only" = "  " ] || case $only in *" $1 "*) true ;; *) false ;; esac; }   # (each group sets its own RS_HOME)
# Each run removes $base/<name>: an empty or relative base would aim that at / or the cwd.
case $base in /?*) ;; *) echo "run.sh: the scratch dir must be an absolute path: '$base'" >&2; exit 1 ;; esac
here=$(cd "$(dirname "$0")" && pwd)
fail=0
if [ "$par" -gt 1 ]; then
    jobs=""
    for g in $(sed -n 's/^if want \([a-z0-9]*\); then$/\1/p' "$0"); do
        case $only in "  " | *" $g "*) ;; *) continue ;; esac
        case $g in keys | dnd) case $jobs in *clip | *clip+*) jobs="$jobs+$g"; continue ;; esac ;; esac
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
# (run-script.sh), the variables its alone (a subshell), a failure said by name. RS_HOME: the
# group's HOME for every script of it (OFX_TEST_HOME; code-health review, 2026-11-03: written on
# each line); a group's mount list is run-script's own (/ alone) unless it passes one.
rs() {
    n=$1; shift; echo "== $n"
    # (RS_HOME only while it is this $T's: a group's second T= doesn't inherit the first's home; and a
    # mount list given must be there, as an empty one would leave no mounts at all; 5M review, 2026-11-03)
    ( case ${RS_HOME:-} in "$T"/*) export OFX_TEST_HOME="$RS_HOME" ;; esac
      while [ $# -gt 0 ] && [ "$1" != -- ]; do
          case $1 in OFX_TEST_MOUNTINFO=*) [ -f "${1#*=}" ] || { echo "$n: no mount list at ${1#*=}" >&2; exit 1; } ;; esac
          export "$1"; shift
      done; [ $# -eq 0 ] || shift
      exec "$here/../../tools/gui/run-script.sh" "$T" "$here/$n.script" "$bin" "$@" ) || failed "$n"
}
mi() {   # <T>/mountinfo with / alone on it: ext4, or btrfs (the drive rules differ)
    case $1 in ext4) printf '1 0 0:1 / / rw - ext4 /dev/x rw\n' ;; *) printf '33 1 0:31 / / rw - btrfs /dev/x rw\n' ;; esac > "$T/mountinfo"
}
git_fake() {   # a test's git among <T>/fakes (the guard runs no program outside the run's folder): the real one, after line $2 when given
    { printf '#!/bin/sh\n'; [ -z "${2:-}" ] || printf '%s\n' "$2"; printf 'exec /usr/bin/git "$@"\n'; } > "$1/fakes/git"; chmod +x "$1/fakes/git"
}
git_env() {   # in a scratch repository's subshell: git's own config files out of it, nothing above <T> seen, a fixed author
    export GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null GIT_CEILING_DIRECTORIES="$1" GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@t GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@t
}
menu_tree() {   # <home>/tree for the menus' groups (f74, f8): sub/, a.txt (3 bytes), b.txt (10), c.txt (1), .hid
    mkdir -p "$1/tree/sub"; printf aaa > "$1/tree/a.txt"; printf 0123456789 > "$1/tree/b.txt"; printf c > "$1/tree/c.txt"; : > "$1/tree/.hid"
}
fresh() {   # $base/<name> as T, emptied (made writable first: a run that stopped early, OFX_GUI_FAIL_FAST, may have left a read-only folder: f12g, f13e, f7)
    T=$base/$1; [ ! -d "$T" ] || chmod -R u+rwx "$T"; rm -rf "$T"
}
home_tree() {   # fresh, with H its home
    fresh "$1"; H=$T/home
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
fresh big; mkdir -p "$T/tree"; (cd "$T/tree" && seq -f 'f%g' 0 9999 | xargs touch)
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
fresh f2; mkdir -p "$T/tree/adir" "$T/tree/bdir" "$T/config/gtk-3.0"
printf '%030d' 0 > "$T/tree/apple.txt"; printf '%010d' 0 > "$T/tree/Banana.txt"; printf '%020d' 0 > "$T/tree/cherry.txt"
printf '%05d' 0 > "$T/tree/.dot"; touch "$T/tree/adir/in-a.txt" "$T/tree/bdir/in-b.txt"
touch -d 2024-01-03 "$T/tree/apple.txt"; touch -d 2024-01-01 "$T/tree/Banana.txt"; touch -d 2024-01-02 "$T/tree/cherry.txt"
touch -d 2024-02-01 "$T/tree/adir"; touch -d 2024-01-15 "$T/tree/bdir"
printf 'file://%s/tree/bdir Bee\nfile://%s/tree Tree\n' "$T" "$T" > "$T/config/gtk-3.0/bookmarks"
rs f2 -- "$T/tree"
fi
if want f3; then
fresh f3; mkdir -p "$T/tree/adir" "$T/tree/bdir" "$T/data"
touch "$T/tree/adir/inner.txt" "$T/tree/a.txt" "$T/tree/b.txt" "$T/tree/c.txt"
head -c 8388608 /dev/urandom > "$T/tree/big.bin"
rs f3 OFX_TEST_COPY_DELAY_MS=150 -- "$T/tree"
fi
if want f4; then
home_tree f4; mkdir -p "$H/tree/adir" "$H/tree/bdir" "$T/data"
RS_HOME=$H
touch "$H/tree/adir/x" "$H/tree/adir/y" "$H/tree/adir/.h" "$H/tree/f.txt"
L="$H/tree"; for i in 1 2 3 4 5 6 7 8; do L="$L/zz-a-rather-long-folder-name-$i"; done; mkdir -p "$L"
rs f4 OFX_TEST_COUNT_DELAY_MS=50 -- "$H/tree"
fi
if want f5; then
home_tree f5; mkdir -p "$H/tree/adir" "$H/tree/bdir" "$H/tree/cdir" "$H/tree/gone" "$T/data"
RS_HOME=$H
touch "$H/tree/adir/a1" "$H/tree/adir/a2" "$H/tree/bdir/b1" "$H/tree/gone/g1"
for i in $(seq -w 1 40); do touch "$H/tree/f$i"; done
head -c 8388608 /dev/urandom > "$H/tree/big.bin"
rs f5 OFX_TEST_COPY_DELAY_MS=150 -- "$H/tree"
fi
if want f52; then
home_tree f52; mkdir -p "$H/tree/adir" "$H/tree/bdir" "$T/data"
RS_HOME=$H
touch "$H/tree/adir/a1" "$H/tree/notes.txt" "$H/tree/.dot"
head -c 4194304 /dev/urandom > "$H/tree/bdir/big.bin"
L="$H/tree"; for i in 1 2 3 4 5 6 7 8; do L="$L/zz-a-rather-long-folder-name-$i"; done; mkdir -p "$L"
rs f52 OFX_TEST_COPY_DELAY_MS=150 OFX_TEST_CROSS=1 -- "$H/tree"
fi
if want f53; then
home_tree f53; mkdir -p "$H/tree/src" "$H/tree/src2" "$H/tree/adir" "$H/tree/bdir" "$H/tree/cdir" "$H/tree/ddir" "$H/tree/many" "$T/data"
RS_HOME=$H
echo d > "$H/tree/ddir/d.txt"
head -c 8388608 /dev/urandom > "$H/tree/src/big1.bin"; head -c 8388608 /dev/urandom > "$H/tree/src2/big2.bin"
echo z > "$H/tree/src/z.txt"; echo z > "$H/tree/src2/z.txt"; echo old > "$H/tree/bdir/z.txt"; echo old > "$H/tree/cdir/z.txt"
for i in $(seq -w 1 10); do touch "$H/tree/many/f$i"; done
rs f53 OFX_TEST_COPY_DELAY_MS=300 -- "$H/tree"
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
fresh f6   # (a failed run can leave f6's locked folder)
mkdir -p "$T/tree/adir" "$T/tree/bdir" "$T/config/gtk-3.0" "$T/data"
touch "$T/tree/adir/inner.txt" "$T/tree/b.txt" "$T/tree/c.txt"; printf abc > "$T/tree/a.txt"
printf 'file://%s/tree/bdir Bee\n' "$T" > "$T/config/gtk-3.0/bookmarks"
mi btrfs
mkdir -p "$T/many/zz" "$T/media/OFXUSB"; for i in $(seq -w 0 69); do mkdir "$T/many/d$i"; done
head -c 8388608 /dev/zero > "$T/big.bin"
rs f6 OFX_TEST_COPY_DELAY_MS=200 OFX_TEST_MEDIA="$T/media" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
# f6b with the drives' space 30 s late (a hung drive): the window answers meanwhile and quits at once
# (under 25 s: with 10 and 9 s it failed with six runs at once, 2026-10-26).
start=$(date +%s)
rs f6b OFX_TEST_SPACE_DELAY_MS=30000 OFX_TEST_MEDIA="$T/media" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
took=$(( $(date +%s) - start )); if [ "$took" -ge 25 ]; then cat "$T/times"; echo "f6b: FAILED, took ${took}s (the space thread held the quit?)"; fail=1; else echo "f6b: quit in ${took}s with the space 30s late"; fi
fi
if want usb; then
# Unmounted drives (2026-10-30): a block device folder and a udev database of the run's own (the
# script adds and removes drives in them), mounted by run-script's fake udisksctl under media/.
fresh usb; mkdir -p "$T/tree/adir" "$T/sys" "$T/udevdb" "$T/media" "$T/home"
touch "$T/tree/a.txt"
mi btrfs
rs usb OFX_TEST_HOME="$T/home" OFX_TEST_MEDIA="$T/media" OFX_TEST_MOUNTINFO="$T/mountinfo" OFX_TEST_SYSBLOCK="$T/sys" OFX_TEST_UDEV="$T/udevdb" -- "$T/tree"
fi
if want f11; then
# F11: what ofx remembers per folder, in the run's own state and config folders (run-script's).
home_tree f11; mkdir -p "$T/tree/sub1" "$T/tree/sub2"
touch "$T/tree/sub1/x.txt" "$T/tree/sub1/y.txt" "$T/tree/sub2/a.txt" "$T/tree/sub2/b.txt" "$T/tree/sub2/c.txt" "$T/tree/.hid"
echo top > "$T/tree/top.txt"
touch -d 2019-01-01 "$T/tree/.hid"; touch -d 2020-01-01 "$T/tree/sub1"; touch -d 2021-01-01 "$T/tree/sub2"; touch -d 2022-01-01 "$T/tree/top.txt"   # (the modified order: sub1, sub2; .hid, top.txt)
mkdir -p "$T/state/ofx" && printf 'sidebar=places\nfolder %s 1 icons - - - -\n' "$T" > "$T/state/ofx/state"   # (as before F11: no folderdefaults=)
rs f11 -- "$T/tree"
rs f11b -- "$T/tree"
rs f11c -- "$T/tree"
rs f11d -- "$T/tree"
# step 3: restore-tabs on, HOME the tree (a plain start)
printf 'restore-tabs = on\n' >> "$T/config/ofx/ofx.conf"
RS_HOME=$T/tree
rs f11e -- "$T/tree"
rs f11f -- "$T/tree"
printf 'window 4242 1 0 %s\n' "$T/tree/sub2" >> "$T/state/ofx/state"
rs f11g -- "$T/tree"
rs f11h -- --restore 4242 "$T/tree"
printf 'window 5151 1 0 %s\nwindow 5252 2 0 %s\n' "$T/tree" "$T/tree" >> "$T/state/ofx/state"
rs f11i -- "$T/tree/sub1"
# (j: Close All with another window open keeps its tabs; k: closed as one of two, none; m: a SIGTERM keeps them)
for x in j k l m; do
  rs "f11$x" -- "$T/tree"
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
echo "== f11n"; OFX_TEST_WRITE_MS=2000 OFX_TEST_HOME="$RS_HOME" "$here/../../tools/gui/run-script.sh" "$T" "$here/f11n.script" "$bin" "$T/tree"   # (killed: it "fails", no end line)
grep -q "^window $(cat "$T/f11n.pid" 2>/dev/null) [0-9]* 1 open:" "$T/state/ofx/state" || { echo "f11n: no open line after the kill"; failed f11n; }
rs f11o -- "$T/tree"
( trap '' USR1; exec sleep 20 ) >/dev/null 2>&1 & echo $! > "$T/other.pid"
printf 'window %s 1 0 open:%s:%s %s\n' "$(cat "$T/other.pid")" "$(cut -c1-8 /proc/sys/kernel/random/boot_id)" "$(sed 's/.*) //' /proc/$(cat "$T/other.pid")/stat | cut -d' ' -f20)" "$T/tree/sub1" >> "$T/state/ofx/state"
rs f11p -- "$T/tree"
kill "$(cat "$T/other.pid")" 2>/dev/null
( trap '' USR1; exec sleep 20 ) >/dev/null 2>&1 & echo $! > "$T/other.pid"
sed 's/.*) //' /proc/$(cat "$T/other.pid")/stat | cut -d' ' -f20 > "$T/windows/$(cat "$T/other.pid")"
rs f11q -- "$T/tree"
kill "$(cat "$T/other.pid")" 2>/dev/null
printf 'restore-tabs = on\nbogus\n' >> "$T/config/ofx/ofx.conf"
rs f11r -- "$T/tree/top.txt"
printf 'window 6161 9999999999 0 closed %s\n' "$T/tree/sub1" >> "$T/state/ofx/state"
rs f11s -- "$T/tree"
# The table's strings in 8 KiB, a name of 200 bytes kept again and again (f11t)
fresh f11t; mkdir -p "$T/tree/d"; mi btrfs
touch "$T/tree/d/a" "$T/tree/d/$(printf 'x%.0s' $(seq 200))"
rs f11t OFX_TEST_FOLDERS_MEM=8192 OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
fi
if want f10; then
# F10 step 1: ofx.conf, made at the start in the run's own config folder (run-script's).
fresh f10; mkdir -p "$T/tree/target" "$T/tree/node_modules"
touch -d '2025-01-02 03:04:05' "$T/tree/a.txt"; touch "$T/tree/target/x.txt" "$T/tree/node_modules/x.txt"
cp "$here/f64/wide.png" "$T/tree/z.png"
rs f10 -- "$T/tree"
rs f10b -- "$T/tree"
rm -rf "$T/config/ofx" && echo x > "$T/config/ofx"
rs f10c -- "$T/tree"
ln -s "$T/config/ofx" "$T/tree/cfg"
rs f10d -- "$T/tree"
fi
if want f62; then
# F6.2: the icon view, then f62b on the same tree: the view came back from the state file.
fresh f62; mkdir -p "$T/tree/adir" "$T/tree/bdir"
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
fresh f63; mkdir -p "$T/tree"
for i in $(seq -w 1 60); do touch "$T/tree/f$i"; done
mi btrfs
rs f63 OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
rs f63b OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
# A full icon cache at a zoom (F6.3 review): 24 slots, 12 MIME types.
fresh f63c; mkdir -p "$T/tree"
for f in a.txt b.png c.jpg d.pdf e.c f.py g.sh h.html i.md j.zip k.mp3 l.mp4; do touch "$T/tree/$f"; done
rs f63c OFX_TEST_ICONS_MAX=24 OFX_TEST_MOUNTINFO="$base/f63/mountinfo" -- "$T/tree"
# The same 12 types again in in/, remembered as icons (F11): entered from the list (f63d)
fresh f63d; mkdir -p "$T/tree/in"
for f in a.txt b.png c.jpg d.pdf e.c f.py g.sh h.html i.md j.zip k.mp3 l.mp4; do touch "$T/tree/$f" "$T/tree/in/$f"; done
mkdir -p "$T/state/ofx" && printf 'folder %s 1 icons - - - -\n' "$T/tree/in" > "$T/state/ofx/state"
rs f63d OFX_TEST_ICONS_MAX=24 OFX_TEST_MOUNTINFO="$base/f63/mountinfo" -- "$T/tree"
fi
if want f64; then
# F6.4: previews in the icon view (tests/gui/f64/ has the images), then f64b on the same tree and
# cache: a new window reads them, and knows the failures, making none. f64c: textures dropped.
fresh f64; mkdir -p "$T/tree/sub" "$T/tree/far" "$T/cache/thumbnails/normal"
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
mkdir -p "$T/state/ofx" && printf 'folder %s 1 icons - - - -\n' "$T/tree/far" > "$T/state/ofx/state"   # (far/ opens in the icon view)
rs f64 OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
rs f64b OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
# 40 previews of 96 by 64 (24 KiB each): f64d where 10 may be held (fewer than a screen's 18), f64c
# where 20 may: scrolling away drops the first ones.
fresh f64c; mkdir -p "$T/tree"
for i in $(seq -w 1 40); do cp "$here/f64/wide.png" "$T/tree/w$i.png"; done
rs f64c OFX_TEST_THUMB_HELD_MAX=491520 OFX_TEST_MOUNTINFO="$base/f64/mountinfo" -- "$T/tree"
rs f64d OFX_TEST_THUMB_HELD_MAX=245760 OFX_TEST_MOUNTINFO="$base/f64/mountinfo" -- "$T/tree"
fi
if want f7; then
# F7.1: the menu bar. f7: home/tree with a.txt, b.txt, sub.d/, ro/ (read only); f7b: Close All Windows, a copy running.
# HOME and the mount list are scratch: the trash view shows the scratch trash only, never a drive's.
home_tree f7; mkdir -p "$H/tree/sub.d" "$H/tree/ro"; echo a > "$H/tree/a.txt"; echo b > "$H/tree/b.txt"
RS_HOME=$H
echo x > "$H/tree/ro/x.txt"; chmod 555 "$H/tree/ro"
# F7.1.1: text/plain's default app in the scratch config, its desktop file in the scratch data home.
mkdir -p "$T/config" "$T/data/applications"
printf '[Default Applications]\ntext/plain=ofx-test-editor.desktop\n' > "$T/config/mimeapps.list"
printf '[Desktop Entry]\nType=Application\nName=Test editor\nExec=true %%f\n' > "$T/data/applications/ofx-test-editor.desktop"
rs f7 -- "$H/tree"
chmod 755 "$H/tree/ro"
fresh f7b; mkdir -p "$T/tree/src" "$T/tree/dst"; head -c 8388608 /dev/urandom > "$T/tree/src/big.bin"
rs f7b OFX_TEST_COPY_DELAY_MS=300 -- "$T/tree"
# Its own entry gone with it, the live one's taken away at the menu's pass; the sleeps ended.
for k in live reused; do [ -f "$T/$k.pid" ] && kill "$(cat "$T/$k.pid")" 2>/dev/null; done
if [ -n "$(ls -A "$T/windows" 2>/dev/null)" ]; then echo "f7b: FAILED: left in the registry: $(ls -A "$T/windows")"; fail=1; fi
if [ -e "$T/tree/dst/big.bin" ] || ls -A "$T/tree/dst" | grep -q ofx-part; then echo "f7b: FAILED: the copy wasn't cancelled"; fail=1; fi
fi
if want f72; then
# F7.2: the Properties panel. HOME=<T>/home; home/tree: notes.txt (11 bytes, 0644), run.sh (0755),
# dir/ (a 10 bytes; sub/b 20 bytes), empty/, link -> notes.txt, broken -> none, root -> /etc/hostname
# (root's), out -> a file outside <T> (the guard: never changed).
fresh f72; O=$base/f72-outside; rm -rf "$O"; H=$T/home; mkdir -p "$H/tree/dir/sub" "$H/tree/empty" "$O"
RS_HOME=$H
printf 'hello world' > "$H/tree/notes.txt"; printf '#!/bin/sh\n' > "$H/tree/run.sh"
printf '0123456789' > "$H/tree/dir/a"; printf '01234567890123456789' > "$H/tree/dir/sub/b"
chmod 644 "$H/tree/notes.txt"; chmod 755 "$H/tree/run.sh"
ln -s notes.txt "$H/tree/link"; ln -s none "$H/tree/broken"; ln -s /etc/hostname "$H/tree/root"
echo keep > "$O/victim"; chmod 644 "$O/victim"; ln -s "$O/victim" "$H/tree/out"
rs f72 -- "$H/tree"
if [ "$(stat -c %a "$O/victim")" != 644 ]; then echo "f72: FAILED: a file outside the scratch tree changed its mode"; fail=1; fi
rm -rf "$O"
fi
if want f73; then
# F7.3: Open With. HOME=<T>/home; home/tree: b.txt, notes.txt, e.txt (empty), pic.png, sub/. The
# test's apps in the scratch data home (the system's are listed too: the script filters on
# "ofx t"); their defaults in the scratch config. Launches reach the fake uwsm-app only.
home_tree f73; mkdir -p "$H/tree/sub" "$T/config" "$T/data/applications"
RS_HOME=$H
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
rs f73 XDG_CURRENT_DESKTOP=Hyprland -- "$H/tree"
rs f73b -- "$H/tree/notes.txt"
# --select (FileManager1's ShowItems): a folder selected in its own folder, and a link where it is
rs f73c -- --select "$H/tree/sub/"
ln -s sub "$H/tree/zlink"
rs f73d -- --select "$H/tree/zlink"
fi
if want f74; then
# F7.4: the Edit, View and Go menus. HOME=<T>/home; home/tree: sub/, a.txt (3 bytes), b.txt (10),
# c.txt (1), .hid; the mount list scratch, so the trash view shows the scratch trash only.
home_tree f74
RS_HOME=$H
menu_tree "$H"
rs f74 -- "$H/tree"
fi
if want f75; then
# F7.5: the Bookmarks menu. HOME=<T>/home; home/tree: a/, b/; the bookmarks file in the scratch
# config (run-script points XDG_CONFIG_HOME there): home/tree/a labelled "Alpha", an sftp line ofx
# keeps but doesn't list, home itself.
home_tree f75; mkdir -p "$H/tree/a" "$H/tree/b" "$T/config/gtk-3.0"
RS_HOME=$H
printf 'file://%s/tree/a Alpha\nsftp://host/x Server\nfile://%s\n' "$H" "$H" > "$T/config/gtk-3.0/bookmarks"
rs f75 -- "$H/tree"
rs f75b -- "$H/tree"
# f75c: 20 bookmarked folders, so the sidebar scrolls (the wheel under a question).
home_tree f75c; mkdir -p "$H/tree/a" "$T/config/gtk-3.0"
RS_HOME=$H
for i in $(seq -w 1 20); do mkdir -p "$H/m$i"; printf 'file://%s/m%s\n' "$H" "$i"; done > "$T/config/gtk-3.0/bookmarks"
rs f75c -- "$H/tree"
fi
if want f15; then
# F15 step 2: ofx.conf's key lines, written into the run's own config folder (run-script's) as the
# script goes.
fresh f15; mkdir -p "$T/tree/a" "$T/tree/b"; touch "$T/tree/c.txt"
rs f15 -- "$T/tree"
rs f15b -- "$T/tree"
fi
if want f16; then
# F16 step 2: a far folder read by ofx-list. tree/share stands for a share: the mount list says it is
# a gvfs FUSE mount (no network here; the net group has a real one). share: sub/ (3 files), x.txt,
# .dot; local: a.txt.
fresh f16; mkdir -p "$T/tree/share/sub" "$T/tree/local"
touch "$T/tree/share/sub/1" "$T/tree/share/sub/2" "$T/tree/share/sub/3" "$T/tree/share/x.txt" "$T/tree/share/.dot" "$T/tree/local/a.txt"
ln -s ../share "$T/tree/local/nas"; ln -s ../share/x.txt "$T/tree/local/doc"   # (2b: links into the share)
ln -s "../share/Not a directory" "$T/tree/local/ghost"; mkdir -p "$T/tree/local/mnt"   # (a link to nothing there, by a telling name; a far mount point)
printf '1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw,nosuid,nodev - fuse.gvfsd-fuse gvfsd-fuse rw\n3 1 0:3 / %s rw - nfs4 h:/x rw\n' "$T/tree/share" "$T/tree/local/mnt" > "$T/mountinfo"
rs f16 OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
# f16b: git on a far folder. f16b/tree/repo (local) and f16b/tree/share2 (far by the mount list), each
# a scratch repository with a changed file; git's wrapper among the fakes, as f12g's.
fresh f16b; mkdir -p "$T/tree/repo" "$T/tree/share2" "$T/fakes"
git_fake "$T"
for r in repo share2; do
    ( cd "$T/tree/$r" && git_env "$T" && git init -q -b main . && printf 'a\n' > a.txt && git add -A && git commit -qm one && printf 'b\n' >> a.txt ) || failed f16b
done
printf '1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw,nosuid,nodev - fuse.gvfsd-fuse gvfsd-fuse rw\n' "$T/tree/share2" > "$T/mountinfo"
rs f16b OFX_TEST_MOUNTINFO="$T/mountinfo" OFX_TEST_GIT_GAP_MS=10000 -- "$T/tree/repo"
# f16c: a far link in a folder reached through a local link (step 2b review): tree/proj is a link to
# real/proj, whose nas is ../../share, right only from the real path; share (far) holds f.txt.
fresh f16c; mkdir -p "$T/tree/real/proj" "$T/tree/share"; : > "$T/tree/share/f.txt"
ln -s real/proj "$T/tree/proj"; ln -s ../../share "$T/tree/real/proj/nas"
printf '1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw,nosuid,nodev - fuse.gvfsd-fuse gvfsd-fuse rw\n' "$T/tree/share" > "$T/mountinfo"
rs f16c OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree"
# f16d (step 3): paths that reach the share through local links, and far folders that turn out to be
# a file or gone. tree/share (far) holds sub/ and x.txt; tree/local/mylinks is a link to ../links,
# whose nas is ../share: a chain no path's text shows. ofx starts on local/mylinks/nas.
fresh f16d; mkdir -p "$T/tree/share/sub" "$T/tree/local" "$T/tree/links"; : > "$T/tree/share/x.txt"; : > "$T/tree/local/a.txt"
ln -s ../links "$T/tree/local/mylinks"; ln -s ../share "$T/tree/links/nas"; ln -s mylinks/nas "$T/tree/local/chain"
# media/nas: a far drive (nfs) whose trash holds an item, never looked at (the Trash icon stays empty).
mkdir -p "$T/media/nas/.Trash-$(id -u)/info" "$T/media/nas/.Trash-$(id -u)/files"; : > "$T/media/nas/.Trash-$(id -u)/files/old"
printf '[Trash Info]\nPath=%s\nDeletionDate=2026-11-09T10:00:00\n' "$T/media/nas/old" > "$T/media/nas/.Trash-$(id -u)/info/old.trashinfo"
printf '1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw,nosuid,nodev - fuse.gvfsd-fuse gvfsd-fuse rw\n3 1 0:3 / %s rw - nfs4 h:/x rw\n' "$T/tree/share" "$T/media/nas" > "$T/mountinfo"
rs f16d OFX_TEST_MEDIA="$T/media" OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$T/tree/local/mylinks/nas"
fi
if want net; then
# F16 step 2 on a real share (tests/net/lib.sh: a Samba container, a gvfs of the run's own under <T>/rt,
# so the share's path is inside the guard's root). ofx starts in the share; <T>/pause and <T>/unpause
# stop and start its server for the script. Skipped, saying so, without podman or gvfs-smb.
fresh net; mkdir -p "$T/home"
( NET_LIB="$here/../net/lib.sh"; . "$NET_LIB"
  net_ok || exit 0
  trap net_end EXIT; trap 'exit 130' INT TERM
  { net_server "$T" && net_hold "$T" pub; } || { echo "== net: FAILED (its server or share)"; exit 1; }
  ln -s "$NET_SHARE" "$T/home/nas"   # (2b: a link into the share from the home folder)
  for w in pause unpause; do
      printf '#!/bin/sh\nexec env -i HOME="%s" XDG_RUNTIME_DIR="%s" PATH=/usr/bin podman %s %s > /dev/null\n' "$NET_REAL_HOME" "$NET_REAL_RT" "$w" "$NET_C" > "$T/$w"
      chmod +x "$T/$w"
  done
  printf '1 0 0:1 / / rw - ext4 /dev/x rw\n2 1 0:2 / %s rw,nosuid,nodev - fuse.gvfsd-fuse gvfsd-fuse rw\n' "$T/rt/gvfs" > "$T/mountinfo"
  RS_HOME=$T/home fail=0
  rs net OFX_TEST_MOUNTINFO="$T/mountinfo" -- "$NET_SHARE"
  exit $fail ) || fail=1
fi
if want f8; then
# F8: the right-click menus. The tree as f74's: home/tree: sub/, a.txt, b.txt, c.txt, .hid; the
# scratch mount list (the trash view shows the scratch trash only).
home_tree f8
RS_HOME=$H
menu_tree "$H"
rs f8 -- "$H/tree"
# F8 step 2: a folder's own entries, Make Link, Follow Link. home/tree: sub/inner.txt, a.txt, zlink ->
# sub/inner.txt; the scratch config's gtk-3.0 for the bookmarks file.
home_tree f8b; mkdir -p "$H/tree/sub" "$T/config/gtk-3.0"
RS_HOME=$H
printf aaa > "$H/tree/a.txt"; : > "$H/tree/sub/inner.txt"; ln -s sub/inner.txt "$H/tree/zlink"
rs f8b -- "$H/tree"
mkdir -p "$T/state/ofx" && printf 'perms=off\nmenubar=off\ncolors=off\ngroup=on\n' > "$T/state/ofx/state"   # (View → Show Permissions, Show Menu, Show Colors off, Show Group on, as a start finds them)
# An 860 px window (OFX_SCRIPT_SIZE): home/tree with two files, its own scratch.
E=$base/f8e; rm -rf "$E"; mkdir -p "$E/home/tree"; printf a > "$E/home/tree/a.txt"; printf b > "$E/home/tree/b.txt"
echo "== f8e"; OFX_SCRIPT_SIZE=860x516 OFX_TEST_HOME="$E/home" "$here/../../tools/gui/run-script.sh" "$E" "$here/f8e.script" "$bin" "$E/home/tree" || failed f8e
rs f8d -- "$H/tree"
# F8 step 3: a folder elsewhere's menu. home/tree: sub/, sub2/; the bookmarks file: home/tree/sub
# labelled "Sub", then home/tree/sub2; the scratch trash there and empty.
home_tree f8c; mkdir -p "$H/tree/sub" "$H/tree/sub2" "$T/config/gtk-3.0" "$T/data/Trash/files" "$T/data/Trash/info"
RS_HOME=$H
printf 'file://%s/tree/sub Sub\nfile://%s/tree/sub2\n' "$H" "$H" > "$T/config/gtk-3.0/bookmarks"
rs f8c -- "$H/tree"
fi
if want f9; then
# F9 step 1: the search view. home/tree: names at three depths, two index.ts, a hidden folder and
# file, .git and node_modules (left out), a link to a folder (not followed), a link to a file,
# a file 262 bytes below, and zz/ with 10,001 files (the limit).
home_tree f9; mkdir -p "$T/config/gtk-3.0" "$H/tree/src/deep" "$H/tree/.hid" "$H/tree/.git" "$H/tree/node_modules" "$H/tree/docs" "$H/tree/zz"
RS_HOME=$H
touch "$H/tree/a.ts" "$H/tree/b.txt" "$H/tree/src/c.ts" "$H/tree/src/index.ts" "$H/tree/src/deep/index.ts" "$H/tree/src/deep/d.TS" \
      "$H/tree/.hid/e.ts" "$H/tree/.f.ts" "$H/tree/.git/g.ts" "$H/tree/node_modules/h.ts" "$H/tree/docs/notes.txt"
printf x > "$H/tree/src/index.ts"; printf x > "$H/tree/src/deep/index.ts"   # (an empty file opens through its type's app)
ln -s src "$H/tree/link"; ln -s ../src/c.ts "$H/tree/docs/clink"
# A result whose folder and name, as the cursor keeps it, are longer than a name (review, 2026-10-28)
L="$H/tree/long/$(printf 'x%.0s' $(seq 250))"; mkdir -p "$L"; printf x > "$L/m2.txt"
(cd "$H/tree/zz" && seq -f 'zz%05g' 0 10000 | xargs touch)
rs f9 -- "$H/tree"
# F9 step 3: inside files. "my tree" (a space in every path, for Open at Line's words): two .ts files and a .js with useState, a binary with it, a 17 MB
# file, a link to a file, and a file without it.
home_tree f9b; mkdir -p "$T/config/gtk-3.0" "$H/my tree/src" "$H/my tree/docs"
RS_HOME=$H
printf 'import x\nconst [a, b] = useState(0)\n  useState again\n' > "$H/my tree/src/app.ts"
printf 'useState here\n' > "$H/my tree/src/other.js"; printf 'const USESTATE = 1\n' > "$H/my tree/src/Upper.ts"
printf 'useState\0' > "$H/my tree/bin.dat"; truncate -s 17M "$H/my tree/big.txt"; printf 'nothing\n' > "$H/my tree/docs/readme.md"
ln -s src/app.ts "$H/my tree/link.ts"
rs f9b -- "$H/my tree"
fi
if want clip; then
# The system clipboard, in the test sway (tools/gui/sway.sh: weston has no data-control, 2026-10-28).
# Due in 30 s, not 120: a window stuck on the clipboard's lock runs out the limit.
T=$base/clip; tree "$T"; rm -f "$T/tree/.hidden" "$T/tree/b2.txt" "$T/tree/b10.txt"; touch "$T/tree/b.txt"
rs clip RUN_SCRIPT_LIMIT=30 OFX_COMPOSITOR=sway -- "$T/tree"
# Copy Path (F13), read back as clip's text is: home/tree: adir/, empty/, a.txt, b.txt.
home_tree copypath; mkdir -p "$H/tree/adir" "$H/tree/empty"; touch "$H/tree/a.txt" "$H/tree/b.txt"
RS_HOME=$H
rs copypath RUN_SCRIPT_LIMIT=30 OFX_COMPOSITOR=sway -- "$H/tree"
fi
if want keys; then
# Keys through the compositor (tools/gui/vkbd.c in the test sway, 2026-10-28): GLFW's keycodes and
# gui/glfwkeys.c, which the other scripts' keys skip.
fresh keys; mkdir -p "$T/tree/adir" "$T/tree/bdir"; touch "$T/tree/a.txt" "$T/tree/my-file name.txt" "$T/tree/adir/inner.txt"
# The test tools' guard first (tools/gui/testsway.h, shared by vkbd, vptr and dndpeer): each acts
# only on the sway OFX_GUI_ROOT names, as the kernel sees the socket's other end (review,
# 2026-10-28). Refused: a display with no test sway's socket file; an inherited WAYLAND_SOCKET; a
# real test sway's socket while sway.pid names another process called sway (a copied sleep). The
# real pid then works, so the refusals aren't for some other reason (no display at all refuses as
# well: code-health review, 2026-11-06). Each tool's run: vkbd types b, vptr moves, dndpeer's log
# says ready (it writes it after the guard; its window is stopped after a second).
# A refused dndpeer writes no log.
G=$T/guard; mkdir -p "$G/bin"
guarded() {
    t=$1; shift
    case $t in
    dndpeer) timeout 1 "$here/../../build/dndpeer" target "$G/peer.log"; grep -q "^ready" "$G/peer.log" 2>/dev/null ;;
    vptr) "$here/../../build/vptr" move 10 10 ;;
    vkbd) "$here/../../build/vkbd" b ;;
    esac
}
for t in vkbd vptr dndpeer; do
    if WAYLAND_DISPLAY=ofx-no-such OFX_GUI_ROOT="$G" guarded $t 2>/dev/null; then echo "keys: $t ran with no test sway"; failed keys; fi
done
if gs=$("$here/../../tools/gui/sway.sh" start "$G"); then
    real=$(cat "$G/sway.pid") bad=""   # (failed only after the stop: under OFX_GUI_FAIL_FAST it exits)
    cp /usr/bin/sleep "$G/bin/sway"; "$G/bin/sway" 30 & fake=$!
    for t in vkbd vptr dndpeer; do
        if WAYLAND_SOCKET=99 WAYLAND_DISPLAY="$gs" OFX_GUI_ROOT="$G" guarded $t 2>/dev/null; then bad="$bad $t took WAYLAND_SOCKET;"; fi
        echo "$fake" > "$G/sway.pid"
        if WAYLAND_DISPLAY="$gs" OFX_GUI_ROOT="$G" guarded $t 2>/dev/null; then bad="$bad $t ran in a sway sway.pid doesn't name;"; fi
        echo "$real" > "$G/sway.pid"
    done
    kill "$fake"
    [ ! -e "$G/peer.log" ] || bad="$bad a refused dndpeer wrote its log;"
    for t in vkbd vptr dndpeer; do
        WAYLAND_DISPLAY="$gs" OFX_GUI_ROOT="$G" guarded $t || bad="$bad $t refused the test sway itself;"
    done
    "$here/../../tools/gui/sway.sh" stop "$G"
    [ -z "$bad" ] || { echo "keys:$bad"; failed keys; }
else
    failed keys
fi
rs keys RUN_SCRIPT_LIMIT=60 OFX_COMPOSITOR=sway -- "$T/tree"
fi
if want dnd; then
# Drag and drop with other programs (F14, gui/dnd.c), in the test sway: dndpeer is the other program,
# vptr the pointer (tools/gui/; both refused outside the test sway, as vkbd: the keys group proves
# their guard).
# Each script on its own tree: adir/, a.txt, b.txt, and from/c.txt beside it (a source's file).
dnd_tree() { fresh "$1"; mkdir -p "$T/tree/adir" "$T/from"; touch "$T/tree/a.txt" "$T/tree/b.txt" "$T/from/c.txt"; }
dnd_tree dnd; rs dnd RUN_SCRIPT_LIMIT=40 OFX_COMPOSITOR=sway -- "$T/tree"
for x in b c d e f g h i j k; do dnd_tree "dnd$x"; rs "dnd$x" RUN_SCRIPT_LIMIT=40 OFX_COMPOSITOR=sway -- "$T/tree"; done
fi
if want f12; then
# F12 step 3: previews by thumbnailers, the run's own fakes (run-script.sh: the window reads no
# other .thumbnailer files): copy makes a video's (they are PNGs, talk.mp4 with 11 MiB after it:
# past preview-max-size, which a thumbnailer doesn't go by), hang never finishes (killed after
# OFX_TEST_THUMB_RUN_MS), each in bubblewrap as the real ones would be.
fresh f12; mkdir -p "$T/tree" "$T/thumbnailers" "$T/tbin"
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
c=$here/../../build/ofx-clean   # (built from the sources now, or f12b would test an old one: make builds it)
for f in "$here/../../fs/clean.c" "$here/../../tools/clean.c"; do [ "$c" -nt "$f" ] || { echo "f12b: $c is missing or older than $f: make" >&2; failed f12b; }; done
cp -L "$c" "$T/fakes/ofx-clean.real"
printf '#!/bin/sh\nsleep 1\nexec "%s" "$@"\n' "$T/fakes/ofx-clean.real" > "$T/fakes/ofx-clean"; chmod +x "$T/fakes/ofx-clean"   # (a second's run: f12b sees the entry greyed meanwhile)
printf 'x' > "$T/cache/thumbnails/large/0123456789abcdef0123456789abcdef.png"
printf 'y' > "$T/cache/thumbnails/fail/ofx/0123456789abcdef0123456789abcdee.png"
rs f12b OFX_TEST_THUMB_RUN_MS=500 OFX_TEST_THUMBNAILERS="$T/thumbnailers" -- "$T/tree"
fi
if want f13; then
# F13 step 2: the sidebar's width (f13 drags it; f13b to f13d start from a remembered one).
home_tree f13; mkdir -p "$H/tree/adir"; for k in 1 2 3 4 5 6 7 8; do : > "$H/tree/f$k.txt"; done
RS_HOME=$H
rs f13 -- "$H/tree"
side_state() {   # <name> <sidewidth>: $base/<name> with a state file holding it (f13b to f13d)
    home_tree "$1"; mkdir -p "$H/tree" "$T/state/ofx"; printf 'sidewidth=%s\n' "$2" > "$T/state/ofx/state"; RS_HOME=$H
}
# (each rs by name, so tools/mutation/gui.py finds its group; a narrower window: half of it is 350)
side_state f13b 300; rs f13b OFX_SCRIPT_SIZE=700x516 -- "$H/tree"
side_state f13c 9999; rs f13c OFX_SCRIPT_SIZE=700x516 -- "$H/tree"
side_state f13d abc; rs f13d -- "$H/tree"
# F13 step 3: drag and drop. home/tree: adir/, bdir/, ro/ (read only), a.txt, b.txt, c.txt; bdir
# bookmarked.
home_tree f13e; mkdir -p "$H/tree/adir" "$H/tree/bdir" "$H/tree/ro" "$T/config/gtk-3.0"
for f in a b c; do printf %s "$f" > "$H/tree/$f.txt"; done; chmod 555 "$H/tree/ro"
printf 'file://%s/tree/bdir\n' "$H" > "$T/config/gtk-3.0/bookmarks"
RS_HOME=$H
rs f13e -- "$H/tree"
chmod 755 "$H/tree/ro"
home_tree f13f; mkdir -p "$H/tree/adir" "$H/tree/bdir"; ln -s adir "$H/tree/alink"
RS_HOME=$H
rs f13f -- "$H/tree"
# F13 step 4: spring-loaded tabs. home/tree: adir/deep/, a.txt, b.txt.
home_tree f13g; mkdir -p "$H/tree/adir/deep"; : > "$H/tree/a.txt"; : > "$H/tree/b.txt"
RS_HOME=$H
rs f13g -- "$H/tree"
# The badge's icon across an icon cache reset (code-health review, 2026-11-06). home/tree: adir/, a.txt.
home_tree f13h; mkdir -p "$H/tree/adir"; : > "$H/tree/a.txt"
RS_HOME=$H
rs f13h -- "$H/tree"
fi
if want f12g; then
# F12 step 5: git status, on a scratch repository made here (git's own config files out of it), the
# real git behind a wrapper among the fakes (the guard runs no program outside the run's folder), in
# bubblewrap as always. HOME is the run's: no config of Michael's comes in.
fresh f12g; mkdir -p "$T/tree/repo/src" "$T/fakes"
git_fake "$T"
( cd "$T/tree/repo" && git_env "$T" &&
  git init -q -b main . && printf 'a\n' > a.txt && printf 'k\n' > kept.txt && printf '*.log\nbuild/\n' > .gitignore && printf 'b\n' > src/b.txt &&
  git add -A && git commit -qm one &&
  printf 'a2\n' >> a.txt && printf 'n\n' > src/new.txt && git add src/new.txt && mkdir newdir build && printf 'u\n' > newdir/u && printf 'l\n' > x.log && printf 'o\n' > build/out.o ) || failed f12g
rs f12g OFX_TEST_GIT_GAP_MS=3000 -- "$T/tree/repo"   # (the gap 3 s: its put-off ask checked before it is due, even loaded)
chmod -R u+w "$T"   # (f12g makes its repository read-only for a while: writable again, even if it failed there)
fi
if want f12h; then
# F12 step 5b: the branch button and its menu, on a repository of its own (as f12g's, made here, git's
# own config out of it). Fixed dates, so the commits' ids are too (the detached HEAD's label):
# main one ahead of and one behind its upstream base; feat-a and feat-b checked out after main, then
# main again; old0..old9 on the first commit, older than all. Beside it, 12 folders down (wider than the path's room, so the scratch
# folder's own path never shows), unborn: no commit yet, on a long branch name.
# Step 5c: feat-a (and feat-b after it) hold only-a/, which main doesn't; other changes a.txt (a
# switch there refused with a.txt changed); the wrapper takes a second over a switch to base (the
# menu says so meanwhile, and past OFX_TEST_GIT_SWITCH_MS it carries on). A post-checkout hook says
# something on feat-a.
fresh f12h; mkdir -p "$T/tree/repo" "$T/fakes"
git_fake "$T" 'case "$*" in *" switch "*" base") sleep 1 ;; esac'
( cd "$T/tree/repo" && git_env "$T" &&
  c() { GIT_AUTHOR_DATE="$1 +0000" GIT_COMMITTER_DATE="$1 +0000" git commit -q --allow-empty -m "$1"; } &&
  git init -q -b main . && printf 'a\n' > a.txt && git add a.txt && c 1700000000 &&
  for i in 0 1 2 3 4 5 6 7 8 9; do git branch "old$i"; done && git branch base &&
  git switch -q -c other && printf 'o\n' > a.txt && git add a.txt && c 1700000050 && git switch -q main &&
  c 1700000100 && git switch -q base && c 1700000200 && git switch -q main && git branch -q -u base &&
  git switch -q -c feat-a && mkdir only-a && printf 'x\n' > only-a/x.txt && git add only-a && c 1700000300 &&
  git switch -q -c feat-b && c 1700000400 && git switch -q main &&
  printf '#!/bin/sh\n[ "$(git branch --show-current)" = feat-a ] && echo on feat-a now >&2\nexit 0\n' > .git/hooks/post-checkout && chmod +x .git/hooks/post-checkout &&
  d=../folder-01/folder-02/folder-03/folder-04/folder-05/folder-06/folder-07/folder-08/folder-09/folder-10/folder-11/folder-12/unborn &&
  mkdir -p $d && cd $d && git init -q -b a-branch-name-long-enough-to-take-its-third-of-the-room . ) || failed f12h
rs f12h OFX_TEST_GIT_SWITCH_MS=500 -- "$T/tree/repo"
fi
if want f12i; then
# F12 step 6 review: repo O on main with a branch other, and inside it repo inner with 12 branches,
# other among them; the script removes inner under the branches panel.
fresh f12i; mkdir -p "$T/tree/O" "$T/fakes"
git_fake "$T"
( cd "$T/tree/O" && git_env "$T" &&
  git init -q -b main . && git commit -q --allow-empty -m o && git branch other &&
  mkdir inner && cd inner && git init -q -b main . && git commit -q --allow-empty -m i &&
  for b in other b1 b2 b3 b4 b5 b6 b7 b8 b9 b10; do git branch "$b"; done ) || failed f12i
rs f12i -- "$T/tree/O/inner"
fi
if want hang; then
fresh hang; mkdir -p "$T/tree/stuck"
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
fresh guard3; O=$base/guard3-outside; rm -rf "$O"; mkdir -p "$T" "$O"; echo keep > "$O/victim.txt"
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
fresh guard; mkdir -p "$T"
echo "== guard"
"$here/../../tools/gui/run-script.sh" "$T" "$here/guard.script" "$bin" "$base" > "$T/run.out" 2>&1
if grep -q "sh refused: .* is not under OFX_GUI_ROOT" "$T/run.out" && [ ! -e "$T/outside-ran" ] && grep -q "no asan reports" "$T/run.out"; then
    echo "guard: sh refused outside the scratch tree"
else
    cat "$T/run.out"; echo "guard: FAILED"; fail=1
fi
fi
if want harness; then
# The script runner's own checks (2026-11-04): a window that quits with commands left, or during a
# wait-for, fails, and so does a `quits` whose command leaves it open; a quit with only comments
# left passes. Without the
# first, a mutant that quit where f7b expects a question passed.
fresh harness; mkdir -p "$T/tree"
echo "== harness"
h() {   # <script> <rc> <line the output must hold>
    "$here/../../tools/gui/run-script.sh" "$T" "$here/$1.script" "$bin" "$T/tree" > "$T/$1.out" 2>&1
    rc=$?
    if [ "$rc" != "$2" ] || ! grep -q "$3" "$T/$1.out"; then cat "$T/$1.out"; echo "harness: FAILED ($1: rc $rc)"; fail=1; fi
}
h harness 1 "the window quit before line 7, the script's end not reached"
h harnessb 1 "the window didn't quit"
h harnessc 0 "script: 0 of 0 expectations failed"
h harnessd 1 "the window quit during a wait-for, before the script's end"
fi
exit $fail
