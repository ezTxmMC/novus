#!/usr/bin/env bash
# The self-hosting ladder. Verifies that
#   1. the snapshot (bootstrap/novusc.c) builds and can compile the sources,
#   2. the compiler built from the sources can compile itself,
#   3. the fixpoint holds: the second-generation compiler emits byte-identical
#      C to the one it was built from (stage2.c == stage3.c),
#   4. that C is what is checked in as the snapshot (or a warning otherwise),
#   5. a compiler built from that C by a different C compiler (clang or gcc,
#      whichever family $NOVUS_CC is not; skipped when there is none) emits
#      the same C: the language fixes the evaluation order of operands, so
#      nothing the compiler generates may depend on what the C compiler picks.
#      The C of stage 3 is used, not the snapshot, so a stale snapshot cannot
#      fail this stage.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC="${NOVUS_CC:-cc}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
export NOVUS_CC="$CC"
cd "$ROOT"

LIBS="-lm -lpthread"

$CC -O2 -ffp-contract=off bootstrap/novusc.c -o "$WORK/novusc0" $LIBS
echo "stage 0 ok: snapshot builds ($("$WORK/novusc0" version))"

"$WORK/novusc0" build compiler/main.nv -o "$WORK/novusc1" > /dev/null
echo "stage 1 ok: snapshot compiles the current sources"

"$WORK/novusc1" emit compiler/main.nv -o "$WORK/stage2.c" > /dev/null
$CC -O2 -ffp-contract=off "$WORK/stage2.c" -o "$WORK/novusc2" $LIBS
echo "stage 2 ok: the compiler compiles itself"

"$WORK/novusc2" emit compiler/main.nv -o "$WORK/stage3.c" > /dev/null
cmp "$WORK/stage2.c" "$WORK/stage3.c"
echo "stage 3 ok: FIXPOINT - novusc compiles itself byte-identically ($(wc -l < "$WORK/stage3.c") lines of C)"

if cmp -s "$WORK/stage3.c" bootstrap/novusc.c; then
    echo "snapshot ok: bootstrap/novusc.c is up to date"
else
    echo "note: bootstrap/novusc.c differs from the current sources - run scripts/snapshot.sh"
fi

# The family of a C compiler ("clang" or "gcc"), by what it defines: the name
# of the command says little (cc is either, gcc is clang on macOS).
c_family() { # compiler command words...
    local macros
    macros="$(echo | "$@" -dM -E -x c - 2> /dev/null)" || return 0
    case "$macros" in
        *__clang__*) echo clang ;;
        *__GNUC__*) echo gcc ;;
    esac
}

# A compiler of the other family than $CC, if the machine has one.
other_compiler() {
    local mine candidate
    mine="$(c_family $CC)"
    for candidate in clang gcc; do
        if command -v "$candidate" > /dev/null && [ "$candidate" != "$mine" ] && [ "$(c_family "$candidate")" = "$candidate" ]; then
            echo "$candidate"
            return
        fi
    done
}

OTHER="$(other_compiler)"
if [ -n "$OTHER" ]; then
    "$OTHER" -O1 -w -ffp-contract=off "$WORK/stage3.c" -o "$WORK/novusc-other" $LIBS
    "$WORK/novusc-other" emit compiler/main.nv -o "$WORK/other.c" > /dev/null
    cmp "$WORK/other.c" "$WORK/stage3.c"
    echo "stage 4 ok: built with $OTHER the compiler emits the same C"
else
    echo "stage 4 skipped: no C compiler of a second family"
fi

for example in shapes wordcount todo mccloud; do
    "$WORK/novusc2" check "examples/$example/main.nv" > /dev/null
done
echo "examples ok: compile with the self-built compiler"
