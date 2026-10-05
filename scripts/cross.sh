#!/usr/bin/env bash
# Cross compiles novusc and the language server novus-lsp for every supported platform from one machine
# using zig's bundled clang (https://ziglang.org - any recent release works).
#
#   scripts/cross.sh              -> dist/novusc-<target>[.exe] and dist/novus-lsp-<target>[.exe]
#   scripts/cross.sh x86_64-macos -> only that target
#
# Environment:
#   NOVUS_ZIG      zig executable, default: zig from PATH
#   NOVUSC         compiler used to emit the C, default build/novusc
#   NOVUS_DIST     output directory, default dist
#   NOVUS_SRC      main file of the first program, default compiler/main.nv
#   NOVUS_NAME     its name in dist/, default novusc
#   NOVUS_LSP_SRC  main file of the second program, default lsp/main.nv; empty skips it
#   NOVUS_LSP_NAME its name in dist/, default novus-lsp
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ZIG="${NOVUS_ZIG:-zig}"
NOVUSC="${NOVUSC:-$ROOT/build/novusc}"
DIST="${NOVUS_DIST:-$ROOT/dist}"
SRC="${NOVUS_SRC:-compiler/main.nv}"
NAME="${NOVUS_NAME:-novusc}"
SECOND_SRC="${NOVUS_LSP_SRC-lsp/main.nv}"
SECOND_NAME="${NOVUS_LSP_NAME:-novus-lsp}"
TARGETS="${*:-x86_64-linux-gnu.2.17 aarch64-linux-gnu.2.17 x86_64-linux-musl x86_64-windows-gnu aarch64-windows-gnu x86_64-macos aarch64-macos}"

command -v "$ZIG" > /dev/null || { echo "error: zig not found (set NOVUS_ZIG)" >&2; exit 1; }
mkdir -p "$DIST"

# build_program <source relative to the repository> <name>: emits the C once, then compiles it for every target.
build_program() {
    local source="$1" base="$2" c_file="$DIST/$2.c" target name libs
    "$NOVUSC" emit "$ROOT/$source" -o "$c_file" > /dev/null
    for target in $TARGETS; do
        name="$base-${target%%.*}"
        libs="-lm"
        case "$target" in
            *windows*) name="$name.exe" ;;
            *) libs="$libs -lpthread" ;;
        esac
        echo "$target -> dist/$name"
        "$ZIG" cc -target "$target" -O2 -ffp-contract=off -s "$c_file" -o "$DIST/$name" $libs
        rm -f "$DIST/${name%.exe}.pdb" "$DIST/$name.pdb"
    done
    rm -f "$c_file"
}

build_program "$SRC" "$NAME"
if [ -n "$SECOND_SRC" ]; then
    build_program "$SECOND_SRC" "$SECOND_NAME"
fi
ls -la "$DIST"
