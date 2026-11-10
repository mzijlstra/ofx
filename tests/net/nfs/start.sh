#!/bin/sh
# The export's files (on the tmpfs the run mounts at /srv/nfs), rpcbind (ganesha registers its NFS
# and mountd ports with it), then ganesha in the foreground.
mkdir -p /srv/nfs/sub && echo nfs > /srv/nfs/hello.txt && for i in $(seq 1 20); do : > /srv/nfs/sub/f$i; done && chmod -R a+rwX /srv/nfs || exit 1
rpcbind -w || exit 1
exec ganesha.nfsd -F -L /dev/stdout -f /etc/ganesha/ganesha.conf
