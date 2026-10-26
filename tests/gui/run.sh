#!/bin/sh
# Every GUI script, each on a fresh scratch tree, each in its own headless weston
# (tools/gui/run-script.sh):
#   tests/gui/run.sh <scratch-dir> [binary] [group...]
# The groups (one or more scripts on one tree): browse big gone watchgap f2 f3 f4 f5 f52 f53 f54
# f6 (f6, f6b) f62 (f62, f62b, f62c) f63 (f63, f63b, f63c) f64 (f64, f64b, f64c, f64d) f7 (f7, f7b) f72 f73 (f73, f73b, f73c, f73d) hang guard3 guard. Named ones only, or all of them; a group
# that fails says "== <script>: FAILED" or "<group>: FAILED" (tools/mutation/gui.py reads it).
set -u
base=${1:?usage: run.sh <scratch-dir> [binary]}
bin=${2:-$(cd "$(dirname "$0")/../.." && pwd)/build/ofx}
shift 2 2>/dev/null || shift $#
only=" $* "
want() { [ "$only" = "  " ] || case $only in *" $1 "*) true ;; *) false ;; esac; }
# Each run removes $base/<name>: an empty or relative base would aim that at / or the cwd.
case $base in /?*) ;; *) echo "run.sh: the scratch dir must be an absolute path: '$base'" >&2; exit 1 ;; esac
here=$(cd "$(dirname "$0")" && pwd)
fail=0
# A run that fails says so by name: a script that can't even start (its tree missing) prints no
# "script: N of M" line of its own (fe01f7c ran f6 that way unseen).
# OFX_GUI_FAIL_FAST=1 (the GUI mutants, tools/mutation/gui.py): the first failed script ends the run,
# and each script ends at its first failure; one is all a mutant needs to count as killed.
failed() { echo "== $1: FAILED"; fail=1; [ -z "${OFX_GUI_FAIL_FAST:-}" ] || exit 1; }
tree() {   # a fresh <T>/tree
    rm -rf "$1" && mkdir -p "$1/tree/adir" "$1/tree/bdir"
    touch "$1/tree/adir/inner.txt" "$1/tree/a.txt" "$1/tree/b2.txt" "$1/tree/.hidden"
    printf '0123456789' > "$1/tree/b10.txt"
}
if want browse; then
T=$base/browse; tree "$T"
echo "== browse"; "$here/../../tools/gui/run-script.sh" "$T" "$here/browse.script" "$bin" "$T/tree" || failed browse
fi
if want big; then
T=$base/big; rm -rf "$T"; mkdir -p "$T/tree"; (cd "$T/tree" && seq -f 'f%g' 0 9999 | xargs touch)
echo "== big"; "$here/../../tools/gui/run-script.sh" "$T" "$here/big.script" "$bin" "$T/tree" || failed big
fi
if want gone; then
T=$base/gone; tree "$T"
echo "== gone"; "$here/../../tools/gui/run-script.sh" "$T" "$here/gone.script" "$bin" "$T/tree" || failed gone
fi
if want watchgap; then
T=$base/watchgap; tree "$T"
echo "== watchgap"; OFX_TEST_WATCH_DELAY_MS=300 "$here/../../tools/gui/run-script.sh" "$T" "$here/watchgap.script" "$bin" "$T/tree" || failed watchgap
fi
if want f2; then
T=$base/f2; rm -rf "$T"; mkdir -p "$T/tree/adir" "$T/tree/bdir" "$T/config/gtk-3.0"
printf '%030d' 0 > "$T/tree/apple.txt"; printf '%010d' 0 > "$T/tree/Banana.txt"; printf '%020d' 0 > "$T/tree/cherry.txt"
printf '%05d' 0 > "$T/tree/.dot"; touch "$T/tree/adir/in-a.txt" "$T/tree/bdir/in-b.txt"
touch -d 2024-01-03 "$T/tree/apple.txt"; touch -d 2024-01-01 "$T/tree/Banana.txt"; touch -d 2024-01-02 "$T/tree/cherry.txt"
touch -d 2024-02-01 "$T/tree/adir"; touch -d 2024-01-15 "$T/tree/bdir"
printf 'file://%s/tree/bdir Bee\nfile://%s/tree Tree\n' "$T" "$T" > "$T/config/gtk-3.0/bookmarks"
echo "== f2"; "$here/../../tools/gui/run-script.sh" "$T" "$here/f2.script" "$bin" "$T/tree" || failed f2
fi
if want f3; then
T=$base/f3; rm -rf "$T"; mkdir -p "$T/tree/adir" "$T/tree/bdir" "$T/data"
touch "$T/tree/adir/inner.txt" "$T/tree/a.txt" "$T/tree/b.txt" "$T/tree/c.txt"
head -c 8388608 /dev/urandom > "$T/tree/big.bin"
echo "== f3"; OFX_TEST_COPY_DELAY_MS=150 "$here/../../tools/gui/run-script.sh" "$T" "$here/f3.script" "$bin" "$T/tree" || failed f3
fi
if want f4; then
T=$base/f4; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/adir" "$H/tree/bdir" "$T/data"
touch "$H/tree/adir/x" "$H/tree/adir/y" "$H/tree/adir/.h" "$H/tree/f.txt"
L="$H/tree"; for i in 1 2 3 4 5 6 7 8; do L="$L/zz-a-rather-long-folder-name-$i"; done; mkdir -p "$L"
echo "== f4"; OFX_TEST_COUNT_DELAY_MS=50 OFX_TEST_HOME="$H" "$here/../../tools/gui/run-script.sh" "$T" "$here/f4.script" "$bin" "$H/tree" || failed f4
fi
if want f5; then
T=$base/f5; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/adir" "$H/tree/bdir" "$H/tree/cdir" "$H/tree/gone" "$T/data"
touch "$H/tree/adir/a1" "$H/tree/adir/a2" "$H/tree/bdir/b1" "$H/tree/gone/g1"
for i in $(seq -w 1 40); do touch "$H/tree/f$i"; done
head -c 8388608 /dev/urandom > "$H/tree/big.bin"
echo "== f5"; OFX_TEST_COPY_DELAY_MS=150 OFX_TEST_HOME="$H" "$here/../../tools/gui/run-script.sh" "$T" "$here/f5.script" "$bin" "$H/tree" || failed f5
fi
if want f52; then
T=$base/f52; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/adir" "$H/tree/bdir" "$T/data"
touch "$H/tree/adir/a1" "$H/tree/notes.txt" "$H/tree/.dot"
head -c 4194304 /dev/urandom > "$H/tree/bdir/big.bin"
L="$H/tree"; for i in 1 2 3 4 5 6 7 8; do L="$L/zz-a-rather-long-folder-name-$i"; done; mkdir -p "$L"
echo "== f52"; OFX_TEST_COPY_DELAY_MS=150 OFX_TEST_CROSS=1 OFX_TEST_HOME="$H" "$here/../../tools/gui/run-script.sh" "$T" "$here/f52.script" "$bin" "$H/tree" || failed f52
fi
if want f53; then
T=$base/f53; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/src" "$H/tree/src2" "$H/tree/adir" "$H/tree/bdir" "$H/tree/cdir" "$H/tree/ddir" "$H/tree/many" "$T/data"
echo d > "$H/tree/ddir/d.txt"
head -c 8388608 /dev/urandom > "$H/tree/src/big1.bin"; head -c 8388608 /dev/urandom > "$H/tree/src2/big2.bin"
echo z > "$H/tree/src/z.txt"; echo z > "$H/tree/src2/z.txt"; echo old > "$H/tree/bdir/z.txt"; echo old > "$H/tree/cdir/z.txt"
for i in $(seq -w 1 10); do touch "$H/tree/many/f$i"; done
echo "== f53"; OFX_TEST_COPY_DELAY_MS=300 OFX_TEST_HOME="$H" "$here/../../tools/gui/run-script.sh" "$T" "$here/f53.script" "$bin" "$H/tree" || failed f53
# Quitting cancelled the running copy and dropped the waiting ones: no part file, no big1.bin.
if ls -A "$H/tree/adir" | grep -q ofx-part || [ -e "$H/tree/adir/big1.bin" ]; then echo "f53: FAILED: quitting left a copy in adir"; ls -A "$H/tree/adir"; fail=1; fi
fi
if want f54; then
T=$base/f54; tree "$T"
echo "== f54"; "$here/../../tools/gui/run-script.sh" "$T" "$here/f54.script" "$bin" "$T/tree" || failed f54
fi
if want f6; then
# F6: a bookmark, a mount list of its own (OFX_TEST_MOUNTINFO), then f6b on the same tree: the
# sidebar's mode came back from the state file.
T=$base/f6; [ -d "$T" ] && chmod -R u+rwx "$T"; rm -rf "$T"   # a failed run can leave f6's locked folder
mkdir -p "$T/tree/adir" "$T/tree/bdir" "$T/config/gtk-3.0" "$T/data"
touch "$T/tree/adir/inner.txt" "$T/tree/b.txt" "$T/tree/c.txt"; printf abc > "$T/tree/a.txt"
printf 'file://%s/tree/bdir Bee\n' "$T" > "$T/config/gtk-3.0/bookmarks"
printf '33 1 0:31 / / rw - btrfs /dev/x rw\n' > "$T/mountinfo"
mkdir -p "$T/many/zz" "$T/media/OFXUSB"; for i in $(seq -w 0 69); do mkdir "$T/many/d$i"; done
head -c 8388608 /dev/zero > "$T/big.bin"
echo "== f6"; OFX_TEST_COPY_DELAY_MS=200 OFX_TEST_MEDIA="$T/media" OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f6.script" "$bin" "$T/tree" || failed f6
# f6b with the drives' space 30 s late (a hung drive): the window answers meanwhile and quits at once
# (under 25 s: with 10 and 9 s it failed with six runs at once, 2026-10-26).
echo "== f6b"; start=$(date +%s)
OFX_TEST_SPACE_DELAY_MS=30000 OFX_TEST_MEDIA="$T/media" OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f6b.script" "$bin" "$T/tree" || failed f6b
took=$(( $(date +%s) - start )); if [ "$took" -ge 25 ]; then cat "$T/times"; echo "f6b: FAILED, took ${took}s (the space thread held the quit?)"; fail=1; else echo "f6b: quit in ${took}s with the space 30s late"; fi
fi
if want f62; then
# F6.2: the icon view, then f62b on the same tree: the view came back from the state file.
T=$base/f62; rm -rf "$T"; mkdir -p "$T/tree/adir" "$T/tree/bdir"
touch "$T/tree/adir/inner.txt" "$T/tree/bdir/bin.txt" "$T/tree/a rather long file name that wraps over lines.txt" \
      "$T/tree/verylongnamewithoutanybreaksatallhere.bin"
for i in $(seq -w 1 20); do touch "$T/tree/f$i"; done
printf '33 1 0:31 / / rw - btrfs /dev/x rw\n' > "$T/mountinfo"
echo "== f62"; OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f62.script" "$bin" "$T/tree" || failed f62
echo "== f62b"; OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f62b.script" "$bin" "$T/tree" || failed f62b
echo "== f62c"; OFX_TEST_ICONS_MAX=2 OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f62c.script" "$bin" "$T/tree" || failed f62c
fi
if want f63; then
# F6.3: the icon view's sizes, then f63b on the same tree: the size came back from the state file.
T=$base/f63; rm -rf "$T"; mkdir -p "$T/tree"
for i in $(seq -w 1 60); do touch "$T/tree/f$i"; done
printf '33 1 0:31 / / rw - btrfs /dev/x rw\n' > "$T/mountinfo"
echo "== f63"; OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f63.script" "$bin" "$T/tree" || failed f63
echo "== f63b"; OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f63b.script" "$bin" "$T/tree" || failed f63b
# A full icon cache at a zoom (reviewer-2, F6.3 review): 24 slots, 12 MIME types.
T=$base/f63c; rm -rf "$T"; mkdir -p "$T/tree"
for f in a.txt b.png c.jpg d.pdf e.c f.py g.sh h.html i.md j.zip k.mp3 l.mp4; do touch "$T/tree/$f"; done
echo "== f63c"; OFX_TEST_ICONS_MAX=24 OFX_TEST_MOUNTINFO="$base/f63/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f63c.script" "$bin" "$T/tree" || failed f63c
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
echo "== f64"; OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f64.script" "$bin" "$T/tree" || failed f64
echo "== f64b"; OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f64b.script" "$bin" "$T/tree" || failed f64b
# 40 previews of 96 by 64 (24 KiB each): f64d where 10 may be held (fewer than a screen's 18), f64c
# where 20 may: scrolling away drops the first ones.
T=$base/f64c; rm -rf "$T"; mkdir -p "$T/tree"
for i in $(seq -w 1 40); do cp "$here/f64/wide.png" "$T/tree/w$i.png"; done
echo "== f64c"; OFX_TEST_THUMB_HELD_MAX=491520 OFX_TEST_MOUNTINFO="$base/f64/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f64c.script" "$bin" "$T/tree" || failed f64c
echo "== f64d"; OFX_TEST_THUMB_HELD_MAX=245760 OFX_TEST_MOUNTINFO="$base/f64/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f64d.script" "$bin" "$T/tree" || failed f64d
fi
if want f7; then
# F7.1: the menu bar. f7: home/tree with a.txt, b.txt, sub.d/, ro/ (read only); f7b: Close All Windows, a copy running.
# HOME and the mount list are scratch: the trash view shows the scratch trash only, never a drive's.
T=$base/f7; rm -rf "$T"; H=$T/home; mkdir -p "$H/tree/sub.d" "$H/tree/ro"; echo a > "$H/tree/a.txt"; echo b > "$H/tree/b.txt"
echo x > "$H/tree/ro/x.txt"; chmod 555 "$H/tree/ro"
printf '1 0 0:1 / / rw - ext4 /dev/x rw\n' > "$T/mountinfo"
# F7.1.1: text/plain's default app in the scratch config, its desktop file in the scratch data home.
mkdir -p "$T/config" "$T/data/applications"
printf '[Default Applications]\ntext/plain=ofx-test-editor.desktop\n' > "$T/config/mimeapps.list"
printf '[Desktop Entry]\nType=Application\nName=Test editor\nExec=true %%f\n' > "$T/data/applications/ofx-test-editor.desktop"
echo "== f7"; OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f7.script" "$bin" "$H/tree" || failed f7
chmod 755 "$H/tree/ro"
T=$base/f7b; rm -rf "$T"; mkdir -p "$T/tree/src" "$T/tree/dst"; head -c 8388608 /dev/urandom > "$T/tree/src/big.bin"
echo "== f7b"; OFX_TEST_COPY_DELAY_MS=300 OFX_TEST_MOUNTINFO="$base/f7/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f7b.script" "$bin" "$T/tree" || failed f7b
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
printf '1 0 0:1 / / rw - ext4 /dev/x rw\n' > "$T/mountinfo"
echo "== f72"; OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f72.script" "$bin" "$H/tree" || failed f72
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
# An SVG over 1 MiB isn't drawn (reviewer-2, F7.3.1): the generic icon.
{ printf '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><!-- '; head -c 1100000 /dev/zero | tr '\0' x; printf ' --><rect width="16" height="16"/></svg>\n'; } > "$T/data/pixmaps/zz-big.svg"
app zz-big "Zz Big Icon" 'Icon=zz-big\n'
# Set as default (F7.3.1): the desktop's list, read first, sets image/png (Omarchy's sets http).
printf '[Default Applications]\nimage/png=ofx-t-view.desktop\n' > "$T/config/hyprland-mimeapps.list"
printf '1 0 0:1 / / rw - ext4 /dev/x rw\n' > "$T/mountinfo"
echo "== f73"; XDG_CURRENT_DESKTOP=Hyprland OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f73.script" "$bin" "$H/tree" || failed f73
echo "== f73b"; OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f73b.script" "$bin" "$H/tree/notes.txt" || failed f73b
# --select (FileManager1's ShowItems): a folder selected in its own folder, and a link where it is
echo "== f73c"; OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f73c.script" "$bin" --select "$H/tree/sub/" || failed f73c
ln -s sub "$H/tree/zlink"
echo "== f73d"; OFX_TEST_HOME="$H" OFX_TEST_MOUNTINFO="$T/mountinfo" "$here/../../tools/gui/run-script.sh" "$T" "$here/f73d.script" "$bin" --select "$H/tree/zlink" || failed f73d
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
