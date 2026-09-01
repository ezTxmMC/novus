#!/usr/bin/env bash
# Golden tests: every test/cases/<name>.nv (or test/cases/<name>/main.nv) is
# compiled and run with novusc; stdout+stderr must match <name>.golden and
# the exit code must match <name>.rc (default 0). Optional <name>.args holds
# command line arguments. The examples are covered as well.
#
#   test/run_tests.sh            run everything
#   test/run_tests.sh classes    run tests whose name contains "classes"
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NOVUSC="${NOVUSC:-$ROOT/build/novusc}"
FILTER="${1:-}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
export NOVUS_CFLAGS="${NOVUS_CFLAGS:--O0}"
pass=0
failed=0

run_case() { # name, source, golden, rc-file, args-file, workdir
    local name="$1" source="$2" golden="$3" rcfile="$4" argsfile="$5" dir="$6"
    local expected_rc=0 args=()
    [[ -n "$FILTER" && "$name" != *"$FILTER"* ]] && return
    [ -f "$rcfile" ] && expected_rc="$(cat "$rcfile")"
    [ -f "$argsfile" ] && read -r -a args < "$argsfile"
    # run with a path relative to the case directory so messages stay stable
    local rel="$source"
    case "$source" in
        "$dir"/*) rel="${source#"$dir"/}" ;;
    esac
    # ${args[@]+...}: bash 3.2 (macOS) treats an empty array as unbound under set -u
    (cd "$dir" && "$NOVUSC" run "$rel" ${args[@]+"${args[@]}"}) > "$WORK/raw" 2>&1
    local rc=$?
    tr -d '\r' < "$WORK/raw" > "$WORK/actual"   # tolerate CRLF (Windows tools, git autocrlf)
    if [ "$rc" != "$expected_rc" ]; then
        echo "FAIL $name (exit code $rc, expected $expected_rc)"
        head -20 "$WORK/actual"
        failed=$((failed + 1))
    elif ! diff -u "$golden" "$WORK/actual" > "$WORK/diff"; then
        echo "FAIL $name (output differs)"
        head -40 "$WORK/diff"
        failed=$((failed + 1))
    else
        echo "ok   $name"
        pass=$((pass + 1))
    fi
}

for entry in "$ROOT"/test/cases/*; do
    if [ -d "$entry" ]; then
        name="$(basename "$entry")"
        run_case "$name" "$entry/main.nv" "$entry/main.golden" "$entry/main.rc" "$entry/main.args" "$entry"
    else
        case "$entry" in
            *.nv) ;;
            *) continue ;;
        esac
        name="$(basename "$entry" .nv)"
        base="${entry%.nv}"
        run_case "$name" "$entry" "$base.golden" "$base.rc" "$base.args" "$ROOT/test/cases"
    fi
done

# the language showcase
run_case "syntax" "$ROOT/test/syntax.nv" "$ROOT/test/syntax.golden" "/nonexistent" "/nonexistent" "$ROOT/test"

# examples (their golden files live next to them)
run_case "examples/mccloud" "$ROOT/examples/mccloud/main.nv" "$ROOT/examples/mccloud/main.golden" "$ROOT/examples/mccloud/main.rc" "/nonexistent" "$WORK"
run_case "examples/shapes" "$ROOT/examples/shapes/main.nv" "$ROOT/examples/shapes/main.golden" "/nonexistent" "/nonexistent" "$WORK"
echo "the cat and the dog" > "$WORK/words.txt"
echo "The cat sat" >> "$WORK/words.txt"
echo "words.txt" > "$WORK/wc.args"
run_case "examples/wordcount" "$ROOT/examples/wordcount/main.nv" "$ROOT/examples/wordcount/main.golden" "/nonexistent" "$WORK/wc.args" "$WORK"
if [[ -z "$FILTER" || "examples/todo" == *"$FILTER"* ]]; then
    rm -f "$WORK/todo.json"
    ( cd "$WORK" && for cmd in 'add "buy milk"' 'add "walk dog"' 'list' 'done 0' 'list' ''; do
        eval "\"$NOVUSC\" run \"$ROOT/examples/todo/main.nv\" $cmd"
    done ) 2>&1 | tr -d '\r' > "$WORK/todo.actual"
    if diff -u "$ROOT/examples/todo/main.golden" "$WORK/todo.actual" > "$WORK/diff"; then
        echo "ok   examples/todo"; pass=$((pass + 1))
    else
        echo "FAIL examples/todo"; head -40 "$WORK/diff"; failed=$((failed + 1))
    fi
fi

echo "$pass passed, $failed failed"
[ "$failed" = 0 ]
