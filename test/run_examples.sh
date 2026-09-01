#!/usr/bin/env bash
# Runs every example under examples/NN-*/ and compares its output with the
# .golden file next to it (optional .args for arguments, .rc for the exit code).
#
#   test/run_examples.sh              run all examples
#   test/run_examples.sh strings      only examples whose path contains "strings"
#   test/run_examples.sh --update     regenerate the golden files
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NOVUSC="${NOVUSC:-$ROOT/build/novusc}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
export NOVUS_CFLAGS="${NOVUS_CFLAGS:--O0}"
UPDATE=0
FILTER=""
for arg in "$@"; do
    case "$arg" in
        --update) UPDATE=1 ;;
        *) FILTER="$arg" ;;
    esac
done

run_one() { # invoked per example through xargs
    local file="$1" root="$2" novusc="$3" update="$4"
    local base="${file%.nv}" name="${file#"$root"/examples/}"
    local dir args=() expected_rc=0 rc
    dir="$(dirname "$file")"
    [ -f "$base.args" ] && read -r -a args < "$base.args"
    [ -f "$base.rc" ] && expected_rc="$(cat "$base.rc")"
    local out
    out="$(mktemp)"
    (cd "$dir" && "$novusc" run "$(basename "$file")" ${args[@]+"${args[@]}"}) > "$out" 2>&1
    rc=$?
    tr -d '\r' < "$out" > "$out.clean" && mv "$out.clean" "$out"
    if [ "$update" = 1 ]; then
        mv "$out" "$base.golden"
        [ "$rc" != 0 ] && echo "$rc" > "$base.rc"
        echo "updated $name (exit $rc)"
        return 0
    fi
    if [ "$rc" != "$expected_rc" ]; then
        echo "FAIL $name (exit code $rc, expected $expected_rc)"
        head -5 "$out"
        rm -f "$out"
        return 1
    fi
    if ! diff -u "$base.golden" "$out" > "$out.diff" 2>&1; then
        echo "FAIL $name (output differs)"
        head -12 "$out.diff"
        rm -f "$out" "$out.diff"
        return 1
    fi
    rm -f "$out" "$out.diff"
    return 0
}
export -f run_one

mapfile -t files < <(find "$ROOT/examples" -mindepth 2 -name '*.nv' -path '*/[0-9][0-9]-*' | sort)
if [ -n "$FILTER" ]; then
    mapfile -t files < <(printf '%s\n' "${files[@]}" | grep -- "$FILTER")
fi
total=${#files[@]}
[ "$total" = 0 ] && { echo "no examples matched"; exit 1; }

printf '%s\n' "${files[@]}" | xargs -P "$JOBS" -I{} bash -c 'run_one "$@"' _ {} "$ROOT" "$NOVUSC" "$UPDATE" > "$ROOT/.examples.log" 2>&1
status=$?
if [ "$UPDATE" = 1 ]; then
    echo "$total examples updated"
    rm -f "$ROOT/.examples.log"
    exit 0
fi
failed=$(grep -c '^FAIL ' "$ROOT/.examples.log" || true)
[ -z "$failed" ] && failed=0
if [ "$failed" != 0 ]; then
    cat "$ROOT/.examples.log"
fi
rm -f "$ROOT/.examples.log"
echo "$((total - failed))/$total examples ok"
[ "$failed" = 0 ] && [ "$status" = 0 ]
