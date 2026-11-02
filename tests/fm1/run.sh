#!/bin/sh
# ofx-fm1 end to end (2026-10-26): a private session bus (dbus-run-session with its own config,
# whose only service folder is the test's), so no call reaches Michael's bus and Nemo's service is
# never seen; the bus starts ofx-fm1 from the test's service file. uwsm-app and ofx are logging
# fakes and the session has no WAYLAND_DISPLAY: nothing can open a window.
#
#   tests/fm1/run.sh <absolute scratch> <absolute ofx-fm1>
set -eu
S=$1 BIN=$2
case $S in /*) ;; *) echo "fm1: the scratch folder must be absolute" >&2; exit 2 ;; esac
rm -rf "$S"
# The bus's socket in a short folder of its own: a unix socket's path is at most 107 bytes, and a
# long scratch path overran it (reviewer-2).
B=$(mktemp -d "${TMPDIR:-/tmp}/ofx-fm1-XXXXXX")
trap 'rm -rf "$B"' EXIT
mkdir -p "$S/services" "$S/fakes" "$S/t/sub dir" "$S/t/b"
: > "$S/t/sub dir/a b.txt"
: > "$S/t/sub dir/c.txt"
: > "$S/t/b/d.txt"

cat > "$S/fakes/uwsm-app" <<EOF
#!/bin/sh
printf '%s\n' "\$*" >> "$S/spawn.log"
EOF
cat > "$S/fakes/ofx" <<EOF
#!/bin/sh
echo "real ofx run: \$*" >> "$S/spawn.log"
EOF
chmod +x "$S/fakes/uwsm-app" "$S/fakes/ofx"
cat > "$S/services/org.freedesktop.FileManager1.service" <<EOF
[D-BUS Service]
Name=org.freedesktop.FileManager1
Exec=$BIN
EOF
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

# Inside the private session: each call, then a second ofx-fm1 (the name is taken: it exits 1).
cat > "$S/calls.sh" <<'EOF'
set -u
case $DBUS_SESSION_BUS_ADDRESS in "unix:path=$B/bus"*) ;; *) echo "fm1: not the test's bus: $DBUS_SESSION_BUS_ADDRESS" >&2; exit 3 ;; esac
call() { gdbus call --session --timeout 5 --dest org.freedesktop.FileManager1 --object-path /org/freedesktop/FileManager1 --method "org.freedesktop.FileManager1.$1" "$2" "''" > /dev/null; }
T=$S/t
call ShowItems "['file://$T/sub%20dir/a%20b.txt']"
call ShowFolders "['file://$T/b', 'file://$T/b', 'file://$T/gone', 'sftp://host/x', 'file://$T/sub%20dir']"
call ShowItems "['file://$T/sub%20dir/c.txt', 'file://$T/sub%20dir/a%20b.txt', 'file://$T/b/d.txt']"
call ShowItemProperties "['file://$T/b/d.txt']"
"$BIN" 2> "$S/second.err"; echo "second: $?" >> "$S/spawn.log"
EOF

# Neither Michael's bus address nor his runtime folder (where sd-bus would look for it) reaches the
# session: a service the bus starts inherits the daemon's environment.
mkdir -p "$S/run" && chmod 700 "$S/run"
env -u WAYLAND_DISPLAY -u DISPLAY -u DBUS_SESSION_BUS_ADDRESS XDG_RUNTIME_DIR="$S/run" PATH="$S/fakes:$PATH" OFX_FM1_OFX="$S/fakes/ofx" S="$S" B="$B" BIN="$BIN" \
    ASAN_OPTIONS="log_path=$S/asan" \
    dbus-run-session --config-file="$S/bus.conf" -- sh "$S/calls.sh"
for i in $(seq 30); do pgrep -fx "$BIN" > /dev/null || break; sleep 0.1; done   # the bus is gone: ofx-fm1 sees it and exits (3 s at most: a load made 0.3 s a false failure, F11 review)

# Sorted: each launch is detached, so the lines come in no set order. ShowItems on a file: its
# folder's window, it selected (--select); the same folder twice, a path not there or not file://, once
# or not at all; ShowItemProperties as ShowItems.
X=$S/fakes/ofx T=$S/t
sort > "$S/want" <<EOF
-- $X --select $T/sub dir/a b.txt
-- $X $T/b
-- $X $T/sub dir
-- $X --select $T/sub dir/c.txt
-- $X --select $T/b/d.txt
-- $X --select $T/b/d.txt
second: 1
EOF
sort "$S/spawn.log" > "$S/got"
fail=0
if ! cmp -s "$S/want" "$S/got"; then echo "fm1: the windows asked for differ:"; diff "$S/want" "$S/got" || true; fail=1; fi
if pgrep -fx "$BIN" > /dev/null; then echo "fm1: ofx-fm1 still running after the bus went"; pkill -fx "$BIN"; fail=1; fi
if ls "$S"/asan.* > /dev/null 2>&1; then echo "fm1: asan reports:"; cat "$S"/asan.*; fail=1; fi
[ $fail = 0 ] && echo "fm1: 3 checks passed (windows, exit, asan)"
exit $fail
