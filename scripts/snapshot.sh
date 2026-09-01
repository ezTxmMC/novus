#!/usr/bin/env bash
# Regenerates the bootstrap snapshot (bootstrap/novusc.c) and the embedded
# runtime module (compiler/runtime/runtime.nv) from the current sources, verifying
# the self-hosting fixpoint on the way.
#
# Run this after changing anything under compiler/ or runtime/. The compiler
# used to start the ladder is build/novusc (run scripts/bootstrap.sh first);
# it must understand every feature the current sources use - when adding a
# builtin, regenerate the snapshot before using the builtin in the compiler.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NOVUSC="${NOVUSC:-$ROOT/build/novusc}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$ROOT"

echo "embedding runtime/novus_rt.h into compiler/runtime/runtime.nv"
"$NOVUSC" run tools/embed.nv runtime/novus_rt.h > "$WORK/runtime.nv"
mv "$WORK/runtime.nv" compiler/runtime/runtime.nv

echo "stage A: current sources compiled by $NOVUSC"
"$NOVUSC" build compiler/main.nv -o "$WORK/novuscA" > /dev/null
echo "stage B: compiled by stage A"
"$WORK/novuscA" emit compiler/main.nv -o "$WORK/B.c" > /dev/null
"$WORK/novuscA" build compiler/main.nv -o "$WORK/novuscB" > /dev/null
echo "stage C: compiled by stage B"
"$WORK/novuscB" emit compiler/main.nv -o "$WORK/C.c" > /dev/null
if ! cmp -s "$WORK/B.c" "$WORK/C.c"; then
    echo "error: no fixpoint - stage B and stage C generate different C" >&2
    diff "$WORK/B.c" "$WORK/C.c" | head -20 >&2
    exit 1
fi
cp "$WORK/C.c" bootstrap/novusc.c
cp "$WORK/novuscB" build/novusc
echo "ok: bootstrap/novusc.c updated ($(wc -l < bootstrap/novusc.c) lines), build/novusc refreshed"
