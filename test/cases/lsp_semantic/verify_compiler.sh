#!/usr/bin/env bash
# Checks the expectations of the rule fixtures against the real compiler: every fixture of fixtures.nv is written to
# disk by `main.nv dump <dir>` and run (or checked) with build/novusc. Not part of the golden run; run it by hand
# after a change of the fixtures or of the compiler: test/cases/lsp_semantic/verify_compiler.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
NOVUSC="${NOVUSC:-$ROOT/build/novusc}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
(cd "$HERE" && "$NOVUSC" run main.nv dump "$WORK") || { echo "dump failed"; exit 1; }
failed=0
for dir in "$WORK"/*/; do
    name="$(basename "$dir")"
    expected="$(cat "$dir/expected")"
    actual="$(cd "$dir" && "$NOVUSC" run main.nv 2>&1)"
    rc=$?
    case "$expected" in
        run:*)
            want="${expected#run:}"
            got="$(printf '%s' "$actual" | tr '\n' '|')"
            if [ "$rc" = 0 ] && [ "$got" = "$want" ]; then echo "ok   $name"; else echo "FAIL $name: got rc=$rc <$got>, expected <$want>"; failed=1; fi ;;
        error:*)
            want="${expected#error:}"
            if [ "$rc" != 0 ] && printf '%s' "$actual" | grep -qF "$want"; then echo "ok   $name"; else echo "FAIL $name: rc=$rc <$actual>, expected an error with <$want>"; failed=1; fi ;;
        *)
            if [ "$rc" = 0 ]; then echo "ok   $name"; else echo "FAIL $name: rc=$rc <$actual>"; failed=1; fi ;;
    esac
done
exit $failed
