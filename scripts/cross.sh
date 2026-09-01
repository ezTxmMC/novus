#!/usr/bin/env bash
# Cross compiles novusc for every supported platform from one machine using
# zig's bundled clang (https://ziglang.org - any recent release works).
#
#   scripts/cross.sh              -> dist/novusc-<target>[.exe]
#   scripts/cross.sh x86_64-macos -> only that target
#
# Environment: NOVUS_ZIG (zig executable, default: zig from PATH),
# NOVUSC (compiler used to emit the C, default build/novusc).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ZIG="${NOVUS_ZIG:-zig}"
NOVUSC="${NOVUSC:-$ROOT/build/novusc}"
DIST="${NOVUS_DIST:-$ROOT/dist}"
TARGETS="${*:-x86_64-linux-gnu.2.17 aarch64-linux-gnu.2.17 x86_64-linux-musl x86_64-windows-gnu aarch64-windows-gnu x86_64-macos aarch64-macos}"

command -v "$ZIG" > /dev/null || { echo "error: zig not found (set NOVUS_ZIG)" >&2; exit 1; }
mkdir -p "$DIST"
"$NOVUSC" emit "$ROOT/compiler/main.nv" -o "$DIST/novusc.c" > /dev/null

for target in $TARGETS; do
    name="novusc-${target%%.*}"
    libs="-lm"
    case "$target" in
        *windows*) name="$name.exe" ;;
        *) libs="$libs -lpthread" ;;
    esac
    echo "$target -> dist/$name"
    "$ZIG" cc -target "$target" -O2 -s "$DIST/novusc.c" -o "$DIST/$name" $libs
    rm -f "$DIST/${name%.exe}.pdb" "$DIST/$name.pdb"
done
rm -f "$DIST/novusc.c"
ls -la "$DIST"
