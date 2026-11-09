#!/bin/sh
# Network shares end to end (F16): a Samba server in a rootless podman container on 127.0.0.1 (an
# image built here from tests/net/Containerfile), and a gvfs of the test's own: a private session
# bus (dbus-run-session with its own config) whose only service is gvfsd, with XDG_RUNTIME_DIR and
# HOME in the scratch folder. So gvfsd-fuse mounts under the scratch folder, and the desktop's gvfs,
# its /run/user/$UID/gvfs and its keyring are never reached (with the default config the private
# session started a gnome-keyring: F16 spike). podman itself runs with the real HOME and
# XDG_RUNTIME_DIR, its storage and state; it touches only the container named for this run.
#
#   tests/net/run.sh <absolute scratch named net-test>
set -eu
S=$1
case $S in /*/net-test | /*/net-test-*) ;; *) echo "net: the scratch folder must be absolute and named net-test: '$S'" >&2; exit 2 ;; esac
for p in podman gio dbus-run-session; do
    command -v "$p" > /dev/null || { echo "net: skipped, $p isn't installed"; exit 0; }
done
[ -x /usr/lib/gvfsd-smb ] || { echo "net: skipped, gvfs-smb isn't installed"; exit 0; }
D=$(cd "$(dirname "$0")" && pwd)
rm -rf "$S"
mkdir -p "$S/rt" "$S/home" "$S/services"
chmod 700 "$S/rt"
REAL_HOME=$HOME REAL_RT=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
pm() { env HOME="$REAL_HOME" XDG_RUNTIME_DIR="$REAL_RT" podman "$@"; }

# The image, named for its files' contents: a change to them builds a new one.
TAG=localhost/ofx-test-smb:$(cat "$D/Containerfile" "$D/smb.conf" | sha1sum | cut -c1-12)
pm image exists "$TAG" || pm build -q -t "$TAG" "$D" > /dev/null
C=ofx-test-$$
# The bus's socket in a short folder of its own (a unix socket's path is at most 107 bytes).
B=$(mktemp -d "${TMPDIR:-/tmp}/ofx-net-XXXXXX")
trap 'pm rm -f -t 0 "$C" > /dev/null 2>&1; rm -rf "$B"' EXIT
trap 'exit 130' INT TERM
pm run -d --rm --name "$C" -p 127.0.0.1::445 "$TAG" > /dev/null
PORT=$(pm port "$C" 445/tcp | sed 's/.*://')
for _ in $(seq 50); do   # (smbd says when it serves: up to 5 s)
    pm logs "$C" 2>&1 | grep -q "ready to serve connections" && break
    sleep 0.1
done

cp /usr/share/dbus-1/services/org.gtk.vfs.Daemon.service "$S/services/"
cat > "$S/bus.conf" <<EOF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:path=$B/bus</listen>
  <servicedir>$S/services</servicedir>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
EOF

# Inside the private session.
cat > "$S/checks.sh" <<'EOF'
set -u
case $DBUS_SESSION_BUS_ADDRESS in "unix:path=$B/bus"*) ;; *) echo "net: not the test's bus: $DBUS_SESSION_BUS_ADDRESS" >&2; exit 3 ;; esac
case $XDG_RUNTIME_DIR in "$S/rt") ;; *) echo "net: not the test's runtime folder: $XDG_RUNTIME_DIR" >&2; exit 3 ;; esac
pm() { env HOME="$REAL_HOME" XDG_RUNTIME_DIR="$REAL_RT" podman "$@"; }
n=0 bad=0
check() {   # check <what> <command...>: counted; said when it fails
    what=$1; shift
    n=$((n + 1))
    "$@" > /dev/null 2>&1 || { bad=$((bad + 1)); echo "net: FAILED: $what"; }
}
state() { awk '{print $3}' "/proc/$1/stat" 2> /dev/null || echo gone; }
U=smb://127.0.0.1:$PORT
G=$XDG_RUNTIME_DIR/gvfs
PUB="$G/smb-share:port=$PORT,server=127.0.0.1,share=pub"
PRIV="$G/smb-share:port=$PORT,server=127.0.0.1,share=priv"

# A guest share: mounted (-a: anonymous), listed and written through its FUSE path.
check "guest mount" timeout 20 gio mount -a "$U/pub" < /dev/null
check "its FUSE path" test -d "$PUB"
check "its files" test "$(ls "$PUB/sub" | wc -l)" -eq 50
check "a read" grep -qx hi "$PUB/hello.txt"
check "a write" sh -c "echo new > '$PUB/written.txt' && grep -qx new '$PUB/written.txt'"
check "under the test's gvfs only" grep -q " $G .* - fuse.gvfsd-fuse " /proc/self/mountinfo

# A share with a password: user, domain, password on stdin. A wrong one isn't mounted.
check "wrong password refused" sh -c "! printf 'tester\nWORKGROUP\nwrong\n' | timeout 20 gio mount '$U/priv'"
check "not mounted" test ! -e "$PRIV"
check "password mount" sh -c "printf 'tester\nWORKGROUP\nsecret\n' | timeout 20 gio mount '$U/priv'"
check "its file" grep -qx p "$PRIV/p.txt"

# A server that stops answering (paused): a read blocks. SIGKILL doesn't end it at once: a FUSE
# request already with gvfsd waits for its answer (D, request_wait_answer; about 20 s until gvfsd
# gave up, measured 2026-11-09). So a reader on a share must be a child ofx doesn't wait for, never
# a thread of its own (F16 step 1). With the server back, the killed reader ends at once.
pm pause "$C" > /dev/null
ls "$PUB" > /dev/null 2>&1 &
L=$!
sleep 2
check "a read blocks on a dead server" test "$(state $L)" = S
kill -9 $L 2> /dev/null
sleep 0.5
check "SIGKILL leaves it waiting for gvfsd" test "$(state $L)" = D
pm unpause "$C" > /dev/null
ended() { for _ in $(seq 50); do case $(state $L) in Z | gone) return 0 ;; esac; sleep 0.1; done; return 1; }
check "it ends once the server answers" ended   # (Z: ended, not yet reaped)
wait $L 2> /dev/null || true
check "answers again" timeout 10 ls "$PUB"

check "unmount pub" timeout 20 gio mount -u "$U/pub"
check "unmount priv" timeout 20 gio mount -u "$U/priv"
check "none left" test -z "$(ls "$G")"
echo "net: $n checks, $bad failed"
[ $bad -eq 0 ]
EOF

rc=0
env -i HOME="$S/home" XDG_RUNTIME_DIR="$S/rt" PATH=/usr/bin LANG=C.UTF-8 \
    S="$S" B="$B" PORT="$PORT" C="$C" REAL_HOME="$REAL_HOME" REAL_RT="$REAL_RT" \
    timeout 180 dbus-run-session --config-file="$S/bus.conf" -- sh "$S/checks.sh" > "$S/session.log" 2>&1 || rc=$?
grep "^net: " "$S/session.log" || true
# The session's end stops its gvfs: nothing of it is left mounted.
if grep -q " $S/" /proc/self/mountinfo; then echo "net: FAILED: a mount under $S is left"; rc=1; fi
[ $rc -eq 0 ] || { echo "net: the session's log: $S/session.log"; exit 1; }
rm -rf "$S"
