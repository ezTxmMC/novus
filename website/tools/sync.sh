#!/usr/bin/env bash
# Refreshes snapshot/ from a Novus checkout. The site reads only snapshot/ (the
# examples, the benchmark sources and results, the numbers of the landing page), so building it needs no Novus
# source; only this script does.
#
#   tools/sync.sh <path to a novus checkout>
set -eu
SRC="${1:?usage: tools/sync.sh <path to a novus checkout>}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$HERE/snapshot"
for need in examples benchmarks/results.json compiler/project.nv bootstrap/novusc.c; do
    [ -e "$SRC/$need" ] || { echo "sync: $SRC is not a novus checkout ($need is missing)" >&2; exit 1; }
done

rm -rf "$OUT"
mkdir -p "$OUT/examples" "$OUT/benchmarks"

for chapter in "$SRC"/examples/*/; do
    name="$(basename "$chapter")"
    mkdir -p "$OUT/examples/$name"
    for file in "$chapter"*.nv "$chapter"*.golden "$chapter"*.rc; do
        [ -f "$file" ] && cp "$file" "$OUT/examples/$name/"
    done
done

cp "$SRC/benchmarks/results.json" "$OUT/benchmarks/"
for language in novus cpp rust go crystal java node python; do
    [ -d "$SRC/benchmarks/$language" ] || continue
    mkdir -p "$OUT/benchmarks/$language"
    for file in "$SRC/benchmarks/$language"/*; do
        case "$file" in
            *.nv | *.cpp | *.rs | *.go | *.cr | *.java | *.js | *.py) cp "$file" "$OUT/benchmarks/$language/" ;;
        esac
    done
done

# lines of the hand written compiler: the embedded runtime and std are generated
version="$(sed -n 's/^version "\([^"]*\)".*/\1/p' "$SRC/compiler/project.nv" | head -n 1)"
compiler_lines="$(find "$SRC/compiler" -name '*.nv' ! -name runtime.nv ! -name stdlib.nv -exec cat {} + | wc -l | tr -d ' ')"
snapshot_lines="$(wc -l < "$SRC/bootstrap/novusc.c" | tr -d ' ')"
printf '{ "version": "%s", "compilerLines": %s, "snapshotLines": %s }\n' "${version:-0.1.0}" "$compiler_lines" "$snapshot_lines" > "$OUT/stats.json"
echo "sync: snapshot/ refreshed from $SRC"
