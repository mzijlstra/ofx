#!/bin/sh
# ofx-list as ofx runs it in a test (the cleanup's step A, after F16): the guard (OFX_GUI_ROOT) on a
# scratch folder, so `--do` writes inside it and refuses a decoy beside it. Until 2026-11-10 its main
# never turned the guard on: every far action in the GUI tests and mutants ran unguarded.
#   tests/list/run.sh <ofx-list>
set -u
bin=${1:?usage: run.sh <ofx-list>}
T=$(mktemp -d "${TMPDIR:-/tmp}/ofx-list-test-XXXXXX") || exit 1
D="$T-decoy"
trap 'rm -rf "$T" "$D"' EXIT
mkdir -p "$T/in" "$D"
fails=0
fail() { echo "list: $1"; fails=$((fails + 1)); }
do_() { OFX_GUI_ROOT="$T" "$bin" --do "$@" > "$T/out" 2> "$T/err"; }

# Inside the root: made.
do_ mkdir "$T/in" made
head -1 "$T/out" | grep -q '^ok ' || fail "mkdir inside: $(cat "$T/out" "$T/err")"
[ -d "$T/in/made" ] || fail "mkdir inside: no folder"
# The decoy beside it: refused, nothing made, the guard's reason given.
for a in "mkdir $D x" "mkfile $D y" "symlink $D /etc z"; do
    # shellcheck disable=SC2086
    do_ $a
    head -1 "$T/out" | grep -q '^no ' || fail "$a: not refused: $(cat "$T/out")"
    grep -q "outside" "$T/out" || fail "$a: no reason: $(cat "$T/out")"
done
: > "$D/keep"
do_ rename "$D" keep gone
head -1 "$T/out" | grep -q '^no ' || fail "rename in the decoy: not refused"
[ -e "$D/keep" ] && [ ! -e "$D/gone" ] || fail "rename in the decoy: done"
[ -z "$(ls "$D" | grep -v '^keep$')" ] || fail "the decoy: $(ls "$D")"
echo "list: $fails failed"
[ "$fails" -eq 0 ]
