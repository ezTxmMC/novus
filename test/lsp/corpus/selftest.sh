#!/usr/bin/env bash
# Regression tests of the corpus and fuzz tools and of the golden comparison (called by test/run_lsp_corpus.sh, step d).
#   selftest.sh <repoRoot> <syntax_tool> <fuzz_tool>      the fuzz tool may be missing (--no-fuzz): its checks are skipped
# Every case builds a small input in a temporary directory that must make a tool or the comparison fail (or pass), and checks
# the exit code and a line of the output. Environment: NOVUSC (the real compiler, for the unterminated-block cases).
set -u
ROOT="$1"
SYNTAX_TOOL="$2"
FUZZ_TOOL="$3"
NOVUSC="${NOVUSC:-$ROOT/build/novusc}"
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/corpus_lib.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
FAILURES=0
OUTPUT=""
CODE=0

# run <command...>: OUTPUT and CODE of the command (stdout and stderr together).
run() {
    CODE=0
    OUTPUT="$("$@" 2>&1)" || CODE=$?
}

# expect <description> <exit code> [pattern]: the last run must have this exit code and, with a pattern, print a line that matches it.
expect() {
    if [ "$CODE" -ne "$2" ]; then
        echo "selftest: $1: exit code $CODE, expected $2"
        echo "$OUTPUT" | head -n 8
        FAILURES=$((FAILURES + 1))
        return
    fi
    if [ -n "${3:-}" ] && ! echo "$OUTPUT" | grep -q -- "$3"; then
        echo "selftest: $1: the output has no line matching '$3'"
        echo "$OUTPUT" | head -n 8
        FAILURES=$((FAILURES + 1))
    fi
}

empty_list() {
    printf '# no entries\n' > "$WORK/empty.txt"
}

write_fake_novusc() {
    printf '#!/bin/sh\n%s\n' "$2" > "$WORK/$1"
    chmod +x "$WORK/$1"
}

write_main_file() {
    mkdir -p "$(dirname "$1")"
    printf '%b' "$2" > "$1"
}

test_unterminated_is_accepted_anywhere() {
    local tree="$WORK/unterminated"
    write_main_file "$tree/block.nv" 'method main() {\n  println(1)\n\n'
    write_main_file "$tree/string.nv" 'method main() {\n  var text = "oops\n  println(text)\n}\n'
    run "$SYNTAX_TOOL" compare "$tree" "$NOVUSC" "$WORK/empty.txt" "$WORK/empty.txt"
    expect "an unclosed block or string is a match although the lines differ" 0 'summary compare.rejected-syntax-missed 0'
    expect "both programs are rejected as syntax errors" 0 'summary compare.rejected-syntax 2'
}

test_crashing_compiler_is_not_a_rejection() {
    local tree="$WORK/crash"
    write_main_file "$tree/a.nv" 'method main() {\n}\n'
    write_fake_novusc fake_exit3 'exit 3'
    write_fake_novusc fake_exit139 'exit 139'
    run "$SYNTAX_TOOL" compare "$tree" "$WORK/fake_exit3" "$WORK/empty.txt" "$WORK/empty.txt"
    expect "exit 3 without an error line" 1 'summary compare.failed-to-run 1'
    run "$SYNTAX_TOOL" compare "$tree" "$WORK/fake_exit139" "$WORK/empty.txt" "$WORK/empty.txt"
    expect "a signal exit" 1 'summary compare.failed-to-run 1'
}

test_missing_inputs_are_errors() {
    run "$SYNTAX_TOOL" report "$WORK/nonexistent.nv"
    expect "report of a missing file" 2 'is not a file'
    run "$SYNTAX_TOOL" "$WORK/nonexistent-root" "$WORK/empty.txt"
    expect "a missing root" 2 'is not a directory'
    run "$SYNTAX_TOOL" "$WORK" "$WORK/nonexistent-list.txt"
    expect "a missing known-bad list" 2 'is not a file'
    run "$SYNTAX_TOOL" compare "$WORK" "$NOVUSC" "$WORK/nonexistent-list.txt" "$WORK/empty.txt"
    expect "a missing different-line list" 2 'is not a file'
}

# A file above 2 MiB and a directory 70 levels deep are left out by the walker; the independent count must notice.
test_skipped_files_are_counted() {
    local tree="$WORK/skipped" deep="$WORK/skipped/d" level=1
    write_main_file "$tree/ok.nv" 'method main() {\n}\n'
    { printf '// '; head -c 2400000 /dev/zero | tr '\0' 'a'; printf '\n'; } > "$tree/big.nv"
    while [ "$level" -lt 70 ]; do deep="$deep/d"; level=$((level + 1)); done
    write_main_file "$deep/deep.nv" 'method main() {\n}\n'
    run "$SYNTAX_TOOL" "$tree" "$WORK/empty.txt"
    expect "the corpus run itself stays silent about skipped files" 0 'summary syntax.files.nv 1'
    echo "$OUTPUT" | grep '^summary ' > "$WORK/skipped.summary"
    corpus_add_skipped "$WORK/skipped.summary" syntax.skipped "$(corpus_expected_files "$tree")" '^syntax[.]files[.]'
    grep -q '^summary syntax.skipped 2$' "$WORK/skipped.summary" || { echo "selftest: the two skipped files were not counted"; cat "$WORK/skipped.summary"; FAILURES=$((FAILURES + 1)); }
}

# golden_case <description> <golden line> <expected result: 0 or 1>
golden_case() {
    printf 'summary syntax.unexpected 5\nsummary syntax.missing 9\n' > "$WORK/golden.summary"
    printf '%s\n' "$2" > "$WORK/golden.case"
    CODE=0
    OUTPUT="$(corpus_compare_golden "$WORK/golden.case" "$WORK/golden.summary" 2>&1)" || CODE=$?
    expect "$1" "$3"
}

test_golden_comparison() {
    golden_case "a valid golden passes" "== syntax.unexpected 5
== syntax.missing 9" 0
    golden_case "a floor that holds passes" ">= syntax.unexpected 4
== syntax.missing 9" 0
    golden_case "a non-numeric value fails" "== syntax.unexpected 5x
== syntax.missing 9" 1
    golden_case "an empty value fails" "== syntax.unexpected
== syntax.missing 9" 1
    golden_case "an unknown operator fails" "=== syntax.unexpected 5
== syntax.missing 9" 1
    golden_case "a comparison operator that is no contract operator fails" "<= syntax.unexpected 5
== syntax.missing 9" 1
    golden_case "a different value fails" "== syntax.unexpected 6
== syntax.missing 9" 1
    golden_case "a counter that is not in the golden fails" "== syntax.unexpected 5" 1
    golden_case "a counter twice fails" "== syntax.unexpected 5
== syntax.unexpected 5
== syntax.missing 9" 1
}

test_update_without_fuzz_is_refused() {
    local before after
    before="$(cksum < "$HERE/summary.golden")"
    run "$ROOT/test/run_lsp_corpus.sh" --update --no-fuzz
    after="$(cksum < "$HERE/summary.golden")"
    expect "--update --no-fuzz" 2 'do not combine'
    [ "$before" = "$after" ] || { echo "selftest: --update --no-fuzz changed the golden"; FAILURES=$((FAILURES + 1)); }
}

test_fuzz_tool() {
    local tree="$WORK/fuzz" nest="$WORK/fuzz-nest"
    write_main_file "$tree/crlf.nv" 'method main() {\r\n  println("h\xc3\xa4llo \xf0\x9f\x98\x80 \xe4\xb8\xad")\r\n}\r\n'
    write_main_file "$tree/second.nv" 'method main() {\n  println("a")\n  println("b")\n  println("c")\n}\n'
    write_main_file "$tree/third.nv" 'method main() {\n  println("a")\n  println("b")\n  println("c")\n}\n'
    run "$FUZZ_TOOL" "$WORK/nonexistent-root"
    expect "fuzz of a missing root" 2 'is not a directory'
    run "$FUZZ_TOOL" "$tree"
    expect "a CRLF file with multi-byte characters" 0 'summary fuzz.violations 0'
    expect "every file of the tree was fuzzed" 0 'summary fuzz.files 3'
    FUZZ_TOOL_CAP_SECONDS=0 run "$FUZZ_TOOL" "$tree"
    expect "the cap of the whole run" 1 'longer than the cap'
    write_main_file "$nest/nest.nv" "method main() {\n  var x = $(printf '(%.0s' $(seq 1 3000))1$(printf ')%.0s' $(seq 1 3000))\n}\n"
    FUZZ_TOOL_FILE_BUDGET_MILLIS=0 run "$FUZZ_TOOL" "$nest"
    expect "the budget of one file" 1 'nest.nv \[budget\]'
}

empty_list
test_unterminated_is_accepted_anywhere
test_crashing_compiler_is_not_a_rejection
test_missing_inputs_are_errors
test_skipped_files_are_counted
test_golden_comparison
test_update_without_fuzz_is_refused
[ -x "$FUZZ_TOOL" ] && test_fuzz_tool
if [ "$FAILURES" -ne 0 ]; then
    echo "selftest: $FAILURES checks failed"
    exit 1
fi
echo "selftest: ok"
