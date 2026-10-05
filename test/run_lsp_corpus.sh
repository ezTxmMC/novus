#!/usr/bin/env bash
# Corpus and fuzz checks of the novus-lsp syntax layer (lsp/DESIGN.md 9.5 items 4 and 5).
#
#   test/run_lsp_corpus.sh            build the two tools, run the checks, compare the counters with the golden
#   test/run_lsp_corpus.sh --update   rewrite test/lsp/corpus/summary.golden from the counters of this run
#   test/run_lsp_corpus.sh --no-fuzz  skip the fuzz step (it is the slow one); not combinable with --update
#
# Steps (each prints its problems and `summary <key> <n>` lines):
#   a. syntax_tool         every .nv, .nvh and project.nv file: zero own syntax issues, except the files of
#                          test/lsp/corpus/known_bad.txt, which must yield at least one
#   b. syntax_tool compare every single-file program (a `method main`) against `build/novusc check`: own syntax and own semantic
#                          findings on the accepted programs (every user file of their closure), the semantic fixtures need one
#   e. diagnose_tool       the own report (`diagnosticReportLines`) of every .nv and .nvh file runs to the end (counter diagnose.files)
#   c. fuzz_tool           deterministic mutations of every corpus file; the invariants of the syntax layer must hold
#   d. selftest.sh         the tools and the golden comparison against inputs that must fail (regression tests of this script)
# The number of files that the tools visited is compared with an independent `find` (counters syntax.skipped and
# fuzz.skipped, both exactly 0), and the counters are compared with test/lsp/corpus/summary.golden: a line `== key n` must match exactly, a line
# `>= key n` (a count that grows with the repository) is a floor, so a silent drop is noticed. A key that is missing on
# either side fails or is malformed. Environment: NOVUSC (default build/novusc), CORPUS_FUZZ_TIMEOUT (seconds, default 240: the
# hard stop of the fuzz step; the tool itself stops at FUZZ_TOOL_CAP_SECONDS, which this script sets to 150 unless the
# caller sets it (the repository grew from 772 to over 1100 files in Wave 4, and the tool default of 60 s no longer covers it), and gives a file at most
# FUZZ_TOOL_FILE_BUDGET_MILLIS, default 10000).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NOVUSC="${NOVUSC:-$ROOT/build/novusc}"
case "$NOVUSC" in /*) ;; *) NOVUSC="$PWD/$NOVUSC" ;; esac # the comparison runs the compiler from the repository root
CORPUS="$ROOT/test/lsp/corpus"
GOLDEN="$CORPUS/summary.golden"
TOOLS="$ROOT/test/lsp/tools"
FUZZ_TIMEOUT="${CORPUS_FUZZ_TIMEOUT:-240}"
export FUZZ_TOOL_CAP_SECONDS="${FUZZ_TOOL_CAP_SECONDS:-150}"
UPDATE=0
RUN_FUZZ=1
for argument in "$@"; do
    case "$argument" in
        --update) UPDATE=1 ;;
        --no-fuzz) RUN_FUZZ=0 ;;
        *) echo "usage: test/run_lsp_corpus.sh [--update] [--no-fuzz]" >&2; exit 2 ;;
    esac
done
if [ "$UPDATE" -eq 1 ] && [ "$RUN_FUZZ" -eq 0 ]; then
    # The golden of a run without fuzz counters would lose the fuzz section and its `== fuzz.violations 0` invariants.
    echo "corpus: --update needs the fuzz step, do not combine it with --no-fuzz" >&2
    exit 2
fi
. "$CORPUS/corpus_lib.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
FAILED=0

build_tool() {
    "$NOVUSC" build "$TOOLS/$1/main.nv" -o "$WORK/$1" > "$WORK/$1.build" 2>&1 && return 0
    echo "corpus: building $1 failed" >&2
    cat "$WORK/$1.build" >&2
    return 1
}

# run_step <title> <command...>: the output is shown without the summary lines, which are collected.
run_step() {
    local title="$1"
    shift
    local output="$WORK/step.out"
    local code=0
    "$@" > "$output" 2>&1 || code=$?
    STEP_CODE="$code"
    grep '^summary ' "$output" >> "$WORK/summary.actual"
    grep -v '^summary ' "$output"
    if [ "$code" -ne 0 ]; then
        echo "corpus: $title failed (exit code $code)"
        FAILED=1
    fi
}

# The fuzz tool names each file on stderr before it runs its cases; that is kept apart so a hang can be located.
run_fuzz_tool() {
    # macOS has no timeout(1) unless coreutils is installed; the cap is then left to the CI job.
    if command -v timeout > /dev/null 2>&1; then
        timeout "$FUZZ_TIMEOUT" "$WORK/fuzz_tool" "$ROOT" 2> "$WORK/fuzz.progress"
        return
    fi
    "$WORK/fuzz_tool" "$ROOT" 2> "$WORK/fuzz.progress"
}

run_fuzz_step() {
    run_step "fuzz (c)" run_fuzz_tool
    [ "$STEP_CODE" -eq 0 ] && return
    echo "corpus: the fuzz tool was working on: $(tail -n 1 "$WORK/fuzz.progress")"
}

: > "$WORK/summary.actual"
build_tool syntax_tool || exit 1
build_tool diagnose_tool || exit 1
if [ "$RUN_FUZZ" -eq 1 ]; then
    build_tool fuzz_tool || exit 1
fi

EXPECTED_FILES="$(corpus_expected_files "$ROOT")"
run_step "syntax corpus (a)" "$WORK/syntax_tool" "$ROOT" "$CORPUS/known_bad.txt"
corpus_add_skipped "$WORK/summary.actual" syntax.skipped "$EXPECTED_FILES" '^syntax[.]files[.]'
run_step "novusc comparison (b)" "$WORK/syntax_tool" compare "$ROOT" "$NOVUSC" "$CORPUS/different_line.txt" "$CORPUS/semantic_fixtures.txt"
run_step "own diagnostics report (e)" "$WORK/diagnose_tool" corpus "$ROOT"
corpus_add_skipped "$WORK/summary.actual" diagnose.skipped "$EXPECTED_FILES" '^diagnose[.]files$'
if [ "$RUN_FUZZ" -eq 1 ]; then
    run_fuzz_step
    corpus_add_skipped "$WORK/summary.actual" fuzz.skipped "$EXPECTED_FILES" '^fuzz[.]files$'
fi
if ! TOOLS_DIR="$WORK" NOVUSC="$NOVUSC" "$CORPUS/selftest.sh" "$ROOT" "$WORK/syntax_tool" "$WORK/fuzz_tool"; then
    echo "corpus: selftest (d) failed"
    FAILED=1
fi

if [ "$UPDATE" -eq 1 ]; then
    [ "$FAILED" -eq 0 ] || { echo "corpus: not updating the golden while a step fails"; exit 1; }
    corpus_write_golden "$GOLDEN" "$WORK/summary.actual"
    echo "corpus: wrote $GOLDEN"
    exit 0
fi
if [ "$RUN_FUZZ" -eq 0 ]; then
    sed -i.bak '/^summary fuzz\./d' "$WORK/summary.actual"
    grep -v ' fuzz\.' "$GOLDEN" > "$WORK/golden.partial" 2>/dev/null
    GOLDEN="$WORK/golden.partial"
fi
corpus_compare_golden "$GOLDEN" "$WORK/summary.actual" || FAILED=1
if [ "$FAILED" -ne 0 ]; then
    echo "corpus: FAILED"
    exit 1
fi
echo "corpus: ok ($(grep -c . "$WORK/summary.actual") counters)"
