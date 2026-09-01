#!/usr/bin/env bash
# Builds novusc from the checked-in C snapshot - the only requirement is a
# C compiler (cc / gcc / clang / zig cc).
#
#   stage0: bootstrap/novusc.c  -> build/novusc0   (the snapshot)
#   stage1: compiler/*.nv       -> build/novusc1   (current sources, built by stage0)
#   stage2: compiler/*.nv       -> build/novusc    (built by stage1, i.e. by itself)
#
# Environment: NOVUS_CC (C compiler, default cc), NOVUS_CFLAGS (extra flags),
# NOVUS_OUT (output directory, default build/).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${NOVUS_OUT:-$ROOT/build}"
CC="${NOVUS_CC:-cc}"
CFLAGS="${NOVUS_CFLAGS:-}"
export NOVUS_CC="$CC"
export NOVUS_CFLAGS="$CFLAGS"
EXE=""
LIBS="-lm"
case "$(uname -s 2>/dev/null || echo unknown)" in
    MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;;
    *) LIBS="$LIBS -lpthread" ;;   # threads and virtual threads
esac

mkdir -p "$OUT"
echo "stage0: $CC bootstrap/novusc.c -> $OUT/novusc0$EXE"
$CC -O2 $CFLAGS "$ROOT/bootstrap/novusc.c" -o "$OUT/novusc0$EXE" $LIBS

echo "stage1: compiling compiler/main.nv with the snapshot"
"$OUT/novusc0$EXE" build "$ROOT/compiler/main.nv" -o "$OUT/novusc1$EXE" > /dev/null

echo "stage2: compiling compiler/main.nv with stage1"
"$OUT/novusc1$EXE" build "$ROOT/compiler/main.nv" -o "$OUT/novusc$EXE" > /dev/null

echo "ok: $OUT/novusc$EXE ($("$OUT/novusc$EXE" version))"
