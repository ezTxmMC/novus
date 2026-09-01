#!/usr/bin/env bash
# The self-hosting ladder. Verifies that
#   1. the snapshot (bootstrap/novusc.c) builds and can compile the sources,
#   2. the compiler built from the sources can compile itself,
#   3. the fixpoint holds: the second-generation compiler emits byte-identical
#      C to the one it was built from (stage2.c == stage3.c),
#   4. that C is what is checked in as the snapshot (or a warning otherwise).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC="${NOVUS_CC:-cc}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
export NOVUS_CC="$CC"
cd "$ROOT"

$CC -O2 bootstrap/novusc.c -o "$WORK/novusc0" -lm
echo "stage 0 ok: snapshot builds ($("$WORK/novusc0" version))"

"$WORK/novusc0" build compiler/main.nv -o "$WORK/novusc1" > /dev/null
echo "stage 1 ok: snapshot compiles the current sources"

"$WORK/novusc1" emit compiler/main.nv -o "$WORK/stage2.c" > /dev/null
$CC -O2 "$WORK/stage2.c" -o "$WORK/novusc2" -lm
echo "stage 2 ok: the compiler compiles itself"

"$WORK/novusc2" emit compiler/main.nv -o "$WORK/stage3.c" > /dev/null
cmp "$WORK/stage2.c" "$WORK/stage3.c"
echo "stage 3 ok: FIXPOINT - novusc compiles itself byte-identically ($(wc -l < "$WORK/stage3.c") lines of C)"

if cmp -s "$WORK/stage3.c" bootstrap/novusc.c; then
    echo "snapshot ok: bootstrap/novusc.c is up to date"
else
    echo "note: bootstrap/novusc.c differs from the current sources - run scripts/snapshot.sh"
fi

for example in shapes wordcount todo mccloud; do
    "$WORK/novusc2" check "examples/$example/main.nv" > /dev/null
done
echo "examples ok: compile with the self-built compiler"
