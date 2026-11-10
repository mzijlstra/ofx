# The network tests' server and gvfs (F16), sourced by tests/net/run.sh (make test-net) and
# tests/gui/run.sh (the net group).
#
# The server: a Samba container, built from tests/net/Containerfile, on 127.0.0.1 only, named
# ofx-test-<pid>. podman always runs with the real HOME and XDG_RUNTIME_DIR (its storage and state:
# with scratch ones it starts a pause process of its own), and touches only that container.
#
# gvfs: a session of the test's own (dbus-run-session with a bus config whose only service is gvfsd;
# the default one started a gnome-keyring), XDG_RUNTIME_DIR and HOME under the scratch folder, so
# gvfsd-fuse mounts there. The desktop's gvfs, /run/user/$UID/gvfs and keyring are never reached.
#
#   net_ok                      true when podman, gio, dbus-run-session and gvfs-smb are there (else
#                               says what is missing)
#   net_server <scratch>        the image built if needed, the container started: NET_C, NET_PORT
#   net_pause / net_unpause     the server stops answering / answers again
#   net_session <scratch> <cmd> runs <cmd> (a file, sh) in the private session, waiting for it
#   net_hold <scratch> <share>  the private session started in the background with <share> mounted
#                               (guest): NET_SHARE its path; it stays until net_release
#   net_release                 the session ended (its gvfs with it: nothing stays mounted)
#   net_end                     the container removed, the session ended (the callers' trap)
#   net_nfs <scratch>           true when the NFS checks can run (else says why not): the NFS test
#                               server (tests/net/nfs-server.sh, which takes root) answers
#
# The NFS server isn't started here: gvfs-nfs finds a server's ports only through its portmapper, on
# 111, which takes root to publish. `sudo tests/net/nfs-server.sh start` runs one (nfs-ganesha, its
# export /srv/nfs, on 127.0.0.1 only) until `stop`.

NET_D=$(cd "$(dirname "${NET_LIB:-$0}")" && pwd)
NET_REAL_HOME=$HOME NET_REAL_RT=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
NET_C= NET_PORT= NET_B= NET_HOLD_PID= NET_FIFO=

# podman with only what it needs of the real environment: under run-script the XDG folders are the
# run's, and podman keeps its storage and config there (XDG_DATA_HOME, XDG_CONFIG_HOME).
net_pm() { env -i HOME="$NET_REAL_HOME" XDG_RUNTIME_DIR="$NET_REAL_RT" PATH=/usr/bin podman "$@"; }

net_ok() {
    for p in podman gio dbus-run-session; do
        command -v "$p" > /dev/null || { echo "net: skipped, $p isn't installed"; return 1; }
    done
    [ -x /usr/lib/gvfsd-smb ] || { echo "net: skipped, gvfs-smb isn't installed"; return 1; }
}

net_server() {
    tag=localhost/ofx-test-smb:$(cat "$NET_D/Containerfile" "$NET_D/smb.conf" | sha1sum | cut -c1-12)
    if ! net_pm image exists "$tag"; then
        net_pm build -q -t "$tag" "$NET_D" > /dev/null || return 1
        # Its older builds go (named for the files they were built from; only this repository's tags).
        net_pm images --format '{{.Repository}}:{{.Tag}}' | grep '^localhost/ofx-test-smb:' | grep -vx "$tag" |
            xargs -r env -i HOME="$NET_REAL_HOME" XDG_RUNTIME_DIR="$NET_REAL_RT" PATH=/usr/bin podman rmi > /dev/null 2>&1 || true
    fi
    NET_C=ofx-test-$$
    net_pm run -d --rm --name "$NET_C" -p 127.0.0.1::445 "$tag" > /dev/null || return 1
    NET_PORT=$(net_pm port "$NET_C" 445/tcp | sed 's/.*://')
    for _ in $(seq 100); do   # (until smbd listens: up to 10 s; it logs no "ready" line)
        net_pm exec "$NET_C" netstat -ltn 2> /dev/null | grep -q ':445 ' && return 0
        sleep 0.1
    done
    echo "net: the server didn't start" >&2
    return 1
}

net_pause() { net_pm pause "$NET_C" > /dev/null; }
net_unpause() { net_pm unpause "$NET_C" > /dev/null; }

# The private session's bus config, its runtime folder and home under $1; its socket in a short
# folder of its own (a unix socket's path is at most 107 bytes).
net_bus() {
    mkdir -p "$1/rt" "$1/home" "$1/services"
    chmod 700 "$1/rt"
    cp /usr/share/dbus-1/services/org.gtk.vfs.Daemon.service "$1/services/"
    [ -n "$NET_B" ] || NET_B=$(mktemp -d "${TMPDIR:-/tmp}/ofx-net-XXXXXX")
    # (gvfsd-nfs has a file capability, so GLib ignores DBUS_SESSION_BUS_ADDRESS there and looks for
    # the bus only at $XDG_RUNTIME_DIR/bus, measured 2026-11-10)
    ln -sfn "$NET_B/bus" "$1/rt/bus"
    cat > "$1/bus.conf" <<EOF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:path=$NET_B/bus</listen>
  <servicedir>$1/services</servicedir>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
EOF
}

# The private session's environment, for env -i (the commands inside check they are in it).
net_env() {
    echo HOME="$1/home" XDG_RUNTIME_DIR="$1/rt" PATH=/usr/bin LANG=C.UTF-8 S="$1" B="$NET_B" PORT="$NET_PORT" C="$NET_C" \
        REAL_HOME="$NET_REAL_HOME" REAL_RT="$NET_REAL_RT"
}

net_session() {
    net_bus "$1"
    # shellcheck disable=SC2046
    env -i $(net_env "$1") timeout 180 dbus-run-session --config-file="$1/bus.conf" -- sh "$2"
}

net_hold() {
    net_bus "$1"
    NET_FIFO=$1/hold
    rm -f "$NET_FIFO" "$1/held"
    mkfifo "$NET_FIFO"
    cat > "$1/hold.sh" <<'EOF'
case $DBUS_SESSION_BUS_ADDRESS in "unix:path=$B/bus"*) ;; *) echo "net: not the test's bus" >&2; exit 3 ;; esac
timeout 20 gio mount -a "smb://127.0.0.1:$PORT/$SHARE" < /dev/null || exit 1
: > "$S/held"
read -r _ < "$S/hold"   # (until net_release writes, or its end closes the fifo)
timeout 20 gio mount -u "smb://127.0.0.1:$PORT/$SHARE" > /dev/null 2>&1
EOF
    # shellcheck disable=SC2046
    env -i $(net_env "$1") SHARE="$2" timeout 900 dbus-run-session --config-file="$1/bus.conf" -- sh "$1/hold.sh" > "$1/hold.log" 2>&1 &
    NET_HOLD_PID=$!
    exec 9> "$NET_FIFO"   # (held open here: the reader's read waits for a line, not an end)
    for _ in $(seq 100); do
        [ -e "$1/held" ] && { NET_SHARE="$1/rt/gvfs/smb-share:port=$NET_PORT,server=127.0.0.1,share=$2"; return 0; }
        kill -0 "$NET_HOLD_PID" 2> /dev/null || break
        sleep 0.1
    done
    echo "net: the share wasn't mounted (log: $1/hold.log)" >&2
    return 1
}

net_release() {
    [ -n "$NET_HOLD_PID" ] || return 0
    echo done >&9 2> /dev/null
    exec 9>&-
    wait "$NET_HOLD_PID" 2> /dev/null
    NET_HOLD_PID=
}

net_nfs() {
    [ -x /usr/lib/gvfsd-nfs ] || { echo "nfs: skipped, gvfs-nfs isn't installed"; return 1; }
    # (a socket's path holds 107 bytes: the bus at <scratch>/rt/bus must fit)
    [ "${#1}" -le 100 ] || { echo "nfs: skipped, the scratch folder's path is too long for the bus's socket under it (${#1} bytes, 100 at most)"; return 1; }
    timeout 1 bash -c ': < /dev/tcp/127.0.0.1/2049' 2> /dev/null && timeout 1 bash -c ': < /dev/tcp/127.0.0.1/111' 2> /dev/null ||
        { echo "nfs: skipped, no NFS test server on 127.0.0.1 (sudo tests/net/nfs-server.sh start)"; return 1; }
}

net_end() {   # (every step, whatever the one before did: it is the trap, and set -e is off in it)
    set +e
    [ -z "$NET_C" ] || net_unpause > /dev/null 2>&1   # (a paused server would hold the unmount)
    net_release
    [ -z "$NET_C" ] || net_pm rm -f -t 0 "$NET_C" > /dev/null 2>&1
    [ -z "$NET_B" ] || rm -rf "$NET_B"
    return 0
}
