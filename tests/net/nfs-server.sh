#!/bin/sh
# ofx's test NFS server (F16 step 6), run by hand as root: gvfs-nfs reaches a server only through
# the portmapper on port 111, which rootless podman can't publish. Everything else of the net tests
# stays rootless; they use this server when it answers on 127.0.0.1:111, and skip its checks (saying
# so) when it doesn't.
#
#   sudo tests/net/nfs-server.sh start    the image built (root's podman storage) and the container
#                                         started: ofx-test-nfs, ports 111, 2049 and 20048 on
#                                         127.0.0.1 only
#   sudo tests/net/nfs-server.sh stop     the container removed (the image kept)
#   sudo tests/net/nfs-server.sh status
#
# It touches only the container named ofx-test-nfs and the images tagged localhost/ofx-test-nfs.
set -eu
[ "$(id -u)" -eq 0 ] || { echo "nfs-server: run it with sudo (port 111 needs a rootful container)" >&2; exit 2; }
d=$(cd "$(dirname "$0")/nfs" && pwd)
name=ofx-test-nfs
case ${1:-} in
start)
    tag=localhost/ofx-test-nfs:$(cat "$d/Containerfile" "$d/ganesha.conf" "$d/start.sh" | sha1sum | cut -c1-12)
    if ! podman image exists "$tag"; then
        # (on the host's network: root's bridge had no name server here, as the host's resolver is
        # systemd-resolved's stub on 127.0.0.53; 2026-11-10)
        podman build --network host -t "$tag" "$d"
        podman images --format '{{.Repository}}:{{.Tag}}' | grep '^localhost/ofx-test-nfs:' | grep -vx "$tag" | xargs -r podman rmi > /dev/null 2>&1 || true
    fi
    podman rm -f -t 0 "$name" > /dev/null 2>&1 || true
    podman run -d --rm --name "$name" --cap-add DAC_READ_SEARCH --cap-add SYS_RESOURCE --mount type=tmpfs,destination=/srv/nfs,tmpfs-size=16m \
        -p 127.0.0.1:111:111/tcp -p 127.0.0.1:111:111/udp -p 127.0.0.1:2049:2049/tcp -p 127.0.0.1:20048:20048/tcp \
        "$tag" > /dev/null
    for _ in $(seq 100); do   # (until ganesha has registered with rpcbind: up to 20 s)
        if podman exec "$name" rpcinfo -p 2> /dev/null | grep -q mountd; then echo "nfs-server: up ($tag)"; exit 0; fi
        sleep 0.2
    done
    echo "nfs-server: ganesha didn't register; its log:" >&2
    podman logs "$name" 2>&1 | tail -20 >&2
    exit 1 ;;
stop) podman rm -f -t 0 "$name" > /dev/null 2>&1 || true; echo "nfs-server: stopped" ;;
status) podman ps --filter "name=^$name\$" --format '{{.Names}} {{.Status}} {{.Ports}}' ;;
*) echo "usage: sudo $0 start|stop|status" >&2; exit 2 ;;
esac
