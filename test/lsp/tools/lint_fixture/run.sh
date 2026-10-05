#!/usr/bin/env bash
# Self-test of scripts/lsp_lint.sh: a tree that breaks every rule (bad/), a tree with a dependency cycle and a
# clean tree. The generated parts (a folder with 11 files, a file and a class above 200 lines, a function above 50
# lines) are made here instead of being committed. The expected output is bad.expected / cycle.expected.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
failed=0

compare() { # name actual expected
    if diff -u "$3" "$2" > "$WORK/diff"; then
        echo "ok   lint-fixture $1"
        return
    fi
    echo "FAIL lint-fixture $1"
    head -40 "$WORK/diff"
    failed=1
}

generate_bad_tree() {
    mkdir -p "$WORK/bad/crowded/many"
    cp -R "$HERE/bad/." "$WORK/bad/"
    # the sources are stored as .nv.txt so that no tool that scans the repository for .nv files trips over them
    local stored
    while IFS= read -r stored; do
        mv "$stored" "${stored%.txt}"
    done < <(find "$WORK/bad" -name '*.nv.txt')
    local index
    for index in 01 02 03 04 05 06 07 08 09 10 11; do
        printf 'package many\n' > "$WORK/bad/crowded/many/file$index.nv"
    done
    {
        echo 'package wiring'
        echo
        echo 'define class WiringLong {'
        for index in $(seq 1 199); do echo "    // line $index"; done
        echo '}'
    } > "$WORK/bad/server/wiring/long.nv"
    {
        echo 'package wiring'
        echo
        echo 'method wiringLongFunction() {'
        for index in $(seq 1 52); do echo '    var counter = 1'; done
        echo '}'
    } > "$WORK/bad/server/wiring/function.nv"
}

generate_bad_tree
bash "$ROOT/scripts/lsp_lint.sh" "$WORK/bad" > "$WORK/bad.actual" 2> /dev/null
compare bad "$WORK/bad.actual" "$HERE/bad.expected"

mkdir -p "$WORK/cycle/a" "$WORK/cycle/b" "$WORK/cycle/c"
printf 'a: b\nb: c\nc: a\n' > "$WORK/cycle/packages.txt"
: > "$WORK/cycle/contract_names.txt"
printf 'package a\n' > "$WORK/cycle/a/a.nv"
printf 'package b\n' > "$WORK/cycle/b/b.nv"
printf 'package c\n' > "$WORK/cycle/c/c.nv"
bash "$ROOT/scripts/lsp_lint.sh" "$WORK/cycle" > "$WORK/cycle.actual" 2> /dev/null
compare cycle "$WORK/cycle.actual" "$HERE/cycle.expected"

mkdir -p "$WORK/clean/base/alpha"
printf 'base/alpha: \n' > "$WORK/clean/packages.txt"
printf 'AlphaThing alpha\n' > "$WORK/clean/contract_names.txt"
printf 'package alpha\n\ndefine class AlphaThing {\n    construct() {\n    }\n}\n' > "$WORK/clean/base/alpha/alpha.nv"
bash "$ROOT/scripts/lsp_lint.sh" "$WORK/clean" > "$WORK/clean.actual" 2> /dev/null
compare clean "$WORK/clean.actual" "$HERE/clean.expected"
exit $failed
