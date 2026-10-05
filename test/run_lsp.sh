#!/usr/bin/env bash
# Protocol tests of novus-lsp (DESIGN 9.5-2). Every directory test/lsp/<name>/ is a scenario:
#
#   files/            the workspace; copied to a temp dir (optional)
#   session.jsonl     one JSON-RPC message per line; the driver frames every line with Content-Length
#                     (a line need not be valid JSON, it is sent as it is)
#   input.sh          instead of session.jsonl: the script writes the raw stdin bytes itself (wrong,
#                     oversized or truncated frames, a frame in several writes with sleep in between);
#                     the driver does not frame. It sources test/lsp/lib.sh for the helpers
#   expected.jsonl    what the server wrote to stdout, one JSON message per line
#   expected.rc       the expected exit code of the server (default 0)
#   limit             the timeout of this scenario in seconds instead of LSP_TIMEOUT (a server that must end
#                     by itself while its stdin stays open needs a short one)
#   pending           a known problem outside the server's own code: the file says why. The scenario is
#                     reported as "pend" and does not fail the run; when it passes, "ok" says to remove the file
#   keep-capabilities by default result.capabilities of every initialize response is replaced by the
#                     string "@CAPABILITIES@", so no scenario depends on which handlers a later wave
#                     registers; only the scenario `capabilities` keeps the real map
#   env               extra environment of the SERVER process (not of input.sh), one NAME=value per line;
#                     blank lines and lines that start with # are skipped; the placeholders below are
#                     replaced in the values. Typical content: NOVUS_LSP_CHECK=1 (run the fake novusc of
#                     the scenario) or NOVUS_DEPS=@ROOT@/deps (a dependency cache inside the workspace)
#
# The placeholders @ROOT@, @ROOTURI@ (the temp workspace and its file:// URI) and @TMP@ (the temporary
# directory of this scenario: the server runs with TMPDIR set to it, so the std sources that it
# materialises and the shadow trees of its checks stay inside the run) are replaced in session.jsonl,
# in the values of `env` and in input.sh output (input.sh also finds the directory in $TMPROOT), and put
# back in the server output before it is compared. The fingerprint in the name of the std source
# directory (novus-lsp-std-<modules>-<bytes>) is replaced by @FINGERPRINT@ in the output, because it
# changes with every compiler build that changes the embedded std.
#
#   test/run_lsp.sh                 run every scenario
#   test/run_lsp.sh robust          run the scenarios whose name contains "robust"
#   test/run_lsp.sh cancel          a filter that is the name of a scenario runs that scenario only
#   test/run_lsp.sh --update [f]    rewrite expected.jsonl for review (never run it blindly)
#
# NOVUS_LSP_BIN selects another server binary and skips the build; LSP_TIMEOUT (seconds, default 60)
# limits one scenario.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NOVUSC="${NOVUSC:-$ROOT/build/novusc}"
BIN="${NOVUS_LSP_BIN:-$ROOT/build/novus-lsp}"
LIMIT="${LSP_TIMEOUT:-60}"
LIB="$ROOT/test/lsp/lib.sh"
UPDATE=0
if [ "${1:-}" = "--update" ]; then
    UPDATE=1
    shift
fi
FILTER="${1:-}"
# A filter that names a scenario directory selects exactly that scenario (so `--update cancel` does not
# rewrite robust-cancel); any other filter is a substring match.
EXACT=0
if [ -n "$FILTER" ] && { [ -f "$ROOT/test/lsp/$FILTER/session.jsonl" ] || [ -f "$ROOT/test/lsp/$FILTER/input.sh" ]; }; then
    EXACT=1
fi
export NOVUS_LSP_TEST=1
WORK="$(mktemp -d)"
[ -n "${KEEP_WORK:-}" ] || trap 'rm -rf "$WORK"' EXIT
pass=0
failed=0
pending=0

build_server() {
    [ -n "${NOVUS_LSP_BIN:-}" ] && return 0
    if [ -x "$BIN" ] && [ -z "$(find "$ROOT/lsp" "$ROOT/compiler/std" -name '*.nv' -newer "$BIN" -print -quit 2>/dev/null)" ]; then
        return 0
    fi
    echo "building $BIN"
    mkdir -p "$(dirname "$BIN")"
    "$NOVUSC" build "$ROOT/lsp/main.nv" -o "$BIN"
}

# run_limited <seconds> <command...>: a hung server must not hang the run; exit code 124 means timed out.
run_limited() {
    local seconds="$1"
    shift
    if command -v timeout > /dev/null 2>&1; then
        timeout "$seconds" "$@"
        return $?
    fi
    "$@"
}

# selected <name>: whether the filter lets this scenario run.
selected() {
    [ -z "$FILTER" ] && return 0
    if [ "$EXACT" = 1 ]; then
        [ "$1" = "$FILTER" ]
        return $?
    fi
    [[ "$1" == *"$FILTER"* ]]
}

# frame_lines <file> <root> <rooturi> <tmp>: every line of the session as one Content-Length frame.
frame_lines() {
    local line length
    while IFS= read -r line || [ -n "$line" ]; do
        line="${line//@ROOTURI@/$3}"
        line="${line//@ROOT@/$2}"
        line="${line//@TMP@/$4}"
        length="$(printf '%s' "$line" | LC_ALL=C wc -c | tr -d ' ')"
        printf 'Content-Length: %s\r\n\r\n%s' "$length" "$line"
    done < "$1"
}

# deframe <file>: the body of every frame of the server output, one per line.
deframe() {
    local header length=0
    exec 3< "$1"
    while IFS= read -r -u 3 header; do
        header="${header%$'\r'}"
        case "$header" in
            [Cc]ontent-[Ll]ength:*) length="${header#*:}"; length="${length// /}" ;;
            "") LC_ALL=C head -c "$length" <&3; echo ;;
        esac
    done
    exec 3<&-
}

# capabilities <file>: replaces result.capabilities (a map) of initialize responses by "@CAPABILITIES@".
# The server writes sorted keys, so "capabilities" is the first key of the result.
hide_capabilities() {
    LC_ALL=C awk '
    {
        marker = "\"result\":{\"capabilities\":{"
        at = index($0, marker)
        if (at == 0) { print; next }
        start = at + length("\"result\":{\"capabilities\":")
        depth = 0; inString = 0; escaped = 0; stop = 0
        for (pos = start; pos <= length($0); pos++) {
            c = substr($0, pos, 1)
            if (inString) {
                if (escaped) { escaped = 0 }
                else if (c == "\\") { escaped = 1 }
                else if (c == "\"") { inString = 0 }
                continue
            }
            if (c == "\"") { inString = 1 }
            else if (c == "{") { depth++ }
            else if (c == "}") { depth--; if (depth == 0) { stop = pos; break } }
        }
        if (stop == 0) { print; next }
        print substr($0, 1, start - 1) "\"@CAPABILITIES@\"" substr($0, stop + 1)
    }' "$1"
}

# restore_placeholders <file> <root> <rooturi> <tmp>
restore_placeholders() {
    local line
    while IFS= read -r line || [ -n "$line" ]; do
        line="${line//"$3"/@ROOTURI@}"
        line="${line//"$2"/@ROOT@}"
        line="${line//"$4"/@TMP@}"
        printf '%s\n' "$line"
    done < "$1" | sed -E 's/novus-lsp-std-[0-9]+-[0-9]+/novus-lsp-std-@FINGERPRINT@/g'
}

# read_env <dir> <root> <rooturi> <tmp>: fills the array ENV_ARGS from the optional file `env` of the scenario.
read_env() {
    local line
    ENV_ARGS=()
    [ -f "$1/env" ] || return 0
    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in
            "" | "#"*) continue ;;
        esac
        if ! [[ "$line" =~ ^[A-Za-z_][A-Za-z0-9_]*= ]]; then
            echo "FAIL $(basename "$1"): the line '$line' of its env file is no NAME=value" >&2
            return 1
        fi
        line="${line//@ROOTURI@/$3}"
        line="${line//@ROOT@/$2}"
        line="${line//@TMP@/$4}"
        ENV_ARGS+=("$line")
    done < "$1/env"
}

run_scenario() { # dir
    local dir="$1" name rc expected_rc=0 workspace uri tmp limit="$LIMIT"
    name="$(basename "$dir")"
    [ -f "$dir/session.jsonl" ] || [ -f "$dir/input.sh" ] || return 0
    selected "$name" || return 0
    workspace="$WORK/$name/workspace"
    tmp="$WORK/$name/tmp"
    mkdir -p "$workspace" "$tmp"
    [ -d "$dir/files" ] && cp -R "$dir/files/." "$workspace/"
    uri="file://$workspace"
    [ -f "$dir/limit" ] && limit="$(tr -d ' \n' < "$dir/limit")"
    [ -f "$dir/expected.rc" ] && expected_rc="$(tr -d ' \n' < "$dir/expected.rc")"
    read_env "$dir" "$workspace" "$uri" "$tmp" || { failed=$((failed + 1)); return 0; }
    start_server "$dir" "$name" "$workspace" "$uri" "$tmp" "$limit"
    rc="$(cat "$WORK/$name/rc")"
    deframe "$WORK/$name/raw" > "$WORK/$name/lines"
    if [ -f "$dir/keep-capabilities" ]; then
        cp "$WORK/$name/lines" "$WORK/$name/shown"
    else
        hide_capabilities "$WORK/$name/lines" > "$WORK/$name/shown"
    fi
    restore_placeholders "$WORK/$name/shown" "$workspace" "$uri" "$tmp" > "$WORK/$name/actual"
    check_scenario "$dir" "$name" "$rc" "$expected_rc"
}

# start_server <dir> <name> <workspace> <uri> <tmp> <limit>: runs the server on the session of the scenario.
# The environment of the server is ENV_ARGS (the `env` file) on top of TMPDIR and NOVUS_LSP_TEST.
start_server() {
    local dir="$1" name="$2" workspace="$3" uri="$4" tmp="$5" limit="$6"
    if [ -f "$dir/input.sh" ]; then
        (cd "$dir" && ROOT="$workspace" ROOTURI="$uri" TMPROOT="$tmp" LSP_LIB="$LIB" bash input.sh 2> "$WORK/$name/input.err") \
            | (cd "$workspace" && TMPDIR="$tmp" run_limited "$limit" env ${ENV_ARGS[@]+"${ENV_ARGS[@]}"} "$BIN" > "$WORK/$name/raw" 2> "$WORK/$name/stderr"; echo $? > "$WORK/$name/rc")
        return 0
    fi
    frame_lines "$dir/session.jsonl" "$workspace" "$uri" "$tmp" > "$WORK/$name/input"
    (cd "$workspace" && TMPDIR="$tmp" run_limited "$limit" env ${ENV_ARGS[@]+"${ENV_ARGS[@]}"} "$BIN" < "$WORK/$name/input" > "$WORK/$name/raw" 2> "$WORK/$name/stderr"; echo $? > "$WORK/$name/rc")
}

check_scenario() { # dir name rc expected_rc
    local dir="$1" name="$2" rc="$3" expected_rc="$4"
    if [ "$UPDATE" = 1 ]; then
        cp "$WORK/$name/actual" "$dir/expected.jsonl"
        echo "updated $name (exit code $rc)"
        return 0
    fi
    if scenario_differs "$dir" "$name" "$rc" "$expected_rc"; then
        report_failure "$dir" "$name"
        return 0
    fi
    if [ -f "$dir/pending" ]; then
        echo "ok   $name (it passes now: remove $dir/pending)"
    else
        echo "ok   $name"
    fi
    pass=$((pass + 1))
}

# scenario_differs <dir> <name> <rc> <expected_rc>: succeeds when the scenario failed; the reason is in $WORK/<name>/why.
scenario_differs() {
    local dir="$1" name="$2" rc="$3" expected_rc="$4"
    if [ "$rc" != "$expected_rc" ]; then
        { echo "exit code $rc, expected $expected_rc"; head -5 "$WORK/$name/stderr"; } > "$WORK/$name/why"
        return 0
    fi
    if ! diff -u "$dir/expected.jsonl" "$WORK/$name/actual" > "$WORK/$name/diff"; then
        { echo "output differs"; head -30 "$WORK/$name/diff"; } > "$WORK/$name/why"
        return 0
    fi
    return 1
}

report_failure() { # dir name
    local dir="$1" name="$2"
    if [ -f "$dir/pending" ]; then
        echo "pend $name ($(head -1 "$dir/pending"))"
        pending=$((pending + 1))
        return 0
    fi
    echo "FAIL $name ($(head -1 "$WORK/$name/why"))"
    tail -n +2 "$WORK/$name/why"
    failed=$((failed + 1))
}

# The lint has a test of its own (a tree that breaks every rule): it runs with the protocol scenarios.
run_lint_selftest() {
    [[ -n "$FILTER" && "lint-fixture" != *"$FILTER"* ]] && return 0
    local output
    output="$(bash "$ROOT/test/lsp/tools/lint_fixture/run.sh" 2>&1)"
    if [ $? = 0 ]; then
        echo "$output"
        pass=$((pass + $(echo "$output" | grep -c '^ok')))
        return 0
    fi
    echo "$output"
    failed=$((failed + 1))
}

build_server || { echo "FAIL: the server does not build"; exit 1; }
for dir in "$ROOT"/test/lsp/*/; do
    run_scenario "${dir%/}"
done
run_lint_selftest
echo "$pass passed, $failed failed, $pending pending"
[ "$failed" = 0 ]
