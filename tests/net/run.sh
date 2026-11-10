#!/bin/sh
# Network shares end to end (F16): mount, list, read and write a Samba share through a gvfs of the
# test's own, a password share, and a server that stops answering. tests/net/lib.sh has the server
# and the private session, and what they keep away from.
#
#   tests/net/run.sh <absolute scratch named net-test>
set -eu
S=$1
case $S in /*/net-test | /*/net-test-*) ;; *) echo "net: the scratch folder must be absolute and named net-test: '$S'" >&2; exit 2 ;; esac
NET_LIB=$0
. "$(dirname "$0")/lib.sh"
net_ok || exit 0
rm -rf "$S"
mkdir -p "$S"
trap net_end EXIT
trap 'exit 130' INT TERM
net_server "$S" || exit 1

# Inside the private session.
cat > "$S/checks.sh" <<'EOF'
set -u
case $DBUS_SESSION_BUS_ADDRESS in "unix:path=$B/bus"*) ;; *) echo "net: not the test's bus: $DBUS_SESSION_BUS_ADDRESS" >&2; exit 3 ;; esac
case $XDG_RUNTIME_DIR in "$S/rt") ;; *) echo "net: not the test's runtime folder: $XDG_RUNTIME_DIR" >&2; exit 3 ;; esac
pm() { env -i HOME="$REAL_HOME" XDG_RUNTIME_DIR="$REAL_RT" PATH=/usr/bin podman "$@"; }
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
check "a read blocks on a dead server" test "$(state $L)" = S   # (if it ever flakes: S or D, never "not gone"; review)
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

# NFS (F16 step 6), when the NFS test server runs (lib.sh's net_nfs): gvfs asks nothing, mounts the
# export holding the path, and names its folder by the export.
if [ -e "$S/nfs" ]; then
    NFS="$G/nfs:host=127.0.0.1,prefix=%2Fsrv%2Fnfs"
    check "nfs: a folder's address mounts its export" timeout 20 gio mount nfs://127.0.0.1/srv/nfs/sub < /dev/null
    check "nfs: its FUSE path" test -d "$NFS"
    check "nfs: gio info names the folder in it" sh -c "gio info nfs://127.0.0.1/srv/nfs/sub 2> /dev/null | grep -qxF 'local path: $NFS/sub'"
    check "nfs: its files" test "$(ls "$NFS/sub" | wc -l)" -eq 20
    check "nfs: a read" grep -qx nfs "$NFS/hello.txt"
    check "nfs: a write, as the user" sh -c "echo new > '$NFS/written-$$.txt' && grep -qx new '$NFS/written-$$.txt' && test \"\$(stat -c %u '$NFS/written-$$.txt')\" = $(id -u) && rm '$NFS/written-$$.txt'"
    check "nfs: the export again: already mounted" sh -c "timeout 20 gio mount nfs://127.0.0.1/srv/nfs < /dev/null 2>&1 | grep -q 'already mounted'"
    check "nfs: a path not exported" sh -c "timeout 20 gio mount nfs://127.0.0.1/nope < /dev/null 2>&1 | grep -q 'Mount point does not exist'"
    check "nfs: unmount by its folder" timeout 20 gio mount -u "$NFS"
fi
check "none left" test -z "$(ls "$G")"
echo "net: $n checks, $bad failed"
[ $bad -eq 0 ]
EOF

if net_nfs "$S"; then : > "$S/nfs"; fi
rc=0
net_session "$S" "$S/checks.sh" > "$S/session.log" 2>&1 || rc=$?
grep "^net: " "$S/session.log" || true
# The session's end stops its gvfs: nothing of it is left mounted.
if grep -q " $S/" /proc/self/mountinfo; then echo "net: FAILED: a mount under $S is left"; rc=1; fi
[ $rc -eq 0 ] || { echo "net: the session's log: $S/session.log"; exit 1; }
rm -rf "$S"
