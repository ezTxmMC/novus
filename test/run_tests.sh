#!/usr/bin/env bash
# Golden tests: every test/cases/<name>.nv (or test/cases/<name>/main.nv) is
# compiled and run with novusc; stdout+stderr must match <name>.golden and
# the exit code must match <name>.rc (default 0). Optional <name>.args holds
# command line arguments, optional <name>.stdin holds what the program reads
# from standard input (without it stdin is empty: /dev/null, which also keeps
# a terminal away from the timeout guard); an input too big to
# keep in the repository is written by <name>.stdin.sh instead. An (empty)
# <name>.stdin.open marker gives the program a stdin that stays open and
# never delivers anything, for a program that must end while a thread is
# blocked reading it. Every case is stopped (and fails) after CASE_SECONDS, or
# after the seconds in an optional <name>.timeout, so a regression that hangs
# a program or the compiler cannot hang the suite; the guard is `timeout`,
# `gtimeout` or perl, and without any of them a warning says there is none. An
# optional <name>.needs_mb holds the free memory (MiB) the case needs: it is
# skipped on a machine known to have less.
# The examples are covered as well, and so are the C tests of the runtime in
# test/runtime/.
#
#   test/run_tests.sh            run everything
#   test/run_tests.sh classes    run tests whose name contains "classes"
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NOVUSC="${NOVUSC:-$ROOT/build/novusc}"
export NOVUSC # for the cases that start the compiler themselves
FILTER="${1:-}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
export NOVUS_CFLAGS="${NOVUS_CFLAGS:--O0}"
# a cache of its own (see run_cache.nv for the tests of the cache itself)
export NOVUS_CACHE="${NOVUS_CACHE:-$WORK/cache}"
CASE_SECONDS="${CASE_SECONDS:-300}"
pass=0
failed=0
skipped=0

# Runs a command and stops it after `seconds`: the exit code is then not 0
# (124 with timeout, a signal's 128+n with perl).
with_timeout() { # seconds, command...
    local seconds="$1"
    shift
    if command -v timeout > /dev/null; then
        timeout "$seconds" "$@"
    elif command -v gtimeout > /dev/null; then
        gtimeout "$seconds" "$@"
    elif command -v perl > /dev/null; then
        perl -e 'alarm shift; exec @ARGV or die "exec: $!"' "$seconds" "$@"
    else
        "$@"
    fi
}

if ! command -v timeout > /dev/null && ! command -v gtimeout > /dev/null && ! command -v perl > /dev/null; then
    echo "warning: no timeout, gtimeout or perl: a hanging case cannot be stopped" >&2
fi

# The memory that is free for new allocations in MiB, empty when unknown.
available_mb() {
    if [ -r /proc/meminfo ]; then
        awk '/^MemAvailable:/ { print int($2 / 1024) }' /proc/meminfo
        return
    fi
    if command -v sysctl > /dev/null; then
        local bytes
        bytes="$(sysctl -n hw.memsize 2> /dev/null)" && echo $((bytes / 1048576))
    fi
}

# The stdin file of a case: <base>.stdin, or the output of <base>.stdin.sh,
# a named pipe nobody writes to for <base>.stdin.open, or a path that does
# not exist (the case then inherits stdin).
stdin_for() { # base
    local base="$1" generated fifo
    if [ -f "$base.stdin.open" ]; then
        fifo="$WORK/$(basename "$base").open"
        mkfifo "$fifo"
        echo "$fifo"
        return
    fi
    if [ -f "$base.stdin" ]; then
        echo "$base.stdin"
        return
    fi
    if [ -f "$base.stdin.sh" ]; then
        generated="$WORK/$(basename "$base").stdin"
        bash "$base.stdin.sh" > "$generated"
        echo "$generated"
        return
    fi
    echo "/nonexistent"
}

# Runs a case whose stdin never delivers anything and never ends: this shell
# holds the write end of the pipe (descriptor 7) until the program is done.
run_with_open_stdin() { # dir, fifo, source, seconds, args...
    local dir="$1" fifo="$2" source="$3" seconds="$4"
    shift 4
    exec 7<>"$fifo"
    (cd "$dir" && with_timeout "$seconds" "$NOVUSC" run "$source" "$@") < "$fifo"
    local rc=$?
    exec 7>&-
    return $rc
}

# Whether the case needs more memory than is known to be free.
lacks_memory() { # needs_mb file
    local needed available
    [ -f "$1" ] || return 1
    needed="$(cat "$1")"
    available="$(available_mb)"
    [ -n "$available" ] && [ "$available" -lt "$needed" ]
}

# Cases marked with <name>.posix_only drive a POSIX shell (VAR=value prefixes, sh, pwd, links).
on_windows() {
    case "$(uname -s 2>/dev/null)" in MINGW* | MSYS* | CYGWIN*) return 0 ;; esac
    return 1
}

run_case() { # name, source, golden, rc-file, args-file, stdin-file, workdir
    local name="$1" source="$2" golden="$3" rcfile="$4" argsfile="$5" stdinfile="$6" dir="$7"
    local expected_rc=0 args=() seconds="$CASE_SECONDS" base="${source%.nv}"
    [[ -n "$FILTER" && "$name" != *"$FILTER"* ]] && return
    [ -f "$base.timeout" ] && seconds="$(cat "$base.timeout")"
    if lacks_memory "$base.needs_mb"; then
        echo "skip $name (needs $(cat "$base.needs_mb") MiB of free memory)"
        skipped=$((skipped + 1))
        return
    fi
    if [ -f "$base.posix_only" ] && on_windows; then
        echo "skip $name (needs a POSIX shell)"
        skipped=$((skipped + 1))
        return
    fi
    [ -f "$rcfile" ] && expected_rc="$(cat "$rcfile")"
    [ -f "$argsfile" ] && read -r -a args < "$argsfile"
    # run with a path relative to the case directory so messages stay stable
    local rel="$source"
    case "$source" in
        "$dir"/*) rel="${source#"$dir"/}" ;;
    esac
    # ${args[@]+...}: bash 3.2 (macOS) treats an empty array as unbound under set -u
    if [ -p "$stdinfile" ]; then
        run_with_open_stdin "$dir" "$stdinfile" "$rel" "$seconds" ${args[@]+"${args[@]}"} > "$WORK/raw" 2>&1
    elif [ -f "$stdinfile" ]; then
        (cd "$dir" && with_timeout "$seconds" "$NOVUSC" run "$rel" ${args[@]+"${args[@]}"}) < "$stdinfile" > "$WORK/raw" 2>&1
    else
        (cd "$dir" && with_timeout "$seconds" "$NOVUSC" run "$rel" ${args[@]+"${args[@]}"}) < /dev/null > "$WORK/raw" 2>&1
    fi
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
        run_case "$name" "$entry/main.nv" "$entry/main.golden" "$entry/main.rc" "$entry/main.args" "$(stdin_for "$entry/main")" "$entry"
    else
        case "$entry" in
            *.nv) ;;
            *) continue ;;
        esac
        name="$(basename "$entry" .nv)"
        base="${entry%.nv}"
        run_case "$name" "$entry" "$base.golden" "$base.rc" "$base.args" "$(stdin_for "$base")" "$ROOT/test/cases"
    fi
done

# Tests of the C runtime itself: test/runtime/<name>.c is compiled against
# runtime/novus_rt.h and run; what it prints must match <name>.golden. They
# reach what no Novus program can - the bytes behind a string, the collection
# counter, counters inside the runtime.
run_runtime_case() { # source
    local source="$1" name binary
    name="$(basename "$source" .c)"
    [[ -n "$FILTER" && "runtime/$name" != *"$FILTER"* ]] && return
    binary="$WORK/runtime-$name"
    if ! "${NOVUS_CC:-cc}" -O0 -w "$source" -o "$binary" -lm -lpthread > "$WORK/raw" 2>&1; then
        echo "FAIL runtime/$name (does not compile)"
        head -20 "$WORK/raw"
        failed=$((failed + 1))
    elif ! with_timeout "$CASE_SECONDS" "$binary" > "$WORK/raw" 2>&1; then
        echo "FAIL runtime/$name (exit code $?)"
        head -20 "$WORK/raw"
        failed=$((failed + 1))
    elif ! diff -u "${source%.c}.golden" "$WORK/raw" > "$WORK/diff"; then
        echo "FAIL runtime/$name (output differs)"
        head -40 "$WORK/diff"
        failed=$((failed + 1))
    else
        echo "ok   runtime/$name"
        pass=$((pass + 1))
    fi
}
case "$(uname -s 2> /dev/null)" in
    MINGW* | MSYS* | CYGWIN*) ;; # the tests link pthreads
    *) for source in "$ROOT"/test/runtime/*.c; do run_runtime_case "$source"; done ;;
esac

# the language showcase
run_case "syntax" "$ROOT/test/syntax.nv" "$ROOT/test/syntax.golden" "/nonexistent" "/nonexistent" "/nonexistent" "$ROOT/test"

# examples (their golden files live next to them)
run_case "examples/mccloud" "$ROOT/examples/mccloud/main.nv" "$ROOT/examples/mccloud/main.golden" "$ROOT/examples/mccloud/main.rc" "/nonexistent" "/nonexistent" "$WORK"
run_case "examples/shapes" "$ROOT/examples/shapes/main.nv" "$ROOT/examples/shapes/main.golden" "/nonexistent" "/nonexistent" "/nonexistent" "$WORK"
echo "the cat and the dog" > "$WORK/words.txt"
echo "The cat sat" >> "$WORK/words.txt"
echo "words.txt" > "$WORK/wc.args"
run_case "examples/wordcount" "$ROOT/examples/wordcount/main.nv" "$ROOT/examples/wordcount/main.golden" "/nonexistent" "$WORK/wc.args" "/nonexistent" "$WORK"
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

# the web example serves forever: it only has to compile
if [[ -z "$FILTER" || "examples/web" == *"$FILTER"* ]]; then
    if (cd "$ROOT/examples/web" && "$NOVUSC" check main.nv) > "$WORK/web.out" 2>&1; then
        echo "ok   examples/web"; pass=$((pass + 1))
    else
        echo "FAIL examples/web"; head -20 "$WORK/web.out"; failed=$((failed + 1))
    fi
fi

echo "$pass passed, $failed failed, $skipped skipped"
[ "$failed" = 0 ]
